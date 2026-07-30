#include "network/wire_codec.h"

#include <cstring>
#include <limits>
#include <utility>

namespace rmdb::wire {

WireReader::WireReader(const uint8_t *data, size_t size)
    : data_(data), size_(size) {
    if (data_ == nullptr && size_ != 0) {
        throw ProtocolError("null payload with non-zero length");
    }
}

WireReader::WireReader(const std::vector<uint8_t> &bytes)
    : WireReader(bytes.data(), bytes.size()) {}

void WireReader::require(size_t count) const {
    if (count > size_ - offset_) {
        throw ProtocolError("truncated wire payload");
    }
}

uint8_t WireReader::get_u8() {
    require(1);
    return data_[offset_++];
}

uint16_t WireReader::get_u16() {
    require(2);
    const uint16_t value =
        (static_cast<uint16_t>(data_[offset_]) << 8U) |
        static_cast<uint16_t>(data_[offset_ + 1]);
    offset_ += 2;
    return value;
}

uint32_t WireReader::get_u32() {
    require(4);
    const uint32_t value =
        (static_cast<uint32_t>(data_[offset_]) << 24U) |
        (static_cast<uint32_t>(data_[offset_ + 1]) << 16U) |
        (static_cast<uint32_t>(data_[offset_ + 2]) << 8U) |
        static_cast<uint32_t>(data_[offset_ + 3]);
    offset_ += 4;
    return value;
}

uint64_t WireReader::get_u64() {
    require(8);
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value = (value << 8U) | static_cast<uint64_t>(data_[offset_ + i]);
    }
    offset_ += 8;
    return value;
}

int32_t WireReader::get_i32() {
    const uint32_t raw = get_u32();
    int32_t value = 0;
    static_assert(sizeof(value) == sizeof(raw), "INT32 must be 32 bits");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::vector<uint8_t> WireReader::get_bytes(size_t count) {
    require(count);
    std::vector<uint8_t> value(data_ + offset_, data_ + offset_ + count);
    offset_ += count;
    return value;
}

std::string WireReader::get_string(size_t count) {
    require(count);
    std::string value(reinterpret_cast<const char *>(data_ + offset_), count);
    offset_ += count;
    return value;
}

size_t WireReader::remaining() const { return size_ - offset_; }

void WireReader::require_consumed() const {
    if (remaining() != 0) {
        throw ProtocolError("unexplained trailing wire payload bytes");
    }
}

void WireWriter::put_u8(uint8_t value) { bytes_.push_back(value); }

void WireWriter::put_u16(uint16_t value) {
    bytes_.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    bytes_.push_back(static_cast<uint8_t>(value & 0xffU));
}

void WireWriter::put_u32(uint32_t value) {
    bytes_.push_back(static_cast<uint8_t>((value >> 24U) & 0xffU));
    bytes_.push_back(static_cast<uint8_t>((value >> 16U) & 0xffU));
    bytes_.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    bytes_.push_back(static_cast<uint8_t>(value & 0xffU));
}

void WireWriter::put_u64(uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes_.push_back(
            static_cast<uint8_t>((value >> static_cast<unsigned>(shift)) &
                                 0xffU));
    }
}

void WireWriter::put_i32(int32_t value) {
    uint32_t raw = 0;
    static_assert(sizeof(value) == sizeof(raw), "INT32 must be 32 bits");
    std::memcpy(&raw, &value, sizeof(raw));
    put_u32(raw);
}

void WireWriter::put_bytes(const uint8_t *data, size_t size) {
    if (data == nullptr && size != 0) {
        throw ProtocolError("null bytes with non-zero length");
    }
    if (size > kMaxPayloadBytes ||
        bytes_.size() > kMaxPayloadBytes - size) {
        throw ProtocolError("wire payload exceeds 1 MiB");
    }
    bytes_.insert(bytes_.end(), data, data + size);
}

void WireWriter::put_bytes(const std::vector<uint8_t> &bytes) {
    put_bytes(bytes.data(), bytes.size());
}

void WireWriter::put_string(const std::string &value) {
    put_bytes(reinterpret_cast<const uint8_t *>(value.data()), value.size());
}

const std::vector<uint8_t> &WireWriter::bytes() const { return bytes_; }

std::vector<uint8_t> WireWriter::take_bytes() { return std::move(bytes_); }

size_t WireWriter::size() const { return bytes_.size(); }

void validate_common_header(const FrameHeader &header) {
    if (header.payload_bytes > kMaxPayloadBytes) {
        throw ProtocolError("frame payload exceeds 1 MiB");
    }
    if (header.reserved != 0) {
        throw ProtocolError("frame reserved field must be zero");
    }
}

void validate_client_header(const FrameHeader &header) {
    validate_common_header(header);
    switch (static_cast<ClientTag>(header.tag)) {
        case ClientTag::EXEC_STREAM:
        case ClientTag::PREPARE_SET:
            if (header.flags != 0) {
                throw ProtocolError(
                    "EXEC_STREAM/PREPARE_SET flags must be zero");
            }
            return;
        case ClientTag::EXEC_BATCH:
            if (header.flags != kExecBatchAutoAbort) {
                throw ProtocolError(
                    "EXEC_BATCH flags must contain only AUTO_ABORT");
            }
            return;
    }
    throw ProtocolError("unknown client frame tag");
}

FrameHeader decode_header(const uint8_t *data, size_t size) {
    if (size != kFrameHeaderBytes) {
        throw ProtocolError("frame header must be exactly 8 bytes");
    }
    WireReader reader(data, size);
    FrameHeader header;
    header.payload_bytes = reader.get_u32();
    header.tag = reader.get_u8();
    header.flags = reader.get_u8();
    header.reserved = reader.get_u16();
    reader.require_consumed();
    validate_common_header(header);
    return header;
}

std::vector<uint8_t> encode_header(const FrameHeader &header) {
    validate_common_header(header);
    WireWriter writer;
    writer.put_u32(header.payload_bytes);
    writer.put_u8(header.tag);
    writer.put_u8(header.flags);
    writer.put_u16(header.reserved);
    return writer.take_bytes();
}

std::vector<uint8_t> encode_frame(uint8_t tag, uint8_t flags,
                                  const std::vector<uint8_t> &payload) {
    if (payload.size() > kMaxPayloadBytes) {
        throw ProtocolError("frame payload exceeds 1 MiB");
    }
    FrameHeader header{static_cast<uint32_t>(payload.size()), tag, flags, 0};
    WireWriter writer;
    const auto encoded_header = encode_header(header);
    writer.put_bytes(encoded_header);
    writer.put_bytes(payload);
    return writer.take_bytes();
}

}  // namespace rmdb::wire
