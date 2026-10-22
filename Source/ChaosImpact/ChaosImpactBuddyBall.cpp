#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactPrisonBall.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactSfx.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** Out past 1 and back (a pop). */
	float BuddyEaseOutBack(const float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f) - 1.0f;
		constexpr float Over = 2.2f;
		return 1.0f + X * X * ((Over + 1.0f) * X + Over);
	}
}

AChaosImpactBuddyBall::AChaosImpactBuddyBall()
{
	// Deliberately not a new special-ball type: all ordinary-ball rules stay intact.
	ProjectileMovement->bTickBeforeOwner = false; // The base ball explicitly schedules owner -> projectile.

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Band(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Meshes/SM_CircularBand.SM_CircularBand"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShellMaterial(TEXT("/Game/ChaosImpact/BuddyBall/M_CI_BuddyShell.M_CI_BuddyShell"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(TEXT("/Game/ChaosImpact/BuddyBall/M_CI_BuddyGlow.M_CI_BuddyGlow"));

	Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shell"));
	Shell->SetupAttachment(RootComponent);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->SetCastShadow(false);
	Shell->SetVisibility(false);
	if (Sphere.Succeeded())
	{
		Shell->SetStaticMesh(Sphere.Object);
	}
	if (ShellMaterial.Succeeded())
	{
		Shell->SetMaterial(0, ShellMaterial.Object);
	}
	// The ring on the floor stays level and on the ground wherever the ball bobs.
	FloorRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorRing"));
	FloorRing->SetupAttachment(RootComponent);
	FloorRing->SetUsingAbsoluteRotation(true);
	FloorRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorRing->SetCastShadow(false);
	FloorRing->SetVisibility(false);
	if (Band.Succeeded())
	{
		FloorRing->SetStaticMesh(Band.Object);
	}
	if (GlowMaterial.Succeeded())
	{
		FloorRing->SetMaterial(0, GlowMaterial.Object);
	}
	for (int32 I = 0; I < 7; ++I)
	{
		UStaticMeshComponent* Ghost = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Ghost%d"), I));
		Ghost->SetupAttachment(RootComponent);
		Ghost->SetUsingAbsoluteLocation(true);
		Ghost->SetUsingAbsoluteScale(true);
		Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ghost->SetCastShadow(false);
		Ghost->SetVisibility(false);
		if (Sphere.Succeeded())
		{
			Ghost->SetStaticMesh(Sphere.Object);
		}
		if (GlowMaterial.Succeeded())
		{
			Ghost->SetMaterial(0, GlowMaterial.Object);
		}
		Ghosts.Add(Ghost);
	}
}

void AChaosImpactBuddyBall::InitializePassenger(AChaosImpactCharacter* Character)
{
	if (!HasAuthority()) { return; }
	Passenger = Character;
	MakePickup();
	CollisionSphere->SetSphereRadius(35.0f);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->IgnoreActorWhenMoving(Character, true);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	OnRep_Passenger();
	ForceNetUpdate();
}

void AChaosImpactBuddyBall::InitializeCaptive(AChaosImpactCharacter* Character, AChaosImpactCharacter* Captor)
{
	if (!HasAuthority() || !Character || !Captor) { return; }
	bPrison = true;
	CaptureDeadline = GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + ChaosImpactBallTypes::PrisonSeconds;
	InitializePassenger(Character);
	// Keep the captive on the floor, able to roll gently, instead of hovering like an ordinary pickup.
	MakeRollingPickup(FVector::ZeroVector);
	// Shells are server-hit world objects, unlike remote pawns whose hits are owner-reported.
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	PickupBlockedUntil = GetWorld()->GetTimeSeconds()+.25;
	CollisionSphere->IgnoreActorWhenMoving(Character,true);
	ForceNetUpdate();
}

float AChaosImpactBuddyBall::GetCaptureSecondsLeft() const
{
	const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	// A newly joined client's clock estimate can lag by a few ms; never show 5.1s or overfill the dial.
	return bPrison && State ? FMath::Clamp(static_cast<float>(CaptureDeadline-State->GetServerWorldTimeSeconds()),0.f,ChaosImpactBallTypes::PrisonSeconds) : 0.f;
}

bool AChaosImpactBuddyBall::CanBeCollectedBy(const AChaosImpactCharacter* Collector) const
{
	if (!bPrison) { return Collector && Collector->GetBallForm()->CanCollect(Passenger.Get()); }
	return Collector && Passenger.IsValid() && Collector!=Passenger.Get() && !Collector->IsEliminated()
		&& !Collector->IsInBallForm() && !Collector->IsTrainingMenuFrozen() && !Collector->IsIceFrozen()
		&& !Collector->IsChargingThrow() && !Collector->IsThrowReleasePending()
		&& Collector->GetCarriedBallCount()<Collector->GetMaximumCarriedBalls()
		&& ChaosImpactBallTypes::IsPrisonMode(GetWorld())
		&& !AChaosImpactGameState::AreTeammates(GetWorld(),Collector,Passenger.Get());
}

void AChaosImpactBuddyBall::DropFromCarrier()
{
	if (!HasAuthority() || !bPrison || bResolvingPrisonImpact) { return; }
	auto* Holder = Carrier.Get();
	const FVector Away = Holder ? Holder->GetActorForwardVector() : FVector::ForwardVector;
	const FVector At = Holder ? Holder->GetActorLocation()+Away*95-FVector::UpVector*35 : GetActorLocation();
	if (Holder) { Holder->GetBallForm()->Remove(this); }
	Carrier.Reset();
	bLaunched = false;
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocation(At);
	MakeRollingPickup(Away*340);
	PickupBlockedUntil = GetWorld()->GetTimeSeconds()+.75;
	ForceNetUpdate();
}

void AChaosImpactBuddyBall::ResolvePrisonImpact(AActor* Target)
{
	if (!HasAuthority() || !bPrison || !bLaunched || bResolvingPrisonImpact || IsPickup() || Target==Passenger.Get()) { return; }
	bResolvingPrisonImpact = true;
	UE_LOG(LogTemp,Log,TEXT("Prison impact: shell=%s target=%s remaining=%.2f position=%s"),*GetName(),*GetNameSafe(Target),GetCaptureSecondsLeft(),*GetActorLocation().ToCompactString());
	auto* Rider = Passenger.Get();
	auto* Thrower = Cast<AChaosImpactCharacter>(GetThrowingPawn());
	auto* Controller = Thrower ? Thrower->GetController() : nullptr;
	// Cache both recipients before the first TakeDamage restores (and destroys) this shell.
	const float HitDamage = Target ? UGameplayStatics::ApplyDamage(Target,1.f,Controller,this,nullptr) : 0.f;
	const float CaptiveDamage = Rider ? UGameplayStatics::ApplyDamage(Rider,1.f,Controller,this,nullptr) : 0.f;
	if (Thrower && (HitDamage>0 || CaptiveDamage>0)) { Thrower->RecoverStaminaFromBallHit(); }
	if (Rider && Rider->IsInBallForm()) { Rider->GetBallForm()->Restore(); }
}

void AChaosImpactBuddyBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactBuddyBall, Passenger);
	DOREPLIFETIME(AChaosImpactBuddyBall, Carrier);
	DOREPLIFETIME(AChaosImpactBuddyBall, bLaunched);
	DOREPLIFETIME(AChaosImpactBuddyBall, bPrison);
	DOREPLIFETIME(AChaosImpactBuddyBall, CaptureDeadline);
}

