#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactTempleStage.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Meshes and materials the temple is built from (solo stage 15's sandstone, runes and clouds). */
struct FChaosImpactTempleAssets
{
	UStaticMesh* Cube = nullptr;
	UStaticMesh* Cone = nullptr;
	UStaticMesh* Plane = nullptr;
	UStaticMesh* Cylinder = nullptr;
	UStaticMesh* Chamfer = nullptr;
	UStaticMesh* Band = nullptr;
	UMaterialInterface* Floor = nullptr;
	UMaterialInterface* Bridge = nullptr;
	UMaterialInterface* Pale = nullptr;
	UMaterialInterface* Sand = nullptr;
	UMaterialInterface* Dark = nullptr;
	UMaterialInterface* RuneTeal = nullptr;
	UMaterialInterface* RuneGold = nullptr;
	UMaterialInterface* Gold = nullptr;
	UMaterialInterface* Moss = nullptr;
	UMaterialInterface* FieldTeal = nullptr;
	UMaterialInterface* FieldSoft = nullptr;
	UMaterialInterface* FieldAmber = nullptr;
	UMaterialInterface* NeonAmber = nullptr;
	UMaterialInterface* Cloud = nullptr;
	UMaterialInterface* Pine = nullptr;
	UMaterialInterface* Bark = nullptr;
};

/**
 * VS ステージ3: the sky temple (after solo stage 15's temple over the sea of clouds at sunset). Nine floating islands,
 * four-fold symmetric so no side is better:
 *  - the temple island in the middle: a three-step altar (its ball pad only ever gives special balls: 祝福の祭壇) in a
 *    circle of eight columns
 *  - four islands north, south, east and west, on wide bridges, each with a seal in its middle where a pillar of light
 *    comes down now and then (光の柱: warned a moment before; whoever is in it when it strikes takes 1 and is thrown up;
 *    a dash goes through it)
 *  - four small islands at the corners on narrow bridges between those, each with a portal gate facing the middle:
 *    walking into one comes straight out of the opposite corner's, and a ball thrown into one flies on out of that
 *    one (and hurts whoever it meets there)
 * The island edges hold players in (invisible walls that stop characters only: a ball thrown off an island falls).
 * Beyond: the sea of clouds, drifting islets and a great stone ring turning slowly behind the temple.
 */
UCLASS(Blueprintable, meta=(DisplayName="Chaos Impact Temple Stage"))
class AChaosImpactTempleStage : public AChaosImpactStageBase
{
	GENERATED_BODY()

public:
	AChaosImpactTempleStage();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual TArray<FVector> GetSpawnPoints() const override;
	virtual TArray<FVector> GetBallPoints() const override;
	virtual float GetHalfExtent() const override { return TempleHalfExtent; }
	/** Off the islands (in the clouds), below them, or past the playable square. */
	virtual bool IsOutside(const FVector& Location) const override;

	static constexpr float TempleHalfExtent = 3500.0f;
	static constexpr float CentreRadius = 1550.0f;
	static constexpr float SideDistance = 2700.0f;
	static constexpr float SideRadius = 800.0f;
	static constexpr float CornerDistance = 2350.0f;
	static constexpr float CornerRadius = 620.0f;
	static constexpr float SealRadius = 380.0f;
	static constexpr float PillarWarnSeconds = 2.6f;
	static constexpr float PillarStrikeSeconds = 1.2f;
	static constexpr float PillarDamage = 1.0f;
	static constexpr float PortalAperture = 230.0f;

	/** Seconds between one pillar of light and the next, at random between these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Temple Stage", meta=(ClampMin="3.0"))
	float PillarIntervalMin = 11.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Temple Stage", meta=(ClampMin="3.0"))
	float PillarIntervalMax = 15.0f;
	/** The first pillar, this long into the match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Temple Stage", meta=(ClampMin="0.0"))
	float FirstPillarAfter = 8.0f;

	/** Seal (0 north, 1 east, 2 south, 3 west) and where a pillar strikes, stage-relative on the floor. */
	static FVector GetSealOffset(int32 Seal);
	/** Portal gate (0-3, corners NE, SE, SW, NW): stage-relative middle of its opening on the floor, and the way it faces. */
	static FVector GetPortalOffset(int32 Portal);
	static FVector GetPortalFacing(int32 Portal);
	/** The gate a portal sends you out of (the opposite corner). */
	static int32 GetLinkedPortal(int32 Portal) { return (Portal + 2) % 4; }

