#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGTitle.h"
#include "ChaosImpactSfx.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

namespace
{
/** The host's clock, the same on every machine (the down timer is kept in it). */
double RPGServerNow(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
}
}

// Down and revive, and falling off the field. All of it runs where the game is decided (the host online, the only
// machine offline); the down state, the timer and the revive progress are read by every screen.
//
// - With two or more players, HP reaching 0 is a "down" for RPGTuning::DownSeconds: the player lies there, cannot
//   move, throw or be hurt, and enemies leave them alone. A teammate standing close and holding the interact button
//   for RPGTuning::ReviveHoldSeconds gets them up with a third of their HP. If the helper is hit meanwhile, the
//   count starts over. Time running out, or everybody being down at once, is the usual knockout and respawn.
// - Alone it is the usual knockout straight away.
// - Falling far below the last firm ground (or out of the world) costs a little HP and puts the player back there.

int32 ARPGPrototypeCharacter::CountHeroes(const UWorld* World)
{
	int32 Count = 0;
	if (!World) { return 0; }
	for (TActorIterator<ARPGPrototypeCharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (!It->IsGrunt() && It->IsPlayerControlled() && !It->IsInTitle()) { ++Count; }
	}
	return Count;
}

FString ARPGPrototypeCharacter::GetHeroLabel() const
{
	if (!HeroName.IsEmpty()) { return HeroName; }
	// A player on another machine without a hero name: the name their machine goes by.
	if (GetLocalRPGIndex() < 0) { if (const APlayerState* State = GetPlayerState()) { if (!State->GetPlayerName().IsEmpty()) { return State->GetPlayerName(); } } }
	return FString::Printf(TEXT("%dP"), FMath::Max(0, GetLocalRPGIndex()) + 1);
}

float ARPGPrototypeCharacter::GetDownedLeft() const
{
	return bDowned && GetWorld() ? FMath::Clamp(static_cast<float>(DownedEndsAt - RPGServerNow(GetWorld())), 0.0f, RPGTuning::DownSeconds) : 0.0f;
}

ARPGPrototypeCharacter* ARPGPrototypeCharacter::FindReviveTarget() const
{
	if (bGrunt || bDowned || IsEliminated() || !GetWorld()) { return nullptr; }
	ARPGPrototypeCharacter* Best = nullptr;
	float BestDistance = FMath::Square(RPGTuning::ReviveRadius);
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->IsGrunt() || !It->IsDowned()) { continue; }
		const float Distance = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (Distance <= BestDistance && FMath::Abs(It->GetActorLocation().Z - GetActorLocation().Z) < 200) { Best = *It; BestDistance = Distance; }
	}
	return Best;
}

void ARPGPrototypeCharacter::EnterDowned()
{
	if (bDowned || bGrunt || IsEliminated()) { return; }
	bDowned = true;
	DownedEndsAt = RPGServerNow(GetWorld()) + RPGTuning::DownSeconds;
	ReviveProgress = 0; Reviver.Reset();
	KnockbackDistance = 0; Statuses.Empty(); StatusSources.Empty();
	ApplyDownedPose(true);
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->IsGrunt()) { continue; }
		It->ShowNotice(FString::Printf(TEXT("%s がダウンした！近くで長押しして起こそう"), *GetHeroLabel()), 3);
	}
}

void ARPGPrototypeCharacter::ApplyDownedPose(bool bLying)
{
	// Every machine: lying on the ground (the whole model tips back from the feet), not moving, no menus.
	if (bLying)
	{
		if (IsLocallyControlled())
		{
			CancelChargingThrow();
			if (bBagOpen) { SetBagOpen(false); }
			if (bQuestMenuOpen) { SetQuestMenuOpen(false); }
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm, .4f);
		}
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		StandingMeshTransform = GetMesh()->GetRelativeTransform();
		FTransform Lying = StandingMeshTransform;
		Lying.SetRotation(FQuat(FVector::RightVector, FMath::DegreesToRadians(-82.0f)) * StandingMeshTransform.GetRotation());
		Lying.AddToTranslation(FVector(0, 0, 18));
		GetMesh()->SetRelativeTransform(Lying);
		return;
	}
	GetMesh()->SetRelativeTransform(StandingMeshTransform);
	if (!IsEliminated()) { GetCharacterMovement()->SetMovementMode(MOVE_Walking); }
}

void ARPGPrototypeCharacter::OnRep_Downed()
{
	ApplyDownedPose(bDowned);
	if (!bDowned && !IsEliminated() && IsLocallyControlled()) { ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm, .8f); }
}

void ARPGPrototypeCharacter::FinishDowned(bool bRevived)
{
	if (!bDowned) { return; }
	bDowned = false;
	ReviveProgress = 0; Reviver.Reset();
	ApplyDownedPose(false);
	if (bRevived)
	{
		Health = FMath::Max(1.0f, MaxHealth * RPGTuning::ReviveHealthFraction);
		RevivedAt = GetWorld()->GetTimeSeconds();
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		ShowNotice(TEXT("起こしてもらった！"));
		if (IsLocallyControlled()) { ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm, .8f); }
		return;
	}
	// The usual knockout and respawn, credited to nobody.
	bBleedingOut = true;
	bIsDashing = false;
	AChaosImpactCharacter::TakeDamage(FMath::Max(Health, 1.0f) + 1, FDamageEvent(), nullptr, this);
	bBleedingOut = false;
}

