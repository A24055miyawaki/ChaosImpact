#include "ChaosImpactMenuWidget.h"
#include "Engine/GameViewportClient.h"
#include "ChaosImpactPaint.h"

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactTitleDemo.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactResults.h"
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
#include "ChaosImpactSoloStages.h"
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
			return Menu && Menu->HandleAnyUserKeyDown(InKeyEvent);
		}

		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			UChaosImpactMenuWidget* Menu = Widget.Get();
			return Menu && Menu->HandleAnyUserAnalog(InAnalogInputEvent);
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
		// The full-screen pause belongs to P1, but the mouse may belong to P2-P4. Reduced mouse controls must
		// still be able to close that menu without borrowing P1's controller.
		if (Controller && Widget && (Widget->GetScreen() == EChaosImpactScreen::Pause
			|| Widget->GetScreen() == EChaosImpactScreen::Settings
			|| Widget->GetScreen() == EChaosImpactScreen::TrainingSettings
			|| Widget->GetScreen() == EChaosImpactScreen::TrainingOverlay))
		{
			if (const UGameInstance* Instance = Controller->GetGameInstance())
			{
				for (const ULocalPlayer* Local : Instance->GetLocalPlayers())
				{
					const AChaosImpactPlayerController* MousePlayer = Local ? Cast<AChaosImpactPlayerController>(Local->GetPlayerController(Controller->GetWorld())) : nullptr;
					if (MousePlayer && !MousePlayer->IsUsingGamepad()
						&& ChaosImpactSettings::GetPlayerControlScheme(MousePlayer) != EChaosImpactControlScheme::Normal) { return true; }
				}
			}
		}
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

	/** Tips shown one after another while waiting online (a few seconds each). */
	const TCHAR* OnlineWaitTip(const float Time)
	{
		static const TCHAR* Tips[] = {
			TEXT("ためて投げると、速くて重い一球になる"),
			TEXT("ダッシュ中はボールをすり抜けられる"),
			TEXT("ボール化すると、味方に投げてもらえる"),
			TEXT("同じあいことばの人だけが、同じ部屋に入れる"),
			TEXT("ロビーではボールで遊びながら待てる"),
			TEXT("特殊ボールは、拾うまで何が出るかわからない"),
		};
		return Tips[FMath::FloorToInt(Time / 4.0f) % UE_ARRAY_COUNT(Tips)];
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

	/**
	 * Rules screen, クラッシュボール: a card beside the rows with the field in miniature (red goal, the giant ball, blue
	 * goal, each team's throws pushing it the other way) and the rule in four short points.
	 */
	void PaintCrashRuleGuide(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time)
	{
		constexpr float W = 390.0f;
		constexpr float H = 484.0f;
		const float In = EaseOut((Time - 0.3f) / 0.35f);
		const FGeometry Card = MakeSkewed(Design, 1190.0f + (1.0f - In) * 90.0f, 206.0f, W, H, -0.06f);
		const FMenuPainter P{Card, Elements, Layer, In};
		const FLinearColor Red = ChaosImpactMatch::GetTeamColor(0);
		const FLinearColor Blue = ChaosImpactMatch::GetTeamColor(1);
		P.Box(10.0f, 12.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		P.Box(0.0f, 0.0f, W, H, FLinearColor(0.012f, 0.016f, 0.03f, 0.94f));
		P.Box(0.0f, 0.0f, W, 8.0f, Violet);
		P.Text(TEXT("クラッシュボール"), 26.0f, 18.0f, 28.0f, Paper);
		P.Text(TEXT("2チーム ・ 5分"), 28.0f, 60.0f, 16.0f, Violet, ETextAlign::Left, TEXT("Bold"));

		// The field: red goal left, blue goal right, the giant ball rocking between them as throws land.
		const FMenuPainter F{Card, Elements, Layer + 1, In};
		constexpr float FX = 24.0f;
		constexpr float FY = 90.0f;
		constexpr float FW = W - 48.0f;
		constexpr float FH = 120.0f;
		F.Box(FX, FY, FW, FH, FLinearColor(0.035f, 0.05f, 0.09f, 1.0f));
		F.Box(FX + FW * 0.5f - 1.0f, FY + 6.0f, 2.0f, FH - 12.0f, WithAlpha(Paper, 0.18f));
		F.Box(FX, FY + 4.0f, FW * 0.5f, 3.0f, WithAlpha(Red, 0.35f));
		F.Box(FX + FW * 0.5f, FY + 4.0f, FW * 0.5f, 3.0f, WithAlpha(Blue, 0.35f));
		const auto Goal = [&](const float X, const FLinearColor& Color, const float Facing)
		{
			F.Box(X - 12.0f, FY + FH * 0.5f - 30.0f, 24.0f, 60.0f, WithAlpha(Color, 0.25f));
			F.Line(FVector2D(X + Facing * 12.0f, FY + FH * 0.5f - 30.0f), FVector2D(X - Facing * 12.0f, FY + FH * 0.5f - 30.0f), Color, 4.0f);
			F.Line(FVector2D(X - Facing * 12.0f, FY + FH * 0.5f - 30.0f), FVector2D(X - Facing * 12.0f, FY + FH * 0.5f + 30.0f), Color, 4.0f);
			F.Line(FVector2D(X - Facing * 12.0f, FY + FH * 0.5f + 30.0f), FVector2D(X + Facing * 12.0f, FY + FH * 0.5f + 30.0f), Color, 4.0f);
		};
		Goal(FX + 18.0f, Red, 1.0f);
		Goal(FX + FW - 18.0f, Blue, -1.0f);
		const float Sway = FMath::Sin(Time * 1.3f) * 34.0f;
		const FVector2D Ball(FX + FW * 0.5f + Sway, FY + FH * 0.5f);
		// Each team's small balls fly in from its own side and knock the giant ball towards the other goal.
		for (int32 Team = 0; Team < 2; ++Team)
		{
			const float Phase = FMath::Frac(Time * 0.9f + Team * 0.5f);
			const float Side = Team == 0 ? -1.0f : 1.0f;
			const FVector2D From(Ball.X + Side * 120.0f, Ball.Y + (Team == 0 ? 26.0f : -26.0f));
			const FVector2D Shot = FMath::Lerp(From, Ball + FVector2D(Side * 26.0f, 0.0f), Phase);
			const FLinearColor Color = Team == 0 ? Red : Blue;
			F.Ring(Shot, 3.5f, WithAlpha(Color, FMath::Sin(Phase * UE_PI)), 7.0f, 16);
			// The way this team pushes.
			const float ArrowX = Ball.X - Side * 44.0f;
			const float Tip = ArrowX - Side * 26.0f;
			F.Line(FVector2D(ArrowX, Ball.Y + (Team == 0 ? -38.0f : 38.0f)), FVector2D(Tip, Ball.Y + (Team == 0 ? -38.0f : 38.0f)), Color, 4.0f);
			F.Line(FVector2D(Tip, Ball.Y + (Team == 0 ? -38.0f : 38.0f)), FVector2D(Tip + Side * 9.0f, Ball.Y + (Team == 0 ? -46.0f : 30.0f)), Color, 4.0f);
			F.Line(FVector2D(Tip, Ball.Y + (Team == 0 ? -38.0f : 38.0f)), FVector2D(Tip + Side * 9.0f, Ball.Y + (Team == 0 ? -30.0f : 46.0f)), Color, 4.0f);
		}
		F.Ring(Ball, 12.0f, Gold, 24.0f, 40);
		F.Ring(Ball, 24.0f, Ink, 3.0f, 40);
		F.Ring(Ball + FVector2D(-7.0f, -7.0f), 3.0f, WithAlpha(Paper, 0.8f), 6.0f, 12);

		// The rule in four points.
		struct FPoint { const TCHAR* Head; const TCHAR* Body; FLinearColor Color; };
		const FPoint Points[] = {
			{TEXT("巨大球を押す"), TEXT("ボールを当てて押す。溜めるほど強い"), Gold},
			{TEXT("ゴールでKO勝ち"), TEXT("相手ゴールに入れたら即勝利"), Violet},
			{TEXT("カウント"), TEXT("100から減る。少ない方が勝ち"), Ice},
			{TEXT("延長戦"), TEXT("負けている側が攻める間は続く"), Fire},
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
		{
			const float PointIn = EaseOut((Time - 0.45f - Index * 0.07f) / 0.3f);
			const FMenuPainter L{Card, Elements, Layer + 1, In * PointIn};
			const float Y = 230.0f + Index * 62.0f + (1.0f - PointIn) * 14.0f;
			L.Box(24.0f, Y + 4.0f, 30.0f, 30.0f, Points[Index].Color);
			L.Text(FString::FromInt(Index + 1), 39.0f, Y + 6.0f, 18.0f, Ink, ETextAlign::Center);
			L.Text(Points[Index].Head, 66.0f, Y, 21.0f, Paper);
			L.Text(Points[Index].Body, 66.0f, Y + 31.0f, 14.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		}
	}

	/** Deep navy field split by a blue slab on the left and a red slab on the right. */
	void PaintBackdrop(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const float Time, const FSlateBrush* Demo = nullptr, const float DemoAlpha = 0.0f)
	{
		const FMenuPainter Flat{Design, Elements, Layer};
		for (int32 Band = 0; Band < 18; ++Band)
		{
			const float T = Band / 17.0f;
			Flat.Box(-400.0f, Band * 50.0f, 2400.0f, 51.0f, FMath::Lerp(
				FLinearColor(0.022f, 0.03f, 0.06f, 1.0f), FLinearColor(0.003f, 0.004f, 0.01f, 1.0f), T));
		}
		if (Demo && DemoAlpha > 0.0f)
		{
			// The title demo shows through the dark middle only: between the blue and red slabs, inside the top and
			// bottom bars, colourless and dimmed into the backdrop's own navy.
			const FGeometry Middle = MakeSkewed(Design, 291.0f, 54.0f, 1038.0f, 792.0f, -0.36f);
			Elements.PushClip(FSlateClippingZone(Middle));
			FSlateDrawElement::MakeBox(Elements, Layer,
				Design.ToPaintGeometry(FVector2f(1324.0f, 792.0f), FSlateLayoutTransform(FVector2f(148.0f, 54.0f))),
				Demo, ESlateDrawEffect::None, FLinearColor(0.36f, 0.47f, 0.88f, 0.34f * DemoAlpha));
			Elements.PopClip();
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

	/**
	 * Stage select: a card like PaintCard's with an in-game picture of the stage across its top and the stage's
	 * number below. The picture stays upright over the leaning card, lit up when the card is selected.
	 */
	void PaintStageShotCard(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const FSlateBrush& Shot, const FString& Big, const FString& Sub, const FLinearColor& Accent,
		const float Blend, const bool bPressed, const float Alpha, const float Time)
	{
		// The card itself: background, streaks, accent bars and the selection outline, without its text.
		PaintCard(Design, Elements, Layer, Rect, FString(), FString(), Accent, Blend, bPressed, Alpha, Time, 0.0f, 0);
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = EaseOut(Blend);
		const bool bCompact = H < 320.0f;
		const FGeometry Card = MakeSkewed(Design, Rect.Left, Rect.Top - 12.0f * B, W, H, -0.14f,
			(1.0f + 0.025f * B) * (bPressed ? 0.97f : 1.0f));
		const FMenuPainter Words{Card, Elements, Layer + 2, Alpha};
		Words.Text(Big, bCompact ? 28.0f : 40.0f, bCompact ? H - 150.0f : H - 184.0f, bCompact ? 40.0f : 60.0f,
			WithAlpha(Paper, 0.7f + 0.3f * B), ETextAlign::Left, TEXT("Black"), 3.0f * B, Ink);
		Words.Text(Sub, bCompact ? 32.0f : 46.0f, bCompact ? H - 74.0f : H - 96.0f, bCompact ? 26.0f : 30.0f,
			WithAlpha(Paper, 0.55f + 0.45f * B));

		// 16:9 preview: across the top on tall cards, or on the right half on compact two-row cards.
		const float ShotW = bCompact ? 224.0f : W - 80.0f;
		const float ShotH = ShotW * 9.0f / 16.0f;
		const float LocalTop = bCompact ? (H - ShotH) * 0.5f : 26.0f;
		const float ShotY = Rect.Top + LocalTop - 12.0f * B;
		const float Lean = 0.14f * (H * 0.5f - (LocalTop + ShotH * 0.5f));
		const float ShotX = (bCompact ? Rect.Right - ShotW - 24.0f : Rect.Left + 40.0f) + Lean;
		const FMenuPainter Frame{Design, Elements, Layer + 1, Alpha};
		Frame.Box(ShotX + 8.0f, ShotY + 10.0f, ShotW, ShotH, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		const float Light = 0.5f + 0.5f * B;
		FSlateDrawElement::MakeBox(Elements, Layer + 1,
			Design.ToPaintGeometry(FVector2f(ShotW, ShotH), FSlateLayoutTransform(FVector2f(ShotX, ShotY))),
			&Shot, ESlateDrawEffect::None, FLinearColor(Light, Light, Light, Alpha));
		const FMenuPainter Edge{Design, Elements, Layer + 2, Alpha};
		Edge.Outline(ShotX, ShotY, ShotW, ShotH, WithAlpha(B > 0.01f ? Paper : Accent, 0.5f + 0.5f * B), 3.0f);
	}

	/**
	 * Stage select: a small top-down sketch of the stage over the upper part of its card (see PaintCard).
	 * Used while the stage has no picture in Content/UI/StageSelect.
	 * 0 is the square arena (AChaosImpactVersusStage), 1 the terraced harbour (AChaosImpactSplashStage, layout units).
	 */
	void PaintStageMap(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const int32 StageIndex, const float Blend, const float Alpha)
	{
		const float B = EaseOut(Blend);
		const float CardH = Rect.Bottom - Rect.Top;
		const bool bCompact = CardH < 320.0f;
		const float Half = bCompact ? 82.0f : 112.0f;
		// Follows the card's lean at this height and its lift when selected.
		const FVector2D Center(bCompact ? Rect.Right - 114.0f : (Rect.Left + Rect.Right) * 0.5f + 12.0f,
			bCompact ? (Rect.Top + Rect.Bottom) * 0.5f - 12.0f * B : Rect.Top + 150.0f - 12.0f * B);
		const FMenuPainter P{Design, Elements, Layer, Alpha};
		const float MaxExtent = (StageIndex == 1 || StageIndex == 3) ? 3400.0f
			: (StageIndex == 2 || StageIndex == 5) ? 3500.0f
			: StageIndex == 4 ? 3300.0f : 3000.0f;
		const float S = Half / MaxExtent;
		// World X is up the map and world Y to the right.
		const auto ToMap = [&Center, S](const float X, const float Y) { return Center + FVector2D(Y, -X) * S; };
		const auto Block = [&P, &ToMap, S](const float X, const float Y, const float SizeX, const float SizeY, const FLinearColor& Color)
		{
			const FVector2D TopLeft = ToMap(X + SizeX * 0.5f, Y - SizeY * 0.5f);
			P.Box(TopLeft.X, TopLeft.Y, SizeY * S, SizeX * S, Color);
		};
		const FLinearColor Ground(0.1f, 0.13f, 0.21f, 0.96f);
		const FLinearColor Edge = WithAlpha(Paper, 0.55f + 0.4f * B);
		P.Box(Center.X - Half - 16.0f, Center.Y - Half - 16.0f, Half * 2.0f + 32.0f, Half * 2.0f + 32.0f,
			FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));

		if (StageIndex == 2)
		{
			// Sky temple: nine islands on bridges; the altar in the middle, a seal on each side island, a portal gate on
			// each corner island (the two pairs in their colours).
			const auto Disc = [&](const float X, const float Y, const float Radius, const FLinearColor& Color)
			{
				constexpr int32 Strips = 14;
				for (int32 Strip = 0; Strip < Strips; ++Strip)
				{
					const float Middle = -Radius + (Strip + 0.5f) * 2.0f * Radius / Strips;
					Block(X + Middle, Y, 2.0f * Radius / Strips, 2.0f * FMath::Sqrt(FMath::Max(0.0f, Radius * Radius - Middle * Middle)), Color);
				}
			};
			const FVector2D Sides[] = {{2700.0f, 0.0f}, {0.0f, 2700.0f}, {-2700.0f, 0.0f}, {0.0f, -2700.0f}};
			const FVector2D Corners[] = {{2350.0f, 2350.0f}, {-2350.0f, 2350.0f}, {-2350.0f, -2350.0f}, {2350.0f, -2350.0f}};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				P.Line(ToMap(0.0f, 0.0f), ToMap(Sides[Index].X, Sides[Index].Y), Ground, 700.0f * S);
				P.Line(ToMap(Sides[Index].X, Sides[Index].Y), ToMap(Corners[Index].X, Corners[Index].Y), Ground, 420.0f * S);
				P.Line(ToMap(Sides[(Index + 1) % 4].X, Sides[(Index + 1) % 4].Y), ToMap(Corners[Index].X, Corners[Index].Y), Ground, 420.0f * S);
			}
			Disc(0.0f, 0.0f, 1550.0f, Ground);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Disc(Sides[Index].X, Sides[Index].Y, 800.0f, Ground);
				Disc(Corners[Index].X, Corners[Index].Y, 620.0f, Ground);
				Disc(Sides[Index].X, Sides[Index].Y, 380.0f, WithAlpha(Ice, 0.75f));
				const FVector2D Facing = -Corners[Index].GetSafeNormal();
				const FVector2D Gate = Corners[Index] - Facing * 200.0f;
				const FVector2D Across(-Facing.Y, Facing.X);
				P.Line(ToMap(Gate.X - Across.X * 290.0f, Gate.Y - Across.Y * 290.0f), ToMap(Gate.X + Across.X * 290.0f, Gate.Y + Across.Y * 290.0f),
					Index % 2 ? Fire : Ice, 4.0f);
			}
			Disc(0.0f, 0.0f, 700.0f, WithAlpha(Gold, 0.85f));
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const float Angle = FMath::DegreesToRadians(22.5f + Index * 45.0f);
				Block(FMath::Cos(Angle) * 1200.0f, FMath::Sin(Angle) * 1200.0f, 180.0f, 180.0f, Paper);
			}
			return;
		}
		if (StageIndex == 3)
		{
			// Stage 4: Neon Cyber Highway Junction (octagonal asphalt + 4 overpasses + central holo court + 4 pulse gates).
			constexpr float Reach = 3400.0f;
			constexpr float Bevel = 5200.0f;
			for (int32 Strip = 0; Strip < 34; ++Strip)
			{
				const float X = -Reach + 100.0f + Strip * 200.0f;
				Block(X, 0.0f, 200.0f, 2.0f * FMath::Min(Reach, Bevel - FMath::Abs(X)), Ground);
			}
			Block(0.0f, 0.0f, 1520.0f, 1520.0f, WithAlpha(Ice, 0.45f));
			P.Ring(Center, 520.0f * S, Ice, 2.5f, 28);
			P.Ring(Center, 240.0f * S, Gold, 2.5f, 20);
			for (int32 Quarter = 0; Quarter < 4; ++Quarter)
			{
				const FLinearColor Neon = PlayerAccents[Quarter];
				const bool bSide = Quarter % 2 == 1;
				FVector2D Overpass(2950.0f, 0.0f);
				FVector2D Gate(1160.0f, 1160.0f);
				FVector2D Bank(1620.0f, 1620.0f);
				for (int32 Turn = 0; Turn < Quarter; ++Turn)
				{
					Overpass = FVector2D(-Overpass.Y, Overpass.X);
					Gate = FVector2D(-Gate.Y, Gate.X);
					Bank = FVector2D(-Bank.Y, Bank.X);
				}
				Block(Overpass.X, Overpass.Y, bSide ? 2500.0f : 860.0f, bSide ? 860.0f : 2500.0f, WithAlpha(Neon, 0.7f));
				const FVector2D Tangent = FVector2D(-Gate.Y, Gate.X).GetSafeNormal() * 300.0f;
				P.Line(ToMap(Gate.X - Tangent.X, Gate.Y - Tangent.Y), ToMap(Gate.X + Tangent.X, Gate.Y + Tangent.Y),
					Quarter % 2 == 0 ? Ice : Gold, 3.5f);
				P.Line(ToMap(Bank.X - Tangent.X, Bank.Y - Tangent.Y), ToMap(Bank.X + Tangent.X, Bank.Y + Tangent.Y), Paper, 3.0f);
			}
			return;
		}
		if (StageIndex == 4)
		{
			// Stage 5: Frozen Yukimi Sanctuary & Ice Pond (outer wooden veranda + central ice pond + 4 Taiko bridges).
			constexpr float Reach = 3300.0f;
			constexpr float Bevel = 5000.0f;
			for (int32 Strip = 0; Strip < 33; ++Strip)
			{
				const float X = -Reach + 100.0f + Strip * 200.0f;
				Block(X, 0.0f, 200.0f, 2.0f * FMath::Min(Reach, Bevel - FMath::Abs(X)), FLinearColor(0.26f, 0.17f, 0.12f, 0.96f));
			}
			Block(0.0f, 0.0f, 3500.0f, 3500.0f, FLinearColor(0.28f, 0.62f, 0.88f, 0.88f));
			P.Ring(Center, 470.0f * S, Paper, 3.0f, 24);
			P.Ring(Center, 220.0f * S, Gold, 2.5f, 20);
			for (int32 Quarter = 0; Quarter < 4; ++Quarter)
			{
				const bool bSide = Quarter % 2 == 1;
				FVector2D Bridge(1560.0f, 0.0f);
				FVector2D Pillar(1020.0f, 1020.0f);
				for (int32 Turn = 0; Turn < Quarter; ++Turn)
				{
					Bridge = FVector2D(-Bridge.Y, Bridge.X);
					Pillar = FVector2D(-Pillar.Y, Pillar.X);
				}
				Block(Bridge.X, Bridge.Y, bSide ? 460.0f : 620.0f, bSide ? 620.0f : 460.0f, Fire);
				Block(Pillar.X, Pillar.Y, 220.0f, 220.0f, Paper);
			}
			return;
		}
		if (StageIndex == 5)
		{
			// Stage 6: Molten Core Foundry (central circular core + 4 thermal vents + 4 bays + 4 corner chambers).
			const auto Disc = [&](const float X, const float Y, const float Radius, const FLinearColor& Color)
			{
				constexpr int32 Strips = 12;
				for (int32 Strip = 0; Strip < Strips; ++Strip)
				{
					const float Middle = -Radius + (Strip + 0.5f) * 2.0f * Radius / Strips;
					Block(X + Middle, Y, 2.0f * Radius / Strips, 2.0f * FMath::Sqrt(FMath::Max(0.0f, Radius * Radius - Middle * Middle)), Color);
				}
			};
			const FVector2D Bays[] = {{2600.0f, 0.0f}, {0.0f, 2600.0f}, {-2600.0f, 0.0f}, {0.0f, -2600.0f}};
			const FVector2D Corners[] = {{2450.0f, 2450.0f}, {-2450.0f, 2450.0f}, {-2450.0f, -2450.0f}, {2450.0f, -2450.0f}};
			const FVector2D Vents[] = {{920.0f, 0.0f}, {0.0f, 920.0f}, {-920.0f, 0.0f}, {0.0f, -920.0f}};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				P.Line(ToMap(0.0f, 0.0f), ToMap(Bays[Index].X, Bays[Index].Y), Ground, 620.0f * S);
				P.Line(ToMap(0.0f, 0.0f), ToMap(Corners[Index].X, Corners[Index].Y), Ground, 360.0f * S);
				P.Line(ToMap(Bays[Index].X, Bays[Index].Y), ToMap(Corners[Index].X, Corners[Index].Y), Ground, 420.0f * S);
				P.Line(ToMap(Bays[(Index + 1) % 4].X, Bays[(Index + 1) % 4].Y), ToMap(Corners[Index].X, Corners[Index].Y), Ground, 420.0f * S);
			}
			Disc(0.0f, 0.0f, 1520.0f, Ground);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const bool bSide = Index % 2 == 1;
				Block(Bays[Index].X, Bays[Index].Y, bSide ? 1700.0f : 1260.0f, bSide ? 1260.0f : 1700.0f, Ground);
				Block(Bays[Index].X, Bays[Index].Y, bSide ? 540.0f : 230.0f, bSide ? 230.0f : 540.0f, Paper);
				Disc(Corners[Index].X, Corners[Index].Y, 640.0f, Ground);
				Disc(Corners[Index].X, Corners[Index].Y, 300.0f, WithAlpha(Ice, 0.65f));
				Disc(Vents[Index].X, Vents[Index].Y, 330.0f, WithAlpha(Fire, 0.85f));
			}
			Disc(0.0f, 0.0f, 460.0f, WithAlpha(Gold, 0.9f));
			return;
		}
		if (StageIndex != 1)
		{
			// Square arena: a low centre deck, bank walls round it, pillars, L-shelters and low cover.
			Block(0.0f, 0.0f, 6000.0f, 6000.0f, Ground);
			P.Outline(Center.X - Half, Center.Y - Half, Half * 2.0f, Half * 2.0f, Edge, 3.0f);
			Block(0.0f, 0.0f, 1000.0f, 1000.0f, WithAlpha(Gold, 0.85f));
			for (const float Sign : {1.0f, -1.0f})
			{
				Block(Sign * 620.0f, 0.0f, 240.0f, 500.0f, WithAlpha(Gold, 0.5f));
				Block(0.0f, Sign * 620.0f, 500.0f, 240.0f, WithAlpha(Gold, 0.5f));
				Block(Sign * 1750.0f, 0.0f, 240.0f, 240.0f, Paper);
				Block(0.0f, Sign * 1750.0f, 240.0f, 240.0f, Paper);
			}
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const float SX = Side == 0 || Side == 3 ? 1.0f : -1.0f;
				const float SY = Side < 2 ? 1.0f : -1.0f;
				Block(SX * 1830.0f, SY * 1550.0f, 620.0f, 60.0f, Ice);
				Block(SX * 1550.0f, SY * 1830.0f, 60.0f, 620.0f, Ice);
				const FVector2D Along = FVector2D(-SY, SX).GetSafeNormal() * 310.0f;
				P.Line(ToMap(SX * 1080.0f - Along.X, SY * 1080.0f - Along.Y), ToMap(SX * 1080.0f + Along.X, SY * 1080.0f + Along.Y),
					Fire, 3.0f);
			}
			Block(2300.0f, -750.0f, 380.0f, 80.0f, Muted);
			Block(-2300.0f, 750.0f, 380.0f, 80.0f, Muted);
			Block(750.0f, 2300.0f, 80.0f, 380.0f, Muted);
			Block(-750.0f, -2300.0f, 80.0f, 380.0f, Muted);
			return;
		}

		// Terraced harbour: an octagon (|X|, |Y| <= 3400, |X| + |Y| <= 4900), filled in strips.
		constexpr float Reach = 3400.0f;
		constexpr float Bevel = 4900.0f;
		for (int32 Strip = 0; Strip < 34; ++Strip)
		{
			const float X = -Reach + 100.0f + Strip * 200.0f;
			Block(X, 0.0f, 200.0f, 2.0f * FMath::Min(Reach, Bevel - FMath::Abs(X)), Ground);
		}
		const FVector2D Corners[] = {{Reach, Bevel - Reach}, {Bevel - Reach, Reach}, {Reach - Bevel, Reach}, {-Reach, Bevel - Reach},
			{-Reach, Reach - Bevel}, {Reach - Bevel, -Reach}, {Bevel - Reach, -Reach}, {Reach, Reach - Bevel}};
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FVector2D& From = Corners[Index];
			const FVector2D& To = Corners[(Index + 1) % 8];
			P.Line(ToMap(From.X, From.Y), ToMap(To.X, To.Y), Edge, 3.0f);
		}
		// Each quarter: a balcony with a high terrace behind it, a container in front and a corner warehouse.
		for (int32 Quarter = 0; Quarter < 4; ++Quarter)
		{
			const FLinearColor InkColor = PlayerAccents[Quarter];
			FLinearColor Dark = InkColor * 0.45f;
			Dark.A = 1.0f;
			const auto QuarterBlock = [&Block, Quarter](const float X, const float Y, const float SizeX, const float SizeY,
				const FLinearColor& Color)
			{
				FVector2D At(X, Y);
				for (int32 Turn = 0; Turn < Quarter; ++Turn)
				{
					At = FVector2D(-At.Y, At.X);
				}
				const bool bSideways = Quarter % 2 == 1;
				Block(At.X, At.Y, bSideways ? SizeY : SizeX, bSideways ? SizeX : SizeY, Color);
			};
			QuarterBlock(2250.0f, 0.0f, 700.0f, 1900.0f, Dark);
			QuarterBlock(3000.0f, 0.0f, 800.0f, 1300.0f, InkColor);
			QuarterBlock(1500.0f, 0.0f, 240.0f, 560.0f, Paper);
			QuarterBlock(2270.0f, 2270.0f, 420.0f, 420.0f, Muted);
		}
		P.Ring(Center, 430.0f * S, Ice, 3.0f, 32);
		P.Ring(Center, 200.0f * S, WithAlpha(Paper, 0.8f), 2.0f, 24);
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

	CharacterSelect = NewObject<UChaosImpactCharacterSelect>(this);
	SettingsScreen = NewObject<UChaosImpactSettingsScreen>(this);

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

	// A stage without a picture yet keeps the drawn sketch (PaintStageMap).
	StageShots.SetNum(ChaosImpactMatch::StageCount);
	StageShotBrushes.SetNum(ChaosImpactMatch::StageCount);
	for (int32 Index = 0; Index < ChaosImpactMatch::StageCount; ++Index)
	{
		const FString File = FPaths::Combine(FPaths::ProjectContentDir(),
			FString::Printf(TEXT("UI/StageSelect/Stage%d.png"), Index + 1));
		StageShots[Index] = FPaths::FileExists(File) ? FImageUtils::ImportFileAsTexture2D(File) : nullptr;
		if (StageShots[Index])
		{
			StageShotBrushes[Index].SetResourceObject(StageShots[Index]);
			StageShotBrushes[Index].ImageSize = FVector2D(StageShots[Index]->GetSizeX(), StageShots[Index]->GetSizeY());
			StageShotBrushes[Index].DrawAs = ESlateBrushDrawType::Image;
		}
	}
}

