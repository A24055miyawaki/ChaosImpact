// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactBall.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTrainingTarget.h"

#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactDriveBall.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactTornado.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
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
	BallMeshBaseScale = BallMesh->GetRelativeScale3D();
	CollisionSphere->OnComponentHit.AddDynamic(this, &AChaosImpactBall::HandleImpact);
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AChaosImpactBall::HandlePickupOverlap);
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &AChaosImpactBall::HandleBounce);
	// Who it may pass through (see UpdateDashPassThrough) is settled before it moves each frame.
	ProjectileMovement->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
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
	DOREPLIFETIME(AChaosImpactBall, SnowScale);
	DOREPLIFETIME(AChaosImpactBall, LandedPickupExpiresAt);
	DOREPLIFETIME(AChaosImpactBall, PickupAvailableAtSeconds);
	DOREPLIFETIME(AChaosImpactBall, DriveDeadline);
}

namespace
{
	enum class EChaosImpactBallNetMode : uint8 { Held, Straight, Arc, Rolling, Hover };

	// A flight is fully known from one update (straight or a fixed arc, mirrored off walls), so it can be carried on
	// well past the newest update: at a high ping with jitter or a lost packet the ball never stops in the air.
	constexpr float MaxFlightExtrapolationSeconds = 0.6f;
	constexpr float MaxRollingExtrapolationSeconds = 0.35f;
	constexpr float ThunderMaxAheadSeconds = 0.12f;
	constexpr float ClientErrorDecayRate = 14.0f;
	// Bigger differences than this are a different ball state altogether (respawned, picked up): jump to it.
	// Anything smaller is blended away, so a late update never makes a ball jump.
	constexpr float ClientSnapDistance = 900.0f;
	/** A very fast ball (a thunder ball after a few walls) travels that far in moments: allow a little time of it. */
	constexpr float ClientSnapSeconds = 0.2f;
	constexpr float HoverFrequency = 2.2f;
	constexpr float HoverHeight = 7.0f;
	constexpr float HoverYawSpeed = 55.0f;
	/** A state can be newer than the displayed time (and the thrower's own ball can be shown a little behind its
	 * server copy); predict back that far instead of stalling. */
	constexpr float MaxBackExtrapolationSeconds = 0.4f;

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

FVector AChaosImpactBall::PredictNetLocation(const double ServerNow, bool* bOutBurst) const
{
	if (bOutBurst)
	{
		*bOutBurst = false;
	}
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
		// The thrower's own ball may be shown a little ahead (ClientTimeLead); allow that much further. A thunder ball
		// gets faster at every wall (up to two metres a frame) and soon bounces about in corners: only a short way
		// ahead of its updates, never far enough to go wrong.
		const float MaxAhead = BallType == EChaosImpactBallType::Thunder ? ThunderMaxAheadSeconds : MaxFlightExtrapolationSeconds;
		const float Seconds = FMath::Clamp(Elapsed, -MaxBackExtrapolationSeconds, MaxAhead + ClientTimeLead);
		FVector End = Start + Velocity * Seconds;
		if (bArc && GetWorld())
		{
			End.Z += 0.5f * GetWorld()->GetGravityZ() * GetArcGravityScale() * Seconds * Seconds;
		}
		if (BallType == EChaosImpactBallType::Beam)
		{
			// A beam goes straight through everything.
			return End;
		}
		// Do not extrapolate through walls: do at each wall what the server does there.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChaosImpactBallPredict), false, this);
		const float Radius = CollisionSphere->GetScaledSphereRadius();
		// A special ball (all but thunder) bursts on the first wall: it is shown stopping there, never through it.
		const bool bBurstsOnWalls = IsSpecialBall() && BallType != EChaosImpactBallType::Thunder;
		// Only the stage (players, other balls and effects are not walls: a ball meeting them is decided otherwise).
		const FCollisionObjectQueryParams StageOnly(ECC_WorldStatic);
		if (bArc || Seconds <= 0.0f || !GetWorld())
		{
			FHitResult Hit;
			if (GetWorld() && GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, StageOnly,
				FCollisionShape::MakeSphere(Radius), Params) && !Hit.bStartPenetrating)
			{
				if (bArc && Hit.ImpactNormal.Z > 0.65f && !bBurstsOnWalls && Seconds > 0.0f)
				{
					// Landed: the server lets it roll on from there (MakeRollingPickup), so it is shown rolling on
					// instead of sitting where it touched down until that news arrives.
					const float Rolled = Seconds * (1.0f - Hit.Time);
					const FVector Roll = FVector(Velocity.X, Velocity.Y, 0.0f).GetClampedToMaxSize(1600.0f) * 0.55f;
					constexpr float RollDamping = 0.75f;
					return Hit.Location + Roll * ((1.0f - FMath::Exp(-RollDamping * Rolled)) / RollDamping);
				}
				if ((bArc && Hit.ImpactNormal.Z > 0.65f) || bBurstsOnWalls)
				{
					if (bOutBurst)
					{
						*bOutBurst = bBurstsOnWalls;
					}
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
		// Straight: carried on wall after wall (a long extrapolation of a fast ball can meet several), a thunder ball
		// speeding up at each like the server's.
		FVector Position = Start;
		FVector Moving = Velocity;
		float TimeLeft = Seconds;
		for (int32 Bounce = 0; Bounce < 4 && TimeLeft > 0.0f; ++Bounce)
		{
			const FVector SegmentEnd = Position + Moving * TimeLeft;
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByObjectType(Hit, Position, SegmentEnd, FQuat::Identity, StageOnly,
				FCollisionShape::MakeSphere(Radius), Params))
			{
				return SegmentEnd;
			}
			if (Hit.bStartPenetrating)
			{
				// Wedged in a corner: stay put rather than slip through the wall (the next update says where it went).
				return Bounce == 0 ? SegmentEnd : Position;
			}
			if (bBurstsOnWalls)
			{
				if (bOutBurst)
				{
					*bOutBurst = true;
				}
				return Hit.Location;
			}
			TimeLeft *= 1.0f - Hit.Time;
			// Just off the wall, so the next leg does not start inside it.
			Position = Hit.Location + Hit.ImpactNormal * 1.0f;
			Moving = FMath::GetReflectionVector(Moving, Hit.ImpactNormal);
			Moving.Z = 0.0f;
			if (BallType == EChaosImpactBallType::Thunder)
			{
				Moving = Moving.GetSafeNormal() * FMath::Min(static_cast<float>(Moving.Size()) * ChaosImpactBallTypes::ThunderBounceSpeedUp,
					ChaosImpactBallTypes::ThunderMaxSpeed);
			}
		}
		return Position;
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
			ClientTimeLead = 0.0f;
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
	if (ClientErrorOffset.SizeSquared() > FMath::Square(FMath::Max(ClientSnapDistance, static_cast<float>(FVector(NetState.Velocity).Size()) * ClientSnapSeconds)))
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
	if (IsHidden() && !bClientPickupClaimed && !bPredictedBurstShown)
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
	double PresentationTime = GetPresentationServerTime() + ClientTimeLead;
	bool bBurstReached = false;
	FVector Desired = PredictNetLocation(PresentationTime, &bBurstReached);
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
		if (ClientErrorOffset.SizeSquared() > FMath::Square(FMath::Max(ClientSnapDistance, static_cast<float>(FVector(NetState.Velocity).Size()) * ClientSnapSeconds)))
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
		// The adoption may have set a time lead: draw this very frame with it too (or the ball steps back for one frame).
		PresentationTime = GetPresentationServerTime() + ClientTimeLead;
		Desired = PredictNetLocation(PresentationTime, &bBurstReached);
	}
	// A special ball reaching the stage bursts there: shown at once (a flash, the ball gone) instead of the ball
	// waiting there for the server's news, which can take a whole round trip for the thrower's own ball shown ahead.
	// Its zone comes with the news. Should the next update show it flying on after all, it simply reappears.
	const bool bShowsBurst = bBurstReached && bFlightState && BallType != EChaosImpactBallType::Drive
		&& BallType != EChaosImpactBallType::Beam && BallType != EChaosImpactBallType::Thunder;
	if (bShowsBurst && !bPredictedBurstShown)
	{
		bPredictedBurstShown = true;
		SetActorHiddenInGame(true);
		MulticastContactBurst_Implementation(Desired, FRotator::ZeroRotator);
	}
	else if (!bShowsBurst && bPredictedBurstShown)
	{
		bPredictedBurstShown = false;
		if (!bClientPickupClaimed)
		{
			SetActorHiddenInGame(false);
		}
	}
	if (!bFlightState)
	{
		TryClaimLocalPickup();
		// Landed or picked up: ease back onto the arriving state instead of staying ahead of it.
		ClientTimeLead *= FMath::Exp(-8.0f * DeltaSeconds);
	}
	ClientErrorOffset *= FMath::Exp(-ActiveErrorDecayRate * DeltaSeconds);
	const FVector PreviousLocation = GetActorLocation();
	SetActorLocation(Desired + ClientErrorOffset);
	ClientLastPresentationTime = PresentationTime;
#if !UE_BUILD_SHIPPING
	// Development (-CINetTrace): a flying ball drawn faster or slower than it flies (a jump or a stall) is logged.
	static const bool bTraceBalls = FParse::Param(FCommandLine::Get(), TEXT("CINetTrace"));
	if (bTraceBalls && bFlightState && DeltaSeconds > 0.0f && bAdoptionTraced && !bPredictedBurstShown)
	{
		const float Drawn = static_cast<float>((GetActorLocation() - PreviousLocation).Size());
		const float Expected = static_cast<float>(FVector(NetState.Velocity).Size()) * DeltaSeconds;
		if (FMath::Abs(Drawn - Expected) > FMath::Max(25.0f, Expected * 0.5f))
		{
			const AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(ReplicatedThrower.Get());
			UE_LOG(LogChaosImpact, Log, TEXT("BallJerk drawn=%.0f expected=%.0f own=%d type=%s mode=%d lead=%.3f err=%.0f age=%.3f dt=%.3f"),
				Drawn, Expected, Thrower && Thrower->IsLocallyControlled() ? 1 : 0, ChaosImpactBallTypes::GetInternalName(BallType),
				NetState.Mode, ClientTimeLead, ClientErrorOffset.Size(), PresentationTime - NetState.ServerTime, DeltaSeconds);
		}
	}
	bAdoptionTraced = bFlightState;
#endif
	TryReportLocalHit(PreviousLocation, GetActorLocation());
}

void AChaosImpactBall::TryReportLocalHit(const FVector& From, const FVector& To)
{
	const bool bFlying = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
		|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
	// A nova touches nobody on its way (its blast does the hitting).
	if (bClientHitReported || bDetonated || !bFlying || !GetWorld() || BallType == EChaosImpactBallType::Nova)
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
			|| WasThrownBy(LocalCharacter)
			|| AChaosImpactGameState::AreTeammates(GetWorld(), ReplicatedThrower.Get(), LocalCharacter))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = LocalCharacter->GetCapsuleComponent();
		if (BallType == EChaosImpactBallType::Beam)
		{
			// The beam keeps going: report the hit, but never stop or move the drawn beam for it.
			FVector Struck;
			if (BeamTouches(From, To, Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(),
				Capsule->GetScaledCapsuleHalfHeight(), Struck))
			{
				bClientHitReported = true;
				LocalCharacter->ReportBallHitFromClient(this, Struck);
				UE_LOG(LogChaosImpact, Log, TEXT("Beam hit detected locally on %s"), *LocalCharacter->GetName());
				return;
			}
			continue;
		}
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
		PingSeconds = FMath::Clamp(VictimState->GetPingInMilliseconds() / 1000.0f, 0.0f, 1.0f);
	}
	// The reported point is on the ball's path (its centre): a big ball's centre is well away from whoever it touched.
	const float VictimReach = 450.0f + CollisionSphere->GetScaledSphereRadius();
	if (bDetonated)
	{
		// This copy flew through the remote victim and burst further on; their screen saw the contact first.
		AChaosImpactHazardZone* Zone = DetonationZone.Get();
		const bool bLateAccepted = Zone && !WasThrownBy(Victim)
			&& GetWorld()->GetTimeSeconds() - DetonatedAt <= PingSeconds + 0.3
			&& FVector::Dist(Victim->GetActorLocation(), HitLocation) <= VictimReach
			&& Zone->TryApplyLateHit(Victim);
		UE_LOG(LogChaosImpact, Log, TEXT("Reported hit on a detonated ball %s: victim=%s"),
			bLateAccepted ? TEXT("accepted") : TEXT("rejected"), *Victim->GetName());
		return bLateAccepted;
	}
	if (BallType == EChaosImpactBallType::Beam)
	{
		// Each victim once; the beam flies on regardless.
		const bool bBeamAccepted = !WasThrownBy(Victim) && !BeamStruck.Contains(Victim)
			&& FVector::Dist(Victim->GetActorLocation(), HitLocation) <= 450.0f;
		UE_LOG(LogChaosImpact, Log, TEXT("Reported beam hit %s: victim=%s"), bBeamAccepted ? TEXT("accepted") : TEXT("rejected"),
			*Victim->GetName());
		if (bBeamAccepted)
		{
			BeamStruck.Add(Victim);
			ResolveDamagingHit(Victim, HitLocation, (Victim->GetActorLocation() - HitLocation).GetSafeNormal2D());
		}
		return bBeamAccepted;
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
	const float BallTolerance = FMath::Min(250.0f + BallSpeed * (PingSeconds + 0.1f), 2600.0f);
	const float BallError = FVector::Dist(GetActorLocation(), HitLocation);
	const float VictimError = FVector::Dist(Victim->GetActorLocation(), HitLocation);
	const bool bAccepted = BallError <= BallTolerance && VictimError <= VictimReach;
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
	// so even non-lethal hits have immediate visual feedback. Special balls burst instead; a beam flashes and flies on.
	if (!IsSpecialBall())
	{
		MulticastContactBurst(ImpactPoint, ImpactNormal.Rotation());
	}
	else if (BallType == EChaosImpactBallType::Beam)
	{
		MulticastBeamStrike(ImpactPoint);
	}
	if (BallType == EChaosImpactBallType::Nova)
	{
		// No hit of its own: it bursts where it is, and the blast hits everyone in reach (this victim too) alike.
		Detonate(GetActorLocation(), nullptr);
		return;
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
	if (BallType == EChaosImpactBallType::Beam)
	{
		return;
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
		if (ClientErrorOffset.SizeSquared() > FMath::Square(FMath::Max(ClientSnapDistance, static_cast<float>(FVector(NetState.Velocity).Size()) * ClientSnapSeconds)))
		{
			ClientErrorOffset = FVector::ZeroVector;
		}
		// Blend the small remaining difference away slowly so the thrower never sees a jump.
		constexpr float AdoptedErrorDecayRate = 4.0f;
		const bool bStillFlying = NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Straight)
			|| NetState.Mode == static_cast<uint8>(EChaosImpactBallNetMode::Arc);
		ActiveErrorDecayRate = bStillFlying ? AdoptedErrorDecayRate : ClientErrorDecayRate;
		// The preview has been flying since the release on this screen, so it is usually ahead along the
		// flight by the network delay. Blending that away made the thrower's own ball visibly slow down for a
		// moment. Keep it as a small time lead instead: this screen shows its own ball slightly further along
		// its (server) path, at full speed. Only the sideways part is blended away.
		const FVector FlightVelocity = NetState.Velocity;
		const double FlightSpeed = FlightVelocity.Size();
		if (bStillFlying && FlightSpeed > 100.0)
		{
			const FVector FlightDirection = FlightVelocity / FlightSpeed;
			const double Along = FVector::DotProduct(ClientErrorOffset, FlightDirection);
			// Ahead (the usual case) or behind (the server let go of it sooner): either way a steady time offset, so
			// the ball carries on at full speed from where this screen shows it instead of lurching to catch up.
			const double MaxLead = BallType == EChaosImpactBallType::Thunder ? ThunderMaxAheadSeconds : MaxOwnThrowTimeLeadSeconds;
			ClientTimeLead = static_cast<float>(FMath::Clamp(Along / FlightSpeed, -0.35, MaxLead));
			ClientErrorOffset -= FlightDirection * (ClientTimeLead * FlightSpeed);
		}
		InheritContactPresentation(*Predicted);
		// A drive ball being steered: the thrower steers (and the camera follows) this one from now on.
		Thrower->OnDriveBallAdopted(Predicted, this);
		Predicted->Destroy();
		UE_LOG(LogChaosImpact, Log, TEXT("Predicted throw adopted (offset %.0f, along flight %.0f) lead %.3fs"),
			ClientErrorOffset.Size(), FVector::DotProduct(ClientErrorOffset, FVector(NetState.Velocity).GetSafeNormal()),
			ClientTimeLead);
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
	// The server's own lockout (just hit, carried by a tornado): a claim before it ends would only be refused,
	// making the ball vanish and come back. By the time this claim arrives the server is a little further on.
	if (GetPresentationServerTime() < static_cast<double>(PickupAvailableAtSeconds) - 0.1)
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
			// Someone else right by it (as drawn here, so really perhaps already on it): the server may give it to them.
			bool bContested = false;
			for (TActorIterator<AChaosImpactCharacter> Other(GetWorld()); Other && !bContested; ++Other)
			{
				bContested = *Other != LocalCharacter && !Other->IsEliminated()
					&& FVector::Dist2D(Other->GetPresentationLocation(), GetActorLocation()) < 350.0f;
			}
			bClientPickupClaimed = true;
			SetActorHiddenInGame(true);
			LocalCharacter->ClaimPickupFromClient(this, bContested);
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
	constexpr float MaxWaitSeconds = 1.3f;
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
					LocalRoundTrip = FMath::Clamp(LocalState->GetPingInMilliseconds() / 1000.0f, 0.0f, 1.0f);
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
	if (!bContactSoundPlayed)
	{
		bContactSoundPlayed = true;
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BallHit, Location);
	}
	if (UNiagaraSystem* ContactBurst = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::Damage))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ContactBurst,
			Location, Rotation, FVector(ContactEffectScale));
	}
}

