#include "zircon/DDR.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace zircon {

namespace {

bool addOverflows(uint32_t base, uint32_t offset) {
    return base > std::numeric_limits<uint32_t>::max() - offset;
}

} // namespace

DDR::DDR()
    : DDR(Config{}) {
}

DDR::DDR(Config config)
    : config_(config) {
    validateDataWidth(config_.nativeDataBits, 32, "nativeDataBits");
    validateDataWidth(config_.axiDataBits, std::numeric_limits<uint32_t>::max(), "axiDataBits");

    if (config_.sizeBytes == 0) {
        throw std::invalid_argument("DDR sizeBytes must be nonzero");
    }

    nativeBeatBytes_ = config_.nativeDataBits / 8;
    axiBeatBytes_ = config_.axiDataBits / 8;
    memory_.assign(config_.sizeBytes, 0);
    resetImage_ = memory_;
}

void DDR::reset() {
    memory_ = resetImage_;
    writeActive_ = false;
    writeTransaction_ = AxiTransaction{};
    writeResponseValid_ = false;
    writeResponse_ = AxiWriteResponse{};
    readActive_ = false;
    readTransaction_ = AxiTransaction{};
}

std::size_t DDR::sizeBytes() const {
    return memory_.size();
}

uint32_t DDR::nativeDataBits() const {
    return config_.nativeDataBits;
}

uint32_t DDR::nativeBeatBytes() const {
    return nativeBeatBytes_;
}

uint32_t DDR::axiDataBits() const {
    return config_.axiDataBits;
}

uint32_t DDR::axiBeatBytes() const {
    return axiBeatBytes_;
}

DDR::NativeResponse DDR::tickNative(const NativeRequest& request) {
    NativeResponse response;
    response.ready = true;

    if (!request.valid) {
        return response;
    }

    if (!nativeAccessValid(request.addr, request.size)) {
        response.error = true;
        response.rvalid = !request.write;
        return response;
    }

    if (request.write) {
        write(request.addr, request.wdata, request.size, request.wstrb);
    } else {
        response.rvalid = true;
        response.rdata = read(request.addr, request.size);
    }

    return response;
}

DDR::Axi4Output DDR::tickAxi4(const Axi4Input& input) {
    Axi4Output output;
    output.rData.assign(axiBeatBytes_, 0);
    output.awReady = !writeActive_ && !writeResponseValid_;
    output.wReady = writeActive_;
    output.arReady = !readActive_;

    if (writeResponseValid_) {
        output.bValid = true;
        output.bId = writeResponse_.id;
        output.bResp = writeResponse_.resp;
        if (input.bReady) {
            writeResponseValid_ = false;
            writeResponse_ = AxiWriteResponse{};
        }
    }

    if (readActive_) {
        output.rValid = true;
        output.rId = readTransaction_.id;
        output.rResp = readTransaction_.resp;
        output.rLast = isLastBeat(readTransaction_);
        if (readTransaction_.resp == AxiResp::Okay) {
            output.rData = readAxiBeat(readTransaction_);
        }

        if (input.rReady) {
            if (isLastBeat(readTransaction_)) {
                readActive_ = false;
                readTransaction_ = AxiTransaction{};
            } else {
                advanceAxiTransaction(readTransaction_);
            }
        }
    }

    if (input.aw.valid && output.awReady) {
        writeActive_ = true;
        writeTransaction_ = AxiTransaction{
            input.aw.id,
            input.aw.addr,
            input.aw.len,
            input.aw.size,
            input.aw.burst,
            0,
            validateAxiAddress(input.aw)
        };
    }

    if (input.w.valid && output.wReady) {
        const bool lastBeat = isLastBeat(writeTransaction_);
        if (input.w.last != lastBeat) {
            writeTransaction_.resp = AxiResp::SlvErr;
        }

        if (writeTransaction_.resp == AxiResp::Okay) {
            try {
                writeAxiBeat(writeTransaction_, input.w);
            } catch (const std::exception&) {
                writeTransaction_.resp = AxiResp::SlvErr;
            }
        }

        if (lastBeat) {
            writeResponseValid_ = true;
            writeResponse_ = AxiWriteResponse{writeTransaction_.id, writeTransaction_.resp};
            writeActive_ = false;
            writeTransaction_ = AxiTransaction{};
        } else {
            advanceAxiTransaction(writeTransaction_);
        }
    }

    if (input.ar.valid && output.arReady) {
        readActive_ = true;
        readTransaction_ = AxiTransaction{
            input.ar.id,
            input.ar.addr,
            input.ar.len,
            input.ar.size,
            input.ar.burst,
            0,
            validateAxiAddress(input.ar)
        };
    }

    return output;
}

uint32_t DDR::read(uint32_t addr, uint32_t size) const {
    if (!directAccessValid(addr, size)) {
        throw std::out_of_range("DDR read is out of range");
    }

    uint32_t value = 0;
    for (uint32_t index = 0; index < size; ++index) {
        value |= static_cast<uint32_t>(memory_[addr + index]) << (index * 8);
    }
    return value;
}

void DDR::write(uint32_t addr, uint32_t value, uint32_t size, uint32_t wstrb) {
    if (!directAccessValid(addr, size)) {
        throw std::out_of_range("DDR write is out of range");
    }

    for (uint32_t index = 0; index < size; ++index) {
        if (((wstrb >> index) & 1u) != 0) {
            memory_[addr + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xffu);
        }
    }
}

