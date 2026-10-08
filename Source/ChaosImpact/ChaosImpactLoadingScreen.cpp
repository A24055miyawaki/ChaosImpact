#include "ChaosImpactLoadingScreen.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	namespace LoadingPaint = ChaosImpactPaint;
	using FLoadingBall = FChaosImpactLoadingState::FBall;

	struct FLoadingTip
	{
		EChaosImpactBallType Ball;
		const TCHAR* Text;
	};

	const FLoadingTip LoadingTips[] = {
		{EChaosImpactBallType::Normal, TEXT("ふつうのボールは、前にいる相手へ少し曲がっていく。")},
		{EChaosImpactBallType::Fire, TEXT("ファイアボールは飛んだあとに火を残す。燃えている床に乗るとダメージ！")},
		{EChaosImpactBallType::Ice, TEXT("アイスボールは床を凍らせて滑りやすくし、近くの相手を氷づけにする。")},
		{EChaosImpactBallType::Thunder, TEXT("サンダーボールは壁で跳ね返るたびに速くなる。")},
		{EChaosImpactBallType::Black, TEXT("ブラックボールは着地点にブラックホールを作って相手を吸い寄せる。中心は燃える！")},
		{EChaosImpactBallType::Wind, TEXT("ウィンドボールは投げると竜巻になる。巻き込んだボールは自分の球として飛んでいく。")},
		{EChaosImpactBallType::Smoke, TEXT("スモークボールの煙に巻かれた相手は、しばらく周りが見えにくい。")},
		{EChaosImpactBallType::Beam, TEXT("ビームは壁も人も貫いて、一直線上の相手全員に当たる。")},
		{EChaosImpactBallType::Snow, TEXT("スノーボールは持って歩くほど大きくなる。大きいと頭の上から投げ落とす！")},
		{EChaosImpactBallType::Nova, TEXT("ノヴァはためるほど大きくなる。光る着地点をよく見て逃げよう。")},
		{EChaosImpactBallType::Simae, TEXT("シマエナガボールからは、相手を追いかけるシマエナガが飛び出す。")},
		{EChaosImpactBallType::Drive, TEXT("ドライブは投げたあと自分で操れる。逆を押すとその場で折り返す！")},
		{EChaosImpactBallType::Normal, TEXT("ダッシュ中は無敵。飛んでくるボールもすり抜けられる。")},
		{EChaosImpactBallType::Normal, TEXT("ボールは2つまで持てる。入れ替えで投げる順番を変えられる。")},
		{EChaosImpactBallType::Normal, TEXT("長押しでためると、速くて強い球になる。")},
		{EChaosImpactBallType::Normal, TEXT("操作は設定画面で、ニックネームごと・デバイスごとに変えられる。")},
	};

	const EChaosImpactBallType LoadingBallTypes[] = {EChaosImpactBallType::Normal, EChaosImpactBallType::Fire,
		EChaosImpactBallType::Ice, EChaosImpactBallType::Thunder, EChaosImpactBallType::Black, EChaosImpactBallType::Wind,
		EChaosImpactBallType::Smoke, EChaosImpactBallType::Beam, EChaosImpactBallType::Snow, EChaosImpactBallType::Nova,
		EChaosImpactBallType::Simae, EChaosImpactBallType::Drive};
	constexpr int32 LoadingBallTypeCount = UE_ARRAY_COUNT(LoadingBallTypes);

	EChaosImpactBallType LoadingBallType(const int32 Type)
	{
		return LoadingBallTypes[FMath::Clamp(Type, 0, LoadingBallTypeCount - 1)];
	}

	FLinearColor LoadingBallColor(const int32 Type)
	{
		const EChaosImpactBallType Ball = LoadingBallType(Type);
		return Ball == EChaosImpactBallType::Normal ? FLinearColor(0.92f, 0.94f, 1.0f)
			: Ball == EChaosImpactBallType::Simae ? FLinearColor(0.97f, 0.97f, 0.97f) : ChaosImpactBallTypes::GetColor(Ball);
	}

	FString LoadingStatus(const EChaosImpactLoadingKind Kind)
	{
		switch (Kind)
		{
		case EChaosImpactLoadingKind::Startup: return TEXT("ゲームを準備しています");
		case EChaosImpactLoadingKind::Training: return TEXT("トレーニングを準備しています");
		case EChaosImpactLoadingKind::RoomCreate: return TEXT("部屋をつくっています");
		case EChaosImpactLoadingKind::RoomJoin: return TEXT("部屋に入っています");
		case EChaosImpactLoadingKind::RoomLeave: return TEXT("部屋から出ています");
		case EChaosImpactLoadingKind::Arcade: return TEXT("ミニゲーム");
		default: return TEXT("タイトルにもどります");
		}
	}

	const TCHAR* LoadingGameName(const EChaosImpactLoadingGame Game)
	{
		switch (Game)
		{
		case EChaosImpactLoadingGame::Dodge: return TEXT("ジャンプでよけろ！");
		case EChaosImpactLoadingGame::Juggle: return TEXT("リフティング");
		case EChaosImpactLoadingGame::Target: return TEXT("まとあて");
		case EChaosImpactLoadingGame::Gallery: return TEXT("ボールずかん");
		case EChaosImpactLoadingGame::Breakout: return TEXT("ブロックくずし");
		case EChaosImpactLoadingGame::Flappy: return TEXT("シマエナガとべ！");
		case EChaosImpactLoadingGame::Avoid: return TEXT("よけまくれ！");
		default: return Game >= EChaosImpactLoadingGame::Catch ? FChaosImpactLoadingState::ExtraGameName(Game) : TEXT("ボールわり");
		}
	}

	/** What a ball does (its tip), for the gallery. */
	const TCHAR* LoadingBallText(const EChaosImpactBallType Ball)
	{
		for (const FLoadingTip& Tip : LoadingTips)
		{
			if (Tip.Ball == Ball)
			{
				return Tip.Text;
			}
		}
		return TEXT("");
	}

	/** Japanese has no spaces to break at: a new line every so many characters, just after punctuation when it is near. */
	TArray<FString> LoadingWrap(const FString& Text, const int32 MaxChars)
	{
		TArray<FString> Lines;
		FString Line;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			Line.AppendChar(Text[Index]);
			const bool bPunctuation = Text[Index] == TEXT('、') || Text[Index] == TEXT('。') || Text[Index] == TEXT('！');
			if (Line.Len() >= MaxChars || (bPunctuation && Line.Len() >= MaxChars - 6 && Index + 1 < Text.Len()))
			{
				Lines.Add(Line);
				Line.Reset();
			}
		}
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
		return Lines;
	}

	bool LoadingCircleHitsBox(const FVector2D& Center, const float Radius, const float MinX, const float MinY, const float MaxX, const float MaxY)
	{
		const FVector2D Nearest(FMath::Clamp(Center.X, static_cast<double>(MinX), static_cast<double>(MaxX)),
			FMath::Clamp(Center.Y, static_cast<double>(MinY), static_cast<double>(MaxY)));
		return FVector2D::DistSquared(Nearest, Center) <= FMath::Square(Radius);
	}

	constexpr float LoadingFadeSeconds = 0.3f;
	/** Untouched this long, the game plays itself. */
	constexpr float LoadingAutoAfter = 2.5f;
	constexpr float LoadingBurstSeconds = 0.6f;
	constexpr float LoadingLabelSeconds = 0.9f;

	// Pop
	constexpr int32 LoadingPopCount = 11;
	constexpr float LoadingGravity = 1500.0f;
	// Dodge
	constexpr float LoadingDodgeFloor = 560.0f;
	constexpr float LoadingDodgeRunnerX = 330.0f;
	constexpr float LoadingDodgeRunnerTall = 110.0f;
	constexpr float LoadingDodgeRunnerHalfWidth = 20.0f;
	constexpr float LoadingDodgeGravity = 3200.0f;
	constexpr float LoadingDodgeJump = 1150.0f;
	constexpr float LoadingDodgeBallRadius = 26.0f;
	/** A high ball's centre above the floor: over a runner standing, into one jumping. */
	constexpr float LoadingDodgeHighAbove = 150.0f;
	// Juggle
	constexpr float LoadingJuggleBar = 545.0f;
	constexpr float LoadingJuggleLeft = 880.0f;
	constexpr float LoadingJuggleRight = 1500.0f;
	constexpr float LoadingJuggleRadius = 40.0f;
	/** How far above the bar the ball's bottom can be and still be kicked. */
	constexpr float LoadingJuggleWindow = 80.0f;
	// Target
	const FVector2D LoadingLauncher(170.0f, 610.0f);
	constexpr float LoadingShotSpeed = 1700.0f;
	constexpr float LoadingShotDrop = 300.0f;
	constexpr int32 LoadingTargetCount = 4;
	// Gallery
	constexpr float LoadingPageSeconds = 4.0f;
	// Breakout
	constexpr float LoadingBreakLeft = 380.0f;
	constexpr float LoadingBreakRight = 1220.0f;
	constexpr float LoadingBreakTop = 200.0f;
	constexpr float LoadingBreakPaddleY = 590.0f;
	constexpr float LoadingBreakPaddleHalf = 75.0f;
	constexpr float LoadingBreakMissY = 630.0f;
	constexpr int32 LoadingBreakColumns = 10;
	constexpr int32 LoadingBreakRows = 5;
	constexpr float LoadingBreakBrickWidth = (LoadingBreakRight - LoadingBreakLeft) / LoadingBreakColumns;
	constexpr float LoadingBreakBrickHeight = 22.0f;
	constexpr float LoadingBreakBrickStep = 30.0f;
	constexpr float LoadingBreakBallRadius = 11.0f;
	// Flappy
	constexpr float LoadingFlapLeft = 300.0f;
	constexpr float LoadingFlapRight = 1300.0f;
	constexpr float LoadingFlapTop = 200.0f;
	constexpr float LoadingFlapGround = 620.0f;
	constexpr float LoadingFlapBirdX = 560.0f;
	constexpr float LoadingFlapBirdRadius = 24.0f;
	constexpr float LoadingFlapGravity = 1700.0f;
	constexpr float LoadingFlapImpulse = -560.0f;
	constexpr float LoadingFlapPillarWidth = 80.0f;
	constexpr float LoadingFlapPillarEvery = 1.55f;
	constexpr float LoadingFlapGapHalf = 110.0f;
	// Avoid
	constexpr float LoadingAvoidLeft = 380.0f;
	constexpr float LoadingAvoidRight = 1220.0f;
	constexpr float LoadingAvoidTop = 200.0f;
	constexpr float LoadingAvoidGround = 610.0f;
	constexpr float LoadingAvoidHalf = 22.0f;
	constexpr float LoadingAvoidTall = 70.0f;

	/** Left or right on a key (keys, the d-pad); 0 for any other. The stick comes as an analog value instead. */
	int32 LoadingSteerDirection(const FKey& Key)
	{
		if (Key == EKeys::Left || Key == EKeys::A || Key == EKeys::Gamepad_DPad_Left)
		{
			return -1;
		}
		if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right)
		{
			return 1;
		}
		return 0;
	}

	/** -1 up, 1 down (only for the games that move up and down too). */
	int32 LoadingVerticalDirection(const FKey& Key)
	{
		if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up)
		{
			return -1;
		}
		if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down)
		{
			return 1;
		}
		return 0;
	}

	/** Each game's best this session. */
	int32 LoadingBest[static_cast<int32>(EChaosImpactLoadingGame::Count)] = {};

	/** Sees every button while the loading screen is up (over the menus): plays, or gives up with Escape / B. */
	class FLoadingInputProcessor : public IInputProcessor
	{
	public:
		explicit FLoadingInputProcessor(UChaosImpactLoadingSubsystem* InOwner) : Owner(InOwner) {}
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UChaosImpactLoadingSubsystem* Loading = Owner.Get();
			if (!Loading) { return false; }
			// Unattended runs use explicit test commands, never the user's live devices.
			if (FApp::IsUnattended()) { return Loading->IsShowing(); }
			// Held keys must not leak into the menu beneath the arcade, or repeatedly trigger timing games.
			if (InKeyEvent.IsRepeat()) { return Loading->IsShowing() && InKeyEvent.GetKey() != EKeys::F11; }
			return Loading->HandleKey(InKeyEvent.GetKey(), InKeyEvent.GetInputDeviceId().GetId());
		}
		virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UChaosImpactLoadingSubsystem* Loading = Owner.Get();
			if (FApp::IsUnattended()) { return Loading && Loading->IsShowing(); }
			return Loading && Loading->HandleKeyUp(InKeyEvent.GetKey());
		}
		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			// The sticks steer the paddle, and do not move the menus underneath meanwhile.
			UChaosImpactLoadingSubsystem* Loading = Owner.Get();
			if (FApp::IsUnattended()) { return Loading && Loading->IsShowing(); }
			return Loading && Loading->HandleAnalog(InAnalogInputEvent.GetKey(), InAnalogInputEvent.GetAnalogValue());
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("ChaosImpactLoading"); }

	private:
		TWeakObjectPtr<UChaosImpactLoadingSubsystem> Owner;
	};

	/** A ball as the loading screens draw it: shadow, colour, shine (and a little bird's face on a Simae ball). */
	void LoadingDrawBall(const LoadingPaint::FPainter& Back, const LoadingPaint::FPainter& Front, const FVector2D& At,
		const float Radius, const int32 Type, const float Spin = -1.0f)
	{
		Back.Disc(At + FVector2D(Radius * 0.12f, Radius * 0.18f), Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		Back.Disc(At, Radius, LoadingBallColor(Type));
		if (Spin >= 0.0f)
		{
			// A mark going round, so it is seen to roll.
			Front.Disc(At + FVector2D(FMath::Cos(Spin), FMath::Sin(Spin)) * Radius * 0.55f, Radius * 0.17f, FLinearColor(0.0f, 0.0f, 0.0f, 0.22f));
		}
		Front.Disc(At + FVector2D(-Radius * 0.32f, -Radius * 0.34f), Radius * 0.32f, FLinearColor(1.0f, 1.0f, 1.0f, 0.45f));
		if (LoadingBallType(Type) == EChaosImpactBallType::Simae)
		{
			Front.Disc(At + FVector2D(-Radius * 0.28f, -Radius * 0.05f), Radius * 0.1f, LoadingPaint::Ink);
			Front.Disc(At + FVector2D(Radius * 0.28f, -Radius * 0.05f), Radius * 0.1f, LoadingPaint::Ink);
			Front.Disc(At + FVector2D(0.0f, Radius * 0.12f), Radius * 0.07f, LoadingPaint::Ink);
		}
	}

	/** The colours of the slanted sides behind each game. */
	void LoadingSideColors(const FChaosImpactLoadingState& State, FLinearColor& OutLeft, FLinearColor& OutRight)
	{
		switch (State.Game)
		{
		case EChaosImpactLoadingGame::Dodge: OutLeft = LoadingPaint::Gold; OutRight = LoadingPaint::Ice; break;
		case EChaosImpactLoadingGame::Juggle: OutLeft = LoadingPaint::Violet; OutRight = LoadingPaint::Gold; break;
		case EChaosImpactLoadingGame::Target: OutLeft = LoadingPaint::Fire; OutRight = LoadingPaint::Violet; break;
		case EChaosImpactLoadingGame::Gallery:
			OutLeft = LoadingBallColor(State.Page);
			OutRight = LoadingPaint::Ice;
			break;
		case EChaosImpactLoadingGame::Breakout: OutLeft = LoadingPaint::Gold; OutRight = LoadingPaint::Violet; break;
		case EChaosImpactLoadingGame::Flappy: OutLeft = LoadingPaint::Ice; OutRight = LoadingPaint::Paper; break;
		case EChaosImpactLoadingGame::Avoid: OutLeft = LoadingPaint::Fire; OutRight = LoadingPaint::Gold; break;
		default:
			OutLeft = LoadingPaint::PlayerAccents[static_cast<int32>(State.Game) % 4];
			OutRight = LoadingPaint::PlayerAccents[(static_cast<int32>(State.Game) + 1) % 4];
			break;
		}
	}
}

