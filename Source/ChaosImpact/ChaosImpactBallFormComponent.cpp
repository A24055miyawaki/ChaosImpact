#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UChaosImpactBallFormComponent::UChaosImpactBallFormComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AChaosImpactCharacter* UChaosImpactBallFormComponent::Character() const { return Cast<AChaosImpactCharacter>(GetOwner()); }
AChaosImpactBuddyBall* UChaosImpactBallFormComponent::GetHeldBall(const int32 Slot) const
{
	return Slot >= 0 && Slot < 2 ? Slots[Slot].Get() : nullptr;
}

bool UChaosImpactBallFormComponent::IsAvailable() const
{
	const UWorld* World = GetWorld();
	const auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	return bEnabled && World && World->GetNetMode() == NM_Standalone && !(Match && Match->bVersusMatch)
		&& PC && PC->IsTrainingMode() && PC->ShowsTrainingPauseEntries();
}

bool UChaosImpactBallFormComponent::CanCollect(const AChaosImpactCharacter* Passenger) const
{
	const AChaosImpactCharacter* P = Character();
	if (!IsAvailable() || !P || !Passenger || P == Passenger || P->IsEliminated() || IsInBallForm()
		|| P->IsTrainingMenuFrozen() || P->IsIceFrozen() || P->IsChargingThrow() || P->IsThrowReleasePending()
		|| P->GetCarriedBallCount() >= P->GetMaximumCarriedBalls()) { return false; }
	return AChaosImpactGameState::AreTeammates(GetWorld(), P, Passenger);
}

bool UChaosImpactBallFormComponent::Toggle()
{
	AChaosImpactCharacter* P = Character();
	if (!P || !IsAvailable() || P->IsEliminated() || P->IsTrainingMenuFrozen()) { return false; }
	if (IsInBallForm()) { Restore(); return true; }
	if (GetWorld()->GetTimeSeconds() < NextTransformAt || P->IsIceFrozen() || P->IsDashing()
		|| P->IsThrowReleasePending() || P->IsDriving() || P->WindCarrier != nullptr
		|| !P->GetCharacterMovement()->IsMovingOnGround() || !P->ToonCharacter) { return false; }
	P->RequestCancelThrow();
	P->bClickMoveActive = false;
	P->ClickMovePath.Reset();
	P->bPointerMoveHeld = false;
	P->DropCarriedBalls();
	P->ClearCarriedBalls();
	P->UpdateBallPresentation();
	SafeLocation = P->GetActorLocation();
	const FVector GroundBall = SafeLocation - FVector::UpVector * (P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 36.0f);
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactBuddyBall* Ball = GetWorld()->SpawnActor<AChaosImpactBuddyBall>(GroundBall, FRotator::ZeroRotator, Spawn);
	if (!Ball) { return false; }
	FormBall = Ball;
	SavedCollision = static_cast<uint8>(P->GetCapsuleComponent()->GetCollisionEnabled());
	SavedMovement = static_cast<uint8>(P->GetCharacterMovement()->MovementMode);
	SavedCustomMovement = P->GetCharacterMovement()->CustomMovementMode;
	SavedPuppetTransform = P->ToonCharacter->GetRelativeTransform();
	bSavedPuppet = true;
	P->GetCharacterMovement()->StopMovementImmediately();
	P->GetCharacterMovement()->DisableMovement();
	P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->ToonCharacter->SetBallPose(true);
	Ball->InitializePassenger(P);
	return true;
}

