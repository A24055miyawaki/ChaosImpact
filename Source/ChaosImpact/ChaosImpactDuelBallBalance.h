#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"

class UWorld;
class AActor;

/** Server-only lifetime shared by a throw, its trail, burst and flock. Never replicated or keyed by player. */
struct FChaosImpactDuelThrowDamage
{
	static TSharedPtr<FChaosImpactDuelThrowDamage> Create(const UWorld* World);
	float Consume(AActor* Victim, float Requested);
	bool HasHit(const AActor* Victim) const;
private:
	TMap<FObjectKey, float> DamageByVictim;
};

namespace ChaosImpactDuelBallBalance
{
	bool IsDuel(const UWorld* World);
	constexpr float IceFreezeSeconds = .4f;
	constexpr float IceSlowSeconds = .8f;
	constexpr float IceSlowMultiplier = .75f;
	constexpr float IceRepeatGraceSeconds = 1.2f;
	constexpr float BlackRadius = 500.f;
	constexpr float BlackSeconds = 2.5f;
	constexpr float BlackEdgePull = 180.f;
	constexpr float BlackCorePull = 620.f;
	constexpr float SmokeRadius = 260.f;
	constexpr float SimaeLifetimeSeconds = 4.5f;
	constexpr float SimaeAcquireReach = 1800.f;
	constexpr float SimaeMinSpeed = 720.f;
	constexpr float SimaeMaxSpeed = 920.f;
	constexpr float SimaeTurnDegrees = 200.f;
}
