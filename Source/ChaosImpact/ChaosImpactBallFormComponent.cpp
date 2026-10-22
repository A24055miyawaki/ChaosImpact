#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactTackleComponent.h"
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
#include "Net/UnrealNetwork.h"

UChaosImpactBallFormComponent::UChaosImpactBallFormComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

void UChaosImpactBallFormComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UChaosImpactBallFormComponent, FormBall);
	DOREPLIFETIME(UChaosImpactBallFormComponent, Slots);
	DOREPLIFETIME(UChaosImpactBallFormComponent, CaptureImmuneUntil);
	DOREPLIFETIME(UChaosImpactBallFormComponent, ReleaseProtectedUntil);
}

void UChaosImpactBallFormComponent::ServerToggle_Implementation() { Toggle(); }
void UChaosImpactBallFormComponent::ServerSteer_Implementation(FVector_NetQuantizeNormal Direction)
{
	if (IsInBallForm() && (IsAvailable() || IsCaptured()) && !Direction.ContainsNaN()) { FormBall->SetSteering(Direction); }
}
void UChaosImpactBallFormComponent::Steer(const FVector& Direction)
{
	auto* P = Character();
	if (!P || !IsInBallForm() || Direction.ContainsNaN()) { return; }
	if (P->HasAuthority()) { FormBall->SetSteering(Direction); }
	else if (P->IsLocallyControlled() && GetWorld()->GetTimeSeconds() >= NextSteerAt)
	{
		ServerSteer(Direction.GetSafeNormal2D());
		NextSteerAt = GetWorld()->GetTimeSeconds() + .05;
	}
}
void UChaosImpactBallFormComponent::OnRep_Slots()
{
	if (auto* P = Character()) { P->UpdateBallPresentation(); }
}
void UChaosImpactBallFormComponent::OnRep_FormBall()
{
	if (IsInBallForm()) { ApplyFormPresentation(); }
	else { Restore(false); }
}
void UChaosImpactBallFormComponent::ApplyFormPresentation()
{
	auto* P = Character();
	if (!P || bSavedPuppet) { return; }
	// The owning client keeps its own charge/click-move state (it is not replicated back by the pawn).
	P->CancelChargingThrow();
	if (FormBall.IsValid() && FormBall->IsPrisonBall())
	{
		P->EndDriving(true);
		if (!P->HasAuthority())
		{
			GetWorld()->GetTimerManager().ClearTimer(P->ThrowReleaseTimer);
			if (auto* Preview=P->PendingThrowBall.Get(); Preview && Preview->GetLocalRole()==ROLE_Authority) { Preview->Destroy(); }
			P->PendingThrowBall.Reset(); P->PredictedThrowBall.Reset(); P->bThrowReleasePending=false;
		}
	}
	P->bClickMoveActive = false;
	P->ClickMovePath.Reset();
	P->bPointerMoveHeld = false;
	SavedCollision = static_cast<uint8>(P->GetCapsuleComponent()->GetCollisionEnabled());
	SavedMovement = static_cast<uint8>(P->GetCharacterMovement()->MovementMode);
	// MovementMode may have arrived before the component reference on an online client.
	if (SavedMovement == MOVE_None) { SavedMovement = MOVE_Walking; }
	SavedCustomMovement = P->GetCharacterMovement()->CustomMovementMode;
	if (P->ToonCharacter)
	{
		SavedPuppetTransform = P->ToonCharacter->GetRelativeTransform();
		P->ToonCharacter->SetBallPose(true);
	}
	bSavedPuppet = true;
	P->GetCharacterMovement()->StopMovementImmediately();
	P->GetCharacterMovement()->DisableMovement();
	P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CurlAge = 0.0f; UncurlAge = -1.0f; bCurlCenterKnown = false;
	CurlCenter = FVector(0, 0, 85);
	if (FormBall.IsValid()) { AChaosImpactBuddyBall::PlayPop(this, FormBall->GetShellCenter(), FormBall->GetTeamColor(), false); }
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
	if (!bEnabled || !World)
	{
		return false;
	}
	// The replicated match/team rules are also available on remote clients.
	if (Match && Match->bVersusMatch)
	{
		return Match->IsTeamBattle() && Match->Phase == EChaosImpactOnlinePhase::Match;
	}
	return PC && PC->IsTrainingMode() && PC->ShowsTrainingPauseEntries();
}

