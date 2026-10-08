#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPaint.h"

// Every game is plain simulation + Slate drawing. This also works on the MoviePlayer's loading thread.
namespace ChaosImpactLoadingArcadeDetails
{
	namespace P = ChaosImpactPaint;
	constexpr float Left = 400.0f, Right = 1200.0f, Top = 225.0f, Bottom = 610.0f;
	const FVector2D Center(800.0, 415.0);
	float LaneX(int32 Lane) { return 550.0f + 250.0f * Lane; }
	FVector2D CirclePoint(float Angle, float Radius) { return Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius; }
	FVector2D SnakeStep(int32 Direction)
	{
		const FVector2D Directions[] = {FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0), FVector2D(0, -1)};
		return Directions[Direction & 3];
	}
	void Orb(const P::FPainter& Back, const P::FPainter& Front, FVector2D At, float Radius, FLinearColor Color)
	{
		Back.Disc(At + FVector2D(3, 5), Radius, FLinearColor(0, 0, 0, 0.5f));
		Front.Disc(At, Radius, Color);
		Front.Disc(At + FVector2D(-Radius * 0.3, -Radius * 0.3), Radius * 0.25f, P::Paper);
	}
	void Fighter(const P::FPainter& Back, const P::FPainter& Front, FVector2D At, FLinearColor Color)
	{
		Back.Disc(At + FVector2D(0, 12), 29, P::WithAlpha(P::Ink, 0.7f));
		Front.Disc(At, 24, Color);
		Front.Disc(At + FVector2D(0, -8), 12, P::Paper);
		Front.Line(At + FVector2D(-24, 7), At + FVector2D(-34, -4), Color, 12);
		Front.Line(At + FVector2D(24, 7), At + FVector2D(34, -4), Color, 12);
	}
	void Dummy(const P::FPainter& Back, const P::FPainter& Front, FVector2D At, float Hit = 0)
	{
		Back.Disc(At + FVector2D(0, 18), 31, P::WithAlpha(P::Ink, 0.7f));
		Front.Box(At.X - 20, At.Y - 26, 40, 53, Hit > 0 ? P::Gold : P::Fire);
		Front.Disc(At - FVector2D(0, 26), 20, Hit > 0 ? P::Gold : P::Fire);
		Front.Ring(At, 12, P::Paper, 4);
		Front.Disc(At, 4, P::Paper);
	}
	void Bird(const P::FPainter& Back, const P::FPainter& Front, const FChaosImpactLoadingState::FBall& Ball, float Clock)
	{
		const FVector2D Wing(22, FMath::Sin(Clock * 24 + Ball.Position.X) * 13);
		Front.Line(Ball.Position - FVector2D(9, 0), Ball.Position - Wing, P::Paper, 7);
		Front.Line(Ball.Position + FVector2D(9, 0), Ball.Position + Wing, P::Paper, 7);
		Orb(Back, Front, Ball.Position, 14, P::Paper);
		Front.Disc(Ball.Position + FVector2D(-4, -3), 2.5f, P::Ink);
		Front.Disc(Ball.Position + FVector2D(4, -3), 2.5f, P::Ink);
		Front.Line(Ball.Position, Ball.Position + Ball.Velocity.GetSafeNormal() * 20, P::Gold, 3);
	}
	void SpecialBall(const P::FPainter& Back, const P::FPainter& Front, FVector2D At, float Radius, int32 Type, float Clock)
	{
		const FLinearColor Colors[] = {P::Fire, P::Ice, P::Paper, FLinearColor(0.4f, 0.95f, 1)};
		Orb(Back, Front, At, Radius, Colors[Type % 4]);
		if (Type == 0) { Front.Line(At + FVector2D(-Radius, 2), At + FVector2D(-Radius * 1.7f, -8), P::Gold, 5); }
		if (Type == 1)
		{
			for (int32 I = 0; I < 3; ++I)
			{
				const float Angle = I * UE_PI / 3;
				const FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));
				Front.Line(At - Axis * Radius * 0.65, At + Axis * Radius * 0.65, P::Paper, 3);
			}
		}
		if (Type == 2) { Front.Disc(At + FVector2D(Radius * 0.4f, Radius * 0.35f), Radius * 0.33f, P::WithAlpha(P::Ice, 0.35f)); }
		if (Type == 3) { Front.Ring(At, Radius + 8 + FMath::Sin(Clock * 4) * 3, P::Gold, 3); }
	}
	/** A shima-enaga: round and white, two dots of eyes and a tiny beak towards Facing, and (if asked) its long tail. */
	void Enaga(const P::FPainter& Back, const P::FPainter& Front, FVector2D At, float Radius, FVector2D Facing, bool bTail)
	{
		const FVector2D Forward = Facing.IsNearlyZero() ? FVector2D(1, 0) : Facing.GetSafeNormal();
		const FVector2D Side(-Forward.Y, Forward.X);
		if (bTail) { Back.Line(At - Forward * Radius * 0.6, At - Forward * Radius * 2.2, P::Ink, Radius * 0.35f); }
		Back.Disc(At + FVector2D(Radius * 0.12, Radius * 0.22), Radius * 1.05f, FLinearColor(0, 0, 0, 0.35f));
		Front.Disc(At, Radius, FLinearColor(0.97f, 0.97f, 0.97f));
		Front.Disc(At + Forward * Radius * 0.3 + Side * Radius * 0.34, Radius * 0.14f, P::Ink);
		Front.Disc(At + Forward * Radius * 0.3 - Side * Radius * 0.34, Radius * 0.14f, P::Ink);
		Front.Disc(At + Forward * Radius * 0.72, Radius * 0.12f, P::Gold);
	}

	// Snake: a 20 x 10 board, and where a cell is drawn.
	constexpr int32 SnakeColumns = 20, SnakeRows = 10;
	FVector2D SnakeCell(const FVector2D& Cell) { return FVector2D(410 + Cell.X * 40, 230 + Cell.Y * 36); }
	bool SnakeInside(const FVector2D& Cell) { return Cell.X >= 0 && Cell.X < SnakeColumns && Cell.Y >= 0 && Cell.Y < SnakeRows; }

	// Quest (シマエナガ大ぼうけん): the snowy field, the foe's walk along the top, and the foes one after another.
	constexpr float QuestLeft = 440.0f, QuestRight = 1160.0f, QuestTop = 315.0f, QuestBottom = 595.0f;
	constexpr float QuestFoeY = 262.0f;
	constexpr int32 QuestMaxHP = 5;
	constexpr float QuestPatternSeconds = 5.0f;
	constexpr float QuestThrowSpeed = 720.0f;
	constexpr int32 QuestSnowDamage = 12;
	constexpr int32 FoeKinds = 5;
	const TCHAR* const FoeNames[FoeKinds] = {TEXT("ゆきだるま"), TEXT("カラス"), TEXT("ふくろう"), TEXT("つららん"), TEXT("ゆきおに")};
	const TCHAR* FoeName(int32 Level) { return FoeNames[Level % FoeKinds]; }
	int32 FoeMaxHP(int32 Level) { return 40 + 15 * Level; }
}

const TCHAR* FChaosImpactLoadingState::ExtraGameName(EChaosImpactLoadingGame Which)
{
	switch (Which)
	{
	case EChaosImpactLoadingGame::Catch: return TEXT("ボール回収＆投球");
	case EChaosImpactLoadingGame::LaneRush: return TEXT("ファイアレーン");
	case EChaosImpactLoadingGame::Orbit: return TEXT("シマエナガ包囲網");
	case EChaosImpactLoadingGame::Stack: return TEXT("スノーボール育成");
	case EChaosImpactLoadingGame::Golf: return TEXT("バウンドシュート");
	case EChaosImpactLoadingGame::Fishing: return TEXT("チャージストライク");
	case EChaosImpactLoadingGame::Rhythm: return TEXT("ビーム一掃");
	case EChaosImpactLoadingGame::Parry: return TEXT("リフレクトラリー");
	case EChaosImpactLoadingGame::Memory: return TEXT("ボールオーダー");
	case EChaosImpactLoadingGame::Reaction: return TEXT("ノヴァ緊急回避");
	case EChaosImpactLoadingGame::Snake: return TEXT("シマエナガ行列");
	case EChaosImpactLoadingGame::Balance: return TEXT("ブラックホール");
	case EChaosImpactLoadingGame::Quest: return TEXT("シマエナガ大ぼうけん");
	default: return TEXT("");
	}
}

bool FChaosImpactLoadingState::UsesSteering() const
{
	return Game == EChaosImpactLoadingGame::Breakout || Game == EChaosImpactLoadingGame::Avoid
		|| Game == EChaosImpactLoadingGame::Catch || Game == EChaosImpactLoadingGame::LaneRush || Game == EChaosImpactLoadingGame::Orbit
		|| Game == EChaosImpactLoadingGame::Stack || Game == EChaosImpactLoadingGame::Memory || Game == EChaosImpactLoadingGame::Snake
		|| Game == EChaosImpactLoadingGame::Balance || Game == EChaosImpactLoadingGame::Quest;
}

bool FChaosImpactLoadingState::UsesPointerSteering() const
{
	return Game == EChaosImpactLoadingGame::Breakout || Game == EChaosImpactLoadingGame::Avoid
		|| Game == EChaosImpactLoadingGame::Catch || Game == EChaosImpactLoadingGame::LaneRush;
}