void UChaosImpactMenuWidget::NativeDestruct()
{
	if (UGameViewportClient* Viewport = bLevelHiddenByMenu && GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->bDisableWorldRendering = false;
		bLevelHiddenByMenu = false;
	}
	if (CharacterSelect)
	{
		CharacterSelect->Close();
	}
	if (SettingsScreen)
	{
		SettingsScreen->Close();
	}
	if (JoinInputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(JoinInputProcessor);
	}
	JoinInputProcessor.Reset();
	Super::NativeDestruct();
}

bool UChaosImpactMenuWidget::HandleAnyUserKeyDown(const FKeyEvent& InKeyEvent)
{
	// F11 / Alt+Enter in menus and in play alike (this sees every key first), kept the same as the settings screen.
	if (!GIsEditor
		&& (InKeyEvent.GetKey() == EKeys::F11 || (InKeyEvent.GetKey() == EKeys::Enter && InKeyEvent.IsAltDown())))
	{
		if (!InKeyEvent.IsRepeat())
		{
			ChaosImpactSettings::ToggleFullscreen();
		}
		// Consume repeats too: never let the engine's own fullscreen shortcut run after ours.
		return true;
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return CharacterSelect->HandleKeyDown(InKeyEvent);
	}
	if (IsSettingsActive())
	{
		return SettingsScreen->HandleKeyDown(InKeyEvent);
	}
	if (Screen == EChaosImpactScreen::MatchEnd && Entries.IsEmpty() && GetVisibility() != ESlateVisibility::Collapsed)
	{
		if (!InKeyEvent.IsRepeat())
		{
			GoOnFromPodium();
		}
		return true;
	}
	return TryJoinControllerFromAnyUser(InKeyEvent);
}

