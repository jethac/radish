#!/usr/bin/env python3
"""Export the neural core of a Kokoro iSTFTNet decoder (e.g. the 7M student,
oddadmix/Kokoro-7M-Distill, Apache-2.0) for ncnn, at the same boundary as the
Kokoro-82M Radish graph: inputs asr [1,C,T], F0 [1,2T], N [1,2T], style
[1,128] and the harmonic spectrum har [1,22,frames]; output the conv_post
result [1,22,frames] before exp/sin and the inverse STFT (those stay on CPU).

usage: export-student-core.py KOKORO_PATCHED_DIR CONFIG [WEIGHTS|--random] OUT_DIR [--T 168]
       export-student-core.py KOKORO_PATCHED_DIR paradee:PRESET[@SCRIPTS_DIR] [CKPT|--random] OUT_DIR
The paradee: form builds the narrow student decoder (e.g. nx25k2: AdaIN 256,
upsample_initial_channel 128, resblock kernels [3, 7], dim_in 512, style 128)
from Paradee's student_decoder.py and loads a training checkpoint ('model').
Writes OUT_DIR/core.pt (TorchScript for pnnx), inputs in*.f32, reference out0.f32.
"""
import argparse, json, sys
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('kokoro_dir', type=Path)
ap.add_argument('config')
ap.add_argument('weights')
ap.add_argument('out', type=Path)
ap.add_argument('--T', type=int, default=168)
ap.add_argument('--seed', type=int, default=0)
a = ap.parse_args()
sys.path.insert(0, str(a.kokoro_dir))
import types
# Import kokoro.istftnet without kokoro/__init__ (which pulls in the text side,
# transformers and huggingface_hub): the decoder needs none of it.
_pkg = types.ModuleType('kokoro')
_pkg.__path__ = [str(a.kokoro_dir / 'kokoro')]
sys.modules['kokoro'] = _pkg
import torch
import torch.nn.functional as F
from torch.nn.utils import parametrize
from kokoro.istftnet import Decoder

if str(a.config).startswith('paradee:'):
    preset, _, scripts = str(a.config)[8:].partition('@')
    sys.path.insert(0, scripts or '/home/jethac/Projects/lenovo-8/streams/tts-distil/downloads/paradee/src/training/scripts')
    import student_decoder as sd_mod
    # Narrow presets as in tts-distil's narrow.py: (AdaIN width, upsample_initial_channel, kernels)
    for k, (w, u) in {'x20': (192, 96), 'x25': (256, 128), 'x30': (320, 160), 'x35': (352, 176)}.items():
        sd_mod.DEC_PRESETS.setdefault(f'n{k}k2', (w, u, [3, 7]))
    dec = sd_mod.StudentDecoder(preset, dim_in=512, style_dim=128)
    del dec.style
    cfg = {'hidden_dim': 512, 'style_dim': 128}
    if a.weights != '--random':
        sd = torch.load(a.weights, map_location='cpu', weights_only=True)['model']
        missing, unexpected = dec.load_state_dict(sd, strict=False)
        print('missing', len(missing), 'unexpected', len(unexpected))
    from torch.nn.utils import remove_weight_norm
    for m in dec.modules():
        try:
            remove_weight_norm(m)
        except Exception:
            pass
else:
    cfg = json.loads(Path(a.config).read_text())
    dec = Decoder(dim_in=cfg['hidden_dim'], style_dim=cfg['style_dim'], dim_out=cfg['n_mels'], **cfg['istftnet'])
    if a.weights != '--random':
        sd = torch.load(a.weights, map_location='cpu', weights_only=True)['decoder']
        sd = {k[7:] if k.startswith('module.') else k: v for k, v in sd.items()}
        missing, unexpected = dec.load_state_dict(sd, strict=False)
        print('missing', len(missing), 'unexpected', len(unexpected))
dec.eval()
for m in dec.modules():
    if parametrize.is_parametrized(m):
        for name in list(m.parametrizations.keys()):
            parametrize.remove_parametrizations(m, name, leave_parametrized=True)


class Norm2d(torch.nn.Module):
    """InstanceNorm1d as a height-1 InstanceNorm2d: pnnx maps it to ncnn
    InstanceNorm with its affine weights (InstanceNorm1d stays unconverted)."""
    def __init__(self, n):
        super().__init__()
        self.n = torch.nn.InstanceNorm2d(n.num_features, eps=n.eps, affine=n.affine)
        if n.affine:
            self.n.weight.data.copy_(n.weight.data)
            self.n.bias.data.copy_(n.bias.data)

    def forward(self, x):
        b, c, t = x.shape
        return self.n(x.reshape(b, c, 1, t)).reshape(b, c, t)


