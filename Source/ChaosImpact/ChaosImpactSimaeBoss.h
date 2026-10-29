#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactSimaeBoss.generated.h"

class AChaosImpactBall;
class AChaosImpactSimaeWarning;
class UAnimSequence;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class USkeletalMesh;
struct FGeometry;
class FSlateWindowElementList;

/** What the shima-enaga boss is doing. */
UENUM(BlueprintType)
enum class EChaosImpactSimaeMove : uint8
{
	/** Walking about between attacks. */
	None,
	/** マッスル叩きつけ: up into the air, down onto one fist. Hurts and knocks away everyone near; they drop every ball. */
	Slam,
	/** 剛球: a plain ball driven straight through a wide lane (three at full power). */
	Throw,
	/** 雪崩落とし: a roar, and great snowballs drop out of the sky where the warnings are. */
	Avalanche,
	/** 突進: a charge straight across the snow. */
	Charge,
	/** At half health: the change into full power (it cannot be hurt meanwhile). */
	PowerUp
};

/**
 * The full-power shima-enaga (シマエナガ・フルパワー): a boss in its own right, with its own model and moves. It plays
 * itself (no CPU controller) and, like any solo enemy, is a character: hit by balls, counted by the solo room, and
 * gone when knocked out. At half health it powers up: the full-power model, wrapped in a flame-like red aura, with
 * wider and faster attacks.
 *
 * Put it in a solo room's Boss Class; with the room's Boss Waits From Start it stands in the arena from the first
 * moment (asleep, and unhurt) and wakes when the room starts. Its tuning is under Chaos Impact|Simae Boss.
 */