void UChaosImpactMenuWidget::GoOnFromPodium()
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	UChaosImpactResultsView* Results = Controller ? Controller->GetResultsView() : nullptr;
	if (Results && Results->GetPage() == 0 && Results->CanAdvance())
	{
		Results->ShowStats();
		// The menu comes in fresh under the numbers.
		ShowScreen(EChaosImpactScreen::MatchEnd);
	}
}

bool UChaosImpactMenuWidget::IsSettingsActive() const
{
	return Screen == EChaosImpactScreen::Settings && SettingsScreen && SettingsScreen->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed;
}

bool UChaosImpactMenuWidget::HandleAnyUserAnalog(const FAnalogInputEvent& InAnalogEvent)
{
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return CharacterSelect->HandleAnalog(InAnalogEvent);
	}
	if (IsSettingsActive())
	{
		return SettingsScreen->HandleAnalog(InAnalogEvent);
	}
	return false;
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
	const EChaosImpactScreen PreviousScreen = Screen;
	if (CharacterSelect && NewScreen != EChaosImpactScreen::CharacterSelect)
	{
		CharacterSelect->Close();
	}
	// Leaving the select screen: the level is drawn again at once (this widget may stop ticking when hidden).
	if (UGameViewportClient* Viewport = bLevelHiddenByMenu && NewScreen != EChaosImpactScreen::CharacterSelect && GetWorld()
		? GetWorld()->GetGameViewport() : nullptr)
	{
		Viewport->bDisableWorldRendering = false;
		bLevelHiddenByMenu = false;
	}
	if (SettingsScreen && NewScreen != EChaosImpactScreen::Settings)
	{
		SettingsScreen->Close();
	}
	Screen = NewScreen;
	bTrainingBallPickerOpen = false;
	bTrainingTeamsOpen = false;
	if (Screen == EChaosImpactScreen::Pause && PreviousScreen != EChaosImpactScreen::Pause)
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiOpen);
	}
	if (Screen == EChaosImpactScreen::Settings && SettingsScreen && !SettingsScreen->IsOpen())
	{
		SettingsScreen->Open(Cast<AChaosImpactPlayerController>(GetOwningPlayer()));
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect
		&& (PreviousScreen != EChaosImpactScreen::CharacterSelect || !CharacterSelect->IsOpen()))
	{
		CharacterSelect->Open(Cast<AChaosImpactPlayerController>(GetOwningPlayer()));
	}
	SelectedIndex = 0;
	PressedIndex = INDEX_NONE;
	ArmedIndex = INDEX_NONE;
	ScreenStartedAt = FPlatformTime::Seconds();
	AnimationSeconds = 0.0f;
	BuildEntries();
	SelectBlend.Init(0.0f, Entries.Num());

	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (Screen == EChaosImpactScreen::StageSelect && Controller)
	{
		// The stage chosen last time is picked again by just pressing 決定.
		SelectedIndex = ChaosImpactMatch::SanitizeStage(Controller->GetPendingMatchRules().StageIndex);
	}
	else if (Screen == EChaosImpactScreen::MatchRules && PreviousScreen == EChaosImpactScreen::StageSelect)
	{
		// Back from stage select lands on 決定 again.
		SelectedIndex = 4;
	}
	else if (Screen == EChaosImpactScreen::MatchRules && Entries.IsValidIndex(0) && Entries[0].bDisabled)
	{
		// クラッシュボール: the fixed time row is passed over, straight to the rule.
		SelectedIndex = 1;
	}
	else if (Screen == EChaosImpactScreen::MatchEnd)
	{
		// Under the numbers the cursor starts on the first choice (もう一度 / ロビーへもどる).
		ResultsRowCursor = 0;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (IsResultsChoice(Index))
			{
				SelectedIndex = Index;
				break;
			}
		}
	}

	// Slots that are already filled when the page opens (the reserved keyboard, or a
	// return visit) still play their join animation, staggered after the page wipe.
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

