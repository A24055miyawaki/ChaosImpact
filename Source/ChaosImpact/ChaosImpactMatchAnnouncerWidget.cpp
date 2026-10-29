#include "ChaosImpactMatchAnnouncerWidget.h"

#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCrashBall.h"
#include "EngineUtils.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactSfx.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FString GetAnnouncerName(const APlayerState* Member)
	{
		if (!Member)
		{
			return TEXT("---");
		}
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		return Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
	}

	/** The duel's colours: side 0 red, side 1 blue (as the halves of the arena). */
	FLinearColor DuelSideColor(const int32 Side)
	{
		return ChaosImpactMatch::GetTeamColor(Side == 1 ? 1 : 0);
	}

	/** A stable 0-1 value per index, for confetti that looks random but does not flicker between frames. */
	float AnnouncerHash(const int32 Index, const float Salt)
	{
		return FMath::Frac(FMath::Sin(Index * 12.9898f + Salt * 78.233f) * 43758.5453f);
	}
}

void UChaosImpactMatchAnnouncerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ForceVolatile(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AnnouncerRoot"));
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ResultsView = NewObject<UChaosImpactResultsView>(this);
}

void UChaosImpactMatchAnnouncerWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (ResultsView)
	{
		ResultsView->Tick(GetOwningPlayer(), InDeltaTime);
	}
	UpdateSounds();
}

void UChaosImpactMatchAnnouncerWidget::UpdateSounds()
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bStarting = Match && Match->bOnlineRoom && Match->Phase == EChaosImpactOnlinePhase::Starting;
	if (!Match || (!Match->bVersusMatch && !bStarting))
	{
		SoundPhase = -1;
		return;
	}
	// Starting (online, before the stage opens) counts as a phase of its own here.
	const int32 Phase = bStarting ? 1000 : static_cast<int32>(Match->Phase);
	if (Phase != SoundPhase)
	{
		SoundPhase = Phase;
		SoundSecond = MAX_int32;
		bSoundMinuteCalled = false;
		bSoundPodiumShown = false;
		if (bStarting)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchStarting);
		}
		else if (Match->Phase == EChaosImpactOnlinePhase::Match && !Match->Rules.bDuel)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchGo);
		}
		else if (Match->Phase == EChaosImpactOnlinePhase::Results)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchFinish);
		}
	}
	const float Remaining = Match->GetPhaseRemainingSeconds();
	const int32 Seconds = FMath::CeilToInt(Remaining);
	if (bStarting)
	{
		// 3, 2, 1.
		if (Seconds > 0 && Seconds < SoundSecond)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchTick);
		}
		SoundSecond = Seconds;
	}
	else if (Match->Phase == EChaosImpactOnlinePhase::Intro)
	{
		if (Match->ReadyStartedAt > 0.0 && Match->ReadyStartedAt != SoundReadyAt)
		{
			SoundReadyAt = Match->ReadyStartedAt;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchReady);
		}
	}
	else if (Match->Phase == EChaosImpactOnlinePhase::Match && Match->Rules.bDuel)
	{
		// The duel: a sound for each step of a round, and the last five seconds of a round ticking.
		const FChaosImpactDuelState& Duel = Match->Duel;
		if (static_cast<int32>(Duel.Step) != SoundDuelStep || Duel.StepStartedAt != SoundDuelStepAt)
		{
			SoundDuelStep = static_cast<int32>(Duel.Step);
			SoundDuelStepAt = Duel.StepStartedAt;
			SoundSecond = MAX_int32;
			SoundReelsStopped = 0;
			if (Duel.Step == EChaosImpactDuelStep::BallDraw || Duel.Step == EChaosImpactDuelStep::RoundIntro)
			{
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchReady);
			}
			else if (Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath)
			{
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchGo);
			}
			else if (Duel.Step == EChaosImpactDuelStep::RoundOver)
			{
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchFinish);
			}
		}
		// The slot's reels: a tick as each one stops, rising, and the cheer once all three are in.
		if (Duel.Step == EChaosImpactDuelStep::BallDraw)
		{
			const float DrawAge = Match->GetDuelStepElapsed();
			while (SoundReelsStopped < ChaosImpactMatch::DuelBallPicks && DrawAge >= ChaosImpactMatch::DuelReelStopSeconds[SoundReelsStopped])
			{
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchTick, 1.0f, 1.0f + SoundReelsStopped * 0.12f);
				if (++SoundReelsStopped == ChaosImpactMatch::DuelBallPicks)
				{
					ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchStarting);
				}
			}
		}
		const float RoundLeft = Match->GetDuelStepRemaining();
		const int32 RoundSeconds = FMath::CeilToInt(RoundLeft);
		if ((Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath)
			&& RoundLeft > 0.0f && RoundSeconds <= 5 && RoundSeconds < SoundSecond)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchTick, 1.0f, 1.0f + (5 - RoundSeconds) * 0.06f);
		}
		SoundSecond = RoundSeconds;
	}
	else if (Match->Phase == EChaosImpactOnlinePhase::Match)
	{
		const float Elapsed = Match->GetPhaseElapsedSeconds();
		if (!bSoundMinuteCalled && Elapsed + Remaining > 61.0f && Remaining <= 60.0f && Remaining > 58.5f)
		{
			bSoundMinuteCalled = true;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchMinute);
		}
		// The last five seconds, each a little higher.
		if (Remaining > 0.0f && Seconds <= 5 && Seconds < SoundSecond)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchTick, 1.0f, 1.0f + (5 - Seconds) * 0.06f);
		}
		SoundSecond = Seconds;
	}
	else if (Match->Phase == EChaosImpactOnlinePhase::Results && ResultsView && ResultsView->IsActive()
		&& ResultsView->GetShowSeconds() >= 0.0f && !bSoundPodiumShown)
	{
		// The podium: confetti and the crowd.
		bSoundPodiumShown = true;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchConfetti);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::MatchCheer);
	}
}