// =====================================================================================================================
// State
// =====================================================================================================================

FChaosImpactLoadingState::FChaosImpactLoadingState(const EChaosImpactLoadingKind InKind, const EChaosImpactLoadingGame InGame, const int32 Seed)
	: Kind(InKind)
	, Game(InGame)
	, Random(Seed)
{
	Status = LoadingStatus(InKind);
	Tip = Random.RandRange(0, UE_ARRAY_COUNT(LoadingTips) - 1);
	StartedAt = FPlatformTime::Seconds();
	LastStepAt = StartedAt;
	PageAt = StartedAt;
	StartGame(InGame);
}

bool FChaosImpactLoadingState::IsAuto(const double Now) const
{
	// The arcade is for playing: it never plays itself.
	// A direction held down (or the stick held over) is playing too.
	const bool bHeld = SteerLeft || SteerRight || SteerUp || SteerDown || StickX != 0.0f || StickY != 0.0f;
	return !bArcade && !bHeld && Now - LastPressAt > LoadingAutoAfter;
}

FVector2D FChaosImpactLoadingState::GetAim() const
{
	// Sweeps from nearly straight up to low across, and back.
	const float Swing = 0.5f + 0.5f * FMath::Sin(AimClock * 2.4f);
	const float Angle = FMath::DegreesToRadians(FMath::Lerp(-78.0f, -14.0f, Swing));
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
}

void FChaosImpactLoadingState::AddBall(const bool bFromTop)
{
	FLoadingBall Ball;
	Ball.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
	Ball.Radius = Random.FRandRange(22.0f, 44.0f);
	Ball.Position = FVector2D(Random.FRandRange(80.0f, 1520.0f), bFromTop ? -Ball.Radius - Random.FRandRange(0.0f, 200.0f)
		: Random.FRandRange(80.0f, 700.0f));
	Ball.Velocity = FVector2D(Random.FRandRange(-260.0f, 260.0f), Random.FRandRange(-200.0f, 100.0f));
	Balls.Add(Ball);
}

void FChaosImpactLoadingState::AddDodgeBall()
{
	FLoadingBall Ball;
	Ball.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
	Ball.Radius = LoadingDodgeBallRadius;
	// High ones once a few have been jumped, so standing still is sometimes right.
	Ball.bHigh = Score >= 3 && Random.FRand() < 0.25f;
	Ball.Position = FVector2D(1600.0f + Ball.Radius + 20.0f,
		LoadingDodgeFloor - (Ball.bHigh ? LoadingDodgeHighAbove : Ball.Radius));
	Balls.Add(Ball);
}

void FChaosImpactLoadingState::AddTarget(const bool bAnywhere)
{
	FLoadingBall Target;
	Target.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
	Target.Radius = Random.FRandRange(30.0f, 56.0f);
	const float Direction = Random.FRand() < 0.5f ? -1.0f : 1.0f;
	Target.Velocity = FVector2D(Direction * Random.FRandRange(150.0f, 320.0f), 0.0f);
	Target.Position = FVector2D(bAnywhere ? Random.FRandRange(420.0f, 1500.0f) : (Direction > 0.0f ? -70.0f : 1670.0f),
		Random.FRandRange(240.0f, 500.0f));
	Balls.Add(Target);
}

void FChaosImpactLoadingState::ResetJuggle()
{
	Balls.Reset();
	FLoadingBall Ball;
	Ball.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
	Ball.Radius = LoadingJuggleRadius;
	Ball.Position = FVector2D(Random.FRandRange(1000.0f, 1380.0f), -Ball.Radius - 20.0f);
	Ball.Velocity = FVector2D(Random.FRandRange(-120.0f, 120.0f), 0.0f);
	Balls.Add(Ball);
}

void FChaosImpactLoadingState::QueueSound(const EChaosImpactSfx Sound)
{
	if ((bArcade || !IsAuto(LastStepAt)) && PendingSounds.Num() < 8)
	{
		PendingSounds.Add(Sound);
	}
}

void FChaosImpactLoadingState::Burst(const FVector2D& Where, const FLinearColor& Color, const FString& Label)
{
	// The big moments ring out.
	if (Label.Contains(TEXT("PERFECT")) || Label.Contains(TEXT("KO")) || Label.Contains(TEXT("クリア"))
		|| Label.Contains(TEXT("やっつけた")) || Label.Contains(TEXT("CRITICAL")))
	{
		QueueSound(EChaosImpactSfx::MiniBig);
	}
	FBurst New;
	New.Position = Where;
	New.Color = Color;
	New.Label = Label;
	Bursts.Add(New);
}

void FChaosImpactLoadingState::AddScore(const int32 Points)
{
	QueueSound(EChaosImpactSfx::MiniScore);
	Score += Points;
	Best = FMath::Max(Best, Score);
	LoadingBest[static_cast<int32>(Game)] = Best;
}

void FChaosImpactLoadingState::Step(const double Now)
{
	const float Delta = FMath::Clamp(static_cast<float>(Now - LastStepAt), 0.0f, 0.05f);
	LastStepAt = Now;
	const bool bAuto = IsAuto(Now);
	switch (Game)
	{
	case EChaosImpactLoadingGame::Pop: StepPop(Delta); break;
	case EChaosImpactLoadingGame::Dodge: StepDodge(Delta, bAuto); break;
	case EChaosImpactLoadingGame::Juggle: StepJuggle(Delta, bAuto); break;
	case EChaosImpactLoadingGame::Target: StepTarget(Delta, bAuto); break;
	case EChaosImpactLoadingGame::Gallery:
		if (Now - PageAt > LoadingPageSeconds)
		{
			NextPage(Now);
		}
		break;
	case EChaosImpactLoadingGame::Breakout: StepBreakout(Delta, bAuto); break;
	case EChaosImpactLoadingGame::Flappy: StepFlappy(Delta, bAuto); break;
	case EChaosImpactLoadingGame::Avoid: StepAvoid(Delta, bAuto); break;
	default: StepExtraGame(Delta, bAuto); break;
	}
	for (FBurst& Each : Bursts)
	{
		Each.Age += Delta;
	}
	Bursts.RemoveAll([](const FBurst& Each) { return Each.Age > (Each.Label.IsEmpty() ? LoadingBurstSeconds : LoadingLabelSeconds); });
	// Hit (or missed), whichever game: the flash has just begun.
	if (HitFlash > SoundHitFlash + 0.05f)
	{
		QueueSound(EChaosImpactSfx::MiniMiss);
	}
	SoundHitFlash = HitFlash;
}

void FChaosImpactLoadingState::StepPop(const float Delta)
{
	for (FLoadingBall& Ball : Balls)
	{
		Ball.Velocity.Y += LoadingGravity * Delta;
		Ball.Position += Ball.Velocity * Delta;
		const float Floor = 900.0f - Ball.Radius;
		if (Ball.Position.Y > Floor)
		{
			// Lively bounces that never die away.
			Ball.Position.Y = Floor;
			Ball.Velocity.Y = -FMath::Max(FMath::Abs(Ball.Velocity.Y) * 0.8f, 780.0f + Ball.Radius * 6.0f);
		}
		if (Ball.Position.X < Ball.Radius || Ball.Position.X > 1600.0f - Ball.Radius)
		{
			Ball.Position.X = FMath::Clamp(Ball.Position.X, Ball.Radius, 1600.0f - Ball.Radius);
			Ball.Velocity.X = -Ball.Velocity.X;
		}
	}
	// Popped balls come back from the top.
	while (Balls.Num() < LoadingPopCount)
	{
		AddBall(true);
	}
}

