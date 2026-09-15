#include "ChaosImpactMenuWidget.h"

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
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
	const FLinearColor Violet(0.55f, 0.28f, 1.0f, 1.0f);
	const FLinearColor PlayerAccents[] = {Ice, Fire, Gold, Violet};

	bool IsMenuKeyAllowed(const UChaosImpactMenuWidget* Widget, const FKey Key)
	{
		const AChaosImpactPlayerController* Controller = Widget
			? Cast<AChaosImpactPlayerController>(Widget->GetOwningPlayer()) : nullptr;
		if (!Controller || !Controller->IsTrainingMode())
		{
			return true;
		}
		// Player entry decides which device is P1's, so it follows the device being chosen there.
		if (Widget->GetScreen() == EChaosImpactScreen::ControllerAssignment)
		{
			return Key.IsGamepadKey() == Controller->WillPrimaryUseGamepad();
		}
		// A lone player works the menus with either device (team select, pause, rules, results). With several
		// local players each device belongs to its own player; online follows the room's any-device rule.
		const UGameInstance* GameInstance = Widget->GetGameInstance();
		if (GameInstance && GameInstance->GetLocalPlayers().Num() <= 1)
		{
			return true;
		}
		return Controller->AcceptsInputKey(Key);
	}

	/** Player entry sees every controller's buttons, whichever Slate user they belong to. */
	class FControllerJoinProcessor : public IInputProcessor
	{
	public:
		explicit FControllerJoinProcessor(UChaosImpactMenuWidget* InWidget) : Widget(InWidget) {}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UChaosImpactMenuWidget* Menu = Widget.Get();
			return Menu && Menu->TryJoinControllerFromAnyUser(InKeyEvent);
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("ChaosImpactControllerJoin"); }

	private:
		TWeakObjectPtr<UChaosImpactMenuWidget> Widget;
	};

	bool IsMenuMouseAllowed(const UChaosImpactMenuWidget* Widget)
	{
		const AChaosImpactPlayerController* Controller = Widget
			? Cast<AChaosImpactPlayerController>(Widget->GetOwningPlayer()) : nullptr;
		const bool bUsePendingGamepadMode = Widget
			&& Widget->GetScreen() == EChaosImpactScreen::ControllerAssignment;
		return !Controller || !Controller->IsTrainingMode()
			|| !(bUsePendingGamepadMode
				? Controller->WillPrimaryUseGamepad()
				: Controller->IsPrimaryUsingGamepad());
	}

	// Drawing and pointer hit testing both use this 1600 x 900 design space.
	float DesignScale(const FGeometry& Geometry)
	{
		return FMath::Max(0.01f, FMath::Min(Geometry.GetLocalSize().X / 1600.0f,
			Geometry.GetLocalSize().Y / 900.0f));
	}

	FLinearColor WithAlpha(FLinearColor Color, const float Alpha)
	{
		Color.A *= FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Color;
	}

	float EaseOut(const float T)
	{
		return 1.0f - FMath::Pow(1.0f - FMath::Clamp(T, 0.0f, 1.0f), 3.0f);
	}

	/** A child space that leans forward ("/") and optionally scales about its center. */
	FGeometry MakeSkewed(const FGeometry& Parent, const float X, const float Y, const float W,
		const float H, const float Shear, const float Scale = 1.0f)
	{
		const FSlateRenderTransform Render(TMatrix2x2<float>(Scale, 0.0f, Scale * Shear, Scale));
		return Parent.MakeChild(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y)),
			Render, FVector2f(0.5f, 0.5f));
	}

	enum class ETextAlign : uint8 { Left, Center, Right };

	struct FMenuPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Elements;
		int32 Layer;
		float Alpha = 1.0f;

		void Box(float X, float Y, float W, float H, FLinearColor Color) const
		{
			if (W <= 0.0f || H <= 0.0f)
			{
				return;
			}
			FSlateDrawElement::MakeBox(Elements, Layer,
				Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
				FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}

		void Line(FVector2D From, FVector2D To, FLinearColor Color, float Width = 1.0f) const
		{
			const TArray<FVector2D> Points{From, To};
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, WithAlpha(Color, Alpha), true, Width);
		}

		void Ring(FVector2D Center, float Radius, FLinearColor Color, float Width, int32 Segments = 56) const
		{
			TArray<FVector2D> Points;
			Points.Reserve(Segments + 1);
			for (int32 Segment = 0; Segment <= Segments; ++Segment)
			{
				const float Angle = UE_TWO_PI * Segment / Segments;
				Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
			}
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
				Points, ESlateDrawEffect::None, WithAlpha(Color, Alpha), true, Width);
		}

		void Outline(float X, float Y, float W, float H, FLinearColor Color, float Width) const
		{
			Box(X, Y, W, Width, Color);
			Box(X, Y + H - Width, W, Width, Color);
			Box(X, Y, Width, H, Color);
			Box(X + W - Width, Y, Width, H, Color);
		}

		void Text(const FString& Value, float X, float Y, float Size, FLinearColor Color,
			ETextAlign Align = ETextAlign::Left, FName Face = TEXT("Black"),
			float OutlineSize = 0.0f, FLinearColor OutlineColor = Ink) const
		{
			if (Value.IsEmpty() || Alpha * Color.A <= 0.001f)
			{
				return;
			}
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Face, Size,
				FFontOutlineSettings(FMath::RoundToInt(OutlineSize), WithAlpha(OutlineColor, Alpha * Color.A)));
			if (Align != ETextAlign::Left)
			{
				const float Width = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
					->Measure(Value, Font).X;
				X -= Align == ETextAlign::Center ? Width * 0.5f : Width;
			}
			const FVector2f Area(2400.0f, Size * 2.4f);
			// Drawn once: an offset drop-shadow copy read as doubled text.
			FSlateDrawElement::MakeText(Elements, Layer,
				Geometry.ToPaintGeometry(Area, FSlateLayoutTransform(FVector2f(X, Y))),
				Value, Font, ESlateDrawEffect::None, WithAlpha(Color, Alpha));
		}
	};

	/** Deep navy field split by a blue slab on the left and a red slab on the right. */
	void PaintBackdrop(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const float Time)
	{
		const FMenuPainter Flat{Design, Elements, Layer};
		for (int32 Band = 0; Band < 18; ++Band)
		{
			const float T = Band / 17.0f;
			Flat.Box(-400.0f, Band * 50.0f, 2400.0f, 51.0f, FMath::Lerp(
				FLinearColor(0.022f, 0.03f, 0.06f, 1.0f), FLinearColor(0.003f, 0.004f, 0.01f, 1.0f), T));
		}

		const FGeometry Slant = MakeSkewed(Design, 0.0f, 0.0f, 1600.0f, 900.0f, -0.36f);
		const FMenuPainter S{Slant, Elements, Layer};
		S.Box(-620.0f, -40.0f, 900.0f, 980.0f, FLinearColor(0.0f, 0.10f, 0.40f, 0.62f));
		S.Box(280.0f, -40.0f, 11.0f, 980.0f, WithAlpha(Ice, 0.9f));
		S.Box(305.0f, -40.0f, 3.0f, 980.0f, WithAlpha(Ice, 0.35f));
		S.Box(1340.0f, -40.0f, 900.0f, 980.0f, FLinearColor(0.40f, 0.0f, 0.03f, 0.62f));
		S.Box(1329.0f, -40.0f, 11.0f, 980.0f, WithAlpha(Fire, 0.9f));
		S.Box(1312.0f, -40.0f, 3.0f, 980.0f, WithAlpha(Fire, 0.35f));
		for (int32 Stripe = 0; Stripe < 14; ++Stripe)
		{
			const float X = FMath::Fmod(Stripe * 173.0f + Time * (70.0f + Stripe * 11.0f), 2100.0f) - 250.0f;
			S.Box(X, -40.0f, 1.5f + Stripe % 3, 980.0f,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.016f + (Stripe % 4) * 0.007f));
		}
		Flat.Box(-400.0f, 0.0f, 2400.0f, 54.0f, WithAlpha(Ink, 0.55f));
		Flat.Box(-400.0f, 846.0f, 2400.0f, 54.0f, WithAlpha(Ink, 0.65f));
	}

	/** Red/blue bars that cross the screen once when a menu page opens. */
	void PaintEnterWipe(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const float Time)
	{
		if (Time >= 0.5f)
		{
			return;
		}
		const float E = EaseOut(Time / 0.45f);
		const FGeometry Slant = MakeSkewed(Design, 0.0f, 0.0f, 1600.0f, 900.0f, -0.36f);
		const FMenuPainter S{Slant, Elements, Layer, 1.0f - E};
		S.Box(FMath::Lerp(1750.0f, -900.0f, E), -40.0f, 300.0f, 980.0f, Fire);
		S.Box(FMath::Lerp(2120.0f, -560.0f, E), -40.0f, 90.0f, 980.0f, Ice);
	}

	void PaintHeader(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FString& Label, const float Time)
	{
		const float E = EaseOut((Time - 0.06f) / 0.4f);
		const FGeometry Header = MakeSkewed(Design, 110.0f - (1.0f - E) * 90.0f, 66.0f, 900.0f, 120.0f, -0.2f);
		const FMenuPainter P{Header, Elements, Layer, E};
		P.Text(Label, 0.0f, 0.0f, 60.0f, Paper, ETextAlign::Left, TEXT("Black"));
		// The short blue tick sits before the red bar so it never crosses the backdrop's blue edge.
		P.Box(4.0f, 100.0f, 44.0f * E, 10.0f, Ice);
		P.Box(60.0f, 100.0f, 230.0f * E, 10.0f, Fire);
	}

	void PaintBar(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const FString& Label, const FLinearColor& Accent, const float Blend,
		const bool bPressed, const bool bDisabled, const float Alpha, const float Time)
	{
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const FGeometry Bar = MakeSkewed(Design, Rect.Left + 14.0f * Blend, Rect.Top, W, H, -0.3f,
			bPressed ? 0.96f : 1.0f);
		const FMenuPainter P{Bar, Elements, Layer, Alpha};
		const bool bNeutral = Accent.Equals(Muted);
		const FLinearColor Fill = bNeutral ? Paper : Accent;
		const bool bInkText = bNeutral || Accent.Equals(Gold);
		const float Fold = bDisabled ? 0.0f : EaseOut(Blend);

		P.Box(8.0f, 9.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		P.Box(0.0f, 0.0f, W, H, bDisabled
			? FLinearColor(0.02f, 0.024f, 0.034f, 0.9f) : FLinearColor(0.018f, 0.024f, 0.04f, 0.97f));
		P.Box(0.0f, 0.0f, W * Fold, H, Fill);
		P.Box(0.0f, 0.0f, 9.0f, H, bDisabled ? WithAlpha(Muted, 0.35f) : Accent);

		const FLinearColor Resting = bDisabled ? WithAlpha(Muted, 0.55f) : Paper;
		const FLinearColor TextColor = FMath::Lerp(Resting, bInkText ? Ink : Paper, Fold);
		const float Size = H >= 80.0f ? 32.0f : 27.0f;
		P.Text(Label, 38.0f, H * 0.5f - Size * 0.8f, Size, TextColor);
		if (Fold > 0.05f)
		{
			const float CX = W - 40.0f;
			P.Line(FVector2D(CX - 12.0f, H * 0.5f - 12.0f), FVector2D(CX, H * 0.5f), WithAlpha(TextColor, Fold), 4.0f);
			P.Line(FVector2D(CX, H * 0.5f), FVector2D(CX - 12.0f, H * 0.5f + 12.0f), WithAlpha(TextColor, Fold), 4.0f);
			P.Outline(-6.0f, -6.0f, W + 12.0f, H + 12.0f,
				WithAlpha(Paper, Fold * (0.3f + 0.3f * FMath::Sin(Time * 6.0f))), 2.0f);
		}
	}

	void PaintCard(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const FString& Big, const FString& Sub, const FLinearColor& Accent,
		const float Blend, const bool bPressed, const float Alpha, const float Time, const float BigSize,
		const int32 Pips)
	{
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = EaseOut(Blend);
		const FGeometry Card = MakeSkewed(Design, Rect.Left, Rect.Top - 12.0f * B, W, H, -0.14f,
			(1.0f + 0.025f * B) * (bPressed ? 0.97f : 1.0f));
		const FMenuPainter P{Card, Elements, Layer, Alpha};
		FLinearColor Deep = Accent * 0.4f;
		Deep.A = 1.0f;

		P.Box(14.0f, 16.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		P.Box(0.0f, 0.0f, W, H, FLinearColor(0.02f, 0.027f, 0.047f, 0.97f));
		P.Box(0.0f, 0.0f, W, H, WithAlpha(Deep, 0.25f + 0.75f * B));
		// Vertical bands lean with the card and read as speed streaks once it is selected.
		for (int32 Band = 0; Band < 3; ++Band)
		{
			P.Box(W * 0.52f + Band * 64.0f, 0.0f, 26.0f - Band * 7.0f, H, WithAlpha(Accent, 0.08f + 0.16f * B));
		}
		P.Box(0.0f, 0.0f, W, 6.0f, WithAlpha(Accent, 0.45f + 0.55f * B));
		P.Box(0.0f, H - 14.0f, W, 14.0f, Accent);

		P.Text(Big, 40.0f, H - BigSize * 1.3f - 110.0f, BigSize, WithAlpha(Paper, 0.7f + 0.3f * B),
			ETextAlign::Left, TEXT("Black"), 3.0f * B, Ink);
		P.Text(Sub, 46.0f, H - 84.0f, 30.0f, WithAlpha(Paper, 0.55f + 0.45f * B));
		for (int32 Pip = 0; Pip < Pips; ++Pip)
		{
			P.Box(W - 34.0f - (Pips - Pip) * 26.0f, 30.0f, 16.0f, 34.0f, WithAlpha(Paper, 0.5f + 0.5f * B));
		}
		if (B > 0.01f)
		{
			P.Outline(-7.0f, -7.0f, W + 14.0f, H + 14.0f,
				WithAlpha(Paper, B * (0.7f + 0.3f * FMath::Sin(Time * 5.0f))), 4.0f);
		}
	}

	void PaintPadGlyph(const FMenuPainter& P, const float CX, const float CY, const float S,
		const FLinearColor& Accent)
	{
		const auto Part = [&](float X, float Y, float W, float H, const FLinearColor& Color)
		{
			P.Box(CX + X * S, CY + Y * S, W * S, H * S, Color);
		};
		Part(-78.0f, -30.0f, 156.0f, 56.0f, Paper);
		Part(-94.0f, -12.0f, 50.0f, 70.0f, Paper);
		Part(44.0f, -12.0f, 50.0f, 70.0f, Paper);
		Part(-64.0f, -18.0f, 10.0f, 30.0f, Ink);
		Part(-74.0f, -8.0f, 30.0f, 10.0f, Ink);
		Part(46.0f, -22.0f, 12.0f, 12.0f, Accent);
		Part(60.0f, -9.0f, 12.0f, 12.0f, Accent);
		Part(32.0f, -9.0f, 12.0f, 12.0f, Accent);
		Part(46.0f, 4.0f, 12.0f, 12.0f, Accent);
		Part(-28.0f, 6.0f, 18.0f, 18.0f, Ink);
		Part(10.0f, 6.0f, 18.0f, 18.0f, Ink);
	}

	void PaintKeyboardGlyph(const FMenuPainter& P, const float CX, const float CY, const float S,
		const FLinearColor& Accent)
	{
		P.Outline(CX - 96.0f * S, CY - 42.0f * S, 192.0f * S, 84.0f * S, Paper, 6.0f * S);
		for (int32 Row = 0; Row < 2; ++Row)
		{
			for (int32 Key = 0; Key < 7; ++Key)
			{
				P.Box(CX + (-76.0f + Key * 22.0f) * S, CY + (-26.0f + Row * 20.0f) * S,
					16.0f * S, 14.0f * S, Row == 0 && Key == 1 ? Accent : Paper);
			}
		}
		P.Box(CX - 46.0f * S, CY + 16.0f * S, 92.0f * S, 12.0f * S, Paper);
	}

	void PaintJoinSlot(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const int32 PlayerIndex, const bool bRequired, const bool bNext,
		const bool bJoined, const bool bKeyboard, const float JoinAge, const float Time, const float Alpha)
	{
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const FLinearColor Accent = PlayerAccents[PlayerIndex];
		const FString Label = FString::Printf(TEXT("P%d"), PlayerIndex + 1);
		// Damped spring: the card lands small, overshoots, then settles like a console join.
		const float Pop = bJoined
			? 1.0f - 0.2f * FMath::Exp(-7.5f * JoinAge) * FMath::Cos(19.0f * JoinAge) : 1.0f;
		const FGeometry Card = MakeSkewed(Design, Rect.Left, Rect.Top, W, H, -0.1f, Pop);
		const FMenuPainter P{Card, Elements, Layer, Alpha};
		const FVector2D Center(W * 0.5f, H * 0.53f);

		P.Box(12.0f, 14.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		if (!bRequired)
		{
			P.Box(0.0f, 0.0f, W, H, FLinearColor(0.012f, 0.016f, 0.026f, 0.72f));
			P.Outline(0.0f, 0.0f, W, H, FLinearColor(0.11f, 0.13f, 0.17f, 0.8f), 2.0f);
			P.Text(Label, W * 0.5f, H * 0.5f - 62.0f, 88.0f, FLinearColor(0.13f, 0.15f, 0.2f, 1.0f),
				ETextAlign::Center);
			return;
		}

		P.Box(0.0f, 0.0f, W, H, FLinearColor(0.02f, 0.026f, 0.045f, 0.98f));
		if (bJoined)
		{
			FLinearColor Deep = Accent * 0.5f;
			Deep.A = 1.0f;
			const float Flood = EaseOut(JoinAge / 0.3f);
			P.Box(0.0f, H * (1.0f - Flood), W, H * Flood, Deep);
			for (int32 Band = 0; Band < 3; ++Band)
			{
				P.Box(W * 0.18f + Band * 74.0f, H * (1.0f - Flood), 24.0f - Band * 6.0f, H * Flood,
					WithAlpha(Accent, 0.25f));
			}
			P.Box(0.0f, 0.0f, W, 10.0f, Accent);
			P.Box(0.0f, H - 10.0f, W, 10.0f, Accent);
			P.Outline(0.0f, 0.0f, W, H, Accent, 3.0f);

			const float Drop = -110.0f * FMath::Exp(-9.0f * JoinAge) * FMath::Cos(15.0f * JoinAge);
			P.Text(Label, W * 0.5f, 30.0f + Drop, 100.0f, Paper, ETextAlign::Center, TEXT("Black"), 4.0f, Ink);

			const float GlyphAge = FMath::Max(0.0f, JoinAge - 0.06f);
			const float GlyphScale = 1.0f + 0.9f * FMath::Exp(-13.0f * GlyphAge);
			FMenuPainter Glyph = P;
			Glyph.Alpha = Alpha * FMath::Clamp(GlyphAge / 0.08f, 0.0f, 1.0f);
			if (bKeyboard)
			{
				PaintKeyboardGlyph(Glyph, Center.X, Center.Y, GlyphScale, Accent);
			}
			else
			{
				PaintPadGlyph(Glyph, Center.X, Center.Y, GlyphScale, Accent);
			}

			const float TagIn = EaseOut((JoinAge - 0.22f) / 0.25f);
			FMenuPainter Tag = P;
			Tag.Alpha = Alpha * TagIn;
			Tag.Text(bKeyboard ? TEXT("キーボード") : TEXT("コントローラー"),
				W * 0.5f, H - 136.0f, 26.0f, Paper, ETextAlign::Center);
			Tag.Box(W * 0.5f - 94.0f + (1.0f - TagIn) * 70.0f, H - 84.0f, 188.0f, 48.0f, Paper);
			Tag.Text(TEXT("READY"), W * 0.5f + (1.0f - TagIn) * 70.0f, H - 82.0f, 32.0f, Ink,
				ETextAlign::Center, TEXT("BlackItalic"));

			const float Flash = FMath::Max(0.0f, 1.0f - JoinAge / 0.2f);
			P.Box(-6.0f, -6.0f, W + 12.0f, H + 12.0f, WithAlpha(Paper, 0.9f * Flash));
		}
		else
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
			const FLinearColor Idle(0.2f, 0.24f, 0.32f, 1.0f);
			P.Outline(0.0f, 0.0f, W, H, bNext ? WithAlpha(Accent, 0.45f + 0.55f * Pulse) : Idle,
				bNext ? 5.0f : 2.0f);
			P.Text(Label, W * 0.5f, 30.0f, 100.0f, bNext ? WithAlpha(Accent, 0.9f) : Idle, ETextAlign::Center);
			if (bNext)
			{
				P.Box(8.0f, FMath::Fmod(Time * 240.0f, H - 40.0f) + 20.0f, W - 16.0f, 3.0f, WithAlpha(Accent, 0.35f));
				P.Ring(Center, 48.0f + 7.0f * Pulse, WithAlpha(Accent, 0.85f), 4.0f);
				P.Box(Center.X - 21.0f, Center.Y - 3.0f, 42.0f, 6.0f, Paper);
				P.Box(Center.X - 3.0f, Center.Y - 21.0f, 6.0f, 42.0f, Paper);
				P.Text(TEXT("PRESS ANY BUTTON"), W * 0.5f, H - 96.0f, 22.0f,
					WithAlpha(Paper, 0.5f + 0.5f * Pulse), ETextAlign::Center, TEXT("BoldCondensed"));
			}
			else
			{
				P.Ring(Center, 48.0f, Idle, 3.0f);
			}
		}
	}

	/** Shockwave and sparks drawn unskewed over the slot while a join lands. */
	void PaintJoinBurst(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const int32 PlayerIndex, const float JoinAge)
	{
		if (JoinAge < 0.0f || JoinAge >= 0.7f)
		{
			return;
		}
		const float E = EaseOut(JoinAge / 0.7f);
		const FMenuPainter P{Design, Elements, Layer, 1.0f - JoinAge / 0.7f};
		const FLinearColor Accent = PlayerAccents[PlayerIndex];
		const FVector2D Center((Rect.Left + Rect.Right) * 0.5f, (Rect.Top + Rect.Bottom) * 0.5f);
		P.Ring(Center, 90.0f + 360.0f * E, Accent, 2.0f + 10.0f * (1.0f - E), 72);
		P.Ring(Center, 40.0f + 250.0f * E, Paper, 4.0f, 72);
		for (int32 Spark = 0; Spark < 12; ++Spark)
		{
			const float Angle = FMath::DegreesToRadians(Spark * 30.0f + PlayerIndex * 11.0f);
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			const float Inner = 130.0f + 300.0f * E;
			P.Line(Center + Direction * Inner, Center + Direction * (Inner + 20.0f + 70.0f * (1.0f - E)),
				Spark % 2 == 0 ? Accent : Paper, 5.0f);
		}
	}

	FSlateRect JoinSlotRect(const int32 PlayerIndex)
	{
		const float Width = 300.0f;
		const float Gap = 36.0f;
		const float X = (1600.0f - (Width * 4.0f + Gap * 3.0f)) * 0.5f + PlayerIndex * (Width + Gap);
		return FSlateRect(X, 212.0f, X + Width, 642.0f);
	}

	FSlateRect PasswordDigitRect(const int32 Index)
	{
		const float X = 455.0f + Index * 180.0f;
		return FSlateRect(X, 300.0f, X + 150.0f, 510.0f);
	}

	FVector2D ToDesignPoint(const FGeometry& Geometry, const FVector2D& ScreenPosition)
	{
		const float Scale = DesignScale(Geometry);
		const FVector2D Offset = (Geometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
		return (Geometry.AbsoluteToLocal(ScreenPosition) - Offset) / Scale;
	}
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
	if (UCanvasPanel* Canvas = WidgetTree ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr)
	{
		// A real text field so user names can be typed with IME (Japanese) input.
		NameInput = WidgetTree->ConstructWidget<UEditableText>(UEditableText::StaticClass(), TEXT("OnlineNameInput"));
		// UUserWidget paints its children before NativePaint, so the menu backdrop would cover this
		// field. It stays invisible and only handles typing/IME; NativePaint draws the text on top.
		NameInput->WidgetStyle.SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f)));
		NameInput->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Black"), 44.0f));
		NameInput->SetJustification(ETextJustify::Center);
		NameInput->OnTextCommitted.AddDynamic(this, &UChaosImpactMenuWidget::HandleNameCommitted);
		NameInput->OnTextChanged.AddDynamic(this, &UChaosImpactMenuWidget::HandleNameChanged);
		NameInput->SetVisibility(ESlateVisibility::Collapsed);
		Canvas->AddChildToCanvas(NameInput);
	}

	if (FSlateApplication::IsInitialized() && !JoinInputProcessor.IsValid())
	{
		JoinInputProcessor = MakeShared<FControllerJoinProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(JoinInputProcessor);
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

void UChaosImpactMenuWidget::NativeDestruct()
{
	if (JoinInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(JoinInputProcessor);
	}
	JoinInputProcessor.Reset();
	Super::NativeDestruct();
}

bool UChaosImpactMenuWidget::TryJoinControllerFromAnyUser(const FKeyEvent& InKeyEvent)
{
	if (Screen != EChaosImpactScreen::ControllerAssignment || !InKeyEvent.GetKey().IsGamepadKey()
		|| InKeyEvent.IsRepeat() || GetVisibility() == ESlateVisibility::Collapsed)
	{
		return false;
	}
	AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const int32 InputDeviceId = InKeyEvent.GetInputDeviceId().GetId();
	if (!Controller || Controller->IsControllerJoined(InputDeviceId))
	{
		return false;
	}
	Controller->RegisterControllerJoin(InputDeviceId, static_cast<int32>(InKeyEvent.GetUserIndex()));
	return Controller->IsControllerJoined(InputDeviceId);
}

void UChaosImpactMenuWidget::ShowScreen(const EChaosImpactScreen NewScreen)
{
	Screen = NewScreen;
	SelectedIndex = 0;
	PressedIndex = INDEX_NONE;
	ArmedIndex = INDEX_NONE;
	ScreenStartedAt = FPlatformTime::Seconds();
	AnimationSeconds = 0.0f;
	BuildEntries();
	SelectBlend.Init(0.0f, Entries.Num());

	// Slots that are already filled when the page opens (the reserved keyboard, or a
	// return visit) still play their join animation, staggered after the page wipe.
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	for (int32 PlayerIndex = 0; PlayerIndex < 4; ++PlayerIndex)
	{
		bSlotJoined[PlayerIndex] = Controller && Screen == EChaosImpactScreen::ControllerAssignment
			&& Controller->IsInputAssignedToPlayer(PlayerIndex);
		SlotJoinedAt[PlayerIndex] = bSlotJoined[PlayerIndex]
			? ScreenStartedAt + 0.32 + 0.12 * PlayerIndex : -1000.0;
	}

	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (NameInput)
	{
		const bool bNameScreen = Screen == EChaosImpactScreen::OnlineName || Screen == EChaosImpactScreen::OnlineRoomName;
		NameInput->SetVisibility(bNameScreen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (Screen == EChaosImpactScreen::OnlineName && Sessions)
		{
			NameInput->SetText(FText::FromString(Sessions->GetPlayerName()));
		}
		else if (Screen == EChaosImpactScreen::OnlineRoomName)
		{
			// Renaming starts from the current name; a new room from "<player>のへや".
			const AChaosImpactPlayerController* RoomController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
			NameInput->SetText(FText::FromString(RoomController && RoomController->IsRenamingRoom() && Room
				? Room->RoomName : Sessions ? Sessions->GetDefaultRoomName() : FString()));
		}
		bNameFocusPending = bNameScreen;
	}
	if (Screen == EChaosImpactScreen::OnlinePassword)
	{
		PasswordCursor = 0;
		const FString Previous = Sessions ? Sessions->GetPassword() : FString();
		for (int32 Digit = 0; Digit < 4 && Previous.Len() == 4; ++Digit)
		{
			PasswordDigits[Digit] = FMath::Clamp(Previous[Digit] - TEXT('0'), 0, 9);
		}
	}
	LastCreateError = Sessions ? Sessions->GetCreateError() : FString();

	SetVisibility(Screen == EChaosImpactScreen::Playing
		? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UChaosImpactMenuWidget::HandleNameCommitted(const FText& Text, const ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter && Screen == EChaosImpactScreen::OnlineName)
	{
		SubmitName();
	}
	else if (CommitMethod == ETextCommit::OnEnter && Screen == EChaosImpactScreen::OnlineRoomName)
	{
		SubmitRoomNameEntry();
	}
}

void UChaosImpactMenuWidget::SubmitRoomNameEntry()
{
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		Controller && NameInput)
	{
		Controller->SubmitRoomName(NameInput->GetText().ToString());
	}
}

void UChaosImpactMenuWidget::HandleNameChanged(const FText& Text)
{
	const FString Value = Text.ToString();
	const int32 MaxLength = Screen == EChaosImpactScreen::OnlineRoomName
		? UChaosImpactSessionSubsystem::MaxRoomNameLength : UChaosImpactSessionSubsystem::MaxNameLength;
	if (NameInput && Value.Len() > MaxLength)
	{
		NameInput->SetText(FText::FromString(Value.Left(MaxLength)));
	}
}

void UChaosImpactMenuWidget::SubmitName()
{
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		Controller && NameInput)
	{
		Controller->SubmitOnlineName(NameInput->GetText().ToString());
	}
}

FString UChaosImpactMenuWidget::GetPasswordString() const
{
	return FString::Printf(TEXT("%d%d%d%d"), PasswordDigits[0], PasswordDigits[1], PasswordDigits[2], PasswordDigits[3]);
}

void UChaosImpactMenuWidget::SubmitPassword()
{
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
	{
		Controller->SubmitRoomPassword(GetPasswordString());
	}
}

bool UChaosImpactMenuWidget::HandlePasswordKey(const FKey& Key)
{
	if (Screen != EChaosImpactScreen::OnlinePassword)
	{
		return false;
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		const int32 Delta = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up ? 1 : -1;
		PasswordDigits[PasswordCursor] = (PasswordDigits[PasswordCursor] + Delta + 10) % 10;
		return true;
	}
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::BackSpace)
	{
		PasswordCursor = FMath::Max(0, PasswordCursor - 1);
		return true;
	}
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		PasswordCursor = FMath::Min(3, PasswordCursor + 1);
		return true;
	}
	static const FKey DigitKeys[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	static const FKey PadKeys[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
		EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight,
		EKeys::NumPadNine};
	for (int32 Value = 0; Value < 10; ++Value)
	{
		if (Key == DigitKeys[Value] || Key == PadKeys[Value])
		{
			PasswordDigits[PasswordCursor] = Value;
			PasswordCursor = FMath::Min(3, PasswordCursor + 1);
			return true;
		}
	}
	return false;
}

