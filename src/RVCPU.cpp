#include "zircon/RVCPU.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace zircon {

namespace {

constexpr uint32_t OPCODE_LOAD = 0x03;
constexpr uint32_t OPCODE_LOAD_FP = 0x07;
constexpr uint32_t OPCODE_OP_IMM = 0x13;
constexpr uint32_t OPCODE_AUIPC = 0x17;
constexpr uint32_t OPCODE_STORE = 0x23;
constexpr uint32_t OPCODE_STORE_FP = 0x27;
constexpr uint32_t OPCODE_OP = 0x33;
constexpr uint32_t OPCODE_LUI = 0x37;
constexpr uint32_t OPCODE_BRANCH = 0x63;
constexpr uint32_t OPCODE_JALR = 0x67;
constexpr uint32_t OPCODE_JAL = 0x6f;
constexpr uint32_t OPCODE_OP_FP = 0x53;
constexpr uint32_t OPCODE_FMADD = 0x43;
constexpr uint32_t OPCODE_FMSUB = 0x47;
constexpr uint32_t OPCODE_FNMSUB = 0x4b;
constexpr uint32_t OPCODE_FNMADD = 0x4f;

uint32_t asUInt32(int32_t value) {
    return static_cast<uint32_t>(value);
}

int32_t asInt32(uint32_t value) {
    return static_cast<int32_t>(value);
}

uint32_t high32(uint64_t value) {
    return static_cast<uint32_t>(value >> 32);
}

int32_t saturateInt32(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= static_cast<float>(std::numeric_limits<int32_t>::min())) {
        return std::numeric_limits<int32_t>::min();
    }
    if (value >= static_cast<float>(std::numeric_limits<int32_t>::max())) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(value);
}

uint32_t saturateUInt32(float value) {
    if (std::isnan(value) || value <= 0.0f) {
        return 0;
    }
    if (value >= static_cast<float>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(value);
}

} // namespace

RVCPU::RVCPU() {
    reset();
}

RVCPU::RVCPU(uint32_t initialPc) {
    reset(initialPc);
}

void RVCPU::reset(uint32_t initialPc) {
    pc_ = initialPc;
    gpr_.fill(0);
    fpr_.fill(0);
    pendingMem_.reset();
}

StepResult RVCPU::step(uint32_t inst) {
    if (pendingMem_.has_value()) {
        throw std::logic_error("finishMemory must be called before the next step");
    }

    switch (opcode(inst)) {
    case OPCODE_OP:
        return executeRType(inst);
    case OPCODE_LOAD:
    case OPCODE_LOAD_FP:
    case OPCODE_OP_IMM:
    case OPCODE_JALR:
        return executeIType(inst);
    case OPCODE_STORE:
    case OPCODE_STORE_FP:
        return executeSType(inst);
    case OPCODE_BRANCH:
        return executeBType(inst);
    case OPCODE_JAL:
        return executeJType(inst);
    case OPCODE_LUI:
    case OPCODE_AUIPC:
        return executeUType(inst);
    case OPCODE_OP_FP:
    case OPCODE_FMADD:
    case OPCODE_FMSUB:
    case OPCODE_FNMSUB:
    case OPCODE_FNMADD:
        return executeFType(inst);
    default:
        throwIllegal(inst, "unknown opcode");
    }
}

void RVCPU::finishMemory(uint32_t rdata) {
    if (!pendingMem_.has_value()) {
        throw std::logic_error("finishMemory called without a pending memory request");
    }

    const PendingMemory pending = *pendingMem_;

    if (pending.op == MemOp::Load) {
        if (pending.target == PendingTarget::GPR && pending.rd != 0) {
            const uint32_t width = pending.size * 8;
            const uint32_t value = pending.signExtend
                ? signExtend(rdata, width)
                : zeroExtend(rdata, width);
            writeGPR(pending.rd, value);
        } else if (pending.target == PendingTarget::FPR) {
            checkRegisterIndex(pending.rd);
            fpr_[pending.rd] = rdata;
        }
    }

    pc_ = pending.nextPc;
    pendingMem_.reset();
    enforceGPRZero();
}

uint32_t RVCPU::getPC() const {
    return pc_;
}

