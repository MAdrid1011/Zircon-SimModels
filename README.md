# Zircon

Zircon is a C++ simulation library with two peer models:

- `RVCPU`: a RISC-V CPU model that executes one fetched instruction per `step` call.
- `DDR`: a byte-addressable memory slave model with native valid/ready and AXI4 interfaces.

The library does not provide a `main` function. External simulation code connects the models and owns the simulation loop.

Default local build:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The default preset writes local build files under `tests/build`.
