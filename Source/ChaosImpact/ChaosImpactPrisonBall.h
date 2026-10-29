#pragma once
#include "CoreMinimal.h"
class AActor;
class USceneComponent;
class UObject;
class UProceduralMeshComponent;
namespace ChaosImpactPrison
{
	UProceduralMeshComponent* CreateCage(AActor* Owner, USceneComponent* Parent);
	/**
	 * The cage round a prison ball or its captive: lit iron bars, a heavy lock band whose ticks of light go out as the
	 * capture runs out (red and rattling in its last second). Captured, it slams shut over its first moments;
	 * resting on the floor (FloorBelow: how far under its middle, negative when not) the same clock is drawn as a ring of light on the floor round it.
	 */
	void UpdateCage(UProceduralMeshComponent* Mesh, float Radius, float Remaining, float Time, bool bCaptured, float FloorBelow = -1.0f);
	/** The cage slamming shut round a captive (cosmetic, this machine). */
	void PlayLock(UObject* WorldContext, const FVector& At, float Radius);
	/** The cage breaking open as its captive goes free (cosmetic, this machine). */
	void PlayBreak(UObject* WorldContext, const FVector& At, float Radius);
}
