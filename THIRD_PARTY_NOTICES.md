# Third-party notices

Original Radish code, tools and documentation are licensed under [MIT](LICENSE).
Existing upstream copyright notices and license terms are retained.

- Mesa patch context and adapted Freedreno code are MIT licensed. The represented
  upstream notices are in [licenses/mesa-MIT.txt](licenses/mesa-MIT.txt); individual
  source headers remain authoritative. Mesa itself is not bundled. Its pinned
  source includes additional components with their own licenses.
- ncnn patch context and modifications to ncnn source retain BSD-3-Clause terms.
  See the pinned upstream [license and notices](licenses/ncnn-LICENSE.txt).
  ncnn and its third-party dependencies are not bundled.
- glslang, Vulkan headers/loader, Vulkan CTS, ONNX Runtime, model weights and
  generated fixtures are external dependencies. Their licenses are separate
  from this repository's MIT license; this repository does not include them.

Pinned revisions and patch ordering are recorded in
[radish/patches/README.md](radish/patches/README.md).
