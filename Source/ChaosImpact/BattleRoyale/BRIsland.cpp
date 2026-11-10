#include "BRIsland.h"
#include "BRIslandLayout.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Async/ParallelFor.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"
#include "ChaosImpact.h"

namespace
{
	const TCHAR* const BRShapePaths[] = {TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		TEXT("/Engine/BasicShapes/Sphere.Sphere"), TEXT("/Engine/BasicShapes/Cone.Cone")};
	const TCHAR* const BRMatPaths[] = {TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Surface.M_BR_Surface"),
		TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_SurfaceFade.M_BR_SurfaceFade"),
		TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Metal.M_BR_Metal"),
		TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Leaves.M_BR_Leaves"),
		TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Glow.M_BR_Glow")};
	/** A terrain chunk's side in cells, and the cells between a chunk's rough vertices. */
	constexpr int32 BRChunkCells = 24;
	/** Tops lower than this (cm above the ground) are stepped over rather than bumped into. */
	constexpr float BRStepOver = 45.0f;
	constexpr int32 BRCoarseStep = 4;
}

ABRIsland::ABRIsland()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	GetRootComponent()->SetMobility(EComponentMobility::Static);
}

void ABRIsland::BeginPlay()
{
	Super::BeginPlay();
	const double Started = FPlatformTime::Seconds();
	Build();
	int32 Pieces = 0;
	for (const TPair<uint32, FSceneryCell>& Cell : Scenery)
	{
		for (const TPair<uint32, FBatch>& Batch : Cell.Value.Batches) { Pieces += Batch.Value.Pieces.Num(); }
	}
	UE_LOG(LogChaosImpact, Log, TEXT("BRIsland built in %.2f s (%d chunks, %d scenery squares, %d pieces)"), FPlatformTime::Seconds() - Started, Chunks.Num(), Scenery.Num(), Pieces);
}

void ABRIsland::Build()
{
	TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Terrain.M_BR_Terrain"));
	SeaMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/Materials/M_BR_Sea.M_BR_Sea"));
	for (int32 Index = 0; Index < static_cast<int32>(EBRMat::Count); ++Index)
	{
		Materials[Index] = LoadObject<UMaterialInterface>(nullptr, BRMatPaths[Index]);
	}
	BuildTerrain();
	BuildSea();
	BuildShoreBarrier();
	BuildCapital();
	BuildStadium();
	BuildFactory();
	BuildPort();
	BuildAirfield();
	BuildLab();
	BuildTown(BRLayout::Town, 11);
	BuildTown(BRLayout::Ridgetown, 12);
	BuildFarm(BRLayout::Farm, 21);
	BuildFarm(BRLayout::Westfarm, 22);
	BuildCanyon();
	BuildMountain();
	BuildForest();
	BuildCrater();
	BuildFunfair();
	BuildRiver();
	BuildScatter();
	BuildDressing();
	Occupied.Empty();
}

void ABRIsland::BuildChunkSection(const int32 FromX, const int32 FromY, const int32 Step, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
	TArray<FVector2D>& UV0, TArray<FVector2D>& UV1, TArray<FVector2D>& UV2, TArray<FVector2D>& UV3, TArray<FLinearColor>& Colours, TArray<FProcMeshTangent>& Tangents) const
{
	const int32 W = BRChunkCells / Step + 1;
	const float Span = BRMap::CellSize * Step;
	const float Left = -BRMap::HalfMap + FromX * BRMap::CellSize, Bottom = -BRMap::HalfMap + FromY * BRMap::CellSize;
	// One height for each corner, padded by one so the normals at the edges match the next chunk's.
	const int32 Padded = W + 2;
	TArray<float> Heights;
	Heights.SetNumUninitialized(Padded * Padded);
	ParallelFor(Padded, [&](const int32 Y)
	{
		for (int32 X = 0; X < Padded; ++X)
		{
			Heights[Y * Padded + X] = BRMap::HeightAt(Left + (X - 1) * Span, Bottom + (Y - 1) * Span);
		}
	});
	const auto H = [&Heights, Padded](const int32 X, const int32 Y) { return Heights[(Y + 1) * Padded + X + 1]; };

	// The grid, then a skirt hanging from its edge so a neighbour at another detail never shows a crack.
	const int32 Grid = W * W;
	const int32 Edge = (W - 1) * 4;
	const int32 Count = Grid + Edge;
	Vertices.SetNumUninitialized(Count); Normals.SetNumUninitialized(Count);
	UV0.SetNumUninitialized(Count); UV1.SetNumUninitialized(Count); UV2.SetNumUninitialized(Count); UV3.SetNumUninitialized(Count);
	Colours.SetNumUninitialized(Count); Tangents.SetNumUninitialized(Count);
	ParallelFor(W, [&](const int32 Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Y * W + X;
			const float WX = Left + X * Span, WY = Bottom + Y * Span, Height = H(X, Y);
			Vertices[I] = FVector(WX, WY, Height);
			const FVector Normal = FVector(H(X - 1, Y) - H(X + 1, Y), H(X, Y - 1) - H(X, Y + 1), 2.0f * Span).GetSafeNormal();
			Normals[I] = Normal;
			Tangents[I] = FProcMeshTangent(FVector(Normal.Z, 0.0f, -Normal.X).GetSafeNormal(), false);
			const BRMap::FPaint Paint = BRMap::PaintAt(WX, WY, Height);
			// As the terrain material reads them: colour = dirt, paving, sand, moss; UV1 = concrete, snow; UV2 = ash, dry; UV3 = marsh.
			Colours[I] = FLinearColor(Paint.Dirt, Paint.Paved, Paint.Sand, Paint.Moss);
			UV0[I] = FVector2D(WX, WY) / 500.0;
			UV1[I] = FVector2D(Paint.Concrete, Paint.Snow);
			UV2[I] = FVector2D(Paint.Ash, Paint.Dry);
			UV3[I] = FVector2D(Paint.Marsh, 0.0);
		}
	});
	Triangles.Reset((W - 1) * (W - 1) * 6 + Edge * 6);
	for (int32 Y = 0; Y + 1 < W; ++Y)
	{
		for (int32 X = 0; X + 1 < W; ++X)
		{
			const int32 A = Y * W + X, B = A + 1, C = A + W, D = C + 1;
			Triangles.Append({A, C, B, B, C, D});
		}
	}
	// Round the edge, a corner at a time.
	const auto EdgeIndex = [W](const int32 Along)
	{
		const int32 Side = W - 1;
		if (Along < Side) { return Along; }
		if (Along < Side * 2) { return (Along - Side) * W + Side; }
		if (Along < Side * 3) { return Side * W + (Side - (Along - Side * 2)); }
		return (Side - (Along - Side * 3)) * W;
	};
	for (int32 Along = 0; Along < Edge; ++Along)
	{
		const int32 Top = EdgeIndex(Along);
		const int32 Low = Grid + Along;
		Vertices[Low] = Vertices[Top] - FVector(0.0, 0.0, 700.0);
		Normals[Low] = Normals[Top]; Tangents[Low] = Tangents[Top]; Colours[Low] = Colours[Top];
		UV0[Low] = UV0[Top]; UV1[Low] = UV1[Top]; UV2[Low] = UV2[Top]; UV3[Low] = UV3[Top];
		const int32 NextTop = EdgeIndex((Along + 1) % Edge);
		const int32 NextLow = Grid + (Along + 1) % Edge;
		// (Both windings: which way it faces depends on the side.)
		Triangles.Append({Top, NextTop, Low, Low, NextTop, NextLow});
	}
}

