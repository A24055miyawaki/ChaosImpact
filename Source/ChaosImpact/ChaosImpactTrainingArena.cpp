// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactTrainingArena.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AChaosImpactTrainingArena::AChaosImpactTrainingArena()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// A large multi-zone proving ground: open center, long-range lane, rebound
	// maze, slalom route and two elevations. The camera follows the player, so the
	// arena can be much wider than a single screen without sacrificing readability.
	FloorBlock = CreateBlock(TEXT("TrainingFloor"), FVector(0.0f, 0.0f, -12.0f),
		FVector(7600.0f, 5200.0f, 24.0f));
	ZoneBlocks.Add(CreateZone(TEXT("CentralCourtZone"), FVector(0.0f, 0.0f, 1.0f),
		FVector(1900.0f, 1550.0f, 2.0f)));
	ZoneBlocks.Add(CreateZone(TEXT("LongRangeZone"), FVector(2570.0f, 0.0f, 1.5f),
		FVector(2050.0f, 1800.0f, 3.0f)));
	ZoneBlocks.Add(CreateZone(TEXT("ReboundMazeZone"), FVector(-2350.0f, 0.0f, 1.0f),
		FVector(2100.0f, 1900.0f, 2.0f)));

	WallBlocks.Add(CreateBlock(TEXT("NorthBoundary"), FVector(0.0f, 2615.0f, 160.0f),
		FVector(7640.0f, 30.0f, 320.0f)));
	WallBlocks.Add(CreateBlock(TEXT("SouthBoundary"), FVector(0.0f, -2615.0f, 160.0f),
		FVector(7640.0f, 30.0f, 320.0f)));
	WallBlocks.Add(CreateBlock(TEXT("EastBoundary"), FVector(3815.0f, 0.0f, 160.0f),
		FVector(30.0f, 5200.0f, 320.0f)));
	WallBlocks.Add(CreateBlock(TEXT("WestBoundary"), FVector(-3815.0f, 0.0f, 160.0f),
		FVector(30.0f, 5200.0f, 320.0f)));

	// Long-range backstop and central bank-shot walls.
	WallBlocks.Add(CreateBlock(TEXT("RangeBackstop"), FVector(3300.0f, 0.0f, 145.0f),
		FVector(45.0f, 1900.0f, 290.0f)));
	WallBlocks.Add(CreateBlock(TEXT("CenterBankWallA"), FVector(900.0f, 520.0f, 105.0f),
		FVector(720.0f, 46.0f, 210.0f), FRotator(0.0f, 20.0f, 0.0f)));
	WallBlocks.Add(CreateBlock(TEXT("CenterBankWallB"), FVector(900.0f, -560.0f, 105.0f),
		FVector(720.0f, 46.0f, 210.0f), FRotator(0.0f, -20.0f, 0.0f)));

	// West rebound maze: wide enough to run through, tight enough to test AI routing.
	WallBlocks.Add(CreateBlock(TEXT("MazeWallA"), FVector(-1150.0f, 260.0f, 105.0f),
		FVector(46.0f, 760.0f, 210.0f), FRotator(0.0f, 8.0f, 0.0f)));
	WallBlocks.Add(CreateBlock(TEXT("MazeWallB"), FVector(-1900.0f, -620.0f, 105.0f),
		FVector(720.0f, 46.0f, 210.0f), FRotator(0.0f, -18.0f, 0.0f)));
	WallBlocks.Add(CreateBlock(TEXT("MazeWallC"), FVector(-2600.0f, 360.0f, 105.0f),
		FVector(46.0f, 820.0f, 210.0f), FRotator(0.0f, -10.0f, 0.0f)));
	WallBlocks.Add(CreateBlock(TEXT("MazeWallD"), FVector(-3050.0f, -780.0f, 105.0f),
		FVector(620.0f, 46.0f, 210.0f), FRotator(0.0f, 16.0f, 0.0f)));

	// South-east slalom and jumpable cover.
	WallBlocks.Add(CreateBlock(TEXT("SlalomWallA"), FVector(1600.0f, -1150.0f, 90.0f),
		FVector(50.0f, 560.0f, 180.0f)));
	WallBlocks.Add(CreateBlock(TEXT("SlalomWallB"), FVector(2250.0f, -650.0f, 90.0f),
		FVector(50.0f, 560.0f, 180.0f)));
	WallBlocks.Add(CreateBlock(TEXT("JumpCoverEast"), FVector(1100.0f, -1700.0f, 52.5f),
		FVector(360.0f, 85.0f, 105.0f), FRotator(0.0f, -15.0f, 0.0f)));
	WallBlocks.Add(CreateBlock(TEXT("JumpCoverWest"), FVector(-500.0f, -1500.0f, 42.5f),
		FVector(300.0f, 85.0f, 85.0f), FRotator(0.0f, 14.0f, 0.0f)));

	// Four climbable steps lead to a broad high-ground firing deck.
	StepBlocks.Add(CreateBlock(TEXT("HighStep01"), FVector(1530.0f, 1660.0f, 15.0f),
		FVector(150.0f, 300.0f, 30.0f)));
	StepBlocks.Add(CreateBlock(TEXT("HighStep02"), FVector(1660.0f, 1660.0f, 30.0f),
		FVector(110.0f, 300.0f, 60.0f)));
	StepBlocks.Add(CreateBlock(TEXT("HighStep03"), FVector(1760.0f, 1660.0f, 45.0f),
		FVector(90.0f, 300.0f, 90.0f)));
	StepBlocks.Add(CreateBlock(TEXT("HighStep04"), FVector(1840.0f, 1660.0f, 60.0f),
		FVector(80.0f, 300.0f, 120.0f)));
	PlatformBlocks.Add(CreateBlock(TEXT("HighGroundDeck"), FVector(2250.0f, 1660.0f, 60.0f),
		FVector(760.0f, 660.0f, 120.0f)));

	// A lower west deck is approachable from either side and useful for drop shots.
	StepBlocks.Add(CreateBlock(TEXT("WestLowStepA"), FVector(-1880.0f, -1660.0f, 20.0f),
		FVector(180.0f, 300.0f, 40.0f)));
	StepBlocks.Add(CreateBlock(TEXT("WestLowStepB"), FVector(-2030.0f, -1660.0f, 40.0f),
		FVector(120.0f, 300.0f, 80.0f)));
	PlatformBlocks.Add(CreateBlock(TEXT("WestLowDeck"), FVector(-2450.0f, -1660.0f, 40.0f),
		FVector(720.0f, 600.0f, 80.0f)));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatMaterial(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	if (FlatMaterial.Succeeded())
	{
		TArray<UStaticMeshComponent*> Blocks;
		Blocks.Add(FloorBlock);
		for (UStaticMeshComponent* Block : WallBlocks) { Blocks.Add(Block); }
		for (UStaticMeshComponent* Block : StepBlocks) { Blocks.Add(Block); }
		for (UStaticMeshComponent* Block : PlatformBlocks) { Blocks.Add(Block); }
		for (UStaticMeshComponent* Block : ZoneBlocks) { Blocks.Add(Block); }
		for (UStaticMeshComponent* Block : Blocks)
		{
			Block->SetMaterial(0, FlatMaterial.Object);
		}
	}
}

