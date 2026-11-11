#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactMatchTypes.h"

class UWorld;

/**
 * ボールスイッチ: the special balls a match's host has switched off (FChaosImpactMatchRules::OffBalls). An off kind never
 * comes out of a pad, is never drawn for a duel's slot, and (the nova) puts out no parts.
 */
namespace ChaosImpactBallSwitch
{
	/** Every special kind (not the normal ball) in the order the switch lists them. */
	inline TArray<EChaosImpactBallType> GetSwitchable()
	{
		TArray<EChaosImpactBallType> Out;
		for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
		{
			Out.Add(static_cast<EChaosImpactBallType>(Type));
		}
		return Out;
	}

	inline bool IsOn(const FChaosImpactMatchRules& Rules, const EChaosImpactBallType Type)
	{
		const int32 Index = static_cast<int32>(Type);
		return Index <= 0 || (Rules.OffBalls & (1 << Index)) == 0;
	}

	inline void SetOn(FChaosImpactMatchRules& Rules, const EChaosImpactBallType Type, const bool bOn)
	{
		const int32 Index = static_cast<int32>(Type);
		if (Index > 0)
		{
			Rules.OffBalls = bOn ? (Rules.OffBalls & ~(1 << Index)) : (Rules.OffBalls | (1 << Index));
		}
	}

	inline int32 CountOn(const FChaosImpactMatchRules& Rules)
	{
		int32 On = 0;
		for (const EChaosImpactBallType Type : GetSwitchable())
		{
			On += IsOn(Rules, Type) ? 1 : 0;
		}
		return On;
	}

	/** Switched off in this world's match (its rules on the game state); never in a world without match rules. */
	CHAOSIMPACT_API bool IsOff(const UWorld* World, EChaosImpactBallType Type);

	/**
	 * A duel's (or a tournament match's) three slot balls: every kind on but the nova and the prison, three different;
	 * never the hook and the anchor in one match; with fewer kinds to draw than slots, the ones
	 * drawn again to fill the slots; none at all with every kind off (normal balls only). Rand(Min, Max) inclusive.
	 */
	CHAOSIMPACT_API TArray<uint8> DrawDuelBalls(const FChaosImpactMatchRules& Rules, int32 Picks, TFunctionRef<int32(int32, int32)> Rand);
}
