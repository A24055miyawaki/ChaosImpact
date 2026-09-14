// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChaosImpactGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AChaosImpactGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AChaosImpactGameMode();
	/** Live training changes; these never reload the current level. */
	void SetTrainingLocalPlayerCountLive(int32 DesiredPlayers);
	void SetTrainingCPUCountLive(int32 DesiredCPUCount);

	/** Online room: stop accepting new members. The match flow is not started for now. */
	void CloseRecruitment();
	/** Countdown → match → results. Currently not reachable from the menu. */
	void StartOnlineMatch();
	/** Counts a KO for the match scoreboard; ignored outside the match phase. */
	void RegisterKnockout(AController* Killer);
	bool IsOnlineRoom() const { return GetNetMode() == NM_ListenServer; }

	static constexpr float MatchCountdownSeconds = 3.0f;
	static constexpr float MatchSeconds = 180.0f;
	static constexpr float ResultsSeconds = 7.0f;

protected:
	virtual void BeginPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

private:
	void SetupLocalTrainingPlayers();
	/** Re-pairs every active local player after startup or an in-place player-count change. */
	void ApplyTrainingControllerAssignments(int32 DesiredPlayers, int32 KeyboardPlayerIndex);
	void SpawnTrainingCPU(const FVector& Anchor, const FRotator& Facing, int32 CPUIndex);
	void SyncTrainingCPUCount(int32 DesiredCPUCount, const FVector& Anchor, const FRotator& Facing);
	/** Re-pairs after spawning/possession settles; player creation can remap device ids. */
	void ReapplyTrainingControllerAssignments();
	void LogTrainingControllerRouting() const;
	void UpdateRoomMemberCount();
	void BeginOnlineMatch();
	void EndOnlineMatch();
	void ReturnToOnlineLobby();
	FVector GetRoomAnchor() const;
	FTimerHandle ControllerReassignTimer;
	FTimerHandle OnlinePhaseTimer;
	int32 NextJoinOrder = 1;
	/** Development overrides for multi-instance testing (-CIMatchSeconds=, -CIAutoStartMatch=). */
	float MatchDuration = MatchSeconds;
	FTimerHandle AutoStartMatchTimer;
	FVector RoomAnchor = FVector::ZeroVector;
	bool bHasRoomAnchor = false;
	int32 TrainingKeyboardPlayerIndex = 0;
	int32 LiveDesiredPlayerCount = INDEX_NONE;
	int32 LiveDesiredCPUCount = INDEX_NONE;
	double LocalPlayerSetupStartedAt = 0.0;
};



