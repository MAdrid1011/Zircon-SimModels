#include "zircon/DDR.hpp"
#include "zircon/RVCPU.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kProgramSizeBytes = 64 * 1024;
constexpr uint32_t kStatusAddr = 0x6000;
constexpr uint32_t kFailCodeAddr = 0x6004;
constexpr uint32_t kStatusPass = 0x51f15e9d;
constexpr uint32_t kStatusFail = 0xdeadc0de;

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

uint32_t nativeRead(zircon::DDR& ddr, uint32_t addr, uint32_t size) {
    zircon::DDR::NativeRequest request;
    request.valid = true;
    request.write = false;
    request.addr = addr;
    request.size = size;

    const zircon::DDR::NativeResponse response = ddr.tickNative(request);
    if (!response.ready || !response.rvalid || response.error) {
        throw std::runtime_error("native read failed");
    }

    return response.rdata;
}

void nativeWrite(zircon::DDR& ddr, uint32_t addr, uint32_t data, uint32_t size) {
    zircon::DDR::NativeRequest request;
    request.valid = true;
    request.write = true;
    request.addr = addr;
    request.wdata = data;
    request.size = size;
    request.wstrb = (size >= 4) ? 0xffffffffu : ((1u << size) - 1u);

    const zircon::DDR::NativeResponse response = ddr.tickNative(request);
    if (!response.ready || response.error) {
        throw std::runtime_error("native write failed");
    }
}

void finishCpuMemory(zircon::RVCPU& cpu, zircon::DDR& ddr, const zircon::MemRequest& request) {
    if (request.op == zircon::MemOp::Load) {
        const uint32_t data = nativeRead(ddr, request.addr, request.size);
        cpu.finishMemory(data);
        return;
    }

    nativeWrite(ddr, request.addr, request.wdata, request.size);
    cpu.finishMemory();
}

uint32_t runProgram(zircon::RVCPU& cpu, zircon::DDR& ddr, uint32_t maxSteps) {
    for (uint32_t step = 0; step < maxSteps; ++step) {
        const uint32_t status = nativeRead(ddr, kStatusAddr, 4);
        if (status == kStatusPass || status == kStatusFail) {
            std::cout << "ISA program completed after " << step << " steps\n";
            return status;
        }

        const uint32_t pc = cpu.getPC();
        const uint32_t inst = nativeRead(ddr, pc, 4);

        zircon::StepResult result;
        try {
            result = cpu.step(inst);
        } catch (const std::exception&) {
            std::cerr << "CPU failed at pc=0x" << std::hex << pc
                      << " inst=0x" << inst << std::dec << "\n";
            throw;
        }

        if (result.status == zircon::StepStatus::NeedMemory) {
            finishCpuMemory(cpu, ddr, result.memReq);
        }
    }

    return 0;
}

} // namespace

int main() {
    try {
        const std::vector<uint8_t> program = readFile(ZIRCON_ISA_RV32IMF_BIN);
        if (program.empty() || program.size() > kProgramSizeBytes) {
            std::cerr << "program binary size is invalid: " << program.size() << "\n";
            return 1;
        }

        zircon::DDR::Config config;
        config.sizeBytes = kProgramSizeBytes;
        config.nativeDataBits = 32;
        config.axiDataBits = 32;
        zircon::DDR ddr(config);

        std::copy(program.begin(), program.end(), ddr.data().begin());

        zircon::RVCPU cpu(0);
        const uint32_t status = runProgram(cpu, ddr, 1000000);
        if (status != kStatusPass) {
            const uint32_t failCode = nativeRead(ddr, kFailCodeAddr, 4);
            std::cerr << "ISA test failed with status=0x" << std::hex << status
                      << " failCode=0x" << failCode << std::dec << "\n";
            return 1;
        }

        std::cout << "RV32IMF ISA coverage test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
