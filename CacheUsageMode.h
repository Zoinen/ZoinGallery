#ifndef CACHEUSAGEMODE_H
#define CACHEUSAGEMODE_H

enum class CacheUsageMode {
    Off = 0,
    On = 1,
    OnlyCache = 2
};

inline CacheUsageMode cacheUsageModeFromInt(int value) {
    switch (value) {
    case static_cast<int>(CacheUsageMode::Off):
        return CacheUsageMode::Off;
    case static_cast<int>(CacheUsageMode::OnlyCache):
        return CacheUsageMode::OnlyCache;
    default:
        return CacheUsageMode::On;
    }
}

inline bool cacheReadsEnabled(CacheUsageMode mode) {
    return mode != CacheUsageMode::Off;
}

inline bool cacheWritesEnabled(CacheUsageMode mode) {
    return mode == CacheUsageMode::On;
}

inline bool sourceReadsEnabled(CacheUsageMode mode) {
    return mode != CacheUsageMode::OnlyCache;
}

#endif // CACHEUSAGEMODE_H
