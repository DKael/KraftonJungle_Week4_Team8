#pragma once

#if IS_OBJ_VIEWER

#include "Core/CoreMinimal.h"
#include "Renderer/RendererModule.h"
#include "Renderer/SceneView.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Engine::ApplicationCore
{
    class IApplication;
    class FWindowsApplication;
}
namespace Engine::Asset { class UStaticMesh; }
class UAssetManager;
class IAssetLoader;

class FObjViewerEngineLoop
{
public:
    bool  PreInit(HINSTANCE HInstance, uint32 NCmdShow);
    int32 Run();
    void  ShutDown();

private:
    void Tick();
    bool RunFrameOnce();

    void UpdateOrbitCamera();
    void FitCameraToMesh();
    void LoadMesh(const FWString& Path);
    void OpenFileDialog();
    void DrawUI();

    static bool HandleMessage(HWND HWnd, UINT Msg, WPARAM WParam, LPARAM LParam,
                              LRESULT& OutResult, void* UserData);
    bool HandleMessageInternal(HWND HWnd, UINT Msg, WPARAM WParam, LPARAM LParam,
                               LRESULT& OutResult);

    Engine::ApplicationCore::FWindowsApplication* GetWindowsApp() const;

private:
    Engine::ApplicationCore::IApplication* Application = nullptr;
    FRendererModule*                        Renderer    = nullptr;
    UAssetManager*                          AssetManager = nullptr;
    IAssetLoader*                           MeshLoader  = nullptr;
    IAssetLoader*                           MatLoader   = nullptr;

    Engine::Asset::UStaticMesh* LoadedMesh = nullptr;
    FWString                    LoadedMeshPath;
    FString                     LoadedMeshName;

    FSceneView SceneView;

    // Orbit camera: spherical coordinates around OrbitTarget (Z-up)
    FVector OrbitTarget = FVector(0.f, 0.f, 0.f);
    float   OrbitRadius = 5.f;
    float   OrbitYaw    = 45.f;   // degrees, rotation around Z
    float   OrbitPitch  = 30.f;   // degrees, 0 = top, 90 = equator, 180 = bottom

    // Mouse drag state
    bool  bRMBDown   = false;   // right mouse = orbit
    bool  bMMBDown   = false;   // middle mouse = pan
    float LastMouseX = 0.f;
    float LastMouseY = 0.f;

    int32  CachedW   = 0;
    int32  CachedH   = 0;
    double PrevTime  = 0.0;
    float  DeltaTime = 0.0f;
    float  FPS       = 0.0f;

    bool bIsExit                    = false;
    bool bIsInSizeMoveLoop          = false;
    bool bIsRenderingDuringSizeMove = false;
};

#endif // IS_OBJ_VIEWER