void UChaosImpactBallFormComponent::Restore(const bool bRelocate)
{
	if (bRestoring || (!FormBall.IsValid() && !bSavedPuppet)) { return; }
	bRestoring = true;
	AChaosImpactCharacter* P = Character();
	AChaosImpactBuddyBall* Ball = FormBall.Get();
	FVector At = Ball ? Ball->GetActorLocation() : (P ? P->GetActorLocation() : SafeLocation);
	FormBall.Reset();
	if (Ball)
	{
		Ball->ForgetPassenger();
		if (AChaosImpactCharacter* Holder = Ball->GetCarrier()) { Holder->GetBallForm()->Remove(Ball); }
		// Also remove a held ball released during the throw animation, before its release cue.
		if (auto* Holder = Cast<AChaosImpactCharacter>(Ball->GetOwner()); Holder && Holder->PendingThrowBall.Get() == Ball)
		{
			Holder->GetWorldTimerManager().ClearTimer(Holder->ThrowReleaseTimer);
			Holder->PendingThrowBall.Reset();
			Holder->bThrowReleasePending = false;
			Holder->UpdateBallPresentation();
		}
		Ball->Destroy();
	}
	if (P)
	{
		if (P->ToonCharacter && bSavedPuppet)
		{
			P->ToonCharacter->SetBallPose(false);
			P->ToonCharacter->SetRelativeTransform(SavedPuppetTransform);
		}
		if (bRelocate && !P->IsEliminated())
		{
			FCollisionQueryParams Query(SCENE_QUERY_STAT(BuddyUncurl), false, P);
			if (Ball) { Query.AddIgnoredActor(Ball); }
			const float Half = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector Free = SafeLocation;
			for (int32 I = 0; I < 9; ++I)
			{
				const FVector Offset = I == 0 ? FVector::ZeroVector : FRotator(0, (I-1)*45.0f, 0).Vector()*130.0f;
				FHitResult Floor;
				if (GetWorld()->LineTraceSingleByChannel(Floor, At+Offset+FVector::UpVector*150,
					At+Offset-FVector::UpVector*700, ECC_WorldStatic, Query) && Floor.ImpactNormal.Z > .65f)
				{
					const FVector Candidate = Floor.ImpactPoint + FVector::UpVector*(Half+3);
					if (!GetWorld()->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(P->GetCapsuleComponent()->GetScaledCapsuleRadius(), Half), Query))
					{ Free = Candidate; break; }
				}
			}
			P->SetActorLocation(Free, false, nullptr, ETeleportType::TeleportPhysics);
		}
		P->GetCapsuleComponent()->SetCollisionEnabled(static_cast<ECollisionEnabled::Type>(SavedCollision));
		P->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(SavedMovement), SavedCustomMovement);
		if (P->IsTrainingMenuFrozen())
		{
			P->SavedTrainingMenuMovementMode = SavedMovement;
			P->SavedTrainingMenuCustomMovementMode = SavedCustomMovement;
			P->GetCharacterMovement()->DisableMovement();
		}
		P->GetCharacterMovement()->StopMovementImmediately();
		// A steering click held through landing must not start a new throw after uncurling.
		P->bWaitForThrowRelease = true;
		P->bMouseChargeActive = false;
		P->bWasMouseDownLastTick = false;
		P->UpdateBallPresentation();
	}
	bSavedPuppet = false;
	NextTransformAt = GetWorld() ? GetWorld()->GetTimeSeconds()+.5 : 0;
	bRestoring = false;
}

void UChaosImpactBallFormComponent::Push(AChaosImpactBuddyBall* Ball, const int32 Slot)
{
	if (Slot >= 0 && Slot < 2) { Slots[Slot] = Ball; }
}
void UChaosImpactBallFormComponent::Pop() { Slots[0] = Slots[1]; Slots[1].Reset(); }
void UChaosImpactBallFormComponent::SwapSlots() { ::Swap(Slots[0], Slots[1]); }
void UChaosImpactBallFormComponent::Clear()
{
	const TWeakObjectPtr<AChaosImpactBuddyBall> Copy[] = {Slots[0], Slots[1]};
	Slots[0].Reset(); Slots[1].Reset();
	for (const auto& Ball : Copy) { if (Ball.IsValid()) { Ball->ReleasePassenger(); } }
	Restore(false);
}
void UChaosImpactBallFormComponent::Remove(AChaosImpactBuddyBall* Ball)
{
	AChaosImpactCharacter* P = Character();
	if (!P) { return; }
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (Slots[Slot].Get() != Ball) { continue; }
		P->RequestCancelThrow();
		if (Slot == 1) { P->SwapCarriedBalls(); }
		P->PopCarriedBall();
		P->UpdateBallPresentation();
		break;
	}
}

