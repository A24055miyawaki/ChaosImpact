#include "ChaosImpactMatchAnnouncerWidget.h"

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactResults.h"
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
	return BaseLayer + 4;
}