void FChaosImpactLoadingState::StepDodge(const float Delta, const bool bAuto)
{
	const float Speed = 620.0f + FMath::Min(Score * 18.0f, 420.0f);
	Scroll += Speed * Delta;
	if (RunnerHeight > 0.0f || RunnerSpeed > 0.0f)
	{
		RunnerSpeed -= LoadingDodgeGravity * Delta;
		RunnerHeight += RunnerSpeed * Delta;
		if (RunnerHeight <= 0.0f)
		{
			RunnerHeight = 0.0f;
			RunnerSpeed = 0.0f;
		}
	}
	HitFlash = FMath::Max(0.0f, HitFlash - Delta);
	SpawnIn -= Delta;
	if (SpawnIn <= 0.0f)
	{
		AddDodgeBall();
		SpawnIn = Random.FRandRange(0.85f, 1.5f) * FMath::Max(0.6f, 1.0f - Score * 0.015f);
	}
	const float Bottom = LoadingDodgeFloor - RunnerHeight;
	const float Top = Bottom - LoadingDodgeRunnerTall;
	for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
	{
		FLoadingBall& Ball = Balls[Index];
		Ball.Position.X -= Speed * Delta;
		if (LoadingCircleHitsBox(Ball.Position, Ball.Radius * 0.85f, LoadingDodgeRunnerX - LoadingDodgeRunnerHalfWidth, Top,
			LoadingDodgeRunnerX + LoadingDodgeRunnerHalfWidth, Bottom))
		{
			Burst(Ball.Position, LoadingPaint::Fire, TEXT("いたっ！"));
			HitFlash = 0.5f;
			Score = 0;
			Balls.RemoveAt(Index);
			continue;
		}
		if (!Ball.bCounted && Ball.Position.X < LoadingDodgeRunnerX - LoadingDodgeRunnerHalfWidth - Ball.Radius)
		{
			Ball.bCounted = true;
			AddScore(1);
		}
		if (Ball.Position.X < -Ball.Radius - 40.0f)
		{
			Balls.RemoveAt(Index);
		}
	}
	if (bAuto && RunnerHeight <= 0.0f)
	{
		for (const FLoadingBall& Ball : Balls)
		{
			const float Arrive = (Ball.Position.X - Ball.Radius - (LoadingDodgeRunnerX + LoadingDodgeRunnerHalfWidth)) / Speed;
			if (!Ball.bHigh && Arrive > 0.04f && Arrive < 0.14f)
			{
				Jump();
				break;
			}
		}
	}
}

void FChaosImpactLoadingState::StepJuggle(const float Delta, const bool bAuto)
{
	Cooldown = FMath::Max(0.0f, Cooldown - Delta);
	if (RespawnIn > 0.0f)
	{
		RespawnIn -= Delta;
		if (RespawnIn <= 0.0f)
		{
			ResetJuggle();
		}
		return;
	}
	if (Balls.IsEmpty())
	{
		ResetJuggle();
		return;
	}
	FLoadingBall& Ball = Balls[0];
	Ball.Velocity.Y += LoadingGravity * Delta;
	Ball.Position += Ball.Velocity * Delta;
	if (Ball.Position.X < LoadingJuggleLeft + Ball.Radius || Ball.Position.X > LoadingJuggleRight - Ball.Radius)
	{
		Ball.Position.X = FMath::Clamp(Ball.Position.X, LoadingJuggleLeft + Ball.Radius, LoadingJuggleRight - Ball.Radius);
		Ball.Velocity.X = -Ball.Velocity.X;
	}
	const float Gap = LoadingJuggleBar - static_cast<float>(Ball.Position.Y + Ball.Radius);
	if (bAuto && Ball.Velocity.Y > 0.0f && Gap < 16.0f)
	{
		Kick(true);
		return;
	}
	if (Ball.Position.Y - Ball.Radius > LoadingJuggleBar + 10.0f)
	{
		// Through the bar: start again.
		Burst(FVector2D(Ball.Position.X, LoadingJuggleBar), LoadingPaint::Muted, TEXT("おしい！"));
		Score = 0;
		Balls.Reset();
		RespawnIn = 0.7f;
	}
}

void FChaosImpactLoadingState::StepTarget(const float Delta, const bool bAuto)
{
	AimClock += Delta;
	ThrowCooldown = FMath::Max(0.0f, ThrowCooldown - Delta);
	for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
	{
		Balls[Index].Position += Balls[Index].Velocity * Delta;
		if (Balls[Index].Position.X < -120.0f || Balls[Index].Position.X > 1720.0f)
		{
			Balls.RemoveAt(Index);
		}
	}
	for (int32 ShotIndex = Shots.Num() - 1; ShotIndex >= 0; --ShotIndex)
	{
		FLoadingBall& Shot = Shots[ShotIndex];
		Shot.Velocity.Y += LoadingShotDrop * Delta;
		Shot.Position += Shot.Velocity * Delta;
		bool bHit = false;
		for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
		{
			const FLoadingBall& Target = Balls[Index];
			if (FVector2D::Distance(Shot.Position, Target.Position) <= Target.Radius + Shot.Radius * 0.6f)
			{
				// The small ones are worth more.
				const int32 Points = Target.Radius < 38.0f ? 3 : Target.Radius < 48.0f ? 2 : 1;
				Burst(Target.Position, LoadingBallColor(Target.Type), FString::Printf(TEXT("+%d"), Points));
				AddScore(Points);
				Balls.RemoveAt(Index);
				bHit = true;
				break;
			}
		}
		if (bHit || Shot.Position.Y < -60.0f || Shot.Position.X > 1660.0f || Shot.Position.X < -60.0f || Shot.Position.Y > 960.0f)
		{
			Shots.RemoveAt(ShotIndex);
		}
	}
	while (Balls.Num() < LoadingTargetCount)
	{
		AddTarget(false);
	}
	if (bAuto && ThrowCooldown <= 0.0f && Shots.IsEmpty())
	{
		// Throws when the swinging aim would meet a target where it will be.
		const FVector2D Aim = GetAim();
		for (const FLoadingBall& Target : Balls)
		{
			FVector2D Ahead = Target.Position;
			float Time = 0.0f;
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				Time = static_cast<float>(FVector2D::Distance(Ahead, LoadingLauncher)) / LoadingShotSpeed;
				Ahead = Target.Position + Target.Velocity * Time;
			}
			const FVector2D Shot = LoadingLauncher + Aim * LoadingShotSpeed * Time + FVector2D(0.0f, 0.5f * LoadingShotDrop * Time * Time);
			if (FVector2D::Distance(Shot, Ahead) < Target.Radius * 0.6f)
			{
				Throw(Aim);
				ThrowCooldown = 0.6f;
				break;
			}
		}
	}
}

void FChaosImpactLoadingState::PopAt(const FVector2D& DesignPoint)
{
	for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
	{
		if (FVector2D::Distance(Balls[Index].Position, DesignPoint) <= Balls[Index].Radius + 12.0f)
		{
			Burst(Balls[Index].Position, LoadingBallColor(Balls[Index].Type));
			Balls.RemoveAt(Index);
			AddScore(1);
			return;
		}
	}
	Burst(DesignPoint, FLinearColor(1.0f, 1.0f, 1.0f, 0.6f));
}

void FChaosImpactLoadingState::PopOne()
{
	int32 Lowest = INDEX_NONE;
	for (int32 Index = 0; Index < Balls.Num(); ++Index)
	{
		if (Lowest == INDEX_NONE || Balls[Index].Position.Y > Balls[Lowest].Position.Y)
		{
			Lowest = Index;
		}
	}
	if (Lowest != INDEX_NONE)
	{
		Burst(Balls[Lowest].Position, LoadingBallColor(Balls[Lowest].Type));
		Balls.RemoveAt(Lowest);
		AddScore(1);
	}
}

void FChaosImpactLoadingState::Jump()
{
	if (RunnerHeight <= 0.0f)
	{
		RunnerSpeed = LoadingDodgeJump;
		RunnerHeight = 0.01f;
	}
}

bool FChaosImpactLoadingState::Kick(const bool bAuto)
{
	if (Balls.IsEmpty() || RespawnIn > 0.0f)
	{
		return false;
	}
	FLoadingBall& Ball = Balls[0];
	const float Gap = LoadingJuggleBar - static_cast<float>(Ball.Position.Y + Ball.Radius);
	if (Ball.Velocity.Y < -200.0f || Gap > LoadingJuggleWindow || Gap < -Ball.Radius)
	{
		if (!bAuto && Gap > LoadingJuggleWindow && Ball.Velocity.Y > 0.0f)
		{
			Burst(FVector2D(Ball.Position.X, LoadingJuggleBar - 30.0f), LoadingPaint::Muted, TEXT("はやい！"));
		}
		return false;
	}
	const bool bPerfect = Gap < 22.0f && Gap > -14.0f;
	Ball.Position.Y = FMath::Min(Ball.Position.Y, static_cast<double>(LoadingJuggleBar - Ball.Radius));
	// Up to about the middle of the screen (under the game's name).
	Ball.Velocity = FVector2D(Random.FRandRange(-220.0f, 220.0f), -Random.FRandRange(860.0f, 960.0f));
	AddScore(1);
	Burst(FVector2D(Ball.Position.X, LoadingJuggleBar), bPerfect ? LoadingPaint::Gold : LoadingPaint::Paper,
		bPerfect && !bAuto ? TEXT("PERFECT!") : TEXT(""));
	return true;
}

void FChaosImpactLoadingState::Throw(const FVector2D Direction)
{
	if (Shots.Num() >= 3)
	{
		return;
	}
	FLoadingBall Shot;
	Shot.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
	Shot.Radius = 16.0f;
	Shot.Position = LoadingLauncher + Direction * 50.0f;
	Shot.Velocity = Direction * LoadingShotSpeed;
	Shots.Add(Shot);
	ThrowCooldown = FMath::Max(ThrowCooldown, 0.15f);
}

void FChaosImpactLoadingState::NextPage(const double Now)
{
	Page = (Page + 1) % LoadingBallTypeCount;
	PageAt = Now;
	Score = FMath::Min(Score + 1, LoadingBallTypeCount);
}

void FChaosImpactLoadingState::Press(const double Now)
{
	LastPressAt = Now;
	QueueSound(EChaosImpactSfx::MiniTap);
	switch (Game)
	{
	case EChaosImpactLoadingGame::Pop: PopOne(); break;
	case EChaosImpactLoadingGame::Dodge: Jump(); break;
	case EChaosImpactLoadingGame::Juggle:
		if (Cooldown <= 0.0f && !Kick(false))
		{
			// Mashing does not work: a miss holds the button off for a moment.
			Cooldown = 0.3f;
		}
		break;
	case EChaosImpactLoadingGame::Target:
		if (ThrowCooldown <= 0.0f)
		{
			Throw(GetAim());
		}
		break;
	case EChaosImpactLoadingGame::Gallery: NextPage(Now); break;
	case EChaosImpactLoadingGame::Breakout:
		if (bBallStuck && !Balls.IsEmpty())
		{
			// Off it goes, a little to one side.
			bBallStuck = false;
			const float Angle = FMath::DegreesToRadians(Random.FRandRange(-30.0f, 30.0f));
			Balls[0].Velocity = FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * 640.0f;
		}
		break;
	case EChaosImpactLoadingGame::Flappy: Flap(); break;
	default: PressExtraGame(); break;
	}
}

void FChaosImpactLoadingState::ClickAt(const FVector2D& DesignPoint, const double Now)
{
	if (Game == EChaosImpactLoadingGame::Memory)
	{
		if (DesignPoint.Y >= 340 && DesignPoint.Y <= 495 && DesignPoint.X >= 455 && DesignPoint.X <= 1145)
		{
			Selection = FMath::Clamp(FMath::RoundToInt((DesignPoint.X - 530) / 180), 0, 3);
			Press(Now);
		}
	}
	else if (Game == EChaosImpactLoadingGame::Pop)
	{
		LastPressAt = Now;
		PopAt(DesignPoint);
	}
	else if (Game == EChaosImpactLoadingGame::Target && DesignPoint.Y < LoadingLauncher.Y - 20.0f)
	{
		// Thrown at where was clicked.
		LastPressAt = Now;
		if (ThrowCooldown <= 0.0f)
		{
			Throw((DesignPoint - LoadingLauncher).GetSafeNormal());
		}
	}
	else
	{
		Press(Now);
	}
}


// ---- New games, the arcade, steering ---------------------------------------------------------------------------------

