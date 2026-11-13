#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactCraneYardStage.generated.h"

class AChaosImpactCharacter;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * ソロ ステージ4「クレーン操車場」: an anchor course over a container port at dusk.
 *
 * The way is a line of container stacks (and boxes hanging from the cranes, swinging back and forth over the yard and the
 * berth) far too far apart to jump: the player always holds an anchor ball (a new one the moment one is thrown), and
 * throws it at the striped post on the next stack to be whisked across. Fall, or be knocked out, and it is back to the
 * crane's girder at the start: there are no checkpoints. (A fall costs one health and puts them back on the stack they
 * threw from; with no health left, it is back to the girder, health full again.) Strong enemies stand on stacks off to the side and never move;
 * each, when the player is near, flashes red at them and sends a drive ball steered after them. The last stack, on the
 * container ship, has the goal on it.
 *
 * Between some stacks a bundle of containers hangs from a crane and swings across the way at the height an anchor flies:
 * it stops an anchor thrown into it and knocks a player it meets (in the air or standing) off. A cyan arrow over the
 * next post (and a ring where the anchor will set them down) shows where to go.
 *
 * Everything is built when play begins (the level holds this actor, its lighting and the player start). The course is
 * fixed data (34 stacks, 36 enemies), checked by ChaosImpact.Solo.CraneYard: every hop reachable by the anchor, no stack
 * near another but its neighbours.
 */
UCLASS(meta=(DisplayName="Chaos Impact Crane Yard Stage"))
class AChaosImpactCraneYardStage : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactCraneYardStage();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** One stack (or hanging box) of the way, in the stage's frame: X along the yard, Y towards the sea, Z up from the quay. */
	struct FPlatform
	{
		FVector2D Center;
		/** Its top (where the player stands). */
		float Top;
		/** Along its heading (the way it is crossed), and across. */
		float Length;
		float Width;
		/** The heading it is reached along (degrees): its post stands on the far side. */
		float Yaw;
		/** A hanging box: how far it swings either way across its heading, and in how many seconds there and back. */
		float Swing;
		float Period;
	};
	struct FSniper
	{
		FVector2D Where;
		float Top;
	};
	static const TArray<FPlatform>& GetPlatforms();
	static const TArray<FSniper>& GetSnipers();

	/** The anchor's reach that the course is laid out for (from where one lands to the next post). */
	static constexpr float CourseReach = 2250.0f;
	/** Hand height over the feet when throwing (the character's throw offset over its capsule's middle). */
	static constexpr float HandHeight = 151.0f;
	static constexpr float PostRadius = 42.0f;
	/** A container: a forty-footer is 1220 x 244 x 259 cm. */
	static constexpr float BoxHeight = 259.0f;
	/** Feet under this (over the quay: the ship's deck is at 520, the lowest stack's top at 790) is a fall: back to the start. */
	static constexpr float FallHeight = 650.0f;

	/** The platform's frame (world) now: its middle on its top, a hanging box where it has swung to. */
	FTransform GetPlatformFrame(int32 Index) const;
	/** The post on it (world, the post's foot on the top). */
	FVector GetPostFoot(int32 Index) const;
	/** Where the anchor sets the player down on it (just before its post, world, on the top). */
	FVector GetLandingPoint(int32 Index) const;
	/** The point is over this platform's top (within its footprint, world). */
	bool IsOverPlatform(int32 Index, const FVector& Point, float Margin = 0.0f) const;
	FVector GetStartPoint() const;
	/** The hanging boxes swing on this clock (server time, the same on every machine). */
	double GetSwingTime() const;

	/** Reached the goal (once; the stage is then cleared). */
	bool HasReachedGoal() const { return bGoalReached; }
	int32 GetFallCount() const { return Falls; }
	/** A swinging load: across the way from Platform - 1 to Platform, hung from Pivot (stage frame) on a cable. */
	struct FHazard
	{
		int32 Platform;
		float Amplitude;
		float Period;
		float Cable;
	};
	static const TArray<FHazard>& GetHazards();
	/** The swinging loads' boxes now (world): centre, rotation, half size. */
	void GetHazardBox(int32 Index, FVector& OutCentre, FQuat& OutRotation, FVector& OutHalf) const;
	/** Tests: the loads held still at the far end of their swing, out of the way. */
	void SetHazardsParked(bool bParked) { bHazardsParked = bParked; }
	/** The stack the player is standing on (the furthest reached), INDEX_NONE before the start is left. */
	int32 GetReachedPlatform() const { return Reached; }
	/** Tests: the goal forgotten, to play on. */
	void RestartForTests() { bGoalReached = false; }
	/** Development / tests: the enemies hold their fire. */
	void SetSnipersQuiet(bool bQuiet) { bSnipersQuiet = bQuiet; }
	/** Tests: the enemy characters standing (in GetSnipers order). */
	TArray<AChaosImpactCharacter*> GetSniperCharacters() const;