void UChaosImpactMenuWidget::RefreshEntries()
{
	BuildEntries();
	SelectBlend.SetNumZeroed(Entries.Num());
	InvalidateLayoutAndVolatility();
}

void UChaosImpactMenuWidget::BuildEntries()
{
	Entries.Reset();
	switch (Screen)
	{
	case EChaosImpactScreen::Title:
		Entries.Add({FSlateRect(520, 700, 1080, 800), TEXT("PRESS START"), TEXT(""), TEXT(""), Gold});
		break;
	case EChaosImpactScreen::ModeSelect:
		Entries.Add({FSlateRect(150, 236, 770, 652), TEXT("ソロモード"), TEXT(""), TEXT("SOLO"), Ice});
		Entries.Add({FSlateRect(830, 236, 1450, 652), TEXT("VSモード"), TEXT(""), TEXT("VS"), Fire});
		Entries.Add({FSlateRect(1010, 714, 1450, 798), TEXT("トレーニング"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("タイトルへ"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::TrainingSetup:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bGamepad = Controller && Controller->WillPrimaryUseGamepad();
		for (int32 Players = 1; Players <= 4; ++Players)
		{
			const float X = 150.0f + (Players - 1) * 331.0f;
			Entries.Add({FSlateRect(X, 236, X + 305, 626), Players == 1 ? TEXT("PLAYER") : TEXT("PLAYERS"),
				TEXT(""), FString::FromInt(Players), PlayerAccents[Players - 1]});
		}
		Entries.Add({FSlateRect(930, 708, 1450, 788),
			bGamepad ? TEXT("1P  コントローラー") : TEXT("1P  キーボード＋マウス"),
			TEXT(""), TEXT(""), bGamepad ? Fire : Ice});
		Entries.Add({FSlateRect(150, 716, 470, 780),
			Controller && Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusLocal ? TEXT("戻る") : TEXT("モード選択へ"),
			TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::VSSelect:
		Entries.Add({FSlateRect(150, 236, 770, 652), TEXT("ローカル"), TEXT(""), TEXT("LOCAL"), Ice});
		Entries.Add({FSlateRect(830, 236, 1450, 652), TEXT("通信"), TEXT(""), TEXT("ONLINE"), Fire});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("モード選択へ"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::OnlinePlayers:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bGamepad = Controller && Controller->WillPrimaryUseGamepad();
		for (int32 Players = 1; Players <= 2; ++Players)
		{
			const float X = 482.0f + (Players - 1) * 331.0f;
			Entries.Add({FSlateRect(X, 236, X + 305, 626), Players == 1 ? TEXT("PLAYER") : TEXT("PLAYERS"),
				TEXT(""), FString::FromInt(Players), PlayerAccents[Players - 1]});
		}
		Entries.Add({FSlateRect(930, 708, 1450, 788),
			bGamepad ? TEXT("1P  コントローラー") : TEXT("1P  キーボード＋マウス"),
			TEXT(""), TEXT(""), bGamepad ? Fire : Ice});
		Entries.Add({FSlateRect(150, 716, 470, 780), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::ControllerAssignment:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bOnlineSetup = Controller && Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusOnline
			&& Controller->GetControllerAssignmentReturnScreen() == EChaosImpactScreen::OnlinePlayers;
		FMenuEntry Start{FSlateRect(1030, 712, 1454, 800), bOnlineSetup ? TEXT("決定") : TEXT("ゲーム開始"),
			TEXT(""), TEXT(""), Gold};
		Start.bDisabled = !(Controller && Controller->AreControllerAssignmentsComplete());
		Entries.Add(Start);
		Entries.Add({FSlateRect(146, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::SoloReady:
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("モード選択へ"), TEXT(""), TEXT(""), Muted});
		Entries.Add({FSlateRect(1010, 714, 1450, 798), TEXT("トレーニングへ"), TEXT(""), TEXT(""), Gold});
		break;
	case EChaosImpactScreen::MultiReady:
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		Entries.Add({FSlateRect(150, 236, 770, 652), TEXT("へやをつくる"), TEXT(""), TEXT("CREATE"), Ice});
		Entries.Add({FSlateRect(830, 236, 1450, 652), TEXT("へやをさがす"), TEXT(""), TEXT("SEARCH"), Fire});
		const AChaosImpactPlayerController* OnlineController =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bPair = OnlineController && OnlineController->GetRequestedLocalPlayerCount() >= 2;
		Entries.Add({FSlateRect(930, 714, 1450, 798),
			FString::Printf(TEXT("なまえ  %s%s"), Sessions ? *Sessions->GetPlayerName() : TEXT(""),
				bPair ? TEXT("  ＋(2)") : TEXT("")),
			TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::OnlineName:
	case EChaosImpactScreen::OnlinePassword:
	case EChaosImpactScreen::OnlineRoomName:
		Entries.Add({FSlateRect(1010, 714, 1450, 798), TEXT("決定"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::RoomList:
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		static const TArray<FChaosImpactRoomListing> NoListings;
		const TArray<FChaosImpactRoomListing>& Listings = Sessions ? Sessions->GetRoomListings() : NoListings;
		const int32 LocalPlayers = Sessions ? Sessions->GetLocalPlayerCount() : 1;
		// Each room is a row: its name (Title), host (Detail) and members (Number).
		for (int32 Index = 0; Index < FMath::Min(Listings.Num(), 5); ++Index)
		{
			const FChaosImpactRoomListing& Listing = Listings[Index];
			FMenuEntry Row{FSlateRect(250, 232 + Index * 92, 1350, 312 + Index * 92), Listing.RoomName, Listing.HostName,
				FString::Printf(TEXT("%d/%d"), Listing.Members, AChaosImpactGameState::MaxMembers), Ice};
			Row.bDisabled = !Listing.bOpen || Listing.Members + LocalPlayers > AChaosImpactGameState::MaxMembers;
			Entries.Add(Row);
		}
		Entries.Add({FSlateRect(1010, 724, 1450, 800), TEXT("さがしなおす"), TEXT("refresh"), TEXT(""), Gold});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT("back"), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::OnlineStatus:
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		const bool bFailed = Sessions && !Sessions->GetCreateError().IsEmpty();
		Entries.Add({FSlateRect(560, 714, 1040, 798), bFailed ? TEXT("あいことばを変える") : TEXT("やめる"),
			TEXT(""), TEXT(""), bFailed ? Gold : Muted});
		break;
	}
	case EChaosImpactScreen::Pause:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		if (Controller && Controller->IsOnlineRoom())
		{
			float Y = 285.0f;
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice});
			const AChaosImpactGameState* PauseRoom = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
			if (Controller->CanOpenMatchRulesFromPause())
			{
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62),
					PauseRoom && PauseRoom->bRulesDecided ? TEXT("ルールを変える") : TEXT("ルールを決める"),
					TEXT("start_match"), TEXT(""), Gold});
			}
			if (Controller->CanCloseRecruitment())
			{
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("メンバー募集終了"), TEXT("close_recruit"),
					TEXT(""), Gold});
			}
			if (Controller->CanReopenRecruitment())
			{
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("メンバー募集を再開"), TEXT("reopen_recruit"),
					TEXT(""), Ice});
			}
			if (Controller->CanRenameRoom())
			{
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("へやの名前を変える"), TEXT("rename_room"),
					TEXT(""), Ice});
			}
			Y += 80.0f;
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62),
				AChaosImpactPlayerController::IsRumbleEnabled() ? TEXT("振動：ON") : TEXT("振動：OFF"),
				TEXT("rumble"), TEXT(""), AChaosImpactPlayerController::IsRumbleEnabled() ? Ice : Muted});
			Y += 80.0f;
			const bool bLeaveArmed = ArmedIndex == Entries.Num();
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62),
				bLeaveArmed ? TEXT("もう一度おすと決定")
					: Controller->IsOnlineRoomHost() ? TEXT("へやを解散する") : TEXT("へやをぬける"),
				TEXT("leave"), TEXT(""), Fire});
			break;
		}
		const bool bArc = Controller
			&& Controller->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
		Entries.Add({FSlateRect(490, 245, 1110, 307), TEXT("ゲームに戻る"), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(490, 325, 1110, 387),
			bArc ? TEXT("投球軌道：放物線") : TEXT("投球軌道：直線"), TEXT(""), TEXT(""), bArc ? Fire : Ice});
		if (Controller && Controller->ShowsTrainingPauseEntries())
		{
			Entries.Add({FSlateRect(490, 405, 1110, 467), TEXT("トレーニング設定"), TEXT(""), TEXT(""), Ice});
			Entries.Add({FSlateRect(490, 485, 1110, 547), TEXT("トレーニングをリトライ"), TEXT(""), TEXT(""), Gold});
		}
		Entries.Add({FSlateRect(490, 565, 1110, 627), TEXT("モード選択へ"), TEXT(""), TEXT(""), Fire});
		Entries.Add({FSlateRect(490, 645, 1110, 707), TEXT("タイトル画面へ"), TEXT(""), TEXT(""), Muted});
		// Appended last so the existing pause entry indices stay unchanged.
		Entries.Add({FSlateRect(490, 725, 1110, 787),
			AChaosImpactPlayerController::IsRumbleEnabled() ? TEXT("振動：ON") : TEXT("振動：OFF"),
			TEXT("rumble"), TEXT(""), AChaosImpactPlayerController::IsRumbleEnabled() ? Ice : Muted});
		if (Controller && Controller->IsSearchingForRoom())
		{
			Entries.Add({FSlateRect(490, 805, 1110, 867),
				ArmedIndex == Entries.Num() ? TEXT("もう一度おすと決定") : TEXT("へやをさがすのをやめる"),
				TEXT("stop_search"), TEXT(""), Fire});
		}
		break;
	}
	case EChaosImpactScreen::TrainingSettings:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const int32 Players = Controller ? Controller->GetRequestedLocalPlayerCount() : 1;
		const bool bTargets = !Controller || Controller->AreTrainingTargetsEnabled();
		const int32 CPUCount = Controller ? Controller->GetTrainingCPUCount() : 0;
		const bool bGamepad = Controller && Controller->WillPrimaryUseGamepad();
		Entries.Add({FSlateRect(450, 220, 1150, 282),
			FString::Printf(TEXT("プレイヤー人数：%d人"), Players), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(450, 300, 1150, 362),
			bGamepad ? TEXT("1P  コントローラー") : TEXT("1P  キーボード＋マウス"),
			TEXT(""), TEXT(""), bGamepad ? Fire : Ice});
		Entries.Add({FSlateRect(450, 380, 1150, 442),
			bTargets ? TEXT("マト：あり") : TEXT("マト：なし"), TEXT(""), TEXT(""), bTargets ? Gold : Muted});
		Entries.Add({FSlateRect(450, 460, 1150, 522),
			FString::Printf(TEXT("CPUプレイヤー：%d体"), CPUCount), TEXT(""), TEXT(""),
			CPUCount > 0 ? Fire : Muted});
		Entries.Add({FSlateRect(450, 575, 1150, 647), TEXT("設定を適用"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(450, 675, 1150, 747), TEXT("ポーズ画面へ戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::TrainingOverlay:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const int32 Players = Controller ? Controller->GetRequestedLocalPlayerCount() : 1;
		const bool bTargets = !Controller || Controller->AreTrainingTargetsEnabled();
		const int32 CPUCount = Controller ? Controller->GetTrainingCPUCount() : 0;
		const bool bArc = Controller
			&& Controller->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
		Entries.Add({FSlateRect(78, 174, 624, 232),
			bArc ? TEXT("投球軌道：放物線") : TEXT("投球軌道：直線"), TEXT(""), TEXT(""), bArc ? Fire : Ice});
		Entries.Add({FSlateRect(78, 244, 624, 302),
			FString::Printf(TEXT("プレイヤー人数：%d人"), Players), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(78, 314, 624, 372),
			bTargets ? TEXT("マト：あり") : TEXT("マト：なし"), TEXT(""), TEXT(""), bTargets ? Gold : Muted});
		Entries.Add({FSlateRect(78, 384, 624, 442),
			FString::Printf(TEXT("CPUプレイヤー：%d体"), CPUCount), TEXT(""), TEXT(""),
			CPUCount > 0 ? Fire : Muted});
		const EChaosImpactBallType SummonType = Controller
				? Controller->GetTrainingSummonBallType() : EChaosImpactBallType::Fire;
			const FLinearColor SummonAccent = SummonType == EChaosImpactBallType::Fire ? Fire
				: SummonType == EChaosImpactBallType::Ice ? Ice
				: SummonType == EChaosImpactBallType::Normal ? Gold : ChaosImpactBallTypes::GetColor(SummonType);
			Entries.Add({FSlateRect(78, 454, 624, 512),
				FString::Printf(TEXT("呼び出すボール：%s"), ChaosImpactBallTypes::GetDisplayName(SummonType)),
				TEXT(""), TEXT(""), SummonAccent});
			Entries.Add({FSlateRect(78, 524, 624, 582), TEXT("ボールを呼び出す"), TEXT(""), TEXT(""), SummonAccent});
			Entries.Add({FSlateRect(78, 608, 624, 666), TEXT("トレーニングをリセット"), TEXT(""), TEXT(""), Fire});
		Entries.Add({FSlateRect(78, 684, 624, 742), TEXT("閉じる"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::MatchRules:
	{
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const FChaosImpactMatchRules Rules = Controller ? Controller->GetPendingMatchRules() : FChaosImpactMatchRules();
		Entries.Add({FSlateRect(430, 226, 1170, 298), FString::Printf(TEXT("試合時間　＜  %d分  ＞"), Rules.Minutes),
			TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(430, 318, 1170, 390),
			FString::Printf(TEXT("ルール　＜  %s  ＞"), *ChaosImpactMatch::DescribeTeams(Rules.TeamCount)),
			TEXT(""), TEXT(""), Rules.IsTeamBattle() ? Gold : Fire});
		Entries.Add({FSlateRect(430, 410, 1170, 482), FString::Printf(TEXT("CPU　＜  %d人  ＞"), Rules.CPUCount),
			TEXT(""), TEXT(""), Rules.CPUCount > 0 ? Fire : Muted});
		Entries.Add({FSlateRect(1030, 712, 1454, 800), TEXT("決定"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(146, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::TeamSelect:
	{
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bCanStart = Controller && Controller->CanStartVersusMatch();
		FMenuEntry Start{FSlateRect(1030, 712, 1454, 800), bCanStart ? TEXT("試合開始") : TEXT("ホスト待ち"),
			TEXT(""), TEXT(""), Gold};
		Start.bDisabled = !bCanStart;
		Entries.Add(Start);
		if (bCanStart)
		{
			Entries.Add({FSlateRect(146, 724, 470, 788), TEXT("ルール変更"), TEXT(""), TEXT(""), Muted});
		}
		break;
	}
	case EChaosImpactScreen::MatchEnd:
		Entries.Add({FSlateRect(230, 776, 630, 846), TEXT("もう一度"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(670, 776, 1070, 846), TEXT("ルールを変える"), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(1110, 776, 1510, 846), TEXT("メニューへ"), TEXT(""), TEXT(""), Muted});
		break;
	default:
		break;
	}
}

void UChaosImpactMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Slate continues to animate while the gameplay world is paused.
	const double Now = FPlatformTime::Seconds();
	AnimationSeconds = static_cast<float>(Now - ScreenStartedAt);

	SelectBlend.SetNumZeroed(Entries.Num());
	for (int32 Index = 0; Index < SelectBlend.Num(); ++Index)
	{
		SelectBlend[Index] = FMath::FInterpTo(SelectBlend[Index],
			Index == SelectedIndex ? 1.0f : 0.0f, InDeltaTime, 16.0f);
	}

	if (NameInput && (Screen == EChaosImpactScreen::OnlineName || Screen == EChaosImpactScreen::OnlineRoomName))
	{
		const float Scale = DesignScale(MyGeometry);
		const FVector2D Offset = (MyGeometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
		if (UCanvasPanelSlot* InputSlot = Cast<UCanvasPanelSlot>(NameInput->Slot))
		{
			InputSlot->SetPosition(Offset + FVector2D(440.0f, 372.0f) * Scale);
			InputSlot->SetSize(FVector2D(720.0f, 96.0f) * Scale);
		}
		if (!FMath::IsNearlyEqual(NameInputFontScale, Scale, 0.01f))
		{
			NameInputFontScale = Scale;
			NameInput->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Black"), FMath::Max(12.0f, 52.0f * Scale)));
		}
		if (bNameFocusPending)
		{
			bNameFocusPending = false;
			NameInput->SetUserFocus(GetOwningPlayer());
		}
	}
	if (Screen == EChaosImpactScreen::RoomList)
	{
		if (const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			Sessions && Sessions->GetRoomListingsVersion() != LastRoomListingsVersion)
		{
			// The list refreshes every couple of seconds; keep the selection where it was.
			LastRoomListingsVersion = Sessions->GetRoomListingsVersion();
			const int32 PreviousSelection = SelectedIndex;
			RefreshEntries();
			SelectedIndex = FMath::Clamp(PreviousSelection, 0, FMath::Max(0, Entries.Num() - 1));
		}
	}
	if (Screen == EChaosImpactScreen::OnlineStatus)
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		const FString CreateError = Sessions ? Sessions->GetCreateError() : FString();
		if (CreateError != LastCreateError)
		{
			LastCreateError = CreateError;
			RefreshEntries();
		}
	}

	if (Screen == EChaosImpactScreen::ControllerAssignment)
	{
		if (const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			for (int32 PlayerIndex = 0; PlayerIndex < 4; ++PlayerIndex)
			{
				const bool bJoined = Controller->IsInputAssignedToPlayer(PlayerIndex);
				if (bJoined && !bSlotJoined[PlayerIndex])
				{
					SlotJoinedAt[PlayerIndex] = Now;
				}
				bSlotJoined[PlayerIndex] = bJoined;
			}
		}
	}
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
	const float T = AnimationSeconds;
	const double Now = FPlatformTime::Seconds();
	const bool bFrontEnd = Screen != EChaosImpactScreen::TrainingOverlay
		&& Screen != EChaosImpactScreen::Pause && Screen != EChaosImpactScreen::TrainingSettings
		&& Screen != EChaosImpactScreen::MatchEnd;

	const FMenuPainter Full{AllottedGeometry, OutDrawElements, BaseLayer + 1};
	FMenuPainter P{DesignGeometry, OutDrawElements, BaseLayer + 2};

	if (Screen == EChaosImpactScreen::TrainingOverlay)
	{
		// Keep the live arena readable and place the opaque UI to the side of P1.
		Full.Box(0, 0, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y,
			FLinearColor(0.0f, 0.0f, 0.0f, 0.14f));
		const FGeometry Panel = MakeSkewed(DesignGeometry, 32, 30, 640, 760, -0.04f);
		const FMenuPainter Side{Panel, OutDrawElements, BaseLayer + 2};
		Side.Box(0, 0, 640, 760, FLinearColor(0.008f, 0.014f, 0.027f, 0.93f));
		Side.Box(0, 0, 10, 760, Ice);
		Side.Box(630, 0, 10, 760, Fire);
		P.Text(TEXT("TRAINING"), 66, 60, 44, Paper);
		P.Box(68, 122, 150, 8, Fire);
		P.Box(226, 122, 48, 8, Ice);
	}
	else if (Screen == EChaosImpactScreen::MatchEnd)
	{
		// The results stay visible above; only a band for the choices is added.
		const FMenuPainter Band{DesignGeometry, OutDrawElements, BaseLayer + 1, EaseOut(T / 0.3f)};
		Band.Box(-800.0f, 752.0f, 3200.0f, 118.0f, WithAlpha(Ink, 0.86f));
		Band.Box(-800.0f, 752.0f, 3200.0f, 4.0f, Gold);
	}
	else
	{
		Full.Box(0, 0, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Ink);
		PaintBackdrop(DesignGeometry, OutDrawElements, BaseLayer + 1, T);
	}

	if (Screen == EChaosImpactScreen::Title)
	{
		const float ImpactAt = 0.84f;
		const float Arrival = FMath::Clamp((T - 0.12f) / 0.72f, 0.0f, 1.0f);
		const float Ease = Arrival * Arrival * Arrival;
		const float Zoom = FMath::Lerp(1.9f, 1.0f, Ease) + 0.008f * FMath::Sin(T * 2.2f);
		const float Shake = T > ImpactAt && T < 1.2f
			? FMath::Sin(T * 80.0f) * (1.2f - T) * 26.0f : 0.0f;
		const float Width = 1080.0f * Zoom;
		const float Height = Width * LogoBrush.ImageSize.Y / FMath::Max(LogoBrush.ImageSize.X, 1.0f);

		// Incoming slashes converge on the logo right before it lands.
		if (T < ImpactAt + 0.1f)
		{
			const float Slash = EaseOut(T / ImpactAt);
			const FGeometry Slant = MakeSkewed(DesignGeometry, 0, 0, 1600, 900, -0.36f);
			const FMenuPainter S{Slant, OutDrawElements, BaseLayer + 2, 1.0f - FMath::Max(0.0f, T - ImpactAt) * 10.0f};
			S.Box(FMath::Lerp(-700.0f, 560.0f, Slash), 380, 520, 14, Ice);
			S.Box(FMath::Lerp(1780.0f, 520.0f, Slash), 432, 520, 14, Fire);
		}
		if (T > ImpactAt && T < ImpactAt + 0.8f)
		{
			const float Wave = (T - ImpactAt) / 0.8f;
			const FMenuPainter Rings{DesignGeometry, OutDrawElements, BaseLayer + 2, 1.0f - Wave};
			Rings.Ring(FVector2D(800, 412), 120.0f + 820.0f * EaseOut(Wave), Paper, 3.0f + 12.0f * (1.0f - Wave), 96);
			Rings.Ring(FVector2D(800, 412), 60.0f + 560.0f * EaseOut(Wave), Fire, 6.0f, 96);
			Rings.Ring(FVector2D(800, 412), 30.0f + 380.0f * EaseOut(Wave), Ice, 6.0f, 96);
		}

		if (LogoTexture)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 3,
				DesignGeometry.ToPaintGeometry(FVector2f(Width, Height),
					FSlateLayoutTransform(FVector2f(800.0f - Width * 0.5f + Shake, 408.0f - Height * 0.5f))),
				&LogoBrush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, FMath::Clamp(Arrival * 1.6f, 0.0f, 1.0f)));
		}
		else
		{
			P.Text(TEXT("カオスインパクト"), 800 + Shake, 330, 96, Paper, ETextAlign::Center);
		}

		if (T > ImpactAt && T < ImpactAt + 0.3f)
		{
			const FMenuPainter Flash{AllottedGeometry, OutDrawElements, BaseLayer + 4};
			Flash.Box(0, 0, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y,
				WithAlpha(Paper, 0.8f * (1.0f - (T - ImpactAt) / 0.3f)));
		}

		const float PromptIn = EaseOut((T - 1.25f) / 0.35f);
		if (PromptIn > 0.0f)
		{
			const float Blink = 0.4f + 0.6f * (0.5f + 0.5f * FMath::Cos((T - 1.25f) * 4.2f));
			const FGeometry Prompt = MakeSkewed(DesignGeometry, 520, 716, 560, 70, -0.25f);
			const FMenuPainter Row{Prompt, OutDrawElements, BaseLayer + 3, PromptIn};
			Row.Box(280.0f - 260.0f * PromptIn, 28, 80, 12, Ice);
			Row.Box(200.0f + 260.0f * PromptIn, 28, 80, 12, Fire);
			Row.Text(TEXT("PRESS START"), 280, 4, 44, WithAlpha(Paper, Blink), ETextAlign::Center);
		}
	}
	else if (Screen == EChaosImpactScreen::ModeSelect)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("MODE SELECT"), T);
	}
	else if (Screen == EChaosImpactScreen::TrainingSetup)
	{
		const AChaosImpactPlayerController* SetupController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bVersus = SetupController && SetupController->GetPlayFlow() == EChaosImpactPlayFlow::VersusLocal;
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, bVersus ? TEXT("VS LOCAL") : TEXT("TRAINING"), T);
	}
	else if (Screen == EChaosImpactScreen::ControllerAssignment)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("PLAYER ENTRY"), T);
		if (const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			const int32 Players = Controller->GetRequestedLocalPlayerCount();
			const int32 ReadyPlayers = Controller->GetAssignedPlayerCount();
			for (int32 PlayerIndex = 0; PlayerIndex < 4; ++PlayerIndex)
			{
				const float SlotIn = EaseOut((T - 0.1f - 0.06f * PlayerIndex) / 0.35f);
				FSlateRect Rect = JoinSlotRect(PlayerIndex);
				Rect = Rect.OffsetBy(FVector2D((1.0f - SlotIn) * 140.0f, 0.0f));
				const bool bRequired = PlayerIndex < Players;
				const float JoinAge = static_cast<float>(Now - SlotJoinedAt[PlayerIndex]);
				const bool bJoined = bRequired && Controller->IsInputAssignedToPlayer(PlayerIndex) && JoinAge >= 0.0f;
				const bool bNext = bRequired && !Controller->IsInputAssignedToPlayer(PlayerIndex)
					&& PlayerIndex == ReadyPlayers;
				PaintJoinSlot(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, PlayerIndex, bRequired,
					bNext, bJoined, Controller->IsKeyboardMouseAssignedToPlayer(PlayerIndex), JoinAge, T, SlotIn);
				if (bJoined)
				{
					PaintJoinBurst(DesignGeometry, OutDrawElements, BaseLayer + 5, Rect, PlayerIndex, JoinAge);
				}
			}
		}
	}
	else if (Screen == EChaosImpactScreen::SoloReady)
	{
		const float E = EaseOut(T / 0.45f);
		const FGeometry Title = MakeSkewed(DesignGeometry, 0, 240, 1600, 320, -0.2f, FMath::Lerp(1.25f, 1.0f, E));
		const FMenuPainter Big{Title, OutDrawElements, BaseLayer + 3, E};
		Big.Text(TEXT("SOLO"), 800, 0, 150, Paper, ETextAlign::Center, TEXT("Black"), 5.0f, Ice);
		Big.Text(TEXT("COMING SOON"), 800, 226, 38, Ice, ETextAlign::Center, TEXT("BlackItalic"));
	}
	else if (Screen == EChaosImpactScreen::MultiReady)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("ONLINE"), T);
	}
	else if (Screen == EChaosImpactScreen::VSSelect)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("VS MODE"), T);
	}
	else if (Screen == EChaosImpactScreen::OnlinePlayers)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("VS ONLINE"), T);
	}
	else if (Screen == EChaosImpactScreen::OnlineName || Screen == EChaosImpactScreen::OnlineRoomName)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2,
			Screen == EChaosImpactScreen::OnlineRoomName ? TEXT("へやのなまえ") : TEXT("なまえ"), T);
		const float E = EaseOut((T - 0.1f) / 0.35f);
		const FGeometry Panel = MakeSkewed(DesignGeometry, 400.0f + (1.0f - E) * 120.0f, 340.0f, 800.0f, 160.0f, -0.18f);
		const FMenuPainter Field{Panel, OutDrawElements, BaseLayer + 2, E};
		Field.Box(12.0f, 14.0f, 800.0f, 160.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		Field.Box(0.0f, 0.0f, 800.0f, 160.0f, FLinearColor(0.02f, 0.027f, 0.047f, 0.97f));
		Field.Box(0.0f, 0.0f, 12.0f, 160.0f, Ice);
		Field.Box(40.0f, 136.0f, 720.0f, 6.0f, WithAlpha(Fire, 0.55f + 0.45f * FMath::Sin(T * 5.0f)));
		const FString Typed = NameInput ? NameInput->GetText().ToString() : FString();
		const FMenuPainter NameText{Panel, OutDrawElements, BaseLayer + 3, E};
		NameText.Text(Typed, 400.0f, 34.0f, 58.0f, Paper, ETextAlign::Center);
		if (NameInput && NameInput->HasKeyboardFocus() && FMath::Fmod(T, 1.0f) < 0.55f)
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Black"), 58.0f);
			const float Width = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Typed, Font).X;
			NameText.Box(400.0f + Width * 0.5f + 8.0f, 40.0f, 5.0f, 80.0f, Ice);
		}
		P.Text(FString::Printf(TEXT("%d/%d"), Typed.Len(), UChaosImpactSessionSubsystem::MaxNameLength),
			1180.0f, 520.0f, 26.0f, Muted, ETextAlign::Right);
	}
	else if (Screen == EChaosImpactScreen::RoomList)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("へやをさがす"), T);
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		const float ChipIn = EaseOut((T - 0.1f) / 0.3f);
		const FMenuPainter ChipPainter{MakeSkewed(DesignGeometry, 1070.0f + (1.0f - ChipIn) * 80.0f, 96.0f, 360.0f, 70.0f, -0.3f),
			OutDrawElements, BaseLayer + 2, ChipIn};
		ChipPainter.Box(0.0f, 0.0f, 360.0f, 70.0f, Fire);
		ChipPainter.Text(FString::Printf(TEXT("あいことば  %s"), Sessions ? *Sessions->GetPassword() : TEXT("")),
			180.0f, 14.0f, 30.0f, Paper, ETextAlign::Center);
		if (Sessions && Sessions->GetRoomListings().IsEmpty())
		{
			// Nothing yet: keep looking, with a small spinner.
			const FMenuPainter Waiting{DesignGeometry, OutDrawElements, BaseLayer + 3, EaseOut((T - 0.2f) / 0.3f)};
			Waiting.Text(Sessions->HasSearchedOnce() ? TEXT("このあいことばのへやはまだありません") : TEXT("へやをさがしています"),
				800.0f, 380.0f, 40.0f, Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
			Waiting.Text(TEXT("見つかると ここに出ます"), 800.0f, 446.0f, 24.0f, Muted, ETextAlign::Center);
			const FVector2D SpinCenter(800.0f, 540.0f);
			for (int32 Dot = 0; Dot < 8; ++Dot)
			{
				const float Angle = T * 6.0f + Dot * UE_TWO_PI / 8.0f;
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				Waiting.Line(SpinCenter + Direction * 18.0f, SpinCenter + Direction * 32.0f,
					WithAlpha(Paper, 0.15f + 0.85f * Dot / 7.0f), 6.0f);
			}
		}
	}
	else if (Screen == EChaosImpactScreen::OnlinePassword)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("あいことば"), T);
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bCreate = !Controller || Controller->IsPendingCreateRoom();
		const FLinearColor ModeColor = bCreate ? Ice : Fire;
		const float ChipIn = EaseOut((T - 0.1f) / 0.3f);
		const FGeometry Chip = MakeSkewed(DesignGeometry, 1070.0f + (1.0f - ChipIn) * 80.0f, 96.0f, 360.0f, 70.0f, -0.3f);
		const FMenuPainter ChipPainter{Chip, OutDrawElements, BaseLayer + 2, ChipIn};
		ChipPainter.Box(0.0f, 0.0f, 360.0f, 70.0f, ModeColor);
		ChipPainter.Text(bCreate ? TEXT("へやをつくる") : TEXT("へやをさがす"), 180.0f, 10.0f, 34.0f, Paper,
			ETextAlign::Center);

		for (int32 Digit = 0; Digit < 4; ++Digit)
		{
			const float CardIn = EaseOut((T - 0.12f - 0.05f * Digit) / 0.3f);
			const FSlateRect Rect = PasswordDigitRect(Digit).OffsetBy(FVector2D((1.0f - CardIn) * 100.0f, 0.0f));
			const float W = Rect.Right - Rect.Left;
			const float H = Rect.Bottom - Rect.Top;
			const bool bSelected = Digit == PasswordCursor;
			const FGeometry Card = MakeSkewed(DesignGeometry, Rect.Left, Rect.Top, W, H, -0.12f, bSelected ? 1.06f : 1.0f);
			const FMenuPainter C{Card, OutDrawElements, BaseLayer + 3, CardIn};
			FLinearColor Deep = ModeColor * 0.45f;
			Deep.A = 1.0f;
			C.Box(10.0f, 12.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
			C.Box(0.0f, 0.0f, W, H, bSelected ? Deep : FLinearColor(0.02f, 0.027f, 0.047f, 0.97f));
			C.Box(0.0f, H - 10.0f, W, 10.0f, bSelected ? ModeColor : FLinearColor(0.2f, 0.24f, 0.32f, 1.0f));
			C.Text(FString::FromInt(PasswordDigits[Digit]), W * 0.5f, 20.0f, 120.0f, Paper, ETextAlign::Center,
				TEXT("Black"), bSelected ? 4.0f : 0.0f, Ink);
			if (bSelected)
			{
				const float Pulse = 0.6f + 0.4f * FMath::Sin(T * 6.0f);
				C.Outline(-6.0f, -6.0f, W + 12.0f, H + 12.0f, WithAlpha(Paper, Pulse), 4.0f);
				const FVector2D Top(W * 0.5f, -30.0f);
				const FVector2D Bottom(W * 0.5f, H + 30.0f);
				C.Line(Top + FVector2D(-18.0f, 10.0f), Top, Paper, 5.0f);
				C.Line(Top, Top + FVector2D(18.0f, 10.0f), Paper, 5.0f);
				C.Line(Bottom + FVector2D(-18.0f, -10.0f), Bottom, Paper, 5.0f);
				C.Line(Bottom, Bottom + FVector2D(18.0f, -10.0f), Paper, 5.0f);
			}
		}
	}
	else if (Screen == EChaosImpactScreen::OnlineStatus)
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		const FString Error = Sessions ? Sessions->GetCreateError() : FString();
		const float E = EaseOut(T / 0.35f);
		const FGeometry Band = MakeSkewed(DesignGeometry, -200.0f - (1.0f - E) * 400.0f, 300.0f, 2000.0f, 240.0f, -0.3f);
		const FMenuPainter BandPainter{Band, OutDrawElements, BaseLayer + 2, E};
		BandPainter.Box(0.0f, 0.0f, 2000.0f, 240.0f, FLinearColor(0.015f, 0.018f, 0.035f, 0.92f));
		BandPainter.Box(0.0f, 0.0f, 2000.0f, 8.0f, Error.IsEmpty() ? Ice : Fire);
		BandPainter.Box(0.0f, 232.0f, 2000.0f, 8.0f, Error.IsEmpty() ? Fire : Fire);
		const FMenuPainter Label{DesignGeometry, OutDrawElements, BaseLayer + 3, E};
		Label.Text(Error.IsEmpty() ? FString(TEXT("へやをつくっています")) : Error, 800.0f, 350.0f, 54.0f,
			Error.IsEmpty() ? Paper : Fire, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
		Label.Text(FString::Printf(TEXT("あいことば  %s"), Sessions ? *Sessions->GetPassword() : TEXT("")),
			800.0f, 450.0f, 30.0f, Muted, ETextAlign::Center);
		if (Error.IsEmpty())
		{
			for (int32 Dot = 0; Dot < 8; ++Dot)
			{
				const float Angle = T * 5.0f + Dot * UE_TWO_PI / 8.0f;
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				Label.Line(FVector2D(800.0f, 610.0f) + Direction * 26.0f, FVector2D(800.0f, 610.0f) + Direction * 44.0f,
					WithAlpha(Dot == 0 ? Fire : Paper, 0.25f + 0.75f * Dot / 7.0f), 6.0f);
			}
		}
	}
	else if (Screen == EChaosImpactScreen::Pause)
	{
		const float Glitch = FMath::Clamp(1.0f - T / 0.55f, 0.0f, 1.0f);
		const float JitterX = FMath::Sin(T * 93.0f) * Glitch * 13.0f;
		if (Glitch > 0.0f)
		{
			for (int32 Scanline = 0; Scanline < 12; ++Scanline)
			{
				const float Y = FMath::Fmod(Scanline * 79.0f + T * 920.0f, 900.0f);
				P.Box(0.0f, Y, 1600.0f, Scanline % 3 == 0 ? 3.0f : 1.0f,
					FLinearColor(0.72f, 0.88f, 1.0f, 0.11f * Glitch));
			}
		}
		P.Text(TEXT("PAUSE"), 800.0f + JitterX * 0.18f, 94, 78, Paper, ETextAlign::Center);
		P.Box(650, 206, 150, 8, Ice);
		P.Box(800, 206, 150, 8, Fire);
	}
	else if (Screen == EChaosImpactScreen::MatchRules)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("RULE"), T);
		if (const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			const FChaosImpactMatchRules& Rules = Controller->GetPendingMatchRules();
			const int32 Humans = Controller->GetMatchHumanCount();
			const int32 Total = Humans + Rules.CPUCount;
			const float In = EaseOut((T - 0.2f) / 0.35f);
			const FGeometry Panel = MakeSkewed(DesignGeometry, 430.0f + (1.0f - In) * 80.0f, 512.0f, 740.0f, 168.0f, -0.12f);
			const FMenuPainter Info{Panel, OutDrawElements, BaseLayer + 2, In};
			Info.Box(12.0f, 14.0f, 740.0f, 168.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			Info.Box(0.0f, 0.0f, 740.0f, 168.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
			Info.Box(0.0f, 0.0f, 10.0f, 168.0f, Gold);
			Info.Text(FString::Printf(TEXT("プレイヤー %d人 ＋ CPU %d人 ＝ %d人"), Humans, Rules.CPUCount, Total),
				36.0f, 14.0f, 30.0f, Paper);
			Info.Text(Rules.IsTeamBattle()
				? FString::Printf(TEXT("1チーム最大 %d人・チームの合計ポイントで勝負"),
					ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Total))
				: FString(TEXT("ポイントが一番多い人の勝ち")), 38.0f, 66.0f, 24.0f, Gold);
			Info.Text(TEXT("敵に当てる +1pt　　撃破ボーナス +1pt"), 38.0f, 110.0f, 24.0f, Muted);
		}
	}
	else if (Screen == EChaosImpactScreen::TeamSelect)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("TEAM SELECT"), T);
		const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		if (Match && Match->IsTeamBattle())
		{
			const int32 Teams = Match->Rules.TeamCount;
			const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
			const int32 Capacity = ChaosImpactMatch::GetTeamCapacity(Teams, Humans.Num() + Match->Rules.CPUCount);
			// Players on this machine, in local player order, so their cards can be marked P1-P4.
			TArray<const APlayerState*, TInlineAllocator<4>> LocalStates;
			if (const UGameInstance* OwningGameInstance = GetGameInstance())
			{
				for (const ULocalPlayer* LocalPlayer : OwningGameInstance->GetLocalPlayers())
				{
					const APlayerController* LocalController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
					LocalStates.Add(LocalController ? LocalController->PlayerState.Get() : nullptr);
				}
			}
			P.Text(FString::Printf(TEXT("＜ ＞ でチームを選ぶ（それぞれのコントローラーで）　CPU %d人は人数の少ないチームに入ります"),
				Match->Rules.CPUCount), 800.0f, 196.0f, 24.0f, Muted, ETextAlign::Center);
			constexpr float Gap = 28.0f;
			const float Width = (1300.0f - Gap * (Teams - 1)) / Teams;
			for (int32 Team = 0; Team < Teams; ++Team)
			{
				const float ColumnIn = EaseOut((T - 0.1f - 0.05f * Team) / 0.35f);
				const FGeometry Column = MakeSkewed(DesignGeometry, 150.0f + Team * (Width + Gap) + (1.0f - ColumnIn) * 100.0f,
					244.0f, Width, 440.0f, -0.06f);
				const FMenuPainter C{Column, OutDrawElements, BaseLayer + 3, ColumnIn};
				const FLinearColor TeamColor = ChaosImpactMatch::GetTeamColor(Team);
				FLinearColor Deep = TeamColor * 0.3f;
				Deep.A = 1.0f;
				int32 Count = 0;
				for (const AChaosImpactPlayerState* Member : Humans)
				{
					Count += Member->TeamIndex == Team ? 1 : 0;
				}
				C.Box(12.0f, 14.0f, Width, 440.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
				C.Box(0.0f, 0.0f, Width, 440.0f, FLinearColor(0.02f, 0.026f, 0.045f, 0.96f));
				C.Box(0.0f, 0.0f, Width, 74.0f, Deep);
				C.Box(0.0f, 68.0f, Width, 6.0f, TeamColor);
				C.Text(ChaosImpactMatch::GetTeamName(Team), 24.0f, 12.0f, 36.0f, Paper);
				C.Text(FString::Printf(TEXT("%d/%d"), Count, Capacity), Width - 20.0f, 18.0f, 28.0f,
					Count >= Capacity ? Fire : Gold, ETextAlign::Right);
				int32 Row = 0;
				for (const AChaosImpactPlayerState* Member : Humans)
				{
					if (Member->TeamIndex != Team)
					{
						continue;
					}
					const float Y = 92.0f + Row++ * 62.0f;
					const int32 LocalIndex = LocalStates.IndexOfByKey(Member);
					const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
					const FString Name = Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
					C.Box(14.0f, Y, Width - 28.0f, 52.0f, LocalIndex >= 0
						? WithAlpha(TeamColor, 0.4f) : FLinearColor(0.035f, 0.045f, 0.075f, 0.96f));
					C.Text(Name, 30.0f, Y + 8.0f, 27.0f, Paper);
					if (LocalIndex >= 0)
					{
						C.Text(FString::Printf(TEXT("P%d"), LocalIndex + 1), Width - 30.0f, Y + 9.0f, 26.0f,
							PlayerAccents[LocalIndex % 4], ETextAlign::Right, TEXT("BlackItalic"));
						C.Outline(14.0f, Y, Width - 28.0f, 52.0f, WithAlpha(Paper, 0.55f + 0.35f * FMath::Sin(T * 6.0f)), 2.0f);
					}
				}
			}
		}
	}
	else if (Screen == EChaosImpactScreen::TrainingSettings)
	{
		P.Text(TEXT("TRAINING SETTINGS"), 800, 92, 62, Paper, ETextAlign::Center);
		P.Box(610, 186, 190, 8, Ice);
		P.Box(800, 186, 190, 8, Fire);
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Screen == EChaosImpactScreen::Title)
		{
			break;
		}
		const FMenuEntry& Entry = Entries[Index];
		const float Blend = SelectBlend.IsValidIndex(Index)
			? SelectBlend[Index] : (Index == SelectedIndex ? 1.0f : 0.0f);
		const float EntryIn = bFrontEnd ? EaseOut((T - 0.12f - 0.05f * Index) / 0.35f) : 1.0f;
		const FSlateRect Rect = Entry.Rect.OffsetBy(FVector2D((1.0f - EntryIn) * 120.0f, 0.0f));
		const bool bPressed = PressedIndex == Index;
		if ((Screen == EChaosImpactScreen::ModeSelect || Screen == EChaosImpactScreen::MultiReady
			|| Screen == EChaosImpactScreen::VSSelect) && Index < 2)
		{
			PaintCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title,
				Entry.Accent, Blend, bPressed, EntryIn, T, 118.0f, 0);
		}
		else if ((Screen == EChaosImpactScreen::TrainingSetup && Index < 4)
			|| (Screen == EChaosImpactScreen::OnlinePlayers && Index < 2))
		{
			PaintCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title,
				Entry.Accent, Blend, bPressed, EntryIn, T, 170.0f, Index + 1);
		}
		else if (Screen == EChaosImpactScreen::RoomList && Entry.Detail != TEXT("refresh") && Entry.Detail != TEXT("back"))
		{
			// A room: name, host, members, connection bars, and why it cannot be joined when it cannot.
			const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			const float W = static_cast<float>(Rect.GetSize().X);
			const float H = static_cast<float>(Rect.GetSize().Y);
			const FMenuPainter Row{MakeSkewed(DesignGeometry, static_cast<float>(Rect.Left), static_cast<float>(Rect.Top), W, H, -0.12f),
				OutDrawElements, BaseLayer + 4, EntryIn};
			Row.Box(10.0f, 10.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			Row.Box(0.0f, 0.0f, W, H, FMath::Lerp(FLinearColor(0.02f, 0.027f, 0.047f, 0.95f),
				FLinearColor(0.05f, 0.09f, 0.16f, 0.98f), Blend));
			Row.Box(0.0f, 0.0f, 12.0f + 10.0f * Blend, H, Entry.bDisabled ? Muted : Entry.Accent);
			Row.Text(Entry.Title, 44.0f, 6.0f, 34.0f, Entry.bDisabled ? Muted : Paper, ETextAlign::Left, TEXT("Black"));
			Row.Text(FString::Printf(TEXT("ホスト  %s"), *Entry.Detail), 46.0f, 48.0f, 20.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			Row.Text(Entry.Number, W - 210.0f, 20.0f, 32.0f, Entry.bDisabled ? Muted : Paper, ETextAlign::Right, TEXT("Bold"));
			if (Sessions && Sessions->GetRoomListings().IsValidIndex(Index))
			{
				const FChaosImpactRoomListing& Listing = Sessions->GetRoomListings()[Index];
				const int32 Level = Listing.PingMs <= 40 ? 4 : Listing.PingMs <= 80 ? 3 : Listing.PingMs <= 150 ? 2 : 1;
				const FLinearColor SignalColor = Level == 4 ? FLinearColor(0.2f, 0.95f, 0.45f)
					: Level == 3 ? FLinearColor(0.65f, 0.95f, 0.3f) : Level == 2 ? Gold : Fire;
				for (int32 Bar = 0; Bar < 4; ++Bar)
				{
					const float BarHeight = 12.0f + Bar * 11.0f;
					Row.Box(W - 170.0f + Bar * 16.0f, H - 16.0f - BarHeight, 11.0f, BarHeight,
						Bar < Level ? SignalColor : WithAlpha(Paper, 0.16f));
				}
				if (Entry.bDisabled)
				{
					Row.Box(W - 104.0f, 22.0f, 92.0f, 36.0f, Fire);
					Row.Text(Listing.bOpen ? TEXT("満員") : TEXT("締切"), W - 58.0f, 24.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Black"));
				}
			}
			if (Blend > 0.01f)
			{
				Row.Outline(-4.0f, -4.0f, W + 8.0f, H + 8.0f, WithAlpha(Paper, 0.6f * Blend), 3.0f);
			}
		}
		else
		{
			PaintBar(DesignGeometry, OutDrawElements, BaseLayer + 4, Rect, Entry.Title, Entry.Accent,
				Blend, bPressed, Entry.bDisabled, EntryIn, T);
		}
	}

	if (bFrontEnd && Screen != EChaosImpactScreen::Title)
	{
		PaintEnterWipe(DesignGeometry, OutDrawElements, BaseLayer + 6, T);
	}
	return BaseLayer + 7;
}

void UChaosImpactMenuWidget::Navigate(const FKey Key)
{
	if (Entries.IsEmpty())
	{
		return;
	}
	if (Key != EKeys::Tab && HandlePasswordKey(Key))
	{
		return;
	}
	const bool bLeft = Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left;
	const bool bRight = Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right;
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		Controller && (bLeft || bRight))
	{
		// Rule rows change their value sideways; team select moves P1 between teams.
		if (Screen == EChaosImpactScreen::MatchRules && SelectedIndex <= 2)
		{
			Controller->AdjustMatchRule(SelectedIndex, bRight ? 1 : -1);
			BuildEntries();
			return;
		}
		if (Screen == EChaosImpactScreen::TeamSelect)
		{
			Controller->ChangeOwnTeam(bRight ? 1 : -1);
			return;
		}
	}
	const bool bBack = Key == EKeys::Up || Key == EKeys::Left
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left;
	if ((Screen == EChaosImpactScreen::ModeSelect || Screen == EChaosImpactScreen::MultiReady)
		&& Key != EKeys::Tab)
	{
		// A spatial grid: Solo/Multi above Back/Training.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		const int32 Horizontal[] = {1, 0, 3, 2};
		const int32 Vertical[] = {3, 2, 1, 0};
		SelectedIndex = bHorizontal ? Horizontal[SelectedIndex] : Vertical[SelectedIndex];
	}
	else if (Screen == EChaosImpactScreen::VSSelect && Key != EKeys::Tab)
	{
		// Local / Online side by side above a single back button.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		SelectedIndex = bHorizontal
			? (SelectedIndex == 0 ? 1 : SelectedIndex == 1 ? 0 : 2)
			: (SelectedIndex == 2 ? 0 : 2);
	}
	else
	{
		SelectedIndex = (SelectedIndex + Entries.Num() + (bBack ? -1 : 1)) % Entries.Num();
	}
	PressedIndex = INDEX_NONE;
	DisarmSelection();
}

