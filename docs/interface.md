# Zircon 接口说明

## 适用范围

本文档说明如何在外部仿真程序中调用 Zircon 库。库提供两个同级模型。`RVCPU` 执行 RISC-V 指令并生成访存请求。`DDR` 提供字节寻址内存、native valid/ready 端口和 AXI4 从设备端口。

库没有提供 `main` 函数。外部程序需要负责取指、模型连接、周期推进和仿真结束条件。

## 构建与链接

仓库根目录提供 CMake 工程：

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

默认 preset 会把 CMake 构建目录放在 `tests/build`。该目录只保存本地构建产物，不进入版本管理。

在另一个 CMake 工程中使用本库时，可以将本仓库作为子目录加入，然后按需链接两个目标：

```cmake
add_subdirectory(path/to/Zircon-RVCPU-Simulator)
target_link_libraries(your_target PRIVATE Zircon::RVCPU Zircon::DDR)
```

公开头文件如下：

```cpp
#include "zircon/RVCPU.hpp"
#include "zircon/DDR.hpp"
```

所有公开类型位于 `zircon` 命名空间。

## `RVCPU` 对象

`RVCPU` 表示一个 RV32 CPU 实例。对象内部保存程序计数器、整数寄存器、浮点寄存器和一条未完成访存请求的提交信息。

常用构造和复位接口如下：

```cpp
zircon::RVCPU cpu;
zircon::RVCPU cpuWithPc(0x80000000);

cpu.reset();
cpu.reset(0x80000000);
```

`reset` 会清空寄存器和未完成访存请求，并设置新的程序计数器。寄存器 `x0` 在写入后仍保持为 0。

程序计数器和寄存器访问接口如下：

```cpp
uint32_t pc = cpu.getPC();
cpu.setPC(0x80000000);

uint32_t x1 = cpu.getGPR(1);
cpu.setGPR(1, 0x12345678);

uint32_t f0Bits = cpu.getFPRBits(0);
cpu.setFPRBits(0, f0Bits);
```

浮点寄存器接口读写 32 bit 原始位模式。调用者如果需要浮点数值，可以在外部完成位模式转换。

## CPU 单步执行

外部程序先根据 `cpu.getPC()` 取出一条指令，然后调用 `step`：

```cpp
uint32_t inst = fetchInstruction(cpu.getPC());
zircon::StepResult result = cpu.step(inst);
```

`step` 返回两种状态：

| 状态 | 含义 |
| --- | --- |
| `StepStatus::Retired` | 指令已经完成，CPU 已经更新寄存器和程序计数器 |
| `StepStatus::NeedMemory` | 指令需要一次外部访存，CPU 已经生成访存请求 |

当 `step` 返回 `NeedMemory` 时，访存请求保存在 `StepResult::memReq` 中：

```cpp
struct MemRequest {
    MemOp op;
    uint32_t addr;
    uint32_t wdata;
    uint32_t size;
};
```

字段含义如下：

| 字段 | 含义 |
| --- | --- |
| `op` | 访存类型，取值为 `MemOp::Load` 或 `MemOp::Store` |
| `addr` | CPU 计算出的有效地址 |
| `wdata` | store 写数据，load 请求中该字段为 0 |
| `size` | 访问字节数，当前取值为 1、2、4 |

对于 load 请求，外部程序读取内存数据后，将读到的值传入 `finishMemory`：

```cpp
uint32_t data = memory.read(req.addr, req.size);
cpu.finishMemory(data);
```

对于 store 请求，外部程序完成写入后调用无参数形式：

```cpp
memory.write(req.addr, req.wdata, req.size);
cpu.finishMemory();
```

如果上一条访存请求还没有通过 `finishMemory` 完成，再次调用 `step` 会抛出 `std::logic_error`。非法指令或当前未支持的编码会抛出 `std::invalid_argument`。

## `DDR` 对象

`DDR` 表示一个内存从设备模型。对象内部保存字节数组和端口握手状态。容量和端口位宽通过 `Config` 配置：

```cpp
zircon::DDR::Config config;
config.sizeBytes = 1024 * 1024;
config.nativeDataBits = 32;
config.axiDataBits = 64;

zircon::DDR ddr(config);
```

`nativeDataBits` 当前允许 8、16 和 32 bit。`axiDataBits` 要求为按字节计数的 2 的幂次宽度。配置非法时构造函数会抛出 `std::invalid_argument`。

`DDR` 也提供直接读写方法，便于简单测试和 CPU 访存请求桥接：

```cpp
uint32_t value = ddr.read(0x1000, 4);
ddr.write(0x1000, value, 4);
```

直接读写使用小端序。访问越界或访问宽度非法时会抛出 `std::out_of_range` 或 `std::invalid_argument`。

## Native 端口

native 端口使用一组简单 valid/ready 信号。每次调用 `tickNative` 表示推进一个端口周期。

```cpp
zircon::DDR::NativeRequest req;
req.valid = true;
req.write = false;
req.addr = 0x1000;
req.size = 4;

zircon::DDR::NativeResponse resp = ddr.tickNative(req);
```

请求字段如下：

| 字段 | 含义 |
| --- | --- |
| `valid` | 请求有效 |
| `write` | 为 true 时表示写请求，为 false 时表示读请求 |
| `addr` | 字节地址 |
| `wdata` | 写数据，小端序放在低位 |
| `wstrb` | 写字节使能，低位对应低地址字节 |
| `size` | 访问字节数 |

响应字段如下：

