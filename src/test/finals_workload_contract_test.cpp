#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
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

}  // namespace

int main(int argc, char **argv) {
    require(argc == 2, "expected repository source directory");
    const std::filesystem::path root(argv[1]);
    const std::string source = read_file(
        root / "src" / "test" / "finals_workload_driver.py");

    require(source.find("prepare_set(") != std::string::npos,
            "driver must install PREPARE_SET");
    require(source.find("execute_batch(") != std::string::npos,
            "ranked transactions must use EXEC_BATCH");
    require(source.find("WAREHOUSES = 50") != std::string::npos,
            "driver must cover 50 warehouses");
    require(source.find("MIX = (45, 43, 4, 4, 4)") != std::string::npos,
            "driver must use finals mix");
    require(source.find("MEASURE_SECONDS = 150") != std::string::npos,
            "driver must use 150-second windows");
    require(source.find("ROUNDS = 3") != std::string::npos,
            "driver must use three ranked rounds");
    require(source.find("client.execute(sql)") == std::string::npos,
            "ranked transactions must not fall back to EXEC_STREAM");

    std::cout << "finals workload contract tests passed\n";
    return 0;
}
