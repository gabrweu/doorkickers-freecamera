// Free Camera for normal play. It rotates (yaw) and tilts the regular top-down camera, and orders still work.
// camera/Camera.cpp reorients the camera, camera/Shadows.cpp refits the shadows, camera/Icons.cpp stands the map icons
// up, ui/MenuButton.cpp adds the Esc menu button and ui/Ui.cpp draws the settings window.
//
// Every game hook is a safe hook (dk2ml.h). The game is built with link-time code generation, so its callers keep
// values in registers that the calling convention lets a callee overwrite. A plain C++ detour on Camera::SetDefaults,
// which the shadow passes call, corrupts the shadow map even when it only passes the call through.
// The frame tick and the map-load signal come from the loader's events. The Esc menu button's click comes from a GUI
// kit callback, and CaptureGameInput keeps clicks on the settings window off the map. None of these needs a hook here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

// The loader reads this from the DLL without running it. It gives the name on the Native mods screen and in the
// Workshop prompt, and the plugin API version Free Camera needs.
DK2ML_PLUGIN_MANIFEST(1, "Free Camera", PLUGIN_VERSION, "era", "https://github.com/gabrweu/doorkickers-freecamera",
                      112);

bool g_windowOpen = false;

namespace {

// GameClient::UpdateCamera(this, int dt) runs every frame while a map is loaded.
int UpdateCameraPre(DK2ML_Regs* r, void*)
{
    void* client = dk2ml::Arg<void*>(r, 0);
    r->scratch[0] = reinterpret_cast<uint64_t>(client);
    Camera_BeforeUpdate(client, dk2ml::Arg<int>(r, 1));
    return DK2ML_CALL_ORIGINAL;
}

void UpdateCameraPost(DK2ML_Regs* r, void*)
{
    Camera_AfterUpdate(Kept<void>(r->scratch[0]));
}

// GameInput::UpdateCameraControls(this, int dt) does the keyboard panning and the game's own tilt hotkeys. Its panning
// directions come from the camera's view matrix, so they already follow the rotated screen. The post turns the stock
// tilt into freecam tilt.
int UpdateCameraControlsPre(DK2ML_Regs* r, void*)
{
    void* client = game::GameClient();
    r->scratch[0] = reinterpret_cast<uint64_t>(client);
    if (client) {
        Camera_PrepareStockTilt(client);
    }
    return DK2ML_CALL_ORIGINAL;
}

void UpdateCameraControlsPost(DK2ML_Regs* r, void*)
{
    void* client = Kept<void>(r->scratch[0]);
    if (client) {
        Camera_AbsorbStockTilt(client);
    }
}

// GameInput::UpdateMouseScrollPan(this, int dt) does the edge-scroll, in world X/Z, and the middle-button drag, which
// sets the impulse from map coordinates. The post turns only the edge-scroll relative to the rotated screen.
int UpdateMouseScrollPanPre(DK2ML_Regs* r, void*)
{
    void* client = game::GameClient();
    uint32_t buttons = *game::PointerState_m_buttonsDown | *game::PointerState_m_buttonsJustDown;
    bool dragging = (buttons & game::kMiddleButtonBit) != 0;
    r->scratch[0] = reinterpret_cast<uint64_t>(dragging ? nullptr : client);
    if (client && !dragging) {
        Vector3 before = game::Camera_m_impulse(game::Camera(client));
        static_assert(sizeof(Vector3) <= 2 * sizeof(uint64_t), "impulse must fit scratch[1..2]");
        memcpy(&r->scratch[1], &before, sizeof(before));
    }
    return DK2ML_CALL_ORIGINAL;
}

void UpdateMouseScrollPanPost(DK2ML_Regs* r, void*)
{
    void* client = Kept<void>(r->scratch[0]);
    if (!client) {
        return;
    }

    Vector3 before;
    memcpy(&before, &r->scratch[1], sizeof(before));
    Camera_EdgeScrollToScreen(client, before);
}

bool g_capturing = false; // whether Free Camera holds CaptureInput

// Runs every frame inside the game's ImGui frame (the loader's FRAME event). It keeps the Esc menu button attached and
// draws the settings window. While the window is open, Free Camera captures the game's input, so clicks on the window
// don't also reach the map. The loader then answers GameGUI::IsAnyMenuOpened with true.
void OnFrame(const DK2ML_Event*, void*)
{
    MenuButton_Update();
    if (g_windowOpen != g_capturing) {
        g_capturing = dk2ml::CaptureInput(game::api, g_windowOpen) ? g_windowOpen : g_capturing;
    }
    Ui_Draw();
}

// The game (re)loaded its GUI (the loader's GUI_LOADED event), so the Esc menus are new items.
void OnGuiLoaded(const DK2ML_Event*, void*)
{
    MenuButton_OnGuiLoaded();
}

// Runs on every map load, restarts included (the loader's MAP_LOADED event). The game resets the view camera for it.
void OnMapLoaded(const DK2ML_Event*, void*)
{
    Camera_OnMissionStart();
    Icons_OnMissionStart();
}

// GameClient::UpdateCamera sizes the clip planes for a top-down camera.
//  - zNear = max(3, height - max(10, floor height) - 0.05). Below the stock minimum height (the extra close-zoom range
//    Free Camera adds), that 3-unit minimum cuts away the floor right under the camera. So there the plane moves
//    closer, only as far as needed. At stock zoom it stays as is, because zNear reaches 3 over tall buildings at
//    normal zoom too, and changing it there changes SSAO.
//  - zFar comes from the zoom range. That's too short for a tilted view looking across the map, so it's raised to what
//    the view needs.
// Camera::SetProjectionPerspective(this, float width, float height, float fov, float zNear, float zFar) takes zNear and
// zFar as the 5th and 6th arguments, on the stack.
int SetProjectionPerspectivePre(DK2ML_Regs* r, void*)
{
    constexpr float kMinNear = 0.1f;
    void* camera = dk2ml::Arg<void*>(r, 0);
    void* client = game::GameClient();
    if (!client || camera != game::Camera(client) || game::FreelookEnabled(client)) {
        return DK2ML_CALL_ORIGINAL;
    }

    float zNear = dk2ml::Arg<float>(r, 4);
    float zFar = dk2ml::Arg<float>(r, 5);
    float cameraHeight = game::Camera_m_pos(camera).y;
    if (Camera_InExtraCloseZoom(cameraHeight)) {
        zNear = std::clamp((cameraHeight - Camera_GroundY()) * 0.5f, kMinNear, zNear);
    }
    zFar = std::max(zFar, Camera_NeededFarPlane(cameraHeight));

    dk2ml::SetArg(r, 4, zNear);
    dk2ml::SetArg(r, 5, zFar);
    return DK2ML_CALL_ORIGINAL;
}

// Camera::CollideWithBounds keeps the camera inside m_bounds. Only this call gets freecam's widened limits. The rest
// of the game, which also reads m_bounds as the map rectangle, always sees the game's own.
int CollideWithBoundsPre(DK2ML_Regs* r, void*)
{
    void* camera = dk2ml::Arg<void*>(r, 0);
    r->scratch[0] = reinterpret_cast<uint64_t>(camera);
    r->scratch[1] = Camera_SwapInCollisionBounds(camera);
    return DK2ML_CALL_ORIGINAL;
}

void CollideWithBoundsPost(DK2ML_Regs* r, void*)
{
    if (r->scratch[1]) {
        Camera_RestoreCollisionBounds(Kept<void>(r->scratch[0]));
    }
}

// Camera::MoveToPoint_Add(this, const Vector3& pos) queues the glide for portrait clicks, selection cycling and
// scripted focus points. It clamps pos to m_bounds, so it gets our widened limits too, and pos becomes the position
// that centers the point in a tilted view.
int MoveToPointPre(DK2ML_Regs* r, void*)
{
    void* camera = dk2ml::Arg<void*>(r, 0);
    const Vector3* pos = dk2ml::Arg<const Vector3*>(r, 1);
    dk2ml::SetArg(r, 1, Camera_CenterMoveTarget(camera, pos));

    r->scratch[0] = reinterpret_cast<uint64_t>(camera);
    r->scratch[1] = Camera_SwapInCollisionBounds(camera);
    return DK2ML_CALL_ORIGINAL;
}

void MoveToPointPost(DK2ML_Regs* r, void*)
{
    if (r->scratch[1]) {
        Camera_RestoreCollisionBounds(Kept<void>(r->scratch[0]));
    }
}

} // namespace

