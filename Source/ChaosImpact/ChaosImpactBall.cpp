// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTrainingTarget.h"

#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactIceMeshes.h"
#include "NiagaraComponent.h"
#include "ProceduralMeshComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AChaosImpactBall::AChaosImpactBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Online rooms: the server simulates every ball. Engine movement replication is not used:
	// it snaps clients between updates and forces client physics on for rolling balls.
	// NetState carries position + velocity instead, and clients predict and smooth locally.
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(60.0f);
	SetMinNetUpdateFrequency(30.0f);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->InitSphereRadius(24.0f);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetNotifyRigidBodyCollision(true);

	BallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BallMesh"));
	BallMesh->SetupAttachment(CollisionSphere);
	BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		BallMesh->SetStaticMesh(SphereMesh.Object);
		BallMesh->SetRelativeScale3D(FVector(0.48f));
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bConstrainToPlane = true;
	ProjectileMovement->SetPlaneConstraintNormal(FVector::UpVector);
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = Bounciness;
	ProjectileMovement->SetPlaneConstraintOrigin(GetActorLocation());
	ProjectileMovement->Friction = 0.0f;
	ProjectileMovement->BounceVelocityStopSimulatingThreshold = 0.0f;
	ProjectileMovement->bForceSubStepping = true;
	ProjectileMovement->MaxSimulationTimeStep = 0.02f;
	ProjectileMovement->MaxSimulationIterations = 8;

	InitialLifeSpan = 0.0f;
}

void AChaosImpactBall::BeginPlay()
{
	Super::BeginPlay();

	ProjectileMovement->Bounciness = Bounciness;
	CollisionSphere->OnComponentHit.AddDynamic(this, &AChaosImpactBall::HandleImpact);
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AChaosImpactBall::HandlePickupOverlap);
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &AChaosImpactBall::HandleBounce);
	ThrowingPawn = GetInstigator();
	ApplyBallTypePresentation();

	if (AActor* OwningActor = ThrowingPawn.IsValid()
		? static_cast<AActor*>(ThrowingPawn.Get()) : GetOwner())
	{
		CollisionSphere->IgnoreActorWhenMoving(OwningActor, true);
	}
	if (!HasAuthority())
	{
		// A client copy is purely visual: no local physics, projectile motion or hits.
		// It still ticks to predict and smooth its displayed position.
		ProjectileMovement->Deactivate();
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorTickEnabled(true);
	}
}

void AChaosImpactBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactBall, bIsPickup);
	DOREPLIFETIME(AChaosImpactBall, bIsRolling);
	DOREPLIFETIME(AChaosImpactBall, NetState);
	DOREPLIFETIME(AChaosImpactBall, ReplicatedThrower);
	DOREPLIFETIME(AChaosImpactBall, BallType);
	DOREPLIFETIME(AChaosImpactBall, bDetonated);
}

namespace
{
	enum class EChaosImpactBallNetMode : uint8 { Held, Straight, Arc, Rolling, Hover };

	constexpr float MaxFlightExtrapolationSeconds = 0.15f;
	constexpr float MaxRollingExtrapolationSeconds = 0.1f;
	constexpr float ClientErrorDecayRate = 14.0f;
	constexpr float ClientSnapDistance = 400.0f;
	constexpr float HoverFrequency = 2.2f;
	constexpr float HoverHeight = 7.0f;
	constexpr float HoverYawSpeed = 55.0f;
	/** A state can be slightly newer than the displayed time; predict back that far instead of stalling. */
	constexpr float MaxBackExtrapolationSeconds = 0.15f;

	struct FChaosImpactNetClock
	{
		double OffsetSeconds = 0.0;
		bool bValid = false;
	};

	/** One clock per client world (several can share a process in PIE). */
	TMap<uint32, FChaosImpactNetClock>& GetNetClocks()
	{
		static TMap<uint32, FChaosImpactNetClock> Clocks;
		return Clocks;
	}
}

double AChaosImpactBall::GetServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

double AChaosImpactBall::GetPresentationServerTime() const
{
	const UWorld* World = GetWorld();
	if (HasAuthority() || !World)
	{
		return GetServerNow();
	}
	const FChaosImpactNetClock* Clock = GetNetClocks().Find(World->GetUniqueID());
	return Clock && Clock->bValid ? World->GetTimeSeconds() - Clock->OffsetSeconds : GetServerNow();
}

void AChaosImpactBall::ObserveServerTime(const double ServerTime)
{
	const UWorld* World = GetWorld();
	if (!World || ServerTime <= 0.0)
	{
		return;
	}
	// Local time minus server time = network delay plus a constant clock difference. The smallest value
	// seen is the cleanest delay; drift upward slowly so a worsening connection is followed too.
	constexpr double DriftUpRate = 0.01;
	const double Sample = World->GetTimeSeconds() - ServerTime;
	FChaosImpactNetClock& Clock = GetNetClocks().FindOrAdd(World->GetUniqueID());
	if (!Clock.bValid || Sample < Clock.OffsetSeconds)
	{
		Clock.OffsetSeconds = Sample;
		Clock.bValid = true;
	}
	else
	{
		Clock.OffsetSeconds += (Sample - Clock.OffsetSeconds) * DriftUpRate;
	}
}

void AChaosImpactBall::UpdateNetState()
{
	if (!HasAuthority() || bCosmeticPrediction || GetNetMode() == NM_Standalone || !GetWorld())
	{
		return;
	}
	FChaosImpactBallNetState NewState;
	NewState.ServerTime = GetServerNow();
	NewState.Location = GetActorLocation();
	if (GetAttachParentActor())
	{
		NewState.Mode = static_cast<uint8>(EChaosImpactBallNetMode::Held);
	}
	else if (bIsPickup && !bIsRolling)
	{
		NewState.Mode = static_cast<uint8>(EChaosImpactBallNetMode::Hover);
		NewState.Location = PickupBaseLocation;
		// A hovering ball does not move; resending it every tick would only waste bandwidth.
		if (NetState.Mode == NewState.Mode && NetState.Location.Equals(NewState.Location, 1.0f))
		{
			return;
		}
	}
	else if (bIsRolling)
	{
		NewState.Mode = static_cast<uint8>(EChaosImpactBallNetMode::Rolling);
		NewState.Velocity = CollisionSphere->GetPhysicsLinearVelocity();
	}
	else
	{
		NewState.Mode = static_cast<uint8>(ActiveFlightMode == EChaosImpactBallFlightMode::Arc
			? EChaosImpactBallNetMode::Arc : EChaosImpactBallNetMode::Straight);
		NewState.Velocity = ProjectileMovement->Velocity;
	}
	NetState = NewState;
}