void FChaosImpactLoadingState::StartGame(const EChaosImpactLoadingGame NewGame)
{
	Game = NewGame < EChaosImpactLoadingGame::Count ? NewGame : EChaosImpactLoadingGame::Pop;
	Balls.Reset();
	Shots.Reset();
	Bursts.Reset();
	Score = 0;
	Best = LoadingBest[static_cast<int32>(Game)];
	HitFlash = 0.0f;
	RespawnIn = 0.0f;
	Cooldown = 0.0f;
	SpawnIn = 0.8f;
	RunnerHeight = 0.0f;
	RunnerSpeed = 0.0f;
	Scroll = 0.0f;
	AimClock = 0.0f;
	ThrowCooldown = 0.0f;
	PageAt = FPlatformTime::Seconds();
	PaddleX = 800.0f;
	bBallStuck = true;
	SteerLeft = SteerRight = 0;
	StickX = 0.0f;
	SteerUp = SteerDown = 0;
	StickY = 0.0f;
	PointerX = -1.0f;
	PointerMovedAt = -100.0;
	switch (Game)
	{
	case EChaosImpactLoadingGame::Pop:
		for (int32 Index = 0; Index < LoadingPopCount; ++Index)
		{
			AddBall(false);
		}
		break;
	case EChaosImpactLoadingGame::Juggle:
		ResetJuggle();
		break;
	case EChaosImpactLoadingGame::Target:
		for (int32 Index = 0; Index < LoadingTargetCount; ++Index)
		{
			AddTarget(true);
		}
		break;
	case EChaosImpactLoadingGame::Gallery:
		Page = Random.RandRange(0, LoadingBallTypeCount - 1);
		Score = 1;
		break;
	case EChaosImpactLoadingGame::Breakout:
	{
		ResetBricks();
		FLoadingBall Ball;
		Ball.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
		Ball.Radius = LoadingBreakBallRadius;
		Balls.Add(Ball);
		Cooldown = 0.6f;
		break;
	}
	case EChaosImpactLoadingGame::Flappy:
		ResetFlappy();
		break;
	case EChaosImpactLoadingGame::Avoid:
		SpawnIn = 0.6f;
		break;
	default:
		StartExtraGame();
		break;
	}
}

void FChaosImpactLoadingState::SwitchGame(const int32 Direction)
{
	QueueSound(EChaosImpactSfx::UiTab);
	constexpr int32 Count = static_cast<int32>(EChaosImpactLoadingGame::Count);
	StartGame(static_cast<EChaosImpactLoadingGame>((static_cast<int32>(Game) + Direction % Count + Count) % Count));
}

void FChaosImpactLoadingState::SetSteerKey(const int32 Direction, const bool bDown, const double Now)
{
	(Direction < 0 ? SteerLeft : SteerRight) = bDown ? 1 : 0;
	if (bDown)
	{
		TurnExtraGame(Direction);
		LastPressAt = Now;
		// The keys take over from the mouse.
		PointerMovedAt = -100.0;
	}
}

void FChaosImpactLoadingState::SetStick(const float X, const double Now)
{
	// A drifting/unused axis must not repeatedly cancel the one-button games' automatic demo.
	if (!UsesSteering()) { StickX = 0.0f; return; }
	if (FMath::Abs(X) > 0.6f && (FMath::Abs(StickX) <= 0.6f || FMath::Sign(X) != FMath::Sign(StickX)))
	{
		TurnExtraGame(X < 0.0f ? -1 : 1);
	}
	StickX = FMath::Abs(X) > 0.25f ? X : 0.0f;
	if (StickX != 0.0f)
	{
		LastPressAt = Now;
		PointerMovedAt = -100.0;
	}
}

void FChaosImpactLoadingState::SetVerticalKey(const int32 Direction, const bool bDown, const double Now)
{
	(Direction < 0 ? SteerUp : SteerDown) = bDown ? 1 : 0;
	if (bDown)
	{
		TurnVertical(Direction);
		LastPressAt = Now;
	}
}

void FChaosImpactLoadingState::SetStickY(const float Y, const double Now)
{
	if (!UsesVertical()) { StickY = 0.0f; return; }
	// The stick's up is positive; the screen's is negative.
	const float Down = -Y;
	if (FMath::Abs(Down) > 0.6f && (FMath::Abs(StickY) <= 0.6f || FMath::Sign(Down) != FMath::Sign(StickY)))
	{
		TurnVertical(Down < 0.0f ? -1 : 1);
	}
	StickY = FMath::Abs(Down) > 0.25f ? Down : 0.0f;
	if (StickY != 0.0f)
	{
		LastPressAt = Now;
	}
}

void FChaosImpactLoadingState::SetPointer(const float DesignX, const double Now)
{
	// Hovering is not playing: only games that actually steer with the pointer treat it as input.
	if (!UsesPointerSteering()) { return; }
	if (FMath::Abs(DesignX - PointerX) > 2.0f)
	{
		PointerX = DesignX;
		PointerMovedAt = Now;
		LastPressAt = Now;
	}
}

float FChaosImpactLoadingState::Steer(const float X, const float Speed, const float Delta, const bool bAuto, const float AutoX,
	const float Min, const float Max) const
{
	float Moved = X;
	if (bAuto)
	{
		Moved = X + FMath::Clamp(AutoX - X, -Speed * Delta, Speed * Delta);
	}
	else if (PointerX >= 0.0f && LastStepAt - PointerMovedAt < 2.0)
	{
		// The mouse: straight to under the pointer (quickly, not at once).
		Moved = X + FMath::Clamp(PointerX - X, -Speed * 1.8f * Delta, Speed * 1.8f * Delta);
	}
	else
	{
		const float Direction = FMath::Clamp(static_cast<float>(SteerRight - SteerLeft) + StickX, -1.0f, 1.0f);
		Moved = X + Direction * Speed * Delta;
	}
	return FMath::Clamp(Moved, Min, Max);
}

void FChaosImpactLoadingState::ResetBricks()
{
	Bricks.SetNumZeroed(LoadingBreakColumns * LoadingBreakRows);
	for (int32 Row = 0; Row < LoadingBreakRows; ++Row)
	{
		// A row of one ball's colour each.
		const uint8 Colour = static_cast<uint8>(1 + (Row * 3 + Random.RandRange(0, 2)) % LoadingBallTypeCount);
		for (int32 Column = 0; Column < LoadingBreakColumns; ++Column)
		{
			Bricks[Row * LoadingBreakColumns + Column] = Colour;
		}
	}
}

void FChaosImpactLoadingState::StepBreakout(const float Delta, const bool bAuto)
{
	if (Balls.IsEmpty())
	{
		return;
	}
	FLoadingBall& Ball = Balls[0];
	// Playing itself: under the ball, a little to one side so it goes off at an angle.
	const float AutoX = static_cast<float>(Ball.Position.X) + (Ball.Velocity.X >= 0.0 ? -18.0f : 18.0f);
	PaddleX = Steer(PaddleX, 1300.0f, Delta, bAuto, AutoX, LoadingBreakLeft + LoadingBreakPaddleHalf,
		LoadingBreakRight - LoadingBreakPaddleHalf);
	if (bBallStuck)
	{
		Ball.Position = FVector2D(PaddleX, LoadingBreakPaddleY - Ball.Radius - 2.0f);
		Cooldown -= Delta;
		if (bAuto && Cooldown <= 0.0f)
		{
			Press(LastStepAt - 100.0);
		}
		return;
	}
	const float Speed = 640.0f + FMath::Min(Score * 8.0f, 360.0f);
	Ball.Velocity = Ball.Velocity.GetSafeNormal() * Speed;
	const float PreviousBottom = static_cast<float>(Ball.Position.Y) + Ball.Radius;
	Ball.Position += Ball.Velocity * Delta;
	if (Ball.Position.X < LoadingBreakLeft + Ball.Radius)
	{
		Ball.Position.X = LoadingBreakLeft + Ball.Radius;
		Ball.Velocity.X = FMath::Abs(Ball.Velocity.X);
	}
	else if (Ball.Position.X > LoadingBreakRight - Ball.Radius)
	{
		Ball.Position.X = LoadingBreakRight - Ball.Radius;
		Ball.Velocity.X = -FMath::Abs(Ball.Velocity.X);
	}
	if (Ball.Position.Y < LoadingBreakTop + Ball.Radius)
	{
		Ball.Position.Y = LoadingBreakTop + Ball.Radius;
		Ball.Velocity.Y = FMath::Abs(Ball.Velocity.Y);
	}
	// Off the paddle at an angle by where it struck (the ends send it off sideways).
	// (Crossing the paddle's top this frame, however far it went in one long frame.)
	if (Ball.Velocity.Y > 0.0 && Ball.Position.Y + Ball.Radius >= LoadingBreakPaddleY && PreviousBottom <= LoadingBreakPaddleY + 4.0f
		&& FMath::Abs(Ball.Position.X - PaddleX) <= LoadingBreakPaddleHalf + Ball.Radius)
	{
		const float Off = FMath::Clamp(static_cast<float>(Ball.Position.X - PaddleX) / LoadingBreakPaddleHalf, -1.0f, 1.0f);
		const float Angle = FMath::DegreesToRadians(Off * 62.0f);
		Ball.Velocity = FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * Speed;
		Ball.Position.Y = LoadingBreakPaddleY - Ball.Radius;
	}
	// One brick a frame: knocked out, the ball turned back on the side it came in.
	for (int32 Index = 0; Index < Bricks.Num(); ++Index)
	{
		if (Bricks[Index] == 0)
		{
			continue;
		}
		const float X = LoadingBreakLeft + (Index % LoadingBreakColumns) * LoadingBreakBrickWidth + 3.0f;
		const float Y = LoadingBreakTop + 20.0f + (Index / LoadingBreakColumns) * LoadingBreakBrickStep;
		const float W = LoadingBreakBrickWidth - 6.0f;
		const float H = LoadingBreakBrickHeight;
		if (!LoadingCircleHitsBox(Ball.Position, Ball.Radius, X, Y, X + W, Y + H))
		{
			continue;
		}
		const float OverX = FMath::Min(static_cast<float>(Ball.Position.X) + Ball.Radius - X, X + W - static_cast<float>(Ball.Position.X) + Ball.Radius);
		const float OverY = FMath::Min(static_cast<float>(Ball.Position.Y) + Ball.Radius - Y, Y + H - static_cast<float>(Ball.Position.Y) + Ball.Radius);
		if (OverX < OverY)
		{
			Ball.Velocity.X = -Ball.Velocity.X;
		}
		else
		{
			Ball.Velocity.Y = -Ball.Velocity.Y;
		}
		Burst(FVector2D(X + W * 0.5f, Y + H * 0.5f), LoadingBallColor(Bricks[Index] - 1));
		Bricks[Index] = 0;
		AddScore(1);
		break;
	}
	if (!Bricks.Contains(static_cast<uint8>(1)) && Bricks.FindByPredicate([](const uint8 Brick) { return Brick != 0; }) == nullptr)
	{
		// The wall is down: a new one, and on it goes (faster).
		Burst(FVector2D(800.0f, 380.0f), LoadingPaint::Gold, TEXT("クリア！"));
		ResetBricks();
	}
	if (Ball.Position.Y - Ball.Radius > LoadingBreakMissY)
	{
		// Missed: from the start again.
		Burst(FVector2D(Ball.Position.X, LoadingBreakPaddleY), LoadingPaint::Muted, TEXT("おしい！"));
		Score = 0;
		ResetBricks();
		bBallStuck = true;
		Cooldown = 0.7f;
	}
}

void FChaosImpactLoadingState::ResetFlappy()
{
	Balls.Reset();
	BirdY = (LoadingFlapTop + LoadingFlapGround) * 0.5f;
	BirdSpeed = 0.0f;
	SpawnIn = 1.0f;
	RespawnIn = 0.0f;
}

void FChaosImpactLoadingState::Flap()
{
	if (RespawnIn <= 0.0f)
	{
		BirdSpeed = LoadingFlapImpulse;
		BirdFlapAt = static_cast<float>(LastStepAt - StartedAt);
	}
}