void UChaosImpactMatchAnnouncerWidget::PaintCrashScoreboard(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements,
	const int32 BaseLayer, const AChaosImpactGameState& Match, const double ServerNow, const float Time) const
{
	using namespace ChaosImpactPaint;
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const auto TeamFill = [](const int32 Team)
	{
		const FLinearColor Color = ChaosImpactMatch::GetTeamColor(Team);
		return MakeFill(FMath::Lerp(Color, Paper, 0.45f), Color * FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
	};
	const auto Since = [ServerNow](const double When) { return When > 0.0 ? static_cast<float>(ServerNow - When) : 100.0f; };
	const int32 Leader = Match.GetCrashLeader();
	// Laid out the way the field is seen: when the camera has the blue goal on the left, so has the scoreboard.
	// (Every view of a match looks the same way, so the first player's camera stands for all.)
	float Flip = 1.0f;
	FVector GoalAt[2];
	int32 GoalsFound = 0;
	for (TActorIterator<AChaosImpactCrashGoal> It(GetWorld()); It; ++It)
	{
		GoalAt[FMath::Clamp(It->GetTeamIndex(), 0, 1)] = It->GetActorLocation();
		++GoalsFound;
	}
	if (const APlayerController* Viewer = GetOwningPlayer(); Viewer && GoalsFound >= 2)
	{
		FVector Camera;
		FRotator Rotation;
		Viewer->GetPlayerViewPoint(Camera, Rotation);
		const FVector Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
		Flip = FVector::DotProduct(Right, GoalAt[1] - GoalAt[0]) < 0.0 ? -1.0f : 1.0f;
	}
	// -1 for the team shown on the left, +1 for the right.
	const auto SideOf = [Flip](const int32 Team) { return (Team == 0 ? -1.0f : 1.0f) * Flip; };

	// The counts: a plate each side of the clock. A count that just went down punches out and flashes.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float X = SideOf(Team) < 0.0f ? -374.f : 180.f;
		const FLinearColor Accent = ChaosImpactMatch::GetTeamColor(Team);
		const float Drop = Since(Match.Crash.CountDroppedAt[Team]);
		const float Punch = Drop >= 0.0f && Drop < 1.0f ? FMath::Exp(-9.0f * Drop) : 0.0f;
		const FPainter Plate{TopCenter, OutDrawElements, BaseLayer + 3};
		HudShard(Plate, {At(X, 6), At(X + 194, 6), At(X + 182, 82), At(X - 14, 82)},
			Leader == Team ? MakeFill(FMath::Lerp(NightFill.Top, Accent, 0.35f), NightFill.Bottom) : NightFill, 3.f, FVector2D(4, 5),
			FMath::Lerp(Navy, Paper, Punch * 0.8f));
		const FPainter Face = HudAbove(Plate);
		HudStroke(Face, {At(X + 3, 79), At(X + 179, 79)}, FMath::Lerp(Accent, Paper, Punch), Leader == Team ? 7.f : 5.f);
		Face.Text(ChaosImpactMatch::GetTeamName(Team), X + 12, 13, 17, Paper, ETextAlign::Left, TEXT("Bold"), 1.f, Ink);
		if (Leader == Team)
		{
			Face.Text(TEXT("リード"), X + 12, 44, 16, FMath::Lerp(Accent, Paper, 0.25f + 0.25f * FMath::Sin(Time * 5.0f)),
				ETextAlign::Left, TEXT("Black"), 1.f, Ink);
		}
		const float Size = 43.0f * (1.0f + 0.45f * Punch);
		HudText3D(HudAbove(Face), FString::FromInt(Match.GetCrashCount(Team)), X + 162 + Size * 0.05f, 24 - (Size - 43.0f) * 0.55f, Size,
			FMath::Lerp(Paper, Accent, Punch * 0.6f), ETextAlign::Right);
	}

	// The gauge: the red goal on the left, the blue goal on the right, as the counts sit.
	constexpr float G = 300.0f;
	constexpr float Y = 106.0f;
	const FPainter Track{TopCenter, OutDrawElements, BaseLayer + 5};
	HudShard(Track, {At(-G, Y - 9), At(G + 7, Y - 9), At(G, Y + 9), At(-G - 7, Y + 9)}, NightFill, 2.f, FVector2D(2, 3));
	const FPainter OnTrack = HudAbove(Track);
	// How far each team has got: red's bar runs right towards the blue goal, blue's left towards the red goal.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const int32 Count = Match.GetCrashCount(Team);
		const float Reach = FMath::Clamp((100 - Count) / 99.0f, 0.0f, 1.0f) * G;
		if (Reach > 1.0f)
		{
			// Each team's bar runs from the middle towards the other team's goal.
			const float Tip = -SideOf(Team) * Reach;
			const float From = FMath::Min(0.0f, Tip);
			const float To = FMath::Max(0.0f, Tip);
			HudPoly(OnTrack, {At(From, Y - 5), At(To, Y - 5), At(To, Y + 5), At(From, Y + 5)}, TeamFill(Team), 2);
			OnTrack.Box(Tip - 1.5f, Y - 9, 3, 18, WithAlpha(Paper, 0.85f));
		}
	}
	for (int32 Tick = -3; Tick <= 3; ++Tick)
	{
		const float TX = Tick * G / 4.0f;
		HudAbove(OnTrack).Box(TX - (Tick == 0 ? 1.5f : 1.0f), Y - (Tick == 0 ? 12 : 4), Tick == 0 ? 3.f : 2.f, Tick == 0 ? 24.f : 8.f,
			WithAlpha(Paper, Tick == 0 ? 0.75f : 0.3f));
	}
	// The goals at either end throb faster the closer the giant ball gets to them.
	const float Progress = FMath::Clamp(Match.Crash.Progress, -1.0f, 1.0f);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const float Side = SideOf(Team);
		const float Danger = FMath::Clamp((Progress * (Team == 0 ? -1.0f : 1.0f) - 0.55f) / 0.45f, 0.0f, 1.0f);
		const float Throb = Danger * (0.5f + 0.5f * FMath::Sin(Time * FMath::Lerp(6.0f, 16.0f, Danger)));
		const FLinearColor Color = FMath::Lerp(ChaosImpactMatch::GetTeamColor(Team), Paper, Throb * 0.6f);
		const float GX = Side * (G + 34.0f);
		const float Scale = 1.0f + 0.22f * Throb;
		const FPainter Goal{TopCenter, OutDrawElements, BaseLayer + 6};
		if (Danger > 0.0f)
		{
			Goal.Disc(At(GX, Y), 26.0f * Scale, WithAlpha(ChaosImpactMatch::GetTeamColor(Team), 0.35f * Throb));
		}
		const float W = 15.0f * Scale;
		const float H = 15.0f * Scale;
		HudPoly(HudAbove(Goal, 1), {At(GX - W, Y - H + 4), At(GX + W, Y - H + 4), At(GX + W, Y + H), At(GX - W, Y + H)},
			SolidFill(WithAlpha(Navy, 0.85f)), 1);
		// Posts and crossbar, the net in lines behind.
		const FPainter Frame = HudAbove(Goal, 2);
		for (int32 Strand = 1; Strand < 4; ++Strand)
		{
			const float SX = GX - W + 2.0f * W * Strand / 4.0f;
			HudStroke(Frame, {At(SX, Y - H + 6), At(SX, Y + H)}, WithAlpha(Color, 0.45f), 1.5f);
		}
		HudStroke(HudAbove(Frame, 1), {At(GX - W, Y + H), At(GX - W, Y - H + 2), At(GX + W, Y - H + 2), At(GX + W, Y + H)}, Color, 4.0f);
	}
	// The giant ball itself, in the colour of the team that pushed it last.
	const float BX = Progress * G * Flip;
	const int32 Pusher = Match.Crash.LastPushTeam;
	const FLinearColor BallColor = Pusher == 0 || Pusher == 1 ? ChaosImpactMatch::GetTeamColor(Pusher) : Gold;
	const float Recovered = Since(Match.Crash.LastRecoveryAt);
	const float Flash = Recovered >= 0.0f && Recovered < 0.8f ? 1.0f - Recovered / 0.8f : 0.0f;
	const FPainter Marker{TopCenter, OutDrawElements, BaseLayer + 10};
	Marker.Disc(At(BX + 2, Y + 3), 14.0f, WithAlpha(Extrude, 0.9f));
	Marker.Disc(At(BX, Y), 14.0f, Navy);
	HudAbove(Marker, 1).Disc(At(BX, Y), 11.0f, FMath::Lerp(BallColor, Paper, Flash * 0.8f));
	HudAbove(Marker, 2).Disc(At(BX - 3.5f, Y - 4), 3.5f, WithAlpha(Paper, 0.85f));
	if (Flash > 0.0f)
	{
		HudAbove(Marker, 2).Ring(At(BX, Y), 14.0f + 22.0f * (1.0f - Flash), WithAlpha(Paper, Flash), 2.0f);
	}
}

int32 UChaosImpactMatchAnnouncerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);
	// The results (podium, numbers) take the whole screen once FINISH is over; online they stay into the lobby.
	if (ResultsView && ResultsView->IsActive() && ResultsView->GetShowSeconds() >= 0.0f)
	{
		return ResultsView->Paint(AllottedGeometry, OutDrawElements, BaseLayer + 1);
	}
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const bool bStarting = Match && Match->bOnlineRoom && Match->Phase == EChaosImpactOnlinePhase::Starting;
	if (!Match || (!Match->bVersusMatch && !bStarting) || Size.X < 1.0f || Size.Y < 1.0f)
	{
		return BaseLayer;
	}
	const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f);
	const FGeometry Center = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.5f, S);
	const FGeometry TopCenter = MakeAnchor(AllottedGeometry, Size.X * 0.5f, 0.0f, S);
	const double ServerNow = Match->GetServerWorldTimeSeconds();
	const bool bTeams = Match->IsTeamBattle();

	// A word scaled about the screen centre: it lands slightly large and settles, standing out like the logo's letters.
	// The big moments (Ready?, GO!, FINISH) burst out of a jagged blast of ice or fire behind it.
	const auto PaintCallout = [&](const FString& Word, const float TextSize, const float Scale, const float Alpha,
		const FLinearColor& Color, const int32 Layer, const FHudFill* Blast = nullptr)
	{
		if (Blast)
		{
			const float Open = FMath::Clamp(Alpha * 1.3f, 0.0f, 1.0f) * Scale;
			const float Outer = TextSize * 1.05f * Open;
			const float HalfWidth = (HudMeasure(Word, TextSize, TEXT("BlackItalic")) * 0.5f + TextSize * 0.6f) * Open;
			HudBurst(FPainter{Center, OutDrawElements, Layer, Alpha}, FVector2D(0.0, -TextSize * 0.1f), TextSize * 0.62f * Open, Outer, 18,
				*Blast, -6.0f, FVector2D(HalfWidth / FMath::Max(Outer, 1.0f), 0.85), 6.0f, FVector2D(9.0, 10.0), TextSize);
		}
		const FGeometry Space = MakeSkewed(Center, -800.0f, -TextSize * 0.8f, 1600.0f, TextSize * 1.6f, -0.12f, Scale);
		HudText3D(FPainter{Space, OutDrawElements, Layer + 3, Alpha}, Word, 800.0f, 0.0f, TextSize, Color, ETextAlign::Center,
			TEXT("BlackItalic"), TextSize * 0.07f);
	};

	if (bStarting)
	{
		// Everyone is ready (or the wait ran out): a band sweeps in, then 3, 2, 1 before the stage opens.
		const float Elapsed = Match->GetPhaseElapsedSeconds();
		const float Remaining = Match->GetPhaseRemainingSeconds();
		const float BandIn = EaseOut(Elapsed / 0.3f);
		const float BandOut = FMath::Clamp((0.25f - Remaining) / 0.25f, 0.0f, 1.0f);
		const FGeometry Band = MakeSkewed(Center, -1300.0f + (1.0f - BandIn) * -900.0f, -120.0f, 2600.0f, 240.0f, -0.18f);
		const FPainter BandPainter{Band, OutDrawElements, BaseLayer + 1, 1.0f - BandOut};
		BandPainter.Box(0.0f, 0.0f, 2600.0f, 240.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.82f));
		BandPainter.Box(0.0f, 0.0f, 2600.0f, 8.0f, Ice);
		BandPainter.Box(0.0f, 232.0f, 2600.0f, 8.0f, Fire);
		if (Elapsed < 0.35f)
		{
			const FPainter Flash{AllottedGeometry, OutDrawElements, BaseLayer + 1};
			Flash.Box(0.0f, 0.0f, Size.X, Size.Y, WithAlpha(Paper, 0.35f * (1.0f - Elapsed / 0.35f)));
		}
		const float WordIn = FMath::Clamp((Elapsed - 0.1f) / 0.15f, 0.0f, 1.0f);
		const FGeometry WordSpace = MakeSkewed(Center, -800.0f, -96.0f, 1600.0f, 120.0f, 0.0f,
			1.0f + 0.5f * FMath::Exp(-10.0f * FMath::Max(0.0f, Elapsed - 0.1f)));
		const FPainter Word{WordSpace, OutDrawElements, BaseLayer + 2, WordIn * (1.0f - BandOut)};
		Word.Text(TEXT("試合を開始します"), 800.0f, 0.0f, 84.0f, Paper, ETextAlign::Center, TEXT("Black"), 5.0f, Ink);
		const int32 Seconds = FMath::CeilToInt(Remaining);
		if (Seconds > 0)
		{
			const float Local = static_cast<float>(Seconds) - Remaining;
			const FGeometry NumberSpace = MakeSkewed(Center, -200.0f, 20.0f, 400.0f, 100.0f, 0.0f,
				1.0f + 0.6f * FMath::Exp(-9.0f * Local));
			const FPainter Number{NumberSpace, OutDrawElements, BaseLayer + 2, WordIn * (1.0f - 0.5f * Local)};
			Number.Text(FString::FromInt(Seconds), 200.0f, 0.0f, 80.0f, Gold, ETextAlign::Center, TEXT("Black"), 5.0f, Ink);
		}
		return BaseLayer + 3;
	}

	if (Match->Phase == EChaosImpactOnlinePhase::Intro)
	{
		if (Match->ReadyStartedAt > 0.0)
		{
			const float T = static_cast<float>(ServerNow - Match->ReadyStartedAt);
			PaintCallout(TEXT("Ready?"), 96.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * T), EaseOut(T / 0.12f), Paper, BaseLayer + 2, &IceFill);
		}
		else if (Match->bOnlineRoom
			&& Match->GetIntroElapsedSeconds() > ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds + 0.3f)
		{
			// This screen is done; someone else's opening is still playing.
			const float Pulse = 0.55f + 0.35f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 4.0f);
			const FPainter Waiting{Center, OutDrawElements, BaseLayer + 1, Pulse};
			Waiting.Text(TEXT("ほかのプレイヤーを待っています"), 0.0f, 60.0f, 20.0f, Paper, ETextAlign::Center,
				TEXT("Regular"), 2.0f, Ink);
		}
		return BaseLayer + 7;
	}

	if (Match->Phase == EChaosImpactOnlinePhase::Match && Match->Rules.bDuel)
	{
		const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
		PaintDuelScoreboard(TopCenter, OutDrawElements, BaseLayer + 1, *Match, Time);
		const FChaosImpactDuelState& Duel = Match->Duel;
		const float Age = Match->GetDuelStepElapsed();
		const auto Fade = [](const float T, const float Hold, const float Out) { return EaseOut(T / 0.12f) * (1.0f - FMath::Clamp((T - Hold) / Out, 0.0f, 1.0f)); };
		if (Duel.Step == EChaosImpactDuelStep::BallDraw)
		{
			PaintDuelBallSlot(Center, OutDrawElements, BaseLayer + 10, *Match, Time);
		}
		else if (Duel.Step == EChaosImpactDuelStep::RoundIntro && Age < 1.6f)
		{
			const bool bFinal = Duel.Round >= ChaosImpactMatch::DuelMaxRounds
				|| (Duel.Wins[0] == ChaosImpactMatch::DuelRoundsToWin - 1 && Duel.Wins[1] == ChaosImpactMatch::DuelRoundsToWin - 1);
			PaintCallout(bFinal ? FString(TEXT("FINAL ROUND")) : FString::Printf(TEXT("ROUND %d"), Duel.Round), 110.0f,
				1.0f + 0.6f * FMath::Exp(-11.0f * Age), Fade(Age, 1.2f, 0.4f), bFinal ? Gold : Paper, BaseLayer + 12, &IceFill);
		}
		else if (Duel.Step == EChaosImpactDuelStep::Fight && Age < 0.9f)
		{
			PaintCallout(TEXT("FIGHT!"), 150.0f, FMath::Lerp(2.0f, 1.0f, EaseOut(Age / 0.18f)),
				1.0f - FMath::Clamp((Age - 0.55f) / 0.35f, 0.0f, 1.0f), Paper, BaseLayer + 12, &FireFill);
		}
		else if (Duel.Step == EChaosImpactDuelStep::SuddenDeath && Age < 2.0f)
		{
			const float Alpha = Fade(Age, 1.6f, 0.4f);
			PaintCallout(TEXT("サドンデス！"), 104.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * Age), Alpha, Paper, BaseLayer + 12, &FireFill);
			FPainter{Center, OutDrawElements, BaseLayer + 16, Alpha}.Text(TEXT("先に当てたほうがラウンドを取る"), 0.0f, 86.0f, 30.0f, Gold,
				ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
		}
		else if (Duel.Step == EChaosImpactDuelStep::RoundOver)
		{
			if (Age < 1.5f && Duel.RoundEnd != EChaosImpactDuelEnd::Forfeit)
			{
				// How it ended, slammed in...
				const TCHAR* Word = Duel.RoundEnd == EChaosImpactDuelEnd::Time ? TEXT("TIME!")
					: Duel.RoundEnd == EChaosImpactDuelEnd::DoubleKnockout ? TEXT("DOUBLE K.O.")
					: Duel.RoundEnd == EChaosImpactDuelEnd::Draw ? TEXT("TIME UP") : TEXT("K.O.!");
				PaintCallout(Word, 140.0f, FMath::Lerp(1.9f, 1.0f, EaseOut(Age / 0.18f)), Fade(Age, 1.1f, 0.4f), Paper, BaseLayer + 12, &FireFill);
			}
			else if (Age >= 1.5f)
			{
				// ...then who took the round, in their colour.
				const float Local = Age - 1.5f;
				const bool bDrawn = Duel.RoundWinner == INDEX_NONE;
				const FLinearColor Color = bDrawn ? Paper : DuelSideColor(Duel.RoundWinner);
				const FHudFill Blast = MakeFill(FMath::Lerp(Color, Paper, 0.5f), Color * FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
				PaintCallout(bDrawn ? FString(TEXT("ROUND DRAW")) : FString::Printf(TEXT("%s WIN"), *GetAnnouncerName(Duel.Sides[Duel.RoundWinner])),
					96.0f, 1.0f + 0.5f * FMath::Exp(-10.0f * Local), Fade(Local, 1.5f, 0.35f), Paper, BaseLayer + 12, bDrawn ? nullptr : &Blast);
			}
		}
		// The last five seconds of a round, large in the middle.
		const float RoundLeft = Match->GetDuelStepRemaining();
		if ((Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath) && RoundLeft > 0.0f && RoundLeft <= 5.0f
			&& Age > 1.0f)
		{
			const int32 Seconds = FMath::CeilToInt(RoundLeft);
			const float Local = static_cast<float>(Seconds) - RoundLeft;
			PaintCallout(FString::FromInt(Seconds), 120.0f, 1.0f + 0.5f * FMath::Exp(-8.0f * Local), 0.7f * (1.0f - 0.6f * Local), Paper,
				BaseLayer + 12);
		}
		return BaseLayer + 18;
	}

	if (Match->Phase == EChaosImpactOnlinePhase::Match)
	{
		const float Elapsed = Match->GetPhaseElapsedSeconds();
		const float Remaining = Match->GetPhaseRemainingSeconds();
		const int32 Seconds = FMath::CeilToInt(Remaining);
		// The time belongs to everyone, so split screen shows it once at the top of the whole screen.
		// One minute left: a banner across the middle, and the clock turns gold for a moment.
		const float MinuteLeftAge = 60.0f - Remaining;
		const bool bMinuteCall = Elapsed + Remaining > 61.0f && MinuteLeftAge >= 0.0f && MinuteLeftAge < 1.8f;
		// The clock: a broken-edged slab at the top with the time standing out on it, a blade under it of the time left,
		// and two loose shards bobbing beside it. The last ten seconds it turns red-hot, a fire blast flaring behind it
		// and the whole of it jolting on every second.
		const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
		const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
		const float Total = FMath::Max(Elapsed + Remaining, 1.0f);
		const bool bHurry = Remaining <= 10.0f && !Match->Crash.bOvertime;
		const float Beat = bHurry ? 1.0f - FMath::Frac(Remaining) : 0.0f;
		const float Kick = Beat * Beat;
		const float Shake = bHurry ? FMath::Sin(Beat * 40.0f) * Kick * 6.0f : 0.0f;
		const FPainter ClockBase{TopCenter, OutDrawElements, BaseLayer + 1};
		if (bHurry)
		{
			HudBurst(ClockBase, At(Shake, 40.0f), 60.0f * (1.0f + 0.3f * Kick), 96.0f * (1.0f + 0.3f * Kick), 14, FireFill, Time * 40.0f,
				FVector2D(1.9, 0.62), 3.0f, FVector2D::ZeroVector, 2.0f);
		}
		const FPainter Slab = HudAbove(ClockBase);
		HudShard(Slab, {At(-122.0f + Shake, 4.0f), At(124.0f + Shake, 4.0f), At(142.0f + Shake, 32.0f), At(110.0f + Shake, 78.0f),
			At(-106.0f + Shake, 80.0f), At(-140.0f + Shake, 36.0f)}, bHurry ? MakeFill(FLinearColor(0.5f, 0.06f, 0.04f, 0.96f),
			FLinearColor(0.12f, 0.01f, 0.02f, 0.96f)) : NightFill, 4.0f, FVector2D(7.0, 8.0));
		const FPainter OnSlab = HudAbove(Slab);
		HudStroke(OnSlab, {At(-119.0f + Shake, 8.0f), At(122.0f + Shake, 8.0f), At(137.0f + Shake, 32.0f)}, bHurry ? FireFill.Top : IceFill.Middle, 3.0f);
		const TArray<FVector2D> Blade{At(-86.0f + Shake, 62.0f), At(80.0f + Shake, 62.0f), At(94.0f + Shake, 66.0f), At(80.0f + Shake, 70.0f),
			At(-90.0f + Shake, 70.0f)};
		HudPoly(OnSlab, HudInflate(Blade, 2.0f), SolidFill(Navy), 1);
		// クラッシュボール overtime: the blade is the overtime team's grace, refilled by each of its hits on the giant ball.
		const bool bOvertime = Match->Rules.bCrashBall && Match->Crash.bOvertime;
		const auto TeamFill = [](const int32 Team)
		{
			const FLinearColor Color = ChaosImpactMatch::GetTeamColor(Team);
			return MakeFill(FMath::Lerp(Color, Paper, 0.45f), Color * FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
		};
		const float BladeFill = bOvertime
			? 1.0f - static_cast<float>(ServerNow - FMath::Max(Match->Crash.OvertimeHitAt, Match->Crash.OvertimeStartedAt))
				/ ChaosImpactMatch::CrashOvertimeGraceSeconds
			: Remaining / Total;
		HudPoly(HudAbove(OnSlab, 1), HudClip(Blade, -90.0f + Shake + 184.0f * FMath::Clamp(BladeFill, 0.0f, 1.0f), false, true),
			bOvertime ? TeamFill(Match->Crash.OvertimeTeam) : bHurry ? FireFill : IceFill, 2);
		const float Bob = FMath::Sin(Time * 2.2f) * 3.0f;
		HudShard(OnSlab, {At(-170.0f, 14.0f + Bob), At(-150.0f, 20.0f + Bob), At(-160.0f, 40.0f + Bob)}, bHurry ? FireFill : IceFill, 2.0f,
			FVector2D(3.0, 4.0));
		HudShard(OnSlab, {At(156.0f, 44.0f - Bob), At(176.0f, 34.0f - Bob), At(170.0f, 58.0f - Bob)}, FireFill, 2.0f, FVector2D(3.0, 4.0));
		const FPainter Digits{MakeSkewed(TopCenter, -120.0f + Shake, 2.0f, 240.0f, 66.0f, -0.12f, 1.0f + 0.25f * Kick), OutDrawElements,
			BaseLayer + 9};
		HudText3D(Digits, bOvertime ? FString(TEXT("延長")) : FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 120.0f, -6.0f, 46.0f,
			bOvertime ? FMath::Lerp(ChaosImpactMatch::GetTeamColor(Match->Crash.OvertimeTeam), Paper, 0.35f + 0.25f * FMath::Sin(Time * 9.0f))
				: bHurry ? FireFill.Top : bMinuteCall ? Gold : Paper, ETextAlign::Center);
		if (Match->Rules.bCrashBall)
		{
			PaintCrashScoreboard(TopCenter, OutDrawElements, BaseLayer, *Match, ServerNow, Time);
			// Big moments only: who took the lead, and overtime starting.
			const int32 Leader = Match->GetCrashLeader();
			const double LeadAge = ServerNow - Match->Crash.LastLeadChangedAt;
			if (Leader != INDEX_NONE && LeadAge >= 0. && LeadAge < 1.5 && Elapsed > 1.1f && !bOvertime)
			{
				PaintCallout(FString::Printf(TEXT("%s リード！"), ChaosImpactMatch::GetTeamName(Leader)), 66, 1.f + .2f * FMath::Exp(-8.f * LeadAge),
					FMath::Min(1.f, static_cast<float>((1.5 - LeadAge) * 3.)), ChaosImpactMatch::GetTeamColor(Leader), BaseLayer + 12);
			}
			const double OvertimeAge = ServerNow - Match->Crash.OvertimeStartedAt;
			if (bOvertime && OvertimeAge >= 0. && OvertimeAge < 1.8)
			{
				const float Age = static_cast<float>(OvertimeAge);
				const FHudFill Blast = TeamFill(Match->Crash.OvertimeTeam);
				PaintCallout(TEXT("延長戦！"), 104.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * Age),
					EaseOut(Age / 0.12f) * (1.0f - FMath::Clamp((Age - 1.4f) / 0.4f, 0.0f, 1.0f)), Paper, BaseLayer + 12, &Blast);
			}
		}
		if (bMinuteCall)
		{
			// Like Ready?: the words alone, no band over the play field, and gone quickly.
			const float Alpha = EaseOut(MinuteLeftAge / 0.12f) * (1.0f - FMath::Clamp((MinuteLeftAge - 1.4f) / 0.4f, 0.0f, 1.0f));
			PaintCallout(TEXT("あと1分！"), 96.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * MinuteLeftAge), Alpha, Gold, BaseLayer + 12);
		}

		if (Elapsed < 1.1f)
		{
			PaintCallout(TEXT("GO!"), 150.0f, FMath::Lerp(2.0f, 1.0f, EaseOut(Elapsed / 0.18f)),
				1.0f - FMath::Clamp((Elapsed - 0.7f) / 0.4f, 0.0f, 1.0f), Paper, BaseLayer + 12, &FireFill);
		}
		if (Remaining > 0.0f && Remaining <= 5.0f)
		{
			const float Local = static_cast<float>(Seconds) - Remaining;
			PaintCallout(FString::FromInt(Seconds), 120.0f, 1.0f + 0.5f * FMath::Exp(-8.0f * Local),
				0.7f * (1.0f - 0.6f * Local), Paper, BaseLayer + 12);
		}
		return BaseLayer + 18;
	}

	if (Match->Phase != EChaosImpactOnlinePhase::Results)
	{
		return BaseLayer;
	}

	// ---- Results ----------------------------------------------------------------------------------
	const float Elapsed = Match->GetPhaseElapsedSeconds();
	const FPainter Full{AllottedGeometry, OutDrawElements, BaseLayer + 1};
	if (Elapsed < 1.7f)
	{
		// The whistle moment: a flash, then FINISH slams in over the frozen field. A giant ball in the goal is a
		// KNOCKOUT, in the scoring team's colour.
		const bool bKnockout = Match->Rules.bCrashBall && Match->Crash.bKnockout && Match->Crash.Winner != INDEX_NONE;
		Full.Box(0.0f, 0.0f, Size.X, Size.Y, WithAlpha(bKnockout ? FMath::Lerp(Paper, ChaosImpactMatch::GetTeamColor(Match->Crash.Winner), 0.3f) : Paper,
			(bKnockout ? 0.6f : 0.45f) * FMath::Clamp(1.0f - Elapsed / 0.25f, 0.0f, 1.0f)));
		const FLinearColor KnockoutColor = bKnockout ? ChaosImpactMatch::GetTeamColor(Match->Crash.Winner) : Paper;
		const FHudFill KnockoutBlast = MakeFill(FMath::Lerp(KnockoutColor, Paper, 0.5f), KnockoutColor * FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
		if (Match->Rules.bDuel)
		{
			// The duel: who won it (YOU WIN / YOU LOSE for a player alone at the screen), in the winner's colour.
			const FChaosImpactDuelState& Duel = Match->Duel;
			const UGameInstance* GameInstance = GetGameInstance();
			const bool bShared = GameInstance && GameInstance->GetLocalPlayers().Num() > 1;
			const int32 OwnSide = GetOwningPlayer() ? Duel.SideOf(GetOwningPlayer()->PlayerState) : INDEX_NONE;
			const bool bDrawn = Duel.MatchWinner == INDEX_NONE;
			const FString Word = bDrawn ? FString(TEXT("DRAW"))
				: OwnSide != INDEX_NONE && !bShared ? FString(OwnSide == Duel.MatchWinner ? TEXT("YOU WIN!") : TEXT("YOU LOSE"))
				: FString::Printf(TEXT("%s WIN!"), *GetAnnouncerName(Duel.Sides[Duel.MatchWinner]));
			const FLinearColor Color = bDrawn ? Paper : DuelSideColor(Duel.MatchWinner);
			const FHudFill Blast = MakeFill(FMath::Lerp(Color, Paper, 0.5f), Color * FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
			PaintCallout(Word, 124.0f, FMath::Lerp(1.8f, 1.0f, EaseOut(Elapsed / 0.2f)), 1.0f - FMath::Clamp((Elapsed - 1.3f) / 0.4f, 0.0f, 1.0f),
				Paper, BaseLayer + 3, &Blast);
			return BaseLayer + 8;
		}
		PaintCallout(bKnockout ? TEXT("KNOCKOUT!") : TEXT("FINISH"), bKnockout ? 128.0f : 120.0f, FMath::Lerp(1.8f, 1.0f, EaseOut(Elapsed / 0.2f)),
			1.0f - FMath::Clamp((Elapsed - 1.3f) / 0.4f, 0.0f, 1.0f), Paper, BaseLayer + 3, bKnockout ? &KnockoutBlast : &FireFill);
	}
	return BaseLayer + 8;
}

int32 UChaosImpactMatchAnnouncerWidget::GetDuelLeftSide(const AChaosImpactGameState& Match) const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const bool bShared = GameInstance && GameInstance->GetLocalPlayers().Num() > 1;
	const int32 OwnSide = GetOwningPlayer() ? Match.Duel.SideOf(GetOwningPlayer()->PlayerState) : INDEX_NONE;
	return !bShared && OwnSide == 1 ? 1 : 0;
}