void RVCPU::setPC(uint32_t value) {
    pc_ = value;
}

uint32_t RVCPU::getGPR(uint32_t index) const {
    checkRegisterIndex(index);
    return gpr_[index];
}

void RVCPU::setGPR(uint32_t index, uint32_t value) {
    checkRegisterIndex(index);
    writeGPR(index, value);
}

uint32_t RVCPU::getFPRBits(uint32_t index) const {
    checkRegisterIndex(index);
    return fpr_[index];
}

void RVCPU::setFPRBits(uint32_t index, uint32_t value) {
    checkRegisterIndex(index);
    fpr_[index] = value;
}

bool RVCPU::hasPendingMemory() const {
    return pendingMem_.has_value();
}

uint32_t RVCPU::bits(uint32_t value, uint32_t lo, uint32_t hi) {
    if (lo > hi || hi >= 32) {
        throw std::out_of_range("invalid bit range");
    }

    const uint32_t width = hi - lo + 1;
    const uint32_t shifted = value >> lo;
    if (width == 32) {
        return shifted;
    }

    return shifted & ((1u << width) - 1u);
}

uint32_t RVCPU::signExtend(uint32_t value, uint32_t width) {
    if (width == 0 || width > 32) {
        throw std::out_of_range("invalid sign extension width");
    }
    if (width == 32) {
        return value;
    }

    const uint32_t mask = (1u << width) - 1u;
    const uint32_t signBit = 1u << (width - 1);
    value &= mask;
    return (value ^ signBit) - signBit;
}

uint32_t RVCPU::zeroExtend(uint32_t value, uint32_t width) {
    if (width == 0 || width > 32) {
        throw std::out_of_range("invalid zero extension width");
    }
    if (width == 32) {
        return value;
    }

    return value & ((1u << width) - 1u);
}

StepResult RVCPU::executeRType(uint32_t inst) {
    const uint32_t rD = rd(inst);
    const uint32_t rS1 = rs1(inst);
    const uint32_t rS2 = rs2(inst);
    const uint32_t f3 = funct3(inst);
    const uint32_t f7 = funct7(inst);
    const uint32_t lhs = gpr_[rS1];
    const uint32_t rhs = gpr_[rS2];
    uint32_t result = 0;

    if (f7 == 0x00 || f7 == 0x20) {
        switch (f3) {
        case 0x0:
            result = (f7 == 0x20) ? lhs - rhs : lhs + rhs;
            break;
        case 0x1:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid SLL encoding");
            }
            result = lhs << (rhs & 0x1f);
            break;
        case 0x2:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid SLT encoding");
            }
            result = asInt32(lhs) < asInt32(rhs) ? 1u : 0u;
            break;
        case 0x3:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid SLTU encoding");
            }
            result = lhs < rhs ? 1u : 0u;
            break;
        case 0x4:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid XOR encoding");
            }
            result = lhs ^ rhs;
            break;
        case 0x5:
            result = (f7 == 0x20)
                ? asUInt32(asInt32(lhs) >> (rhs & 0x1f))
                : lhs >> (rhs & 0x1f);
            break;
        case 0x6:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid OR encoding");
            }
            result = lhs | rhs;
            break;
        case 0x7:
            if (f7 != 0x00) {
                throwIllegal(inst, "invalid AND encoding");
            }
            result = lhs & rhs;
            break;
        default:
            throwIllegal(inst, "invalid R-type funct3");
        }
    } else if (f7 == 0x01) {
        switch (f3) {
        case 0x0:
            result = static_cast<uint32_t>(
                static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs));
            break;
        case 0x1: {
            const int64_t product =
                static_cast<int64_t>(asInt32(lhs)) * static_cast<int64_t>(asInt32(rhs));
            result = high32(static_cast<uint64_t>(product));
            break;
        }
        case 0x2: {
            const int64_t product =
                static_cast<int64_t>(asInt32(lhs)) * static_cast<int64_t>(rhs);
            result = high32(static_cast<uint64_t>(product));
            break;
        }
        case 0x3: {
            const uint64_t product =
                static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs);
            result = high32(product);
            break;
        }
        case 0x4:
            if (rhs == 0) {
                result = 0xffffffffu;
            } else if (lhs == 0x80000000u && rhs == 0xffffffffu) {
                result = lhs;
            } else {
                result = asUInt32(asInt32(lhs) / asInt32(rhs));
            }
            break;
        case 0x5:
            result = (rhs == 0) ? 0xffffffffu : lhs / rhs;
            break;
        case 0x6:
            if (rhs == 0) {
                result = lhs;
            } else if (lhs == 0x80000000u && rhs == 0xffffffffu) {
                result = 0;
            } else {
                result = asUInt32(asInt32(lhs) % asInt32(rhs));
            }
            break;
        case 0x7:
            result = (rhs == 0) ? lhs : lhs % rhs;
            break;
        default:
            throwIllegal(inst, "invalid M-extension funct3");
        }
    } else {
        throwIllegal(inst, "invalid R-type funct7");
    }

    writeGPR(rD, result);
    return retire(pc_ + 4);
}

