#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "network/request_dispatcher.h"
#include "network/wire_codec.h"
#include "transaction/transaction_write_batch.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class Executable final : public rmdb::wire::PreparedExecutable {};

class StorageBatchService final : public rmdb::wire::ExecutionService {
   public:
    rmdb::wire::PreparedArtifact prepare(
        const rmdb::wire::PrepareEntry &) override {
        return {{}, std::make_shared<Executable>()};
    }
    void execute_stream(const std::string &,
                        rmdb::execution::ResultSink &) override {}
    void execute_prepared(
        const rmdb::wire::PreparedStatement &statement,
        const std::vector<rmdb::execution::TypedValue> &,
        rmdb::execution::ResultSink &sink) override {
        if (statement.statement_id == 1) {
            active = true;
            batch.stage_insert("items", 1, std::vector<char>(4, 7));
            sink.command_ok();
            return;
        }
        throw std::runtime_error("injected batch failure");
    }
    bool has_active_transaction() const override { return active; }
    void abort_active_transaction() override {
        batch.discard();
        active = false;
        rollback_finished = true;
    }

    TransactionWriteBatch batch;
    bool active{false};
    bool rollback_finished{false};
};

}  // namespace

int main() {
    StorageBatchService service;
    rmdb::wire::RequestDispatcher dispatcher(service);
    rmdb::wire::WireWriter prepare;
    prepare.put_u16(2);
    for (uint16_t id = 1; id <= 2; ++id) {
        prepare.put_u16(id);
        prepare.put_u8(0);
        prepare.put_u16(0);
        prepare.put_string_u32("statement");
    }
    dispatcher.dispatch(
        {static_cast<uint32_t>(prepare.size()),
         static_cast<uint8_t>(rmdb::wire::ClientTag::PREPARE_SET), 0, 0},
        prepare.bytes(), [](const std::vector<uint8_t> &) {});

    rmdb::wire::WireWriter batch;
    batch.put_u16(2);
    batch.put_u16(1);
    batch.put_u16(2);
    bool emitted = false;
    dispatcher.dispatch(
        {static_cast<uint32_t>(batch.size()),
         static_cast<uint8_t>(rmdb::wire::ClientTag::EXEC_BATCH),
         rmdb::wire::kExecBatchAutoAbort, 0},
        batch.bytes(), [&](const std::vector<uint8_t> &) {
            require(service.rollback_finished,
                    "AUTO_ABORT replied before rollback completion");
            require(service.batch.writes().empty(),
                    "AUTO_ABORT failure left staged storage writes");
            emitted = true;
        });
    require(emitted, "AUTO_ABORT failure frame was not emitted");
    std::cout << "AUTO_ABORT storage batch tests passed\n";
}