void FChaosImpactLoadingState::StepFlappy(const float Delta, const bool bAuto)
{
	HitFlash = FMath::Max(0.0f, HitFlash - Delta);
	if (RespawnIn > 0.0f)
	{
		RespawnIn -= Delta;
		if (RespawnIn <= 0.0f)
		{
			ResetFlappy();
		}
		return;
	}
	BirdSpeed += LoadingFlapGravity * Delta;
	BirdY += BirdSpeed * Delta;
	if (BirdY - LoadingFlapBirdRadius < LoadingFlapTop)
	{
		BirdY = LoadingFlapTop + LoadingFlapBirdRadius;
		BirdSpeed = FMath::Max(BirdSpeed, 0.0f);
	}
	const float Speed = 260.0f + FMath::Min(Score * 6.0f, 160.0f);
	SpawnIn -= Delta;
	if (SpawnIn <= 0.0f)
	{
		FLoadingBall Pillar;
		Pillar.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
		Pillar.Radius = FMath::Max(LoadingFlapGapHalf - Score * 1.5f, 82.0f);
		Pillar.Position = FVector2D(LoadingFlapRight + LoadingFlapPillarWidth,
			Random.FRandRange(LoadingFlapTop + Pillar.Radius + 30.0f, LoadingFlapGround - Pillar.Radius - 30.0f));
		Balls.Add(Pillar);
		SpawnIn = LoadingFlapPillarEvery * 260.0f / Speed;
	}
	bool bCrashed = BirdY + LoadingFlapBirdRadius > LoadingFlapGround;
	const FLoadingBall* Next = nullptr;
	for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
	{
		FLoadingBall& Pillar = Balls[Index];
		Pillar.Position.X -= Speed * Delta;
		const float Reach = LoadingFlapPillarWidth * 0.5f + LoadingFlapBirdRadius * 0.8f;
		if (FMath::Abs(Pillar.Position.X - LoadingFlapBirdX) < Reach
			&& (BirdY - LoadingFlapBirdRadius * 0.8f < Pillar.Position.Y - Pillar.Radius
				|| BirdY + LoadingFlapBirdRadius * 0.8f > Pillar.Position.Y + Pillar.Radius))
		{
			bCrashed = true;
		}
		if (!Pillar.bCounted && Pillar.Position.X + LoadingFlapPillarWidth * 0.5f < LoadingFlapBirdX - LoadingFlapBirdRadius)
		{
			Pillar.bCounted = true;
			AddScore(1);
		}
		if (Pillar.Position.X < LoadingFlapLeft - LoadingFlapPillarWidth)
		{
			Balls.RemoveAt(Index);
		}
	}
	for (const FLoadingBall& Pillar : Balls)
	{
		if (Pillar.Position.X + LoadingFlapPillarWidth * 0.5f > LoadingFlapBirdX - LoadingFlapBirdRadius && (!Next || Pillar.Position.X < Next->Position.X))
		{
			Next = &Pillar;
		}
	}
	if (bCrashed)
	{
		Burst(FVector2D(LoadingFlapBirdX, BirdY), LoadingPaint::Paper, TEXT("ぶつかった！"));
		Score = 0;
		HitFlash = 0.5f;
		RespawnIn = 0.8f;
		return;
	}
	if (bAuto)
	{
		// Playing itself: a flap whenever it sinks below the middle of the next gap.
		const float Aim = Next ? static_cast<float>(Next->Position.Y) + 30.0f : (LoadingFlapTop + LoadingFlapGround) * 0.5f;
		if (BirdY > Aim && BirdSpeed > -120.0f)
		{
			Flap();
		}
	}
}

void FChaosImpactLoadingState::StepAvoid(const float Delta, const bool bAuto)
{
	HitFlash = FMath::Max(0.0f, HitFlash - Delta);
	// Playing itself: to the spot with the least coming down on it soon (staying put if that is as good).
	float AutoX = PaddleX;
	if (bAuto)
	{
		float BestDanger = TNumericLimits<float>::Max();
		for (float X = LoadingAvoidLeft + LoadingAvoidHalf; X <= LoadingAvoidRight - LoadingAvoidHalf; X += 40.0f)
		{
			float Danger = FMath::Abs(X - PaddleX) * 0.002f;
			for (const FLoadingBall& Ball : Balls)
			{
				const float Arrive = static_cast<float>(LoadingAvoidGround - LoadingAvoidTall - Ball.Position.Y) / FMath::Max(static_cast<float>(Ball.Velocity.Y), 1.0f);
				const float Reach = FMath::Abs(X - PaddleX) / 900.0f;
				if (Arrive < 1.2f && FMath::Abs(static_cast<float>(Ball.Position.X) - X) < Ball.Radius + LoadingAvoidHalf + 25.0f)
				{
					Danger += Arrive < Reach + 0.1f ? 50.0f : 3.0f / FMath::Max(Arrive, 0.05f);
				}
			}
			if (Danger < BestDanger)
			{
				BestDanger = Danger;
				AutoX = X;
			}
		}
	}
	PaddleX = Steer(PaddleX, 900.0f, Delta, bAuto, AutoX, LoadingAvoidLeft + LoadingAvoidHalf, LoadingAvoidRight - LoadingAvoidHalf);
	SpawnIn -= Delta;
	if (SpawnIn <= 0.0f)
	{
		FLoadingBall Ball;
		Ball.Type = Random.RandRange(0, LoadingBallTypeCount - 1);
		Ball.Radius = Random.FRandRange(16.0f, 34.0f);
		Ball.Position = FVector2D(Random.FRandRange(LoadingAvoidLeft + 30.0f, LoadingAvoidRight - 30.0f), LoadingAvoidTop - Ball.Radius);
		Ball.Velocity = FVector2D(0.0f, Random.FRandRange(320.0f, 560.0f) + FMath::Min(Score * 5.0f, 320.0f));
		Balls.Add(Ball);
		SpawnIn = FMath::Max(0.16f, 0.55f - Score * 0.006f);
	}
	for (int32 Index = Balls.Num() - 1; Index >= 0; --Index)
	{
		FLoadingBall& Ball = Balls[Index];
		Ball.Position += Ball.Velocity * Delta;
		if (LoadingCircleHitsBox(Ball.Position, Ball.Radius * 0.85f, PaddleX - LoadingAvoidHalf, LoadingAvoidGround - LoadingAvoidTall,
			PaddleX + LoadingAvoidHalf, LoadingAvoidGround))
		{
			Burst(Ball.Position, LoadingPaint::Fire, TEXT("いたっ！"));
			HitFlash = 0.5f;
			Score = 0;
			Balls.RemoveAt(Index);
			continue;
		}
		if (Ball.Position.Y + Ball.Radius > LoadingAvoidGround)
		{
			Burst(FVector2D(Ball.Position.X, LoadingAvoidGround), LoadingPaint::WithAlpha(LoadingBallColor(Ball.Type), 0.6f));
			AddScore(1);
			Balls.RemoveAt(Index);
		}
	}
}

// =====================================================================================================================
// Widget
// =====================================================================================================================

void SChaosImpactLoadingScreen::Construct(const FArguments& InArgs)
{
	State = InArgs._State;
	bInteractive = InArgs._Interactive;
	SetCanTick(false);
}

FReply SChaosImpactLoadingScreen::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bInteractive && FApp::IsUnattended()) { return FReply::Handled(); }
	if (!bInteractive || !State.IsValid())
	{
		return FReply::Unhandled();
	}
	const FVector2D Size = FVector2D(MyGeometry.GetLocalSize());
	const float Fit = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	const FVector2D Design = (Local - (Size - FVector2D(1600.0f, 900.0f) * Fit) * 0.5f) / Fit;
	FScopeLock Guard(&State->Lock);
	if (State->FadeStartedAt < 0.0)
	{
		State->ClickAt(Design, FPlatformTime::Seconds());
	}
	return FReply::Handled();
}

FReply SChaosImpactLoadingScreen::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bInteractive && FApp::IsUnattended()) { return FReply::Handled(); }
	if (!bInteractive || !State.IsValid())
	{
		return FReply::Unhandled();
	}
	// The mouse steers the paddle (or the player) left and right.
	const FVector2D Size = FVector2D(MyGeometry.GetLocalSize());
	const float Fit = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	const float DesignX = static_cast<float>((Local.X - (Size.X - 1600.0f * Fit) * 0.5f) / Fit);
	FScopeLock Guard(&State->Lock);
	State->SetPointer(DesignX, FPlatformTime::Seconds());
	return FReply::Handled();
}

