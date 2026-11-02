#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGAbilityGates.generated.h"

class ARPGPrototypeCharacter;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Ways that open as a hero grows (2026-11): a cracked boulder a strong tackle breaks (タックル Lv3), a wall of ice a
 * fire ball melts, and a leap across a gorge a strong dash makes (ダッシュ Lv3). Each guards a secret place. The host
 * decides boulders and ice (once open, open for everyone in the world); a leap is each player's own (they move
 * themselves). A hero who cannot yet get through is told what it takes.
 */
UCLASS()
class ARPGGateObstacle : public AActor
{
	GENERATED_BODY()
public:
	ARPGGateObstacle();
	enum : uint8 { Boulder = 0, Ice = 1 };
	UPROPERTY(EditAnywhere, Replicated, Category="RPG Gate") uint8 Kind = Boulder;
	/** Across the way it blocks (its width, in the yaw it faces). */
	UPROPERTY(EditAnywhere, Category="RPG Gate") float Width = 420;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool IsOpen() const { return bOpen; }
	/** Tests: opens it as if broken / melted. */
	void Open();
private:
	void Build();
	UFUNCTION() void OnRep_Open();
	UPROPERTY(ReplicatedUsing=OnRep_Open) bool bOpen = false;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TObjectPtr<UPointLightComponent> Glow;
	/** Ice: how far it has melted (0..1). */
	float Melt = 0;
	double NextHintAt = 0;
	float OpenAge = -1;
};

/** One end of a leap across a gorge: a dash started here with ダッシュ Lv3 carries a hero to Partner. */
UCLASS()
class ARPGLeapPoint : public AActor
{
	GENERATED_BODY()
public:
	ARPGLeapPoint();
	/** Where the leap lands (world). */
	UPROPERTY(EditAnywhere, Category="RPG Gate", meta=(MakeEditWidget="true")) FVector Partner = FVector(1000, 0, 0);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	/** A leap point under this player (within reach of its pad), if any. */
	static ARPGLeapPoint* Under(const ARPGPrototypeCharacter* Player);
	/** Throws the player across (their own machine). */
	void Launch(ARPGPrototypeCharacter* Player) const;
private:
	void Build();
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ring;
	float Clock = 0;
};
