#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "execution/execution_result.h"

namespace rmdb::wire {

enum class ResultKind : uint8_t {
    COMMAND = 0,
    QUERY = 1,
};

struct PrepareEntry {
    uint16_t statement_id;
    ResultKind result_kind;
    std::vector<SqlType> parameter_types;
    std::string sql;
};

struct PreparedStatement {
    uint16_t statement_id;
    ResultKind result_kind;
    std::vector<SqlType> parameter_types;
    std::string sql;
    std::vector<execution::OutputColumn> output_schema;
};

using PrepareCallback = std::function<std::vector<execution::OutputColumn>(
    const PrepareEntry &entry)>;

std::vector<uint16_t> scan_parameter_markers(const std::string &sql);
void validate_parameter_markers(const std::string &sql,
                                uint16_t parameter_count);
std::vector<PrepareEntry> decode_prepare_set(
    const std::vector<uint8_t> &payload);

class PreparedDictionary {
 public:
    void install_atomically(const std::vector<PrepareEntry> &entries,
                            const PrepareCallback &prepare);

    const PreparedStatement *find(uint16_t statement_id) const;
    const std::vector<PreparedStatement> &in_request_order() const noexcept;
    size_t size() const noexcept;

 private:
    std::vector<PreparedStatement> statements_;
    std::unordered_map<uint16_t, size_t> by_id_;
};

}  // namespace rmdb::wire
