#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ChaosImpactParty.generated.h"

class AChaosImpactCharacter;
class AChaosImpactFxKit;
class AChaosImpactPlayerState;
class APlayerState;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class AChaosImpactStageBase;
struct FChaosImpactMatchRules;
namespace ChaosImpactPaint { struct FPainter; }

/**
 * パーティルール: ルビーラッシュ (rubies lie about the stage; a ball hit shakes a share loose, a knockout half) and
 * クラウンキープ (one crown; whoever holds it walks a little slower and counts towards ChaosImpactMatch::CrownGoalCounts).
 *
 * Both are played the same way online: the host keeps the items and every count; who touched an item first is judged by
 * the time it was touched on each player's own screen (like ball pickups, see AChaosImpactBall::FairTouchReachSeconds),
 * and a player's own pickups show at once on their own screen.
 */
namespace ChaosImpactParty
{
	/** The crown's id in a claim (rubies are numbered from 1). */
	inline constexpr int32 CrownId = -1;
	/** How close a character must come to pick a ruby up (across, and up/down). */
	inline constexpr float RubyReach = 112.0f;
	inline constexpr float RubyReachHeight = 175.0f;
	inline constexpr float CrownReach = 135.0f;
	/** The crown's holder walks this much of their usual speed. */
	inline constexpr float CrownSpeedScale = 0.84f;
	/** Rubies (and the crown) a player dropped cannot be picked up by them again for this long. */
	inline constexpr float DropperBlockSeconds = 1.1f;
	/** A burst of hits (a fire zone's burns) shakes rubies loose once in this long. */
	inline constexpr float HitDropCooldownSeconds = 0.9f;
	/**
	 * Three sizes of ruby: an ordinary one (1), a large one (BigRubyValue) and a huge one (HugeRubyValue). A strong player
	 * ends a match with several hundred. A drop is at most MaxGemsPerDrop gems: more than that come as larger gems.
	 */
	inline constexpr int32 MaxGemsPerDrop = 24;
	inline constexpr int32 BigRubyValue = 5;
	inline constexpr int32 HugeRubyValue = 20;
	/** How a drop of Amount is split: ordinary, large and huge gems, at most MaxGemsPerDrop in all. */
	inline void SplitDrop(const int32 Amount, int32& OutOnes, int32& OutBig, int32& OutHuge)
	{
		OutOnes = FMath::Max(Amount, 0);
		OutBig = 0;
		OutHuge = 0;
		while (OutOnes + OutBig + OutHuge > MaxGemsPerDrop)
		{
			if (OutOnes >= BigRubyValue)
			{
				OutOnes -= BigRubyValue;
				++OutBig;
			}
			else if (OutBig >= HugeRubyValue / BigRubyValue)
			{
				OutBig -= HugeRubyValue / BigRubyValue;
				++OutHuge;
			}
			else
			{
				break;
			}
		}
	}
	/** Rubies knocked loose vanish after this long (they blink for the last few seconds). */
	inline constexpr float DroppedRubyLifeSeconds = 24.0f;
	inline constexpr float DroppedRubyBlinkSeconds = 4.0f;
	/**
	 * Rubies kept lying on the stage (more for each competitor: about 140 with eight), coming back quickly in little
	 * clusters, now and then a large one among them. A match should see a good player past a hundred.
	 */
	inline constexpr int32 FieldRubiesBase = 90;
	inline constexpr int32 FieldRubiesPerCompetitor = 6;
	inline constexpr float FieldRefillSeconds = 0.15f;
	inline constexpr int32 FieldClusterMin = 4;
	inline constexpr int32 FieldClusterMax = 7;
	/** Of the stage's own rubies, this share are large and this share huge (about 2.4 rubies a gem on average). */
	inline constexpr float FieldBigChance = 0.25f;
	inline constexpr float FieldHugeChance = 0.02f;

	/** The colours of the two rules (menu, HUD). */
	CHAOSIMPACT_API FLinearColor GetRuleColor(const FChaosImpactMatchRules& Rules);
	inline const FLinearColor RubyColor(1.0f, 0.1f, 0.3f, 1.0f);
	inline const FLinearColor CrownColor(1.0f, 0.74f, 0.16f, 1.0f);

	/** How fast this character walks under the party rules (1 but for the crown's holder). */
	CHAOSIMPACT_API float GetSpeedScale(const AChaosImpactCharacter* Character);
	/** The number shown for a competitor: rubies held (with this machine's own pickups not yet confirmed), or counts left. */
	CHAOSIMPACT_API int32 GetShownCount(const UWorld* World, const APlayerState* Member);
	/** When this competitor's shown count last went up on this machine (HUD glow; negative: never). */
	CHAOSIMPACT_API double GetCountGainedAt(const UWorld* World, const APlayerState* Member);