void ABRIsland::BuildTerrain()
{
	const int32 Cells = FMath::CeilToInt(BRMap::HalfMap * 2.0f / BRMap::CellSize);
	for (int32 ChunkY = 0; ChunkY * BRChunkCells < Cells; ++ChunkY)
	{
		for (int32 ChunkX = 0; ChunkX * BRChunkCells < Cells; ++ChunkX)
		{
			const int32 FromX = ChunkX * BRChunkCells, FromY = ChunkY * BRChunkCells;
			// Open sea has nothing to draw or stand on.
			const float CX = -BRMap::HalfMap + (FromX + BRChunkCells * 0.5f) * BRMap::CellSize;
			const float CY = -BRMap::HalfMap + (FromY + BRChunkCells * 0.5f) * BRMap::CellSize;
			if (BRMap::ShoreDistance(CX, CY) < -8000.0f) { continue; }
			TArray<FVector> Vertices, Normals;
			TArray<FVector2D> UV0, UV1, UV2, UV3;
			TArray<FLinearColor> Colours;
			TArray<FProcMeshTangent> Tangents;
			TArray<int32> Triangles;
			BuildChunkSection(FromX, FromY, BRCoarseStep, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colours, Tangents);
			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
			Mesh->SetupAttachment(GetRootComponent());
			Mesh->bUseAsyncCooking = false;
			Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colours, Tangents, false, false);
			Mesh->SetMaterial(0, TerrainMaterial);
			Mesh->SetMaterial(1, TerrainMaterial);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionObjectType(ECC_WorldStatic);
			Mesh->SetCollisionResponseToAllChannels(ECR_Block);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->RegisterComponent();
			TerrainChunks.Add(Mesh);
			Chunks.Add({Mesh, FromX, FromY, false});
		}
	}
}

void ABRIsland::LoadFineChunk(const int32 Index)
{
	FTerrainChunk& Chunk = Chunks[Index];
	if (Chunk.bFine) { return; }
	TArray<FVector> Vertices, Normals;
	TArray<FVector2D> UV0, UV1, UV2, UV3;
	TArray<FLinearColor> Colours;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	BuildChunkSection(Chunk.FromX, Chunk.FromY, 1, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colours, Tangents);
	Chunk.Mesh->CreateMeshSection_LinearColor(1, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colours, Tangents, true, false);
	Chunk.Mesh->SetMeshSectionVisible(0, false);
	Chunk.bFine = true;
}

void ABRIsland::UnloadFineChunk(const int32 Index)
{
	FTerrainChunk& Chunk = Chunks[Index];
	if (!Chunk.bFine) { return; }
	Chunk.Mesh->ClearMeshSection(1);
	Chunk.Mesh->SetMeshSectionVisible(0, true);
	Chunk.bFine = false;
}

void ABRIsland::GatherFoci(TArray<FVector2D>& Out) const
{
	// Everyone here who is a real body in the world, and wherever a person here is looking from.
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		Out.Add(FVector2D(It->GetActorLocation()));
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		if (Player && Player->IsLocalController() && Player->PlayerCameraManager)
		{
			Out.Add(FVector2D(Player->PlayerCameraManager->GetCameraLocation()));
		}
	}
	Out.Append(ExtraFoci);
}

void ABRIsland::StreamAround(const bool bForce)
{
	TArray<FVector2D> Foci;
	GatherFoci(Foci);
	const auto Nearest = [&Foci](const FVector2D& At)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector2D& Focus : Foci) { Best = FMath::Min(Best, FVector2D::DistSquared(At, Focus)); }
		return Best;
	};
	// ---- The fine ground.
	constexpr double LoadRange = 14000.0, DropRange = 21000.0;
	TArray<TPair<double, int32>> Wanted;
	for (int32 Index = 0; Index < Chunks.Num(); ++Index)
	{
		FTerrainChunk& Chunk = Chunks[Index];
		const FVector2D Centre(-BRMap::HalfMap + (Chunk.FromX + BRChunkCells * 0.5) * BRMap::CellSize, -BRMap::HalfMap + (Chunk.FromY + BRChunkCells * 0.5) * BRMap::CellSize);
		const double D = Nearest(Centre);
		if (!Chunk.bFine && D < LoadRange * LoadRange) { Wanted.Add({D, Index}); }
		else if (Chunk.bFine && D > DropRange * DropRange) { UnloadFineChunk(Index); }
	}
	Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (int32 I = 0; I < Wanted.Num() && (bForce || I < 2); ++I) { LoadFineChunk(Wanted[I].Value); }

	// ---- The scenery: a square's batches become components, a few each frame.
	constexpr double SceneryLoad = 26000.0, SceneryDrop = 36000.0;
	int32 Budget = bForce ? TNumericLimits<int32>::Max() : 5;
	for (TPair<uint32, FSceneryCell>& Pair : Scenery)
	{
		const int32 CX = static_cast<int16>(Pair.Key >> 16), CY = static_cast<int16>(Pair.Key & 0xFFFFu);
		const FVector2D Centre((CX + 0.5) * SceneryCellSize, (CY + 0.5) * SceneryCellSize);
		const double D = Nearest(Centre);
		if (D < SceneryLoad * SceneryLoad)
		{
			for (TPair<uint32, FBatch>& Batch : Pair.Value.Batches)
			{
				if (!Batch.Value.Component && Budget > 0) { LoadSceneryPart(Pair.Key, Batch.Key); --Budget; }
			}
		}
		else if (D > SceneryDrop * SceneryDrop)
		{
			for (TPair<uint32, FBatch>& Batch : Pair.Value.Batches)
			{
				if (Batch.Value.Component)
				{
					BatchComponents.Remove(Batch.Value.Component);
					Batch.Value.Component->DestroyComponent();
					Batch.Value.Component = nullptr;
				}
			}
		}
	}
}