int32 SChaosImpactLoadingScreen::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	if (!State.IsValid())
	{
		return LayerId;
	}
	FScopeLock Guard(&State->Lock);
	FChaosImpactLoadingState& S = *State;
	const double Now = FPlatformTime::Seconds();
	S.Step(Now);
	const float T = static_cast<float>(Now - S.StartedAt);
	const bool bAuto = S.IsAuto(Now);
	const float Fade = S.FadeStartedAt < 0.0 ? 1.0f
		: FMath::Clamp(1.0f - static_cast<float>(Now - S.FadeStartedAt) / LoadingFadeSeconds, 0.0f, 1.0f);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float Fit = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
	const FGeometry Design = AllottedGeometry.MakeChild(FVector2f(1600.0f, 900.0f),
		FSlateLayoutTransform(Fit, FVector2f((Size.X - 1600.0f * Fit) * 0.5f, (Size.Y - 900.0f * Fit) * 0.5f)));

	// Background: dark, with slanted coloured sides (each game its own colours).
	const LoadingPaint::FPainter Full{AllottedGeometry, OutDrawElements, LayerId, Fade};
	Full.Box(0.0f, 0.0f, Size.X, Size.Y, LoadingPaint::Ink);
	FLinearColor Left, Right;
	LoadingSideColors(S, Left, Right);
	const FGeometry Slant = LoadingPaint::MakeSkewed(Design, 0.0f, 0.0f, 1600.0f, 900.0f, -0.36f);
	const LoadingPaint::FPainter Sides{Slant, OutDrawElements, LayerId + 1, Fade};
	Sides.Box(-900.0f, -100.0f, 1180.0f, 1100.0f, LoadingPaint::WithAlpha(Left, 0.22f));
	Sides.Box(274.0f, -100.0f, 6.0f, 1100.0f, LoadingPaint::WithAlpha(Left, 0.8f));
	Sides.Box(1320.0f, -100.0f, 1200.0f, 1100.0f, LoadingPaint::WithAlpha(Right, 0.22f));
	Sides.Box(1320.0f, -100.0f, 6.0f, 1100.0f, LoadingPaint::WithAlpha(Right, 0.8f));

	const LoadingPaint::FPainter Back{Design, OutDrawElements, LayerId + 2, Fade};
	const LoadingPaint::FPainter Front{Design, OutDrawElements, LayerId + 3, Fade};
	FString Hint;
	switch (S.Game)
	{
	case EChaosImpactLoadingGame::Pop:
	{
		for (const FLoadingBall& Ball : S.Balls)
		{
			LoadingDrawBall(Back, Front, Ball.Position, Ball.Radius, Ball.Type);
		}
		Hint = FString::Printf(TEXT("クリック・ボタンでボールを割れる！　割った数 %d"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Dodge:
	{
		// The floor running by.
		Back.Box(0.0f, LoadingDodgeFloor, 1600.0f, 4.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.5f));
		for (int32 Dash = 0; Dash < 22; ++Dash)
		{
			const float X = FMath::Fmod(Dash * 80.0f - S.Scroll + 1760.0f * 100.0f, 1760.0f) - 80.0f;
			Back.Box(X, LoadingDodgeFloor + 16.0f, 40.0f, 3.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.18f));
		}
		// The runner: a little player, red and shaking when hit.
		const float X = LoadingDodgeRunnerX + (S.HitFlash > 0.0f ? 5.0f * FMath::Sin(T * 70.0f) : 0.0f);
		const float Base = LoadingDodgeFloor - S.RunnerHeight;
		const FLinearColor Body = S.HitFlash > 0.0f && FMath::Fmod(T, 0.12f) < 0.06f ? LoadingPaint::Fire : LoadingPaint::Ice;
		const float ShadowWidth = 24.0f * (1.0f - FMath::Min(S.RunnerHeight / 260.0f, 0.6f));
		Back.Box(LoadingDodgeRunnerX - ShadowWidth, LoadingDodgeFloor - 3.0f, ShadowWidth * 2.0f, 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		const bool bInAir = S.RunnerHeight > 0.0f;
		const float Stride = bInAir ? 0.4f : FMath::Sin(S.Scroll * 0.025f);
		Back.Line(FVector2D(X, Base - 38.0f), FVector2D(X + 14.0f * Stride, Base - (bInAir ? 14.0f : 2.0f)), Body, 8.0f);
		Back.Line(FVector2D(X, Base - 38.0f), FVector2D(X - 14.0f * Stride, Base - (bInAir ? 18.0f : 2.0f)), Body, 8.0f);
		Back.Disc(FVector2D(X, Base - 72.0f), 18.0f, Body);
		Back.Box(X - 18.0f, Base - 72.0f, 36.0f, 28.0f, Body);
		Back.Disc(FVector2D(X, Base - 44.0f), 18.0f, Body);
		Back.Line(FVector2D(X, Base - 74.0f), FVector2D(X + 20.0f * (bInAir ? 1.0f : -Stride), Base - (bInAir ? 96.0f : 52.0f)), Body, 7.0f);
		Front.Disc(FVector2D(X + 3.0f, Base - 98.0f), 15.0f, LoadingPaint::Paper);
		Front.Disc(FVector2D(X + 9.0f, Base - 100.0f), 3.0f, LoadingPaint::Ink);
		for (const FLoadingBall& Ball : S.Balls)
		{
			// Speed lines behind, and a roll.
			for (int32 Line = 0; Line < 3; ++Line)
			{
				const float Y = static_cast<float>(Ball.Position.Y) + (Line - 1) * Ball.Radius * 0.5f;
				Back.Line(FVector2D(Ball.Position.X + Ball.Radius + 8.0f, Y), FVector2D(Ball.Position.X + Ball.Radius + 40.0f + Line * 12.0f, Y),
					LoadingPaint::WithAlpha(LoadingBallColor(Ball.Type), 0.35f), 3.0f);
			}
			LoadingDrawBall(Back, Front, Ball.Position, Ball.Radius, Ball.Type,
				Ball.bHigh ? -1.0f : FMath::Fmod(-S.Scroll / Ball.Radius, UE_TWO_PI) + UE_TWO_PI);
		}
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), LoadingDodgeRunnerX, Base - 150.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("ボタン・クリックでジャンプ！　高いボールは跳ばずに見送ろう　よけた数 %d"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Juggle:
	{
		const float Width = LoadingJuggleRight - LoadingJuggleLeft;
		const float Center = (LoadingJuggleLeft + LoadingJuggleRight) * 0.5f;
		Back.Text(FString::FromInt(S.Score), Center, 210.0f, 120.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.12f),
			LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		float Gap = 1000.0f;
		bool bFalling = false;
		if (!S.Balls.IsEmpty())
		{
			Gap = LoadingJuggleBar - static_cast<float>(S.Balls[0].Position.Y + S.Balls[0].Radius);
			bFalling = S.Balls[0].Velocity.Y > 0.0f;
		}
		const bool bInWindow = bFalling && Gap < LoadingJuggleWindow && Gap > -LoadingJuggleRadius;
		// The window to kick in, lit while the ball is in it, and the bar.
		Back.Box(LoadingJuggleLeft, LoadingJuggleBar - LoadingJuggleWindow, Width, LoadingJuggleWindow,
			LoadingPaint::WithAlpha(LoadingPaint::Gold, bInWindow ? 0.22f : 0.06f));
		Back.Box(LoadingJuggleLeft, LoadingJuggleBar, Width, 6.0f, LoadingPaint::Gold);
		Back.Box(LoadingJuggleLeft, 180.0f, 3.0f, LoadingJuggleBar - 180.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.12f));
		Back.Box(LoadingJuggleRight - 3.0f, 180.0f, 3.0f, LoadingJuggleBar - 180.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.12f));
		if (!S.Balls.IsEmpty())
		{
			const FLoadingBall& Ball = S.Balls[0];
			// Its shadow on the bar, and a ring closing in as it comes down onto it.
			const float Near = 1.0f - FMath::Clamp(Gap / 450.0f, 0.0f, 1.0f);
			Back.Box(static_cast<float>(Ball.Position.X) - 40.0f * Near, LoadingJuggleBar - 4.0f, 80.0f * Near, 4.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			LoadingDrawBall(Back, Front, Ball.Position, Ball.Radius, Ball.Type);
			if (bFalling && Gap > 0.0f && Gap < 300.0f)
			{
				Front.Ring(Ball.Position, Ball.Radius + Gap * 0.35f, LoadingPaint::WithAlpha(LoadingPaint::Gold, bInWindow ? 1.0f : 0.5f), 4.0f, 40);
			}
		}
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), Center, LoadingJuggleBar + 20.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("ボールがバーに来たらボタン！　れんぞく %d"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Target:
	{
		for (const FLoadingBall& Target : S.Balls)
		{
			// A bullseye in the ball's colour.
			const FLinearColor Color = LoadingBallColor(Target.Type);
			Back.Disc(Target.Position + FVector2D(5.0f, 7.0f), Target.Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
			Back.Disc(Target.Position, Target.Radius, Color);
			Front.Disc(Target.Position, Target.Radius * 0.7f, LoadingPaint::Paper);
			Front.Disc(Target.Position, Target.Radius * 0.42f, Color);
			Front.Disc(Target.Position, Target.Radius * 0.16f, LoadingPaint::Paper);
		}
		// The launcher and its swinging aim, with where a throw would go.
		const FVector2D Aim = S.GetAim();
		for (int32 Dot = 1; Dot <= 7; ++Dot)
		{
			const float Time = Dot * 0.05f;
			const FVector2D At = LoadingLauncher + Aim * LoadingShotSpeed * Time + FVector2D(0.0f, 0.5f * LoadingShotDrop * Time * Time);
			Back.Disc(At, 5.0f, LoadingPaint::WithAlpha(LoadingPaint::Gold, 0.55f - Dot * 0.06f));
		}
		Back.Disc(LoadingLauncher, 40.0f, LoadingPaint::Ink);
		Front.Ring(LoadingLauncher, 40.0f, LoadingPaint::Gold, 4.0f, 40);
		Front.Line(LoadingLauncher, LoadingLauncher + Aim * 70.0f, LoadingPaint::Gold, 8.0f);
		for (const FLoadingBall& Shot : S.Shots)
		{
			Back.Line(Shot.Position - Shot.Velocity.GetSafeNormal() * 46.0f, Shot.Position,
				LoadingPaint::WithAlpha(LoadingBallColor(Shot.Type), 0.45f), 6.0f);
			LoadingDrawBall(Back, Front, Shot.Position, Shot.Radius, Shot.Type);
		}
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), LoadingLauncher.X, LoadingLauncher.Y + 52.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("ボタンで投げる（クリックした所へも投げられる）　小さい的ほど高得点　%d点"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Gallery:
	{
		const EChaosImpactBallType Ball = LoadingBallType(S.Page);
		const FLinearColor Color = LoadingBallColor(S.Page);
		const float In = LoadingPaint::EaseOut(static_cast<float>(Now - S.PageAt) / 0.4f);
		const FVector2D Center(430.0f - (1.0f - In) * 90.0f, 470.0f);
		// The ball, big, with rays turning round it.
		for (int32 Ray = 0; Ray < 14; ++Ray)
		{
			const float Angle = Ray * UE_TWO_PI / 14.0f + T * 0.4f;
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			const float Reach = 215.0f + 25.0f * FMath::Sin(T * 3.0f + Ray);
			Back.Line(Center + Direction * 180.0f, Center + Direction * Reach, LoadingPaint::WithAlpha(Color, 0.45f * In), 6.0f);
		}
		Back.Ring(Center, 172.0f, LoadingPaint::WithAlpha(Color, 0.35f * In), 3.0f, 64);
		const LoadingPaint::FPainter BallBack{Design, OutDrawElements, LayerId + 2, Fade * In};
		const LoadingPaint::FPainter BallFront{Design, OutDrawElements, LayerId + 3, Fade * In};
		LoadingDrawBall(BallBack, BallFront, Center + FVector2D(0.0f, 8.0f * FMath::Sin(T * 2.0f)), 140.0f, S.Page);
		// Its name and what it does.
		const LoadingPaint::FPainter Words{Design, OutDrawElements, LayerId + 3, Fade * In};
		const float X = 720.0f + (1.0f - In) * 60.0f;
		Words.Text(FString::Printf(TEXT("No.%02d / %d"), S.Page + 1, LoadingBallTypeCount), X, 250.0f, 22.0f, LoadingPaint::Muted,
			LoadingPaint::ETextAlign::Left, TEXT("BlackItalic"));
		Words.Text(FString::Printf(TEXT("%sボール"), ChaosImpactBallTypes::GetDisplayName(Ball)), X, 286.0f, 64.0f, Color,
			LoadingPaint::ETextAlign::Left, TEXT("BlackItalic"), 3.0f, LoadingPaint::Ink);
		Words.Box(X, 384.0f, 520.0f, 4.0f, LoadingPaint::WithAlpha(Color, 0.8f));
		const TArray<FString> Lines = LoadingWrap(LoadingBallText(Ball), 21);
		for (int32 Line = 0; Line < Lines.Num(); ++Line)
		{
			Words.Text(Lines[Line], X, 408.0f + Line * 44.0f, 26.0f, LoadingPaint::Paper, LoadingPaint::ETextAlign::Left, TEXT("Bold"),
				2.0f, LoadingPaint::Ink);
		}
		for (int32 Dot = 0; Dot < LoadingBallTypeCount; ++Dot)
		{
			Front.Disc(FVector2D(726.0f + Dot * 26.0f, 560.0f + 44.0f * FMath::Max(0, Lines.Num() - 2)), Dot == S.Page ? 8.0f : 5.0f,
				Dot == S.Page ? LoadingBallColor(Dot) : LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.3f));
		}
		Hint = TEXT("ボタン・クリックで次のボールへ");
		break;
	}
	case EChaosImpactLoadingGame::Breakout:
	{
		// The court: its frame, the wall of bricks, the paddle and the ball.
		const float Width = LoadingBreakRight - LoadingBreakLeft;
		Back.Box(LoadingBreakLeft - 8.0f, LoadingBreakTop - 8.0f, Width + 16.0f, 6.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.35f));
		Back.Box(LoadingBreakLeft - 8.0f, LoadingBreakTop - 8.0f, 6.0f, LoadingBreakMissY - LoadingBreakTop, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.35f));
		Back.Box(LoadingBreakRight + 2.0f, LoadingBreakTop - 8.0f, 6.0f, LoadingBreakMissY - LoadingBreakTop, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.35f));
		Back.Box(LoadingBreakLeft, LoadingBreakTop, Width, LoadingBreakMissY - LoadingBreakTop, FLinearColor(0.0f, 0.0f, 0.0f, 0.25f));
		for (int32 Index = 0; Index < S.Bricks.Num(); ++Index)
		{
			if (S.Bricks[Index] == 0)
			{
				continue;
			}
			const float X = LoadingBreakLeft + (Index % LoadingBreakColumns) * LoadingBreakBrickWidth + 3.0f;
			const float Y = LoadingBreakTop + 20.0f + (Index / LoadingBreakColumns) * LoadingBreakBrickStep;
			const FLinearColor Colour = LoadingBallColor(S.Bricks[Index] - 1);
			Back.Box(X, Y, LoadingBreakBrickWidth - 6.0f, LoadingBreakBrickHeight, Colour);
			Front.Box(X, Y, LoadingBreakBrickWidth - 6.0f, 5.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.35f));
			Front.Box(X, Y + LoadingBreakBrickHeight - 4.0f, LoadingBreakBrickWidth - 6.0f, 4.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.25f));
		}
		Back.Box(S.PaddleX - LoadingBreakPaddleHalf, LoadingBreakPaddleY, LoadingBreakPaddleHalf * 2.0f, 14.0f, LoadingPaint::Gold);
		Front.Box(S.PaddleX - LoadingBreakPaddleHalf, LoadingBreakPaddleY, LoadingBreakPaddleHalf * 2.0f, 4.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		if (!S.Balls.IsEmpty())
		{
			const FLoadingBall& Ball = S.Balls[0];
			if (!S.bBallStuck)
			{
				Back.Line(Ball.Position - Ball.Velocity.GetSafeNormal() * 34.0f, Ball.Position,
					LoadingPaint::WithAlpha(LoadingBallColor(Ball.Type), 0.4f), 6.0f);
			}
			LoadingDrawBall(Back, Front, Ball.Position, Ball.Radius, Ball.Type);
		}
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), S.PaddleX, LoadingBreakPaddleY + 20.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("←→・スティック・マウスで動かす　ボタンで発射　こわした数 %d"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Flappy:
	{
		// The sky between the ceiling and the ground, pillars coming through it, and the bird.
		const float Width = LoadingFlapRight - LoadingFlapLeft;
		Back.Box(LoadingFlapLeft, LoadingFlapTop, Width, LoadingFlapGround - LoadingFlapTop, FLinearColor(0.02f, 0.06f, 0.12f, 0.55f));
		Back.Box(LoadingFlapLeft, LoadingFlapGround, Width, 10.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.5f));
		Back.Box(LoadingFlapLeft, LoadingFlapTop - 6.0f, Width, 6.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.25f));
		for (int32 Snow = 0; Snow < 26; ++Snow)
		{
			// Snow drifting by behind (a shima-enaga's winter).
			const float X = LoadingFlapRight - FMath::Fmod(Snow * 97.0f + T * (40.0f + (Snow % 3) * 25.0f), Width);
			const float Y = LoadingFlapTop + FMath::Fmod(Snow * 53.0f + T * 30.0f, LoadingFlapGround - LoadingFlapTop);
			Back.Disc(FVector2D(X, Y), 2.0f + (Snow % 3), LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.25f));
		}
		for (const FLoadingBall& Pillar : S.Balls)
		{
			const float PillarLeft = FMath::Max(static_cast<float>(Pillar.Position.X) - LoadingFlapPillarWidth * 0.5f, LoadingFlapLeft);
			const float PillarRight = FMath::Min(static_cast<float>(Pillar.Position.X) + LoadingFlapPillarWidth * 0.5f, LoadingFlapRight);
			if (PillarRight <= PillarLeft)
			{
				continue;
			}
			const FLinearColor Colour = LoadingBallColor(Pillar.Type);
			const float GapTop = static_cast<float>(Pillar.Position.Y) - Pillar.Radius;
			const float GapBottom = static_cast<float>(Pillar.Position.Y) + Pillar.Radius;
			Back.Box(PillarLeft, LoadingFlapTop, PillarRight - PillarLeft, GapTop - LoadingFlapTop, Colour);
			Back.Box(PillarLeft, GapBottom, PillarRight - PillarLeft, LoadingFlapGround - GapBottom, Colour);
			Front.Box(PillarLeft - 6.0f, GapTop - 18.0f, PillarRight - PillarLeft + 12.0f, 18.0f, LoadingPaint::WithAlpha(Colour, 0.9f));
			Front.Box(PillarLeft - 6.0f, GapBottom, PillarRight - PillarLeft + 12.0f, 18.0f, LoadingPaint::WithAlpha(Colour, 0.9f));
			Front.Box(PillarLeft, LoadingFlapTop, 6.0f, GapTop - LoadingFlapTop, FLinearColor(1.0f, 1.0f, 1.0f, 0.25f));
			Front.Box(PillarLeft, GapBottom, 6.0f, LoadingFlapGround - GapBottom, FLinearColor(1.0f, 1.0f, 1.0f, 0.25f));
		}
		if (S.RespawnIn <= 0.0f)
		{
			// A round white bird: tilted with its climb, a wing up just after each flap.
			const FVector2D Bird(LoadingFlapBirdX, S.BirdY);
			const float R = LoadingFlapBirdRadius;
			const float Tilt = FMath::Clamp(S.BirdSpeed / 900.0f, -0.5f, 0.6f);
			const float SinceFlap = T - S.BirdFlapAt;
			const float Wing = SinceFlap < 0.18f ? -1.0f : FMath::Sin(T * 14.0f) * 0.3f;
			Back.Disc(Bird + FVector2D(-R * 0.9f, R * 0.1f), R * 0.45f, LoadingPaint::Ink);
			Back.Disc(Bird, R, FLinearColor(0.97f, 0.97f, 0.97f));
			Front.Disc(Bird + FVector2D(-R * 0.15f, R * (0.25f + 0.3f * Wing)), R * 0.48f, FLinearColor(0.82f, 0.84f, 0.88f));
			Front.Disc(Bird + FVector2D(R * 0.42f, -R * 0.2f + Tilt * R * 0.3f), R * 0.11f, LoadingPaint::Ink);
			Front.Disc(Bird + FVector2D(R * 0.82f, R * 0.02f + Tilt * R * 0.3f), R * 0.1f, LoadingPaint::Gold);
		}
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), LoadingFlapBirdX, LoadingFlapGround + 16.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("ボタン・クリックで羽ばたく　くぐった数 %d"), S.Score);
		break;
	}
	case EChaosImpactLoadingGame::Avoid:
	{
		// Balls raining down onto a strip of floor, each with its landing spot marked; the player steps about below.
		const float Width = LoadingAvoidRight - LoadingAvoidLeft;
		Back.Box(LoadingAvoidLeft, LoadingAvoidTop, Width, LoadingAvoidGround - LoadingAvoidTop, FLinearColor(0.0f, 0.0f, 0.0f, 0.22f));
		Back.Box(LoadingAvoidLeft, LoadingAvoidGround, Width, 6.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.55f));
		for (const FLoadingBall& Ball : S.Balls)
		{
			const float Near = FMath::Clamp(static_cast<float>(Ball.Position.Y - LoadingAvoidTop) / (LoadingAvoidGround - LoadingAvoidTop), 0.0f, 1.0f);
			Back.Box(static_cast<float>(Ball.Position.X) - Ball.Radius * Near, LoadingAvoidGround - 3.0f, Ball.Radius * 2.0f * Near, 5.0f,
				LoadingPaint::WithAlpha(LoadingPaint::Fire, 0.25f + 0.5f * Near));
			LoadingDrawBall(Back, Front, Ball.Position, Ball.Radius, Ball.Type);
		}
		const float X = S.PaddleX + (S.HitFlash > 0.0f ? 5.0f * FMath::Sin(T * 70.0f) : 0.0f);
		const float Base = LoadingAvoidGround;
		const FLinearColor Body = S.HitFlash > 0.0f && FMath::Fmod(T, 0.12f) < 0.06f ? LoadingPaint::Fire : LoadingPaint::Ice;
		Back.Line(FVector2D(X, Base - 26.0f), FVector2D(X - 9.0f, Base), Body, 7.0f);
		Back.Line(FVector2D(X, Base - 26.0f), FVector2D(X + 9.0f, Base), Body, 7.0f);
		Back.Disc(FVector2D(X, Base - 48.0f), 16.0f, Body);
		Back.Box(X - 16.0f, Base - 48.0f, 32.0f, 20.0f, Body);
		Front.Disc(FVector2D(X, Base - 70.0f), 13.0f, LoadingPaint::Paper);
		if (bAuto)
		{
			Front.Text(TEXT("AUTO"), X, Base + 14.0f, 16.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.6f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"));
		}
		Hint = FString::Printf(TEXT("←→・スティック・マウスでよける　よけた数 %d"), S.Score);
		break;
	}
	default:
		Hint = S.PaintExtraGame(Back, Front);
		break;
	}

	// Bursts (and the words some of them carry).
	const LoadingPaint::FPainter Shine{Design, OutDrawElements, LayerId + 4, Fade};
	for (const FChaosImpactLoadingState::FBurst& Each : S.Bursts)
	{
		const float Grow = LoadingPaint::EaseOut(Each.Age / LoadingBurstSeconds);
		Shine.Ring(Each.Position, 18.0f + 70.0f * Grow, LoadingPaint::WithAlpha(Each.Color, 1.0f - Grow), 5.0f * (1.0f - Grow) + 1.0f, 32);
		for (int32 Piece = 0; Piece < 8; ++Piece)
		{
			const float Angle = Piece * UE_TWO_PI / 8.0f + 0.3f;
			const FVector2D At = Each.Position + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (20.0f + 90.0f * Grow);
			Shine.Box(static_cast<float>(At.X) - 4.0f, static_cast<float>(At.Y) - 4.0f, 8.0f * (1.0f - Grow) + 2.0f,
				8.0f * (1.0f - Grow) + 2.0f, LoadingPaint::WithAlpha(Each.Color, 1.0f - Grow));
		}
		if (!Each.Label.IsEmpty())
		{
			const float Up = LoadingPaint::EaseOut(Each.Age / LoadingLabelSeconds);
			Shine.Text(Each.Label, static_cast<float>(Each.Position.X), static_cast<float>(Each.Position.Y) - 50.0f - 40.0f * Up, 30.0f,
				LoadingPaint::WithAlpha(Each.Color, 1.0f - FMath::Max(0.0f, Each.Age / LoadingLabelSeconds - 0.6f) / 0.4f),
				LoadingPaint::ETextAlign::Center, TEXT("BlackItalic"), 3.0f, LoadingPaint::Ink);
		}
	}

	// What is loading, and how far it has got.
	const LoadingPaint::FPainter Text{Design, OutDrawElements, LayerId + 5, Fade};
	const int32 Dots = FMath::FloorToInt(T * 3.0f) % 4;
	if (S.bArcade)
	{
		Text.Text(TEXT("MINI GAME"), 70.0f, 46.0f, 56.0f, LoadingPaint::Paper, LoadingPaint::ETextAlign::Left, TEXT("BlackItalic"), 3.0f,
			LoadingPaint::Ink);
		Text.Text(TEXT("Q / E ・ LB / RB 切りかえ　R / ＋ リトライ"), 74.0f, 126.0f, 22.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.85f),
			LoadingPaint::ETextAlign::Left, TEXT("Bold"), 2.0f, LoadingPaint::Ink);
	}
	else
	{
		Text.Text(FString(TEXT("LOADING")) + FString::ChrN(Dots, TEXT('.')), 70.0f, 46.0f, 56.0f, LoadingPaint::Paper,
			LoadingPaint::ETextAlign::Left, TEXT("BlackItalic"), 3.0f, LoadingPaint::Ink);
		Text.Text(S.Status, 74.0f, 126.0f, 22.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.85f), LoadingPaint::ETextAlign::Left,
			TEXT("Bold"), 2.0f, LoadingPaint::Ink);
	}
	if (S.Progress >= 0.0f && !S.bArcade)
	{
		Text.Box(74.0f, 170.0f, 440.0f, 8.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.15f));
		Text.Box(74.0f, 170.0f, 440.0f * FMath::Clamp(S.Progress, 0.0f, 1.0f), 8.0f, LoadingPaint::Gold);
		Text.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.0f * FMath::Clamp(S.Progress, 0.0f, 1.0f))), 530.0f, 160.0f,
			18.0f, LoadingPaint::Gold, LoadingPaint::ETextAlign::Left, TEXT("Bold"));
	}

	// Which game this is, and its best.
	Text.Text(S.bArcade ? FString::Printf(TEXT("%d / %d"), static_cast<int32>(S.Game) + 1, static_cast<int32>(EChaosImpactLoadingGame::Count))
		: FString(TEXT("MINI GAME")), 1530.0f, 44.0f, 16.0f, LoadingPaint::Gold, LoadingPaint::ETextAlign::Right, TEXT("BlackItalic"));
	Text.Text(LoadingGameName(S.Game), 1530.0f, 66.0f, 34.0f, LoadingPaint::Paper, LoadingPaint::ETextAlign::Right, TEXT("BlackItalic"),
		3.0f, LoadingPaint::Ink);
	if (S.Game != EChaosImpactLoadingGame::Gallery && S.Best > 0)
	{
		Text.Text(FString::Printf(TEXT("ベスト %d"), S.Best), 1530.0f, 126.0f, 20.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, 0.75f),
			LoadingPaint::ETextAlign::Right, TEXT("Bold"), 2.0f, LoadingPaint::Ink);
	}

	// A tip, on a card with its ball (the gallery is all tips already).
	if (S.Game != EChaosImpactLoadingGame::Gallery && !S.bArcade)
	{
		const FLoadingTip& Tip = LoadingTips[FMath::Clamp(S.Tip, 0, static_cast<int32>(UE_ARRAY_COUNT(LoadingTips)) - 1)];
		const float In = LoadingPaint::EaseOut(T / 0.35f);
		const FGeometry Card = LoadingPaint::MakeSkewed(Design, 300.0f + (1.0f - In) * 60.0f, 640.0f, 1000.0f, 150.0f, -0.18f);
		const LoadingPaint::FPainter CardBack{Card, OutDrawElements, LayerId + 6, Fade * In};
		const LoadingPaint::FPainter CardFront{Card, OutDrawElements, LayerId + 7, Fade * In};
		CardBack.Box(10.0f, 12.0f, 1000.0f, 150.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		CardBack.Box(0.0f, 0.0f, 1000.0f, 150.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.96f));
		CardBack.Box(0.0f, 0.0f, 10.0f, 150.0f, LoadingPaint::Gold);
		const FLinearColor BallColor = Tip.Ball == EChaosImpactBallType::Normal ? FLinearColor(0.92f, 0.94f, 1.0f)
			: Tip.Ball == EChaosImpactBallType::Simae ? FLinearColor(0.97f, 0.97f, 0.97f) : ChaosImpactBallTypes::GetColor(Tip.Ball);
		const float Bob = 4.0f * FMath::Sin(T * 3.0f);
		CardFront.Disc(FVector2D(86.0f, 75.0f + Bob), 44.0f, BallColor);
		CardFront.Disc(FVector2D(72.0f, 60.0f + Bob), 14.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.45f));
		CardFront.Text(TEXT("豆知識"), 160.0f, 18.0f, 20.0f, LoadingPaint::Gold, LoadingPaint::ETextAlign::Left, TEXT("BlackItalic"));
		const TArray<FString> TipLines = LoadingWrap(Tip.Text, 27);
		const float TipTop = TipLines.Num() > 1 ? 50.0f : 62.0f;
		for (int32 Line = 0; Line < TipLines.Num() && Line < 2; ++Line)
		{
			CardFront.Text(TipLines[Line], 160.0f, TipTop + Line * 40.0f, 23.0f, LoadingPaint::Paper, LoadingPaint::ETextAlign::Left, TEXT("Bold"));
		}
	}

	// Over the game, it can be played.
	const LoadingPaint::FPainter Hints{Design, OutDrawElements, LayerId + 8, Fade};
	if (bInteractive)
	{
		const float Pulse = 0.6f + 0.4f * FMath::Sin(T * 3.0f);
		Hints.Text(Hint, 1540.0f, 838.0f, 18.0f, LoadingPaint::WithAlpha(LoadingPaint::Paper, Pulse), LoadingPaint::ETextAlign::Right,
			TEXT("Bold"), 2.0f, LoadingPaint::Ink);
		if (S.bCancelable)
		{
			Hints.Text(S.bArcade ? TEXT("Esc / B でもどる") : TEXT("Esc / B でやめる"), 70.0f, 838.0f, 18.0f, LoadingPaint::Muted,
				LoadingPaint::ETextAlign::Left, TEXT("Bold"));
		}
	}
	return LayerId + 9;
}