void UChaosImpactMenuWidget::OpenTrainingBallPicker()
{
	if (Screen != EChaosImpactScreen::TrainingOverlay) { return; }
	bTrainingBallPickerOpen = true;
	BuildEntries();
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	SelectedIndex = Owner ? static_cast<int32>(Owner->GetTrainingSummonBallType()) : 0;
	SelectBlend.Init(0,Entries.Num()); PressedIndex = INDEX_NONE;
}

void UChaosImpactMenuWidget::CloseTrainingBallPicker()
{
	bTrainingBallPickerOpen = false;
	BuildEntries(); SelectedIndex = 4;
	SelectBlend.Init(0,Entries.Num()); PressedIndex = INDEX_NONE;
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
		Entries.Add({FSlateRect(520, 720, 940, 792), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
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
		if (Controller && Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusLocal)
		{
			// Watching a CPU match: one player on one screen, so it is picked here, before players and characters.
			Entries.Add({FSlateRect(510, 712, 890, 784), TEXT("観戦"), TEXT("spectate"), TEXT(""), Ice});
		}
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
		FMenuEntry Start{FSlateRect(1030, 712, 1454, 800), TEXT("キャラ選択へ"), TEXT(""), TEXT(""), Gold};
		(void)bOnlineSetup;
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
		Entries.Add({FSlateRect(150, 236, 770, 652), TEXT("部屋をつくる"), TEXT(""), TEXT("CREATE"), Ice});
		Entries.Add({FSlateRect(830, 236, 1450, 652), TEXT("部屋をさがす"), TEXT(""), TEXT("SEARCH"), Fire});
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
		// Each room is a row: its name (Title), host (Detail) and members (Number).
		for (int32 Index = 0; Index < FMath::Min(Listings.Num(), 5); ++Index)
		{
			const FChaosImpactRoomListing& Listing = Listings[Index];
			const bool bWatch = Sessions->WouldJoinAsSpectator(Listing);
			FMenuEntry Row{FSlateRect(250, 232 + Index * 92, 1350, 312 + Index * 92), Listing.RoomName, Listing.HostName,
				bWatch ? FString::Printf(TEXT("観戦 %d/%d"), Listing.Spectators, AChaosImpactGameState::MaxSpectators)
					: FString::Printf(TEXT("%d/%d"), Listing.Members, AChaosImpactGameState::MaxMembers), bWatch ? Gold : Ice};
			Row.bDisabled = !Sessions->GetJoinBlocker(Listing).IsEmpty();
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
	case EChaosImpactScreen::OnlineConnecting:
	{
		const auto* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		if (Controller && !Controller->IsCheckingOnlineConnection())
		{
			Entries.Add({FSlateRect(400, 714, 780, 798), TEXT("もう一度接続する"), TEXT(""), TEXT(""), Ice});
			Entries.Add({FSlateRect(820, 714, 1200, 798), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		}
		else { Entries.Add({FSlateRect(560, 714, 1040, 798), TEXT("戻る"), TEXT(""), TEXT(""), Muted}); }
		break;
	}

	case EChaosImpactScreen::SoloStageSelect:
		// ステージ1、ステージ2の選択用カードと、戻るボタン
		Entries.Add({ FSlateRect(190, 198, 770, 702), TEXT("ステージ 1"), TEXT(""), TEXT("STAGE 1"), Gold });
		Entries.Add({ FSlateRect(830, 198, 1410, 702), TEXT("ステージ 2"), TEXT(""), TEXT("STAGE 2"), Ice });
		Entries.Add({ FSlateRect(146, 736, 470, 800), TEXT("モード選択へ"), TEXT(""), TEXT(""), Muted });
		break;

	case EChaosImpactScreen::Pause:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		if (Controller && Controller->IsOnlineRoom())
		{
			float Y = 285.0f;
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice});
			if (Controller->CanChangeLoadoutInRoom())
			{
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("キャラを変える"), TEXT("change_character"), TEXT(""), Ice});
				Y += 80.0f;
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("名前を変える"), TEXT("change_name"), TEXT(""), Ice});
			}
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
				Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("部屋の名前を変える"), TEXT("rename_room"),
					TEXT(""), Ice});
			}
			Y += 80.0f;
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
			Y += 80.0f;
			const bool bLeaveArmed = ArmedIndex == Entries.Num();
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62),
				bLeaveArmed ? TEXT("もう一度おすと決定")
					: Controller->IsOnlineRoomHost() ? TEXT("部屋を解散する") : TEXT("部屋をぬける"),
				TEXT("leave"), TEXT(""), Fire});
			// Many choices (a host in the lobby has up to nine): closer together, all on screen under PAUSE.
			if (Entries.Num() > 6)
			{
				const float Step = FMath::Min(80.0f, 600.0f / (Entries.Num() - 1));
				for (int32 Index = 0; Index < Entries.Num(); ++Index)
				{
					const float Top = 240.0f + Index * Step;
					Entries[Index].Rect = FSlateRect(490, Top, 1110, Top + FMath::Min(62.0f, Step - 8.0f));
				}
			}
			break;
		}

		// Solo mode's world map.
		if (Controller && Controller->IsSoloWorldMap())
		{
			Entries.Add({ FSlateRect(490, 245, 1110, 307), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 325, 1110, 387), TEXT("ファイル選択へ"), TEXT("goto_solo_title"), TEXT(""), Gold });
			Entries.Add({ FSlateRect(490, 405, 1110, 467), TEXT("モード選択へ"), TEXT("goto_mode"), TEXT(""), Fire });
			Entries.Add({ FSlateRect(490, 485, 1110, 547), TEXT("タイトル画面へ"), TEXT("goto_title"), TEXT(""), Muted });
			Entries.Add({ FSlateRect(490, 565, 1110, 627), TEXT("設定"), TEXT("settings"), TEXT(""), Violet });
#if !UE_BUILD_SHIPPING
			const UChaosImpactSoloProgress* Progress = UChaosImpactSoloProgress::Load();
			Entries.Add({ FSlateRect(490, 665, 1110, 727), Progress && Progress->bUnlockAll ? TEXT("（開発用）全部ひらくをやめる")
				: TEXT("（開発用）マップを全部ひらく"), TEXT("map_unlock"), TEXT(""), Gold });
			Entries.Add({ FSlateRect(490, 745, 1110, 807), TEXT("（開発用）進行をリセット"), TEXT("map_reset"), TEXT(""), Muted });