void UChaosImpactBallFormComponent::ResetGesture() { bHeld = false; bTriggered = false; bWaitForManageRelease = true; }
bool UChaosImpactBallFormComponent::HandleInput()
{
	AChaosImpactCharacter* P = Character();
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!IsAvailable() || !PC) { return IsInBallForm(); }
	using namespace ChaosImpactSettings;
	const bool bSimple = GetPlayerControlScheme(PC) == EChaosImpactControlScheme::Simple;
	const bool bMouseOnly = IsMouseOnlyScheme(GetPlayerControlScheme(PC));
	const EChaosImpactAction Action = bSimple ? EChaosImpactAction::SwapBall : bMouseOnly ? EChaosImpactAction::Dash : EChaosImpactAction::DropBall;
	const bool bDown = IsActionDown(PC, Action);
	if (bWaitForManageRelease)
	{
		if (bDown) { return IsInBallForm(); }
		bWaitForManageRelease = false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if ((bDown || WasActionJustPressed(PC,Action)) && !bHeld)
	{
		bHeld = true; bTriggered = false; PressedAt = Now;
		if (IsInBallForm()) { Toggle(); bTriggered = true; }
	}
	if (bHeld && bDown && !bTriggered && Now-PressedAt >= HoldSeconds)
	{ Toggle(); bTriggered = true; }
	if (!bDown && bHeld)
	{
		// Normal layout: defer the drop until release so a long press can transform without an accidental drop.
		if (!bSimple && !bMouseOnly && !bTriggered) { P->RequestDropBall(); }
		bHeld = false;
	}
	if (IsInBallForm())
	{
		bool bStick = false;
		FVector2D Input = GetMoveInput(PC, bStick).GetClampedToMaxSize(1);
		if (bStick && Input.Size() < .2) { Input = FVector2D::ZeroVector; }
		const FRotator Yaw(0, P->CameraBoom->GetComponentRotation().Yaw, 0);
		FVector Direction = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X)*Input.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*Input.X;
		if (IsMouseOnlyScheme(GetPlayerControlScheme(PC)))
		{
			FVector Point;
			if (IsActionDown(PC, EChaosImpactAction::Throw) && P->FindMouseAimPoint(Point)) { Direction = (Point-P->GetActorLocation()).GetSafeNormal2D(); }
		}
		FormBall->SetSteering(Direction);
		return true;
	}
	return false;
}

void UChaosImpactBallFormComponent::TickComponent(const float Delta, const ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Delta, Type, Function);
	AChaosImpactCharacter* P = Character();
	if (!P) { return; }
	if (IsInBallForm())
	{
		if (!IsAvailable() || P->IsEliminated()) { Restore(false); return; }
		const FVector Center = FormBall->GetActorLocation();
		P->GetCharacterMovement()->DisableMovement();
		P->SetActorLocation(Center, false, nullptr, ETeleportType::TeleportPhysics);
		if (P->ToonCharacter)
		{
			const float Scale = P->ToonCharacterScale*.55f;
			const float Spin = FormBall->IsLaunched() ? GetWorld()->GetTimeSeconds()*600.0f : 0.0f;
			const FQuat Rotation = FQuat(P->GetActorRightVector(), FMath::DegreesToRadians(Spin)) * P->GetMesh()->GetComponentQuat();
			P->ToonCharacter->SetWorldScale3D(FVector(Scale));
			P->ToonCharacter->SetWorldLocationAndRotation(Center-Rotation.RotateVector(FVector(0,0,85)*Scale), Rotation);
			P->ToonCharacter->UpdatePose(true);
		}
	}
	for (int32 I = 0; I < 2; ++I) { if (Slots[I].IsValid()) { Slots[I]->FollowHand(); } }
}

void UChaosImpactBallFormComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Clear();
	Super::EndPlay(Reason);
}
