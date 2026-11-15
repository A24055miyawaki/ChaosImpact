#include "ChaosImpactShoulderCameraComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactTrainingTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
	// A curve on the tilt: small tilts turn finely (aiming), a full tilt still turns at the full rate (looking round).
	const float Tilt = FMath::Clamp((Magnitude - DeadZone) / (1 - DeadZone), 0.f, 1.f);
	const FVector2D Stick = Raw.GetSafeNormal() * FMath::Lerp(Tilt, Tilt * Tilt, 0.45f);
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

FVector UChaosImpactShoulderCameraComponent::MakeConvergencePoint(FVector ViewLocation, float ViewYaw, float Depth,
	FVector Character, float MinThrow)
{
	const FVector Flat = FRotator(0, ViewYaw, 0).Vector();
	const float CharacterDepth = static_cast<float>(FVector::DotProduct(Character - ViewLocation, Flat));
	FVector Point = ViewLocation + Flat * FMath::Max(Depth, CharacterDepth + MinThrow);
	Point.Z = Character.Z;
	return Point;
}

FVector UChaosImpactShoulderCameraComponent::GetThrowAimDirection(const FVector& From) const
{
	const FVector Flat = GetFlatAimDirection();
	if (!bAimPointValid) { return Flat; }
	FVector Toward = AimPoint - From;
	Toward.Z = 0;
	if (Toward.SizeSquared() < FMath::Square(150.0f)) { return Flat; }
	Toward.Normalize();
	const float Dot = static_cast<float>(FVector::DotProduct(Flat, Toward));
	if (Dot <= 0.0f) { return Flat; }
	const float Degrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
	if (Degrees <= MaxConvergeDegrees) { return Toward; }
	const float Turn = FVector::CrossProduct(Flat, Toward).Z >= 0 ? MaxConvergeDegrees : -MaxConvergeDegrees;
	return Flat.RotateAngleAxis(Turn, FVector::UpVector);
}

FVector UChaosImpactShoulderCameraComponent::GetDesiredSocketOffset(const bool bCharging) const
{
	FVector Offset = bCharging ? ChargeSocketOffset : SocketOffset;
	Offset.Y *= Side;
	return Offset;
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

namespace
{
	/** Everyone a throw is for: other sides' characters still in, and training targets still up. */
	template <typename FVisit>
	void ForEachShoulderTarget(const AChaosImpactCharacter* Self, FVisit&& Visit)
	{
		UWorld* World = Self->GetWorld();
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			if (*It != Self && !It->IsEliminated() && !It->IsHidden() && !AChaosImpactGameState::AreTeammates(World, Self, *It))
			{
				Visit(static_cast<const AActor*>(*It));
			}
		}
		for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
		{
			if (!It->IsDefeated() && !It->IsHidden())
			{
				Visit(static_cast<const AActor*>(*It));
			}
		}
	}
}

bool UChaosImpactShoulderCameraComponent::FindAssistTarget(const AChaosImpactCharacter* Character, const FVector& ViewLocation,
	float& OutYaw, float& OutOffDegrees) const
{
	const FVector Origin = Character->GetActorLocation();
	const FVector Flat = GetFlatAimDirection();
	const AActor* Best = nullptr;
	float BestOff = AssistAngle;
	float BestYaw = Yaw;
	ForEachShoulderTarget(Character, [&](const AActor* Candidate)
	{
		const FVector At = Candidate->GetActorLocation();
		const FVector FromCharacter = At - Origin;
		if (FromCharacter.SizeSquared2D() > FMath::Square(3500.0f) || FVector::DotProduct(FromCharacter, Flat) <= 0.0) { return; }
		const float TowardYaw = static_cast<float>((At - ViewLocation).Rotation().Yaw);
		const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, TowardYaw));
		if (Off < BestOff) { Best = Candidate; BestOff = Off; BestYaw = TowardYaw; }
	});
	if (!Best) { return false; }
	// Never through a wall.
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactShoulderAssist), false, Character);
	Query.AddIgnoredActor(Best);
	if (Character->GetWorld()->LineTraceSingleByChannel(Hit, Origin + TargetOffset, Best->GetActorLocation(), ECC_Visibility, Query))
	{
		return false;
	}
	OutYaw = BestYaw;
	OutOffDegrees = BestOff;
	return true;
}

