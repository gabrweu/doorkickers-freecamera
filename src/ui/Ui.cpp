// The in-mission settings window, drawn with the game's ImGui.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

int* g_rebindSlot = nullptr;
bool g_keysHeldAtRebindStart[256] = {};

constexpr DWORD kResetConfirmMs = 3000; // the second click's window
DWORD g_resetArmedAt = 0;

constexpr int kVkUnassigned = 0x07;
constexpr int kVkReserved = 0xFF;

std::string KeyName(int vk)
{
    switch (vk) { // mouse buttons have no scan code, so GetKeyNameText can't name them
    case VK_MBUTTON: return "Middle mouse";
    case VK_XBUTTON1: return "Mouse 4 (back)";
    case VK_XBUTTON2: return "Mouse 5 (forward)";
    }

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan) << 16;
    switch (vk) { // extended keys need bit 24 for the right name
    // clang-format off
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
    case VK_RMENU: case VK_RCONTROL:
        lParam |= 1 << 24;
        break;
    // clang-format on
    }

    char name[64] = {};
    if (scan && GetKeyNameTextA(lParam, name, sizeof(name)) > 0) {
        return name;
    }
    char hex[16];
    sprintf_s(hex, "0x%02X", vk);
    return hex;
}

bool Button(const char* label)
{
    return game::imgui::ButtonEx(label, ImVec2{0, 0}, 0);
}

bool Slider(const char* label, float* v, Range range, const char* format, int flags = 0)
{
    return game::imgui::SliderScalar(label, static_cast<int>(game::imgui::ImGuiDataType_Float.Get()), v, &range.min,
                                     &range.max, format, flags);
}

void Separator()
{
    game::imgui::SeparatorEx(static_cast<int>(game::imgui::ImGuiSeparatorFlags_Horizontal.Get()));
}

// bindings hold the generic modifier, not the left/right one
int GenericModifier(int vk)
{
    switch (vk) {
    case VK_LSHIFT:
    case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU: return VK_MENU;
    default: return vk;
    }
}

// Keys held when rebinding started count once released. The scan starts at VK_MBUTTON: left and right are the
// game's order buttons.
void UpdateRebind()
{
    if (!game::api->IsGameFocused()) {
        return;
    }

    for (int vk = VK_MBUTTON; vk < kVkReserved; ++vk) {
        if (vk == kVkUnassigned) {
            continue;
        }

        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!down) {
            g_keysHeldAtRebindStart[vk] = false;
            continue;
        }
        bool genericModifier = vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU;
        if (g_keysHeldAtRebindStart[vk] || genericModifier) {
            continue;
        }

        if (vk != VK_ESCAPE) {
            *g_rebindSlot = GenericModifier(vk);
        }
        g_rebindSlot = nullptr;
        return;
    }
}

