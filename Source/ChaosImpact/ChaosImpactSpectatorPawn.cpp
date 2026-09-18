#include "ChaosImpactSpectatorPawn.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactVersusStage.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	constexpr float SpectatorFlySpeed = 1500.0f;
	constexpr float SpectatorFastSpeed = 3600.0f;
	constexpr float SpectatorStickTurnRate = 150.0f;
	constexpr float SpectatorMouseTurnRate = 0.2f;
}

AChaosImpactSpectatorPawn::AChaosImpactSpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Keeps flying while the match is frozen (offline time stop).
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// Online: spawned by the host for one spectator and sent to that machine only, which moves it itself.
	bReplicates = true;
	SetReplicatingMovement(false);
	bOnlyRelevantToOwner = true;
	// Moved and turned here from the raw keys and sticks, not the default spectator bindings.
	bAddDefaultMovementBindings = false;
	BaseEyeHeight = 0.0f;
	if (USphereComponent* Sphere = GetCollisionComponent())
	{
		// A free camera: passes through everything.
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UStaticMeshComponent* Mesh = GetMeshComponent())
	{
		Mesh->SetHiddenInGame(true);
	}
	if (UPawnMovementComponent* Movement = GetMovementComponent())
	{
		Movement->SetComponentTickEnabled(false);
	}
	FParse::Value(FCommandLine::Get(), TEXT("CIDevSpectateCycle="), DevCycleSeconds);
}

void AChaosImpactSpectatorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Hud)
	{
		Hud->RemoveFromParent();
		Hud = nullptr;
	}
	if (AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetController()))
	{
		Viewer->SetGameplayUIHidden(false);
		Viewer->SetSpectateTimeStopped(false);
	}
	Super::EndPlay(EndPlayReason);
}

void AChaosImpactSpectatorPawn::PlaceOverStage(const FVector& StageCenter)
{
	const FVector Eye = StageCenter + FVector(0.0f, -2600.0f, 2000.0f);
	SetActorLocation(ClampToStage(Eye));
	if (AController* Viewer = GetController())
	{
		Viewer->SetControlRotation((StageCenter - Eye).Rotation());
	}
	bPlaced = true;
	bPlacedForMatch = true;
}

void AChaosImpactSpectatorPawn::UpdatePlacement(APlayerController* Viewer)
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bMatch = Match && Match->bVersusMatch;
	if (bPlaced && bMatch == bPlacedForMatch)
	{
		return;
	}
	if (!bPlaced)
	{
		RoomHome = GetActorLocation();
	}
	Followed = nullptr;
	FlyVelocity = FVector::ZeroVector;
	if (Viewer->GetViewTarget() != this)
	{
		Viewer->SetViewTarget(this);
	}
	if (bMatch)
	{
		PlaceOverStage(Match->StageCenter);
	}
	else
	{
		// Over the room, looking down at where the members stand.
		SetActorLocation(RoomHome);
		Viewer->SetControlRotation(FRotator(-35.0f, 90.0f, 0.0f));
		bPlaced = true;
		bPlacedForMatch = false;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Spectator camera placed over the %s at %s looking %s (stage center %s)"),
		bMatch ? TEXT("stage") : TEXT("room"), *GetActorLocation().ToCompactString(),
		*Viewer->GetControlRotation().ToCompactString(), Match ? *FVector(Match->StageCenter).ToCompactString() : TEXT("-"));
}

FVector AChaosImpactSpectatorPawn::ClampToStage(const FVector& Location) const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->bVersusMatch)
	{
		return Location;
	}
	const FVector Center = Match->StageCenter;
	const float Reach = AChaosImpactVersusStage::HalfExtent + BoundsMargin;
	return FVector(FMath::Clamp(Location.X, Center.X - Reach, Center.X + Reach),
		FMath::Clamp(Location.Y, Center.Y - Reach, Center.Y + Reach),
		FMath::Clamp(Location.Z, Center.Z + MinHeight, Center.Z + MaxHeight));
}

TArray<AChaosImpactCharacter*> AChaosImpactSpectatorPawn::GetFollowTargets() const
{
	TArray<AChaosImpactCharacter*> Targets;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match)
	{
		return Targets;
	}
	// In the standings' order, so the same button always goes to the same player. Found from the characters
	// themselves: on a spectator's machine a player state does not always know its pawn.
	const TArray<AChaosImpactPlayerState*> Competitors = Match->GetCompetitors(true);
	TArray<TPair<int32, AChaosImpactCharacter*>> Ordered;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		const int32 Order = Competitors.IndexOfByKey(It->GetPlayerState<AChaosImpactPlayerState>());
		if (Order != INDEX_NONE)
		{
			Ordered.Emplace(Order, *It);
		}
	}
	Ordered.Sort([](const TPair<int32, AChaosImpactCharacter*>& A, const TPair<int32, AChaosImpactCharacter*>& B)
	{
		return A.Key < B.Key;
	});
	for (const TPair<int32, AChaosImpactCharacter*>& Entry : Ordered)
	{
		Targets.Add(Entry.Value);
	}
	return Targets;
}

