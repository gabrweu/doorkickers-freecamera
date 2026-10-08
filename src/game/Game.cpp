#include "game/Game.h"

namespace game {

const DK2ML_API* api = nullptr;

// hooked functions
dk2ml::Fn<void(void*, int)> GameClient_UpdateCamera{"GameClient::UpdateCamera"};
dk2ml::Fn<void(void*, int)> GameInput_UpdateCameraControls{"GameInput::UpdateCameraControls"};
dk2ml::Fn<void(void*, int)> GameInput_UpdateMouseScrollPan{"GameInput::UpdateMouseScrollPan"};
dk2ml::Fn<void(void*, float, float, float, float, float)> Camera_SetProjectionPerspective{
    "Camera::SetProjectionPerspective"};
dk2ml::Fn<void(const void*, const void*, void*, void*)> GameRenderer_GetShadowMapCameraParams{
    "?GetShadowMapCameraParams@GameRenderer@@AEBAXAEBVCamera@@AEAV2@AEAVMatrix@@@Z"};
dk2ml::Fn<Vector3*(const void*, Vector3*, int, const Vector3*, Vector3*)> Camera_CollideWithBounds{
    "?CollideWithBounds@Camera@@AEBA?AVVector3@@HV2@AEAV2@@Z"};
dk2ml::Fn<void(void*, int, int, int, bool)> GameRenderer_BuildRenderLists{
    "?BuildRenderLists@GameRenderer@@AEAAXHHH_N@Z"};
dk2ml::Fn<void(void*)> GameRenderer_RenderPaths{"GameRenderer::RenderPaths"};
dk2ml::Fn<void(void*)> RenderObject2D_UpdateRenderData{"RenderObject2D::UpdateRenderData"};
dk2ml::Fn<float*(const void*, float*, const Vector3*)> GameClient_ConvertMapToScreenCoords{
    "GameClient::ConvertMapToScreenCoords"};
dk2ml::Fn<void(void*, const Vector3*)> Camera_MoveToPoint_Add{"Camera::MoveToPoint_Add"};

// called functions
dk2ml::Fn<Vector3*(const void*, Vector3*, float, float)> GameClient_ConvertScreenToMapCoords{
    "GameClient::ConvertScreenToMapCoords"};
dk2ml::Fn<void(void*)> Camera_UpdateViewMatrix{"Camera::UpdateViewMatrix"};
dk2ml::Fn<void(void*, float, float, float, float, float, float)> Camera_SetProjectionOrtho{
    "?SetProjectionOrtho@Camera@@QEAAXMMMMMM@Z"};
dk2ml::Fn<const void*(const void*, uint32_t)> TextureManagerImpl_Get{"?Get@TextureManagerImpl@@UEBAPEBUTexture@@I@Z"};

namespace imgui {
dk2ml::Fn<bool(const char*, bool*, int)> Begin{"ImGui::Begin"};
dk2ml::Fn<void()> End{"ImGui::End"};
dk2ml::Fn<bool(const char*, bool*)> Checkbox{"ImGui::Checkbox"};
dk2ml::Fn<bool(const char*, int, void*, const void*, const void*, const char*, int)> SliderScalar{
    "ImGui::SliderScalar"};
dk2ml::Fn<bool(const char*, const ImVec2&, int)> ButtonEx{"ImGui::ButtonEx"};
dk2ml::Fn<void(const char*, ...)> Text{"ImGui::Text"};
dk2ml::Fn<void(const char*, ...)> TextDisabled{"ImGui::TextDisabled"};
dk2ml::Fn<void(int)> SeparatorEx{"ImGui::SeparatorEx"};
dk2ml::Enum ImGuiDataType_Float{"ImGuiDataType_", "ImGuiDataType_Float"};
dk2ml::Enum ImGuiWindowFlags_NoCollapse{"ImGuiWindowFlags_", "ImGuiWindowFlags_NoCollapse"};
dk2ml::Enum ImGuiWindowFlags_AlwaysAutoResize{"ImGuiWindowFlags_", "ImGuiWindowFlags_AlwaysAutoResize"};
dk2ml::Enum ImGuiSeparatorFlags_Horizontal{"ImGuiSeparatorFlags_", "ImGuiSeparatorFlags_Horizontal"};
dk2ml::Enum ImGuiSliderFlags_Logarithmic{"ImGuiSliderFlags_", "ImGuiSliderFlags_Logarithmic"};
} // namespace imgui

// globals
dk2ml::Global<void*> g_pGameClient{"g_pGameClient"};
dk2ml::Global<void*> g_pGameGUI{"g_pGameGUI"};
dk2ml::Global<void*> g_pEditor{"g_pEditor"};
dk2ml::Global<void*> Light_Client_g_pDirectionalLight{"?g_pDirectionalLight@Light_Client@@2PEBV1@EB"};
dk2ml::Global<uint8_t> Human_Client_typeList{"?typeList@Human_Client@@2V?$LinkedList@VHuman_Client@@@@A"};
dk2ml::Global<uint32_t> PointerState_m_buttonsDown{"?m_buttonsDown@PointerState@@0IA"};
dk2ml::Global<uint32_t> PointerState_m_buttonsJustDown{"?m_buttonsJustDown@PointerState@@0IA"};
dk2ml::Global<void*> g_textureManager{"g_textureManager"};
dk2ml::Global<uint32_t> g_goSilentStatusTexture{"g_goSilentStatusTexture"};
dk2ml::Global<uint32_t> g_alwaysWaitStatusTexture{"g_alwaysWaitStatusTexture"};
dk2ml::Global<uint32_t> g_speedSyncStatusTexture{"g_speedSyncStatusTexture"};
dk2ml::Global<uint32_t> g_inShadowStatusTexture{"g_inShadowStatusTexture"};
dk2ml::Global<uint32_t> g_covertStatusTexture{"g_covertStatusTexture"};
dk2ml::Global<uint32_t> g_suspiciousStatusTexture{"g_suspiciousStatusTexture"};
dk2ml::Global<uint32_t> g_dangerAreaPathTexture{"g_dangerAreaPathTexture"};

// fields
dk2ml::Field<uint8_t> GameClient_m_freelook{"GameClient", "m_freelook"};
dk2ml::Field<bool> GameClient_sFreelook_bEnabled{"GameClient::sFreelook", "bEnabled"};
dk2ml::Field<int> GameClient_m_viewport{"GameClient", "m_viewport"};
dk2ml::Field<uint8_t> GameClient_m_camera{"GameClient", "m_camera"};
dk2ml::Field<Vector3> Camera_m_impulse{"Camera", "m_impulse"};
dk2ml::Field<Vector3> Camera_m_pos{"Camera", "m_pos"};
dk2ml::Field<Vector3> Camera_m_actualPos{"Camera", "m_actualPos"};
dk2ml::Field<Vector3> Camera_m_rotAngles{"Camera", "m_rotAngles"};
dk2ml::Field<bool> Camera_m_beautyMode{"Camera", "m_beautyMode"};
dk2ml::Field<Vector3> Camera_m_beautyAngles{"Camera", "m_beautyAngles"};
dk2ml::Field<float> Camera_m_minHeight{"Camera", "m_minHeight"};
dk2ml::Field<float> Camera_m_fov{"Camera", "m_fov"};
dk2ml::Field<float> Camera_m_aspectRatio{"Camera", "m_aspectRatio"};
dk2ml::TypeSize sizeof_Camera{"Camera"};
dk2ml::Field<Bounds> Camera_m_bounds{"Camera", "m_bounds"};
dk2ml::Field<float> Camera_m_matView{"Camera", "m_matView"};
dk2ml::Field<uint8_t> GameRenderer_m_map{"GameRenderer", "m_map"};
dk2ml::Field<int> GameRenderer_sMap_width{"GameRenderer::sMap", "width"};
dk2ml::Field<int> GameRenderer_sMap_height{"GameRenderer::sMap", "height"};
dk2ml::Field<float> GameRenderer_sMap_depthBounds{"GameRenderer::sMap", "depthBounds"};
dk2ml::Field<uint8_t> GameRenderer_m_camera{"GameRenderer", "m_camera"};
dk2ml::Field<int> GameRenderer_m_viewport{"GameRenderer", "m_viewport"};
dk2ml::Field<uint32_t> GameRenderer_m_selectionTexture{"GameRenderer", "m_selectionTexture"};
dk2ml::Field<uint32_t> RenderObject2D_texture{"RenderObject2D", "texture"};
dk2ml::Field<bool> RenderObject2D_bNeedsUpdate{"RenderObject2D", "bNeedsUpdate"};
dk2ml::Field<Vector3> RenderObject2D_origin{"RenderObject2D", "origin"};
dk2ml::Field<Vector3> RenderObject2D_forward{"RenderObject2D", "forward"};
dk2ml::Field<Vector3> RenderObject2D_right{"RenderObject2D", "right"};
dk2ml::Field<float> RenderObject2D_size{"RenderObject2D", "size"};
dk2ml::Field<char> Texture_fileName{"Texture", "fileName"};
dk2ml::Field<Vector3> Entity_Common_m_forward{"Entity_Common", "m_forward"};
dk2ml::Field<uint8_t> GameGUI_m_deploySlots{"GameGUI", "m_deploySlots"};
dk2ml::Field<int> List_DeploySlots_m_elements{"List<GameGUI::sDeploySlot *>", "m_elements"};
dk2ml::Field<void*> LinkedList_Human_head{"LinkedList<Human_Client>", "head"};
dk2ml::Field<void*> LinkedList_Human_next{"LinkedList<Human_Client>", "next"};
dk2ml::Field<void*> LinkedList_Human_owner{"LinkedList<Human_Client>", "owner"};
dk2ml::Field<uint8_t> Human_Client_linkType{"Human_Client", "linkType"};
dk2ml::Field<uint32_t> Human_Client_m_ownerMask{"Human_Client", "m_ownerMask"};
dk2ml::Field<void*> Entity_Common_m_pTemplate{"Entity_Common", "m_pTemplate"};
dk2ml::Field<float> Entity_Common_m_health{"Entity_Common", "m_health"};
dk2ml::Field<Vector3> Entity_Common_m_origin{"Entity_Common", "m_origin"};
dk2ml::Field<int> Human_Template_type{"Human_Template", "type"};
dk2ml::Field<bool> Human_Template_isVIP{"Human_Template", "isVIP"};
dk2ml::Field<uint8_t> GameClient_m_server{"GameClient", "m_server"};
dk2ml::Field<int> GameClient_Server_clientIndex{"GameClient::Server", "clientIndex"};

// enum values
dk2ml::Enum CGAMESTATE_RUNNING{"GameClient::eCGameState", "CGAMESTATE_RUNNING"};
dk2ml::Enum HUMAN_GOODGUY{"eHumanType", "HUMAN_GOODGUY"};
dk2ml::Enum EVENT_CLICK{"GUI::Item::eItemEventType", "EVENT_CLICK"};

// typeList is an intrusive list. Every node (the list object itself and each human's linkType) holds head, next, prev
// and owner, and head points to the list's sentinel. The walk matches OnDeployFinished's: it starts at the list's next
// and stops at null or back at the sentinel.
int OwnTroopers(void* gameClient, Vector3* out, int max)
{
    constexpr int kMaxNodes = 1024; // a broken list must not hang the game
    constexpr int kMaxClients = 32; // Human_Client::m_ownerMask has one bit per client
    int clientIndex = dk2ml::At<int>(gameClient, GameClient_m_server.Offset() + GameClient_Server_clientIndex.Offset());
    if (clientIndex < 0 || clientIndex >= kMaxClients) {
        return 0;
    }

    void* list = Human_Client_typeList.Address();
    void* sentinel = LinkedList_Human_head(list);
    void* node = LinkedList_Human_next(list);
    int count = 0;
    for (int i = 0; node && node != sentinel && i < kMaxNodes && count < max; ++i) {
        void* human = LinkedList_Human_owner(node);
        node = LinkedList_Human_next(node);
        if (!human) {
            continue;
        }

        void* tmpl = Entity_Common_m_pTemplate(human);
        if (!tmpl || Human_Template_type(tmpl) != HUMAN_GOODGUY.Get() || Human_Template_isVIP(tmpl)) {
            continue;
        }
        bool ours = (Human_Client_m_ownerMask(human) & (1u << clientIndex)) != 0;
        if (!ours || !(Entity_Common_m_health(human) > 0)) {
            continue;
        }

        out[count++] = Entity_Common_m_origin(human);
    }
    return count;
}

} // namespace game