void AChaosImpactBuddyBall::OnRep_Passenger()
{
	if (!Passenger.IsValid()) { return; }
	BallMesh->SetVisibility(false, true);
	Age = 0.0f;
	ShellLook = Shell->CreateDynamicMaterialInstance(0);
	RingLook = FloorRing->CreateDynamicMaterialInstance(0);
	const FLinearColor Color = GetTeamColor();
	TeamLookColor = Color;
	if (ShellLook)
	{
		ShellLook->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (RingLook)
	{
		RingLook->SetVectorParameterValue(TEXT("Color"), Color);
	}
	GhostLooks.Reset();
	for (UStaticMeshComponent* Ghost : Ghosts)
	{
		UMaterialInstanceDynamic* Look = Ghost->CreateDynamicMaterialInstance(0);
		if (Look)
		{
			Look->SetVectorParameterValue(TEXT("Color"), Color);
		}
		GhostLooks.Add(Look);
	}
	Shell->SetVisibility(true);
	UpdateLook(0.0f);
}

FLinearColor AChaosImpactBuddyBall::GetTeamColor() const
{
	if (bPrison) { return ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Prison); }
	const AChaosImpactCharacter* Rider = Passenger.Get();
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	int32 Team = -1;
	if (Rider && Match && Match->bVersusMatch)
	{
		if (const AChaosImpactPlayerState* State = Rider->GetPlayerState<AChaosImpactPlayerState>())
		{
			Team = State->TeamIndex;
		}
	}
	else if (Rider)
	{
		Team = Rider->SoloTeam;
	}
	return Team >= 0 ? ChaosImpactMatch::GetTeamColor(Team) : FLinearColor(0.35f, 0.75f, 1.0f);
}

