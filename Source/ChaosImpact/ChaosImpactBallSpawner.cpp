#include "ChaosImpactBallSpawner.h"

#include "ChaosImpactBall.h"
#include "ChaosImpact.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AChaosImpactBallSpawner::AChaosImpactBallSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	// The server places pads at runtime; online members see them too.
	bReplicates = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpawnPad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpawnPad"));
	SpawnPad->SetupAttachment(SceneRoot);
	SpawnPad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpawnPad->SetRelativeScale3D(FVector(0.72f, 0.72f, 0.045f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		SpawnPad->SetStaticMesh(CylinderMesh.Object);
	}

	SpawnLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("SpawnLight"));
	SpawnLight->SetupAttachment(SceneRoot);
	SpawnLight->SetRelativeLocation(FVector(0.0f, 0.0f, 45.0f));
	SpawnLight->SetLightColor(FLinearColor(0.0f, 0.55f, 1.0f));
	SpawnLight->SetIntensity(1800.0f);
	SpawnLight->SetAttenuationRadius(210.0f);
	SpawnLight->SetCastShadows(false);

	BallClass = AChaosImpactBall::StaticClass();
}

void AChaosImpactBallSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (!bAlwaysActive && !ChaosImpact::IsTrainingWorld(GetWorld()))
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}
	// Balls are replicated actors, so only the server (or a standalone game) spawns them.
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(SpawnTimer, this,
			&AChaosImpactBallSpawner::TrySpawnBall, RespawnInterval, true, 0.2f);
	}
}

EChaosImpactBallType AChaosImpactBallSpawner::RollBallType() const
{
	const TPair<EChaosImpactBallType, float> Chances[] =
	{
		{EChaosImpactBallType::Fire, FireBallChance}, {EChaosImpactBallType::Ice, IceBallChance},
		{EChaosImpactBallType::Thunder, ThunderBallChance}, {EChaosImpactBallType::Black, BlackBallChance}
	};
	const float Roll = FMath::FRand();
	float Threshold = 0.0f;
	for (const TPair<EChaosImpactBallType, float>& Chance : Chances)
	{
		Threshold += Chance.Value;
		if (Roll < Threshold)
		{
			return Chance.Key;
		}
	}
	return EChaosImpactBallType::Normal;
}

void AChaosImpactBallSpawner::TrySpawnBall()
{
	if (!GetWorld() || !HasAuthority() || !BallClass || ActiveBall.IsValid())
	{
		return;
	}

	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation() + FVector::UpVector * BallHeight);
	AChaosImpactBall* Ball = GetWorld()->SpawnActorDeferred<AChaosImpactBall>(BallClass, SpawnTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ball)
	{
		return;
	}
	const EChaosImpactBallType Type = RollBallType();
	Ball->SetBallType(Type);
	Ball->FinishSpawning(SpawnTransform);
	Ball->MakePickup();
	ActiveBall = Ball;
	// The pad glows in the ball's color, so a special ball is noticeable from a distance.
	SpawnLight->SetLightColor(Type == EChaosImpactBallType::Normal
		? FLinearColor(0.0f, 0.55f, 1.0f) : ChaosImpactBallTypes::GetColor(Type));
	SpawnLight->SetIntensity(Type == EChaosImpactBallType::Normal ? 1800.0f : 4200.0f);
}