StepResult RVCPU::executeIType(uint32_t inst) {
    const uint32_t op = opcode(inst);
    const uint32_t rD = rd(inst);
    const uint32_t rS1 = rs1(inst);
    const int32_t imm = decodeIImm(inst);

    if (op == OPCODE_LOAD) {
        const uint32_t size = decodeLoadSize(inst);
        const bool isSigned = decodeLoadSigned(inst);
        const uint32_t addr = gpr_[rS1] + asUInt32(imm);

        return needMemory(
            MemRequest{MemOp::Load, addr, 0, size},
            PendingMemory{MemOp::Load, PendingTarget::GPR, rD, size, isSigned, pc_ + 4});
    }

    if (op == OPCODE_LOAD_FP) {
        if (funct3(inst) != 0x2) {
            throwIllegal(inst, "only FLW is supported for floating-point loads");
        }

        const uint32_t addr = gpr_[rS1] + asUInt32(imm);
        return needMemory(
            MemRequest{MemOp::Load, addr, 0, 4},
            PendingMemory{MemOp::Load, PendingTarget::FPR, rD, 4, false, pc_ + 4});
    }

    if (op == OPCODE_JALR) {
        if (funct3(inst) != 0x0) {
            throwIllegal(inst, "invalid JALR funct3");
        }

        const uint32_t nextPc = (gpr_[rS1] + asUInt32(imm)) & ~1u;
        writeGPR(rD, pc_ + 4);
        return retire(nextPc);
    }

    if (op != OPCODE_OP_IMM) {
        throwIllegal(inst, "invalid I-type opcode");
    }

    uint32_t result = 0;
    const uint32_t f3 = funct3(inst);
    const uint32_t lhs = gpr_[rS1];

    switch (f3) {
    case 0x0:
        result = lhs + asUInt32(imm);
        break;
    case 0x1:
        if (bits(inst, 25, 31) != 0x00) {
            throwIllegal(inst, "invalid SLLI encoding");
        }
        result = lhs << bits(inst, 20, 24);
        break;
    case 0x2:
        result = asInt32(lhs) < imm ? 1u : 0u;
        break;
    case 0x3:
        result = lhs < asUInt32(imm) ? 1u : 0u;
        break;
    case 0x4:
        result = lhs ^ asUInt32(imm);
        break;
    case 0x5: {
        const uint32_t shiftKind = bits(inst, 25, 31);
        if (shiftKind == 0x00) {
            result = lhs >> bits(inst, 20, 24);
        } else if (shiftKind == 0x20) {
            result = asUInt32(asInt32(lhs) >> bits(inst, 20, 24));
        } else {
            throwIllegal(inst, "invalid SRLI/SRAI encoding");
        }
        break;
    }
    case 0x6:
        result = lhs | asUInt32(imm);
        break;
    case 0x7:
        result = lhs & asUInt32(imm);
        break;
    default:
        throwIllegal(inst, "invalid I-type funct3");
    }

    writeGPR(rD, result);
    return retire(pc_ + 4);
}

