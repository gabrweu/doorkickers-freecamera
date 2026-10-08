// The HUD button and its camera wheel (gui/freecam_hud.xml), moved into the bottom bars each frame. The GUI kit has no
// checked state, so each toggle is two Buttons and the matching one is shown. XML has no click-outside event, so the
// hover callbacks track our items and a press anywhere else closes the wheel.
#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

struct HudButton {
    const char* containerItem; // both state Buttons
    const char* offButton;
    const char* onButton; // shown while the locked view is
    const char* barItem;
    bool gaveUp;
};

HudButton g_buttons[] = {
    {"#dk2ml_freecam_lock", "#dk2ml_freecam_lock_off", "#dk2ml_freecam_lock_on", "HUD", false},
    {"#dk2ml_freecam_lock_replay", "#dk2ml_freecam_lock_replay_off", "#dk2ml_freecam_lock_replay_on", "HUD_Replay_Full",
     false},
};

enum class Slice {
    Lock,
    RotateLeft,
    TiltDown,
    Reset,
    NorthUp,
    TiltUp,
    RotateRight,
};

struct WheelSlice {
    const char* item;
    Slice slice;
};

constexpr const char* kWheelItem = "#dk2ml_freecam_wheel";
const WheelSlice kSlices[] = {
    {"#dk2ml_freecam_wheel_lock", Slice::Lock},
    {"#dk2ml_freecam_wheel_rotate_left", Slice::RotateLeft},
    {"#dk2ml_freecam_wheel_tilt_down", Slice::TiltDown},
    {"#dk2ml_freecam_wheel_reset", Slice::Reset},
    {"#dk2ml_freecam_wheel_north", Slice::NorthUp},
    {"#dk2ml_freecam_wheel_tilt_up", Slice::TiltUp},
    {"#dk2ml_freecam_wheel_rotate_right", Slice::RotateRight},
};

struct WheelToggle {
    const char* offItem;
    const char* onItem;
    bool Settings::* setting;
};

const WheelToggle kToggles[] = {
    {"#dk2ml_freecam_wheel_lockrot_off", "#dk2ml_freecam_wheel_lockrot_on", &Settings::lockDragRotation},
    {"#dk2ml_freecam_wheel_locktilt_off", "#dk2ml_freecam_wheel_locktilt_on", &Settings::lockDragTilt},
};

constexpr const char* kDialogItem = "#dk2ml_freecam_reset_dialog";
constexpr const char* kDialogYes = "#dk2ml_freecam_reset_yes";

bool g_wheelWired = false; // until the next GUI reload
bool g_wheelGaveUp = false;
bool g_wheelOpen = false;
void* g_hovered = nullptr; // our item under the cursor
void* g_heldSlice = nullptr; // a held tilt slice

// --- the wheel ---

void ReleaseTilt()
{
    if (g_heldSlice) {
        g_heldSlice = nullptr;
        Camera_WheelHoldTilt(0);
    }
}

void CloseWheel()
{
    ReleaseTilt();
    g_wheelOpen = false;
    g_hovered = nullptr; // a hidden slice gets no "hover end"
    void* wheel = dk2ml::gui::Find(game::api, kWheelItem);
    if (wheel) {
        dk2ml::gui::Show(game::api, wheel, false);
    }
}

// The one wheel moves into the clicked button's item.
void ToggleWheel(const HudButton& b)
{
    if (g_wheelOpen) {
        CloseWheel();
        return;
    }

    const DK2ML_API* api = game::api;
    void* wheel = dk2ml::gui::Find(api, kWheelItem);
    void* container = dk2ml::gui::Find(api, b.containerItem);
    if (!wheel || !container || !g_wheelWired) {
        return;
    }
    if (dk2ml::gui::Parent(api, wheel) != container && !dk2ml::gui::AddChild(api, container, wheel)) {
        api->Log("failed to move the camera wheel to %s", b.barItem);
        return;
    }
    dk2ml::gui::Show(api, wheel);
    g_wheelOpen = true;
}

void Hovered(void* item, float, float, void*)
{
    g_hovered = item;
}

void HoverEnded(void* item, float, float, void*)
{
    if (g_hovered == item) {
        g_hovered = nullptr;
    }
    if (g_heldSlice == item) {
        ReleaseTilt(); // like letting go of the key
    }
}

// No and Esc only hide it (in the XML); Yes calls ResetConfirmed.
void OpenResetDialog()
{
    CloseWheel();
    void* dialog = dk2ml::gui::Find(game::api, kDialogItem);
    if (dialog) {
        dk2ml::gui::Show(game::api, dialog);
    }
}

