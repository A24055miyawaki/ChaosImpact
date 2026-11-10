#pragma once

#include "CoreMinimal.h"

/**
 * The winner's motion on the podium (brought over from SMASH//EDGE's victory poses, everyone in Smiley's style): a
 * whole-body layer over the idle clip (a hop, a lean back, a sway or spin, a squash and stretch) and the arms up or a
 * finger pointing. Characters with their own authored clips play their appeal on a loop instead of the body layer.
 */
struct FChaosImpactVictoryPose
{
	FVector Offset = FVector::ZeroVector;
	/** Degrees. Lean is forward, Tilt to its side, Spin about the vertical. */
	float Lean = 0.0f, Tilt = 0.0f, Spin = 0.0f;
	/** Above 1 stretches tall, below 1 squashes; the width makes up for it. */
	float Stretch = 1.0f;
	float ArmsRaised = 0.0f, Point = 0.0f;
};

namespace ChaosImpactVictoryMotion
{
	/** The pose Time seconds into the celebration. */
	FChaosImpactVictoryPose Evaluate(int32 Character, float Time);
	/** The pose as the model's relative transform (model space: +Y forward, +Z up), at the model's own scale. */
	FTransform BodyTransform(const FChaosImpactVictoryPose& Pose, float BaseScale);
	/** Seconds between a model's own appeal clips while it celebrates (authored models). */
	constexpr float AppealEverySeconds = 2.4f;
}