void AChaosImpactBall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSoundPresentation(DeltaSeconds);
	if (!HasAuthority())
	{
		TickClientPresentation(DeltaSeconds);
		UpdateContactPresentation(DeltaSeconds);
		UpdateBallTypePresentation(DeltaSeconds);
		UpdateExpiryPresentation(DeltaSeconds);
		TraceLocalThrow();
		return;
	}
	if (bDetonated)
	{
		return;
	}
	if (bIsPickup && LandedPickupExpiresAt > 0.0 && !bPickupConsumed && GetServerNow() >= LandedPickupExpiresAt)
	{
		// Nobody collected it in time. A pickup claim still in flight is refused as "ball gone".
		Destroy();
		return;
	}
	if (!bIsPickup)
	{
		if (ProjectileMovement->IsActive() && !GetAttachParentActor())
		{
			UpdateDashPassThrough();
			if (BallType == EChaosImpactBallType::Normal)
			{
				// The thrower's preview bends the same way, from what its own screen shows.
				UpdateHoming(DeltaSeconds);
			}
			else if (BallType == EChaosImpactBallType::Fire && !bCosmeticPrediction)
			{
				UpdateFireTrail();
			}
			else if (BallType == EChaosImpactBallType::Beam)
			{
				UpdateBeamHits();
			}
			else if (BallType == EChaosImpactBallType::Drive)
			{
				UpdateDriveFlight(DeltaSeconds);
				if (bDetonated || IsHidden())
				{
					return;
				}
			}
		}
		FlightSeconds += DeltaSeconds;
		const float Flight = BallType == EChaosImpactBallType::Thunder ? ChaosImpactBallTypes::ThunderFlightSeconds
			: BallType == EChaosImpactBallType::Beam ? ChaosImpactBallTypes::BeamRange / ChaosImpactBallTypes::BeamSpeed
			// Its steering time decides its end; this only catches one never steered.
			: BallType == EChaosImpactBallType::Drive ? ChaosImpactBallTypes::DriveControlSeconds + 1.0f
			: LifeSeconds;
		if (FlightSeconds >= Flight && !GetAttachParentActor())
		{
			if (IsSpecialBall() && !bCosmeticPrediction)
			{
				Detonate(GetActorLocation(), nullptr);
				return;
			}
			if (BallType == EChaosImpactBallType::Beam)
			{
				// The thrower's preview of a beam fades out with it.
				EndCosmeticFlight();
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
	UpdateExpiryPresentation(DeltaSeconds);
	TraceLocalThrow();
}

void AChaosImpactBall::UpdateExpiryPresentation(const float DeltaSeconds)
{
	if (!BallMesh)
	{
		return;
	}
	bool bShow = true;
	float Scale = 1.0f;
	const double Remaining = bIsPickup && LandedPickupExpiresAt > 0.0
		? LandedPickupExpiresAt - GetServerNow() : TNumericLimits<double>::Max();
	if (LandedPickupBlinkSeconds > 0.0f && Remaining < LandedPickupBlinkSeconds)
	{
		const float Urgency = 1.0f - FMath::Clamp(static_cast<float>(Remaining) / LandedPickupBlinkSeconds, 0.0f, 1.0f);
		// About 3 blinks a second at first, 12 at the end.
		ExpiryBlinkPhase += DeltaSeconds * FMath::Lerp(3.0f, 12.0f, Urgency);
		bShow = FMath::Frac(ExpiryBlinkPhase) < 0.62f;
		// Shrinks away over the final moment instead of popping out of existence.
		Scale = FMath::Clamp(static_cast<float>(Remaining) / 0.3f, 0.05f, 1.0f);
	}
	else
	{
		ExpiryBlinkPhase = 0.0f;
	}
	if (bShow != bExpiryShown)
	{
		// Only the ball itself: attached effects keep their own visibility rules.
		BallMesh->SetVisibility(bShow, false);
		bExpiryShown = bShow;
	}
	if (!FMath::IsNearlyEqual(Scale, ExpiryScale))
	{
		BallMesh->SetRelativeScale3D(BallMeshBaseScale * Scale);
		ExpiryScale = Scale;
	}
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
	if (BallType == EChaosImpactBallType::Wind)
	{
		// A wind ball never flies: leaving the hand, it becomes a tornado (the server's; a throw preview just goes).
		if (!ThrowingPawn.IsValid())
		{
			ThrowingPawn = GetInstigator();
		}
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorHiddenInGame(true);
		if (bCosmeticPrediction || !HasAuthority() || !GetWorld())
		{
			SetActorTickEnabled(false);
			SetLifeSpan(0.25f);
			return;
		}
		bDetonated = true;
		DetonatedAt = GetWorld()->GetTimeSeconds();
		FlightEndedAt = DetonatedAt;
		SetActorTickEnabled(false);
		AChaosImpactTornado::Release(GetWorld(), GetActorLocation(), Direction, ThrowingPawn.Get());
		SetLifeSpan(1.0f);
		ForceNetUpdate();
		return;
	}
	const FVector HorizontalDirection(Direction.X, Direction.Y, 0.0f);
	// Thunder and beam balls always fly straight at their own speed, however the throw was charged or aimed.
	const bool bThunder = BallType == EChaosImpactBallType::Thunder;
	const bool bBeam = BallType == EChaosImpactBallType::Beam;
	const EChaosImpactBallFlightMode UsedFlightMode = bThunder || bBeam ? EChaosImpactBallFlightMode::Straight : FlightMode;
	const float UsedSpeed = bThunder ? ChaosImpactBallTypes::ThunderSpeed : bBeam ? ChaosImpactBallTypes::BeamSpeed : Speed;
	const bool bArc = UsedFlightMode == EChaosImpactBallFlightMode::Arc;
	ActiveFlightMode = UsedFlightMode;
	FlightSeconds = 0.0f;
	LastFireTrailLocation = GetActorLocation();
	FireTrailCount = 0;
	FireTrailLeader.Reset();
	if (!ThrowingPawn.IsValid())
	{
		ThrowingPawn = GetInstigator();
	}
	ReplicatedThrower = ThrowingPawn.Get();
	ApplyThrowerIgnores();
	bIsPickup = false;
	bIsRolling = false;
	LandedPickupExpiresAt = 0.0;
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	if (bCosmeticPrediction)
	{
		// The preview never touches players; the victim's own screen judges real hits.
		CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (BallType == EChaosImpactBallType::Nova)
	{
		// A nova flies over everyone and everything loose until it meets the stage itself, exactly where its landing
		// was shown; its blast is what hits them.
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	}
	SetActorTickEnabled(true);
	ProjectileMovement->Activate(true);
	ProjectileMovement->ProjectileGravityScale = bArc ? GetArcGravityScale() : 0.0f;
	ProjectileMovement->bConstrainToPlane = !bArc;
	ProjectileMovement->SetPlaneConstraintEnabled(!bArc);
	ProjectileMovement->Bounciness = bArc ? 0.72f : Bounciness;
	ProjectileMovement->Friction = bArc ? 0.12f : 0.0f;
	ProjectileMovement->Velocity = HorizontalDirection.GetSafeNormal() * UsedSpeed
		+ (bArc ? FVector::UpVector * ArcUpwardSpeed : FVector::ZeroVector);
	if (bBeam)
	{
		// Light passes through walls and people alike; who it strikes is worked out along its path each frame.
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BeamLastLocation = GetActorLocation();
		BeamStruck.Reset();
	}
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
	LandedPickupExpiresAt = 0.0;
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
	LandedPickupExpiresAt = GetServerNow() + LandedPickupLifetimeSeconds;
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

bool AChaosImpactBall::IsFlyingOnServer() const
{
	return HasAuthority() && !bCosmeticPrediction && !bIsPickup && !bDetonated && !GetAttachParentActor()
		&& ProjectileMovement && ProjectileMovement->IsActive();
}

bool AChaosImpactBall::DeflectByWind(const FVector& NewVelocity, APawn* NewThrower)
{
	if (!IsFlyingOnServer())
	{
		return false;
	}
	AdoptThrower(NewThrower);
	FVector Velocity = NewVelocity;
	if (ActiveFlightMode != EChaosImpactBallFlightMode::Arc)
	{
		// A level flight stays level.
		Velocity.Z = 0.0f;
	}
	ProjectileMovement->Velocity = Velocity;
	bHasReflected = true;
	++ReflectionCount;
	// It starts a new flight from the tornado rather than landing moments later.
	FlightSeconds = 0.0f;
	UpdateNetState();
	ForceNetUpdate();
	return true;
}

bool AChaosImpactBall::CatchInWind()
{
	if (!HasAuthority() || bCosmeticPrediction || !bIsPickup || bPickupConsumed || bCarriedByWind || bDetonated
		|| !GetWorld())
	{
		return false;
	}
	bCarriedByWind = true;
	// Whatever it was (a spawner's or a summoned ball), it is an ordinary loose ball from now on.
	bLeftSpawnPoint = true;
	// Carried like a hovering ball (its moving base replicates), out of everyone's reach.
	if (bIsRolling)
	{
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		bIsRolling = false;
	}
	LandedPickupExpiresAt = 0.0;
	PickupAvailableAtSeconds = TNumericLimits<float>::Max();
	PickupBaseLocation = GetActorLocation();
	UpdateNetState();
	return true;
}

void AChaosImpactBall::CarryInWind(const FVector& Location)
{
	if (!bCarriedByWind)
	{
		return;
	}
	PickupBaseLocation = Location;
	SetActorLocation(Location);
}

void AChaosImpactBall::ReleaseFromWind(const FVector& Ground, const FVector& FlingVelocity, APawn* NewThrower)
{
	if (!bCarriedByWind)
	{
		return;
	}
	bCarriedByWind = false;
	const FVector Outward = FlingVelocity.GetSafeNormal2D();
	if (NewThrower && BallType != EChaosImpactBallType::Wind && !Outward.IsNearlyZero())
	{
		// Flung out as the tornado owner's throw: a short arc that hurts whoever it meets, then lands as usual.
		SetActorLocation(Ground + FVector(0.0f, 0.0f, 90.0f));
		AdoptThrower(NewThrower);
		Launch(Outward, WindFlingSpeed, EChaosImpactBallFlightMode::Arc, WindFlingUpSpeed);
		ForceNetUpdate();
		return;
	}
	SetActorLocation(Ground + FVector(0.0f, 0.0f, CollisionSphere->GetScaledSphereRadius() + 2.0f));
	MakeRollingPickup(FlingVelocity);
	ForceNetUpdate();
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

	if (const AChaosImpactCharacter* Dasher = Cast<AChaosImpactCharacter>(OtherActor); Dasher && Dasher->IsDashing())
	{
		// A dash is untouchable: meeting a ball mid-dash is no hit at all (no flash, and the ball flies on).
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
	if (AChaosImpactGameState::AreTeammates(GetWorld(), ThrowingPawn.Get(), OtherActor))
	{
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
	if (AChaosImpactBall* OtherBall = Cast<AChaosImpactBall>(ImpactResult.GetActor()))
	{
		// No ball stops or turns a drive ball: it flies straight on through and knocks the other away.
		if (BallType == EChaosImpactBallType::Drive)
		{
			CollisionSphere->IgnoreActorWhenMoving(OtherBall, true);
			ProjectileMovement->Velocity = ImpactVelocity;
			if (!bCosmeticPrediction)
			{
				OtherBall->KnockAwayByDrive(this);
			}
			return;
		}
		if (!bCosmeticPrediction && OtherBall->BallType == EChaosImpactBallType::Drive && !OtherBall->bIsPickup
			&& !OtherBall->bDetonated)
		{
			KnockAwayByDrive(OtherBall);
			return;
		}
	}
	// A thunder ball rebounds like a normal ball until it meets someone or its time runs out.
	if (IsSpecialBall() && BallType != EChaosImpactBallType::Thunder)
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
	if (BallType == EChaosImpactBallType::Thunder)
	{
		// Every wall it rebounds off makes a thunder ball faster (the velocity is already the rebound here).
		const FVector Rebound = ProjectileMovement->Velocity;
		ProjectileMovement->Velocity = Rebound.GetSafeNormal()
			* FMath::Min(static_cast<float>(Rebound.Size()) * ChaosImpactBallTypes::ThunderBounceSpeedUp, ChaosImpactBallTypes::ThunderMaxSpeed);
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

void AChaosImpactBall::ApplyThrowerIgnores()
{
	if (ThrowingPawn.IsValid())
	{
		CollisionSphere->IgnoreActorWhenMoving(ThrowingPawn.Get(), true);
	}
	if (ThrowingPawn.IsValid() && GetWorld())
	{
		// Team battle: balls fly straight through the thrower's teammates.
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (AChaosImpactGameState::AreTeammates(GetWorld(), ThrowingPawn.Get(), *It))
			{
				CollisionSphere->IgnoreActorWhenMoving(*It, true);
			}
		}
	}
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
}

void AChaosImpactBall::UpdateDashPassThrough()
{
	UWorld* World = GetWorld();
	if (!World || !CollisionSphere)
	{
		return;
	}
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		// The thrower, their teammates and (online) remote players are passed through for good (ApplyThrowerIgnores).
		if (Character == ThrowingPawn.Get() || AChaosImpactGameState::AreTeammates(World, ThrowingPawn.Get(), Character)
			|| (HasAuthority() && GetNetMode() != NM_Standalone && Character->IsRemotePlayerOnServer()))
		{
			continue;
		}
		// Both ways: the ball flies on through, and the dash goes on through the ball.
		const bool bDashing = Character->IsDashing();
		// A dash right through a ball that would have hit counts as a dodge (once per ball, for the results).
		if (bDashing && HasAuthority() && !bCosmeticPrediction && !DodgedBy.Contains(Character)
			&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) < FMath::Square(120.0f))
		{
			DodgedBy.Add(Character);
			if (AChaosImpactGameMode* Mode = World->GetAuthGameMode<AChaosImpactGameMode>())
			{
				Mode->RecordDodge(Character);
			}
		}
		CollisionSphere->IgnoreActorWhenMoving(Character, bDashing);
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Capsule->IgnoreActorWhenMoving(this, bDashing);
		}
	}
}

void AChaosImpactBall::AdoptThrower(APawn* NewThrower)
{
	if (!NewThrower || !HasAuthority() || bCosmeticPrediction)
	{
		return;
	}
	ThrowingPawn = NewThrower;
	ReplicatedThrower = NewThrower;
	SetOwner(NewThrower);
	SetInstigator(NewThrower);
	// Its first thrower is fair game now, and the new one's teammates are not.
	CollisionSphere->ClearMoveIgnoreActors();
	ApplyThrowerIgnores();
}

void AChaosImpactBall::UpdateHoming(const float DeltaSeconds)
{
	const FVector Velocity = ProjectileMovement->Velocity;
	const FVector Flat(Velocity.X, Velocity.Y, 0.0f);
	const float Speed = static_cast<float>(Flat.Size());
	UWorld* World = GetWorld();
	if (!World || Speed < 300.0f)
	{
		return;
	}
	// The nearest opponent ahead: not the thrower, a teammate or someone knocked out.
	const APawn* Thrower = ThrowingPawn.Get();
	const FVector Heading = Flat / Speed;
	const FVector Location = GetActorLocation();
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(ChaosImpactBallTypes::NormalHomingConeDegrees));
	const AChaosImpactCharacter* Target = nullptr;
	float TargetDistance = ChaosImpactBallTypes::NormalHomingReach;
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		if (*It == Thrower || It->IsEliminated() || It->IsHidden() || AChaosImpactGameState::AreTeammates(World, Thrower, *It))
		{
			continue;
		}
		const FVector Offset = It->GetActorLocation() - Location;
		const float Distance = static_cast<float>(Offset.Size2D());
		if (Distance < 1.0f || Distance >= TargetDistance
			|| FVector::DotProduct(FVector(Offset.X, Offset.Y, 0.0f) / Distance, Heading) < MinimumDot)
		{
			continue;
		}
		Target = *It;
		TargetDistance = Distance;
	}
	if (!Target)
	{
		return;
	}
	// Turn toward it at a limited rate, keeping the speed and the arc's rise or fall.
	const FVector Toward = (Target->GetActorLocation() - Location).GetSafeNormal2D();
	const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Heading, Toward)), -1.0f, 1.0f));
	const float Step = FMath::Min(Angle, FMath::DegreesToRadians(ChaosImpactBallTypes::NormalHomingDegreesPerSecond) * DeltaSeconds);
	const float Side = FVector::CrossProduct(Heading, Toward).Z >= 0.0 ? 1.0f : -1.0f;
	const FVector Turned = Heading.RotateAngleAxisRad(Step * Side, FVector::UpVector);
	ProjectileMovement->Velocity = Turned * Speed + FVector(0.0f, 0.0f, Velocity.Z);
}