private:
	struct FBucketKey
	{
		UStaticMesh* Mesh = nullptr;
		UMaterialInterface* Material = nullptr;
		bool bCollide = false;
		bool operator==(const FBucketKey& Other) const { return Mesh == Other.Mesh && Material == Other.Material && bCollide == Other.bCollide; }
		friend uint32 GetTypeHash(const FBucketKey& Key) { return HashCombine(HashCombine(GetTypeHash(Key.Mesh), GetTypeHash(Key.Material)), Key.bCollide ? 1u : 0u); }
	};
	/** A box (the basic cube, 100 cm) of this size, centred at Where (stage frame), turned by Yaw. */
	void Box(const FVector& Where, const FVector& Size, UMaterialInterface* Material, bool bCollide, float Yaw = 0.0f, float Pitch = 0.0f, float Roll = 0.0f);
	/** An upright cylinder (radius, height) standing on Foot. */
	void Pole(const FVector& Foot, float Radius, float Height, UMaterialInterface* Material, bool bCollide);
	void Instance(UStaticMesh* Mesh, const FTransform& Where, UMaterialInterface* Material, bool bCollide);
	UInstancedStaticMeshComponent* Bucket(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCollide);
	UMaterialInstanceDynamic* Paint(const TCHAR* Parent, const FLinearColor& Color);
	UMaterialInstanceDynamic* Glow(const FLinearColor& Color, float Intensity);
	UPointLightComponent* Light(const FVector& Where, const FLinearColor& Color, float Intensity, float Radius);

	void BuildMaterials();
	void BuildGround();
	void BuildSea();
	/** A stack of containers in the stage frame: Footprint (along, across) turned by Yaw, from Base up to Top. */
	void BuildStack(const FVector2D& Center, float Yaw, const FVector2D& Footprint, float Base, float Top, int32 Seed, bool bCollide);
	/** One container: its middle at Center (bottom at Bottom), along Yaw, Length x 244 x 259. */
	void BuildContainer(const FVector2D& Center, float Bottom, float Yaw, float Length, UMaterialInterface* Body, bool bCollide);
	void BuildPlatforms();
	void BuildPost(int32 Index);
	void BuildHangingBox(int32 Index);
	void BuildSniperStacks();
	void BuildFillerYard();
	void BuildCranes();
	void BuildShip();
	void BuildGoal();
	void BuildSkyline();
	void BuildLamps();
	/** The ground under a stage-frame point: the quay, the ship's deck or the water. */
	float BaseUnder(const FVector2D& At) const;

	/** Ground taken (stacks, swings, legs, masts): nothing else stands there. */
	bool IsFree(const FVector2D& At, float Radius) const;
	void Occupy(const FVector2D& At, float Radius);
	void RegisterOccupied();
	/** A tall portal crane: two girders along RunYaw at Girder height, its legs moved out along the run until clear. */
	void BuildGantry(const FVector2D& Middle, float RunYaw, float HalfRun, float Girder, UMaterialInterface* Paint);
	void BuildHazards();
	void BuildMarker();
	void TickHazards(double Now);
	void TickMarker(double Now);
	/** The swing angle (degrees, across the way) of a load now. */
	float GetHazardAngle(int32 Index) const;
	void SpawnSnipers();
	void TickSnipers(double Now, float DeltaSeconds);
	bool IsAmongNearest(int32 Index, int32 Count) const;
	void TickPlayer(double Now);
	void TickSwings(double Now);
	void SendPlayerToStart(AChaosImpactCharacter* Player, bool bFell);
	/** A fall: one health less and back on the stack thrown from, or (none left) to the start. */
	void HandleFall(AChaosImpactCharacter* Player);
	AChaosImpactCharacter* GetPlayer() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;
	TMap<FBucketKey, TObjectPtr<UInstancedStaticMeshComponent>> Buckets;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BucketList;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> BoxColours;

	/** A hanging box: what swings (its container, spreader, post), and its cables (stretched to it from the trolley). */
	struct FSwing
	{
		int32 Platform = INDEX_NONE;
		TObjectPtr<USceneComponent> Carrier;
		TArray<TObjectPtr<UStaticMeshComponent>> Cables;
		TArray<FVector> CableTops;
		TArray<FVector> CableFeet;
	};
	TArray<FSwing> Swings;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> SwingCarriers;

	struct FSniperState
	{
		TWeakObjectPtr<AChaosImpactCharacter> Character;
		double NextFireAt = 0.0;
		double WarnUntil = -1.0;
		double ReleaseAt = -1.0;
		bool bThrown = false;
		TObjectPtr<UStaticMeshComponent> Beam;
		TObjectPtr<UStaticMeshComponent> Ring;
		TObjectPtr<UPointLightComponent> Flash;
	};
	TArray<FSniperState> SniperStates;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> SniperParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PostRings;
	/** Each post's shaft (its stripes and rings hang off it), Index - 1. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PostShafts;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GoalRing;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GoalBeam;

	struct FHazardParts
	{
		TObjectPtr<USceneComponent> Pivot;
		TObjectPtr<UStaticMeshComponent> Load;
		double LastKnockAt = -10.0;
	};
	TArray<FHazardParts> HazardParts;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> HazardPivots;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Marker;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MarkerRing;
	/** The arrow on the floor at the player's feet, pointing at the next landing. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> FootArrow;
	/** X, Y: the middle of a taken circle (stage frame); Z: its radius. */
	TArray<FVector> Occupied;
	int32 Reached = 0;
	float LastPlayerHealth = 0.0f;
	bool bHazardsParked = false;

	bool bBuilt = false;
	bool bGoalReached = false;
	bool bSnipersQuiet = false;
	int32 Falls = 0;
	double ResetFadeAt = -10.0;
};
