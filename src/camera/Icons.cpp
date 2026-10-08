// Upright map icons while engaged. GameRenderer::RenderPaths builds them as flat quads, mostly in helpers without a
// symbol, so the hooks scope to it.
// - Axis-aligned quads get the screen's axes for their UpdateRenderData call (world X -> right, Z -> up), centered
//   where they were, so clicks still match. Oriented quads and ground markers stay flat.
// - The status column's perspective correction assumes the stock camera, so it's skipped and the badges are laid out
//   on the screen's axes.
// - RenderListSort2D draws quads by their first corner's height, without depth writes, so upright quads keep the
//   stock key (KeepStockDrawOrder).
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

// logs each texture once per map with its class, to fill kGroundTextures
constexpr bool kLogTextures = true;

constexpr float kAxisEpsilon = 1e-3f;
constexpr float kPointEpsilon = 1e-4f; // same point, computed once and passed twice
constexpr int kSelectionTextures = 3; // GameRenderer::m_selectionTexture[3]
constexpr float kMinEyeHeight = 0.01f;
constexpr size_t kMaxVertexBytes = 64; // Render::Vertex3D is 24
constexpr int kQuadCorners = 4;

// file name substrings
constexpr const char* kGroundTextures[] = {
    "selection",
    "waypoints/ignore_path", // the movement circle, also _highlighted
    "waypoints/ignore_potential", // the same while the path is drawn
};

const dk2ml::Global<uint32_t>* const kStatusTextures[] = {
    &game::g_goSilentStatusTexture, &game::g_alwaysWaitStatusTexture, &game::g_speedSyncStatusTexture,
    &game::g_inShadowStatusTexture, &game::g_covertStatusTexture,     &game::g_suspiciousStatusTexture,
    &game::g_dangerAreaPathTexture,
};

bool g_inRenderPaths = false;
const void* g_renderer = nullptr;
Vector3 g_cameraRight; // world space
Vector3 g_cameraUp;
Vector3 g_eye;

// the current operator's status column, from its pair of ConvertMapToScreenCoords calls
struct StatusAnchor {
    Vector3 point; // on the ground
    float firstScreen[2];
    bool awaitingSecond;
    bool valid;
    float liftY; // the badges' height
};

StatusAnchor g_anchor = {};

std::unordered_map<uint32_t, bool> g_groundTexture; // by texture id
std::unordered_set<uint32_t> g_loggedTextures;

// for the post to restore
void* g_changed = nullptr;
Vector3 g_savedOrigin;
Vector3 g_savedForward;
Vector3 g_savedRight;

bool Active()
{
    void* client = game::GameClient();
    return g_settings.uprightIcons && client && Camera_Engaged() && !game::FreelookEnabled(client);
}

// From the renderer's camera copy. Row 1's sign doesn't match screen up, so it's taken from right's X/Z perpendicular.
bool ReadCameraAxes(const void* renderer)
{
    const void* camera = static_cast<const char*>(renderer) + game::GameRenderer_m_camera.Offset();
    const float* m = &game::Camera_m_matView(camera);
    Vector3 right = {m[0], m[1], m[2]};
    Vector3 up = {m[4], m[5], m[6]};
    float rightLen = Length(right);
    float upLen = Length(up);
    if (rightLen < 1e-6f || upLen < 1e-6f) {
        return false;
    }

    Vector3 screenUpXZ = {-right.z, 0, right.x};
    float sign = Dot(up, screenUpXZ) < 0 ? -1.0f : 1.0f;
    g_cameraRight = Mul(right, 1.0f / rightLen);
    g_cameraUp = Mul(up, sign / upLen);
    g_eye = game::Camera_m_pos(camera);
    return true;
}

const char* TextureName(uint32_t texture)
{
    void* manager = *game::g_textureManager;
    const void* info = manager ? game::TextureManagerImpl_Get(manager, texture) : nullptr;
    return info ? &game::Texture_fileName(info) : nullptr;
}

void LogTextureOnce(const void* object, const char* kind)
{
    uint32_t texture = game::RenderObject2D_texture(object);
    if (!kLogTextures || !g_loggedTextures.insert(texture).second) {
        return;
    }

    const char* name = TextureName(texture);
    Vector3 o = game::RenderObject2D_origin(object);
    const float* size = &game::RenderObject2D_size(object);
    game::api->Log("icons: texture %u '%s' %s, origin (%.2f %.2f %.2f) size %.2f x %.2f", texture, name ? name : "?",
                   kind, o.x, o.y, o.z, size[0], size[1]);
}

bool IsGroundTextureName(const char* name)
{
    if (!name) {
        return false;
    }
    for (const char* ground : kGroundTextures) {
        if (strstr(name, ground)) {
            return true;
        }
    }
    return false;
}

