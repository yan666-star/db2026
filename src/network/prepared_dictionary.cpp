#include "network/prepared_dictionary.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <utility>

#include "network/wire_codec.h"

namespace rmdb::wire {
namespace {

SqlType decode_sql_type(uint8_t raw) {
    switch (static_cast<SqlType>(raw)) {
        case SqlType::INT32:
        case SqlType::FLOAT32:
        case SqlType::CHAR:
            return static_cast<SqlType>(raw);
    }
    throw ProtocolError("unknown prepared parameter SQL type");
}

ResultKind decode_result_kind(uint8_t raw) {
    switch (static_cast<ResultKind>(raw)) {
        case ResultKind::COMMAND:
        case ResultKind::QUERY:
            return static_cast<ResultKind>(raw);
    }
    throw ProtocolError("unknown prepared result kind");
}

void validate_entries(const std::vector<PrepareEntry> &entries) {
    if (entries.empty() || entries.size() > 256U) {
        throw ProtocolError("PREPARE_SET statement count must be 1..=256");
    }

    std::unordered_set<uint16_t> ids;
    ids.reserve(entries.size());
    for (const auto &entry : entries) {
        if (entry.statement_id == 0U) {
            throw ProtocolError("prepared statement id must be non-zero");
        }
        if (!ids.insert(entry.statement_id).second) {
            throw ProtocolError("duplicate prepared statement id");
        }
        if (entry.sql.empty()) {
            throw ProtocolError("prepared SQL must not be empty");
        }
        if (entry.parameter_types.size() >
            static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
            throw ProtocolError("prepared parameter count exceeds u16");
        }
        validate_parameter_markers(
            entry.sql, static_cast<uint16_t>(entry.parameter_types.size()));
    }
}

}  // namespace

std::vector<uint16_t> scan_parameter_markers(const std::string &sql) {
    std::unordered_set<uint16_t> seen;
    bool in_string = false;

    for (size_t index = 0; index < sql.size();) {
        const char current = sql[index];
        if (current == '\'') {
            if (in_string && index + 1U < sql.size() &&
                sql[index + 1U] == '\'') {
                index += 2U;
                continue;
            }
            in_string = !in_string;
            index++;
            continue;
        }
        if (in_string || current != '$') {
            index++;
            continue;
        }

        const size_t digit_begin = index + 1U;
        if (digit_begin >= sql.size() ||
            !std::isdigit(static_cast<unsigned char>(sql[digit_begin]))) {
            index++;
            continue;
        }

        uint32_t ordinal = 0;
        size_t cursor = digit_begin;
        while (cursor < sql.size() &&
               std::isdigit(static_cast<unsigned char>(sql[cursor]))) {
            ordinal = ordinal * 10U +
                      static_cast<uint32_t>(sql[cursor] - '0');
            if (ordinal > std::numeric_limits<uint16_t>::max()) {
                throw ProtocolError("prepared parameter ordinal exceeds u16");
            }
            cursor++;
        }
        if (ordinal == 0U) {
            throw ProtocolError("prepared parameter ordinals start at one");
        }
        seen.insert(static_cast<uint16_t>(ordinal));
        index = cursor;
    }

    if (in_string) {
        throw ProtocolError("unterminated string in prepared SQL");
    }

    std::vector<uint16_t> result(seen.begin(), seen.end());
    std::sort(result.begin(), result.end());
    return result;
}

void validate_parameter_markers(const std::string &sql,
                                uint16_t parameter_count) {
    const auto ordinals = scan_parameter_markers(sql);
    if (ordinals.size() != static_cast<size_t>(parameter_count)) {
        throw ProtocolError(
            "prepared marker set does not match parameter count");
    }
    for (uint16_t index = 0; index < parameter_count; ++index) {
        if (ordinals[index] != static_cast<uint16_t>(index + 1U)) {
            throw ProtocolError(
                "prepared parameter markers must form a dense set");
        }
    }
}

std::vector<PrepareEntry> decode_prepare_set(
    const std::vector<uint8_t> &payload) {
    WireReader reader(payload);
    const uint16_t statement_count = reader.get_u16();
    if (statement_count == 0U || statement_count > 256U) {
        throw ProtocolError("PREPARE_SET statement count must be 1..=256");
    }

    std::vector<PrepareEntry> entries;
    entries.reserve(statement_count);
    for (uint16_t index = 0; index < statement_count; ++index) {
        PrepareEntry entry;
        entry.statement_id = reader.get_u16();
        entry.result_kind = decode_result_kind(reader.get_u8());

        const uint16_t parameter_count = reader.get_u16();
        entry.parameter_types.reserve(parameter_count);
        for (uint16_t parameter = 0; parameter < parameter_count;
             ++parameter) {
            entry.parameter_types.push_back(decode_sql_type(reader.get_u8()));
        }

        const uint32_t sql_bytes = reader.get_u32();
        entry.sql = reader.get_string(sql_bytes);
        require_valid_utf8(entry.sql, "prepared SQL");
        entries.push_back(std::move(entry));
    }
    reader.require_consumed();
    validate_entries(entries);
    return entries;
}

void PreparedDictionary::install_atomically(
    const std::vector<PrepareEntry> &entries,
    const PrepareCallback &prepare) {
    if (!prepare) {
        throw ProtocolError("prepared statement callback is missing");
    }
    validate_entries(entries);

    std::vector<PreparedStatement> replacement;
    replacement.reserve(entries.size());
    std::unordered_map<uint16_t, size_t> replacement_index;
    replacement_index.reserve(entries.size());

    for (const auto &entry : entries) {
        auto schema = prepare(entry);
        if (entry.result_kind == ResultKind::QUERY && schema.empty()) {
            throw ProtocolError(
                "prepared query must expose at least one result column");
        }
        if (entry.result_kind == ResultKind::COMMAND && !schema.empty()) {
            throw ProtocolError(
                "prepared command must expose zero result columns");
        }

        replacement_index.emplace(entry.statement_id, replacement.size());
        replacement.push_back(
            {entry.statement_id,
             entry.result_kind,
             entry.parameter_types,
             entry.sql,
             std::move(schema)});
    }

    statements_.swap(replacement);
    by_id_.swap(replacement_index);
}

const PreparedStatement *PreparedDictionary::find(
    uint16_t statement_id) const {
    const auto found = by_id_.find(statement_id);
    if (found == by_id_.end()) {
        return nullptr;
    }
    return &statements_[found->second];
}

const std::vector<PreparedStatement> &
PreparedDictionary::in_request_order() const noexcept {
    return statements_;
}

size_t PreparedDictionary::size() const noexcept {
    return statements_.size();
}

}  // namespace rmdb::wire