	/** HUD and menu pictures: a cut ruby (Size: its half height) and a crown (Size: its half width), catching the light. */
	CHAOSIMPACT_API void PaintRuby(const ChaosImpactPaint::FPainter& P, const FVector2D& Center, float Size, float Time, float Shine = 1.0f);
	CHAOSIMPACT_API void PaintCrown(const ChaosImpactPaint::FPainter& P, const FVector2D& Center, float Size, float Time, float Shine = 1.0f);

	/** The faceted gem every ruby (and the crown's stones) is drawn with: radius 1, standing on +Z. Built once. */
	CHAOSIMPACT_API UStaticMesh* GetGemMesh();
	/** Materials (Content/ChaosImpact/Party, made by Scripts/Party/make_party_materials.py), with fallbacks. */
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeGemMaterial(UObject* Outer, const FLinearColor& Body, const FLinearColor& FireColor, float Glow);
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeGemGlowMaterial(UObject* Outer, const FLinearColor& Color, float Intensity);
	CHAOSIMPACT_API UMaterialInstanceDynamic* MakeGoldMaterial(UObject* Outer, float Glow);

	/**
	 * Server: a free spot on the floor between MinDistance and MaxDistance from Origin that can be walked to from it (where
	 * rubies or the crown land when knocked loose); the nearest of Points when there is none.
	 */
	CHAOSIMPACT_API FVector FindLandSpot(UWorld* World, const AChaosImpactStageBase* Stage, const TArray<FVector>& Points,
		const FVector& Origin, float MinDistance, float MaxDistance, FRandomStream& Stream);
	/** Server: the floor under (or near) Location, if there is one within reach. */
	CHAOSIMPACT_API bool FindFloor(const UWorld* World, const FVector& Location, float Up, float Down, FVector& OutFloor);
	/** Server: room for a character standing on Floor (nothing solid in the way). */
	CHAOSIMPACT_API bool IsStandingRoom(const UWorld* World, const FVector& Floor);
	CHAOSIMPACT_API FVector NearestPoint(const TArray<FVector>& Points, const FVector& Location);
}

/** One ruby on the stage: where it flies from and lands, when, and (once taken) who took it. */
USTRUCT()
struct FChaosImpactRubyGem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	/** 1, or ChaosImpactParty::BigRubyValue for a large gem. */
	UPROPERTY() uint8 Value = 1;
	/** Knocked loose from a player (it vanishes in time); otherwise one of the stage's own. */
	UPROPERTY() bool bDropped = false;
	UPROPERTY() FVector_NetQuantize10 From = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 To = FVector::ZeroVector;
	/** Server time it set off from From (it lies at To from LaunchAt + Flight). */
	UPROPERTY() double LaunchAt = 0.0;
	UPROPERTY() float Flight = 0.5f;
	/** Taken: by whom and when (it is shown flying to them, then removed). */
	UPROPERTY() TObjectPtr<APawn> Collector = nullptr;
	UPROPERTY() double CollectedAt = 0.0;
	/** Knocked loose from this player: they cannot take it back for ChaosImpactParty::DropperBlockSeconds. */
	UPROPERTY() TObjectPtr<APawn> Dropper = nullptr;

	bool IsCollected() const { return CollectedAt > 0.0; }
	double GetLandedAt() const { return LaunchAt + Flight; }
};

USTRUCT()
struct FChaosImpactRubyGemList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FChaosImpactRubyGem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FChaosImpactRubyGem, FChaosImpactRubyGemList>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FChaosImpactRubyGemList> : public TStructOpsTypeTraitsBase2<FChaosImpactRubyGemList>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * ルビーラッシュ's rubies, all of them: kept by the host, replicated as one list, drawn on every machine as instances of
 * one faceted gem (with a soft glow round each and glints now and then).
 */