void ABRIsland::DevCheckSolid(const FVector2D& Around, const float Radius) const
{
	// Shoots a ray at each solid piece near the spot from just outside it, and says how many let it through.
	int32 Checked[4] = {0, 0, 0, 0}, Missed[4] = {0, 0, 0, 0};
	int32 SolidTotal = 0, PassTotal = 0;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BRSolidCheck), false);
	for (const TPair<uint32, FSceneryCell>& Cell : Scenery)
	{
		for (const TPair<uint32, FBatch>& Pair : Cell.Value.Batches)
		{
			const FBatch& Batch = Pair.Value;
			(Batch.bSolid ? SolidTotal : PassTotal) += Batch.Pieces.Num();
			if (!Batch.bSolid || !Batch.Component) { continue; }
			for (const FBatchPiece& Item : Batch.Pieces)
			{
				const FVector At = Item.Where.GetLocation();
				if (FVector2D::Distance(FVector2D(At), Around) > Radius) { continue; }
				const FVector Dim = Item.Where.GetScale3D() * 100.0f;
				const FVector Across = Item.Where.GetRotation().RotateVector(Dim.X < Dim.Y ? FVector::ForwardVector : FVector::RightVector);
				const float Reach = static_cast<float>(FMath::Min(Dim.X, Dim.Y)) * 0.5f + 40.0f;
				FHitResult Hit;
				++Checked[static_cast<int32>(Batch.Shape)];
				if (!GetWorld()->LineTraceSingleByChannel(Hit, At + Across * Reach, At, ECC_Pawn, Query)) { ++Missed[static_cast<int32>(Batch.Shape)]; }
			}
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("BRTEST solid pieces on the island=%d passable=%d"), SolidTotal, PassTotal);
	for (int32 Shape = 0; Shape < 4; ++Shape)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST solid check shape %d: checked=%d let through=%d"), Shape, Checked[Shape], Missed[Shape]);
	}
}

void ABRIsland::EnsureLoadedAround(const FVector2D& At)
{
	ExtraFoci.Add(At);
	StreamAround(true);
	ExtraFoci.Remove(At);
}

void ABRIsland::LoadSceneryPart(const uint32 CellKey, const uint32 BatchKey)
{
	FBatch& Batch = Scenery[CellKey].Batches[BatchKey];
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this);
	Component->SetupAttachment(GetRootComponent());
	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, BRShapePaths[static_cast<int32>(Batch.Shape)]));
	Component->SetMaterial(0, Materials[static_cast<int32>(Batch.Mat)]);
	Component->NumCustomDataFloats = 4;
	Component->SetCollisionEnabled(Batch.bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (Batch.bSolid)
	{
		Component->SetCollisionObjectType(ECC_WorldStatic);
		Component->SetCollisionResponseToAllChannels(ECR_Block);
	}
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(Batch.bShadow);
	Component->RegisterComponent();
	TArray<FTransform> Transforms;
	Transforms.Reserve(Batch.Pieces.Num());
	for (const FBatchPiece& Item : Batch.Pieces) { Transforms.Add(Item.Where); }
	Component->AddInstances(Transforms, false, true);
	for (int32 Index = 0; Index < Batch.Pieces.Num(); ++Index)
	{
		const FBatchPiece& Item = Batch.Pieces[Index];
		const float Data[4] = {Item.Colour.R, Item.Colour.G, Item.Colour.B, Item.Pattern};
		Component->SetCustomData(Index, MakeArrayView(Data, 4), false);
	}
	Component->MarkRenderStateDirty();
	Batch.Component = Component;
	BatchComponents.Add(Component);
}

void ABRIsland::BuildSea()
{
	UStaticMeshComponent* Sea = NewObject<UStaticMeshComponent>(this);
	Sea->SetupAttachment(GetRootComponent());
	Sea->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	Sea->SetMaterial(0, SeaMaterial);
	Sea->SetWorldLocation(FVector::ZeroVector);
	Sea->SetWorldScale3D(FVector(5000.0f));
	Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sea->SetCastShadow(false);
	Sea->SetCanEverAffectNavigation(false);
	Sea->RegisterComponent();
}

void ABRIsland::BuildShoreBarrier()
{
	// An invisible wall just out in the shallows: the sea is scenery, never a place to be knocked into.
	constexpr int32 Steps = 720;
	FVector Previous = FVector::ZeroVector;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Angle = UE_TWO_PI * (Step % Steps) / Steps;
		const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle));
		// In from far out until the shore is at hand.
		float Radius = BRMap::HalfMap;
		while (Radius > 5000.0f && BRMap::ShoreDistance(Way.X * Radius, Way.Y * Radius) < -500.0f) { Radius -= 120.0f; }
		const FVector Here(Way.X * Radius, Way.Y * Radius, 0.0f);
		if (Step > 0)
		{
			const FVector Span = Here - Previous;
			UBoxComponent* Wall = NewObject<UBoxComponent>(this);
			Wall->SetupAttachment(GetRootComponent());
			Wall->SetBoxExtent(FVector(Span.Size() * 0.5f + 80.0f, 60.0f, 3000.0f));
			Wall->SetWorldLocationAndRotation((Here + Previous) * 0.5f + FVector(0.0f, 0.0f, 2500.0f), Span.Rotation());
			Wall->SetCollisionProfileName(TEXT("InvisibleWall"));
			Wall->SetCanEverAffectNavigation(false);
			Wall->RegisterComponent();
		}
		Previous = Here;
	}
}

