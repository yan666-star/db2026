/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <fstream>

#include "errors.h"
#include "index/ix.h"
#include "record/rm.h"
#include "record_printer.h"

namespace {

void copy_file_for_checkpoint(const std::string &source,
                              const std::string &destination) {
    std::ifstream input(source, std::ios::binary);
    if (!input.is_open()) {
        throw InternalError("Cannot open checkpoint snapshot source");
    }
    std::ofstream output(
        destination, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw InternalError("Cannot open checkpoint snapshot destination");
    }
    output << input.rdbuf();
    output.flush();
    if (input.bad() || !output.good()) {
        throw InternalError("Cannot copy index checkpoint snapshot");
    }
    output.close();
    if (!output) {
        throw InternalError("Cannot close index checkpoint snapshot");
    }
}

}

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string& db_name) {
    struct stat st;
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    DbMeta *new_db = new DbMeta();
    new_db->name_ = db_name;

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    if (chdir(db_name.c_str()) < 0) {
        throw UnixError();
    }
    std::remove("output.txt");

    std::ifstream ifs(DB_META_NAME);
    if (!ifs.is_open()) {
        throw RMDBError("Cannot open database meta file");
    }
    ifs >> db_;
    ifs.close();

    fhs_.clear();
    ihs_.clear();
    for (auto &entry : db_.tabs_) {
        const std::string &tab_name = entry.first;
        fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));
        for (auto &index : entry.second.indexes) {
            std::string ix_name = ix_manager_->get_index_name(tab_name, index.cols);
            ihs_.emplace(ix_name, ix_manager_->open_index(tab_name, index.cols));
        }
    }
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

void SmManager::flush_for_checkpoint() {
    flush_meta();
    for (const auto &entry : fhs_) {
        entry.second->flush_file_header();
    }
    for (const auto &entry : ihs_) {
        entry.second->flush_file_header();
    }
    buffer_pool_manager_->flush_all_pages();
    disk_manager_->sync_all_open_files();
    disk_manager_->sync_file(DB_META_NAME);
}

void SmManager::rebuild_indexes_for_recovery(
    const std::unordered_set<std::string> &table_names) {
    for (const auto &table_name : table_names) {
        TabMeta &table = db_.get_table(table_name);
        RmFileHandle *file_handle = fhs_.at(table_name).get();
        for (const auto &index : table.indexes) {
            std::string index_name =
                ix_manager_->get_index_name(table_name, index.cols);
            auto old_handle = ihs_.find(index_name);
            if (old_handle != ihs_.end()) {
                ix_manager_->close_index(old_handle->second.get());
                ihs_.erase(old_handle);
            }
            if (ix_manager_->exists(table_name, index.cols)) {
                ix_manager_->destroy_index(table_name, index.cols);
            }

            ix_manager_->create_index(table_name, index.cols);
            auto new_handle = ix_manager_->open_index(table_name, index.cols);
            IxIndexHandle *index_handle = new_handle.get();
            ihs_.emplace(index_name, std::move(new_handle));

            RmScan scan(file_handle);
            while (!scan.is_end()) {
                Rid rid = scan.rid();
                auto record = file_handle->get_record(rid, nullptr);
                std::vector<char> key(index.col_tot_len);
                int key_offset = 0;
                for (const auto &col : index.cols) {
                    memcpy(key.data() + key_offset,
                           record->data + col.offset, col.len);
                    key_offset += col.len;
                }
                index_handle->insert_entry(key.data(), rid, nullptr);
                scan.next();
            }
        }
    }
}

void SmManager::create_index_snapshots(int64_t checkpoint_offset) {
    for (const auto &entry : ihs_) {
        const std::string snapshot =
            entry.first + ".checkpoint." + std::to_string(checkpoint_offset);
        copy_file_for_checkpoint(entry.first, snapshot);
        disk_manager_->sync_file(snapshot);
    }
}

bool SmManager::restore_index_snapshots(int64_t checkpoint_offset) {
    for (const auto &entry : ihs_) {
        const std::string snapshot =
            entry.first + ".checkpoint." + std::to_string(checkpoint_offset);
        if (!disk_manager_->is_file(snapshot)) {
            return false;
        }
    }

    std::vector<std::string> index_names;
    index_names.reserve(ihs_.size());
    for (const auto &entry : ihs_) {
        index_names.push_back(entry.first);
    }
    for (const auto &index_name : index_names) {
        ix_manager_->close_index(ihs_.at(index_name).get());
        ihs_.erase(index_name);
    }

    for (auto &table_entry : db_.tabs_) {
        const std::string &table_name = table_entry.first;
        for (const auto &index : table_entry.second.indexes) {
            const std::string index_name =
                ix_manager_->get_index_name(table_name, index.cols);
            const std::string snapshot =
                index_name + ".checkpoint." + std::to_string(checkpoint_offset);
            copy_file_for_checkpoint(snapshot, index_name);
            disk_manager_->sync_file(index_name);
            ihs_.emplace(
                index_name, ix_manager_->open_index(table_name, index.cols));
        }
    }
    return true;
}

