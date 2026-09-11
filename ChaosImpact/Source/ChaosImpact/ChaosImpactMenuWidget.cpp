#include "ChaosImpactMenuWidget.h"

#include "ChaosImpactPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor Ink(0.006f, 0.009f, 0.018f, 1.0f);
	const FLinearColor Fire(1.0f, 0.055f, 0.12f, 1.0f);
	const FLinearColor Ice(0.0f, 0.55f, 1.0f, 1.0f);
	const FLinearColor Gold(1.0f, 0.68f, 0.06f, 1.0f);
	const FLinearColor Paper(0.96f, 0.975f, 1.0f, 1.0f);
	const FLinearColor Muted(0.48f, 0.54f, 0.64f, 1.0f);

	// Drawing and pointer hit testing both use this 1600 x 900 design space.
	float DesignScale(const FGeometry& Geometry)
	{
		return FMath::Max(0.01f, FMath::Min(Geometry.GetLocalSize().X / 1600.0f,
			Geometry.GetLocalSize().Y / 900.0f));
	}

	struct FMenuPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Elements;
		int32 Layer;

		void Box(float X, float Y, float W, float H, FLinearColor Color) const
		{
			FSlateDrawElement::MakeBox(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
				FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		}

		void Line(FVector2D From, FVector2D To, FLinearColor Color, float Width = 1.0f) const
		{
			const TArray<FVector2D> Points{From, To};
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, Color, true, Width);
		}

		void Outline(float X, float Y, float W, float H, FLinearColor Color, float Width) const
		{
			Box(X, Y, W, Width, Color);
			Box(X, Y + H - Width, W, Width, Color);
			Box(X, Y, Width, H, Color);
			Box(X + W - Width, Y, Width, H, Color);
		}

		void Text(const FString& Value, float X, float Y, int32 Size, FLinearColor Color,
			bool bCentered = false, bool bBold = false) const
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
			if (bCentered)
			{
				X -= FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, Font).X * 0.5f;
			}
			const FLinearColor ShadowColor(0.0f, 0.0f, 0.0f, 1.0f);
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(1600.0f, 120.0f),
					FSlateLayoutTransform(FVector2f(X + 3.0f, Y + 3.0f))),
				Value, Font, ESlateDrawEffect::None, ShadowColor);
			Color.A = 1.0f;
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(1600.0f, 120.0f), FSlateLayoutTransform(FVector2f(X, Y))),
				Value, Font, ESlateDrawEffect::None, Color);
		}
	};
}

void UChaosImpactMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	ForceVolatile(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	}

	LogoTexture = FImageUtils::ImportFileAsTexture2D(
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("UI/TitleLogoTransparent.png")));
	if (LogoTexture)
	{
		LogoBrush.SetResourceObject(LogoTexture);
		LogoBrush.ImageSize = FVector2D(LogoTexture->GetSizeX(), LogoTexture->GetSizeY());
		LogoBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

void UChaosImpactMenuWidget::ShowScreen(const EChaosImpactScreen NewScreen)
{
	Screen = NewScreen;
	SelectedIndex = 0;
	PressedIndex = INDEX_NONE;
	ScreenStartedAt = FPlatformTime::Seconds();
	AnimationSeconds = 0.0f;
	BuildEntries();
	SetVisibility(Screen == EChaosImpactScreen::Playing
		? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UChaosImpactMenuWidget::BuildEntries()
{
	Entries.Reset();
	switch (Screen)
	{
	case EChaosImpactScreen::Title:
		Entries.Add({FSlateRect(570, 710, 1030, 790), TEXT("PRESS START"), TEXT(""), TEXT(""), Gold});
		break;
	case EChaosImpactScreen::ModeSelect:
		Entries.Add({FSlateRect(160, 275, 775, 590), TEXT("ソロモード"), TEXT(""), TEXT("SOLO"), Ice});
		Entries.Add({FSlateRect(825, 275, 1440, 590), TEXT("マルチモード"), TEXT(""), TEXT("MULTI"), Fire});
		Entries.Add({FSlateRect(1000, 682, 1440, 772), TEXT("トレーニング"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(160, 707, 490, 772), TEXT("タイトルへ"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::SoloReady:
	case EChaosImpactScreen::MultiReady:
		Entries.Add({FSlateRect(160, 700, 570, 780), TEXT("モード選択へ"), TEXT(""), TEXT(""), Muted});
		Entries.Add({FSlateRect(990, 700, 1440, 780), TEXT("トレーニングへ"), TEXT(""), TEXT(""), Gold});
		break;
	case EChaosImpactScreen::Pause:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bArc = Controller
			&& Controller->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
		Entries.Add({FSlateRect(490, 255, 1110, 327), TEXT("ゲームに戻る"), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(490, 355, 1110, 427),
			bArc ? TEXT("投球軌道：放物線") : TEXT("投球軌道：直線"), TEXT(""), TEXT(""), bArc ? Fire : Ice});
		if (Controller && Controller->IsTrainingMode())
		{
			Entries.Add({FSlateRect(490, 455, 1110, 527), TEXT("トレーニングをリトライ"), TEXT(""), TEXT(""), Gold});
		}
		Entries.Add({FSlateRect(490, 555, 1110, 627), TEXT("モード選択へ"), TEXT(""), TEXT(""), Fire});
		Entries.Add({FSlateRect(490, 655, 1110, 727), TEXT("タイトル画面へ"), TEXT(""), TEXT(""), Muted});
		break;
	}
	default:
		break;
	}
}

void UChaosImpactMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Slate continues to animate while the gameplay world is paused.
	AnimationSeconds = static_cast<float>(FPlatformTime::Seconds() - ScreenStartedAt);
}

int32 UChaosImpactMenuWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (Screen == EChaosImpactScreen::Playing)
	{
		return BaseLayer;
	}

	const float Scale = DesignScale(AllottedGeometry);
	const FVector2D Offset = (AllottedGeometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
	const FGeometry DesignGeometry = AllottedGeometry.MakeChild(
		FVector2f(1600, 900), FSlateLayoutTransform(Scale, FVector2f(Offset)));
	FMenuPainter Full{AllottedGeometry, OutDrawElements, BaseLayer + 1};
	Full.Box(0, 0, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Ink);
	FMenuPainter P{DesignGeometry, OutDrawElements, BaseLayer + 2};

	// Quiet, high-contrast backdrop. Decoration stays at the edges so the UI never fights it.
	for (int32 Band = 0; Band < 12; ++Band)
	{
		const float T = static_cast<float>(Band) / 11.0f;
		const FLinearColor Top(0.025f, 0.035f, 0.065f, 1.0f);
		const FLinearColor Bottom(0.004f, 0.006f, 0.013f, 1.0f);
		P.Box(0, Band * 75.0f, 1600, 76, FMath::Lerp(Top, Bottom, T));
	}
	P.Box(0, 0, 9, 900, Ice);
	P.Box(1591, 0, 9, 900, Fire);
	P.Line(FVector2D(-120, 900), FVector2D(330, 620), FLinearColor(0.0f, 0.28f, 0.65f, 0.42f), 90);
	P.Line(FVector2D(1720, 0), FVector2D(1390, 240), FLinearColor(0.72f, 0.01f, 0.055f, 0.38f), 76);
	P.Line(FVector2D(0, 842), FVector2D(410, 842), FLinearColor(0.0f, 0.45f, 1.0f, 0.55f), 3);
	P.Line(FVector2D(1190, 58), FVector2D(1600, 58), FLinearColor(1.0f, 0.03f, 0.09f, 0.5f), 3);

	if (Screen == EChaosImpactScreen::Title)
	{
		const float Arrival = FMath::Clamp((AnimationSeconds - 0.12f) / 0.72f, 0.0f, 1.0f);
		const float Ease = 1.0f - FMath::Pow(1.0f - Arrival, 3.0f);
		const float Zoom = FMath::Lerp(1.22f, 1.0f, Ease);
		const float Shake = AnimationSeconds > 0.75f && AnimationSeconds < 1.1f
			? FMath::Sin(AnimationSeconds * 70.0f) * (1.1f - AnimationSeconds) * 8.0f : 0.0f;
		const float Width = 1080.0f * Zoom;
		const float Height = Width * LogoBrush.ImageSize.Y / FMath::Max(LogoBrush.ImageSize.X, 1.0f);
		const float X = 800.0f - Width * 0.5f + Shake;
		const float Y = 412.0f - Height * 0.5f;

		if (LogoTexture)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 3,
				DesignGeometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(X, Y))),
				&LogoBrush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Arrival));
		}
		else
		{
			P.Text(TEXT("カオスインパクト"), 800, 350, 80, Paper, true, true);
		}
		// Two quick edge sweeps sell the opening impact without placing anything behind the logo.
		const float Sweep = FMath::Clamp(AnimationSeconds / 0.75f, 0.0f, 1.0f);
		if (Sweep < 1.0f)
		{
			P.Line(FVector2D(-50, 210), FVector2D(360 * Sweep, 270), Ice, 10);
			P.Line(FVector2D(1650, 620), FVector2D(1650 - 360 * Sweep, 560), Fire, 10);
		}
	}
	else if (Screen == EChaosImpactScreen::ModeSelect)
	{
		P.Text(TEXT("モード選択"), 800, 92, 62, Paper, true, true);
		P.Line(FVector2D(640, 184), FVector2D(800, 184), Ice, 5);
		P.Line(FVector2D(800, 184), FVector2D(960, 184), Fire, 5);
	}
	else if (Screen == EChaosImpactScreen::SoloReady || Screen == EChaosImpactScreen::MultiReady)
	{
		const bool bSolo = Screen == EChaosImpactScreen::SoloReady;
		const FLinearColor Accent = bSolo ? Ice : Fire;
		P.Text(bSolo ? TEXT("SOLO") : TEXT("MULTI"), 800, 102, 82, Paper, true, true);
		P.Line(FVector2D(660, 215), FVector2D(940, 215), Accent, 6);
		P.Text(bSolo ? TEXT("ソロモード") : TEXT("マルチモード"), 800, 255, 44, Paper, true, true);
		P.Text(TEXT("準備中"), 800, 390, 66, Paper, true, true);
	}
	else if (Screen == EChaosImpactScreen::Pause)
	{
		P.Text(TEXT("PAUSE"), 800, 94, 78, Paper, true, true);
		P.Line(FVector2D(650, 211), FVector2D(800, 211), Ice, 5);
		P.Line(FVector2D(800, 211), FVector2D(950, 211), Fire, 5);
	}

	P.Layer = BaseLayer + 4;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FMenuEntry& Entry = Entries[Index];
		const bool bSelected = SelectedIndex == Index;
		const float X = Entry.Rect.Left;
		const float Y = Entry.Rect.Top;
		const float W = Entry.Rect.Right - X;
		const float H = Entry.Rect.Bottom - Y;
		// The colored state never covers the label: black panel, white type, colored edge only.
		P.Box(X + 8, Y + 9, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.56f));
		P.Box(X, Y, W, H, PressedIndex == Index
			? FLinearColor(0.065f, 0.075f, 0.105f, 1.0f)
			: FLinearColor(0.018f, 0.024f, 0.040f, 1.0f));
		P.Outline(X, Y, W, H, bSelected ? Entry.Accent : FLinearColor(0.23f, 0.27f, 0.34f, 1.0f),
			bSelected ? 6.0f : 2.0f);
		P.Box(X, Y, bSelected ? 14.0f : 6.0f, H, Entry.Accent);
		if (!Entry.Number.IsEmpty())
		{
			P.Text(Entry.Number, X + 52, Y + 48, 68, Paper, false, true);
			P.Line(FVector2D(X + 52, Y + 142), FVector2D(X + 210, Y + 142), Entry.Accent, 5);
			P.Text(Entry.Title, X + 52, Y + 182, 38, Paper, false, true);
			if (bSelected)
			{
				P.Text(TEXT("▶"), X + W - 82, Y + H - 76, 28, Paper);
			}
		}
		else
		{
			P.Text(Entry.Title, X + 42, Y + H * 0.5f - 22, H > 95 ? 30 : 25, Paper, false, true);
			if (bSelected)
			{
				P.Text(TEXT("▶"), X + W - 57, Y + H * 0.5f - 17, 23, Paper);
			}
		}
	}
	return BaseLayer + 5;
}