	/** The seal a pillar is coming down on (-1: none), and how far it is (0-1 the warning, 1-2 the strike). */
	int32 GetPillarSeal() const { return PillarSeal; }
	float GetPillarProgress() const;
	/** Tests: brings the next pillar down on Seal now (server). */
	void StartPillar(int32 Seal);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Pillar();
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPortal(int32 FromPortal, FVector_NetQuantize From, FVector_NetQuantize To);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPillarStrike(int32 Seal);

	UPROPERTY(ReplicatedUsing=OnRep_Pillar)
	int32 PillarSeal = -1;
	/** Server world time the current pillar's warning began (the game state's clock). */
	UPROPERTY(ReplicatedUsing=OnRep_Pillar)
	float PillarStartedAt = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Temple Stage", meta=(MakeEditWidget))
	TArray<FVector> SpawnPointOffsets;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Temple Stage", meta=(MakeEditWidget))
	TArray<FVector> BallPointOffsets;

private:
	UStaticMeshComponent* AddPart(USceneComponent* Parent, const FString& Name, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollision = false,
		bool bShadow = true);
	/** A block: Size its full size about its middle (Location). */
	UStaticMeshComponent* AddBox(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material, const FVector& Location,
		const FVector& Size, float Yaw = 0.0f, bool bCollision = true, bool bChamfer = false);
	/** An upright cylinder standing on Location. */
	UStaticMeshComponent* AddCylinder(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material,
		const FVector& Location, float Diameter, float Height, bool bCollision = true);
	/** A flat band of light or gold lying on the floor. */
	UStaticMeshComponent* AddRing(USceneComponent* Parent, const FString& Name, UMaterialInterface* Material, const FVector& Location,
		float Radius, float Thickness);
	/** A wall that stops characters only (balls and floor traces pass through it). */
	void AddFence(const FString& Name, const FVector& From, const FVector& To);
	/** A floating island: a flagstone top at Height, a stone lip, a rock tapering away underneath. */
	void BuildIsland(const FString& Name, const FVector& Centre, float Radius, float Depth, const TArray<float>& OpeningsYaw,
		const TArray<float>& OpeningWidths);
	void BuildBridge(const FString& Name, const FVector& From, const FVector& To, float Width);
	void BuildColumn(const FString& Name, const FVector& At, float Height);
	void BuildSeal(int32 Seal);
	void BuildPortal(int32 Portal);
	void BuildScenery();

	void TickPillarServer(double Now);
	void UpdatePillarLook(float DeltaSeconds);
	void TickPortalsServer();
	void TickAltarServer();
	bool IsMatchOn() const;
	double GetServerNow() const;
	void StrikePillar(int32 Seal);
	void WarpCharacter(AChaosImpactCharacter* Character, int32 Portal);
	void WarpBall(AChaosImpactBall* Ball, int32 Portal);

	FChaosImpactTempleAssets Assets;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> RingRoot;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> IsletRoots;
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> PortalSwirls;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SealDiscs;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SealRings;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PillarBeams;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PillarWarnBeams;
	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> SealLights;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> AltarGlows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SealDiscLooks;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PillarLooks;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WarnLooks;

	/** Server: when the next pillar comes, and who it already struck. */
	double NextPillarAt = -1.0;
	bool bPillarStruck = false;
	TSet<TWeakObjectPtr<AChaosImpactCharacter>> PillarVictims;
	/** Server: who may not go through a portal again yet, and where each flying ball was a frame ago. */
	TMap<TWeakObjectPtr<AActor>, double> PortalCooldown;
	TMap<TWeakObjectPtr<AChaosImpactBall>, FVector> BallLastLocation;
	bool bAltarBlessed = false;
	double NextAltarCheckAt = 0.0;
	float LookClock = 0.0f;
	int32 LastStrikeShown = -1;
	float StrikeFlash = 0.0f;
};
