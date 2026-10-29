#include "ChaosImpactDuelBallBalance.h"
#include "ChaosImpactGameState.h"
#include "Engine/World.h"

bool ChaosImpactDuelBallBalance::IsDuel(const UWorld* World)
{
	const auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	return Match && Match->IsDuel();
}

TSharedPtr<FChaosImpactDuelThrowDamage> FChaosImpactDuelThrowDamage::Create(const UWorld* World)
{
	if (ChaosImpactDuelBallBalance::IsDuel(World) && World->GetNetMode() != NM_Client)
	{
		return MakeShared<FChaosImpactDuelThrowDamage>();
	}
	return nullptr;
}

float FChaosImpactDuelThrowDamage::Consume(AActor* Victim, const float Requested)
{
	if (!IsValid(Victim) || Requested <= 0.f) { return 0.f; }
	float& Used = DamageByVictim.FindOrAdd(FObjectKey(Victim));
	const float Allowed = FMath::Min(Requested, FMath::Max(0.f, 1.f - Used));
	Used += Allowed;
	return Allowed;
}

bool FChaosImpactDuelThrowDamage::HasHit(const AActor* Victim) const
{
	const float* Used = Victim ? DamageByVictim.Find(FObjectKey(Victim)) : nullptr;
	return Used && *Used >= 1.f;
}
