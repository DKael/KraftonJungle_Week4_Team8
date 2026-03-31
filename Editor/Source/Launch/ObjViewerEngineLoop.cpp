#include "ObjViewerEngineLoop.h"

#if IS_OBJ_VIEWER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include "imgui.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Core/Misc/NameSubsystem.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetLoader/StaticMeshLoader.h"
#include "Asset/AssetLoader/MaterialLoader.h"
#include "Asset/StaticMesh.h"

#include "Renderer/Types/RenderItem.h"
#include "Renderer/SceneFrameRenderData.h"
#include "Renderer/Types/ViewMode.h"

#include "ApplicationCore/Windows/WindowsApplication.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ─── PreInit ─────────────────────────────────────────────────────────────────

bool FObjViewerEngineLoop::PreInit(HINSTANCE HInstance, uint32 NCmdShow)
{
    (void)HInstance;
    (void)NCmdShow;

    Engine::Core::Misc::FNameSubsystem::Init();

    Application = Engine::ApplicationCore::FWindowsApplication::Create();
    if (!Application) return false;

    auto* WinApp = GetWindowsApp();
    if (!WinApp) return false;

    if (!WinApp->CreateApplicationWindow(L"OBJ Viewer", 1280, 720))
        return false;

    HWND Hwnd = static_cast<HWND>(Application->GetNativeWindowHandle());
    DragAcceptFiles(Hwnd, TRUE);

    Renderer = new FRendererModule();
    if (!Renderer->StartupModule(Hwnd)) return false;

    AssetManager = new UAssetManager();
    MatLoader    = new FMaterialLoader(AssetManager);
    MeshLoader   = new FStaticMeshLoader(&Renderer->GetRHI(), AssetManager);
    AssetManager->RegisterLoader(MatLoader);
    AssetManager->RegisterLoader(MeshLoader);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(Hwnd);
    ImGui_ImplDX11_Init(Renderer->GetRHI().GetDevice(), Renderer->GetRHI().GetDeviceContext());

    WinApp->SetMessageHandler(&FObjViewerEngineLoop::HandleMessage, this);

    CachedW = Application->GetWindowWidth();
    CachedH = Application->GetWindowHeight();
    Camera = new FViewportCamera();
    Camera->SetFOV(3.141592f * 0.5f); // 90°
    Camera->OnResize(CachedW, CachedH);

    NavController.SetCamera(Camera);
    NavController.SetRotationSpeed(0.4f);
    NavController.SetOrbiting(true);
    NavController.SetOrbitPivot(FVector::Zero());
    NavController.SetOrbitRadius(5.f);
    NavController.SetOrbitAngles(0.f, 0.f);
    NavController.UpdateOrbitCamera();
    UpdateOrbitCamera();

    PrevTime = FPlatformTime::Seconds();
    return true;
}

// ─── Run / Tick ───────────────────────────────────────────────────────────────

int32 FObjViewerEngineLoop::Run()
{
    while (!bIsExit)
        Tick();
    return 0;
}

void FObjViewerEngineLoop::Tick()
{
    Application->PumpMessages();
    if (Application->IsExitRequested())
    {
        bIsExit = true;
        return;
    }
    RunFrameOnce();
}

// ─── ShutDown ─────────────────────────────────────────────────────────────────

void FObjViewerEngineLoop::ShutDown()
{
    if (auto* WinApp = GetWindowsApp())
        WinApp->SetMessageHandler(nullptr, nullptr);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    delete MeshLoader;   MeshLoader   = nullptr;
    delete MatLoader;    MatLoader    = nullptr;
    delete AssetManager; AssetManager = nullptr;

    if (Camera)
    {
        delete Camera;
        Camera = nullptr;
    }

    if (Renderer)
    {
        Renderer->ShutdownModule();
        delete Renderer;
        Renderer = nullptr;
    }

    if (Application)
    {
        Application->DestroyApplicationWindow();
        delete Application;
        Application = nullptr;
    }
}

// ─── Frame ────────────────────────────────────────────────────────────────────