void KeyRow(const char* label, int* slot)
{
    if (g_rebindSlot == slot) {
        game::imgui::Text("%s: press a key or mouse button (Esc cancels)...", label);
        return;
    }

    game::imgui::Text("%s: %s", label, KeyName(*slot).c_str());
    char button[96];
    sprintf_s(button, "Change##%s", label);
    if (Button(button) && !g_rebindSlot) {
        g_rebindSlot = slot;
        for (int vk = 0; vk < 256; ++vk) {
            g_keysHeldAtRebindStart[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    }
}

void DrawStatus()
{
    game::imgui::Text("Rotation %.0f deg, tilt %.0f deg", Camera_Yaw(), Camera_Tilt());

    float orbitDistance = 0;
    if (game::Deploying()) {
        game::imgui::TextDisabled("troop placement: stock camera until the mission starts");
    } else if (!Camera_Engaged()) {
        game::imgui::TextDisabled("dormant: stock camera until you use a freecam control");
    } else if (Camera_Orbit(&orbitDistance)) {
        game::imgui::TextDisabled("orbit: distance %.1f", orbitDistance);
    } else {
        game::imgui::TextDisabled("orbit: idle");
    }

    if (Button("Toggle locked / unlocked view")) {
        Camera_ToggleView();
    }
}

bool DrawMouseAndViewSettings()
{
    Settings& s = g_settings;
    bool changed = false;

    changed |= Slider("Mouse sensitivity", &s.mouseSensitivity, kMouseSensitivityRange, "%.2fx");
    changed |= game::imgui::Checkbox("Reverse orbit drag (rotate/tilt)", &s.reverseOrbitDrag);
    changed |= game::imgui::Checkbox("Reverse look-in-place drag", &s.reverseLookDrag);
    changed |= game::imgui::Checkbox("Lock rotation for mouse drags (they only tilt)", &s.lockDragRotation);
    changed |= game::imgui::Checkbox("Lock tilt for mouse drags (they only rotate)", &s.lockDragTilt);

    changed |= Slider("Rotate key step (0 = turn while held)", &s.rotateStep, kRotateStepRange, "%.0f deg");
    if (s.rotateStep <= 0) {
        changed |= Slider("Key rotate speed", &s.keyRotateSpeed, kKeyRotateSpeedRange, "%.0f deg/s");
    }
    changed |= Slider("Max tilt", &s.maxTilt, kMaxTiltRange, "%.0f deg");
    changed |= Slider("Smoothing", &s.smoothing, kSmoothingRange, "%.2f s");

    changed |= game::imgui::Checkbox("Start missions in the locked view", &s.startLocked);
    changed |= game::imgui::Checkbox("Locked view centers on the screen center (else the trooper nearest the cursor)",
                                     &s.lockedScreenCenter);
    changed |= game::imgui::Checkbox("Switching to the locked view puts the cursor at the screen center",
                                     &s.cursorToCenterOnLock);
    changed |= Slider("Locked view angle", &s.lockedTilt, kLockedTiltRange, "%.0f deg");
    changed |= Slider("Locked view heading (0 = north-up)", &s.lockedHeading, kLockedHeadingRange, "%.0f deg");
    changed |=
        Slider("Locked view zoom (0 = stock closest, 1 = stock farthest)", &s.lockedZoom, kLockedZoomRange, "%.2f");

    changed |= game::imgui::Checkbox("Hide cursor while dragging (otherwise a move icon)", &s.hideCursorWhileDragging);
    changed |= game::imgui::Checkbox("After a drag, put the cursor at the screen center (else where it started)",
                                     &s.cursorToCenterAfterDrag);
    changed |= game::imgui::Checkbox("Map icons face the camera (else flat on the ground)", &s.uprightIcons);
    changed |= game::imgui::Checkbox("Camera button in the bottom bar", &s.hudButton);
    return changed;
}

bool DrawZoomSettings()
{
    Settings& s = g_settings;
    bool changed = false;

    // logarithmic, so the close end isn't squeezed
    changed |= Slider("Closest zoom", &s.zoomInFactor, kZoomInFactorRange, "%.3fx stock",
                      static_cast<int>(game::imgui::ImGuiSliderFlags_Logarithmic.Get()));
    changed |= Slider("Farthest zoom", &s.zoomOutFactor, kZoomOutFactorRange, "%.1fx stock");

    changed |= game::imgui::Checkbox("Allow the camera to go outside the map", &s.allowOutsideMap);
    if (!s.allowOutsideMap) {
        changed |= Slider("Room beyond the map edges", &s.edgeRoom, kEdgeRoomRange, "%.0f");
    }
    return changed;
}

void DrawKeys()
{
    Settings& s = g_settings;
    KeyRow("Hold to rotate/tilt with the mouse", &s.rotateModifier);
    KeyRow("Hold to look around in place", &s.lookModifier);
    KeyRow("Rotate left", &s.rotateLeftKey);
    KeyRow("Rotate right", &s.rotateRightKey);
    KeyRow("Toggle locked / unlocked view", &s.toggleViewKey);
    KeyRow("Reset to north-up", &s.resetHeadingKey);
}

bool DrawResetButton()
{
    bool resetArmed = g_resetArmedAt != 0 && GetTickCount() - g_resetArmedAt < kResetConfirmMs;
    if (!Button(resetArmed ? "Click again to reset all settings##reset" : "Reset all settings##reset")) {
        return false;
    }

    if (!resetArmed) {
        g_resetArmedAt = GetTickCount();
        return false;
    }
    g_settings = Settings{};
    g_rebindSlot = nullptr;
    g_resetArmedAt = 0;
    return true;
}

// Returns true if a setting changed.
bool DrawSettings()
{
    bool changed = false;
    if (g_rebindSlot) {
        int before = *g_rebindSlot;
        UpdateRebind();
        // finished with a key or Esc; saving an unchanged value is harmless
        changed |= g_rebindSlot == nullptr && before != 0;
    }

    DrawStatus();
    Separator();
    changed |= DrawMouseAndViewSettings();

    Separator();
    changed |= DrawZoomSettings();

    Separator();
    DrawKeys();

    Separator();
    changed |= DrawResetButton();
    return changed;
}

// Sliders report changes every frame while dragged, so saving waits for a pause.
constexpr DWORD kSaveDelayMs = 500;
bool g_dirty = false;
DWORD g_lastChange = 0;

} // namespace

bool Ui_IsRebinding()
{
    return g_rebindSlot != nullptr;
}

void Ui_SettingsChanged()
{
    g_dirty = true;
    g_lastChange = GetTickCount();
}

void Ui_Draw()
{
    if (g_dirty && GetTickCount() - g_lastChange >= kSaveDelayMs) {
        g_dirty = false;
        Settings_Save();
    }

    if (!g_windowOpen) {
        g_rebindSlot = nullptr;
        return;
    }

    // Outside a mission the keys that close it aren't read, and its capture would report a menu open.
    bool open = game::MissionRunning();
    bool changed = false;
    if (open) {
        int flags = static_cast<int>(game::imgui::ImGuiWindowFlags_NoCollapse.Get() |
                                     game::imgui::ImGuiWindowFlags_AlwaysAutoResize.Get());
        if (game::imgui::Begin("Camera##dk2ml_freecam", &open, flags)) {
            changed = DrawSettings();
            if (Button("Close")) {
                open = false;
            }
        }
        game::imgui::End();
    }

    if (changed) {
        Ui_SettingsChanged();
    }
    if (!open) {
        g_windowOpen = false;
        g_rebindSlot = nullptr;
        g_dirty = false;
        Settings_Save();
    }
}