StepResult RVCPU::executeBType(uint32_t inst) {
    const uint32_t lhs = gpr_[rs1(inst)];
    const uint32_t rhs = gpr_[rs2(inst)];
    bool take = false;

    switch (funct3(inst)) {
    case 0x0:
        take = lhs == rhs;
        break;
    case 0x1:
        take = lhs != rhs;
        break;
    case 0x4:
        take = asInt32(lhs) < asInt32(rhs);
        break;
    case 0x5:
        take = asInt32(lhs) >= asInt32(rhs);
        break;
    case 0x6:
        take = lhs < rhs;
        break;
    case 0x7:
        take = lhs >= rhs;
        break;
    default:
        throwIllegal(inst, "invalid branch funct3");
    }

    const uint32_t nextPc = take ? pc_ + asUInt32(decodeBImm(inst)) : pc_ + 4;
    return retire(nextPc);
}

StepResult RVCPU::executeSType(uint32_t inst) {
    const uint32_t op = opcode(inst);
    const uint32_t rS1 = rs1(inst);
    const uint32_t rS2 = rs2(inst);
    const int32_t imm = decodeSImm(inst);
    const uint32_t addr = gpr_[rS1] + asUInt32(imm);

    if (op == OPCODE_STORE) {
        const uint32_t size = decodeStoreSize(inst);
        const uint32_t data = gpr_[rS2] & maskForBytes(size);

        return needMemory(
            MemRequest{MemOp::Store, addr, data, size},
            PendingMemory{MemOp::Store, PendingTarget::None, 0, size, false, pc_ + 4});
    }

    if (op == OPCODE_STORE_FP) {
        if (funct3(inst) != 0x2) {
            throwIllegal(inst, "only FSW is supported for floating-point stores");
        }

        return needMemory(
            MemRequest{MemOp::Store, addr, fpr_[rS2], 4},
            PendingMemory{MemOp::Store, PendingTarget::None, 0, 4, false, pc_ + 4});
    }

    throwIllegal(inst, "invalid S-type opcode");
}

StepResult RVCPU::executeJType(uint32_t inst) {
    const uint32_t nextPc = pc_ + asUInt32(decodeJImm(inst));
    writeGPR(rd(inst), pc_ + 4);
    return retire(nextPc);
}

StepResult RVCPU::executeUType(uint32_t inst) {
    const uint32_t imm = decodeUImm(inst);

    if (opcode(inst) == OPCODE_LUI) {
        writeGPR(rd(inst), imm);
    } else if (opcode(inst) == OPCODE_AUIPC) {
        writeGPR(rd(inst), pc_ + imm);
    } else {
        throwIllegal(inst, "invalid U-type opcode");
    }

    return retire(pc_ + 4);
}

