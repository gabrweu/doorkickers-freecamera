#include "settings/Settings.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwchar>

Settings g_settings;

namespace {

std::wstring g_path;

std::wstring Read(const wchar_t* key)
{
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"freecam", key, L"", buf, 64, g_path.c_str());
    return buf;
}

int ReadKey(const wchar_t* key, int def)
{
    unsigned long vk = wcstoul(Read(key).c_str(), nullptr, 0); // decimal or 0x hex
    return (vk > 0 && vk < 256) ? static_cast<int>(vk) : def;
}

float ReadFloat(const wchar_t* key, float def, Range range)
{
    std::wstring s = Read(key);
    if (s.empty()) {
        return def;
    }
    return std::clamp(static_cast<float>(_wtof(s.c_str())), range.min, range.max);
}

bool ReadBool(const wchar_t* key, bool def)
{
    return ReadFloat(key, def ? 1.0f : 0.0f, Range{0.0f, 1.0f}) != 0;
}

void Write(const wchar_t* key, const wchar_t* value)
{
    WritePrivateProfileStringW(L"freecam", key, value, g_path.c_str());
}

void WriteKey(const wchar_t* key, int vk)
{
    wchar_t buf[16];
    swprintf_s(buf, L"0x%02X", vk);
    Write(key, buf);
}

void WriteFloat(const wchar_t* key, float v)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", v);
    Write(key, buf);
}

void WriteBool(const wchar_t* key, bool v)
{
    Write(key, v ? L"1" : L"0");
}

} // namespace

// a missing or empty key keeps its default
void Settings_Load(const std::wstring& path)
{
    g_path = path;
    Settings d;
    Settings& s = g_settings;

    s.rotateModifier = ReadKey(L"rotateModifier", d.rotateModifier);
    s.lookModifier = ReadKey(L"lookModifier", d.lookModifier);
    s.rotateLeftKey = ReadKey(L"rotateLeftKey", d.rotateLeftKey);
    s.rotateRightKey = ReadKey(L"rotateRightKey", d.rotateRightKey);
    s.toggleViewKey = ReadKey(L"toggleViewKey", d.toggleViewKey);
    s.resetHeadingKey = ReadKey(L"resetHeadingKey", d.resetHeadingKey);

    s.rotateStep = ReadFloat(L"rotateStep", d.rotateStep, kRotateStepRange);
    s.keyRotateSpeed = ReadFloat(L"keyRotateSpeed", d.keyRotateSpeed, kKeyRotateSpeedRange);
    s.maxTilt = ReadFloat(L"maxTilt", d.maxTilt, kMaxTiltRange);
    s.smoothing = ReadFloat(L"smoothing", d.smoothing, kSmoothingRange);

    s.mouseSensitivity = ReadFloat(L"mouseSensitivity", d.mouseSensitivity, kMouseSensitivityRange);
    s.reverseOrbitDrag = ReadBool(L"reverseOrbitDrag", d.reverseOrbitDrag);
    s.reverseLookDrag = ReadBool(L"reverseLookDrag", d.reverseLookDrag);
    s.lockDragRotation = ReadBool(L"lockDragRotation", d.lockDragRotation);
    s.lockDragTilt = ReadBool(L"lockDragTilt", d.lockDragTilt);
    s.hideCursorWhileDragging = ReadBool(L"hideCursorWhileDragging", d.hideCursorWhileDragging);
    s.cursorToCenterAfterDrag = ReadBool(L"cursorToCenterAfterDrag", d.cursorToCenterAfterDrag);

    s.startLocked = ReadBool(L"startLocked", d.startLocked);
    s.lockedScreenCenter = ReadBool(L"lockedScreenCenter", d.lockedScreenCenter);
    s.cursorToCenterOnLock = ReadBool(L"cursorToCenterOnLock", d.cursorToCenterOnLock);
    s.lockedTilt = ReadFloat(L"lockedTilt", d.lockedTilt, kLockedTiltRange);
    s.lockedHeading = ReadFloat(L"lockedHeading", d.lockedHeading, kLockedHeadingRange);
    s.lockedZoom = ReadFloat(L"lockedZoom", d.lockedZoom, kLockedZoomRange);

    s.zoomInFactor = ReadFloat(L"zoomInFactor", d.zoomInFactor, kZoomInFactorRange);
    s.zoomOutFactor = ReadFloat(L"zoomOutFactor", d.zoomOutFactor, kZoomOutFactorRange);
    s.allowOutsideMap = ReadBool(L"allowOutsideMap", d.allowOutsideMap);
    s.edgeRoom = ReadFloat(L"edgeRoom", d.edgeRoom, kEdgeRoomRange);

    s.uprightIcons = ReadBool(L"uprightIcons", d.uprightIcons);
}

void Settings_ResetCameraView()
{
    const Settings d;
    g_settings.lockedTilt = d.lockedTilt;
    g_settings.lockedHeading = d.lockedHeading;
    g_settings.lockedZoom = d.lockedZoom;
    g_settings.lockDragRotation = d.lockDragRotation;
    g_settings.lockDragTilt = d.lockDragTilt;
}

void Settings_Save()
{
    const Settings& s = g_settings;

    WriteKey(L"rotateModifier", s.rotateModifier);
    WriteKey(L"lookModifier", s.lookModifier);
    WriteKey(L"rotateLeftKey", s.rotateLeftKey);
    WriteKey(L"rotateRightKey", s.rotateRightKey);
    WriteKey(L"toggleViewKey", s.toggleViewKey);
    WriteKey(L"resetHeadingKey", s.resetHeadingKey);

    WriteFloat(L"rotateStep", s.rotateStep);
    WriteFloat(L"keyRotateSpeed", s.keyRotateSpeed);
    WriteFloat(L"maxTilt", s.maxTilt);
    WriteFloat(L"smoothing", s.smoothing);

    WriteFloat(L"mouseSensitivity", s.mouseSensitivity);
    WriteBool(L"reverseOrbitDrag", s.reverseOrbitDrag);
    WriteBool(L"reverseLookDrag", s.reverseLookDrag);
    WriteBool(L"lockDragRotation", s.lockDragRotation);
    WriteBool(L"lockDragTilt", s.lockDragTilt);
    WriteBool(L"hideCursorWhileDragging", s.hideCursorWhileDragging);
    WriteBool(L"cursorToCenterAfterDrag", s.cursorToCenterAfterDrag);

    WriteBool(L"startLocked", s.startLocked);
    WriteBool(L"lockedScreenCenter", s.lockedScreenCenter);
    WriteBool(L"cursorToCenterOnLock", s.cursorToCenterOnLock);
    WriteFloat(L"lockedTilt", s.lockedTilt);
    WriteFloat(L"lockedHeading", s.lockedHeading);
    WriteFloat(L"lockedZoom", s.lockedZoom);

    // goes down to 0.02, so 3 decimals
    wchar_t zoomIn[32];
    swprintf_s(zoomIn, L"%.3f", s.zoomInFactor);
    Write(L"zoomInFactor", zoomIn);
    WriteFloat(L"zoomOutFactor", s.zoomOutFactor);
    WriteBool(L"allowOutsideMap", s.allowOutsideMap);
    WriteFloat(L"edgeRoom", s.edgeRoom);

    WriteBool(L"uprightIcons", s.uprightIcons);
}
