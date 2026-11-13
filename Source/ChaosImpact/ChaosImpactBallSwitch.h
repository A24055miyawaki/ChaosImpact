#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactMatchTypes.h"

class UWorld;

/**
 * ボールスイッチ: the balls a match's host has switched off (FChaosImpactMatchRules::OffBalls, bit = the kind; the normal
 * ball too). An off kind never comes out of a pad, is never drawn for a duel's slot, and (the nova) puts out no parts.
 * The switch screen cannot be left with every kind off; rules that still have none on play with normal balls (IsOff).
 */
namespace ChaosImpactBallSwitch
{
	/** Every kind, the normal ball first, in the order the switch lists them. */
	inline TArray<EChaosImpactBallType> GetSwitchable()
	{
		TArray<EChaosImpactBallType> Out;
		for (int32 Type = 0; Type < ChaosImpactBallTypes::Count; ++Type)
		{
			Out.Add(static_cast<EChaosImpactBallType>(Type));
		}
		return Out;
	}

	/** Every kind's bit. */
	inline int32 AllBits() { return (1 << ChaosImpactBallTypes::Count) - 1; }

	inline bool IsOn(const FChaosImpactMatchRules& Rules, const EChaosImpactBallType Type)
	{
		const int32 Index = static_cast<int32>(Type);
		if (Index < 0 || Index >= ChaosImpactBallTypes::Count)
		{
			return true;
		}
		return (Rules.OffBalls & (1 << Index)) == 0;
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

	inline void SetOn(FChaosImpactMatchRules& Rules, const EChaosImpactBallType Type, const bool bOn)
	{
		const int32 Index = static_cast<int32>(Type);
		if (Index >= 0 && Index < ChaosImpactBallTypes::Count)
		{
			Rules.OffBalls = bOn ? (Rules.OffBalls & ~(1 << Index)) : (Rules.OffBalls | (1 << Index));
		}
	}

	/** Every kind off (the switch screen will not close like this). */
	inline bool IsNoneOn(const FChaosImpactMatchRules& Rules) { return CountOn(Rules) == 0; }

	/** Switched off in this world's match (its rules on the game state); never in a world without match rules. */
	CHAOSIMPACT_API bool IsOff(const UWorld* World, EChaosImpactBallType Type);

	/**
	 * A duel's (or a tournament match's) three slot balls: every special kind on but the nova and the prison, three
	 * different; never the hook and the anchor in one match; with fewer kinds to draw than slots, the ones drawn again to
	 * fill the slots; none at all with no special kind to draw (normal balls only). Rand(Min, Max) inclusive.
	 */
	CHAOSIMPACT_API TArray<uint8> DrawDuelBalls(const FChaosImpactMatchRules& Rules, int32 Picks, TFunctionRef<int32(int32, int32)> Rand);
}
