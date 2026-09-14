#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "ChaosImpactGameState.generated.h"

UENUM(BlueprintType)
enum class EChaosImpactOnlinePhase : uint8
{
	/** Members gather in the training arena while recruitment is open. */
	Lobby,
	/** Recruitment closed; short countdown before the match. */
	Countdown,
	Match,
	Results
};

/** Online-room member data: display name comes from APlayerState::GetPlayerName. */
UCLASS()
class AChaosImpactPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 for the host, then 1, 2, ... in the order members entered the room. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	int32 JoinOrder = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bRoomHost = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	int32 Knockouts = 0;
};

/** Replicated room and match state for the online multiplayer mode. */
UCLASS()
class AChaosImpactGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Members sorted host first, then by join order. */
	TArray<AChaosImpactPlayerState*> GetMembersInJoinOrder() const;
	float GetPhaseRemainingSeconds() const;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bOnlineRoom = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	EChaosImpactOnlinePhase Phase = EChaosImpactOnlinePhase::Lobby;

	/** Server world time at which the current countdown / match / results phase ends. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	double PhaseEndsAt = 0.0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	FString RoomPassword;

	/** The host ended recruitment; members stay in the lobby but nobody new can join. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Chaos Impact|Online")
	bool bRecruitmentClosed = false;

	static constexpr int32 MaxMembers = 8;
};
