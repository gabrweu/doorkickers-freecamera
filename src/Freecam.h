#pragma once

#include <cstdint>

struct Vector3;
struct DK2ML_API;

constexpr float kPi = 3.14159265f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// a pointer kept in scratch[] from a pre for its post
template <typename T> T* Kept(uint64_t slot)
{
    return reinterpret_cast<T*>(slot);
}

// Freecam.cpp
extern bool g_windowOpen;
// A game menu (Esc menu, etc.) is open. Our own settings window doesn't count.
bool Freecam_GameMenuOpen();

// camera/Shadows.cpp: the shadow fit and culling hooks (GetShadowMapCameraParams, BuildRenderLists)
bool Shadows_Hook(const DK2ML_API* api);

// camera/Icons.cpp: upright map icons and status badges while engaged (everything RenderPaths builds)
bool Icons_Hook(const DK2ML_API* api);
void Icons_OnMissionStart(); // texture ids may be reused after a map load

// camera/Camera.cpp: rotation and tilt of the normal camera
// before GameClient::UpdateCamera: input, easing, angles and orbit
void Camera_BeforeUpdate(void* gameClient, int dt);
// after GameClient::UpdateCamera: our zoom-out limit, and keeping the view over the map
void Camera_AfterUpdate(void* gameClient);
// after GameInput::UpdateMouseScrollPan: edge-scroll relative to the rotated screen
void Camera_EdgeScrollToScreen(void* gameClient, const Vector3& impulseBefore);
// before GameInput::UpdateCameraControls: the baseline for the stock tilt keys
void Camera_PrepareStockTilt(void* gameClient);
// after GameInput::UpdateCameraControls: the stock tilt keys' change becomes freecam tilt
void Camera_AbsorbStockTilt(void* gameClient);
// top-down <-> saved angled view, each with its own zoom; applied on the next game update
void Camera_ToggleView();
// map loaded or restarted: everything back to stock, dormant until first use
void Camera_OnMissionStart();
bool Camera_Engaged(); // false while dormant (stock camera, stock limits)
float Camera_Yaw();
float Camera_Tilt();
// for the settings window's readout: whether a pivot is held, and at what distance
bool Camera_Orbit(float* distance);
// the far clip plane freecam's view needs (0 = the game's is fine)
float Camera_NeededFarPlane(float cameraHeight);
// Camera::CollideWithBounds and Camera::MoveToPoint_Add hooks: our widened limits exist only for the duration of
// those calls
bool Camera_SwapInCollisionBounds(void* camera);
void Camera_RestoreCollisionBounds(void* camera);
// Camera::MoveToPoint_Add hook: the position that centers the requested point in our view (or pos unchanged)
const Vector3* Camera_CenterMoveTarget(void* camera, const Vector3* pos);
bool Camera_InExtraCloseZoom(float cameraHeight); // engaged and below the stock minimum zoom height
float Camera_GroundY();                           // height of the ground the camera last looked at

// ui/MenuButton.cpp: keeps the settings buttons (from gui/freecam.xml) in the Esc menus' rows of icon buttons
void MenuButton_Update();
void MenuButton_OnGuiLoaded(); // the game reloaded its GUI: try the menus again

// ui/Ui.cpp: the in-mission settings window, drawn inside the game's ImGui frame
void Ui_Draw();            // every frame; also saves pending setting changes
void Ui_SettingsChanged(); // a setting changed somewhere: save soon
bool Ui_IsRebinding();     // a key setting is waiting for a new key

// ui/Options.cpp: the settings on the loader's "Native mods" screen (DK2ML_API::AddOption)
void Options_Register(const DK2ML_API* api);