#endif
			break;
		}

		// ★ソロモードプレイ中のポーズメニュー（新規追加分岐）
		if (Controller && Controller->IsSoloMode())
		{
			const bool bArc = Controller->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
			Entries.Add({ FSlateRect(490, 245, 1110, 307), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 325, 1110, 387),
				bArc ? TEXT("投球軌道：放物線") : TEXT("投球軌道：直線"), TEXT(""), TEXT(""), bArc ? Fire : Ice });
			Entries.Add({ FSlateRect(490, 405, 1110, 467), TEXT("ステージをやり直す"), TEXT("solo_retry"), TEXT(""), Gold });
			Entries.Add({ FSlateRect(490, 485, 1110, 547), TEXT("ワールドマップへ"), TEXT("solo_world_map"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 565, 1110, 627), TEXT("モード選択へ"), TEXT(""), TEXT(""), Fire });
			Entries.Add({ FSlateRect(490, 645, 1110, 707), TEXT("タイトル画面へ"), TEXT(""), TEXT(""), Muted });
			Entries.Add({ FSlateRect(490, 725, 1110, 787), TEXT("設定"), TEXT("settings"), TEXT(""), Violet });
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
		Entries.Add({FSlateRect(490, 725, 1110, 787), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
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
		if (bTrainingTeamsOpen && Controller)
		{
			Entries.Add({FSlateRect(78,174,624,223),Controller->AreTrainingTeamsEnabled() ? TEXT("チーム戦：ON") : TEXT("チーム戦：OFF"),TEXT(""),TEXT(""),Gold});
			for (int32 TeamSlot = 0; TeamSlot < 8; ++TeamSlot)
			{
				const bool bCPU = TeamSlot >= 4;
				const int32 Index = TeamSlot % 4;
				const int32 Team = Controller->GetTrainingTeam(bCPU,Index);
				const float Y = 231+TeamSlot*57;
				const FString Label = bCPU ? FString::Printf(TEXT("CPU%d　＜ チーム%d ＞"),Index+1,Team+1) : FString::Printf(TEXT("%dP　＜ チーム%d ＞"),Index+1,Team+1);
				FMenuEntry Row{FSlateRect(78,Y,624,Y+49),Label,TEXT(""),TEXT(""),ChaosImpactMatch::GetTeamColor(Team)};
				Row.bDisabled = !Controller->AreTrainingTeamsEnabled() || Index >= (bCPU ? Controller->GetTrainingCPUCount() : Controller->GetRequestedLocalPlayerCount());
				Entries.Add(Row);
			}
			Entries.Add({FSlateRect(78,687,624,736),TEXT("戻る"),TEXT(""),TEXT(""),Muted});
			break;
		}
		if (bTrainingBallPickerOpen)
		{
			for (int32 Index = 0; Index < ChaosImpactBallTypes::Count; ++Index)
			{
				const EChaosImpactBallType Type = ChaosImpactBallTypes::FromIndex(Index);
				const float X = 78 + (Index % 3) * 186, Y = 174 + (Index / 3) * 94;
				Entries.Add({FSlateRect(X,Y,X + 174,Y + 84),ChaosImpactBallTypes::GetDisplayName(Type),
					TEXT("ball_type"),FString::FromInt(Index),ChaosImpactBallTypes::GetColor(Type)});
			}
			Entries.Add({FSlateRect(78,684,624,742),TEXT("戻る"),TEXT("ball_back"),TEXT(""),Muted});
			break;
		}
		const int32 Players = Controller ? Controller->GetRequestedLocalPlayerCount() : 1;
		const bool bTargets = !Controller || Controller->AreTrainingTargetsEnabled();
		const int32 CPUCount = Controller ? Controller->GetTrainingCPUCount() : 0;
		const bool bArc = Controller
			&& Controller->GetBallFlightMode() == EChaosImpactBallFlightMode::Arc;
		Entries.Add({FSlateRect(78, 174, 624, 228),
			bArc ? TEXT("投球軌道：放物線") : TEXT("投球軌道：直線"), TEXT(""), TEXT(""), bArc ? Fire : Ice});
		Entries.Add({FSlateRect(78, 238, 624, 292),
			FString::Printf(TEXT("プレイヤー人数：%d人"), Players), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(78, 302, 624, 356),
			bTargets ? TEXT("マト：あり") : TEXT("マト：なし"), TEXT(""), TEXT(""), bTargets ? Gold : Muted});
		Entries.Add({FSlateRect(78, 366, 624, 420),
			FString::Printf(TEXT("CPUプレイヤー：%d体"), CPUCount), TEXT(""), TEXT(""),
			CPUCount > 0 ? Fire : Muted});
		const EChaosImpactBallType SummonType = Controller
				? Controller->GetTrainingSummonBallType() : EChaosImpactBallType::Fire;
			const FLinearColor SummonAccent = SummonType == EChaosImpactBallType::Fire ? Fire
				: SummonType == EChaosImpactBallType::Ice ? Ice
				: SummonType == EChaosImpactBallType::Normal ? Gold : ChaosImpactBallTypes::GetColor(SummonType);
			Entries.Add({FSlateRect(78, 430, 624, 484),
				FString::Printf(TEXT("ボール一覧：%s"), ChaosImpactBallTypes::GetDisplayName(SummonType)),
				TEXT(""), TEXT(""), SummonAccent});
			Entries.Add({FSlateRect(78, 494, 624, 548), TEXT("ボールを呼び出す"), TEXT(""), TEXT(""), SummonAccent});
		Entries.Add({FSlateRect(78, 558, 624, 612), Controller && Controller->IsTrainingShoulderCameraEnabled()
			? TEXT("カメラ：肩越し（試験）") : TEXT("カメラ：見下ろし"), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(78, 622, 624, 676), TEXT("チーム設定"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(78, 686, 624, 740), TEXT("トレーニングをリセット"), TEXT(""), TEXT(""), Fire});
		Entries.Add({FSlateRect(78, 750, 624, 804), TEXT("閉じる"), TEXT(""), TEXT(""), Muted});
		for (int32 I = 0; I < Entries.Num(); ++I) { Entries[I].Rect = FSlateRect(78,174+I*57,624,223+I*57); }
		break;
	}
	case EChaosImpactScreen::MatchRules:
	{
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const FChaosImpactMatchRules Rules = Controller ? Controller->GetPendingMatchRules() : FChaosImpactMatchRules();
		// クラッシュボール always lasts five minutes: the time row stays, greyed out.
		FMenuEntry Minutes{FSlateRect(430, 206, 1170, 270), Rules.bDuel
				? FString::Printf(TEXT("試合時間　1ラウンド%d秒（固定）"), FMath::RoundToInt(ChaosImpactMatch::DuelRoundSeconds))
				: Rules.bCrashBall
				? FString::Printf(TEXT("試合時間　%d分（固定）"), Rules.Minutes)
				: FString::Printf(TEXT("試合時間　＜  %d分  ＞"), Rules.Minutes),
			TEXT(""), TEXT(""), Ice};
		Minutes.bDisabled = Rules.bCrashBall || Rules.bDuel;
		Entries.Add(Minutes);
		Entries.Add({FSlateRect(430, 282, 1170, 346),
			FString::Printf(TEXT("ルール　＜  %s  ＞"), *ChaosImpactMatch::DescribeRule(Rules)),
			TEXT(""), TEXT(""), Rules.bDuel ? Ice : Rules.bCrashBall ? Violet : Rules.IsTeamBattle() ? Gold : Fire});
		// The duel: one player faces a CPU, two players face each other (fixed).
		FMenuEntry CPUs{FSlateRect(430, 358, 1170, 422), Rules.bDuel
				? FString(Rules.CPUCount > 0 ? TEXT("相手　CPU（固定）") : TEXT("相手　プレイヤー同士（固定）"))
				: FString::Printf(TEXT("CPU　＜  %d人  ＞"), Rules.CPUCount),
			TEXT(""), TEXT(""), Rules.CPUCount > 0 ? Fire : Muted};
		CPUs.bDisabled = Rules.bDuel;
		Entries.Add(CPUs);
		// Only with CPUs in the match; otherwise shown greyed out and skipped by the cursor.
		FMenuEntry Strength{FSlateRect(430, 434, 1170, 498), Rules.CPUCount > 0
				? FString::Printf(TEXT("CPUの強さ　＜  %s  ＞"), ChaosImpactMatch::GetCPULevelName(Rules.CPULevel))
				: FString(TEXT("CPUの強さ　（CPUなし）")),
			TEXT(""), TEXT(""), Rules.CPULevel == ChaosImpactMatch::CPULevelWeak ? Ice
				: Rules.CPULevel == ChaosImpactMatch::CPULevelNormal ? Gold
				: Rules.CPULevel == ChaosImpactMatch::CPULevelStrong ? Fire : FLinearColor(0.78f, 0.36f, 1.0f, 1.0f)};
		Strength.bDisabled = Rules.CPUCount <= 0;
		Entries.Add(Strength);
		Entries.Add({FSlateRect(1030, 712, 1454, 800), Rules.bDuel ? TEXT("試合開始") : TEXT("決定"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(146, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::StageSelect:
		// Six stages in a 3x2 grid (StageIndex order) above a back button. Picking a card starts.
		Entries.Add({FSlateRect(110, 184, 560, 434), ChaosImpactMatch::GetStageLabel(0), TEXT(""), TEXT("STAGE 1"), Gold});
		Entries.Add({FSlateRect(575, 184, 1025, 434), ChaosImpactMatch::GetStageLabel(1), TEXT(""), TEXT("STAGE 2"), Ice});
		Entries.Add({FSlateRect(1040, 184, 1490, 434), ChaosImpactMatch::GetStageLabel(2), TEXT(""), TEXT("STAGE 3"), Violet});
		Entries.Add({FSlateRect(110, 456, 560, 706), ChaosImpactMatch::GetStageLabel(3), TEXT(""), TEXT("STAGE 4"), Ice});
		Entries.Add({FSlateRect(575, 456, 1025, 706), ChaosImpactMatch::GetStageLabel(4), TEXT(""), TEXT("STAGE 5"), Gold});
		Entries.Add({FSlateRect(1040, 456, 1490, 706), ChaosImpactMatch::GetStageLabel(5), TEXT(""), TEXT("STAGE 6"), Fire});
		Entries.Add({FSlateRect(146, 736, 470, 800), TEXT("ルールへ戻る"), TEXT(""), TEXT(""), Muted});
		break;
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
	{
		// On the podium any button goes on (no choices yet); under the numbers, a rematch (local) or the lobby (online).
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const UChaosImpactResultsView* Results = Controller ? Controller->GetResultsView() : nullptr;
		if (Results && Results->GetPage() == 0)
		{
			break;
		}
		if (Results)
		{
			// The numbers: each row picks whose line the graph shows; an arrow at the edge goes to the awards and back.
			if (Results->GetStatsTab() == 0)
			{
				const TArray<FChaosImpactResultEntry>& Rows = Results->GetData().Entries;
				for (int32 Row = 0; Row < Rows.Num(); ++Row)
				{
					Entries.Add({UChaosImpactResultsView::GetStatsRowRect(Row, Rows.Num()), Rows[Row].Name, TEXT("row"),
						FString::FromInt(Row), Rows[Row].Color});
				}
				if (UChaosImpactResultsView::bAwardsEnabled)
				{
					Entries.Add({UChaosImpactResultsView::GetTabArrowRect(0), TEXT("アワード"), TEXT("awards"), TEXT(""), Gold});
				}
			}
			else
			{
				Entries.Add({UChaosImpactResultsView::GetTabArrowRect(1), TEXT("成績"), TEXT("stats"), TEXT(""), Ice});
			}
		}
		if (Controller && Controller->IsOnlineRoom())
		{
			Entries.Add({FSlateRect(600, 776, 1000, 846), TEXT("ロビーへもどる"), TEXT("lobby"), TEXT(""), Gold});
		}
		else
		{
			Entries.Add({FSlateRect(230, 776, 630, 846), TEXT("もう一度"), TEXT("rematch"), TEXT(""), Gold});
			Entries.Add({FSlateRect(670, 776, 1070, 846), TEXT("ルールを変える"), TEXT("rules"), TEXT(""), Ice});
			Entries.Add({FSlateRect(1110, 776, 1510, 846), TEXT("メニューへ"), TEXT("menu"), TEXT(""), Muted});
		}
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
	const double Now = FPlatformTime::Seconds();
	AnimationSeconds = static_cast<float>(Now - ScreenStartedAt);
	// The cursor moved to another entry (keys, stick or mouse): a tick. A new screen starting on its first does not.
	if (Screen == SoundScreen && SelectedIndex != SoundSelectedIndex && SoundSelectedIndex != INDEX_NONE && Entries.IsValidIndex(SelectedIndex))
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiMove);
	}
	SoundScreen = Screen;
	SoundSelectedIndex = SelectedIndex;
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect)
	{
		CharacterSelect->Tick(InDeltaTime);
	}
	if (Screen == EChaosImpactScreen::Settings && SettingsScreen)
	{
		SettingsScreen->Tick(InDeltaTime);
	}

	// The select screen covers the whole screen: the level behind it is not drawn at all (its pictures are filmed
	// on their own). Every other screen keeps the level as it was.
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		// (Not once the VS card is up: its pictures are filmed with the level's drawing.)
		const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bHideLevel = Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen()
			&& Owner && !Owner->IsTravelPending();
		if (Viewport->bDisableWorldRendering != bHideLevel && (bHideLevel || bLevelHiddenByMenu))
		{
			Viewport->bDisableWorldRendering = bHideLevel;
			bLevelHiddenByMenu = bHideLevel;
		}
	}

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
	if (Screen == EChaosImpactScreen::OnlineConnecting)
	{
		const auto* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const FString Error = Controller ? Controller->GetOnlineConnectionError() : FString();
		if (Error != LastCreateError)
		{
			LastCreateError = Error; RefreshEntries();
			SelectedIndex = FMath::Clamp(SelectedIndex, 0, FMath::Max(0, Entries.Num() - 1));
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
		P.Text(bTrainingBallPickerOpen ? TEXT("ボールを選ぶ") : bTrainingTeamsOpen ? TEXT("チーム設定") : TEXT("TRAINING"), 66, 60, 44, Paper);
		P.Box(68, 122, 150, 8, Fire);
		P.Box(226, 122, 48, 8, Ice);
	}
	else if (Screen == EChaosImpactScreen::MatchEnd)
	{
		// On the podium nothing is added: any button goes on.
		if (!Entries.IsEmpty())
		{
			// The results stay visible above; only a band for the choices is added.
			const FMenuPainter Band{DesignGeometry, OutDrawElements, BaseLayer + 1, EaseOut(T / 0.3f)};
			Band.Box(-800.0f, 752.0f, 3200.0f, 118.0f, WithAlpha(Ink, 0.86f));
			Band.Box(-800.0f, 752.0f, 3200.0f, 4.0f, Gold);
		}
	}
	else
	{
		Full.Box(0, 0, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Ink);
		const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const AChaosImpactTitleDemo* Demo = Screen == EChaosImpactScreen::Title && Owner ? Owner->GetTitleDemo() : nullptr;
		const float DemoAlpha = Demo && Demo->GetPicture() ? Demo->GetFadeIn() : 0.0f;
		if (DemoAlpha > 0.0f && DemoBrush.GetResourceObject() != Demo->GetPicture())
		{
			DemoBrush.SetResourceObject(Demo->GetPicture());
			DemoBrush.ImageSize = FVector2D(AChaosImpactTitleDemo::PictureWidth, AChaosImpactTitleDemo::PictureHeight);
			DemoBrush.DrawAs = ESlateBrushDrawType::Image;
		}
		PaintBackdrop(DesignGeometry, OutDrawElements, BaseLayer + 1, T, DemoAlpha > 0.0f ? &DemoBrush : nullptr, DemoAlpha);
	}

	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect)
	{
		const int32 SelectLayer = CharacterSelect->Paint(DesignGeometry, OutDrawElements, BaseLayer + 2);
		PaintEnterWipe(DesignGeometry, OutDrawElements, SelectLayer + 1, T);
		return SelectLayer + 2;
	}
	if (Screen == EChaosImpactScreen::Settings && SettingsScreen)
	{
		const int32 SettingsLayer = SettingsScreen->Paint(DesignGeometry, OutDrawElements, BaseLayer + 2);
		PaintEnterWipe(DesignGeometry, OutDrawElements, SettingsLayer + 1, T);
		return SettingsLayer + 2;
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
	else if (Screen == EChaosImpactScreen::SoloStageSelect)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("SOLO STAGE SELECT"), T);
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
			Screen == EChaosImpactScreen::OnlineRoomName ? TEXT("部屋のなまえ") : TEXT("なまえ"), T);
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
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("部屋をさがす"), T);
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		const float ChipIn = EaseOut((T - 0.1f) / 0.3f);
		const FMenuPainter ChipPainter{MakeSkewed(DesignGeometry, 1070.0f + (1.0f - ChipIn) * 80.0f, 96.0f, 360.0f, 70.0f, -0.3f),
			OutDrawElements, BaseLayer + 2, ChipIn};
		ChipPainter.Box(0.0f, 0.0f, 360.0f, 70.0f, Fire);
		ChipPainter.Text(FString::Printf(TEXT("あいことば  %s"), Sessions ? *Sessions->GetPassword() : TEXT("")),
			180.0f, 14.0f, 30.0f, Paper, ETextAlign::Center);
		if (Sessions && Sessions->GetRoomListings().IsEmpty())
		{
			// Nothing yet: keep looking. A panel with the balls hopping, how long it has been, and a tip.
			const float In = EaseOut((T - 0.2f) / 0.3f);
			const FMenuPainter Card{MakeSkewed(DesignGeometry, 380.0f, 300.0f + (1.0f - In) * 40.0f, 840.0f, 330.0f, -0.08f),
				OutDrawElements, BaseLayer + 3, In};
			Card.Box(12.0f, 14.0f, 840.0f, 330.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			Card.Box(0.0f, 0.0f, 840.0f, 330.0f, FLinearColor(0.015f, 0.02f, 0.04f, 0.94f));
			Card.Box(0.0f, 0.0f, 840.0f, 6.0f, Fire);
			const bool bNone = Sessions->HasSearchedOnce();
			Card.Text(bNone ? TEXT("このあいことばの部屋はまだありません") : TEXT("部屋をさがしています"),
				420.0f, 30.0f, 38.0f, Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
			Card.Text(bNone ? TEXT("だれかが部屋をつくると、ここに出ます") : TEXT("見つかると ここに出ます"), 420.0f, 86.0f, 22.0f, Muted,
				ETextAlign::Center);
			ChaosImpactPaint::HudWaitBalls(ChaosImpactPaint::FPainter{Card.Geometry, Card.Elements, Card.Layer + 1, Card.Alpha},
				FVector2D(420.0, 222.0), T, 36.0f);
			Card.Text(FString::Printf(TEXT("けいか %s"), *ChaosImpactPaint::HudElapsed(T)), 812.0f, 288.0f, 20.0f, Muted, ETextAlign::Right,
				TEXT("Bold"));
			Card.Text(FString::Printf(TEXT("ヒント：%s"), OnlineWaitTip(T)), 28.0f, 288.0f, 20.0f, WithAlpha(Paper, 0.8f), ETextAlign::Left);
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
		ChipPainter.Text(bCreate ? TEXT("部屋をつくる") : TEXT("部屋をさがす"), 180.0f, 10.0f, 34.0f, Paper,
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
	else if (Screen == EChaosImpactScreen::OnlineConnecting)
	{
		const auto* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bWaiting = Controller && Controller->IsCheckingOnlineConnection();
		const float E = EaseOut(T / .25f);
		const FGeometry Band = MakeSkewed(DesignGeometry, -180.f, 300.f, 1960.f, 240.f, -.3f);
		const FMenuPainter Panel{Band, OutDrawElements, BaseLayer + 2, E};
		Panel.Box(0, 0, 1960, 240, FLinearColor(.015f, .018f, .035f, .96f));
		Panel.Box(0, 0, 1960, 7, bWaiting ? Ice : Fire);
		const FMenuPainter Label{DesignGeometry, OutDrawElements, BaseLayer + 3, E};
		Label.Text(bWaiting ? TEXT("接続しています…") : TEXT("接続できませんでした"), 800, 350, 54,
			Paper, ETextAlign::Center, TEXT("Black"), 3, Ink);
		if (bWaiting)
		{
			ChaosImpactPaint::HudWaitBalls(ChaosImpactPaint::FPainter{Label.Geometry, Label.Elements, Label.Layer + 1, Label.Alpha},
				FVector2D(800, 465), T, 30);
		}
		else if (Controller)
		{
			Label.Text(Controller->GetOnlineConnectionError(), 800, 458, 26, Paper, ETextAlign::Center);
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
		Label.Text(Error.IsEmpty() ? FString(TEXT("部屋をつくっています")) : Error, 800.0f, 350.0f, 54.0f,
			Error.IsEmpty() ? Paper : Fire, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
		Label.Text(FString::Printf(TEXT("あいことば  %s"), Sessions ? *Sessions->GetPassword() : TEXT("")),
			800.0f, 450.0f, 30.0f, Muted, ETextAlign::Center);
		if (Error.IsEmpty())
		{
			ChaosImpactPaint::HudWaitBalls(ChaosImpactPaint::FPainter{Label.Geometry, Label.Elements, Label.Layer + 1, Label.Alpha},
				FVector2D(800.0, 618.0), T, 34.0f);
			Label.Text(FString::Printf(TEXT("ヒント：%s"), OnlineWaitTip(T)), 800.0f, 494.0f, 22.0f, WithAlpha(Paper, 0.8f), ETextAlign::Center);
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
			const int32 Humans = ChaosImpactMatch::GetCompetingHumans(Rules, Controller->GetMatchHumanCount());
			const int32 Total = Humans + Rules.CPUCount;
			const float In = EaseOut((T - 0.2f) / 0.35f);
			const FGeometry Panel = MakeSkewed(DesignGeometry, 430.0f + (1.0f - In) * 80.0f, 530.0f, 740.0f, 156.0f, -0.12f);
			const FMenuPainter Info{Panel, OutDrawElements, BaseLayer + 2, In};
			Info.Box(12.0f, 14.0f, 740.0f, 156.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			Info.Box(0.0f, 0.0f, 740.0f, 156.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
			Info.Box(0.0f, 0.0f, 10.0f, 156.0f, Rules.bSpectate ? Ice : Gold);
			if (Rules.bDuel)
			{
				// The duel: who faces whom, and how a round and the match are won.
				Info.Box(0.0f, 0.0f, 10.0f, 156.0f, Ice);
				Info.Text(Rules.CPUCount > 0 ? FString::Printf(TEXT("1対1：あなた 対 CPU（%s）"), ChaosImpactMatch::GetCPULevelName(Rules.CPULevel))
					: FString(TEXT("1対1：プレイヤー同士")), 36.0f, 14.0f, 30.0f, Paper);
				Info.Text(FString::Printf(TEXT("%d本先取・体力3・特殊ボールは毎試合スロットで%d種類"), ChaosImpactMatch::DuelRoundsToWin,
					ChaosImpactMatch::DuelBallPicks), 38.0f, 66.0f, 24.0f, Ice);
				Info.Text(FString::Printf(TEXT("時間切れは体力の多いほう、同じなら%d秒のサドンデス"), FMath::RoundToInt(ChaosImpactMatch::DuelSuddenDeathSeconds)),
					38.0f, 110.0f, 22.0f, Muted);
			}
			else
			{
				Info.Text(Rules.bSpectate
					? FString::Printf(TEXT("観戦：CPU %d人の試合をカメラで見る"), Rules.CPUCount)
					: FString::Printf(TEXT("プレイヤー %d人 ＋ CPU %d人 ＝ %d人"), Humans, Rules.CPUCount, Total),
					36.0f, 14.0f, 30.0f, Paper);
				Info.Text(Rules.bCrashBall
					? FString::Printf(TEXT("レッド 対 ブルー・1チーム最大 %d人"), ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Total))
					: Rules.IsTeamBattle()
					? FString::Printf(TEXT("1チーム最大 %d人・チームの合計ポイントで勝負"),
						ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Total))
					: FString(TEXT("ポイントが一番多い人の勝ち")), 38.0f, 66.0f, 24.0f, Rules.bCrashBall ? Violet : Gold);
				if (!Rules.bSpectate)
				{
					Info.Text(Rules.bCrashBall ? TEXT("巨大球に当てる +1pt　　敵に当てる +1pt") : TEXT("敵に当てる +1pt　　撃破ボーナス +1pt"),
						38.0f, 110.0f, 24.0f, Muted);
				}
			}
			if (Rules.bCrashBall)
			{
				PaintCrashRuleGuide(DesignGeometry, OutDrawElements, BaseLayer + 2, T);
			}
		}
	}
	else if (Screen == EChaosImpactScreen::StageSelect)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("STAGE SELECT"), T);
		if (const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			// The rules just chosen, as a reminder of what the stage is for.
			const FChaosImpactMatchRules& Rules = Controller->GetPendingMatchRules();
			const float ChipIn = EaseOut((T - 0.1f) / 0.3f);
			const FMenuPainter Chip{MakeSkewed(DesignGeometry, 770.0f + (1.0f - ChipIn) * 80.0f, 96.0f, 680.0f, 70.0f, -0.3f),
				OutDrawElements, BaseLayer + 2, ChipIn};
			Chip.Box(0.0f, 0.0f, 680.0f, 70.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
			Chip.Box(0.0f, 0.0f, 10.0f, 70.0f, Rules.bSpectate ? Ice : Gold);
			Chip.Text(FString::Printf(TEXT("%s%s ・ %d分 ・ CPU %d人%s"), Rules.bSpectate ? TEXT("観戦 ・ ") : TEXT(""),
				*ChaosImpactMatch::DescribeRule(Rules), Rules.Minutes, Rules.CPUCount,
				Rules.CPUCount > 0 ? *FString::Printf(TEXT("（%s）"), ChaosImpactMatch::GetCPULevelName(Rules.CPULevel)) : TEXT("")),
				346.0f, 18.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Bold"));
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
				if (Match->Rules.bCrashBall)
				{
					// クラッシュボール: the goal this team pushes the giant ball into, in the other team's colour.
					const FLinearColor Target = ChaosImpactMatch::GetTeamColor(1 - Team);
					C.Box(14.0f, 372.0f, Width - 28.0f, 52.0f, WithAlpha(Target, 0.18f));
					C.Box(14.0f, 372.0f, 8.0f, 52.0f, Target);
					C.Text(FString::Printf(TEXT("%sのゴールへ巨大球を押し込め！"), ChaosImpactMatch::GetTeamName(1 - Team)),
						Width * 0.5f, 382.0f, 24.0f, Paper, ETextAlign::Center);
				}
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
		if (Screen == EChaosImpactScreen::TrainingOverlay && Entry.Detail == TEXT("ball_type"))
		{
			const ChaosImpactPaint::FPainter Tile{DesignGeometry,OutDrawElements,BaseLayer + 4};
			const ChaosImpactPaint::FPainter TileText{DesignGeometry,OutDrawElements,BaseLayer + 5};
			const float X = Rect.Left, Y = Rect.Top, W = Rect.GetSize().X, H = Rect.GetSize().Y;
			const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			const bool bCurrent = Owner && static_cast<int32>(Owner->GetTrainingSummonBallType()) == FCString::Atoi(*Entry.Number);
			Tile.Box(X,Y,W,H,FMath::Lerp(FLinearColor(0.015f,0.023f,0.035f,1),FLinearColor(0.07f,0.11f,0.18f,1),Blend));
			Tile.Outline(X,Y,W,H,Index == SelectedIndex ? Paper : bCurrent ? Gold : WithAlpha(Paper,0.25f),Index == SelectedIndex || bCurrent ? 3 : 1);
			const FVector2D At(X + W * 0.5f,Y + 36);
			Tile.Disc(At + FVector2D(2,3),21,Ink);
			Tile.Disc(At,21,Entry.Accent); Tile.Ring(At,22,Paper,2);
			Tile.Disc(At + FVector2D(-6,-7),5,WithAlpha(Paper,0.7f));
			TileText.Text(Entry.Title,X + W * 0.5f,Y + 67,21,Paper,ChaosImpactPaint::ETextAlign::Center);
			if (bCurrent) { TileText.Text(TEXT("✓"),X + W - 22,Y + 8,22,Gold,ChaosImpactPaint::ETextAlign::Center); }
		}
		else if ((Screen == EChaosImpactScreen::ModeSelect || Screen == EChaosImpactScreen::MultiReady
			|| Screen == EChaosImpactScreen::VSSelect) && Index < 2)
		{
			PaintCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title,
				Entry.Accent, Blend, bPressed, EntryIn, T, 118.0f, 0);
		}
		else if (Screen == EChaosImpactScreen::StageSelect && Index < ChaosImpactMatch::StageCount)
		{
			if (StageShots.IsValidIndex(Index) && StageShots[Index])
			{
				PaintStageShotCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, StageShotBrushes[Index],
					Entry.Number, Entry.Title, Entry.Accent, Blend, bPressed, EntryIn, T);
			}
			else
			{
				const float BigFontSize = (Rect.Bottom - Rect.Top) < 320.0f ? 40.0f : 64.0f;
				PaintCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title,
					Entry.Accent, Blend, bPressed, EntryIn, T, BigFontSize, 0);
				PaintStageMap(DesignGeometry, OutDrawElements, BaseLayer + 4, Rect, Index, Blend, EntryIn);
			}
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
					Row.Text(Sessions->GetJoinBlocker(Listing), W - 58.0f, 24.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Black"));
				}
				else if (Sessions->WouldJoinAsSpectator(Listing))
				{
					// Mid-match or closed: joins to watch.
					Row.Box(W - 104.0f, 22.0f, 92.0f, 36.0f, Gold);
					Row.Text(TEXT("観戦"), W - 58.0f, 24.0f, 24.0f, Ink, ETextAlign::Center, TEXT("Black"));
				}
			}
			if (Blend > 0.01f)
			{
				Row.Outline(-4.0f, -4.0f, W + 8.0f, H + 8.0f, WithAlpha(Paper, 0.6f * Blend), 3.0f);
			}
		}
		else if (Screen == EChaosImpactScreen::MatchEnd && Entry.Detail == TEXT("row"))
		{
			// The row itself is drawn with the results: only the cursor on it.
			if (Blend > 0.01f)
			{
				const FMenuPainter RowCursor{DesignGeometry, OutDrawElements, BaseLayer + 4, Blend};
				const float W = static_cast<float>(Rect.GetSize().X);
				const float H = static_cast<float>(Rect.GetSize().Y);
				RowCursor.Outline(static_cast<float>(Rect.Left) - 4.0f, static_cast<float>(Rect.Top) - 4.0f, W + 8.0f, H + 8.0f,
					WithAlpha(Paper, 0.6f + 0.3f * FMath::Sin(T * 6.0f)), 3.0f);
				RowCursor.Text(TEXT("▶"), static_cast<float>(Rect.Left) - 30.0f, static_cast<float>(Rect.Top) + H * 0.5f - 13.0f, 20.0f, Paper);
			}
		}
		else if (Screen == EChaosImpactScreen::MatchEnd && (Entry.Detail == TEXT("awards") || Entry.Detail == TEXT("stats")))
		{
			// A tab at the screen's edge: a chevron pointing the way, with the page's name down it.
			const bool bForward = Entry.Detail == TEXT("awards");
			const float W = static_cast<float>(Rect.GetSize().X);
			const float H = static_cast<float>(Rect.GetSize().Y);
			const float X = static_cast<float>(Rect.Left);
			const float Y = static_cast<float>(Rect.Top);
			const FMenuPainter Tab{DesignGeometry, OutDrawElements, BaseLayer + 4};
			Tab.Box(X, Y, W, H, FMath::Lerp(WithAlpha(Ink, 0.82f), FLinearColor(0.05f, 0.09f, 0.16f, 0.95f), Blend));
			Tab.Box(bForward ? X + W - 5.0f : X, Y, 5.0f, H, Entry.Accent);
			const float Direction = bForward ? 1.0f : -1.0f;
			const float Nudge = Direction * 6.0f * (0.5f + 0.5f * FMath::Sin(T * 5.0f));
			const FVector2D Tip(X + W * 0.5f + 12.0f * Direction + Nudge, Y + 56.0f);
			const FLinearColor ChevronColor = FMath::Lerp(Entry.Accent, Paper, Blend * 0.6f);
			Tab.Line(Tip + FVector2D(-22.0f * Direction, -28.0f), Tip, ChevronColor, 7.0f);
			Tab.Line(Tip, Tip + FVector2D(-22.0f * Direction, 28.0f), ChevronColor, 7.0f);
			for (int32 Letter = 0; Letter < Entry.Title.Len(); ++Letter)
			{
				Tab.Text(Entry.Title.Mid(Letter, 1), X + W * 0.5f, Y + 104.0f + Letter * 28.0f, 21.0f, Paper, ETextAlign::Center, TEXT("Black"));
			}
			if (Blend > 0.01f)
			{
				Tab.Outline(X - 3.0f, Y - 3.0f, W + 6.0f, H + 6.0f, WithAlpha(Paper, 0.7f * Blend), 3.0f);
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
		if (Screen == EChaosImpactScreen::MatchRules && SelectedIndex <= 3)
		{
			if (Entries[SelectedIndex].bDisabled)
			{
				return;
			}
			Controller->AdjustMatchRule(SelectedIndex, bRight ? 1 : -1);
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiValue);
			BuildEntries();
			return;
		}
		if (Screen == EChaosImpactScreen::TeamSelect)
		{
			Controller->ChangeOwnTeam(bRight ? 1 : -1);
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiValue);
			return;
		}
	}
	const bool bBack = Key == EKeys::Up || Key == EKeys::Left
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left;
	if (Screen == EChaosImpactScreen::TrainingOverlay && bTrainingTeamsOpen && (bLeft || bRight))
	{
		if (auto* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			if (SelectedIndex == 0) { Owner->ToggleTrainingTeams(); }
			else if (SelectedIndex >= 1 && SelectedIndex <= 8 && !Entries[SelectedIndex].bDisabled)
			{ Owner->CycleTrainingTeam(SelectedIndex >= 5,(SelectedIndex-1)%4,bRight ? 1 : -1); }
			BuildEntries();
		}
		return;
	}
	if (Screen == EChaosImpactScreen::TrainingOverlay && bTrainingBallPickerOpen && Key != EKeys::Tab)
	{
		const int32 Back = ChaosImpactBallTypes::Count;
		if (SelectedIndex == Back)
		{
			const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			SelectedIndex = Owner ? static_cast<int32>(Owner->GetTrainingSummonBallType()) : 0;
		}
		else if (bLeft || bRight) { SelectedIndex = (SelectedIndex / 3) * 3 + (SelectedIndex % 3 + (bRight ? 1 : 2)) % 3; }
		else if (bBack) { SelectedIndex = SelectedIndex < 3 ? Back : SelectedIndex - 3; }
		else { SelectedIndex = SelectedIndex + 3 >= Back ? Back : SelectedIndex + 3; }
	}
	else if (Screen == EChaosImpactScreen::MatchEnd && Key != EKeys::Tab)
	{
		NavigateResults(Key);
	}
	else if (Screen == EChaosImpactScreen::ModeSelect && Key != EKeys::Tab && Entries.Num() == 5)
	{
		// Solo/VS above; タイトルへ, 設定 and トレーニング along the bottom.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		if (bHorizontal && SelectedIndex <= 1)
		{
			SelectedIndex = 1 - SelectedIndex;
		}
		else if (bHorizontal)
		{
			const int32 Bottom[] = {3, 4, 2};
			int32 At = 0;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				At = Bottom[Index] == SelectedIndex ? Index : At;
			}
			SelectedIndex = Bottom[(At + (bRight ? 1 : 2)) % 3];
		}
		else
		{
			const int32 Vertical[] = {3, 2, 1, 0, 0};
			SelectedIndex = Vertical[SelectedIndex];
		}
	}
	else if ((Screen == EChaosImpactScreen::ModeSelect || Screen == EChaosImpactScreen::MultiReady)
		&& Key != EKeys::Tab)
	{
		// A spatial grid: Solo/Multi above Back/Training.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		const int32 Horizontal[] = {1, 0, 3, 2};
		const int32 Vertical[] = {3, 2, 1, 0};
		SelectedIndex = bHorizontal ? Horizontal[SelectedIndex] : Vertical[SelectedIndex];
	}
	else if (Screen == EChaosImpactScreen::StageSelect && Key != EKeys::Tab)
	{
		// 3x2 grid of stages (0..2 top row, 3..5 bottom row) above the back button (Stages).
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
		const int32 Stages = ChaosImpactMatch::StageCount;
		constexpr int32 Cols = 3;
		if (bHorizontal)
		{
			if (SelectedIndex < Stages)
			{
				const bool bToLeft = Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left;
				const int32 RowStart = (SelectedIndex / Cols) * Cols;
				const int32 RowCount = FMath::Min(Cols, Stages - RowStart);
				SelectedIndex = RowStart + (SelectedIndex - RowStart + (bToLeft ? RowCount - 1 : 1)) % RowCount;
			}
		}
		else if (SelectedIndex >= Stages)
		{
			SelectedIndex = bUp ? FMath::Clamp(LastStageCard >= Cols ? LastStageCard : LastStageCard + Cols, 0, Stages - 1)
				: FMath::Clamp(LastStageCard % Cols, 0, Stages - 1);
		}
		else if (bUp)
		{
			LastStageCard = SelectedIndex;
			SelectedIndex = SelectedIndex >= Cols ? SelectedIndex - Cols : Stages;
		}
		else
		{
			LastStageCard = SelectedIndex;
			SelectedIndex = SelectedIndex + Cols < Stages ? SelectedIndex + Cols : Stages;
		}
	}
	else if ((Screen == EChaosImpactScreen::VSSelect || Screen == EChaosImpactScreen::SoloStageSelect) && Key != EKeys::Tab)
	{
		// 左右でステージ1と2を行き来し、下入力で戻るボタンへ移動
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right
			|| Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		SelectedIndex = bHorizontal
			? (SelectedIndex == 0 ? 1 : SelectedIndex == 1 ? 0 : 2)
			: (SelectedIndex == 2 ? 0 : 2);
	}
	else
	{
		SelectedIndex = (SelectedIndex + Entries.Num() + (bBack ? -1 : 1)) % Entries.Num();
		// The rules' greyed-out row (CPUの強さ with no CPUs) is passed over.
		if (Screen == EChaosImpactScreen::MatchRules && Entries[SelectedIndex].bDisabled)
		{
			SelectedIndex = (SelectedIndex + Entries.Num() + (bBack ? -1 : 1)) % Entries.Num();
		}
	}
	PressedIndex = INDEX_NONE;
	DisarmSelection();
}

bool UChaosImpactMenuWidget::IsResultsChoice(const int32 Index) const
{
	return Entries.IsValidIndex(Index) && Entries[Index].Detail != TEXT("row") && Entries[Index].Detail != TEXT("awards")
		&& Entries[Index].Detail != TEXT("stats");
}

void UChaosImpactMenuWidget::SwitchResultsTab(const int32 Tab)
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	UChaosImpactResultsView* Results = Controller ? Controller->GetResultsView() : nullptr;
	if (!Results || Results->GetPage() != 1 || (Tab != 0 && !UChaosImpactResultsView::bAwardsEnabled))
	{
		return;
	}
	Results->SetStatsTab(Tab);
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiTab);
	BuildEntries();
	SelectBlend.Init(0.0f, Entries.Num());
	PressedIndex = INDEX_NONE;
	// On the awards the cursor is on the arrow back; back on the numbers, on the row it was on.
	SelectedIndex = 0;
	if (Tab == 0)
	{
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (Entries[Index].Detail == TEXT("row") && FCString::Atoi(*Entries[Index].Number) == ResultsRowCursor)
			{
				SelectedIndex = Index;
			}
		}
	}
}

