#include "ChaosImpactMatchAnnouncerWidget.h"

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
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
}

int32 UChaosImpactMatchAnnouncerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);
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

	// A word scaled about the screen centre: it lands slightly large and settles.
	const auto PaintCallout = [&](const FString& Word, const float TextSize, const float Scale, const float Alpha,
		const FLinearColor& Color, const int32 Layer)
	{
		const FGeometry Space = MakeSkewed(Center, -800.0f, -TextSize * 0.8f, 1600.0f, TextSize * 1.6f, 0.0f, Scale);
		const FPainter Callout{Space, OutDrawElements, Layer, Alpha};
		Callout.Text(Word, 800.0f, 0.0f, TextSize, Color, ETextAlign::Center, TEXT("Black"), 5.0f, Ink);
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
			PaintCallout(TEXT("Ready?"), 96.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * T), EaseOut(T / 0.12f), Paper, BaseLayer + 2);
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
		return BaseLayer + 3;
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
		const FPainter Clock{TopCenter, OutDrawElements, BaseLayer + 1};
		Clock.Box(-60.0f, 10.0f, 120.0f, 40.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.42f));
		Clock.Text(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 0.0f, 12.0f, 28.0f,
			Remaining <= 10.0f ? Fire : bMinuteCall ? Gold : Paper, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
		if (bMinuteCall)
		{
			// Like Ready?: the words alone, no band over the play field, and gone quickly.
			const float Alpha = EaseOut(MinuteLeftAge / 0.12f) * (1.0f - FMath::Clamp((MinuteLeftAge - 1.4f) / 0.4f, 0.0f, 1.0f));
			PaintCallout(TEXT("あと1分！"), 96.0f, 1.0f + 0.6f * FMath::Exp(-11.0f * MinuteLeftAge), Alpha, Gold, BaseLayer + 2);
		}

		if (Elapsed < 1.1f)
		{
			PaintCallout(TEXT("GO!"), 150.0f, FMath::Lerp(2.0f, 1.0f, EaseOut(Elapsed / 0.18f)),
				1.0f - FMath::Clamp((Elapsed - 0.7f) / 0.4f, 0.0f, 1.0f), Paper, BaseLayer + 2);
		}
		if (Remaining > 0.0f && Remaining <= 5.0f)
		{
			const float Local = static_cast<float>(Seconds) - Remaining;
			PaintCallout(FString::FromInt(Seconds), 120.0f, 1.0f + 0.5f * FMath::Exp(-8.0f * Local),
				0.7f * (1.0f - 0.6f * Local), Paper, BaseLayer + 2);
		}
		return BaseLayer + 3;
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
		// The whistle moment: a flash, then FINISH slams in over the frozen field.
		Full.Box(0.0f, 0.0f, Size.X, Size.Y, WithAlpha(Paper, 0.45f * FMath::Clamp(1.0f - Elapsed / 0.25f, 0.0f, 1.0f)));
		PaintCallout(TEXT("FINISH"), 120.0f, FMath::Lerp(1.8f, 1.0f, EaseOut(Elapsed / 0.2f)),
			1.0f - FMath::Clamp((Elapsed - 1.3f) / 0.4f, 0.0f, 1.0f), Paper, BaseLayer + 3);
	}
	const float Show = Elapsed - 1.5f;
	if (Show <= 0.0f)
	{
		return BaseLayer + 4;
	}
	Full.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, 0.62f * EaseOut(Show / 0.5f)));

	// Players on this machine get their P tag in the list.
	TArray<const APlayerState*, TInlineAllocator<4>> LocalStates;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			const APlayerController* LocalController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
			LocalStates.Add(LocalController ? LocalController->PlayerState.Get() : nullptr);
		}
	}
	const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
	const auto RankOf = [&Ranking](const int32 Index)
	{
		int32 Rank = 1;
		for (int32 Other = 0; Other < Index; ++Other)
		{
			Rank += Ranking[Other]->Points > Ranking[Index]->Points ? 1 : 0;
		}
		return Rank;
	};
	const auto ColorOf = [bTeams](const AChaosImpactPlayerState* Member)
	{
		return bTeams && Member->TeamIndex >= 0 ? ChaosImpactMatch::GetTeamColor(Member->TeamIndex)
			: Member->IsABot() ? Muted : Paper;
	};

	const FPainter Board{Center, OutDrawElements, BaseLayer + 2};
	{
		// A small caption whose rules open outwards.
		const float In = EaseOut(Show / 0.5f);
		const FPainter Caption{Center, OutDrawElements, BaseLayer + 2, In};
		Caption.Text(TEXT("RESULT"), 0.0f, -352.0f, 20.0f, WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Bold"));
		Caption.Box(-60.0f - 180.0f * In, -338.0f, 180.0f * In, 1.0f, WithAlpha(Paper, 0.4f));
		Caption.Box(60.0f, -338.0f, 180.0f * In, 1.0f, WithAlpha(Paper, 0.4f));
	}

	float WinnerAt = 0.0f;
	FString Headline;
	FLinearColor HeadlineColor = Gold;
	float ListTop = -200.0f;

	if (bTeams)
	{
		// Team totals count up while each bar grows to its share of the best total.
		TArray<TPair<int32, int32>> Teams;
		for (int32 Team = 0; Team < Match->Rules.TeamCount; ++Team)
		{
			Teams.Add({Team, Match->GetTeamPoints(Team)});
		}
		Teams.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Value > B.Value; });
		const int32 Best = FMath::Max(1, Teams[0].Value);
		const bool bTie = Teams.Num() > 1 && Teams[1].Value == Teams[0].Value;
		for (int32 Index = 0; Index < Teams.Num(); ++Index)
		{
			const float Grow = EaseOut((Show - 0.4f - Index * 0.12f) / 1.1f);
			if (Grow <= 0.0f)
			{
				continue;
			}
			const float Y = -240.0f + Index * 54.0f;
			const FLinearColor TeamColor = ChaosImpactMatch::GetTeamColor(Teams[Index].Key);
			const FPainter Bar{Center, OutDrawElements, BaseLayer + 2, FMath::Min(1.0f, Grow * 3.0f)};
			Bar.Box(-320.0f, Y, 640.0f, 44.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
			Bar.Box(-320.0f, Y, 640.0f * Grow * Teams[Index].Value / Best, 44.0f, WithAlpha(TeamColor, 0.78f));
			Bar.Text(ChaosImpactMatch::GetTeamName(Teams[Index].Key), -304.0f, Y + 9.0f, 22.0f, Paper,
				ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
			Bar.Text(FString::FromInt(FMath::RoundToInt(Teams[Index].Value * Grow)), 304.0f, Y + 6.0f, 26.0f, Paper,
				ETextAlign::Right, TEXT("Black"), 2.0f, Ink);
			if (Index == 0 && !bTie && Show > 1.7f)
			{
				Bar.Outline(-322.0f, Y - 2.0f, 644.0f, 48.0f,
					WithAlpha(Paper, 0.5f + 0.4f * FMath::Sin((Show - 1.7f) * 6.0f)), 2.0f);
			}
		}
		WinnerAt = 1.8f;
		Headline = bTie ? FString(TEXT("DRAW"))
			: FString::Printf(TEXT("%sチームの勝ち！"), ChaosImpactMatch::GetTeamName(Teams[0].Key));
		HeadlineColor = bTie ? Paper : ChaosImpactMatch::GetTeamColor(Teams[0].Key);
		ListTop = -240.0f + Teams.Num() * 54.0f + 26.0f;

		// Everyone's own score, in a compact list under the bars.
		for (int32 Index = 0; Index < Ranking.Num(); ++Index)
		{
			const float RowIn = EaseOut((Show - 2.1f - Index * 0.07f) / 0.3f);
			if (RowIn <= 0.0f)
			{
				continue;
			}
			const AChaosImpactPlayerState* Member = Ranking[Index];
			const float Y = ListTop + Index * 30.0f + (1.0f - RowIn) * 10.0f;
			const FPainter Row{Center, OutDrawElements, BaseLayer + 2, RowIn};
			const int32 LocalIndex = LocalStates.IndexOfByKey(Member);
			Row.Box(-320.0f, Y, 640.0f, 26.0f, FLinearColor(0.0f, 0.0f, 0.0f, LocalIndex >= 0 ? 0.5f : 0.3f));
			Row.Box(-320.0f, Y, 3.0f, 26.0f, ColorOf(Member));
			Row.Text(GetAnnouncerName(Member), -300.0f, Y + 3.0f, 17.0f, Paper, ETextAlign::Left, TEXT("Regular"));
			// The tag is only useful when the name does not already say which player it is.
			if (LocalIndex >= 0 && GetAnnouncerName(Member) != FString::Printf(TEXT("P%d"), LocalIndex + 1))
			{
				Row.Text(FString::Printf(TEXT("P%d"), LocalIndex + 1), -60.0f, Y + 4.0f, 15.0f,
					PlayerAccents[LocalIndex % 4], ETextAlign::Left, TEXT("Bold"));
			}
			Row.Text(FString::Printf(TEXT("KO %d"), Member->Knockouts), 200.0f, Y + 5.0f, 14.0f, Muted,
				ETextAlign::Right, TEXT("Regular"));
			Row.Text(FString::Printf(TEXT("%d pt"), Member->Points), 304.0f, Y + 3.0f, 17.0f, Paper,
				ETextAlign::Right, TEXT("Bold"));
		}
	}
	else if (!Ranking.IsEmpty())
	{
		// Places are revealed from last to first, each score counting up; the winner comes last and larger.
		const int32 Count = Ranking.Num();
		constexpr float RowStep = 42.0f;
		const float RowsTop = -150.0f;
		for (int32 Index = Count - 1; Index >= 0; --Index)
		{
			const float RevealAt = 0.5f + (Count - 1 - Index) * 0.32f;
			const float RowIn = EaseOut((Show - RevealAt) / 0.3f);
			if (RowIn <= 0.0f)
			{
				continue;
			}
			const AChaosImpactPlayerState* Member = Ranking[Index];
			const bool bFirst = Index == 0;
			const float Height = bFirst ? 54.0f : 34.0f;
			const float Y = bFirst ? RowsTop - 70.0f : RowsTop + (Index - 1) * RowStep;
			const float Slide = (1.0f - RowIn) * 24.0f;
			const float Counted = EaseOut((Show - RevealAt) / 0.45f);
			const FPainter Row{Center, OutDrawElements, BaseLayer + 2, RowIn};
			const int32 LocalIndex = LocalStates.IndexOfByKey(Member);
			const int32 Rank = RankOf(Index);
			Row.Box(-320.0f + Slide, Y, 640.0f, Height, FLinearColor(0.0f, 0.0f, 0.0f, bFirst ? 0.55f : 0.36f));
			Row.Box(-320.0f + Slide, Y, bFirst ? 5.0f : 3.0f, Height, bFirst ? Gold : ColorOf(Member));
			const float TextSize = bFirst ? 28.0f : 19.0f;
			const float TextY = Y + (Height - TextSize * 1.3f) * 0.5f;
			Row.Text(FString::FromInt(Rank), -290.0f + Slide, TextY, TextSize, Rank == 1 ? Gold : Muted,
				ETextAlign::Center, TEXT("Bold"));
			Row.Text(GetAnnouncerName(Member), -258.0f + Slide, TextY, TextSize, Paper, ETextAlign::Left,
				bFirst ? TEXT("Black") : TEXT("Regular"), bFirst ? 2.0f : 0.0f, Ink);
			// The tag is only useful when the name does not already say which player it is.
			if (LocalIndex >= 0 && GetAnnouncerName(Member) != FString::Printf(TEXT("P%d"), LocalIndex + 1))
			{
				Row.Text(FString::Printf(TEXT("P%d"), LocalIndex + 1), 40.0f + Slide, TextY + 3.0f, 15.0f,
					PlayerAccents[LocalIndex % 4], ETextAlign::Left, TEXT("Bold"));
			}
			Row.Text(FString::Printf(TEXT("KO %d"), Member->Knockouts), 190.0f + Slide, TextY + 4.0f, 14.0f, Muted,
				ETextAlign::Right, TEXT("Regular"));
			Row.Text(FString::Printf(TEXT("%d pt"), FMath::RoundToInt(Member->Points * Counted)), 304.0f + Slide, TextY,
				TextSize, bFirst ? Gold : Paper, ETextAlign::Right, TEXT("Bold"), bFirst ? 2.0f : 0.0f, Ink);
			if (bFirst && Show > RevealAt + 0.3f)
			{
				// A thin gold line sweeps under the winner once they land.
				const float Sweep = EaseOut((Show - RevealAt - 0.3f) / 0.5f);
				Row.Box(-320.0f, Y + Height + 3.0f, 640.0f * Sweep, 2.0f, Gold);
			}
		}
		WinnerAt = 0.5f + (Count - 1) * 0.32f + 0.35f;
		const bool bTie = Count > 1 && Ranking[1]->Points == Ranking[0]->Points;
		Headline = bTie ? FString(TEXT("DRAW")) : FString(TEXT("WINNER"));
		HeadlineColor = bTie ? Paper : Gold;
	}

	// The headline lands once the winner is known, followed by a short fall of confetti.
	if (Show > WinnerAt && !Headline.IsEmpty())
	{
		const float T = Show - WinnerAt;
		const FGeometry HeadSpace = MakeSkewed(Center, -800.0f, -318.0f, 1600.0f, 70.0f, 0.0f,
			1.0f + 0.35f * FMath::Exp(-10.0f * T));
		const FPainter Head{HeadSpace, OutDrawElements, BaseLayer + 3, EaseOut(T / 0.15f)};
		Head.Text(Headline, 800.0f, 0.0f, 44.0f, HeadlineColor, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);

		if (Headline != TEXT("DRAW") && T < 4.0f)
		{
			const FLinearColor ConfettiColors[] = {Gold, Paper, Ice, Fire, HeadlineColor};
			const FPainter Confetti{Center, OutDrawElements, BaseLayer + 1, FMath::Clamp((4.0f - T) / 0.8f, 0.0f, 1.0f)};
			const float HalfWidth = Size.X * 0.5f / S;
			const float HalfHeight = Size.Y * 0.5f / S;
			for (int32 Piece = 0; Piece < 70; ++Piece)
			{
				const float Speed = 180.0f + 260.0f * AnnouncerHash(Piece, 1.0f);
				const float Y = -HalfHeight - 40.0f + (T * Speed) - AnnouncerHash(Piece, 2.0f) * 400.0f;
				if (Y < -HalfHeight - 20.0f || Y > HalfHeight)
				{
					continue;
				}
				const float X = (AnnouncerHash(Piece, 3.0f) * 2.0f - 1.0f) * HalfWidth
					+ FMath::Sin(T * (2.0f + AnnouncerHash(Piece, 4.0f) * 3.0f) + Piece) * 18.0f;
				// A flat piece seen edge-on now and then, which reads as tumbling.
				const float Turn = FMath::Abs(FMath::Sin(T * (4.0f + AnnouncerHash(Piece, 5.0f) * 6.0f) + Piece));
				Confetti.Box(X, Y, 3.0f + 6.0f * Turn, 10.0f, ConfettiColors[Piece % UE_ARRAY_COUNT(ConfettiColors)]);
			}
		}
	}
	return BaseLayer + 4;
}