void ResetConfirmed(void*, float, float, void*)
{
    if (game::Editing()) {
        return;
    }

    Settings_ResetCameraView();
    Ui_SettingsChanged();
    Camera_GoToLockedView();
}

void SliceClicked(void*, float, float, void* user)
{
    if (game::Editing()) {
        return;
    }

    switch (static_cast<const WheelSlice*>(user)->slice) {
    case Slice::RotateLeft: Camera_WheelRotate(-1); break;
    case Slice::RotateRight: Camera_WheelRotate(1); break;
    case Slice::NorthUp: Camera_WheelResetHeading(); break;
    case Slice::Lock: Camera_ToggleView(); break;
    case Slice::Reset: OpenResetDialog(); break;
    case Slice::TiltUp:
    case Slice::TiltDown: break; // while held (TiltPressed)
    }
}

void TiltPressed(void* item, float, float, void* user)
{
    if (game::Editing()) {
        return;
    }

    bool up = static_cast<const WheelSlice*>(user)->slice == Slice::TiltUp;
    g_heldSlice = item;
    Camera_WheelHoldTilt(up ? 1 : -1);
}

void TiltReleased(void*, float, float, void*)
{
    ReleaseTilt();
}

bool WireSlice(void* wheel, const WheelSlice& s)
{
    const DK2ML_API* api = game::api;
    void* item = dk2ml::gui::Find(api, s.item, wheel);
    void* user = const_cast<WheelSlice*>(&s);
    bool wired = item && dk2ml::gui::OnAction(api, item, game::EVENT_CLICK.Get(), SliceClicked, user) &&
                 dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_HOVER.Get(), Hovered) &&
                 dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_HOVER_END.Get(), HoverEnded);

    bool tilt = s.slice == Slice::TiltUp || s.slice == Slice::TiltDown;
    if (wired && tilt) {
        wired = dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_DOWN.Get(), TiltPressed, user) &&
                dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_UP.Get(), TiltReleased);
    }
    return wired;
}

void ShowIfChanged(void* item, bool show)
{
    if (item && dk2ml::gui::IsShown(game::api, item) != show) {
        dk2ml::gui::Show(game::api, item, show);
    }
}

// Returns the Button now shown.
void* SyncToggle(void* wheel, const WheelToggle& t)
{
    const DK2ML_API* api = game::api;
    bool on = g_settings.*t.setting;
    void* onItem = dk2ml::gui::Find(api, t.onItem, wheel);
    void* offItem = dk2ml::gui::Find(api, t.offItem, wheel);
    ShowIfChanged(onItem, on);
    ShowIfChanged(offItem, !on);
    return on ? onItem : offItem;
}

void ToggleClicked(void*, float, float, void* user)
{
    if (game::Editing()) {
        return;
    }

    const WheelToggle& t = *static_cast<const WheelToggle*>(user);
    g_settings.*t.setting = !(g_settings.*t.setting);
    Ui_SettingsChanged();

    // The hidden Button gets no hover end, and the cursor is on its replacement.
    void* wheel = dk2ml::gui::Find(game::api, kWheelItem);
    if (wheel) {
        g_hovered = SyncToggle(wheel, t);
    }
}

bool WireToggleButton(void* wheel, const char* name, const WheelToggle& t)
{
    const DK2ML_API* api = game::api;
    void* item = dk2ml::gui::Find(api, name, wheel);
    void* user = const_cast<WheelToggle*>(&t);
    return item && dk2ml::gui::OnAction(api, item, game::EVENT_CLICK.Get(), ToggleClicked, user) &&
           dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_HOVER.Get(), Hovered) &&
           dk2ml::gui::OnAction(api, item, game::EVENT_CURSOR_HOVER_END.Get(), HoverEnded);
}

void WireWheel()
{
    void* wheel = dk2ml::gui::Find(game::api, kWheelItem);
    if (!wheel) {
        return; // not loaded yet, or the mod is disabled
    }

    for (const WheelSlice& s : kSlices) {
        if (!WireSlice(wheel, s)) {
            game::api->Log("failed to wire the camera wheel (%s)", s.item);
            g_wheelGaveUp = true;
            return;
        }
    }
    for (const WheelToggle& t : kToggles) {
        if (!WireToggleButton(wheel, t.offItem, t) || !WireToggleButton(wheel, t.onItem, t)) {
            game::api->Log("failed to wire the camera wheel (%s)", t.offItem);
            g_wheelGaveUp = true;
            return;
        }
    }

    const DK2ML_API* api = game::api;
    void* dialog = dk2ml::gui::Find(api, kDialogItem);
    void* yes = dialog ? dk2ml::gui::Find(api, kDialogYes, dialog) : nullptr;
    if (!yes || !dk2ml::gui::OnAction(api, yes, game::EVENT_CLICK.Get(), ResetConfirmed)) {
        api->Log("failed to wire the camera wheel (%s)", kDialogYes);
        g_wheelGaveUp = true;
        return;
    }
    g_wheelWired = true;
}