bool FChaosImpactLoadingState::UsesVertical() const
{
	return Game == EChaosImpactLoadingGame::Snake || Game == EChaosImpactLoadingGame::Balance || Game == EChaosImpactLoadingGame::Quest;
}

FVector2D FChaosImpactLoadingState::SteerVector() const
{
	const FVector2D Direction(FMath::Clamp(static_cast<float>(SteerRight - SteerLeft) + StickX, -1.0f, 1.0f),
		FMath::Clamp(static_cast<float>(SteerDown - SteerUp) + StickY, -1.0f, 1.0f));
	return Direction.Size() > 1.0 ? Direction.GetSafeNormal() : Direction;
}

void FChaosImpactLoadingState::StartExtraGame()
{
	using namespace ChaosImpactLoadingArcadeDetails;
	GameClock = ActionFlash = SafeTime = RoundTimer = Marker = 0.0f;
	ReactionCueAt = -1.0;
	Width = 220.0f;
	Phase = Combo = Inventory = Selection = SequenceIndex = MoveDirection = PendingDirection = 0;
	MiniPlayer = FVector2D(800, 560);
	MiniGoal = FVector2D(1120, 460);
	Trail.Reset();
	Sequence.Reset();
	switch (Game)
	{
	case EChaosImpactLoadingGame::Catch: MiniGoal = FVector2D(800, 265); break;
	case EChaosImpactLoadingGame::LaneRush: Selection = 1; break;
	case EChaosImpactLoadingGame::Orbit:
		Marker = UE_HALF_PI; MoveDirection = 1; MiniPlayer = CirclePoint(Marker, 145); SpawnIn = 1.0f; break;
	case EChaosImpactLoadingGame::Stack:
		MiniPlayer = FVector2D(600, 460); MiniGoal = FVector2D(1100, 415); Width = 18; MoveDirection = 1; break;
	case EChaosImpactLoadingGame::Golf:
		MiniPlayer = FVector2D(460, 540); MiniGoal = FVector2D(1120, 430); break;
	case EChaosImpactLoadingGame::Fishing: MiniPlayer = FVector2D(470, 380); MiniGoal = FVector2D(1110, 380); Width = 80; break;
	case EChaosImpactLoadingGame::Memory: NewMemoryRound(); break;
	case EChaosImpactLoadingGame::Reaction: RoundTimer = Random.FRandRange(1.0f, 2.5f); MiniPlayer = Center; break;
	case EChaosImpactLoadingGame::Snake:
		Trail = {FVector2D(6, 5), FVector2D(5, 5), FVector2D(4, 5)};
		MiniGoal = FVector2D(12, 5); SpawnIn = 0.4f; break;
	case EChaosImpactLoadingGame::Balance: MiniPlayer = FVector2D(520, 300); MiniGoal = FVector2D(1060, 520); break;
	case EChaosImpactLoadingGame::Quest:
		QuestHP = QuestMaxHP; MiniPlayer = FVector2D(800, QuestBottom - 50); NewFoe(0);
		break;
	default: break;
	}
}

void FChaosImpactLoadingState::MiniMiss(const FVector2D& At)
{
	using namespace ChaosImpactLoadingArcadeDetails;
	QueueSound(EChaosImpactSfx::MiniMiss);
	Burst(At, P::Fire, TEXT("もう一回！"));
	HitFlash = 0.4f;
	Score = Combo = 0;
}

void FChaosImpactLoadingState::TurnExtraGame(int32 Direction)
{
	if (Game == EChaosImpactLoadingGame::Stack && Cooldown <= 0 && Direction != MoveDirection)
	{
		// Like carrying a snowball in the main game: alternating movement grows it, holding a key doesn't.
		MoveDirection = Direction; Width = FMath::Min(72.0f, Width + 4); MiniPlayer.X = 600 + Direction * 25;
	}
	if (Game == EChaosImpactLoadingGame::Memory) { Selection = (Selection + Direction + 4) % 4; }
	if (Game == EChaosImpactLoadingGame::LaneRush) { Selection = FMath::Clamp(Selection + Direction, 0, 2); }
	if (Game == EChaosImpactLoadingGame::Snake)
	{
		// Right 0, left 2: never straight back onto its own line.
		const int32 Want = Direction > 0 ? 0 : 2;
		if (Want != (MoveDirection + 2) % 4) { PendingDirection = Want; }
	}
}

void FChaosImpactLoadingState::TurnVertical(int32 Direction)
{
	if (Game == EChaosImpactLoadingGame::Snake)
	{
		// Down 1, up 3.
		const int32 Want = Direction > 0 ? 1 : 3;
		if (Want != (MoveDirection + 2) % 4) { PendingDirection = Want; }
	}
}

void FChaosImpactLoadingState::NewMemoryRound()
{
	Sequence.Add(Random.RandRange(0, 3));
	SequenceIndex = 0;
	Phase = 0;
	RoundTimer = 0.0f;
}

void FChaosImpactLoadingState::NewSnakeFood()
{
	using namespace ChaosImpactLoadingArcadeDetails;
	// Bounded selection, including the full-board case: never an unbounded loading-thread loop.
	TArray<FVector2D> Empty;
	for (int32 Y = 0; Y < SnakeRows; ++Y)
		for (int32 X = 0; X < SnakeColumns; ++X)
			if (!Trail.Contains(FVector2D(X, Y))) { Empty.Add(FVector2D(X, Y)); }
	if (!Empty.IsEmpty()) { MiniGoal = Empty[Random.RandRange(0, Empty.Num() - 1)]; }
	else
	{
		Burst(Center, P::Gold, TEXT("クリア！"));
		Trail = {FVector2D(6, 5), FVector2D(5, 5), FVector2D(4, 5)}; MoveDirection = PendingDirection = 0; MiniGoal = FVector2D(12, 5);
	}
}

void FChaosImpactLoadingState::PressExtraGame()
{
	using namespace ChaosImpactLoadingArcadeDetails;
	if (Cooldown > 0.0f) { return; }
	switch (Game)
	{
	case EChaosImpactLoadingGame::Catch:
		// Straight up from where the player stands: line up under the target first.
		if (Inventory > 0)
		{
			FBall Shot; Shot.Position = FVector2D(PaddleX, 530); Shot.Velocity = FVector2D(0, -950);
			Shot.Radius = 14; Shots.Add(Shot); --Inventory; Cooldown = 0.3f; ActionFlash = 0.25f;
		}
		break;
	case EChaosImpactLoadingGame::LaneRush:
		break;
	case EChaosImpactLoadingGame::Orbit:
		// A dash round the ring the way it was going (untouchable while it lasts).
		ActionFlash = SafeTime = 0.25f; Cooldown = 0.9f; break;
	case EChaosImpactLoadingGame::Stack:
	{
		if (Width < 30) { MiniMiss(MiniPlayer); Width = 18; Cooldown = 0.45f; break; }
		FBall Shot; Shot.Position = MiniPlayer - FVector2D(0, 40); Shot.Radius = Width;
		Shot.Velocity = (MiniGoal - Shot.Position).GetSafeNormal() * 850; Shots.Add(Shot);
		Width = 18; Cooldown = 0.65f; ActionFlash = 0.4f;
		break;
	}
	case EChaosImpactLoadingGame::Golf:
		if (Phase == 0)
		{
			FBall Ball; Ball.Position = MiniPlayer; Ball.Radius = 12.0f;
			Ball.Velocity = FVector2D(FMath::Cos(Marker), FMath::Sin(Marker)) * 920.0;
			Balls.Add(Ball); Phase = 1; ActionFlash = 0.25f;
		}
		break;
	case EChaosImpactLoadingGame::Fishing:
	{
		if (Phase != 0) { break; }
		const float Gap = FMath::Abs(Marker - 900);
		FBall Shot; Shot.Position = MiniPlayer; Shot.Type = Gap < Width ? (Gap < 20 ? 2 : 1) : 0;
		Shot.Velocity = FVector2D(850, Shot.Type > 0 ? 0 : -210); Shot.Radius = 12 + Shot.Type * 7;
		Balls.Add(Shot); Phase = 1; Cooldown = 0.3f; ActionFlash = 0.3f;
		break;
	}
	case EChaosImpactLoadingGame::Rhythm:
	case EChaosImpactLoadingGame::Parry:
	{
		const float Line = Game == EChaosImpactLoadingGame::Rhythm ? 560.0f : 650.0f;
		int32 Closest = INDEX_NONE; float Distance = 10000;
		for (int32 I = 0; I < Balls.Num(); ++I)
		{
			const float Gap = FMath::Abs(static_cast<float>(Balls[I].Position.X) - Line);
			if (Gap < Distance) { Distance = Gap; Closest = I; }
		}
		if (Closest != INDEX_NONE && Distance < 65.0f)
		{
			const bool Perfect = Distance < 22.0f;
			++Combo;
			if (Game == EChaosImpactLoadingGame::Rhythm)
			{
				// Targets in one row are pierced by a single beam, not three separate throws.
				const int32 Wave = Balls[Closest].Type;
				int32 Knockouts = 0;
				for (int32 I = Balls.Num() - 1; I >= 0; --I)
				{
					if (Balls[I].Type == Wave)
					{
						++Knockouts; AddScore(Perfect ? 3 : 1); Burst(Balls[I].Position, P::Violet); Balls.RemoveAt(I);
					}
				}
				Burst(FVector2D(800, 350), P::Violet, FString::Printf(TEXT("%d体 貫通KO"), Knockouts));
			}
			else
			{
				AddScore(Perfect ? 3 : 1);
				Burst(Balls[Closest].Position, Perfect ? P::Gold : P::Ice, Perfect ? TEXT("PERFECT!") : TEXT("GOOD"));
				FBall Reflected = Balls[Closest]; Reflected.Velocity.X = 1300; Shots.Add(Reflected);
				Balls.RemoveAt(Closest);
			}
		}
		else { Combo = 0; Burst(FVector2D(Line, 420), P::Muted, TEXT("はやい！")); }
		ActionFlash = 0.2f; Cooldown = 0.2f;
		break;
	}
	case EChaosImpactLoadingGame::Memory:
		if (Phase != 1 || !Sequence.IsValidIndex(SequenceIndex)) { break; }
		if (Selection == Sequence[SequenceIndex])
		{
			Burst(FVector2D(530 + Selection * 180, 420), P::PlayerAccents[Selection]);
			++SequenceIndex; AddScore(1);
			if (SequenceIndex == Sequence.Num())
			{
				Burst(Center, P::Gold, TEXT("正解！"));
				if (Sequence.Num() >= 8) { Sequence.Reset(); }
				NewMemoryRound();
			}
		}
		else { MiniMiss(Center); Sequence.Reset(); NewMemoryRound(); }
		Cooldown = 0.25f;
		break;
	case EChaosImpactLoadingGame::Reaction:
		if (Phase == 0)
		{
			MiniMiss(Center); Phase = 2; RoundTimer = 0.8f; Cooldown = 0.3f;
		}
		else if (Phase == 1)
		{
			// Real elapsed time, not clamped physics time: a slow frame must not flatter the reaction score.
			const double ResponseSeconds = ReactionCueAt >= 0 ? FMath::Max(LastPressAt, LastStepAt) - ReactionCueAt : RoundTimer;
			const int32 Milliseconds = FMath::Max(0, FMath::RoundToInt(ResponseSeconds * 1000.0));
			AddScore(FMath::Max(1, 1000 - Milliseconds));
			Burst(Center, P::Gold, FString::Printf(TEXT("%d ms"), Milliseconds));
			MiniPlayer = Center + FVector2D(240, 0); ActionFlash = 0.5f;
			Phase = 2; RoundTimer = 0.9f;
		}
		break;
	case EChaosImpactLoadingGame::Snake:
		break;
	case EChaosImpactLoadingGame::Balance:
		// A dash that breaks free of the pull for a moment.
		SafeTime = ActionFlash = 0.28f; Cooldown = 1.0f; break;
	case EChaosImpactLoadingGame::Quest:
		PressQuest(); break;
	default: break;
	}
}

