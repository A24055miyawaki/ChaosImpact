#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChaosImpactBodyPaint.generated.h"

class UChaosImpactPuppetComponent;
class UTexture2D;

/**
 * ペイント: pictures drawn freely on a character's body. Each of the model's materials has a layer over its colours
 * (the material's "CI Paint" node, texture parameter PaintLayer, at UV 0): a picture the size of ChaosImpactBodyPaint::Size,
 * transparent where nothing is drawn. Layers are kept with the colour preset as PNG and sent online like the colours.
 */
namespace ChaosImpactBodyPaint
{
	inline constexpr int32 Size = 1024;
	inline constexpr int32 TileSize = 64;
	inline constexpr int32 TilesPerSide = Size / TileSize;
	inline constexpr int32 MaxLayers = 6;
	/** How many strokes can be taken back. */
	inline constexpr int32 MaxUndo = 60;
	/** The most a preset's pictures may weigh (all its layers' PNGs): sent to everyone online. */
	inline constexpr int32 MaxBytes = 600 * 1024;
}

struct FChaosImpactPaintLayerData;

namespace ChaosImpactBodyPaint
{
	/** A picture as a texture for play (with its smaller sizes, see-through edges kept clean). */
	CHAOSIMPACT_API UTexture2D* MakeTexture(const TArray<FColor>& Pixels, UObject* Outer);
	/** The layers' PNGs in one run of bytes (kept, compared and sent as one), and back. */
	CHAOSIMPACT_API TArray<uint8> Pack(const TArray<FChaosImpactPaintLayerData>& Layers);
	CHAOSIMPACT_API bool Unpack(const TArray<uint8>& Packed, TArray<FChaosImpactPaintLayerData>& OutLayers);
	/** The packed pictures' key (never 0; 0 is no pictures). */
	CHAOSIMPACT_API int32 KeyOf(const TArray<uint8>& Packed);
	/** All the layers' PNG bytes together. */
	CHAOSIMPACT_API int32 WeightOf(const TArray<FChaosImpactPaintLayerData>& Layers);
}

/** One layer's picture as it is kept and sent. */
USTRUCT()
struct FChaosImpactPaintLayerData
{
	GENERATED_BODY()

	/** Which of the model's layers (its material slot; the limbs' model has one). */
	UPROPERTY()
	int32 Layer = 0;

	UPROPERTY()
	TArray<uint8> Png;
};

/** One layer being drawn on: its pixels (sRGB, straight alpha) and the texture that shows them. */
UCLASS()
class UChaosImpactPaintLayer : public UObject
{
	GENERATED_BODY()
public:
	void Init();
	/** Pushes the pixels changed since the last flush to the texture. */
	void Flush();
	void MarkDirty(const FIntRect& Rect);
	bool IsBlank() const;
	void Clear();
	TArray<uint8> ToPng() const;
	bool FromPng(const TArray<uint8>& Png);
	UTexture2D* GetTexture() const { return Texture; }
	TArray<FColor>& GetPixels() { return Pixels; }
	const TArray<FColor>& GetPixels() const { return Pixels; }

private:
	TArray<FColor> Pixels;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Texture;
	FIntRect Dirty;
	bool bDirty = false;
};

/** How a stroke paints. */
struct FChaosImpactBrush
{
	FColor Colour = FColor::White;
	/** World units. */
	float Radius = 6.0f;
	/** 0 soft edge .. 1 hard edge. */
	float Hardness = 0.8f;
	/** 0..1, for the whole stroke (overlapping dabs of one stroke do not build up). */
	float Opacity = 1.0f;
	bool bErase = false;
	/** Drawn on the other side of the body too (mirrored across the model's middle). */
	bool bMirror = false;
};

/**
 * The model's surface as it is drawn on: its triangles where they stand (the model held still in its pose), with
 * their place on their layer's picture, and the strokes laid on the layers, with their undo.
 */
class FChaosImpactPaintSurface
{
public:
	struct FHit
	{
		FVector Location = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		int32 Triangle = INDEX_NONE;
		int32 Layer = INDEX_NONE;
		FVector2f UV = FVector2f::ZeroVector;
		/** Where on the triangle (its corners' shares). */
		FVector3f Bary = FVector3f(1.0f, 0.0f, 0.0f);
	};

	/** From the model as it stands now (call again if it moves). False: nothing could be read. */
	bool Build(const UChaosImpactPuppetComponent* Model);
	bool IsBuilt() const { return !Tris.IsEmpty(); }
	int32 GetLayerCount() const { return LayerCount; }
	/** The nearest surface along the ray (facing it). */
	bool Raycast(const FVector& Origin, const FVector& Direction, FHit& OutHit) const;

