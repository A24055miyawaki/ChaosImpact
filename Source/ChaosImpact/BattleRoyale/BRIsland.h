#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BRIsland.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class ACameraActor;

/**
 * The battle royale's island (removable, like the RPG: see Docs/BattleRoyale/README.md). About 1.6 km across; every machine
 * builds the same island from the functions below, so nothing of it is sent over the network.
 *
 * All of it is here and in BRIslandTerrain.cpp / BRIslandDistricts*.cpp; the only thing outside is the title's choice
 * (AChaosImpactPlayerController::OpenBattleRoyaleIsland), which looks for the level by name.
 */
namespace BRMap
{
	/** Half the side of the square the terrain covers (the sea beyond it is one plane). */
	inline constexpr float HalfMap = 102000.0f;
	inline constexpr float CellSize = 250.0f;

	struct FPaint
	{
		float Dirt = 0.0f, Paved = 0.0f, Sand = 0.0f, Moss = 0.0f, Snow = 0.0f, Ash = 0.0f, Dry = 0.0f, Marsh = 0.0f, Concrete = 0.0f;
	};

	/** Metres inland from the shore (cm; negative out at sea). */
	float ShoreDistance(float X, float Y);
	float HeightAt(float X, float Y);
	FPaint PaintAt(float X, float Y, float Height);
	/** True where scenery may be scattered: on land, off the roads, out of the districts' built-up parts and the river. */
	bool IsOpenGround(float X, float Y);
	/** The small settlements scattered over the open country: each sits on its own level ground. */
	const TArray<FVector2D>& Places();
	/** A smoothed road through these points, as the terrain has it, for putting things beside it. */
	const TArray<TArray<FVector2D>>& Roads();
}

enum class EBRMat : uint8 { Surface, Fade, Metal, Leaves, Glow, Count };
enum class EBRShape : uint8 { Cube, Cylinder, Sphere, Cone, Count };

UCLASS()
class CHAOSIMPACT_API ABRIsland : public AActor
{
	GENERATED_BODY()

public:
	ABRIsland();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Places to stand in each part of the island (the checks and the opening use these). */
	static const TArray<TPair<FString, FVector2D>>& Landmarks();
	/** Loads the ground and scenery around a point now, before someone is put there. */
	void EnsureLoadedAround(const FVector2D& At);
	void DevCheckSolid(const FVector2D& Around, float Radius) const;

private:
	struct FBatchPiece { FTransform Where; FLinearColor Colour; float Pattern; };
	struct FBatch { EBRShape Shape = EBRShape::Cube; EBRMat Mat = EBRMat::Surface; bool bSolid = false; bool bShadow = false; TArray<FBatchPiece> Pieces; TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr; };
	/** One square of the island's scenery: its batches, made into components only while someone is near. */
	struct FSceneryCell { TMap<uint32, FBatch> Batches; bool bWanted = false; };
	struct FTerrainChunk { TObjectPtr<UProceduralMeshComponent> Mesh = nullptr; int32 FromX = 0; int32 FromY = 0; bool bFine = false; };

	void Build();
	/** Every chunk's rough shape, so the whole island is there to see (from the air, say). */
	void BuildTerrain();
	void BuildSea();
	void BuildShoreBarrier();
	/** Loads the fine ground and the scenery round the people here, a little each frame (Force: all of it at once, for the start). */
	void StreamAround(bool bForce);
	void GatherFoci(TArray<FVector2D>& Out) const;
	void LoadFineChunk(int32 Index);
	void UnloadFineChunk(int32 Index);
	void LoadSceneryPart(uint32 CellKey, uint32 BatchKey);
	void BuildChunkSection(int32 FromX, int32 FromY, int32 Step, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals,
		TArray<FVector2D>& UV0, TArray<FVector2D>& UV1, TArray<FVector2D>& UV2, TArray<FVector2D>& UV3, TArray<FLinearColor>& Colours, TArray<struct FProcMeshTangent>& Tangents) const;

