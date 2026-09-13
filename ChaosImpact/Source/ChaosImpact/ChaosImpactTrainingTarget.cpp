// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactTrainingTarget.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FVector TargetBaseScale(0.72f, 0.72f, 0.14f);
	const FVector TargetPoleScale(0.105f, 0.105f, 0.92f);
	const FVector TargetBagScale(0.62f, 0.62f, 1.05f);
	const FVector TargetPlateScale(0.43f, 0.43f, 0.055f);
}

AChaosImpactTrainingTarget::AChaosImpactTrainingTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FallingAssembly = CreateDefaultSubobject<USceneComponent>(TEXT("FallingAssembly"));
	FallingAssembly->SetupAttachment(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatMaterial(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeightedBase"));
	BaseMesh->SetupAttachment(SceneRoot);
	BaseMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));
	BaseMesh->SetRelativeScale3D(TargetBaseScale);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderAsset.Succeeded())
	{
		BaseMesh->SetStaticMesh(CylinderAsset.Object);
	}

	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SupportPole"));
	PoleMesh->SetupAttachment(FallingAssembly);
	PoleMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 58.0f));
	PoleMesh->SetRelativeScale3D(TargetPoleScale);
	PoleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderAsset.Succeeded())
	{
		PoleMesh->SetStaticMesh(CylinderAsset.Object);
	}

	BagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sandbag"));
	BagMesh->SetupAttachment(FallingAssembly);
	BagMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 132.0f));
	BagMesh->SetRelativeScale3D(TargetBagScale);
	BagMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BagMesh->SetCollisionObjectType(ECC_WorldDynamic);
	BagMesh->SetCollisionResponseToAllChannels(ECR_Block);
	BagMesh->SetGenerateOverlapEvents(false);
	if (SphereAsset.Succeeded())
	{
		BagMesh->SetStaticMesh(SphereAsset.Object);
	}

	FacePlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetPlate"));
	FacePlateMesh->SetupAttachment(FallingAssembly);
	FacePlateMesh->SetRelativeLocation(FVector(58.0f, 0.0f, 137.0f));
	FacePlateMesh->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	FacePlateMesh->SetRelativeScale3D(TargetPlateScale);
	FacePlateMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylinderAsset.Succeeded())
	{
		FacePlateMesh->SetStaticMesh(CylinderAsset.Object);
	}
	FacePlateMesh->SetVisibility(false, true);
	FacePlateMesh->SetHiddenInGame(true);

	ShockCore = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ImpactShockCore"));
	ShockCore->SetupAttachment(SceneRoot);
	ShockCore->SetRelativeLocation(FVector(0.0f, 0.0f, 132.0f));
	ShockCore->SetRelativeScale3D(FVector(0.1f));
	ShockCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShockCore->SetHiddenInGame(true);
	if (SphereAsset.Succeeded())
	{
		ShockCore->SetStaticMesh(SphereAsset.Object);
	}

	for (int32 PieceIndex = 0; PieceIndex < 42; ++PieceIndex)
	{
		const FName PieceName(*FString::Printf(TEXT("ImpactShard_%02d"), PieceIndex));
		UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(PieceName);
		Piece->SetupAttachment(SceneRoot);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetHiddenInGame(true);
		if (CubeAsset.Succeeded())
		{
			Piece->SetStaticMesh(CubeAsset.Object);
		}
		BurstPieces.Add(Piece);
	}
	if (FlatMaterial.Succeeded())
	{
		BaseMesh->SetMaterial(0, FlatMaterial.Object);
		PoleMesh->SetMaterial(0, FlatMaterial.Object);
		BagMesh->SetMaterial(0, FlatMaterial.Object);
		FacePlateMesh->SetMaterial(0, FlatMaterial.Object);
		ShockCore->SetMaterial(0, FlatMaterial.Object);
		for (UStaticMeshComponent* Piece : BurstPieces)
		{
			Piece->SetMaterial(0, FlatMaterial.Object);
		}
	}

	HitLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ImpactFlash"));
	HitLight->SetupAttachment(SceneRoot);
	HitLight->SetRelativeLocation(FVector(0.0f, 0.0f, 135.0f));
	HitLight->SetLightColor(FLinearColor(0.05f, 0.65f, 1.0f));
	HitLight->SetAttenuationRadius(520.0f);
	HitLight->SetCastShadows(false);
	HitLight->SetIntensity(0.0f);
}

void AChaosImpactTrainingTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The cyan circular face plate read as an unrelated marker rather than part
	// of the sandbag, so keep it removed in both the editor and the running game.
	if (FacePlateMesh)
	{
		FacePlateMesh->SetVisibility(false, true);
		FacePlateMesh->SetHiddenInGame(true);
	}
}