	/** A stroke: begun, its dabs (each from the last one's place on, spaced along), and ended (one undo step). */
	void BeginStroke(const TArray<UChaosImpactPaintLayer*>& Layers, const FChaosImpactBrush& Brush);
	void StrokeTo(const FHit& Hit, const FVector& ViewDirection);
	/** Where 左右対称 draws for a hit (the other side of the body). */
	bool GetMirrorLocation(const FHit& Hit, FVector& OutLocation) const;
	/** The stroke goes on, but the next dab does not join up with the last (the pen went off the model). */
	void Lift() { bHasLastDab = false; }
	void EndStroke();
	bool IsStroking() const { return bStroking; }
	/** Everything joined to the hit across the surface that is the colour clicked, filled with the brush's colour. */
	void Fill(const TArray<UChaosImpactPaintLayer*>& Layers, const FHit& Hit, const FChaosImpactBrush& Brush);
	/** Every layer cleared (one undo step). */
	void ClearAll(const TArray<UChaosImpactPaintLayer*>& Layers);
	/** The drawn colour at the hit (false: nothing drawn there). */
	bool PickColour(const TArray<UChaosImpactPaintLayer*>& Layers, const FHit& Hit, FColor& OutColour) const;

	bool CanUndo() const { return !Undo.IsEmpty(); }
	bool CanRedo() const { return !Redo.IsEmpty(); }
	void UndoStep(const TArray<UChaosImpactPaintLayer*>& Layers);
	void RedoStep(const TArray<UChaosImpactPaintLayer*>& Layers);
	void ForgetHistory();
	/** The middle the mirror reflects across (a point on it and its normal). */
	void GetMirrorPlane(FVector& OutPoint, FVector& OutNormal) const { OutPoint = MirrorPoint; OutNormal = MirrorNormal; }
	/** Where the model's surface stands (as built). */
	const FBox& GetBounds() const { return Bounds; }
	/** Some stroke can be taken back or done again. */
	bool HasHistory() const { return CanUndo() || CanRedo(); }

private:
	struct FTri
	{
		FVector3f P[3];
		FVector2f UV[3];
		FVector3f Normal;
		FVector3f Centre;
		float Reach = 0.0f;
		int32 Layer = 0;
		int32 Island = 0;
		/** The same triangle as the model was made (its rest pose, the mesh's own space): 左右対称 mirrors there. */
		FVector3f R[3];
		FVector3f RestCentre;
		FVector3f RestNormal;
		float RestReach = 0.0f;
	};
	struct FTileEdit
	{
		int32 Layer = 0;
		int32 Tile = 0;
		TArray<FColor> Before;
		TArray<FColor> After;
	};
	struct FStep
	{
		TArray<FTileEdit> Tiles;
	};

	/** The tile's pixels kept as they were before this stroke touched it (once). */
	FTileEdit& Touch(int32 Layer, int32 Tile);
	void Dab(const FVector& Centre, const FVector& ViewDirection, const FVector3f& Rest, const FVector3f& RestNormal);
	/** The rest pose's point and facing under a hit. */
	void RestOf(const FHit& Hit, FVector3f& OutRest, FVector3f& OutNormal) const;
	/** 左右対称 in the rest pose: which of the mesh's own axes is sideways, and its middle. */
	void FindRestMirror();
	/** One pixel of the stroke at strength Weight (the strongest it has had this stroke wins). */
	void Apply(int32 Layer, int32 X, int32 Y, float Weight);
	/** Pixels just outside the drawn pieces take their neighbours' colours (no seam shows where pieces meet). */
	void Bleed(int32 Layer, const FIntRect& Rect);
	void FinishStep();
	static void PutTile(UChaosImpactPaintLayer* Layer, int32 Tile, const TArray<FColor>& Pixels);
	void FindMirror(const UChaosImpactPuppetComponent* Model);
	void BuildGrid();
	void BuildNeighbours();
	/** The triangles that may be within Reach of At (from the grid). */
	void Near(const FVector3f& At, float Reach, TArray<int32>& Out) const;

	TArray<FTri> Tris;
	int32 LayerCount = 0;
	/** Per layer: which pixels some piece of the model covers. */
	TArray<TBitArray<>> Covered;
	FVector MirrorPoint = FVector::ZeroVector;
	FVector MirrorNormal = FVector::RightVector;
	FBox Bounds = FBox(ForceInit);
	/** The model is one mesh: 左右対称 mirrors in its rest pose (true to the body whatever its pose); else across MirrorPoint/Normal. */
	bool bRestMirror = false;
	int32 RestAxis = 0;
	float RestMiddle = 0.0f;
	/** World units per unit of the mesh's own space. */
	float RestScale = 1.0f;
	FVector3f LastRest = FVector3f::ZeroVector;
	FVector3f LastRestNormal = FVector3f::UpVector;
	/** The triangles by where they stand: a grid of cells GridCell wide over the bounds. */
	float GridCell = 8.0f;
	FIntVector GridCells = FIntVector(1, 1, 1);
	TArray<TArray<int32>> Grid;
	/** Each triangle's neighbours across its three edges (INDEX_NONE: an open edge). */
	TArray<FIntVector> Neighbours;
	mutable TArray<uint32> NearMark;
	mutable uint32 NearStamp = 0;

	// The stroke being drawn.
	bool bStroking = false;
	TArray<UChaosImpactPaintLayer*> StrokeLayers;
	FChaosImpactBrush StrokeBrush;
	FVector LastDab = FVector::ZeroVector;
	bool bHasLastDab = false;
	TMap<int64, int32> StrokeTiles;
	FStep StrokeStep;
	/** The stroke's strength so far at each pixel it touched (layer, pixel). */
	TMap<int64, float> StrokeWeights;
	TArray<FIntRect> StrokeRects;

	TArray<FStep> Undo;
	TArray<FStep> Redo;
};
