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
	UpdateChanceSummary();
}

void AChaosImpactBallSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateChanceSummary();
}

float* AChaosImpactBallSpawner::WeightFor(const EChaosImpactBallType Type)
{
	switch (Type)
	{
	case EChaosImpactBallType::Normal: return &NormalBallChance;
	case EChaosImpactBallType::Fire: return &FireBallChance;
	case EChaosImpactBallType::Ice: return &IceBallChance;
	case EChaosImpactBallType::Thunder: return &ThunderBallChance;
	case EChaosImpactBallType::Black: return &BlackBallChance;
	case EChaosImpactBallType::Wind: return &WindBallChance;
	case EChaosImpactBallType::Smoke: return &SmokeBallChance;
	case EChaosImpactBallType::Beam: return &BeamBallChance;
	case EChaosImpactBallType::Snow: return &SnowBallChance;
	case EChaosImpactBallType::Simae: return &SimaeBallChance;
	case EChaosImpactBallType::Drive: return &DriveBallChance;
	default: return nullptr;
	}
}

float AChaosImpactBallSpawner::GetChance(const EChaosImpactBallType Type) const
{
	float Total = 0.0f;
	for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
	{
		const float* Weight = WeightFor(static_cast<EChaosImpactBallType>(Index));
		Total += Weight ? FMath::Max(0.0f, *Weight) : 0.0f;
	}
	const float* Weight = WeightFor(Type);
	// Every weight 0: only normal balls.
	if (Total <= 0.0f)
	{
		return Type == EChaosImpactBallType::Normal ? 1.0f : 0.0f;
	}
	return Weight ? FMath::Max(0.0f, *Weight) / Total : 0.0f;
}

void AChaosImpactBallSpawner::SetChanceWeight(const EChaosImpactBallType Type, const float Weight)
{
	if (float* Setting = WeightFor(Type))
	{
		*Setting = FMath::Max(0.0f, Weight);
		UpdateChanceSummary();
	}
}

void AChaosImpactBallSpawner::UpdateChanceSummary()
{
	TArray<FString> Lines;
	for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
	{
		const EChaosImpactBallType Type = static_cast<EChaosImpactBallType>(Index);
		const float Chance = GetChance(Type);
		if (Chance > 0.0f)
		{
			Lines.Add(FString::Printf(TEXT("%s %.1f%%"), ChaosImpactBallTypes::GetInternalName(Type), Chance * 100.0f));
		}
	}
	ChanceSummary = FString::Join(Lines, TEXT("\n"));
}

void AChaosImpactBallSpawner::BeginPlay()
{
	Super::BeginPlay();
	// Placed in a level: training and solo mode (the title and VS make their own pads).
	const UWorld* World = GetWorld();
	const bool bSoloWorld = World && World->URL.HasOption(TEXT("CISolo=1"));
	if (!bAlwaysActive && !ChaosImpact::IsTrainingWorld(World) && !bSoloWorld)
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}
	bInPlay = true;
	// Balls are replicated actors, so only the server (or a standalone game) spawns them.
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(SpawnTimer, this,
			&AChaosImpactBallSpawner::TrySpawnBall, RespawnInterval, true, 0.2f);
	}
}

EChaosImpactBallType AChaosImpactBallSpawner::RollBallType() const
{
	float Roll = FMath::FRand();
	for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
	{
		const EChaosImpactBallType Type = static_cast<EChaosImpactBallType>(Index);
		Roll -= GetChance(Type);
		if (Roll < 0.0f)
		{
			return Type;
		}
	}
	return EChaosImpactBallType::Normal;
}

void AChaosImpactBallSpawner::TrySpawnBall()
{
	// A ball carried away from the pad (by a tornado) no longer holds its place.
	if (ActiveBall.IsValid() && ActiveBall->HasLeftSpawnPoint())
	{
		ActiveBall.Reset();
	}
	// A nova part on the pad stays until it is taken.
	if (!GetWorld() || !HasAuthority() || !BallClass || ActiveBall.IsValid() || ActiveNovaPart.IsValid())
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

bool AChaosImpactBallSpawner::PutOutNovaPart(const EChaosImpactNovaPart Part)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !bInPlay || ActiveNovaPart.IsValid())
	{
		return false;
	}
	// Its ball (still lying here) makes way for it.
	if (AChaosImpactBall* Ball = ActiveBall.Get(); Ball && !Ball->HasLeftSpawnPoint() && Ball->IsPickupAvailable())
	{
		Ball->Destroy();
	}
	ActiveBall.Reset();
	ActiveNovaPart = AChaosImpactNovaPart::Spawn(World, Part, GetActorLocation() + FVector::UpVector * ChaosImpactNovaParts::PartHoverHeight);
	if (!ActiveNovaPart.IsValid())
	{
		return false;
	}
	SpawnLight->SetLightColor(ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Nova));
	SpawnLight->SetIntensity(5200.0f);
	return true;
}