FVector AChaosImpactBall::PredictNetLocation(const double ServerNow) const
{
	const FVector Start = NetState.Location;
	const FVector Velocity = NetState.Velocity;
	const float Elapsed = static_cast<float>(ServerNow - NetState.ServerTime);
	switch (static_cast<EChaosImpactBallNetMode>(NetState.Mode))
	{
	case EChaosImpactBallNetMode::Rolling:
	{
		const float Seconds = FMath::Clamp(Elapsed, -MaxBackExtrapolationSeconds, MaxRollingExtrapolationSeconds);
		return Start + FVector(Velocity.X, Velocity.Y, 0.0f) * Seconds;
	}
	case EChaosImpactBallNetMode::Straight:
	case EChaosImpactBallNetMode::Arc:
	{
		const bool bArc = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
		const float Seconds = FMath::Clamp(Elapsed, -MaxBackExtrapolationSeconds, MaxFlightExtrapolationSeconds);
		FVector End = Start + Velocity * Seconds;
		if (bArc && GetWorld())
		{
			End.Z += 0.5f * GetWorld()->GetGravityZ() * Seconds * Seconds;
		}
		// Do not extrapolate through walls: mirror off the first blocking surface like the server does.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactBallPredict), false, this);
		const float Radius = CollisionSphere->GetScaledSphereRadius();
		if (GetWorld() && GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_WorldStatic,
			FCollisionShape::MakeSphere(Radius), Params) && !Hit.bStartPenetrating)
		{
			if (bArc && Hit.ImpactNormal.Z > 0.65f)
			{
				return Hit.Location;
			}
			FVector Remaining = FMath::GetReflectionVector(End - Hit.Location, Hit.ImpactNormal);
			if (!bArc)
			{
				Remaining.Z = 0.0f;
			}
			return Hit.Location + Remaining;
		}
		return End;
	}
	case EChaosImpactBallNetMode::Held:
	case EChaosImpactBallNetMode::Hover:
	default:
		return Start;
	}
}

void AChaosImpactBall::OnRep_NetState()
{
	ObserveServerTime(NetState.ServerTime);
	if (NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Hover))
	{
		ClientHoverTime = 0.0f;
	}
	if (NetState.Mode != static_cast<uint8>(EChaosImpactBallNetMode::Straight)
		&& NetState.Mode != static_cast<uint8>(EChaosImpactBallNetMode::Arc))
	{
		// Landed, held or picked up: a later throw of this ball may hit again.
		bClientHitReported = false;
		ClientHitFrozenUntil = 0.0;
		if (NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Held))
		{
			// A new throw begins: its preview on the thrower's screen can be handed over again.
			bAwaitingLaunchAdoption = true;
			AdoptionWaitFrames = 0;
		}
		ActiveErrorDecayRate = ClientErrorDecayRate;
		// A hold that is still showing resolves itself; only allow a new one on the next flight.
		bContactHoldUsed = false;
		bHasContactCheckLocation = false;
	}
	if (NetState.Mode != static_cast<uint8>(EChaosImpactBallNetMode::Rolling))
	{
		ClientRollingSince = 0.0;
	}
	else if (ClientRollingSince <= 0.0 && GetWorld())
	{
		ClientRollingSince = GetWorld()->GetTimeSeconds();
	}
	if (!bClientHasPresentation || GetAttachParentActor())
	{
		return;
	}
	// Keep the displayed ball where it is and blend the correction away over a few frames.
	// Compare against the new state at the same moment the current position was drawn for; comparing
	// with "now" instead pulled the ball back by one frame of travel on every update.
	ClientErrorOffset = GetActorLocation() - PredictNetLocation(ClientLastPresentationTime);
	if (ClientErrorOffset.SizeSquared() > FMath::Square(ClientSnapDistance))
	{
		ClientErrorOffset = FVector::ZeroVector;
	}
}

void AChaosImpactBall::TickClientPresentation(const float DeltaSeconds)
{
	if (bDetonated)
	{
		// Burst on the server: its hazard zone shows the rest.
		SetActorHiddenInGame(true);
		return;
	}
	if (GetAttachParentActor())
	{
		// Held in a hand: attachment replication positions it. Blend out from the hand on release.
		bClientHasPresentation = false;
		// The thrower's own screen already shows its cosmetic ball in that hand.
		const AChaosImpactCharacter* Holder = Cast<AChaosImpactCharacter>(GetAttachParentActor());
		SetActorHiddenInGame(Holder && Holder->IsLocallyControlled());
		return;
	}
	// Still in the thrower's hand on the server, but the attachment has not arrived here yet (or was
	// already undone): the thrower's own screen shows its preview, so this copy stays out of sight.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (NetState.ServerTime > 0.0 && NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Held)
		&& OwnerPawn && OwnerPawn->IsLocallyControlled())
	{
		SetActorHiddenInGame(true);
		bClientHasPresentation = false;
		return;
	}
	if (IsHidden() && !bClientPickupClaimed)
	{
		SetActorHiddenInGame(false);
	}
	if (NetState.ServerTime <= 0.0)
	{
		return;
	}
	if (bClientHitReported && GetWorld() && GetWorld()->GetTimeSeconds() < ClientHitFrozenUntil)
	{
		return;
	}
	const double PresentationTime = GetPresentationServerTime();
	FVector Desired = PredictNetLocation(PresentationTime);
	const bool bHover = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Hover);
	if (bHover)
	{
		ClientHoverTime += DeltaSeconds;
		Desired.Z += FMath::Sin(ClientHoverTime * HoverFrequency) * HoverHeight;
		AddActorLocalRotation(FRotator(0.0f, DeltaSeconds * HoverYawSpeed, 0.0f));
	}
	if (!bClientHasPresentation)
	{
		bClientHasPresentation = true;
		ClientErrorOffset = GetActorLocation() - Desired;
		if (ClientErrorOffset.SizeSquared() > FMath::Square(ClientSnapDistance))
		{
			ClientErrorOffset = FVector::ZeroVector;
		}
	}
	const bool bFlightState = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
		|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
	// Also when the throw landed (e.g. hit someone right away) before ever flying on this screen,
	// so the thrower's preview does not keep flying through the target.
	if (bAwaitingLaunchAdoption
		&& (bFlightState || NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Rolling)))
	{
		TryAdoptPredictedThrow(Desired);
	}
	if (!bFlightState)
	{
		TryClaimLocalPickup();
	}
	ClientErrorOffset *= FMath::Exp(-ActiveErrorDecayRate * DeltaSeconds);
	const FVector PreviousLocation = GetActorLocation();
	SetActorLocation(Desired + ClientErrorOffset);
	ClientLastPresentationTime = PresentationTime;
	TryReportLocalHit(PreviousLocation, GetActorLocation());
}

