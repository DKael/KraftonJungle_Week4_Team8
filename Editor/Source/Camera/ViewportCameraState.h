#pragma once
#include "Camera/ViewportCamera.h"

struct FViewportCameraState
{
    FVector                 Location        = FVector::Zero();
    FQuat                   Rotation        = FQuat::Identity;
    EViewportProjectionType ProjectionType  = EViewportProjectionType::Perspective;
    float                   FOV             = 3.141592f * 0.5f;
    float                   NearPlane       = 0.1f;
    float                   FarPlane        = 2000.0f;
    float                   OrthoHeight     = 100.0f;

    static FViewportCameraState FromCamera(const FViewportCamera& Camera)
    {
        FViewportCameraState State;
        State.Location       = Camera.GetLocation();
        State.Rotation       = Camera.GetRotation();
        State.ProjectionType = Camera.GetProjectionType();
        State.FOV            = Camera.GetFOV();
        State.NearPlane      = Camera.GetNearPlane();
        State.FarPlane       = Camera.GetFarPlane();
        State.OrthoHeight    = Camera.GetOrthoHeight();
        return State;
    }

    void ApplyToCamera(FViewportCamera& Camera) const
    {
        Camera.SetLocation(Location);
        Camera.SetRotation(Rotation);
        Camera.SetProjectionType(ProjectionType);
        Camera.SetFOV(FOV);
        Camera.SetNearPlane(NearPlane);
        Camera.SetFarPlane(FarPlane);
        Camera.SetOrthoHeight(OrthoHeight);
    }
};