void FChaosImpactLoadingState::StepExtraGame(float Delta, bool bAuto)
{
	using namespace ChaosImpactLoadingArcadeDetails;
	GameClock += Delta;
	Cooldown = FMath::Max(0.0f, Cooldown - Delta);
	HitFlash = FMath::Max(0.0f, HitFlash - Delta);
	ActionFlash = FMath::Max(0.0f, ActionFlash - Delta);
	SafeTime = FMath::Max(0.0f, SafeTime - Delta);
	SpawnIn -= Delta;
	switch (Game)
	{
	case EChaosImpactLoadingGame::Catch:
	{
		// The target walks to and fro (quicker as the score goes up); a throw goes straight up, so line up under it.
		const float Pace = 0.9f + FMath::Min(Score * 0.02f, 0.8f);
		MiniGoal = FVector2D(800 + 260 * FMath::Sin(GameClock * Pace), 265);
		const float AheadX = 800 + 260 * FMath::Sin((GameClock + (530 - 265) / 950.0f) * Pace);
		float AutoX = AheadX;
		if (Inventory < 2)
		{
			for (const FBall& Ball : Balls) { if (Ball.Type == 0 && Ball.Position.Y > 300) { AutoX = Ball.Position.X; break; } }
		}
		PaddleX = Steer(PaddleX, 820, Delta, bAuto, AutoX, Left + 45, Right - 45);
		if (bAuto && Inventory > 0 && Cooldown <= 0 && FMath::Abs(PaddleX - AheadX) < 18) { PressExtraGame(); }
		if (SpawnIn <= 0)
		{
			FBall Ball; Ball.Position = FVector2D(Random.FRandRange(450, 1150), Top);
			Ball.Type = Random.FRand() < 0.25f ? 1 : 0; Ball.Radius = 18;
			Ball.Velocity.Y = 250 + FMath::Min(Score * 4.0f, 240.0f); Balls.Add(Ball); SpawnIn = 0.5f;
		}
		for (int32 I = Balls.Num() - 1; I >= 0; --I)
		{
			FBall& Ball = Balls[I]; Ball.Position += Ball.Velocity * Delta;
			if (Ball.Position.Y >= 560)
			{
				if (FMath::Abs(Ball.Position.X - PaddleX) < 60)
				{
					if (Ball.Type == 0 && Inventory < 2) { ++Inventory; AddScore(1); Burst(Ball.Position, P::Paper, TEXT("拾った！")); }
					else if (Ball.Type == 1 && SafeTime <= 0) { MiniMiss(Ball.Position); Inventory = 0; SafeTime = 0.8f; }
				}
				else if (Ball.Type == 0) { Combo = 0; }
				Balls.RemoveAt(I);
			}
		}
		for (int32 I = Shots.Num() - 1; I >= 0; --I)
		{
			FBall& Shot = Shots[I]; Shot.Position += Shot.Velocity * Delta; Shot.Age += Delta;
			if (FMath::Abs(Shot.Position.X - MiniGoal.X) < 34 && FMath::Abs(Shot.Position.Y - MiniGoal.Y) < 30)
			{
				AddScore(2); Burst(MiniGoal, P::Gold, TEXT("KO +2")); Shots.RemoveAt(I);
			}
			else if (Shot.Position.Y < Top) { Burst(FVector2D(Shot.Position.X, Top + 30), P::Muted, TEXT("はずれ")); Shots.RemoveAt(I); }
		}
		break;
	}
	case EChaosImpactLoadingGame::LaneRush:
	{
		if (!bAuto && PointerX >= 0 && LastStepAt - PointerMovedAt < 2) { Selection = FMath::Clamp(FMath::RoundToInt((PointerX - 550) / 250), 0, 2); }
		if (SpawnIn <= 0)
		{
			const int32 SafeLane = Random.RandRange(0, 2);
			for (int32 L = 0; L < 3; ++L)
			{
				FBall Ball; Ball.Position = FVector2D(LaneX(L), Top); Ball.Type = L == SafeLane ? 0 : 1;
				Ball.Velocity.Y = 220 + FMath::Min(Score * 5.0f, 170.0f); Ball.Radius = 26; Balls.Add(Ball);
			}
			SpawnIn = 1.05f;
		}
		if (bAuto)
		{
			for (const FBall& Ball : Balls) { if (Ball.Type == 0 && Ball.Position.Y > 330) { Selection = FMath::RoundToInt((Ball.Position.X - 550) / 250); break; } }
		}
		PaddleX += (LaneX(Selection) - PaddleX) * FMath::Min(1.0f, Delta * 18);
		for (int32 I = Balls.Num() - 1; I >= 0; --I)
		{
			FBall& Ball = Balls[I]; Ball.Position += Ball.Velocity * Delta;
			if (Ball.Position.Y > 545 && !Ball.bCounted)
			{
				Ball.bCounted = true;
				if (FMath::Abs(Ball.Position.X - PaddleX) < 70)
				{
					if (Ball.Type == 0) { AddScore(2); Burst(Ball.Position, P::Gold, TEXT("+2")); }
					else if (SafeTime <= 0) { MiniMiss(Ball.Position); SafeTime = 0.5f; }
				}
			}
			if (Ball.Position.Y > Bottom + 30) { Balls.RemoveAt(I); }
		}
		break;
	}
	case EChaosImpactLoadingGame::Orbit:
	{
		// Left and right take the player round the ring; each bird flies straight at where the player was as it set off.
		float Input = FMath::Clamp(static_cast<float>(SteerRight - SteerLeft) + StickX, -1.0f, 1.0f);
		if (bAuto)
		{
			// Off the line of the bird arriving soonest.
			Input = 0.0f;
			float Soonest = 10.0f;
			for (const FBall& Ball : Balls)
			{
				const double Speed2 = Ball.Velocity.SizeSquared();
				if (Speed2 < 1.0) { continue; }
				const float TimeTo = static_cast<float>(FVector2D::DotProduct(MiniPlayer - Ball.Position, Ball.Velocity) / Speed2);
				const FVector2D Closest = Ball.Position + Ball.Velocity * TimeTo;
				if (TimeTo > 0.0f && TimeTo < Soonest && FVector2D::Distance(Closest, MiniPlayer) < 60.0)
				{
					Soonest = TimeTo;
					const FVector2D Tangent(-FMath::Sin(Marker), FMath::Cos(Marker));
					const float Side = static_cast<float>(FVector2D::DotProduct(Tangent, MiniPlayer - Closest));
					Input = FMath::Abs(Side) > 4.0f ? FMath::Sign(Side) : static_cast<float>(MoveDirection);
				}
			}
			if (Soonest < 0.3f && Cooldown <= 0) { PressExtraGame(); }
		}
		if (Input != 0.0f) { MoveDirection = Input > 0.0f ? 1 : -1; }
		Marker += (ActionFlash > 0 ? MoveDirection * 6.5f : Input * 2.6f) * Delta;
		MiniPlayer = CirclePoint(Marker, 145);
		if (SpawnIn <= 0 && Balls.Num() < 24)
		{
			FBall Ball; const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
			Ball.Position = Center + FVector2D(FMath::Cos(Angle) * 390, FMath::Sin(Angle) * 185);
			Ball.Velocity = (MiniPlayer - Ball.Position).GetSafeNormal() * (240 + FMath::Min(Score * 4.0f, 200.0f));
			Ball.Radius = 14; Balls.Add(Ball);
			SpawnIn = FMath::Max(0.45f, 1.2f - Score * 0.03f);
		}
		for (int32 I = Balls.Num() - 1; I >= 0; --I)
		{
			FBall& Ball = Balls[I]; Ball.Age += Delta; Ball.Position += Ball.Velocity * Delta;
			if (FVector2D::Distance(Ball.Position, MiniPlayer) < 30 && SafeTime <= 0) { MiniMiss(MiniPlayer); SafeTime = 0.8f; Balls.RemoveAt(I); continue; }
			const bool bOut = Ball.Position.X < Left - 30 || Ball.Position.X > Right + 30 || Ball.Position.Y < Top - 30 || Ball.Position.Y > Bottom + 30;
			// Flown right across and out: dodged.
			if (Ball.Age > 0.5f && bOut) { AddScore(1); Balls.RemoveAt(I); }
			else if (Ball.Age > 6.0f) { Balls.RemoveAt(I); }
		}
		break;
	}
	case EChaosImpactLoadingGame::Stack:
		RoundTimer += Delta;
		if (bAuto && RoundTimer > 0.075f) { TurnExtraGame(-MoveDirection); RoundTimer = 0; }
		if (bAuto && Width >= 54) { PressExtraGame(); }
		for (int32 I = Shots.Num() - 1; I >= 0; --I)
		{
			Shots[I].Position += Shots[I].Velocity * Delta;
			if (FVector2D::Distance(Shots[I].Position, MiniGoal) < 35)
			{
				const int32 Points = Shots[I].Radius >= 54 ? 2 : 1;
				AddScore(Points); Burst(MiniGoal, P::Paper, Points == 2 ? TEXT("巨大スノー KO +2") : TEXT("HIT +1"));
				Shots.RemoveAt(I);
			}
		}
		break;
	case EChaosImpactLoadingGame::Golf:
	{
		Marker = -0.75f + FMath::Sin(GameClock * 1.8f) * 0.45f;
		if (Phase == 0 && bAuto)
		{
			// Aim at the target mirrored across the upper wall: one real wall bounce is required.
			const FVector2D Aim = (FVector2D(MiniGoal.X, 2 * (Top + 15) - MiniGoal.Y) - MiniPlayer).GetSafeNormal();
			if (FMath::Abs(Marker - FMath::Atan2(Aim.Y, Aim.X)) < 0.025) { PressExtraGame(); }
		}
		if (Phase == 1 && !Balls.IsEmpty())
		{
			FBall& Ball = Balls[0]; Ball.Position += Ball.Velocity * Delta;
			const float Speed = FMath::Max(0.0f, static_cast<float>(Ball.Velocity.Size()) - 420 * Delta);
			Ball.Velocity = Ball.Velocity.GetSafeNormal() * Speed;
			if (Ball.Position.Y < Top + 15 || Ball.Position.Y > Bottom - 15) { Ball.Position.Y = FMath::Clamp(Ball.Position.Y, double(Top + 15), double(Bottom - 15)); Ball.Velocity.Y *= -1; Ball.bCounted = true; }
			if (FVector2D::Distance(Ball.Position, MiniGoal) < 30)
			{
				if (Ball.bCounted) { AddScore(3); Burst(MiniGoal, P::Gold, TEXT("壁反射 KO +3")); }
				else { Burst(MiniGoal, P::Muted, TEXT("壁に当てよう！")); }
				Phase = 2; RoundTimer = 0.8f; Balls.Reset();
			}
			else if (Speed < 25 || Ball.Position.X > Right + 30)
			{
				Burst(Ball.Position, P::Muted, TEXT("おしい！")); Phase = 2; RoundTimer = 0.8f; Balls.Reset();
			}
		}
		if (Phase == 2)
		{
			RoundTimer -= Delta;
			if (RoundTimer <= 0) { Phase = 0; MiniGoal = FVector2D(Random.FRandRange(1000, 1140), Random.FRandRange(390, 530)); }
		}
		break;
	}
	case EChaosImpactLoadingGame::Fishing:
		Marker = 800 + 340 * FMath::Sin(GameClock * (2.4f + FMath::Min(Score * 0.025f, 1.0f)));
		if (bAuto && FMath::Abs(Marker - 900) < Width * 0.5f) { PressExtraGame(); }
		for (int32 I = Balls.Num() - 1; I >= 0; --I)
		{
			Balls[I].Position += Balls[I].Velocity * Delta;
			if (FVector2D::Distance(Balls[I].Position, MiniGoal) < 35)
			{
				++Combo; AddScore(Balls[I].Type == 2 ? 3 : 2);
				Burst(MiniGoal, P::Gold, Balls[I].Type == 2 ? TEXT("フルチャージ KO +3") : TEXT("KO +2"));
				Width = FMath::Max(36.0f, 80.0f - Score * 1.2f); Balls.RemoveAt(I); Phase = 2; RoundTimer = 0.35f;
			}
			else if (Balls[I].Position.X > Right || Balls[I].Position.Y < Top)
			{
				MiniMiss(MiniGoal); Width = 80; Balls.RemoveAt(I); Phase = 2; RoundTimer = 0.35f;
			}
		}
		if (Phase == 2) { RoundTimer -= Delta; if (RoundTimer <= 0) { Phase = 0; } }
		break;
	case EChaosImpactLoadingGame::Rhythm:
	case EChaosImpactLoadingGame::Parry:
	{
		const float Line = Game == EChaosImpactLoadingGame::Rhythm ? 560 : 650;
		if (SpawnIn <= 0)
		{
			const bool Beam = Game == EChaosImpactLoadingGame::Rhythm;
			for (int32 I = 0; I < (Beam ? 3 : 1); ++I)
			{
				FBall Ball; Ball.Position = FVector2D(Beam ? Right - 90 + I * 45 : Right, 425); Ball.Radius = 22;
				Ball.Type = Selection;
				Ball.Velocity.X = -(Beam ? 440 : 560 + FMath::Min(Score * 6.0f, 300.0f)); Balls.Add(Ball);
			}
			++Selection; SpawnIn = Beam ? 1.1f : Random.FRandRange(0.55f, 1.0f);
		}
		for (FBall& Ball : Balls) { Ball.Position += Ball.Velocity * Delta; }
		if (bAuto)
		{
			for (const FBall& Ball : Balls) { if (FMath::Abs(Ball.Position.X - Line) < 18) { PressExtraGame(); break; } }
		}
		for (int32 I = Balls.Num() - 1; I >= 0; --I)
		{
			if (Balls[I].Position.X < Line - 70) { MiniMiss(FVector2D(Line, 425)); Balls.RemoveAt(I); }
		}
		for (FBall& Shot : Shots) { Shot.Position += Shot.Velocity * Delta; }
		for (int32 I = Shots.Num() - 1; I >= 0; --I)
		{
			if (Shots[I].Position.X > 1120) { AddScore(2); Burst(FVector2D(1120, 425), P::Gold, TEXT("反撃 KO +2")); Shots.RemoveAt(I); }
		}
		break;
	}
	case EChaosImpactLoadingGame::Memory:
		RoundTimer += Delta;
		if (Phase == 0 && RoundTimer >= Sequence.Num() * 0.65f + 0.35f) { Phase = 1; RoundTimer = 0; }
		if (Phase == 1 && bAuto && RoundTimer > 0.38f && Sequence.IsValidIndex(SequenceIndex))
		{
			Selection = Sequence[SequenceIndex]; PressExtraGame(); RoundTimer = 0;
		}
		break;
	case EChaosImpactLoadingGame::Reaction:
		if (Phase == 0 || Phase == 2)
		{
			RoundTimer -= Delta;
			if (RoundTimer <= 0)
			{
				if (Phase == 0) { Phase = 1; RoundTimer = 0; ReactionCueAt = LastStepAt; }
				else { Phase = 0; MiniPlayer = Center; RoundTimer = Random.FRandRange(1, 2.5f); }
			}
		}
		else
		{
			RoundTimer = static_cast<float>(LastStepAt - ReactionCueAt);
			if (bAuto && RoundTimer >= 0.22f) { PressExtraGame(); }
			else if (RoundTimer > 0.75f) { MiniMiss(Center); Phase = 2; RoundTimer = 0.9f; }
		}
		break;
	case EChaosImpactLoadingGame::Snake:
	{
		// A shima-enaga leading its young: each snowball eaten adds one to the line; the wall or the line itself ends it.
		if (SpawnIn > 0 || Trail.IsEmpty()) { break; }
		const auto Blocked = [this](const FVector2D& Cell)
		{
			// The last of the line moves on out of the way.
			const int32 At = Trail.IndexOfByKey(Cell);
			return !SnakeInside(Cell) || (At != INDEX_NONE && At < Trail.Num() - 1);
		};
		if (bAuto)
		{
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Turn : {0, -1, 1})
			{
				const int32 Direction = (MoveDirection + Turn + 4) % 4;
				const FVector2D Ahead = Trail[0] + SnakeStep(Direction);
				if (Blocked(Ahead)) { continue; }
				const float Distance = FVector2D::DistSquared(Ahead, MiniGoal);
				if (Distance < BestDistance) { BestDistance = Distance; PendingDirection = Direction; }
			}
		}
		MoveDirection = PendingDirection;
		const FVector2D Next = Trail[0] + SnakeStep(MoveDirection);
		if (Blocked(Next))
		{
			MiniMiss(SnakeCell(Trail[0]));
			Trail = {FVector2D(6, 5), FVector2D(5, 5), FVector2D(4, 5)};
			MoveDirection = PendingDirection = 0;
			NewSnakeFood();
			SpawnIn = 0.8f;
			break;
		}
		const bool Ate = Next == MiniGoal;
		Trail.Insert(Next, 0);
		if (Ate) { AddScore(1); NewSnakeFood(); Burst(SnakeCell(Next), P::Gold, TEXT("+1")); }
		else { Trail.Pop(EAllowShrinking::No); }
		SpawnIn = FMath::Max(0.08f, 0.16f - Trail.Num() * 0.002f);
		break;
	}
	case EChaosImpactLoadingGame::Balance:
	{
		// Pulled towards the black hole (the harder the nearer): move about anywhere to pick up the balls; a dash breaks free.
		const FVector2D ToHole = Center - MiniPlayer;
		const float HoleDistance = FMath::Max(1.0f, static_cast<float>(ToHole.Size()));
		FVector2D Input = SteerVector();
		if (bAuto)
		{
			Input = (MiniGoal - MiniPlayer).GetSafeNormal();
			if (HoleDistance < 160.0f) { Input = (Input - ToHole / HoleDistance * 1.6).GetSafeNormal(); }
			if (HoleDistance < 100.0f && Cooldown <= 0) { PressExtraGame(); }
		}
		const bool bDashing = ActionFlash > 0;
		if (bDashing && Input.IsNearlyZero()) { Input = -ToHole / HoleDistance; }
		const float Pull = SafeTime > 0 ? 0.0f : FMath::Clamp(26000.0f / HoleDistance, 50.0f, 280.0f) * (1.0f + FMath::Min(Score * 0.03f, 0.6f));
		MiniPlayer += Input * (bDashing ? 780.0f : 300.0f) * Delta + ToHole / HoleDistance * Pull * Delta;
		MiniPlayer.X = FMath::Clamp(MiniPlayer.X, double(Left + 22), double(Right - 22));
		MiniPlayer.Y = FMath::Clamp(MiniPlayer.Y, double(Top + 22), double(Bottom - 22));
		if (FVector2D::Distance(MiniPlayer, Center) < 50 && SafeTime <= 0)
		{
			MiniMiss(MiniPlayer); MiniPlayer = FVector2D(Left + 60, Top + 60); SafeTime = 1.0f;
		}
		if (FVector2D::Distance(MiniPlayer, MiniGoal) < 34)
		{
			AddScore(1); Burst(MiniGoal, P::Gold, TEXT("+1"));
			for (int32 Try = 0; Try < 8; ++Try)
			{
				MiniGoal = FVector2D(Random.FRandRange(Left + 50, Right - 50), Random.FRandRange(Top + 40, Bottom - 40));
				if (FVector2D::Distance(MiniGoal, Center) > 140 && FVector2D::Distance(MiniGoal, MiniPlayer) > 160) { break; }
			}
		}
		break;
	}
	case EChaosImpactLoadingGame::Quest:
		StepQuest(Delta, bAuto);
		break;
	default: break;
	}
}