void AChaosImpactTrainingArena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Construction runs in the level editor as well as for spawned actors, so
	// designers see the same color-coded lanes, tiles and walls before pressing Play.
	ApplyArenaColors();
}

UStaticMeshComponent* AChaosImpactTrainingArena::CreateZone(const FName& Name,
	const FVector& Location, const FVector& Size)
{
	UStaticMeshComponent* Zone = CreateBlock(Name, Location, Size);
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->SetGenerateOverlapEvents(false);
	return Zone;
}

UStaticMeshComponent* AChaosImpactTrainingArena::CreateBlock(const FName& Name,
	const FVector& Location, const FVector& Size, const FRotator& Rotation)
{
	UStaticMeshComponent* Block = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Block->SetupAttachment(SceneRoot);
	Block->SetRelativeLocation(Location);
	Block->SetRelativeRotation(Rotation);
	Block->SetRelativeScale3D(Size / 100.0f);
	Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Block->SetCollisionObjectType(ECC_WorldStatic);
	Block->SetCollisionResponseToAllChannels(ECR_Block);
	Block->SetGenerateOverlapEvents(false);
	Block->CanCharacterStepUpOn = ECB_Yes;
	Block->SetCastShadow(true);
	Block->bEditableWhenInherited = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		Block->SetStaticMesh(CubeAsset.Object);
	}
	return Block;
}

void AChaosImpactTrainingArena::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld() || !GetWorld()->URL.HasOption(TEXT("CITraining=1")))
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}

	ApplyArenaColors();
}

void AChaosImpactTrainingArena::ApplyArenaColors()
{
	TintBlock(FloorBlock, FLinearColor(0.012f, 0.018f, 0.045f));
	for (int32 Index = 0; Index < WallBlocks.Num(); ++Index)
	{
		TintBlock(WallBlocks[Index], Index < 4
			? FLinearColor(0.015f, 0.07f, 0.14f)
			: Index % 2 == 0 ? FLinearColor(0.0f, 0.42f, 0.85f)
				: FLinearColor(0.88f, 0.025f, 0.075f));
	}
	for (UStaticMeshComponent* Block : StepBlocks)
	{
		TintBlock(Block, FLinearColor(0.02f, 0.55f, 0.9f));
	}
	for (UStaticMeshComponent* Block : PlatformBlocks)
	{
		TintBlock(Block, FLinearColor(0.04f, 0.13f, 0.23f));
	}
	for (int32 Index = 0; Index < ZoneBlocks.Num(); ++Index)
	{
		const FLinearColor ZoneColors[] =
		{
			FLinearColor(0.015f, 0.09f, 0.16f),
			FLinearColor(0.12f, 0.025f, 0.045f),
			FLinearColor(0.025f, 0.075f, 0.12f)
		};
		TintBlock(ZoneBlocks[Index], ZoneColors[Index % UE_ARRAY_COUNT(ZoneColors)]);
	}
}

void AChaosImpactTrainingArena::TintBlock(
	UStaticMeshComponent* Block, const FLinearColor& Color)
{
	if (Block)
	{
		UMaterialInstanceDynamic* Material =
			Cast<UMaterialInstanceDynamic>(Block->GetMaterial(0));
		if (!Material)
		{
			Material = Block->CreateDynamicMaterialInstance(0);
		}
		if (Material)
		{
			Material->SetVectorParameterValue(TEXT("Base Color"), Color);
			Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Material->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}
