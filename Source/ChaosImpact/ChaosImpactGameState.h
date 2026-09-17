#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactGameState.generated.h"

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

	/** VS match score: hits on opponents plus knockout bonuses. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Points = 0;

	/** Team battle team (0-3); INDEX_NONE in a free-for-all or outside a match. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 TeamIndex = INDEX_NONE;

	/** Plays on the host's machine (either local player there): hits on them are decided by the host at once. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bHostMachine = false;

	/** The second player on a machine; it shares the first player's connection. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bSecondOfMachine = false;

	/** Lobby: this member pressed 準備OK for the rules the host decided. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bReadyForMatch = false;

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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Human members sorted host first, then by join order. CPUs are not members. */
	TArray<AChaosImpactPlayerState*> GetMembersInJoinOrder() const;
	int32 CountHumanMembers() const;
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
	/** True when both actors are pawns of the same team in a team battle. */
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

	/** Server time Ready? began, once every machine finished its opening; 0 while still waiting. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Match")
	double ReadyStartedAt = 0.0;

	static constexpr int32 MaxMembers = 8;

private:
	/** This machine's own clock for the opening it is playing (see GetIntroElapsedSeconds). */
	mutable double IntroSeenForStart = -1.0;
	mutable double IntroSeenLocalTime = 0.0;
	mutable float IntroSeenOffset = 0.0f;
};