bool FObjViewerEngineLoop::RunFrameOnce()
{
    if (bIsRenderingDuringSizeMove) return false;

    // Handle window resize
    const int32 W = Application->GetWindowWidth();
    const int32 H = Application->GetWindowHeight();
    if (W != CachedW || H != CachedH)
    {
        CachedW = W;
        CachedH = H;
        Renderer->OnWindowResized(W, H);
        UpdateOrbitCamera();
    }

    bIsRenderingDuringSizeMove = true;

    // Frame timing
    const double Now = FPlatformTime::Seconds();
    double Raw = Now - PrevTime;
    PrevTime = Now;
    if (Raw < 1.0 / 1000.0) Raw = 1.0 / 1000.0;
    else if (Raw > 1.0 / 15.0) Raw = 1.0 / 15.0;
    DeltaTime = static_cast<float>(Raw);
    FPS       = static_cast<float>(1.0 / Raw);

    // Build scene frame data
    FSceneFrameRenderData FrameData;
    if (LoadedMesh && LoadedMesh->GetRenderResource())
    {
        FStaticMeshRenderItem Item;
        Item.World          = FMatrix::Identity;
        Item.RenderResource = LoadedMesh->GetRenderResource();
        Item.WorldAABB      = LoadedMesh->GetRenderResource()->BoundingBox;
        Item.State.ObjectId = 1;
        Item.State.SetVisible(true);
        Item.State.SetPickable(false);

        for (uint32 i = 0; i < LoadedMesh->GetNumSections(); ++i)
        {
            const auto* Slot = LoadedMesh->GetMaterialSlot(i);
            FStaticMeshMaterialBinding Binding;
            Binding.Material        = Slot ? Slot->Material        : nullptr;
            Binding.SubMaterialName = Slot ? Slot->SubMaterialName : FString{};
            Item.MaterialBindings.push_back(Binding);
        }
        FrameData.StaticMeshes.push_back(std::move(Item));
    }

    FEditorRenderData RenderData;
    RenderData.SceneView      = &SceneView;
    RenderData.bShowGrid      = false;
    RenderData.bShowWorldAxes = true;

    Renderer->BeginFrame();
    Renderer->SetSceneFrameData(std::move(FrameData));
    Renderer->Render(RenderData, EViewModeIndex::VMI_Lit);
    DrawUI();
    Renderer->EndFrame();

    bIsRenderingDuringSizeMove = false;
    return true;
}

// ─── Orbit camera ─────────────────────────────────────────────────────────────

void FObjViewerEngineLoop::UpdateOrbitCamera()
{
    // NavController has already repositioned the camera; just sync the SceneView.
    SceneView.SetViewMatrix(Camera->GetViewMatrix());
    SceneView.SetProjectionMatrix(Camera->GetProjectionMatrix());
    SceneView.SetViewLocation(Camera->GetLocation());
    SceneView.SetViewRect({ 0, 0, CachedW, CachedH });
    SceneView.SetClipPlanes(0.1f, 2000.f);
}

void FObjViewerEngineLoop::FitCameraToMesh()
{
    if (!LoadedMesh || !LoadedMesh->GetRenderResource()) return;

    const auto& BB       = LoadedMesh->GetRenderResource()->BoundingBox;
    const FVector Center = (BB.Max + BB.Min) * 0.5f;
    const float Diagonal = (BB.Max - BB.Min).Size();

    NavController.SetOrbitPivot(Center);
    NavController.SetOrbitRadius(Diagonal * 1.5f + 0.1f);
    NavController.SetOrbitAngles(0.f, 0.f);
    NavController.UpdateOrbitCamera();
    UpdateOrbitCamera();
}

// ─── Mesh loading ─────────────────────────────────────────────────────────────

void FObjViewerEngineLoop::LoadMesh(const FWString& Path)
{
    UAsset* Asset = AssetManager->Load(Path);
    Engine::Asset::UStaticMesh* SM = Cast<Engine::Asset::UStaticMesh>(Asset);
    if (!SM) return;

    LoadedMesh     = SM;
    LoadedMeshPath = Path;

    const auto LastSep = Path.find_last_of(L"\\/");
    const FWString NameW = (LastSep != FWString::npos) ? Path.substr(LastSep + 1) : Path;
    LoadedMeshName.clear();
    LoadedMeshName.reserve(NameW.size());
    for (wchar_t Ch : NameW)
        LoadedMeshName += static_cast<char>(Ch < 128 ? Ch : '?');

    FitCameraToMesh();
}

void FObjViewerEngineLoop::OpenFileDialog()
{
    wchar_t FilePath[MAX_PATH] = {};
    OPENFILENAMEW OFN  = {};
    OFN.lStructSize    = sizeof(OFN);
    OFN.hwndOwner      = static_cast<HWND>(Application->GetNativeWindowHandle());
    OFN.lpstrFilter = L"OBJ Files (*.obj)\0*.obj\0"
                      L"All Files (*.*)\0*.*\0\0";
    OFN.lpstrFile      = FilePath;
    OFN.nMaxFile       = MAX_PATH;
    OFN.Flags          = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    OFN.lpstrTitle     = L"Open OBJ File";

    if (GetOpenFileNameW(&OFN))
        LoadMesh(FilePath);
}

// ─── ImGui overlay ────────────────────────────────────────────────────────────

void FObjViewerEngineLoop::DrawUI()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::Begin("##ObjViewer", nullptr,
                 ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
                 ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::Button("Open OBJ...", ImVec2(-1.f, 0.f)))
        OpenFileDialog();

    ImGui::Separator();

    if (LoadedMesh)
        ImGui::TextUnformatted(LoadedMeshName.c_str());
    else
        ImGui::TextDisabled("No mesh loaded");

    ImGui::Separator();
    ImGui::Text("FPS: %.1f", FPS);
    ImGui::Spacing();
    ImGui::TextDisabled("RMB drag : orbit");
    ImGui::TextDisabled("MMB drag : pan");
    ImGui::TextDisabled("Scroll   : zoom");

    ImGui::End();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// ─── WndProc handler ──────────────────────────────────────────────────────────

