#include "ChaosImpactGameState.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

void AChaosImpactPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactPlayerState, JoinOrder);
	DOREPLIFETIME(AChaosImpactPlayerState, CharacterIndex);
	DOREPLIFETIME(AChaosImpactPlayerState, ColourChoice);
	DOREPLIFETIME(AChaosImpactPlayerState, bRoomHost);
	DOREPLIFETIME(AChaosImpactPlayerState, bSpectating);
	DOREPLIFETIME(AChaosImpactPlayerState, Knockouts);
	DOREPLIFETIME(AChaosImpactPlayerState, Points);
	DOREPLIFETIME(AChaosImpactPlayerState, TeamIndex);
	DOREPLIFETIME(AChaosImpactPlayerState, bHostMachine);
	DOREPLIFETIME(AChaosImpactPlayerState, bSecondOfMachine);
	DOREPLIFETIME(AChaosImpactPlayerState, bReadyForMatch);
}

void AChaosImpactGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactGameState, bOnlineRoom);
	DOREPLIFETIME(AChaosImpactGameState, Phase);
	DOREPLIFETIME(AChaosImpactGameState, PhaseEndsAt);
	DOREPLIFETIME(AChaosImpactGameState, PhaseStartedAt);
	DOREPLIFETIME(AChaosImpactGameState, RoomPassword);
	DOREPLIFETIME(AChaosImpactGameState, bRecruitmentClosed);
	DOREPLIFETIME(AChaosImpactGameState, bVersusMatch);
	DOREPLIFETIME(AChaosImpactGameState, Rules);
	DOREPLIFETIME(AChaosImpactGameState, StageCenter);
	DOREPLIFETIME(AChaosImpactGameState, ReadyStartedAt);
	DOREPLIFETIME(AChaosImpactGameState, RoomName);
	DOREPLIFETIME(AChaosImpactGameState, bRulesDecided);
	DOREPLIFETIME(AChaosImpactGameState, ReadyDeadline);
}

float AChaosImpactGameState::GetIntroElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	if (Phase != EChaosImpactOnlinePhase::Intro || !World)
	{
		return GetPhaseElapsedSeconds();
	}
	const double LocalNow = World->GetTimeSeconds();
	if (IntroSeenForStart != PhaseStartedAt)
	{
		IntroSeenForStart = PhaseStartedAt;
		IntroSeenLocalTime = LocalNow;
		// Arriving a moment late stays in step with the host; arriving much later starts from the top.
		IntroSeenOffset = FMath::Min(GetPhaseElapsedSeconds(), 0.3f);
	}
	return static_cast<float>(LocalNow - IntroSeenLocalTime) + IntroSeenOffset;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetMembersInJoinOrder() const
{
	TArray<AChaosImpactPlayerState*> Members;
	for (APlayerState* State : PlayerArray)
	{
		if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
			Member && !Member->IsInactive() && !Member->IsABot() && !Member->bSpectating)
		{
			Members.Add(Member);
		}
	}
	Members.Sort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
	{
		if (A.bRoomHost != B.bRoomHost)
		{
			return A.bRoomHost;
		}
		return A.JoinOrder < B.JoinOrder;
	});
	return Members;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetSpectators() const
{
	TArray<AChaosImpactPlayerState*> Spectators;
	for (APlayerState* State : PlayerArray)
	{
		if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
			Member && !Member->IsInactive() && !Member->IsABot() && Member->bSpectating)
		{
			Spectators.Add(Member);
		}
	}
	return Spectators;
}

int32 AChaosImpactGameState::CountHumanMembers() const
{
	return GetMembersInJoinOrder().Num();
}

int32 AChaosImpactGameState::CountMachines() const
{
	int32 Machines = 0;
	for (const AChaosImpactPlayerState* Member : GetMembersInJoinOrder())
	{
		Machines += Member->bSecondOfMachine ? 0 : 1;
	}
	return Machines;
}

int32 AChaosImpactGameState::CountReadyMembers() const
{
	int32 Ready = 0;
	for (const AChaosImpactPlayerState* Member : GetMembersInJoinOrder())
	{
		Ready += Member->bReadyForMatch ? 1 : 0;
	}
	return Ready;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetCompetitors(const bool bIncludeCPUs) const
{
	TArray<AChaosImpactPlayerState*> Competitors = GetMembersInJoinOrder();
	if (bIncludeCPUs)
	{
		TArray<AChaosImpactPlayerState*> CPUs;
		for (APlayerState* State : PlayerArray)
		{
			if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
				Member && Member->IsABot() && !Member->IsInactive())
			{
				CPUs.Add(Member);
			}
		}
		CPUs.Sort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
		{
			return A.GetPlayerName() < B.GetPlayerName();
		});
		Competitors.Append(CPUs);
	}
	return Competitors;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetRanking() const
{
	TArray<AChaosImpactPlayerState*> Ranking = GetCompetitors(true);
	Ranking.StableSort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
	{
		if (A.Points != B.Points)
		{
			return A.Points > B.Points;
		}
		return A.Knockouts > B.Knockouts;
	});
	return Ranking;
}

int32 AChaosImpactGameState::GetTeamPoints(const int32 Team) const
{
	int32 Total = 0;
	for (const AChaosImpactPlayerState* Member : GetCompetitors(true))
	{
		Total += Member->TeamIndex == Team ? Member->Points : 0;
	}
	return Total;
}

float AChaosImpactGameState::GetPhaseRemainingSeconds() const
{
	return FMath::Max(0.0f, static_cast<float>(PhaseEndsAt - GetServerWorldTimeSeconds()));
}

float AChaosImpactGameState::GetPhaseElapsedSeconds() const
{
	return FMath::Max(0.0f, static_cast<float>(GetServerWorldTimeSeconds() - PhaseStartedAt));
}

bool AChaosImpactGameState::IsMatchInputLocked() const
{
	if (!bVersusMatch)
	{
		return false;
	}
	switch (Phase)
	{
	case EChaosImpactOnlinePhase::TeamSelect:
	case EChaosImpactOnlinePhase::Results:
		return true;
	case EChaosImpactOnlinePhase::Intro:
		// Held until Ready? has run; judged by server time, so GO frees everyone at the same moment
		// even before the phase change itself arrives.
		return ReadyStartedAt <= 0.0 || GetServerWorldTimeSeconds() < PhaseEndsAt;
	default:
		return false;
	}
}

bool AChaosImpactGameState::AreTeammates(const UWorld* World, const AActor* A, const AActor* B)
{
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->IsTeamBattle() || !A || !B || A == B)
	{
		return false;
	}
	const APawn* PawnA = Cast<APawn>(A);
	const APawn* PawnB = Cast<APawn>(B);
	const AChaosImpactPlayerState* StateA = PawnA ? PawnA->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const AChaosImpactPlayerState* StateB = PawnB ? PawnB->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	return StateA && StateB && StateA->TeamIndex >= 0 && StateA->TeamIndex == StateB->TeamIndex;
}