void UChaosImpactMenuWidget::NavigateResults(const FKey Key)
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const UChaosImpactResultsView* Results = Controller ? Controller->GetResultsView() : nullptr;
	const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
	const bool bDown = Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down;
	const bool bLeft = Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left;
	const bool bRight = Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right;
	TArray<int32> Rows;
	TArray<int32> Choices;
	int32 Arrow = INDEX_NONE;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].Detail == TEXT("row"))
		{
			Rows.Add(Index);
		}
		else if (IsResultsChoice(Index))
		{
			Choices.Add(Index);
		}
		else
		{
			Arrow = Index;
		}
	}
	const int32 Row = Rows.IndexOfByKey(SelectedIndex);
	const int32 Choice = Choices.IndexOfByKey(SelectedIndex);
	const bool bOnArrow = SelectedIndex == Arrow && Arrow != INDEX_NONE;
	const auto Sideways = [this, &Choices, Choice, bRight]()
	{
		if (!Choices.IsEmpty())
		{
			SelectedIndex = Choices[(FMath::Max(Choice, 0) + (bRight ? 1 : Choices.Num() - 1)) % Choices.Num()];
		}
	};
	if (!Results || Results->GetStatsTab() == 0)
	{
		// The numbers: up and down through the rows (and on down to the choices); right from a row goes to the awards.
		if (Row != INDEX_NONE)
		{
			if (bRight)
			{
				SwitchResultsTab(1);
			}
			else if (bUp && Row > 0)
			{
				SelectedIndex = Rows[Row - 1];
			}
			else if (bDown)
			{
				SelectedIndex = Row + 1 < Rows.Num() ? Rows[Row + 1] : Choices.IsEmpty() ? SelectedIndex : Choices[0];
			}
		}
		else if (bOnArrow)
		{
			if (bRight)
			{
				SwitchResultsTab(1);
			}
			else if (bLeft && Rows.IsValidIndex(ResultsRowCursor))
			{
				SelectedIndex = Rows[ResultsRowCursor];
			}
			else if (bDown && !Choices.IsEmpty())
			{
				SelectedIndex = Choices.Last();
			}
		}
		else if (bUp && !Rows.IsEmpty())
		{
			SelectedIndex = Rows[FMath::Clamp(ResultsRowCursor, 0, Rows.Num() - 1)];
		}
		else if (bLeft || bRight)
		{
			Sideways();
		}
	}
	else
	{
		// The awards: left goes back (from the arrow, or past the first choice).
		if (bOnArrow)
		{
			if (bLeft)
			{
				SwitchResultsTab(0);
			}
			else if (bDown && !Choices.IsEmpty())
			{
				SelectedIndex = Choices[0];
			}
		}
		else if (bUp && Arrow != INDEX_NONE)
		{
			SelectedIndex = Arrow;
		}
		else if (bLeft && Choice <= 0)
		{
			SwitchResultsTab(0);
		}
		else if (bLeft || bRight)
		{
			Sideways();
		}
	}
	if (Entries.IsValidIndex(SelectedIndex) && Entries[SelectedIndex].Detail == TEXT("row"))
	{
		ResultsRowCursor = FCString::Atoi(*Entries[SelectedIndex].Number);
	}
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
	ChaosImpactSfx::Play2D(this, Entries[SelectedIndex].bDisabled ? EChaosImpactSfx::UiDeny
		: Screen == EChaosImpactScreen::Title ? EChaosImpactSfx::UiStart : EChaosImpactSfx::UiConfirm);
	switch (Screen)
	{
	case EChaosImpactScreen::Title:
		Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
		break;
	case EChaosImpactScreen::ModeSelect:
		if (SelectedIndex == 0)
		{
			// ソロは専用のタイトル（ファイル選択）から。
			Controller->OpenSoloFromMenu();
		}
		else if (SelectedIndex == 1) { Controller->ShowMenuScreen(EChaosImpactScreen::VSSelect); }
		else if (SelectedIndex == 2) { Controller->BeginTrainingSetup(); }
		else if (SelectedIndex == 4) { Controller->OpenSettings(); }
		else { Controller->ShowMenuScreen(EChaosImpactScreen::Title); }
		break;

	case EChaosImpactScreen::SoloStageSelect:
		if (SelectedIndex == 0)
		{
			Controller->OpenSoloLevel(1); // ステージ 1 開始
		}
		else if (SelectedIndex == 1)
		{
			Controller->OpenSoloLevel(2); // ステージ 2 開始
		}
		else
		{
			GoBack();
		}
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
		else if (Entries[SelectedIndex].Detail == TEXT("spectate"))
		{
			Controller->BeginLocalSpectate();
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
	case EChaosImpactScreen::OnlineConnecting:
		if (!Controller->IsCheckingOnlineConnection() && SelectedIndex == 0) { Controller->BeginVersusOnline(); }
		else { Controller->CancelOnlineConnection(); }
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
		if (SelectedIndex <= 3)
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				Controller->AdjustMatchRule(SelectedIndex, 1);
				BuildEntries();
			}
		}
		else if (SelectedIndex == 4)
		{
			Controller->OpenStageSelect();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::StageSelect:
		if (SelectedIndex < ChaosImpactMatch::StageCount)
		{
			Controller->ChooseStage(SelectedIndex);
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
	{
		const FString Choice = Entries[SelectedIndex].Detail;
		if (Choice == TEXT("row"))
		{
			// Only this person's line in the graph (again: everyone's).
			ResultsRowCursor = FCString::Atoi(*Entries[SelectedIndex].Number);
			if (UChaosImpactResultsView* Results = Controller->GetResultsView())
			{
				Results->ToggleGraphFocus(ResultsRowCursor);
			}
		}
		else if (Choice == TEXT("awards") || Choice == TEXT("stats"))
		{
			SwitchResultsTab(Choice == TEXT("awards") ? 1 : 0);
		}
		else if (Choice == TEXT("lobby"))
		{
			Controller->ReturnToLobbyFromResults();
		}
		else if (Choice == TEXT("rematch"))
		{
			Controller->RetryVersusMatch();
		}
		else if (Choice == TEXT("rules"))
		{
			Controller->OpenMatchRules(EChaosImpactScreen::MatchEnd);
		}
		else
		{
			Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
		}
		break;
	}
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
		if (Controller->HandleWorldMapPauseChoice(Entries[SelectedIndex].Detail))
		{
			break;
		}
		if (Entries[SelectedIndex].Detail == TEXT("solo_retry"))
		{
			Controller->RetrySoloLevel();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("solo_stage_select"))
		{
			Controller->ShowMenuScreen(EChaosImpactScreen::SoloStageSelect);
		}
		if (Entries[SelectedIndex].Detail == TEXT("settings"))
		{
			Controller->OpenSettings();
			break;
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
		else if (Entries[SelectedIndex].Detail == TEXT("change_character"))
		{
			Controller->BeginRoomCharacterChange();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("change_name"))
		{
			Controller->BeginRoomPlayerRename();
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
		if (bTrainingTeamsOpen)
		{
			if (SelectedIndex == 0) { Controller->ToggleTrainingTeams(); }
			else if (SelectedIndex >= 1 && SelectedIndex <= 8)
			{ Controller->CycleTrainingTeam(SelectedIndex >= 5,(SelectedIndex-1)%4); }
			else { bTrainingTeamsOpen = false; SelectedIndex = 7; }
			BuildEntries(); break;
		}
		if (bTrainingBallPickerOpen)
		{
			if (SelectedIndex < ChaosImpactBallTypes::Count) { Controller->SetTrainingSummonBallType(ChaosImpactBallTypes::FromIndex(SelectedIndex)); }
			CloseTrainingBallPicker(); break;
		}
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
			OpenTrainingBallPicker();
		}
		else if (SelectedIndex == 5)
		{
			Controller->SummonTrainingBall();
		}
		else if (SelectedIndex == 6)
		{
			Controller->ToggleTrainingShoulderCamera();
			BuildEntries();
		}
		else if (SelectedIndex == 7)
		{
			bTrainingTeamsOpen = true; SelectedIndex = 0; BuildEntries();
		}
		else if (SelectedIndex == 8)
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
	if (Screen == EChaosImpactScreen::TrainingOverlay && bTrainingTeamsOpen)
	{ bTrainingTeamsOpen = false; SelectedIndex = 7; BuildEntries(); ChaosImpactSfx::Play2D(this,EChaosImpactSfx::UiBack); return; }
	if (Screen == EChaosImpactScreen::TrainingOverlay && bTrainingBallPickerOpen)
	{ CloseTrainingBallPicker(); ChaosImpactSfx::Play2D(this,EChaosImpactSfx::UiBack); return; }
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
	{
		if (Screen != EChaosImpactScreen::Title)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiBack);
		}
		switch (Screen)
		{
		case EChaosImpactScreen::Pause:
			Controller->ResumeGameplay();
			break;
		case EChaosImpactScreen::TrainingSettings:
			Controller->ShowMenuScreen(EChaosImpactScreen::Pause);
			break;
		case EChaosImpactScreen::Settings:
			Controller->CloseSettings();
			break;
		case EChaosImpactScreen::TrainingOverlay:
			Controller->CloseTrainingOverlay();
			break;
		case EChaosImpactScreen::ModeSelect:
			Controller->ShowMenuScreen(EChaosImpactScreen::Title);
			break;
		case EChaosImpactScreen::SoloStageSelect:
			Controller->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
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
		case EChaosImpactScreen::OnlineConnecting:
			Controller->CancelOnlineConnection();
			break;
		case EChaosImpactScreen::MultiReady:
			Controller->ShowMenuScreen(EChaosImpactScreen::OnlinePlayers);
			break;
		case EChaosImpactScreen::ControllerAssignment:
			Controller->ShowMenuScreen(Controller->GetControllerAssignmentReturnScreen());
			break;
		case EChaosImpactScreen::OnlineName:
			if (Controller->IsRenamingPlayer())
			{
				Controller->CancelRoomPlayerRename();
			}
			else
			{
				Controller->ShowMenuScreen(EChaosImpactScreen::MultiReady);
			}
			break;
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
		case EChaosImpactScreen::StageSelect:
			Controller->CancelStageSelect();
			break;
		default:
			break;
		}
	}
}

