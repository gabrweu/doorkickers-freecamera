// gui/freecam.xml defines the settings buttons as top-level items, because shipping menus.xml or campaign.xml would
// clash with other menu and UI mods. Each frame, every button moves into its Esc menu's row of icon buttons ("Extra
// Buttons": options, how to play, bug report) through the loader's GUI kit, which runs the game's own AddChild action.
// The button then shows and hides with that menu.
// Single missions and campaign missions have different Esc menus, so each menu gets its own button item. "Extra
// Buttons" exists in several menus, so it's looked up inside the menu.
//
// The button's click is an <Action type="Callback">. Attach points it at ToggleWindow through the GUI kit. A GUI reload
// makes new items, so the next frame attaches and wires them again. A menu that had no room for the button may have it
// after a reload (another UI mod turned off in the Mods menu), so a reload also ends giving up.
#include "Freecam.h"
#include "game/Game.h"

namespace {

constexpr const char* kRowItem = "Extra Buttons";

struct MenuButton {
    const char* containerItem; // in gui/freecam.xml: the background and the button, moved together
    const char* buttonItem; // the Button inside it, whose OnClick has the Callback action
    const char* menuItem; // the Esc menu it goes into
    bool gaveUp;
};

MenuButton g_buttons[] = {
    // single missions (menus.xml)
    {"#dk2ml_freecam", "#dk2ml_freecam_button", "Menu_Ingame", false},
    // campaign missions (campaign.xml)
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
        return; // freecam.xml not loaded (yet, or the mod is disabled)
    }

    void* menu = dk2ml::gui::Find(api, b.menuItem);
    void* row = menu ? dk2ml::gui::Find(api, kRowItem, menu) : nullptr;
    if (!row) {
        // a UI mod changed this Esc menu; the settings window still opens with Shift + toggle key
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
        // don't retry every frame; the settings window still opens with Shift + toggle key
        api->Log("failed to attach the Esc menu button (%s)", b.menuItem);
        b.gaveUp = true;
        return;
    }

    dk2ml::gui::Show(api, container);
    api->Log("attached Esc menu button (%s)", b.menuItem);
}

} // namespace

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
