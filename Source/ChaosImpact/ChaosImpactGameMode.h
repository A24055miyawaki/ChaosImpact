// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactGameMode.generated.h"

class AChaosImpactBallSpawner;
class AChaosImpactGameState;
class AChaosImpactPlayerState;
class AChaosImpactStageBase;
class AChaosImpactSpectatorPawn;
class AChaosImpactCrashGoal;
class AChaosImpactTrainingArena;
class AChaosImpactCPUController;
enum class EChaosImpactOnlinePhase : uint8;
enum class EChaosImpactDuelStep : uint8;
enum class EChaosImpactDuelEnd : uint8;

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
	/** 訓練場: a CPU of its own (training's CPU count leaves it alone), standing at Location. */
	class AChaosImpactCharacter* SpawnDojoCPU(const FVector& Location, const FRotator& Rotation, int32 Number);
	/** Live training changes; these never reload the current level. */
	void SetTrainingLocalPlayerCountLive(int32 DesiredPlayers);
	void SetTrainingCPUCountLive(int32 DesiredCPUCount);

	/** Online room: stop accepting new members. */
	void CloseRecruitment();
	/** The VS stage and where it goes (the title demo plays on one there; see AChaosImpactTitleDemo). */
	TSubclassOf<AChaosImpactStageBase> GetVersusStageClass() const { return VersusStageClass; }
	FVector GetVersusStageLocation() const { return VersusStageLocation; }
	bool IsOnlineRoom() const { return GetNetMode() == NM_ListenServer; }

	/**
	 * VS match, local or from an online room's host: spawns the stage and CPUs, then team select for a
	 * team battle or straight to the opening. Calling it again restarts with the new rules.
	 */
	void ConfigureVersusMatch(const FChaosImpactMatchRules& InRules);
	/**
	 * クラッシュボール, server: a thrown ball pushed the giant ball. A push toward the opponents' goal earns the
	 * thrower a point; the attacking team's hits keep overtime going.
	 */
	void NotifyCrashBallHit(APawn* Thrower, int32 TeamIndex, bool bCharged, const FVector& PushDirection);
	void NotifyCrashBallRecovered();
	FVector GetCrashPushDirection(int32 TeamIndex, const FVector& Location) const;
	/** クラッシュボール: the stage's spawn points on Team's own half (its goal's side), or all of them if none. */
	TArray<FVector> GetCrashTeamSpawnPoints(int32 Team) const;
	/** Team select: moves a human to the next team in Direction that still has room. */
	void ChangeMemberTeam(AChaosImpactPlayerState* Member, int32 Direction);
	/** Team select: moves a human straight to Team (a click on its card), if it has room. */
	void SetMemberTeam(AChaosImpactPlayerState* Member, int32 Team);
	/** Team select is over: CPUs fill the smallest teams and the opening starts. */
	void ConfirmTeamsAndStart();
	/** Online rooms pick teams in the lobby once team rules are decided: every player gets one (their own pick kept while
	 * it fits, the rest to the smallest team), watchers and rooms without team rules none. */
	void FitLobbyTeams();
	/** A player's machine has played the opening to the end; Ready? starts once every human's has. */
	void ReportIntroFinished(APlayerState* Member, double IntroStartedAt);
	/** Server: points for hitting or knocking out an opponent. Ignored outside the match phase. */
	void AwardMatchPoints(APlayerState* Scorer, int32 Points, bool bKnockout);
	/** The results' numbers (during a VS match only). */
	void RecordThrow(APawn* Thrower, bool bSpecial);
	void RecordHit(APawn* Source, APawn* Victim, const AActor* Causer);
	void RecordDodge(APawn* Dasher);
	/** Online: this member left the results for the lobby. */
	void ReturnFromResults(APlayerState* Member);
	/** A matched 1v1's results: this player asks for 同じ相手と再戦; once both have, the next game begins. */
	void RequestRematch(APlayerState* Member);
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
	/** ランクマッチ (the duel), server: a competitor was just knocked out (the round ends; both at once: drawn). */
	void NotifyDuelKnockout(APawn* Victim);
	/** ランクマッチ, server: Victim was hit (in sudden death the first hit takes the round). */
	void NotifyDuelHit(APawn* Victim);
	// ---- トーナメント (ChaosImpactGameModeTournament.cpp) ----
	/**
	 * Starts the tournament the lobby decided (Rules.Tournament), with everyone in the room who plays and its CPUs; or,
	 * People given (ローカルトーナメント 1試合ずつ), with those people instead (nobody's player yet).
	 */
	void StartTournament(const TArray<FChaosImpactTournamentEntrant>* People = nullptr);

	// ---- ローカルトーナメント (ChaosImpactGameModeLocalTourney.cpp) ----
	/** The level opened for it (?CILocalTournament=1): the registration (UChaosImpactLocalTourneySubsystem) is played. */
	void BeginLocalTournament();
	/** 1試合ずつ: a match called whose people have not taken their controllers yet (INDEX_NONE: none). */
	int32 GetLocalSeatRequest() const;
	/**
	 * Its people took them: SideDevices per side, -1 the keyboard, a pad's input device id, or anything below for a
	 * CPU. They become this machine's players for the match (one or two, split screen), and it begins.
	 */
	void SeatLocalTournamentMatch(int32 MatchIndex, const TArray<int32>& SideDevices);
	/** A member's choice while not playing: practise, watch (a match, or any being played), or the bracket. */
	void SetTournamentWait(APlayerController* Member, EChaosImpactTournamentWait Wait, int32 WatchMatch = INDEX_NONE);
	/** The host's answer for two CPUs next (one match at a time): 0 watch it, 1 skip it, 2 skip every one. */
	void AnswerTournamentCPUMatch(APlayerController* Member, int32 Choice);
	/** The tournament is over (or given up by the host): everyone back to the lobby. */
	void EndTournament();
	/** The champion's screen cut short (Enter / A on it). */
	void SkipTournamentResults(APlayerController* Member);
	/** A public tournament room: its start (server time); it starts then with two people or more. */
	void ScheduleTournament(double StartsAt);

	/** A matched room (ランダムマッチ / ランクマッチ): how long it waits for everyone matched before it starts anyway. */
	static constexpr float MatchmadeWaitSeconds = 25.0f;
	/** A matched room closes this long after its results began, whoever is still looking at them. */
	static constexpr float MatchmadeResultsSeconds = ChaosImpactMatch::ResultsViewLimitSeconds + 2.0f;

	/**
	 * Pairs input devices with this machine's local players using the world URL
	 * (CIPadDevice options). Online clients have no game mode and call this themselves.
	 */
	/** PadDevices: the pads' input device ids in player order (else the level's CIPadDevice options). */
	static void ApplyLocalControllerAssignments(UWorld* World, int32 DesiredPlayers, int32 KeyboardPlayerIndex,
		const TArray<int32>* PadDevices = nullptr);