void ARPGPrototypeCharacter::TickDownAndCare(float DeltaSeconds)
{
	if (bGrunt || !HasAuthority() || !GetWorld()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (bDowned)
	{
		// Up again when someone holds the button close by long enough; the same helper must keep at it.
		bool bAnyoneUp = false;
		ARPGPrototypeCharacter* Helper = nullptr;
		float Best = FMath::Square(RPGTuning::ReviveRadius);
		for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
		{
			if (*It == this || It->IsGrunt() || !It->IsPlayerControlled() || It->IsDowned() || It->IsEliminated() || It->IsInTitle()) { continue; }
			bAnyoneUp = true;
			const float Distance = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
			if (It->bReviveHeld && Distance <= Best) { Helper = *It; Best = Distance; }
		}
		if (!bAnyoneUp || RPGServerNow(GetWorld()) >= DownedEndsAt) { FinishDowned(false); return; }
		if (Helper && Helper == Reviver.Get()) { ReviveProgress += DeltaSeconds / RPGTuning::ReviveHoldSeconds; }
		else
		{
			if (ReviveProgress > 0.05f) { UE_LOG(LogTemp, Log, TEXT("RPG revive of %s restarts (helper %s -> %s)"), *GetName(), *GetNameSafe(Reviver.Get()), *GetNameSafe(Helper)); }
			Reviver = Helper; ReviveProgress = Helper ? DeltaSeconds / RPGTuning::ReviveHoldSeconds : 0.0f;
		}
		if (ReviveProgress >= 1) { FinishDowned(true); }
		return;
	}
	if (IsEliminated()) { SafeGround.Reset(); GroundedSince = -1; return; }
	// Firm ground, sampled every half second once the player has stood on it for a moment; the oldest of the last
	// few is where a fall goes back to (never the edge they just slipped off).
	const auto* Move = GetCharacterMovement();
	if (Move && Move->IsMovingOnGround() && !IsRPGKnockedBack() && !IsDriving())
	{
		if (GroundedSince < 0) { GroundedSince = Now; }
		if (Now - GroundedSince > 0.5 && Now >= NextSafeGroundAt)
		{
			// Moved far since the last sample (a teleport, a gate, fast travel): the old ones are from somewhere else.
			if (!SafeGround.IsEmpty() && FVector::DistSquared(SafeGround.Last(), GetActorLocation()) > FMath::Square(1200.0f)) { SafeGround.Reset(); }
			SafeGround.Add(GetActorLocation()); if (SafeGround.Num() > 4) { SafeGround.RemoveAt(0); }
			NextSafeGroundAt = Now + 0.5;
		}
	}
	else { GroundedSince = -1; }
	const float KillZ = GetWorld()->GetWorldSettings() ? GetWorld()->GetWorldSettings()->KillZ : -HALF_WORLD_MAX1;
	const float Z = GetActorLocation().Z;
	if (!SafeGround.IsEmpty() && (Z < SafeGround.Last().Z - RPGTuning::FallDepth || Z < KillZ + 400)) { RecoverFromFall(); return; }
	// Resting by a rest beacon (not fighting).
	const bool bResting = Now - LastDamagedAt > RPGTuning::OutOfCombatSeconds && Now - LastAttackAt > RPGTuning::OutOfCombatSeconds && Health < MaxHealth;
	if (bResting)
	{
		for (TActorIterator<ARPGAdventureCheckpoint> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) < FMath::Square(RPGTuning::BeaconRestRadius))
			{ RestoreHP(MaxHealth * RPGTuning::BeaconRestPercentPerSecond * 0.01f * DeltaSeconds); break; }
		}
	}
}

void ARPGPrototypeCharacter::RecoverFromFall()
{
	if (bGrunt || IsEliminated() || !GetWorld()) { return; }
	// The firm ground known is from somewhere else (just teleported): the respawn point instead.
	if (!SafeGround.IsEmpty() && FVector::DistSquared2D(SafeGround.Last(), GetActorLocation()) > FMath::Square(2500.0f)) { SafeGround.Reset(); }
	FVector Back = SafeGround.IsEmpty() ? FVector::ZeroVector : SafeGround[0];
	if (SafeGround.IsEmpty())
	{
		// Nothing known yet: the respawn point (the colony or the last beacon).
		Back = GetActorLocation();
		if (const auto* Adventure = ARPGAdventureWorld::Find(GetWorld())) { Back = Adventure->PlayerSpawn(FMath::Max(0, GetLocalRPGIndex())); }
		if (ActiveAdventureCheckpoint.IsValid()) { Back = ActiveAdventureCheckpoint->RespawnFor(FMath::Max(0, GetLocalRPGIndex())); }
	}
	UE_LOG(LogTemp, Log, TEXT("RPG fall recovery: %s at %s, %d firm spots, back to %s"), *GetName(), *GetActorLocation().ToString(), SafeGround.Num(), *Back.ToString());
	const float Lost = FMath::Min(MaxHealth * RPGTuning::FallDamagePercent * 0.01f, FMath::Max(0.0f, Health - 1));
	Health -= Lost;
	LastDamagedAt = GetWorld()->GetTimeSeconds();
	CancelChargingThrow();
	KnockbackDistance = 0;
	GetCharacterMovement()->StopMovementImmediately();
	SetActorLocation(Back + FVector(0, 0, 40), false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GroundedSince = -1;
	// A guest moves its own character: tell its machine (and ignore its old position for a moment).
	NotifyServerTeleport();
	if (auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); Mode && Lost > 0) { Mode->AddDamageText(this, Lost); }
	ShowNotice(TEXT("足をすべらせて落ちてしまった"));
}

void ARPGPrototypeCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
	// Enemies are gone as before; a player is put back on firm ground.
	if (bGrunt) { Super::FellOutOfWorld(DamageType); return; }
	if (HasAuthority()) { RecoverFromFall(); }
}
