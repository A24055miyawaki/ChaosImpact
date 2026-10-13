#include "ChaosImpactShoulderCameraComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "GameFramework/SpringArmComponent.h"

UChaosImpactShoulderCameraComponent::UChaosImpactShoulderCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UChaosImpactShoulderCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const auto* Character = Cast<AChaosImpactCharacter>(GetOwner()))
	{
		bOriginalCollisionTest = Character->GetCameraBoom()->bDoCollisionTest;
	}
}

FVector2D UChaosImpactShoulderCameraComponent::MouseLookDegrees(FVector2D Delta, float Sensitivity)
{
	return Delta * Sensitivity;
}

FVector2D UChaosImpactShoulderCameraComponent::PadLookDegrees(FVector2D Raw, float DeltaSeconds,
	float DeadZone, float YawRate, float PitchRate)
{
	const float Magnitude = Raw.Size();
	if (!FMath::IsFinite(Magnitude) || Magnitude <= DeadZone) { return FVector2D::ZeroVector; }
	const FVector2D Stick = Raw.GetSafeNormal() * FMath::Clamp((Magnitude - DeadZone) / (1 - DeadZone), 0.f, 1.f);
	return FVector2D(Stick.X * YawRate, -Stick.Y * PitchRate) * DeltaSeconds;
}

void UChaosImpactShoulderCameraComponent::AddLookDegrees(float YawDegrees, float PitchDegrees)
{
	if (!bActive || !FMath::IsFinite(YawDegrees) || !FMath::IsFinite(PitchDegrees)) { return; }
	Yaw = FRotator::NormalizeAxis(Yaw + YawDegrees);
	Pitch = FMath::Clamp(Pitch + PitchDegrees, MinimumPitch, MaximumPitch);
}

FVector UChaosImpactShoulderCameraComponent::MakeReticleAnchor(FVector ViewLocation, FRotator ViewRotation, float ThrowHeight)
{
	FVector Anchor = ViewLocation + FRotator(0,ViewRotation.Yaw,0).Vector() * 1500;
	Anchor.Z = ThrowHeight;
	return Anchor;
}

FVector2D UChaosImpactShoulderCameraComponent::GetLookDegreesForPlayer(const AChaosImpactPlayerController* PC,
	FVector2D Input, float DeltaSeconds) const
{
	if (!PC) { return FVector2D::ZeroVector; }
	const int32 Profile = ChaosImpactSettings::GetPlayerProfile(PC);
	const auto Device = ChaosImpactSettings::GetPlayerDevice(PC);
	const float X = ChaosImpactSettings::GetShoulderSensitivity(Profile, Device) / 100.f;
	const float Y = ChaosImpactSettings::GetShoulderSensitivity(Profile, Device, true) / 100.f;
	return PC->IsUsingGamepad()
		? PadLookDegrees(Input, DeltaSeconds, PadDeadZone, PadYawRate * X, PadPitchRate * Y)
		: MouseLookDegrees(Input, MouseSensitivity * X);
}

void UChaosImpactShoulderCameraComponent::UpdateCamera(float DeltaSeconds)
{
	auto* Character = Cast<AChaosImpactCharacter>(GetOwner());
	auto* PC = Character ? Cast<AChaosImpactPlayerController>(Character->GetController()) : nullptr;
	if (!Character) { return; }
	const bool bRequested = PC && Character->IsLocallyControlled() && PC->IsTrainingShoulderCameraEnabled()
		&& !ChaosImpactSettings::IsMouseOnlyScheme(ChaosImpactSettings::GetPlayerControlScheme(PC));
	if (bRequested && !bRequestedLastFrame)
	{
		Yaw = Character->GetCameraBoom()->GetComponentRotation().Yaw;
		Pitch = -12;
	}
	const bool bWasRequested = bRequestedLastFrame;
	bRequestedLastFrame = bRequested;
	const bool bWasActive = bActive;
	bActive = bRequested && PC->IsGameplayActive() && !Character->IsTrainingMenuFrozen()
		&& !Character->IsEliminated() && !Character->IsChargingNova() && !Character->IsDriving();
	if (bActive || bWasActive) { Character->GetCameraBoom()->bDoCollisionTest = bActive ? true : bOriginalCollisionTest; }
	if ((bRequested || bWasRequested) && PC && PC->IsGameplayActive() && !Character->IsTrainingMenuFrozen())
	{
		PC->RefreshGameplayMouseCapture(bActive);
	}
	if (!bActive || !bWasActive)
	{
		bSkipFirstInput = true;
		return;
	}
	// Discard the transition frame's cursor warp / a stick used to close the menu.
	if (bSkipFirstInput) { bSkipFirstInput = false; return; }
	FVector2D Look;
	if (PC->IsUsingGamepad())
	{
		const bool bSwap = ChaosImpactSettings::GetPlayerControlScheme(PC) == EChaosImpactControlScheme::Normal
			&& ChaosImpactSettings::GetProfile(ChaosImpactSettings::GetPlayerProfile(PC)).bSwapSticks;
		const FVector2D Raw(PC->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_LeftX : EKeys::Gamepad_RightX),
			PC->GetInputAnalogKeyState(bSwap ? EKeys::Gamepad_LeftY : EKeys::Gamepad_RightY));
		Look = GetLookDegreesForPlayer(PC, Raw, DeltaSeconds);
	}
	else
	{
		float X = 0, Y = 0;
		PC->GetInputMouseDelta(X, Y);
		Look = GetLookDegreesForPlayer(PC, FVector2D(X,Y), DeltaSeconds);
	}
	AddLookDegrees(Look.X, Look.Y);
}