// ---- The pieces ----

void ABRIsland::Piece(const EBRShape Shape, const EBRMat Mat, const FVector& At, const FVector& Size, const FRotator& Rotation,
	const FLinearColor& Colour, const float Pattern, const bool bWantSolid, const bool bShadow)
{
	// What can be seen stands in the way. Only leaves, light, and what is low enough to step over let you through.
	bool bSolid = bWantSolid, bClaim = bWantSolid;
	if (!bSolid && Mat != EBRMat::Leaves && Mat != EBRMat::Glow)
	{
		const FRotationMatrix Turned(Rotation);
		const float HalfZ = static_cast<float>(FMath::Abs(Turned.GetScaledAxis(EAxis::X).Z) * Size.X + FMath::Abs(Turned.GetScaledAxis(EAxis::Y).Z) * Size.Y + FMath::Abs(Turned.GetScaledAxis(EAxis::Z).Z) * Size.Z) * 0.5f;
		const float Ground = BRMap::HeightAt(At.X, At.Y);
		if (At.Z + HalfZ - Ground > BRStepOver)
		{
			bSolid = true;
			// (Something high up leaves the ground under it free for scenery.)
			bClaim = At.Z - HalfZ - Ground < 250.0f;
		}
	}
	const int32 CX = FMath::FloorToInt(At.X / SceneryCellSize), CY = FMath::FloorToInt(At.Y / SceneryCellSize);
	const uint32 CellKey = (static_cast<uint32>(static_cast<uint16>(CX)) << 16) | static_cast<uint16>(CY);
	const uint32 Key = (static_cast<uint32>(Shape) << 8) | (static_cast<uint32>(Mat) << 4) | (bSolid ? 2u : 0u) | (bShadow ? 1u : 0u);
	if (bClaim)
	{
		// Its footprint (the widest it can be at this turn), so later scenery keeps off it.
		const float HalfX = static_cast<float>(FMath::Abs(FRotationMatrix(Rotation).GetScaledAxis(EAxis::X).X) * Size.X + FMath::Abs(FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y).X) * Size.Y) * 0.5f;
		const float HalfY = static_cast<float>(FMath::Abs(FRotationMatrix(Rotation).GetScaledAxis(EAxis::X).Y) * Size.X + FMath::Abs(FRotationMatrix(Rotation).GetScaledAxis(EAxis::Y).Y) * Size.Y) * 0.5f;
		Claim(FVector2D(At), HalfX, HalfY);
	}
	FBatch& Batch = Scenery.FindOrAdd(CellKey).Batches.FindOrAdd(Key);
	Batch.Shape = Shape;
	Batch.Mat = Mat;
	Batch.bSolid = bSolid;
	Batch.bShadow = bShadow;
	Batch.Pieces.Add({FTransform(Rotation, At, Size / 100.0f), Colour, Pattern});
}

FVector ABRIsland::OnGround(const float X, const float Y, const float Lift) const
{
	return FVector(X, Y, BRMap::HeightAt(X, Y) + Lift);
}

void ABRIsland::Box(const FVector& At, const FVector& Size, const FLinearColor& Colour, const float Yaw, const float Pattern, const bool bSolid)
{
	Piece(EBRShape::Cube, EBRMat::Surface, At, Size, FRotator(0.0f, Yaw, 0.0f), Colour, Pattern, bSolid);
}

void ABRIsland::WallBox(const FVector& At, const FVector& Size, const FLinearColor& Colour, const float Yaw, const float Pattern)
{
	// Walls thin out where they would hide the player.
	Piece(EBRShape::Cube, EBRMat::Fade, At, Size, FRotator(0.0f, Yaw, 0.0f), Colour, Pattern, true);
}

void ABRIsland::Trim(const FVector& At, const FVector& Size, const FLinearColor& Colour, const float Yaw)
{
	Piece(EBRShape::Cube, EBRMat::Fade, At, Size, FRotator(0.0f, Yaw, 0.0f), Colour, 0.0f, false, false);
}

void ABRIsland::Glow(const FVector& At, const FVector& Size, const FLinearColor& Colour, const float Yaw, const EBRShape Shape)
{
	Piece(Shape, EBRMat::Glow, At, Size, FRotator(0.0f, Yaw, 0.0f), Colour, 0.0f, false, false);
}

void ABRIsland::Disc(const FVector& At, const float Radius, const float Height, const FLinearColor& Colour, const EBRMat Mat, const bool bSolid)
{
	Piece(EBRShape::Cylinder, Mat, At, FVector(Radius * 2.0f, Radius * 2.0f, Height), FRotator::ZeroRotator, Colour, 0.0f, bSolid);
}

void ABRIsland::Dome(const FVector& At, const float Radius, const FLinearColor& Colour, const EBRMat Mat, const bool bSolid)
{
	Piece(EBRShape::Sphere, Mat, At, FVector(Radius * 2.0f), FRotator::ZeroRotator, Colour, 0.0f, bSolid);
}

void ABRIsland::Beam(const FVector& From, const FVector& To, const float Width, const float Height, const FLinearColor& Colour, const EBRMat Mat, const bool bSolid)
{
	const FVector Span = To - From;
	Piece(EBRShape::Cube, Mat, (From + To) * 0.5f, FVector(Span.Size(), Width, Height), Span.Rotation(), Colour, 0.0f, bSolid);
}

void ABRIsland::Pole(const FVector& From, const FVector& To, const float Radius, const FLinearColor& Colour, const EBRMat Mat, const bool bSolid)
{
	const FVector Span = To - From;
	// (A cylinder stands along Z.)
	const FRotator Rotation = FRotationMatrix::MakeFromZ(Span.GetSafeNormal()).Rotator();
	Piece(EBRShape::Cylinder, Mat, (From + To) * 0.5f, FVector(Radius * 2.0f, Radius * 2.0f, Span.Size()), Rotation, Colour, 0.0f, bSolid);
}