FReply UChaosImpactMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = ChaosImpactSettings::ToMenuKey(InKeyEvent.GetKey(), InKeyEvent.GetInputDeviceId().GetId());
	// The exploration map's hidden way in (OpenWorld/, removable): on the title, up up down down left right left right B A
	// (the arrow keys or the pad's D-pad, then B and A).
	if (Screen == EChaosImpactScreen::Title && !InKeyEvent.IsRepeat())
	{
		static int32 SecretStep = 0;
		const FKey Raw = InKeyEvent.GetKey();
		const auto Is = [&Raw](const FKey& A, const FKey& B) { return Raw == A || Raw == B; };
		const bool Steps[] = {Is(EKeys::Up, EKeys::Gamepad_DPad_Up), Is(EKeys::Up, EKeys::Gamepad_DPad_Up),
			Is(EKeys::Down, EKeys::Gamepad_DPad_Down), Is(EKeys::Down, EKeys::Gamepad_DPad_Down),
			Is(EKeys::Left, EKeys::Gamepad_DPad_Left), Is(EKeys::Right, EKeys::Gamepad_DPad_Right),
			Is(EKeys::Left, EKeys::Gamepad_DPad_Left), Is(EKeys::Right, EKeys::Gamepad_DPad_Right),
			Is(EKeys::B, EKeys::Gamepad_FaceButton_Right), Is(EKeys::A, EKeys::Gamepad_FaceButton_Bottom)};
		SecretStep = Steps[SecretStep] ? SecretStep + 1 : (Steps[0] ? 1 : 0);
		if (SecretStep == UE_ARRAY_COUNT(Steps))
		{
			SecretStep = 0;
			if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
				Controller && Controller->OpenExplorationMap(TEXT("?CIRPGTitle=1")))
			{
				return FReply::Handled();
			}
		}
	}
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
			|| (ChaosImpactSettings::IsActionKey(GetOwningPlayer(), EChaosImpactAction::TrainingMenu, Key)
				&& Screen == EChaosImpactScreen::TrainingOverlay)
			|| (ChaosImpactSettings::IsActionKey(GetOwningPlayer(), EChaosImpactAction::Pause, Key)
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
		&& (Key == EKeys::Escape
			|| ChaosImpactSettings::ToMenuKey(Key, InKeyEvent.GetInputDeviceId().GetId()) == EKeys::Gamepad_FaceButton_Right))
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