UCLASS(NotPlaceable)
class CHAOSIMPACT_API AChaosImpactRubyField : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactRubyField();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Server
	/** Where rubies may lie (floor every player can reach) and how many to keep lying about; lays them out. */
	void SetupField(AChaosImpactStageBase* InStage, const TArray<FVector>& Points, int32 Target);
	/** Amount rubies knocked loose from Dropper, flying out from Origin to the floor round it. */
	void Scatter(const FVector& Origin, int32 Amount, APawn* Dropper);
	/** A pickup touched at TouchTime (server time). Settled once nobody can have touched it earlier. */
	void Claim(AChaosImpactCharacter* Character, int32 Id, double TouchTime);
	/** The nearest point rubies may lie on (drops from a player knocked off the stage land there). */
	FVector GetNearestFieldPoint(const FVector& Location) const;
	const TArray<FVector>& GetFieldPoints() const { return FieldPoints; }

	// ---- Every machine
	/** This machine's own pickup was answered (a refusal shows the ruby again and takes back the count). */
	void OnLocalClaimResolved(const AChaosImpactCharacter* Character, int32 Id, bool bAccepted);
	/** Rubies this machine's player picked up that the host has not counted yet. */
	int32 GetPendingValue(const APawn* Pawn) const;
	/** When this competitor's shown count last went up here (server time; negative: not yet). */
	double GetCountGainedAt(const APlayerState* Member) const;
	const TArray<FChaosImpactRubyGem>& GetGems() const { return Gems.Items; }
	/** The nearest ruby that can be picked up now (CPUs), or null. */
	const FChaosImpactRubyGem* FindNearestAvailable(const FVector& Location, float MaxDistance, const APawn* For) const;
	int32 CountLying() const;

	static AChaosImpactRubyField* Get(const UWorld* World);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Replicated)
	FChaosImpactRubyGemList Gems;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GemInstances;
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> GlowInstances;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GemMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;
	TWeakObjectPtr<AChaosImpactFxKit> Sparkles;

	double GetNow() const;
	bool IsAvailable(const FChaosImpactRubyGem& Gem, double Now) const;
	bool CanTake(const FChaosImpactRubyGem& Gem, const APawn* Pawn, double Now) const;
	FChaosImpactRubyGem* FindGem(int32 Id);
	const FChaosImpactRubyGem* FindGem(int32 Id) const;
	/** Server: a free spot for a stage ruby (away from the others). */
	bool PickFieldSpot(FVector& Out, double Now);
	/** Server: a little cluster of the stage's own rubies falling in round a free spot. False when there is no room. */
	bool AddFieldCluster(double LaunchAt, float Drop, float Flight);
	void AddGem(const FVector& From, const FVector& To, double LaunchAt, float Flight, uint8 Value, bool bDropped, APawn* Dropper);
	void ServerTick(double Now);
	void SettleClaims(double Now, bool bForce);
	void DetectLocalPickups(double Now);
	void UpdateVisuals(double Now, float DeltaSeconds);
	/** Where a gem is drawn now, and how big (0 when not shown). */
	bool GetGemTransform(const FChaosImpactRubyGem& Gem, double Now, FTransform& Out, float& OutFlash) const;
	void PlayPickupEffect(const FVector& At, uint8 Value, const APawn* Collector, bool bOwn);
	void PlayLandEffect(const FVector& At, uint8 Value);
	void PlaySparkle(const FVector& At, float Size);
	AChaosImpactFxKit* GetSparkles();

	// Server
	TWeakObjectPtr<AChaosImpactStageBase> Stage;
	FRandomStream ServerStream;
	TArray<FVector> FieldPoints;
	int32 FieldTarget = 0;
	int32 NextId = 1;
	double NextRefillAt = 0.0;
	struct FClaim
	{
		TWeakObjectPtr<AChaosImpactCharacter> Best;
		double BestTouch = 0.0;
		double FirstAt = 0.0;
		TArray<TWeakObjectPtr<AChaosImpactCharacter>> Claimers;
	};
	TMap<int32, FClaim> Claims;

	// Every machine: this machine's players' pickups not yet answered, and what has been shown of each gem.
	struct FLocalClaim
	{
		TWeakObjectPtr<const APawn> Pawn;
		int32 Value = 0;
		double At = 0.0;
		/** The count the host's number reaches once this one is in (until then it is added on top). */
		int32 Threshold = 0;
		int32 PointsAtClaim = 0;
		bool bResolved = false;
		double ResolvedAt = 0.0;
	};
	TMap<int32, FLocalClaim> LocalClaims;
	struct FSeenCount
	{
		int32 Count = 0;
		double GainedAt = -100.0;
	};
	TMap<TWeakObjectPtr<const APlayerState>, FSeenCount> SeenCounts;
	struct FShown
	{
		bool bLanded = false;
		bool bCollected = false;
		FVector Last = FVector::ZeroVector;
	};
	TMap<int32, FShown> Shown;
	/** Gems flying into whoever took them (drawn a moment after they are gone from the list). */
	struct FFlyIn
	{
		FVector From = FVector::ZeroVector;
		TWeakObjectPtr<const APawn> To;
		double Start = 0.0;
		uint8 Value = 1;
		bool bOwn = false;
	};
	TArray<FFlyIn> FlyIns;
	double NextSparkleAt = 0.0;
	int32 PickupStreak = 0;
	double PickupStreakAt = -10.0;
	FRandomStream VisualStream;
};

