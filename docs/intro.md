# Zircon 模拟库

## 总览

Zircon 提供两个同级 C++ 模型。`RVCPU` 是 RISC-V CPU 模型，`DDR` 是内存从设备模型。两者都位于 `zircon` 命名空间，分别通过 `Zircon::RVCPU` 和 `Zircon::DDR` 作为 CMake 目标导出。

`RVCPU` 只维护处理器内部状态，不持有主存。`DDR` 只维护字节寻址的内存数组和端口握手状态，不依赖 CPU。外部仿真环境负责取指、连接两个模型、推进周期和决定仿真结束条件。

## 工程结构

公开头文件位于 `include/zircon`：

- `RVCPU.hpp`: 定义 `RVCPU`、单步执行结果和 CPU 侧访存请求。
- `DDR.hpp`: 定义 `DDR`、native valid/ready 端口和 AXI4 从设备端口。

源文件位于 `src`：

- `RVCPU.cpp`: 实现指令译码、寄存器更新、程序计数器更新和 CPU 侧访存请求生成。
- `DDR.cpp`: 实现字节数组存储、native 端口访问和 AXI4 端口访问。

## `RVCPU` 类

`RVCPU` 表示一个 RV32 CPU 实例。它负责执行已经取出的 32 bit 指令，并在 `load/store` 或 `FLW/FSW` 时生成访存请求。

主要状态如下：

- `pc`: 程序计数器。
- `gpr`: 32 个整数寄存器。
- `fpr`: 32 个浮点寄存器，保存 32 bit 原始位模式。
- `pendingMem`: 当前未完成的访存提交信息。

CPU 对外提供单步执行入口：

```cpp
zircon::StepResult step(uint32_t inst);
```

普通指令在 `step` 内完成提交，并返回 `StepStatus::Retired`。访存指令返回 `StepStatus::NeedMemory`，外部环境根据 `MemRequest` 完成内存访问，然后调用 `finishMemory`。

```cpp
void finishMemory(uint32_t rdata = 0);
```

在单线程仿真中，调用约定是先完成当前访存，再执行下一条指令。如果存在未完成访存请求时继续调用 `step`，实现会抛出 `std::logic_error`。

## `DDR` 类

`DDR` 表示一个面向系统仿真的内存从设备模型。它提供字节寻址存储，不模拟真实 DDR 的 bank、row buffer、refresh 或训练过程。该模型用于承接 CPU、总线或测试环境发来的读写请求。

`DDR` 有两套端口：

- native valid/ready 端口: 面向简单仿真环境，每个周期最多接受一个读或写请求，读响应可以在同一周期返回。
- AXI4 从设备端口: 面向总线级仿真环境，提供 AW、W、B、AR 和 R 五个通道的基本 valid/ready 握手。

native 端口数据位宽通过 `nativeDataBits` 配置，当前允许 8、16 和 32 bit。AXI4 数据位宽通过 `axiDataBits` 配置，要求是按字节计数的 2 的幂次宽度。

```cpp
zircon::DDR::Config config;
config.sizeBytes = 1024 * 1024;
config.nativeDataBits = 32;
config.axiDataBits = 64;

zircon::DDR ddr(config);
```

## 模型连接关系

`RVCPU` 和 `DDR` 不互相包含，也不互相链接。外部仿真环境可以同时链接两个库，并在需要时把 CPU 侧访存请求转换为 DDR 端口请求。

```cpp
auto result = cpu.step(inst);

if (result.status == zircon::StepStatus::NeedMemory) {
    const auto& req = result.memReq;

    if (req.op == zircon::MemOp::Load) {
        uint32_t data = ddr.read(req.addr, req.size);
        cpu.finishMemory(data);
    } else {
        ddr.write(req.addr, req.wdata, req.size);
        cpu.finishMemory();
    }
}
```

这个边界使 CPU 模型可以连接不同内存模型，也使 `DDR` 可以连接不同主设备。库本身不规定仿真循环、总线拓扑或退出条件。
