#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactStageBase.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMesh;
class UPrimitiveComponent;

USTRUCT(BlueprintType)
struct FChaosImpactCrashRoutePart
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Start = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector End = FVector(500, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Width = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Thickness = 36.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> Material;
};

/**
 * What the game mode needs from a VS stage: where players appear, where the ball pads go and its middle.
 * Every stage is a square of GetHalfExtent() around its actor location; the game state passes that on so the
 * spectator camera stays over the right area. Subclasses build their own geometry (Versus Stage, Splash Stage).
 */
UCLASS(Abstract)
class AChaosImpactStageBase : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactStageBase();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** (Runs the stage effects below; the stages call it through Super::Tick.) */
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Props up on the walls (billboards, graffiti) that each player's own view drops while that player's camera is
	 * close to them, so they never stand between the camera and the play (the walls themselves fade by their material:
	 * see MakeWallsSeeThrough). The player controllers read these every frame.
	 */
	const TArray<TWeakObjectPtr<UPrimitiveComponent>>& GetNearCameraParts() const { return NearCameraParts; }
	/** Half the floor width of the standard stage. */
	static constexpr float HalfExtent = 3000.0f;

	/** Half the floor width of this stage (the playable square inside its outer walls). */
	virtual float GetHalfExtent() const { return HalfExtent; }

	/** World positions on the floor surface where players may appear. */
	virtual TArray<FVector> GetSpawnPoints() const { return {}; }
	/** World positions on the floor surface for ball pads. */
	virtual TArray<FVector> GetBallPoints() const { return {}; }
	/** Extra ball pad world positions added in Crash Ball mode to keep ball supply plentiful along push routes. */
	virtual TArray<FVector> GetCrashBallExtraBallPoints() const { return ToWorld(CrashBallExtraBallOffsets); }
	FVector GetCenter() const { return GetActorLocation(); }

	/** World position where the Giant Ball rests on the floor at the start of a Crash Ball match. */
	virtual FVector GetCrashBallStartFloorLocation() const;

	/** World transform for a team's home goal (Team 0 = Red on -X side, Team 1 = Blue on +X side). */
	virtual FTransform GetCrashBallGoalTransform(int32 TeamIndex) const;

	/**
	 * Computes both teams' current position count (100 at center -> 1 just in front of enemy goal) and signed
	 * territory progress (-1.0 at Team 0 goal .. 0.0 at center .. +1.0 at Team 1 goal) using 2D route projection.
	 */
	virtual void ComputeCrashBallCounts(const FVector& WorldBallLocation, int32& OutTeam0AttackCount,
		int32& OutTeam1AttackCount, float& OutSignedProgress) const;

	/** Returns the horizontal unit direction in which AttackingTeam (0 or 1) should push the Giant Ball from WorldBallLocation. */
	virtual FVector GetCrashBallPushDirection(int32 AttackingTeam, const FVector& WorldBallLocation) const;

	/**
	 * クラッシュボール's goals at opposite corners: red's at Red, blue's at Blue (stage frame, on the floor), each facing the
	 * middle, and the ways round the obstacles from the middle to each (found with ChaosImpact.CrashBall.DiagonalProbe).
	 */
	void SetCrashDiagonalGoals(const FVector& Red, const FVector& Blue, const TArray<FVector>& ToBlue, const TArray<FVector>& ToRed);

	/** How far round each goal the ground is cleared of obstacles in クラッシュボール (pillars, props, warp pads). */
	static constexpr float CrashGoalClearRadius = 700.0f;
	/** Something to clear for クラッシュボール: standing up within CrashGoalClearRadius of a goal, and not floor or outer wall. */
	bool IsInCrashGoalArea(const UStaticMeshComponent* Part) const;

	/** Enables or disables Crash Ball-specific route improvements and obstacle adjustments on this stage. */
	virtual void ConfigureCrashBallMode(bool bEnable);
	bool IsCrashBallModeActive() const { return bCrashBallModeActive; }

	/** Stage-relative floor offset for the Giant Ball's starting center position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	FVector CrashBallStartOffset = FVector::ZeroVector;

	/** Stage-relative floor offset for Team 0 (Red) home goal. Team 1 attacks this goal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	FVector Team0GoalOffset = FVector(-2400.0f, 0.0f, 0.0f);

	/** Stage-relative rotation for Team 0 (Red) home goal (Forward +X faces stage center). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	FRotator Team0GoalRotation = FRotator(0.0f, 0.0f, 0.0f);

	/** Stage-relative floor offset for Team 1 (Blue) home goal. Team 0 attacks this goal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	FVector Team1GoalOffset = FVector(2400.0f, 0.0f, 0.0f);

	/** Stage-relative rotation for Team 1 (Blue) home goal (Forward -X faces stage center). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	FRotator Team1GoalRotation = FRotator(0.0f, 180.0f, 0.0f);

	/**
	 * Optional stage-relative waypoints from center to Team 1's goal (Team 0's attack route).
	 * When empty, the direct segment from CrashBallStartOffset to Team1GoalOffset is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	TArray<FVector> Team0AttackWaypoints;

	/**
	 * Optional stage-relative waypoints from center to Team 0's goal (Team 1's attack route).
	 * When empty, the direct segment from CrashBallStartOffset to Team0GoalOffset is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	TArray<FVector> Team1AttackWaypoints;

	/** Maximum lateral distance from the route axis that still counts at full progression. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	float CrashBallRouteMaxLateralDistance = 2200.0f;

	/** Additional stage-relative ball spawner offsets active during Crash Ball matches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	TArray<FVector> CrashBallExtraBallOffsets;

	/** Additional wide ramps/bridges; endpoints describe their walkable top surface, in stage coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	TArray<FChaosImpactCrashRoutePart> CrashRouteParts;
	/** Names/prefixes of specific obstacles replaced only in this rule. All other stage geometry stays intact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|Crash Ball")
	TArray<FString> CrashRemovedObstaclePrefixes;
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Crash Ball")
	bool bPreviewCrashBallLayout = false;
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Crash Ball", meta=(ClampMin="320", ClampMax="1200"))
	float CrashGoalWidth = 640.f;
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Crash Ball", meta=(ClampMin="280", ClampMax="800"))
	float CrashGoalHeight = 400.f;
	UPROPERTY(EditAnywhere, Category="Chaos Impact|Crash Ball")
	TSoftObjectPtr<UMaterialInterface> CrashRouteMaterial;

	/**
	 * A character at Location has left the stage: past the outer walls or below the floor, where it can do
	 * nothing (a bug, a glitch through a wall). The match knocks it out so it respawns on the stage.
	 */
	virtual bool IsOutside(const FVector& Location) const;