void AChaosImpactBall::UpdateFireTrail()
{
	if (FireTrailCount >= ChaosImpactBallTypes::FireTrailMaxPatches || !GetWorld())
	{
		return;
	}
	const FVector Location = GetActorLocation();
	if (FVector::Dist2D(Location, LastFireTrailLocation) < ChaosImpactBallTypes::FireTrailSpacing)
	{
		return;
	}
	LastFireTrailLocation = Location;
	if (AChaosImpactHazardZone* Fire = AChaosImpactHazardZone::SpawnFireTrail(GetWorld(), Location, ThrowingPawn.Get(),
		FireTrailLeader.Get()))
	{
		++FireTrailCount;
		if (!FireTrailLeader.IsValid())
		{
			FireTrailLeader = Fire;
		}
	}
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
		StopFlightSound();
	}
}

void AChaosImpactBall::UpdateSoundPresentation(const float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer || DeltaSeconds <= 0.0f || !BallMesh)
	{
		return;
	}
	const bool bShown = !IsHidden() && !bDetonated && BallMesh->IsVisible() && !GetAttachParentActor();
	const FVector At = BallMesh->GetComponentLocation();
	const FVector Velocity = bSoundHasLast ? (At - SoundLastLocation) / DeltaSeconds : FVector::ZeroVector;
	// A bounce: going fast, then suddenly another way (not a correction's jump: the new speed stays believable).
	const double LastSpeed = SoundLastVelocity.Size();
	const double Speed = Velocity.Size();
	if (bShown && bSoundHasLast && LastSpeed > 400.0 && Speed > 120.0 && Speed < LastSpeed * 2.5
		&& FVector::DotProduct(SoundLastVelocity / LastSpeed, Velocity / Speed) < 0.6)
	{
		const float Loudness = FMath::GetMappedRangeValueClamped(FVector2f(400.0f, 2500.0f), FVector2f(0.45f, 1.0f),
			static_cast<float>(LastSpeed));
		ChaosImpactSfx::PlayAt(this, BallType == EChaosImpactBallType::Thunder ? EChaosImpactSfx::ThunderBounce
			: EChaosImpactSfx::BallBounce, At, Loudness);
	}
	SoundLastLocation = At;
	SoundLastVelocity = Velocity;
	bSoundHasLast = bShown;
	if (bShown && !bSoundSpawnChecked)
	{
		// Not the balls already there as a level starts (or as this screen joins).
		bSoundSpawnChecked = true;
		if (bIsPickup && !ThrowingPawn.IsValid() && GetGameTimeSinceCreation() < 0.5f && GetWorld()->GetTimeSeconds() > 3.0)
		{
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::StageBallSpawn, At);
		}
	}

	// The hum of a ball in flight.
	const EChaosImpactSfx Hum = BallType == EChaosImpactBallType::Fire ? EChaosImpactSfx::FireLoop
		: BallType == EChaosImpactBallType::Thunder ? EChaosImpactSfx::ThunderLoop
		: BallType == EChaosImpactBallType::Drive ? EChaosImpactSfx::DriveLoop : EChaosImpactSfx::Count;
	const bool bFlying = bShown && !bIsPickup && Hum != EChaosImpactSfx::Count;
	if (bFlying && !FlightSound)
	{
		FlightSound = ChaosImpactSfx::PlayAttached(Hum, BallMesh);
	}
	else if (!bFlying && FlightSound)
	{
		StopFlightSound();
	}
}

