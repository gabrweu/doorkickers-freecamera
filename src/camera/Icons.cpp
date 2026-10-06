// Upright map icons while freecam is engaged (Freecam.cpp installs the hooks via Icons_Hook).
//
// GameRenderer::RenderPaths builds the map's icons and ground markers as temporary RenderObject2Ds: selection circles,
// look arrows and cones (in BatchSelectionCircles), waypoint icons and their borders, and the status badges beside each
// operator (go silent, always wait, speed sync, and the concealment ones). Most of them are built in helpers without a
// symbol of their own, so the hooks scope everything to RenderPaths.
// Each goes through RenderObject2D::UpdateRenderData, which spans its quad from origin along forward and right (each
// scaled by one of the quad's sides), and the finished quad is copied into a batch. The game lays them flat, built for
// the stock camera: north-up, looking straight down. So a turned camera shows them turned, and a tilted one squashes
// them.
// While freecam is engaged, an icon's quad gets the camera's axes instead: world +X becomes the screen's right and
// world +Z its up, which is what the stock camera shows them as. Waypoint icons stay centered where they were, so
// clicks still find them. Ground markers stay flat. Those are the quads the game orients itself (arrows and cones along
// a direction) and the ones whose texture is a ground marker's.
//
// The status column needs more. The game places it beside the operator at an anchor point, and then corrects it for
// the stock camera's perspective: it projects the anchor at the ground and at the badges' height with
// GameClient::ConvertMapToScreenCoords, and turns the screen difference back into a world X/Z offset, as if screen x
// were world X and screen y world Z. Under a turned or tilted camera that offset throws the badges away from the
// operator. So while engaged the second projection returns the first's result (no correction), and the badges' offsets
// from the anchor, which are the stock screen layout, are laid out along the screen's axes.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "Freecam.h"
#include "game/Game.h"
#include "settings/Settings.h"

namespace {

// Logs every texture RenderPaths draws, once per map, with how it's classified (status, icon or ground).
constexpr bool kLogTextures = true;

constexpr float kAxisEpsilon = 1e-3f; // how close a quad's axis must be to world X or Z to count as unrotated
constexpr float kPointEpsilon = 1e-4f; // same coordinate, for points the game computed once and passed twice
constexpr int kSelectionTextures = 3; // GameRenderer::m_selectionTexture[3]

// Texture file names (substrings) of the flat ground markers, from the discovery log.
constexpr const char* kGroundTextures[] = {
    "selection",
};

// the status badges drawn beside an operator
const dk2ml::Global<uint32_t>* const kStatusTextures[] = {
    &game::g_goSilentStatusTexture, &game::g_alwaysWaitStatusTexture, &game::g_speedSyncStatusTexture,
    &game::g_inShadowStatusTexture, &game::g_covertStatusTexture,     &game::g_suspiciousStatusTexture,
    &game::g_dangerAreaPathTexture,
};

bool g_inRenderPaths = false;
const void* g_renderer = nullptr;
Vector3 g_cameraRight; // world-space screen right of the frame's camera
Vector3 g_cameraUp; // world-space screen up

// The current operator's status column, from its pair of ConvertMapToScreenCoords calls.
struct StatusAnchor {
    Vector3 point; // the first call's point: the anchor on the ground
    float firstScreen[2]; // the first call's result
    bool awaitingSecond; // the first call returned, the second (at the badges' height) is next
    bool valid; // both calls happened: the badges at liftY belong to this anchor
    float liftY; // the second call's height, which is the badges' height
};

StatusAnchor g_anchor = {};

// whether a texture is a ground marker, by texture id; cleared on every map load because ids are reused
std::unordered_map<uint32_t, bool> g_groundTexture;
std::unordered_set<uint32_t> g_loggedTextures;

// what the pre changed, for the post to put back
void* g_changed = nullptr;
Vector3 g_savedOrigin;
Vector3 g_savedForward;
Vector3 g_savedRight;

bool Active()
{
    void* client = game::GameClient();
    return g_settings.uprightIcons && client && Camera_Engaged() && !game::FreelookEnabled(client);
}

// The screen's right and up in world space, from the renderer's copy of the view camera. The view matrix is row-major
// [R | -R*pos], so row 0 is right. Row 1's sign doesn't match screen up (see Camera.cpp's ScreenAxes), so its sign is
// taken from right's X/Z perpendicular, which is screen up at every yaw.
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

// A badge of the current status column: a status texture at the height the anchor's second projection used. The same
// textures also mark path points in danger areas, at path height.
bool IsStatusBadge(const void* object)
{
    if (!g_anchor.valid || !IsStatusTexture(game::RenderObject2D_texture(object))) {
        return false;
    }
    Vector3 origin = game::RenderObject2D_origin(object);
    return std::fabs(origin.y - g_anchor.liftY) < kAxisEpsilon;
}

// along world X or Z, either way
bool IsAxis(Vector3 v)
{
    bool alongX = std::fabs(std::fabs(v.x) - 1.0f) < kAxisEpsilon && std::fabs(v.z) < kAxisEpsilon;
    bool alongZ = std::fabs(std::fabs(v.z) - 1.0f) < kAxisEpsilon && std::fabs(v.x) < kAxisEpsilon;
    return std::fabs(v.y) < kAxisEpsilon && (alongX || alongZ);
}

// world X/Z as the stock camera shows them, mapped onto the current screen
Vector3 ToScreen(Vector3 v)
{
    return Add(Mul(g_cameraRight, v.x), Mul(g_cameraUp, v.z));
}

// A badge's offset from the anchor is the column's layout in stock screen terms (x right, z up). It keeps that layout
// on the current screen, around the anchor at the badge's height.
Vector3 StatusBadgeOrigin(Vector3 origin)
{
    Vector3 layout = {origin.x - g_anchor.point.x, 0, origin.z - g_anchor.point.z};
    Vector3 lifted = {g_anchor.point.x, origin.y, g_anchor.point.z};
    return Add(lifted, ToScreen(layout));
}

// GameRenderer::RenderPaths(this) takes the renderer in rcx.
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

// Vector2 GameClient::ConvertMapToScreenCoords(Vector3) const: the result comes back through a hidden pointer (rdx)
// and the Vector3 is passed as a pointer (r8). Within RenderPaths it's called only for the status column's pair. The
// second call of a pair has the same X/Z as the first.
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

// RenderObject2D::UpdateRenderData(this) rebuilds the quad only while bNeedsUpdate is set, which it is for RenderPaths'
// fresh temporaries. Only the render thread draws, so one saved copy is enough.
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
    bool oriented = !IsAxis(forward) || !IsAxis(right); // arrows and cones keep the game's direction
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

void UpdateRenderDataPost(DK2ML_Regs*, void*)
{
    if (!g_changed) {
        return;
    }
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
