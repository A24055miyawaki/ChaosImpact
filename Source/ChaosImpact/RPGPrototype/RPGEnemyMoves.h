#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGEnemyMoves.generated.h"

class ARPGPrototypeCharacter;
class AChaosImpactSimaeWarning;
class UStaticMeshComponent;

/**
 * The field enemies' warned attacks (2026-11): a bomb lobbed onto a circle, a charge down a lane, a sniper's shot
 * along a thin line. Made by the host; every screen draws the same warning (it replicates), and when it is due the
 * host deals the hits (a dash slips through).
 */
UCLASS(NotBlueprintable, NotPlaceable)
class ARPGGroundStrike : public AActor
{
	GENERATED_BODY()
public:
	ARPGGroundStrike();
	enum : uint8 { Bomb = 0, Charge = 1, Snipe = 2 };
	/** A bomb thrown from its source onto a circle (it lands when the warning fills). */
	static ARPGGroundStrike* Lob(ARPGPrototypeCharacter* Source, const FVector& Center, float Radius, float Delay, float Damage);
	/** A charge (its source runs down the lane) or a sniper's shot (straight through everyone on it). */
	static ARPGGroundStrike* Lane(ARPGPrototypeCharacter* Source, uint8 InKind, const FVector& From, const FVector& Toward, float InLength, float HalfWidth, float Delay, float Damage);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	uint8 GetKind() const { return Kind; }
private:
	void Fire();
	UPROPERTY(Replicated) uint8 Kind = Bomb;
	UPROPERTY(Replicated) FVector_NetQuantize Start = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector_NetQuantize ThrownFrom = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector_NetQuantizeNormal Direction = FVector::ForwardVector;
	UPROPERTY(Replicated) float Length = 0;
	UPROPERTY(Replicated) float Radius = 200;
	UPROPERTY(Replicated) float Delay = 1;
	UPROPERTY(Replicated) TObjectPtr<ARPGPrototypeCharacter> Source;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Shell;
	TWeakObjectPtr<AChaosImpactSimaeWarning> Warning;
	float Damage = 0;
	float Age = 0;
	bool bFired = false;
};
