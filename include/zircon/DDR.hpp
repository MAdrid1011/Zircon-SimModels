#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zircon {

class DDR {
public:
    struct Config {
        std::size_t sizeBytes = 1024 * 1024;
        uint32_t nativeDataBits = 32;
        uint32_t axiDataBits = 32;
    };

    struct NativeRequest {
        bool valid = false;
        bool write = false;
        uint32_t addr = 0;
        uint32_t wdata = 0;
        uint32_t wstrb = 0xffffffffu;
        uint32_t size = 4;
    };

    struct NativeResponse {
        bool ready = false;
        bool rvalid = false;
        uint32_t rdata = 0;
        bool error = false;
    };

    enum class AxiBurst : uint8_t {
        Fixed = 0,
        Incrementing = 1,
        Wrapping = 2
    };

    enum class AxiResp : uint8_t {
        Okay = 0,
        ExOkay = 1,
        SlvErr = 2,
        DecErr = 3
    };

    struct Axi4Address {
        bool valid = false;
        uint32_t id = 0;
        uint32_t addr = 0;
        uint8_t len = 0;
        uint8_t size = 2;
        AxiBurst burst = AxiBurst::Incrementing;
    };

    struct Axi4WriteData {
        bool valid = false;
        std::vector<uint8_t> data;
        std::vector<uint8_t> strb;
        bool last = false;
    };

    struct Axi4Input {
        Axi4Address aw;
        Axi4WriteData w;
        bool bReady = true;
        Axi4Address ar;
        bool rReady = true;
    };

    struct Axi4Output {
        bool awReady = false;
        bool wReady = false;
        bool bValid = false;
        uint32_t bId = 0;
        AxiResp bResp = AxiResp::Okay;
        bool arReady = false;
        bool rValid = false;
        uint32_t rId = 0;
        std::vector<uint8_t> rData;
        AxiResp rResp = AxiResp::Okay;
        bool rLast = false;
    };

    DDR();
    explicit DDR(Config config);

    void reset();

    std::size_t sizeBytes() const;
    uint32_t nativeDataBits() const;
    uint32_t nativeBeatBytes() const;
    uint32_t axiDataBits() const;
    uint32_t axiBeatBytes() const;

    NativeResponse tickNative(const NativeRequest& request);
    Axi4Output tickAxi4(const Axi4Input& input);

    uint32_t read(uint32_t addr, uint32_t size) const;
    void write(uint32_t addr, uint32_t data, uint32_t size, uint32_t wstrb = 0xffffffffu);

    const std::vector<uint8_t>& data() const;
    std::vector<uint8_t>& data();

private:
    struct AxiTransaction {
        uint32_t id = 0;
        uint32_t addr = 0;
        uint8_t len = 0;
        uint8_t size = 0;
        AxiBurst burst = AxiBurst::Incrementing;
        uint32_t beat = 0;
        AxiResp resp = AxiResp::Okay;
    };

    struct AxiWriteResponse {
        uint32_t id = 0;
        AxiResp resp = AxiResp::Okay;
    };

    static bool isPowerOfTwo(uint32_t value);
    static void validateDataWidth(uint32_t bits, uint32_t maxBits, const char* name);
    static uint32_t bytesFromAxiSize(uint8_t size);
    static uint32_t maskForBytes(uint32_t size);

    bool nativeAccessValid(uint32_t addr, uint32_t size) const;
    bool directAccessValid(uint32_t addr, uint32_t size) const;

    AxiResp validateAxiAddress(const Axi4Address& address) const;
    uint32_t beatAddress(const AxiTransaction& transaction) const;
    uint32_t beatBytes(const AxiTransaction& transaction) const;
    bool isLastBeat(const AxiTransaction& transaction) const;
    void advanceAxiTransaction(AxiTransaction& transaction);
    void writeAxiBeat(const AxiTransaction& transaction, const Axi4WriteData& data);
    std::vector<uint8_t> readAxiBeat(const AxiTransaction& transaction) const;

    Config config_;
    uint32_t nativeBeatBytes_ = 4;
    uint32_t axiBeatBytes_ = 4;
    std::vector<uint8_t> memory_;
    std::vector<uint8_t> resetImage_;
    bool writeActive_ = false;
    AxiTransaction writeTransaction_;
    bool writeResponseValid_ = false;
    AxiWriteResponse writeResponse_;
    bool readActive_ = false;
    AxiTransaction readTransaction_;
};

} // namespace zircon