void AChaosImpactBall::TryReportLocalHit(const FVector& From, const FVector& To)
{
	const bool bFlying = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
		|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
	if (bClientHitReported || bDetonated || !bFlying || !GetWorld())
	{
		return;
	}
	constexpr double HitFreezeSeconds = 0.6;
	const float BallRadius = CollisionSphere->GetScaledSphereRadius();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		AChaosImpactCharacter* LocalCharacter = PlayerController && PlayerController->IsLocalController()
			? Cast<AChaosImpactCharacter>(PlayerController->GetPawn()) : nullptr;
		// Dash invulnerability is judged with the owner's own, exact dash timing.
		if (!LocalCharacter || LocalCharacter->IsEliminated() || LocalCharacter->IsDashing()
			|| WasThrownBy(LocalCharacter))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = LocalCharacter->GetCapsuleComponent();
		const float HitRadius = Capsule->GetScaledCapsuleRadius() + BallRadius;
		const float AxisHalfLength = FMath::Max(0.0f,
			Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius());
		const FVector Center = Capsule->GetComponentLocation();
		FVector OnBallPath;
		FVector OnCapsuleAxis;
		FMath::SegmentDistToSegmentSafe(From, To, Center - FVector::UpVector * AxisHalfLength,
			Center + FVector::UpVector * AxisHalfLength, OnBallPath, OnCapsuleAxis);
		if (FVector::DistSquared(OnBallPath, OnCapsuleAxis) <= FMath::Square(HitRadius))
		{
			bClientHitReported = true;
			ClientHitFrozenUntil = GetWorld()->GetTimeSeconds() + HitFreezeSeconds;
			SetActorLocation(OnBallPath);
			LocalCharacter->ReportBallHitFromClient(this, OnBallPath);
			UE_LOG(LogChaosImpact, Log, TEXT("Ball hit detected locally on %s"), *LocalCharacter->GetName());
			return;
		}
	}
}

bool AChaosImpactBall::AcceptReportedHit(AChaosImpactCharacter* Victim, const FVector& HitLocation)
{
	if (!HasAuthority() || !IsValid(Victim) || !GetWorld())
	{
		return false;
	}
	float PingSeconds = 0.1f;
	if (const APlayerState* VictimState = Victim->GetPlayerState())
	{
		PingSeconds = FMath::Clamp(VictimState->GetPingInMilliseconds() / 1000.0f, 0.0f, 0.5f);
	}
	if (bDetonated)
	{
		// This copy flew through the remote victim and burst further on; their screen saw the contact first.
		AChaosImpactHazardZone* Zone = DetonationZone.Get();
		const bool bLateAccepted = Zone && !WasThrownBy(Victim)
			&& GetWorld()->GetTimeSeconds() - DetonatedAt <= PingSeconds + 0.3
			&& FVector::Dist(Victim->GetActorLocation(), HitLocation) <= 450.0f
			&& Zone->TryApplyLateHit(Victim);
		UE_LOG(LogChaosImpact, Log, TEXT("Reported hit on a detonated ball %s: victim=%s"),
			bLateAccepted ? TEXT("accepted") : TEXT("rejected"), *Victim->GetName());
		return bLateAccepted;
	}
	// The victim saw the ball while it was still flying; here it may have landed in the meantime.
	const bool bLandedMomentsAgo = bIsPickup && FlightEndedAt > 0.0
		&& GetWorld()->GetTimeSeconds() - FlightEndedAt <= PingSeconds + 0.2;
	const TCHAR* RejectReason = GetAttachParentActor() ? TEXT("held")
		: WasThrownBy(Victim) ? TEXT("own ball")
		: bIsPickup && !bLandedMomentsAgo ? TEXT("already landed")
		: nullptr;
	if (RejectReason)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Reported ball hit rejected: victim=%s reason=%s"),
			*Victim->GetName(), RejectReason);
		return false;
	}
	// Generous bounds: the victim's screen can be up to a round trip away from this copy.
	const float BallSpeed = bLandedMomentsAgo ? 2000.0f : ProjectileMovement->Velocity.Size();
	const float BallTolerance = FMath::Min(250.0f + BallSpeed * (PingSeconds + 0.1f), 1800.0f);
	const float BallError = FVector::Dist(GetActorLocation(), HitLocation);
	const float VictimError = FVector::Dist(Victim->GetActorLocation(), HitLocation);
	const bool bAccepted = BallError <= BallTolerance && VictimError <= 450.0f;
	UE_LOG(LogChaosImpact, Log,
		TEXT("Reported ball hit %s: victim=%s ballError=%.0f/%.0f victimError=%.0f ping=%.0fms"),
		bAccepted ? TEXT("accepted") : TEXT("rejected"), *Victim->GetName(), BallError, BallTolerance,
		VictimError, PingSeconds * 1000.0f);
	if (!bAccepted)
	{
		return false;
	}
	if (bLandedMomentsAgo)
	{
		// Count it once; the ball stays where it landed, briefly out of the victim's reach.
		FlightEndedAt = 0.0;
		PickupAvailableAtSeconds = FMath::Max(PickupAvailableAtSeconds,
			static_cast<float>(GetWorld()->GetTimeSeconds()) + HitPickupLockoutSeconds);
	}
	else
	{
		SetActorLocation(HitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	ResolveDamagingHit(Victim, HitLocation, (Victim->GetActorLocation() - HitLocation).GetSafeNormal2D());
	return true;
}

void AChaosImpactBall::ResolveDamagingHit(AActor* OtherActor, const FVector& ImpactPoint,
	const FVector& ImpactNormal)
{
	// A compact contact flash is independent of the target/player elimination burst,
	// so even non-lethal hits have immediate visual feedback. Special balls burst instead.
	if (!IsSpecialBall())
	{
		MulticastContactBurst(ImpactPoint, ImpactNormal.Rotation());
	}
	const float AppliedDamage = UGameplayStatics::ApplyDamage(
		OtherActor, Damage, GetInstigatorController(), this, nullptr);
	if (AppliedDamage > 0.0f)
	{
		if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(ThrowingPawn.Get());
			Thrower && Thrower != OtherActor)
		{
			Thrower->RecoverStaminaFromBallHit();
		}
	}
	if (IsSpecialBall())
	{
		Detonate(ImpactPoint, OtherActor);
		return;
	}
	DropToGroundAsPickup();
}