class Deconv2d(torch.nn.Module):
    """ConvTranspose1d as a height-1 ConvTranspose2d (ncnn Deconvolution /
    DeconvolutionDepthWise have Vulkan paths; the 1D layers do not)."""
    def __init__(self, c):
        super().__init__()
        self.c = torch.nn.ConvTranspose2d(c.in_channels, c.out_channels, (1, c.kernel_size[0]),
                                          stride=(1, c.stride[0]), padding=(0, c.padding[0]),
                                          output_padding=(0, c.output_padding[0]), groups=c.groups,
                                          bias=c.bias is not None, dilation=(1, c.dilation[0]))
        self.c.weight.data.copy_(c.weight.data.unsqueeze(2))
        if c.bias is not None:
            self.c.bias.data.copy_(c.bias.data)

    def forward(self, x):
        b, c, t = x.shape
        y = self.c(x.reshape(b, c, 1, t))
        return y.reshape(b, y.shape[1], y.shape[3])


def swap(module):
    for name, child in list(module.named_children()):
        if isinstance(child, torch.nn.InstanceNorm1d):
            setattr(module, name, Norm2d(child))
        elif isinstance(child, torch.nn.ConvTranspose1d):
            setattr(module, name, Deconv2d(child))
        else:
            swap(child)


swap(dec)


class Core(torch.nn.Module):
    def __init__(self, d):
        super().__init__()
        self.d = d

    def forward(self, asr, F0_curve, N, s, har):
        d, g = self.d, self.d.generator
        F0 = d.F0_conv(F0_curve.reshape(1, 1, -1))
        Nc = d.N_conv(N.reshape(1, 1, -1))
        x = torch.cat([asr, F0, Nc], axis=1)
        x = d.encode(x, s)
        asr_res = d.asr_res(asr)
        res = True
        for block in d.decode:
            if res:
                x = torch.cat([x, asr_res, F0, Nc], axis=1)
            x = block(x, s)
            if block.upsample_type != 'none':
                res = False
        for i in range(g.num_upsamples):
            x = F.leaky_relu(x, negative_slope=0.1)
            x_source = g.noise_convs[i](har)
            x_source = g.noise_res[i](x_source, s)
            x = g.ups[i](x)
            if i == g.num_upsamples - 1:
                x = g.reflection_pad(x)
            x = x + x_source
            xs = None
            for j in range(g.num_kernels):
                r = g.resblocks[i * g.num_kernels + j](x, s)
                xs = r if xs is None else xs + r
            x = xs / g.num_kernels
        x = F.leaky_relu(x)
        return g.conv_post(x)


core = Core(dec).eval()
T = a.T
frames = 2 * T * 300 // 5 + 1
torch.manual_seed(a.seed)
inputs = (torch.randn(1, cfg['hidden_dim'], T) * 0.5,
          150 + 50 * torch.rand(1, 2 * T),
          torch.randn(1, 2 * T) * 0.3,
          torch.randn(1, cfg['style_dim']) * 0.3,
          torch.randn(1, 22, frames) * 0.5)
with torch.no_grad():
    out = core(*inputs)
    ts = torch.jit.trace(core, inputs)
a.out.mkdir(parents=True, exist_ok=True)
ts.save(str(a.out / 'core.pt'))
for i, t in enumerate(inputs):
    t.contiguous().numpy().astype('float32').tofile(a.out / f'in{i}.f32')
out.contiguous().numpy().astype('float32').tofile(a.out / 'out0.f32')
shapes = ','.join('[' + ','.join(str(v) for v in t.shape) + ']' for t in inputs)
(a.out / 'inputshape.txt').write_text(shapes + '\n')
# ncnn order without the batch dimension: dims w [h [c]]
(a.out / 'shapes.txt').write_text(''.join(f'{t.dim() - 1} ' + ' '.join(str(v) for v in reversed(t.shape[1:])) + '\n' for t in inputs))
n = sum(p.numel() for p in core.parameters())
print(f'T={T} frames={frames} out={tuple(out.shape)} params={n} inputshape={shapes}')
