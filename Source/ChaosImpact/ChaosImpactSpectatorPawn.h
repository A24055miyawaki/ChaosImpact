#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "ChaosImpactSpectatorPawn.generated.h"

class AChaosImpactCharacter;
class UChaosImpactChargeWidget;

/**
 * A spectator's camera. Flies freely inside the stage like a debug camera, or looks through any competitor's own
 * camera, with the match HUD (standings, names) that can be hidden for a clean view.
 *
 * Controls (keyboard / gamepad): move WASD / left stick, up and down E Q / RT LT, look with the right mouse button
 * held / right stick, faster with Shift / L3, next and previous player → ← / RB LB, free camera F / X, HUD H / Y,
 * and offline only, stop time T / A (the whole match freezes while the camera still flies).
 *
 * Online, the host spawns it for the spectator's controller and replicates it to that machine only; the
 * spectator's machine moves it by itself (its movement is not replicated).
 */
UCLASS()
class AChaosImpactSpectatorPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	AChaosImpactSpectatorPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Puts the camera above the stage, looking at its middle. */
	void PlaceOverStage(const FVector& StageCenter);
	/** The competitor whose camera is shown, or null in free flight. */
	AChaosImpactCharacter* GetFollowedCharacter() const { return Followed.Get(); }
	bool IsHudHidden() const { return bHudHidden; }
	/** Steps to the next (+1) or previous (-1) competitor's camera; from free flight it starts at the first. */
	void CycleFollow(int32 Direction);
	void StopFollowing();
	void SetHudHidden(bool bHide);
	/** Offline only: freezes the match (see AChaosImpactPlayerController::SetSpectateTimeStopped). */
	void SetTimeStopped(bool bStop);
	bool IsTimeStopped() const;

	/** How far past the stage's edge, and how high, the free camera may go. */
	static constexpr float BoundsMargin = 600.0f;
	static constexpr float MinHeight = 60.0f;
	static constexpr float MaxHeight = 3200.0f;

private:
	/**
	 * Online: tells the host where this spectator is looking. The host decides what each machine receives by how
	 * near it is to that machine's view, and its copy of this camera would otherwise stay where it was spawned.
	 */
	UFUNCTION(Server, Unreliable)
	void ServerSyncView(FVector_NetQuantize Location);
	void UpdateViewSync(APlayerController* Viewer, float DeltaSeconds);
	void UpdateFreeFlight(APlayerController* Viewer, float DeltaSeconds);
	/** Over the stage when a match begins, back over the room when it ends. */
	void UpdatePlacement(APlayerController* Viewer);
	void UpdateDevCycle(float DeltaSeconds);
	void EnsureHud(APlayerController* Viewer);
	TArray<AChaosImpactCharacter*> GetFollowTargets() const;
	FVector ClampToStage(const FVector& Location) const;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactChargeWidget> Hud;

	TWeakObjectPtr<AChaosImpactCharacter> Followed;
	FVector FlyVelocity = FVector::ZeroVector;
	bool bHudHidden = false;
	/** Placed for the current kind of view (room or stage) on this machine. */
	bool bPlaced = false;
	bool bPlacedForMatch = false;
	/** Where it first appeared in the room, to come back to after a match. */
	FVector RoomHome = FVector::ZeroVector;
	/** Development (-CIDevSpectateCycle=<seconds>): steps through the players by itself, with screenshots. */
	float DevCycleSeconds = 0.0f;
	float DevCycleElapsed = 0.0f;
	int32 DevCycleShots = 0;
	float ViewSyncElapsed = 0.0f;
};
