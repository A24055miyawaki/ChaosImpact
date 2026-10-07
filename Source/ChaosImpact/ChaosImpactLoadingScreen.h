#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Widgets/SLeafWidget.h"
#include "ChaosImpactLoadingScreen.generated.h"

class IInputProcessor;
class UGameViewportClient;
class UWorld;

/** Why the game is loading (what the screen says). */
enum class EChaosImpactLoadingKind : uint8
{
	Startup,
	Training,
	RoomCreate,
	RoomJoin,
	RoomLeave,
	Title
};

/** The little game a loading screen plays (a different one each time). */
enum class EChaosImpactLoadingGame : uint8
{
	/** Balls bounce round the screen: pop them. */
	Pop,
	/** Balls roll in: jump them (the high ones pass over if you stay down). */
	Dodge,
	/** Keep one ball up: a button as it comes down onto the bar. */
	Juggle,
	/** Throw at targets drifting by. */
	Target,
	/** The balls one by one, with what each does. */
	Gallery,
	Count
};

/**
 * What a loading screen shows: its little game, a tip, what is loading and how far it has got. Shared by the screen's
 * copies (the one over the game and the one the movie player draws while a level loads), so the game carries on through
 * the whole load without a jump; left alone for a moment, the game plays itself.
 */
class FChaosImpactLoadingState
{
public:
	struct FBall
	{
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Velocity = FVector2D::ZeroVector;
		float Radius = 30.0f;
		int32 Type = 0;
		/** Dodge: flies at head height (stay down). */
		bool bHigh = false;
		/** Dodge: already counted as dodged. */
		bool bCounted = false;
	};
	struct FBurst
	{
		FVector2D Position = FVector2D::ZeroVector;
		float Age = 0.0f;
		FLinearColor Color = FLinearColor::White;
		FString Label;
	};

	FChaosImpactLoadingState(EChaosImpactLoadingKind InKind, EChaosImpactLoadingGame InGame, int32 Seed);
	/** Moves everything on to Now (real seconds). Call with Lock held. */
	void Step(double Now);
	/** Any button. */
	void Press(double Now);
	/** A click at a point in the screen's 1600 x 900 design space. */
	void ClickAt(const FVector2D& DesignPoint, double Now);
	/** Nobody has pressed anything for a while: the game plays itself. */
	bool IsAuto(double Now) const;
	/** Target: where the launcher points now (a unit direction). */
	FVector2D GetAim() const;

	FCriticalSection Lock;
	/** Pop: the bouncing balls. Dodge: the balls rolling in. Juggle: the one ball. Target: the targets. */
	TArray<FBall> Balls;
	/** Target: the balls thrown. */
	TArray<FBall> Shots;
	TArray<FBurst> Bursts;
	EChaosImpactLoadingKind Kind = EChaosImpactLoadingKind::Training;
	EChaosImpactLoadingGame Game = EChaosImpactLoadingGame::Pop;
	FString Status;
	int32 Tip = 0;
	/** 0-1, or negative when it cannot be told. */
	float Progress = -1.0f;
	/** Popped, dodged in a row, kept up in a row, points, or balls seen. */
	int32 Score = 0;
	/** This game's best this session. */
	int32 Best = 0;
	double StartedAt = 0.0;
	double LastStepAt = 0.0;
	double LastPressAt = -100.0;
	/** When it began to fade out (negative while it is up). */
	double FadeStartedAt = -1.0;
	bool bCancelable = false;
	FRandomStream Random;

	// Dodge
	float RunnerHeight = 0.0f;
	float RunnerSpeed = 0.0f;
	float HitFlash = 0.0f;
	float SpawnIn = 0.8f;
	float Scroll = 0.0f;
	// Juggle
	float RespawnIn = 0.0f;
	float Cooldown = 0.0f;
	// Target
	float AimClock = 0.0f;
	float ThrowCooldown = 0.0f;
	// Gallery
	int32 Page = 0;
	double PageAt = 0.0;

private:
	void AddBall(bool bFromTop);
	void AddDodgeBall();
	void AddTarget(bool bAnywhere);
	void ResetJuggle();
	void Burst(const FVector2D& Where, const FLinearColor& Color, const FString& Label = FString());
	void AddScore(int32 Points);
	void StepPop(float Delta);
	void StepDodge(float Delta, bool bAuto);
	void StepJuggle(float Delta, bool bAuto);
	void StepTarget(float Delta, bool bAuto);
	void PopAt(const FVector2D& DesignPoint);
	void PopOne();
	void Jump();
	bool Kick(bool bAuto);
	void Throw(FVector2D Direction);
	void NextPage(double Now);
};

