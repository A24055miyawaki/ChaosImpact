#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactDojoLessons.h"
#include "ChaosImpactPaint.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactDojo.generated.h"

class AChaosImpactBall;
class AChaosImpactCharacter;
class AChaosImpactCPUController;
class AChaosImpactPlayerController;
class AChaosImpactSuddenDeathZone;
class AChaosImpactTrainingTarget;
class FSlateWindowElementList;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
struct FGeometry;

/** Where a lesson is: its slides, waiting for the start, being played, or cleared. */
enum class EChaosImpactDojoPhase : uint8
{
	/** No lesson: the lesson list is up. */
	Idle,
	/** The lesson's slides, one after another (the player stands still). */
	Intro,
	/** The task shown, waiting for the start button. */
	Ready,
	Play,
	/** CLEAR!: shown a moment, then a choice: the next lesson, this one again, or the lesson list. */
	Clear
};

/**
 * 訓練場: runs one lesson at a time on a VS stage, offline, for the first local player. It sets the lesson out (the
 * player's place, CPUs that stand / walk / throw / charge / fight, targets, balls, glowing rings, a shrinking circle),
 * counts the task down from what the player and the CPUs really do, and keeps what has been cleared. Its HUD (the
 * slides, the task, the count, CLEAR) is drawn by the player's charge widget through PaintHud; the lesson list is the
 * menu's DojoSelect screen (UChaosImpactDojoMenu).
 */
UCLASS(NotPlaceable)
class AChaosImpactDojo : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactDojo();

	/** The dojo in this world (one at most; none outside 訓練場). */
	static AChaosImpactDojo* Find(const UWorld* World);
	static bool IsDojoWorld(const UWorld* World);
	/** CLEAR! stays this long before the next lesson. */
	static constexpr double ClearSeconds = 2.4;

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Starts a lesson from its slides (on its own level: another one is opened first when it is held elsewhere). */
	void StartLesson(int32 Category, int32 Lesson);
	/** This world is the level the lesson is held on. */
	bool IsLessonHere(const FChaosImpactDojoLesson& Lesson) const;
	/** Back to the lesson list (the lesson's things cleared away). */
	void ReturnToList();
	/** The same lesson again, from its start button. */
	void RetryLesson();

	EChaosImpactDojoPhase GetPhase() const { return Phase; }
	int32 GetCategory() const { return Category; }
	int32 GetLessonIndex() const { return LessonIndex; }
	const FChaosImpactDojoLesson* GetLesson() const;
	int32 GetSlide() const { return Slide; }
	int32 GetDone() const { return Done; }
	/** The lesson the list should show selected (the one just played, or the first not cleared). */
	void GetListFocus(int32& OutCategory, int32& OutLesson) const;

	/** Tests: do the lesson's task one step as if the player had. */
	void DebugCount(int32 Steps = 1);
	/** After CLEAR: 0 the next lesson, 1 this one again, 2 the lesson list. */
	void ChooseAfterClear(int32 Choice);
	/** The choice after CLEAR is up (its buttons shown, answering). */
	bool IsClearMenuUp() const;
	/** CLEAR's own moment before its choices come up. */
	static constexpr double ClearMenuDelay = 1.1;
	/** Tests: the slides passed and the task started. */
	void DebugBegin();
	/** Tests: the slides passed, waiting on the start button. */
	void DebugReady() { if (const FChaosImpactDojoLesson* L = GetLesson()) { Slide = L->Slides.Num(); Phase = EChaosImpactDojoPhase::Ready; PhaseAt = GetWorld()->GetTimeSeconds(); } }
	/** The lesson the list has selected (its clip plays in the list's preview). */
	void SetPreview(int32 InCategory, int32 InLesson) { PreviewCategory = InCategory; PreviewLesson = InLesson; }
	/** The slides' and the preview's clip player. */
	class UChaosImpactDojoVideo* GetVideo() const { return Video; }
	/** Filming the slides' clips (-CIDojoRecord=...): ChaosImpactDojoRecord.cpp. */
	static bool IsRecordingRun();
	bool IsRecording() const { return bRecording; }
	/** The clip a slide shows (Content/Movies/Dojo/<name>.mp4). */
	static FString GetClipName(EChaosImpactDojoArt Art, EChaosImpactBallType Ball);
	/** Tests: the ring to walk to next (the stage's middle when none). */
	FVector DebugNextRing() const { return Rings.IsValidIndex(RingAt) ? Rings[RingAt] : Center; }

	/** The slides, the task card, the count, the pointer and CLEAR, over the player's view. */
	void PaintHud(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer, const AChaosImpactPlayerController* Viewer,
		float Scale) const;
	/** A slide's little moving picture, in the box (X, Y, W, H) of Paint's space. */
	static void PaintArt(const ChaosImpactPaint::FPainter& Paint, EChaosImpactDojoArt Art, EChaosImpactBallType Ball, float X, float Y,
		float W, float H, float Time);