void UChaosImpactMenuWidget::Navigate(const FKey Key)
{
	if (Entries.IsEmpty())
	{
		return;
	}
	const bool bBack = Key == EKeys::Up || Key == EKeys::Left
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left;
	if (Screen == EChaosImpactScreen::ModeSelect && Key != EKeys::Tab)
	{
		// A spatial grid: Solo/Multi above Back/Training.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		const int32 Horizontal[] = {1, 0, 3, 2};
		const int32 Vertical[] = {3, 2, 1, 0};
		SelectedIndex = bHorizontal ? Horizontal[SelectedIndex] : Vertical[SelectedIndex];
	}
	else
	{
		SelectedIndex = (SelectedIndex + Entries.Num() + (bBack ? -1 : 1)) % Entries.Num();
	}
	PressedIndex = INDEX_NONE;
}

void UChaosImpactMenuWidget::ConfirmSelection()
{
	AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (!Controller || !Entries.IsValidIndex(SelectedIndex))
	{
		return;
	}
	switch (Screen)
	{
	case EChaosImpactScreen::Title:
		Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
		break;
	case EChaosImpactScreen::ModeSelect:
		if (SelectedIndex == 2) { Controller->StartTraining(); }
		else { Controller->ShowMenuScreen(SelectedIndex == 0 ? EChaosImpactScreen::SoloReady
			: SelectedIndex == 1 ? EChaosImpactScreen::MultiReady : EChaosImpactScreen::Title); }
		break;
	case EChaosImpactScreen::SoloReady:
	case EChaosImpactScreen::MultiReady:
		if (SelectedIndex == 1) { Controller->StartTraining(); }
		else { Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect); }
		break;
	case EChaosImpactScreen::Pause:
		if (SelectedIndex == 0)
		{
			Controller->ResumeGameplay();
		}
		else if (SelectedIndex == 1)
		{
			Controller->ToggleBallFlightMode();
			BuildEntries();
		}
		else
		{
			const int32 RetryIndex = Controller->IsTrainingMode() ? 2 : INDEX_NONE;
			const int32 ModeIndex = Controller->IsTrainingMode() ? 3 : 2;
			if (SelectedIndex == RetryIndex)
			{
				Controller->RetryTraining();
			}
			else
			{
				Controller->ShowMenuScreen(SelectedIndex == ModeIndex
					? EChaosImpactScreen::ModeSelect : EChaosImpactScreen::Title);
			}
		}
		break;
	default:
		break;
	}
}