FString FChaosImpactLoadingState::PaintExtraGame(const ChaosImpactPaint::FPainter& Back, const ChaosImpactPaint::FPainter& Front) const
{
	using namespace ChaosImpactLoadingArcadeDetails;
	Back.Box(370, 205, 860, 425, FLinearColor(0, 0, 0, 0.35f));
	Back.Outline(370, 205, 860, 425, P::WithAlpha(P::Paper, 0.16f), 2);
	Front.Text(FString::FromInt(Score), 1200, 230, 46, P::Gold, P::ETextAlign::Right, TEXT("BlackItalic"));
	const auto Player = [&](FVector2D At)
	{
		Fighter(Back, Front, At, HitFlash > 0 ? P::Fire : P::Ice);
		if (ActionFlash > 0) { Front.Ring(At, 36 + 20 * ActionFlash, P::Gold, 4); }
	};
	const auto Obstacles = [&]()
	{
		for (const FBall& Ball : Balls)
		{
			Orb(Back, Front, Ball.Position, Ball.Radius, Ball.Type == 0 ? P::Paper : P::Fire);
			if (Ball.Type == 1)
			{
				Front.Line(Ball.Position, Ball.Position - Ball.Velocity.GetSafeNormal() * 40, P::Gold, 6);
			}
		}
	};
	switch (Game)
	{
	case EChaosImpactLoadingGame::Catch:
		Obstacles();
		Dummy(Back, Front, MiniGoal);
		// Where a throw would go.
		Back.Line(FVector2D(PaddleX, 520), FVector2D(PaddleX, Top + 10), P::WithAlpha(P::Paper, Inventory > 0 ? 0.2f : 0.06f), 3);
		Player(FVector2D(PaddleX, 565));
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FVector2D At(PaddleX + (Slot == 0 ? -44 : 44), 550);
			if (Slot < Inventory) { Orb(Back, Front, At, 14, P::Paper); }
			else { Back.Ring(At, 14, P::Muted, 2); }
		}
		for (const FBall& Shot : Shots) { Orb(Back, Front, Shot.Position, 14, P::Ice); }
		return TEXT("←→で白い球を拾う　ボタンで真上に投げてマトに当てろ！　赤い球はよける");
	case EChaosImpactLoadingGame::LaneRush:
		for (int32 L = 0; L < 3; ++L) { Back.Box(LaneX(L) - 105, Top, 210, 385, P::WithAlpha(P::Ice, L == Selection ? 0.13f : 0.04f)); }
		Obstacles(); Player(FVector2D(PaddleX, 560));
		return TEXT("←→で白いボールのレーンへ！　赤いファイア球はよける");
	case EChaosImpactLoadingGame::Orbit:
		Back.Ring(Center, 145, P::WithAlpha(P::Ice, 0.45f), 7);
		Back.Disc(Center, 75, P::WithAlpha(P::Paper, 0.12f));
		for (const FBall& Ball : Balls) { Bird(Back, Front, Ball, GameClock); }
		Player(MiniPlayer);
		return TEXT("←→でリングを回ってシマエナガをよける　ボタンでダッシュ（無敵）");
	case EChaosImpactLoadingGame::Stack:
		Player(MiniPlayer);
		SpecialBall(Back, Front, MiniPlayer - FVector2D(0, 48 + Width * 0.5f), Width, 2, GameClock);
		Dummy(Back, Front, MiniGoal);
		for (const FBall& Shot : Shots) { SpecialBall(Back, Front, Shot.Position, Shot.Radius, 2, GameClock); }
		Back.Box(485, 550, 230, 12, P::Muted);
		Front.Box(485, 550, (Width - 18) / 54 * 230, 12, P::Ice);
		Front.Text(TEXT("← →"), 600, 485, 34, P::Paper, P::ETextAlign::Center);
		return TEXT("←→を交互に連打してスノーを育てる　ボタンで投げてKO！");
	case EChaosImpactLoadingGame::Golf:
		Back.Line(FVector2D(Left, Top + 1), FVector2D(Right, Top + 1), P::Ice, 14);
		Back.Line(FVector2D(Left, Bottom), FVector2D(Right, Bottom), P::Muted, 14);
		Dummy(Back, Front, MiniGoal);
		Fighter(Back, Front, MiniPlayer + FVector2D(-28, 0), P::Ice);
		if (Phase == 0)
		{
			Orb(Back, Front, MiniPlayer, 12, P::Paper);
			Front.Line(MiniPlayer, MiniPlayer + FVector2D(FMath::Cos(Marker), FMath::Sin(Marker)) * 135, P::Gold, 5);
		}
		for (const FBall& Ball : Balls) { Orb(Back, Front, Ball.Position, 12, P::Paper); }
		return TEXT("ボタンで投げる　壁で1回はね返してマトに当てろ！（直撃はノーカウント）");
	case EChaosImpactLoadingGame::Fishing:
		Player(MiniPlayer); Dummy(Back, Front, MiniGoal);
		if (Phase == 0) { Orb(Back, Front, MiniPlayer + FVector2D(35, -5), 18, P::Paper); }
		for (const FBall& Shot : Balls) { Orb(Back, Front, Shot.Position, Shot.Radius, Shot.Type == 2 ? P::Gold : P::Paper); }
		Back.Box(440, 505, 720, 20, P::WithAlpha(P::Paper, 0.18f));
		Front.Box(900 - Width, 500, Width * 2, 30, P::Gold);
		Front.Line(FVector2D(Marker, 480), FVector2D(Marker, 545), P::Paper, 7);
		Front.Text(TEXT("CHARGE"), 800, 555, 24, P::Paper, P::ETextAlign::Center);
		return TEXT("白い針が金のゾーンでボタン！　まん中ならフルチャージ");
	case EChaosImpactLoadingGame::Rhythm:
	case EChaosImpactLoadingGame::Parry:
	{
		const float Line = Game == EChaosImpactLoadingGame::Rhythm ? 560 : 650;
		Back.Box(Left, 390, 800, 70, P::WithAlpha(P::Violet, 0.14f));
		Back.Box(Line - 65, 370, 130, 110, P::WithAlpha(P::Gold, 0.13f));
		Front.Line(FVector2D(Line, 345), FVector2D(Line, 505), ActionFlash > 0 ? P::Gold : P::Paper, 6);
		if (Game == EChaosImpactLoadingGame::Rhythm)
		{
			Fighter(Back, Front, FVector2D(435, 425), P::Ice);
			for (const FBall& Ball : Balls) { Dummy(Back, Front, Ball.Position); }
			if (ActionFlash > 0)
			{
				Front.Line(FVector2D(435, 425), FVector2D(Right, 425), P::WithAlpha(P::Violet, ActionFlash * 3), 25);
				Front.Line(FVector2D(435, 425), FVector2D(Right, 425), P::Paper, 5);
			}
		}
		else
		{
			Fighter(Back, Front, FVector2D(Line - 30, 425), P::Ice); Dummy(Back, Front, FVector2D(1120, 425));
			// The shield, held out on the line.
			Front.Arc(FVector2D(Line - 30, 425), 48, -55, 55, ActionFlash > 0 ? P::Gold : P::Paper, 8);
			for (const FBall& Ball : Balls) { Orb(Back, Front, Ball.Position, 22, P::Paper); }
		}
		for (const FBall& Shot : Shots) { Front.Line(Shot.Position - FVector2D(55, 0), Shot.Position, P::Gold, 8); }
		Front.Text(FString::Printf(TEXT("%d COMBO"), Combo), 800, 530, 30, P::Paper, P::ETextAlign::Center);
		return Game == EChaosImpactLoadingGame::Rhythm ? TEXT("マトが白いラインに来たらボタンでビーム！") : TEXT("球が白いラインに来たらボタンで打ち返す！");
	}
	case EChaosImpactLoadingGame::Memory:
	{
		const int32 Lit = Phase == 0 && RoundTimer < Sequence.Num() * 0.65f ? FMath::FloorToInt(RoundTimer / 0.65f) : INDEX_NONE;
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector2D At(530 + I * 180, 420);
			const bool Highlight = Lit != INDEX_NONE && Sequence.IsValidIndex(Lit) && Sequence[Lit] == I && FMath::Fmod(RoundTimer, 0.65f) < 0.45f;
			SpecialBall(Back, Front, At, Highlight ? 55 : 40, I, GameClock);
			if (Highlight) { Front.Ring(At, 68, P::Gold, 8); }
			const TCHAR* Names[] = {TEXT("ファイア"), TEXT("アイス"), TEXT("スノー"), TEXT("ノヴァ")};
			Front.Text(Names[I], At.X, 485, 20, P::Paper, P::ETextAlign::Center);
			if (Phase == 1 && I == Selection) { Front.Ring(At, 65, P::Paper, 5); }
		}
		Front.Text(Phase == 0 ? TEXT("光る順番をおぼえよう") : TEXT("同じ順番でえらぼう！"), 800, 540, 28, P::Paper, P::ETextAlign::Center);
		return TEXT("光ったボールを同じ順番でえらぶ（←→＋ボタン / クリック）");
	}
	case EChaosImpactLoadingGame::Reaction:
		Back.Disc(Center, Phase == 1 ? 175 : 110, P::WithAlpha(Phase == 1 ? P::Gold : P::Fire, 0.25f));
		Front.Ring(Center, 175, Phase == 1 ? P::Gold : P::Fire, 5);
		SpecialBall(Back, Front, Center - FVector2D(0, Phase == 0 ? 85 : 0), Phase == 1 ? 70 : 36, 3, GameClock);
		Player(MiniPlayer + FVector2D(0, 40));
		Front.Text(Phase == 1 ? TEXT("回避！") : Phase == 0 ? TEXT("光るまで待て…") : HitFlash > 0 ? TEXT("KO…") : TEXT("次のノヴァ"), 800, 550, 30, P::Paper, P::ETextAlign::Center, TEXT("Black"), 3);
		return TEXT("ノヴァが光った瞬間にボタン！　早すぎても遅すぎてもダメ");
	case EChaosImpactLoadingGame::Snake:
		for (int32 I = 0; I <= SnakeColumns; ++I) { Back.Line(FVector2D(390 + I * 40, 212), FVector2D(390 + I * 40, 572), P::WithAlpha(P::Paper, 0.07f)); }
		for (int32 I = 0; I <= SnakeRows; ++I) { Back.Line(FVector2D(390, 212 + I * 36), FVector2D(1190, 212 + I * 36), P::WithAlpha(P::Paper, 0.07f)); }
		Back.Outline(390, 212, 800, 360, P::Ice, 4);
		Orb(Back, Front, SnakeCell(MiniGoal), 12, P::Paper);
		// The young at the back first, so the parent leading is on top.
		for (int32 I = Trail.Num() - 1; I >= 0; --I)
		{
			const FVector2D Facing = I == 0 ? SnakeStep(MoveDirection) : Trail[I - 1] - Trail[I];
			Enaga(Back, Front, SnakeCell(Trail[I]), I == 0 ? 16.0f : 12.0f, Facing, I == Trail.Num() - 1);
		}
		return TEXT("←→↑↓で雪玉を食べて行列をのばす　壁や行列にぶつかるとやり直し");
	case EChaosImpactLoadingGame::Balance:
	{
		Back.Disc(Center, 145, P::WithAlpha(P::Violet, 0.18f));
		for (int32 I = 0; I < 3; ++I)
		{
			Back.Arc(Center, 70 + I * 22, GameClock * 100 + I * 120, GameClock * 100 + I * 120 + 240, P::WithAlpha(P::Violet, 0.65f), 8);
		}
		Back.Disc(Center, 50, P::Ink); Front.Ring(Center, 52, P::Fire, 3);
		Orb(Back, Front, MiniGoal, 15, P::Gold); Front.Ring(MiniGoal, 26 + FMath::Sin(GameClock * 5) * 3, P::Gold, 3);
		Player(MiniPlayer);
		return TEXT("←→↑↓で動いてボールを集める　吸いこまれそうならボタンでダッシュ！");
	}
	case EChaosImpactLoadingGame::Quest:
		PaintQuest(Back, Front);
		return TEXT("↑↓←→でよける　雪玉をひろってボタンで真上に投げ、敵をやっつけろ！");
	default: return FString();
	}
}

