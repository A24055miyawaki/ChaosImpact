#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGFieldEvents.generated.h"

class ARPGTreasureChest;
class ARPGPrototypeMerchant;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Things that happen in the field now and then (2026-11), one at a time, decided by the host and seen by everyone:
 * a falling star leaves a chest of its own; a travelling caravan sets up its workshop by a road (cheaper while it
 * stays); a snowstorm or a sandstorm sweeps its land (its enemies hit harder and drop more). Each shows on the map
 * (★) and is announced. Each machine builds the event's things itself from what the host says.
 */
UCLASS(NotBlueprintable, NotPlaceable)
class ARPGFieldEvents : public AActor
{
	GENERATED_BODY()
public:
	ARPGFieldEvents();
	enum : uint8 { None = 0, Meteor = 1, Caravan = 2, Storm = 3 };
	static ARPGFieldEvents* Find(const UWorld* World);
	/** The host: once per adventure world. */
	static ARPGFieldEvents* Ensure(UWorld* World);
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	uint8 GetKind() const { return Kind; }
	/** Counts the events started; a new one has a new number. */
	int32 GetEventId() const { return EventId; }
	FVector GetLocation() const { return At; }
	FString GetTitle() const;
	/** A storm is blowing over this point now. */
	static bool IsStormAt(const UWorld* World, const FVector& Where);
	/** Is this the caravan's workshop? */
	static bool IsCaravan(const AActor* Merchant);
	/** Tests: starts one now (host). */
	void StartEvent(uint8 InKind);
	/** Seconds until the current one ends (0 none). */
	float GetSecondsLeft() const;
private:
	UFUNCTION() void OnRep_Event();
	void Present();
	void Clear();
	bool PickPlace(uint8 InKind, FVector& Out, FString& OutRegion) const;
	UPROPERTY(ReplicatedUsing=OnRep_Event) uint8 Kind = None;
	UPROPERTY(ReplicatedUsing=OnRep_Event) int32 EventId = 0;
	UPROPERTY(Replicated) FVector_NetQuantize At = FVector::ZeroVector;
	UPROPERTY(Replicated) FString Region;
	UPROPERTY(Replicated) float EndsAt = 0;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TObjectPtr<UPointLightComponent> Glow;
	TWeakObjectPtr<ARPGTreasureChest> Chest;
	TWeakObjectPtr<ARPGPrototypeMerchant> Merchant;
	int32 Presented = -1;
	double NextEventAt = 0;
	float Clock = 0;
};