FReply UChaosImpactMenuWidget::NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent)
{
	// Letters typed for a nickname (the keys themselves are handled before this).
	if (IsSettingsActive() && SettingsScreen->HandleCharacter(InCharEvent.GetCharacter()))
	{
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen()
		&& CharacterSelect->HandleCharacter(InCharEvent.GetCharacter()))
	{
		return FReply::Handled();
	}
	return Super::NativeOnKeyChar(InGeometry, InCharEvent);
}

FReply UChaosImpactMenuWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsSettingsActive())
	{
		SettingsScreen->HandleWheel(InMouseEvent.GetWheelDelta());
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UChaosImpactMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsSettingsActive())
	{
		SettingsScreen->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen())
	{
		CharacterSelect->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	}
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
	if (IsSettingsActive())
	{
		SettingsScreen->HandleMouseDown(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()),
			InMouseEvent.GetEffectingButton());
		return FReply::Handled().SetUserFocus(TakeWidget());
	}
	if (Screen == EChaosImpactScreen::MatchEnd && Entries.IsEmpty())
	{
		GoOnFromPodium();
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect)
	{
		const float Scale = DesignScale(InGeometry);
		const FVector2D Offset = (InGeometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
		CharacterSelect->HandleClick((InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()) - Offset) / Scale);
		return FReply::Handled();
	}
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
