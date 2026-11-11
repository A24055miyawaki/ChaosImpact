#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ChaosImpactEditorTools.generated.h"

class USkeletalMesh;

/** Small helpers for the editor's Python scripts (Scripts/...): what Python cannot reach on its own. Editor only. */
UCLASS()
class UChaosImpactEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Every LOD of the mesh keeps its vertices on the CPU too (ペイント reads them in a packaged game). True if changed. */
	UFUNCTION(BlueprintCallable, Category = "Chaos Impact|Editor")
	static bool AllowSkeletalCpuAccess(USkeletalMesh* Mesh, bool bOnlyCheck);
};