bool IsSelectionTexture(uint32_t texture)
{
    const uint32_t* selection = &game::GameRenderer_m_selectionTexture(g_renderer);
    for (int i = 0; i < kSelectionTextures; ++i) {
        if (selection[i] == texture) {
            return true;
        }
    }
    return false;
}

bool IsGroundTexture(const void* object)
{
    uint32_t texture = game::RenderObject2D_texture(object);
    auto known = g_groundTexture.find(texture);
    if (known != g_groundTexture.end()) {
        return known->second;
    }

    bool ground = IsSelectionTexture(texture) || IsGroundTextureName(TextureName(texture));
    g_groundTexture[texture] = ground;
    return ground;
}

bool IsStatusTexture(uint32_t texture)
{
    for (const dk2ml::Global<uint32_t>* status : kStatusTextures) {
        if (texture != 0 && **status == texture) {
            return true;
        }
    }
    return false;
}

// By height, because the same textures also mark path points in danger areas.
bool IsStatusBadge(const void* object)
{
    if (!g_anchor.valid || !IsStatusTexture(game::RenderObject2D_texture(object))) {
        return false;
    }
    Vector3 origin = game::RenderObject2D_origin(object);
    return std::fabs(origin.y - g_anchor.liftY) < kAxisEpsilon;
}

bool IsAxis(Vector3 v)
{
    bool alongX = std::fabs(std::fabs(v.x) - 1.0f) < kAxisEpsilon && std::fabs(v.z) < kAxisEpsilon;
    bool alongZ = std::fabs(std::fabs(v.z) - 1.0f) < kAxisEpsilon && std::fabs(v.x) < kAxisEpsilon;
    return std::fabs(v.y) < kAxisEpsilon && (alongX || alongZ);
}

// world X -> screen right, Z -> screen up, as the stock camera shows them
Vector3 ToScreen(Vector3 v)
{
    return Add(Mul(g_cameraRight, v.x), Mul(g_cameraUp, v.z));
}

// A badge's offset from the anchor is the stock screen layout (x right, z up).
Vector3 StatusBadgeOrigin(Vector3 origin)
{
    Vector3 layout = {origin.x - g_anchor.point.x, 0, origin.z - g_anchor.point.z};
    Vector3 lifted = {g_anchor.point.x, origin.y, g_anchor.point.z};
    return Add(lifted, ToScreen(layout));
}

int RenderPathsPre(DK2ML_Regs* r, void*)
{
    g_renderer = dk2ml::Arg<const void*>(r, 0);
    g_inRenderPaths = g_renderer && ReadCameraAxes(g_renderer);
    g_anchor = {};
    return DK2ML_CALL_ORIGINAL;
}

void RenderPathsPost(DK2ML_Regs*, void*)
{
    g_inRenderPaths = false;
}

// Vector2 GameClient::ConvertMapToScreenCoords(Vector3) const: result pointer in rdx, point pointer in r8. In
// RenderPaths it's called only for the status column: the anchor, then the same X/Z at the badges' height.
enum ProjectionCall : uint64_t { kUntouched, kFirst, kSecond };

int ConvertMapToScreenCoordsPre(DK2ML_Regs* r, void*)
{
    r->scratch[0] = kUntouched;
    if (!g_inRenderPaths || !Active()) {
        return DK2ML_CALL_ORIGINAL;
    }

    const Vector3* point = dk2ml::Arg<const Vector3*>(r, 2);
    bool sameXZ = std::fabs(point->x - g_anchor.point.x) < kPointEpsilon &&
                  std::fabs(point->z - g_anchor.point.z) < kPointEpsilon;
    r->scratch[1] = reinterpret_cast<uint64_t>(dk2ml::Arg<float*>(r, 1));
    if (g_anchor.awaitingSecond && sameXZ) {
        g_anchor.awaitingSecond = false;
        g_anchor.valid = true;
        g_anchor.liftY = point->y;
        r->scratch[0] = kSecond;
        return DK2ML_CALL_ORIGINAL;
    }

    g_anchor = {};
    g_anchor.point = *point;
    r->scratch[0] = kFirst;
    return DK2ML_CALL_ORIGINAL;
}

void ConvertMapToScreenCoordsPost(DK2ML_Regs* r, void*)
{
    float* result = Kept<float>(r->scratch[1]);
    if (r->scratch[0] == kFirst) {
        memcpy(g_anchor.firstScreen, result, sizeof(g_anchor.firstScreen));
        g_anchor.awaitingSecond = true;
    } else if (r->scratch[0] == kSecond) {
        memcpy(result, g_anchor.firstScreen, sizeof(g_anchor.firstScreen)); // no perspective correction
    }
}

