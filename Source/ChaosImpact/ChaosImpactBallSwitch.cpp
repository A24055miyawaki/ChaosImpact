#include "ChaosImpactBallSwitch.h"

#include "ChaosImpactGameState.h"
#include "Engine/World.h"

bool ChaosImpactBallSwitch::IsOff(const UWorld* World, const EChaosImpactBallType Type)
{
	if (!World)
	{
		return false;
	}
	const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return false;
	}
	// Rules with nothing on at all (never left so on the switch screen) play with normal balls.
	if (Type == EChaosImpactBallType::Normal && IsNoneOn(Match->Rules))
	{
		return false;
	}
	return !IsOn(Match->Rules, Type);
}

TArray<uint8> ChaosImpactBallSwitch::DrawDuelBalls(const FChaosImpactMatchRules& Rules, const int32 Picks, TFunctionRef<int32(int32, int32)> Rand)
{
	TArray<uint8> Pool;
	for (const EChaosImpactBallType Kind : GetSwitchable())
	{
		if (Kind != EChaosImpactBallType::Normal && Kind != EChaosImpactBallType::Nova && Kind != EChaosImpactBallType::Prison && IsOn(Rules, Kind))
		{
			Pool.Add(static_cast<uint8>(Kind));
		}
	}
	TArray<uint8> Drawn;
	const uint8 Hook = static_cast<uint8>(EChaosImpactBallType::Hook);
	const uint8 Anchor = static_cast<uint8>(EChaosImpactBallType::Anchor);
	while (Drawn.Num() < Picks && !Pool.IsEmpty())
	{
		const uint8 Pick = Pool[Rand(0, Pool.Num() - 1)];
		Drawn.Add(Pick);
		Pool.Remove(Pick);
		// The hook and the anchor look alike in hand: never both in one match (a slot left over is filled with one drawn).
		const uint8 Twin = Pick == Hook ? Anchor : Pick == Anchor ? Hook : 0;
		if (Twin != 0)
		{
			Pool.Remove(Twin);
		}
	}
	// Fewer kinds on than slots: the ones drawn fill the rest.
	const int32 Kinds = Drawn.Num();
	while (Kinds > 0 && Drawn.Num() < Picks)
	{
		const uint8 Again = Drawn[Rand(0, Kinds - 1)];
		Drawn.Add(Again);
	}
	return Drawn;
}
