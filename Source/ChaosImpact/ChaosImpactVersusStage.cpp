#include "ChaosImpactVersusStage.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void TintStageBlock(UStaticMeshComponent* Block, const FLinearColor& Color)
	{
		if (!Block)
		{
			return;
		}
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Block->GetMaterial(0));
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

	// Floor-surface offsets from the stage centre. Every point keeps clear of the cover blocks.
	const FVector StageSpawnOffsets[] =
	{
		// The corners behind the shelters hold the warp pads (child actors of BP_VersusStage).
		FVector(2650.0f, 0.0f, 0.0f), FVector(-2650.0f, 0.0f, 0.0f),
		FVector(0.0f, 2650.0f, 0.0f), FVector(0.0f, -2650.0f, 0.0f),
		FVector(1300.0f, 2650.0f, 0.0f), FVector(-1300.0f, -2650.0f, 0.0f),
		FVector(2650.0f, -1300.0f, 0.0f), FVector(-2650.0f, 1300.0f, 0.0f),
		FVector(-1300.0f, 2650.0f, 0.0f), FVector(1300.0f, -2650.0f, 0.0f),
		FVector(2650.0f, 1300.0f, 0.0f), FVector(-2650.0f, -1300.0f, 0.0f)
	};

	const FVector StageBallOffsets[] =
	{
		FVector(0.0f, 0.0f, 40.0f),
		FVector(1300.0f, 0.0f, 0.0f), FVector(-1300.0f, 0.0f, 0.0f),
		FVector(0.0f, 1300.0f, 0.0f), FVector(0.0f, -1300.0f, 0.0f),
		FVector(1850.0f, 1850.0f, 0.0f), FVector(-1850.0f, -1850.0f, 0.0f),
		FVector(1850.0f, -1850.0f, 0.0f), FVector(-1850.0f, 1850.0f, 0.0f)
	};
}

