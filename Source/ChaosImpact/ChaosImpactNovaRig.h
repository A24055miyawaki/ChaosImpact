#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;

/** One key of a part's animation curve (from the FBX; see ChaosImpactNovaRigData.inl). */
struct FChaosImpactNovaKey
{
	float Time;
	float Value;
	/** 0 held, 1 straight, 2 curved (Hermite with the slopes below, per second). */
	int32 Interp;
	float RightSlope;
	float NextLeftSlope;
};

/** A part's translation ('T'), rotation in degrees ('R'), scale ('S') or visibility ('V') along one axis, over time. */
struct FChaosImpactNovaCurve
{
	int32 Node;
	char Channel;
	int32 Axis;
	const FChaosImpactNovaKey* Keys;
	int32 NumKeys;
};

/** A part as it was made: its parent, its mesh, its rest transform and pivot (all in the source's Y-up space). */
struct FChaosImpactNovaNode
{
	const TCHAR* Name;
	int32 Parent;
	/** The static mesh in /Game/ChaosImpact/Nova/Meshes (empty: no mesh). */
	const TCHAR* Mesh;
	const TCHAR* Material;
	FVector3f T;
	FVector3f R;
	FVector3f S;
	FVector3f RotationPivot;
	FVector3f ScalingPivot;
	float Visibility;
};

struct FChaosImpactNovaRigData
{
	const TCHAR* Name;
	const FChaosImpactNovaNode* Nodes;
	int32 NumNodes;
	const FChaosImpactNovaCurve* Curves;
	int32 NumCurves;
};

/** The Nova models (Downloads/Nova FBX files). */
enum class EChaosImpactNovaRig : uint8
{
	/** The thrown nova landing: it spins, explodes (a growing, then shrinking ball of fire) and falls apart. */
	NovaFire,
	/** The finished nova, whole (held, charged, flying). */
	Nova,
	/** The three parts it is made from, each turning as it waits to be picked up. */
	Gear,
	Ring,
	Core
};

namespace ChaosImpactNova
{
	// The landing (NovaFire), in seconds of its source animation.
	/** Lands (the game starts the animation here; the gears spin from the start). */
	constexpr float FireStart = 21.6667f;
	/** The ball of fire starts to grow. */
	constexpr float FireExplodeAt = 24.1667f;
	/** Fully grown (it holds a while). */
	constexpr float FireFullAt = 26.25f;
	/** It starts to shrink; the whole nova is swapped for its spinning set. */
	constexpr float FireShrinkAt = 28.3333f;
	/** Gone. */
	constexpr float FireGoneAt = 30.0f;
	/** It falls apart. */
	constexpr float FireBreakAt = 35.0f;
	constexpr float FireEnd = 37.0833f;
	/** The parts' turning loop (Gear, Ring). */
	constexpr float PartLoopSeconds = 4.1667f;
	/** The finished nova's size in its source: its rings are this far across (centimetres). */
	constexpr float ModelRadius = 87.0f;

	/** The ball of fire's size (0-1 of its largest) at a time of the source animation. */
	CHAOSIMPACT_API float GetExplosionSize(float AnimTime);
	/** A curve value at a time (Default where the part has no such curve). */
	CHAOSIMPACT_API float Evaluate(EChaosImpactNovaRig Rig, int32 Node, TCHAR Channel, int32 Axis, float Time, float Default);
	CHAOSIMPACT_API int32 FindNode(EChaosImpactNovaRig Rig, const TCHAR* Name);
	CHAOSIMPACT_API int32 GetNodeCount(EChaosImpactNovaRig Rig);
	CHAOSIMPACT_API FString GetMeshPath(EChaosImpactNovaRig Rig, int32 Node);
	/** The model's own look: the shared textured material with this rig's texture. */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeMaterial(UObject* Outer, EChaosImpactNovaRig Rig);
	/** The ball of fire's material (Glow, Fade, Heat). */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeExplosionMaterial(UObject* Outer);
	/** Every mesh, texture and material the Nova uses (to load them up front). */
	CHAOSIMPACT_API void GetAssetPaths(TArray<FSoftObjectPath>& OutPaths);
}

/**
 * One of the Nova models in a level: a mesh component per part under a root, each placed every Pose as the source
 * animation placed it. Not a UObject: the components belong to (and are kept by) the owning actor.
 */
class CHAOSIMPACT_API FChaosImpactNovaRig
{
public:
	/** Builds the parts under Parent (the rig's root follows Parent; place and size it with GetRoot). */
	void Build(AActor* Owner, USceneComponent* Parent, EChaosImpactNovaRig Rig);
	bool IsBuilt() const { return Root.IsValid(); }
	USceneComponent* GetRoot() const { return Root.Get(); }
	UStaticMeshComponent* GetPart(int32 Node) const;
	int32 FindNode(const TCHAR* Name) const;
	/** Places every part as at Time (seconds of the source animation). */
	void Pose(float Time);
	/** Turns a part further (degrees about its own axes, in the source's space), on top of its animation. */
	void SetExtraRotation(int32 Node, const FVector3f& Degrees);
	/** A part shown or not regardless of its animation (or back to its animation). */
	void SetForcedHidden(int32 Node, bool bHidden);
	void SetVisible(bool bVisible);
	/** See-through as it goes: 1 solid .. 0 gone (dissolved away, and from the next Pose not drawn at all). */
	void SetFade(float Fade);
	EChaosImpactNovaRig GetRig() const { return Rig; }

private:
	EChaosImpactNovaRig Rig = EChaosImpactNovaRig::Nova;
	TWeakObjectPtr<USceneComponent> Root;
	TWeakObjectPtr<UMaterialInstanceDynamic> Look;
	TArray<TWeakObjectPtr<UStaticMeshComponent>> Parts;
	TArray<FVector3f> ExtraRotation;
	TArray<bool> ForcedHidden;
	bool bVisible = true;
	float Fade = 1.0f;
};
