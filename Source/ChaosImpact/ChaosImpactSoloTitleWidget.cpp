#include "ChaosImpactSoloTitleWidget.h"

#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSoloTitle.h"

namespace
{
	constexpr float PlanW = 1600.0f;
	constexpr float PlanH = 900.0f;
	constexpr float CardW = 400.0f;
	constexpr float CardH = 290.0f;
	constexpr float CardTop = 505.0f;
	constexpr float CardGap = 40.0f;

	FVector2D CardPlan(const int32 Index)
	{
		const float Left = (PlanW - (CardW * 3.0f + CardGap * 2.0f)) * 0.5f;
		return FVector2D(Left + Index * (CardW + CardGap), CardTop);
	}
}

float UChaosImpactSoloTitleWidget::PlanScale(const FVector2D& Size)
{
	return static_cast<float>(FMath::Max(0.1, FMath::Min(Size.X / PlanW, Size.Y / PlanH)));
}

FVector2D UChaosImpactSoloTitleWidget::PlanOrigin(const FVector2D& Size)
{
	const float S = PlanScale(Size);
	return FVector2D((Size.X - PlanW * S) * 0.5, (Size.Y - PlanH * S) * 0.5);
}

FBox2D UChaosImpactSoloTitleWidget::CardRect(const FVector2D& Size, const int32 Index)
{
	const float S = PlanScale(Size);
	const FVector2D O = PlanOrigin(Size) + CardPlan(Index) * S;
	return FBox2D(O, O + FVector2D(CardW, CardH) * S);
}

FBox2D UChaosImpactSoloTitleWidget::EraseRect(const FVector2D& Size, const int32 Index)
{
	const float S = PlanScale(Size);
	const FVector2D O = PlanOrigin(Size) + (CardPlan(Index) + FVector2D(CardW - 150.0f, CardH - 60.0f)) * S;
	return FBox2D(O, O + FVector2D(126.0f, 42.0f) * S);
}

FBox2D UChaosImpactSoloTitleWidget::AnswerRect(const FVector2D& Size, const bool bYes)
{
	const float S = PlanScale(Size);
	const FVector2D O = PlanOrigin(Size) + FVector2D(bYes ? 560.0f : 820.0f, 560.0f) * S;
	return FBox2D(O, O + FVector2D(220.0f, 70.0f) * S);
}

int32 UChaosImpactSoloTitleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;
	using EStep = EChaosImpactSoloTitleStep;

	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AChaosImpactSoloTitle* T = Title.Get();
	const FVector2f Size2f = AllottedGeometry.GetLocalSize();
	if (!T || Size2f.X < 1.0f)
	{
		return Base;
	}
	const FVector2D Size(Size2f.X, Size2f.Y);
	const float S = PlanScale(Size);
	const FVector2D Origin = PlanOrigin(Size);
	const FGeometry Plan = MakeAnchor(AllottedGeometry, static_cast<float>(Origin.X), static_cast<float>(Origin.Y), S);
	const FGeometry Whole = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, 1.0f);
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	const float Time = T->GetAge();
	const EStep Step = T->GetStep();
	const float StepAge = T->GetStepAge();
	const bool bPad = [this]()
	{
		const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		return PC && PC->IsUsingGamepad();
	}();
	const FHudFill Glow = MakeFill(FLinearColor(1.0f, 0.97f, 0.82f), FLinearColor(1.0f, 0.66f, 0.2f));
	const FLinearColor Dusk(0.06f, 0.04f, 0.16f);

	// ---- The screen darkened toward the bottom (the cards stand on it) and at the edges.
	{
		const FPainter Shade{Whole, OutDrawElements, Base};
		const int32 Bands = 14;
		for (int32 I = 0; I < Bands; ++I)
		{
			const float A = static_cast<float>(I) / Bands;
			Shade.Box(0.0f, Size2f.Y * (0.45f + 0.55f * A), Size2f.X, Size2f.Y * 0.55f / Bands + 1.0f, WithAlpha(Dusk, 0.05f + 0.6f * A * A));
		}
		Shade.Box(0.0f, 0.0f, Size2f.X, Size2f.Y * 0.08f, WithAlpha(Dusk, 0.35f));
	}

	// ---- The logo: big over the temple while waiting, smaller and higher with the files.
	{
		const float ToFiles = Step == EStep::Press ? 0.0f : Step == EStep::Leaving ? 1.0f : EaseOut(StepAge / 0.45f);
		const float LogoIn = EaseOut((Time - 0.6f) / 1.2f);
		const float Scale = FMath::Lerp(1.0f, 0.62f, ToFiles);
		const FVector2D Centre = FMath::Lerp(At(800.0f, 300.0f), At(800.0f, 150.0f), ToFiles);
		const FGeometry Logo = MakeAnchor(Plan, static_cast<float>(Centre.X), static_cast<float>(Centre.Y), Scale);
		// Its light: a slow wheel of rays and a soft halo.
		const FPainter Rays{Logo, OutDrawElements, Base + 4, LogoIn * 0.32f};
		HudBurst(Rays, At(0.0f, -10.0f), 120.0f, 330.0f, 18, Glow, Time * 4.0f, FVector2D(1.7, 0.75), 0.0f, FVector2D::ZeroVector);
		const FPainter Halo{Logo, OutDrawElements, Base + 3, LogoIn};
		for (int32 I = 0; I < 6; ++I)
		{
			Halo.Disc(At(0.0f, -10.0f), 230.0f - I * 32.0f, FLinearColor(1.0f, 0.78f, 0.45f, 0.035f));
		}
		HudText3D(FPainter{Logo, OutDrawElements, Base + 10, LogoIn}, TEXT("避球"), 0.0f, -150.0f, 150.0f, FLinearColor(1.0f, 0.97f, 0.88f),
			ETextAlign::Center, TEXT("BlackItalic"), 9.0f);
		FPainter{Logo, OutDrawElements, Base + 14, LogoIn}.Text(TEXT("C H A O S   I M P A C T"), 0.0f, 52.0f, 26.0f, FLinearColor(1.0f, 0.86f, 0.55f),
			ETextAlign::Center, TEXT("BlackItalic"), 2.0f, Navy);
		// The mode's ribbon.
		const FPainter Ribbon{Logo, OutDrawElements, Base + 16, LogoIn};
		HudShard(Ribbon, {At(-250.0f, 100.0f), At(250.0f, 92.0f), At(236.0f, 150.0f), At(-236.0f, 158.0f)},
			MakeFill(FLinearColor(0.28f, 0.16f, 0.6f, 0.96f), FLinearColor(0.08f, 0.04f, 0.22f, 0.96f)), 3.0f, FVector2D(6.0, 7.0));
		HudStroke(HudAbove(Ribbon, 3), {At(-200.0f, 146.0f), At(200.0f, 140.0f)}, FLinearColor(1.0f, 0.75f, 0.3f), 2.5f);
		HudText3D(FPainter{Logo, OutDrawElements, Base + 22, LogoIn}, TEXT("ソロアドベンチャー"), 0.0f, 100.0f, 34.0f, Paper, ETextAlign::Center);
		FPainter{Logo, OutDrawElements, Base + 24, LogoIn * (1.0f - ToFiles)}.Text(TEXT("ドッチ・ボール大統領から、世界とボールを取り戻せ"),
			0.0f, 176.0f, 20.0f, FLinearColor(0.92f, 0.88f, 1.0f), ETextAlign::Center, TEXT("Bold"), 2.0f, Navy);
	}

	// ---- Waiting for a press.
	if (Step == EStep::Press && Time > 1.6f)
	{
		const float Blink = 0.55f + 0.45f * FMath::Sin(Time * 3.2f);
		const FPainter Press{Plan, OutDrawElements, Base + 30, EaseOut((Time - 1.6f) / 0.6f)};
		HudText3D(Press, TEXT("PRESS START"), 800.0f, 640.0f, 40.0f, WithAlpha(FLinearColor(1.0f, 0.9f, 0.6f), Blink), ETextAlign::Center);
		Press.Text(bPad ? TEXT("A ボタン") : TEXT("クリック / Enter"), 800.0f, 700.0f, 18.0f, FLinearColor(0.9f, 0.92f, 1.0f, 0.85f), ETextAlign::Center,
			TEXT("Black"), 2.0f, Navy);
	}

	// ---- The files.
	if (Step == EStep::Files || Step == EStep::ConfirmErase || Step == EStep::Leaving)
	{
		const float In = Step == EStep::Files ? EaseOut(StepAge / 0.4f) : 1.0f;
		FPainter{Plan, OutDrawElements, Base + 30, In}.Text(TEXT("ファイルをえらんでください"), 800.0f, 440.0f, 22.0f, Paper, ETextAlign::Center,
			TEXT("Black"), 3.0f, Navy);
		for (int32 I = 0; I < 3; ++I)
		{
			const FChaosImpactSoloFileView& File = T->GetFile(I);
			const bool bPicked = I == T->GetSelected();
			const float Lift = bPicked ? -10.0f - 3.0f * FMath::Sin(Time * 4.0f) : 0.0f;
			const FVector2D C = CardPlan(I) + FVector2D(0.0, Lift + (1.0f - In) * 60.0f);
			const float L = static_cast<float>(C.X);
			const float Tp = static_cast<float>(C.Y);
			const FPainter Card{Plan, OutDrawElements, Base + 40 + I * 12, In * (bPicked || Step != EStep::ConfirmErase ? 1.0f : 0.6f)};
			if (bPicked)
			{
				for (int32 G = 0; G < 4; ++G)
				{
					Card.Box(L - 6.0f - G * 4.0f, Tp - 6.0f - G * 4.0f, CardW + 12.0f + G * 8.0f, CardH + 12.0f + G * 8.0f,
						FLinearColor(1.0f, 0.75f, 0.3f, 0.06f));
				}
			}
			HudShard(Card, {At(L + 14.0f, Tp), At(L + CardW, Tp - 6.0f), At(L + CardW - 12.0f, Tp + CardH), At(L, Tp + CardH + 4.0f)},
				bPicked ? MakeFill(FLinearColor(0.3f, 0.2f, 0.62f, 0.97f), FLinearColor(0.1f, 0.05f, 0.26f, 0.97f)) : NightFill, bPicked ? 4.0f : 3.0f,
				FVector2D(7.0, 8.0));
			const FPainter Top = HudAbove(Card, 4);
			// The file's number in a burst.
			HudBurst(Top, At(L + 56.0f, Tp + 52.0f), 30.0f, 44.0f, 9, File.bExists ? Glow : SilverFill, Time * (bPicked ? 30.0f : 6.0f),
				FVector2D(1.0, 1.0), 3.0f, FVector2D(4.0, 5.0), static_cast<float>(I));
			HudText3D(HudAbove(Top, 4), FString::FromInt(I + 1), L + 56.0f, Tp + 28.0f, 34.0f, Paper, ETextAlign::Center);
			const FPainter Words = HudAbove(Top, 8);
			Words.Text(FString::Printf(TEXT("ファイル %d"), I + 1), L + 108.0f, Tp + 30.0f, 24.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"), 2.0f, Navy);
			if (!File.bExists)
			{
				Words.Text(TEXT("NEW GAME"), L + CardW * 0.5f, Tp + 126.0f, 34.0f, FLinearColor(1.0f, 0.88f, 0.55f), ETextAlign::Center, TEXT("BlackItalic"),
					3.0f, Navy);
				Words.Text(TEXT("はじめから冒険をはじめる"), L + CardW * 0.5f, Tp + 180.0f, 16.0f, FLinearColor(0.85f, 0.88f, 1.0f), ETextAlign::Center,
					TEXT("Bold"));
				continue;
			}
			// How far it has come: cleared stages, a bar, where it stands, when it was played.
			Words.Text(File.Map == 2 ? TEXT("デットボール合衆国") : TEXT("占領された世界"), L + 108.0f, Tp + 66.0f, 15.0f,
				File.Map == 2 ? FLinearColor(1.0f, 0.6f, 0.55f) : FLinearColor(0.6f, 0.85f, 1.0f), ETextAlign::Left, TEXT("Black"));
			Words.Text(FString::Printf(TEXT("クリア %d / %d"), File.Cleared, File.Total), L + 28.0f, Tp + 104.0f, 26.0f, Paper, ETextAlign::Left,
				TEXT("BlackItalic"), 2.0f, Navy);
			if (File.bAllClear)
			{
				Words.Text(TEXT("★ ALL CLEAR"), L + CardW - 28.0f, Tp + 110.0f, 18.0f, Gold, ETextAlign::Right, TEXT("BlackItalic"), 2.0f, Navy);
			}
			const float Fill = File.Total > 0 ? static_cast<float>(File.Cleared) / File.Total : 0.0f;
			Words.Box(L + 28.0f, Tp + 146.0f, CardW - 56.0f, 10.0f, FLinearColor(0.02f, 0.03f, 0.1f, 0.9f));
			HudAbove(Words, 1).Box(L + 28.0f, Tp + 146.0f, (CardW - 56.0f) * Fill, 10.0f, FLinearColor(1.0f, 0.72f, 0.25f));
			Words.Text(File.Area, L + 28.0f, Tp + 168.0f, 14.0f, FLinearColor(0.75f, 0.8f, 0.95f), ETextAlign::Left, TEXT("Black"));
			Words.Text(File.Place, L + 28.0f, Tp + 188.0f, 20.0f, Paper, ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
			Words.Text(File.LastPlayed.GetTicks() > 0 ? File.LastPlayed.ToString(TEXT("%Y/%m/%d %H:%M")) : FString(), L + 28.0f, Tp + 240.0f, 14.0f,
				FLinearColor(0.7f, 0.74f, 0.88f), ETextAlign::Left, TEXT("Bold"));
			// Its erase button (the picked file's).
			if (bPicked && Step == EStep::Files)
			{
				const float EL = L + CardW - 150.0f;
				const float ET = Tp + CardH - 60.0f;
				const bool bHover = T->IsEraseHovered();
				HudShard(HudAbove(Words, 2), {At(EL + 6.0f, ET), At(EL + 126.0f, ET - 2.0f), At(EL + 120.0f, ET + 42.0f), At(EL, ET + 44.0f)},
					bHover ? FireFill : MakeFill(FLinearColor(0.5f, 0.12f, 0.2f, 0.95f), FLinearColor(0.25f, 0.04f, 0.1f, 0.95f)), 2.5f, FVector2D(3.0, 4.0));
				HudAbove(Words, 6).Text(bPad ? TEXT("Y：けす") : TEXT("けす"), EL + 63.0f, ET + 8.0f, 17.0f, Paper, ETextAlign::Center, TEXT("Black"), 2.0f,
					Navy);
			}
		}
		// What the buttons do.
		const FString Hint = bPad ? TEXT("左右：えらぶ　A：はじめる　Y：けす　B：もどる")
			: TEXT("←→ / クリック：えらぶ　Enter / クリック：はじめる　Delete：けす　Esc：もどる");
		FPainter{Plan, OutDrawElements, Base + 80, In}.Text(Hint, 800.0f, 852.0f, 16.0f, FLinearColor(0.9f, 0.92f, 1.0f, 0.9f), ETextAlign::Center,
			TEXT("Black"), 2.0f, Navy);
	}

	// ---- The question before erasing.
	if (Step == EStep::ConfirmErase)
	{
		const float In = EaseOut(StepAge / 0.25f);
		FPainter{Whole, OutDrawElements, Base + 100, In}.Box(0.0f, 0.0f, Size2f.X, Size2f.Y, FLinearColor(0.01f, 0.0f, 0.04f, 0.6f));
		const FPainter Panel{Plan, OutDrawElements, Base + 104, In};
		HudShard(Panel, {At(470.0f, 330.0f), At(1140.0f, 320.0f), At(1124.0f, 660.0f), At(456.0f, 672.0f)},
			MakeFill(FLinearColor(0.32f, 0.08f, 0.16f, 0.98f), FLinearColor(0.1f, 0.02f, 0.06f, 0.98f)), 4.0f, FVector2D(8.0, 9.0));
		const FChaosImpactSoloFileView& File = T->GetFile(T->GetSelected());
		const FPainter Words{Plan, OutDrawElements, Base + 110, In};
		HudText3D(Words, FString::Printf(TEXT("ファイル %d をけしますか？"), T->GetSelected() + 1), 800.0f, 372.0f, 36.0f, Paper, ETextAlign::Center);
		Words.Text(FString::Printf(TEXT("クリア %d / %d の記録がなくなります。"), File.Cleared, File.Total), 800.0f, 446.0f, 20.0f,
			FLinearColor(1.0f, 0.85f, 0.8f), ETextAlign::Center, TEXT("Black"));
		Words.Text(TEXT("けしたファイルはもとに戻せません。"), 800.0f, 480.0f, 18.0f, FLinearColor(1.0f, 0.6f, 0.55f), ETextAlign::Center, TEXT("Bold"));
		for (const bool bYes : {true, false})
		{
			const bool bOn = T->IsEraseYes() == bYes;
			const float X = bYes ? 560.0f : 820.0f;
			HudShard(HudAbove(Words, 2), {At(X + 8.0f, 560.0f), At(X + 220.0f, 556.0f), At(X + 212.0f, 630.0f), At(X, 634.0f)},
				bOn ? (bYes ? FireFill : IceFill) : NightFill, bOn ? 4.0f : 2.5f, FVector2D(4.0, 5.0));
			HudText3D(HudAbove(Words, 6), bYes ? TEXT("はい") : TEXT("いいえ"), X + 110.0f, 572.0f, 30.0f, Paper, ETextAlign::Center);
		}
	}

	// ---- Coming in from the dark; going out into the light.
	{
		const FPainter Cover{Whole, OutDrawElements, Base + 200};
		const float Dark = 1.0f - FMath::Clamp(Time / 1.4f, 0.0f, 1.0f);
		Cover.Box(0.0f, 0.0f, Size2f.X, Size2f.Y, FLinearColor(0.0f, 0.0f, 0.02f, Dark));
		if (Step == EStep::Leaving)
		{
			Cover.Box(0.0f, 0.0f, Size2f.X, Size2f.Y, FLinearColor(1.0f, 0.97f, 0.9f, FMath::Clamp(StepAge / 0.8f, 0.0f, 1.0f)));
		}
	}
	return Base + 210;
}
