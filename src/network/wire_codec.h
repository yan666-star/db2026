#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "network/wire_protocol.h"

namespace rmdb::wire {

class WireReader {
 public:
    WireReader(const uint8_t *data, size_t size);
    explicit WireReader(const std::vector<uint8_t> &bytes);

    uint8_t get_u8();
    uint16_t get_u16();
    uint32_t get_u32();
    uint64_t get_u64();
    int32_t get_i32();
    std::vector<uint8_t> get_bytes(size_t count);
    std::string get_string(size_t count);

    size_t remaining() const;
    void require_consumed() const;

 private:
    void require(size_t count) const;

    const uint8_t *data_;
    size_t size_;
    size_t offset_{0};
};

class WireWriter {
 public:
    void put_u8(uint8_t value);
    void put_u16(uint16_t value);
    void put_u32(uint32_t value);
    void put_u64(uint64_t value);
    void put_i32(int32_t value);
    void put_bytes(const uint8_t *data, size_t size);
    void put_bytes(const std::vector<uint8_t> &bytes);
    void put_string(const std::string &value);

    const std::vector<uint8_t> &bytes() const;
    std::vector<uint8_t> take_bytes();
    size_t size() const;

 private:
    std::vector<uint8_t> bytes_;
};

FrameHeader decode_header(const uint8_t *data, size_t size);
std::vector<uint8_t> encode_header(const FrameHeader &header);
std::vector<uint8_t> encode_frame(uint8_t tag, uint8_t flags,
                                  const std::vector<uint8_t> &payload);

void validate_common_header(const FrameHeader &header);
void validate_client_header(const FrameHeader &header);

}  // namespace rmdb::wire
