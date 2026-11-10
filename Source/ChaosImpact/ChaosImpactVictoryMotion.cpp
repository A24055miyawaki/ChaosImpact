#include "ChaosImpactVictoryMotion.h"
#include "ChaosImpactCharacterRoster.h"

namespace
{
	/** How one character celebrates (the same values as SMASH//EDGE for the ten it shares; four of this game's own). */
	struct FVictoryStyle
	{
		float BobHz = 1.2f, BobCm = 2.0f, Squash = 0.02f, SwayDeg = 1.5f, SwayHz = 0.5f, Hover = 0.0f, Posture = 0.0f;
		/** Above zero, the motion snaps to this many updates a second (servos, creaking wood). */
		float ServoHz = 0.0f;
		float WinHop = 16.0f, WinHz = 2.0f, WinSpinHz = 0.0f, WinArms = 1.0f, WinPoint = 0.0f, WinTilt = 5.0f;
	};

	FVictoryStyle MakeStyle(const int32 Character)
	{
		FVictoryStyle S;
		switch (Character)
		{
		case 0: // Smiley: all rubber, everything bounces.
			S.BobHz = 2.3f; S.BobCm = 3.5f; S.Squash = 0.085f; S.SwayDeg = 3.0f; S.SwayHz = 1.15f;
			S.WinHop = 34.0f; S.WinHz = 2.6f; S.WinTilt = 11.0f; break;
		case 1: // Shimaenaga: built like a bodybuilder, flexing rather than hopping.
			S.BobHz = 0.8f; S.BobCm = 1.8f; S.Squash = 0.035f; S.SwayDeg = 1.2f; S.Posture = -3.0f;
			S.WinHop = 0.0f; S.WinHz = 1.6f; S.WinTilt = 7.0f; break;
		case 2: // Dodge fairy: drifting, turning slowly in the air.
			S.BobHz = 0.55f; S.BobCm = 7.0f; S.Squash = 0.012f; S.SwayDeg = 5.0f; S.SwayHz = 0.4f; S.Hover = 9.0f; S.Posture = -2.0f;
			S.WinHop = 22.0f; S.WinHz = 0.9f; S.WinSpinHz = 0.9f; S.WinTilt = 9.0f; break;
		case 3: // Woodwoman: a stiff creak from the hips.
			S.BobHz = 0.45f; S.BobCm = 0.6f; S.Squash = 0.003f; S.SwayDeg = 3.4f; S.SwayHz = 0.32f; S.ServoHz = 14.0f;
			S.WinHop = 0.0f; S.WinHz = 0.7f; S.WinTilt = 9.0f; break;
		case 4: // Ant kids: quick little skitters.
			S.BobHz = 6.0f; S.BobCm = 1.1f; S.Squash = 0.03f; S.SwayDeg = 1.8f; S.SwayHz = 4.2f; S.Posture = 5.0f;
			S.WinHop = 24.0f; S.WinHz = 5.5f; S.WinTilt = 6.0f; break;
		case 5: // Tuna: wriggles and turns as it swims in the air.
			S.BobHz = 1.1f; S.BobCm = 5.0f; S.Squash = 0.0f; S.SwayDeg = 7.0f; S.SwayHz = 2.1f; S.Hover = 4.0f;
			S.WinHop = 30.0f; S.WinHz = 1.5f; S.WinSpinHz = 0.5f; S.WinArms = 0.0f; S.WinTilt = 16.0f; break;
		case 6: // Hero: chest out, pointing to the sky.
			S.BobHz = 1.0f; S.BobCm = 1.2f; S.Squash = 0.01f; S.SwayDeg = 0.7f; S.Posture = -6.0f;
			S.WinHop = 0.0f; S.WinHz = 0.6f; S.WinArms = 0.0f; S.WinPoint = 1.0f; S.WinTilt = 1.5f; break;
		case 7: // Robo: servos, nothing smooth, a slow mechanical turn.
			S.BobHz = 1.2f; S.BobCm = 1.0f; S.Squash = 0.0f; S.SwayDeg = 2.5f; S.SwayHz = 0.35f; S.ServoHz = 9.0f;
			S.WinHop = 8.0f; S.WinHz = 2.0f; S.WinSpinHz = 0.25f; S.WinTilt = 0.0f; break;
		case 8: // Thinking face: hovers, tilts and turns as if still working it out.
			S.BobHz = 0.7f; S.BobCm = 5.0f; S.Squash = 0.025f; S.SwayDeg = 9.0f; S.SwayHz = 0.3f; S.Hover = 6.0f;
			S.WinHop = 20.0f; S.WinHz = 1.1f; S.WinSpinHz = 0.7f; S.WinArms = 0.0f; S.WinTilt = 14.0f; break;
		case 9: // Reverse Nicola: one wobble away from falling over.
			S.BobHz = 1.9f; S.BobCm = 1.6f; S.Squash = 0.012f; S.SwayDeg = 4.8f; S.SwayHz = 1.05f; S.Posture = 2.0f;
			S.WinHop = 28.0f; S.WinHz = 2.4f; S.WinArms = 0.0f; S.WinTilt = 13.0f; break;
		case ChaosImpactRoster::CapIndex: // Mug carp: hops like a fish out of water, turning a little.
			S.BobHz = 1.6f; S.BobCm = 2.5f; S.Squash = 0.06f; S.SwayDeg = 4.0f; S.SwayHz = 0.9f;
			S.WinHop = 18.0f; S.WinHz = 2.2f; S.WinSpinHz = 0.4f; S.WinArms = 0.0f; S.WinTilt = 8.0f; break;
		case ChaosImpactRoster::CellIndex: // Cell: the perfect form, chest out and pointing up.
			S.BobHz = 0.9f; S.BobCm = 1.0f; S.Squash = 0.008f; S.SwayDeg = 0.8f; S.Posture = -5.0f;
			S.WinHop = 0.0f; S.WinHz = 0.5f; S.WinArms = 0.0f; S.WinPoint = 1.0f; S.WinTilt = 2.0f; break;
		case ChaosImpactRoster::CatIndex: // The hollow-eyed saviours: float up and turn, a little uncanny.
			S.BobHz = 0.6f; S.BobCm = 4.0f; S.Squash = 0.02f; S.SwayDeg = 6.0f; S.SwayHz = 0.35f; S.Hover = 6.0f;
			S.WinHop = 12.0f; S.WinHz = 1.2f; S.WinSpinHz = 0.3f; S.WinTilt = 10.0f; break;
		case ChaosImpactRoster::BirdIndex: // Bird striker: round and springy, rocking side to side.
			S.BobHz = 1.8f; S.BobCm = 3.0f; S.Squash = 0.07f; S.SwayDeg = 5.0f; S.SwayHz = 0.9f;
			S.WinHop = 20.0f; S.WinHz = 1.8f; S.WinArms = 0.0f; S.WinTilt = 9.0f; break;
		default: break;
		}
		return S;
	}