bool UChaosImpactBallFormComponent::CanCollect(const AChaosImpactCharacter* Passenger) const
{
	const AChaosImpactCharacter* P = Character();
	if (!IsAvailable() || !P || !Passenger || P == Passenger || P->IsEliminated() || IsInBallForm()
		|| P->IsTrainingMenuFrozen() || P->IsIceFrozen() || P->IsChargingThrow() || P->IsThrowReleasePending()
		|| P->GetCarriedBallCount() >= P->GetMaximumCarriedBalls()) { return false; }
	return AChaosImpactGameState::AreTeammates(GetWorld(), P, Passenger);
}

bool UChaosImpactBallFormComponent::IsCaptured() const { return FormBall.IsValid() && FormBall->IsPrisonBall(); }
bool UChaosImpactBallFormComponent::IsCaptureImmune() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State && State->GetServerWorldTimeSeconds() < CaptureImmuneUntil;
}
bool UChaosImpactBallFormComponent::IsReleaseProtected() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State && State->GetServerWorldTimeSeconds() < ReleaseProtectedUntil;
}

bool UChaosImpactBallFormComponent::Capture(AChaosImpactCharacter* Captor)
{
	auto* P = Character();
	if (!P || !Captor || !P->HasAuthority() || Captor == P || Captor->IsEliminated()
		|| !ChaosImpactBallTypes::IsPrisonMode(GetWorld()) || P->IsEliminated() || P->IsDashing()
		|| IsInBallForm() || IsCaptureImmune() || IsReleaseProtected()
		|| AChaosImpactGameState::AreTeammates(GetWorld(), P, Captor)) { return false; }
	// Spawn first: allocation failure must not destroy the victim's inventory or interrupt a throw.
	const FVector At = P->GetActorLocation() - FVector::UpVector * (P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-36);
	FActorSpawnParameters Spawn;
	Spawn.Owner = P;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Ball = GetWorld()->SpawnActor<AChaosImpactBuddyBall>(At, FRotator::ZeroRotator, Spawn);
	if (!Ball) { return false; }
	P->RequestCancelThrow();
	P->GetTackle()->ResetAction();
	if (AChaosImpactBall* Pending=P->PendingThrowBall.Get())
	{
		GetWorld()->GetTimerManager().ClearTimer(P->ThrowReleaseTimer);
		P->PendingThrowBall.Reset(); P->bThrowReleasePending=false;
		if (auto* Buddy=Cast<AChaosImpactBuddyBall>(Pending))
		{
			if (Buddy->IsPrisonBall()) { Buddy->DropFromCarrier(); }
			else { Buddy->ReleasePassenger(); }
		}
		else { Pending->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); Pending->MakeRollingPickup(FVector::ZeroVector); }
	}
	P->DropCarriedBalls();
	P->EndDriving(true);
	P->ClearCarriedBalls();
	SafeLocation = P->GetActorLocation();
	P->UpdateBallPresentation();
	FormBall = Ball;
	// An airborne capture over the void still needs a known respawn fallback.
	FHitResult Floor;
	if (!GetWorld()->LineTraceSingleByChannel(Floor,SafeLocation+FVector::UpVector*80,SafeLocation-FVector::UpVector*900,
		ECC_WorldStatic,FCollisionQueryParams(SCENE_QUERY_STAT(PrisonSafeStart),false,P))) { SafeLocation=P->InitialSpawnLocation; }
	Ball->InitializeCaptive(P, Captor);
	ApplyFormPresentation();
	P->NotifyServerTeleport();
	P->ClientBallCountReset(P->CarriedBallCount, P->CarriedBallTypes);
	P->ForceNetUpdate();
	return true;
}

