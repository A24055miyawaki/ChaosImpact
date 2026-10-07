#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactLoadingScreen.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactLoadingArcadeTest, "ChaosImpact.Menu.LoadingArcadeRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactLoadingArcadeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Eight original and thirteen additional games"), static_cast<int32>(EChaosImpactLoadingGame::Count), 21);
	for (int32 Index = 8; Index < 21; ++Index)
	{
		const auto Game = static_cast<EChaosImpactLoadingGame>(Index);
		FChaosImpactLoadingState State(EChaosImpactLoadingKind::Training, Game, 403 + Index);
		const double BaseTime = State.LastStepAt;
		int32 EarnedScore = 0;
		for (int32 Frame = 1; Frame <= 1800; ++Frame)
		{
			State.Step(BaseTime + Frame / 120.0);
			EarnedScore = FMath::Max(EarnedScore, State.Score);
		}
		const FString Label = FString::Printf(TEXT("Game %d (%s)"), Index, FChaosImpactLoadingState::ExtraGameName(Game));
		TestTrue(Label + TEXT(" has an automatic demo that scores"), EarnedScore > 0);
		TestTrue(Label + TEXT(" keeps loading-thread objects bounded"), State.Balls.Num() < 64 && State.Shots.Num() < 32
			&& State.Bursts.Num() < 64 && State.Trail.Num() <= 200);
		TestTrue(Label + TEXT(" simulation remains finite"), FMath::IsFinite(State.Marker) && !State.MiniPlayer.ContainsNaN());
		State.bArcade = true;
		TestFalse(Label + TEXT(" arcade never plays itself"), State.IsAuto(BaseTime + 100));
		State.SetSteerKey(1, true, BaseTime + 16);
		State.SetStick(0.7f, BaseTime + 16);
		State.SetPointer(650, BaseTime + 16);
		State.SwitchGame(1);
		TestEqual(Label + TEXT(" switching clears held keys"), State.SteerRight, 0);
		TestEqual(Label + TEXT(" switching clears the old stick"), State.StickX, 0.0f);
	}

	FChaosImpactLoadingState Snow(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Stack, 2);
	Snow.bArcade = true;
	const double SnowTime = Snow.LastStepAt;
	for (int32 I = 0; I < 10; ++I) { Snow.SetSteerKey(I % 2 == 0 ? -1 : 1, true, SnowTime); }
	TestEqual(TEXT("Alternating movement grows the snowball"), Snow.Width, 58.0f);
	Snow.SetSteerKey(1, true, SnowTime); TestEqual(TEXT("Holding one direction does not grow snow"), Snow.Width, 58.0f);
	Snow.Press(SnowTime); Snow.Press(SnowTime);
	TestEqual(TEXT("Snow throw spam cannot duplicate projectiles"), Snow.Shots.Num(), 1);
	TestEqual(TEXT("Snow scores on impact, not on the throw"), Snow.Score, 0);
	for (int32 I = 1; I <= 120; ++I) { Snow.Step(SnowTime + I / 120.0); }
	TestEqual(TEXT("Large snow impact earns two"), Snow.Score, 2);
	Snow.Cooldown = 0; Snow.Press(Snow.LastStepAt);
	TestEqual(TEXT("Small snow fails but preserves the record"), Snow.Score, 0);
	TestTrue(TEXT("Snow record survives a miss"), Snow.Best >= 2);

	FChaosImpactLoadingState Reaction(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Reaction, 3);
	Reaction.bArcade = true; Reaction.Press(Reaction.LastStepAt);
	TestEqual(TEXT("Pressing red does not award points"), Reaction.Score, 0);
	TestEqual(TEXT("Pressing red enters next-round delay"), Reaction.Phase, 2);
	Reaction.Cooldown = 0; Reaction.Phase = 1; Reaction.RoundTimer = 0.2f;
	Reaction.Press(Reaction.LastStepAt); TestEqual(TEXT("200ms response awards 800 points"), Reaction.Score, 800);

	FChaosImpactLoadingState Memory(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Memory, 4);
	Memory.bArcade = true; Memory.Sequence = {2, 0}; Memory.Phase = 1;
	Memory.ClickAt(FVector2D(890, 420), Memory.LastStepAt);
	TestEqual(TEXT("Clicking the third color selects it"), Memory.Selection, 2);
	TestEqual(TEXT("Correct memory click advances once"), Memory.SequenceIndex, 1);
	Memory.Cooldown = 0; Memory.Selection = 1; Memory.Press(Memory.LastStepAt);
	TestEqual(TEXT("Wrong memory answer begins a fresh one-color round"), Memory.Sequence.Num(), 1);
	TestEqual(TEXT("Memory returns to showing the sequence"), Memory.Phase, 0);

	for (const auto Game : {EChaosImpactLoadingGame::Rhythm, EChaosImpactLoadingGame::Parry})
	{
		FChaosImpactLoadingState State(EChaosImpactLoadingKind::Arcade, Game, 5);
		State.bArcade = true;
		FChaosImpactLoadingState::FBall Ball; Ball.Position = FVector2D(Game == EChaosImpactLoadingGame::Rhythm ? 560 : 650, 425);
		Ball.Velocity.X = -500; State.Balls.Add(Ball);
		State.Press(State.LastStepAt);
		TestEqual(TEXT("A perfectly timed hit earns three"), State.Score, 3);
		TestEqual(TEXT("A timed hit consumes exactly one ball"), State.Balls.Num(), 0);
		if (Game == EChaosImpactLoadingGame::Parry) { TestEqual(TEXT("A parry sends one shot back"), State.Shots.Num(), 1); }
		State.Press(State.LastStepAt); TestEqual(TEXT("Mashing never adds phantom points"), State.Score, 3);
	}

	FChaosImpactLoadingState Snake(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Snake, 6);
	Snake.bArcade = true; const double SnakeTime = Snake.LastStepAt;
	Snake.SetSteerKey(-1, true, SnakeTime);
	TestEqual(TEXT("The line never turns straight back onto itself"), Snake.PendingDirection, 0);
	Snake.SetVerticalKey(-1, true, SnakeTime);
	TestEqual(TEXT("Up turns the line up"), Snake.PendingDirection, 3);
	Snake.PendingDirection = Snake.MoveDirection = 0; Snake.MiniGoal = Snake.Trail[0] + FVector2D(1, 0); Snake.SpawnIn = 0.0f;
	Snake.Step(SnakeTime + 0.01);
	TestEqual(TEXT("A snowball eaten adds one to the line"), Snake.Trail.Num(), 4);
	TestEqual(TEXT("…and scores"), Snake.Score, 1);
	Snake.Trail = {FVector2D(19, 5), FVector2D(18, 5), FVector2D(17, 5)}; Snake.MiniGoal = FVector2D(0, 0); Snake.SpawnIn = 0.0f;
	Snake.Step(SnakeTime + 0.02);
	TestEqual(TEXT("The wall ends the run"), Snake.Score, 0);
	TestEqual(TEXT("…and the line starts short again"), Snake.Trail.Num(), 3);
	FChaosImpactLoadingState Lane(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::LaneRush, 7);
	Lane.SetStick(0.8f, Lane.LastStepAt); Lane.SetStick(0.8f, Lane.LastStepAt);
	TestEqual(TEXT("A held stick changes lane once, not every input event"), Lane.Selection, 2);
	Lane.SetStick(0, Lane.LastStepAt); Lane.SetStick(-0.8f, Lane.LastStepAt);
	TestEqual(TEXT("A new stick deflection changes lane again"), Lane.Selection, 1);
	Lane.SetStick(0.8f, Lane.LastStepAt);
	TestEqual(TEXT("Reversing the stick without releasing changes lane"), Lane.Selection, 2);

	FChaosImpactLoadingState Catch(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Catch, 8);
	Catch.bArcade = true; const double CatchTime = Catch.LastStepAt;
	for (int32 I = 0; I < 3; ++I)
	{
		FChaosImpactLoadingState::FBall Ball; Ball.Position = FVector2D(Catch.PaddleX, 560); Catch.Balls.Add(Ball);
	}
	Catch.Step(CatchTime + 0.01);
	TestEqual(TEXT("Inventory is capped at two like the main game"), Catch.Inventory, 2);
	Catch.Press(Catch.LastStepAt); TestEqual(TEXT("Throw consumes one held ball"), Catch.Inventory, 1);
	TestEqual(TEXT("Throw creates one projectile"), Catch.Shots.Num(), 1);

	FChaosImpactLoadingState Beam(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Rhythm, 9);
	Beam.bArcade = true;
	for (int32 I = 0; I < 3; ++I)
	{
		FChaosImpactLoadingState::FBall Target; Target.Position = FVector2D(560 + I * 45, 425); Target.Type = 7; Beam.Balls.Add(Target);
	}
	Beam.Press(Beam.LastStepAt); TestEqual(TEXT("One beam pierces the entire target row"), Beam.Balls.Num(), 0);
	TestEqual(TEXT("Three perfect beam knockouts earn nine"), Beam.Score, 9);

	FChaosImpactLoadingState Nova(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Reaction, 10);
	Nova.bArcade = true; const double NovaTime = Nova.LastStepAt;
	Nova.Phase = 1; Nova.ReactionCueAt = NovaTime;
	Nova.Step(NovaTime + 0.8);
	TestEqual(TEXT("Nova strikes when the dodge is too late"), Nova.Phase, 2);
	TestEqual(TEXT("An undodged nova never gives a score"), Nova.Score, 0);

	for (const auto Game : {EChaosImpactLoadingGame::Dodge, EChaosImpactLoadingGame::Juggle, EChaosImpactLoadingGame::Flappy,
		EChaosImpactLoadingGame::Reaction})
	{
		FChaosImpactLoadingState Idle(EChaosImpactLoadingKind::Training, Game, 11);
		Idle.SetPointer(700, Idle.LastStepAt + 10); Idle.SetStick(0.9f, Idle.LastStepAt + 10);
		TestTrue(TEXT("Unused mouse motion and stick axes never cancel a one-button game's demo"), Idle.IsAuto(Idle.LastStepAt + 10));
		Idle.Press(Idle.LastStepAt + 10);
		TestFalse(TEXT("A real press still takes over the demo"), Idle.IsAuto(Idle.LastStepAt + 10));
	}

	// シマエナガ大ぼうけん: moving about, being hit, picking up and throwing, beating a foe, being knocked out.
	FChaosImpactLoadingState Quest(EChaosImpactLoadingKind::Arcade, EChaosImpactLoadingGame::Quest, 12);
	Quest.bArcade = true; const double QuestTime = Quest.LastStepAt;
	TestEqual(TEXT("It starts straight away"), Quest.Phase, 0);
	const FVector2D QuestStart = Quest.MiniPlayer;
	Quest.SetVerticalKey(-1, true, QuestTime); Quest.SetSteerKey(1, true, QuestTime);
	for (int32 I = 1; I <= 30; ++I) { Quest.Balls.Reset(); Quest.Pickups.Reset(); Quest.Step(QuestTime + I / 120.0); }
	Quest.SetVerticalKey(-1, false, QuestTime); Quest.SetSteerKey(1, false, QuestTime);
	TestTrue(TEXT("Up and right move the shima-enaga up and right"), Quest.MiniPlayer.Y < QuestStart.Y - 20 && Quest.MiniPlayer.X > QuestStart.X + 20);
	FChaosImpactLoadingState::FBall QuestBall; QuestBall.Position = Quest.MiniPlayer; QuestBall.Radius = 9;
	Quest.Balls = {QuestBall}; Quest.SafeTime = 0;
	Quest.Step(Quest.LastStepAt + 0.01);
	TestEqual(TEXT("A hit takes one pip of げんき"), Quest.QuestHP, 4);
	Quest.Balls.Reset(); Quest.Pickups.Reset();
	FChaosImpactLoadingState::FBall Pickup; Pickup.Position = Quest.MiniPlayer; Pickup.Type = 0;
	Quest.Pickups = {Pickup}; Quest.Step(Quest.LastStepAt + 0.01);
	TestEqual(TEXT("Walking onto a snowball picks it up"), Quest.Inventory, 1);
	Quest.Cooldown = 0; Quest.Press(Quest.LastStepAt);
	TestEqual(TEXT("The button throws it"), Quest.Shots.Num(), 1);
	TestEqual(TEXT("…and the hands are empty"), Quest.Inventory, 0);
	const int32 FoeFull = Quest.FoeHP;
	Quest.Shots[0].Position = Quest.MiniGoal + FVector2D(0, 10); Quest.Balls.Reset();
	Quest.Step(Quest.LastStepAt + 0.001);
	TestEqual(TEXT("A snowball on the foe wears it down"), Quest.FoeHP, FoeFull - 12);
	for (int32 Hit = 0; Hit < 3; ++Hit)
	{
		Quest.Balls.Reset();
		FChaosImpactLoadingState::FBall Throw; Throw.Position = Quest.MiniGoal + FVector2D(0, 10);
		Quest.Shots = {Throw}; Quest.Step(Quest.LastStepAt + 0.001);
	}
	TestEqual(TEXT("Four snowballs beat the first foe"), Quest.Phase, 4);
	TestEqual(TEXT("…for five"), Quest.Score, 5);
	Quest.RoundTimer = 0.001f; Quest.Step(Quest.LastStepAt + 0.01);
	TestEqual(TEXT("The next foe comes on"), Quest.FoeLevel, 1);
	TestEqual(TEXT("…straight into it"), Quest.Phase, 0);
	Quest.Score = 5; Quest.QuestHP = 1; QuestBall.Position = Quest.MiniPlayer; Quest.Balls = {QuestBall}; Quest.SafeTime = 0;
	Quest.Step(Quest.LastStepAt + 0.01);
	TestEqual(TEXT("Out of げんき: knocked out"), Quest.Phase, 5);
	TestEqual(TEXT("…the run's score gone"), Quest.Score, 0);
	Quest.RoundTimer = 0.001f; Quest.Step(Quest.LastStepAt + 0.01);
	TestEqual(TEXT("…then from the first foe again"), Quest.FoeLevel, 0);
	TestEqual(TEXT("…with full げんき"), Quest.QuestHP, 5);

	// These original games reset the current score after a miss. A final-frame score alone is not
	// evidence that they never scored. Cover a range of gap/bounce seeds without render/prewarm hitches.
	for (const auto Game : {EChaosImpactLoadingGame::Dodge, EChaosImpactLoadingGame::Juggle, EChaosImpactLoadingGame::Flappy})
	{
		for (int32 Seed = 1; Seed <= 8; ++Seed)
		{
			FChaosImpactLoadingState Original(EChaosImpactLoadingKind::Training, Game, Seed);
			const double Start = Original.LastStepAt; int32 Peak = 0;
			for (int32 I = 1; I <= 2400; ++I) { Original.Step(Start + I / 120.0); Peak = FMath::Max(Peak, Original.Score); }
			TestTrue(FString::Printf(TEXT("Original demo %d seed %d earns points before a restart"), static_cast<int32>(Game), Seed), Peak > 0);
		}
	}
	return true;
}
#endif
