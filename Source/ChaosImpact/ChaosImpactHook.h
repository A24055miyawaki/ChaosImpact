#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactHook.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
struct FChaosImpactDuelThrowDamage;

/** Where a hook is in its life. */
UENUM()
enum class EChaosImpactHookPhase : uint8
{
	/** Flying out from the hand, straight, until it catches something, meets a wall or reaches its length. */
	Out,
	/** Caught an opponent or a ball and reeling it in to the thrower. */
	Pull,
	/** Coming back empty (or after its catch) to the hand. */
	Back
};

/**
 * The hook ball's hook: released the moment the ball leaves the hand, a three-pronged steel hook on a chain flies out
 * straight. An opponent it catches takes one hit and is reeled in to just in front of the thrower (a dash on their
 * side breaks free); a ball lying on the floor is reeled in to the thrower's hand (onto the floor at their feet when
 * their hands are full). A wall or its full length sends it back empty.
 *
 * As with the tornado, its flight out is a function of the server time it started, so every machine draws it in the
 * same place without sending its position. The server catches the characters it moves itself (the host, CPUs) and
 * every ball; a remote player's own screen judges their own contact, starts their pull there and reports it.
 */
UCLASS(NotPlaceable)
class AChaosImpactHook : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactHook();

	/**
	 * Server: a hook from From (the hand), heading along Direction (flat), thrown by Source. bAnchor: an anchor ball's
	 * anchor instead (sticks in a wall or the floor and whisks the thrower there; catches nobody and nothing).
	 */
	static AChaosImpactHook* Release(UWorld* World, const FVector& From, const FVector& Direction, APawn* Source, bool bAnchor = false);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: a remote player's screen saw this hook catch them. True when it counted (they are hit and pulled). */
	bool TryApplyReportedHit(AChaosImpactCharacter* Victim);

	APawn* GetSourcePawn() const { return SourcePawn; }
	TSharedPtr<FChaosImpactDuelThrowDamage> GetDuelThrowDamage() const { return DuelThrowDamage; }
	EChaosImpactHookPhase GetPhase() const { return Phase; }
	bool IsAnchor() const { return bAnchor; }
	/** An anchor stuck where it can carry its thrower, and the time has come (every machine works it out the same). */
	bool ShouldZipNow() const;
	/** Where an anchor puts its thrower down (feet on the floor), and how high over the way the flight arcs. */
	FVector GetZipTarget() const { return ZipTarget; }
	FVector GetAnchorPoint() const { return AnchorPoint; }
	/** Stuck in a wall (its thrower lands in front of it) rather than the floor. */
	bool IsWallAnchor() const { return bAnchor && FVector::Dist2D(FVector(AnchorPoint), FVector(ZipTarget)) > 20.0f; }
	/** Reeling in what it caught (the thrower's hands pull hand over hand). */
	bool IsReeling() const { return Phase == EChaosImpactHookPhase::Pull && Caught != nullptr; }
	/** The hook's head on this machine now. */
	FVector GetHeadLocation() const { return Head; }
	/** Where a pulled character is reeled to: just in front of the thrower, on the side they come from. */
	FVector GetPullTarget(const FVector& From) const;

	static constexpr float OutSpeed = 3300.0f;
	static constexpr float Length = 2400.0f;
	static constexpr float BackSpeed = 4200.0f;
	/** How fast a caught opponent or ball is reeled in. */
	static constexpr float PullSpeed = 2300.0f;
	/** A pulled opponent stops this far from the thrower. */
	static constexpr float PullStopDistance = 150.0f;
	/** At most this long reeling an opponent (a wall in the way, a dash). */
	static constexpr float PullMaxSeconds = 1.3f;
	/** The head catches within this much of a character's capsule, or of a ball. */
	static constexpr float CatchRadius = 45.0f;
	static constexpr float BallCatchRadius = 95.0f;
	/** A ball this far above or below the hook's line (on the floor, on a pad) is still caught. */
	static constexpr float BallCatchHeight = 260.0f;
	/** A report from a remote player's screen may be this much further off than a local contact. */
	static constexpr float ReportTolerance = 360.0f;
	/** An anchor's flight to where it sticks, and its thrower's flight there. */
	static constexpr float AnchorSpeed = 4200.0f;
	static constexpr float ZipSpeed = 2700.0f;
	static constexpr float ZipMaxSeconds = 1.5f;

