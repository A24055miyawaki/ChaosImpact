#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactGameState.generated.h"

class AChaosImpactCrashBall;

/** One replicated snapshot keeps the objective, score and result consistent on clients. */
USTRUCT()
struct FChaosImpactCrashMatchState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AChaosImpactCrashBall> Ball;
	UPROPERTY() int32 RedCount = 100;
	UPROPERTY() int32 BlueCount = 100;
	UPROPERTY() float Progress = 0.f;
	UPROPERTY() bool bOvertime = false;
	UPROPERTY() bool bKnockout = false;
	UPROPERTY() int32 Winner = INDEX_NONE;
	UPROPERTY() int32 OvertimeTeam = INDEX_NONE;
	UPROPERTY() double LastLeadChangedAt = -100.;
	UPROPERTY() double LastRecoveryAt = -100.;
	/** Overtime: when it began and the overtime team's last direct hit (the HUD drains its grace from there). */
	UPROPERTY() double OvertimeStartedAt = -100.;
	UPROPERTY() double OvertimeHitAt = -100.;
	/** When each team's count last went down (the scoreboard punches the number). */
	UPROPERTY() double CountDroppedAt[2] = {-100., -100.};
	/** Which team pushed the ball last (INDEX_NONE before the first push). */
	UPROPERTY() int32 LastPushTeam = INDEX_NONE;
};

UENUM(BlueprintType)
enum class EChaosImpactOnlinePhase : uint8
{
	/** Members gather in the training arena while recruitment is open. */
	Lobby,
	/** Unused since VS matches have their own opening. */
	Countdown,
	Match,
	Results,
	/** VS team battle: everyone picks a team before the match. */
	TeamSelect,
	/** VS opening: flyover, dive to each player's own view, Ready?. Nobody can act until it ends. */
	Intro,
	/** Online lobby: everyone is ready (or the wait ran out); a short "match starting" countdown before the match. */
	Starting
};

/** A room the relay made for a ランダムマッチ or ランクマッチ (not a 合言葉 room): it starts by itself and ends with the match. */
UENUM(BlueprintType)
enum class EChaosImpactMatchmade : uint8
{
	None,
	Random,
	Rank
};

/** Where a ランクマッチ round stands (only while the match phase runs). */
UENUM(BlueprintType)
enum class EChaosImpactDuelStep : uint8
{
	None,
	/** The ball slot, before the first round: which special balls this match is played with. */
	BallDraw,
	/** ROUND N, then FIGHT!: nobody can act. */
	RoundIntro,
	Fight,
	/** The time ran out with the health level: the next hit wins the round. */
	SuddenDeath,
	/** The round is decided; shown for a moment before the next. */
	RoundOver,
	/** The match is decided (the results follow). */
	Finished
};

/** How a ランクマッチ round was decided. */
UENUM(BlueprintType)
enum class EChaosImpactDuelEnd : uint8
{
	None,
	Knockout,
	/** The time ran out: more health left wins. */
	Time,
	/** Sudden death: the first hit. */
	SuddenDeath,
	/** Sudden death ran out with no hit. */
	Draw,
	DoubleKnockout,
	/** The other player left the match. */
	Forfeit
};

/** One ランクマッチ, replicated: the two sides, the rounds and their results. */
USTRUCT()
struct FChaosImpactDuelState
{
	GENERATED_BODY()
	/** Side 0 and side 1 (the host machine's player is side 0). */
	UPROPERTY() TObjectPtr<APlayerState> Sides[2];
	/** 1-based; 0 before the first round. */
	UPROPERTY() int32 Round = 0;
	UPROPERTY() int32 Wins[2] = {0, 0};
	UPROPERTY() EChaosImpactDuelStep Step = EChaosImpactDuelStep::None;
	/** Server time the step began and ends (0: no end). */
	UPROPERTY() double StepStartedAt = 0.0;
	UPROPERTY() double StepEndsAt = 0.0;
	/** The round just decided: its winner's side (INDEX_NONE for a drawn round) and how. */
	UPROPERTY() int32 RoundWinner = INDEX_NONE;
	UPROPERTY() EChaosImpactDuelEnd RoundEnd = EChaosImpactDuelEnd::None;
	/** Every decided round's winner in order (-1: drawn), for the pips. */
	UPROPERTY() TArray<int8> RoundResults;
	/** Once Finished: the winner's side, or INDEX_NONE for a drawn match. */
	UPROPERTY() int32 MatchWinner = INDEX_NONE;
	/** The special balls drawn for this match (EChaosImpactBallType values; the pads put them out besides normal balls). */
	UPROPERTY() TArray<uint8> BallPicks;

