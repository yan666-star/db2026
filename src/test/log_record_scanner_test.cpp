#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <unistd.h>

#include "recovery/log_record_scanner.h"
#include "storage/disk_manager.h"

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void append_record(DiskManager *disk, const LogRecord &record) {
    std::vector<char> bytes(record.log_tot_len_);
    record.serialize(bytes.data());
    disk->write_log(bytes.data(), static_cast<int>(bytes.size()));
}

}  // namespace

int main() {
    char directory_template[] = "/tmp/rmdb-log-scanner-test-XXXXXX";
    char *directory = mkdtemp(directory_template);
    require(directory != nullptr, "mkdtemp failed");

    const std::filesystem::path previous =
        std::filesystem::current_path();
    std::filesystem::current_path(directory);
    std::ofstream(LOG_FILE_NAME, std::ios::binary).close();

    DiskManager disk;
    BeginLogRecord begin(17);
    begin.lsn_ = 101;
    append_record(&disk, begin);

    RmRecord value(257);
    for (int index = 0; index < value.size; ++index) {
        value.data[index] = static_cast<char>((index * 31) & 0xff);
    }
    InsertLogRecord insert(17, value, Rid{9, 3}, "cross_boundary");
    insert.lsn_ = 102;
    insert.prev_lsn_ = begin.lsn_;
    append_record(&disk, insert);

    CommitLogRecord commit(17);
    commit.lsn_ = 103;
    commit.prev_lsn_ = insert.lsn_;
    append_record(&disk, commit);
    const int64_t complete_end = disk.get_file_size(LOG_FILE_NAME);

    char truncated_tail[LOG_HEADER_SIZE - 1]{};
    disk.write_log(truncated_tail, sizeof(truncated_tail));
    const int64_t log_end = disk.get_file_size(LOG_FILE_NAME);

    LogRecordScanner scanner(&disk, 0, log_end, 64);
    std::vector<LogType> types;
    std::vector<int64_t> offsets;
    while (auto scanned = scanner.next()) {
        types.push_back(scanned->record->log_type_);
        offsets.push_back(scanned->offset);
    }

    require(types == std::vector<LogType>{
                         LogType::begin, LogType::INSERT, LogType::commit},
            "scanner did not preserve WAL record order across buffer boundaries");
    require(offsets.size() == 3 && offsets.front() == 0 &&
                offsets[1] == begin.log_tot_len_ &&
                offsets[2] == begin.log_tot_len_ + insert.log_tot_len_,
            "scanner returned incorrect record offsets");
    require(scanner.valid_end() == complete_end,
            "scanner did not stop at the last complete WAL record");
    require(scanner.position() == complete_end,
            "scanner advanced into a truncated WAL tail");

    const int log_fd = disk.GetLogFd();
    if (log_fd >= 0) {
        disk.close_file(log_fd);
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(directory);
    std::cout << "log record scanner tests passed\n";
    return 0;
}