void AChaosImpactBuddyBall::SetSteering(const FVector& Input)
{
	if (!HasAuthority() || Input.ContainsNaN()) { return; }
	Steering = Input.GetClampedToMaxSize(1.0f);
	// Retain the input between 20 Hz owner updates, but never turn forever after a lost connection.
	SteeringUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + .15 : 0;
}

float AChaosImpactBuddyBall::GetAppear() const
{
	return BuddyEaseOutBack(Age / 0.3f);
}

FVector AChaosImpactBuddyBall::GetShellCenter() const
{
	return GetActorLocation() + FVector(0.0f, 0.0f, Bob);
}

bool AChaosImpactBuddyBall::IsPickupAvailable() const
{
	return Passenger.IsValid() && !Carrier.IsValid() && (!bLaunched || bPrison) && Super::IsPickupAvailable()
		&& (!bPrison || GetWorld()->GetTimeSeconds()>=PickupBlockedUntil);
}

bool AChaosImpactBuddyBall::ConsumePickup(AChaosImpactCharacter* Collector)
{
	if (!HasAuthority() || !IsPickupAvailable() || !CanBeCollectedBy(Collector)
		|| !Collector->TryPickupBall(this)) { return false; }
	Carrier = Collector;
	bPickupConsumed = true;
	SetOwner(Collector);
	SetInstigator(Collector);
	// Keep the actual passenger ball; never destroy it and replace it with an ordinary pickup.
	PrepareForAnimatedThrow(Collector->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator);
	if (bPrison)
	{
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
	}
	SetActorTickEnabled(true);
	FollowHand();
	// Unlike a normal pickup this actor survives; the owner must not predict/destroy a replacement ball.
	Collector->ClientBallCountReset(Collector->GetCarriedBallCount(), Collector->GetCarriedBallTypesPacked());
	Collector->ForceNetUpdate();
	ForceNetUpdate();
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::CharPickup, GetActorLocation());
	return true;
}

void AChaosImpactBuddyBall::FollowHand()
{
	if (AChaosImpactCharacter* Holder = Carrier.Get())
	{
		for (int32 Slot = 0; Slot < Holder->GetCarriedBallCount(); ++Slot)
		{
			if (Holder->GetBallForm()->GetHeldBall(Slot) == this)
			{
				SetActorLocation(Holder->GetBuddyHoldLocation(Slot));
				return;
			}
		}
	}
}

void AChaosImpactBuddyBall::Launch(const FVector& Direction, const float Speed,
	const EChaosImpactBallFlightMode Mode, const float UpSpeed)
{
	if (!HasAuthority()) { return; }
	if (bPrison) { UE_LOG(LogTemp,Log,TEXT("Prison launch: remaining=%.2f holder=%s position=%s direction=%s"),GetCaptureSecondsLeft(),*GetNameSafe(Carrier.Get()),*GetActorLocation().ToCompactString(),*Direction.ToCompactString()); }
	if (bPrison && GetInstigator()) { AdoptThrower(GetInstigator()); }
	bLaunched = true;
	LaunchedAt = GetWorld()->GetTimeSeconds();
	Carrier.Reset();
	Flash = 1.0f;
	Super::Launch(Direction, Speed, Mode, UpSpeed);
	BallMesh->SetVisibility(false, true);
	CollisionSphere->IgnoreActorWhenMoving(Passenger.Get(), true);
	Trace.Reset();
}