void AChaosImpactBall::StopFlightSound()
{
	ChaosImpactSfx::Stop(FlightSound, 0.12f);
	FlightSound = nullptr;
}

void AChaosImpactBall::Detonate(const FVector& Location, AActor* DirectVictim)
{
	if (!HasAuthority() || bCosmeticPrediction || bDetonated || !IsSpecialBall() || !GetWorld())
	{
		return;
	}
	bDetonated = true;
	StopFlightSound();
	DetonatedAt = GetWorld()->GetTimeSeconds();
	FlightEndedAt = DetonatedAt;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);
	if (BallType == EChaosImpactBallType::Simae)
	{
		AChaosImpactSimaeBird::ReleaseFlock(GetWorld(), Location, ThrowingPawn.Get(), DirectVictim);
		DetonationZone = nullptr;
	}
	else if (BallType == EChaosImpactBallType::Drive)
	{
		// No blast of its own: the hit (if any) has been dealt; this is only its show.
		DriveDeadline = 0.0;
		MulticastDriveBurst(Location, DirectVictim != nullptr);
		DetonationZone = nullptr;
	}
	else
	{
		DetonationZone = AChaosImpactHazardZone::Detonate(GetWorld(), BallType, Location, ThrowingPawn.Get(), DirectVictim,
			BallType == EChaosImpactBallType::Snow || BallType == EChaosImpactBallType::Nova ? SnowScale : 1.0f);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("%s ball detonated at %s (direct victim %s)"),
		ChaosImpactBallTypes::GetInternalName(BallType), *Location.ToCompactString(), *GetNameSafe(DirectVictim));
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
	const auto AttachAura = [this](const TCHAR* SystemPath, const float Scale) -> UNiagaraComponent*
	{
		UNiagaraSystem* System = LoadEffect(SystemPath);
		UNiagaraComponent* Aura = System ? UNiagaraFunctionLibrary::SpawnSystemAttached(System, BallMesh, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false) : nullptr;
		if (Aura)
		{
			Aura->SetUsingAbsoluteScale(true);
			Aura->SetWorldScale3D(FVector(Scale));
		}
		return Aura;
	};
	// Follows the ball but keeps its own world scale and rotation.
	const auto AttachShell = [this](UStaticMesh* Shape, UMaterialInterface* Look) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Shell = NewObject<UStaticMeshComponent>(this);
		Shell->SetStaticMesh(Shape);
		Shell->SetMaterial(0, Look);
		Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shell->SetCastShadow(false);
		Shell->SetupAttachment(BallMesh);
		Shell->SetUsingAbsoluteScale(true);
		Shell->SetUsingAbsoluteRotation(true);
		Shell->RegisterComponent();
		return Shell;
	};
	FLinearColor LightColor(0.5f, 0.82f, 1.0f);
	float LightIntensity = 1800.0f;
	float LightRadius = 260.0f;

	if (BallType == EChaosImpactBallType::Fire)
	{
		// A glowing core with real flames licking off it; they stream behind the ball in flight.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(1.0f, 0.24f, 0.01f), 1.1f));
		TypeAuraEffect = AttachAura(Effects::Fire, 1.0f);
		if (TypeAuraEffect)
		{
			SetEffectFloat(TypeAuraEffect, TEXT("Flame Scale"), 0.9f);
			SetEffectFloat(TypeAuraEffect, TEXT("Smoke Spawn Scale"), 0.15f);
			SetEffectFloat(TypeAuraEffect, TEXT("Base Light Intentsity"), 0.0f);
		}
		LightColor = FLinearColor(1.0f, 0.45f, 0.1f);
		LightIntensity = 3500.0f;
		LightRadius = 360.0f;
	}
	else if (BallType == EChaosImpactBallType::Thunder)
	{
		// A white-hot core in a crackling shell, with arcs of lightning jumping off it.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(1.0f, 0.78f, 0.12f), 1.5f));
		TypeAuraEffect = AttachAura(Effects::Electricity, 0.45f);
		TypeGlowMaterial = MakeAdditive(this, FLinearColor(1.0f, 0.8f, 0.15f), 0.9f, 1.0f);
		TypeGlow = AttachShell(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), TypeGlowMaterial);
		TypeArcMaterial = MakeAdditive(this, FLinearColor(1.0f, 0.88f, 0.4f), 3.0f);
		TypeArcs = ChaosImpactLightning::CreateComponent(this, BallMesh, TypeArcMaterial);
		if (TypeArcs)
		{
			TypeArcs->SetUsingAbsoluteScale(true);
			TypeArcs->SetUsingAbsoluteRotation(true);
		}
		LightColor = FLinearColor(1.0f, 0.9f, 0.45f);
		LightIntensity = 5200.0f;
		LightRadius = 460.0f;
	}
	else if (BallType == EChaosImpactBallType::Wind)
	{
		// A green core wrapped in a swirling shell, with two rings whirling flat around it.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(0.2f, 0.9f, 0.35f), 1.0f));
		TypeGlowMaterial = MakeAdditive(this, FLinearColor(0.45f, 1.0f, 0.5f), 1.2f, 1.0f);
		TypeGlow = AttachShell(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), TypeGlowMaterial);
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		TypeRings.Add(AttachShell(Cylinder, MakeAdditive(this, FLinearColor(0.8f, 1.0f, 0.75f), 2.0f, 1.0f)));
		TypeRings.Add(AttachShell(Cylinder, MakeAdditive(this, FLinearColor(0.3f, 0.95f, 0.45f), 1.6f, 1.0f)));
		LightColor = FLinearColor(0.45f, 1.0f, 0.5f);
		LightIntensity = 2400.0f;
		LightRadius = 340.0f;
	}
	else if (BallType == EChaosImpactBallType::Black)
	{
		// A lightless core inside a violet event horizon, circled by two tilted, spinning rings.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(0.012f, 0.0f, 0.025f), 1.0f));
		TypeAuraEffect = AttachAura(Effects::DarkAura, 0.5f);
		TypeGlowMaterial = MakeAdditive(this, FLinearColor(0.62f, 0.2f, 1.0f), 1.5f, 1.0f);
		TypeGlow = AttachShell(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), TypeGlowMaterial);
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		TypeRings.Add(AttachShell(Cylinder, MakeAdditive(this, FLinearColor(0.9f, 0.38f, 1.0f), 2.2f, 1.0f)));
		TypeRings.Add(AttachShell(Cylinder, MakeAdditive(this, FLinearColor(0.45f, 0.28f, 1.0f), 1.8f, 1.0f)));
		LightColor = FLinearColor(0.6f, 0.25f, 1.0f);
		LightIntensity = 2600.0f;
		LightRadius = 340.0f;
	}
	else if (BallType == EChaosImpactBallType::Smoke)
	{
		// A soot-dark core leaking smoke, in a dim haze with a ring of it swirling round.
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(0.09f, 0.085f, 0.12f), 1.0f));
		TypeAuraEffect = AttachAura(Effects::LastHitSmoke, 0.6f);
		if (TypeAuraEffect)
		{
			TypeAuraEffect->SetAllowScalability(false);
			SetEffectColor(TypeAuraEffect, TEXT("Smoke Color"), FLinearColor(0.42f, 0.4f, 0.5f));
			SetEffectSize(TypeAuraEffect, TEXT("Sprite Size"), 55.0f);
		}
		TypeGlowMaterial = MakeAdditive(this, FLinearColor(0.55f, 0.5f, 0.72f), 0.8f, 1.0f);
		TypeGlow = AttachShell(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), TypeGlowMaterial);
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		TypeRings.Add(AttachShell(Cylinder, MakeAdditive(this, FLinearColor(0.72f, 0.68f, 0.88f), 0.9f, 1.0f)));
		LightColor = FLinearColor(0.6f, 0.55f, 0.85f);
		LightIntensity = 900.0f;
		LightRadius = 260.0f;
	}
	else if (BallType == EChaosImpactBallType::Beam)
	{
		// A white-hot pink core in a halo of its light, throwing off sparks; in flight a shaft of light trails it.
		const FLinearColor Pink = GetColor(EChaosImpactBallType::Beam);
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(1.0f, 0.72f, 0.96f), 6.0f));
		TypeGlowMaterial = MakeAdditive(this, Pink, 2.0f, 1.0f);
		TypeGlow = AttachShell(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")), TypeGlowMaterial);
		TypeAuraEffect = AttachAura(Effects::WindSparks, 0.5f);
		if (TypeAuraEffect)
		{
			SetEffectFloat(TypeAuraEffect, TEXT("Spark Spawn Rate"), 60.0f);
		}
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		const auto MakeShaft = [this, Cylinder](UMaterialInstanceDynamic* Look) -> UStaticMeshComponent*
		{
			// Placed in the world each frame, from where it was thrown to where it is.
			UStaticMeshComponent* Shaft = NewObject<UStaticMeshComponent>(this);
			Shaft->SetStaticMesh(Cylinder);
			Shaft->SetMaterial(0, Look);
			Shaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Shaft->SetGenerateOverlapEvents(false);
			Shaft->SetCastShadow(false);
			Shaft->SetVisibility(false);
			Shaft->RegisterComponent();
			return Shaft;
		};
		BeamCoreMaterial = MakeAdditive(this, FLinearColor(1.0f, 0.82f, 0.97f), 0.0f);
		BeamGlowMaterial = MakeAdditive(this, Pink, 0.0f, 1.0f);
		BeamCore = MakeShaft(BeamCoreMaterial);
		BeamGlow = MakeShaft(BeamGlowMaterial);
		LightColor = Pink;
		LightIntensity = 6000.0f;
		LightRadius = 520.0f;
	}
	else if (BallType == EChaosImpactBallType::Nova)
	{
		// The ball itself is only a faint glow; the layers of light round a white-hot core are the nova.
		BallMesh->SetMaterial(0, MakeAdditive(this, FLinearColor(0.25f, 0.6f, 1.0f), 0.12f));
		BuildNovaLook(this, BallMesh, NovaLook);
		UpdateNovaLook(NovaLook, 24.0f * SnowScale, 0.0f);
		LightIntensity = 0.0f;
	}
	else if (BallType == EChaosImpactBallType::Simae)
	{
		// Its imported round face replaces the engine sphere. Both material slots share the character material and texture.
		if (UStaticMesh* SimaeMesh = LoadObject<UStaticMesh>(nullptr, SimaeAssets::BallMesh))
		{
			BallMesh->SetStaticMesh(SimaeMesh);
		}
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, SimaeAssets::TexturedMaterial);
		UTexture* Texture = LoadObject<UTexture>(nullptr, SimaeAssets::BallTexture);
		if (UMaterialInstanceDynamic* Look = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr)
		{
			Look->SetTextureParameterValue(TEXT("BodyTexture"), Texture);
			Look->SetVectorParameterValue(TEXT("BodyTint"), FLinearColor::White);
			BallMesh->SetMaterial(0, Look);
			BallMesh->SetMaterial(1, Look);
		}
		// Its own light and company (three little birds, ripples, sparkles): see BuildSimaeBallLook.
		BuildSimaeBallLook(this, BallMesh, SimaeLook);
		LightIntensity = 0.0f;
	}
	else if (BallType == EChaosImpactBallType::Drive)
	{
		// A golden ball of energy: its core, glow, rings and trail are the drive look (ChaosImpactDriveBall.cpp).
		BallMesh->SetMaterial(0, MakeEmissive(this, FLinearColor(1.0f, 0.58f, 0.1f), 2.4f));
		ChaosImpactDrive::BuildLook(this, BallMesh, DriveLook);
		LightIntensity = 0.0f;
	}
	else if (BallType == EChaosImpactBallType::Snow)
	{
		// Packed snow with clumps stuck on it: no glow, it is just snow.
		UMaterialInstanceDynamic* Snow = MakeSnow(this);
		BallMesh->SetMaterial(0, Snow);
		AttachSnowLumps(this, BallMesh, Snow, static_cast<int32>(GetUniqueID()));
		LightIntensity = 0.0f;
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

	if (LightIntensity > 0.0f)
	{
		TypeLight = NewObject<UPointLightComponent>(this);
		TypeLight->SetupAttachment(BallMesh);
		TypeLight->SetLightColor(LightColor);
		TypeLight->SetIntensity(LightIntensity);
		TypeLight->SetAttenuationRadius(LightRadius);
		TypeLight->SetCastShadows(false);
		TypeLight->RegisterComponent();
	}
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
	if (NovaLook.IsBuilt())
	{
		ChaosImpactBallTypes::UpdateNovaLook(NovaLook, 24.0f * SnowScale, T);
	}
	if (SimaeLook.IsBuilt())
	{
		ChaosImpactBallTypes::UpdateSimaeBallLook(this, SimaeLook, T, DeltaSeconds, bIsPickup, bFlying,
			!IsHidden() && !bDetonated && !GetAttachParentActor());
		if (bIsPickup)
		{
			// Waiting, it looks about: turning its face this way and that, tilting its head.
			BallMesh->SetRelativeRotation(FRotator(5.0f * FMath::Sin(T * 2.3f), 38.0f * FMath::Sin(T * 0.85f)
				+ 14.0f * FMath::Sin(T * 2.1f), 7.0f * FMath::Sin(T * 1.6f)));
		}
		else if (bFlying && GetVelocity().SizeSquared() > 2500.0f)
		{
			// Flying face first.
			BallMesh->SetWorldRotation(GetVelocity().GetSafeNormal2D().Rotation());
		}
	}
	if (DriveLook.IsBuilt())
	{
		ChaosImpactDrive::UpdateLook(this, DriveLook, T, DeltaSeconds, bFlying,
			!IsHidden() && !bDetonated && !GetAttachParentActor(), GetDriveControlLeft(), GetVelocity());
	}
	// A nova is far too big for a ribbon trail.
	if (bFlying && !FlightTrailEffect && BallType != EChaosImpactBallType::Nova && BallType != EChaosImpactBallType::Drive)
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
	const bool bThunder = BallType == EChaosImpactBallType::Thunder;
	const bool bBeam = BallType == EChaosImpactBallType::Beam;
	const bool bSmoke = BallType == EChaosImpactBallType::Smoke;
	if (TypeGlow)
	{
		// Thunder crackles at random; the black ball's horizon breathes slowly; a beam's halo shimmers fast; a smoke
		// ball's haze swells lazily.
		const float ShellScale = bThunder ? 0.66f * FMath::FRandRange(0.88f, 1.28f)
			: bBeam ? 1.35f * (1.0f + 0.12f * FMath::Sin(T * 31.0f))
			: bSmoke ? 0.78f * (1.0f + 0.1f * FMath::Sin(T * 2.5f))
			: 0.62f * (1.0f + 0.06f * FMath::Sin(T * 5.0f));
		TypeGlow->SetWorldScale3D(FVector(ShellScale * ExpiryScale));
		if (TypeGlowMaterial)
		{
			TypeGlowMaterial->SetScalarParameterValue(TEXT("Intensity"),
				bThunder ? FMath::FRandRange(0.4f, 1.3f) : bBeam ? 2.2f + 0.8f * FMath::Sin(T * 23.0f)
				: bSmoke ? 0.7f + 0.2f * FMath::Sin(T * 3.0f) : 1.4f + 0.4f * FMath::Sin(T * 7.0f));
		}
	}
	if (bBeam)
	{
		UpdateBeamShaft(DeltaSeconds, bFlying);
	}
	for (int32 Index = 0; Index < TypeRings.Num(); ++Index)
	{
		if (UStaticMeshComponent* Ring = TypeRings[Index])
		{
			const bool bInner = Index == 0;
			// Wind: nearly flat and much faster, stacked a little apart like a small whirlwind (smoke: the same, lazily).
			const bool bWind = BallType == EChaosImpactBallType::Wind || bSmoke;
			Ring->SetWorldRotation(bWind ? FRotator(bInner ? 8.0f : -6.0f, T * (bInner ? 620.0f : 480.0f) * (bSmoke ? 0.3f : 1.0f), 0.0f)
				: FRotator(bInner ? 62.0f : -48.0f, T * (bInner ? 230.0f : -170.0f), bInner ? 10.0f : -25.0f));
			const float RingSize = (bInner ? 0.95f : 1.25f) * ExpiryScale;
			Ring->SetWorldScale3D(FVector(RingSize, RingSize, 0.03f));
		}
	}
	if (TypeArcs && T >= NextArcFlickerTime)
	{
		NextArcFlickerTime = T + 0.05f;
		FRandomStream Stream(FMath::Rand());
		ChaosImpactIceMeshes::FMeshBuffers Arcs;
		const FVector Facing = ChaosImpactLightning::GetViewDirection(GetWorld());
		const int32 ArcCount = Stream.RandRange(2, 4);
		for (int32 Index = 0; Index < ArcCount; ++Index)
		{
			const FVector Out = Stream.GetUnitVector();
			ChaosImpactLightning::AppendBolt(Arcs, Out * 20.0f,
				Out * Stream.FRandRange(55.0f, 105.0f) + Stream.GetUnitVector() * 16.0f, 5.5f, Facing, Stream, 1);
		}
		// Streaks trail behind it in flight.
		const FVector Velocity = GetBallVelocity();
		if (bFlying && Velocity.SizeSquared() > 1.0)
		{
			const FVector Back = -Velocity.GetSafeNormal();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				ChaosImpactLightning::AppendBolt(Arcs, Back * 18.0f + Stream.GetUnitVector() * 10.0f,
					Back * Stream.FRandRange(140.0f, 260.0f) + Stream.GetUnitVector() * 34.0f, 7.0f, Facing, Stream, 1);
			}
		}
		ChaosImpactLightning::SetMesh(TypeArcs, Arcs);
		TypeArcs->SetVisibility(!IsHidden() && ExpiryScale > 0.2f && bExpiryShown);
		if (TypeArcMaterial)
		{
			TypeArcMaterial->SetScalarParameterValue(TEXT("Intensity"), Stream.FRandRange(2.0f, 4.5f));
		}
	}
	if (TypeLight)
	{
		float LightIntensity = 1800.0f * (0.92f + 0.08f * FMath::Sin(T * 3.0f));
		if (bFire)
		{
			LightIntensity = 3500.0f * (0.82f + 0.12f * FMath::Sin(T * 23.0f) + 0.06f * FMath::Sin(T * 41.0f));
		}
		else if (bThunder)
		{
			LightIntensity = 5200.0f * FMath::FRandRange(0.35f, 1.3f);
		}
		else if (BallType == EChaosImpactBallType::Black)
		{
			LightIntensity = 2600.0f * (0.8f + 0.2f * FMath::Sin(T * 6.0f));
		}
		else if (bBeam)
		{
			LightIntensity = 6000.0f * (0.8f + 0.2f * FMath::Sin(T * 40.0f));
		}
		else if (bSmoke)
		{
			LightIntensity = 900.0f * (0.85f + 0.15f * FMath::Sin(T * 2.0f));
		}
		TypeLight->SetIntensity(LightIntensity);
	}
}

