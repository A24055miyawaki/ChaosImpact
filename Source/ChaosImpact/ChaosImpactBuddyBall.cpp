#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AChaosImpactBuddyBall::AChaosImpactBuddyBall()
{
	// Deliberately not a new special-ball type: all ordinary-ball rules stay intact.
	ProjectileMovement->bTickBeforeOwner = false; // The base ball explicitly schedules owner -> projectile.
}

void AChaosImpactBuddyBall::InitializePassenger(AChaosImpactCharacter* Character)
{
	Passenger = Character;
	MakePickup();
	CollisionSphere->SetSphereRadius(35.0f);
	CollisionSphere->SetCollisionObjectType(ECC_Pawn);
	CollisionSphere->IgnoreActorWhenMoving(Character, true);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	BallMesh->SetVisibility(false, true);
}

bool AChaosImpactBuddyBall::IsPickupAvailable() const
{
	return Passenger.IsValid() && !Carrier.IsValid() && !bLaunched && Super::IsPickupAvailable();
}

bool AChaosImpactBuddyBall::ConsumePickup(AChaosImpactCharacter* Collector)
{
	if (!IsPickupAvailable() || !Collector || !Collector->GetBallForm()->CanCollect(Passenger.Get())
		|| !Collector->TryPickupBall(this)) { return false; }
	Carrier = Collector;
	bPickupConsumed = true;
	SetOwner(Collector);
	SetInstigator(Collector);
	// Keep the actual passenger ball; never destroy it and replace it with an ordinary pickup.
	PrepareForAnimatedThrow(Collector->GetRootComponent(), NAME_None, FVector::ZeroVector, FRotator::ZeroRotator);
	SetActorTickEnabled(true);
	FollowHand();
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
	bLaunched = true;
	LaunchedAt = GetWorld()->GetTimeSeconds();
	Carrier.Reset();
	Super::Launch(Direction, Speed, Mode, UpSpeed);
	BallMesh->SetVisibility(false, true);
	CollisionSphere->IgnoreActorWhenMoving(Passenger.Get(), true);
}

void AChaosImpactBuddyBall::Tick(const float DeltaSeconds)
{
	if (!Passenger.IsValid()) { Destroy(); return; }
	if (Carrier.IsValid())
	{
		// Held pickups must not execute the base hover code, which would move them back to the floor.
		if (Carrier->IsEliminated()) { ReleasePassenger(); return; }
		const bool bAllied = AChaosImpactGameState::AreTeammates(GetWorld(),Carrier.Get(),Passenger.Get());
		if (!bAllied) { ReleasePassenger(); return; }
		FollowHand();
		return;
	}
	if (bLaunched && (!Steering.IsNearlyZero()) && ProjectileMovement->IsActive())
	{
		const FVector V = ProjectileMovement->Velocity;
		const float Speed = V.Size2D();
		const FVector Turned = FMath::VInterpNormalRotationTo(V.GetSafeNormal2D(), Steering.GetSafeNormal2D(), DeltaSeconds, 32.0f);
		ProjectileMovement->Velocity = Turned * Speed + FVector::UpVector * V.Z;
		Steering = FVector::ZeroVector;
	}
	if (bLaunched) { Super::Tick(DeltaSeconds); }
	BallMesh->SetVisibility(false, true);
	if (bLaunched && (IsPickup() || GetWorld()->GetTimeSeconds() - LaunchedAt > 4.0))
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

void AChaosImpactBuddyBall::ReleasePassenger()
{
	if (AChaosImpactCharacter* Rider = Passenger.Get()) { Rider->GetBallForm()->Restore(); }
	else { Destroy(); }
}

float AChaosImpactBuddyBall::TakeDamage(const float Amount, const FDamageEvent& Event,
	AController* InstigatorController, AActor* Causer)
{
	return Passenger.IsValid() ? Passenger->TakeDamage(Amount, Event, InstigatorController, Causer) : 0.0f;
}

void AChaosImpactBuddyBall::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Reason == EEndPlayReason::Destroyed && Passenger.IsValid()) { Passenger->GetBallForm()->Restore(); }
	Super::EndPlay(Reason);
}