protected:
	virtual void BeginPlay() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	/** An online room moving to its next level: each guest is sent only the level and why (it keeps its own options). */
	virtual APlayerController* ProcessClientTravel(FString& URL, bool bSeamless, bool bAbsolute) override;
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
	/** ローカルトーナメント: the seats' devices (instead of the level's options) while its players change match by match. */
	bool bUseLocalSeatDevices = false;
	TArray<int32> LocalSeatPadDevices;
	int32 LocalSeatKeyboard = 0;
	int32 LocalSeatedMatch = INDEX_NONE;
	FTimerHandle LocalTournamentTimer;
	double LocalTournamentWaitSince = 0.0;
	bool IsLocalSeatPending(int32 MatchIndex) const;
	/** After a seated match: its players go back to being the one watching player. */
	void UnseatLocalTournament();
	int32 LiveDesiredPlayerCount = INDEX_NONE;
	int32 LiveDesiredCPUCount = INDEX_NONE;
	double LocalPlayerSetupStartedAt = 0.0;

	// VS match flow: [TeamSelect] -> Intro -> Match -> Results -> (online) back to the lobby.
	void SetMatchPhase(EChaosImpactOnlinePhase NewPhase, float Seconds);
	/** Makes the stage for StageIndex (0..5), or the duel arena, the one in use, replacing a different one. */
	void EnsureVersusStage(int32 StageIndex, bool bDuelStage = false);
	void StartMatchIntro();
	/** Every opening has finished (or the wait timed out): Ready?, then GO. */
	void StartReadyCountdown();
	void BeginMatchPlay();
	/** Players whose machine reported the current opening finished. */
	TSet<FObjectKey> IntroFinishedMembers;
	void EndMatch();
	void InitializeRoyale();
	void UpdateRoyale();
	void HandleRoyaleTimeExpired();
	void FinishRoyale(const TArray<int32>& Winners);
	void WatchAfterRoyaleElimination(APlayerController* Player);
	FTimerHandle RoyaleTimer;
	void SetupCrashMatch();
	void ClearCrashMatch();
	void UpdateCrashMatch();
	void HandleMatchTimeExpired();
	FTimerHandle CrashMatchTimer;
	UPROPERTY(Transient) TArray<TObjectPtr<AChaosImpactCrashGoal>> CrashGoals;
	/** How long an attacking team can go without a direct hit (or with the ball back in its own half) in overtime. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Crash Ball", meta=(ClampMin="0.5"))
	float CrashOvertimeGraceSeconds = ChaosImpactMatch::CrashOvertimeGraceSeconds;
	/** Overtime ends after this long whatever happens (the leader wins). */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Crash Ball", meta=(ClampMin="10"))
	float CrashOvertimeMaxSeconds = 60.f;
	double CrashLastDirectHit[2] = {-100., -100.};
	/** Overtime: since when the ball has been back in the overtime team's own half (-1 while it is not). */
	double CrashOvertimeRetreatSince = -1.;
	/** Online: everyone returns to the room's lobby after the results. */
	void ReturnToLobbyAfterMatch();
	void ClearMatchBalls();
	void DestroyStageBallSpawners();
	void StartLocalMatchFromURL();
	void RunDevAutoVersus();

	// Online lobby: rules decided -> everyone ready (or the wait ran out) -> Starting countdown -> match.
	void UpdateLobbyReady();

	// ---- An online room plays each match in its own level (the stage's, or the duel arena): the host opens it with
	// the whole room following (everyone loads it and comes back in), and the lobby again after the results.
	/**
	 * Opens Level for the whole room, with the options the room was opened with and Purpose (CIRoomMatch=1 /
	 * CIRoomReturn=1); first remembers who everyone was (UChaosImpactSessionSubsystem::RoomCarry).
	 */
	void TravelRoom(const FString& Level, const TCHAR* Purpose);
	void TravelRoomToMatch();
	/** In the match level: the match is waited for until everyone the lobby sent has come back in. */
	void BeginRoomMatchLevel(AChaosImpactGameState* RoomState);
	void UpdateRoomArrivals();
	/** Who a member is from one of the room's levels to the next: their machine and which of its players. */
	FString GetCarryKey(const AChaosImpactPlayerState* Member, bool bSecondOfMachine) const;
	bool IsCarriedMachine(const FString& MachineToken) const;
	/** An online room's match level (CIRoomMatch=1). */
	bool bRoomMatchLevel = false;
	double RoomArrivalDeadline = 0.0;
	FTimerHandle RoomArrivalTimer;
	FTimerHandle RoomReturnTimer;
	/** How long the match level waits for everyone to arrive before it starts with whoever came. */
	static constexpr float RoomArrivalWaitSeconds = 25.0f;
	void BeginStartingCountdown();
	void StartDecidedMatch();
	void ClearReady();
	float GetReadyWaitSeconds() const;
	/** Server: members whose machine has this token (a reconnecting machine's old, stale places). */
	int32 CountMembersOfMachine(const FString& Token) const;
	/** Development: -CIAutoLobby=<teams>:<cpus>:<delay> closes recruitment and decides rules once two machines are in. */
	void RunDevAutoLobby();
	FTimerHandle LobbyReadyTimer;
	FTimerHandle HistoryTimer;
	FTimerHandle ResultsViewTimer;
	/** Adds everyone's points to their graph. */
	void SampleMatchHistory();
	/** Online: nobody holds the lobby on the results any longer. */
	void ClearResultsViewers();
	FTimerHandle DevBlackHoleTimer;
	FTimerHandle DevWindTimer;
	FTimerHandle StartingTimer;
	FTimerHandle DevAutoLobbyTimer;

	// Matched rooms: everyone matched arrives -> the match starts by itself -> after the results each machine goes home.
	void UpdateMatchmadeRoom();
	FChaosImpactMatchRules MakeMatchmadeRules(int32 Humans) const;
	/** The results are over (or ran too long): every guest is sent home, then the host goes. */
	void CloseMatchmadeRoom();
	/** The host goes home once no guest is left (or straight away when Force). */
	void FinishMatchmadeIfEmpty(bool bForce);
	FTimerHandle MatchmadeTimer;
	FTimerHandle MatchmadeCloseTimer;
	bool bMatchmadeMatchBegun = false;
	bool bMatchmadeHostDone = false;

	// ランクマッチ (the duel): ROUND N -> FIGHT -> [time up, level: SUDDEN DEATH] -> round over -> ... -> results.
	void SetDuelStep(EChaosImpactDuelStep Step, float Seconds);
	void StartDuelRound(int32 Round);
	void BeginDuelFight();
	void HandleDuelTimeUp();
	void EvaluateDuelKnockouts();
	void EndDuelRound(int32 WinnerSide, EChaosImpactDuelEnd How);
	void AfterDuelRound();
	/** Both duellists back at their starts, healed and empty-handed, for a new round. */
	void PlaceDuelists();
	/** The ball slot is over: the pads put out the kinds it drew (as often as in a VS match), then ROUND 1. */
	void FinishDuelBallDraw();
	/** The pads put out normal balls and the duel's drawn kinds (only normal balls before the draw is shown). */
	void ApplyDuelBallChances(bool bWithPicks);
	/** A duellist left mid-match: the other one wins it. */
