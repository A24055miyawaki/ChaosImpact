#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactTackleVFX.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/** Short-lived, world-space cosmetics shared by every split-screen view. No gameplay collision. */
UCLASS(NotBlueprintable, Transient)
class AChaosImpactTackleVFX : public AActor
{
    GENERATED_BODY()
public:
    AChaosImpactTackleVFX();
    static AChaosImpactTackleVFX* Play(UWorld* World, const FVector& At, const FVector& Direction,
        bool bImpact, AActor* Follow = nullptr);
    virtual void Tick(float DeltaSeconds) override;
    int32 GetVertexCount() const;
    bool IsImpact() const { return bHit; }
private:
    void Rebuild();
    float Age = 0;
    bool bHit = false;
    TWeakObjectPtr<AActor> Following;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Look;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowLooks;
};
