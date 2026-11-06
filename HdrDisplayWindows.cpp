#include "HdrDisplay.h"

#include <qt_windows.h>
#include <QList>
#include <cstring>

namespace ZoinGallery {
namespace {
// Windows 11 24H2 separates HDR from automatic SDR color management. Keep
// the SDK-defined ABI usable when building with an older Windows SDK too.
struct AdvancedColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 flags;
    UINT32 encoding;
    UINT32 bitsPerChannel;
    UINT32 activeColorMode;
};
struct HdrState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 enabled;
};

DISPLAYCONFIG_DEVICE_INFO_HEADER headerFor(
    const HdrDisplayState &state, UINT32 size, int type)
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER header{};
    header.type = DISPLAYCONFIG_DEVICE_INFO_TYPE(type);
    header.size = size;
    static_assert(sizeof(header.adapterId) == sizeof(state.adapterId));
    std::memcpy(&header.adapterId, &state.adapterId, sizeof(state.adapterId));
    header.id = state.targetId;
    return header;
}
}

static HdrDisplayState queryDisplay(const QString &deviceName, const HdrDisplayState *identity)
{
    HdrDisplayState result;
    result.deviceName = deviceName;
    if (deviceName.isEmpty()) return result;
    QList<DISPLAYCONFIG_PATH_INFO> paths;
    QList<DISPLAYCONFIG_MODE_INFO> modes;
    LONG status = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 3 && status == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS) break;
        paths.resize(pathCount);
        modes.resize(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                                    &modeCount, modes.data(), nullptr);
        if (status == ERROR_SUCCESS) paths.resize(pathCount);
    }
    result.error = status;
    if (status != ERROR_SUCCESS) return result;
    for (const auto &path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (identity) {
            if (path.targetInfo.id != identity->targetId
                || std::memcmp(&path.targetInfo.adapterId, &identity->adapterId, sizeof(identity->adapterId))) continue;
        } else if (QString::fromWCharArray(source.viewGdiDeviceName).compare(deviceName, Qt::CaseInsensitive)) continue;
        result.deviceName = QString::fromWCharArray(source.viewGdiDeviceName);
        std::memcpy(&result.adapterId, &path.targetInfo.adapterId, sizeof(result.adapterId));
        result.targetId = path.targetInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = headerFor(result, sizeof(target), DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME);
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
            result.displayName = QString::fromWCharArray(target.monitorFriendlyDeviceName);

        AdvancedColorInfo2 modern{};
        modern.header = headerFor(result, sizeof(modern), 15); // GET_ADVANCED_COLOR_INFO_2
        if (DisplayConfigGetDeviceInfo(&modern.header) == ERROR_SUCCESS) {
            result.modernApi = true;
            result.supported = (modern.flags & (1u << 4)) && !(modern.flags & (1u << 3));
            result.enabled = modern.flags & (1u << 5);
            result.active = modern.activeColorMode == 2; // ADVANCED_COLOR_MODE_HDR
            return result;
        }
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy{};
        legacy.header = headerFor(result, sizeof(legacy), DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO);
        result.error = DisplayConfigGetDeviceInfo(&legacy.header);
        if (result.error == ERROR_SUCCESS) {
            result.supported = legacy.advancedColorSupported && !legacy.advancedColorForceDisabled;
            result.enabled = result.active = legacy.advancedColorEnabled;
        }
        return result;
    }
    result.error = ERROR_NOT_FOUND;
    return result;
}

HdrDisplayState queryHdrDisplay(const QString &deviceName) { return queryDisplay(deviceName, nullptr); }
HdrDisplayState queryHdrDisplayState(const HdrDisplayState &display) { return queryDisplay(display.deviceName, &display); }

HdrDisplayState queryHdrDisplayForWindow(quintptr nativeWindow)
{
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    const auto handle = MonitorFromWindow(reinterpret_cast<HWND>(nativeWindow), MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO *>(&monitor))) {
        HdrDisplayState state;
        state.error = GetLastError();
        return state;
    }
    return queryHdrDisplay(QString::fromWCharArray(monitor.szDevice));
}

int setHdrDisplayEnabled(const HdrDisplayState &display, bool enabled)
{
    if (!display.supported) return ERROR_NOT_SUPPORTED;
    if (display.modernApi) {
        HdrState state{};
        state.header = headerFor(display, sizeof(state), 16); // SET_HDR_STATE
        state.enabled = enabled ? 1 : 0;
        return DisplayConfigSetDeviceInfo(&state.header);
    }
    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state{};
    state.header = headerFor(display, sizeof(state), DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE);
    state.enableAdvancedColor = enabled;
    return DisplayConfigSetDeviceInfo(&state.header);
}
} // namespace ZoinGallery