public:
	/** The duel is given to the other side (LeavingSide left, or surrendered). */
	void ForfeitDuel(int32 LeavingSide);
private:
	float GetDuelRoundSeconds() const;
	/** The pawn (alive or knocked out) of a duel side. */
	APawn* GetDuelPawn(int32 Side) const;
	FTimerHandle DuelTimer;
	bool bDuelKnockoutCheckPending = false;
	FChaosImpactMatchRules DevAutoLobbyRules;
	/** Machines (and spectators) the auto lobby waits for. */
	int32 DevAutoLobbyMachines = 2;

	// ---- トーナメント (server) ----
	void TickTournament();
	/** The next round (byes and walk-overs decided at once). */
	void StartTournamentRound();
	/** Starts what can be started now: every ready match (at once), or the next one (one at a time). */
	void ScheduleTournamentMatches();
	/** Decides a match without it being played (a bye, a walk-over, two CPUs) and moves the bracket on. */
	void DecideTournamentMatch(int32 MatchIndex, int32 WinnerSide, bool bForfeit, bool bBye);
	/** Calls a match's players: an arena for it, the call shown, the players brought in after CallSeconds. */
	void CallTournamentMatch(int32 MatchIndex);
	void BeginTournamentMatch(int32 MatchIndex);
	/** One arena's duel, step by step (as the room's own duel, by server time). */
	void UpdateTournamentArena(int32 Arena);
	void SetArenaStep(int32 Arena, EChaosImpactDuelStep Step, float Seconds);
	void StartArenaRound(int32 Arena, int32 Round);
	void PlaceArenaDuellists(int32 Arena);
	void EndArenaRound(int32 Arena, int32 WinnerSide, EChaosImpactDuelEnd How);
	void EvaluateArenaKnockouts(int32 Arena);
	void FinishTournamentMatch(int32 MatchIndex);
	/** The match's players go back to what they chose (practice, watching), its CPUs leave, the arena is cleared. */
	void ReleaseTournamentArena(int32 Arena);
	/** A free arena (one is made, beside the others, when none is). */
	int32 AcquireTournamentArena();
	void ClearArenaBalls(int32 Arena);
	void ApplyArenaBallChances(int32 Arena, bool bWithPicks);

	// ---- Sudden death (a duel's, or a tournament arena's; Arena INDEX_NONE: the duel) ----
	// The floor closes in as a circle (AChaosImpactSuddenDeathZone): outside it for a moment loses the round. Special
	// balls of three kinds picked at its start keep appearing on the floor still in play. At the bell, the one nearer
	// the middle wins (no draw).
	struct FSuddenDeath
	{
		TWeakObjectPtr<class AChaosImpactSuddenDeathZone> Zone;
		TArray<EChaosImpactBallType> Kinds;
		TArray<TWeakObjectPtr<class AChaosImpactBall>> Balls;
		FVector Center = FVector::ZeroVector;
		float HalfExtent = 1000.0f;
		double NextBallAt = 0.0;
		float OutSeconds[2] = {0.0f, 0.0f};
	};
	TMap<int32, FSuddenDeath> SuddenDeaths;
	FTimerHandle SuddenDeathTimer;
	void BeginSuddenDeath(int32 Arena, const FVector& Center, float HalfExtent);
	void EndSuddenDeath(int32 Arena);
	void UpdateSuddenDeaths();
	/** The side nearer the middle at the bell (INDEX_NONE: level to the centimetre, or nobody standing). */
	int32 JudgeSuddenDeathByMiddle(int32 Arena) const;
	APawn* GetSuddenDeathPawn(int32 Arena, int32 Side) const;
	static constexpr float SuddenDeathBallEvery = 0.9f;
	static constexpr int32 SuddenDeathMaxBalls = 8;
	APawn* GetArenaPawn(int32 Arena, int32 Side) const;
	/** An entrant's pawn for their match: a person's own character (made if they were watching), a new CPU. */
	APawn* BringEntrantToArena(int32 Entrant, int32 Arena, int32 Side);
	/** Where a member goes when not playing: their machine's practice area, or a spectator camera over an arena. */
	void PlaceTournamentMember(APlayerController* Member);
	void PlaceInPractice(APlayerController* Member);
	void PlaceAsTournamentSpectator(APlayerController* Member, int32 Arena);
	/** The practice area of a member's machine (made the first time). */
	FVector GetPracticeCenter(const AChaosImpactPlayerState* Member);
	/** A 合言葉 room's tournament shares one practice room among everyone; other tournaments give each machine its own. */
	bool IsPracticeShared() const;
	/** The arena a spectator should look at: the match they asked for if it is being played, else any being played. */
	int32 PickArenaToWatch(int32 WantedMatch) const;
	/** An entrant left the room (or lost the connection): their match being played and all still to come are lost. */
	void ForfeitTournamentEntrant(int32 Entrant);
	void ClearTournamentWorld();
	FTimerHandle TournamentTimer;
	FTimerHandle TournamentScheduleTimer;
	FRandomStream TournamentRandom;
	double TournamentRoundBreakUntil = 0.0;
	double TournamentAskUntil = 0.0;
	/** Per arena: its stage, its ball pads, its CPUs, a knock-out to judge. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AChaosImpactStageBase>> TournamentStages;
	TArray<TArray<TWeakObjectPtr<AChaosImpactBallSpawner>>> TournamentArenaSpawners;
	TArray<TArray<TWeakObjectPtr<AController>>> TournamentArenaCPUs;
	TArray<bool> TournamentKnockoutPending;
	/** Per arena: when its finished match's banner is over (0: not finished). */
	TArray<double> TournamentArenaReleaseAt;
	/** Two CPUs being decided in the background (simultaneous rounds): match -> when. */
	TMap<int32, double> TournamentCPUDecideAt;
	/** Each machine's practice area (by its token), and its ball pads. */
	TMap<FString, TWeakObjectPtr<AChaosImpactTrainingArena>> TournamentPractice;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> TournamentPracticeActors;
	FVector TournamentArenaOrigin = FVector::ZeroVector;
	/** A public tournament's places for people (it takes no more before it starts). */
	int32 PublicTourneyMaxPlayers = 8;
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
	/** ステージ3 (the sky temple), raised in a room's lobby level; a local match travels to its own level instead. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> TempleStageClass;
	/** ステージ4 (the neon cyber highway junction). */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> CyberStageClass;
	/** ステージ5 (the frozen yukimi sanctuary). */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> SnowStageClass;
	/** ステージ6 (the molten core foundry). */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> ForgeStageClass;
	/** ランクマッチ's arena (デュエルアリーナ), used by every duel whatever stage is chosen. */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Versus", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AChaosImpactStageBase> DuelStageClass;

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