void AChaosImpactBall::TryAdoptPredictedThrow(const FVector& Desired)
{
	AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(ReplicatedThrower.Get());
	if (!Thrower)
	{
		// The thrower reference can arrive a few updates after the flight state.
		bAwaitingLaunchAdoption = ++AdoptionWaitFrames <= 30;
		return;
	}
	bAwaitingLaunchAdoption = false;
	if (!Thrower->IsLocallyControlled())
	{
		return;
	}
	if (AChaosImpactBall* Predicted = Thrower->TakePredictedThrowBall())
	{
		// Continue from the ball this screen has been showing since the release.
		ClientErrorOffset = Predicted->GetActorLocation() - Desired;
		if (ClientErrorOffset.SizeSquared() > FMath::Square(ClientSnapDistance))
		{
			ClientErrorOffset = FVector::ZeroVector;
		}
		// Blend the small remaining difference away slowly so the thrower never sees a jump.
		constexpr float AdoptedErrorDecayRate = 4.0f;
		const bool bStillFlying = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
			|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
		ActiveErrorDecayRate = bStillFlying ? AdoptedErrorDecayRate : ClientErrorDecayRate;
		InheritContactPresentation(*Predicted);
		Predicted->Destroy();
		UE_LOG(LogChaosImpact, Log, TEXT("Predicted throw adopted (offset %.0f, along flight %.0f)"),
			ClientErrorOffset.Size(), FVector::DotProduct(ClientErrorOffset, FVector(NetState.Velocity).GetSafeNormal()));
	}
}

void AChaosImpactBall::TryClaimLocalPickup()
{
	const bool bPickupState = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Hover)
		|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Rolling);
	if (bClientPickupClaimed || !bPickupState || !GetWorld()
		|| GetWorld()->GetTimeSeconds() < ClientPickupRetryAt)
	{
		return;
	}
	// Mirrors the server's short lockout after a hit, so claims are not refused.
	constexpr double RollingLockoutSeconds = 0.6;
	if (NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Rolling)
		&& GetWorld()->GetTimeSeconds() - ClientRollingSince < RollingLockoutSeconds)
	{
		return;
	}
	const float BallRadius = CollisionSphere->GetScaledSphereRadius();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		AChaosImpactCharacter* LocalCharacter = PlayerController && PlayerController->IsLocalController()
			? Cast<AChaosImpactCharacter>(PlayerController->GetPawn()) : nullptr;
		if (!LocalCharacter || LocalCharacter->IsEliminated() || LocalCharacter->IsEliminationPredicted()
			|| LocalCharacter->GetCarriedBallCount() >= LocalCharacter->GetMaximumCarriedBalls())
		{
			continue;
		}
		const UCapsuleComponent* Capsule = LocalCharacter->GetCapsuleComponent();
		const FVector Delta = GetActorLocation() - Capsule->GetComponentLocation();
		const float Reach = Capsule->GetScaledCapsuleRadius() + BallRadius;
		if (FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(Reach)
			&& FMath::Abs(Delta.Z) <= Capsule->GetScaledCapsuleHalfHeight() + BallRadius)
		{
			bClientPickupClaimed = true;
			SetActorHiddenInGame(true);
			LocalCharacter->ClaimPickupFromClient(this);
			return;
		}
	}
}

void AChaosImpactBall::CancelLocalPickupClaim()
{
	bClientPickupClaimed = false;
	SetActorHiddenInGame(false);
	if (GetWorld())
	{
		ClientPickupRetryAt = GetWorld()->GetTimeSeconds() + 0.3;
	}
}

bool AChaosImpactBall::ConsumePickup(AChaosImpactCharacter* Collector)
{
	if (!HasAuthority() || bCosmeticPrediction || !IsPickupAvailable() || bPickupConsumed
		|| !IsValid(Collector) || !Collector->TryPickupBall(this))
	{
		return false;
	}
	bPickupConsumed = true;
	Destroy();
	return true;
}

void AChaosImpactBall::EndCosmeticFlight()
{
	// A preview that lands before the server's ball takes over simply disappears.
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
	if (IsSpecialBall())
	{
		// A special preview would burst here; the server's zone arrives a moment later.
		SetActorHiddenInGame(true);
	}
	SetLifeSpan(0.25f);
}

bool AChaosImpactBall::IsFlyingForPresentation() const
{
	if (GetAttachParentActor())
	{
		return false;
	}
	if (HasAuthority())
	{
		return !bIsPickup && ProjectileMovement->IsActive();
	}
	return NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
		|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
}

bool AChaosImpactBall::ShouldHoldForCharacter(const AChaosImpactCharacter* Character) const
{
	if (!IsValid(Character) || Character->IsEliminated() || Character->IsDashingForPresentation()
		|| WasThrownBy(Character))
	{
		return false;
	}
	// The host's real ball only passes through remote players (everyone else it physically hits);
	// clients and throw previews draw every other character from the network.
	return HasAuthority() && !bCosmeticPrediction
		? Character->IsRemotePlayerOnServer()
		: !Character->IsLocallyControlled();
}

void AChaosImpactBall::ResetContactPresentation()
{
	VisualOffset = FVector::ZeroVector;
	bContactHolding = false;
	bContactDropping = false;
	bContactHoldUsed = false;
	bHasContactCheckLocation = false;
	ContactTarget.Reset();
	if (BallMesh)
	{
		BallMesh->SetRelativeLocation(FVector::ZeroVector);
	}
}

void AChaosImpactBall::InheritContactPresentation(const AChaosImpactBall& Preview)
{
	// This ball is placed exactly where the preview's actor was, so the preview's mesh offset carries over.
	VisualOffset = Preview.VisualOffset;
	bContactHolding = Preview.bContactHolding;
	bContactHoldUsed = Preview.bContactHoldUsed;
	ContactHoldPoint = Preview.ContactHoldPoint;
	ContactHoldUntil = Preview.ContactHoldUntil;
	ContactTarget = Preview.ContactTarget;
	ContactHoldStartedAt = Preview.ContactHoldStartedAt;
	bContactDropping = Preview.bContactDropping;
	ContactDropStart = Preview.ContactDropStart;
	ContactDropVelocity = Preview.ContactDropVelocity;
	ContactDropStartedAt = Preview.ContactDropStartedAt;
	bHasContactCheckLocation = false;
}

