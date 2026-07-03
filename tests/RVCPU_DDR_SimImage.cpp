// Runner for an externally built RVA "sim_image" program (e.g. the vecadd test
// image). This image has an unusual memory map:
//   - code / data / bss  live at base 0x00000000 (loaded from the raw binary)
//   - the stack lives in a separate high RAM: __stack_top = 0x80100000
//   - a memory mapped device / console sits at 0xf0000000
//
// A single flat array spanning 0..0x80100000 would be ~2 GB, so this harness
// models two RAM regions (low + stack) with a DDR instance each, plus a minimal
// MMIO device for character output and program exit. SYSTEM instructions the
// RVCPU model does not implement (wfi/mret/ecall/ebreak) are skipped as no-ops.
//
// Usage: test_rvcpu_sim_image [path/to/sim_image.bin]
// With no argument it falls back to the ZIRCON_SIM_IMAGE_BIN compile definition.

#include "zircon/DDR.hpp"
#include "zircon/RVCPU.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kLowBase = 0x00000000u;   // code / data / bss
constexpr uint32_t kLowSize = 0x00100000u;   // 1 MB is ample for this image
constexpr uint32_t kStackBase = 0x80000000u; // dedicated stack RAM window
constexpr uint32_t kStackSize = 0x00200000u; // covers the 0x80100000 stack top
constexpr uint32_t kMmioBase = 0xf0000000u;
constexpr uint32_t kMmioPutc = 0xf0000200u;  // rva_sim_putc / puts / putu / putx
constexpr uint32_t kMmioExit = 0xf0000204u;  // rva_sim_exit
constexpr uint32_t kMaxSteps = 5'000'000u;

// SYSTEM opcode with funct3==0: ecall/ebreak/wfi/mret/sfence. The RVCPU model
// throws on these; CSR instructions (funct3 != 0) are handled normally by step.
bool isSystemNonCsr(uint32_t inst) {
    return (inst & 0x7fu) == 0x73u && ((inst >> 12) & 0x7u) == 0x0u;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open program binary: " + path);
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to measure program binary");
    }
    input.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    if (!input && size != 0) {
        throw std::runtime_error("failed to read program binary");
    }
    return data;
}

uint32_t nativeRead(zircon::DDR& ddr, uint32_t localAddr, uint32_t size) {
    zircon::DDR::NativeRequest request;
    request.valid = true;
    request.write = false;
    request.addr = localAddr;
    request.size = size;

    const zircon::DDR::NativeResponse response = ddr.tickNative(request);
    if (!response.ready || !response.rvalid || response.error) {
        throw std::runtime_error("DDR read failed");
    }
    return response.rdata;
}

void nativeWrite(zircon::DDR& ddr, uint32_t localAddr, uint32_t data, uint32_t size) {
    zircon::DDR::NativeRequest request;
    request.valid = true;
    request.write = true;
    request.addr = localAddr;
    request.wdata = data;
    request.size = size;
    request.wstrb = (size >= 4) ? 0xffffffffu : ((1u << size) - 1u);

    const zircon::DDR::NativeResponse response = ddr.tickNative(request);
    if (!response.ready || response.error) {
        throw std::runtime_error("DDR write failed");
    }
}

// Minimal memory-mapped device: console output and a program-exit latch.
// The command/response FIFOs of the real RVA device are not modeled.
struct Mmio {
    bool exited = false;
    uint32_t exitCode = 0;

    void store(uint32_t addr, uint32_t data) {
        switch (addr) {
        case kMmioPutc:
            std::cout << static_cast<char>(data & 0xffu);
            break;
        case kMmioExit:
            exited = true;
            exitCode = data;
            break;
        default:
            // Unmodeled device register: drop the write.
            break;
        }
    }

    uint32_t load(uint32_t /*addr*/) const {
        // No device responses are modeled; FIFOs read as empty.
        return 0;
    }
};

// Address router across the two RAM windows and the MMIO region.
class Bus {
public:
    Bus(zircon::DDR& low, zircon::DDR& stack, Mmio& mmio)
        : low_(low), stack_(stack), mmio_(mmio) {}

