#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCPUController.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;

/**
 * CPU opponent that plays the same character under the same rules as a human.
 * It simulates ball flight (arc gravity, floor pickup, wall reflections and the release delay),
 * dodges by testing walk/dash/jump escapes against every incoming ball, leads moving targets,
 * searches bank shots, times throws against committed opponents and manages balls and stamina.
 */
UCLASS()
class AChaosImpactCPUController : public AAIController
{
	GENERATED_BODY()

public:
	/** Development: seconds spent thinking by every CPU so far (to measure how heavy they are). */
	static double DevThinkSeconds;
	/** Development: the same split up (perception, learning, target, dodging, offence, positioning). */
	static double DevSectionSeconds[6];
	AChaosImpactCPUController();
	virtual void Tick(float DeltaSeconds) override;
	bool UsesArcFlightMode() const;

	/** 0 = relaxed sparring partner, 1 = full strength. Scales reaction time, aim error and tactics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chaos Impact|CPU", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Skill = 1.0f;

	/**
	 * The VS rules' CPU strength (ChaosImpactMatch::CPULevel*): よわい for beginners, ふつう, つよい (the full CPU), or
	 * さいきょう, which plays the game out exactly (see the "さいきょう" section of the .cpp).
	 * Sets Skill and the handicaps below; つよい and さいきょう have none.
	 */
	void SetDifficulty(int32 Level);
	int32 GetDifficulty() const { return Difficulty; }

