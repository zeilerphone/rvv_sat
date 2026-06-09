# RVV SAT

## Overview
This repo currently consists of a naive approach to parallelizing DPLL. The only component targeted to be optimized for vector instructions is the Boolean Constraint Propagation function. 

## Quick Start
Toolchain prerequisites: 

`riscv64_unknown_elf_gcc`: 
- after ensuring you have the prerequisites, clone `https://github.com/riscv/riscv-gnu-toolchain`
- use 
, spike, pk, 

## Project Structure:
rvv_sat/
├── Makefile
├── README.md
├── benchmarks
│   ├── test.cnf
│   ├── uf20-*.cnf
│   ├── uf200-01.cnf
│   └── uf75-01.cnf
├── include
│   ├── bcp.h
│   ├── cnf.h
│   ├── debug.h
│   ├── solver.h
│   └── trail.h
├── src
│   ├── bcp_queue.c
│   ├── bcp_rvv.c
│   ├── bcp_scalar.c
│   ├── cnf.c
│   ├── debug.c
│   ├── main.c
│   ├── solver.c
│   └── trail.c
└── tests
    ├── bcp_run.c
    ├── bcp_step.c
    ├── parse.c
    ├── rewind.c
    ├── solve.c
    └── unit_bcp.c

## Design Notes
### Variable/Literal representation
There are two different ways that literal are represented. The `.cnf` problem files store clause literal membership in the DIMACS cnf format which uses 1-indexed signed values. Internally, literals are encoded in 0-indexed, unsigned format. Conversion helper inline functions are included in `cnf.h`. 
To convert from *DIMACS to internal*, `v -> i`: 
- for positive `v` literal : `l = 2 * (v - 1)`
- for negative `v` literal : `l = 2 * (v - 1) + 1`
To convert from *internal to DIMACS*:
- for "even" internal literal (`(l & x1) == 0`) : `v = (l // 2) + 1`
- for "odd" internal literal (`(l & x1) == 1`) : `v = -(l // 2) - 1`

### Clauses 
Clause membership is stored in 3 different ways. The `Formula` struct contains: 
- a sparse binary matrix stored in CSR with rows corresponding to clauses and columns corresponding to literals 
- a dense `num_clauses` by `max_clause_length` array, row-major
- a transposed csr for the sparse binary 

The different storage methods may need to be merged in the future. For now, the sparse matrix exists for forward scalar iterations and other cases in which unequal strides are acceptable. The dense matrix exists to aid the vectorized solver by making the clause membership constant and masking out zero clauses. The transposed sparse matrix is used to find clauses containing a literal. 

### 

## Build & test
After setting up the prerequisites, `Makefile` contains many build and test rules for convenience. 

### Requirement Check
The project targets `rv64gcv` and is currently tested using the RISC-V GNU toolchain along with the Spike ISA simulator. Requirements are listed above.

The Makefile expected the RISCV environment label to point to the toolchain installation root. For standard installations, the following will do this:
```
export RISCV=$HOME/riscv
export PATH=$RISCV/bin:$PATH
```

To ensure the toolchain is built and configured correctly, use 
```
make check-env
```
This prints:
- dedicated compiler path
- Spike path
- Proxy Kernel path
- selected BCP backend
- selected CNF default

### Building
The solver can be built with the default backend with:
```
make
```
The generated binary is written to `build/rvv_dpll_<backend>`. 

By default, the BCP backend is the scalar version. This can be changed at build time using the BCP variable:
```
make BCP=scalar
OR
make BCP=rvv
```
This structure allows for direct comparison between scalar and vectorized propagation.

### Running
The solver can be run on a DIMACS CNF benchmark:
```
make run CNF=benchmarks/uf20-01.cnf
```
Using the vector BCP backend:
```
make run BCP=rvv CNF=benchmarks/uf20-01.cnf
```
Right now, execution runs through Spike. The make command runs:
```
spike --isa=rv64gcv pk <binary> <cnf>
```
If CNF is omitted, the Makefile defaults to `benchmarks/uf20-01.cnf`.

### Testing and Debugging
Spike includes an interactive debugger. It can be launched with:
```
make debug CNF=benchmarks/uf20-01.cnf
```
This launches `spike -d` which can be used for:
- inspecting control flow
- verifying RVV instruction generation
- debugging propagation logic
- checking solver state transitions

There is a test suite available. The following runs the full test suite:
```
make test
```
This command executes several focused test binaries under Spike.
Individual tests can be run with 
```
make <test name>
```
The following individual tests are available.
`test-parse`: Tests DIMACS parsing and formula construction. Useful for validating initialization and construction of datastructures. 
`test-bcp-step`: Checks the correctness of `bcp_prop_one` steps. 
`test-bcp-run`: Checks the correctness of `bcp_run`.
`test-rewind`: Checks the correctness of `bcp_rwnd_one` rewind iterations.
`test-solve`: Runs the solver on a series of test cases. These aim to capture edge cases that might be missed in testing on DIMACS benchmarks.

### Comparing Scalar vs RVV implementations
Build and run both implementations sequentially:
```
make compare CNF=benchmarks/uf20-01.cnf
```
This is useful for checking correctness and a rough performance evaluation under Spike.

### Assembly inspection
There are a number of make rules to inspect complier output and generated RVV instructions. 
#### Compiler Assembly:
```
make asm
```
Outputs: `build/asm_*.S`
Useful to check auto-generated assembly, inspect vector intrinsics, and validate instrinsic lowering.

#### Test Assembly:
```
make testasm
```
Outputs assembly for test programs. Useful for connecting program execution from the Spike debugger to the C code.
#### Disassembly:
```
make dis
```
Outputs: `build/rvv_dpll.dis` 
This includes interleaved source, linked code, fully resolved instruction layout.
```
make objdis
```
Outputs: `build/*.obj.dis
Useful for inspecting compiled object files, inlined functions, and emitted RVV instructions.

### Cleaning
```
make clean
```
Removes all build artifacts. 

## Performance methodology
The programs are, currently, only run using the Spike simulator. Spike is a functional and not-cycle accurate simulator. This means that assumptions made about performance may be missing important real issuse such as hardware stalls or branch mispredictions. 
However, these limitations do not make Spike simulator useless. Because we can compare `bcp_scalar` with `bcp_rvv`, we can still get a rough estimate of relative performance by comparing cycle counts between the two.

## Roadmap
## References
SAT-Accel
Chaff
Field Programmable Ising Arrays with In-Memory Computing
etc.

## Contributing notes


## LLM attribution:
Initial scaffolding for non-BCP files was drafted with Claude Opus 4.7 and revised by hand. The scalar and vector BCP files were written by me.
`cnf.c`   : initial parsing script, has since been added 
`debug.c` : print, parse bypass, and snapshot debug helpers, `verify_sat`, large drag and drop dump fragments at bottom of file
`main.c`  : `verify_sat`(duplicate), `print_assignment`, argument parsing and  exit messages for `main`
`tests/*` : most test cases, verified and modified by hand