void AChaosImpactSpectatorPawn::CycleFollow(const int32 Direction)
{
	APlayerController* Viewer = Cast<APlayerController>(GetController());
	const TArray<AChaosImpactCharacter*> Targets = GetFollowTargets();
	if (!Viewer || Targets.IsEmpty())
	{
		return;
	}
	const int32 Current = Targets.IndexOfByKey(Followed.Get());
	const int32 Next = Current == INDEX_NONE
		? (Direction >= 0 ? 0 : Targets.Num() - 1)
		: ((Current + Direction) % Targets.Num() + Targets.Num()) % Targets.Num();
	Followed = Targets[Next];
	// No blend while time is stopped: a blend would never finish.
	Viewer->SetViewTargetWithBlend(Targets[Next], IsTimeStopped() ? 0.0f : 0.35f, VTBlend_EaseInOut, 2.0f);
}

void AChaosImpactSpectatorPawn::StopFollowing()
{
	APlayerController* Viewer = Cast<APlayerController>(GetController());
	if (!Viewer || !Followed.IsValid())
	{
		Followed = nullptr;
		return;
	}
	Followed = nullptr;
	// Carry on from where that camera was.
	if (Viewer->PlayerCameraManager)
	{
		SetActorLocation(ClampToStage(Viewer->PlayerCameraManager->GetCameraLocation()));
		Viewer->SetControlRotation(Viewer->PlayerCameraManager->GetCameraRotation());
	}
	FlyVelocity = FVector::ZeroVector;
	Viewer->SetViewTargetWithBlend(this, IsTimeStopped() ? 0.0f : 0.25f);
}

void AChaosImpactSpectatorPawn::SetHudHidden(const bool bHide)
{
	bHudHidden = bHide;
	if (Hud)
	{
		Hud->SetVisibility(bHide ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
	if (AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetController()))
	{
		Viewer->SetGameplayUIHidden(bHide);
	}
}

void AChaosImpactSpectatorPawn::SetTimeStopped(const bool bStop)
{
	if (AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetController()))
	{
		Viewer->SetSpectateTimeStopped(bStop);
	}
}

bool AChaosImpactSpectatorPawn::IsTimeStopped() const
{
	const AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetController());
	return Viewer && Viewer->IsSpectateTimeStopped();
}

void AChaosImpactSpectatorPawn::EnsureHud(APlayerController* Viewer)
{
	if (Hud || !Viewer->IsLocalController())
	{
		return;
	}
	Hud = CreateWidget<UChaosImpactChargeWidget>(Viewer, UChaosImpactChargeWidget::StaticClass());
	if (Hud)
	{
		Hud->SetSpectatorView(true);
		Hud->AddToPlayerScreen(20);
		SetHudHidden(bHudHidden);
	}
}