void AChaosImpactBall::UpdateContactPresentation(const float DeltaSeconds)
{
	if (GetNetMode() == NM_Standalone || !GetWorld() || !BallMesh)
	{
		return;
	}
	if (GetAttachParentActor())
	{
		if (bContactHolding || !VisualOffset.IsZero())
		{
			ResetContactPresentation();
		}
		return;
	}
	constexpr float VisualOffsetDecayRate = 10.0f;
	constexpr float ConfirmationMarginSeconds = 0.08f;
	constexpr float MinWaitSeconds = 0.1f;
	constexpr float MaxWaitSeconds = 0.6f;
	// The ball stays still on the body for at most this long before showing the likely hit.
	constexpr float MaxStillSeconds = 0.1f;
	// Same as a real hit on the server (DropToGroundAsPickup -> MakeRollingPickup).
	static constexpr float RollingSpeedScale = 0.55f;
	static constexpr float RollingMaxImpactSpeed = 1600.0f;
	static constexpr float RollingDampingRate = 0.75f;
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Actual = GetActorLocation();
	const bool bFlying = IsFlyingForPresentation();
	const float BallRadius = CollisionSphere->GetScaledSphereRadius();
	const auto ContactDisplayAt = [this](const double Time)
	{
		if (!bContactDropping)
		{
			return ContactHoldPoint;
		}
		const float Elapsed = static_cast<float>(Time - ContactDropStartedAt);
		const float Travel = (1.0f - FMath::Exp(-RollingDampingRate * Elapsed)) / RollingDampingRate;
		return ContactDropStart + ContactDropVelocity * Travel;
	};

	if (bContactHolding)
	{
		const bool bTargetDown = !ContactTarget.IsValid() || ContactTarget->IsEliminated();
		const bool bConfirmed = !bFlying || bTargetDown;
		if (bConfirmed || Now >= ContactHoldUntil)
		{
			// Landed (hit confirmed) or the wait ran out (a miss): blend on from what is shown now.
			VisualOffset = ContactDisplayAt(Now) - Actual;
			bContactHolding = false;
			bContactDropping = false;
			UE_LOG(LogChaosImpact, Log, TEXT("Contact hold %s after %.2fs"),
				bConfirmed ? TEXT("confirmed") : TEXT("missed"), Now - ContactHoldStartedAt);
		}
		else
		{
			if (!bContactDropping && Now - ContactHoldStartedAt >= MaxStillSeconds)
			{
				// Still unconfirmed: show the likely outcome (a hit) instead of a ball frozen in the air.
				FHitResult Ground;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactBallContactDrop), false, this);
				ContactDropStart = GetWorld()->LineTraceSingleByChannel(Ground,
					ContactHoldPoint + FVector::UpVector * 20.0f, ContactHoldPoint - FVector::UpVector * 2000.0f,
					ECC_WorldStatic, Params)
					? Ground.ImpactPoint + Ground.ImpactNormal * BallRadius : ContactHoldPoint;
				const FVector Incoming = HasAuthority() ? ProjectileMovement->Velocity : FVector(NetState.Velocity);
				ContactDropVelocity = FVector(Incoming.X, Incoming.Y, 0.0f)
					.GetClampedToMaxSize(RollingMaxImpactSpeed) * RollingSpeedScale;
				ContactDropStartedAt = Now;
				bContactDropping = true;
				UE_LOG(LogChaosImpact, Log, TEXT("Contact predicted drop after %.2fs"), Now - ContactHoldStartedAt);
			}
			VisualOffset = ContactDisplayAt(Now) - Actual;
		}
	}
	else if (bFlying && !bContactHoldUsed && bHasContactCheckLocation)
	{
		const FVector From = LastContactCheckLocation + VisualOffset;
		const FVector To = Actual + VisualOffset;
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			AChaosImpactCharacter* Character = *It;
			// Hits on the host's own player and on CPUs are decided by the host at once, so there is
			// nothing to wait for; holding would only make a near miss stop and then lurch.
			const AChaosImpactPlayerState* TargetState = Character
				? Character->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			const bool bConfirmationDelayed = (HasAuthority() && !bCosmeticPrediction)
				|| (TargetState && !TargetState->IsABot() && !TargetState->bHostMachine);
			if (!ShouldHoldForCharacter(Character) || !bConfirmationDelayed)
			{
				continue;
			}
			const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
			const FVector Center = Character->GetPresentationLocation();
			const float HitRadius = Capsule->GetScaledCapsuleRadius() + BallRadius;
			const float AxisHalfLength = FMath::Max(0.0f,
				Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius());
			FVector OnPath;
			FVector OnAxis;
			FMath::SegmentDistToSegmentSafe(From, To, Center - FVector::UpVector * AxisHalfLength,
				Center + FVector::UpVector * AxisHalfLength, OnPath, OnAxis);
			if (FVector::DistSquared(OnPath, OnAxis) > FMath::Square(HitRadius))
			{
				continue;
			}
			FVector Outward = FVector(OnPath.X - OnAxis.X, OnPath.Y - OnAxis.Y, 0.0f).GetSafeNormal();
			if (Outward.IsNearlyZero())
			{
				Outward = (From - To).GetSafeNormal2D();
			}
			ContactHoldPoint = FVector(OnAxis.X, OnAxis.Y, OnPath.Z) + Outward * HitRadius;
			// Wait about as long as the confirmation takes to reach this screen.
			float LocalRoundTrip = 0.0f;
			if (!HasAuthority() || bCosmeticPrediction)
			{
				const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
				if (const APlayerState* LocalState = LocalController ? LocalController->PlayerState.Get() : nullptr)
				{
					LocalRoundTrip = FMath::Clamp(LocalState->GetPingInMilliseconds() / 1000.0f, 0.0f, 0.5f);
				}
			}
			const float HoldSeconds = FMath::Clamp(Character->GetNetworkRoundTripSeconds()
				+ LocalRoundTrip * 0.5f + ConfirmationMarginSeconds, MinWaitSeconds, MaxWaitSeconds);
			ContactHoldUntil = Now + HoldSeconds;
			ContactHoldStartedAt = Now;
			bContactDropping = false;
			ContactTarget = Character;
			bContactHolding = true;
			bContactHoldUsed = true;
			VisualOffset = ContactHoldPoint - Actual;
			UE_LOG(LogChaosImpact, Log, TEXT("Contact hold started on %s for %.2fs"), *Character->GetName(), HoldSeconds);
			break;
		}
	}
	if (!bContactHolding)
	{
		VisualOffset *= FMath::Exp(-VisualOffsetDecayRate * DeltaSeconds);
		if (VisualOffset.SizeSquared() < 0.01f)
		{
			VisualOffset = FVector::ZeroVector;
		}
	}

	FVector Display = Actual + VisualOffset;
	if (bFlying && !bContactHolding)
	{
		// Never draw an unconfirmed ball inside someone's drawn body: it grazes past instead.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (!ShouldHoldForCharacter(*It))
			{
				continue;
			}
			const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
			const FVector Center = It->GetPresentationLocation();
			const float HitRadius = Capsule->GetScaledCapsuleRadius() + BallRadius;
			const FVector Flat(Display.X - Center.X, Display.Y - Center.Y, 0.0f);
			if (FMath::Abs(Display.Z - Center.Z) <= Capsule->GetScaledCapsuleHalfHeight() + BallRadius
				&& Flat.SizeSquared() < FMath::Square(HitRadius))
			{
				const FVector Outward = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
				Display.X = Center.X + Outward.X * HitRadius;
				Display.Y = Center.Y + Outward.Y * HitRadius;
			}
		}
	}
	BallMesh->SetWorldLocation(Display);
	LastContactCheckLocation = Actual;
	bHasContactCheckLocation = bFlying;
}

