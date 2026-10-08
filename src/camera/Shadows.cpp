// Shadows while engaged. The game fits the shadow map to a frustum with its own short depth range from the camera
// height, and the map is clamp-to-border 0: a close zoom loses shadows and a tilted view sees dark ground. So the fit
// gets an orthographic stand-in looking straight down at the visible map, stretched toward the sun, with sizes in steps
// so texel snapping keeps edges still. Culling gets a wider fov.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Freecam.h"
#include "game/Game.h"

namespace {

// null unless engaged and the dev FPS camera is off
void* EngagedMissionCamera()
{
    void* client = game::GameClient();
    if (!client || !Camera_Engaged() || game::FreelookEnabled(client)) {
        return nullptr;
    }
    return game::Camera(client);
}

// The renderer passes its own copy of the mission camera.
bool IsMissionCameraOrCopy(const void* camera, void* mission)
{
    if (camera == mission) {
        return true;
    }

    auto* c = static_cast<const char*>(camera);
    auto* m = static_cast<const char*>(mission);
    constexpr size_t kMatrixBytes = 16 * sizeof(float);
    int32_t pos = game::Camera_m_pos.Offset();
    int32_t view = game::Camera_m_matView.Offset();
    return memcmp(c + pos, m + pos, sizeof(Vector3)) == 0 && memcmp(c + view, m + view, kMatrixBytes) == 0;
}

constexpr size_t kCameraCopyBytes = 2048; // checked against sizeof_Camera

// the game doesn't use these projections' near and far
constexpr float kUnusedNear = 1.0f;
constexpr float kUnusedFar = 1000.0f;

constexpr float kShadowMargin = 1.0f; // world units
constexpr float kShadowSizeSteps = 4.0f; // box sizes are 2^(k / steps), ~19% apart
constexpr float kShadowTopClearance = 10.0f; // the game's near plane is at height - (mapTop + 9)
constexpr float kMinSunDown = 0.1f; // flatter suns get no stretch
constexpr float kFovSlack = 1.05f; // tan(fov / 2) multiplier

struct ShadowMap {
    float halfWidth, halfDepth; // centered on the origin
    float bottom, top; // lowest and highest geometry
};

// in X/Z
struct MapRect {
    float minX, maxX;
    float minZ, maxZ;
};

bool ReadShadowMap(const void* renderer, ShadowMap* out)
{
    const void* map = static_cast<const char*>(renderer) + game::GameRenderer_m_map.Offset();
    int width = game::GameRenderer_sMap_width(map);
    int height = game::GameRenderer_sMap_height(map);
    const float* depth = &game::GameRenderer_sMap_depthBounds(map);
    if (width <= 0 || height <= 0 || !(depth[1] >= depth[0])) {
        return false;
    }

    *out = {width * 0.5f, height * 0.5f, depth[0], depth[1]};
    return true;
}

// Keeps the part of a convex polygon where dot(n, p - origin) >= offset (Sutherland-Hodgman, one plane).
int ClipPolygon(const Vector3* in, int count, Vector3 n, Vector3 origin, float offset, Vector3* out)
{
    int outCount = 0;
    for (int i = 0; i < count; ++i) {
        const Vector3& a = in[i];
        const Vector3& b = in[(i + 1) % count];
        float da = Dot(n, Sub(a, origin)) - offset;
        float db = Dot(n, Sub(b, origin)) - offset;
        if (da >= 0) {
            out[outCount++] = a;
        }
        if ((da >= 0) != (db >= 0)) {
            out[outCount++] = Add(a, Mul(Sub(b, a), da / (da - db)));
        }
    }
    return outCount;
}

// The part of the map in view, at any height between its bottom and top. Returns false if none is.
bool VisibleMapRect(const void* camera, float aspect, const ShadowMap& map, MapRect* out)
{
    const float* m = &game::Camera_m_matView(camera);
    Vector3 eye = game::Camera_m_pos(camera);
    float fov = game::Camera_m_fov(camera);

    // the rows of R in [R | -R*pos] are the camera axes; the view axis points down
    Vector3 right = {m[0], m[1], m[2]};
    Vector3 up = {m[4], m[5], m[6]};
    Vector3 forward = {m[8], m[9], m[10]};
    if (forward.y > 0) {
        forward = Mul(forward, -1.0f);
    }

    // the frustum's side planes, inward normals
    float ty = std::tan(fov * 0.5f * kDegToRad) * kFovSlack;
    float tx = ty * aspect;
    const Vector3 planes[] = {Sub(Mul(forward, tx), right), Add(Mul(forward, tx), right), Sub(Mul(forward, ty), up),
                              Add(Mul(forward, ty), up)};

    bool any = false;
    out->minX = out->minZ = 1e30f;
    out->maxX = out->maxZ = -1e30f;
    for (float y : {map.bottom, map.top}) {
        Vector3 a[16] = {{-map.halfWidth, y, -map.halfDepth},
                         {map.halfWidth, y, -map.halfDepth},
                         {map.halfWidth, y, map.halfDepth},
                         {-map.halfWidth, y, map.halfDepth}};
        Vector3 b[16];
        int n = ClipPolygon(a, 4, forward, eye, 0.01f, b); // in front of the camera
        for (const Vector3& plane : planes) {
            if (n == 0) {
                break;
            }
            n = ClipPolygon(b, n, plane, eye, 0.0f, a); // each clip adds at most one point, so n stays within 9
            memcpy(b, a, sizeof(Vector3) * n);
        }

        for (int i = 0; i < n; ++i) {
            any = true;
            out->minX = std::min(out->minX, b[i].x);
            out->maxX = std::max(out->maxX, b[i].x);
            out->minZ = std::min(out->minZ, b[i].z);
            out->maxZ = std::max(out->maxZ, b[i].z);
        }
    }
    return any;
}

float QuantizeUp(float size)
{
    return std::exp2(std::ceil(std::log2(std::max(size, 0.5f)) * kShadowSizeSteps) / kShadowSizeSteps);
}

// The visible map, stretched toward the sun, plus a margin.
bool ShadowRect(const void* renderer, const void* camera, const void* light, const ShadowMap& map, MapRect* out)
{
    const int* viewport = &game::GameRenderer_m_viewport(renderer);
    float aspect = viewport[3] > 0 ? static_cast<float>(viewport[2]) / viewport[3] : 16.0f / 9.0f;
    MapRect rect;
    if (!VisibleMapRect(camera, aspect, map, &rect)) {
        return false;
    }

    // anything toward the sun, up to the map's top, can cast into view
    Vector3 sun = game::Entity_Common_m_forward(light);
    if (sun.y < -kMinSunDown) {
        float t = (map.top - map.bottom) / -sun.y;
        float dx = -sun.x * t;
        float dz = -sun.z * t;
        rect.minX = std::min(rect.minX, rect.minX + dx);
        rect.maxX = std::max(rect.maxX, rect.maxX + dx);
        rect.minZ = std::min(rect.minZ, rect.minZ + dz);
        rect.maxZ = std::max(rect.maxZ, rect.maxZ + dz);
    }

    rect.minX = std::max(rect.minX, -map.halfWidth) - kShadowMargin;
    rect.maxX = std::min(rect.maxX, map.halfWidth) + kShadowMargin;
    rect.minZ = std::max(rect.minZ, -map.halfDepth) - kShadowMargin;
    rect.maxZ = std::min(rect.maxZ, map.halfDepth) + kShadowMargin;
    bool empty = rect.maxX <= rect.minX || rect.maxZ <= rect.minZ;
    *out = rect;
    return !empty;
}

// Static, because the original reads it after the pre returns. Only the render thread fits shadows.
alignas(16) char g_shadowStandIn[kCameraCopyBytes];

// A copy of the camera looking straight down at rect's middle, with an orthographic projection covering rect.
void* BuildShadowStandIn(const void* camera, const ShadowMap& map, const MapRect& rect)
{
    float halfX = QuantizeUp((rect.maxX - rect.minX) * 0.5f);
    float halfZ = QuantizeUp((rect.maxZ - rect.minZ) * 0.5f);
    float height = std::max(game::Camera_m_actualPos(camera).y, map.top + kShadowTopClearance);
    Vector3 center = {(rect.minX + rect.maxX) * 0.5f, height, (rect.minZ + rect.maxZ) * 0.5f};

    char* standIn = g_shadowStandIn;
    memcpy(standIn, camera, game::sizeof_Camera.Get());
    game::Camera_m_rotAngles(standIn) = {-90.0f, 0.0f, 0.0f};
    game::Camera_m_pos(standIn) = center;
    game::Camera_m_actualPos(standIn) = center;
    game::Camera_UpdateViewMatrix(standIn);

    // half-extents along the stand-in's screen axes, whichever way yaw 0 maps them
    const float* m = &game::Camera_m_matView(standIn);
    float halfRight = std::fabs(m[0]) * halfX + std::fabs(m[2]) * halfZ;
    float halfUp = std::fabs(m[4]) * halfX + std::fabs(m[6]) * halfZ;

    game::Camera_SetProjectionOrtho(standIn, -halfRight, halfRight, -halfUp, halfUp, kUnusedNear, kUnusedFar);
    return standIn;
}

// GameRenderer::GetShadowMapCameraParams(this, const Camera&, Camera& shadowCamera, Matrix&) const
int GetShadowMapCameraParamsPre(DK2ML_Regs* r, void*)
{
    const void* renderer = dk2ml::Arg<const void*>(r, 0);
    const void* camera = dk2ml::Arg<const void*>(r, 1);
    void* mission = EngagedMissionCamera();
    const void* light = *game::Light_Client_g_pDirectionalLight;
    bool standInFits = game::sizeof_Camera.Get() <= sizeof(g_shadowStandIn);
    if (!mission || !light || !standInFits || !IsMissionCameraOrCopy(camera, mission)) {
        return DK2ML_CALL_ORIGINAL;
    }

    ShadowMap map;
    if (!ReadShadowMap(renderer, &map)) {
        return DK2ML_CALL_ORIGINAL;
    }
    MapRect rect;
    if (!ShadowRect(renderer, camera, light, map, &rect)) {
        return DK2ML_CALL_ORIGINAL;
    }

    dk2ml::SetArg(r, 1, BuildShadowStandIn(camera, map, rect));
    return DK2ML_CALL_ORIGINAL;
}

// GameRenderer::BuildRenderLists culls draws and shadow casters against the camera copy's side planes, treating tall
// objects as 2.1 high, so tilted views lose off-screen shadows and tall objects. Only ComputeFrustumPlanes reads the
// copy there, so its fov is widened for the call.
constexpr float kCullWiden = 1.5f; // tan(fov / 2) multiplier
constexpr float kCullMaxFov = 120.0f;
alignas(16) char g_cullSaved[kCameraCopyBytes];

int BuildRenderListsPre(DK2ML_Regs* r, void*)
{
    r->scratch[0] = 0;
    char* renderer = dk2ml::Arg<char*>(r, 0);
    void* camera = renderer + game::GameRenderer_m_camera.Offset();
    void* mission = EngagedMissionCamera();
    bool copyFits = game::sizeof_Camera.Get() <= sizeof(g_cullSaved);
    if (!mission || !copyFits || !IsMissionCameraOrCopy(camera, mission)) {
        return DK2ML_CALL_ORIGINAL;
    }

    const int* viewport = &game::GameRenderer_m_viewport(renderer);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return DK2ML_CALL_ORIGINAL;
    }

    float fov = game::Camera_m_fov(camera);
    float wider = 2.0f * std::atan(std::tan(fov * 0.5f * kDegToRad) * kCullWiden) * kRadToDeg;
    memcpy(g_cullSaved, camera, game::sizeof_Camera.Get());
    r->scratch[0] = reinterpret_cast<uint64_t>(camera);

    game::Camera_SetProjectionPerspective(camera, static_cast<float>(viewport[2]), static_cast<float>(viewport[3]),
                                          std::min(wider, kCullMaxFov), kUnusedNear, kUnusedFar);
    return DK2ML_CALL_ORIGINAL;
}

void BuildRenderListsPost(DK2ML_Regs* r, void*)
{
    if (r->scratch[0]) {
        memcpy(Kept<void>(r->scratch[0]), g_cullSaved, game::sizeof_Camera.Get());
    }
}

} // namespace

bool Shadows_Hook(const DK2ML_API* api)
{
    return dk2ml::Hook(api, game::GameRenderer_GetShadowMapCameraParams, GetShadowMapCameraParamsPre) &&
           dk2ml::Hook(api, game::GameRenderer_BuildRenderLists, BuildRenderListsPre, BuildRenderListsPost);
}
