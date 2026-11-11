#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactSettings.h"

/**
 * 訓練場 (the dojo): every rule of the game taught one lesson at a time, in four groups (基本 / 応用 / ボール / 対戦ルール).
 * A lesson is a few illustrated slides, then a task done for real on a VS stage (with CPUs, targets and balls set out for
 * it), counted down to CLEAR. This file is the catalogue; AChaosImpactDojo runs a lesson, UChaosImpactDojoMenu lists them.
 */

/** What a lesson's task asks for (how AChaosImpactDojo counts it). */
enum class EChaosImpactDojoGoal : uint8
{
	/** Walk to the glowing rings, one after another. */
	Reach,
	/** Pick up the balls lying on the floor. */
	PickUp,
	/** Throw (any charge). */
	Throw,
	/** Throw at full charge. */
	ChargedThrow,
	/** Knock down the targets. */
	HitTarget,
	/** Hit the CPU (with the lesson's ball kind when it has one). */
	HitCPU,
	/** Knock the CPU out. */
	KnockOut,
	Dash,
	Jump,
	Tackle,
	/** Swap the order of the two balls held. */
	Swap,
	/** Put a charged ball away without throwing it. */
	Cancel,
	/** Put a ball down. */
	Drop,
	/** Turn into a ball (hold the button). */
	BallForm,
	/** Open the overview map. */
	OpenMap,
	/** The CPU throws at the player: come through that many throws without being hit (a hit starts it over). */
	Dodge,
	/** As Dodge, but only a throw passed through while dashing counts. */
	DashThrough,
	/** Stop the CPU's charged throw with a tackle. */
	Interrupt,
	/** Freeze the CPU (ice). */
	Freeze,
	/** Put the CPU in smoke. */
	Blind,
	/** Shut the CPU in the prison shell. */
	Trap,
	/** Catch the CPU with a tornado. */
	WindCarry,
	/** Hook the CPU and reel it in. */
	HookPull,
	/** One beam through both CPUs. */
	BeamBoth,
	/** Stay inside the shrinking circle (sudden death) for the count in seconds. */
	StayInZone,
	/** Pick up the nova's three parts, then hit the CPU with the nova they make (count: 3 parts + 1 hit). */
	NovaBuild
};

/** How a lesson's CPUs behave (set by AChaosImpactDojo; their own brains are off unless Fighter). */
enum class EChaosImpactDojoCPU : uint8
{
	None,
	/** Stands still, facing the player. */
	Stand,
	/** Walks to and fro across the player's view. */
	Walk,
	/** Throws the lesson's CPU ball at the player every so often. */
	Thrower,
	/** Keeps charging a long throw (a tackle stops it). */
	Charger,
	/** Plays for real, at the weakest level. */
	Fighter
};

/** A slide's little moving picture (drawn by the dojo HUD and the lesson list's preview). */
enum class EChaosImpactDojoArt : uint8
{
	Move,
	PickUp,
	Throw,
	Charge,
	Aim,
	Dash,
	Jump,
	Tackle,
	Swap,
	Cancel,
	Drop,
	Health,
	KnockOut,
	Moving,
	Dodge,
	DashThrough,
	Interrupt,
	BallForm,
	Map,
	/** The lesson's ball, held up with what it does. */
	Ball,
	Teams,
	Duel,
	SuddenDeath,
	Review,
	Stamina,
	Curve,
	/** The drive course: a drive ball steered down the winding road onto the CPU. */
	DriveCourse
};

/** The part of the HUD a slide or task points at. */
enum class EChaosImpactDojoPointer : uint8
{
	None,
	Health,
	Stamina,
	Balls
};

struct FChaosImpactDojoSlide
{
	FString Text;
	EChaosImpactDojoArt Art = EChaosImpactDojoArt::Move;
	EChaosImpactDojoPointer Pointer = EChaosImpactDojoPointer::None;
};

struct FChaosImpactDojoLesson
{
	/** Kept in the save (do not rename once shipped). */
	FString Id;
	FString Name;
	/** One line for the list's preview. */
	FString Summary;
	TArray<FChaosImpactDojoSlide> Slides;
	/** The task line shown during play, and the keys shown in it (this player's own). */
	FString Task;
	TArray<EChaosImpactAction> TaskKeys;
	/** The unit counted ("回", "個", "体", "秒"). */
	FString Unit = TEXT("回");
	EChaosImpactDojoGoal Goal = EChaosImpactDojoGoal::Throw;
	int32 Count = 3;
	/** Balls handed to the player (kept topped up while bRefill). */
	EChaosImpactBallType Ball = EChaosImpactBallType::Normal;
	int32 Give = 0;
	bool bRefill = true;
	/** A second kind handed with it (the swap lesson). */
	EChaosImpactBallType SecondBall = EChaosImpactBallType::Normal;
	EChaosImpactDojoCPU CPU = EChaosImpactDojoCPU::None;
	int32 CPUCount = 0;
	EChaosImpactBallType CPUBall = EChaosImpactBallType::Normal;
	/** How far up the screen the CPUs stand (and how far the targets / rings are spread). */
	float Distance = 900.0f;
	int32 Targets = 0;
	bool bMovingTargets = false;
	/** HitCPU: the kind that must do it (Normal: any). */
	EChaosImpactBallType HitKind = EChaosImpactBallType::Normal;
	EChaosImpactDojoPointer Pointer = EChaosImpactDojoPointer::None;
	/** Held in the duel's own arena (a 1-on-1 lesson), not on the VS stage. */
	bool bDuelArena = false;
	/** The stage's ball pads put out balls as in a real match. */
	bool bStagePads = false;
	/** The player and the CPUs start on the stage's own match spawn points (a match as it is played there). */
	bool bMatchSpawns = false;
	/** A real local match (its HUD, its CPUs and teams, its stage as it is): the slides here, then the match itself,
	 *  the task counted from the match's own numbers (knockouts, hits, duel rounds won). */
	bool bRealMatch = false;
	/** bRealMatch: 0 every one for themselves, 2 two teams; CPUs in it; a duel. */
	int32 MatchTeams = 0;
	int32 MatchCPUs = 1;
	bool bMatchDuel = false;
	/** A winding road walled both sides, the CPU at its far end: a drive ball steered down it (a wall touched: again). */
	bool bDriveCourse = false;
	/** Challenges: done within this many seconds, else from the start again (0: no limit). */
	float TimeLimit = 0.0f;
	/** Challenges: a hit on the player starts it over. */
	bool bNoDamage = false;
	/** Fighter CPUs' strength (ChaosImpactMatch::CPULevel...). */
	int32 CPULevel = 0;
	/** Thrower CPUs: seconds between throws. */
	float ThrowEvery = 2.1f;
};

struct FChaosImpactDojoCategory
{
	FString Name;
	FString English;
	FLinearColor Color = FLinearColor::White;
	TArray<FChaosImpactDojoLesson> Lessons;
};

namespace ChaosImpactDojo
{
	/** The level a lesson is held on (the VS stage, or the duel's arena). */
	FString GetLessonLevel(const FChaosImpactDojoLesson& Lesson);
	const TArray<FChaosImpactDojoCategory>& GetCategories();
	const FChaosImpactDojoLesson* GetLesson(int32 Category, int32 Lesson);

	/** Progress (kept in the user settings file). */
	bool IsCleared(const FString& LessonId);
	void MarkCleared(const FString& LessonId);
	int32 CountCleared(int32 Category);
	/** Tests: forget every lesson cleared. */
	void ResetProgress();
}