bool UChaosImpactBallFormComponent::Toggle()
{
	AChaosImpactCharacter* P = Character();
	if (!P || IsCaptured() || !IsAvailable() || P->IsEliminated() || P->IsTrainingMenuFrozen() || P->GetTackle()->IsBusy()) { return false; }
	if (!P->HasAuthority())
	{
		if (!P->IsLocallyControlled()) { return false; }
		ServerToggle();
		return true;
	}
	if (IsInBallForm()) { Restore(); return true; }
	if (GetWorld()->GetTimeSeconds() < NextTransformAt || P->IsIceFrozen() || P->IsDashing()
		|| P->IsThrowReleasePending() || P->IsDriving() || P->WindCarrier != nullptr
		|| !P->GetCharacterMovement()->IsMovingOnGround()) { return false; }
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
	Spawn.Owner = P;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactBuddyBall* Ball = GetWorld()->SpawnActor<AChaosImpactBuddyBall>(GroundBall, FRotator::ZeroRotator, Spawn);
	if (!Ball) { return false; }
	FormBall = Ball;
	Ball->InitializePassenger(P);
	ApplyFormPresentation();
	P->NotifyServerTeleport();
	P->ClientBallCountReset(P->CarriedBallCount, P->CarriedBallTypes);
	P->ForceNetUpdate();
	return true;
}

void UChaosImpactBallFormComponent::Restore(const bool bRelocate)
{
	if (bRestoring || (!FormBall.IsValid() && !bSavedPuppet)) { return; }
	bRestoring = true;
	AChaosImpactCharacter* P = Character();
	AChaosImpactBuddyBall* Ball = FormBall.Get();
	if (P && P->HasAuthority() && Ball && Ball->IsPrisonBall())
	{
		const double Now = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
		CaptureImmuneUntil = Now + ChaosImpactBallTypes::PrisonImmunitySeconds;
		ReleaseProtectedUntil = Now + ChaosImpactBallTypes::PrisonReleaseProtection;
	}
	FVector At = Ball ? Ball->GetActorLocation() : (P ? P->GetActorLocation() : SafeLocation);
	if (P && !P->HasAuthority() && bSavedPuppet)
	{
		AChaosImpactBuddyBall::PlayPop(this, At, Ball ? Ball->GetTeamColor() : SavedFormColor, true);
	}
	FormBall.Reset();
	if (Ball && P && P->HasAuthority())
	{
		AChaosImpactBuddyBall::PlayPop(this, Ball->GetShellCenter(), Ball->GetTeamColor(), true);
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
		if (P->HasAuthority() && bRelocate && !P->IsEliminated())
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
			P->NotifyServerTeleport();
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
		if (P->IsEliminated())
		{
			// HP/KO and component references may replicate in either order.
			P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			P->GetCharacterMovement()->DisableMovement();
		}
		// A steering click held through landing must not start a new throw after uncurling.
		P->bWaitForThrowRelease = true;
		P->bMouseChargeActive = false;
		P->bWasMouseDownLastTick = false;
		P->UpdateBallPresentation();
		P->ForceNetUpdate();
	}
	if (bSavedPuppet && P && !P->IsEliminated())
	{
		// Springs back out of the burst shell.
		UncurlAge = 0.0f;
	}
	CurlAge = -1.0f;
	bSavedPuppet = false;
	NextTransformAt = GetWorld() ? GetWorld()->GetTimeSeconds()+.5 : 0;
	bRestoring = false;
}

void UChaosImpactBallFormComponent::Push(AChaosImpactBuddyBall* Ball, const int32 Slot)
{
	if (GetOwner()->HasAuthority() && Slot >= 0 && Slot < 2) { Slots[Slot] = Ball; GetOwner()->ForceNetUpdate(); }
}
void UChaosImpactBallFormComponent::Pop() { if (GetOwner()->HasAuthority()) { Slots[0] = Slots[1]; Slots[1].Reset(); GetOwner()->ForceNetUpdate(); } }
void UChaosImpactBallFormComponent::SwapSlots() { if (GetOwner()->HasAuthority()) { ::Swap(Slots[0], Slots[1]); GetOwner()->ForceNetUpdate(); } }
void UChaosImpactBallFormComponent::Clear()
{
	if (!GetOwner()->HasAuthority()) { return; }
	const TWeakObjectPtr<AChaosImpactBuddyBall> Copy[] = {Slots[0], Slots[1]};
	Slots[0].Reset(); Slots[1].Reset();
	for (const auto& Ball : Copy)
	{
		if (Ball.IsValid())
		{
			if (Ball->IsPrisonBall()) { Ball->DropFromCarrier(); }
			else { Ball->ReleasePassenger(); }
		}
	}
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
		P->ClientBallCountReset(P->CarriedBallCount, P->CarriedBallTypes);
		break;
	}
}