| 字段 | 含义 |
| --- | --- |
| `ready` | DDR 当前周期可以接收请求 |
| `rvalid` | 读数据有效 |
| `rdata` | 读数据，小端序放在低位 |
| `error` | 请求地址或宽度非法 |

当前 native 端口没有内部等待队列。`ready` 在每个周期为 true。读请求在请求被接收的同一周期返回 `rvalid` 和 `rdata`。

## AXI4 端口

AXI4 端口使用 `tickAxi4` 推进一个周期。输入结构包含 AW、W、B、AR 和 R 五个通道的主设备侧信号。输出结构包含 DDR 从设备侧的 ready、valid、响应和读数据。

```cpp
zircon::DDR::Axi4Input in;
in.ar.valid = true;
in.ar.id = 0;
in.ar.addr = 0x1000;
in.ar.len = 0;
in.ar.size = 2;
in.ar.burst = zircon::DDR::AxiBurst::Incrementing;
in.rReady = true;

zircon::DDR::Axi4Output out = ddr.tickAxi4(in);
```

`size` 字段使用 AXI4 的编码方式，表示每拍字节数的以 2 为底的对数。示例中的 `size = 2` 表示每拍 4 字节。

AXI4 端口当前支持以下基本行为：

- AW 和 W 通道完成写地址和写数据握手。
- B 通道返回写响应。
- AR 通道完成读地址握手。
- R 通道逐拍返回读数据和 `last`。
- 支持 `Fixed`、`Incrementing` 和 `Wrapping` 三种 burst 类型。
- 支持 byte strobe 写掩码。
- 每个方向保留一个未完成 burst。

AXI4 读数据使用字节向量承载，长度等于 `axiDataBits / 8`。写数据也使用字节向量，`strb` 中非零字节表示对应 lane 写入有效。

```cpp
zircon::DDR::Axi4Input in;
in.aw.valid = true;
in.aw.addr = 0x2000;
in.aw.len = 0;
in.aw.size = 2;
in.w.valid = true;
in.w.data = {0x78, 0x56, 0x34, 0x12};
in.w.strb = {1, 1, 1, 1};
in.w.last = true;
in.bReady = true;

zircon::DDR::Axi4Output out0 = ddr.tickAxi4(in);
zircon::DDR::Axi4Output out1 = ddr.tickAxi4(in);
```

简单从设备为了保持实现可预测，只在接收 AW 后接收对应 W 数据。因此 AW 和 W 同周期给出时，W 数据通常会在下一次 `tickAxi4` 中被接收。外部主设备模型应根据 `awReady` 和 `wReady` 分别推进两个通道。

## CPU 与 DDR 连接示例

下面的代码只展示调用顺序。外部程序可以选择直接读写 DDR，也可以把 CPU 请求转换到 native 或 AXI4 端口。

```cpp
void simulateOneInstruction(zircon::RVCPU& cpu, zircon::DDR& ddr) {
    uint32_t inst = ddr.read(cpu.getPC(), 4);
    zircon::StepResult result = cpu.step(inst);

    if (result.status == zircon::StepStatus::Retired) {
        return;
    }

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

## 端到端测试

仓库提供一个 RVCPU 与 DDR native 端口连接的端到端测试。测试使用 LLVM 将 8x8 整数矩阵乘法程序编译为 RV32IM 裸机程序，再把生成的 binary 装入 DDR。测试 harness 通过 native valid/ready 端口完成取指、load 和 store，最后读取结果矩阵并与主机侧计算结果比较。

仓库也提供一个 8x8 float 矩阵乘法测试。该测试使用 LLVM 将程序编译为 RV32IMF 裸机程序，并逐项比较结果矩阵的 32 bit 原始位模式。比较过程不会使用误差阈值。

仓库还提供一个 RV32IMF ISA 覆盖测试。该测试使用 inline asm 强制生成整数、乘除、分支跳转、不同宽度访存和单精度浮点指令，并在裸机程序内检查 corner case。失败时程序会写出失败编号，测试 harness 会从 DDR 中读回该编号。

测试程序和启动代码位于 `tests/programs`。测试 harness 位于 `tests/RVCPU_DDR_Native_Matmul.cpp`、`tests/RVCPU_DDR_Native_FloatMatmul.cpp` 和 `tests/RVCPU_DDR_Native_ISA.cpp`。

运行方式如下：

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

该测试需要 LLVM `clang` 和 `llvm-objcopy`。如果 CMake 没有找到这些工具，库本身仍可以构建，但该测试目标不会生成。

每个裸机程序还会在 `tests/build/programs` 下生成一个 `.dump` 文件。该文件由 `llvm-objdump` 生成，包含 section 字节内容和 `.text` 反汇编，便于同时查看指令和矩阵数据。

## 异常约定

当前接口用 C++ 异常表达调用错误、非法配置和非法指令：

| 异常类型 | 触发条件 |
| --- | --- |
| `std::logic_error` | 调用顺序错误，例如存在未完成访存请求时继续调用 `step` |
| `std::invalid_argument` | 指令非法、配置非法、端口访问宽度非法，或当前实现还没有支持该编码 |
| `std::out_of_range` | 寄存器编号、位范围或直接内存访问越界 |

外部仿真程序可以在仿真循环外层捕获这些异常，并将异常转换为日志、断言失败或测试失败。

## 接口边界

`RVCPU` 内部执行指令译码、寄存器更新、程序计数器更新和访存地址计算。`DDR` 内部执行存储数组访问和端口握手。外部程序负责取指、模型连接、周期推进、总线拓扑和终止条件。