// A press away from our items closes the wheel, except the middle button, which pans.
void UpdateWheel()
{
    // a hidden Button gets no hover end
    if (g_hovered && !dk2ml::gui::IsShown(game::api, g_hovered)) {
        g_hovered = nullptr;
    }

    // a release off the slice sends no cursor up
    if (g_heldSlice && *game::PointerState_m_buttonsDown == 0) {
        ReleaseTilt();
    }

    // the settings window can change the toggles too
    void* wheel = g_wheelWired ? dk2ml::gui::Find(game::api, kWheelItem) : nullptr;
    if (wheel) {
        for (const WheelToggle& t : kToggles) {
            SyncToggle(wheel, t);
        }
    }

    if ((game::Deploying() || game::Editing()) && HudButton_DialogOpen()) {
        dk2ml::gui::Show(game::api, dk2ml::gui::Find(game::api, kDialogItem), false);
    }

    if (!g_wheelOpen) {
        return;
    }
    bool gameBusy = Freecam_GameMenuOpen() || game::Deploying() || game::Editing();
    uint32_t pressed = *game::PointerState_m_buttonsJustDown & ~game::kMiddleButtonBit;
    bool pressedElsewhere = pressed != 0 && !g_hovered;
    if (gameBusy || pressedElsewhere) {
        CloseWheel();
    }
}

// --- the button ---

void ButtonClicked(void*, float, float, void* user)
{
    if (!game::Editing()) {
        ToggleWheel(*static_cast<const HudButton*>(user));
    }
}

bool WireButton(HudButton& b, void* button)
{
    const DK2ML_API* api = game::api;
    return button && dk2ml::gui::OnAction(api, button, game::EVENT_CLICK.Get(), ButtonClicked, &b) &&
           dk2ml::gui::OnAction(api, button, game::EVENT_CURSOR_HOVER.Get(), Hovered) &&
           dk2ml::gui::OnAction(api, button, game::EVENT_CURSOR_HOVER_END.Get(), HoverEnded);
}

// Returns the container once it's in its bar.
void* Attach(HudButton& b)
{
    const DK2ML_API* api = game::api;
    void* container = dk2ml::gui::Find(api, b.containerItem);
    if (!container) {
        return nullptr; // not loaded yet, or the mod is disabled
    }

    void* bar = dk2ml::gui::Find(api, b.barItem);
    if (!bar) {
        // a HUD mod changed the bar
        api->Log("no %s in the GUI, lock button not added there", b.barItem);
        b.gaveUp = true;
        return nullptr;
    }
    if (dk2ml::gui::Parent(api, container) == bar) {
        return container;
    }

    void* off = dk2ml::gui::Find(api, b.offButton, container);
    void* on = dk2ml::gui::Find(api, b.onButton, container);
    bool attached = dk2ml::gui::AddChild(api, bar, container) && WireButton(b, off) && WireButton(b, on);
    if (!attached) {
        api->Log("failed to attach the lock button (%s)", b.barItem);
        b.gaveUp = true;
        return nullptr;
    }

    dk2ml::gui::Show(api, container);
    api->Log("attached lock button (%s)", b.barItem);
    return container;
}

void Sync(const HudButton& b, void* container, bool locked)
{
    const DK2ML_API* api = game::api;
    ShowIfChanged(dk2ml::gui::Find(api, b.onButton, container), locked);
    ShowIfChanged(dk2ml::gui::Find(api, b.offButton, container), !locked);
}

} // namespace

void HudButton_OnGuiLoaded()
{
    for (HudButton& b : g_buttons) {
        b.gaveUp = false;
    }

    // new items, no callbacks yet
    ReleaseTilt();
    g_wheelWired = false;
    g_wheelGaveUp = false;
    g_wheelOpen = false;
    g_hovered = nullptr;
}

bool HudButton_DialogOpen()
{
    void* dialog = dk2ml::gui::Find(game::api, kDialogItem);
    return dialog && dk2ml::gui::IsShown(game::api, dialog);
}

void HudButton_Update()
{
    bool locked = Camera_LockedViewShown();
    for (HudButton& b : g_buttons) {
        if (b.gaveUp) {
            continue;
        }

        void* container = Attach(b);
        if (container) {
            Sync(b, container, locked);
        }
    }

    if (!g_wheelWired && !g_wheelGaveUp) {
        WireWheel();
    }
    UpdateWheel();
}
