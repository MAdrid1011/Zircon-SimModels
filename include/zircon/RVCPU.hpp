#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace zircon {

enum class StepStatus {
    Retired,
    NeedMemory
};

enum class MemOp {
    Load,
    Store
};

struct MemRequest {
    MemOp op = MemOp::Load;
    uint32_t addr = 0;
    uint32_t wdata = 0;
    uint32_t size = 0;
};

struct StepResult {
    StepStatus status = StepStatus::Retired;
    MemRequest memReq;
};

class RVCPU {
public:
    RVCPU();
    explicit RVCPU(uint32_t initialPc);

    void reset(uint32_t initialPc = 0);

    StepResult step(uint32_t inst);
    void finishMemory(uint32_t rdata = 0);

    uint32_t getPC() const;
    void setPC(uint32_t value);

    uint32_t getGPR(uint32_t index) const;
    void setGPR(uint32_t index, uint32_t value);

    uint32_t getFPRBits(uint32_t index) const;
    void setFPRBits(uint32_t index, uint32_t value);

    bool hasPendingMemory() const;

    static uint32_t bits(uint32_t value, uint32_t lo, uint32_t hi);
    static uint32_t signExtend(uint32_t value, uint32_t width);
    static uint32_t zeroExtend(uint32_t value, uint32_t width);

private:
    enum class PendingTarget {
        None,
        GPR,
        FPR
    };

    struct PendingMemory {
        MemOp op = MemOp::Load;
        PendingTarget target = PendingTarget::None;
        uint32_t rd = 0;
        uint32_t size = 0;
        bool signExtend = false;
        uint32_t nextPc = 0;
    };

    StepResult executeRType(uint32_t inst);
    StepResult executeIType(uint32_t inst);
    StepResult executeBType(uint32_t inst);
    StepResult executeSType(uint32_t inst);
    StepResult executeJType(uint32_t inst);
    StepResult executeUType(uint32_t inst);
    StepResult executeFType(uint32_t inst);

    StepResult retire(uint32_t nextPc);
    StepResult needMemory(const MemRequest& request, const PendingMemory& pending);

    static uint32_t opcode(uint32_t inst);
    static uint32_t rd(uint32_t inst);
    static uint32_t rs1(uint32_t inst);
    static uint32_t rs2(uint32_t inst);
    static uint32_t rs3(uint32_t inst);
    static uint32_t funct3(uint32_t inst);
    static uint32_t funct7(uint32_t inst);
    static int32_t decodeIImm(uint32_t inst);
    static int32_t decodeSImm(uint32_t inst);
    static int32_t decodeBImm(uint32_t inst);
    static int32_t decodeJImm(uint32_t inst);
    static uint32_t decodeUImm(uint32_t inst);
    static uint32_t decodeLoadSize(uint32_t inst);
    static bool decodeLoadSigned(uint32_t inst);
    static uint32_t decodeStoreSize(uint32_t inst);
    static uint32_t maskForBytes(uint32_t size);

    static float bitsToFloat(uint32_t value);
    static uint32_t floatToBits(float value);
    static uint32_t classifyFloat(uint32_t value);

    void writeGPR(uint32_t index, uint32_t value);
    void checkRegisterIndex(uint32_t index) const;
    void enforceGPRZero();

    [[noreturn]] static void throwIllegal(uint32_t inst, const char* reason);

    uint32_t pc_ = 0;
    std::array<uint32_t, 32> gpr_{};
    std::array<uint32_t, 32> fpr_{};
    std::optional<PendingMemory> pendingMem_;
};

} // namespace zircon
