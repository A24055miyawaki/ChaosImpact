#pragma once
#include "CoreMinimal.h"
class AActor;
class USceneComponent;
class UProceduralMeshComponent;
namespace ChaosImpactPrison
{
	UProceduralMeshComponent* CreateCage(AActor* Owner, USceneComponent* Parent);
	void UpdateCage(UProceduralMeshComponent* Mesh, float Radius, float Remaining, float Time, bool bCaptured);
}
