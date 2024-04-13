#include <fstream>
#include <chrono>
#include <ctime>

#define FILENAME "local.log"

namespace local {

namespace log {

template<typename... Args>
void write(Args&&... args) {
    std::ofstream file(FILENAME);
    if (file.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        file << std::ctime(&time);
        (file << ... << std::forward<Args>(args));
        file.close();
    }
}

}  // namespace log

}  // namespace local