	// The pieces everything is made of. At is the centre, in world cm; Size in cm.
	void Piece(EBRShape Shape, EBRMat Mat, const FVector& At, const FVector& Size, const FRotator& Rotation, const FLinearColor& Colour,
		float Pattern = 0.0f, bool bSolid = true, bool bShadow = true);
	void Box(const FVector& At, const FVector& Size, const FLinearColor& Colour, float Yaw = 0.0f, float Pattern = 0.0f, bool bSolid = true);
	void WallBox(const FVector& At, const FVector& Size, const FLinearColor& Colour, float Yaw = 0.0f, float Pattern = 1.0f);
	void Trim(const FVector& At, const FVector& Size, const FLinearColor& Colour, float Yaw = 0.0f);
	void Glow(const FVector& At, const FVector& Size, const FLinearColor& Colour, float Yaw = 0.0f, EBRShape Shape = EBRShape::Cube);
	void Disc(const FVector& At, float Radius, float Height, const FLinearColor& Colour, EBRMat Mat = EBRMat::Surface, bool bSolid = true);
	void Dome(const FVector& At, float Radius, const FLinearColor& Colour, EBRMat Mat = EBRMat::Fade, bool bSolid = true);
	void Beam(const FVector& From, const FVector& To, float Width, float Height, const FLinearColor& Colour, EBRMat Mat = EBRMat::Surface, bool bSolid = true);
	void Pole(const FVector& From, const FVector& To, float Radius, const FLinearColor& Colour, EBRMat Mat = EBRMat::Metal, bool bSolid = true);

	// Things made of the pieces.
	/** A building you can walk into: a door in each side named in Doors (+X, -X, +Y, -Y as bits 1, 2, 4, 8). */
	void House(const FVector& Centre, const FVector2D& Size, float Height, float Yaw, const FLinearColor& Wall, const FLinearColor& Roof, int32 Doors, bool bGable = true);
	void Tree(const FVector& Foot, float Scale, int32 Kind, FRandomStream& Random);
	void Rock(const FVector& Foot, float Scale, const FLinearColor& Colour, FRandomStream& Random);
	void Lamp(const FVector& Foot, float Height = 520.0f);
	void Crate(const FVector& Foot, float Size, float Yaw, const FLinearColor& Colour);
	void Container(const FVector& Foot, float Yaw, const FLinearColor& Colour);
	void Barrel(const FVector& Foot, const FLinearColor& Colour);
	void Fence(const FVector& From, const FVector& To, const FLinearColor& Colour, float Height = 110.0f);
	void Truck(const FVector& Foot, float Yaw, const FLinearColor& Colour);
	void Car(const FVector& Foot, float Yaw, const FLinearColor& Colour);
	void Dodgeball(const FVector& Centre, float Radius, const FLinearColor& Colour, const FLinearColor& Band, float Yaw = 0.0f, bool bSolid = true);
	void Bench(const FVector& Foot, float Yaw);
	void Flag(const FVector& Foot, float Height, const FLinearColor& Colour);
	void FlowerBed(const FVector& Centre, float Radius, FRandomStream& Random);

	// BRIslandDistricts*.cpp, one for each part of the island.
	void BuildCapital();
	void BuildStadium();
	void BuildFactory();
	void BuildPort();
	void BuildAirfield();
	void BuildLab();
	void BuildTown(const struct FBRDistrict& District, int32 Seed);
	void BuildFarm(const struct FBRDistrict& District, int32 Seed);
	void BuildCanyon();
	void BuildMountain();
	void BuildForest();
	void BuildCrater();
	void BuildFunfair();
	void BuildRiver();
	void BuildScatter();
	void BuildDressing();
	void DressDistrict(const struct FBRDistrict& District, int32 Seed);
	bool PlaceVignette(const FVector2D& P, const BRMap::FPaint& Paint, FRandomStream& Random);
	void GroundCover(const FVector2D& Centre, float Half, const BRMap::FPaint& Paint, FRandomStream& Random);
	void Bushes(const FVector2D& Centre, int32 Count, float Spread, const FLinearColor& Colour, FRandomStream& Random);
	/** What is already standing, by small squares: nothing new is put where something solid is. */
	static uint64 OccupiedKey(int32 X, int32 Y);
	bool IsClear(const FVector2D& At, float Radius) const;
	void Claim(const FVector2D& At, float HalfX, float HalfY);
	TSet<uint64> Occupied;
	static constexpr float OccupiedCell = 120.0f;

	FVector OnGround(float X, float Y, float Lift = 0.0f) const;

	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> TerrainChunks;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> BatchComponents;
	UPROPERTY() TObjectPtr<UMaterialInterface> TerrainMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> SeaMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> Materials[static_cast<int32>(EBRMat::Count)];

	TMap<uint32, FSceneryCell> Scenery;
	TArray<FTerrainChunk> Chunks;
	float StreamClock = 0.0f;
	TArray<FVector2D> ExtraFoci;
	static constexpr float SceneryCellSize = 15000.0f;

	// -CIBRTour: the first player is carried round the island; -CIBROverview: a camera high over all of it (checks and pictures).
	void UpdateDevViews(float DeltaSeconds);
	int32 TourStop = INDEX_NONE;
	float TourTimer = 0.0f;
	bool bOverviewSet = false;
	UPROPERTY() TObjectPtr<ACameraActor> OverviewCamera;
};