// ---- シマエナガ大ぼうけん ----------------------------------------------------------------------------------------------
// All in real time: the foe walks along the top throwing things; the shima-enaga dodges about the snowy field below,
// picks up a snowball and throws it straight up at it. Worn down to nothing, the foe is beaten: on to the next (a little
// livelier).

void FChaosImpactLoadingState::NewFoe(int32 Level)
{
	using namespace ChaosImpactLoadingArcadeDetails;
	FoeLevel = Level;
	FoeHP = FoeMaxHP(Level);
	Phase = 0;
	Pattern = 0;
	RoundTimer = QuestPatternSeconds;
	SpawnIn = 1.0f;
	// The first pickup comes quickly.
	Marker = 0.6f;
	Balls.Reset();
	Shots.Reset();
	Pickups.Reset();
	Inventory = 0;
	MiniGoal = FVector2D(800, QuestFoeY);
	Burst(FVector2D(800, 430), P::Paper, FString::Printf(TEXT("%s が あらわれた！"), FoeName(Level)));
}

void FChaosImpactLoadingState::PressQuest()
{
	using namespace ChaosImpactLoadingArcadeDetails;
	// The snowball held goes straight up.
	if (Phase != 0 || Inventory == 0)
	{
		return;
	}
	FBall Throw;
	Throw.Position = MiniPlayer - FVector2D(0, 16);
	Throw.Velocity = FVector2D(0, -QuestThrowSpeed);
	Throw.Radius = 11.0f;
	Shots.Add(Throw);
	Inventory = 0;
	Cooldown = 0.2f;
}

