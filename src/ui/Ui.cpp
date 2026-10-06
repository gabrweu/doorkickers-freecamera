// The in-mission settings window, drawn with the game's own ImGui. The game always initializes ImGui and renders it
// every frame in GameClient::Render.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

int* g_rebindSlot = nullptr; // key setting waiting for a new key, if any
bool g_keysHeldAtRebindStart[256] = {};

// "Reset all settings" needs a second click within this time, so a misclick can't wipe everything.
constexpr DWORD kResetConfirmMs = 3000;
DWORD g_resetArmedAt = 0;

// Virtual-key codes the rebind scan skips. 0x07 is unassigned, and 0xFF is reserved (the scan stops below it).
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

// GetAsyncKeyState reports the left and right variants of a modifier, but a binding holds the generic one.
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

// Polls for the next key press. Keys already held when rebinding started count only after they're released.
// Mouse buttons count too, except left and right, because those are the click that started rebinding and the game's
// order buttons. So the scan starts at VK_MBUTTON (4), past VK_LBUTTON, VK_RBUTTON and VK_CANCEL.
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

// The camera's state and the toggle button, above the settings.
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

    if (Button("Toggle top-down / saved angle")) {
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

    changed |= Slider("Rotate key step (0 = turn while held)", &s.rotateStep, kRotateStepRange, "%.0f deg");
    if (s.rotateStep <= 0) {
        changed |= Slider("Key rotate speed", &s.keyRotateSpeed, kKeyRotateSpeedRange, "%.0f deg/s");
    }
    changed |= Slider("Max tilt", &s.maxTilt, kMaxTiltRange, "%.0f deg");
    changed |= Slider("Smoothing", &s.smoothing, kSmoothingRange, "%.2f s");

    changed |= game::imgui::Checkbox("Start missions in the top view", &s.startInTopView);
    changed |= game::imgui::Checkbox("Top view centers on the screen center (else the trooper nearest the cursor)",
                                     &s.topViewScreenCenter);
    changed |= game::imgui::Checkbox("Switching to the top view puts the cursor at the screen center",
                                     &s.cursorToCenterOnTopView);
    changed |= Slider("Top view angle", &s.topDownTilt, kTopDownTiltRange, "%.0f deg");
    changed |=
        Slider("Top view zoom (0 = stock closest, 1 = stock farthest)", &s.topViewZoom, kTopViewZoomRange, "%.2f");

    changed |= game::imgui::Checkbox("Hide cursor while dragging (otherwise a move icon)", &s.hideCursorWhileDragging);
    changed |= game::imgui::Checkbox("After a drag, put the cursor at the screen center (else where it started)",
                                     &s.cursorToCenterAfterDrag);
    changed |= game::imgui::Checkbox("Map icons face the camera (else flat on the ground)", &s.uprightIcons);
    return changed;
}

bool DrawZoomSettings()
{
    Settings& s = g_settings;
    bool changed = false;

    // Logarithmic, so the very close end isn't squeezed into a few pixels.
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
    KeyRow("Toggle top-down / saved angle", &s.toggleViewKey);
    KeyRow("Reset to north-up", &s.resetHeadingKey);
    game::imgui::TextDisabled("Shift + toggle key opens this window.");
}

// Returns true if the settings were reset.
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

// The in-mission window's contents. Returns true if a setting changed.
bool DrawSettings()
{
    bool changed = false;
    if (g_rebindSlot) {
        int before = *g_rebindSlot;
        UpdateRebind();
        // Finished, with a key or Esc. Saving an unchanged value is harmless.
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

// Sliders report changes many times per second while dragged, here and on the loader's Native mods screen. So the
// ini is saved once nothing has changed for a moment, not on every change.
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

    // The window belongs to a mission. Outside one, the keys that close it aren't read, and its input capture would
    // keep the loader reporting a menu open.
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
