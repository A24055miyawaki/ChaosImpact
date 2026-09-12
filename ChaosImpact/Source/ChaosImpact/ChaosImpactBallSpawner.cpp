#include "ChaosImpactBallSpawner.h"

#include "ChaosImpactBall.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AChaosImpactBallSpawner::AChaosImpactBallSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

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
	if (!GetWorld() || !GetWorld()->URL.HasOption(TEXT("CITraining=1")))
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}
	GetWorldTimerManager().SetTimer(SpawnTimer, this,
		&AChaosImpactBallSpawner::TrySpawnBall, RespawnInterval, true, 0.2f);
}

void AChaosImpactBallSpawner::TrySpawnBall()
{
	if (!GetWorld() || !BallClass || ActiveBall.IsValid())
	{
		return;
	}

	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Location = GetActorLocation() + FVector::UpVector * BallHeight;
	if (AChaosImpactBall* Ball = GetWorld()->SpawnActor<AChaosImpactBall>(
		BallClass, Location, GetActorRotation(), Parameters))
	{
		Ball->MakePickup();
		ActiveBall = Ball;
	}
}
