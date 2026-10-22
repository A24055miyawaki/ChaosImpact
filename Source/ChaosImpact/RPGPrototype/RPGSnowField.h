#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGPrototype.h"
#include "RPGSnowField.generated.h"

class AChaosImpactSimaeBoss;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * A gate that takes a player between the colony and the snowfield. Interact ([E] / 上ボタン) when standing at it.
 * Each player goes on their own (a shared screen pulls the others after player 1 as usual).
 */
UCLASS()
class ARPGWarpGate : public AActor
{
	GENERATED_BODY()
public:
	ARPGWarpGate();
	UPROPERTY(EditAnywhere, Category="RPG Snowfield") FString DisplayName = TEXT("雪原への門");
	UPROPERTY(EditAnywhere, Category="RPG Snowfield") FVector Destination = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category="RPG Snowfield") float DestinationYaw = 0;
	bool CanUse(const ARPGPrototypeCharacter* Player) const;
	bool Use(ARPGPrototypeCharacter* Player);
	static ARPGWarpGate* Nearest(const ARPGPrototypeCharacter* Player);
	void Build(bool bSnow);
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Portal;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
};

/**
 * The RPG's shell round the solo Shima-enaga boss (AChaosImpactSimaeBoss, whose moves, warnings, power-up and aura are
 * used as they are). RPG balls hit RPG characters, so this invisible RPG enemy stands where the boss stands, takes
 * the RPG damage and passes it on (the boss's own rules apply: asleep and powering up it cannot be hurt, and it always
 * powers up at half). It shows RPG health, numbers and the boss bar, and gives the boss rewards when it falls.
 */
UCLASS()
class ARPGSimaeBody : public ARPGPrototypeCharacter
{
	GENERATED_BODY()
public:
	ARPGSimaeBody();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
	virtual bool IsCrowdControlImmune() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Bind(AChaosImpactSimaeBoss* InBoss, int32 InLevel);
	AChaosImpactSimaeBoss* GetBoss() const { return Boss.Get(); }
	bool IsAwake() const;
	bool IsDefeated() const { return bRewarded; }
	/** For every screen's boss bar (online the host's boss decides; these follow it). */
	bool IsBossFullPower() const { return (BossFlags & 2) != 0; }
	bool IsBossPoweringUp() const { return (BossFlags & 4) != 0; }
protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(Replicated) TObjectPtr<AChaosImpactSimaeBoss> Boss;
	/** 1 awake, 2 full power, 4 powering up. */
	UPROPERTY(Replicated) uint8 BossFlags = 0;
	bool bRewarded = false;
};

/**
 * The snowfield: built when the adventure opens, off the main map, and reached through a gate near the colony board.
 * A camp (warm light, the watcher ユキ, a rest beacon), a pine wood and a frozen lake with enemy groups, and at the far
 * end the Shima-enaga's nest: a walled ring where the boss sleeps until a player walks in. The entrance freezes shut
 * while it fights; all players knocked out or gone resets it, and it comes back a while after it is beaten.
 *
 * Removable: everything is created at run time from this class; no level asset is changed.
 */
UCLASS()
class ARPGSnowField : public AActor
{
	GENERATED_BODY()
public:
	ARPGSnowField();
	virtual void Tick(float DeltaSeconds) override;
	/** Spawns the snowfield (and its gates) once per world. */
	static ARPGSnowField* Ensure(UWorld* World, const FTransform& MapOrigin);
	static ARPGSnowField* Find(const UWorld* World);
	/** Place names inside the snowfield ("雪原・…"), empty outside it. */
	static FString RegionAt(const UWorld* World, const FVector& At);
	/** Where the snowfield starts (a little in front of its gate) and the nest's middle. */
	FVector ArrivalPoint() const;
	FVector NestCenter() const;
	float NestRadius() const { return 1700; }
	/** The boss is awake and the nest is shut. */
	bool IsNestFight() const { return bFighting; }
	ARPGSimaeBody* GetBossBody() const { return Body.Get(); }
	AChaosImpactSimaeBoss* GetBoss() const;
	/** Tests: brings the boss back at once (asleep). */
	void ResetBoss();
	/** World position of the snowfield's origin (its middle). */
	static FVector Origin() { return FVector(15000, 62000, 0); }
	/** Half size of the snow plain. */
	static FVector2D HalfSize() { return FVector2D(7200, 4600); }
	/** Level of the boss and of the enemy groups. */
	static constexpr int32 BossLevel = 12;
protected:
	virtual void BeginPlay() override;
private:
	void Build();
	UStaticMeshComponent* Part(const TCHAR* Shape, const TCHAR* Material, const FVector& At, const FVector& Size,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bSolid = false);
	void Pine(const FVector& At, float Scale);
	void Lantern(const FVector& At);
	void SpawnBoss();
	void SetNestClosed(bool bClosed);
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Pieces;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> NestDoor;
	TWeakObjectPtr<AChaosImpactSimaeBoss> BossActor;
	TWeakObjectPtr<ARPGSimaeBody> Body;
	bool bFighting = false;
	double EmptySince = -1;
	double DefeatedAt = -1;
};
