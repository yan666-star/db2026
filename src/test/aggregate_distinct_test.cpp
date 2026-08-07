#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "execution/executor_aggregation.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class ValuesExecutor final : public AbstractExecutor {
public:
    explicit ValuesExecutor(std::vector<int> values) {
        ColMeta column;
        column.tab_name = "stock";
        column.name = "s_i_id";
        column.type = TYPE_INT;
        column.len = sizeof(int);
        column.offset = 0;
        columns_.push_back(column);
        for (int value : values) {
            auto record = std::make_unique<RmRecord>(sizeof(int));
            std::memcpy(record->data, &value, sizeof(value));
            records_.push_back(std::move(record));
        }
    }

    size_t tupleLen() const override { return sizeof(int); }
    const std::vector<ColMeta> &cols() const override { return columns_; }
    void beginTuple() override { index_ = 0; }
    void nextTuple() override { ++index_; }
    bool is_end() const override { return index_ >= records_.size(); }
    const RmRecord *current_record() const override {
        return is_end() ? nullptr : records_.at(index_).get();
    }
    void enable_bulk_read() override { bulk_read_enabled_ = true; }
    std::unique_ptr<RmRecord> Next() override {
        next_calls_++;
        return std::make_unique<RmRecord>(*records_.at(index_));
    }
    size_t next_calls() const { return next_calls_; }
    bool bulk_read_enabled() const { return bulk_read_enabled_; }
    Rid &rid() override { return rid_; }
    ColMeta get_col_offset(const TabCol &) override { return columns_.front(); }

private:
    std::vector<ColMeta> columns_;
    std::vector<std::unique_ptr<RmRecord>> records_;
    size_t index_{0};
    size_t next_calls_{0};
    bool bulk_read_enabled_{false};
    Rid rid_{};
};

class FloatValuesExecutor final : public AbstractExecutor {
public:
    explicit FloatValuesExecutor(std::vector<float> values) {
        ColMeta column;
        column.tab_name = "order_line";
        column.name = "ol_amount";
        column.type = TYPE_FLOAT;
        column.len = sizeof(float);
        column.offset = 0;
        columns_.push_back(column);
        for (float value : values) {
            auto record = std::make_unique<RmRecord>(sizeof(float));
            std::memcpy(record->data, &value, sizeof(value));
            records_.push_back(std::move(record));
        }
    }

    size_t tupleLen() const override { return sizeof(float); }
    const std::vector<ColMeta> &cols() const override { return columns_; }
    void beginTuple() override { index_ = 0; }
    void nextTuple() override { ++index_; }
    bool is_end() const override { return index_ >= records_.size(); }
    std::unique_ptr<RmRecord> Next() override {
        return std::make_unique<RmRecord>(*records_.at(index_));
    }
    Rid &rid() override { return rid_; }
    ColMeta get_col_offset(const TabCol &) override {
        return columns_.front();
    }

private:
    std::vector<ColMeta> columns_;
    std::vector<std::unique_ptr<RmRecord>> records_;
    size_t index_{0};
    Rid rid_{};
};

int run_count(bool distinct) {
    SelectItem item;
    item.is_agg = true;
    item.agg.type = AGG_COUNT;
    item.agg.col = {"stock", "s_i_id"};
    item.agg.is_distinct = distinct;
    AggregatePlan plan(nullptr, {item}, {}, {}, {}, -1);
    AggregationExecutor executor(
        std::make_unique<ValuesExecutor>(
            std::vector<int>{7, 7, 9, 11, 9, 11, 11}),
        &plan);
    executor.beginTuple();
    require(!executor.is_end(), "scalar COUNT must return one row");
    auto row = executor.Next();
    int result = 0;
    std::memcpy(&result, row->data, sizeof(result));
    executor.nextTuple();
    require(executor.is_end(), "scalar COUNT must return exactly one row");
    return result;
}

void require_aggregate_borrows_scan_records() {
    SelectItem item;
    item.is_agg = true;
    item.agg.type = AGG_COUNT;
    item.agg.col = {"stock", "s_i_id"};
    AggregatePlan plan(nullptr, {item}, {}, {}, {}, -1);

    auto input = std::make_unique<ValuesExecutor>(
        std::vector<int>{1, 2, 3, 4});
    ValuesExecutor *input_view = input.get();
    AggregationExecutor executor(std::move(input), &plan);
    executor.beginTuple();

    require(input_view->next_calls() == 0,
            "aggregation must borrow the scan's current record instead of "
            "allocating an owned copy for every input row");
    require(input_view->bulk_read_enabled(),
            "aggregation must mark its input as a bulk read so an implicit "
            "READ COMMITTED scan can use one table lock");
}

float run_float_sum(const std::vector<float> &values) {
    SelectItem item;
    item.is_agg = true;
    item.agg.type = AGG_SUM;
    item.agg.col = {"order_line", "ol_amount"};
    AggregatePlan plan(nullptr, {item}, {}, {}, {}, -1);
    AggregationExecutor executor(
        std::make_unique<FloatValuesExecutor>(values), &plan);
    executor.beginTuple();
    auto row = executor.Next();
    float result = 0.0F;
    std::memcpy(&result, row->data, sizeof(result));
    return result;
}

void require_seq_scan_owns_local_record_pool(const std::string &source_root) {
    std::ifstream input(source_root + "/src/execution/executor_seq_scan.h");
    std::string source((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
    require(source.find("RmRecordPool record_pool_") != std::string::npos,
            "SeqScanExecutor must own a local record pool");
    require(source.find("context_, &record_pool_") != std::string::npos,
            "page batch reads must consume the local record pool");
    require(source.find("static RmRecordPool") == std::string::npos,
            "record pools must not be global or shared across scans");
}

}  // namespace

int main(int argc, char **argv) {
    require(argc == 2, "aggregate test requires the source root");
    require(run_count(false) == 7, "COUNT(col) must retain every input row");
    require(run_count(true) == 3,
            "COUNT(DISTINCT col) must eliminate join fan-out duplicates");
    require(run_float_sum({16777216.0F, 1.0F, 1.0F}) == 16777218.0F,
            "SUM(FLOAT) must accumulate binary32 inputs in binary64 and "
            "round once");
    require_aggregate_borrows_scan_records();
    require_seq_scan_owns_local_record_pool(argv[1]);
    std::cout << "aggregate distinct tests passed\n";
    return 0;
}
