// The game functions, globals, struct layouts and enum values Free Camera uses, all resolved by name from the PDB
// through the loader. They're declared here as dk2ml.hpp bindings and defined in Game.cpp. dk2ml::ResolveAll resolves
// them in DK2ML_PluginInit and logs every name a game build doesn't have.
#pragma once

#include <cmath>
#include <cstdint>

#include "dk2ml.hpp"

struct ImVec2 {
    float x, y;
};

struct Vector3 {
    float x, y, z;
};

// clang-format off
inline Vector3 Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 Sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 Mul(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vector3 a) { return std::sqrt(Dot(a, a)); }
// clang-format on

// Camera::m_bounds: Vector3 min, Vector3 max (y is the zoom height range)
struct Bounds {
    Vector3 min, max;
};

namespace game {

extern const DK2ML_API* api;

// --- hooked functions (safe hooks in Freecam.cpp and camera/Shadows.cpp) ---
// The signatures say where the arguments are in DK2ML_Regs.
extern dk2ml::Fn<void(void* gameClient, int dt)> GameClient_UpdateCamera;
extern dk2ml::Fn<void(void* gameInput, int dt)> GameInput_UpdateCameraControls;
extern dk2ml::Fn<void(void* camera, float width, float height, float fov, float zNear, float zFar)>
    Camera_SetProjectionPerspective;
// void GameRenderer::GetShadowMapCameraParams(const Camera& view, Camera& shadowCamera, Matrix& shadowMatrix) const
extern dk2ml::Fn<void(const void* renderer, const void* camera, void* shadowCamera, void* matrix)>
    GameRenderer_GetShadowMapCameraParams;
// Vector3 Camera::CollideWithBounds(int, Vector3 desired, Vector3& velocity) const: the result comes back through a
// hidden pointer, and the by-value Vector3 is passed as a pointer on x64
extern dk2ml::Fn<Vector3*(const void* camera, Vector3* result, int iterations, const Vector3* desired,
                          Vector3* velocity)>
    Camera_CollideWithBounds;
// void GameRenderer::BuildRenderLists(int, int, int, bool): culls against m_camera's frustum
extern dk2ml::Fn<void(void* renderer, int a, int b, int c, bool cull)> GameRenderer_BuildRenderLists;

// --- called functions ---
// Vector3 GameClient::ConvertScreenToMapCoords(float x, float y) const; the Vector3 comes back through a hidden pointer
extern dk2ml::Fn<Vector3*(const void* gameClient, Vector3* result, float x, float y)>
    GameClient_ConvertScreenToMapCoords;
extern dk2ml::Fn<void(void* camera)> Camera_UpdateViewMatrix;
extern dk2ml::Fn<void(void* camera, float left, float right, float bottom, float top, float zNear, float zFar)>
    Camera_SetProjectionOrtho;

// the game's ImGui: functions that survived inlining, and its flag enums (values change between ImGui versions)
namespace imgui {
extern dk2ml::Fn<bool(const char* name, bool* open, int flags)> Begin;
extern dk2ml::Fn<void()> End;
extern dk2ml::Fn<bool(const char* label, bool* v)> Checkbox;
extern dk2ml::Fn<bool(const char* label, int dataType, void* data, const void* min, const void* max, const char* format,
                      int flags)>
    SliderScalar;
extern dk2ml::Fn<bool(const char* label, const ImVec2& size, int flags)> ButtonEx;
extern dk2ml::Fn<void(const char* fmt, ...)> Text;
extern dk2ml::Fn<void(const char* fmt, ...)> TextDisabled;
extern dk2ml::Fn<void(int flags)> SeparatorEx;
extern dk2ml::Enum ImGuiDataType_Float;
extern dk2ml::Enum ImGuiWindowFlags_NoCollapse;
extern dk2ml::Enum ImGuiWindowFlags_AlwaysAutoResize;
extern dk2ml::Enum ImGuiSeparatorFlags_Horizontal;
extern dk2ml::Enum ImGuiSliderFlags_Logarithmic;
} // namespace imgui

// --- globals: the variables; dereference for the object (null while it doesn't exist) ---
extern dk2ml::Global<void*> g_pGameClient;
extern dk2ml::Global<void*> g_pGameGUI;
extern dk2ml::Global<void*> Light_Client_g_pDirectionalLight; // the sun; null on maps without one
extern dk2ml::Global<uint8_t> Human_Client_typeList;          // LinkedList<Human_Client>: every human on the map

// --- fields ---
// sFreelook: the dev menu's FPS camera. Free Camera leaves the camera alone while it's on.
extern dk2ml::Field<uint8_t> GameClient_m_freelook;
extern dk2ml::Field<bool> GameClient_sFreelook_bEnabled; // within m_freelook
extern dk2ml::Field<int> GameClient_m_viewport;          // int[4] x y w h: use its address
extern dk2ml::Field<uint8_t> GameClient_m_camera;        // the view Camera, inside the GameClient: use Camera()
extern dk2ml::Field<uint8_t> GameClient_m_server;        // GameClient::Server, inside the GameClient
extern dk2ml::Field<int> GameClient_Server_clientIndex;  // the local player

extern dk2ml::Field<Vector3> Camera_m_impulse;
extern dk2ml::Field<Vector3> Camera_m_pos;
extern dk2ml::Field<Vector3> Camera_m_actualPos;
extern dk2ml::Field<Vector3> Camera_m_rotAngles;    // degrees (pitch, yaw, roll)
extern dk2ml::Field<bool> Camera_m_beautyMode;
extern dk2ml::Field<Vector3> Camera_m_beautyAngles; // stock tilt (x, set by the game's tilt hotkeys, -20..0)
extern dk2ml::Field<float> Camera_m_minHeight;
extern dk2ml::Field<float> Camera_m_fov;            // degrees
extern dk2ml::Field<float> Camera_m_aspectRatio;
extern dk2ml::Field<Bounds> Camera_m_bounds;
extern dk2ml::Field<float> Camera_m_matView;        // float[16], row-major [R | -R*pos]: use its address
extern dk2ml::TypeSize sizeof_Camera;               // a plain data class (no vtable), safe to copy

extern dk2ml::Field<uint8_t> GameRenderer_m_map;          // GameRenderer::sMap, inside the renderer
extern dk2ml::Field<int> GameRenderer_sMap_width;         // the map spans -width/2..width/2 in X
extern dk2ml::Field<int> GameRenderer_sMap_height;        // -height/2..height/2 in Z
extern dk2ml::Field<float> GameRenderer_sMap_depthBounds; // float[2]: lowest and highest geometry (Y)
extern dk2ml::Field<uint8_t> GameRenderer_m_camera;       // the frame's copy of the view camera, inside the renderer
extern dk2ml::Field<int> GameRenderer_m_viewport;         // int[4] x y w h: use its address

extern dk2ml::Field<uint8_t> GameGUI_m_deploySlots;   // List<sDeploySlot*>: the deploy screen's map slots
extern dk2ml::Field<int> List_DeploySlots_m_elements; // within that list: its count

extern dk2ml::Field<Vector3> Entity_Common_m_forward; // a directional light's direction (pointing down)
extern dk2ml::Field<void*> Entity_Common_m_pTemplate; // a Human_Template for humans
extern dk2ml::Field<float> Entity_Common_m_health;
extern dk2ml::Field<Vector3> Entity_Common_m_origin;  // at the feet

extern dk2ml::Field<void*> LinkedList_Human_head;       // the list's sentinel (also in every node)
extern dk2ml::Field<void*> LinkedList_Human_next;
extern dk2ml::Field<void*> LinkedList_Human_owner;      // the Human_Client the node belongs to
extern dk2ml::Field<uint8_t> Human_Client_linkType;     // its node in typeList
extern dk2ml::Field<uint32_t> Human_Client_m_ownerMask; // bit n = controlled by client n
extern dk2ml::Field<int> Human_Template_type;           // eHumanType
extern dk2ml::Field<bool> Human_Template_isVIP;

// --- enum values ---
extern dk2ml::Enum CGAMESTATE_RUNNING; // GameClient::eCGameState: loading is done, the map is live
extern dk2ml::Enum HUMAN_GOODGUY;      // eHumanType
extern dk2ml::Enum EVENT_CLICK;        // GUI::Item::eItemEventType

inline void* GameClient()
{
    return *g_pGameClient;
}

inline void* Camera(void* gameClient)
{
    return static_cast<char*>(gameClient) + GameClient_m_camera.Offset();
}

inline bool FreelookEnabled(void* gameClient)
{
    return dk2ml::At<bool>(gameClient, GameClient_m_freelook.Offset() + GameClient_sFreelook_bEnabled.Offset());
}

// The local player's living troopers (not VIPs), the same filter GameClient::OnDeployFinished uses to pick one for a
// voiceline. Writes up to max positions, returns how many.
int OwnTroopers(void* gameClient, Vector3* out, int max);

// the loader keeps GameClient::m_state for every plugin
inline bool MissionRunning()
{
    return api->GetGameState() == CGAMESTATE_RUNNING.Get();
}

// Placing troops before the mission starts. GameClient::IsDeploying is inlined away, but OnDeployFinished tears the
// deploy screen down only if GameGUI::m_deploySlots is non-empty: the slots exist exactly while troops are placed.
inline bool Deploying()
{
    void* gui = *g_pGameGUI;
    return gui && dk2ml::At<int>(gui, GameGUI_m_deploySlots.Offset() + List_DeploySlots_m_elements.Offset()) > 0;
}

} // namespace game