void ABRIsland::House(const FVector& Centre, const FVector2D& Size, const float Height, const float Yaw, const FLinearColor& Wall, const FLinearColor& Roof,
	const int32 Doors, const bool bGable)
{
	constexpr float Thick = 40.0f, DoorWide = 340.0f;
	const FRotator Turn(0.0f, Yaw, 0.0f);
	const auto At = [&](const float X, const float Y, const float Z) { return Centre + Turn.RotateVector(FVector(X, Y, Z)); };
	const float HX = static_cast<float>(Size.X) * 0.5f, HY = static_cast<float>(Size.Y) * 0.5f;
	Box(At(0.0f, 0.0f, 8.0f), FVector(Size.X + 30.0f, Size.Y + 30.0f, 16.0f), FLinearColor(0.32f, 0.3f, 0.27f), Yaw);

	// One side: two lengths with a door between them when it has one, and a lintel over the door.
	const auto Side = [&](const bool bAlongY, const float Offset, const bool bDoor)
	{
		const float Length = bAlongY ? static_cast<float>(Size.Y) : static_cast<float>(Size.X);
		const auto Place = [&](const float Along, const float Z) { return bAlongY ? At(Offset, Along, Z) : At(Along, Offset, Z); };
		const auto Dim = [&](const float Along, const float Up) { return bAlongY ? FVector(Thick, Along, Up) : FVector(Along, Thick, Up); };
		if (bDoor)
		{
			const float Piece1 = (Length - DoorWide) * 0.5f;
			WallBox(Place(-(DoorWide + Piece1) * 0.5f, Height * 0.5f), Dim(Piece1, Height), Wall, Yaw);
			WallBox(Place((DoorWide + Piece1) * 0.5f, Height * 0.5f), Dim(Piece1, Height), Wall, Yaw);
			WallBox(Place(0.0f, Height - 40.0f), Dim(DoorWide, 80.0f), Wall, Yaw);
		}
		else
		{
			WallBox(Place(0.0f, Height * 0.5f), Dim(Length, Height), Wall, Yaw);
		}
		// Windows: dark panes with a light frame along the wall.
		const int32 Panes = FMath::FloorToInt(Length / 330.0f);
		for (int32 I = 0; I < Panes; ++I)
		{
			const float Along = -Length * 0.5f + (I + 0.5f) * Length / Panes;
			if (bDoor && FMath::Abs(Along) < DoorWide * 0.5f + 110.0f) { continue; }
			Trim(Place(Along, Height * 0.56f) + Turn.RotateVector(bAlongY ? FVector(Offset > 0.0f ? 24.0f : -24.0f, 0.0f, 0.0f) : FVector(0.0f, Offset > 0.0f ? 24.0f : -24.0f, 0.0f)),
				bAlongY ? FVector(6.0f, 150.0f, 130.0f) : FVector(150.0f, 6.0f, 130.0f), FLinearColor(0.09f, 0.16f, 0.24f), Yaw);
		}
	};
	Side(true, HX, (Doors & 1) != 0);
	Side(true, -HX, (Doors & 2) != 0);
	Side(false, HY, (Doors & 4) != 0);
	Side(false, -HY, (Doors & 8) != 0);

	// The roof thins out over the player, as the walls do.
	if (bGable)
	{
		const float Rise = FMath::Min(HY * 0.55f, 230.0f);
		const float Slope = FMath::Sqrt(HY * HY + Rise * Rise) + 60.0f;
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rise, HY));
		for (const float Dir : {1.0f, -1.0f})
		{
			Piece(EBRShape::Cube, EBRMat::Fade, At(0.0f, Dir * HY * 0.5f, Height + Rise * 0.5f), FVector(Size.X + 90.0f, Slope, 26.0f),
				FRotator(0.0f, Yaw, 0.0f) + FRotator(0.0f, 0.0f, Dir * Angle), Roof, 2.0f, true);
		}
		// The gable ends.
		Trim(At(HX, 0.0f, Height + Rise * 0.42f), FVector(26.0f, Size.Y * 0.62f, Rise * 0.8f), Wall, Yaw);
		Trim(At(-HX, 0.0f, Height + Rise * 0.42f), FVector(26.0f, Size.Y * 0.62f, Rise * 0.8f), Wall, Yaw);
	}
	else
	{
		Piece(EBRShape::Cube, EBRMat::Fade, At(0.0f, 0.0f, Height + 14.0f), FVector(Size.X + 70.0f, Size.Y + 70.0f, 28.0f), Turn, Roof, 0.0f, true);
		Trim(At(0.0f, 0.0f, Height + 50.0f), FVector(Size.X * 0.34f, Size.Y * 0.3f, 60.0f), Roof * 0.7f, Yaw);
	}
}

