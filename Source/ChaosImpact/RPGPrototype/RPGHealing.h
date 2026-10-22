#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGHealing.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ARPGPrototypeCharacter;

/**
 * Ways to get HP back in the field (amounts in RPGTuning.h, "回復"):
 * - a heart some enemies drop (every boss drops a few): walk over it to heal a quarter of your HP;
 * - healing herbs by the rest beacons and the enemy camps: walk into the bush to eat its berries, which grow back;
 * - standing by a rest beacon while not fighting (RPGRevive.cpp), and the colony itself (RPGAdventure.cpp).
 * Hearts and herbs are shared: whoever takes them first. Only someone who is hurt takes them.
 */
UCLASS(NotBlueprintable)
class ARPGHealPickup : public AActor
{
	GENERATED_BODY()
public:
	ARPGHealPickup();
	virtual void Tick(float DeltaSeconds) override;
	/** Host only. */
	static ARPGHealPickup* Drop(UWorld* World, const FVector& At);
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY() TObjectPtr<USceneComponent> Spinner;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	FVector Rest = FVector::ZeroVector;
	double BornAt = 0;
};

UCLASS(NotBlueprintable)
class ARPGHealHerb : public AActor
{
	GENERATED_BODY()
public:
	ARPGHealHerb();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	bool IsRipe() const { return bRipe; }
	/** Host only: herbs by every rest beacon and enemy camp (once per world). */
	static void SpawnField(UWorld* World);
	/** Tests: grows the berries back now. */
	void Regrow();
private:
	UPROPERTY(ReplicatedUsing = OnRep_Ripe) bool bRipe = true;
	UFUNCTION() void OnRep_Ripe();
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Berries;
	double RipeAt = 0;
};