UCLASS(Blueprintable)
class AChaosImpactSimaeBoss : public AChaosImpactCharacter
{
	GENERATED_BODY()

public:
	AChaosImpactSimaeBoss();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator,
		AActor* DamageCauser) override;

	/** Asleep: stands still and cannot be hurt (waiting in its arena). Waking starts the fight. */
	UFUNCTION(BlueprintCallable, Category="Chaos Impact|Simae Boss")
	void SetDormant(bool bNewDormant);
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Simae Boss")
	bool IsDormant() const { return bDormant; }
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Simae Boss")
	bool IsFullPower() const { return bFullPower; }
	UFUNCTION(BlueprintPure, Category="Chaos Impact|Simae Boss")
	EChaosImpactSimaeMove GetMove() const { return Move; }
	/** How strongly the aura burns (0 none, about 1 at full power). */
	float GetAuraStrength() const { return AuraStrength; }
	/** Its aura is being drawn (flames rising off it). */
	bool IsAuraDrawn() const;

	/** Tests: starts a move now (as if it had chosen it), against the nearest foe. */
	void ForceMove(EChaosImpactSimaeMove NewMove);
	/** Tests: its attacks still throw people about, but hurt nobody. */
	bool bHarmless = false;
	/** Tests: it never starts a move by itself (only ForceMove does). */
	bool bHoldMoves = false;

	/**
	 * Hits a character with one of the boss's attacks: Damage (0: none), blown along Knockback, and with bDropBalls
	 * every ball they carry falls. A dash goes through untouched. Returns whether it landed.
	 */
	static bool Strike(AChaosImpactSimaeBoss* Boss, AChaosImpactCharacter* Victim, AActor* Causer, float Damage,
		const FVector& Knockback, bool bDropBalls);
	/**
	 * A knockback cut down so whoever it throws lands inside the boss's arena (its solo room's area, or short of any
	 * wall in the way when there is no room): nobody is thrown off the stage.
	 */
	FVector KeepInArena(const AChaosImpactCharacter* Victim, const FVector& Knockback) const;
	/** The point moved inside its arena (at least Margin from the edge); as it is when there is no room. */
	FVector ClampToArena(const FVector& Point, float Margin) const;
	/** Characters the boss's attacks hurt: anyone alive who is not on its side. */
	bool IsFoe(const AChaosImpactCharacter* Character) const;
	/** Every nearby player's camera shakes. */
	void ShakeNearby(const FVector& Where, float Radius, float Strength, float Seconds) const;

	/** The boss bar at the top of the screen while one is fighting (drawn by each player's HUD). */
	static void PaintHud(const UWorld* World, const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
		float Scale);

	// ---- Tuning

	/** How large it stands (its model is about 1.9 m tall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss", meta=(ClampMin="0.5"))
	float BossScale = 2.25f;

	/** Walking pace between attacks, normal and at full power. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss")
	float WalkSpeed = 260.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss")
	float FullPowerWalkSpeed = 380.0f;

	/** Pause after each attack (seconds), normal and at full power. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss")
	float RestSeconds = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss")
	float FullPowerRestSeconds = 0.6f;

	/** How much faster every attack plays at full power. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss", meta=(ClampMin="1.0"))
	float FullPowerTempo = 1.15f;

	/** マッスル叩きつけ: how far its shock reaches, and how far it leaps toward its target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Slam")
	float SlamRadius = 360.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Slam")
	float FullPowerSlamRadius = 470.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Slam")
	float SlamLeap = 450.0f;
	/** Blown away: along the ground and up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Slam")
	float SlamKnockback = 1100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Slam")
	float SlamLift = 520.0f;

	/** 剛球: its speed, how wide the lane it tears through (radius), how far it goes, and the knockback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Goukyu")
	float GoukyuSpeed = 2500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Goukyu")
	float GoukyuRadius = 110.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Goukyu")
	float FullPowerGoukyuRadius = 145.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Goukyu")
	float GoukyuRange = 4200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Goukyu")
	float GoukyuKnockback = 1000.0f;

	/** 雪崩落とし: snowballs dropped, and how long their warnings show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Avalanche")
	int32 AvalancheCount = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Avalanche")
	int32 FullPowerAvalancheCount = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Avalanche")
	float AvalancheWarningSeconds = 1.3f;

	/** 突進: speed and how far it runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Charge")
	float ChargeSpeed = 1900.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Chaos Impact|Simae Boss|Charge")
	float ChargeDistance = 2600.0f;

protected:
	/** Its two models (they share a skeleton and every clip). */
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss")
	TObjectPtr<USkeletalMesh> NormalModel;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss")
	TObjectPtr<USkeletalMesh> FullPowerModel;

	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> IdleClip;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> WalkClip;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> ChargeClip;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> SlamClip;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> ThrowClip;
	UPROPERTY(EditDefaultsOnly, Category="Chaos Impact|Simae Boss|Clips")
	TObjectPtr<UAnimSequence> PowerUpClip;

	/** The full-power aura: flame tongues (rebuilt every frame round the body), a red glow over it, light, embers. */
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Simae Boss")
	TObjectPtr<UProceduralMeshComponent> AuraFlames;
	UPROPERTY(VisibleAnywhere, Category="Chaos Impact|Simae Boss")
	TObjectPtr<UPointLightComponent> AuraLight;