float AChaosImpactBall::GetHitRadius() const
{
	return CollisionSphere ? CollisionSphere->GetScaledSphereRadius() : 24.0f;
}

void AChaosImpactBall::SetSnowScale(const float Scale)
{
	SnowScale = FMath::Clamp(Scale, 0.3f, BallType == EChaosImpactBallType::Nova
		? ChaosImpactBallTypes::NovaMaxScale : ChaosImpactBallTypes::SnowMaxScale);
	ApplySnowScale();
}

void AChaosImpactBall::OnRep_SnowScale()
{
	ApplySnowScale();
}

void AChaosImpactBall::ApplySnowScale()
{
	// The ball as drawn and as it hits: both grow with the snow.
	BallMeshBaseScale = FVector(0.48f * SnowScale);
	if (BallMesh)
	{
		BallMesh->SetRelativeScale3D(BallMeshBaseScale * ExpiryScale);
	}
	if (CollisionSphere)
	{
		CollisionSphere->SetSphereRadius(24.0f * SnowScale);
	}
	if (NovaLook.IsBuilt())
	{
		// Also while held up before the release, when the ball does not tick.
		ChaosImpactBallTypes::UpdateNovaLook(NovaLook, 24.0f * SnowScale, TypeFxTime);
	}
}

void AChaosImpactBall::MulticastBeamStrike_Implementation(FVector_NetQuantize Location)
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		ChaosImpactBallTypes::PlayBeamBurst(this, Location, 0.9f);
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BeamStrike, Location);
	}
}