void UChaosImpactShoulderCameraComponent::UpdateAimPoint(const AChaosImpactCharacter* Character, const FVector& ViewLocation)
{
	const FVector Direction = FRotator(Pitch, Yaw, 0).Vector();
	const FVector Origin = Character->GetActorLocation();
	// From beside the character on (never a wall behind the camera), out to well past any stage.
	const float StartDepth = FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Origin - ViewLocation, Direction)));
	constexpr float Range = 6000.0f;
	float HitDepth = Range;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactShoulderAim), false, Character);
	if (Character->GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation + Direction * StartDepth, ViewLocation + Direction * Range,
		ECC_Visibility, Query))
	{
		HitDepth = StartDepth + Hit.Distance;
	}
	// Whoever the crosshair is on (a little generous: a ball is wide), before whatever the ray met.
	const AActor* Best = nullptr;
	float BestDepth = HitDepth + 80.0f;
	ForEachShoulderTarget(Character, [&](const AActor* Candidate)
	{
		float Radius = 40.0f, HalfHeight = 90.0f;
		Candidate->GetSimpleCollisionCylinder(Radius, HalfHeight);
		Radius += 30.0f;
		HalfHeight = FMath::Max(HalfHeight + 20.0f, Radius);
		const FVector Center = Candidate->GetActorLocation();
		const float Depth = static_cast<float>(FVector::DotProduct(Center - ViewLocation, Direction));
		if (Depth < StartDepth || Depth > BestDepth) { return; }
		FVector Off = Center - (ViewLocation + Direction * Depth);
		Off.Z *= Radius / HalfHeight;
		if (Off.SizeSquared() <= FMath::Square(Radius)) { Best = Candidate; BestDepth = Depth; }
	});
	AimTarget = Best;
	AimPoint = Best ? Best->GetActorLocation()
		: MakeConvergencePoint(ViewLocation, Yaw, static_cast<float>((Direction * HitDepth).Size2D()), Origin, MinConvergeDistance);
	bAimPointValid = true;
}

void UChaosImpactShoulderCameraComponent::UpdateShoulderSide(const AChaosImpactCharacter* Character, const double Now)
{
	UWorld* World = Character->GetWorld();
	const FVector Pivot = Character->GetActorLocation() + TargetOffset;
	const FRotator Flat(0, Yaw, 0);
	const FVector Right = Flat.RotateVector(FVector::RightVector);
	const FVector Back = -Flat.Vector() * ArmLength * 0.35f;
	const float Reach = FMath::Abs(SocketOffset.Y) + 45.0f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactShoulderSide), false, Character);
	const auto Blocked = [&](const float Sign)
	{
		FHitResult Hit;
		return World->SweepSingleByChannel(Hit, Pivot, Pivot + Right * (Reach * Sign) + Back, FQuat::Identity, ECC_Camera,
			FCollisionShape::MakeSphere(14.0f), Query);
	};
	if (Side > 0)
	{
		SideClearSince = -1;
		if (!Blocked(1)) { SideBlockedSince = -1; return; }
		if (SideBlockedSince < 0) { SideBlockedSince = Now; return; }
		// (Only when the left is any better: in a narrow gap it stays put.)
		if (Now - SideBlockedSince > 0.12 && !Blocked(-1)) { Side = -1; SideBlockedSince = -1; }
		return;
	}
	SideBlockedSince = -1;
	if (Blocked(1)) { SideClearSince = -1; return; }
	if (SideClearSince < 0) { SideClearSince = Now; return; }
	if (Now - SideClearSince > 0.6) { Side = 1; SideClearSince = -1; }
}

