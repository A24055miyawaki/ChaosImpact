#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGColony.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

/**
 * What the colony shows of its growth (RPGColony in RPGTutorial.h): flower beds and banners, then a market, a new
 * house and lanterns, then a statue and festival lights. Made on every screen from the stage the host's colony is at
 * (nothing of it is sent: each machine builds its own). Removable: no level asset is changed.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class ARPGColonyDecor : public AActor
{
	GENERATED_BODY()
public:
	ARPGColonyDecor();
	static ARPGColonyDecor* Ensure(UWorld* World);
	virtual void Tick(float DeltaSeconds) override;
private:
	void Build(int32 Stage);
	UStaticMeshComponent* Part(const TCHAR* Mesh, const TCHAR* Material, float X, float Y, float Z, const FVector& Size, float Yaw = 0, bool bSolid = false);
	void Light(float X, float Y, float Z, const FLinearColor& Color, float Intensity, float Radius);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
	int32 Built = -1;
	float Clock = 0;
};
