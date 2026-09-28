// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactGameMode.generated.h"

class AChaosImpactBallSpawner;
class AChaosImpactPlayerState;
class AChaosImpactStageBase;
class AChaosImpactSpectatorPawn;
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
	/**
	 * Online lobby: a member switches between playing and watching (before the match starts). Watching needs a
	 * free spectator place, a machine with a single player and another player left in the room.
	 */
	void SetMemberSpectating(APlayerController* MemberController, bool bSpectate);
	/** Host: lets new members in again; the decided rules are dropped. */
	void ReopenRecruitment();
	void RenameRoom(const FString& NewName);
	/** Server: where a knocked-out competitor comes back during a match, away from opponents. */
	bool ChooseMatchRespawn(const AActor* Character, FVector& OutLocation, FRotator& OutRotation) const;
	/** Server: Location is off the VS stage in use (past its walls or below its floor). False with no stage. */
	bool IsOutsideStage(const FVector& Location) const;

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
	/** Local match: every local player watches through a spectator camera (true) or plays again (false). */
	void SetLocalPlayersSpectating(bool bSpectate);
	/** A roster character for a new CPU: the one fewest players are using, picked at random among equals. */
	int32 PickCPUCharacter(const APlayerState* NewCPU) const;
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
	/** Development (-CIReserveSpectators=N): spectator places taken by nobody, to try a full room. */
	int32 DevReservedSpectators = 0;
	/** A spectator camera over the stage in a match, or over the room otherwise. */
	APawn* SpawnSpectatorCamera(AController* Viewer);
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
	/** Makes the stage for StageIndex (0 standard, 1 splash) the one in use, replacing a different one. */
	void EnsureVersusStage(int32 StageIndex);
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
	FTimerHandle DevWindTimer;
	FTimerHandle StartingTimer;
	FTimerHandle DevAutoLobbyTimer;
	FChaosImpactMatchRules DevAutoLobbyRules;
	/** Development: -CIReadyWaitSeconds= shortens the wait before a decided match starts by itself. */
	float ReadyWaitSecondsOverride = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<AChaosImpactStageBase> VersusStage;

	/** While set, new CPUs appear on these floor points instead of around the anchor (a VS stage's spawns). */
	TArray<FVector> CPUSpawnPoints;

	/**
	 * Stage spawned for VS matches when none is placed in the level. Point this at a Blueprint of
	 * Chaos Impact Versus Stage to use a layout edited in the editor.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> VersusStageClass;

	/**
	 * ステージ2 on the stage select screen. Point this at BP_SplashStage, a Blueprint of Chaos Impact
	 * Splash Stage, to use the layout edited in the editor.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> SplashStageClass;

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