void ABRIsland::Tree(const FVector& Foot, const float Scale, const int32 Kind, FRandomStream& Random)
{
	const float Lean = Random.FRandRange(-3.0f, 3.0f);
	if (Kind == 1)
	{
		// A pine, frosted when the snow paint is on it.
		const float Height = 760.0f * Scale;
		Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Height * 0.25f), FVector(46.0f * Scale, 46.0f * Scale, Height * 0.5f), FRotator(Lean, 0.0f, 0.0f),
			FLinearColor(0.16f, 0.09f, 0.055f), 0.0f, true);
		const bool bSnowy = BRMap::PaintAt(Foot.X, Foot.Y, Foot.Z).Snow > 0.5f;
		const FLinearColor Green = bSnowy ? FLinearColor(0.42f, 0.56f, 0.56f) : FLinearColor(0.05f, 0.19f, 0.11f);
		for (int32 Tier = 0; Tier < 3; ++Tier)
		{
			const float Wide = (360.0f - Tier * 90.0f) * Scale;
			Piece(EBRShape::Cone, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, (250.0f + Tier * 170.0f) * Scale), FVector(Wide, Wide, 300.0f * Scale), FRotator(Lean, Tier * 37.0f, 0.0f),
				Green * (1.0f + Tier * 0.14f), 0.0f, false);
		}
		return;
	}
	if (Kind == 2)
	{
		// A palm: a curved trunk and fronds.
		const float Height = 720.0f * Scale;
		const float Yaw = Random.FRandRange(0.0f, 360.0f);
		const FVector Bend = FRotator(0.0f, Yaw, 0.0f).Vector() * Height * 0.2f;
		const FVector Mid = Foot + FVector(0.0f, 0.0f, Height * 0.55f) + Bend * 0.35f;
		const FVector Top = Foot + FVector(0.0f, 0.0f, Height) + Bend;
		const FLinearColor Bark(0.3f, 0.2f, 0.12f);
		Pole(Foot, Mid, 22.0f * Scale, Bark, EBRMat::Surface);
		Pole(Mid, Top, 17.0f * Scale, Bark, EBRMat::Surface, false);
		for (int32 Frond = 0; Frond < 7; ++Frond)
		{
			const FRotator Turn(-16.0f, Yaw + Frond * 360.0f / 7.0f, 0.0f);
			Piece(EBRShape::Cube, EBRMat::Leaves, Top + Turn.Vector() * 180.0f * Scale - FVector(0.0f, 0.0f, 22.0f * Scale), FVector(380.0f * Scale, 92.0f * Scale, 10.0f), Turn,
				FLinearColor(0.1f, 0.34f, 0.08f) * Random.FRandRange(0.9f, 1.2f), 0.0f, false);
		}
		return;
	}
	if (Kind == 3)
	{
		// Blossom over the capital's lawns and the funfair.
		const float Height = 300.0f * Scale;
		Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Height * 0.5f), FVector(38.0f * Scale, 38.0f * Scale, Height), FRotator(Lean, 0.0f, 0.0f), FLinearColor(0.2f, 0.11f, 0.08f), 0.0f, true);
		for (int32 Blob = 0; Blob < 4; ++Blob)
		{
			const FVector Off(Random.FRandRange(-90.0f, 90.0f) * Scale, Random.FRandRange(-90.0f, 90.0f) * Scale, Random.FRandRange(0.0f, 90.0f) * Scale);
			const float R = Random.FRandRange(220.0f, 300.0f) * Scale;
			Piece(EBRShape::Sphere, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, Height + 70.0f * Scale) + Off, FVector(R, R, R * 0.85f), FRotator::ZeroRotator, FLinearColor(0.85f, 0.42f, 0.58f) * Random.FRandRange(0.9f, 1.1f), 0.0f, false);
		}
		return;
	}
	const float Height = 440.0f * Scale;
	const FLinearColor Bark(0.17f, 0.1f, 0.06f);
	Piece(EBRShape::Cylinder, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, Height * 0.5f), FVector(58.0f * Scale, 58.0f * Scale, Height), FRotator(Lean, 0.0f, Lean * 0.6f), Bark, 0.0f, true);
	const FLinearColor Leaf = FMath::Lerp(FLinearColor(0.11f, 0.3f, 0.06f), FLinearColor(0.32f, 0.42f, 0.09f), Random.FRand());
	for (int32 Blob = 0; Blob < 5; ++Blob)
	{
		const FVector Off(Random.FRandRange(-150.0f, 150.0f) * Scale, Random.FRandRange(-150.0f, 150.0f) * Scale, Random.FRandRange(-20.0f, 130.0f) * Scale);
		const float R = Random.FRandRange(300.0f, 430.0f) * Scale;
		Piece(EBRShape::Sphere, EBRMat::Leaves, Foot + FVector(0.0f, 0.0f, Height + 70.0f * Scale) + Off, FVector(R, R, R * 0.82f), FRotator::ZeroRotator, Leaf * Random.FRandRange(0.82f, 1.18f), 0.0f, false);
	}
}

void ABRIsland::Rock(const FVector& Foot, const float Scale, const FLinearColor& Colour, FRandomStream& Random)
{
	const float Yaw = Random.FRandRange(0.0f, 360.0f);
	Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, 50.0f * Scale), FVector(300.0f, 230.0f, 190.0f) * Scale, FRotator(Random.FRandRange(-10.0f, 10.0f), Yaw, Random.FRandRange(-10.0f, 10.0f)), Colour, 3.0f, true);
	Piece(EBRShape::Sphere, EBRMat::Surface, Foot + FRotator(0.0f, Yaw, 0.0f).Vector() * 150.0f * Scale + FVector(0.0f, 0.0f, 22.0f * Scale), FVector(170.0f, 130.0f, 100.0f) * Scale, FRotator(0.0f, Yaw + 60.0f, 0.0f), Colour * 0.86f, 3.0f, true);
}

void ABRIsland::Lamp(const FVector& Foot, const float Height)
{
	const FLinearColor Dark(0.08f, 0.09f, 0.11f);
	Piece(EBRShape::Cylinder, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, Height * 0.5f), FVector(22.0f, 22.0f, Height), FRotator::ZeroRotator, Dark, 0.0f, true);
	Piece(EBRShape::Cube, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, Height + 8.0f), FVector(90.0f, 90.0f, 14.0f), FRotator::ZeroRotator, Dark, 0.0f, false, false);
	Glow(Foot + FVector(0.0f, 0.0f, Height - 34.0f), FVector(56.0f, 56.0f, 64.0f), FLinearColor(1.0f, 0.68f, 0.28f) * 4.0f);
}

void ABRIsland::Crate(const FVector& Foot, const float Size, const float Yaw, const FLinearColor& Colour)
{
	Box(Foot + FVector(0.0f, 0.0f, Size * 0.5f), FVector(Size), Colour, Yaw, 4.0f);
}

void ABRIsland::Container(const FVector& Foot, const float Yaw, const FLinearColor& Colour)
{
	constexpr float Long = 620.0f, Wide = 250.0f, High = 260.0f;
	Piece(EBRShape::Cube, EBRMat::Surface, Foot + FVector(0.0f, 0.0f, High * 0.5f), FVector(Long, Wide, High), FRotator(0.0f, Yaw, 0.0f), Colour, 5.0f, true);
	const FRotator Turn(0.0f, Yaw, 0.0f);
	Piece(EBRShape::Cube, EBRMat::Surface, Foot + Turn.RotateVector(FVector(Long * 0.5f + 2.0f, 0.0f, High * 0.5f)), FVector(6.0f, Wide - 30.0f, High - 30.0f), Turn, Colour * 0.62f, 0.0f, false, false);
}

