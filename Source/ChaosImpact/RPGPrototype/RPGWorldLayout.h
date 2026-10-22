#pragma once
#include "CoreMinimal.h"

/**
 * The RPG adventure map's shape, in one place: walkable roads and open areas (each at its own height), rock masses
 * standing above them, gorges cut through them, the sea, and the crystal cave. The terrain meshes are built from this
 * (RPGWorldBuilder, editor only); at run time it names the region a point is in.
 *
 * Coordinates are the level's (the adventure world actor stands at the origin). The camera looks along +X, so "north"
 * (up the screen) is +X and "east" (right) is +Y.
 */
namespace RPGWorld
{
enum class ESurface : uint8 { Grass, Dirt, Pavers, Sand, Cave, Moss };

struct FRoad { TArray<FVector> Points; float Width=600; ESurface Path=ESurface::Dirt; const TCHAR* Region=TEXT(""); };
struct FArea { FName Id; FVector At; FVector2D Radius; ESurface Fill=ESurface::Grass; const TCHAR* Region=TEXT(""); };
/** Rock standing up out of the gorge or the ground, Top high: never walkable (its sides are cliffs). */
struct FRock { FVector2D At; FVector2D Radius; float Top=0; };
/** A gorge cut across land (a river far below); roads cross it on bridges. */
struct FGap { TArray<FVector2D> Points; float Width=600; };
struct FBridge { FVector At; float Yaw=0; float Length=0; float Width=0; };

/** What the terrain is at a point. */
struct FSample
{
	float Height=0;
	bool bLand=false;
	/** How far inside walkable ground (negative outside). */
	float Inside=0;
	float Dirt=0, Pavers=0, Sand=0, Cave=0, Moss=0;
};

const TArray<FRoad>& Roads();
const TArray<FArea>& Areas();
const TArray<FRock>& Rocks();
const TArray<FGap>& Gaps();
/** Every place a road crosses a gorge. */
TArray<FBridge> Bridges();

FSample Sample(float X, float Y);
/** Ground height at a point (the terrain's, walkable or not). */
float Height(float X, float Y);
/** Where an area is (its centre on the ground), plus an offset. */
FVector AreaPoint(FName Id, FVector2D Offset=FVector2D::ZeroVector);
const FArea* FindArea(FName Id);
/** The place name for the region a point is in. */
FString RegionAt(const FVector& At);
bool IsCave(float X, float Y);
bool IsSea(float X, float Y);
/** Distance from a point to the nearest road's centre line (and that road's half width). */
float RoadDistance(float X, float Y, float* HalfWidth=nullptr, FVector* Nearest=nullptr);

constexpr float MinX=-12000, MaxX=44000, MinY=-15200, MaxY=14400;
constexpr float VoidHeight=-900;
constexpr float SeaLevel=-70;
constexpr float RiverLevel=-760;
}
