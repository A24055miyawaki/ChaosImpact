// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactGameMode.generated.h"

class AChaosImpactBallSpawner;
class AChaosImpactPlayerState;
class AChaosImpactVersusStage;
enum class EChaosImpactOnlinePhase : uint8;

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

	/** Online room: stop accepting new members. */
	void CloseRecruitment();
	bool IsOnlineRoom() const { return GetNetMode() == NM_ListenServer; }

	/**
	 * VS match, local or from an online room's host: spawns the stage and CPUs, then team select for a
	 * team battle or straight to the opening. Calling it again restarts with the new rules.
	 */
	void ConfigureVersusMatch(const FChaosImpactMatchRules& InRules);
	/** Team select: moves a human to the next team in Direction that still has room. */
	void ChangeMemberTeam(AChaosImpactPlayerState* Member, int32 Direction);
	/** Team select is over: CPUs fill the smallest teams and the opening starts. */
	void ConfirmTeamsAndStart();
	/** A player's machine has played the opening to the end; Ready? starts once every human's has. */
	void ReportIntroFinished(APlayerState* Member, double IntroStartedAt);
	/** Server: points for hitting or knocking out an opponent. Ignored outside the match phase. */
	void AwardMatchPoints(APlayerState* Scorer, int32 Points, bool bKnockout);
	/**
	 * Online lobby, host: the next match's rules. Recruitment closes; every member then presses 準備OK, and the
	 * match starts when all of them have (or when the wait runs out), after a short "starting" countdown.
	 */
	void DecideLobbyRules(const FChaosImpactMatchRules& InRules);
	void SetMemberReady(AChaosImpactPlayerState* Member, bool bReady);
	/** Host: lets new members in again; the decided rules are dropped. */
	void ReopenRecruitment();
	void RenameRoom(const FString& NewName);
	/** Server: where a knocked-out competitor comes back during a match, away from opponents. */
	bool ChooseMatchRespawn(const AActor* Character, FVector& OutLocation, FRotator& OutRotation) const;

	/**
	 * Pairs input devices with this machine's local players using the world URL
	 * (CIPadDevice options). Online clients have no game mode and call this themselves.
	 */
	static void ApplyLocalControllerAssignments(UWorld* World, int32 DesiredPlayers, int32 KeyboardPlayerIndex);

protected:
	virtual void BeginPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal) override;
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
	/** The engine measures ping per machine; a second player's state gets their machine's value. */
	void MirrorSplitscreenPings();
	/** Places held for second players whose machine has joined but who have not arrived yet. */
	int32 GetReservedRoomSlots() const;
	FTimerHandle PingMirrorTimer;
	/** Development: -CIReserveSlots=N pretends N more members are inside (capacity testing). */
	int32 DevReservedSlots = 0;
	FVector GetRoomAnchor() const;
	FTimerHandle ControllerReassignTimer;
	int32 NextJoinOrder = 1;
	FTimerHandle AutoStartMatchTimer;
	FVector RoomAnchor = FVector::ZeroVector;
	bool bHasRoomAnchor = false;
	int32 TrainingKeyboardPlayerIndex = 0;
	int32 LiveDesiredPlayerCount = INDEX_NONE;
	int32 LiveDesiredCPUCount = INDEX_NONE;
	double LocalPlayerSetupStartedAt = 0.0;

	// VS match flow: [TeamSelect] -> Intro -> Match -> Results -> (online) back to the lobby.
	void SetMatchPhase(EChaosImpactOnlinePhase NewPhase, float Seconds);
	void EnsureVersusStage();
	void StartMatchIntro();
	/** Every opening has finished (or the wait timed out): Ready?, then GO. */
	void StartReadyCountdown();
	void BeginMatchPlay();
	/** Players whose machine reported the current opening finished. */
	TSet<FObjectKey> IntroFinishedMembers;
	void EndMatch();
	/** Online: everyone returns to the room's lobby after the results. */
	void ReturnToLobbyAfterMatch();
	void ClearMatchBalls();
	void DestroyStageBallSpawners();
	void StartLocalMatchFromURL();
	void RunDevAutoVersus();

	// Online lobby: rules decided -> everyone ready (or the wait ran out) -> Starting countdown -> match.
	void UpdateLobbyReady();
	void BeginStartingCountdown();
	void StartDecidedMatch();
	void ClearReady();
	float GetReadyWaitSeconds() const;
	/** Server: members whose machine has this token (a reconnecting machine's old, stale places). */
	int32 CountMembersOfMachine(const FString& Token) const;
	/** Development: -CIAutoLobby=<teams>:<cpus>:<delay> closes recruitment and decides rules once two machines are in. */
	void RunDevAutoLobby();
	FTimerHandle LobbyReadyTimer;
	FTimerHandle DevBlackHoleTimer;
	FTimerHandle StartingTimer;
	FTimerHandle DevAutoLobbyTimer;
	FChaosImpactMatchRules DevAutoLobbyRules;
	/** Development: -CIReadyWaitSeconds= shortens the wait before a decided match starts by itself. */
	float ReadyWaitSecondsOverride = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactVersusStage> VersusStage;

	/**
	 * Stage spawned for VS matches when none is placed in the level. Point this at a Blueprint of
	 * Chaos Impact Versus Stage to use a layout edited in the editor.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactVersusStage> VersusStageClass;

	/** Where that stage is spawned. X/Y as set; Z follows the players' floor. Far from the training arena. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	FVector VersusStageLocation = FVector(0.0f, 30000.0f, 0.0f);

	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactBallSpawner>> StageBallSpawners;

	FTimerHandle MatchPhaseTimer;
	FTimerHandle LocalMatchTimer;
	FTimerHandle DevAutoVersusTimer;
	/** Development: -CIMatchSeconds= shortens the match; -CIAutoVersus=<teams>:<cpus>:<delay> starts one online. */
	float MatchDurationOverride = 0.0f;
	FChaosImpactMatchRules DevAutoVersusRules;
	/** A local VS level (CIMatch=1): the match starts once every local player exists. */
	bool bLocalMatchWorld = false;
};