bool FObjViewerEngineLoop::HandleMessage(HWND HWnd, UINT Msg, WPARAM WParam, LPARAM LParam,
                                         LRESULT& OutResult, void* UserData)
{
    auto* Self = static_cast<FObjViewerEngineLoop*>(UserData);
    return Self ? Self->HandleMessageInternal(HWnd, Msg, WParam, LParam, OutResult) : false;
}

bool FObjViewerEngineLoop::HandleMessageInternal(HWND HWnd, UINT Msg, WPARAM WParam,
                                                  LPARAM LParam, LRESULT& OutResult)
{
    // Let ImGui see the message first so WantCaptureMouse is up-to-date
    const LRESULT ImGuiResult = ImGui_ImplWin32_WndProcHandler(HWnd, Msg, WParam, LParam);

    const bool bImGuiWantsMouse =
        ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;

    switch (Msg)
    {
    case WM_CLOSE:
        bIsExit = true;
        if (Application) Application->DestroyApplicationWindow();
        OutResult = 0;
        return true;

    case WM_ENTERSIZEMOVE:
        bIsInSizeMoveLoop = true;
        OutResult = 0;
        return true;

    case WM_EXITSIZEMOVE:
        bIsInSizeMoveLoop = false;
        OutResult = 0;
        return false;

    case WM_SIZE:
        if (bIsInSizeMoveLoop && WParam != SIZE_MINIMIZED)
        {
            const int32 W = static_cast<int32>(LOWORD(LParam));
            const int32 H = static_cast<int32>(HIWORD(LParam));
            if (W > 0 && H > 0 && Renderer)
            {
                CachedW = W; CachedH = H;
                Renderer->OnWindowResized(W, H);
                UpdateOrbitCamera();
                RunFrameOnce();
            }
        }
        break;

    case WM_PAINT:
        if (bIsInSizeMoveLoop)
        {
            PAINTSTRUCT PS = {};
            BeginPaint(HWnd, &PS);
            EndPaint(HWnd, &PS);
            RunFrameOnce();
            OutResult = 0;
            return true;
        }
        break;

    case WM_DROPFILES:
    {
        HDROP Drop = reinterpret_cast<HDROP>(WParam);
        wchar_t DroppedPath[MAX_PATH] = {};
        if (DragQueryFileW(Drop, 0, DroppedPath, MAX_PATH))
        {
            FWString P(DroppedPath);
            const auto Dot = P.find_last_of(L'.');
            if (Dot != FWString::npos)
            {
                FWString Ext = P.substr(Dot);
                for (auto& C : Ext) C = static_cast<wchar_t>(towlower(C));
                if (Ext == L".obj") LoadMesh(P);
            }
        }
        DragFinish(Drop);
        OutResult = 0;
        return true;
    }

    case WM_RBUTTONDOWN:
        if (!bImGuiWantsMouse)
        {
            bRMBDown   = true;
            LastMouseX = static_cast<float>(GET_X_LPARAM(LParam));
            LastMouseY = static_cast<float>(GET_Y_LPARAM(LParam));
            SetCapture(HWnd);
        }
        break;

    case WM_RBUTTONUP:
        bRMBDown = false;
        ReleaseCapture();
        break;

    case WM_MBUTTONDOWN:
        if (!bImGuiWantsMouse)
        {
            bMMBDown   = true;
            LastMouseX = static_cast<float>(GET_X_LPARAM(LParam));
            LastMouseY = static_cast<float>(GET_Y_LPARAM(LParam));
            SetCapture(HWnd);
        }
        break;

    case WM_MBUTTONUP:
        bMMBDown = false;
        ReleaseCapture();
        break;

    case WM_MOUSEMOVE:
    {
        if (!bRMBDown && !bMMBDown) break;

        const float MX = static_cast<float>(GET_X_LPARAM(LParam));
        const float MY = static_cast<float>(GET_Y_LPARAM(LParam));
        const float DX = MX - LastMouseX;
        const float DY = MY - LastMouseY;
        LastMouseX = MX;
        LastMouseY = MY;

        if (bRMBDown)
        {
            // RotationSpeed is set to 0.4, so AddYawInput(DX) == Yaw += DX * 0.4
            NavController.AddYawInput(DX);
            NavController.AddPitchInput(-DY);
            UpdateOrbitCamera();
        }
        else if (bMMBDown)
        {
            // Pan: move orbit pivot (and camera) along the camera's local right/up plane
            NavController.AddPanInput(-DX, DY);
            NavController.UpdateOrbitCamera();
            UpdateOrbitCamera();
        }
        break;
    }

    case WM_MOUSEWHEEL:
        if (!bImGuiWantsMouse)
        {
            const float Delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA;
            // Proportional zoom: 10% of current radius per wheel tick
            NavController.Dolly(Delta * NavController.GetOrbitRadius() * 0.1f);
            UpdateOrbitCamera();
        }
        break;

    default:
        break;
    }

    if (ImGuiResult != 0)
    {
        OutResult = ImGuiResult;
        return true;
    }

    return false;
}

Engine::ApplicationCore::FWindowsApplication* FObjViewerEngineLoop::GetWindowsApp() const
{
    return static_cast<Engine::ApplicationCore::FWindowsApplication*>(Application);
}

#endif // IS_OBJ_VIEWER
