// The settings button in the Esc menus. The loader makes and places it (AddMenuButton, loader API 2); an older loader
// has no Esc button for plugins, so there is none (logged), and the settings stay in the main menu's Modloader screen.
#include "dk2ml.hpp"
#include "Freecam.h"

namespace {

void ToggleWindow(void*, float, float, void*)
{
    g_windowOpen = !g_windowOpen;
}

} // namespace

void MenuButton_Register(const DK2ML_API* api)
{
    DK2ML_MenuButton b = {sizeof(b)};
    b.icon = "data/textures/gui/dk2ml_freecam_normal.dds";
    b.iconHover = "data/textures/gui/dk2ml_freecam_hover.dds";
    b.tooltip = "Free Camera settings";
    b.onClick = ToggleWindow;
    dk2ml::gui::AddMenuButton(api, b); // false (logged) without it: the plugin works without the button
}