void UChaosImpactBallFormComponent::ResetGesture() { bHeld = false; bTriggered = false; bWaitForManageRelease = true; }
bool UChaosImpactBallFormComponent::HandleInput()
{
	AChaosImpactCharacter* P = Character();
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if ((!IsAvailable() && !IsCaptured()) || !PC) { return IsInBallForm(); }
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
		Steer(Direction);
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
		if (P->HasAuthority() && ((IsCaptured() ? !ChaosImpactBallTypes::IsPrisonMode(GetWorld()) : !IsAvailable()) || P->IsEliminated())) { Restore(false); return; }
		ApplyFormPresentation();
		SavedFormColor=FormBall->GetTeamColor();
		const FVector Center = FormBall->GetActorLocation();
		P->GetCharacterMovement()->DisableMovement();
		P->SetActorLocation(Center, false, nullptr, ETeleportType::TeleportPhysics);
		if (P->ToonCharacter)
		{
			P->ToonCharacter->SetBallPose(true);
			// Curled up inside the shell, rolling with it; it shrinks into the shell over the first moment.
			CurlAge += Delta;
			const float In = FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(CurlAge / 0.22f, 0.0f, 1.0f), 2.0f);
			const float Scale = P->ToonCharacterScale * FMath::Lerp(1.0f, 0.46f, In);
			const FQuat Rotation = FormBall->GetRoll() * P->GetMesh()->GetComponentQuat();
			const FVector Middle = FormBall->GetShellCenter();
			P->ToonCharacter->SetWorldScale3D(FVector(Scale));
			P->ToonCharacter->SetWorldLocationAndRotation(Middle-Rotation.RotateVector(CurlCenter*Scale), Rotation);
			P->ToonCharacter->UpdatePose(true);
			if (!bCurlCenterKnown && CurlAge > 0.06f)
			{
				// Where the curled body actually is (each model curls differently), so it stays inside the shell.
				FBox Box(ForceInit);
				TArray<USceneComponent*> Parts;
				P->ToonCharacter->GetChildrenComponents(true, Parts);
				for (USceneComponent* Part : Parts)
				{
					if (const UPrimitiveComponent* Shape = Cast<UPrimitiveComponent>(Part); Shape && Shape->IsVisible())
					{
						Box += Shape->Bounds.GetBox();
					}
				}
				if (Box.IsValid)
				{
					CurlCenter = P->ToonCharacter->GetComponentTransform().InverseTransformPosition(Box.GetCenter());
					bCurlCenterKnown = true;
				}
			}
		}
	}
	else if (bSavedPuppet) { Restore(false); }
	else if (UncurlAge >= 0.0f && P->ToonCharacter)
	{
		// Out of the ball: a quick stretch up past full size, settling back.
		UncurlAge += Delta;
		const float T = UncurlAge / 0.34f;
		const float Stretch = T >= 1.0f ? 1.0f : T < 0.35f ? FMath::Lerp(0.6f, 1.14f, T / 0.35f)
			: 1.0f + 0.14f * FMath::Cos((T - 0.35f) / 0.65f * PI * 1.5f) * (1.0f - (T - 0.35f) / 0.65f);
		P->ToonCharacter->SetRelativeScale3D(SavedPuppetTransform.GetScale3D() * FVector(1.0f / FMath::Sqrt(Stretch), 1.0f / FMath::Sqrt(Stretch), Stretch));
		if (T >= 1.0f)
		{
			P->ToonCharacter->SetRelativeScale3D(SavedPuppetTransform.GetScale3D());
			UncurlAge = -1.0f;
		}
	}
	for (int32 I = 0; I < 2; ++I) { if (Slots[I].IsValid()) { Slots[I]->FollowHand(); } }
}

void UChaosImpactBallFormComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Clear();
	Super::EndPlay(Reason);
}