// =====================================================================================================================
// Subsystem
// =====================================================================================================================

UChaosImpactLoadingSubsystem* UChaosImpactLoadingSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : Cast<UGameInstance>(WorldContext);
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactLoadingSubsystem>() : nullptr;
}

void UChaosImpactLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UChaosImpactLoadingSubsystem::HandlePostLoadMap);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UChaosImpactLoadingSubsystem::Tick));
}

void UChaosImpactLoadingSubsystem::Deinitialize()
{
	Remove();
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Super::Deinitialize();
}

EChaosImpactLoadingGame UChaosImpactLoadingSubsystem::ChooseGame()
{
	constexpr int32 Count = static_cast<int32>(EChaosImpactLoadingGame::Count);
	EChaosImpactLoadingGame Game = EChaosImpactLoadingGame::Count;
	int32 FromCommandLine = INDEX_NONE;
	if (ForcedGame != EChaosImpactLoadingGame::Count)
	{
		Game = ForcedGame;
		ForcedGame = EChaosImpactLoadingGame::Count;
	}
	else if (FParse::Value(FCommandLine::Get(), TEXT("CILoadingGame="), FromCommandLine) && FromCommandLine >= 0 && FromCommandLine < Count)
	{
		Game = static_cast<EChaosImpactLoadingGame>(FromCommandLine);
	}
	else
	{
		// Never the same one twice running.
		const int32 Pick = FMath::RandRange(0, LastGame == EChaosImpactLoadingGame::Count ? Count - 1 : Count - 2);
		Game = static_cast<EChaosImpactLoadingGame>(LastGame != EChaosImpactLoadingGame::Count && Pick >= static_cast<int32>(LastGame)
			? Pick + 1 : Pick);
	}
	LastGame = Game;
	return Game;
}

