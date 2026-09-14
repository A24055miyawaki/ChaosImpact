#include "ChaosImpactGameState.h"

#include "Net/UnrealNetwork.h"

void AChaosImpactPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactPlayerState, JoinOrder);
	DOREPLIFETIME(AChaosImpactPlayerState, bRoomHost);
	DOREPLIFETIME(AChaosImpactPlayerState, Knockouts);
}

void AChaosImpactGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactGameState, bOnlineRoom);
	DOREPLIFETIME(AChaosImpactGameState, Phase);
	DOREPLIFETIME(AChaosImpactGameState, PhaseEndsAt);
	DOREPLIFETIME(AChaosImpactGameState, RoomPassword);
	DOREPLIFETIME(AChaosImpactGameState, bRecruitmentClosed);
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetMembersInJoinOrder() const
{
	TArray<AChaosImpactPlayerState*> Members;
	for (APlayerState* State : PlayerArray)
	{
		if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
			Member && !Member->IsInactive())
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

float AChaosImpactGameState::GetPhaseRemainingSeconds() const
{
	return FMath::Max(0.0f, static_cast<float>(PhaseEndsAt - GetServerWorldTimeSeconds()));
}