private:
	// ---- The fight
	AChaosImpactCharacter* FindTarget() const;
	void ThinkIdle(float DeltaSeconds, AChaosImpactCharacter* Target);
	void ChooseMove(AChaosImpactCharacter* Target);
	void BeginMove(EChaosImpactSimaeMove NewMove);
	void EndMove();
	void TickSlam(float DeltaSeconds);
	void TickThrow(float DeltaSeconds);
	void TickAvalanche(float DeltaSeconds);
	void TickCharge(float DeltaSeconds);
	void TickPowerUp(float DeltaSeconds);
	void SlamImpact();
	void ReleaseGoukyu();
	void DropAvalanche();
	void Transform();
	/** Lets go of a ball still in its hand (it falls where it is). */
	void DropHeldBall();
	/** Turns toward a direction at its own (heavy) pace. */
	void TurnToward(const FVector& Direction, float DeltaSeconds, float DegreesPerSecond = 0.0f);
	float Tempo() const { return bFullPower ? FullPowerTempo : 1.0f; }
	/** Where the boss's target will be in Seconds (it leads its aim). */
	FVector Predict(const AChaosImpactCharacter* Target, float Seconds) const;

	// ---- Presentation
	void PlayClip(UAnimSequence* Clip, bool bLoop, float Rate);
	void BuildPresentation();
	void UpdateAura(float DeltaSeconds);
	void RebuildAuraFlames(float Time);
	void UpdateHitFlash(float DeltaSeconds);
	FVector Bone(FName Name) const;
	FVector PalmLocation() const;
	/** Heavy steps: a puff of snow under each foot as it comes down, the ground shaking a little. */
	void SnowFootfalls(float DeltaSeconds);
	void StartPowerUp();
	/** Calls off whatever it was doing (warnings, a ball in hand, a charge's sound). */
	void AbortMove();
	void PlayDefeat();

	bool bDormant = false;
	bool bFullPower = false;
	EChaosImpactSimaeMove Move = EChaosImpactSimaeMove::None;
	EChaosImpactSimaeMove LastMove = EChaosImpactSimaeMove::None;
	int32 MovesSinceAvalanche = 0;
	/** Seconds into the current move (in the clip's own time: the tempo is already applied). */
	float MoveTime = 0.0f;
	float RestLeft = 1.2f;
	float StrafeSign = 1.0f;
	float StrafeChangeAt = 0.0f;
	bool bMoveFired = false;
	int32 MoveStage = 0;

	TWeakObjectPtr<AChaosImpactCharacter> MoveTarget;
	FVector MoveStart = FVector::ZeroVector;
	FVector MovePoint = FVector::ZeroVector;
	FVector MoveDirection = FVector::ForwardVector;
	TWeakObjectPtr<AChaosImpactSimaeWarning> MoveWarning;
	TArray<TWeakObjectPtr<AChaosImpactSimaeWarning>> ExtraWarnings;
	TWeakObjectPtr<AChaosImpactBall> HeldBall;
	TArray<TWeakObjectPtr<AActor>> ChargeHits;
	float ChargeTravelled = 0.0f;
	float NextDustAt = 0.0f;
	/** How fast the current move's clip plays (its own seconds per second). */
	float MoveRate = 1.0f;
	/** Where the walk is in its stride (a stomp lands at each half). */
	float WalkPhase = 0.0f;
	bool bChargeCrashed = false;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> PlayingClip;
	bool bPlayingLoop = false;

	float AuraStrength = 0.0f;
	float AuraTarget = 0.0f;
	float AuraBurst = 0.0f;
	float HitFlash = 0.0f;
	float HitShake = 0.0f;
	/** Health shown on the bar (it catches up with the real one). */
	float ShownHealth = -1.0f;
	float WakeTime = -1.0f;
	FVector MeshRest = FVector::ZeroVector;

	/** The aura's flames: see-through, smoky tongues of red light (M_CI_SimaeAuraFlame). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlameMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RimMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Embers;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AuraSound;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MoveSound;

	struct FFlameCard
	{
		FName Bone;
		/** Where its root sits from the bone: across the screen (X), toward/away from the camera (Y), up (Z). */
		FVector Offset;
		float Width;
		float Height;
		float Seed;
	};
	TArray<FFlameCard> FlameCards;
	/**
	 * The aura, soft FX-kit pieces riding with it: tongues of red flame rising off its outline and burning away, a hot
	 * glow at its heart and waves of pressure pulsing out over the floor. (Soft on every side: nothing cut off.)
	 */
	TWeakObjectPtr<class AChaosImpactFxKit> AuraFx;
	/** The aura hugging its outline: a copy of its body, following its pose, drawn as a shell of flame (M_CIFX_AuraShell). */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> AuraShell;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AuraShellMaterial;
	void UpdateAuraShell(bool bVisible, float Shown);
	float AuraOwed = 0.0f;
	float AuraPulseOwed = 0.0f;
	FRandomStream AuraStream;
	void FeedAura(float DeltaSeconds, float Shown);
	/** The solo room it fights in (found the first time it is needed). */
	mutable TWeakObjectPtr<class AChaosImpactSoloRoom> HomeRoom;
	mutable bool bLookedForRoom = false;
	/** The solo room whose area it stands in (looked for once). */
	const class AChaosImpactSoloRoom* FindHomeRoom() const;
};
