# Control-flow compile admission

These are compile-only inputs for the two difficult original CTS cases. They
do not replace the original CTS executable or its numerical oracle.

`extract.cpp` preserves the pinned CTS generator bodies and expressions. Build
it with `c++ -std=c++17 -O0 -Wall -Wextra -Werror extract.cpp -o extract`, then
run `./extract OUTPUT_DIRECTORY`. Assemble each generated `.spvasm` with
`spirv-as --target-env vulkan1.0 INPUT -o OUTPUT` and validate with
`spirv-val --target-env vulkan1.1 OUTPUT`. Do not optimize or reshape the module.
The exact tool hashes, module hashes and commands are retained in
`results/official11-control-flow-0045.json` at the repository evidence root.

Build `control_flow_compile.c` using the matching isolated AArch64 Mesa build
flags and headers, including the Freedreno BO and IR3 headers. The evidence
record contains the complete original compiler argv and five header pins.
It includes `core_resources.c` from this directory. The helper accepts only
`DRIVER MODULE --compile-only`; it allocates no test data or commands and
prints actual code/private resource sizes. RBA is disabled to match the
original CTS default feature environment. Physical support remains checked.

Use the separate bounded wide/nested compile runners and review actual native
resources before executing either original singleton case. All original
device reserve, memory, timeout and fault controls remain required. Source
and native admission alone do not establish numerical correctness.
