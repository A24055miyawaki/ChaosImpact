#pragma once
#include "CoreMinimal.h"
class ACharacter;
namespace ChaosImpactClickNavigation
{
	/** Bounded ground search; uses level collision, so no editor NavMesh setup is required. */
	CHAOSIMPACT_API bool FindPath(const ACharacter* Character, const FVector& GroundGoal, TArray<FVector>& OutPath);
}