	/**
	 * Spawned by the game mode for a VS match or training (call before possessing). Otherwise it took over a character
	 * placed in a level (a solo mode enemy) and plays as that character's own settings say (CPULevel, SoloTeam).
	 */
	void MarkAsMatchCPU() { bMatchCPU = true; }
	bool IsMatchCPU() const { return bMatchCPU; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	struct FShotPlan
	{
		bool bValid = false;
		bool bBankShot = false;
		FVector Direction = FVector::ForwardVector;
		/** Seconds from the throw input until the ball reaches the target, including release delay. */
		float ArrivalSeconds = 0.0f;
		float Quality = 0.0f;
	};

	struct FBallThreat
	{
		TArray<FVector> Path;
		float StepSeconds = 0.0f;
		/** The ball's own size (a grown snowball or a nova is far bigger than a ball). */
		float HitRadius = 24.0f;
		/** Height either side that still counts (a beam is a tall shaft of light); 0: the ball's own. */
		float HitHeight = 0.0f;
		/** It bursts where its path ends, hitting everyone within this far (0: no blast). */
		float BurstRadius = 0.0f;
		bool bBursts = false;
		/** A fire ball: the ground under its whole path burns once it has passed. */
		bool bTrail = false;
	};

	/** さいきょう: a throw worked out with the real flight of the ball in hand, and whether it can be escaped at all. */
	struct FSureShot
	{
		bool bValid = false;
		/** Neither walking nor a dash in time gets the target out of it. */
		bool bSure = false;
		/** Walking cannot get the target out of it: only a dash can. */
		bool bWalkProof = false;
		FVector Direction = FVector::ForwardVector;
		/** Seconds from releasing the throw until it hits. */
		float ArrivalSeconds = 0.0f;
	};

	// Perception
	void UpdatePerception(const AChaosImpactCharacter* Self, float DeltaSeconds, double Now);
	FVector GetObservedVelocity(const AChaosImpactCharacter* Observed) const;
	float GetReactionSeconds() const;

	// Ball flight prediction
	void SimulateBallPath(const FVector& Start, FVector Velocity, bool bArc, float HorizonSeconds,
		float StepSeconds, const AActor* IgnoredA, const AActor* IgnoredB, TArray<FVector>& OutPath) const;
	float GetArcFlightLimitSeconds() const;

	// Defence
	void CollectThreats(const AChaosImpactCharacter* Self, double Now, TArray<FBallThreat>& OutThreats) const;
	float EvaluateEscape(const AChaosImpactCharacter* Self, const TArray<FBallThreat>& Threats,
		const FVector& Direction, float TravelLimit, bool bDash, bool bJump) const;
	float EarliestContactSeconds(const AChaosImpactCharacter* Self, const TArray<FBallThreat>& Threats) const;
	bool UpdateEvasion(AChaosImpactCharacter* Self, float Now);
	/** Steps off the line of an opponent's tornado heading this way, dashing out when it is already close. */
	bool UpdateTornadoEvasion(AChaosImpactCharacter* Self, float Now);

	// Offence
	AChaosImpactCharacter* SelectTarget(const AChaosImpactCharacter* Self) const;
	FShotPlan PlanShot(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target,
		float ChargeAlpha, bool bAllowBank) const;
	float FindShotArrival(const TArray<FVector>& Path, float StepSeconds, const AChaosImpactCharacter* Target,
		const FVector& TargetVelocity, float LaunchDelay) const;
	float ChooseDesiredCharge(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target) const;
	void UpdateOffense(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, float Now);

	// Positioning and ball economy
	AChaosImpactBall* SelectPickup(const AChaosImpactCharacter* Self) const;
	bool TryCollectNearbyBall(AChaosImpactCharacter* Self, float Now);
	void UpdatePositioning(AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, float Now);

	// Navigation
	FVector AvoidNearbyObstacle(const FVector& Origin, const FVector& DesiredDirection) const;
	/** Steering with a short commitment, so the CPU cannot dither between two ways around the same wall. */
	FVector SteerAroundObstacles(const FVector& Origin, const FVector& DesiredDirection, float Now);
	/** Walls are hit; balls and other characters are not. Returns how far it got and the wall it met. */
	float SweepWalls(const FVector& Origin, const FVector& Direction, float MaxDistance,
		FVector& OutWallNormal, const AActor* IgnoredActor = nullptr) const;
	/** Keeps characters from piling into each other while they share a lane. */
	FVector GetSeparationDirection(const AChaosImpactCharacter* Self) const;
	bool IsPathClear(const FVector& Origin, const FVector& Direction, float Distance) const;
	float GetFreeTravel(const FVector& Origin, const FVector& Direction, float MaxDistance,
		const AActor* IgnoredActor = nullptr) const;
	bool HasLowObstacleAhead(const FVector& Origin, const FVector& DesiredDirection) const;
	void TryTraverseObstacle(AChaosImpactCharacter* ControlledCharacter,
		const FVector& Direction, float Now);
	void UpdateStuckRecovery(AChaosImpactCharacter* ControlledCharacter, float Now);

	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> LastObservedLocations;
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> ObservedVelocities;
	/** How the target's speed is changing, so a shot leads a player who is still turning. */
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FVector> ObservedAccelerations;
	FVector GetObservedAcceleration(const AChaosImpactCharacter* Observed) const;
	TMap<TWeakObjectPtr<AChaosImpactBall>, double> BallFirstSeenAt;
	TWeakObjectPtr<AChaosImpactCharacter> CurrentTarget;

	/** Height of the ball above this character's feet at release; refined from observed throws. */
	float LearnedReleaseHeight = 130.0f;
	float DesiredChargeAlpha = 0.5f;
	float ChargeStartedAt = 0.0f;
	float LastValidShotAt = 0.0f;
	float ShotAimErrorDegrees = 0.0f;

	float NextDecisionAt = 0.0f;
	float NextThrowAt = 0.0f;
	float NextBankSearchAt = 0.0f;
	float NextStrafeChangeAt = 0.0f;
	float NextJumpAt = 0.0f;
	float StopJumpAt = 0.0f;
	FVector DesiredMoveDirection = FVector::ZeroVector;
	FVector EscapeMoveDirection = FVector::ZeroVector;
	FVector EvadeDirection = FVector::ZeroVector;
	FVector LastProgressLocation = FVector::ZeroVector;
	/** The way around an obstacle that is being followed, and how long it is kept before rethinking. */
	FVector AvoidCommitDirection = FVector::ZeroVector;
	float AvoidCommitUntil = 0.0f;
	/** Consecutive checks that made no progress; each one tries a stronger way out. */
	int32 StuckStreak = 0;
	FVector HomeLocation = FVector::ZeroVector;
	float DesiredMoveScale = 0.0f;
	float LastProgressCheckAt = 0.0f;
	float EscapeUntil = 0.0f;
	float EvadeUntil = 0.0f;
	/** The current dodge is out of a tornado's way (kept while it lasts, over ball dodges). */
	float TornadoEvadeUntil = 0.0f;
	float StrafeSign = 1.0f;
	bool bHoldingJump = false;

	// ---- さいきょう
	bool bPerfect = false;
	/**
	 * Hunting: behind on points, the match nearly over without a lead, or the target simply running. Then it cuts the
	 * runner off from open floor (herding them to the walls), closes in with dashes (always keeping one for a dodge)
	 * and keeps throwing to wear their dashes down, instead of waiting for a certain shot that never comes.
	 */
	bool UpdateHunting(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, float Now);
	bool bHunting = false;
	/**
	 * Around walls: the stage as a grid of metre squares (built once per stage), and from the goal how many squares
	 * away every square is. When a wall stands between it and where it is going, さいきょう walks the shortest way
	 * round instead of pressing into the wall.
	 */
	FVector RouteToward(const AChaosImpactCharacter* Self, const FVector& Goal, float Now);
	TArray<int32> RouteDistances;
	FIntPoint RouteGoalCell = FIntPoint(-1, -1);
	float RouteBuiltAt = -100.0f;
	FVector RouteDirection = FVector::ZeroVector;
	float RouteDirectionAt = -100.0f;
	FVector RouteDirectionGoal = FVector::ZeroVector;
	float DevTraceAt = 0.0f;
	static constexpr float PerfectDecisionSeconds = 0.025f;
	static constexpr float PerfectShotPlanSeconds = 0.05f;
	/** Stamina a hunting dash needs: one dash's worth always stays back for getting out of a ball's way. */
	static constexpr float HuntDashStaminaReserve = 2.2f;
	/** Hunting throws (not certain ones) only from this near: from further a dodger always gets out of the way. */
	static constexpr float HuntThrowRange = 1500.0f;
	float HuntUntil = 0.0f;
	float NextHuntDashAt = 0.0f;
	static int32 GetPoints(const AChaosImpactCharacter* Character);
	/** Holding a snowball: waggling the movement in place rolls it up far faster than walking (and keeps it here). */
	bool bSnowMashing = false;
	FVector SnowMashAxis = FVector::ZeroVector;
	float SnowMashSign = 1.0f;
	float NextSnowMashFlipAt = 0.0f;
	/** The shot planned last: planned a few times a second while charging, not every frame (it is costly). */
	FSureShot CachedShot;
	float CachedShotAt = -1.0f;
	TWeakObjectPtr<const AChaosImpactCharacter> CachedShotTarget;
	/** Burning ground right now (fire, fire trails, a black hole's centre): x, y, z and reach. A dodge never runs through it. */
	TArray<FVector4> ActiveBurns;
	void CollectActiveBurns(const AChaosImpactCharacter* Self);
	/** A ball's whole flight as it really goes: its own size, gravity, homing, rebounds and speed-ups, and its blast. */
	void SimulateTypedFlight(const FVector& Start, FVector Velocity, EChaosImpactBallType Type, float Radius, bool bArc,
		float GravityScale, float HorizonSeconds, float StepSeconds, const AActor* IgnoredA, const AActor* IgnoredB,
		const AChaosImpactCharacter* HomingTarget, FBallThreat& Out) const;
	static float GetBurstRadius(EChaosImpactBallType Type, float Scale);
	/** Every ball that could still reach this CPU, seen at once, flown exactly. */
	void CollectExactThreats(const AChaosImpactCharacter* Self, TArray<FBallThreat>& OutThreats) const;
	/** Where the target will be after Seconds: it keeps its way (not through walls), a dash stops at its end, a black hole draws it in. */
	FVector PredictTargetAt(const AChaosImpactCharacter* Target, float Seconds) const;
	FSureShot PlanSureShot(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, float ChargeAlpha) const;
	void UpdateOffensePerfect(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, float Now);
	void UpdateNovaPerfect(AChaosImpactCharacter* Self, AChaosImpactCharacter* Target, float Now);
	/** Nobody armed is near enough to punish standing still for a nova's charge. */
	bool IsNovaSafe(const AChaosImpactCharacter* Self) const;
	/** How bad it is to stand there: burning ground, a black hole, smoke, a tornado, a nova coming down, the stage's edge. */
	float GetHazardAt(const AChaosImpactCharacter* Self, const FVector& Point) const;
	/** How exposed a spot is: armed opponents with a clear line to it, the nearer the worse. */
	float GetExposureAt(const AChaosImpactCharacter* Self, const FVector& Point) const;
	/** Armed opponents near enough, with a clear line, to throw at this CPU now. */
	int32 CountArmedThreats(const AChaosImpactCharacter* Self) const;
	/** The way closest to Desired that keeps out of hazards and corners. */
	FVector ChooseSafeDirection(const AChaosImpactCharacter* Self, const FVector& Desired) const;
	static float GetPickupValue(EChaosImpactBallType Type);
	/** The same while hunting a runner (fast balls and wide ones first). */
	static float GetHuntPickupValue(EChaosImpactBallType Type);

	/**
	 * さいきょう keeps learning through the match. Each opponent's habits: how quickly they start to dodge a throw,
	 * how far off a look-ahead at their movement turns out, and how often they answer a throw with a dash. And every
	 * hit that gets through to it: dodges keep a wider berth, dashes come earlier, blasts are given more room.
	 */
	struct FOpponentModel
	{
		float ReactionSeconds = 0.15f;
		float PredictionError = 0.0f;
		float DashAnswerRate = 0.5f;
		int32 ThrowsSeen = 0;
		double WatchSince = -1.0;
		FVector WatchVelocity = FVector::ZeroVector;
		/** Across the throw watched: stepping this way is getting out of its way. */
		FVector WatchSide = FVector::ZeroVector;
		TArray<TPair<double, FVector>, TInlineAllocator<8>> Predictions;
		double NextPredictionAt = 0.0;
	};
	TMap<TWeakObjectPtr<AChaosImpactCharacter>, FOpponentModel> Models;
	void UpdateLearning(const AChaosImpactCharacter* Self, float Now);
	/** A throw just went at Target along Direction: watch how it answers. */
	void NoteThrowAt(const AChaosImpactCharacter* Target, const FVector& Direction, float Now);
	const FOpponentModel* FindModel(const AChaosImpactCharacter* Target) const;
	float LastSelfHealth = -1.0f;
	double LastLearnedHitAt = -100.0;
	int32 HitsTaken = 0;
	float LearnedDodgeMargin = 0.0f;
	float LearnedDashLead = 0.0f;
	float LearnedBurstMargin = 0.0f;
	float LearnedHorizonBonus = 0.0f;
	/** How far a dodge must keep from a ball beyond touching (wider once hits have got through). */
	float GetSafeMargin() const;

	/** Somewhere near to walk straight to with a clear shot at the target, when a wall is in the way. */
	bool FindVantagePoint(const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target, float Ideal, FVector& OutPoint) const;
	bool HasClearShotFrom(const FVector& Point, const AChaosImpactCharacter* Self, const AChaosImpactCharacter* Target) const;
	FVector VantagePoint = FVector::ZeroVector;
	float VantageUntil = 0.0f;

	bool bMatchCPU = false;

	// Handicaps for the weaker CPUs (SetDifficulty); at つよい they change nothing.
	int32 Difficulty = 2;
	/** The share of incoming balls noticed at all; the rest are not dodged. */
	float DodgeChance = 1.0f;
	/** Balls this CPU did not notice. */
	TSet<TWeakObjectPtr<AChaosImpactBall>> UnnoticedBalls;
	/** Added to the reaction time from Skill. */
	float ExtraReactionSeconds = 0.0f;
	/**
	 * A shaky hand: each throw leaves up to this many degrees off the planned line. The planner corrects its
	 * own aim error, so this is added only to the direction actually thrown.
	 */
	float ReleaseShakeDegrees = 0.0f;
	float ReleaseShakeThisThrow = 0.0f;
	/** How much of a moving target's motion the shot leads (1 = all of it). */
	float LeadScale = 1.0f;
	/** Walking speed out of the full speed, dodges included. */
	float MoveSpeedScale = 1.0f;
	/** Longer waits between throws. */
	float ThrowDelayScale = 1.0f;
	/** Now and then stands around for a moment: the chance per second, and until when. */
	float IdleChancePerSecond = 0.0f;
	float IdleUntil = 0.0f;
	/** A perched shima-enaga flock is noticed once, then shaken off after a difficulty-dependent reaction. */
	double SimaePerchedSince = -1.0;
	double SimaeDashAt = -1.0;
	/** The planned direction with this throw's shake. */
	FVector ShakeAim(const FVector& Direction) const;
};
