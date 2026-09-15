#include "ChaosImpactFxPreloadSubsystem.h"

#include "ChaosImpactBallTypes.h"

void UChaosImpactFxPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ChaosImpactBallTypes::PreloadAssets(PreloadedAssets);
}