private:
	AChaosImpactPlayerController* GetPlayer() const;
	AChaosImpactCharacter* GetPlayerCharacter() const;
	/** The stage's middle, its size, and the ways up and right of the screen (flat). */
	void ReadStage();
	FVector OnFloor(const FVector& At) const;

	void SetUpLesson();
	void ClearLesson();
	void BeginPlayPhase();
	void Count(int32 Steps = 1);
	void Fail(const FString& Why);
	void OnCleared();

	void TickInput();
	void TickPlay(float DeltaSeconds);
	void TickCPUs(float DeltaSeconds);
	void TickPlayerBalls();
	void TickGoal(float DeltaSeconds);
	void FreezePlayer(bool bFrozen);
	void QuietStage();

	AChaosImpactCharacter* SpawnCPU(const FVector& At);
	void SpawnTarget(const FVector& At, bool bMoving, int32 Index);
	void SpawnPickup(const FVector& At, EChaosImpactBallType Type);
	void SpawnRing(const FVector& At);
	void PaintRings(float DeltaSeconds);

	/** A CPU's hit on the player or the player's on a CPU, as it lands (bound to every character's damage). */
	void OnCharacterDamaged(AChaosImpactCharacter* Victim, float Damage, AActor* Causer, APawn* Source);

	void PaintSlides(const ChaosImpactPaint::FPainter& Paint, float W, float H, float Time) const;
	void PaintTask(const ChaosImpactPaint::FPainter& Paint, const AChaosImpactPlayerController* Viewer, float W, float H, float Time) const;
	void PaintClear(const ChaosImpactPaint::FPainter& Paint, float W, float H, float Time) const;
	void PaintPointer(const ChaosImpactPaint::FPainter& Paint, EChaosImpactDojoPointer Pointer, float W, float H, float Scale, float Time) const;
	/** Where the task's things are (rings, targets, CPUs, balls, parts): a mark over each on screen, an arrow at the edge for each off it. */
	void PaintObjectives(const ChaosImpactPaint::FPainter& Paint, const AChaosImpactPlayerController* Viewer, float W, float H, float Time) const;

	EChaosImpactDojoPhase Phase = EChaosImpactDojoPhase::Idle;
	int32 Category = 0;
	int32 LessonIndex = 0;
	int32 Slide = 0;
	int32 Done = 0;
	double PhaseAt = 0.0;
	double SlideAt = 0.0;
	/** The last count's flash (あと N), and a miss's message. */
	double CountAt = -100.0;
	double FailAt = -100.0;
	FString FailText;
	bool bClearedBefore = false;

	// The stage.
	FVector Center = FVector::ZeroVector;
	float HalfExtent = 1500.0f;
	FVector Up = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
	FVector Start = FVector::ZeroVector;
	bool bStageRead = false;
	/** The whole stage (for lessons that range far) and the open patch found on it (the rest): middle and half size. */
	FVector WideCenter = FVector::ZeroVector;
	float WideHalf = 1500.0f;
	FVector OpenCenter = FVector::ZeroVector;
	float OpenHalf = 1100.0f;
	/** The lesson's place: the open patch, or the whole stage for one that ranges far. */
	void PlaceFor(const FChaosImpactDojoLesson& Lesson);
	/** The stage's own ball pads (set out by the dojo where a match has them). */
	TArray<TWeakObjectPtr<class AChaosImpactBallSpawner>> StagePads;
	/** The stage the lessons are held on (its match spawn points). */
	TWeakObjectPtr<class AChaosImpactStageBase> StageActor;
	/** The lesson this level was opened on has been started. */
	bool bAskedStarted = false;

	// What the lesson set out.
	struct FDojoCPU
	{
		TWeakObjectPtr<AChaosImpactCharacter> Pawn;
		TWeakObjectPtr<AChaosImpactCPUController> Brain;
		FVector Home = FVector::ZeroVector;
		float Phase = 0.0f;
		double NextActAt = 0.0;
		bool bCharging = false;
		double ChargeAt = 0.0;
		bool bWasEliminated = false;
		bool bWasFlag = false;
		float LastHealth = 0.0f;
	};
	TArray<FDojoCPU> CPUs;
	TArray<TWeakObjectPtr<AChaosImpactTrainingTarget>> Targets;
	TArray<bool> TargetDown;
	TArray<TWeakObjectPtr<AChaosImpactBall>> Pickups;
	/** The nova lesson: its three parts, set out on the floor (again once the nova is gone). */
	uint8 NovaPartsSeen = 0;
	bool bNovaHeld = false;
	void SpawnNovaParts();
	TArray<FVector> Rings;
	int32 RingAt = 0;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> RingMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
	TWeakObjectPtr<AChaosImpactSuddenDeathZone> Zone;
	float ZoneTime = 0.0f;
	float OutTime = 0.0f;

	// What the player did, seen frame to frame.
	bool bWasCharging = false;
	float MaxCharge = 0.0f;
	int32 LastBalls = 0;
	uint8 LastTypes = 0;
	double ChargeEndedAt = -100.0;
	int32 BallsAtChargeEnd = 0;
	float ChargeAtEnd = 0.0f;
	bool bChargeEndPending = false;
	bool bWasDashing = false;
	bool bWasFalling = false;
	bool bWasTackling = false;
	bool bWasInForm = false;
	bool bWasMapOpen = false;
	double EmptySince = -1.0;
	/** When the task (re)started, for a time limit; a hit on the player waiting to start it over (no-damage lessons). */
	double TaskStartAt = 0.0;
	bool bRestartPending = false;
	void RestartTask(const FString& Why);

	// Dodge: the CPU's balls in flight at the player (the closest each came, and whether the player was dashing then).
	struct FIncoming
	{
		TWeakObjectPtr<AChaosImpactBall> Ball;
		float Closest = 1.0e9f;
		bool bDashedPast = false;
		double ThrownAt = 0.0;
	};
	TArray<FIncoming> Incoming;
	double PlayerHitAt = -100.0;
	/** Beam: the beams that hit each CPU, to see one go through both. */
	TMap<TWeakObjectPtr<AActor>, int32> BeamHits;
	FDelegateHandle DamageHandle;
	UPROPERTY(Transient)
	TObjectPtr<class UChaosImpactDojoVideo> Video;
	int32 PreviewCategory = INDEX_NONE;
	int32 PreviewLesson = INDEX_NONE;
	void TickVideo();
	/** Eases the player's camera toward the lesson's goals (zero out of a lesson). */
	void UpdateCameraLead(float DeltaSeconds);
	/** The control this lesson needs that the player's control type does not have ("" when it has them all). */
	FString GetMissingControl() const;
	/** This world is a real match opened for a mock-match lesson. */
	bool IsMatchWorld() const;
	/** A mock-match lesson in its match: straight to its task (the match plays its own opening). */
	void BeginMatchLesson(int32 InCategory, int32 InLesson);
	/** The match's numbers to the task; CLEAR, or the match over, back to the list. */
	void TickMatchLesson();
	/** The rules a mock-match lesson's match is played by. */
	static struct FChaosImpactMatchRules MatchRulesFor(const FChaosImpactDojoLesson& Lesson);
	/** When the match ended without the task done (0: not). */
	double MatchOverAt = 0.0;
	/** Back from a mock match: the list on that lesson. */
	void LeaveMatch();
	/** The drive course: its road's middle line (from the player's end), and the walls laid along it. */
	TArray<FVector> CoursePath;
	TWeakObjectPtr<class AChaosImpactDojoCourse> Course;
	TWeakObjectPtr<AChaosImpactBall> CourseBall;
	float CourseBallDistance = 0.0f;
	float CourseBallAlong = 0.0f;
	static constexpr float CourseHalfWidth = 120.0f;
	static constexpr float CourseMouth = 230.0f;
	/** Lays the drive course in the open patch and stands the player at its start. */
	void BuildCourse();
	/** A drive ball down the course: touching a wall (or ended against one) starts the task over. */
	void TickCourse();
	/** The lesson after this one (on into the next group after a group's last); false after the very last. */
	bool FindNextLesson(int32& OutCategory, int32& OutLesson) const;
	/** The choice after CLEAR: moved with left / right (or the mouse), taken with the confirm key (or a click on it). */
	void TickClearMenu();
	int32 ClearChoice = 0;
	FVector2D LastMouse = FVector2D::ZeroVector;
	/** The HUD's size as last painted (the mouse, in viewport pixels, to its units). */
	mutable FVector2D LastHudSize = FVector2D(1920.0, 1080.0);
	/** Where each of the three choices is drawn (HUD units). */
	static FBox2D ClearButton(int32 Index, float W, float H);
	void PaintClearMenu(const ChaosImpactPaint::FPainter& P, const AChaosImpactPlayerController* Viewer, float W, float H, float Time) const;
	/** A lesson thrown from the spot (targets, the drive course): the player cannot walk or dash in it. */
	static bool IsRootedLesson(const FChaosImpactDojoLesson& Lesson);
	FVector CameraLead = FVector::ZeroVector;

	// Filming the clips.
	enum class ERecScript : uint8
	{
		Rings, PickUp, TapTargets, AimTargets, ChargeAtCPU, KnockOutCPU, LeadThrows, Dashes, DashThrough, Sidestep, StandHit, Jumps,
		TackleCPU, Swaps, Cancels, Drops, BallForm, Map, Wander, Snow, Nova, Drive, Anchor, BallAtCPU, PadFight, DriveCourse
	};
	struct FRecClip
	{
		FString Name;
		FString Lesson;
		float Seconds = 4.0f;
		ERecScript Script = ERecScript::BallAtCPU;
		EChaosImpactBallType Ball = EChaosImpactBallType::Normal;
	};
	static const TArray<FRecClip>& GetClips();
	void BeginRecording();
	void StartRecClip();
	void TickRecord(float DeltaSeconds);
	void RunRecScript(const FRecClip& Clip, float T);
	bool RecOnce(FName Key, bool bWhen);
	bool bRecording = false;
	TArray<int32> RecQueue;
	int32 RecAt = 0;
	float RecTime = 0.0f;
	int32 RecFrame = 0;
	TSet<FName> RecFired;
	bool RecHolding = false;
	bool RecSide = false;
	float RecHoldUntil = -1.0f;
	/** The filming camera (the game's angle, framing the player and what the clip is about) and where it looks. */
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> RecCamera;
	FVector RecFocus = FVector::ZeroVector;
	bool bRecFocusSet = false;
	/** Where the script aims now (the cursor is put there on screen, as a player's would be). */
	FVector RecAimAt = FVector::ZeroVector;
	bool bRecAimSet = false;
	void UpdateRecCamera(float DeltaSeconds);
	void UpdateRecCursor();
};