void AChaosImpactTrainingTarget::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
	if (!GetWorld() || !GetWorld()->URL.HasOption(TEXT("CITraining=1")))
	{
		SetTargetVisible(false);
		BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorTickEnabled(false);
		return;
	}

	auto Tint = [](UStaticMeshComponent* Component, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* Material = Component->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	};
	Tint(BaseMesh, FLinearColor(0.01f, 0.02f, 0.055f));
	Tint(PoleMesh, FLinearColor(0.0f, 0.22f, 0.34f));
	Tint(BagMesh, FLinearColor(0.9f, 0.035f, 0.02f));
	Tint(FacePlateMesh, FLinearColor(0.0f, 0.55f, 1.0f));
	Tint(ShockCore, FLinearColor(1.0f, 0.65f, 0.03f));
	for (int32 PieceIndex = 0; PieceIndex < BurstPieces.Num(); ++PieceIndex)
	{
		Tint(BurstPieces[PieceIndex], PieceIndex % 2 == 0
			? FLinearColor(0.0f, 0.7f, 1.0f) : FLinearColor(1.0f, 0.12f, 0.015f));
	}
}

void AChaosImpactTrainingTarget::ConfigureMotion(const EChaosImpactTargetMotion NewMotion,
	const float NewTravelDistance, const float NewCyclesPerSecond, const float StartPhase)
{
	MotionMode = NewMotion;
	TravelDistance = FMath::Max(0.0f, NewTravelDistance);
	CyclesPerSecond = FMath::Max(0.05f, NewCyclesPerSecond);
	MotionPhase = StartPhase;
	HomeLocation = GetActorLocation();
}

void AChaosImpactTrainingTarget::SetTrainingEnabled(const bool bEnabled)
{
	bTrainingEnabled = bEnabled;
	SetActorHiddenInGame(!bEnabled);
	SetActorTickEnabled(bEnabled);
	if (!bEnabled)
	{
		BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	SetTargetVisible(!bDefeated || bRespawning);
	BagMesh->SetCollisionEnabled(!bDefeated && !bRespawning
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void AChaosImpactTrainingTarget::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDefeated)
	{
		DefeatTime += DeltaSeconds;
		const float Alpha = FMath::Clamp(DefeatTime / FallDuration, 0.0f, 1.0f);
		const float FallAlpha = FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.25f);
		FallingAssembly->SetRelativeRotation(FRotator(0.0f, 0.0f, FallAlpha * 86.0f));
		FallingAssembly->SetRelativeLocation(FVector(0.0f, 0.0f, -FallAlpha * 26.0f));

		const float EffectAlpha = FMath::Clamp(DefeatTime / 0.68f, 0.0f, 1.0f);
		ShockCore->SetRelativeLocation(ImpactLocalLocation);
		ShockCore->SetRelativeScale3D(FVector(FMath::Lerp(0.1f, 2.2f, EffectAlpha)));
		ShockCore->SetHiddenInGame(EffectAlpha >= 0.42f);
		HitLight->SetIntensity(FMath::Lerp(22000.0f, 0.0f,
			FMath::Clamp(DefeatTime / 0.36f, 0.0f, 1.0f)));

		for (int32 PieceIndex = 0; PieceIndex < BurstPieces.Num(); ++PieceIndex)
		{
			UStaticMeshComponent* Piece = BurstPieces[PieceIndex];
			const float Angle = PieceIndex * 2.39996323f;
			const float Height = FMath::Lerp(-0.18f, 0.92f,
				static_cast<float>(PieceIndex % 13) / 12.0f);
			const float Radius = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Height * Height));
			const FVector Direction(FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius, Height);
			const float ScatterDistance = 230.0f + 24.0f * (PieceIndex % 8);
			Piece->SetRelativeLocation(ImpactLocalLocation
				+ Direction * FMath::Lerp(8.0f, ScatterDistance, EffectAlpha));
			Piece->SetRelativeRotation(Direction.Rotation()
				+ FRotator(EffectAlpha * 720.0f, EffectAlpha * 540.0f, 0.0f));
			const float ParticleSize = (0.045f + 0.012f * (PieceIndex % 4))
				* FMath::Clamp(1.0f - EffectAlpha, 0.0f, 1.0f);
			Piece->SetRelativeScale3D(FVector(ParticleSize));
			Piece->SetHiddenInGame(EffectAlpha >= 0.98f);
		}

		if (Alpha >= 1.0f)
		{
			SetTargetVisible(false);
		}
		return;
	}

	if (bRespawning)
	{
		RespawnTime += DeltaSeconds;
		const float Alpha = FMath::Clamp(RespawnTime / RespawnGrowDuration, 0.0f, 1.0f);
		const float Grow = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 3.0f);
		const FVector ScaleFactor(FMath::Lerp(0.18f, 1.0f, Grow),
			FMath::Lerp(0.18f, 1.0f, Grow), FMath::Lerp(0.04f, 1.0f, Grow));
		BaseMesh->SetRelativeScale3D(TargetBaseScale * ScaleFactor);
		PoleMesh->SetRelativeScale3D(TargetPoleScale * ScaleFactor);
		BagMesh->SetRelativeScale3D(TargetBagScale * ScaleFactor);
		FacePlateMesh->SetRelativeScale3D(TargetPlateScale * ScaleFactor);
		FallingAssembly->SetRelativeLocation(FVector::UpVector * FMath::Lerp(-125.0f, 0.0f, Grow));
		ShockCore->SetRelativeLocation(FVector(0.0f, 0.0f, FMath::Lerp(8.0f, 132.0f, Grow)));
		ShockCore->SetRelativeScale3D(FVector(FMath::Lerp(0.05f, 1.35f, Grow)));
		ShockCore->SetHiddenInGame(Alpha > 0.72f);
		HitLight->SetIntensity(15000.0f * (1.0f - Alpha));
		if (Alpha >= 1.0f)
		{
			bRespawning = false;
			SetCanBeDamaged(true);
			BagMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ShockCore->SetHiddenInGame(true);
			HitLight->SetIntensity(0.0f);
		}
		return;
	}

	MotionTime += DeltaSeconds;
	if (MotionMode != EChaosImpactTargetMotion::Stationary)
	{
		const float Wave = FMath::Sin((MotionTime * CyclesPerSecond + MotionPhase) * 2.0f * PI);
		const FVector Axis = MotionMode == EChaosImpactTargetMotion::SideToSide
			? GetActorRightVector() : GetActorForwardVector();
		SetActorLocation(HomeLocation + Axis * Wave * TravelDistance, true);
	}
}