void FChaosImpactLoadingState::StepQuest(float Delta, bool bAuto)
{
	using namespace ChaosImpactLoadingArcadeDetails;
	if (Phase == 4 || Phase == 5)
	{
		// The foe beaten (fading), or knocked out: a moment, then on.
		RoundTimer -= Delta;
		if (RoundTimer <= 0)
		{
			if (Phase == 5) { QuestHP = QuestMaxHP; NewFoe(0); }
			else { QuestHP = FMath::Min(QuestMaxHP, QuestHP + 1); NewFoe(FoeLevel + 1); }
		}
		return;
	}
	const float Walk = 0.6f + 0.05f * FMath::Min(FoeLevel, 6);
	const auto FoeX = [Walk](float At) { return 800.0f + 200.0f * FMath::Sin(At * Walk); };
	MiniGoal = FVector2D(FoeX(GameClock), QuestFoeY);

	// The shima-enaga: anywhere on the field.
	FVector2D Input = SteerVector();
	if (bAuto)
	{
		// To the nearest pickup (or under the foe, holding one), and away from whatever is about to arrive.
		FVector2D Want = FVector2D(FoeX(GameClock + 0.45f), QuestBottom - 50) - MiniPlayer;
		if (Inventory == 0)
		{
			double Nearest = TNumericLimits<double>::Max();
			for (const FBall& Pickup : Pickups)
			{
				const double Distance = FVector2D::DistSquared(Pickup.Position, MiniPlayer);
				if (Distance < Nearest) { Nearest = Distance; Want = Pickup.Position - MiniPlayer; }
			}
		}
		Want /= 120.0;
		FVector2D Away = Want.Size() > 1.0 ? Want.GetSafeNormal() : Want;
		for (const FBall& Ball : Balls)
		{
			const FVector2D From = MiniPlayer - (Ball.Position + Ball.Velocity * 0.15);
			const double Distance = From.Size();
			if (Distance < 100.0 && Distance > 0.1) { Away += From / Distance * FMath::Square((100.0 - Distance) / 100.0) * 4.0; }
		}
		Input = Away.Size() > 0.15 ? Away.GetSafeNormal() : FVector2D::ZeroVector;
		const float Arrive = static_cast<float>(MiniPlayer.Y - QuestFoeY) / QuestThrowSpeed;
		if (Inventory > 0 && Cooldown <= 0 && FMath::Abs(static_cast<float>(MiniPlayer.X) - FoeX(GameClock + Arrive)) < 22.0f) { PressQuest(); }
	}
	MiniPlayer += Input * 240.0f * Delta;
	MiniPlayer.X = FMath::Clamp(MiniPlayer.X, double(QuestLeft + 14), double(QuestRight - 14));
	MiniPlayer.Y = FMath::Clamp(MiniPlayer.Y, double(QuestTop + 14), double(QuestBottom - 14));

	// Snowballs turn up on the field (two at most); one can be held at a time.
	Marker -= Delta;
	if (Marker <= 0 && Pickups.Num() < 2)
	{
		FBall Pickup;
		Pickup.Position = FVector2D(Random.FRandRange(QuestLeft + 40, QuestRight - 40), Random.FRandRange(QuestTop + 60, QuestBottom - 30));
		Pickup.Radius = 13.0f;
		Pickups.Add(Pickup);
		Marker = 1.6f;
	}
	for (int32 I = Pickups.Num() - 1; I >= 0; --I)
	{
		FBall& Pickup = Pickups[I];
		Pickup.Age += Delta;
		if (Inventory == 0 && FVector2D::Distance(Pickup.Position, MiniPlayer) < 28)
		{
			Inventory = 1;
			Burst(Pickup.Position, P::Paper, TEXT("雪玉"));
			Pickups.RemoveAt(I);
		}
		else if (Pickup.Age > 8.0f) { Pickups.RemoveAt(I); }
	}

	// The snowballs thrown: on the foe, they wear it down.
	for (int32 I = Shots.Num() - 1; I >= 0; --I)
	{
		FBall& Shot = Shots[I];
		Shot.Position += Shot.Velocity * Delta;
		if (Shot.Position.Y <= MiniGoal.Y + 24 && FMath::Abs(Shot.Position.X - MiniGoal.X) < 46)
		{
			ActionFlash = 0.4f;
			FoeHP = FMath::Max(0, FoeHP - QuestSnowDamage);
			Burst(MiniGoal, P::Gold, FString::Printf(TEXT("-%d"), QuestSnowDamage));
			Shots.RemoveAt(I);
			if (FoeHP <= 0)
			{
				AddScore(5);
				QuestBanner = FString::Printf(TEXT("%s を やっつけた！ +5"), FoeName(FoeLevel));
				Phase = 4; RoundTimer = 2.0f;
				Balls.Reset(); Shots.Reset(); Pickups.Reset(); Inventory = 0;
				return;
			}
		}
		else if (Shot.Position.Y < Top) { Burst(FVector2D(Shot.Position.X, Top + 30), P::Muted, TEXT("はずれ")); Shots.RemoveAt(I); }
	}

	// The foe's patterns, one after another (more kinds, and quicker, as the foes go by).
	RoundTimer -= Delta;
	if (RoundTimer <= 0)
	{
		AddScore(1);
		const int32 Kinds = FMath::Min(4, 1 + FoeLevel);
		const int32 Next = Random.RandRange(0, Kinds - 1);
		Pattern = Next >= Pattern ? Next + 1 : Next;
		Pattern = FMath::Min(Pattern, Kinds);
		RoundTimer = QuestPatternSeconds;
		SpawnIn = 0.5f;
	}
	if (SpawnIn <= 0 && Balls.Num() < 60)
	{
		const float Faster = 1.0f + FMath::Min(FoeLevel * 0.08f, 0.5f);
		const auto Add = [this](const FVector2D& At, const FVector2D& Velocity, int32 Type)
		{
			FBall Ball; Ball.Position = At; Ball.Velocity = Velocity; Ball.Radius = 9.0f; Ball.Type = Type; Balls.Add(Ball);
		};
		switch (Pattern)
		{
		case 0:
			// Snow coming down all over.
			Add(FVector2D(Random.FRandRange(QuestLeft + 10, QuestRight - 10), QuestTop - 8), FVector2D(Random.FRandRange(-30.0f, 30.0f), 200.0f * Faster), 0);
			SpawnIn = 0.16f / Faster;
			break;
		case 1:
		{
			// A wall of ice across the field from either side in turn, a gap in it.
			const bool bFromLeft = (++Selection % 2) == 0;
			const int32 Gap = Random.RandRange(0, 5);
			for (int32 Slot = 0; Slot < 7; ++Slot)
			{
				if (Slot != Gap && Slot != Gap + 1)
				{
					Add(FVector2D(bFromLeft ? QuestLeft - 10 : QuestRight + 10, QuestTop + 22 + Slot * 40), FVector2D((bFromLeft ? 250.0f : -250.0f) * Faster, 0), 1);
				}
			}
			SpawnIn = 0.95f / Faster;
			break;
		}
		case 2:
		{
			// Fire from the foe, five in a fan at the shima-enaga.
			const FVector2D From = MiniGoal + FVector2D(0, 30);
			const float Aim = FMath::Atan2(static_cast<float>(MiniPlayer.Y - From.Y), static_cast<float>(MiniPlayer.X - From.X));
			for (int32 Shot = -2; Shot <= 2; ++Shot)
			{
				Add(From, FVector2D(FMath::Cos(Aim + Shot * 0.2f), FMath::Sin(Aim + Shot * 0.2f)) * 240.0f * Faster, 2);
			}
			SpawnIn = 0.9f / Faster;
			break;
		}
		case 3:
		{
			// A ring bursting out of the foe, turning a little each time.
			for (int32 Shot = 0; Shot < 14; ++Shot)
			{
				const float Angle = Shot * UE_TWO_PI / 14 + GameClock * 0.7f;
				Add(MiniGoal + FVector2D(0, 20), FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 170.0f * Faster, 3);
			}
			SpawnIn = 1.1f / Faster;
			break;
		}
		default:
		{
			// Feathers from the sides that follow for a moment, then fly on straight.
			const FVector2D From(Random.FRand() < 0.5f ? QuestLeft - 5 : QuestRight + 5, Random.FRandRange(QuestTop + 20, QuestBottom - 20));
			Add(From, (MiniPlayer - From).GetSafeNormal() * 160.0f * Faster, 4);
			SpawnIn = 0.6f / Faster;
			break;
		}
		}
	}
	for (int32 I = Balls.Num() - 1; I >= 0; --I)
	{
		FBall& Ball = Balls[I];
		Ball.Age += Delta;
		if (Ball.Type == 4 && Ball.Age < 0.9f) { Ball.Velocity = (MiniPlayer - Ball.Position).GetSafeNormal() * Ball.Velocity.Size(); }
		Ball.Position += Ball.Velocity * Delta;
		if (SafeTime <= 0 && FVector2D::Distance(Ball.Position, MiniPlayer) < Ball.Radius + 9.0f)
		{
			--QuestHP;
			SafeTime = 1.0f; HitFlash = 0.35f;
			Burst(MiniPlayer, P::Fire);
			Balls.RemoveAt(I);
			if (QuestHP <= 0)
			{
				QuestHP = 0;
				MiniMiss(MiniPlayer);
				QuestBanner = TEXT("ふらふら… もういちど ちょうせん！");
				Phase = 5; RoundTimer = 2.2f;
				Balls.Reset(); Shots.Reset(); Pickups.Reset(); Inventory = 0;
				return;
			}
			continue;
		}
		if (Ball.Age > 7.0f || Ball.Position.X < QuestLeft - 60 || Ball.Position.X > QuestRight + 60 || Ball.Position.Y > QuestBottom + 30
			|| Ball.Position.Y < Top)
		{
			Balls.RemoveAt(I);
		}
	}
}

