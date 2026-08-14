/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/disk_manager.h"

#include <assert.h>    // for assert
#include <cerrno>
#include <cstdint>
#include <string.h>    // for memset
#include <sys/stat.h>  // for stat
#include <unistd.h>    // for lseek

#include "defs.h"

DiskManager::DiskManager() = default;

/**
 * @description: 将数据写入文件的指定磁盘页面中
 * @param {int} fd 磁盘文件的文件句柄
 * @param {page_id_t} page_no 写入目标页面的page_id
 * @param {char} *offset 要写入磁盘的数据
 * @param {int} num_bytes 要写入磁盘的数据大小
 */
void DiskManager::write_page(int fd, page_id_t page_no, const char *offset, int num_bytes) {
    off_t file_offset = static_cast<off_t>(page_no) * PAGE_SIZE;
    int written = 0;
    while (written < num_bytes) {
        ssize_t bytes_write =
            pwrite(fd, offset + written, num_bytes - written, file_offset + written);
        if (bytes_write < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_write <= 0) {
            throw InternalError("DiskManager::write_page Error");
        }
        written += static_cast<int>(bytes_write);
    }
}

/**
 * @description: 读取文件中指定编号的页面中的部分数据到内存中
 * @param {int} fd 磁盘文件的文件句柄
 * @param {page_id_t} page_no 指定的页面编号
 * @param {char} *offset 读取的内容写入到offset中
 * @param {int} num_bytes 读取的数据量大小
 */
void DiskManager::read_page(int fd, page_id_t page_no, char *offset, int num_bytes) {
    off_t file_offset = static_cast<off_t>(page_no) * PAGE_SIZE;
    int read_bytes = 0;
    while (read_bytes < num_bytes) {
        ssize_t bytes_read =
            pread(fd, offset + read_bytes, num_bytes - read_bytes,
                  file_offset + read_bytes);
        if (bytes_read < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_read <= 0) {
            throw InternalError("DiskManager::read_page Error");
        }
        read_bytes += static_cast<int>(bytes_read);
    }
}

/**
 * @description: 分配一个新的页号
 * @return {page_id_t} 分配的新页号
 * @param {int} fd 指定文件的文件句柄
 */
page_id_t DiskManager::allocate_page(int fd) {
    // 简单的自增分配策略，指定文件的页面编号加1
    assert(fd >= 0 && fd < MAX_FD);
    return fd2pageno_[fd]++;
}

void DiskManager::deallocate_page(__attribute__((unused)) page_id_t page_id) {}

bool DiskManager::is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void DiskManager::create_dir(const std::string &path) {
    // Create a subdirectory
    std::string cmd = "mkdir " + path;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为path的目录
        throw UnixError();
    }
}

void DiskManager::destroy_dir(const std::string &path) {
    std::string cmd = "rm -r " + path;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 判断指定路径文件是否存在
 * @return {bool} 若指定路径文件存在则返回true 
 * @param {string} &path 指定路径文件
 */
bool DiskManager::is_file(const std::string &path) {
    // 用struct stat获取文件信息
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * @description: 用于创建指定路径文件
 * @return {*}
 * @param {string} &path
 */
void DiskManager::create_file(const std::string &path) {
    if (is_file(path)) {
        throw FileExistsError(path);
    }
    int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        throw UnixError();
    }
    close(fd);
}

/**
 * @description: 删除指定路径的文件
 * @param {string} &path 文件所在路径
 */
void DiskManager::destroy_file(const std::string &path) {
    if (path2fd_.count(path)) {
        throw FileNotClosedError(path);
    }
    if (!is_file(path)) {
        throw FileNotFoundError(path);
    }
    if (unlink(path.c_str()) < 0) {
        throw UnixError();
    }
}


/**
 * @description: 打开指定路径文件 
 * @return {int} 返回打开的文件的文件句柄
 * @param {string} &path 文件所在路径
 */
int DiskManager::open_file(const std::string &path) {
    if (path2fd_.count(path)) {
        throw FileNotClosedError(path);
    }
    if (!is_file(path)) {
        throw FileNotFoundError(path);
    }
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        throw UnixError();
    }
    path2fd_[path] = fd;
    fd2path_[fd] = path;
    return fd;
}

/**
 * @description:用于关闭指定路径文件 
 * @param {int} fd 打开的文件的文件句柄
 */
void DiskManager::close_file(int fd) {
    if (!fd2path_.count(fd)) {
        throw FileNotOpenError(fd);
    }
    std::string path = fd2path_[fd];
    path2fd_.erase(path);
    fd2path_.erase(fd);
    if (log_fd_ == fd) {
        log_fd_ = -1;
        log_write_offset_ = -1;
    }
    if (close(fd) < 0) {
        throw UnixError();
    }
}


/**
 * @description: 获得文件的大小
 * @return {int64_t} 文件的大小
 * @param {string} &file_name 文件名
 */
int64_t DiskManager::get_file_size(const std::string &file_name) {
    struct stat stat_buf;
    int rc = stat(file_name.c_str(), &stat_buf);
    return rc == 0 ? static_cast<int64_t>(stat_buf.st_size) : -1;
}

/**
 * @description: 根据文件句柄获得文件名
 * @return {string} 文件句柄对应文件的文件名
 * @param {int} fd 文件句柄
 */
std::string DiskManager::get_file_name(int fd) {
    if (!fd2path_.count(fd)) {
        throw FileNotOpenError(fd);
    }
    return fd2path_[fd];
}