	int32 SideOf(const APlayerState* Member) const { return Member && Sides[0] == Member ? 0 : Member && Sides[1] == Member ? 1 : INDEX_NONE; }
};

/** Room member or VS competitor. Display name comes from APlayerState::GetPlayerName; CPUs are bots. */
UCLASS()
class AChaosImpactPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Character select: roster index and colour (0-3; INDEX_NONE until this player's machine has sent it). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Character")
	int32 CharacterIndex = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Character")
	int32 ColourChoice = INDEX_NONE;

	/** 0 for the host, then 1, 2, ... in the order members entered the room. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	int32 JoinOrder = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bRoomHost = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	int32 Knockouts = 0;

	/** VS match score: hits on opponents plus knockout bonuses (クラッシュボール: pushes on the giant ball too). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Points = 0;

	/** クラッシュボール: throws that pushed the giant ball toward the opponents' goal. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 CrashPushes = 0;

	/** Team battle team (0-3); INDEX_NONE in a free-for-all or outside a match. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 TeamIndex = INDEX_NONE;

	/** Plays on the host's machine (either local player there): hits on them are decided by the host at once. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bHostMachine = false;

	/** The second player on a machine; it shares the first player's connection. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bSecondOfMachine = false;

	/** Watching through a spectator camera: not a competitor or a room member for the match. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bSpectating = false;

	/** Lobby: this member pressed 準備OK for the rules the host decided. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bReadyForMatch = false;

	// ---- The VS match's numbers, counted by the server for the results ----
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Throws = 0;
	/** Hits on opponents (each burn of a fire zone too). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Hits = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 TimesHit = 0;
	/** Opponents' balls passed through by dashing. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Dodges = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 SpecialThrows = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 DriveHits = 0;
	/** The furthest an opponent was from this player when hit by them, in centimetres. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 LongestHit = 0;
	/** Points every ChaosImpactMatch::HistoryStepSeconds from GO, and at the whistle (the results' graph). */
	UPROPERTY(Replicated)
	TArray<int16> PointsHistory;
	/** Online: still on the results (the lobby waits for everyone to come back before the next match). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bViewingResults = false;

	/** Points, knockouts and every number above back to nothing (a new match). */
	void ResetMatchStats();

	/** Server only: identifies the member's machine across reconnects (the CIMachine login option). */
	FString MachineToken;

	/** Server only: how many players this member's machine brings (1 or 2), and how many have joined. */
	int32 ExpectedMachinePlayers = 1;
	int32 MachinePlayersJoined = 1;
	double MachineLoginAt = 0.0;

	/** A second player on the same machine is shown as "<first player's name>(2)". */
	static FString MakeSecondPlayerName(const FString& FirstPlayerName) { return FirstPlayerName + TEXT("(2)"); }
};

