// Plugin entry: settings, bindings, events and the camera hooks.
// Every hook is a safe hook. The game is built with LTCG, so its callers keep values in registers a plain detour would
// clobber.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

// read by the loader without running the DLL
DK2ML_PLUGIN_MANIFEST(1, "Free Camera", PLUGIN_VERSION, "era", "https://github.com/gabrweu/doorkickers-freecamera",
                      112);

bool g_windowOpen = false;

namespace {

// GameClient::UpdateCamera(this, int dt), every frame while a map is loaded.
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

// GameInput::UpdateCameraControls(this, int dt): keyboard panning (already along the view) and the stock tilt keys.
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

// GameInput::UpdateMouseScrollPan(this, int dt): edge-scroll in world X/Z, and the middle-button drag in map
// coordinates. Only the edge-scroll needs turning.
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

bool g_capturing = false; // we hold CaptureInput

// Every frame, inside the game's ImGui frame.
void OnFrame(const DK2ML_Event*, void*)
{
    if (game::Editing()) {
        g_windowOpen = false;
    }
    MenuButton_Update();
    HudButton_Update();
    // keeps clicks on our windows off the map
    bool capture = g_windowOpen || HudButton_DialogOpen();
    if (capture != g_capturing) {
        g_capturing = dk2ml::CaptureInput(game::api, capture) ? capture : g_capturing;
    }
    Ui_Draw();
}

void OnGuiLoaded(const DK2ML_Event*, void*)
{
    MenuButton_OnGuiLoaded();
    HudButton_OnGuiLoaded();
}

// every map load, restarts included
void OnMapLoaded(const DK2ML_Event*, void*)
{
    Camera_OnMapLoaded();
    Icons_OnMissionStart();
}

// GameClient::ReplaySkipTo(this, int time). A time not after m_gameTime restarts the replay, which is a map load, so
// the view is kept for after it. LTCG dropped `this`: it reads g_pGameClient, and its caller sets only the time.
int ReplaySkipToPre(DK2ML_Regs* r, void*)
{
    void* client = game::GameClient();
    int time = dk2ml::Arg<int>(r, 1);
    if (client && time <= game::GameCommon_m_gameTime(client)) {
        Camera_OnReplayRewind(client);
    }
    return DK2ML_CALL_ORIGINAL;
}

// Camera::SetProjectionPerspective(this, width, height, fov, zNear, zFar); zNear and zFar are on the stack.
// The game's zNear is at least 3, which cuts the floor below the stock minimum height, so only there it moves closer
// (SSAO depends on it at stock zoom). Its zFar is too short for a zoomed-out or tilted view.
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

// Camera::CollideWithBounds: only this call sees the widened bounds; the rest of the game reads m_bounds as the map.
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

// Camera::MoveToPoint_Add(this, const Vector3& pos): portrait clicks, selection cycling, focus points. It clamps pos to
// m_bounds, so it gets the widened bounds too.
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

bool Freecam_GameMenuOpen()
{
    return dk2ml::GameMenuOpen(game::api);
}

DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API* api, const DK2ML_PluginInfo* info)
{
    game::api = api;

    // Workshop updates replace the mod folder, so settings live in the config dir, starting from the shipped defaults.
    std::wstring configDir = api->GetConfigDir();
    std::wstring shipped = std::wstring(info->pluginDir) + L"freecam.ini";
    std::wstring settings = configDir.empty() ? shipped : configDir + L"freecam.ini";
    bool firstRun = settings != shipped && GetFileAttributesW(settings.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (firstRun) {
        CopyFileW(shipped.c_str(), settings.c_str(), TRUE);
    }
    Settings_Load(settings);

    // logs every missing name
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
                  dk2ml::Hook(api, Camera_MoveToPoint_Add, MoveToPointPre, MoveToPointPost) &&
                  dk2ml::Hook(api, GameClient_ReplaySkipTo, ReplaySkipToPre) && Shadows_Hook(api) && Icons_Hook(api);
    if (!hooked) {
        return 3;
    }
    Options_Register(api);

    api->Log(
        "ready: hold vk=0x%02X + mouse to rotate/tilt, vk=0x%02X/0x%02X turn, vk=0x%02X toggles locked/unlocked view",
        g_settings.rotateModifier, g_settings.rotateLeftKey, g_settings.rotateRightKey, g_settings.toggleViewKey);
    return 0;
}
