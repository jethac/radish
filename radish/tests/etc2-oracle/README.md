# Independent ETC2/EAC oracle

This test reference reuses ISC-licensed detex at [cab11584](https://github.com/hglm/detex/tree/cab11584d9be140602a66fd9a88ef0b99f08a97a). Per-file copyright/license notices and the defined-integer portability patch are retained in this directory. Driver layout and view code instead reuses the pinned Mesa Vulkan image helpers and FDL5; the CPU decoder is not driver code.

Host-only check, from this directory:

```sh
cc -O2 -fsanitize=address,undefined -I. -Ivendor selftest.c etc_oracle.c vendor/decompress-etc.c vendor/decompress-eac.c vendor/clamp.c -lm -o /tmp/radish-etc-oracle
/tmp/radish-etc-oracle
```

Known fixtures and40960 generated blocks pass the retained sanitizer run. This does not qualify GPU decoding. The original sRGB consumer still fails; see `../../results/etc2-eac-partial-0045.json`.