bool AChaosImpactBall::BeamTouches(const FVector& From, const FVector& To, const FVector& Center, const float Radius,
	const float HalfHeight, FVector& OutPoint)
{
	// The beam is a thick shaft of light: close enough across, and near enough in height (it stays level).
	OutPoint = FMath::ClosestPointOnSegment(Center, From, To);
	return FVector::Dist2D(OutPoint, Center) <= Radius + ChaosImpactBallTypes::BeamHitRadius
		&& FMath::Abs(OutPoint.Z - Center.Z) <= FMath::Max(HalfHeight, ChaosImpactBallTypes::BeamHitHeight);
}

void AChaosImpactBall::UpdateBeamHits()
{
	UWorld* World = GetWorld();
	const FVector From = BeamLastLocation;
	const FVector To = GetActorLocation();
	BeamLastLocation = To;
	// A throw preview never hurts anyone; the server's beam does.
	if (!World || bCosmeticPrediction || From.Equals(To))
	{
		return;
	}
	const APawn* Thrower = ThrowingPawn.Get();
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		AChaosImpactCharacter* Character = *It;
		// Remote players judge their own hits on their own screens (TryReportLocalHit).
		if (Character == Thrower || Character->IsEliminated() || BeamStruck.Contains(Character)
			|| Character->IsRemotePlayerOnServer() || AChaosImpactGameState::AreTeammates(World, Thrower, Character))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		FVector Struck;
		if (BeamTouches(From, To, Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleRadius(),
			Capsule->GetScaledCapsuleHalfHeight(), Struck))
		{
			// Once each: someone who dashed through it (invulnerable) is not caught by it later either.
			BeamStruck.Add(Character);
			ResolveDamagingHit(Character, Struck, (Character->GetActorLocation() - Struck).GetSafeNormal2D());
		}
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(World); It; ++It)
	{
		FVector Struck;
		if (!It->IsDefeated() && !BeamStruck.Contains(*It) && BeamTouches(From, To, It->GetActorLocation(), 50.0f, 90.0f, Struck))
		{
			BeamStruck.Add(*It);
			ResolveDamagingHit(*It, Struck, FVector::ZeroVector);
		}
	}
}