void SmManager::cleanup_index_snapshots(int64_t checkpoint_offset) {
    const std::string keep_suffix =
        ".checkpoint." + std::to_string(checkpoint_offset);

    DIR *directory = opendir(".");
    if (directory == nullptr) {
        throw UnixError();
    }
    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        struct stat file_stat {};
        if (stat(name.c_str(), &file_stat) != 0 ||
            !S_ISREG(file_stat.st_mode)) {
            continue;
        }
        const auto marker = name.rfind(".checkpoint.");
        if (marker == std::string::npos ||
            name.size() < keep_suffix.size() ||
            name.compare(name.size() - keep_suffix.size(),
                         keep_suffix.size(), keep_suffix) == 0) {
            continue;
        }
        if (unlink(name.c_str()) != 0) {
            closedir(directory);
            throw UnixError();
        }
    }
    if (closedir(directory) != 0) {
        throw UnixError();
    }
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    for (auto &entry : fhs_) {
        rm_manager_->close_file(entry.second.get());
    }
    fhs_.clear();

    for (auto &entry : ihs_) {
        ix_manager_->close_index(entry.second.get());
    }
    ihs_.clear();

    flush_meta();
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context* context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "| Tables |\n";
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        outfile << "| " << tab.name << " |\n";
    }
    printer.print_separator(context);
    outfile.close();
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context 
 */
void SmManager::desc_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col : tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
    // Create table meta
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name = col_def.name,
                       .type = col_def.type,
                       .len = col_def.len,
                       .offset = curr_offset,
                       .index = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset;  // record_size就是col meta所占的大小（表的元数据也是以记录的形式进行存储的）
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_[tab_name] = tab;
    // fhs_[tab_name] = rm_manager_->open_file(tab_name);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    if (!db_.is_table(tab_name)) {
        throw TableNotFoundError(tab_name);
    }

    TabMeta tab = db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        std::string ix_name = ix_manager_->get_index_name(tab_name, index.cols);
        auto it = ihs_.find(ix_name);
        if (it != ihs_.end()) {
            ix_manager_->close_index(it->second.get());
            ihs_.erase(it);
        }
    }

    auto fh_it = fhs_.find(tab_name);
    if (fh_it != fhs_.end()) {
        rm_manager_->close_file(fh_it->second.get());
        fhs_.erase(fh_it);
    }

    db_.tabs_.erase(tab_name);
    flush_meta();

    for (auto &index : tab.indexes) {
        ix_manager_->destroy_index(tab_name, index.cols);
    }
    rm_manager_->destroy_file(tab_name);
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    TabMeta& tab = db_.get_table(tab_name);
    if (ix_manager_->exists(tab_name, col_names)) {
        throw IndexExistsError(tab_name, col_names);
    }

    std::vector<ColMeta> cols(col_names.size());
    int col_tot_len = 0;
    for (size_t i = 0; i < col_names.size(); ++i) {
        auto col = tab.get_col(col_names[i]);
        cols[i] = *col;
        col_tot_len += col->len;
    }

    ix_manager_->create_index(tab_name, cols);
    auto index_handle = ix_manager_->open_index(tab_name, cols);
    tab.indexes.push_back({tab_name, col_tot_len, static_cast<int>(cols.size()), cols});
    ihs_.emplace(ix_manager_->get_index_name(tab_name, col_names), std::move(index_handle));
    flush_meta();

    auto rm_handle = fhs_.at(tab_name).get();
    auto ih = ihs_.at(ix_manager_->get_index_name(tab_name, cols)).get();
    char* key = new char[col_tot_len];

    RmScan rm_scan(rm_handle);
    while (!rm_scan.is_end()) {
        auto record = rm_handle->get_record(rm_scan.rid(), context);
        if (!record) {
            rm_scan.next();
            continue;
        }
        int offset = 0;
        for (size_t i = 0; i < cols.size(); ++i) {
            memcpy(key + offset, record->data + cols[i].offset, cols[i].len);
            offset += cols[i].len;
        }
        std::vector<Rid> tmp_result;
        if (ih->get_value(key, &tmp_result, context == nullptr ? nullptr : context->txn_)) {
            std::string index_name = ix_manager_->get_index_name(tab_name, cols);
            ix_manager_->close_index(ihs_.at(index_name).get());
            ihs_.erase(index_name);
            ix_manager_->destroy_index(tab_name, cols);
            tab.indexes.pop_back();
            flush_meta();
            delete[] key;
            throw RMDBError("failure");
        }
        ih->insert_entry(key, rm_scan.rid(), context == nullptr ? nullptr : context->txn_);
        rm_scan.next();
    }
    delete[] key;
    ih->flush_file_header();
    buffer_pool_manager_->flush_all_pages(ih->GetFd());
    disk_manager_->sync_all_open_files();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    if (!ix_manager_->exists(tab_name, col_names)) {
        throw IndexNotFoundError(tab_name, col_names);
    }
    auto index_name = ix_manager_->get_index_name(tab_name, col_names);
    TabMeta& tab = db_.get_table(tab_name);
    auto index_meta = tab.get_index_meta(col_names);
    tab.indexes.erase(index_meta);
    ix_manager_->close_index(ihs_.at(index_name).get());
    ix_manager_->destroy_index(tab_name, col_names);
    ihs_.erase(index_name);
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {
    std::vector<std::string> col_names;
    for (auto& col : cols) {
        col_names.push_back(col.name);
    }
    drop_index(tab_name, col_names, context);
}