StepResult RVCPU::executeFType(uint32_t inst) {
    const uint32_t op = opcode(inst);

    if (op == OPCODE_FMADD || op == OPCODE_FMSUB ||
        op == OPCODE_FNMSUB || op == OPCODE_FNMADD) {
        if (bits(inst, 25, 26) != 0x0) {
            throwIllegal(inst, "only single-precision fused operations are supported");
        }

        const float a = bitsToFloat(fpr_[rs1(inst)]);
        const float b = bitsToFloat(fpr_[rs2(inst)]);
        const float c = bitsToFloat(fpr_[rs3(inst)]);
        float result = 0.0f;

        if (op == OPCODE_FMADD) {
            result = std::fma(a, b, c);
        } else if (op == OPCODE_FMSUB) {
            result = std::fma(a, b, -c);
        } else if (op == OPCODE_FNMSUB) {
            result = std::fma(-a, b, c);
        } else {
            result = std::fma(-a, b, -c);
        }

        fpr_[rd(inst)] = floatToBits(result);
        return retire(pc_ + 4);
    }

    if (op != OPCODE_OP_FP) {
        throwIllegal(inst, "invalid floating-point opcode");
    }

    const uint32_t rD = rd(inst);
    const uint32_t rS1 = rs1(inst);
    const uint32_t rS2 = rs2(inst);
    const uint32_t f3 = funct3(inst);
    const uint32_t f7 = funct7(inst);
    const float lhs = bitsToFloat(fpr_[rS1]);
    const float rhs = bitsToFloat(fpr_[rS2]);

    switch (f7) {
    case 0x00:
        fpr_[rD] = floatToBits(lhs + rhs);
        break;
    case 0x04:
        fpr_[rD] = floatToBits(lhs - rhs);
        break;
    case 0x08:
        fpr_[rD] = floatToBits(lhs * rhs);
        break;
    case 0x0c:
        fpr_[rD] = floatToBits(lhs / rhs);
        break;
    case 0x10: {
        const uint32_t magnitude = fpr_[rS1] & 0x7fffffffu;
        uint32_t sign = fpr_[rS2] & 0x80000000u;
        if (f3 == 0x1) {
            sign ^= 0x80000000u;
        } else if (f3 == 0x2) {
            sign = (fpr_[rS1] ^ fpr_[rS2]) & 0x80000000u;
        } else if (f3 != 0x0) {
            throwIllegal(inst, "invalid FSGNJ funct3");
        }
        fpr_[rD] = magnitude | sign;
        break;
    }
    case 0x14:
        if (f3 == 0x0) {
            fpr_[rD] = floatToBits(std::fmin(lhs, rhs));
        } else if (f3 == 0x1) {
            fpr_[rD] = floatToBits(std::fmax(lhs, rhs));
        } else {
            throwIllegal(inst, "invalid FMIN/FMAX funct3");
        }
        break;
    case 0x2c:
        fpr_[rD] = floatToBits(std::sqrt(lhs));
        break;
    case 0x50:
        if (f3 == 0x0) {
            writeGPR(rD, lhs <= rhs ? 1u : 0u);
        } else if (f3 == 0x1) {
            writeGPR(rD, lhs < rhs ? 1u : 0u);
        } else if (f3 == 0x2) {
            writeGPR(rD, lhs == rhs ? 1u : 0u);
        } else {
            throwIllegal(inst, "invalid floating-point compare funct3");
        }
        break;
    case 0x60:
        if (rS2 == 0) {
            writeGPR(rD, asUInt32(saturateInt32(lhs)));
        } else if (rS2 == 1) {
            writeGPR(rD, saturateUInt32(lhs));
        } else {
            throwIllegal(inst, "invalid FCVT integer destination");
        }
        break;
    case 0x68:
        if (rS2 == 0) {
            fpr_[rD] = floatToBits(static_cast<float>(asInt32(gpr_[rS1])));
        } else if (rS2 == 1) {
            fpr_[rD] = floatToBits(static_cast<float>(gpr_[rS1]));
        } else {
            throwIllegal(inst, "invalid FCVT single destination");
        }
        break;
    case 0x70:
        if (f3 == 0x0) {
            writeGPR(rD, fpr_[rS1]);
        } else if (f3 == 0x1) {
            writeGPR(rD, classifyFloat(fpr_[rS1]));
        } else {
            throwIllegal(inst, "invalid FMV.X.W/FCLASS.S funct3");
        }
        break;
    case 0x78:
        if (f3 != 0x0) {
            throwIllegal(inst, "invalid FMV.W.X funct3");
        }
        fpr_[rD] = gpr_[rS1];
        break;
    default:
        throwIllegal(inst, "unsupported floating-point operation");
    }

    return retire(pc_ + 4);
}

StepResult RVCPU::retire(uint32_t nextPc) {
    pc_ = nextPc;
    enforceGPRZero();
    return StepResult{};
}

StepResult RVCPU::needMemory(const MemRequest& request, const PendingMemory& pending) {
    pendingMem_ = pending;
    enforceGPRZero();
    return StepResult{StepStatus::NeedMemory, request};
}

uint32_t RVCPU::opcode(uint32_t inst) {
    return bits(inst, 0, 6);
}

uint32_t RVCPU::rd(uint32_t inst) {
    return bits(inst, 7, 11);
}

uint32_t RVCPU::rs1(uint32_t inst) {
    return bits(inst, 15, 19);
}

uint32_t RVCPU::rs2(uint32_t inst) {
    return bits(inst, 20, 24);
}

uint32_t RVCPU::rs3(uint32_t inst) {
    return bits(inst, 27, 31);
}

uint32_t RVCPU::funct3(uint32_t inst) {
    return bits(inst, 12, 14);
}

uint32_t RVCPU::funct7(uint32_t inst) {
    return bits(inst, 25, 31);
}

