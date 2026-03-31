#include "ViewerImGui.h"

#if IS_OBJ_VIEWER

#include <d3d11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using namespace ViewerUI;

void FViewerImGui::Init(HWND Hwnd, ID3D11Device* Device, ID3D11DeviceContext* Context)
{
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(Hwnd);
    ImGui_ImplDX11_Init(Device, Context);
}

void FViewerImGui::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

LRESULT FViewerImGui::ForwardMessage(HWND HWnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    return ImGui_ImplWin32_WndProcHandler(HWnd, Msg, WParam, LParam);
}

bool FViewerImGui::WantCaptureMouse() const
{
    return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
}

FViewerUIOutput FViewerImGui::Draw(const FViewerUIInput& Input)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    FViewerUIOutput Out;
    Out.bOpenRequested  = DrawLoadPanel(Input.MeshName, Input.FPS);
    DrawViewModePanel(Input.CurrentViewMode, Out);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return Out;
}

bool FViewerImGui::DrawLoadPanel(const char* MeshName, float FPS)
{
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::Begin("##LoadPanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);

    const bool bOpenClicked = ImGui::Button("Open OBJ...", ImVec2(-1.f, 0.f));

    ImGui::Separator();

    if (MeshName && MeshName[0] != '\0')
        ImGui::TextUnformatted(MeshName);
    else
        ImGui::TextDisabled("No mesh loaded");

    ImGui::Separator();
    ImGui::Text("FPS: %.1f", FPS);
    ImGui::Spacing();
    ImGui::TextDisabled("RMB drag : orbit");
    ImGui::TextDisabled("MMB drag : pan");
    ImGui::TextDisabled("Scroll   : zoom");

    ImGui::End();
    return bOpenClicked;
}

void FViewerImGui::DrawControlPanel()
{

}

void FViewerImGui::DrawViewModePanel(EViewModeIndex Current, FViewerUIOutput& Out)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 170.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(160.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::Begin("##ViewModePanel", nullptr,
                 ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted("View Mode");
    int Selected = static_cast<int>(Current);
    if (ImGui::Combo("##Shading", &Selected, ViewModeLabels, IM_ARRAYSIZE(ViewModeLabels)))
        Current = static_cast<EViewModeIndex>(Selected);

    ImGui::End();
    Out.SelectedViewMode = Current;
}

#endif // IS_OBJ_VIEWER