void ABRIsland::Barrel(const FVector& Foot, const FLinearColor& Colour)
{
	Piece(EBRShape::Cylinder, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, 60.0f), FVector(84.0f, 84.0f, 120.0f), FRotator::ZeroRotator, Colour, 0.0f, true);
	Piece(EBRShape::Cylinder, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, 92.0f), FVector(90.0f, 90.0f, 8.0f), FRotator::ZeroRotator, Colour * 0.6f, 0.0f, false, false);
}

void ABRIsland::Fence(const FVector& From, const FVector& To, const FLinearColor& Colour, const float Height)
{
	const FVector Span = To - From;
	const int32 Posts = FMath::Max(1, FMath::RoundToInt(Span.Size() / 220.0f));
	for (int32 I = 0; I <= Posts; ++I)
	{
		const FVector At = From + Span * (static_cast<float>(I) / Posts);
		Piece(EBRShape::Cube, EBRMat::Surface, At + FVector(0.0f, 0.0f, Height * 0.5f), FVector(18.0f, 18.0f, Height), FRotator(0.0f, Span.Rotation().Yaw, 0.0f), Colour, 0.0f, false);
	}
	Beam(From + FVector(0.0f, 0.0f, Height * 0.8f), To + FVector(0.0f, 0.0f, Height * 0.8f), 10.0f, 16.0f, Colour, EBRMat::Surface, true);
	Beam(From + FVector(0.0f, 0.0f, Height * 0.42f), To + FVector(0.0f, 0.0f, Height * 0.42f), 10.0f, 16.0f, Colour, EBRMat::Surface, false);
}

void ABRIsland::Truck(const FVector& Foot, const float Yaw, const FLinearColor& Colour)
{
	const FRotator Turn(0.0f, Yaw, 0.0f);
	const auto At = [&](const float X, const float Y, const float Z) { return Foot + Turn.RotateVector(FVector(X, Y, Z)); };
	Box(At(-110.0f, 0.0f, 180.0f), FVector(470.0f, 225.0f, 230.0f), Colour, Yaw, 5.0f);
	Box(At(205.0f, 0.0f, 138.0f), FVector(140.0f, 215.0f, 165.0f), Colour * 0.55f, Yaw);
	Trim(At(278.0f, 0.0f, 165.0f), FVector(6.0f, 185.0f, 72.0f), FLinearColor(0.08f, 0.14f, 0.22f), Yaw);
	for (const float X : {-230.0f, -120.0f, 205.0f})
	{
		for (const float Y : {-105.0f, 105.0f})
		{
			Piece(EBRShape::Cylinder, EBRMat::Surface, At(X, Y, 48.0f), FVector(96.0f, 96.0f, 30.0f), Turn + FRotator(0.0f, 0.0f, 90.0f), FLinearColor(0.03f, 0.03f, 0.035f), 0.0f, false);
		}
	}
}

void ABRIsland::Car(const FVector& Foot, const float Yaw, const FLinearColor& Colour)
{
	const FRotator Turn(0.0f, Yaw, 0.0f);
	const auto At = [&](const float X, const float Y, const float Z) { return Foot + Turn.RotateVector(FVector(X, Y, Z)); };
	Box(At(0.0f, 0.0f, 58.0f), FVector(420.0f, 180.0f, 66.0f), Colour, Yaw);
	Box(At(-22.0f, 0.0f, 118.0f), FVector(220.0f, 158.0f, 60.0f), FLinearColor(0.07f, 0.11f, 0.17f), Yaw, 0.0f, false);
	Box(At(-22.0f, 0.0f, 150.0f), FVector(200.0f, 150.0f, 8.0f), Colour, Yaw, 0.0f, false);
	for (const float X : {-135.0f, 135.0f})
	{
		for (const float Y : {-84.0f, 84.0f})
		{
			Piece(EBRShape::Cylinder, EBRMat::Surface, At(X, Y, 34.0f), FVector(68.0f, 68.0f, 24.0f), Turn + FRotator(0.0f, 0.0f, 90.0f), FLinearColor(0.03f, 0.03f, 0.035f), 0.0f, false);
		}
	}
	Glow(At(212.0f, 58.0f, 64.0f), FVector(5.0f, 34.0f, 16.0f), FLinearColor(1.0f, 0.9f, 0.7f) * 2.0f, Yaw);
	Glow(At(212.0f, -58.0f, 64.0f), FVector(5.0f, 34.0f, 16.0f), FLinearColor(1.0f, 0.9f, 0.7f) * 2.0f, Yaw);
}

void ABRIsland::Dodgeball(const FVector& Centre, const float Radius, const FLinearColor& Colour, const FLinearColor& Band, const float Yaw, const bool bSolid)
{
	Piece(EBRShape::Sphere, EBRMat::Surface, Centre, FVector(Radius * 2.0f), FRotator(0.0f, Yaw, 0.0f), Colour, 0.0f, bSolid);
	// The ball's seams: thin rings a hair larger than it.
	Piece(EBRShape::Sphere, EBRMat::Surface, Centre, FVector(Radius * 2.02f, Radius * 2.02f, Radius * 0.14f), FRotator(0.0f, Yaw, 90.0f), Band, 0.0f, false, false);
	Piece(EBRShape::Sphere, EBRMat::Surface, Centre, FVector(Radius * 2.02f, Radius * 2.02f, Radius * 0.14f), FRotator(0.0f, Yaw + 90.0f, 90.0f), Band, 0.0f, false, false);
	Piece(EBRShape::Sphere, EBRMat::Surface, Centre, FVector(Radius * 2.02f, Radius * 2.02f, Radius * 0.14f), FRotator(0.0f, Yaw, 0.0f), Band, 0.0f, false, false);
}