AChaosImpactVersusStage::AChaosImpactVersusStage()
{
	PrimaryActorTick.bCanEverTick = false;
	// Clients move their own characters, so they need the walls themselves, not just the server.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.0f);
	SpawnPointOffsets.Append(StageSpawnOffsets, UE_ARRAY_COUNT(StageSpawnOffsets));
	BallPointOffsets.Append(StageBallOffsets, UE_ARRAY_COUNT(StageBallOffsets));
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	constexpr float Half = HalfExtent;
	FloorBlock = CreateBlock(TEXT("StageFloor"), FVector(0.0f, 0.0f, -12.0f), FVector(Half * 2.0f, Half * 2.0f, 24.0f));

	// Painted lanes and centre court; no collision.
	MarkingBlocks.Add(CreateBlock(TEXT("LaneX"), FVector(0.0f, 0.0f, 0.6f), FVector(Half * 2.0f, 140.0f, 1.2f),
		FRotator::ZeroRotator, false));
	MarkingBlocks.Add(CreateBlock(TEXT("LaneY"), FVector(0.0f, 0.0f, 0.7f), FVector(140.0f, Half * 2.0f, 1.2f),
		FRotator::ZeroRotator, false));
	MarkingBlocks.Add(CreateBlock(TEXT("CentreCourt"), FVector(0.0f, 0.0f, 0.5f), FVector(1700.0f, 1700.0f, 1.0f),
		FRotator::ZeroRotator, false));

	BoundaryBlocks.Add(CreateBlock(TEXT("NorthWall"), FVector(0.0f, Half + 15.0f, 160.0f), FVector(Half * 2.0f + 60.0f, 30.0f, 320.0f)));
	BoundaryBlocks.Add(CreateBlock(TEXT("SouthWall"), FVector(0.0f, -Half - 15.0f, 160.0f), FVector(Half * 2.0f + 60.0f, 30.0f, 320.0f)));
	BoundaryBlocks.Add(CreateBlock(TEXT("EastWall"), FVector(Half + 15.0f, 0.0f, 160.0f), FVector(30.0f, Half * 2.0f, 320.0f)));
	BoundaryBlocks.Add(CreateBlock(TEXT("WestWall"), FVector(-Half - 15.0f, 0.0f, 160.0f), FVector(30.0f, Half * 2.0f, 320.0f)));

	// Low centre deck with a step on each side.
	DeckBlocks.Add(CreateBlock(TEXT("CentreDeck"), FVector(0.0f, 0.0f, 20.0f), FVector(1000.0f, 1000.0f, 40.0f)));
	DeckBlocks.Add(CreateBlock(TEXT("DeckStepN"), FVector(0.0f, 620.0f, 10.0f), FVector(500.0f, 240.0f, 20.0f)));
	DeckBlocks.Add(CreateBlock(TEXT("DeckStepS"), FVector(0.0f, -620.0f, 10.0f), FVector(500.0f, 240.0f, 20.0f)));
	DeckBlocks.Add(CreateBlock(TEXT("DeckStepE"), FVector(620.0f, 0.0f, 10.0f), FVector(240.0f, 500.0f, 20.0f)));
	DeckBlocks.Add(CreateBlock(TEXT("DeckStepW"), FVector(-620.0f, 0.0f, 10.0f), FVector(240.0f, 500.0f, 20.0f)));

	// Bank-shot walls around the deck, tangent to it.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float SX = Index == 0 || Index == 3 ? 1.0f : -1.0f;
		const float SY = Index < 2 ? 1.0f : -1.0f;
		BankWallBlocks.Add(CreateBlock(*FString::Printf(TEXT("BankWall%d"), Index),
			FVector(SX * 1080.0f, SY * 1080.0f, 90.0f), FVector(620.0f, 46.0f, 180.0f),
			FRotator(0.0f, SX * SY > 0.0f ? -45.0f : 45.0f, 0.0f)));
	}

	PillarBlocks.Add(CreateBlock(TEXT("PillarN"), FVector(0.0f, 1750.0f, 130.0f), FVector(240.0f, 240.0f, 260.0f)));
	PillarBlocks.Add(CreateBlock(TEXT("PillarS"), FVector(0.0f, -1750.0f, 130.0f), FVector(240.0f, 240.0f, 260.0f)));
	PillarBlocks.Add(CreateBlock(TEXT("PillarE"), FVector(1750.0f, 0.0f, 130.0f), FVector(240.0f, 240.0f, 260.0f)));
	PillarBlocks.Add(CreateBlock(TEXT("PillarW"), FVector(-1750.0f, 0.0f, 130.0f), FVector(240.0f, 240.0f, 260.0f)));

	// L-shaped shelters that open towards each corner.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float SX = Index == 0 || Index == 3 ? 1.0f : -1.0f;
		const float SY = Index < 2 ? 1.0f : -1.0f;
		ShelterBlocks.Add(CreateBlock(*FString::Printf(TEXT("ShelterX%d"), Index),
			FVector(SX * 1830.0f, SY * 1550.0f, 105.0f), FVector(620.0f, 60.0f, 210.0f)));
		ShelterBlocks.Add(CreateBlock(*FString::Printf(TEXT("ShelterY%d"), Index),
			FVector(SX * 1550.0f, SY * 1830.0f, 105.0f), FVector(60.0f, 620.0f, 210.0f)));
	}

	// Jumpable cover along the outer ring.
	LowCoverBlocks.Add(CreateBlock(TEXT("LowCoverE"), FVector(2300.0f, -750.0f, 47.5f), FVector(380.0f, 80.0f, 95.0f)));
	LowCoverBlocks.Add(CreateBlock(TEXT("LowCoverW"), FVector(-2300.0f, 750.0f, 47.5f), FVector(380.0f, 80.0f, 95.0f)));
	LowCoverBlocks.Add(CreateBlock(TEXT("LowCoverN"), FVector(750.0f, 2300.0f, 47.5f), FVector(80.0f, 380.0f, 95.0f)));
	LowCoverBlocks.Add(CreateBlock(TEXT("LowCoverS"), FVector(-750.0f, -2300.0f, 47.5f), FVector(80.0f, 380.0f, 95.0f)));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatMaterial(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	if (FlatMaterial.Succeeded())
	{
		TArray<UStaticMeshComponent*> Components;
		GetComponents(Components);
		for (UStaticMeshComponent* Block : Components)
		{
			Block->SetMaterial(0, FlatMaterial.Object);
		}
	}
}

