# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

```bash
cmake --preset default           # configure (binary dir: tests/build)
cmake --build --preset default   # build
ctest --preset default --output-on-failure  # run all tests
```

Run a single test:
```bash
ctest --preset default -R rvcpu_ddr_native_matmul --output-on-failure
```

All build output goes to `tests/build/`. Test program artifacts (ELF, raw binary, disassembly dumps) are at `tests/build/programs/`.

Core libraries only need CMake + C++17. The test suite additionally requires `clang`, `llvm-objcopy`, and `llvm-objdump` with RISC-V target support — if these aren't found, the libraries still build but tests are skipped.

## Architecture

Two **decoupled** static libraries in namespace `zircon`:

- **`Zircon::RVCPU`** — RISC-V RV32IMF CPU model (one fetched instruction per `step` call). Maintains PC, 32 GPRs, 32 FPRs, and an optional pending memory state. Does NOT own or reference main memory.
- **`Zircon::DDR`** — Byte-addressable memory slave model. Provides three access interfaces: direct `read`/`write`, native valid/ready port (`tickNative`), and AXI4 slave port (`tickAxi4`). Does NOT depend on any CPU model.

External simulation code owns instruction fetch, model wiring, cycle advancement, and termination policy. The libraries are intentionally agnostic about the simulation loop, bus topology, and exit conditions.

### RVCPU Execution Model

`RVCPU::step(uint32_t inst)` executes one already-fetched instruction:
- **Normal instructions** return `StepStatus::Retired` — CPU state is fully updated.
- **Load/store/FLW/FSW** return `StepStatus::NeedMemory` with a `MemRequest`. The caller must complete the memory access and call `cpu.finishMemory(rdata)` (for loads) or `cpu.finishMemory()` (for stores) before the next `step`. Calling `step` with a pending memory request throws `std::logic_error`.
- **Illegal/unsupported instructions** throw `std::invalid_argument`.

Instruction dispatch in `RVCPU.cpp` routes by opcode to `executeRType`, `executeIType`, `executeBType`, `executeSType`, `executeJType`, `executeUType`, `executeFType`, and `executeSystem`. The F-type handler covers both `OPCODE_OP_FP` and fused multiply-add (`FMADD`/`FMSUB`/`FNMSUB`/`FNMADD`).

`executeSystem` handles the Zicsr CSR instructions (`CSRRW`/`CSRRS`/`CSRRC` and their immediate forms `CSRRWI`/`CSRRSI`/`CSRRCI`). CSRs are a generic read/write store keyed by 12-bit address: any address is readable/writable, unwritten CSRs read as 0, and there is no read-only/privilege enforcement. CSRRS/CSRRC skip the CSR write when `rs1`/`zimm` is 0 (the `csrr` read idiom). SYSTEM instructions with `funct3==0` (`ecall`/`ebreak`/`mret`/…) are not modeled and throw `std::invalid_argument`. Inspect CSR state via `getCSR`/`setCSR`.

### DDR Interfaces

1. **Direct access** — `ddr.read(addr, size)` / `ddr.write(addr, data, size)` — little-endian, bounds-checked. Used by tests for simplicity when cycle accuracy isn't needed.
2. **Native port** — `ddr.tickNative(request)` — simple valid/ready per cycle. Always ready. Reads return data in the same cycle.
3. **AXI4 port** — `ddr.tickAxi4(input)` — full 5-channel handshake. One outstanding burst per direction. Supports Fixed, Incrementing, and Wrapping bursts.

### Test Architecture

Tests follow a common pattern implemented in `tests/RVCPU_DDR_Native_*.cpp`:

1. LLVM cross-compiles a bare-metal RV32 program (`tests/programs/*.c` + `start.S` + `riscv32.ld`) via CMake custom commands.
2. The C++ test harness loads the raw binary into DDR, creates an `RVCPU` with PC=0, then runs a simulation loop:
   - Check status address for completion sentinel (`0x51f15e9d`).
   - Read instruction at `cpu.getPC()` from DDR via the native port.
   - Call `cpu.step(inst)`; if `NeedMemory`, bridge to DDR native port and call `finishMemory`.
3. After completion, verify results in DDR memory against expected values.

The ISA coverage test (`isa_rv32imf.c`) writes a pass/fail code and a failure index to known addresses; the harness reads these to report which instruction group failed.

When adding new tests, use `add_zircon_rv32_program` in CMakeLists.txt to compile the bare-metal program, then create a test executable linked to `Zircon::RVCPU` and `Zircon::DDR` with the binary path passed via `target_compile_definitions`.

### Exception Conventions

| Exception | Trigger |
|---|---|
| `std::logic_error` | Calling `step` with unresolved pending memory, or `finishMemory` with none pending |
| `std::invalid_argument` | Illegal/unsupported instruction encoding, invalid DDR config, invalid port access width |
| `std::out_of_range` | Register index, bit range, or direct memory access out of bounds |

Callers are expected to catch these at the simulation loop boundary.

### CMake Structure

Two static library targets (`zircon_rvcpu`, `zircon_ddr`) with ALIAS targets (`Zircon::RVCPU`, `Zircon::DDR`). Compile features: `cxx_std_17`. Warning flags: `-Wall -Wextra -Wpedantic` (non-MSVC). Tests gated behind `ZIRCON_BUILD_TESTS` option and LLVM tool detection.

The `add_zircon_rv32_program` CMake function handles cross-compilation: `clang --target=riscv32-unknown-elf -nostdlib` → ELF, `llvm-objcopy -O binary` → raw binary, `llvm-objdump` → disassembly dump.
