#include "zircon/DDR.hpp"
#include "zircon/RVCPU.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kProgramSizeBytes = 64 * 1024;
constexpr uint32_t kResultBase = 0x2000;
constexpr uint32_t kStatusAddr = 0x3000;
constexpr uint32_t kStatusDone = 0x51f15e9d;
constexpr uint32_t kMatrixN = 8;

constexpr std::array<int32_t, kMatrixN * kMatrixN> kMatrixA = {
    1, 2, 3, 4, 5, 6, 7, 8,
    -1, 0, 1, 2, 3, 4, 5, 6,
    7, 6, 5, 4, 3, 2, 1, 0,
    2, -3, 4, -5, 6, -7, 8, -9,
    9, 8, 7, 6, 5, 4, 3, 2,
    -2, -1, 0, 1, 2, 3, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    3, 1, 4, 1, 5, 9, 2, 6,
};

constexpr std::array<int32_t, kMatrixN * kMatrixN> kMatrixB = {
    8, 7, 6, 5, 4, 3, 2, 1,
    1, 3, 5, 7, 9, 11, 13, 15,
    2, 0, -2, 4, -4, 6, -6, 8,
    -1, -2, -3, -4, -5, -6, -7, -8,
    4, 1, 4, 1, 4, 1, 4, 1,
    0, 1, 0, 1, 0, 1, 0, 1,
    6, 5, 4, 3, 2, 1, 0, -1,
    -3, 2, -1, 4, -5, 6, -7, 8,
};

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

std::array<int32_t, kMatrixN * kMatrixN> expectedMatrix() {
    std::array<int32_t, kMatrixN * kMatrixN> expected{};
    for (uint32_t i = 0; i < kMatrixN; ++i) {
        for (uint32_t j = 0; j < kMatrixN; ++j) {
            int32_t sum = 0;
            for (uint32_t k = 0; k < kMatrixN; ++k) {
                sum += kMatrixA[i * kMatrixN + k] * kMatrixB[k * kMatrixN + j];
            }
            expected[i * kMatrixN + j] = sum;
        }
    }
    return expected;
}

bool runProgram(zircon::RVCPU& cpu, zircon::DDR& ddr, uint32_t maxSteps) {
    for (uint32_t step = 0; step < maxSteps; ++step) {
        if (nativeRead(ddr, kStatusAddr, 4) == kStatusDone) {
            std::cout << "program completed after " << step << " steps\n";
            return true;
        }

        const uint32_t pc = cpu.getPC();
        const uint32_t inst = nativeRead(ddr, pc, 4);

        zircon::StepResult result;
        try {
            result = cpu.step(inst);
        } catch (const std::exception& error) {
            std::cerr << "CPU failed at pc=0x" << std::hex << pc
                      << " inst=0x" << inst << std::dec << "\n";
            throw;
        }

        if (result.status == zircon::StepStatus::NeedMemory) {
            finishCpuMemory(cpu, ddr, result.memReq);
        }
    }

    return false;
}

bool verifyResult(zircon::DDR& ddr) {
    const auto expected = expectedMatrix();
    bool ok = true;

    for (uint32_t index = 0; index < expected.size(); ++index) {
        const uint32_t addr = kResultBase + index * 4;
        const int32_t actual = static_cast<int32_t>(nativeRead(ddr, addr, 4));
        if (actual != expected[index]) {
            std::cerr << "mismatch at result[" << index << "]: expected "
                      << expected[index] << ", got " << actual << "\n";
            ok = false;
        }
    }

    return ok;
}

} // namespace

int main() {
    try {
        const std::vector<uint8_t> program = readFile(ZIRCON_MATMUL8_BIN);
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
        const bool completed = runProgram(cpu, ddr, 500000);
        if (!completed) {
            std::cerr << "program did not complete\n";
            return 1;
        }

        if (!verifyResult(ddr)) {
            return 1;
        }

        std::cout << "8x8 matrix multiplication passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