void AChaosImpactSpectatorPawn::UpdateFreeFlight(APlayerController* Viewer, const float DeltaSeconds)
{
	// Look: right stick, or the mouse while its right button is held.
	FRotator View = Viewer->GetControlRotation();
	const float StickYaw = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	const float StickPitch = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	View.Yaw += (FMath::Abs(StickYaw) > 0.15f ? StickYaw : 0.0f) * SpectatorStickTurnRate * DeltaSeconds;
	View.Pitch += (FMath::Abs(StickPitch) > 0.15f ? StickPitch : 0.0f) * SpectatorStickTurnRate * DeltaSeconds;
	if (Viewer->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		Viewer->GetInputMouseDelta(MouseX, MouseY);
		View.Yaw += MouseX * SpectatorMouseTurnRate * 10.0f;
		View.Pitch += MouseY * SpectatorMouseTurnRate * 10.0f;
	}
	View.Pitch = FMath::ClampAngle(View.Pitch, -89.0f, 60.0f);
	View.Roll = 0.0f;
	Viewer->SetControlRotation(View);

	// Move: flat along the view for forward and sideways, straight up and down for rise and fall.
	const FVector Forward = FRotator(0.0f, View.Yaw, 0.0f).Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	float MoveForward = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	float MoveRight = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	MoveForward = FMath::Abs(MoveForward) > 0.15f ? MoveForward : 0.0f;
	MoveRight = FMath::Abs(MoveRight) > 0.15f ? MoveRight : 0.0f;
	MoveForward += (Viewer->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (Viewer->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
	MoveRight += (Viewer->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (Viewer->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
	const float Rise = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis)
		- Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis)
		+ (Viewer->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) - (Viewer->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
	const bool bFast = Viewer->IsInputKeyDown(EKeys::LeftShift) || Viewer->IsInputKeyDown(EKeys::Gamepad_LeftThumbstick);
	FVector Wish = (Forward * MoveForward + Right * MoveRight).GetClampedToMaxSize(1.0f) + FVector::UpVector * FMath::Clamp(Rise, -1.0f, 1.0f);
	Wish *= bFast ? SpectatorFastSpeed : SpectatorFlySpeed;
	// Eases in and out so the view glides rather than jerks.
	FlyVelocity = FMath::VInterpTo(FlyVelocity, Wish, DeltaSeconds, 8.0f);
	SetActorLocation(ClampToStage(GetActorLocation() + FlyVelocity * DeltaSeconds));
}

void AChaosImpactSpectatorPawn::ServerSyncView_Implementation(const FVector_NetQuantize Location)
{
	SetActorLocation(Location);
}

void AChaosImpactSpectatorPawn::UpdateViewSync(APlayerController* Viewer, const float DeltaSeconds)
{
	if (HasAuthority() || !Viewer->PlayerCameraManager)
	{
		return;
	}
	ViewSyncElapsed += DeltaSeconds;
	if (ViewSyncElapsed < 0.2f)
	{
		return;
	}
	ViewSyncElapsed = 0.0f;
	// The camera being shown: the free camera, or the followed player's. The engine sends its camera to the host
	// only while a pawn walks, and this camera never walks.
	const FVector CameraLocation = Viewer->PlayerCameraManager->GetCameraLocation();
	const FRotator CameraRotation = Viewer->PlayerCameraManager->GetCameraRotation();
	Viewer->ServerUpdateCamera(CameraLocation, (static_cast<int32>(FRotator::CompressAxisToShort(CameraRotation.Yaw)) << 16)
		| static_cast<int32>(FRotator::CompressAxisToShort(CameraRotation.Pitch)));
	ServerSyncView(CameraLocation);
}

void AChaosImpactSpectatorPawn::UpdateDevCycle(const float DeltaSeconds)
{
	if (DevCycleSeconds <= 0.0f)
	{
		return;
	}
	const float Before = DevCycleElapsed;
	DevCycleElapsed += DeltaSeconds;
	// A screenshot halfway between switches, once the view has settled on its player.
	const float Half = DevCycleSeconds * 0.5f;
	if (Before < Half && DevCycleElapsed >= Half)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SpectateOnline"),
			FString::Printf(TEXT("spectator_%02d.png"), DevCycleShots++)), true, false);
	}
	if (DevCycleElapsed < DevCycleSeconds)
	{
		return;
	}
	DevCycleElapsed = 0.0f;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	CycleFollow(1);
	UE_LOG(LogChaosImpact, Log, TEXT("Spectator view: %s at %s looking %s (match=%d phase=%d competitors=%d)"),
		Followed.IsValid() ? *Followed->GetOverheadDisplayName() : TEXT("free camera"),
		*GetActorLocation().ToCompactString(), GetController() ? *GetController()->GetControlRotation().ToCompactString() : TEXT("-"),
		Match && Match->bVersusMatch, Match ? static_cast<int32>(Match->Phase) : -1,
		Match ? Match->GetCompetitors(true).Num() : 0);
}

void AChaosImpactSpectatorPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetController());
	if (!Viewer || !Viewer->IsLocalController())
	{
		return;
	}
	EnsureHud(Viewer);
	UpdatePlacement(Viewer);
	UpdateViewSync(Viewer, DeltaSeconds);
	UpdateDevCycle(DeltaSeconds);
	// Menus (pause, rules, results) keep the keys for themselves.
	if (Viewer->GetCurrentScreen() != EChaosImpactScreen::Playing)
	{
		return;
	}
	if (Viewer->WasInputKeyJustPressed(EKeys::Right) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder))
	{
		CycleFollow(1);
	}
	else if (Viewer->WasInputKeyJustPressed(EKeys::Left) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder))
	{
		CycleFollow(-1);
	}
	if (Viewer->WasInputKeyJustPressed(EKeys::F) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left))
	{
		StopFollowing();
	}
	if (Viewer->WasInputKeyJustPressed(EKeys::H) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top))
	{
		SetHudHidden(!bHudHidden);
	}
	if (!Viewer->IsOnlineRoom()
		&& (Viewer->WasInputKeyJustPressed(EKeys::T) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)))
	{
		SetTimeStopped(!IsTimeStopped());
	}
	if (AChaosImpactCharacter* Target = Followed.Get())
	{
		if (!IsValid(Target))
		{
			StopFollowing();
			return;
		}
		// Kept with the followed camera, so going back to free flight starts from there.
		if (Viewer->PlayerCameraManager)
		{
			SetActorLocation(Viewer->PlayerCameraManager->GetCameraLocation());
		}
		return;
	}
	if (Viewer->GetViewTarget() != this)
	{
		Viewer->SetViewTarget(this);
	}
	UpdateFreeFlight(Viewer, DeltaSeconds);
}