void AChaosImpactBall::MulticastContactBurst_Implementation(FVector_NetQuantize Location, FRotator Rotation)
{
	if (UNiagaraSystem* ContactBurst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Damage))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ContactBurst,
			Location, Rotation, FVector(ContactEffectScale));
	}
}

void AChaosImpactBall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		TickClientPresentation(DeltaSeconds);
		UpdateContactPresentation(DeltaSeconds);
		UpdateBallTypePresentation(DeltaSeconds);
		TraceLocalThrow();
		return;
	}
	if (bDetonated)
	{
		return;
	}
	if (!bIsPickup)
	{
		FlightSeconds += DeltaSeconds;
		if (FlightSeconds >= LifeSeconds)
		{
			if (IsSpecialBall() && !bCosmeticPrediction)
			{
				Detonate(GetActorLocation(), nullptr);
				return;
			}
			DropToGroundAsPickup();
		}
	}
	else if (!bIsRolling)
	{
		PickupAnimationTime += DeltaSeconds;
		const float Hover = FMath::Sin(PickupAnimationTime * HoverFrequency) * HoverHeight;
		SetActorLocation(PickupBaseLocation + FVector::UpVector * Hover);
		AddActorLocalRotation(FRotator(0.0f, DeltaSeconds * HoverYawSpeed, 0.0f));
	}
	UpdateNetState();
	UpdateContactPresentation(DeltaSeconds);
	UpdateBallTypePresentation(DeltaSeconds);
	TraceLocalThrow();
}

void AChaosImpactBall::TraceLocalThrow() const
{
#if !UE_BUILD_SHIPPING
	static const bool bTraceEnabled = FParse::Param(FCommandLine::Get(), TEXT("CIBallTrace"));
	if (!bTraceEnabled || GetNetMode() != NM_Client || !BallMesh || !GetWorld())
	{
		return;
	}
	const APawn* ThrowerPawn = bCosmeticPrediction ? GetInstigator()
		: ReplicatedThrower ? ReplicatedThrower.Get() : Cast<APawn>(GetOwner());
	if (!ThrowerPawn || !ThrowerPawn->IsLocallyControlled())
	{
		return;
	}
	// Previews report 0 held / 1 flying / 9 ended; server balls report their replicated mode.
	const int32 Mode = bCosmeticPrediction
		? (GetAttachParentActor() ? 0 : ProjectileMovement->IsActive() ? 1 : 9)
		: static_cast<int32>(NetState.Mode);
	const FVector Drawn = BallMesh->GetComponentLocation();
	UE_LOG(LogChaosImpact, Log, TEXT("BallTrace f=%llu t=%.4f id=%u kind=%s mode=%d attached=%d hidden=%d pos=%.1f,%.1f,%.1f"),
		static_cast<uint64>(GFrameCounter), GetWorld()->GetTimeSeconds(), GetUniqueID(),
		bCosmeticPrediction ? TEXT("preview") : TEXT("server"), Mode, GetAttachParentActor() ? 1 : 0,
		IsHidden() ? 1 : 0, Drawn.X, Drawn.Y, Drawn.Z);
#endif
}

void AChaosImpactBall::Launch(const FVector& Direction, const float Speed,
	const EChaosImpactBallFlightMode FlightMode, const float ArcUpwardSpeed)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ResetContactPresentation();
	const FVector HorizontalDirection(Direction.X, Direction.Y, 0.0f);
	const bool bArc = FlightMode == EChaosImpactBallFlightMode::Arc;
	ActiveFlightMode = FlightMode;
	FlightSeconds = 0.0f;
	if (!ThrowingPawn.IsValid())
	{
		ThrowingPawn = GetInstigator();
	}
	if (ThrowingPawn.IsValid())
	{
		CollisionSphere->IgnoreActorWhenMoving(ThrowingPawn.Get(), true);
	}
	ReplicatedThrower = ThrowingPawn.Get();
	if (HasAuthority() && GetNetMode() != NM_Standalone && GetWorld())
	{
		// Remote players judge their own hits, so this copy flies through their slightly stale positions.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (It->IsRemotePlayerOnServer())
			{
				CollisionSphere->IgnoreActorWhenMoving(*It, true);
			}
		}
	}
	bIsPickup = false;
	bIsRolling = false;
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	if (bCosmeticPrediction)
	{
		// The preview never touches players; the victim's own screen judges real hits.
		CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	SetActorTickEnabled(true);
	ProjectileMovement->Activate(true);
	ProjectileMovement->ProjectileGravityScale = bArc ? 1.0f : 0.0f;
	ProjectileMovement->bConstrainToPlane = !bArc;
	ProjectileMovement->SetPlaneConstraintEnabled(!bArc);
	ProjectileMovement->Bounciness = bArc ? 0.72f : Bounciness;
	ProjectileMovement->Friction = bArc ? 0.12f : 0.0f;
	ProjectileMovement->Velocity = HorizontalDirection.GetSafeNormal() * Speed
		+ (bArc ? FVector::UpVector * ArcUpwardSpeed : FVector::ZeroVector);
	UpdateNetState();
}