	float Wave(const float Time, const float Hz, const float Phase = 0.0f) { return FMath::Sin((Time * Hz + Phase) * 2.0f * UE_PI); }
	float Bounce(const float Time, const float Hz) { return FMath::Abs(FMath::Sin(Time * Hz * UE_PI)); }
}

FChaosImpactVictoryPose ChaosImpactVictoryMotion::Evaluate(const int32 Character, float Time)
{
	// Everyone celebrates the Smiley way (the user's choice): all rubber, big bounces, squash and stretch, arms up. The
	// per-character styles above are kept for reference and for trying other directions.
	(void)Character;
	const FVictoryStyle S = MakeStyle(0);
	if (S.ServoHz > 0.0f)
	{
		Time = FMath::FloorToFloat(Time * S.ServoHz) / S.ServoHz;
	}
	// It comes in over the first half second rather than jumping into the pose.
	const float In = FMath::Clamp(Time / 0.5f, 0.0f, 1.0f);
	FChaosImpactVictoryPose P;
	P.Offset.Z = S.Hover + S.WinHop * Bounce(Time, S.WinHz) * In;
	P.Lean = S.Posture - 7.0f * In;
	P.Tilt = S.WinTilt * Wave(Time, S.WinHz * 0.5f) * In;
	P.Spin = S.WinSpinHz > 0.0f ? FMath::Fmod(Time * S.WinSpinHz * 360.0f, 360.0f) * In : 6.0f * Wave(Time, S.SwayHz, 0.2f);
	P.Stretch = 1.0f + S.Squash * Wave(Time, S.WinHz) * In;
	P.ArmsRaised = S.WinArms * (0.82f + 0.18f * Wave(Time, S.WinHz)) * In;
	P.Point = S.WinPoint * In;
	return P;
}

FTransform ChaosImpactVictoryMotion::BodyTransform(const FChaosImpactVictoryPose& P, const float BaseScale)
{
	const float Wide = 1.0f / FMath::Sqrt(FMath::Max(P.Stretch, 0.2f));
	// FRotator: Roll tips the model's +Y forward down, Pitch tips it sideways, Yaw spins it.
	return FTransform(FRotator(P.Tilt, P.Spin, P.Lean), P.Offset, FVector(Wide, Wide, P.Stretch) * BaseScale);
}
