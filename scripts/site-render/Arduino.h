#pragma once
// Host-only shim. No device I/O is performed while rendering website examples.
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>
using std::min;
using std::max;
using std::isfinite;
inline unsigned long millis() { return 120000; }
inline size_t strlcpy(char *destination, const char *source, size_t capacity) {
    const size_t length = std::strlen(source);
    if (capacity) {
        const size_t copied = std::min(length, capacity - 1);
        std::memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}
#if defined(_WIN32)
inline std::tm *localtime_r(const std::time_t *value, std::tm *result) {
    return localtime_s(result, value) == 0 ? result : nullptr;
}
#endif
struct SiteSerial {
    template<typename... T> void printf(const char *, T...) {}
    void println(const char *) {}
    void flush() {}
};
static SiteSerial Serial;
struct SiteEsp {
    void restart() {}
};
static SiteEsp ESP;