void AChaosImpactBall::PrepareForAnimatedThrow(USceneComponent* HandParent,
	const FName HandSocket, const FVector& RelativeLocation, const FRotator& RelativeRotation)
{
	if (!HandParent)
	{
		return;
	}
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
	AttachToComponent(HandParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		HandSocket);
	SetActorRelativeLocation(RelativeLocation);
	SetActorRelativeRotation(RelativeRotation);
	ResetContactPresentation();
	UpdateNetState();
}

void AChaosImpactBall::MakePickup()
{
	if (bCosmeticPrediction)
	{
		EndCosmeticFlight();
		return;
	}
	if (!bIsPickup && GetWorld())
	{
		FlightEndedAt = GetWorld()->GetTimeSeconds();
	}
	bIsPickup = true;
	bIsRolling = false;
	bPickupConsumed = false;
	PickupAvailableAtSeconds = 0.0f;
	PickupAnimationTime = 0.0f;
	PickupBaseLocation = GetActorLocation();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	SetActorTickEnabled(true);
	UpdateNetState();
}

void AChaosImpactBall::MakeRollingPickup(const FVector& ImpactVelocity)
{
	if (bCosmeticPrediction)
	{
		EndCosmeticFlight();
		return;
	}
	if (!bIsPickup && GetWorld())
	{
		FlightEndedAt = GetWorld()->GetTimeSeconds();
	}
	bIsPickup = true;
	bIsRolling = true;
	bPickupConsumed = false;
	PickupAvailableAtSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds() + HitPickupLockoutSeconds : 0.0f;
	PickupAnimationTime = 0.0f;
	SetOwner(nullptr);
	SetInstigator(nullptr);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();

	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetLinearDamping(0.75f);
	CollisionSphere->SetAngularDamping(0.18f);
	CollisionSphere->SetEnableGravity(true);
	CollisionSphere->SetSimulatePhysics(true);

	const FVector RollingVelocity = FVector(ImpactVelocity.X, ImpactVelocity.Y, 0.0f)
		.GetClampedToMaxSize(1600.0f) * 0.55f;
	CollisionSphere->SetPhysicsLinearVelocity(RollingVelocity);
	const float Radius = FMath::Max(CollisionSphere->GetScaledSphereRadius(), 1.0f);
	const FVector AngularVelocity = FVector::CrossProduct(FVector::UpVector, RollingVelocity) / Radius;
	CollisionSphere->SetPhysicsAngularVelocityInRadians((AngularVelocity));
	SetActorTickEnabled(true);
	UpdateNetState();
}

bool AChaosImpactBall::WasThrownBy(const APawn* Pawn) const
{
	return Pawn && (ThrowingPawn.Get() == Pawn || GetInstigator() == Pawn || GetOwner() == Pawn
		|| ReplicatedThrower == Pawn);
}

FVector AChaosImpactBall::GetBallVelocity() const
{
	if (!HasAuthority())
	{
		return NetState.Velocity;
	}
	return bIsRolling && CollisionSphere
		? CollisionSphere->GetPhysicsLinearVelocity()
		: ProjectileMovement ? ProjectileMovement->Velocity : FVector::ZeroVector;
}

bool AChaosImpactBall::IsPickupAvailable() const
{
	return bIsPickup && (!GetWorld()
		|| GetWorld()->GetTimeSeconds() >= PickupAvailableAtSeconds);
}

void AChaosImpactBall::DropToGroundAsPickup()
{
	if (bIsPickup || !GetWorld())
	{
		return;
	}

	const FVector ImpactVelocity = ProjectileMovement->Velocity;
	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ChaosImpactBallDrop), false, this);
	const FVector TraceStart = GetActorLocation() + FVector::UpVector * 80.0f;
	const FVector TraceEnd = GetActorLocation() - FVector::UpVector * 2000.0f;
	if (GetWorld()->LineTraceSingleByChannel(
		GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		SetActorLocation(GroundHit.ImpactPoint
			+ GroundHit.ImpactNormal * CollisionSphere->GetScaledSphereRadius());
	}
	MakeRollingPickup(ImpactVelocity);
}

void AChaosImpactBall::HandleImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bIsPickup || bDetonated || !HasAuthority() || bCosmeticPrediction)
	{
		return;
	}
	if (!IsValid(OtherActor) || OtherActor == this || OtherActor == GetOwner()
		|| OtherActor == GetInstigator() || OtherActor == ThrowingPawn.Get())
	{
		return;
	}

	const bool bHitPawn = OtherActor->IsA<APawn>();
	const bool bHitTrainingTarget = OtherActor->IsA<AChaosImpactTrainingTarget>();
	if (const AChaosImpactCharacter* HitCharacter = Cast<AChaosImpactCharacter>(OtherActor);
		HitCharacter && HitCharacter->IsRemotePlayerOnServer())
	{
		// Remote players judge their own hits (TryReportLocalHit on their client).
		return;
	}
	if ((bHitPawn || bHitTrainingTarget) && OtherActor->CanBeDamaged())
	{
		ResolveDamagingHit(OtherActor, Hit.ImpactPoint, Hit.ImpactNormal);
	}
}

void AChaosImpactBall::HandlePickupOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	AChaosImpactCharacter* Collector = Cast<AChaosImpactCharacter>(OtherActor);
	// Remote players claim pickups from their own screen; this copy of them is slightly behind.
	if (Collector && !Collector->IsRemotePlayerOnServer())
	{
		ConsumePickup(Collector);
	}
}

void AChaosImpactBall::HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}
	if (ImpactResult.GetActor() && ImpactResult.GetActor()->IsA<APawn>())
	{
		return;
	}
	if (IsSpecialBall())
	{
		// Wall, floor or anything else: special balls burst on the first contact.
		if (bCosmeticPrediction)
		{
			EndCosmeticFlight();
		}
		else
		{
			Detonate(GetActorLocation(), nullptr);
		}
		return;
	}
	if (ActiveFlightMode == EChaosImpactBallFlightMode::Arc
		&& ImpactResult.ImpactNormal.Z > 0.65f)
	{
		SetActorLocation(ImpactResult.ImpactPoint
			+ ImpactResult.ImpactNormal * CollisionSphere->GetScaledSphereRadius());
		MakeRollingPickup(ImpactVelocity);
		return;
	}

	bHasReflected = true;
	++ReflectionCount;

	// A reflected ball can hit other pawns, but never the character who threw it.
}

void AChaosImpactBall::SetBallType(const EChaosImpactBallType Type)
{
	BallType = Type;
	if (HasActorBegunPlay())
	{
		ApplyBallTypePresentation();
	}
}

