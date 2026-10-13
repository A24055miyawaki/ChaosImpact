#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChaosImpactNovaRig.h"
#include "ChaosImpactNovaParts.generated.h"

class AChaosImpactCharacter;
class UPointLightComponent;
class USphereComponent;
class UStaticMeshComponent;

/** The three parts a nova is made from. A player carrying all three has a nova in a ball slot. */
UENUM(BlueprintType)
enum class EChaosImpactNovaPart : uint8
{
	Gear UMETA(DisplayName="ギア"),
	Core UMETA(DisplayName="コア"),
	Ring UMETA(DisplayName="リング")
};

namespace ChaosImpactNovaParts
{
	constexpr uint8 All = 0b111;
	inline uint8 Bit(const EChaosImpactNovaPart Part) { return static_cast<uint8>(1 << static_cast<int32>(Part)); }
	CHAOSIMPACT_API const TCHAR* GetName(EChaosImpactNovaPart Part);
	/** How high a part floats over the ground (or its pad). */
	constexpr float PartHoverHeight = 110.0f;
	/**
	 * Server: puts parts (or a finished nova, as a ball) in play near a place: on the ground there, or, with no place,
	 * out of a ball pad (new ones, and ones lost off the stage).
	 */
	CHAOSIMPACT_API void Drop(UWorld* World, uint8 Parts, const FVector* Near);
	/** Server: a part out of a ball pad: a free one if there is one, else in place of a pad's ball. False with no pad. */
	CHAOSIMPACT_API bool PutOutAtPad(UWorld* World, EChaosImpactNovaPart Part);
	CHAOSIMPACT_API void DropNovaBall(UWorld* World, const FVector* Near);
}

/**
 * A nova part waiting to be picked up: turning in the air (as its model's own animation turns it), lit, with a pillar of
 * light so it is seen from afar. Whoever touches it carries it (and drops it when knocked out).
 */
UCLASS()
class AChaosImpactNovaPart : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactNovaPart();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: a part at a place (set before it is seen). */
	static AChaosImpactNovaPart* Spawn(UWorld* World, EChaosImpactNovaPart Part, const FVector& Location);
	EChaosImpactNovaPart GetPart() const { return Part; }
	/** Server: hands the part to a character touching it (true when taken). */
	bool TryGive(AChaosImpactCharacter* Character);

protected:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void OnRep_Part();
	void BuildLook();

	UPROPERTY(ReplicatedUsing=OnRep_Part)
	EChaosImpactNovaPart Part = EChaosImpactNovaPart::Gear;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Pickup;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Pillar;

	FChaosImpactNovaRig Model;
	float ShowTime = 0.0f;
	/** Not picked up at once by whoever was standing there as it appeared or dropped. */
	double PickableAt = 0.0;
};

/**
 * Server: the nova's parts in a match. In a VS match each of the three comes out once, at a random time, out of a random
 * ball pad (so one nova a match). In training a new set comes out a little after the last nova (and its parts) are all
 * gone. Also tracks nothing else: carried parts and the nova are on the characters, dropped ones are actors.
 */
UCLASS()
class UChaosImpactNovaDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UChaosImpactNovaDirector* Get(const UObject* WorldContext);
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Tests: the whole set out now. */
	void SpawnAllNow();
	/** Whether any part or nova is anywhere in play (carried, lying, flying or going off). */
	bool IsNovaInPlay() const;
	/** Seconds into the match each part is due (negative: out already or none). */
	float GetDueAt(EChaosImpactNovaPart Part) const { return DueAt[static_cast<int32>(Part)]; }

private:
	void Schedule(float From, float To);
	void SpawnPart(EChaosImpactNovaPart Part);

	float DueAt[3] = {-1.0f, -1.0f, -1.0f};
	/** The VS match these were scheduled for (its start time), to notice a new one. */
	double MatchStartedAt = -1.0;
	/** Training: when the last nova was seen gone. */
	double NoneSince = -1.0;
	double TrainingClock = 0.0;
};
