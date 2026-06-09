# ──────────────────────────────────────────────
#   RVV SAT solver
# ──────────────────────────────────────────────
#   Build:           make
#   Run on a CNF:    make run CNF=benchmarks/uf20-01.cnf
#   Run with scalar: make run CNF=... BCP=scalar
#   Run with RVV:    make run CNF=... BCP=rvv      (default)
#   Tests:           make test
#   Debug in spike:  make debug CNF=...
#   Clean:           make clean

# Toolchain
CC      := riscv64-unknown-elf-gcc
OBJDUMP := riscv64-unknown-elf-objdump
SPIKE   := spike
PK      := $(RISCV)/riscv64-unknown-elf/bin/pk
ISA     := rv64gcv

# Flags
CFLAGS  := -march=$(ISA) -mabi=lp64d -O2 -g -Wall -Wextra -static \
           -fno-tree-vectorize -Iinclude
LDFLAGS :=
SPIKEFLAGS := --isa=$(ISA)_zicntr --priv=msu

# Directories
SRC_DIR    := src
INC_DIR    := include
TEST_DIR   := tests
BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj_$(BCP)
BIN 	   := $(BUILD_DIR)/rvv_dpll_$(BCP)

# Source discovery. Solver sources are everything in src/ except main.c
# and the BCP backends; we want both BCP files compiled but only one
# linked into the final binary at a time.
ALL_SRC      := $(wildcard $(SRC_DIR)/*.c)
COMMON_SRC   := $(filter-out $(SRC_DIR)/main.c $(SRC_DIR)/bcp_scalar.c $(SRC_DIR)/bcp_rvv.c $(SRC_DIR)/bcp_rvv_xorsig.c, $(ALL_SRC))
COMMON_OBJ   := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(COMMON_SRC))

MAIN_OBJ     := $(OBJ_DIR)/main.o
SCALAR_OBJ   := $(OBJ_DIR)/bcp_scalar.o
RVV_OBJ      := $(OBJ_DIR)/bcp_rvv.o
XORSIG_OBJ   := $(OBJ_DIR)/bcp_rvv_xorsig.o

# BCP backend selection. Both are always built; one is linked into the
# binary based on the BCP variable. Default is RVV.
# BCP ?= rvv
# until RVV is implemented, default is scalar
BCP ?= scalar
ifeq ($(BCP),scalar)
    BCP_OBJ := $(SCALAR_OBJ)
    BCP_DEF := -DBCP_SCALAR_DEFAULT
else ifeq ($(BCP),rvv)
    BCP_OBJ := $(RVV_OBJ)
    BCP_DEF := -DBCP_RVV_DEFAULT
else ifeq ($(BCP),xorsig)
    BCP_OBJ := $(XORSIG_OBJ)
    BCP_DEF := -DBCP_XORSIG_DEFAULT
else
    $(error BCP must be 'scalar', 'rvv', or 'xorsig', got '$(BCP)')
endif

# Default input for `make run` if none specified
CNF ?= benchmarks/uf20/uf20-01.cnf

# Batch run configuration
BENCH_DIR    ?= benchmarks/uf75
RESULTS_DIR  ?= results
RESULTS_FILE ?= $(RESULTS_DIR)/$(notdir $(BENCH_DIR)).tsv

# ── Targets ───────────────────────────────────

.PHONY: all
all: $(BIN)

$(BIN): $(MAIN_OBJ) $(COMMON_OBJ) $(BCP_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(BCP_DEF) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Run with given CNF
.PHONY: run
run: $(BIN)
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BIN) $(CNF)

# Interactive debugger
.PHONY: debug
debug: $(BIN)
	$(SPIKE) -d $(SPIKEFLAGS) $(PK) $(BIN) $(CNF)

# Build & run tests. Each test in tests/ is its own little program that
# links against the common code plus both BCPs (so it can compare them).
TEST_SRC  := $(wildcard $(TEST_DIR)/*.c)
TEST_BIN  := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/test_%,$(TEST_SRC))

# Default CNF for the parse test
TEST_PARSE_CNF ?= benchmarks/uf20/uf20-01.cnf

$(BUILD_DIR)/test_%: $(TEST_DIR)/%.c $(COMMON_OBJ) $(BCP_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: test
test: test-unit-bcp test-parse test-bcp-step test-bcp-run test-rewind test-solve

.PHONY: test-unit-bcp
test-bcp: $(BUILD_DIR)/test_bcp
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_bcp

.PHONY: test-parse
test-parse: $(BUILD_DIR)/test_parse
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_parse $(TEST_PARSE_CNF)

.PHONY: test-bcp-step
test-bcp-step: $(BUILD_DIR)/test_bcp_step
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_bcp_step

.PHONY: test-bcp-run
test-bcp-run: $(BUILD_DIR)/test_bcp_run
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_bcp_run

.PHONY: test-rewind
test-rewind: $(BUILD_DIR)/test_rewind
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_rewind

.PHONY: test-solve
test-solve: $(BUILD_DIR)/test_solve
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/test_solve

# test: $(TEST_BIN)
# 	@for t in $(TEST_BIN); do \
# 		echo "===== $$t ====="; \
# 		$(SPIKE) $(SPIKEFLAGS) $(PK) $$t || exit 1; \
# 	done

# ── Assembly / Disassembly Inspection ───────────────────────

# Generate compiler-produced assembly (.s) for every src/*.c file
ASM_SRC := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/asm_%.s,$(ALL_SRC))

$(BUILD_DIR)/asm_%.s: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BCP_DEF) -S -fverbose-asm $< -o $@

.PHONY: asm
asm: $(ASM_SRC)
	@echo "Assembly files written to $(BUILD_DIR)/asm_*.s"

# Generate assembly for test files
TEST_ASM := $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/asm_test_%.s,$(TEST_SRC))

$(BUILD_DIR)/asm_test_%.s: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BCP_DEF) -S -fverbose-asm $< -o $@

.PHONY: testasm
testasm: $(TEST_ASM)
	@echo "Test assembly files written to $(BUILD_DIR)/asm_test_*.s"

# Generate disassembly for every object file
OBJ_DIS := $(patsubst $(OBJ_DIR)/%.o,$(BUILD_DIR)/%.obj.dis,\
           $(COMMON_OBJ) $(MAIN_OBJ) $(SCALAR_OBJ) $(RVV_OBJ))

$(BUILD_DIR)/%.obj.dis: $(OBJ_DIR)/%.o | $(BUILD_DIR)
	$(OBJDUMP) -d -S --no-show-raw-insn $< > $@

.PHONY: objdis
objdis: $(OBJ_DIS)
	@echo "Object disassembly files written to $(BUILD_DIR)/*.obj.dis"

# Generate disassembly for every test binary
TEST_DIS := $(patsubst $(BUILD_DIR)/test_%,$(BUILD_DIR)/test_%.dis,$(TEST_BIN))

$(BUILD_DIR)/test_%.dis: $(BUILD_DIR)/test_% | $(BUILD_DIR)
	$(OBJDUMP) -d -S --no-show-raw-insn $< > $@

.PHONY: testdis
testdis: $(TEST_DIS)
	@echo "Test disassembly files written to $(BUILD_DIR)/test_*.dis"

# Disassembly of final binary, with source interleaving
.PHONY: dis
dis: $(BIN)
	$(OBJDUMP) -d -S --no-show-raw-insn $(BIN) > $(BUILD_DIR)/rvv_dpll.dis
	@echo "Disassembly written to $(BUILD_DIR)/rvv_dpll.dis"

# Convenience: rebuild with the other backend
.PHONY: scalar
scalar:
	$(MAKE) BCP=scalar

.PHONY: rvv
rvv:
	$(MAKE) BCP=rvv

# Quick sanity check on the toolchain
.PHONY: check-env
check-env:
	@echo "RISCV    = $(RISCV)"
	@echo "CC       = $(shell which $(CC) 2>/dev/null || echo NOT FOUND)"
	@echo "SPIKE    = $(shell which $(SPIKE) 2>/dev/null || echo NOT FOUND)"
	@echo "PK       = $(PK) $(shell test -f $(PK) && echo [ok] || echo [MISSING])"
	@echo "BCP      = $(BCP) (set with: make BCP=scalar)"
	@echo "CNF      = $(CNF) (set with: make run CNF=...)"

.PHONY: compare
compare:
	$(MAKE) BCP=scalar
	$(MAKE) BCP=rvv
	$(MAKE) BCP=xorsig
	@echo "=== scalar ==="
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/rvv_dpll_scalar $(CNF)
	@echo "=== rvv ==="
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/rvv_dpll_rvv $(CNF)
	@echo "=== xorsig ==="
	$(SPIKE) $(SPIKEFLAGS) $(PK) $(BUILD_DIR)/rvv_dpll_xorsig $(CNF)

.PHONY: xorsig
xorsig:
	$(MAKE) BCP=xorsig

# ── Batch Comparison Run ─────────────────────────────────────────────────────
# Run every .cnf in a benchmark subdirectory through the scalar and xorsig
# backends and record per-problem metrics to a TSV file.
#
#   make batch BENCH_DIR=benchmarks/uf100
#   make batch BENCH_DIR=benchmarks/uf75 RESULTS_FILE=results/myrun.tsv
#   make batch-all          (iterate over all benchmarks/* subdirectories)
#
# TSV columns: problem  backend  result  vars  clauses  solve_instret  cycle_count

.PHONY: batch
batch:
	$(MAKE) BCP=scalar
	$(MAKE) BCP=xorsig
	@mkdir -p $(RESULTS_DIR)
	@printf '# batch run: %s  generated: %s\n' "$(BENCH_DIR)" "$$(date)" \
	    > "$(RESULTS_FILE)"
	@printf 'problem\tbackend\tresult\tvars\tclauses\tsolve_instret\tcycle_count\n' \
	    >> "$(RESULTS_FILE)"
	@n=0; \
	for cnf in $(BENCH_DIR)/*.cnf; do \
	    [ -f "$$cnf" ] || { echo "No .cnf files found in $(BENCH_DIR)" >&2; exit 1; }; \
	    for backend in scalar xorsig; do \
	        bin=$(BUILD_DIR)/rvv_dpll_$$backend; \
	        out=$$($(SPIKE) $(SPIKEFLAGS) $(PK) "$$bin" "$$cnf" 2>&1); \
	        result=$$(printf '%s\n' "$$out" | awk '/^s /{print $$2; exit}'); \
	        vars=$$(printf '%s\n' "$$out" | awk '/c parsed/{print $$4; exit}'); \
	        clauses=$$(printf '%s\n' "$$out" | awk '/c parsed/{print $$6; exit}'); \
	        instret=$$(printf '%s\n' "$$out" | awk '/c solve instret:/{print $$4; exit}'); \
	        cycles=$$(printf '%s\n' "$$out" | awk '/c cycle count:/{print $$4; exit}'); \
	        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
	            "$$cnf" "$$backend" "$${result:-ERROR}" \
	            "$${vars:-?}" "$${clauses:-?}" \
	            "$${instret:-N/A}" "$${cycles:-N/A}" \
	            >> "$(RESULTS_FILE)"; \
	        n=$$((n+1)); \
	        printf '[%4d] %-10s  %-40s  %-15s  instret=%-12s  cycles=%s\n' \
	            "$$n" "$$backend" "$$cnf" "$${result:-ERROR}" \
	            "$${instret:-N/A}" "$${cycles:-N/A}"; \
	    done; \
	done
	@echo "Results written to $(RESULTS_FILE)"

.PHONY: batch-all
batch-all:
	$(MAKE) BCP=scalar
	$(MAKE) BCP=xorsig
	@for dir in benchmarks/*/; do \
	    [ -d "$$dir" ] || continue; \
	    dir="$${dir%/}"; \
	    $(MAKE) batch BENCH_DIR="$$dir"; \
	done

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: help
help:
	@echo "Targets:"
	@echo "  make                          - build with default BCP backend (scalar)"
	@echo "  make BCP=scalar               - build with scalar BCP backend"
	@echo "  make BCP=xorsig               - build with xorsig BCP backend"
	@echo "  make run CNF=...              - run on the given CNF file"
	@echo "  make debug CNF=...            - run in spike interactive debugger"
	@echo "  make compare CNF=...          - run scalar/rvv/xorsig on one CNF"
	@echo "  make batch BENCH_DIR=...      - run all .cnf in a subdirectory,"
	@echo "                                  write TSV to results/<dir>.tsv"
	@echo "  make batch-all                - batch over all benchmarks/* subdirs"
	@echo "  make test                     - build and run all tests"
	@echo "  make dis                      - dump disassembly to build/rvv_dpll.dis"
	@echo "  make check-env                - verify toolchain is set up"
	@echo "  make clean                    - remove build directory"