void AChaosImpactBuddyBall::UpdateLook(const float DeltaSeconds)
{
	// Team/PlayerState references can arrive after the passenger actor reference.
	const FLinearColor TeamColor = GetTeamColor();
	if (!TeamColor.Equals(TeamLookColor))
	{
		TeamLookColor = TeamColor;
		if (ShellLook) { ShellLook->SetVectorParameterValue(TEXT("Color"), TeamColor); }
		if (RingLook) { RingLook->SetVectorParameterValue(TEXT("Color"), TeamColor); }
		for (const auto& Look : GhostLooks) { if (Look) { Look->SetVectorParameterValue(TEXT("Color"), TeamColor); } }
	}
	Age += DeltaSeconds;
	if (bPrison && GetNetMode()!=NM_DedicatedServer)
	{
		if (!PrisonCage)
		{
			PrisonCage = ChaosImpactPrison::CreateCage(this,RootComponent);
			if (PrisonCage) { PrisonCage->SetUsingAbsoluteScale(true); PrisonCage->SetUsingAbsoluteRotation(true); }
		}
		if (PrisonCage)
		{
			PrisonCage->SetWorldLocation(GetShellCenter());
			ChaosImpactPrison::UpdateCage(PrisonCage,ShellRadius*(Carrier.IsValid() ? .82f : 1.f)*FMath::Min(GetAppear(),1.f),GetCaptureSecondsLeft(),Age,true);
		}
	}
	Flash = FMath::Max(0.0f, Flash - DeltaSeconds * 3.0f);
	const bool bHeld = Carrier.IsValid();
	const bool bFlying = bLaunched && !IsPickup();
	const bool bWaiting = !bHeld && !bLaunched;
	// Rolling: about the axis across its way, as fast as its speed over its radius; waiting it turns slowly.
	if (bFlying)
	{
		const FVector Velocity = GetBallVelocity();
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Velocity.GetSafeNormal2D());
		if (!Axis.IsNearlyZero())
		{
			Roll = FQuat(Axis.GetSafeNormal(), static_cast<float>(Velocity.Size()) / ShellRadius * DeltaSeconds) * Roll;
		}
	}
	else
	{
		Roll = FQuat(FVector::UpVector, FMath::DegreesToRadians(bHeld ? 25.0f : 40.0f) * DeltaSeconds) * Roll;
	}
	Roll.Normalize();
	Bob = bWaiting && !bPrison ? 8.0f + FMath::Sin(Age * 3.2f) * 6.0f : 0.0f;
	const float Size = ShellRadius / 50.0f * GetAppear() * (bHeld ? 0.82f : 1.0f);
	Shell->SetWorldLocationAndRotation(GetShellCenter(), Roll);
	Shell->SetWorldScale3D(FVector(FMath::Max(Size, 0.01f)));
	if (ShellLook)
	{
		const float Pulse = bWaiting ? 0.5f + 0.5f * FMath::Sin(Age * 4.0f) : 0.0f;
		const float Urgent = bPrison && GetCaptureSecondsLeft()<1.f ? 2.f*(.5f+.5f*FMath::Sin(Age*36)) : 0.f;
		ShellLook->SetScalarParameterValue(TEXT("Glow"), (bPrison ? 1.2f : 2.6f) + Pulse + Flash*6 + Urgent + (bFlying ? 1.5f : 0.f));
		ShellLook->SetScalarParameterValue(TEXT("Fill"), 0.14f + Flash * 0.4f);
	}
	// Waiting to be picked up: a ring of its colour pulsing on the floor under it.
	FloorRing->SetVisibility(!bPrison && bWaiting && Age > 0.15f);
	if (bWaiting)
	{
		const float Pulse = FMath::Sin(Age * 4.0f);
		const float Ring = (ShellRadius + 26.0f + Pulse * 6.0f) / 70.0f;
		FloorRing->SetWorldLocation(GetActorLocation() - FVector(0.0f, 0.0f, CollisionSphere->GetScaledSphereRadius() - 2.0f));
		FloorRing->SetWorldScale3D(FVector(Ring, Ring, 0.12f));
		if (RingLook)
		{
			RingLook->SetScalarParameterValue(TEXT("Intensity"), 1.6f + 1.0f * (0.5f + 0.5f * Pulse));
		}
	}
	// In flight, afterimages of the shell trail behind it, smaller and fainter the older they are.
	TraceClock += DeltaSeconds;
	if (bFlying && TraceClock >= 0.016f)
	{
		TraceClock = 0.0f;
		Trace.Insert(GetShellCenter(), 0);
		if (Trace.Num() > Ghosts.Num() + 1)
		{
			Trace.SetNum(Ghosts.Num() + 1);
		}
	}
	for (int32 I = 0; I < Ghosts.Num(); ++I)
	{
		const int32 Sample = I + 1;
		const bool bShow = bFlying && Trace.IsValidIndex(Sample);
		Ghosts[I]->SetVisibility(bShow);
		if (bShow)
		{
			const float Fade = 1.0f - static_cast<float>(I + 1) / (Ghosts.Num() + 1);
			Ghosts[I]->SetWorldLocation(Trace[Sample]);
			Ghosts[I]->SetWorldScale3D(FVector(ShellRadius / 50.0f * (0.3f + 0.45f * Fade)));
			if (GhostLooks.IsValidIndex(I) && GhostLooks[I])
			{
				GhostLooks[I]->SetScalarParameterValue(TEXT("Intensity"), 0.35f * Fade * Fade);
			}
		}
	}
	if (!bFlying)
	{
		Trace.Reset();
	}
}

