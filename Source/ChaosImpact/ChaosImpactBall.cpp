// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTrainingTarget.h"

#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
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
	// Online rooms: the server simulates every ball and clients follow its movement.
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.0f);

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
	if (!HasAuthority())
	{
		// A client copy is purely visual: no local physics, projectile motion or hits.
		ProjectileMovement->Deactivate();
		CollisionSphere->SetSimulatePhysics(false);
		CollisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorTickEnabled(false);
	}
}

void AChaosImpactBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactBall, bIsPickup);
	DOREPLIFETIME(AChaosImpactBall, bIsRolling);
}

void AChaosImpactBall::MulticastContactBurst_Implementation(FVector_NetQuantize Location, FRotator Rotation)
{
	if (UNiagaraSystem* ContactBurst = LoadObject<UNiagaraSystem>(
		nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage")))
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
		return;
	}
	if (!bIsPickup)
	{
		FlightSeconds += DeltaSeconds;
		if (FlightSeconds >= LifeSeconds)
		{
			DropToGroundAsPickup();
		}
		return;
	}
	if (bIsRolling)
	{
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
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
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
	bIsRolling = false;
	CollisionSphere->SetSimulatePhysics(false);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
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
}

void AChaosImpactBall::MakePickup()
{
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
}

void AChaosImpactBall::MakeRollingPickup(const FVector& ImpactVelocity)
{
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
}

bool AChaosImpactBall::WasThrownBy(const APawn* Pawn) const
{
	return Pawn && (ThrowingPawn.Get() == Pawn || GetInstigator() == Pawn || GetOwner() == Pawn);
}

FVector AChaosImpactBall::GetBallVelocity() const
{
	if (!HasAuthority())
	{
		return GetVelocity();
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
	if (bIsPickup || !HasAuthority())
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
	if ((bHitPawn || bHitTrainingTarget) && OtherActor->CanBeDamaged())
	{
		// A compact contact flash is independent of the target/player elimination burst,
		// so even non-lethal hits have immediate visual feedback.
		MulticastContactBurst(Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
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
		DropToGroundAsPickup();
	}
}

void AChaosImpactBall::HandlePickupOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !IsPickupAvailable() || bPickupConsumed)
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
	if (!HasAuthority())
	{
		return;
	}
	if (ImpactResult.GetActor() && ImpactResult.GetActor()->IsA<APawn>())
	{
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
