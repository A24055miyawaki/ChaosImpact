#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ChaosImpactAssetTools.generated.h"

class USkeletalMesh;

/** Small editor helpers for the import scripts (Scripts/): what Python cannot reach on an asset by itself. */
UCLASS()
class CHAOSIMPACT_API UChaosImpactAssetTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every LOD of the mesh kept readable on the CPU (the final boss works out its palms from its own vertices). */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Editor")
	static bool AllowSkeletalMeshCPUAccess(USkeletalMesh* Mesh);
};
