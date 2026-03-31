#include "ObjViewerEngineLoop.h"

#if IS_OBJ_VIEWER

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cmath>
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
    MatLoader    = new Engine::Asset::FMaterialLoader();
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
    const float PitchRad = OrbitPitch * 3.14159265f / 180.0f;
    const float YawRad   = OrbitYaw   * 3.14159265f / 180.0f;

    const float SinP = sinf(PitchRad);
    const float CosP = cosf(PitchRad);
    const float SinY = sinf(YawRad);
    const float CosY = cosf(YawRad);

    // Camera offset direction from target (Z-up spherical coordinates)
    const FVector Dir(SinP * CosY, SinP * SinY, CosP);
    const FVector CamPos = OrbitTarget + Dir * OrbitRadius;

    // Swap up vector near poles to avoid gimbal lock
    const FVector Up = (fabsf(CosP) > 0.99f) ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 0.f, 1.f);

    const float   Aspect  = (CachedH > 0) ? static_cast<float>(CachedW) / static_cast<float>(CachedH)
                                           : 16.f / 9.f;
    const FMatrix ViewMat = FMatrix::MakeViewLookAtLH(CamPos, OrbitTarget, Up);
    const FMatrix ProjMat = FMatrix::MakePerspectiveFovLH(3.14159265f * 0.5f, Aspect, 0.1f, 2000.f);

    SceneView.SetViewMatrix(ViewMat);
    SceneView.SetProjectionMatrix(ProjMat);
    SceneView.SetViewLocation(CamPos);
    SceneView.SetViewRect({ 0, 0, CachedW, CachedH });
    SceneView.SetClipPlanes(0.1f, 2000.f);
}

void FObjViewerEngineLoop::FitCameraToMesh()
{
    if (!LoadedMesh || !LoadedMesh->GetRenderResource()) return;

    const auto& BB     = LoadedMesh->GetRenderResource()->BoundingBox;
    const FVector Center = (BB.Min + BB.Max) * 0.5f;
    const float Diagonal = (BB.Max - BB.Min).Size();

    OrbitTarget = Center;
    OrbitRadius = Diagonal * 1.5f + 0.1f;
    OrbitYaw    = 45.f;
    OrbitPitch  = 30.f;
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

    // Derive a UTF-8 display name from the last path segment
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
            OrbitYaw   += DX * 0.4f;
            OrbitPitch += DY * 0.4f;
            if (OrbitPitch <   5.f) OrbitPitch =   5.f;
            if (OrbitPitch > 175.f) OrbitPitch = 175.f;
            UpdateOrbitCamera();
        }
        else if (bMMBDown)
        {
            // Pan in camera-right and world-up directions
            const float YawRad  = OrbitYaw * 3.14159265f / 180.f;
            const float Speed   = OrbitRadius * 0.002f;
            const FVector Right(cosf(YawRad + 3.14159265f * 0.5f),
                                sinf(YawRad + 3.14159265f * 0.5f), 0.f);
            OrbitTarget = OrbitTarget + Right * (-DX * Speed);
            OrbitTarget = OrbitTarget + FVector(0.f, 0.f, 1.f) * (DY * Speed);
            UpdateOrbitCamera();
        }
        break;
    }

    case WM_MOUSEWHEEL:
        if (!bImGuiWantsMouse)
        {
            const float Delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(WParam)) / WHEEL_DELTA;
            OrbitRadius -= Delta * OrbitRadius * 0.1f;
            if (OrbitRadius <    0.1f) OrbitRadius =    0.1f;
            if (OrbitRadius > 5000.f)  OrbitRadius = 5000.f;
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
