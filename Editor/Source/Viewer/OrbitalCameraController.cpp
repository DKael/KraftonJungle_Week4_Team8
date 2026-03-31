#include "OrbitalCameraController.h"

void FOrbitalCameraController::AddYawInput(float Value)
{
    if (Camera == nullptr || FMath::IsNearlyZero(Value))
        return;

    Yaw += Value * RotationSpeed;
    Yaw  = FRotator::NormalizeAxis(Yaw);
    UpdateCamera();
}

void FOrbitalCameraController::AddPitchInput(float Value)
{
    if (Camera == nullptr || FMath::IsNearlyZero(Value))
        return;

    Pitch += Value * RotationSpeed;
    Pitch  = FMath::Clamp(Pitch, -89.f, 89.f);
    UpdateCamera();
}

void FOrbitalCameraController::AddPanInput(float DeltaX, float DeltaY)
{
    if (Camera == nullptr)
        return;
    if (FMath::IsNearlyZero(DeltaX) && FMath::IsNearlyZero(DeltaY))
        return;

    const FVector Right = Camera->GetRightVector().GetSafeNormal();
    const FVector Up    = Camera->GetUpVector().GetSafeNormal();

    OrbitPivot += (Right * DeltaX + Up * DeltaY) * PanSpeed;
    UpdateCamera();
}

void FOrbitalCameraController::Dolly(float Value)
{
    if (Camera == nullptr || FMath::IsNearlyZero(Value))
        return;

    OrbitRadius -= Value;
    OrbitRadius  = std::max(OrbitRadius, MinOrbitRadius);
    UpdateCamera();
}

void FOrbitalCameraController::UpdateCamera()
{
    if (Camera == nullptr)
        return;

    const float   PitchRad = FMath::DegreesToRadians(Pitch);
    const float   YawRad   = FMath::DegreesToRadians(Yaw);
    const FVector ToPivot(
        std::cos(PitchRad) * std::cos(YawRad),
        std::cos(PitchRad) * std::sin(YawRad),
        std::sin(PitchRad)
    );

    Camera->SetLocation(OrbitPivot - ToPivot * OrbitRadius);

    const FVector Forward = ToPivot; // already unit-length from sin²+cos²=1
    FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
    if (Right.IsNearlyZero())
        return; // at poles, keep last valid rotation

    const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

    FMatrix RotMat = FMatrix::Identity;
    RotMat.SetAxes(Forward, Right, Up);

    FQuat Q(RotMat);
    Q.Normalize();
    Camera->SetRotation(Q);
}