protected:
	virtual void BeginPlay() override;
	/** How far past the walls still counts as inside (the walls' own thickness and then some). */
	static constexpr float OutsideMargin = 300.0f;
	/** How far below the floor still counts as inside (a character's middle stands about 96 above it). */
	static constexpr float OutsideDepth = 500.0f;

	UPROPERTY(ReplicatedUsing=OnRep_CrashLayout)
	bool bCrashBallModeActive = false;
	UFUNCTION() void OnRep_CrashLayout();
	void RebuildCrashRoute();
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> CrashRouteMeshes;
	TMap<TWeakObjectPtr<UStaticMeshComponent>, TPair<bool, TEnumAsByte<ECollisionEnabled::Type>>> CrashOriginalObstacles;

	/** Stage-relative offsets to world positions. */
	TArray<FVector> ToWorld(const TArray<FVector>& Offsets) const;

	/**
	 * Gives the blocks whose names start with one of Prefixes the see-through wall look: they fade out, dithered, where
	 * they come nearer the camera than the player, so a wall on the camera's side of the arena never hides the floor or
	 * a player standing by it (Content/ChaosImpact/Versus/M_CI_WallFade, Scripts/Versus/make_wall_fade_materials.py).
	 * Blocks wearing GlowSource get the glowing version. Call before the palette goes on (it sets their Color).
	 */
	void MakeWallsSeeThrough(const TArray<FString>& Prefixes, const UMaterialInterface* GlowSource);
	/** Lists the components whose names start with one of Prefixes as near-camera parts (GetNearCameraParts). */
	void HideNearCamera(const TArray<FString>& Prefixes);

	/**
	 * Stage gimmick effects, each machine's own (cosmetic). A ring of light on the floor going from one radius to another
	 * and fading as it goes (out: a shock spreading from a strike; in: something gathering on a spot that is about to go
	 * off), and a Niagara effect stopped after a while (0: left to end by itself).
	 */
	void PlayFloorRing(const FVector& WorldAt, const FLinearColor& Color, float FromRadius, float ToRadius, float Seconds,
		float Thickness = 8.0f, float Intensity = 8.0f);
	UNiagaraComponent* PlayStageEffect(const TCHAR* Path, const FVector& WorldAt, const FVector& Scale, float Seconds,
		const FLinearColor* Color = nullptr, const FRotator& Rotation = FRotator::ZeroRotator);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WallFadeMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WallFadeGlowMaterial;

private:
	TArray<TWeakObjectPtr<UPrimitiveComponent>> NearCameraParts;

	static constexpr int32 FloorRingSegments = 32;
	struct FFloorRing
	{
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Segments;
		FVector At = FVector::ZeroVector;
		TWeakObjectPtr<UMaterialInstanceDynamic> Look;
		double StartedAt = 0.0;
		float Seconds = 1.0f;
		float From = 100.0f;
		float To = 300.0f;
		float Thickness = 8.0f;
		float Intensity = 8.0f;
	};
	TArray<FFloorRing> FloorRings;
	struct FTimedEffect
	{
		TWeakObjectPtr<UNiagaraComponent> Effect;
		double StopAt = 0.0;
	};
	TArray<FTimedEffect> TimedEffects;
	UPROPERTY()
	TObjectPtr<UStaticMesh> FxRingMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> FxSegmentMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FxGlowMaterial;
};
