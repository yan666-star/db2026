#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

size_t count_occurrences(const std::string &text,
                         const std::string &needle) {
    size_t count = 0;
    size_t cursor = 0;
    while ((cursor = text.find(needle, cursor)) != std::string::npos) {
        ++count;
        cursor += needle.size();
    }
    return count;
}

}  // namespace

int main(int argc, char **argv) {
    require(argc == 2, "expected repository source directory");
    const std::filesystem::path root(argv[1]);
    const std::string manager = read_file(
        root / "src" / "transaction" / "transaction_manager.cpp");
    const std::string portal = read_file(root / "src" / "portal.h");
    const std::string delete_executor = read_file(
        root / "src" / "execution" / "executor_delete.h");
    const std::string update_executor = read_file(
        root / "src" / "execution" / "executor_update.h");

    const size_t prepare_begin =
        manager.find("void TransactionManager::prepare_write(");
    const size_t prepare_end =
        manager.find("void TransactionManager::check_commit_conflict_under_latch",
                     prepare_begin);
    require(prepare_begin != std::string::npos &&
                prepare_end != std::string::npos,
            "prepare_write source range missing");
    const std::string prepare =
        manager.substr(prepare_begin, prepare_end - prepare_begin);
    const size_t prospective =
        prepare.find("MvccVersion prospective;");
    const size_t ssi_check = prepare.find(
        "check_new_rw_dependency_or_abort(txn, reader_id, writer_id);");
    const size_t pending_install =
        prepare.find("history.push_back(std::move(prospective));");
    require(prospective != std::string::npos &&
                ssi_check != std::string::npos &&
                pending_install != std::string::npos,
            "prospective SSI write contract missing");
    require(prospective < ssi_check && ssi_check < pending_install,
            "SSI danger check must precede pending version installation");

    const size_t point_read =
        manager.find("void TransactionManager::register_record_read(");
    const size_t prepare_insert =
        manager.find("void TransactionManager::prepare_insert(", point_read);
    require(point_read != std::string::npos &&
                prepare_insert != std::string::npos,
            "record-read source range missing");
    const std::string point_read_body =
        manager.substr(point_read, prepare_insert - point_read);
    require(point_read_body.find("check_new_rw_dependency_or_abort(") !=
                std::string::npos,
            "point reads must add rw dependencies to existing writers");

    const size_t fixed_victim =
        manager.find("txn_id_t victim = current_txn->get_transaction_id();");
    const size_t immediate_throw = manager.find(
        "throw TransactionAbortException(victim", fixed_victim);
    require(fixed_victim != std::string::npos &&
                immediate_throw != std::string::npos,
            "SSI must abort the current statement transaction immediately");

    require(count_occurrences(
                portal, "x->subplan_, context, nullptr, true, true);") == 2,
            "UPDATE and DELETE RID scans must both track serializable reads");
    require(manager.find(
                "constexpr size_t kDefaultLimit = 0;") !=
                std::string::npos,
            "Snapshot isolation must not serialize 32 ranking clients by "
            "default");
    require(delete_executor.find(
                "context_->txn_mgr_->has_stale_write_target(") !=
                std::string::npos,
            "MVCC DELETE must abort when a stale snapshot yields no writable RID");

    const size_t delete_prepare =
        delete_executor.find("context_->txn_mgr_->prepare_delete(");
    const size_t delete_stage =
        delete_executor.find("context_->txn_->write_batch().stage_delete(");
    require(delete_prepare != std::string::npos &&
                delete_stage != std::string::npos &&
                delete_prepare < delete_stage,
            "DELETE must check/install logical MVCC state before staging");

    const size_t update_prepare =
        update_executor.find("context_->txn_mgr_->prepare_update(");
    const size_t update_stage =
        update_executor.find("context_->txn_->write_batch().stage_update(");
    require(update_prepare != std::string::npos &&
                update_stage != std::string::npos &&
                update_prepare < update_stage,
            "UPDATE must check SSI and install logical MVCC state before staging");

    std::cout << "ssi source contract tests passed\n";
    return 0;
}
