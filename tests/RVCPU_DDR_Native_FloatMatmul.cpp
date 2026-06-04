#include "zircon/DDR.hpp"
#include "zircon/RVCPU.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kProgramSizeBytes = 64 * 1024;
constexpr uint32_t kResultBase = 0x4000;
constexpr uint32_t kStatusAddr = 0x5000;
constexpr uint32_t kStatusDone = 0x51f15e9d;
constexpr uint32_t kMatrixN = 8;

constexpr std::array<float, kMatrixN * kMatrixN> kMatrixA = {
    1.0f, 0.5f, -1.0f, 2.0f, 0.25f, -0.5f, 1.5f, -2.0f,
    -0.5f, 1.0f, 0.75f, -1.25f, 2.5f, -2.0f, 0.125f, 1.75f,
    2.0f, -1.5f, 1.25f, 0.5f, -0.75f, 1.0f, -2.5f, 0.25f,
    0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f,
    -2.0f, -1.0f, 0.5f, 1.5f, -0.25f, 0.75f, -1.25f, 2.25f,
    1.5f, -0.25f, 2.0f, -0.5f, 0.5f, -1.5f, 1.0f, 0.75f,
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, -0.5f, -1.0f,
    -1.75f, 2.25f, -0.75f, 1.25f, -0.5f, 0.5f, 1.0f, -2.0f,
};

constexpr std::array<float, kMatrixN * kMatrixN> kMatrixB = {
    0.5f, -1.0f, 1.5f, -2.0f, 2.5f, -0.25f, 0.75f, 1.0f,
    1.0f, 0.25f, -0.5f, 0.75f, -1.25f, 1.5f, -2.0f, 2.25f,
    -1.5f, 2.0f, 0.5f, -0.25f, 1.0f, -0.75f, 1.25f, -2.5f,
    2.0f, -0.5f, 1.0f, 0.5f, -1.0f, 2.5f, -1.5f, 0.25f,
    0.25f, 0.5f, 1.0f, 2.0f, -0.5f, -1.0f, 1.5f, -2.0f,
    -0.75f, 1.25f, -2.25f, 0.5f, 1.75f, -0.25f, 2.0f, -1.0f,
    1.5f, -2.0f, 2.5f, -1.5f, 0.5f, 1.0f, -0.25f, 0.75f,
    -2.5f, 1.5f, -1.0f, 2.0f, -0.75f, 0.25f, 1.25f, -0.5f,
};

uint32_t floatBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float roundedMulAdd(float sum, float lhs, float rhs) {
    volatile float product = lhs * rhs;
    volatile float next = sum + product;
    return next;
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

std::array<uint32_t, kMatrixN * kMatrixN> expectedBits() {
    std::array<uint32_t, kMatrixN * kMatrixN> expected{};
    for (uint32_t i = 0; i < kMatrixN; ++i) {
        for (uint32_t j = 0; j < kMatrixN; ++j) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < kMatrixN; ++k) {
                sum = roundedMulAdd(sum, kMatrixA[i * kMatrixN + k], kMatrixB[k * kMatrixN + j]);
            }
            expected[i * kMatrixN + j] = floatBits(sum);
        }
    }
    return expected;
}

bool runProgram(zircon::RVCPU& cpu, zircon::DDR& ddr, uint32_t maxSteps) {
    for (uint32_t step = 0; step < maxSteps; ++step) {
        if (nativeRead(ddr, kStatusAddr, 4) == kStatusDone) {
            std::cout << "float program completed after " << step << " steps\n";
            return true;
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

    return false;
}

bool verifyResult(zircon::DDR& ddr) {
    const auto expected = expectedBits();
    bool ok = true;

    for (uint32_t index = 0; index < expected.size(); ++index) {
        const uint32_t addr = kResultBase + index * 4;
        const uint32_t actualBits = nativeRead(ddr, addr, 4);
        if (actualBits != expected[index]) {
            std::cerr << "bit mismatch at result[" << index << "]: expected 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << expected[index]
                      << ", got 0x" << std::setw(8) << actualBits << std::dec << "\n";
            ok = false;
        }
    }

    return ok;
}

} // namespace

int main() {
    try {
        const std::vector<uint8_t> program = readFile(ZIRCON_MATMUL8F_BIN);
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
            std::cerr << "float program did not complete\n";
            return 1;
        }

        if (!verifyResult(ddr)) {
            return 1;
        }

        std::cout << "8x8 float matrix multiplication bit-compare passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