const std::vector<uint8_t>& DDR::data() const {
    return memory_;
}

std::vector<uint8_t>& DDR::data() {
    return memory_;
}

bool DDR::isPowerOfTwo(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void DDR::validateDataWidth(uint32_t bits, uint32_t maxBits, const char* name) {
    if (bits < 8 || bits > maxBits || (bits % 8) != 0 || !isPowerOfTwo(bits)) {
        throw std::invalid_argument(std::string(name) + " must be a power-of-two byte width");
    }
}

uint32_t DDR::bytesFromAxiSize(uint8_t size) {
    if (size >= 31) {
        throw std::invalid_argument("AXI4 size field is too large");
    }
    return 1u << size;
}

uint32_t DDR::maskForBytes(uint32_t size) {
    if (size == 0 || size > 4) {
        throw std::invalid_argument("native access size must be 1, 2, or 4 bytes");
    }
    return (size == 4) ? 0xffffffffu : ((1u << (size * 8)) - 1u);
}

bool DDR::nativeAccessValid(uint32_t addr, uint32_t size) const {
    if (size == 0 || size > nativeBeatBytes_ || size > 4 || !isPowerOfTwo(size)) {
        return false;
    }
    return directAccessValid(addr, size);
}

bool DDR::directAccessValid(uint32_t addr, uint32_t size) const {
    if (size == 0 || size > 4 || !isPowerOfTwo(size)) {
        return false;
    }
    const std::size_t start = addr;
    const std::size_t end = start + size;
    return start <= memory_.size() && end <= memory_.size() && end >= start;
}

DDR::AxiResp DDR::validateAxiAddress(const Axi4Address& address) const {
    uint32_t bytes = 0;
    try {
        bytes = bytesFromAxiSize(address.size);
    } catch (const std::exception&) {
        return AxiResp::SlvErr;
    }

    if (bytes == 0 || bytes > axiBeatBytes_) {
        return AxiResp::SlvErr;
    }

    if (address.burst != AxiBurst::Fixed &&
        address.burst != AxiBurst::Incrementing &&
        address.burst != AxiBurst::Wrapping) {
        return AxiResp::SlvErr;
    }

    const uint32_t beats = static_cast<uint32_t>(address.len) + 1;
    if (address.burst == AxiBurst::Wrapping) {
        const bool legalWrapBeats = beats == 2 || beats == 4 || beats == 8 || beats == 16;
        const uint32_t wrapBytes = beats * bytes;
        if (!legalWrapBeats || (address.addr % wrapBytes) != 0) {
            return AxiResp::SlvErr;
        }
    }

    AxiTransaction transaction{
        address.id,
        address.addr,
        address.len,
        address.size,
        address.burst,
        0,
        AxiResp::Okay
    };

    for (uint32_t beat = 0; beat < beats; ++beat) {
        transaction.beat = beat;
        const uint32_t addr = beatAddress(transaction);
        if (addOverflows(addr, bytes) ||
            static_cast<std::size_t>(addr) + bytes > memory_.size()) {
            return AxiResp::DecErr;
        }
    }

    return AxiResp::Okay;
}

uint32_t DDR::beatAddress(const AxiTransaction& transaction) const {
    const uint32_t bytes = beatBytes(transaction);
    const uint32_t offset = transaction.beat * bytes;

    if (transaction.burst == AxiBurst::Fixed) {
        return transaction.addr;
    }

    if (transaction.burst == AxiBurst::Incrementing) {
        return transaction.addr + offset;
    }

    const uint32_t beats = static_cast<uint32_t>(transaction.len) + 1;
    const uint32_t wrapBytes = beats * bytes;
    const uint32_t wrapBase = transaction.addr - (transaction.addr % wrapBytes);
    return wrapBase + ((transaction.addr + offset - wrapBase) % wrapBytes);
}

uint32_t DDR::beatBytes(const AxiTransaction& transaction) const {
    return bytesFromAxiSize(transaction.size);
}

bool DDR::isLastBeat(const AxiTransaction& transaction) const {
    return transaction.beat == static_cast<uint32_t>(transaction.len);
}

void DDR::advanceAxiTransaction(AxiTransaction& transaction) {
    ++transaction.beat;
}

void DDR::writeAxiBeat(const AxiTransaction& transaction, const Axi4WriteData& data) {
    const uint32_t bytes = beatBytes(transaction);
    const uint32_t addr = beatAddress(transaction);
    const uint32_t lane = addr % axiBeatBytes_;

    if (lane + bytes > data.data.size()) {
        throw std::invalid_argument("AXI4 write data vector is too small");
    }

    for (uint32_t index = 0; index < bytes; ++index) {
        const uint32_t laneIndex = lane + index;
        const bool enabled = data.strb.empty() ||
            (laneIndex < data.strb.size() && data.strb[laneIndex] != 0);
        if (enabled) {
            memory_[addr + index] = data.data[laneIndex];
        }
    }
}

std::vector<uint8_t> DDR::readAxiBeat(const AxiTransaction& transaction) const {
    std::vector<uint8_t> data(axiBeatBytes_, 0);
    const uint32_t bytes = beatBytes(transaction);
    const uint32_t addr = beatAddress(transaction);
    const uint32_t lane = addr % axiBeatBytes_;

    for (uint32_t index = 0; index < bytes; ++index) {
        data[lane + index] = memory_[addr + index];
    }
    return data;
}

} // namespace zircon
