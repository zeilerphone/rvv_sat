# RVV SAT

## Overview
This repo currently consists of a naive approach to parallelizing DPLL. The only component targeted to be optimized for vector instructions is the Boolean Constraint Propagation function. 

## Quick Start

### Toolchain prerequisites 

On Debian/Ubuntu: 
```bash
sudo apt install -y \
  build-essential git cmake autoconf automake \
  device-tree-compiler flex bison texinfo \
  libmpc-dev libmpfr-dev libgmp-dev python3
```

On RHEL/Rocky/AlmaLinux:
```bash
sudo dnf install -y \
  gcc gcc-c++ git cmake autoconf automake \
  dtc flex bison texinfo \
  libmpc-devel mpfr-devel gmp-devel python3
```

No sudo? Use conda and miniforge:
```bash
curl -L https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh \
  | bash -s -- -b -p $HOME/miniforge3
source $HOME/miniforge3/bin/activate
conda create -n riscv-build -c conda-forge \
  cmake autoconf automake make flex bison texinfo \
  dtc mpc mpfr gmp python
conda activate riscv-build
```

#### Environment Variables

Set these in your shell profile (`~/.bashrc` or `~/.bash_profile`) and source it before building anything:

```bash
export RISCV=$HOME/riscv          # install prefix — change to a shared path on a cluster
export PATH=$RISCV/bin:$PATH
```

### Toolchain install
#### RISC-V GNU Toolchain (`riscv64-unknown-elf-gcc`)
```bash
git clone https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain

# Configure for bare-metal (elf) target with RVV support
./configure \
  --prefix=$RISCV \
  --with-arch=rv64gcv \
  --with-abi=lp64d

# Build (this takes 20–40 min; use -j$(nproc) to parallelize)
make -j$(nproc)
```

After this step `$RISCV/bin/riscv64-unknown-elf-gcc` should exist.

#### Spike ISA simulator
```bash
git clone https://github.com/riscv-software-src/riscv-isa-sim.git
cd riscv-isa-sim
mkdir build && cd build

../configure --prefix=$RISCV
make -j$(nproc)
make install
```

Verify: `spike --help` should print `Spike RISC-V ISA Simulator 1.1.1-dev`.

#### RISC-V Proxy Kernel (`pk`)
The proxy kernel must be cross-compiled with the toolchain you just built.

```bash
git clone https://github.com/riscv-software-src/riscv-pk.git
cd riscv-pk
mkdir build && cd build

../configure \
  --prefix=$RISCV \
  --host=riscv64-unknown-elf \
  CC=riscv64-unknown-elf-gcc
make -j$(nproc)
make install
```

After this step `$RISCV/riscv64-unknown-elf/bin/pk` should exist. That exact path is what the Makefile expects via `$(RISCV)/riscv64-unknown-elf/bin/pk`.

### Clone and build the solver
```bash
git clone <repo-url>
cd rvv_sat

# Sanity-check the toolchain
make check-env

# Build scalar and RVV backends
make BCP=scalar
make BCP=rvv

# Run on a benchmark under Spike
make run BCP=rvv CNF=benchmarks/uf75-01.cnf
```


## Project Structure:
```
rvv_sat/
├── Makefile
├── README.md
├── benchmarks
│   ├── test.cnf
│   ├── flat50
│   │   └── flat50-xx.cnf
│   ├── flat100
│   │   └── flat100-xx.cnf
│   ├── sw100-8-0
│   │   └── sw100-xx.cnf
│   ├── sw100-8-2
│   │   └── sw100-xx.cnf
│   ├── sw100-8-4
│   │   └── sw100-xx.cnf
│   ├── sw100-8-6
│   │   └── sw100-xx.cnf
│   ├── sw100-8-8
│   │   └── sw100-xx.cnf
│   ├── uf20
│   │   └── uf20-0xx.cnf
│   ├── uf50
│   │   └── uf50-0xx.cnf
│   ├── uf75
│   │   └── uf75-0xx.cnf
│   ├── uf100
│   │   └── uf100-0xx.cnf
│   ├── uf125
│   │   └── uf125-0xx.cnf
│   ├── uf150
│   │   └── uf150-0xx.cnf
│   ├── uf175
│   │   └── uf175-0xx.cnf
│   ├── uf200
│   │   └── uf200-0xx.cnf
│   ├── uuf50
│   │   └── uuf50-0xx.cnf
│   ├── uuf75
│   │   └── uuf75-0xx.cnf
│   ├── uuf100
│   │   └── uuf100-0xx.cnf
│   ├── uuf125
│   │   └── uuf125-0xx.cnf
│   ├── uuf150
│   │   └── uuf150-0xx.cnf
│   ├── uuf175
│   │   └── uuf175-0xx.cnf
│   └── uuf200
│       └── uuf200-0xx.cnf
├── build
├── include
│   ├── bcp.h
│   ├── cnf.h
│   ├── debug.h
│   ├── solver.h
│   └── trail.h
├── src
│   ├── bcp_queue.c
│   ├── bcp_rvv.c
│   ├── bcp_rvv_xorsig.c
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
    └── solve.c
```

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

A new configuration, using an XOR clause signature similar to the one used in the SAT-Accel paper, is used in the `bcp_rvv_xorsig` variant. This XORs the signature of each of the member clauses with to construct a clause signature. When a member literal becomes falsified, the 'unassigned' counter is decremented and the literal is XOR'd with the clause signature. When a clause becomes unit, this saves on iterating through the clause membership by instead leaving just the unit literal in the clause signature. 

Additionally, the Assignment struct stores the number of unassigned and number of satisfied literals in each clause as one packed 32bit integer. The upper 16 bits are used to store the number of satisfied literals and the lower 16 bits are used to store the number of unassigned literals. 

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


