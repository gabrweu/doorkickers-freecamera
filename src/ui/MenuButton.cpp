// The settings buttons (gui/freecam.xml), moved into each Esc menu's row of icon buttons. Single and campaign missions
// have different Esc menus, so each gets its own item. A GUI reload makes new items, so they're attached again.
#include "Freecam.h"
#include "game/Game.h"

namespace {

constexpr const char* kRowItem = "Extra Buttons"; // in several menus, so looked up inside the menu

struct MenuButton {
    const char* containerItem;
    const char* buttonItem; // its OnClick has the Callback action
    const char* menuItem;
    bool gaveUp;
};

MenuButton g_buttons[] = {
    {"#dk2ml_freecam", "#dk2ml_freecam_button", "Menu_Ingame", false},
    {"#dk2ml_freecam_campaign", "#dk2ml_freecam_campaign_button", "Menu_Ingame_Campaign", false},
};

void ToggleWindow(void*, float, float, void*)
{
    g_windowOpen = !g_windowOpen;
}

void Attach(MenuButton& b)
{
    const DK2ML_API* api = game::api;
    void* container = dk2ml::gui::Find(api, b.containerItem);
    if (!container) {
        return; // not loaded yet, or the mod is disabled
    }

    void* menu = dk2ml::gui::Find(api, b.menuItem);
    void* row = menu ? dk2ml::gui::Find(api, kRowItem, menu) : nullptr;
    if (!row) {
        // a UI mod changed this menu
        api->Log("%s has no %s, settings button not added there", b.menuItem, kRowItem);
        b.gaveUp = true;
        return;
    }
    if (dk2ml::gui::Parent(api, container) == row) {
        return;
    }

    void* button = dk2ml::gui::Find(api, b.buttonItem, container);
    bool attached = dk2ml::gui::AddChild(api, row, container) &&
                    dk2ml::gui::OnAction(api, button, game::EVENT_CLICK.Get(), ToggleWindow);
    if (!attached) {
        api->Log("failed to attach the Esc menu button (%s)", b.menuItem);
        b.gaveUp = true;
        return;
    }

    dk2ml::gui::Show(api, container);
    api->Log("attached Esc menu button (%s)", b.menuItem);
}

} // namespace

// a menu may have room after a reload (another UI mod turned off)
void MenuButton_OnGuiLoaded()
{
    for (MenuButton& b : g_buttons) {
        b.gaveUp = false;
    }
}

void MenuButton_Update()
{
    for (MenuButton& b : g_buttons) {
        if (!b.gaveUp) {
            Attach(b);
        }
    }
}
