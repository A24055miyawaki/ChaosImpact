// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AChaosImpactBall::AChaosImpactBall()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

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

	if (AActor* OwningActor = ThrowingPawn.IsValid()
		? static_cast<AActor*>(ThrowingPawn.Get()) : GetOwner())
	{
		CollisionSphere->IgnoreActorWhenMoving(OwningActor, true);
	}
}

void AChaosImpactBall::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsPickup)
	{
		FlightSeconds += DeltaSeconds;
		if (FlightSeconds >= LifeSeconds)
		{
			DropToGroundAsPickup();
		}
		return;
	}

	PickupAnimationTime += DeltaSeconds;
	const float Hover = FMath::Sin(PickupAnimationTime * 2.2f) * 7.0f;
	SetActorLocation(PickupBaseLocation + FVector::UpVector * Hover);
	AddActorLocalRotation(FRotator(0.0f, DeltaSeconds * 55.0f, 0.0f));
}

void AChaosImpactBall::Launch(const FVector& Direction, const float Speed,
	const EChaosImpactBallFlightMode FlightMode, const float ArcUpwardSpeed)
{
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
	bIsPickup = false;
	SetActorTickEnabled(true);
	ProjectileMovement->Activate(true);
	ProjectileMovement->ProjectileGravityScale = bArc ? 1.0f : 0.0f;
	ProjectileMovement->bConstrainToPlane = !bArc;
	ProjectileMovement->SetPlaneConstraintEnabled(!bArc);
	ProjectileMovement->Bounciness = bArc ? 0.72f : Bounciness;
	ProjectileMovement->Friction = bArc ? 0.12f : 0.0f;
	ProjectileMovement->Velocity = HorizontalDirection.GetSafeNormal() * Speed
		+ (bArc ? FVector::UpVector * ArcUpwardSpeed : FVector::ZeroVector);
}

void AChaosImpactBall::MakePickup()
{
	bIsPickup = true;
	bPickupConsumed = false;
	PickupAnimationTime = 0.0f;
	PickupBaseLocation = GetActorLocation();
	SetOwner(nullptr);
	SetInstigator(nullptr);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);
	SetActorTickEnabled(true);
}

bool AChaosImpactBall::WasThrownBy(const APawn* Pawn) const
{
	return Pawn && (ThrowingPawn.Get() == Pawn || GetInstigator() == Pawn || GetOwner() == Pawn);
}

void AChaosImpactBall::DropToGroundAsPickup()
{
	if (bIsPickup || !GetWorld())
	{
		return;
	}

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
	MakePickup();
}

void AChaosImpactBall::HandleImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bIsPickup)
	{
		return;
	}
	if (!IsValid(OtherActor) || OtherActor == this || OtherActor == GetOwner()
		|| OtherActor == GetInstigator() || OtherActor == ThrowingPawn.Get())
	{
		return;
	}

	if (APawn* HitPawn = Cast<APawn>(OtherActor); HitPawn && HitPawn->CanBeDamaged())
	{
		const float AppliedDamage = UGameplayStatics::ApplyDamage(
			HitPawn, Damage, GetInstigatorController(), this, nullptr);
		if (AppliedDamage > 0.0f)
		{
			if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(ThrowingPawn.Get());
				Thrower && Thrower != HitPawn)
			{
				Thrower->RecoverStaminaFromBallHit();
			}
		}
		DropToGroundAsPickup();
	}
}

void AChaosImpactBall::HandlePickupOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bIsPickup || bPickupConsumed)
	{
		return;
	}

	if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(OtherActor);
		Character && Character->TryPickupBall(this))
	{
		bPickupConsumed = true;
		Destroy();
	}
}

void AChaosImpactBall::HandleBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (ImpactResult.GetActor() && ImpactResult.GetActor()->IsA<APawn>())
	{
		return;
	}
	if (ActiveFlightMode == EChaosImpactBallFlightMode::Arc
		&& ImpactResult.ImpactNormal.Z > 0.65f)
	{
		DropToGroundAsPickup();
		return;
	}

	bHasReflected = true;
	++ReflectionCount;

	// A reflected ball can hit other pawns, but never the character who threw it.
}