void UChaosImpactMatchAnnouncerWidget::PaintDuelScoreboard(const FGeometry& TopCenter, FSlateWindowElementList& OutDrawElements,
	const int32 BaseLayer, const AChaosImpactGameState& Match, const float Time) const
{
	using namespace ChaosImpactPaint;
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const FChaosImpactDuelState& Duel = Match.Duel;
	const bool bLive = Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath;
	const bool bSudden = Duel.Step == EChaosImpactDuelStep::SuddenDeath;
	const float Remaining = Match.GetDuelStepRemaining();
	const bool bHurry = bLive && Remaining <= 10.0f;
	const int32 Seconds = Duel.Step == EChaosImpactDuelStep::RoundIntro || Duel.Step == EChaosImpactDuelStep::BallDraw
		? FMath::RoundToInt(ChaosImpactMatch::DuelRoundSeconds)
		: bLive ? FMath::CeilToInt(Remaining) : 0;

	// The round clock: a broken-edged slab, red-hot in the last seconds and in sudden death.
	const FPainter Slab{TopCenter, OutDrawElements, BaseLayer + 1};
	HudShard(Slab, {At(-112.0f, 4.0f), At(114.0f, 4.0f), At(130.0f, 34.0f), At(102.0f, 92.0f), At(-98.0f, 94.0f), At(-128.0f, 38.0f)},
		bSudden || bHurry ? MakeFill(FLinearColor(0.5f, 0.06f, 0.04f, 0.96f), FLinearColor(0.12f, 0.01f, 0.02f, 0.96f)) : NightFill, 4.0f,
		FVector2D(7.0, 8.0));
	const FPainter OnSlab = HudAbove(Slab);
	OnSlab.Text(bSudden ? FString(TEXT("サドンデス")) : FString::Printf(TEXT("ROUND %d"), FMath::Max(1, Duel.Round)), 0.0f, 9.0f, 15.0f,
		bSudden ? FireFill.Top : WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Black"), 1.5f, Navy);
	const float Pulse = bSudden ? 0.5f + 0.5f * FMath::Sin(Time * 9.0f) : 0.0f;
	HudText3D(HudAbove(OnSlab), FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 0.0f, 28.0f, 44.0f,
		bSudden ? FMath::Lerp(FireFill.Top, Paper, Pulse * 0.5f) : bHurry ? FireFill.Top : Paper, ETextAlign::Center);

	// The players either side: name, the rounds won (pips by the clock) and the health left.
	const int32 LeftSide = GetDuelLeftSide(Match);
	const UGameInstance* GameInstance = GetGameInstance();
	const bool bShared = GameInstance && GameInstance->GetLocalPlayers().Num() > 1;
	const int32 OwnSide = GetOwningPlayer() ? Duel.SideOf(GetOwningPlayer()->PlayerState) : INDEX_NONE;
	for (int32 PlateIndex = 0; PlateIndex < 2; ++PlateIndex)
	{
		const int32 Side = PlateIndex == 0 ? LeftSide : 1 - LeftSide;
		const float Dir = PlateIndex == 0 ? -1.0f : 1.0f;
		const APlayerState* Member = Duel.Sides[Side].Get();
		const FLinearColor Color = DuelSideColor(Side);
		const bool bTookRound = Duel.Step == EChaosImpactDuelStep::RoundOver && Duel.RoundWinner == Side;
		// The plate, leaning away from the clock.
		const float Inner = 140.0f;
		const float Outer = 480.0f;
		const TArray<FVector2D> Plate{At(Dir * Inner, 10.0f), At(Dir * Outer, 10.0f), At(Dir * (Outer - 18.0f), 82.0f), At(Dir * (Inner - 12.0f), 82.0f)};
		const FPainter Back{TopCenter, OutDrawElements, BaseLayer + 1};
		HudShard(Back, Plate, MakeFill(FMath::Lerp(NightFill.Top, Color, bTookRound ? 0.65f : 0.35f), NightFill.Bottom), 3.0f, FVector2D(4.0, 5.0),
			bTookRound ? FMath::Lerp(Navy, Paper, 0.5f + 0.5f * FMath::Sin(Time * 10.0f)) : Navy);
		const FPainter Face = HudAbove(Back);
		HudStroke(Face, {At(Dir * (Inner + 6.0f), 79.0f), At(Dir * (Outer - 20.0f), 79.0f)}, Color, 5.0f);
		// The name at the outer end, with YOU when it is this player alone at the screen.
		const float NameX = Dir * (Outer - 24.0f);
		const ETextAlign NameAlign = PlateIndex == 0 ? ETextAlign::Left : ETextAlign::Right;
		Face.Text(GetAnnouncerName(Member), NameX, 14.0f, 22.0f, Paper, NameAlign, TEXT("Black"), 2.0f, Navy);
		if (Side == OwnSide && !bShared)
		{
			const float TagX = PlateIndex == 0 ? NameX : NameX - 54.0f;
			Face.Box(TagX, 46.0f, 54.0f, 22.0f, Ice);
			Face.Text(TEXT("YOU"), TagX + 27.0f, 47.0f, 15.0f, Ink, ETextAlign::Center, TEXT("Black"));
		}
		// Health: three cells (the last one throbs red).
		const AChaosImpactCharacter* Character = Member ? Cast<AChaosImpactCharacter>(Member->GetPawn()) : nullptr;
		const int32 Cells = Character ? FMath::Clamp(FMath::RoundToInt(Character->GetMaxHealth()), 1, 6) : 3;
		const int32 Left = Character && !Character->IsEliminated() ? FMath::Clamp(FMath::CeilToInt(Character->GetHealth() - KINDA_SMALL_NUMBER), 0, Cells) : 0;
		for (int32 Cell = 0; Cell < Cells; ++Cell)
		{
			const float CellX = PlateIndex == 0 ? -Outer + 96.0f + Cell * 40.0f : Outer - 96.0f - (Cell + 1) * 40.0f + 6.0f;
			const bool bFull = Cell < Left;
			const bool bLast = bFull && Left == 1;
			const FLinearColor CellColor = bLast ? FMath::Lerp(FireFill.Top, Paper, 0.5f + 0.5f * FMath::Sin(Time * 12.0f)) : Paper;
			HudShard(Face, {At(CellX + 6.0f, 50.0f), At(CellX + 36.0f, 50.0f), At(CellX + 30.0f, 66.0f), At(CellX, 66.0f)},
				bFull ? MakeFill(CellColor, FMath::Lerp(CellColor, Color, 0.6f)) : SolidFill(WithAlpha(Navy, 0.85f)), 1.5f, FVector2D(2.0, 2.0));
		}
		// The rounds won, as pips beside the clock.
		for (int32 Pip = 0; Pip < ChaosImpactMatch::DuelRoundsToWin; ++Pip)
		{
			const FVector2D PipAt(Dir * (Inner + 26.0f + Pip * 34.0f), 44.0f);
			const bool bWon = Pip < Duel.Wins[Side];
			const FPainter Gem = HudAbove(Face);
			Gem.Disc(PipAt + FVector2D(2.0, 3.0), 13.0f, WithAlpha(Extrude, 0.9f));
			Gem.Disc(PipAt, 13.0f, Navy);
			HudAbove(Gem, 1).Disc(PipAt, 10.0f, bWon ? Color : WithAlpha(Paper, 0.12f));
			if (bWon)
			{
				HudAbove(Gem, 2).Disc(PipAt + FVector2D(-3.0, -3.5), 3.0f, WithAlpha(Paper, 0.8f));
			}
		}
	}

	// The special balls of this match, small, at the outer ends of the plates' feet (once the slot has shown them).
	if (Duel.Step != EChaosImpactDuelStep::BallDraw && !Duel.BallPicks.IsEmpty())
	{
		const FPainter Balls{TopCenter, OutDrawElements, BaseLayer + 4};
		for (int32 Pick = 0; Pick < Duel.BallPicks.Num(); ++Pick)
		{
			const FVector2D BallAt((Pick - (Duel.BallPicks.Num() - 1) * 0.5f) * 26.0f, 128.0f);
			const FLinearColor BallColor = ChaosImpactBallTypes::GetColor(static_cast<EChaosImpactBallType>(Duel.BallPicks[Pick]));
			Balls.Disc(BallAt + FVector2D(1.5, 2.0), 10.0f, WithAlpha(Extrude, 0.9f));
			Balls.Disc(BallAt, 10.0f, Navy);
			HudAbove(Balls, 1).Disc(BallAt, 7.5f, BallColor);
			HudAbove(Balls, 2).Disc(BallAt + FVector2D(-2.5, -2.5), 2.2f, WithAlpha(Paper, 0.85f));
		}
	}

	// Under the clock: how each round went (the winner's colour, grey for a drawn one).
	const int32 Rounds = Duel.RoundResults.Num();
	for (int32 Round = 0; Round < ChaosImpactMatch::DuelMaxRounds; ++Round)
	{
		const float X = (Round - (ChaosImpactMatch::DuelMaxRounds - 1) * 0.5f) * 26.0f;
		const FPainter Mark{TopCenter, OutDrawElements, BaseLayer + 4};
		const bool bPlayed = Round < Rounds;
		const int32 Winner = bPlayed ? Duel.RoundResults[Round] : INDEX_NONE;
		Mark.Box(X - 9.0f, 104.0f, 18.0f, 6.0f, !bPlayed ? WithAlpha(Paper, Round == Duel.Round - 1 ? 0.5f : 0.15f)
			: Winner < 0 ? WithAlpha(Paper, 0.6f) : DuelSideColor(Winner));
	}
}

void UChaosImpactMatchAnnouncerWidget::PaintDuelBallSlot(const FGeometry& Center, FSlateWindowElementList& OutDrawElements,
	const int32 BaseLayer, const AChaosImpactGameState& Match, const float Time) const
{
	using namespace ChaosImpactPaint;
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const FChaosImpactDuelState& Duel = Match.Duel;
	const float Age = Match.GetDuelStepElapsed();
	const float Alpha = EaseOut(Age / 0.3f) * FMath::Clamp(Match.GetDuelStepRemaining() / 0.35f, 0.0f, 1.0f);
	if (Alpha <= 0.0f)
	{
		return;
	}
	// The reels hold every kind that can be drawn, in the same order as the draw itself.
	TArray<EChaosImpactBallType> Pool;
	for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
	{
		const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(Type);
		if (Kind != EChaosImpactBallType::Nova && Kind != EChaosImpactBallType::Prison)
		{
			Pool.Add(Kind);
		}
	}
	const float LastStop = ChaosImpactMatch::DuelReelStopSeconds[ChaosImpactMatch::DuelBallPicks - 1];
	const bool bAllStopped = Age >= LastStop;
	const float Rise = (1.0f - EaseOut(Age / 0.35f)) * 30.0f;

	// The machine: a dark slab with a gold rim, its title above the reels.
	const FPainter Back{Center, OutDrawElements, BaseLayer, Alpha};
	HudShard(Back, {At(-400.0f, -178.0f + Rise), At(392.0f, -186.0f + Rise), At(410.0f, 140.0f + Rise), At(-386.0f, 150.0f + Rise)}, NightFill, 4.0f,
		FVector2D(8.0, 9.0));
	const FPainter Face = HudAbove(Back);
	HudStroke(Face, {At(-394.0f, -172.0f + Rise), At(388.0f, -180.0f + Rise)}, Gold, 4.0f);
	Face.Text(TEXT("BALL SLOT"), 0.0f, -170.0f + Rise, 18.0f, Gold, ETextAlign::Center, TEXT("BlackItalic"), 1.5f, Navy);
	const float TitlePop = bAllStopped ? 1.0f + 0.25f * FMath::Exp(-9.0f * (Age - LastStop)) : 1.0f;
	const FGeometry TitleSpace = MakeSkewed(Center, -400.0f, -146.0f + Rise, 800.0f, 44.0f, 0.0f, TitlePop);
	HudText3D(FPainter{TitleSpace, OutDrawElements, BaseLayer + 8, Alpha}, bAllStopped
		? FString::Printf(TEXT("この%d種類で勝負！"), ChaosImpactMatch::DuelBallPicks) : FString(TEXT("使用ボール抽選")), 400.0f, 0.0f, 32.0f,
		bAllStopped ? Gold : Paper, ETextAlign::Center, TEXT("Black"));

	constexpr float ItemHeight = 130.0f;
	constexpr float WindowHalf = 100.0f;
	for (int32 Reel = 0; Reel < ChaosImpactMatch::DuelBallPicks; ++Reel)
	{
		const float X = (Reel - 1) * 250.0f;
		const float Y = 10.0f + Rise;
		const EChaosImpactBallType Picked = Duel.BallPicks.IsValidIndex(Reel)
			? static_cast<EChaosImpactBallType>(Duel.BallPicks[Reel]) : EChaosImpactBallType::Fire;
		const float Stop = ChaosImpactMatch::DuelReelStopSeconds[Reel];
		const bool bStopped = Age >= Stop;
		const FLinearColor PickedColor = ChaosImpactBallTypes::GetColor(Picked);
		// The window.
		Face.Box(X - 104.0f, Y - WindowHalf, 208.0f, WindowHalf * 2.0f, FLinearColor(0.008f, 0.012f, 0.028f, 0.96f));
		// The strip turns several times round (later reels longer) and slows to land on the drawn kind.
		const int32 Landing = FMath::Max(0, Pool.IndexOfByKey(Picked));
		const float Final = ((3 + Reel * 2) * Pool.Num() + Landing) * ItemHeight;
		const float Along = FMath::Clamp(Age / Stop, 0.0f, 1.0f);
		const float Position = Final * (1.0f - FMath::Pow(1.0f - Along, 3.0f));
		const int32 First = FMath::FloorToInt(Position / ItemHeight) - 1;
		const float Speed = bStopped ? 0.0f : 3.0f * Final * FMath::Square(1.0f - Along) / Stop;
		// Only what is inside the window shows (the strip runs on behind the slab).
		OutDrawElements.PushClip(FSlateClippingZone(Center.ToPaintGeometry(FVector2f(208.0f, WindowHalf * 2.0f),
			FSlateLayoutTransform(FVector2f(X - 104.0f, Y - WindowHalf)))));
		for (int32 Item = First; Item <= First + 3; ++Item)
		{
			const float Offset = Item * ItemHeight - Position;
			const float Seen = FMath::Clamp(1.25f - FMath::Abs(Offset) / (WindowHalf + 20.0f), 0.0f, 1.0f);
			if (Seen <= 0.0f)
			{
				continue;
			}
			const EChaosImpactBallType Kind = Pool[((Item % Pool.Num()) + Pool.Num()) % Pool.Num()];
			const FLinearColor Color = ChaosImpactBallTypes::GetColor(Kind);
			const bool bLanded = bStopped && Item == (3 + Reel * 2) * Pool.Num() + Landing;
			const float Pop = bLanded ? 1.0f + 0.3f * FMath::Exp(-10.0f * (Age - Stop)) : 1.0f;
			// Once stopped, the neighbours fade and the drawn kind stands alone in its window.
			const float Settle = bStopped && !bLanded ? FMath::Clamp(1.0f - (Age - Stop) / 0.25f, 0.0f, 1.0f) : 1.0f;
			const FPainter Ball{Center, OutDrawElements, BaseLayer + 3, Alpha * Seen * Settle};
			const FVector2D BallAt(X, Y + Offset - 14.0f);
			// Fast reels smear a little along their travel.
			if (Speed > 900.0f)
			{
				Ball.Disc(BallAt - FVector2D(0.0, 18.0), 34.0f, WithAlpha(Color, 0.25f));
			}
			Ball.Disc(BallAt + FVector2D(3.0, 4.0), 40.0f * Pop, WithAlpha(Extrude, 0.9f));
			Ball.Disc(BallAt, 40.0f * Pop, Navy);
			HudAbove(Ball, 1).Disc(BallAt, 35.0f * Pop, Color);
			HudAbove(Ball, 2).Disc(BallAt + FVector2D(-11.0, -12.0) * Pop, 9.0f * Pop, WithAlpha(Paper, 0.75f));
			HudAbove(Ball, 3).Text(ChaosImpactBallTypes::GetDisplayName(Kind), X, BallAt.Y + 44.0f, 22.0f, bLanded ? Paper : WithAlpha(Paper, 0.8f),
				ETextAlign::Center, TEXT("Black"), 2.0f, Navy);
		}
		OutDrawElements.PopClip();
		// The frame: the drawn kind's colour once the reel has stopped, with a flash as it lands.
		const float Landed = bStopped ? Age - Stop : -1.0f;
		const FPainter Frame{Center, OutDrawElements, BaseLayer + 8, Alpha};
		const FLinearColor Rim = bStopped ? FMath::Lerp(Paper, PickedColor, FMath::Clamp(Landed / 0.3f, 0.0f, 1.0f)) : WithAlpha(Paper, 0.35f);
		Frame.Outline(X - 104.0f, Y - WindowHalf, 208.0f, WindowHalf * 2.0f, Rim, bStopped ? 5.0f : 3.0f);
		if (bStopped && Landed < 0.5f)
		{
			const float Flash = 1.0f - Landed / 0.5f;
			Frame.Box(X - 104.0f, Y - WindowHalf, 208.0f, WindowHalf * 2.0f, WithAlpha(Paper, 0.35f * Flash * Flash));
			Frame.Ring(FVector2D(X, Y - 14.0f), 44.0f + 70.0f * (1.0f - Flash), WithAlpha(PickedColor, Flash), 4.0f);
		}
		else if (bStopped)
		{
			// A slow glow round the kinds drawn while the title is shown.
			Frame.Outline(X - 110.0f, Y - WindowHalf - 6.0f, 220.0f, WindowHalf * 2.0f + 12.0f,
				WithAlpha(PickedColor, 0.35f + 0.25f * FMath::Sin(Time * 6.0f + Reel)), 3.0f);
		}
	}
	if (bAllStopped)
	{
		const float In = EaseOut((Age - LastStop) / 0.3f);
		FPainter{Center, OutDrawElements, BaseLayer + 8, Alpha * In}.Text(TEXT("ノーマルと この3種類がステージに出ます"), 0.0f, 120.0f + Rise, 20.0f,
			WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Bold"), 2.0f, Navy);
	}
}