void AChaosImpactBall::UpdateBeamShaft(const float DeltaSeconds, const bool bFlying)
{
	if (!BeamCore || !BeamGlow || !BallMesh)
	{
		return;
	}
	if (bFlying)
	{
		BeamShaftHead = BallMesh->GetComponentLocation();
		if (!bBeamShaftStarted)
		{
			// Leaving the hand: a flash, and the shaft of light starts here.
			bBeamShaftStarted = true;
			BeamShaftStart = BeamShaftHead;
			if (UNiagaraSystem* Flash = ChaosImpactBallTypes::LoadEffect(ChaosImpactBallTypes::Effects::MuzzleFlash))
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Flash, BeamShaftHead, GetBallVelocity().Rotation(), FVector(1.2f));
			}
		}
		BeamShaftFade = 1.0f;
	}
	else if (bBeamShaftStarted)
	{
		BeamShaftFade = FMath::Max(0.0f, BeamShaftFade - DeltaSeconds / 0.35f);
	}
	// The shaft trails at most this far behind the head, so a long flight is a streak rather than a wall of light.
	constexpr float MaxLength = 2600.0f;
	FVector Tail = BeamShaftStart;
	FVector Along = BeamShaftHead - Tail;
	float Length = static_cast<float>(Along.Size());
	if (Length > MaxLength)
	{
		Tail = BeamShaftHead - Along / Length * MaxLength;
		Length = MaxLength;
	}
	const bool bShow = bBeamShaftStarted && BeamShaftFade > 0.0f && Length > 5.0f;
	BeamCore->SetVisibility(bShow);
	BeamGlow->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	Along = (BeamShaftHead - Tail).GetSafeNormal();
	const FRotator Facing = FRotationMatrix::MakeFromZ(Along).Rotator();
	const FVector Middle = (BeamShaftHead + Tail) * 0.5f;
	const float Shimmer = 0.85f + 0.15f * FMath::Sin(TypeFxTime * 37.0f);
	// A thin white-hot core inside a wider pink glow that thins toward its edges.
	BeamCore->SetWorldLocationAndRotation(Middle, Facing);
	BeamCore->SetWorldScale3D(FVector(0.2f * BeamShaftFade, 0.2f * BeamShaftFade, Length / 100.0f));
	BeamGlow->SetWorldLocationAndRotation(Middle, Facing);
	BeamGlow->SetWorldScale3D(FVector(1.0f * Shimmer, 1.0f * Shimmer, Length / 100.0f));
	if (BeamCoreMaterial)
	{
		BeamCoreMaterial->SetScalarParameterValue(TEXT("Intensity"), 7.0f * BeamShaftFade);
	}
	if (BeamGlowMaterial)
	{
		BeamGlowMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f * BeamShaftFade * Shimmer);
	}
}