void SaveQuad(void* object)
{
    g_changed = object;
    g_savedOrigin = game::RenderObject2D_origin(object);
    g_savedForward = game::RenderObject2D_forward(object);
    g_savedRight = game::RenderObject2D_right(object);
}

const char* KindName(bool badge, bool ground)
{
    if (badge) {
        return "status";
    }
    return ground ? "ground" : "icon";
}

// RenderObject2D::UpdateRenderData(this) rebuilds the quad only while bNeedsUpdate is set. Only the render thread
// draws, so one saved copy is enough.
int UpdateRenderDataPre(DK2ML_Regs* r, void*)
{
    g_changed = nullptr;
    void* object = dk2ml::Arg<void*>(r, 0);
    bool pathsQuad = g_inRenderPaths && object && game::RenderObject2D_bNeedsUpdate(object);
    bool active = Active();
    if (!pathsQuad || !(active || kLogTextures)) {
        return DK2ML_CALL_ORIGINAL;
    }

    Vector3 forward = game::RenderObject2D_forward(object);
    Vector3 right = game::RenderObject2D_right(object);
    bool oriented = !IsAxis(forward) || !IsAxis(right); // arrows and cones
    if (oriented) {
        return DK2ML_CALL_ORIGINAL;
    }

    bool badge = IsStatusBadge(object);
    bool ground = !badge && IsGroundTexture(object);
    LogTextureOnce(object, KindName(badge, ground));
    if (!active || ground) {
        return DK2ML_CALL_ORIGINAL;
    }

    SaveQuad(object);
    if (badge) {
        game::RenderObject2D_origin(object) = StatusBadgeOrigin(g_savedOrigin);
    }
    game::RenderObject2D_forward(object) = ToScreen(forward);
    game::RenderObject2D_right(object) = ToScreen(right);
    return DK2ML_CALL_ORIGINAL;
}

Vector3& VertexPos(char* quad, int index)
{
    return game::Vertex3D_pos(quad + index * game::sizeof_Vertex3D.Get());
}

void SwapVertices(char* quad, int a, int b)
{
    size_t size = game::sizeof_Vertex3D.Get();
    char* first = quad + a * size;
    char* second = quad + b * size;
    char saved[kMaxVertexBytes];
    memcpy(saved, first, size);
    memcpy(first, second, size);
    memcpy(second, saved, size);
}

// Puts a bottom corner first, then scales the quad about the eye until that corner is at stockKey, which leaves the
// picture unchanged. The corners run around the edge, so swapping 0/2 and 1/3 draws the same triangles.
void KeepStockDrawOrder(void* object, float stockKey)
{
    if (game::sizeof_Vertex3D.Get() > kMaxVertexBytes) {
        return;
    }

    char* quad = &game::RenderObject2D_quad(object);
    if (VertexPos(quad, 2).y < VertexPos(quad, 0).y) {
        SwapVertices(quad, 0, 2);
        SwapVertices(quad, 1, 3);
    }

    float cornerBelowEye = g_eye.y - VertexPos(quad, 0).y;
    float keyBelowEye = g_eye.y - stockKey;
    if (cornerBelowEye < kMinEyeHeight || keyBelowEye < kMinEyeHeight) {
        return;
    }

    float scale = keyBelowEye / cornerBelowEye;
    for (int i = 0; i < kQuadCorners; ++i) {
        Vector3& pos = VertexPos(quad, i);
        pos = Add(g_eye, Mul(Sub(pos, g_eye), scale));
    }
}

void UpdateRenderDataPost(DK2ML_Regs*, void*)
{
    if (!g_changed) {
        return;
    }

    KeepStockDrawOrder(g_changed, g_savedOrigin.y); // a flat quad's corners are at the origin's height
    game::RenderObject2D_origin(g_changed) = g_savedOrigin;
    game::RenderObject2D_forward(g_changed) = g_savedForward;
    game::RenderObject2D_right(g_changed) = g_savedRight;
    g_changed = nullptr;
}

} // namespace

void Icons_OnMissionStart()
{
    g_groundTexture.clear();
    g_loggedTextures.clear();
}

bool Icons_Hook(const DK2ML_API* api)
{
    using namespace game;
    return dk2ml::Hook(api, GameRenderer_RenderPaths, RenderPathsPre, RenderPathsPost) &&
           dk2ml::Hook(api, GameClient_ConvertMapToScreenCoords, ConvertMapToScreenCoordsPre,
                       ConvertMapToScreenCoordsPost) &&
           dk2ml::Hook(api, RenderObject2D_UpdateRenderData, UpdateRenderDataPre, UpdateRenderDataPost);
}