void AChaosImpactBuddyBall::OnRep_Launched()
{
	if (bLaunched) { Flash = 1.0f; Trace.Reset(); }
}

void AChaosImpactBuddyBall::Tick(const float DeltaSeconds)
{
	// Replicated actor references can arrive after the actor's first frame.
	if (!Passenger.IsValid()) { if (HasAuthority()) { Destroy(); } return; }
	if (!HasAuthority())
	{
		Super::Tick(DeltaSeconds);
		// Base balls hide the owner's real projectile behind its predicted normal ball. A buddy has no preview.
		SetActorHiddenInGame(false);
		BallMesh->SetVisibility(false, true);
		if (Carrier.IsValid()) { FollowHand(); }
		UpdateLook(DeltaSeconds);
		return;
	}
	UpdateLook(DeltaSeconds);
	if (bPrison && (GetCaptureSecondsLeft()<=0 || !ChaosImpactBallTypes::IsPrisonMode(GetWorld()) || Passenger->IsEliminated()))
	{
		ReleasePassenger(); return;
	}
	if (Carrier.IsValid())
	{
		// Held pickups must not execute the base hover code, which would move them back to the floor.
		if (Carrier->IsEliminated()) { if (bPrison) { DropFromCarrier(); } else { ReleasePassenger(); } return; }
		const bool bAllied = AChaosImpactGameState::AreTeammates(GetWorld(),Carrier.Get(),Passenger.Get());
		if (bPrison ? bAllied : !bAllied) { ReleasePassenger(); return; }
		if (bPrison)
		{
			// A teammate must be able to shoot the prison even while an enemy holds it (or winds up a throw).
			CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
			CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
		}
		FollowHand();
		return;
	}
	if (bLaunched && !bPrison && (!Steering.IsNearlyZero()) && ProjectileMovement->IsActive()
		&& GetWorld()->GetTimeSeconds() <= SteeringUntil)
	{
		const FVector V = ProjectileMovement->Velocity;
		const float Speed = V.Size2D();
		const FVector Turned = FMath::VInterpNormalRotationTo(V.GetSafeNormal2D(), Steering.GetSafeNormal2D(), DeltaSeconds, 32.0f);
		ProjectileMovement->Velocity = Turned * Speed + FVector::UpVector * V.Z;
	}
	if (bLaunched || bPrison) { Super::Tick(DeltaSeconds); }
	else { UpdateNetState(); }
	if (bPrison && IsPickup() && CollisionSphere->IsSimulatingPhysics() && GetWorld()->GetTimeSeconds()<=SteeringUntil)
	{
		const FVector Velocity=CollisionSphere->GetPhysicsLinearVelocity();
		const FVector Desired=(Velocity+Steering*420.f*DeltaSeconds).GetClampedToMaxSize2D(165.f);
		CollisionSphere->SetPhysicsLinearVelocity(FVector(Desired.X,Desired.Y,Velocity.Z));
	}
	BallMesh->SetVisibility(false, true);
	if (!bPrison && bLaunched && (IsPickup() || GetWorld()->GetTimeSeconds() - LaunchedAt > 4.0))
	{
		ReleasePassenger();
		return;
	}
	if (IsPickupAvailable())
	{
		// Also retry a nearby teammate who has just freed a slot without moving out/in of the overlap.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FMath::Square(125.0f)
				&& ConsumePickup(*It)) { break; }
		}
	}
}