void SmManager::show_index(const std::string& tab_name, Context* context) {
    TabMeta& tab = db_.get_table(tab_name);
    if (tab.indexes.empty()) {
        return;
    }
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    RecordPrinter printer(3);
    printer.print_separator(context);
    for (auto& index : tab.indexes) {
        std::string col_str = "(";
        for (auto& col : index.cols) {
            col_str += col.name + ",";
        }
        col_str.pop_back();
        col_str += ")";
        printer.print_record({tab_name, "unique", col_str}, context);
        outfile << "| " << tab.name << " | unique | " << col_str << " |\n";
        printer.print_separator(context);
    }
    outfile.close();
}

void SmManager::rollback(WriteRecord* record, Context* context) {
    switch (record->GetWriteType()) {
        case WType::INSERT_TUPLE:
            rollback_insert(record->GetTableName(), record->GetRid(), context);
            break;
        case WType::DELETE_TUPLE:
            rollback_delete(record->GetTableName(), record->GetRid(), record->GetRecord(), context);
            break;
        case WType::UPDATE_TUPLE:
            rollback_update(record->GetTableName(), record->GetRid(), record->GetRecord(), context);
            break;
        default:
            throw RMDBError("Invalid rollback type");
    }
}

void SmManager::rollback_insert(const std::string& table_name, Rid& rid, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    std::unique_ptr<RmRecord> inserted_record;
    try {
        inserted_record = file_handle->get_record(rid, context);
    } catch (const RecordNotFoundError&) {
        return;
    }

    for (auto& index_meta : db_.get_table(table_name).indexes) {
        auto index_name = ix_manager_->get_index_name(table_name, index_meta.cols);
        auto index_handle = ihs_.at(index_name).get();
        char* key_buf = new char[index_meta.col_tot_len];
        int key_offset = 0;
        for (int i = 0; i < index_meta.col_num; ++i) {
            memcpy(key_buf + key_offset, inserted_record->data + index_meta.cols[i].offset,
                   index_meta.cols[i].len);
            key_offset += index_meta.cols[i].len;
        }
        index_handle->delete_entry(key_buf, context->txn_);
        delete[] key_buf;
    }
    file_handle->delete_record(rid, context);
}

void SmManager::rollback_delete(const std::string& table_name, Rid& rid, RmRecord& record, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    file_handle->insert_record(rid, record.data);

    for (auto& index_meta : db_.get_table(table_name).indexes) {
        auto index_name = ix_manager_->get_index_name(table_name, index_meta.cols);
        auto index_handle = ihs_.at(index_name).get();
        char* key_buf = new char[index_meta.col_tot_len];
        int key_offset = 0;
        for (int i = 0; i < index_meta.col_num; ++i) {
            memcpy(key_buf + key_offset, record.data + index_meta.cols[i].offset, index_meta.cols[i].len);
            key_offset += index_meta.cols[i].len;
        }
        index_handle->insert_entry(key_buf, rid, context->txn_);
        delete[] key_buf;
    }
}

void SmManager::rollback_update(const std::string& table_name, Rid& rid, RmRecord& record, Context* context) {
    auto file_handle = fhs_.at(table_name).get();
    std::unique_ptr<RmRecord> new_record;
    try {
        new_record = file_handle->get_record(rid, context);
    } catch (const RecordNotFoundError&) {
        return;
    }

    file_handle->update_record(rid, record.data, context);

    for (auto& index_meta : db_.get_table(table_name).indexes) {
        auto index_name = ix_manager_->get_index_name(table_name, index_meta.cols);
        auto index_handle = ihs_.at(index_name).get();
        char* old_key = new char[index_meta.col_tot_len];
        char* new_key = new char[index_meta.col_tot_len];
        int key_offset = 0;
        for (int i = 0; i < index_meta.col_num; ++i) {
            memcpy(old_key + key_offset, record.data + index_meta.cols[i].offset, index_meta.cols[i].len);
            memcpy(new_key + key_offset, new_record->data + index_meta.cols[i].offset, index_meta.cols[i].len);
            key_offset += index_meta.cols[i].len;
        }
        if (memcmp(old_key, new_key, index_meta.col_tot_len) == 0) {
            delete[] old_key;
            delete[] new_key;
            continue;
        }
        index_handle->insert_entry(old_key, rid, context->txn_);
        index_handle->delete_entry(new_key, context->txn_);
        delete[] old_key;
        delete[] new_key;
    }
}