float AChaosImpactTrainingTarget::TakeDamage(const float DamageAmount,
	const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!bTrainingEnabled || bDefeated || bRespawning || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Defeat(DamageCauser ? DamageCauser->GetActorLocation() : BagMesh->Bounds.Origin);
	return DamageAmount;
}

void AChaosImpactTrainingTarget::Defeat(const FVector& ImpactPoint)
{
	bDefeated = true;
	bRespawning = false;
	DefeatTime = 0.0f;
	ImpactLocalLocation = GetActorTransform().InverseTransformPosition(ImpactPoint);
	ImpactLocalLocation.X = FMath::Clamp(ImpactLocalLocation.X, -65.0f, 65.0f);
	ImpactLocalLocation.Y = FMath::Clamp(ImpactLocalLocation.Y, -65.0f, 65.0f);
	ImpactLocalLocation.Z = FMath::Clamp(ImpactLocalLocation.Z, 70.0f, 190.0f);
	SetCanBeDamaged(false);
	BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShockCore->SetHiddenInGame(false);
	HitLight->SetIntensity(22000.0f);

	for (UStaticMeshComponent* Piece : BurstPieces)
	{
		Piece->SetHiddenInGame(false);
	}

	GetWorldTimerManager().SetTimer(
		RespawnTimer, this, &AChaosImpactTrainingTarget::RespawnTarget, RespawnDelay, false);
}

void AChaosImpactTrainingTarget::RespawnTarget()
{
	bDefeated = false;
	bRespawning = true;
	DefeatTime = 0.0f;
	RespawnTime = 0.0f;
	MotionTime = 0.0f;
	SetActorLocation(HomeLocation, false);
	FallingAssembly->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -125.0f), FRotator::ZeroRotator);
	BaseMesh->SetRelativeScale3D(TargetBaseScale * FVector(0.18f, 0.18f, 0.04f));
	PoleMesh->SetRelativeScale3D(TargetPoleScale * FVector(0.18f, 0.18f, 0.04f));
	BagMesh->SetRelativeScale3D(TargetBagScale * FVector(0.18f, 0.18f, 0.04f));
	FacePlateMesh->SetRelativeScale3D(TargetPlateScale * FVector(0.18f, 0.18f, 0.04f));
	SetTargetVisible(bTrainingEnabled);
	SetCanBeDamaged(false);
	BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShockCore->SetHiddenInGame(false);
	ShockCore->SetRelativeLocation(FVector(0.0f, 0.0f, 8.0f));
	ShockCore->SetRelativeScale3D(FVector(0.05f));
	HitLight->SetIntensity(15000.0f);
	for (UStaticMeshComponent* Piece : BurstPieces)
	{
		Piece->SetHiddenInGame(true);
	}
}

void AChaosImpactTrainingTarget::SetTargetVisible(const bool bVisible)
{
	BaseMesh->SetHiddenInGame(!bVisible);
	PoleMesh->SetHiddenInGame(!bVisible);
	BagMesh->SetHiddenInGame(!bVisible);
	FacePlateMesh->SetHiddenInGame(true);
}