// IsGameMenuOpen asks only about the game's own menus, so Free Camera's input capture doesn't count.
bool Freecam_GameMenuOpen()
{
    return dk2ml::GameMenuOpen(game::api);
}

DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API* api, const DK2ML_PluginInfo* info)
{
    game::api = api;

    // Settings live outside the mod folder, because Steam replaces that folder on every Workshop update. The first run
    // starts from the commented defaults shipped in native\.
    std::wstring configDir = api->GetConfigDir();
    std::wstring shipped = std::wstring(info->pluginDir) + L"freecam.ini";
    std::wstring settings = configDir.empty() ? shipped : configDir + L"freecam.ini";
    bool firstRun = settings != shipped && GetFileAttributesW(settings.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (firstRun) {
        CopyFileW(shipped.c_str(), settings.c_str(), TRUE);
    }
    Settings_Load(settings);

    // logs every name this game build doesn't have (game/Game.cpp)
    if (!dk2ml::ResolveAll(api)) {
        return 2;
    }

    using namespace game;
    bool subscribed = dk2ml::On(api, DK2ML_EVENT_FRAME, OnFrame) &&
                      dk2ml::On(api, DK2ML_EVENT_MAP_LOADED, OnMapLoaded) &&
                      dk2ml::On(api, DK2ML_EVENT_GUI_LOADED, OnGuiLoaded);
    bool hooked = subscribed && dk2ml::Hook(api, GameClient_UpdateCamera, UpdateCameraPre, UpdateCameraPost) &&
                  dk2ml::Hook(api, GameInput_UpdateCameraControls, UpdateCameraControlsPre, UpdateCameraControlsPost) &&
                  dk2ml::Hook(api, GameInput_UpdateMouseScrollPan, UpdateMouseScrollPanPre, UpdateMouseScrollPanPost) &&
                  dk2ml::Hook(api, Camera_SetProjectionPerspective, SetProjectionPerspectivePre) &&
                  dk2ml::Hook(api, Camera_CollideWithBounds, CollideWithBoundsPre, CollideWithBoundsPost) &&
                  dk2ml::Hook(api, Camera_MoveToPoint_Add, MoveToPointPre, MoveToPointPost) && Shadows_Hook(api) &&
                  Icons_Hook(api);
    if (!hooked) {
        return 3;
    }
    Options_Register(api);

    api->Log(
        "ready: hold vk=0x%02X + mouse to rotate/tilt, vk=0x%02X/0x%02X turn, vk=0x%02X toggles top-down/saved angle",
        g_settings.rotateModifier, g_settings.rotateLeftKey, g_settings.rotateRightKey, g_settings.toggleViewKey);
    return 0;
}