/** The loading screen itself, drawn in a 1600 x 900 design space fitted to the screen. */
class SChaosImpactLoadingScreen : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SChaosImpactLoadingScreen) : _Interactive(true) {}
		SLATE_ARGUMENT(TSharedPtr<FChaosImpactLoadingState>, State)
		/** Over the game (clicks play); the movie player's copy only plays itself. */
		SLATE_ARGUMENT(bool, Interactive)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(1600.0f, 900.0f); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return false; }

private:
	TSharedPtr<FChaosImpactLoadingState> State;
	bool bInteractive = true;
};

/**
 * Loading screens, outside the solo mode, only for as long as something is really loading:
 *  - a level (training from the title, an online room being made, joined or left, back to the title): it comes up as
 *    the travel starts, the movie player keeps it on screen while the level loads, and it stays until the new level
 *    says it is ready (effects warmed up), then fades;
 *  - the game's start (the ball effects loading in the background at the title), with how far it has got.
 * Each time it plays one of a few little games (never the same one twice running); over the game it can be played.
 */
UCLASS()
class UChaosImpactLoadingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UChaosImpactLoadingSubsystem* Get(const UObject* WorldContext);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** A level is about to be opened (or travelled to): up now, through the load, until the new level is ready. */
	void BeginTravel(EChaosImpactLoadingKind Kind);
	/** Up over the game while something loads here (Progress: 0-1, or null). OnCancel: Escape / B gives up on it. */
	void ShowWaiting(EChaosImpactLoadingKind Kind, TFunction<float()> Progress, TFunction<void()> OnCancel = nullptr);
	/** The level (or the start) is ready: it fades away. */
	void NotifyWorldReady();
	bool IsShowing() const { return Overlay.IsValid(); }
	/** For the input processor: a button while it is up. True when it took the press. */
	bool HandleKey(const FKey& Key, int32 InputDeviceId);
	/** The game the next loading screen plays (tests; otherwise it is chosen at random). */
	void SetNextGame(EChaosImpactLoadingGame Game) { ForcedGame = Game; }
	/** The screen up now: its game and score. */
	EChaosImpactLoadingGame GetGame() const { return State.IsValid() ? State->Game : EChaosImpactLoadingGame::Count; }
	int32 GetScore() const { return State.IsValid() && Overlay.IsValid() ? State->Score : 0; }

private:
	void Show(EChaosImpactLoadingKind Kind);
	void Remove();
	bool Tick(float DeltaSeconds);
	void HandlePostLoadMap(UWorld* World);
	EChaosImpactLoadingGame ChooseGame();

	TSharedPtr<FChaosImpactLoadingState> State;
	TSharedPtr<SChaosImpactLoadingScreen> Overlay;
	TSharedPtr<IInputProcessor> Input;
	TWeakObjectPtr<UGameViewportClient> OverlayViewport;
	TFunction<float()> ProgressSource;
	TFunction<void()> CancelAction;
	bool bTravelling = false;
	bool bWorldLoaded = false;
	/** Escape / B gave up: done on the next tick (not inside the input processor that saw it). */
	bool bCancelRequested = false;
	double ShownAt = 0.0;
	double WorldLoadedAt = 0.0;
	/** Loading has really finished: it goes once it has been up for MinimumShowSeconds (looking as if still loading). */
	bool bReady = false;
	/** Up at least this long, so its little game can be played even when the load is quick. */
	static constexpr double MinimumShowSeconds = 4.0;
	EChaosImpactLoadingGame LastGame = EChaosImpactLoadingGame::Count;
	EChaosImpactLoadingGame ForcedGame = EChaosImpactLoadingGame::Count;
	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle PostLoadHandle;
};
