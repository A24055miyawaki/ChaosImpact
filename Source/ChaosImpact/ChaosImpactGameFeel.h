#pragma once

#include "CoreMinimal.h"

/**
 * The punch of a hit: the whole game holds still for a few hundredths of a second (hit stop), as fighting games
 * do, so a strong hit lands with weight and the motion after it reads faster.
 */
namespace ChaosImpactFeel
{
	/**
	 * Slows the world to a near stop for Seconds of real time. Only in a game on this machine alone (one screen or
	 * split screen): online the server's clock is everyone's, so there the characters' own freeze is used instead.
	 * Does nothing while something else has the world's speed changed (a demo, a sped-up test, time stopped), or
	 * within 0.4 s of the last hold, or with -CINoHitStop. Returns true when the world was held.
	 */
	CHAOSIMPACT_API bool HitStop(const UObject* WorldContext, float Seconds);
}