private:
	double ServerNow() const;
	/** The thrower's hand on this machine (where the chain starts). */
	FVector GetHand() const;
	/** The head's flight out: where it is TimeOut seconds after leaving the hand. */
	FVector OutPosition(float TimeOut) const;
	bool CanCatch(const AActor* Victim) const;
	/** Contact with the stretch the head moved along this frame (From to To): characters, balls, walls. */
	void CatchAlong(const FVector& From, const FVector& To);
	/** Server: hook Victim (one hit, then reeled in). */
	void CatchCharacter(AChaosImpactCharacter* Victim);
	/** Server: hook a lying ball (reeled in to the hand). */
	void CatchBall(AChaosImpactBall* Ball);
	/** Server: done with the catch (or nothing caught): back to the hand from where the head is. */
	void GoBack();
	void ServerTick(float DeltaSeconds);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLatch(FVector_NetQuantize Location, bool bOnCharacter);

	void BuildPresentation();
	void UpdatePresentation(float DeltaSeconds);

	UPROPERTY(Replicated)
	FVector_NetQuantize Origin;

	UPROPERTY(Replicated)
	FVector_NetQuantizeNormal Heading;

	UPROPERTY(Replicated)
	double StartServerTime = 0.0;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> SourcePawn;

	UPROPERTY(Replicated)
	EChaosImpactHookPhase Phase = EChaosImpactHookPhase::Out;

	/** When the phase now began, and where the head was then (the way back starts there). */
	UPROPERTY(Replicated)
	double PhaseServerTime = 0.0;

	UPROPERTY(Replicated)
	FVector_NetQuantize PhaseFrom;

	/** What it caught (an opponent or a ball), while it reels it in. */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> Caught;

	/** The anchor form: where it sticks (on a wall or in the floor; none: it comes back), when, and where its thrower lands. */
	UPROPERTY(Replicated)
	bool bAnchor = false;

	UPROPERTY(Replicated)
	bool bAnchorHolds = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize AnchorPoint;

	UPROPERTY(Replicated)
	FVector_NetQuantize ZipTarget;

	UPROPERTY(Replicated)
	double ZipServerTime = 0.0;

	/** Server: work out where an anchor sticks, from its line (walls and floors are the same on every machine). */
	void PlaceAnchor();

	TSharedPtr<FChaosImpactDuelThrowDamage> DuelThrowDamage;
	/** The head now and a frame ago, on this machine. */
	FVector Head = FVector::ZeroVector;
	FVector LastHead = FVector::ZeroVector;
	bool bHeadStarted = false;
	/** This machine already caught (or reported) something: it does not catch again. */
	bool bLatchedHere = false;
	/** Where the head stopped against a wall on this machine (it goes back from there). */
	bool bBlockedHere = false;
	double BlockedAt = 0.0;
	FVector BlockedFrom = FVector::ZeroVector;
	/** Server: the caught ball reaching the hand. */
	TWeakObjectPtr<AChaosImpactBall> PulledBall;
	/** A nova part caught on the floor (or its pad), reeled in to its thrower like a ball. */
	TWeakObjectPtr<class AChaosImpactNovaPart> PulledPart;
	void CatchPart(class AChaosImpactNovaPart* Part);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HeadRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Prongs;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Hub;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Links;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Cable;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Spark;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CableMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SparkMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> HeadLight;

	bool bPresentationBuilt = false;
	/** The prongs close as it catches (0 open, 1 shut), and the latch's spark. */
	float Grip = 0.0f;
	double SparkAt = -100.0;
	FVector SparkLocation = FVector::ZeroVector;
};