int32_t RVCPU::decodeIImm(uint32_t inst) {
    return asInt32(signExtend(bits(inst, 20, 31), 12));
}

int32_t RVCPU::decodeSImm(uint32_t inst) {
    const uint32_t imm = (bits(inst, 25, 31) << 5) | bits(inst, 7, 11);
    return asInt32(signExtend(imm, 12));
}

int32_t RVCPU::decodeBImm(uint32_t inst) {
    const uint32_t imm =
        (bits(inst, 31, 31) << 12) |
        (bits(inst, 7, 7) << 11) |
        (bits(inst, 25, 30) << 5) |
        (bits(inst, 8, 11) << 1);
    return asInt32(signExtend(imm, 13));
}

int32_t RVCPU::decodeJImm(uint32_t inst) {
    const uint32_t imm =
        (bits(inst, 31, 31) << 20) |
        (bits(inst, 12, 19) << 12) |
        (bits(inst, 20, 20) << 11) |
        (bits(inst, 21, 30) << 1);
    return asInt32(signExtend(imm, 21));
}

uint32_t RVCPU::decodeUImm(uint32_t inst) {
    return inst & 0xfffff000u;
}

uint32_t RVCPU::decodeLoadSize(uint32_t inst) {
    switch (funct3(inst)) {
    case 0x0:
    case 0x4:
        return 1;
    case 0x1:
    case 0x5:
        return 2;
    case 0x2:
        return 4;
    default:
        throwIllegal(inst, "invalid load width");
    }
}

bool RVCPU::decodeLoadSigned(uint32_t inst) {
    switch (funct3(inst)) {
    case 0x0:
    case 0x1:
    case 0x2:
        return true;
    case 0x4:
    case 0x5:
        return false;
    default:
        throwIllegal(inst, "invalid load sign mode");
    }
}

uint32_t RVCPU::decodeStoreSize(uint32_t inst) {
    switch (funct3(inst)) {
    case 0x0:
        return 1;
    case 0x1:
        return 2;
    case 0x2:
        return 4;
    default:
        throwIllegal(inst, "invalid store width");
    }
}

uint32_t RVCPU::maskForBytes(uint32_t size) {
    switch (size) {
    case 1:
        return 0x000000ffu;
    case 2:
        return 0x0000ffffu;
    case 4:
        return 0xffffffffu;
    default:
        throw std::invalid_argument("invalid memory access size");
    }
}

float RVCPU::bitsToFloat(uint32_t value) {
    float result = 0.0f;
    static_assert(sizeof(result) == sizeof(value), "unexpected float size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t RVCPU::floatToBits(float value) {
    uint32_t result = 0;
    static_assert(sizeof(result) == sizeof(value), "unexpected float size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

uint32_t RVCPU::classifyFloat(uint32_t value) {
    const bool sign = (value & 0x80000000u) != 0;
    const uint32_t exponent = (value >> 23) & 0xffu;
    const uint32_t fraction = value & 0x007fffffu;

    if (exponent == 0xffu) {
        if (fraction == 0) {
            return sign ? (1u << 0) : (1u << 7);
        }
        return (fraction & 0x00400000u) ? (1u << 9) : (1u << 8);
    }

    if (exponent == 0) {
        if (fraction == 0) {
            return sign ? (1u << 3) : (1u << 4);
        }
        return sign ? (1u << 2) : (1u << 5);
    }

    return sign ? (1u << 1) : (1u << 6);
}

void RVCPU::writeGPR(uint32_t index, uint32_t value) {
    checkRegisterIndex(index);
    if (index != 0) {
        gpr_[index] = value;
    }
}

void RVCPU::checkRegisterIndex(uint32_t index) const {
    if (index >= gpr_.size()) {
        throw std::out_of_range("register index out of range");
    }
}

void RVCPU::enforceGPRZero() {
    gpr_[0] = 0;
}

void RVCPU::throwIllegal(uint32_t inst, const char* reason) {
    std::ostringstream oss;
    oss << "illegal or unsupported instruction 0x" << std::hex << inst
        << ": " << reason;
    throw std::invalid_argument(oss.str());
}

} // namespace zircon
