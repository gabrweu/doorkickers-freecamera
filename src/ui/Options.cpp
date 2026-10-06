// Free Camera's settings on the loader's "Native mods" screen (main menu), declared with DK2ML_API::AddOption. The
// loader draws them with the game's own checkboxes, sliders and key buttons. It reads g_settings when the screen opens
// and writes it when the player changes something. The in-mission window (Ui.cpp) edits the same values, and both
// save through Ui_SettingsChanged.
#include "dk2ml.h"
#include "Freecam.h"
#include "settings/Settings.h"

namespace {

void Changed(const DK2ML_Option*, void*)
{
    Ui_SettingsChanged();
}

void ResetAll(const DK2ML_Option*, void*)
{
    g_settings = Settings{};
    Ui_SettingsChanged();
}

DK2ML_Option Make(DK2ML_OptionType type, const char* label, void* value, const char* tooltip = nullptr)
{
    DK2ML_Option o = {};
    o.structSize = sizeof(DK2ML_Option);
    o.type = type;
    o.label = label;
    o.tooltip = tooltip;
    o.value = value;
    o.onChange = Changed;
    return o;
}

DK2ML_Option Slider(DK2ML_OptionType type, const char* label, void* value, Range range, const char* format)
{
    DK2ML_Option o = Make(type, label, value);
    o.min = range.min;
    o.max = range.max;
    o.format = format;
    return o;
}

} // namespace

void Options_Register(const DK2ML_API* api)
{
    Settings& s = g_settings;
    DK2ML_Option options[] = {
        Make(DK2ML_OPTION_HEADER, "Mouse", nullptr),
        Slider(DK2ML_OPTION_FLOAT, "Mouse sensitivity", &s.mouseSensitivity, kMouseSensitivityRange, "%.2fx"),
        Make(DK2ML_OPTION_BOOL, "Reverse orbit drag (rotate/tilt)", &s.reverseOrbitDrag),
        Make(DK2ML_OPTION_BOOL, "Reverse look-in-place drag", &s.reverseLookDrag),
        Make(DK2ML_OPTION_BOOL, "Hide the cursor while dragging", &s.hideCursorWhileDragging,
             "Otherwise a move cursor is shown"),
        Make(DK2ML_OPTION_BOOL, "After a drag, put the cursor at the screen center", &s.cursorToCenterAfterDrag,
             "Otherwise it goes back where the drag started"),

        Make(DK2ML_OPTION_HEADER, "Camera", nullptr),
        Slider(DK2ML_OPTION_FLOAT, "Rotate key step (0 = turn while held)", &s.rotateStep, kRotateStepRange,
               "%.0f deg"),
        Slider(DK2ML_OPTION_FLOAT, "Key rotate speed", &s.keyRotateSpeed, kKeyRotateSpeedRange, "%.0f deg/s"),
        Slider(DK2ML_OPTION_FLOAT, "Max tilt", &s.maxTilt, kMaxTiltRange, "%.0f deg"),
        Slider(DK2ML_OPTION_FLOAT, "Smoothing", &s.smoothing, kSmoothingRange, "%.2f s"),
        Make(DK2ML_OPTION_BOOL, "Start missions in the top view", &s.startInTopView,
             "After troop placement; otherwise the stock camera until you use Free Camera"),
        Make(DK2ML_OPTION_BOOL, "Top view centers on the screen center", &s.topViewScreenCenter,
             "Otherwise on the trooper nearest the mouse cursor"),
        Make(DK2ML_OPTION_BOOL, "Top view puts the cursor at the screen center", &s.cursorToCenterOnTopView,
             "When the toggle key switches to the top view"),
        Slider(DK2ML_OPTION_FLOAT, "Top view angle", &s.topDownTilt, kTopDownTiltRange, "%.0f deg"),
        Slider(DK2ML_OPTION_FLOAT, "Closest zoom", &s.zoomInFactor, kZoomInFactorRange, "%.3fx stock"),
        Slider(DK2ML_OPTION_FLOAT, "Farthest zoom", &s.zoomOutFactor, kZoomOutFactorRange, "%.1fx stock"),
        Make(DK2ML_OPTION_BOOL, "Allow the camera to go outside the map", &s.allowOutsideMap),
        Slider(DK2ML_OPTION_FLOAT, "Room beyond the map edges", &s.edgeRoom, kEdgeRoomRange, "%.0f"),
        Make(DK2ML_OPTION_BOOL, "Map icons face the camera", &s.uprightIcons,
             "Waypoint actions, doors and other map icons stand upright while Free Camera is in use"),

        Make(DK2ML_OPTION_HEADER, "Keys", nullptr),
        Make(DK2ML_OPTION_KEY, "Hold to rotate/tilt with the mouse", &s.rotateModifier),
        Make(DK2ML_OPTION_KEY, "Hold to look around in place", &s.lookModifier),
        Make(DK2ML_OPTION_KEY, "Rotate left", &s.rotateLeftKey),
        Make(DK2ML_OPTION_KEY, "Rotate right", &s.rotateRightKey),
        Make(DK2ML_OPTION_KEY, "Toggle top-down / saved angle", &s.toggleViewKey,
             "Shift + this key opens the settings in a mission"),
        Make(DK2ML_OPTION_KEY, "Reset to north-up", &s.resetHeadingKey,
             "Turns the camera back to north, keeping the tilt"),

        Make(DK2ML_OPTION_BUTTON, "Reset to defaults", nullptr),
    };

    // the last entry is the reset button
    options[sizeof(options) / sizeof(options[0]) - 1].onChange = ResetAll;
    for (const DK2ML_Option& o : options) {
        api->AddOption(&o);
    }
}