void ABRIsland::Bench(const FVector& Foot, const float Yaw)
{
	const FRotator Turn(0.0f, Yaw, 0.0f);
	Box(Foot + FVector(0.0f, 0.0f, 48.0f), FVector(220.0f, 56.0f, 10.0f), FLinearColor(0.36f, 0.2f, 0.09f), Yaw, 4.0f, false);
	Box(Foot + Turn.RotateVector(FVector(0.0f, -30.0f, 84.0f)), FVector(220.0f, 8.0f, 54.0f), FLinearColor(0.36f, 0.2f, 0.09f), Yaw, 4.0f, false);
	Box(Foot + Turn.RotateVector(FVector(-90.0f, 0.0f, 24.0f)), FVector(12.0f, 56.0f, 48.0f), FLinearColor(0.07f, 0.07f, 0.08f), Yaw, 0.0f, false);
	Box(Foot + Turn.RotateVector(FVector(90.0f, 0.0f, 24.0f)), FVector(12.0f, 56.0f, 48.0f), FLinearColor(0.07f, 0.07f, 0.08f), Yaw, 0.0f, false);
}

void ABRIsland::Flag(const FVector& Foot, const float Height, const FLinearColor& Colour)
{
	Piece(EBRShape::Cylinder, EBRMat::Metal, Foot + FVector(0.0f, 0.0f, Height * 0.5f), FVector(16.0f, 16.0f, Height), FRotator::ZeroRotator, FLinearColor(0.8f, 0.8f, 0.82f), 0.0f, false);
	Piece(EBRShape::Cube, EBRMat::Fade, Foot + FVector(0.0f, 95.0f, Height - 60.0f), FVector(6.0f, 180.0f, 112.0f), FRotator::ZeroRotator, Colour, 0.0f, false, true);
}

void ABRIsland::FlowerBed(const FVector& Centre, const float Radius, FRandomStream& Random)
{
	Disc(Centre + FVector(0.0f, 0.0f, 12.0f), Radius, 24.0f, FLinearColor(0.09f, 0.05f, 0.03f), EBRMat::Surface, false);
	const FLinearColor Colours[] = {{1.0f, 0.25f, 0.3f}, {1.0f, 0.75f, 0.12f}, {0.95f, 0.9f, 0.85f}, {0.55f, 0.3f, 0.95f}};
	for (int32 I = 0; I < FMath::RoundToInt(Radius * 0.3f); ++I)
	{
		const float Size = Random.FRandRange(22.0f, 40.0f);
		const FVector Offset(Random.FRandRange(-Radius, Radius) * 0.65f, Random.FRandRange(-Radius, Radius) * 0.65f, 36.0f);
		Piece(EBRShape::Sphere, EBRMat::Leaves, Centre + Offset, FVector(Size), FRotator::ZeroRotator, Colours[Random.RandHelper(4)], 0.0f, false, false);
	}
}

// ---- Looking round the island (checks and pictures) ----

const TArray<TPair<FString, FVector2D>>& ABRIsland::Landmarks()
{
	using namespace BRLayout;
	static const TArray<TPair<FString, FVector2D>> List{
		{TEXT("capital"), Capital.At + FVector2D(-1500.0, 0.0)},
		{TEXT("palace"), Capital.At + FVector2D(1900.0, 0.0)},
		{TEXT("stadium"), Stadium.At + FVector2D(-1300.0, 0.0)},
		{TEXT("stadiumgate"), Stadium.At + FVector2D(-3900.0, -300.0)},
		{TEXT("factory"), Factory.At + FVector2D(-600.0, -300.0)},
		{TEXT("port"), Port.At + FVector2D(0.0, 900.0)},
		{TEXT("town"), Town.At + FVector2D(300.0, 200.0)},
		{TEXT("farm"), Farm.At + FVector2D(200.0, -300.0)},
		{TEXT("canyon"), Canyon.At + FVector2D(-1200.0, 300.0)},
		{TEXT("mountain"), Mountain.At + FVector2D(-1300.0, 1300.0)},
		{TEXT("forest"), Forest.At + FVector2D(300.0, 500.0)},
		{TEXT("crater"), Crater.At + FVector2D(-1500.0, 400.0)},
		{TEXT("funfair"), Funfair.At + FVector2D(-500.0, -200.0)},
		{TEXT("river"), FVector2D(9000.0, 3200.0)}};
	return List;
}

void ABRIsland::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StreamClock -= DeltaSeconds;
	if (StreamClock <= 0.0f)
	{
		StreamClock = 0.05f;
		StreamAround(false);
	}
	UpdateDevViews(DeltaSeconds);
}

void ABRIsland::UpdateDevViews(const float DeltaSeconds)
{
	static const bool bAnyView = FParse::Param(FCommandLine::Get(), TEXT("CIBRTour")) || FParse::Param(FCommandLine::Get(), TEXT("CIBROverview")) || FCString::Strifind(FCommandLine::Get(), TEXT("CIBRStop=")) != nullptr;
	if (!bAnyView) { return; }
	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
	if (!Player) { return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("CIBROverview")))
	{
		if (!OverviewCamera)
		{
			OverviewCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(-1000.0f, 0.0f, 205000.0f), FRotator(-90.0f, 0.0f, 0.0f));
			OverviewCamera->GetCameraComponent()->SetFieldOfView(50.0f);
			OverviewCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
		Player->SetViewTarget(OverviewCamera);
		return;
	}
	ACharacter* Pawn = Cast<ACharacter>(Player->GetPawn());
	if (!Pawn) { return; }
	FString Only;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIBRStop="), Only))
	{
		// Stand at one place only.
		if (TourStop == INDEX_NONE)
		{
			for (int32 Index = 0; Index < Landmarks().Num(); ++Index)
			{
				if (Landmarks()[Index].Key == Only) { TourStop = Index; }
			}
			if (TourStop != INDEX_NONE)
			{
				const FVector2D At = Landmarks()[TourStop].Value;
				EnsureLoadedAround(At);
				Pawn->SetActorLocation(OnGround(At.X, At.Y, 140.0f), false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogChaosImpact, Log, TEXT("BRIsland tour: %s"), *Only);
			}
		}
		return;
	}
	TourTimer -= DeltaSeconds;
	if (TourTimer <= 0.0f)
	{
		TourTimer = 5.0f;
		TourStop = (TourStop + 1) % Landmarks().Num();
		const FVector2D At = Landmarks()[TourStop].Value;
		EnsureLoadedAround(At);
		Pawn->SetActorLocation(OnGround(At.X, At.Y, 140.0f), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogChaosImpact, Log, TEXT("BRIsland tour: %s"), *Landmarks()[TourStop].Key);
	}
}