void UChaosImpactShoulderCameraComponent::UpdateOwnerVisibility(AChaosImpactCharacter* Character, const FVector& ViewLocation, const bool bShow)
{
	const float Near = static_cast<float>(FVector::Dist(ViewLocation, Character->GetActorLocation() + FVector(0, 0, 40)));
	const bool bHide = !bShow && (bOwnerHidden ? Near < 105.0f : Near < 75.0f);
	if (bHide != bOwnerHidden && Character->GetMesh())
	{
		bOwnerHidden = bHide;
		Character->GetMesh()->SetOwnerNoSee(bHide);
	}
}

void UChaosImpactShoulderCameraComponent::UpdateCamera(float DeltaSeconds)
{
	auto* Character = Cast<AChaosImpactCharacter>(GetOwner());
	auto* PC = Character ? Cast<AChaosImpactPlayerController>(Character->GetController()) : nullptr;
	if (!Character || !Character->GetWorld()) { return; }
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
	const double Now = Character->GetWorld()->GetTimeSeconds();
	if (bActive && !bWasActive)
	{
		// Eased round from the view before (top-down, nova, a drive), over the right shoulder.
		bBlendingIn = true;
		BlendStartedAt = Now;
		Side = 1;
		SideBlockedSince = SideClearSince = -1;
	}
	if (bBlendingIn && Now - BlendStartedAt > 0.7) { bBlendingIn = false; }
	if (bActive || bWasActive) { Character->GetCameraBoom()->bDoCollisionTest = bActive ? true : bOriginalCollisionTest; }
	if ((bRequested || bWasRequested) && PC && PC->IsGameplayActive() && !Character->IsTrainingMenuFrozen())
	{
		PC->RefreshGameplayMouseCapture(bActive);
	}
	const FVector ViewLocation = Character->GetCameraBoom()->GetSocketLocation(USpringArmComponent::SocketName);
	if (!bActive)
	{
		if (bWasActive || bOwnerHidden) { UpdateOwnerVisibility(Character, ViewLocation, true); }
		bAimPointValid = false;
		AimTarget = nullptr;
		bSkipFirstInput = true;
		return;
	}
	UpdateShoulderSide(Character, Now);
	UpdateOwnerVisibility(Character, ViewLocation, bBlendingIn);
	// Discard the transition frame's cursor warp / a stick used to close the menu.
	if (!bWasActive || bSkipFirstInput)
	{
		bSkipFirstInput = bWasActive ? false : true;
		UpdateAimPoint(Character, ViewLocation);
		return;
	}
	const bool bPad = PC->IsUsingGamepad();
	FVector2D Look;
	if (bPad)
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
	// Charging a throw: finer aim (the view also closes in a little).
	const bool bCharging = Character->IsChargingThrow();
	if (bCharging) { Look *= ChargeLookScale; }
	// A pad with エイムアシスト: slower over an enemy, and the crosshair keeps up with one it is already near.
	float AssistYaw = 0, AssistOff = 0;
	const bool bAssist = bPad && ChaosImpactSettings::IsAimAssistOn(PC) && FindAssistTarget(Character, ViewLocation, AssistYaw, AssistOff);
	if (bAssist && AssistOff < AssistAngle * 0.6f) { Look *= AssistFriction; }
	AddLookDegrees(Look.X, Look.Y);
	if (bAssist && (bCharging || !Look.IsNearlyZero()))
	{
		const float Fade = FMath::Clamp((AssistAngle - AssistOff) / FMath::Max(AssistAngle * 0.5f, 0.01f), 0.0f, 1.0f);
		const float Step = AssistFollowDegreesPerSecond * DeltaSeconds;
		AddLookDegrees(FMath::Clamp(FMath::FindDeltaAngleDegrees(Yaw, AssistYaw), -Step, Step) * Fade, 0);
	}
	UpdateAimPoint(Character, ViewLocation);
}
