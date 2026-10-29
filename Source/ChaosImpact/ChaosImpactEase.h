#pragma once

#include "CoreMinimal.h"

/**
 * How things move from one place to another across the game: never at a constant speed. Everything that goes from
 * A to B speeds up gently, is quickest in the middle and slows into place (After Effects' イージーイーズ), and
 * anything that chases a moving target (a following camera, a gauge) does so on a critically damped spring, which
 * starts as smoothly as it settles.
 */
namespace ChaosImpactEase
{
	/** イージーイーズ: After Effects' default ease (33% influence both ends) is exactly this curve. */
	inline float InOut(const float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	/** A stronger イージーイーズ (more influence): a gentler start and landing, a quicker middle. */
	inline float InOutStrong(const float T)
	{
		const float X = FMath::Clamp(T, 0.0f, 1.0f);
		return X * X * X * (X * (X * 6.0f - 15.0f) + 10.0f);
	}

	/** イージーイーズイン (arrives slowly: quick at first, settling into place). */
	inline float Out(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/** イージーイーズアウト (leaves slowly: gathering speed, for things going away). */
	inline float In(const float T)
	{
		return FMath::Pow(FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/** Eased progress through [From, From + Seconds] of Time. */
	inline float InOutBetween(const float Time, const float From, const float Seconds)
	{
		return InOut((Time - From) / FMath::Max(Seconds, UE_SMALL_NUMBER));
	}

	/**
	 * A critically damped spring towards Target: no overshoot, and unlike an exponential chase it does not leap off
	 * at full speed when the target moves. SmoothTime is roughly how long it takes to get there. Velocity is the
	 * spring's own state, kept by the caller between frames.
	 */
	template <typename T>
	T SpringTo(const T& Current, const T& Target, T& Velocity, const float DeltaSeconds, const float SmoothTime)
	{
		const float Omega = 2.0f / FMath::Max(SmoothTime, 0.0001f);
		const float X = Omega * DeltaSeconds;
		const float Decay = 1.0f / (1.0f + X + 0.48f * X * X + 0.235f * X * X * X);
		const T Change = Current - Target;
		const T Temp = (Velocity + Change * Omega) * DeltaSeconds;
		Velocity = (Velocity - Temp * Omega) * Decay;
		return Target + (Change + Temp) * Decay;
	}

	/** SpringTo for a rotation, along the shortest way round. */
	inline FRotator SpringToRotator(const FRotator& Current, const FRotator& Target, FVector& Velocity, const float DeltaSeconds,
		const float SmoothTime)
	{
		const FRotator Delta = (Target - Current).GetNormalized();
		const FVector Now(0.0f, 0.0f, 0.0f);
		const FVector Goal(Delta.Pitch, Delta.Yaw, Delta.Roll);
		const FVector Step = SpringTo(Now, Goal, Velocity, DeltaSeconds, SmoothTime);
		return (Current + FRotator(Step.X, Step.Y, Step.Z)).GetNormalized();
	}

	/**
	 * A 0..1 switch that takes Seconds to go across at an even pace (Linear, kept by the caller), shown through
	 * イージーイーズ: what toggles in and out (an overview, a panel) uses the returned value.
	 */
	inline float Toggle(float& Linear, const bool bOn, const float DeltaSeconds, const float Seconds)
	{
		Linear = FMath::FInterpConstantTo(Linear, bOn ? 1.0f : 0.0f, DeltaSeconds, 1.0f / FMath::Max(Seconds, 0.01f));
		return InOut(Linear);
	}
}