void UChaosImpactMenuWidget::GoBack()
{
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
	{
		switch (Screen)
		{
		case EChaosImpactScreen::Pause:
			Controller->ResumeGameplay();
			break;
		case EChaosImpactScreen::ModeSelect:
			Controller->ShowMenuScreen(EChaosImpactScreen::Title);
			break;
		case EChaosImpactScreen::SoloReady:
		case EChaosImpactScreen::MultiReady:
			Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
			break;
		default:
			break;
		}
	}
}

FReply UChaosImpactMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Left || Key == EKeys::Right
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down
		|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Tab)
	{
		Navigate(Key);
	}
	else if (!InKeyEvent.IsRepeat())
	{
		if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			ConfirmSelection();
		}
		else if (Key == EKeys::BackSpace || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right
			|| ((Key == EKeys::P || Key == EKeys::Gamepad_Special_Right) && Screen == EChaosImpactScreen::Pause))
		{
			GoBack();
		}
	}
	return FReply::Handled();
}

FReply UChaosImpactMenuWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Directional-pad navigation is normally consumed by Slate's focus navigation.
	// Intercept it before that step so the custom spatial menu receives every direction.
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down
		|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right)
	{
		return NativeOnKeyDown(InGeometry, InKeyEvent);
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

int32 UChaosImpactMenuWidget::HitTestEntry(const FGeometry& Geometry, const FVector2D& ScreenPosition) const
{
	const float Scale = DesignScale(Geometry);
	const FVector2D Offset = (Geometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
	const FVector2D Point = (Geometry.AbsoluteToLocal(ScreenPosition) - Offset) / Scale;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].Rect.ContainsPoint(Point))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FReply UChaosImpactMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		const int32 Hovered = HitTestEntry(InGeometry, InMouseEvent.GetScreenSpacePosition());
		if (Hovered != INDEX_NONE)
		{
			SelectedIndex = Hovered;
		}
	}
	return FReply::Handled();
}

FReply UChaosImpactMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PressedIndex = HitTestEntry(InGeometry, InMouseEvent.GetScreenSpacePosition());
		if (PressedIndex != INDEX_NONE)
		{
			SelectedIndex = PressedIndex;
		}
		return FReply::Handled().CaptureMouse(TakeWidget()).SetUserFocus(TakeWidget());
	}
	return FReply::Handled();
}

FReply UChaosImpactMenuWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Released = HitTestEntry(InGeometry, InMouseEvent.GetScreenSpacePosition());
		const bool bActivate = PressedIndex != INDEX_NONE && PressedIndex == Released;
		PressedIndex = INDEX_NONE;
		if (bActivate)
		{
			SelectedIndex = Released;
			ConfirmSelection();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Handled();
}

void UChaosImpactMenuWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	PressedIndex = INDEX_NONE;
}