void UChaosImpactLoadingSubsystem::Show(const EChaosImpactLoadingKind Kind)
{
	Remove();
	State = MakeShared<FChaosImpactLoadingState>(Kind, ChooseGame(), static_cast<int32>(FPlatformTime::Cycles()));
	UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
	if (!Viewport)
	{
		return;
	}
	Overlay = SNew(SChaosImpactLoadingScreen).State(State).Interactive(true);
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 20000);
	OverlayViewport = Viewport;
	if (FSlateApplication::IsInitialized())
	{
		Input = MakeShared<FLoadingInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Input, 0);
	}
	ShownAt = FPlatformTime::Seconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Loading screen up: %s (%s)"), *State->Status, LoadingGameName(State->Game));
}

void UChaosImpactLoadingSubsystem::Remove()
{
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* Viewport = OverlayViewport.Get())
		{
			Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Loading screen down after %.2f s"), FPlatformTime::Seconds() - ShownAt);
	}
	Overlay.Reset();
	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	Input.Reset();
	ProgressSource = nullptr;
	CancelAction = nullptr;
	bCancelRequested = false;
	bTravelling = false;
	bWorldLoaded = false;
	bReady = false;
}

void UChaosImpactLoadingSubsystem::BeginTravel(const EChaosImpactLoadingKind Kind)
{
	Show(Kind);
	bTravelling = true;
	bWorldLoaded = false;
	// The movie player draws the same screen (carrying on with the same game) while the level itself loads, when the
	// game thread is busy; playing in the editor has none, and the screen simply waits through the load.
	if (State.IsValid() && IsMoviePlayerEnabled())
	{
		FLoadingScreenAttributes Loading;
		Loading.WidgetLoadingScreen = SNew(SChaosImpactLoadingScreen).State(State).Interactive(false);
		Loading.bAutoCompleteWhenLoadingCompletes = true;
		Loading.MinimumLoadingScreenDisplayTime = 0.0f;
		GetMoviePlayer()->SetupLoadingScreen(Loading);
	}
}

void UChaosImpactLoadingSubsystem::ShowWaiting(const EChaosImpactLoadingKind Kind, TFunction<float()> Progress, TFunction<void()> OnCancel)
{
	Show(Kind);
	ProgressSource = MoveTemp(Progress);
	CancelAction = MoveTemp(OnCancel);
	if (State.IsValid())
	{
		State->bCancelable = static_cast<bool>(CancelAction);
		State->Progress = ProgressSource ? ProgressSource() : -1.0f;
	}
}

void UChaosImpactLoadingSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (bTravelling && Overlay.IsValid())
	{
		bWorldLoaded = true;
		WorldLoadedAt = FPlatformTime::Seconds();
	}
}

void UChaosImpactLoadingSubsystem::NotifyWorldReady()
{
	// A travel waits for its new level; the level being left has nothing to say about it.
	if (!Overlay.IsValid() || !State.IsValid() || (bTravelling && !bWorldLoaded) || State->FadeStartedAt >= 0.0)
	{
		return;
	}
	// Done, but it stays up (still looking busy) until its minimum time is over: see Tick.
	bReady = true;
}

bool UChaosImpactLoadingSubsystem::HandleKey(const FKey& Key, const int32 InputDeviceId)
{
	if (!Overlay.IsValid() || !State.IsValid() || State->FadeStartedAt >= 0.0 || Key == EKeys::F11)
	{
		return false;
	}
	const FKey MenuKey = ChaosImpactSettings::ToMenuKey(Key, InputDeviceId);
	if (CancelAction && (MenuKey == EKeys::Escape || MenuKey == EKeys::Gamepad_FaceButton_Right))
	{
		bCancelRequested = true;
		return true;
	}
	const double Now = FPlatformTime::Seconds();
	FScopeLock Guard(&State->Lock);
	if (State->bArcade && (Key == EKeys::R || Key == EKeys::Gamepad_Special_Right))
	{
		State->StartGame(State->Game);
		return true;
	}
	if (State->bArcade && (Key == EKeys::Q || Key == EKeys::E || Key == EKeys::Tab
		|| Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder))
	{
		State->SwitchGame(Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder ? -1 : 1);
		LastArcadeGame = State->Game;
		return true;
	}
	if (const int32 Steer = LoadingSteerDirection(Key))
	{
		State->SetSteerKey(Steer, true, Now);
		if (State->UsesSteering())
		{
			// Steering only (not a press as well).
			return true;
		}
	}
	if (const int32 Vertical = LoadingVerticalDirection(Key); Vertical && State->UsesVertical())
	{
		State->SetVerticalKey(Vertical, true, Now);
		return true;
	}
	State->Press(Now);
	return true;
}

bool UChaosImpactLoadingSubsystem::HandleKeyUp(const FKey& Key)
{
	if (Overlay.IsValid() && State.IsValid())
	{
		if (const int32 Steer = LoadingSteerDirection(Key))
		{
			FScopeLock Guard(&State->Lock);
			State->SetSteerKey(Steer, false, FPlatformTime::Seconds());
		}
		if (const int32 Vertical = LoadingVerticalDirection(Key))
		{
			FScopeLock Guard(&State->Lock);
			State->SetVerticalKey(Vertical, false, FPlatformTime::Seconds());
		}
	}
	// Never kept from the game: a key let go meanwhile must not stay held down there.
	return false;
}

bool UChaosImpactLoadingSubsystem::HandleAnalog(const FKey& Key, const float Value)
{
	if (!Overlay.IsValid() || !State.IsValid())
	{
		return false;
	}
	if (Key == EKeys::Gamepad_LeftX)
	{
		FScopeLock Guard(&State->Lock);
		State->SetStick(Value, FPlatformTime::Seconds());
	}
	else if (Key == EKeys::Gamepad_LeftY)
	{
		FScopeLock Guard(&State->Lock);
		State->SetStickY(Value, FPlatformTime::Seconds());
	}
	return true;
}

void UChaosImpactLoadingSubsystem::ShowArcade()
{
	ForcedGame = LastArcadeGame;
	Show(EChaosImpactLoadingKind::Arcade);
	if (!State.IsValid())
	{
		return;
	}
	// Escape / B: back to where it was opened from (nothing else to do).
	CancelAction = []() {};
	FScopeLock Guard(&State->Lock);
	State->bArcade = true;
	State->bCancelable = true;
	State->Progress = -1.0f;
}

bool UChaosImpactLoadingSubsystem::Tick(const float DeltaSeconds)
{
	if (!Overlay.IsValid() || !State.IsValid())
	{
		return true;
	}
	{
		TArray<EChaosImpactSfx> Sounds;
		{
			FScopeLock Guard(&State->Lock);
			Swap(Sounds, State->PendingSounds);
		}
		for (const EChaosImpactSfx Sound : Sounds)
		{
			ChaosImpactSfx::Play2D(GetGameInstance(), Sound);
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (bCancelRequested)
	{
		bCancelRequested = false;
		if (State->bArcade)
		{
			LastArcadeGame = State->Game;
		}
		const TFunction<void()> Cancel = CancelAction;
		Remove();
		if (Cancel)
		{
			Cancel();
		}
		return true;
	}
	if (ProgressSource)
	{
		// The bar never runs ahead of the minimum time either, so it fills steadily to the end.
		const float Progress = FMath::Min(ProgressSource(), static_cast<float>((Now - ShownAt) / MinimumShowSeconds));
		FScopeLock Guard(&State->Lock);
		State->Progress = Progress;
	}
	if (bReady && State->FadeStartedAt < 0.0 && Now - ShownAt >= MinimumShowSeconds)
	{
		FScopeLock Guard(&State->Lock);
		State->Progress = State->Progress >= 0.0f ? 1.0f : State->Progress;
		State->FadeStartedAt = Now;
	}
	// Never stuck up: a level that never came, or never said it was ready.
	if (State->FadeStartedAt < 0.0 && ((bTravelling && !bWorldLoaded && Now - ShownAt > 30.0)
		|| (bTravelling && bWorldLoaded && Now - WorldLoadedAt > 6.0)))
	{
		FScopeLock Guard(&State->Lock);
		State->FadeStartedAt = Now;
	}
	if (State->FadeStartedAt >= 0.0 && Now - State->FadeStartedAt >= LoadingFadeSeconds)
	{
		Remove();
	}
	return true;
}