UStaticMeshComponent* AChaosImpactVersusStage::CreateBlock(const FName& Name, const FVector& Location,
	const FVector& Size, const FRotator& Rotation, const bool bCollision)
{
	UStaticMeshComponent* Block = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Block->SetupAttachment(SceneRoot);
	Block->SetRelativeLocation(Location);
	Block->SetRelativeRotation(Rotation);
	Block->SetRelativeScale3D(Size / 100.0f);
	if (bCollision)
	{
		Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Block->SetCollisionObjectType(ECC_WorldStatic);
		Block->SetCollisionResponseToAllChannels(ECR_Block);
		Block->CanCharacterStepUpOn = ECB_Yes;
	}
	else
	{
		Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Block->SetCastShadow(false);
	}
	Block->SetGenerateOverlapEvents(false);
	Block->bEditableWhenInherited = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeAsset.Succeeded())
	{
		Block->SetStaticMesh(CubeAsset.Object);
	}
	return Block;
}

void AChaosImpactVersusStage::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyStageColors();
}

void AChaosImpactVersusStage::BeginPlay()
{
	Super::BeginPlay();
	ApplyStageColors();
}

void AChaosImpactVersusStage::ApplyStageColors()
{
	if (!bApplyDefaultColors)
	{
		return;
	}
	TintStageBlock(FloorBlock, FLinearColor(0.012f, 0.02f, 0.05f));
	const FLinearColor MarkingColors[] =
	{
		FLinearColor(0.03f, 0.06f, 0.12f), FLinearColor(0.03f, 0.06f, 0.12f), FLinearColor(0.02f, 0.09f, 0.16f)
	};
	for (int32 Index = 0; Index < MarkingBlocks.Num(); ++Index)
	{
		TintStageBlock(MarkingBlocks[Index], MarkingColors[Index % UE_ARRAY_COUNT(MarkingColors)]);
	}
	for (UStaticMeshComponent* Block : BoundaryBlocks)
	{
		TintStageBlock(Block, FLinearColor(0.015f, 0.07f, 0.14f));
	}
	for (int32 Index = 0; Index < DeckBlocks.Num(); ++Index)
	{
		TintStageBlock(DeckBlocks[Index], Index == 0 ? FLinearColor(0.04f, 0.13f, 0.23f) : FLinearColor(0.02f, 0.55f, 0.9f));
	}
	for (int32 Index = 0; Index < BankWallBlocks.Num(); ++Index)
	{
		TintStageBlock(BankWallBlocks[Index], Index % 2 == 0 ? FLinearColor(0.88f, 0.025f, 0.075f)
			: FLinearColor(0.0f, 0.42f, 0.85f));
	}
	for (UStaticMeshComponent* Block : PillarBlocks)
	{
		TintStageBlock(Block, FLinearColor(0.9f, 0.52f, 0.04f));
	}
	for (int32 Index = 0; Index < ShelterBlocks.Num(); ++Index)
	{
		TintStageBlock(ShelterBlocks[Index], (Index / 2) % 2 == 0 ? FLinearColor(0.0f, 0.42f, 0.85f)
			: FLinearColor(0.88f, 0.025f, 0.075f));
	}
	for (UStaticMeshComponent* Block : LowCoverBlocks)
	{
		TintStageBlock(Block, FLinearColor(0.32f, 0.12f, 0.7f));
	}
}

TArray<FVector> AChaosImpactVersusStage::GetSpawnPoints() const
{
	TArray<FVector> Points;
	for (const FVector& Offset : SpawnPointOffsets)
	{
		Points.Add(GetActorTransform().TransformPosition(Offset));
	}
	return Points;
}

TArray<FVector> AChaosImpactVersusStage::GetBallPoints() const
{
	TArray<FVector> Points;
	for (const FVector& Offset : BallPointOffsets)
	{
		Points.Add(GetActorTransform().TransformPosition(Offset));
	}
	return Points;
}