    uint32_t read(uint32_t addr, uint32_t size) {
        if (addr >= kMmioBase) {
            return mmio_.load(addr);
        }
        if (inLow(addr)) {
            return nativeRead(low_, addr - kLowBase, size);
        }
        if (inStack(addr)) {
            return nativeRead(stack_, addr - kStackBase, size);
        }
        throw unmapped("read", addr, size);
    }

    void write(uint32_t addr, uint32_t data, uint32_t size) {
        if (addr >= kMmioBase) {
            mmio_.store(addr, data);
            return;
        }
        if (inLow(addr)) {
            nativeWrite(low_, addr - kLowBase, data, size);
            return;
        }
        if (inStack(addr)) {
            nativeWrite(stack_, addr - kStackBase, data, size);
            return;
        }
        throw unmapped("write", addr, size);
    }

private:
    static bool inLow(uint32_t addr) {
        return addr - kLowBase < kLowSize;
    }
    static bool inStack(uint32_t addr) {
        return addr - kStackBase < kStackSize;
    }
    static std::runtime_error unmapped(const char* op, uint32_t addr, uint32_t size) {
        std::ostringstream oss;
        oss << "unmapped " << op << " at 0x" << std::hex << addr
            << " size " << std::dec << size;
        return std::runtime_error(oss.str());
    }

    zircon::DDR& low_;
    zircon::DDR& stack_;
    Mmio& mmio_;
};

} // namespace

int main(int argc, char** argv) {
    try {
        std::string path;
        if (argc > 1) {
            path = argv[1];
        } else {
#ifdef ZIRCON_SIM_IMAGE_BIN
            path = ZIRCON_SIM_IMAGE_BIN;
#else
            std::cerr << "usage: " << argv[0] << " <sim_image.bin>\n";
            return 2;
#endif
        }

        const std::vector<uint8_t> program = readFile(path);
        if (program.empty() || program.size() > kLowSize) {
            std::cerr << "program binary size is invalid: " << program.size() << "\n";
            return 2;
        }

        zircon::DDR::Config lowConfig;
        lowConfig.sizeBytes = kLowSize;
        zircon::DDR lowRam(lowConfig);
        std::copy(program.begin(), program.end(), lowRam.data().begin());

        zircon::DDR::Config stackConfig;
        stackConfig.sizeBytes = kStackSize;
        zircon::DDR stackRam(stackConfig);

        Mmio mmio;
        Bus bus(lowRam, stackRam, mmio);

        zircon::RVCPU cpu(0);

        uint32_t step = 0;
        for (; step < kMaxSteps && !mmio.exited; ++step) {
            const uint32_t pc = cpu.getPC();
            const uint32_t inst = bus.read(pc, 4);

            // wfi/mret/ecall/ebreak are not modeled by RVCPU. Skip them as no-ops
            // so interrupt wait loops fall through to the step limit; the program
            // normally terminates earlier by writing the exit MMIO register.
            if (isSystemNonCsr(inst)) {
                cpu.setPC(pc + 4);
                continue;
            }

            zircon::StepResult result;
            try {
                result = cpu.step(inst);
            } catch (const std::exception& error) {
                std::cout.flush();
                std::cerr << "\nCPU fault at pc=0x" << std::hex << pc
                          << " inst=0x" << inst << std::dec << ": "
                          << error.what() << "\n";
                throw;
            }

            if (result.status != zircon::StepStatus::NeedMemory) {
                continue;
            }

            const zircon::MemRequest& req = result.memReq;
            if (req.op == zircon::MemOp::Load) {
                cpu.finishMemory(bus.read(req.addr, req.size));
            } else {
                bus.write(req.addr, req.wdata, req.size);
                cpu.finishMemory();
            }
        }

        std::cout.flush();
        if (mmio.exited) {
            std::cout << "\n[sim_image exited with code " << mmio.exitCode
                      << " after " << step << " steps]\n";
            return 0;
        }

        std::cerr << "\n[sim_image did not exit within " << kMaxSteps << " steps]\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
