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
bool Freecam_GameMenuOpen(); // the game's own menus; our input capture doesn't count

// camera/Shadows.cpp
bool Shadows_Hook(const DK2ML_API* api);

// camera/Icons.cpp
bool Icons_Hook(const DK2ML_API* api);
void Icons_OnMissionStart(); // texture ids may be reused after a map load

// camera/Camera*.cpp
void Camera_BeforeUpdate(void* gameClient, int dt); // before GameClient::UpdateCamera
void Camera_AfterUpdate(void* gameClient);          // after it
void Camera_EdgeScrollToScreen(void* gameClient, const Vector3& impulseBefore);
void Camera_PrepareStockTilt(void* gameClient); // before GameInput::UpdateCameraControls
void Camera_AbsorbStockTilt(void* gameClient);  // after it
// requests, applied on the next update
void Camera_ToggleView();
void Camera_WheelRotate(int direction);   // -1 left, +1 right, 90 degrees
void Camera_WheelResetHeading();          // north-up
void Camera_WheelHoldTilt(int direction); // +1 like cam_tilt_up, -1 like cam_tilt_down, 0 released
void Camera_GoToLockedView();
void Camera_OnMissionStart(); // back to stock, dormant
void Camera_OnMapLoaded();
void Camera_OnReplayRewind(void* gameClient); // keeps the view for after the reload
bool Camera_Engaged();                        // false while dormant
bool Camera_LockedViewShown();
float Camera_Yaw();
float Camera_Tilt();
bool Camera_Orbit(float* distance);
float Camera_NeededFarPlane(float cameraHeight); // 0: the game's is fine
// the widened bounds, only for the duration of CollideWithBounds and MoveToPoint_Add
bool Camera_SwapInCollisionBounds(void* camera);
void Camera_RestoreCollisionBounds(void* camera);
const Vector3* Camera_CenterMoveTarget(void* camera, const Vector3* pos);
bool Camera_InExtraCloseZoom(float cameraHeight); // engaged and below the stock minimum height
float Camera_GroundY();                           // height of the ground last looked at

// ui/MenuButton.cpp
void MenuButton_Update();
void MenuButton_OnGuiLoaded();

// ui/HudButton.cpp
void HudButton_Update();
void HudButton_OnGuiLoaded();
bool HudButton_DialogOpen(); // the wheel's reset confirmation shows

// ui/Ui.cpp
void Ui_Draw();            // every frame; also saves pending changes
void Ui_SettingsChanged(); // saves soon
bool Ui_IsRebinding();

// ui/Options.cpp
void Options_Register(const DK2ML_API* api);