/** Replicated room and VS match state, used by online rooms and local matches alike. */
UCLASS()
class AChaosImpactGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated)
	FChaosImpactCrashMatchState Crash;
	AChaosImpactCrashBall* GetCrashBall() const { return Crash.Ball; }
	int32 GetCrashCount(int32 Team) const { return Team == 0 ? Crash.RedCount : Crash.BlueCount; }
	int32 GetCrashLeader() const { return Crash.RedCount == Crash.BlueCount ? INDEX_NONE : (Crash.RedCount < Crash.BlueCount ? 0 : 1); }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Human members sorted host first, then by join order. CPUs are not members. */
	TArray<AChaosImpactPlayerState*> GetMembersInJoinOrder() const;
	int32 CountHumanMembers() const;
	/** Room members watching through a spectator camera instead of playing. */
	TArray<AChaosImpactPlayerState*> GetSpectators() const;
	int32 CountSpectators() const { return GetSpectators().Num(); }
	/** Machines in the room: a split-screen pair counts once. */
	int32 CountMachines() const;
	int32 CountReadyMembers() const;
	/** Humans in join order, then CPUs by name. */
	TArray<AChaosImpactPlayerState*> GetCompetitors(bool bIncludeCPUs = true) const;
	/** Competitors by points (then knockouts), highest first. */
	TArray<AChaosImpactPlayerState*> GetRanking() const;
	int32 GetTeamPoints(int32 Team) const;
	float GetPhaseRemainingSeconds() const;
	float GetPhaseElapsedSeconds() const;
	/**
	 * How far this machine is into the opening. Counted from when this machine first saw it, so a machine the
	 * news reached late (loading, lag) still plays the whole opening; the match waits for it before Ready?.
	 * Other phases return GetPhaseElapsedSeconds.
	 */
	float GetIntroElapsedSeconds() const;
	bool IsTeamBattle() const { return bVersusMatch && Rules.IsTeamBattle(); }
	/** Team select, the opening until GO, and the results: characters must not act. */
	bool IsMatchInputLocked() const;
	/**
	 * True when both actors are pawns of the same team in a team battle, or, outside VS matches (solo mode), characters
	 * with the same SoloTeam.
	 */
	static bool AreTeammates(const UWorld* World, const AActor* A, const AActor* B);

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bOnlineRoom = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	EChaosImpactOnlinePhase Phase = EChaosImpactOnlinePhase::Lobby;

	/** Server world time at which the current phase ends (0 when it has no end). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	double PhaseEndsAt = 0.0;

	/** Server world time at which the current phase began; drives the opening camera and banners. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	double PhaseStartedAt = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	FString RoomPassword;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	FString RoomName;

	/** The host ended recruitment; members stay in the lobby but nobody new can join. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bRecruitmentClosed = false;

	/** Lobby: the host decided the next match's rules (in Rules); members now press 準備OK. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bRulesDecided = false;

	/** Server time the decided match starts even if not everyone is ready (0 while not waiting). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	double ReadyDeadline = 0.0;

	static constexpr float ReadyWaitSeconds = 60.0f;
	static constexpr float StartingSeconds = 3.0f;

	/** A VS match is set up: from team select or the opening until the results are over. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bVersusMatch = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	FChaosImpactMatchRules Rules;

	UPROPERTY(Replicated)
	FVector_NetQuantize StageCenter = FVector::ZeroVector;

	/** Half the floor width of the stage in use (stages differ in size). */
	UPROPERTY(Replicated)
	float StageHalfExtent = 3000.0f;

	/** Server time Ready? began, once every machine finished its opening; 0 while still waiting. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	double ReadyStartedAt = 0.0;

	static constexpr int32 MaxMembers = 8;
	/** Spectators a room takes on top of its players. */
	static constexpr int32 MaxSpectators = 4;

	/** A ランダムマッチ / ランクマッチ room: it starts once everyone matched has arrived, and closes after the results. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	EChaosImpactMatchmade Matchmade = EChaosImpactMatchmade::None;
	/** Players the relay matched into this room (a split-screen pair counts two). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	int32 MatchmadeExpected = 0;
	/** Server time the match starts with whoever has arrived (0 once it has started). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	double MatchmadeDeadline = 0.0;
	bool IsMatchmade() const { return Matchmade != EChaosImpactMatchmade::None; }

	UPROPERTY(Replicated)
	FChaosImpactDuelState Duel;
	bool IsDuel() const { return bVersusMatch && Rules.bDuel; }
	/** A ランクマッチ round is being fought (hits count). */
	bool IsDuelLive() const;
	/** Seconds left of the duel's current step (0 when it has no end). */
	float GetDuelStepRemaining() const;
	float GetDuelStepElapsed() const;
	/** The player's side's opponent in the duel (null outside one). */
	APlayerState* GetDuelOpponent(const APlayerState* Member) const;

private:
	/** This machine's own clock for the opening it is playing (see GetIntroElapsedSeconds). */
	mutable double IntroSeenForStart = -1.0;
	mutable double IntroSeenLocalTime = 0.0;
	mutable float IntroSeenOffset = 0.0f;
};