/** The crown's state, kept by the host. */
USTRUCT()
struct FChaosImpactCrownState
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<AChaosImpactCharacter> Holder = nullptr;
	UPROPERTY() TObjectPtr<APlayerState> HolderState = nullptr;
	/**
	 * On the floor: flying from From to To (server times), then lying at To. Held, after a hook: flying from From to its
	 * holder's head until LaunchAt + Flight (the count starts then).
	 */
	UPROPERTY() FVector_NetQuantize10 From = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 To = FVector::ZeroVector;
	UPROPERTY() double LaunchAt = 0.0;
	UPROPERTY() float Flight = 0.0f;
	/** When it was last taken and dropped (the announcements), and when the count in progress began. */
	UPROPERTY() double TakenAt = -100.0;
	UPROPERTY() double DroppedAt = -100.0;
	UPROPERTY() double CountFrom = 0.0;
	/** Who dropped it last (they cannot take it back until DropBlockUntil). */
	UPROPERTY() TObjectPtr<APlayerState> LastDropper = nullptr;
	UPROPERTY() double DropBlockUntil = 0.0;
};

/** クラウンキープ's crown: on the floor, flying, or worn over its holder's head. */
UCLASS(NotPlaceable)
class CHAOSIMPACT_API AChaosImpactCrown : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactCrown();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Server
	/** Lays it at Floor (it falls there from high above over Flight seconds). */
	void PlaceAt(const FVector& Floor, float Flight);
	void Claim(AChaosImpactCharacter* Character, double TouchTime);
	/** Knocked off its holder: flies to Land (a free spot near by). */
	void DropFromHolder(const FVector& Land);
	/** A hook (not an anchor) of By's can catch it now: lying on the floor, or worn by somebody else. */
	bool CanBeHooked(const APawn* By) const;
	/** Caught by By's hook at From (on the floor, or off its holder's head): reeled in to By, who holds it from then on. */
	void TakeByHook(AChaosImpactCharacter* By, const FVector& From);
	/** Server: a count went by (the HUD's ring starts again). */
	void StartNextCount(double From);
	/** The holder made their last count, or the match is over: it stays where it is. */
	void Freeze() { bFrozen = true; }

	// ---- Every machine
	AChaosImpactCharacter* GetHolder() const;
	APlayerState* GetHolderState() const { return State.HolderState; }
	const FChaosImpactCrownState& GetState() const { return State; }
	bool IsOnFloor() const { return !GetHolder(); }
	/** Where it is drawn now (the HUD's arrow points there). */
	FVector GetShownLocation() const { return ShownLocation; }
	/** How far into the count in progress its holder is (0..1). */
	float GetCountProgress() const;
	void OnLocalClaimResolved(const AChaosImpactCharacter* Character, bool bAccepted);

	static AChaosImpactCrown* Get(const UWorld* World);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FChaosImpactCrownState State;
	UFUNCTION()
	void OnRep_State();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Model;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Halo;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GoldMaterial;
	TWeakObjectPtr<AChaosImpactFxKit> Motes;

	void BuildModel();
	double GetNow() const;
	bool CanTake(const AChaosImpactCharacter* Character, double Now) const;
	void Take(AChaosImpactCharacter* Character, double Now);
	void SettleClaim(double Now, bool bForce);
	void DetectLocalPickup(double Now);
	void UpdateVisuals(double Now, float DeltaSeconds);
	void PlayTakeEffect(const FVector& At, bool bOwn);
	void PlayDropEffect(const FVector& At);

	bool bFrozen = false;
	// Server: the claims on it so far.
	TWeakObjectPtr<AChaosImpactCharacter> ClaimBest;
	double ClaimBestTouch = 0.0;
	double ClaimFirstAt = 0.0;
	TArray<TWeakObjectPtr<AChaosImpactCharacter>> Claimers;
	// Every machine: this machine's player who took it, not yet answered.
	TWeakObjectPtr<const AChaosImpactCharacter> LocalTaker;
	double LocalTakenAt = 0.0;
	FVector ShownLocation = FVector::ZeroVector;
	bool bShownPlaced = false;
	double SeenTakenAt = -100.0;
	double SeenDroppedAt = -100.0;
	double NextMoteAt = 0.0;
	float Spin = 0.0f;
};
