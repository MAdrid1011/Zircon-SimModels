# Zircon-SimModels

Zircon-SimModels provides C++ simulation models for Zircon verification work. The repository currently contains two peer models:

- `RVCPU`: a RISC-V RV32 CPU model that executes one fetched instruction per `step` call.
- `DDR`: a byte-addressable memory slave model with native valid/ready and AXI4 interfaces.

The models are intentionally decoupled. `RVCPU` does not own main memory, and `DDR` does not depend on any CPU model. External simulation code owns instruction fetch, model wiring, cycle advancement, and termination policy.

## Components

Public headers are under `include/zircon`:

- `RVCPU.hpp` defines `RVCPU`, `StepResult`, `MemRequest`, and CPU-side memory request types.
- `DDR.hpp` defines `DDR`, its native valid/ready port, and its AXI4 slave port.

CMake exports the two targets as:

```cmake
Zircon::RVCPU
Zircon::DDR
```

The C++ namespace is `zircon`.

## Build

The default preset writes all local build output under `tests/build`.

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

The core libraries only require CMake and a C++17 compiler. The bare-metal RV32 test programs additionally require LLVM tools with RISC-V support:

- `clang`
- `llvm-objcopy`
- `llvm-objdump`

Generated test program artifacts are placed under `tests/build/programs`, including ELF files, raw binaries, and disassembly dumps.

## Using The Library

Add the repository as a CMake subdirectory and link the models you need:

```cmake
add_subdirectory(path/to/Zircon-SimModels)
target_link_libraries(your_target PRIVATE Zircon::RVCPU Zircon::DDR)
```

Include the public headers:

```cpp
#include "zircon/RVCPU.hpp"
#include "zircon/DDR.hpp"
```

`RVCPU::step` executes one already fetched instruction. Normal instructions return `StepStatus::Retired`. Load and store instructions return `StepStatus::NeedMemory` with a `MemRequest` that the outer simulator must complete.

```cpp
zircon::RVCPU cpu(0);
zircon::DDR ddr;

uint32_t inst = fetchInstruction(cpu.getPC());
zircon::StepResult result = cpu.step(inst);

if (result.status == zircon::StepStatus::NeedMemory) {
    const zircon::MemRequest& req = result.memReq;

    if (req.op == zircon::MemOp::Load) {
        uint32_t data = ddr.read(req.addr, req.size);
        cpu.finishMemory(data);
    } else {
        ddr.write(req.addr, req.wdata, req.size);
        cpu.finishMemory();
    }
}
```

The same CPU request can also be bridged to `DDR::tickNative` or to another memory simulator. This keeps the CPU model, memory model, and system harness independently replaceable.

## Tests

The test suite currently includes:

- native `RVCPU` to `DDR` integer 8 by 8 matrix multiplication.
- native `RVCPU` to `DDR` floating-point 8 by 8 matrix multiplication with bit-level result comparison.
- an RV32IMF bare-metal ISA coverage program built from inline assembly.

The tests compile standalone RV32 programs with `-nostdlib` and run them through the C++ models. They do not require a runtime library or a hosted RISC-V environment.

## Documentation

- `docs/intro.md` describes the model split and design boundary.
- `docs/interface.md` describes how an external simulator calls the library interfaces.