void AChaosImpactBall::OnRep_BallType()
{
	if (HasActorBegunPlay())
	{
		ApplyBallTypePresentation();
	}
}

void AChaosImpactBall::OnRep_Detonated()
{
	if (bDetonated)
	{
		SetActorHiddenInGame(true);
	}
}

void AChaosImpactBall::Detonate(const FVector& Location, AActor* DirectVictim)
{
	if (!HasAuthority() || bCosmeticPrediction || bDetonated || !IsSpecialBall() || !GetWorld())
	{
		return;
	}
	bDetonated = true;
	DetonatedAt = GetWorld()->GetTimeSeconds();
	FlightEndedAt = DetonatedAt;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
	DetonationZone = AChaosImpactHazardZone::Detonate(GetWorld(), BallType, Location, ThrowingPawn.Get(), DirectVictim);
	UE_LOG(LogChaosImpact, Log, TEXT("%s ball detonated at %s (direct victim %s)"),
		BallType == EChaosImpactBallType::Ice ? TEXT("Ice") : TEXT("Fire"),
		*Location.ToCompactString(), *GetNameSafe(DirectVictim));
	// Kept hidden for a moment so a hit reported from a remote screen just before can still count.
	SetLifeSpan(1.0f);
	ForceNetUpdate();
}

void AChaosImpactBall::ApplyBallTypePresentation()
{
	if (bTypePresentationBuilt || !IsSpecialBall() || !BallMesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	bTypePresentationBuilt = true;
	using namespace ChaosImpactBallTypes;
	const bool bFire = BallType == EChaosImpactBallType::Fire;

	if (bFire)
	{
		// A glowing core with real flames licking off it; they stream behind the ball in flight.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(1.0f, 0.24f, 0.01f), 1.1f));
		if (UNiagaraSystem* Flames = LoadEffect(Effects::Fire))
		{
			TypeAuraEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(Flames, BallMesh, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
			if (TypeAuraEffect)
			{
				TypeAuraEffect->SetUsingAbsoluteScale(true);
				TypeAuraEffect->SetWorldScale3D(FVector::OneVector);
				SetEffectFloat(TypeAuraEffect, TEXT("Flame Scale"), 0.9f);
				SetEffectFloat(TypeAuraEffect, TEXT("Smoke Spawn Scale"), 0.15f);
				SetEffectFloat(TypeAuraEffect, TEXT("Base Light Intentsity"), 0.0f);
			}
		}
	}
	else
	{
		// A frosted core inside clear, faceted ice with a few crystals breaking through.
		BallMesh->SetMaterial(0, MakeIceCrystal(this, 0.9f, 0.22f, FLinearColor(0.5f, 0.72f, 0.9f)));
		ChaosImpactIceMeshes::FMeshBuffers Gem;
		FRandomStream Stream(static_cast<int32>(GetUniqueID()));
		ChaosImpactIceMeshes::AppendGem(Gem, FTransform::Identity, 31.0f, Stream);
		constexpr int32 SpikeCount = 6;
		for (int32 Index = 0; Index < SpikeCount; ++Index)
		{
			const float Z = 1.0f - 2.0f * (Index + 0.5f) / SpikeCount;
			const float Ring = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
			const float Angle = Index * 2.39996323f;
			const FVector Direction(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, Z);
			ChaosImpactIceMeshes::AppendCrystal(Gem,
				FTransform(FRotationMatrix::MakeFromZ(Direction).Rotator(), Direction * 20.0f),
				Stream.FRandRange(4.5f, 6.5f), Stream.FRandRange(22.0f, 30.0f), Stream);
		}
		if (UProceduralMeshComponent* GemMesh = ChaosImpactIceMeshes::CreateComponent(this, BallMesh, Gem,
			MakeIceCrystal(this, 0.4f, 0.2f)))
		{
			GemMesh->SetUsingAbsoluteScale(true);
			GemMesh->SetWorldScale3D(FVector::OneVector);
			TypeVisual = GemMesh;
		}
	}

	TypeLight = NewObject<UPointLightComponent>(this);
	TypeLight->SetupAttachment(BallMesh);
	TypeLight->SetLightColor(bFire ? FLinearColor(1.0f, 0.45f, 0.1f) : FLinearColor(0.5f, 0.82f, 1.0f));
	TypeLight->SetIntensity(bFire ? 3500.0f : 1800.0f);
	TypeLight->SetAttenuationRadius(bFire ? 360.0f : 260.0f);
	TypeLight->SetCastShadows(false);
	TypeLight->RegisterComponent();
	SetActorTickEnabled(true);
}

void AChaosImpactBall::UpdateBallTypePresentation(const float DeltaSeconds)
{
	if (!BallMesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	TypeFxTime += DeltaSeconds;
	const float T = TypeFxTime;
	const bool bFire = BallType == EChaosImpactBallType::Fire;

	// Every thrown ball leaves a trail: a ribbon for a normal ball, smoke for fire, cold vapor for ice.
	const bool bFlying = IsFlyingForPresentation() && !IsHidden() && !bDetonated;
	if (bFlying && !FlightTrailEffect)
	{
		using namespace ChaosImpactBallTypes;
		const TCHAR* TrailPath = bFire ? Effects::FireTrail : Effects::BallTrail;
		if (UNiagaraSystem* Trail = LoadEffect(TrailPath))
		{
			FlightTrailEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(Trail, BallMesh, NAME_None,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
			if (FlightTrailEffect)
			{
				FlightTrailEffect->SetUsingAbsoluteScale(true);
				FlightTrailEffect->SetWorldScale3D(FVector::OneVector);
				if (bFire)
				{
					SetEffectColor(FlightTrailEffect, TEXT("Smoke Color"), FLinearColor(0.16f, 0.13f, 0.11f));
				}
				bFlightTrailOn = true;
			}
		}
	}
	else if (FlightTrailEffect && bFlying != bFlightTrailOn)
	{
		bFlightTrailOn = bFlying;
		if (bFlying)
		{
			FlightTrailEffect->Activate(true);
		}
		else
		{
			FlightTrailEffect->Deactivate();
		}
	}

	if (TypeVisual)
	{
		TypeVisual->SetRelativeRotation(FRotator(T * 70.0f, T * 120.0f, 0.0f));
	}
	if (TypeLight)
	{
		TypeLight->SetIntensity(bFire
			? 3500.0f * (0.82f + 0.12f * FMath::Sin(T * 23.0f) + 0.06f * FMath::Sin(T * 41.0f))
			: 1800.0f * (0.92f + 0.08f * FMath::Sin(T * 3.0f)));
	}
}