void UChaosImpactMenuWidget::DisarmSelection()
{
	if (ArmedIndex != INDEX_NONE && ArmedIndex != SelectedIndex)
	{
		ArmedIndex = INDEX_NONE;
		BuildEntries();
	}
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
		if (SelectedIndex == 2) { Controller->BeginTrainingSetup(); }
		else { Controller->ShowMenuScreen(SelectedIndex == 0 ? EChaosImpactScreen::SoloReady
			: SelectedIndex == 1 ? EChaosImpactScreen::VSSelect : EChaosImpactScreen::Title); }
		break;
	case EChaosImpactScreen::TrainingSetup:
		if (SelectedIndex >= 0 && SelectedIndex < 4)
		{
			Controller->PrepareTrainingControllerAssignment(SelectedIndex + 1);
		}
		else if (SelectedIndex == 4)
		{
			Controller->TogglePrimaryInputMode();
			BuildEntries();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::VSSelect:
		if (SelectedIndex == 0) { Controller->BeginVersusLocal(); }
		else if (SelectedIndex == 1) { Controller->BeginVersusOnline(); }
		else { GoBack(); }
		break;
	case EChaosImpactScreen::OnlinePlayers:
		if (SelectedIndex == 0 || SelectedIndex == 1)
		{
			Controller->PrepareTrainingControllerAssignment(SelectedIndex + 1);
		}
		else if (SelectedIndex == 2)
		{
			Controller->TogglePrimaryInputMode();
			BuildEntries();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::ControllerAssignment:
		if (SelectedIndex == 0)
		{
			Controller->ConfirmControllerAssignments();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::SoloReady:
		if (SelectedIndex == 1) { Controller->BeginTrainingSetup(); }
		else { Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect); }
		break;
	case EChaosImpactScreen::MultiReady:
		if (SelectedIndex == 0 || SelectedIndex == 1)
		{
			Controller->BeginOnlineFlow(SelectedIndex == 0);
		}
		else if (SelectedIndex == 2)
		{
			Controller->BeginOnlineRename();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::OnlineName:
		if (SelectedIndex == 0) { SubmitName(); } else { GoBack(); }
		break;
	case EChaosImpactScreen::OnlinePassword:
		if (SelectedIndex == 0) { SubmitPassword(); } else { GoBack(); }
		break;
	case EChaosImpactScreen::OnlineRoomName:
		if (SelectedIndex == 0) { SubmitRoomNameEntry(); } else { GoBack(); }
		break;
	case EChaosImpactScreen::RoomList:
		if (Entries[SelectedIndex].Detail == TEXT("refresh"))
		{
			Controller->RefreshRoomList();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("back"))
		{
			GoBack();
		}
		else if (!Entries[SelectedIndex].bDisabled)
		{
			Controller->JoinRoomListing(SelectedIndex);
		}
		break;
	case EChaosImpactScreen::OnlineStatus:
		Controller->CancelOnlineStatus();
		break;
	case EChaosImpactScreen::MatchRules:
		if (SelectedIndex <= 2)
		{
			Controller->AdjustMatchRule(SelectedIndex, 1);
			BuildEntries();
		}
		else if (SelectedIndex == 3)
		{
			Controller->ConfirmMatchRules();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::TeamSelect:
		if (SelectedIndex == 0)
		{
			Controller->RequestStartTeamMatch();
		}
		else if (SelectedIndex == 1)
		{
			Controller->OpenMatchRules(EChaosImpactScreen::Playing);
		}
		break;
	case EChaosImpactScreen::MatchEnd:
		if (SelectedIndex == 0)
		{
			Controller->RetryVersusMatch();
		}
		else if (SelectedIndex == 1)
		{
			Controller->OpenMatchRules(EChaosImpactScreen::MatchEnd);
		}
		else
		{
			Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
		}
		break;
	case EChaosImpactScreen::Pause:
		// Gameplay keeps running behind the online pause menu, so a throw click or dash press
		// right as it opens must not activate anything.
		if ((Controller->IsOnlineRoom() || Controller->IsSearchingForRoom())
			&& FPlatformTime::Seconds() - ScreenStartedAt < 0.25)
		{
			break;
		}
		if ((Entries[SelectedIndex].Detail == TEXT("leave") || Entries[SelectedIndex].Detail == TEXT("stop_search"))
			&& ArmedIndex != SelectedIndex)
		{
			ArmedIndex = SelectedIndex;
			BuildEntries();
			break;
		}
		if (Entries[SelectedIndex].Detail == TEXT("rumble"))
		{
			Controller->ToggleRumbleEnabled();
			const int32 KeepSelection = SelectedIndex;
			BuildEntries();
			SelectedIndex = KeepSelection;
		}
		else if (Entries[SelectedIndex].Detail == TEXT("resume"))
		{
			Controller->ResumeGameplay();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("start_match"))
		{
			Controller->OpenMatchRules(EChaosImpactScreen::Playing);
		}
		else if (Entries[SelectedIndex].Detail == TEXT("close_recruit"))
		{
			Controller->CloseRecruitment();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("reopen_recruit"))
		{
			Controller->ReopenRecruitment();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("rename_room"))
		{
			Controller->BeginRoomRename();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("leave"))
		{
			Controller->LeaveOnlineRoom();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("stop_search"))
		{
			Controller->StopRoomSearch();
			Controller->ResumeGameplay();
		}
		else if (SelectedIndex == 0)
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
			const int32 SettingsIndex = Controller->ShowsTrainingPauseEntries() ? 2 : INDEX_NONE;
			const int32 RetryIndex = Controller->ShowsTrainingPauseEntries() ? 3 : INDEX_NONE;
			const int32 ModeIndex = Controller->ShowsTrainingPauseEntries() ? 4 : 2;
			if (SelectedIndex == SettingsIndex)
			{
				Controller->ResumeGameplay();
				Controller->ToggleTrainingOverlay();
			}
			else if (SelectedIndex == RetryIndex)
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
	case EChaosImpactScreen::TrainingSettings:
		if (SelectedIndex == 0)
		{
			Controller->CycleTrainingPlayerCount();
			BuildEntries();
		}
		else if (SelectedIndex == 1)
		{
			Controller->TogglePrimaryInputMode();
			BuildEntries();
		}
		else if (SelectedIndex == 2)
		{
			Controller->ToggleTrainingTargets();
			BuildEntries();
		}
		else if (SelectedIndex == 3)
		{
			Controller->ToggleTrainingCPU();
			BuildEntries();
		}
		else if (SelectedIndex == 4)
		{
			Controller->ApplyTrainingSettings();
		}
		else
		{
			Controller->ShowMenuScreen(EChaosImpactScreen::Pause);
		}
		break;
	case EChaosImpactScreen::TrainingOverlay:
		if (SelectedIndex == 0)
		{
			Controller->ToggleBallFlightMode();
			BuildEntries();
		}
		else if (SelectedIndex == 1)
		{
			Controller->CycleTrainingPlayerCount();
			BuildEntries();
		}
		else if (SelectedIndex == 2)
		{
			Controller->ToggleTrainingTargets();
			BuildEntries();
		}
		else if (SelectedIndex == 3)
		{
			Controller->ToggleTrainingCPU();
			BuildEntries();
		}
		else if (SelectedIndex == 4)
		{
			Controller->CycleTrainingSummonBallType();
			BuildEntries();
		}
		else if (SelectedIndex == 5)
		{
			Controller->SummonTrainingBall();
		}
		else if (SelectedIndex == 6)
		{
			Controller->RetryTraining();
		}
		else
		{
			Controller->CloseTrainingOverlay();
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
		case EChaosImpactScreen::TrainingSettings:
			Controller->ShowMenuScreen(EChaosImpactScreen::Pause);
			break;
		case EChaosImpactScreen::TrainingOverlay:
			Controller->CloseTrainingOverlay();
			break;
		case EChaosImpactScreen::ModeSelect:
			Controller->ShowMenuScreen(EChaosImpactScreen::Title);
			break;
		case EChaosImpactScreen::SoloReady:
		case EChaosImpactScreen::VSSelect:
			Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
			break;
		case EChaosImpactScreen::TrainingSetup:
			Controller->ShowMenuScreen(Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusLocal
				? EChaosImpactScreen::VSSelect : EChaosImpactScreen::ModeSelect);
			break;
		case EChaosImpactScreen::OnlinePlayers:
			Controller->ShowMenuScreen(EChaosImpactScreen::VSSelect);
			break;
		case EChaosImpactScreen::MultiReady:
			Controller->ShowMenuScreen(EChaosImpactScreen::OnlinePlayers);
			break;
		case EChaosImpactScreen::ControllerAssignment:
			Controller->ShowMenuScreen(Controller->GetControllerAssignmentReturnScreen());
			break;
		case EChaosImpactScreen::OnlineName:
		case EChaosImpactScreen::OnlinePassword:
			Controller->ShowMenuScreen(EChaosImpactScreen::MultiReady);
			break;
		case EChaosImpactScreen::OnlineRoomName:
			if (Controller->IsRenamingRoom())
			{
				Controller->CancelRoomRename();
			}
			else
			{
				Controller->ShowMenuScreen(EChaosImpactScreen::OnlinePassword);
			}
			break;
		case EChaosImpactScreen::RoomList:
			Controller->CloseRoomList();
			break;
		case EChaosImpactScreen::OnlineStatus:
			Controller->CancelOnlineStatus();
			break;
		case EChaosImpactScreen::MatchRules:
			Controller->CancelMatchRules();
			break;
		default:
			break;
		}
	}
}

FReply UChaosImpactMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Screen == EChaosImpactScreen::ControllerAssignment && !Key.IsGamepadKey()
		&& !InKeyEvent.IsRepeat())
	{
		if (AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			const bool bWasJoined = Controller->IsKeyboardMouseJoined();
			if (Controller->RegisterKeyboardMouseJoin() && !bWasJoined)
			{
				// The first keyboard press only claims the next open player slot.
				return FReply::Handled();
			}
		}
	}
	if (Screen == EChaosImpactScreen::ControllerAssignment && Key.IsGamepadKey()
		&& !InKeyEvent.IsRepeat())
	{
		if (AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			const int32 InputDeviceId = InKeyEvent.GetInputDeviceId().GetId();
			const bool bWasJoined = Controller->IsControllerJoined(InputDeviceId);
			if (Controller->RegisterControllerJoin(
				InputDeviceId, static_cast<int32>(InKeyEvent.GetUserIndex()))
				&& !bWasJoined)
			{
				// The press that joins a controller must not also activate the
				// currently selected Start/Back entry. Once everyone is READY,
				// subsequent presses from P1's assigned pad control the menu normally.
				return FReply::Handled();
			}
		}
	}
	if (!IsMenuKeyAllowed(this, Key))
	{
		return FReply::Handled();
	}
	if (HandlePasswordKey(Key))
	{
		return FReply::Handled();
	}
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
			|| ((Key == EKeys::T || Key == EKeys::Hyphen || Key == EKeys::Gamepad_Special_Left)
				&& Screen == EChaosImpactScreen::TrainingOverlay)
			|| ((Key == EKeys::P || Key == EKeys::Gamepad_Special_Right)
				&& (Screen == EChaosImpactScreen::Pause || Screen == EChaosImpactScreen::TrainingSettings)))
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
	// The name field would otherwise swallow Escape; B also leaves while it has focus.
	if (Screen == EChaosImpactScreen::OnlineName && !InKeyEvent.IsRepeat()
		&& (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right))
	{
		GoBack();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UChaosImpactMenuWidget::NativeOnAnalogValueChanged(
	const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	if (!IsMenuKeyAllowed(this, Key))
	{
		return FReply::Handled();
	}
	if (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY)
	{
		return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
	}

	const float Value = InAnalogEvent.GetAnalogValue();
	const double Now = FPlatformTime::Seconds();
	if (FMath::Abs(Value) >= 0.62f && Now - LastAnalogNavigationAt >= 0.20)
	{
		if (Key == EKeys::Gamepad_LeftX)
		{
			Navigate(Value > 0.0f ? EKeys::Gamepad_DPad_Right : EKeys::Gamepad_DPad_Left);
		}
		else
		{
			Navigate(Value > 0.0f ? EKeys::Gamepad_DPad_Up : EKeys::Gamepad_DPad_Down);
		}
		LastAnalogNavigationAt = Now;
	}
	return FReply::Handled();
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
	if (!IsMenuMouseAllowed(this))
	{
		return FReply::Handled();
	}
	if (!InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		const int32 Hovered = HitTestEntry(InGeometry, InMouseEvent.GetScreenSpacePosition());
		if (Hovered != INDEX_NONE)
		{
			SelectedIndex = Hovered;
			DisarmSelection();
		}
	}
	return FReply::Handled();
}

FReply UChaosImpactMenuWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (Screen == EChaosImpactScreen::ControllerAssignment)
	{
		if (AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			const bool bWasJoined = Controller->IsKeyboardMouseJoined();
			if (Controller->RegisterKeyboardMouseJoin() && !bWasJoined)
			{
				// Mouse and keyboard are one shared device; the first click claims it.
				PressedIndex = INDEX_NONE;
				return FReply::Handled();
			}
		}
	}
	if (!IsMenuMouseAllowed(this))
	{
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::OnlinePassword && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Point = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		for (int32 Digit = 0; Digit < 4; ++Digit)
		{
			const FSlateRect Rect = PasswordDigitRect(Digit);
			if (Rect.ContainsPoint(Point))
			{
				PasswordCursor = Digit;
				const bool bUpper = Point.Y < (Rect.Top + Rect.Bottom) * 0.5f;
				PasswordDigits[Digit] = (PasswordDigits[Digit] + (bUpper ? 1 : 9)) % 10;
				return FReply::Handled();
			}
		}
	}
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
	if (!IsMenuMouseAllowed(this))
	{
		PressedIndex = INDEX_NONE;
		return FReply::Handled().ReleaseMouseCapture();
	}
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
