#include "ChaosImpactMatchAnnouncerWidget.h"

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
	FString GetAnnouncerName(const AChaosImpactPlayerState* Member)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		return Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
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
		else if (Match->Phase == EChaosImpactOnlinePhase::Match)
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
		PaintCallout(bKnockout ? TEXT("KNOCKOUT!") : TEXT("FINISH"), bKnockout ? 128.0f : 120.0f, FMath::Lerp(1.8f, 1.0f, EaseOut(Elapsed / 0.2f)),
			1.0f - FMath::Clamp((Elapsed - 1.3f) / 0.4f, 0.0f, 1.0f), Paper, BaseLayer + 3, bKnockout ? &KnockoutBlast : &FireFill);
	}
	return BaseLayer + 8;
}