void FChaosImpactLoadingState::PaintQuest(const ChaosImpactPaint::FPainter& Back, const ChaosImpactPaint::FPainter& Front) const
{
	using namespace ChaosImpactLoadingArcadeDetails;

	// The snowy field, snow falling on it.
	const float FieldWidth = QuestRight - QuestLeft, FieldHeight = QuestBottom - QuestTop;
	Back.Box(QuestLeft, QuestTop, FieldWidth, FieldHeight, FLinearColor(0.78f, 0.9f, 1.0f, 0.12f));
	Back.Box(QuestLeft, QuestTop, FieldWidth, 4, P::WithAlpha(P::Ice, 0.5f));
	for (int32 Flake = 0; Flake < 16; ++Flake)
	{
		const float X = QuestLeft + FMath::Fmod(Flake * 97.0f + GameClock * (14.0f + 6.0f * (Flake % 3)), FieldWidth);
		const float Y = QuestTop + FMath::Fmod(Flake * 53.0f + GameClock * 32.0f, FieldHeight);
		Back.Disc(FVector2D(X, Y), 2.0f + Flake % 2, FLinearColor(1, 1, 1, 0.3f));
	}

	// The foe walking along the top (fading when beaten).
	const int32 FoeKind = FoeLevel % FoeKinds;
	const float Fade = Phase == 4 ? FMath::Clamp(RoundTimer / 2.0f, 0.0f, 1.0f) : 1.0f;
	const FVector2D Foe = MiniGoal + FVector2D(ActionFlash > 0 ? FMath::Sin(GameClock * 60) * 10 * ActionFlash : 0.0f, FMath::Sin(GameClock * 5.0f) * 3);
	const FLinearColor Colors[FoeKinds] = {P::Paper, FLinearColor(0.1f, 0.1f, 0.14f), FLinearColor(0.55f, 0.38f, 0.22f), FLinearColor(0.6f, 0.88f, 1.0f), P::Violet};
	const FLinearColor Body = P::WithAlpha(Colors[FoeKind], Fade);
	const FLinearColor Eye = P::WithAlpha(FoeKind == 1 ? P::Paper : P::Ink, Fade);
	switch (FoeKind)
	{
	case 0:
		// A snowman: two balls, twig arms, a carrot nose.
		Back.Line(Foe + FVector2D(-28, 8), Foe + FVector2D(-60, -14), P::WithAlpha(FLinearColor(0.45f, 0.3f, 0.15f), Fade), 5);
		Back.Line(Foe + FVector2D(28, 8), Foe + FVector2D(60, -14), P::WithAlpha(FLinearColor(0.45f, 0.3f, 0.15f), Fade), 5);
		Front.Disc(Foe + FVector2D(0, 16), 30, Body);
		Front.Disc(Foe - FVector2D(0, 22), 20, Body);
		Front.Disc(Foe + FVector2D(-7, -26), 3.5f, Eye); Front.Disc(Foe + FVector2D(7, -26), 3.5f, Eye);
		Front.Disc(Foe + FVector2D(0, -18), 4, P::WithAlpha(FLinearColor(1.0f, 0.5f, 0.1f), Fade));
		break;
	case 1:
		// A crow: black, a yellow beak.
		Back.Line(Foe + FVector2D(-28, 0), Foe + FVector2D(-56, 14), Body, 12);
		Front.Disc(Foe, 32, Body);
		Front.Line(Foe + FVector2D(22, -4), Foe + FVector2D(48, 2), P::WithAlpha(P::Gold, Fade), 9);
		Front.Disc(Foe + FVector2D(10, -12), 4, Eye);
		break;
	case 2:
		// An owl: big ringed eyes, ear tufts.
		Front.Line(Foe + FVector2D(-20, -24), Foe + FVector2D(-30, -46), Body, 8);
		Front.Line(Foe + FVector2D(20, -24), Foe + FVector2D(30, -46), Body, 8);
		Front.Disc(Foe, 34, Body);
		for (const float Side : {-1.0f, 1.0f})
		{
			Front.Disc(Foe + FVector2D(Side * 13, -8), 11, P::WithAlpha(P::Paper, Fade));
			Front.Disc(Foe + FVector2D(Side * 13, -8), 5.5f, Eye);
		}
		Front.Disc(Foe + FVector2D(0, 6), 4, P::WithAlpha(P::Gold, Fade));
		break;
	case 3:
		// An icicle sprite: pale blue, points hanging off it.
		for (int32 Spike = -2; Spike <= 2; ++Spike)
		{
			Back.Line(Foe + FVector2D(Spike * 12, 18), Foe + FVector2D(Spike * 12, 44 + (2 - FMath::Abs(Spike)) * 8), Body, 6);
		}
		Front.Disc(Foe, 30, Body);
		Front.Disc(Foe + FVector2D(-9, -6), 3.5f, Eye); Front.Disc(Foe + FVector2D(9, -6), 3.5f, Eye);
		break;
	default:
		// A snow ogre: violet, golden horns.
		Front.Line(Foe + FVector2D(-16, -24), Foe + FVector2D(-26, -50), P::WithAlpha(P::Gold, Fade), 8);
		Front.Line(Foe + FVector2D(16, -24), Foe + FVector2D(26, -50), P::WithAlpha(P::Gold, Fade), 8);
		Front.Disc(Foe, 34, Body);
		Front.Disc(Foe + FVector2D(-11, -6), 4.5f, Eye); Front.Disc(Foe + FVector2D(11, -6), 4.5f, Eye);
		Front.Line(Foe + FVector2D(-10, 14), Foe + FVector2D(10, 14), Eye, 4);
		break;
	}
	if (Phase == 0)
	{
		// Under it: how worn down it is.
		Back.Box(Foe.X - 50, Foe.Y + 44, 100, 7, P::WithAlpha(P::Muted, 0.6f));
		Front.Box(Foe.X - 50, Foe.Y + 44, 100.0f * FoeHP / FoeMaxHP(FoeLevel), 7, P::Fire);
	}

	// What is coming at the shima-enaga (its snow greyish blue with a dark rim: not the snowballs to pick up).
	const FLinearColor BallColors[] = {FLinearColor(0.62f, 0.72f, 0.9f), P::Ice, P::Fire, P::Gold, P::Violet};
	for (const FBall& Ball : Balls)
	{
		if (Ball.Type == 4)
		{
			const FVector2D Along = Ball.Velocity.GetSafeNormal() * 12.0;
			Front.Line(Ball.Position - Along, Ball.Position + Along, P::Violet, 6);
			continue;
		}
		Back.Disc(Ball.Position, Ball.Radius + 3, FLinearColor(0, 0, 0, 0.6f));
		Front.Disc(Ball.Position, Ball.Radius, BallColors[Ball.Type % 5]);
	}

	// Snowballs on the field (blinking before they melt away), and those thrown.
	for (const FBall& Pickup : Pickups)
	{
		if (Pickup.Age > 6.0f && FMath::Fmod(GameClock, 0.3f) < 0.15f) { continue; }
		// A glow and a twinkle: something to pick up.
		Back.Disc(Pickup.Position, Pickup.Radius + 10, P::WithAlpha(P::Gold, 0.18f));
		Front.Ring(Pickup.Position, Pickup.Radius + 7 + FMath::Sin(GameClock * 6) * 2, P::WithAlpha(P::Gold, 0.8f), 3);
		Orb(Back, Front, Pickup.Position, Pickup.Radius, P::Paper);
		const float Twinkle = GameClock * 3 + Pickup.Position.X;
		Front.Disc(Pickup.Position + FVector2D(FMath::Cos(Twinkle), FMath::Sin(Twinkle)) * (Pickup.Radius + 7), 3, P::Gold);
	}
	for (const FBall& Shot : Shots) { Orb(Back, Front, Shot.Position, Shot.Radius, P::Paper); }

	// The shima-enaga (blinking for a moment after a hit), with what it holds over its head.
	if (Phase != 5 && (SafeTime <= 0 || FMath::Fmod(GameClock, 0.14f) < 0.08f))
	{
		const FVector2D Steered = SteerVector();
		Enaga(Back, Front, MiniPlayer, 15.0f, Steered.IsNearlyZero() ? FVector2D(0, -1) : Steered, true);
		if (Inventory > 0) { Orb(Back, Front, MiniPlayer - FVector2D(0, 30), 9.0f, P::Paper); }
	}

	// Who, and the shima-enaga's げんき.
	Front.Text(FString::Printf(TEXT("%s　%d体目"), FoeName(FoeLevel), FoeLevel + 1), QuestRight, 603, 18, P::Paper, P::ETextAlign::Right);
	Front.Text(TEXT("げんき"), QuestLeft, 603, 18, P::Paper);
	for (int32 Pip = 0; Pip < QuestMaxHP; ++Pip)
	{
		const FVector2D At(QuestLeft + 90 + Pip * 24, 616);
		if (Pip < QuestHP) { Front.Disc(At, 7, P::Paper); }
		else { Front.Ring(At, 7, P::Muted, 2); }
	}
	if (Phase == 4 || Phase == 5)
	{
		Front.Text(QuestBanner, 800, 420, 32, Phase == 4 ? P::Gold : P::Fire, P::ETextAlign::Center, TEXT("BlackItalic"), 3, P::Ink);
	}
}