void AChaosImpactBall::BeginDrive(const double DeadlineServerTime)
{
	DriveDeadline = DeadlineServerTime;
	DriveSteer = FVector::ZeroVector;
	DriveSpeed = 0.0f;
	ForceNetUpdate();
}

void AChaosImpactBall::SetDriveSteer(const FVector& Direction)
{
	DriveSteer = FVector(Direction.X, Direction.Y, 0.0f).GetSafeNormal();
}

float AChaosImpactBall::GetDriveControlLeft() const
{
	if (DriveDeadline <= 0.0 || bDetonated)
	{
		return -1.0f;
	}
	return FMath::Clamp(static_cast<float>(DriveDeadline - GetServerNow()) / ChaosImpactBallTypes::DriveControlSeconds, 0.0f, 1.0f);
}

void AChaosImpactBall::FizzleDrive()
{
	DriveDeadline = 0.0;
	if (bDetonated)
	{
		return;
	}
	if (bCosmeticPrediction)
	{
		// This screen's throw preview: the server's ball shows the real end.
		SetActorHiddenInGame(true);
		SetLifeSpan(0.5f);
		return;
	}
	if (HasAuthority())
	{
		Detonate(GetActorLocation(), nullptr);
	}
}

void AChaosImpactBall::KnockAwayByDrive(const AChaosImpactBall* Drive)
{
	if (!Drive || !HasAuthority() || bCosmeticPrediction || bIsPickup || bDetonated || !GetWorld()
		|| BallType == EChaosImpactBallType::Drive)
	{
		return;
	}
	if (IsSpecialBall() && BallType != EChaosImpactBallType::Thunder)
	{
		// As against anything else it touches.
		Detonate(GetActorLocation(), nullptr);
		return;
	}
	// Batted aside, the way the drive ball was going, and left rolling on the floor.
	const FVector Along = Drive->GetVelocity().GetSafeNormal2D();
	FVector Away = (GetActorLocation() - Drive->GetActorLocation()).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		Away = Along;
	}
	const FVector Fling = (Away + Along).GetSafeNormal2D() * 1100.0f + FVector::UpVector * 420.0f;
	MakeRollingPickup(Fling.IsNearlyZero() ? FVector::UpVector * 420.0f : Fling);
	ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("%s ball knocked away by a drive ball"), ChaosImpactBallTypes::GetInternalName(BallType));
}

void AChaosImpactBall::UpdateDriveFlight(const float DeltaSeconds)
{
	if (DriveDeadline <= 0.0)
	{
		return;
	}
	if (GetServerNow() >= DriveDeadline)
	{
		// Its time is up: it fizzles out where it is.
		FizzleDrive();
		return;
	}
	FVector Velocity = ProjectileMovement->Velocity;
	// The server's ball (and this screen's throw preview) keeps its own heading and speed; a copy follows the server.
	const bool bOwnFlight = HasAuthority();
	if (bOwnFlight)
	{
		if (DriveSpeed <= 0.0f)
		{
			DriveSpeed = static_cast<float>(Velocity.Size2D());
			DriveHeading = Velocity.GetSafeNormal2D();
		}
		Velocity = DriveHeading * DriveSpeed + FVector(0.0f, 0.0f, Velocity.Z);
	}
	const float Speed = static_cast<float>(Velocity.Size2D());
	if (Speed < 1.0f)
	{
		return;
	}
	if (DriveSteer.IsNearlyZero())
	{
		ProjectileMovement->Velocity = Velocity;
		return;
	}
	// Turns toward where it is steered, at a rate that a fast ball (a charged throw) cannot match.
	using namespace ChaosImpactBallTypes;
	const FVector Current = FVector(Velocity.X, Velocity.Y, 0.0f) / Speed;
	const float Cross = static_cast<float>(Current.X * DriveSteer.Y - Current.Y * DriveSteer.X);
	const float Dot = static_cast<float>(FVector::DotProduct(Current, DriveSteer));
	const float Wanted = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	const float Alpha = FMath::Clamp((Speed - DriveMinSpeed) / (DriveMaxSpeed - DriveMinSpeed), 0.0f, 1.0f);
	const float MaxTurn = FMath::Lerp(DriveTurnSlowBallDegrees, DriveTurnFastBallDegrees, Alpha) * DeltaSeconds;
	// Sent back the way it came (or anywhere far round), it snaps round at once; otherwise it curves.
	const FVector Turned = FMath::Abs(Wanted) > DriveSnapDegrees ? DriveSteer
		: Current.RotateAngleAxis(FMath::Clamp(Wanted, -MaxTurn, MaxTurn), FVector::UpVector);
	ProjectileMovement->Velocity = Turned * Speed + FVector(0.0f, 0.0f, Velocity.Z);
	if (bOwnFlight)
	{
		DriveHeading = Turned;
	}
}

void AChaosImpactBall::MulticastDriveBurst_Implementation(const FVector_NetQuantize Location, const bool bHit)
{
	AChaosImpactDriveBurst::Play(GetWorld(), Location, bHit);
	StopFlightSound();
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::DriveBurst, Location, bHit ? 1.0f : 0.8f);
}