void AChaosImpactBuddyBall::PlayPop(const UObject* WorldContext, const FVector& At, const FLinearColor& Color, const bool bUncurl)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	// The shell bursting (glass) with sparks of the colour; curling up, only the sparks.
	if (bUncurl)
	{
		if (UNiagaraSystem* Glass = LoadEffect(Effects::Shatter))
		{
			if (UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Glass, At, FRotator(90.0f, 0.0f, 0.0f), FVector(1.4f)))
			{
				SetEffectColor(Burst, TEXT("Color"), Color * 2.0f);
			}
		}
	}
	if (UNiagaraSystem* Sparks = LoadEffect(Effects::SparkBurst))
	{
		if (UNiagaraComponent* Burst = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Sparks, At, FRotator::ZeroRotator, FVector(0.8f)))
		{
			for (const TCHAR* Name : {TEXT("Color"), TEXT("Spark Color"), TEXT("Colour")})
			{
				SetEffectColor(Burst, Name, Color * 4.0f);
			}
		}
	}
	ChaosImpactSfx::PlayAt(World, bUncurl ? EChaosImpactSfx::SmokePoof : EChaosImpactSfx::CharSwap, At);
}

void AChaosImpactBuddyBall::ReleasePassenger()
{
	if (!HasAuthority()) { return; }
	if (bPrison) { UE_LOG(LogTemp,Log,TEXT("Prison release: remaining=%.2f launched=%d pickup=%d position=%s"),GetCaptureSecondsLeft(),bLaunched,IsPickup(),*GetActorLocation().ToCompactString()); }
	if (AChaosImpactCharacter* Rider = Passenger.Get()) { Rider->GetBallForm()->Restore(); }
	else { Destroy(); }
}

float AChaosImpactBuddyBall::TakeDamage(const float Amount, const FDamageEvent& Event,
	AController* InstigatorController, AActor* Causer)
{
	if (bPrison)
	{
		const auto* Ball = Cast<AChaosImpactBall>(Causer);
		const APawn* Source = Ball ? Ball->GetThrowingPawn() : nullptr;
		if (HasAuthority() && Amount>0 && Source && Passenger.IsValid() && Source!=Passenger.Get()
			&& AChaosImpactGameState::AreTeammates(GetWorld(),Source,Passenger.Get())) { ReleasePassenger(); }
		return 0.f;
	}
	return HasAuthority() && Passenger.IsValid() ? Passenger->TakeDamage(Amount, Event, InstigatorController, Causer) : 0.0f;
}

void AChaosImpactBuddyBall::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Trail)
	{
		Trail->Deactivate();
	}
	if (HasAuthority() && Reason == EEndPlayReason::Destroyed && Passenger.IsValid()) { Passenger->GetBallForm()->Restore(); }
	Super::EndPlay(Reason);
}