/**
 * @description:  获得文件名对应的文件句柄
 * @return {int} 文件句柄
 * @param {string} &file_name 文件名
 */
int DiskManager::get_file_fd(const std::string &file_name) {
    if (!path2fd_.count(file_name)) {
        return open_file(file_name);
    }
    return path2fd_[file_name];
}


/**
 * @description:  读取日志文件内容
 * @return {int} 返回读取的数据量，若为-1说明读取数据的起始位置超过了文件大小
 * @param {char} *log_data 读取内容到log_data中
 * @param {int} size 读取的数据量大小
 * @param {int64_t} offset 读取的内容在文件中的位置
 */
int DiskManager::read_log(char *log_data, int size, int64_t offset) {
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
    }
    const int64_t file_size = get_file_size(LOG_FILE_NAME);
    if (size < 0 || offset < 0 || file_size < 0 || offset > file_size) {
        return -1;
    }

    size = static_cast<int>(std::min<int64_t>(size, file_size - offset));
    int read_bytes = 0;
    while (read_bytes < size) {
        ssize_t n = pread(
            log_fd_, log_data + read_bytes, size - read_bytes,
            static_cast<off_t>(offset) + read_bytes);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            throw UnixError();
        }
        if (n == 0) {
            break;
        }
        read_bytes += static_cast<int>(n);
    }
    return read_bytes;
}


/**
 * @description: 写日志内容
 * @param {char} *log_data 要写入的日志内容
 * @param {int} size 要写入的内容大小
 */
void DiskManager::write_log(char *log_data, int size) {
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
    }

    if (log_write_offset_ < 0) {
        struct stat stat_buf;
        if (fstat(log_fd_, &stat_buf) < 0) {
            throw UnixError();
        }
        log_write_offset_ = stat_buf.st_size;
    }
    int64_t file_offset = log_write_offset_;
    int written = 0;
    while (written < size) {
        ssize_t n = pwrite(
            log_fd_, log_data + written, size - written,
            file_offset + written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            throw UnixError();
        }
        written += static_cast<int>(n);
    }
    log_write_offset_ += size;
}

void DiskManager::sync_log() {
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
    }
    if (fsync(log_fd_) < 0) {
        throw UnixError();
    }
}

void DiskManager::truncate_log(int64_t size) {
    if (size < 0) {
        throw InternalError("Invalid log truncation size");
    }
    if (log_fd_ == -1) {
        log_fd_ = open_file(LOG_FILE_NAME);
    }
    if (ftruncate(log_fd_, static_cast<off_t>(size)) < 0 ||
        fsync(log_fd_) < 0) {
        throw UnixError();
    }
    log_write_offset_ = size;
}

void DiskManager::sync_all_open_files() {
    for (const auto &entry : fd2path_) {
        if (fsync(entry.first) < 0) {
            throw UnixError();
        }
    }
}

void DiskManager::sync_file(const std::string &path) {
    auto it = path2fd_.find(path);
    if (it != path2fd_.end()) {
        if (fsync(it->second) < 0) {
            throw UnixError();
        }
        return;
    }

    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        throw UnixError();
    }
    if (fsync(fd) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        throw UnixError();
    }
    if (close(fd) < 0) {
        throw UnixError();
    }
}

void DiskManager::write_restart_offset(int64_t offset) {
    const std::string tmp_name = RESTART_FILE_NAME + ".tmp";
    int fd = open(tmp_name.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        throw UnixError();
    }

    const char *data = reinterpret_cast<const char *>(&offset);
    size_t written = 0;
    while (written < sizeof(offset)) {
        ssize_t n = write(fd, data + written, sizeof(offset) - written);
        if (n < 0) {
            int saved_errno = errno;
            close(fd);
            unlink(tmp_name.c_str());
            errno = saved_errno;
            throw UnixError();
        }
        if (n == 0) {
            close(fd);
            unlink(tmp_name.c_str());
            errno = EIO;
            throw UnixError();
        }
        written += static_cast<size_t>(n);
    }

    if (fsync(fd) < 0) {
        int saved_errno = errno;
        close(fd);
        unlink(tmp_name.c_str());
        errno = saved_errno;
        throw UnixError();
    }
    if (close(fd) < 0) {
        unlink(tmp_name.c_str());
        throw UnixError();
    }
    if (rename(tmp_name.c_str(), RESTART_FILE_NAME.c_str()) < 0) {
        unlink(tmp_name.c_str());
        throw UnixError();
    }

    int dir_fd = open(".", O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        throw UnixError();
    }
    if (fsync(dir_fd) < 0) {
        int saved_errno = errno;
        close(dir_fd);
        errno = saved_errno;
        throw UnixError();
    }
    if (close(dir_fd) < 0) {
        throw UnixError();
    }
}

int64_t DiskManager::read_restart_offset() {
    int fd = open(RESTART_FILE_NAME.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        throw UnixError();
    }

    int64_t offset = 0;
    char *data = reinterpret_cast<char *>(&offset);
    size_t read_bytes = 0;
    while (read_bytes < sizeof(offset)) {
        ssize_t n = read(fd, data + read_bytes, sizeof(offset) - read_bytes);
        if (n < 0) {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            throw UnixError();
        }
        if (n == 0) {
            close(fd);
            return 0;
        }
        read_bytes += static_cast<size_t>(n);
    }
    if (close(fd) < 0) {
        throw UnixError();
    }
    return offset < 0 ? 0 : offset;
}
