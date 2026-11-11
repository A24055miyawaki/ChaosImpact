#include "ChaosImpactMenuWidget.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/GameViewportClient.h"
#include "ChaosImpactPaint.h"

#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactTitleDemo.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpact.h"
#include "ChaosImpactColourEdit.h"
#include "ChaosImpactDojoMenu.h"
#include "ChaosImpactModeMenu.h"
#include "ChaosImpactDojoLessons.h"
#include "ChaosImpactDojoVideo.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactLocalTourney.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactCharacterPreview.h"
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
		// Player entry hears every device: whichever presses first takes the next place.
		if (Widget->GetScreen() == EChaosImpactScreen::ControllerAssignment)
		{
			return true;
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
		// (Player entry hears every device, the mouse included.)
		if (Widget && Widget->GetScreen() == EChaosImpactScreen::ControllerAssignment)
		{
			return true;
		}
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
		return !Controller || !Controller->IsTrainingMode() || !Controller->IsPrimaryUsingGamepad();
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
		const FGeometry Card = MakeSkewed(Design, 1100.0f + (1.0f - In) * 90.0f, 196.0f, W, H, -0.06f);
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
			Flat.Box(-1400.0f, Band * 50.0f, 4400.0f, 51.0f, FMath::Lerp(
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
		S.Box(-2000.0f, -40.0f, 2280.0f, 980.0f, FLinearColor(0.0f, 0.10f, 0.40f, 0.62f));
		S.Box(280.0f, -40.0f, 11.0f, 980.0f, WithAlpha(Ice, 0.9f));
		S.Box(305.0f, -40.0f, 3.0f, 980.0f, WithAlpha(Ice, 0.35f));
		S.Box(1340.0f, -40.0f, 2000.0f, 980.0f, FLinearColor(0.40f, 0.0f, 0.03f, 0.62f));
		S.Box(1329.0f, -40.0f, 11.0f, 980.0f, WithAlpha(Fire, 0.9f));
		S.Box(1312.0f, -40.0f, 3.0f, 980.0f, WithAlpha(Fire, 0.35f));
		for (int32 Stripe = 0; Stripe < 24; ++Stripe)
		{
			const float X = FMath::Fmod(Stripe * 283.0f + Time * (70.0f + Stripe * 11.0f), 3800.0f) - 1100.0f;
			S.Box(X, -40.0f, 1.5f + Stripe % 3, 980.0f,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.016f + (Stripe % 4) * 0.007f));
		}
		Flat.Box(-1400.0f, 0.0f, 4400.0f, 54.0f, WithAlpha(Ink, 0.55f));
		Flat.Box(-1400.0f, 846.0f, 4400.0f, 54.0f, WithAlpha(Ink, 0.65f));
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
		// (Smaller when the label would run past the bar's chevron.)
		const float Size = ChaosImpactPaint::HudFit(Label, H >= 80.0f ? 32.0f : 27.0f, W - 38.0f - 60.0f);
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

	/**
	 * The pause menu's item: a slanted row down its panel, numbered; the one chosen fills with its colour, slides out
	 * and carries a chevron.
	 */
	void PaintPauseItem(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
		const FSlateRect& Rect, const FString& Label, const FLinearColor& Accent, const float Blend,
		const bool bPressed, const bool bDisabled, const float Alpha, const float Time, const int32 Number)
	{
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = bDisabled ? 0.0f : EaseOut(Blend);
		const FGeometry Row = MakeSkewed(Design, Rect.Left + 22.0f * B, Rect.Top, W, H, -0.22f, bPressed ? 0.97f : 1.0f);
		const ChaosImpactPaint::FPainter P{Row, Elements, Layer, Alpha * (bDisabled ? 0.5f : 1.0f)};
		const ChaosImpactPaint::FPainter Up = ChaosImpactPaint::HudAbove(P, 1);
		const ChaosImpactPaint::FPainter Top = ChaosImpactPaint::HudAbove(P, 2);
		const TArray<FVector2D> Shape{FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)};
		// Resting: a dark glassy row with its colour's tick; chosen: its colour, light at the top.
		P.Box(6.0f, 7.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f * (0.4f + 0.6f * B)));
		ChaosImpactPaint::HudPoly(P, Shape, ChaosImpactPaint::MakeFill(FLinearColor(0.05f, 0.07f, 0.12f, 0.78f), FLinearColor(0.012f, 0.018f, 0.035f, 0.82f)));
		if (B > 0.01f)
		{
			FLinearColor Deep = Accent * 0.55f;
			Deep.A = 1.0f;
			ChaosImpactPaint::HudPoly(ChaosImpactPaint::FPainter{Row, Elements, Layer, Alpha * B}, {FVector2D(0.0, 0.0), FVector2D(W * B, 0.0),
				FVector2D(W * B, H), FVector2D(0.0, H)}, ChaosImpactPaint::MakeFill(FMath::Lerp(Accent, Paper, 0.18f), Deep));
			Up.Box(0.0f, 0.0f, W * B, 2.0f, WithAlpha(Paper, 0.6f * B));
		}
		Up.Box(0.0f, 0.0f, 7.0f, H, bDisabled ? WithAlpha(Muted, 0.5f) : Accent);
		Up.Box(0.0f, H - 1.0f, W, 1.0f, WithAlpha(Paper, 0.08f));
		// The number, then the label.
		Top.Text(FString::Printf(TEXT("%02d"), Number), 30.0f, H * 0.5f - 13.0f, 18.0f,
			B > 0.5f ? WithAlpha(Paper, 0.85f) : WithAlpha(Accent, 0.9f), ChaosImpactPaint::ETextAlign::Left, TEXT("BlackItalic"));
		const float Size = ChaosImpactPaint::HudFit(Label, H >= 58.0f ? 29.0f : H >= 48.0f ? 25.0f : 21.0f, W - 74.0f - 56.0f);
		if (B > 0.5f)
		{
			ChaosImpactPaint::HudText3D(Top, Label, 74.0f, H * 0.5f - Size * 0.72f, Size, Paper, ChaosImpactPaint::ETextAlign::Left,
				TEXT("Black"), 2.0f);
		}
		else
		{
			Top.Text(Label, 74.0f, H * 0.5f - Size * 0.72f, Size, bDisabled ? Muted : WithAlpha(Paper, 0.92f), ChaosImpactPaint::ETextAlign::Left,
				TEXT("Black"));
		}
		if (B > 0.05f)
		{
			// A chevron nudging the way it goes, and a breathing rim.
			const float CX = W - 34.0f + 5.0f * FMath::Sin(Time * 6.0f) * B;
			const FLinearColor Mark = WithAlpha(Paper, B);
			Top.Line(FVector2D(CX - 11.0f, H * 0.5f - 11.0f), FVector2D(CX, H * 0.5f), Mark, 4.0f);
			Top.Line(FVector2D(CX, H * 0.5f), FVector2D(CX - 11.0f, H * 0.5f + 11.0f), Mark, 4.0f);
			Top.Outline(-4.0f, -4.0f, W + 8.0f, H + 8.0f, WithAlpha(Paper, B * (0.35f + 0.25f * FMath::Sin(Time * 5.0f))), 2.0f);
		}
	}

	/**
	 * A setting on the rules page: its name on the left, its value in a box between ◀ ▶ (they nudge while it is chosen,
	 * to show it changes sideways). A fixed one shows 固定 instead of the arrows.
	 */
	void PaintRuleRow(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FSlateRect& Rect,
		const FString& Label, const FString& Value, const FLinearColor& Accent, const float Blend, const bool bPressed, const bool bDisabled,
		const bool bFixed, const float Alpha, const float Time)
	{
		namespace CP = ChaosImpactPaint;
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = bDisabled ? 0.0f : EaseOut(Blend);
		const FGeometry Row = MakeSkewed(Design, Rect.Left + 14.0f * B, Rect.Top, W, H, -0.12f, bPressed ? 0.98f : 1.0f);
		const CP::FPainter P{Row, Elements, Layer, Alpha * (bDisabled && !bFixed ? 0.55f : 1.0f)};
		const CP::FPainter Up = CP::HudAbove(P, 1);
		const CP::FPainter Top = CP::HudAbove(P, 2);
		P.Box(8.0f, 9.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		FLinearColor Lit = Accent * (0.16f + 0.2f * B);
		Lit.A = 0.96f;
		CP::HudPoly(P, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)},
			CP::MakeFill(FMath::Lerp(FLinearColor(0.04f, 0.055f, 0.1f, 0.94f), Lit, B), FLinearColor(0.008f, 0.012f, 0.026f, 0.96f)));
		Up.Box(0.0f, 0.0f, 8.0f, H, bDisabled && !bFixed ? WithAlpha(Muted, 0.5f) : Accent);
		Up.Box(0.0f, 0.0f, W, 1.5f, WithAlpha(Paper, 0.12f + 0.3f * B));
		// The name.
		Top.Text(Label, 32.0f, H * 0.5f - 15.0f, CP::HudFit(Label, 22.0f, W * 0.3f - 40.0f), B > 0.5f ? Paper : WithAlpha(Paper, 0.75f),
			CP::ETextAlign::Left, TEXT("Black"));
		// The value in its box, between the arrows.
		const float BoxL = W * 0.3f;
		const float BoxW = W - BoxL - 24.0f;
		CP::RoundedBox(Up, BoxL, 10.0f, BoxW, H - 20.0f, 10.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		const float Mid = BoxL + BoxW * 0.5f;
		const float ValueRoom = BoxW - 140.0f;
		const float ValueSize = CP::HudFit(Value, 30.0f, ValueRoom, TEXT("Black"));
		CP::HudText3D(Top, Value, Mid, H * 0.5f - ValueSize * 0.72f, ValueSize, bDisabled && !bFixed ? Muted : Paper, CP::ETextAlign::Center,
			TEXT("Black"), 2.0f);
		if (bFixed)
		{
			const float TagW = CP::HudMeasure(TEXT("固定"), 14.0f, TEXT("Black")) + 20.0f;
			CP::RoundedBox(Top, BoxL + BoxW - TagW - 12.0f, H * 0.5f - 12.0f, TagW, 24.0f, 12.0f, WithAlpha(Paper, 0.16f));
			CP::HudAbove(Top, 1).Text(TEXT("固定"), BoxL + BoxW - TagW * 0.5f - 12.0f, H * 0.5f - 10.0f, 14.0f, WithAlpha(Paper, 0.8f),
				CP::ETextAlign::Center, TEXT("Black"));
		}
		else if (!bDisabled)
		{
			const float Nudge = 4.0f * FMath::Sin(Time * 6.0f) * B;
			const FLinearColor Arrow = FMath::Lerp(WithAlpha(Paper, 0.45f), Accent.Equals(Muted) ? Paper : FMath::Lerp(Accent, Paper, 0.3f), B);
			const float Y = H * 0.5f;
			const float LX = BoxL + 30.0f - Nudge;
			const float RX = BoxL + BoxW - 30.0f + Nudge;
			Top.Line(FVector2D(LX + 9.0f, Y - 13.0f), FVector2D(LX - 4.0f, Y), Arrow, 5.0f);
			Top.Line(FVector2D(LX - 4.0f, Y), FVector2D(LX + 9.0f, Y + 13.0f), Arrow, 5.0f);
			Top.Line(FVector2D(RX - 9.0f, Y - 13.0f), FVector2D(RX + 4.0f, Y), Arrow, 5.0f);
			Top.Line(FVector2D(RX + 4.0f, Y), FVector2D(RX - 9.0f, Y + 13.0f), Arrow, 5.0f);
		}
		if (B > 0.01f)
		{
			CP::HudAbove(P, 3).Outline(-4.0f, -4.0f, W + 8.0f, H + 8.0f, WithAlpha(Paper, B * (0.45f + 0.3f * FMath::Sin(Time * 5.0f))), 2.0f);
		}
	}

	/** A kind of match in the rules screen's list: its emblem, its name, a line on how it is won; the one in use marked. */
	void PaintModeEmblem(const ChaosImpactPaint::FPainter& Icon, const FChaosImpactMatchRules& Rules, const FVector2D& At, const float Unit,
		const FLinearColor& Mark, const float Time);

	void PaintModeTile(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FSlateRect& Rect, const int32 Mode,
		const FString& Name, const FLinearColor& Accent, const float Blend, const bool bPressed, const bool bDisabled, const bool bCurrent,
		const FString& Blocker, const float Alpha, const float Time)
	{
		namespace CP = ChaosImpactPaint;
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = bDisabled ? 0.0f : EaseOut(Blend);
		const FGeometry Card = MakeSkewed(Design, Rect.Left, Rect.Top - 8.0f * B, W, H, -0.1f, (1.0f + 0.03f * B) * (bPressed ? 0.97f : 1.0f));
		const CP::FPainter P{Card, Elements, Layer, Alpha * (bDisabled ? 0.45f : 1.0f)};
		const CP::FPainter Up = CP::HudAbove(P, 1);
		const CP::FPainter Top = CP::HudAbove(P, 3);
		FLinearColor Deep = Accent * (0.16f + 0.26f * B);
		Deep.A = 0.97f;
		P.Box(10.0f, 12.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		CP::HudPoly(P, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)},
			CP::MakeFill(Deep, FLinearColor(0.008f, 0.01f, 0.025f, 0.97f)));
		CP::HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, 5.0), FVector2D(0.0, 5.0)}, CP::MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
		Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.1f), 1.0f);
		FChaosImpactMatchRules Kind;
		AChaosImpactPlayerController::ApplyMatchMode(Kind, Mode);
		PaintModeEmblem(CP::HudAbove(P, 2), Kind, FVector2D(W - 62.0f, H * 0.5f), 42.0f, WithAlpha(FMath::Lerp(Accent, Paper, 0.3f), 0.45f + 0.5f * B),
			Time * (0.4f + 0.6f * B));
		const float NameSize = CP::HudFit(Name, 34.0f, W - 130.0f);
		CP::HudText3D(Top, Name, 20.0f, 30.0f + (34.0f - NameSize) * 0.6f, NameSize, Paper, CP::ETextAlign::Left, TEXT("Black"), 2.0f);
		const FString Line = !Blocker.IsEmpty() ? Blocker
			: Mode == 0 ? TEXT("ポイントを一番かせいだ人の勝ち")
			: Mode <= 3 ? TEXT("チームの合計ポイントで勝負")
			: Mode == 4 ? TEXT("巨大球を相手のゴールへ押し込め")
			: Mode == 5 ? TEXT("3回当てて倒す・2本先取")
			: Mode == 6 ? TEXT("最後の1人になれば勝ち")
			: Mode <= 9 ? TEXT("最後まで生き残ったチームの勝ち")
			: Mode == AChaosImpactPlayerController::TournamentAllAtOnceChoice ? TEXT("全試合いっしょに・待ち時間は練習や観戦")
			: TEXT("1試合ずつ順番に・みんなで観戦");
		Top.Text(Line, 21.0f, H - 40.0f, CP::HudFit(Line, 16.0f, W - 42.0f, TEXT("Bold")), Blocker.IsEmpty() ? WithAlpha(Paper, 0.75f) : Fire,
			CP::ETextAlign::Left, TEXT("Bold"));
		if (bCurrent)
		{
			const float TagW = CP::HudMeasure(TEXT("いまのルール"), 12.0f, TEXT("Black")) + 18.0f;
			CP::RoundedBox(Top, 18.0f, 10.0f, TagW, 20.0f, 10.0f, Gold);
			CP::HudAbove(Top, 1).Text(TEXT("いまのルール"), 18.0f + TagW * 0.5f, 11.0f, 12.0f, Ink, CP::ETextAlign::Center, TEXT("Black"));
		}
		if (B > 0.01f)
		{
			CP::HudAbove(P, 4).Outline(-5.0f, -5.0f, W + 10.0f, H + 10.0f, WithAlpha(Paper, B * (0.6f + 0.3f * FMath::Sin(Time * 5.0f))), 3.0f);
		}
	}

	/** The rules page's emblem for a kind of match (everyone for themselves, teams, 1 v 1, battle royale, tournament). */
	void PaintModeEmblem(const ChaosImpactPaint::FPainter& Icon, const FChaosImpactMatchRules& Rules, const FVector2D& At, const float Unit,
		const FLinearColor& Mark, const float Time)
	{
		namespace CP = ChaosImpactPaint;
		if (Rules.bTournament)
		{
			const float L = At.X - Unit * 0.9f;
			const float M = At.X - Unit * 0.2f;
			const float R = At.X + Unit * 0.5f;
			for (int32 Leaf = 0; Leaf < 4; ++Leaf)
			{
				const float Y = At.Y - Unit * 0.75f + Leaf * Unit * 0.5f;
				Icon.Line(FVector2D(L, Y), FVector2D(M, Y), Mark, 5.0f);
			}
			for (int32 Pair = 0; Pair < 2; ++Pair)
			{
				const float Y0 = At.Y - Unit * 0.75f + Pair * Unit;
				Icon.Line(FVector2D(M, Y0), FVector2D(M, Y0 + Unit * 0.5f), Mark, 5.0f);
				Icon.Line(FVector2D(M, Y0 + Unit * 0.25f), FVector2D(R, Y0 + Unit * 0.25f), Mark, 5.0f);
			}
			Icon.Line(FVector2D(R, At.Y - Unit * 0.5f), FVector2D(R, At.Y + Unit * 0.5f), Mark, 5.0f);
			Icon.Line(FVector2D(R, At.Y), FVector2D(R + Unit * 0.45f, At.Y), Gold, 6.0f);
			Icon.Disc(FVector2D(R + Unit * 0.6f, At.Y), Unit * 0.17f, Gold);
		}
		else if (Rules.bDuel)
		{
			const float Bob = FMath::Sin(Time * 4.0f) * 4.0f;
			Icon.Disc(At + FVector2D(-Unit * 0.62f, Bob), Unit * 0.34f, ChaosImpactMatch::GetTeamColor(1));
			Icon.Disc(At + FVector2D(Unit * 0.62f, -Bob), Unit * 0.34f, ChaosImpactMatch::GetTeamColor(0));
			Icon.Line(At + FVector2D(-Unit * 0.14f, -Unit * 0.42f), At + FVector2D(Unit * 0.1f, -Unit * 0.02f), Mark, 6.0f);
			Icon.Line(At + FVector2D(Unit * 0.1f, -Unit * 0.02f), At + FVector2D(-Unit * 0.1f, Unit * 0.06f), Mark, 6.0f);
			Icon.Line(At + FVector2D(-Unit * 0.1f, Unit * 0.06f), At + FVector2D(Unit * 0.14f, Unit * 0.46f), Mark, 6.0f);
		}
		else if (Rules.bBattleRoyale)
		{
			// A ring closing on the last ones standing.
			Icon.Ring(At, Unit * (0.85f + 0.08f * FMath::Sin(Time * 2.0f)), Mark, 4.0f, 56);
			for (int32 Seat = 0; Seat < 5; ++Seat)
			{
				const float Angle = Time * 0.6f + Seat * UE_TWO_PI / 5.0f;
				Icon.Disc(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.45f, Unit * 0.11f, Seat == 0 ? Gold : WithAlpha(Mark, 0.5f));
			}
		}
		else if (Rules.IsTeamBattle())
		{
			// The teams' colours, a few each, facing.
			const int32 Teams = FMath::Clamp(Rules.TeamCount, 2, 4);
			for (int32 Team = 0; Team < Teams; ++Team)
			{
				const float Angle = Team * UE_TWO_PI / Teams + Time * 0.3f;
				const FVector2D Home = At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.55f;
				for (int32 Member = 0; Member < 3; ++Member)
				{
					const float Around = Angle + (Member - 1) * 0.5f;
					Icon.Disc(Home + FVector2D(FMath::Cos(Around), FMath::Sin(Around)) * Unit * 0.24f, Unit * 0.14f, ChaosImpactMatch::GetTeamColor(Team));
				}
			}
		}
		else
		{
			// Everyone for themselves: players round a ball.
			Icon.Disc(At, Unit * 0.26f, Mark);
			for (int32 Seat = 0; Seat < 8; ++Seat)
			{
				const float Angle = Time * 0.5f + Seat * UE_TWO_PI / 8.0f;
				Icon.Disc(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.75f, Unit * 0.11f, Mark);
			}
		}
	}

	/**
	 * The online menu's tile: a leaning card in its colour, an emblem for what it is (eight players round a ball, two
	 * facing, a bracket, a key, rising bars, signal bars), its name big in English and in Japanese, and a line on what
	 * it does. The one chosen lifts and lights.
	 */
	void PaintOnlineTile(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FSlateRect& Rect,
		const FString& Big, const FString& Name, const FString& Kind, const FLinearColor& Accent, const float Blend, const bool bPressed,
		const bool bDisabled, const float Alpha, const float Time)
	{
		namespace CP = ChaosImpactPaint;
		const float W = Rect.Right - Rect.Left;
		const float H = Rect.Bottom - Rect.Top;
		const float B = EaseOut(Blend);
		const FGeometry Card = MakeSkewed(Design, Rect.Left, Rect.Top - 10.0f * B, W, H, -0.12f, (1.0f + 0.02f * B) * (bPressed ? 0.97f : 1.0f));
		const float Fade = Alpha * (bDisabled ? 0.5f : 1.0f);
		const CP::FPainter P{Card, Elements, Layer, Fade};
		const CP::FPainter Up = CP::HudAbove(P, 1);
		const CP::FPainter Top = CP::HudAbove(P, 3);
		FLinearColor Deep = Accent * (0.18f + 0.22f * B);
		Deep.A = 0.97f;
		P.Box(14.0f, 16.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		CP::HudPoly(P, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)},
			CP::MakeFill(Deep, FLinearColor(0.008f, 0.01f, 0.025f, 0.97f)));
		// Speed streaks leaning with it, brighter once chosen.
		for (int32 Band = 0; Band < 3; ++Band)
		{
			Up.Box(W * 0.56f + Band * 52.0f, 0.0f, 20.0f - Band * 5.0f, H, WithAlpha(Accent, 0.05f + 0.1f * B));
		}
		CP::HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, 6.0), FVector2D(0.0, 6.0)},
			CP::MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
		Up.Box(0.0f, H - 10.0f, W, 10.0f, Accent);
		Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.1f), 1.0f);

		// The emblem, toward the right.
		const float Unit = FMath::Min(H * 0.36f, 96.0f);
		// A narrow tile keeps its words wide: its emblem sits faintly behind them, low on the right.
		const bool bNarrow = W < 440.0f;
		const FVector2D At(W - Unit * 1.25f, bNarrow ? H * 0.56f : H * 0.42f);
		const FLinearColor Mark = WithAlpha(FMath::Lerp(Accent, Paper, 0.35f), bNarrow ? 0.18f + 0.22f * B : 0.35f + 0.55f * B);
		const CP::FPainter Icon = CP::HudAbove(P, 2);
		const float Spin = Time * (0.4f + 0.8f * B);
		if (Kind == TEXT("random"))
		{
			Icon.Disc(At, Unit * 0.28f, Mark);
			for (int32 Seat = 0; Seat < 8; ++Seat)
			{
				const float Angle = Spin + Seat * UE_TWO_PI / 8.0f;
				Icon.Disc(At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.78f, Unit * 0.12f, Mark);
			}
		}
		else if (Kind == TEXT("duel"))
		{
			const float Bob = FMath::Sin(Time * 4.0f) * 4.0f * B;
			Icon.Disc(At + FVector2D(-Unit * 0.62f, Bob), Unit * 0.34f, Mark);
			Icon.Disc(At + FVector2D(Unit * 0.62f, -Bob), Unit * 0.34f, Mark);
			Icon.Line(At + FVector2D(-Unit * 0.14f, -Unit * 0.42f), At + FVector2D(Unit * 0.1f, -Unit * 0.02f), Mark, 6.0f);
			Icon.Line(At + FVector2D(Unit * 0.1f, -Unit * 0.02f), At + FVector2D(-Unit * 0.1f, Unit * 0.06f), Mark, 6.0f);
			Icon.Line(At + FVector2D(-Unit * 0.1f, Unit * 0.06f), At + FVector2D(Unit * 0.14f, Unit * 0.46f), Mark, 6.0f);
		}
		else if (Kind == TEXT("tourney"))
		{
			// Four leaves to one: the bracket, its winner's line lit.
			const float L = At.X - Unit * 0.9f;
			const float M = At.X - Unit * 0.2f;
			const float R = At.X + Unit * 0.5f;
			for (int32 Leaf = 0; Leaf < 4; ++Leaf)
			{
				const float Y = At.Y - Unit * 0.75f + Leaf * Unit * 0.5f;
				Icon.Line(FVector2D(L, Y), FVector2D(M, Y), Mark, 4.0f);
			}
			for (int32 Pair = 0; Pair < 2; ++Pair)
			{
				const float Y0 = At.Y - Unit * 0.75f + Pair * Unit;
				Icon.Line(FVector2D(M, Y0), FVector2D(M, Y0 + Unit * 0.5f), Mark, 4.0f);
				Icon.Line(FVector2D(M, Y0 + Unit * 0.25f), FVector2D(R, Y0 + Unit * 0.25f), Mark, 4.0f);
			}
			Icon.Line(FVector2D(R, At.Y - Unit * 0.5f), FVector2D(R, At.Y + Unit * 0.5f), Mark, 4.0f);
			Icon.Line(FVector2D(R, At.Y), FVector2D(R + Unit * 0.5f, At.Y), WithAlpha(Gold, 0.5f + 0.5f * B), 5.0f);
			Icon.Disc(FVector2D(R + Unit * 0.62f, At.Y), Unit * 0.16f, WithAlpha(Gold, 0.5f + 0.5f * B));
		}
		else if (Kind == TEXT("private"))
		{
			// A key.
			Icon.Ring(At + FVector2D(-Unit * 0.5f, 0.0), Unit * 0.34f, Mark, 8.0f, 40);
			Icon.Line(At + FVector2D(-Unit * 0.16f, 0.0), At + FVector2D(Unit * 0.9f, 0.0), Mark, 9.0f);
			Icon.Line(At + FVector2D(Unit * 0.58f, 0.0), At + FVector2D(Unit * 0.58f, Unit * 0.3f), Mark, 8.0f);
			Icon.Line(At + FVector2D(Unit * 0.82f, 0.0), At + FVector2D(Unit * 0.82f, Unit * 0.22f), Mark, 8.0f);
		}
		else if (Kind == TEXT("record"))
		{
			// Rising bars and the line over them.
			for (int32 Bar = 0; Bar < 4; ++Bar)
			{
				const float BarH = Unit * (0.4f + Bar * 0.3f) * (0.85f + 0.15f * FMath::Sin(Time * 3.0f + Bar) * B);
				CP::RoundedBox(Icon, At.X - Unit * 0.9f + Bar * Unit * 0.48f, At.Y + Unit * 0.7f - BarH, Unit * 0.3f, BarH, 3.0f, Mark);
			}
		}
		else if (Kind == TEXT("linecheck"))
		{
			// Signal bars, and waves going out from them.
			for (int32 Bar = 0; Bar < 4; ++Bar)
			{
				const float BarH = Unit * (0.3f + Bar * 0.25f);
				CP::RoundedBox(Icon, At.X - Unit * 0.8f + Bar * Unit * 0.42f, At.Y + Unit * 0.6f - BarH, Unit * 0.26f, BarH, 3.0f, Mark);
			}
			const float Wave = FMath::Frac(Time * 0.9f);
			Icon.Arc(At + FVector2D(Unit * 0.7f, -Unit * 0.3f), Unit * (0.2f + 0.4f * Wave), -60.0f, 60.0f, WithAlpha(Mark, Mark.A * (1.0f - Wave)), 4.0f);
		}

		// Its name, big in English, then in Japanese, then what it does.
		const float TextW = bNarrow ? W - 70.0f : W - Unit * 2.4f - 30.0f;
		const float BigSize = CP::HudFit(Big, H >= 240.0f ? 52.0f : 40.0f, TextW, TEXT("BlackItalic"));
		CP::HudText3D(Top, Big, 34.0f, 22.0f, BigSize, FMath::Lerp(WithAlpha(Paper, 0.85f), Paper, B), CP::ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
		const float NameSize = CP::HudFit(Name, H >= 240.0f ? 30.0f : 26.0f, TextW);
		Top.Text(Name, 36.0f, 30.0f + BigSize * 1.25f, NameSize, Paper, CP::ETextAlign::Left, TEXT("Black"));
		const FString Line = Kind == TEXT("random") ? TEXT("8人で3分間の個人戦。たりない分はCPU")
			: Kind == TEXT("duel") ? (bDisabled ? TEXT("1v1は1人で参加してください") : TEXT("1対1・2本先取のデュエル"))
			: Kind == TEXT("tourney") ? TEXT("大会にエントリー・大会をひらく")
			: Kind == TEXT("private") ? TEXT("あいことばで友だちと部屋をつくる・入る")
			: Kind == TEXT("record") ? TEXT("これまでの成績")
			: Kind == TEXT("linecheck") ? TEXT("中継サーバーまでの回線をはかる") : TEXT("");
		if (!Line.IsEmpty() && H >= 150.0f)
		{
			Top.Text(Line, 37.0f, H - 50.0f, CP::HudFit(Line, 18.0f, W - 60.0f, TEXT("Bold")), WithAlpha(Paper, 0.6f + 0.3f * B),
				CP::ETextAlign::Left, TEXT("Bold"));
		}
		if (B > 0.01f)
		{
			CP::HudAbove(P, 4).Outline(-6.0f, -6.0f, W + 12.0f, H + 12.0f, WithAlpha(Paper, B * (0.6f + 0.3f * FMath::Sin(Time * 5.0f))), 3.0f);
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

		// (Both fit across the card.)
		const float BigFit = ChaosImpactPaint::HudFit(Big, BigSize, W - 80.0f);
		P.Text(Big, 40.0f, H - BigSize * 1.3f - 110.0f + (BigSize - BigFit) * 0.6f, BigFit, WithAlpha(Paper, 0.7f + 0.3f * B),
			ETextAlign::Left, TEXT("Black"), 3.0f * B, Ink);
		P.Text(Sub, 46.0f, H - 84.0f, ChaosImpactPaint::HudFit(Sub, 30.0f, W - 92.0f), WithAlpha(Paper, 0.55f + 0.45f * B));
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
	ColourEdit = NewObject<UChaosImpactColourEdit>(this);
	DojoMenu = NewObject<UChaosImpactDojoMenu>(this);
	TourneyEntry = NewObject<UChaosImpactTourneyEntry>(this);
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
	if (ColourEdit)
	{
		ColourEdit->Close();
	}
	if (DojoMenu)
	{
		DojoMenu->Close();
	}
	if (TourneyEntry)
	{
		TourneyEntry->Close();
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && ColourEdit->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return ColourEdit->HandleKeyDown(InKeyEvent);
	}
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite && GetVisibility() != ESlateVisibility::Collapsed)
	{
		return HandleDojoInviteKey(InKeyEvent);
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu && DojoMenu->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return DojoMenu->HandleKeyDown(InKeyEvent);
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && TourneyEntry->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return TourneyEntry->HandleKeyDown(InKeyEvent);
	}
	if (Screen == EChaosImpactScreen::TourneySeat && GetVisibility() != ESlateVisibility::Collapsed)
	{
		// Every device's button takes (or gives back) a place; Escape, before the tournament begins, goes back.
		if (!InKeyEvent.IsRepeat() && !InKeyEvent.GetKey().IsMouseButton() && !InKeyEvent.GetKey().IsAnalog())
		{
			if (AChaosImpactPlayerController* Seats = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
			{
				if (InKeyEvent.GetKey() == EKeys::Escape)
				{
					Seats->CancelTourneySeats();
				}
				else
				{
					Seats->PressTourneySeat(!InKeyEvent.GetKey().IsGamepadKey(), InKeyEvent.GetInputDeviceId().GetId());
				}
			}
		}
		return true;
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && ColourEdit->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return ColourEdit->HandleAnalog(InAnalogEvent);
	}
	// (The 訓練場 invitation over the mode select: the stick moves nothing under it.)
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite)
	{
		return true;
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu && DojoMenu->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return DojoMenu->HandleAnalog(InAnalogEvent);
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && TourneyEntry->IsOpen()
		&& GetVisibility() != ESlateVisibility::Collapsed)
	{
		return TourneyEntry->HandleAnalog(InAnalogEvent);
	}
	if (Screen == EChaosImpactScreen::TourneySeat)
	{
		return true;
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
	LastLiveEntriesKey.Reset();
	if (CharacterSelect && NewScreen != EChaosImpactScreen::CharacterSelect)
	{
		CharacterSelect->Close();
	}
	if (ColourEdit && NewScreen != EChaosImpactScreen::ColourEdit)
	{
		ColourEdit->Close();
	}
	if (DojoMenu && NewScreen != EChaosImpactScreen::DojoSelect)
	{
		DojoMenu->Close();
	}
	if (TourneyEntry && NewScreen != EChaosImpactScreen::TourneyEntry)
	{
		TourneyEntry->Close();
	}
	if (NewScreen != EChaosImpactScreen::DuelFound && NewScreen != EChaosImpactScreen::TourneySeat)
	{
		CloseDuelPreviews();
	}
	// The line check measures as its page comes up.
	if (NewScreen == EChaosImpactScreen::LineCheck && PreviousScreen != EChaosImpactScreen::LineCheck)
	{
		if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this); Sessions && !Sessions->GetLineCheck().bRunning)
		{
			Sessions->StartLineCheck();
		}
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && !ColourEdit->IsOpen())
	{
		ColourEdit->Open(Cast<AChaosImpactPlayerController>(GetOwningPlayer()));
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu && !DojoMenu->IsOpen())
	{
		DojoMenu->Open(Cast<AChaosImpactPlayerController>(GetOwningPlayer()));
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && !TourneyEntry->IsOpen())
	{
		TourneyEntry->Open(Cast<AChaosImpactPlayerController>(GetOwningPlayer()));
	}
	SelectedIndex = 0;
	PressedIndex = INDEX_NONE;
	ArmedIndex = INDEX_NONE;
	PauseConfirm.Reset();
	bModePicker = false;
	bBallSwitch = false;
	ScreenStartedAt = FPlatformTime::Seconds();
	AnimationSeconds = 0.0f;
	BuildEntries();
	SelectBlend.Init(0.0f, Entries.Num());

	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	// 「訓練場」で あそんでみよう！: the first mode select of a run, while some lesson is still not cleared.
	{
		static bool bInvitedThisRun = false;
		bDojoInvite = false;
		if (Screen == EChaosImpactScreen::ModeSelect && !bInvitedThisRun && Controller && !Controller->IsOnlineRoom()
			&& !FParse::Param(FCommandLine::Get(), TEXT("CINoDojoInvite")))
		{
			int32 Lessons = 0;
			int32 Cleared = 0;
			const TArray<FChaosImpactDojoCategory>& Groups = ChaosImpactDojo::GetCategories();
			for (int32 Group = 0; Group < Groups.Num(); ++Group)
			{
				Lessons += Groups[Group].Lessons.Num();
				Cleared += ChaosImpactDojo::CountCleared(Group);
			}
			if (Cleared < Lessons)
			{
				bInvitedThisRun = true;
				bDojoInvite = true;
				DojoInviteAt = FPlatformTime::Seconds();
				DojoInviteChoice = 0;
				if (!DojoInviteVideo)
				{
					DojoInviteVideo = NewObject<UChaosImpactDojoVideo>(this);
				}
				if (!DojoInviteMenu)
				{
					DojoInviteMenu = NewObject<UChaosImpactDojoMenu>(this);
				}
				DojoInviteMenu->SetDemo(DojoInviteVideo);
				DojoInviteMenu->Open(const_cast<AChaosImpactPlayerController*>(Controller));
				DojoInviteMenu->SetTab(0);
				DojoInviteStepAt = FPlatformTime::Seconds() + 1.6;
				DojoInviteSteps = 0;
			}
		}
	}
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
			NameInput->SetText(FText::FromString(RoomController && RoomController->IsEditingTourneyTitle() ? RoomController->GetPendingTourney().Title
				: RoomController && RoomController->IsRenamingRoom() && Room ? Room->RoomName : Sessions ? Sessions->GetDefaultRoomName() : FString()));
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

FString UChaosImpactMenuWidget::GetLiveEntriesKey() const
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (Screen == EChaosImpactScreen::DuelFound && Sessions)
	{
		return FString::Printf(TEXT("%d%d%d"), Sessions->IsDuelOffered(), Sessions->GetDuelOffer().bAccepted, Sessions->IsQueueMatched());
	}
	if (Screen == EChaosImpactScreen::LineCheck && Sessions)
	{
		return Sessions->GetLineCheck().bRunning ? TEXT("run") : TEXT("done");
	}
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Screen == EChaosImpactScreen::MatchEnd && Controller && Room && Room->Matchmade == EChaosImpactMatchmade::Duel)
	{
		bool bAsked = false;
		const bool bHere = Controller->IsRematchOpponentHere(bAsked);
		return FString::Printf(TEXT("%d%d%d"), bHere, bAsked, Controller->HasRequestedRematch());
	}
	return TEXT("-");
}

namespace
{
	/** ○ × △ for the last results ("W" / "L" / "D"), oldest first, in a row of small tiles. */
	void PaintDuelRecent(const FMenuPainter& P, const TArray<FString>& Recent, const float X, const float Y, const float Tile)
	{
		for (int32 Index = 0; Index < Recent.Num(); ++Index)
		{
			const bool bWin = Recent[Index] == TEXT("W");
			const bool bDraw = Recent[Index] == TEXT("D");
			const float Left = X + Index * (Tile + 6.0f);
			P.Box(Left, Y, Tile, Tile, bWin ? WithAlpha(Gold, 0.9f) : bDraw ? WithAlpha(Ice, 0.55f) : FLinearColor(0.12f, 0.14f, 0.2f, 1.0f));
			P.Text(bWin ? TEXT("○") : bDraw ? TEXT("△") : TEXT("×"), Left + Tile * 0.5f, Y + Tile * 0.08f, Tile * 0.62f,
				bWin ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		}
	}

	FString DescribeDuelRecord(const FChaosImpactDuelRecord& Record)
	{
		return Record.Draws > 0 ? FString::Printf(TEXT("%d勝 %d敗 %d分"), Record.Wins, Record.Losses, Record.Draws)
			: FString::Printf(TEXT("%d勝 %d敗"), Record.Wins, Record.Losses);
	}

	/** Connection bars (as the room list's): 4 at 40 ms or less, then 80, 150, worse. */
	void PaintSignal(const FMenuPainter& P, const int32 PingMs, const float X, const float Bottom, const float Scale)
	{
		const int32 Level = PingMs <= 40 ? 4 : PingMs <= 80 ? 3 : PingMs <= 150 ? 2 : 1;
		const FLinearColor SignalColor = Level == 4 ? FLinearColor(0.2f, 0.95f, 0.45f)
			: Level == 3 ? FLinearColor(0.65f, 0.95f, 0.3f) : Level == 2 ? Gold : Fire;
		for (int32 Bar = 0; Bar < 4; ++Bar)
		{
			const float BarHeight = (12.0f + Bar * 11.0f) * Scale;
			P.Box(X + Bar * 16.0f * Scale, Bottom - BarHeight, 11.0f * Scale, BarHeight, Bar < Level ? SignalColor : WithAlpha(Paper, 0.16f));
		}
	}
}

void UChaosImpactMenuWidget::PaintOnlineRecord(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Time) const
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions)
	{
		return;
	}
	const FChaosImpactRecords& Records = Sessions->GetRecords();
	const float ChipIn = EaseOut((Time - 0.1f) / 0.3f);
	const FMenuPainter Chip{MakeSkewed(Design, 1070.0f + (1.0f - ChipIn) * 80.0f, 96.0f, 360.0f, 70.0f, -0.3f), Elements, Layer, ChipIn};
	Chip.Box(0.0f, 0.0f, 360.0f, 70.0f, Violet);
	Chip.Text(FString::Printf(TEXT("なまえ  %s"), *Sessions->GetPlayerName()), 180.0f, 14.0f, 30.0f, Paper, ETextAlign::Center);
	if (!Records.bKnown)
	{
		const float In = EaseOut((Time - 0.2f) / 0.3f);
		const FMenuPainter Card{MakeSkewed(Design, 380.0f, 320.0f, 840.0f, 200.0f, -0.08f), Elements, Layer + 1, In};
		Card.Box(0.0f, 0.0f, 840.0f, 200.0f, FLinearColor(0.015f, 0.02f, 0.04f, 0.94f));
		Card.Box(0.0f, 0.0f, 840.0f, 6.0f, Violet);
		Card.Text(Sessions->IsRecordsPending() ? TEXT("戦績を読みこんでいます") : TEXT("中継サーバーにつながりません"), 420.0f, 70.0f, 36.0f,
			Paper, ETextAlign::Center, TEXT("Black"));
		return;
	}
	struct FPanel
	{
		float X;
		const TCHAR* Title;
		FLinearColor Accent;
	};
	const FPanel Panels[] = {{150.0f, TEXT("ランダムマッチ 1v1"), Gold}, {830.0f, TEXT("ランダムマッチ 8人"), Fire}};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float In = EaseOut((Time - 0.14f - 0.06f * Index) / 0.35f);
		const FMenuPainter P{MakeSkewed(Design, Panels[Index].X + (1.0f - In) * 100.0f, 212.0f, 620.0f, 470.0f, -0.08f), Elements, Layer + 1, In};
		P.Box(12.0f, 14.0f, 620.0f, 470.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		P.Box(0.0f, 0.0f, 620.0f, 470.0f, FLinearColor(0.015f, 0.02f, 0.04f, 0.95f));
		P.Box(0.0f, 0.0f, 620.0f, 8.0f, Panels[Index].Accent);
		P.Text(Panels[Index].Title, 34.0f, 22.0f, 34.0f, Paper, ETextAlign::Left, TEXT("Black"));
		if (Index == 0)
		{
			const FChaosImpactDuelRecord& Duel = Records.Duel;
			if (Duel.GetGames() == 0)
			{
				P.Text(TEXT("まだ対戦していません"), 310.0f, 200.0f, 30.0f, Muted, ETextAlign::Center, TEXT("Bold"));
				continue;
			}
			P.Text(DescribeDuelRecord(Duel), 34.0f, 92.0f, 64.0f, Paper, ETextAlign::Left, TEXT("Black"), 3.0f, Ink);
			const int32 Rate = FMath::RoundToInt(100.0f * Duel.Wins / FMath::Max(1, Duel.GetGames()));
			P.Text(FString::Printf(TEXT("勝率  %d%%"), Rate), 36.0f, 196.0f, 32.0f, Gold, ETextAlign::Left, TEXT("Black"));
			P.Text(FString::Printf(TEXT("%d試合"), Duel.GetGames()), 586.0f, 202.0f, 26.0f, Muted, ETextAlign::Right, TEXT("Bold"));
			P.Box(34.0f, 262.0f, 552.0f, 2.0f, WithAlpha(Paper, 0.14f));
			P.Text(TEXT("さいきんの10試合"), 36.0f, 284.0f, 22.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			PaintDuelRecent(P, Duel.Recent, 36.0f, 330.0f, 46.0f);
		}
		else
		{
			if (Records.FfaGames == 0)
			{
				P.Text(TEXT("まだ対戦していません"), 310.0f, 200.0f, 30.0f, Muted, ETextAlign::Center, TEXT("Bold"));
				continue;
			}
			P.Text(FString::Printf(TEXT("1位  %d回"), Records.FfaWins), 34.0f, 92.0f, 64.0f, Paper, ETextAlign::Left, TEXT("Black"), 3.0f, Ink);
			P.Text(FString::Printf(TEXT("平均 %.1f位"), Records.FfaAverage), 586.0f, 118.0f, 32.0f, Paper, ETextAlign::Right, TEXT("Black"));
			P.Text(FString::Printf(TEXT("3位以内  %d回"), Records.FfaTop3), 36.0f, 196.0f, 32.0f, Fire, ETextAlign::Left, TEXT("Black"));
			P.Text(FString::Printf(TEXT("%d試合"), Records.FfaGames), 586.0f, 202.0f, 26.0f, Muted, ETextAlign::Right, TEXT("Bold"));
			P.Box(34.0f, 262.0f, 552.0f, 2.0f, WithAlpha(Paper, 0.14f));
			P.Text(TEXT("さいきんの10試合（順位）"), 36.0f, 284.0f, 22.0f, Muted, ETextAlign::Left, TEXT("Bold"));
			for (int32 Game = 0; Game < Records.FfaRecent.Num(); ++Game)
			{
				const int32 Place = Records.FfaRecent[Game].X;
				const FLinearColor Medal = Place == 1 ? Gold : Place == 2 ? FLinearColor(0.78f, 0.84f, 0.94f)
					: Place == 3 ? FLinearColor(0.86f, 0.52f, 0.26f) : FLinearColor(0.12f, 0.14f, 0.2f, 1.0f);
				const float Left = 36.0f + Game * 52.0f;
				P.Box(Left, 330.0f, 46.0f, 46.0f, Medal);
				P.Text(FString::FromInt(Place), Left + 23.0f, 334.0f, 28.0f, Place <= 3 ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
			}
		}
	}
}

void UChaosImpactMenuWidget::UpdateDuelPreviews(const float DeltaSeconds)
{
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (!Sessions || !World || !Sessions->IsDuelOffered())
	{
		return;
	}
	const FChaosImpactDuelOffer& Offer = Sessions->GetDuelOffer();
	const FString For = FString::Printf(TEXT("%s/%d/%d"), *Offer.MatchId, Offer.Character, Offer.Colour);
	if (DuelPreviewFor != For)
	{
		// This player (as their loadout and カスタムカラー show them) and the opponent, each on a stage of their own.
		CloseDuelPreviews();
		DuelPreviewFor = For;
		const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
		const FChaosImpactLoadout Own = Loadouts ? Loadouts->GetLoadout(0) : FChaosImpactLoadout();
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			AChaosImpactCharacterPreview* Preview = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
				FVector(Side * 1600.0f, -126000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
			if (!Preview)
			{
				continue;
			}
			if (Side == 0)
			{
				Preview->ShowLoadout(Own.Character, FMath::Max(Own.Colour, 0), Ice);
				if (const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(this))
				{
					Preview->ShowCustomColours(Colours->GetPresetParts(Own.Character, Own.ColourPreset));
				}
			}
			else
			{
				Preview->ShowLoadout(Offer.Character, FMath::Max(Offer.Colour, 0), Fire);
			}
			// Turned a little toward each other.
			Preview->SetFacing(Side == 0 ? -24.0f : 24.0f, 0.0f);
			FSlateBrush Brush;
			Brush.SetResourceObject(Preview->GetPicture());
			Brush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			DuelPreviews.Add(Preview);
			DuelPreviewBrushes.Add(Brush);
		}
	}
	// The training is paused under the card: the previews move and are filmed from here.
	for (AChaosImpactCharacterPreview* Preview : DuelPreviews)
	{
		if (IsValid(Preview))
		{
			Preview->Animate(DeltaSeconds);
			Preview->FilmNow();
		}
	}
}

void UChaosImpactMenuWidget::CloseDuelPreviews()
{
	for (AChaosImpactCharacterPreview* Preview : DuelPreviews)
	{
		if (IsValid(Preview))
		{
			Preview->Destroy();
		}
	}
	DuelPreviews.Reset();
	DuelPreviewBrushes.Reset();
	DuelPreviewFor.Reset();
}

void UChaosImpactMenuWidget::PaintDuelFound(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Time) const
{
	namespace CP = ChaosImpactPaint;
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions)
	{
		return;
	}
	const FChaosImpactDuelOffer& Offer = Sessions->GetDuelOffer();
	const bool bStarting = Sessions->IsQueueMatched();
	// This player: their name, character, pad or keyboard, and 1v1 record.
	const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(this);
	const FChaosImpactLoadout Own = Loadouts ? Loadouts->GetLoadout(0) : FChaosImpactLoadout();
	int32 KeyboardPlayer = INDEX_NONE;
	FParse::Value(*(TEXT("?") + Sessions->GetLocalSetupOptions()), TEXT("CIKeyboardPlayer="), KeyboardPlayer);
	struct FSide
	{
		const TCHAR* Label;
		FString Name;
		int32 Character;
		int32 Colour;
		bool bKeyboard;
		bool bKnownRecord;
		FChaosImpactDuelRecord Record;
		bool bAccepted;
		FLinearColor Tone;
	};
	const FSide Sides[] = {
		{TEXT("あなた"), Sessions->GetPlayerName(), Own.Character, FMath::Max(0, Own.Colour), KeyboardPlayer == 0,
			Sessions->GetRecords().bKnown, Sessions->GetRecords().Duel, Offer.bAccepted || bStarting, Ice},
		{TEXT("相手"), Offer.Name, Offer.Character, Offer.Colour, Offer.bKeyboard, true, Offer.Record,
			Offer.bOpponentAccepted || bStarting, Fire}};

	// The title: MATCH FOUND, slammed in, and what it means under it.
	{
		const float In = EaseOut(Time / 0.28f);
		const float Scale = FMath::Lerp(1.6f, 1.0f, In);
		const CP::FPainter Title{CP::MakeAnchor(Design, 800.0f, 92.0f, Scale), Elements, Layer + 2, In};
		CP::HudText3D(Title, TEXT("MATCH FOUND"), 0.0f, -62.0f, 74.0f, Gold, CP::ETextAlign::Center, TEXT("BlackItalic"), 5.0f);
		const CP::FPainter Under{Design, Elements, Layer + 2, EaseOut((Time - 0.2f) / 0.3f)};
		Under.Text(TEXT("対戦相手が見つかりました"), 800.0f, 140.0f, 22.0f, WithAlpha(Paper, 0.85f), CP::ETextAlign::Center, TEXT("Black"));
	}

	constexpr float CardW = 620.0f;
	constexpr float CardH = 540.0f;
	constexpr float CardTop = 184.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FSide& Side = Sides[Index];
		const bool bRight = Index == 1;
		const float In = EaseOut((Time - 0.1f - 0.08f * Index) / 0.38f);
		const float Slide = (1.0f - In) * (bRight ? 160.0f : -160.0f);
		const float Left = (bRight ? 1600.0f - 70.0f - CardW : 70.0f) + Slide;
		const FGeometry CardSpace = MakeSkewed(Design, Left, CardTop, CardW, CardH, -0.08f);
		const CP::FPainter Card{CardSpace, Elements, Layer, In};
		const CP::FPainter Up = CP::HudAbove(Card, 1);
		const CP::FPainter Top = CP::HudAbove(Card, 3);
		FLinearColor Deep = Side.Tone * 0.16f;
		Deep.A = 0.97f;
		// The card: a shadow, a body shading from the side's colour down to night, bands top and bottom.
		Card.Box(14.0f, 16.0f, CardW, CardH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		CP::HudPoly(Card, {FVector2D(0.0, 0.0), FVector2D(CardW, 0.0), FVector2D(CardW, CardH), FVector2D(0.0, CardH)},
			CP::MakeFill(Deep, FLinearColor(0.006f, 0.008f, 0.02f, 0.97f)));
		CP::HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(CardW, 0.0), FVector2D(CardW, 8.0), FVector2D(0.0, 8.0)},
			CP::MakeFill(FMath::Lerp(Side.Tone, Paper, 0.35f), Side.Tone));
		Up.Box(0.0f, CardH - 6.0f, CardW, 6.0f, Side.Tone);
		Up.Outline(0.0f, 0.0f, CardW, CardH, WithAlpha(Paper, 0.14f), 1.5f);
		// Light streaks behind the character.
		const float PicX = bRight ? CardW - 330.0f : 10.0f;
		for (int32 Band = 0; Band < 3; ++Band)
		{
			Up.Box(PicX + 60.0f + Band * 80.0f, 8.0f, 24.0f - Band * 6.0f, CardH - 14.0f, WithAlpha(Side.Tone, 0.07f));
		}
		// The character, standing in its half of the card: cut to the card's shape, its stage's edges faded into the card.
		if (DuelPreviewBrushes.IsValidIndex(Index) && DuelPreviewBrushes[Index].GetResourceObject())
		{
			const float PicW = 420.0f;
			const float PicH = PicW * AChaosImpactCharacterPreview::PictureHeight / AChaosImpactCharacterPreview::PictureWidth;
			const float PicLeft = bRight ? CardW - PicW + 40.0f : -40.0f;
			const float PicTop = CardH - PicH + 40.0f;
			Elements.PushClip(FSlateClippingZone(CardSpace));
			FSlateDrawElement::MakeBox(Elements, Layer + 2, CardSpace.ToPaintGeometry(FVector2f(PicW, PicH), FSlateLayoutTransform(FVector2f(PicLeft, PicTop))),
				&DuelPreviewBrushes[Index], ESlateDrawEffect::None, WithAlpha(FLinearColor::White, In));
			const CP::FPainter Fade{CardSpace, Elements, Layer + 2, In};
			const FLinearColor Night(0.006f, 0.008f, 0.02f, 1.0f);
			constexpr int32 Steps = 14;
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float A = FMath::Square(1.0f - static_cast<float>(Step) / Steps);
				// The inner edge (toward the words) and the floor.
				const float InnerX = bRight ? PicLeft + Step * 8.0f : PicLeft + PicW - (Step + 1) * 8.0f;
				Fade.Box(InnerX, 0.0f, 8.0f, CardH, WithAlpha(Night, A));
				Fade.Box(0.0f, CardH - (Step + 1) * 7.0f, CardW, 7.0f, WithAlpha(Night, 0.85f * A));
				Fade.Box(0.0f, Step * 6.0f, CardW, 6.0f, WithAlpha(Night, 0.6f * A));
			}
			// A breath of the side's colour over it, so it sits in the card.
			Fade.Box(PicLeft, PicTop, PicW, PicH, WithAlpha(Side.Tone, 0.07f));
			Elements.PopClip();
		}
		// The card's bands and rim over the character.
		const CP::FPainter Rim = CP::HudAbove(Card, 3);
		CP::HudPoly(Rim, {FVector2D(0.0, 0.0), FVector2D(CardW, 0.0), FVector2D(CardW, 8.0), FVector2D(0.0, 8.0)},
			CP::MakeFill(FMath::Lerp(Side.Tone, Paper, 0.35f), Side.Tone));
		Rim.Box(0.0f, CardH - 6.0f, CardW, 6.0f, Side.Tone);
		Rim.Outline(0.0f, 0.0f, CardW, CardH, WithAlpha(Paper, 0.16f), 1.5f);
		// The words on the other half.
		const float TX = bRight ? 30.0f : 330.0f;
		const float TW = CardW - 330.0f - 30.0f;
		CP::RoundedBox(Top, TX, 30.0f, 120.0f, 36.0f, 18.0f, Side.Tone);
		Top.Text(Side.Label, TX + 60.0f, 33.0f, 22.0f, Paper, CP::ETextAlign::Center, TEXT("Black"));
		const float NameSize = FMath::Clamp(50.0f * TW / FMath::Max(CP::HudMeasure(Side.Name, 50.0f, TEXT("Black")), 1.0f), 24.0f, 50.0f);
		CP::HudText3D(CP::HudAbove(Top, 1), Side.Name, TX, 84.0f + (50.0f - NameSize) * 0.5f, NameSize, Paper, CP::ETextAlign::Left, TEXT("Black"), 3.0f);
		const FLinearColor Swatch = ChaosImpactRoster::GetColourSwatch(Side.Colour);
		CP::RoundedBox(Top, TX, 164.0f, 26.0f, 26.0f, 6.0f, Swatch);
		const FString CharacterName = ChaosImpactRoster::Get(Side.Character).Name;
		Top.Text(CharacterName, TX + 38.0f, 160.0f, CP::HudFit(CharacterName, 24.0f, TW - 38.0f), Paper, CP::ETextAlign::Left, TEXT("Black"));
		// Pad or keyboard, on a small tag.
		const FString Device = Side.bKeyboard ? TEXT("キーボード") : TEXT("パッド");
		const float DeviceW = CP::HudMeasure(Device, 15.0f, TEXT("Black")) + 24.0f;
		CP::RoundedBox(Top, TX, 206.0f, DeviceW, 26.0f, 13.0f, WithAlpha(Paper, 0.14f));
		Top.Text(Device, TX + DeviceW * 0.5f, 209.0f, 15.0f, WithAlpha(Paper, 0.85f), CP::ETextAlign::Center, TEXT("Black"));
		Top.Box(TX, 254.0f, TW, 1.0f, WithAlpha(Paper, 0.16f));
		Top.Text(TEXT("1v1の戦績"), TX, 268.0f, 18.0f, Muted, CP::ETextAlign::Left, TEXT("Bold"));
		const FMenuPainter Words{CardSpace, Elements, Layer + 3, In};
		if (Side.bKnownRecord && Side.Record.GetGames() > 0)
		{
			const FString RecordText = DescribeDuelRecord(Side.Record);
			CP::HudText3D(CP::HudAbove(Top, 1), RecordText, TX, 296.0f, CP::HudFit(RecordText, 34.0f, TW - 6.0f), Paper, CP::ETextAlign::Left, TEXT("Black"), 2.0f);
			const int32 Games = Side.Record.GetGames();
			const float Rate = Games > 0 ? static_cast<float>(Side.Record.Wins) / Games : 0.0f;
			Top.Text(FString::Printf(TEXT("勝率 %d%%"), FMath::RoundToInt(Rate * 100.0f)), TX, 346.0f, 18.0f, Gold, CP::ETextAlign::Left, TEXT("Black"));
			CP::RoundedBox(Top, TX, 374.0f, TW, 6.0f, 3.0f, WithAlpha(Paper, 0.12f));
			CP::RoundedBox(CP::HudAbove(Top, 1), TX, 374.0f, TW * Rate, 6.0f, 3.0f, Gold);
			PaintDuelRecent(Words, Side.Record.Recent, TX, 398.0f, 30.0f);
		}
		else
		{
			Top.Text(Side.bKnownRecord ? TEXT("はじめての1v1") : TEXT("―"), TX, 298.0f, 28.0f, Paper, CP::ETextAlign::Left, TEXT("Black"));
		}
		if (Side.bAccepted)
		{
			// Said yes: a stamp over the character.
			const float Pop = 1.0f + 0.25f * FMath::Exp(-8.0f * FMath::Max(0.0f, Time - 0.2f));
			const float StampX = bRight ? CardW - 180.0f : 170.0f;
			const CP::FPainter Stamp{CP::MakeAnchor(CardSpace, StampX, 60.0f, Pop), Elements, Layer + 6, In};
			CP::RoundedBox(Stamp, -66.0f, -30.0f, 132.0f, 60.0f, 12.0f, Gold);
			Stamp.Outline(-66.0f, -30.0f, 132.0f, 60.0f, WithAlpha(Paper, 0.8f), 2.0f);
			CP::HudText3D(CP::HudAbove(Stamp, 1), TEXT("OK!"), 0.0f, -27.0f, 42.0f, Paper, CP::ETextAlign::Center, TEXT("BlackItalic"), 2.0f);
		}
	}

	// Between them: VS inside the clock's ring, the connection under it.
	const float MidIn = EaseOut((Time - 0.25f) / 0.3f);
	const CP::FPainter Mid{Design, Elements, Layer + 4, MidIn};
	const FVector2D Hub(800.0, 420.0);
	const float TimeLeft = bStarting ? 0.0f : FMath::Max(0.0f, static_cast<float>(Offer.EndsAt - FPlatformTime::Seconds()));
	const float Fill = bStarting ? 1.0f : FMath::Clamp(TimeLeft / 15.0f, 0.0f, 1.0f);
	const bool bHurry = !bStarting && TimeLeft < 5.0f;
	Mid.Disc(Hub, 92.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.95f));
	Mid.Ring(Hub, 92.0f, WithAlpha(Paper, 0.12f), 10.0f, 72);
	CP::HudAbove(Mid, 1).Arc(Hub, 92.0f, -90.0f, -90.0f + 360.0f * Fill, bStarting ? Gold : bHurry ? Fire : Ice, 10.0f);
	CP::HudText3D(CP::HudAbove(Mid, 2), TEXT("VS"), 800.0f, 352.0f, 76.0f, Paper, CP::ETextAlign::Center, TEXT("BlackItalic"), 4.0f);
	if (bStarting)
	{
		const CP::FPainter Banner{Design, Elements, Layer + 7, MidIn};
		CP::HudPoly(Banner, {FVector2D(480.0, 540.0), FVector2D(1140.0, 540.0), FVector2D(1120.0, 610.0), FVector2D(460.0, 610.0)},
			CP::MakeFill(FMath::Lerp(Gold, Paper, 0.3f), Gold));
		CP::HudText3D(CP::HudAbove(Banner, 1), TEXT("対戦開始！"), 800.0f, 546.0f, 46.0f, Paper, CP::ETextAlign::Center, TEXT("Black"), 3.0f);
		return;
	}
	CP::HudAbove(Mid, 2).Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(TimeLeft)), 800.0f, 448.0f, 28.0f, bHurry ? Fire : Paper, CP::ETextAlign::Center,
		TEXT("BlackItalic"));
	const FMenuPainter Signal{Design, Elements, Layer + 4, MidIn};
	Mid.Text(TEXT("回線"), 800.0f, 530.0f, 18.0f, Muted, CP::ETextAlign::Center, TEXT("Bold"));
	PaintSignal(Signal, Offer.PingMs, 768.0f, 600.0f, 1.0f);
	Mid.Text(Offer.PingMs > 0 && Offer.PingMs < 999 ? FString::Printf(TEXT("約%dms"), Offer.PingMs) : FString(TEXT("―")), 800.0f, 608.0f,
		22.0f, Paper, CP::ETextAlign::Center, TEXT("Black"));
}

namespace
{
	/** What a line check came to: its numbers and a word on them. */
	struct FOlLineVerdict
	{
		int32 Valid = 0;
		int32 Lost = 0;
		int32 Average = 0;
		int32 Low = 0;
		int32 High = 0;
		int32 Jitter = 0;
		int32 LossPercent = 0;
		FString Word;
		FString Advice;
		FLinearColor Color = FLinearColor::White;
	};

	FLinearColor OlLineColor(const int32 Rtt)
	{
		return Rtt <= 60 ? FLinearColor(0.2f, 0.92f, 0.45f, 1.0f) : Rtt <= 120 ? FLinearColor(0.65f, 0.95f, 0.3f, 1.0f)
			: Rtt <= 200 ? Gold : Fire;
	}

	FOlLineVerdict OlJudgeLine(const TArray<int32>& Rtts)
	{
		FOlLineVerdict V;
		int32 Sum = 0;
		int32 Steps = 0;
		int32 StepSum = 0;
		int32 Previous = -1;
		V.Low = MAX_int32;
		for (const int32 Rtt : Rtts)
		{
			if (Rtt == -1)
			{
				++V.Lost;
			}
			if (Rtt < 0)
			{
				continue;
			}
			++V.Valid;
			Sum += Rtt;
			V.Low = FMath::Min(V.Low, Rtt);
			V.High = FMath::Max(V.High, Rtt);
			if (Previous >= 0)
			{
				StepSum += FMath::Abs(Rtt - Previous);
				++Steps;
			}
			Previous = Rtt;
		}
		V.Low = V.Valid > 0 ? V.Low : 0;
		V.Average = V.Valid > 0 ? FMath::RoundToInt(static_cast<float>(Sum) / V.Valid) : 0;
		V.Jitter = Steps > 0 ? FMath::RoundToInt(static_cast<float>(StepSum) / Steps) : 0;
		const int32 Counted = V.Valid + V.Lost;
		V.LossPercent = Counted > 0 ? FMath::RoundToInt(100.0f * V.Lost / Counted) : 0;
		if (V.Valid == 0)
		{
			V.Word = TEXT("つながりません");
			V.Advice = TEXT("中継サーバーに届きませんでした。ネットワークの設定を確認してください");
			V.Color = Fire;
		}
		else if (V.Average <= 60 && V.LossPercent == 0 && V.Jitter <= 15)
		{
			V.Word = TEXT("とても良好");
			V.Advice = TEXT("快適にオンライン対戦できます");
			V.Color = FLinearColor(0.2f, 0.92f, 0.45f, 1.0f);
		}
		else if (V.Average <= 120 && V.LossPercent <= 6 && V.Jitter <= 30)
		{
			V.Word = TEXT("良好");
			V.Advice = TEXT("問題なく遊べます");
			V.Color = FLinearColor(0.65f, 0.95f, 0.3f, 1.0f);
		}
		else if (V.Average <= 200 && V.LossPercent <= 12)
		{
			V.Word = TEXT("ふつう");
			V.Advice = TEXT("少し遅れを感じることがあります。有線LANがおすすめです");
			V.Color = Gold;
		}
		else
		{
			V.Word = TEXT("不安定");
			V.Advice = TEXT("遅れや切断が起きやすい状態です。有線LANやルーターの再起動をためしてください");
			V.Color = Fire;
		}
		return V;
	}

	void OlPageCard(const ChaosImpactPaint::FPainter& C, const float W, const float H, const FLinearColor& Accent)
	{
		namespace CP = ChaosImpactPaint;
		C.Box(14.0f, 16.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		CP::HudPoly(C, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)},
			CP::MakeFill(FLinearColor(0.045f, 0.065f, 0.12f, 0.96f), FLinearColor(0.006f, 0.008f, 0.02f, 0.97f)));
		const CP::FPainter Up = CP::HudAbove(C, 1);
		CP::HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, 6.0), FVector2D(0.0, 6.0)},
			CP::MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
		Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.12f), 1.0f);
	}
}

void UChaosImpactMenuWidget::PaintOnlinePlayerCard(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float Time) const
{
	namespace CP = ChaosImpactPaint;
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions)
	{
		return;
	}
	// Top right: who plays, and the line to the relay as last measured.
	const float In = EaseOut((Time - 0.1f) / 0.35f);
	constexpr float W = 400.0f;
	constexpr float H = 96.0f;
	const CP::FPainter P{MakeSkewed(Design, 1090.0f + (1.0f - In) * 80.0f, 66.0f, W, H, -0.12f), Elements, Layer, In};
	OlPageCard(P, W, H, Gold);
	const CP::FPainter T = CP::HudAbove(P, 2);
	T.Text(TEXT("PLAYER"), 24.0f, 14.0f, 14.0f, Gold, CP::ETextAlign::Left, TEXT("BlackItalic"));
	const FString Name = Sessions->GetPlayerName();
	CP::HudText3D(CP::HudAbove(T, 1), Name, 24.0f, 34.0f, CP::HudFit(Name, 32.0f, 200.0f), Paper, CP::ETextAlign::Left, TEXT("Black"), 2.0f);
	const int32 Rtt = Sessions->GetLastRelayRtt();
	const FLinearColor Tone = Rtt > 0 ? OlLineColor(Rtt) : Muted;
	const int32 Level = Rtt <= 0 ? 0 : Rtt <= 60 ? 4 : Rtt <= 120 ? 3 : Rtt <= 200 ? 2 : 1;
	for (int32 Bar = 0; Bar < 4; ++Bar)
	{
		const float BarH = 10.0f + Bar * 8.0f;
		CP::RoundedBox(T, 236.0f + Bar * 12.0f, 66.0f - BarH, 8.0f, BarH, 2.0f, Bar < Level ? Tone : WithAlpha(Paper, 0.15f));
	}
	const FString RttText = Rtt > 0 ? FString::Printf(TEXT("%dms"), Rtt) : FString(TEXT("未計測"));
	T.Text(RttText, W - 24.0f, 22.0f, CP::HudFit(RttText, 22.0f, 96.0f, TEXT("BlackItalic")), Tone, CP::ETextAlign::Right, TEXT("BlackItalic"));
	T.Text(TEXT("回線"), W - 24.0f, 58.0f, 15.0f, Muted, CP::ETextAlign::Right, TEXT("Bold"));
}

void UChaosImpactMenuWidget::PaintLineCheck(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time) const
{
	namespace CP = ChaosImpactPaint;
	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	if (!Sessions)
	{
		return;
	}
	const UChaosImpactSessionSubsystem::FLineCheck& Check = Sessions->GetLineCheck();
	const FOlLineVerdict V = OlJudgeLine(Check.Rtts);
	int32 Latest = -1;
	for (const int32 Rtt : Check.Rtts)
	{
		Latest = Rtt >= 0 ? Rtt : Latest;
	}
	const bool bShowResult = Check.bDone && !Check.bNoRelay;

	// ---- Left: the meter ----
	const float InL = EaseOut((Time - 0.1f) / 0.35f);
	constexpr float LW = 610.0f;
	constexpr float LH = 500.0f;
	const FGeometry LeftSpace = MakeSkewed(Design, 110.0f - (1.0f - InL) * 80.0f, 196.0f, LW, LH, -0.06f);
	const CP::FPainter L{LeftSpace, Elements, Layer, InL};
	OlPageCard(L, LW, LH, bShowResult ? V.Color : Ice);
	const CP::FPainter LT = CP::HudAbove(L, 2);
	LT.Text(TEXT("PING"), 30.0f, 22.0f, 16.0f, Ice, CP::ETextAlign::Left, TEXT("BlackItalic"));
	const FVector2D Hub(LW * 0.5f, 330.0);
	constexpr float Radius = 200.0f;
	constexpr float Scale = 300.0f;
	const auto AngleOf = [](const float Ms) { return 180.0f + 180.0f * FMath::Clamp(Ms / Scale, 0.0f, 1.0f); };
	// The scale's zones: green, light green, gold, red.
	const float Zones[] = {0.0f, 60.0f, 120.0f, 200.0f, 300.0f};
	const FLinearColor ZoneColors[] = {FLinearColor(0.2f, 0.92f, 0.45f, 1.0f), FLinearColor(0.65f, 0.95f, 0.3f, 1.0f), Gold, Fire};
	const CP::FPainter Dial = CP::HudAbove(L, 1);
	Dial.Arc(Hub, Radius, 180.0f, 360.0f, WithAlpha(Paper, 0.08f), 34.0f);
	for (int32 Zone = 0; Zone < 4; ++Zone)
	{
		Dial.Arc(Hub, Radius, AngleOf(Zones[Zone]) + 0.6f, AngleOf(Zones[Zone + 1]) - 0.6f, WithAlpha(ZoneColors[Zone], 0.75f), 22.0f);
		const float Rad = FMath::DegreesToRadians(AngleOf(Zones[Zone]));
		LT.Text(FString::FromInt(FMath::RoundToInt(Zones[Zone])), Hub.X + FMath::Cos(Rad) * (Radius + 34.0f), Hub.Y + FMath::Sin(Rad) * (Radius + 34.0f) - 10.0f,
			15.0f, Muted, CP::ETextAlign::Center, TEXT("Bold"));
	}
	LT.Text(TEXT("300+"), Hub.X + Radius + 34.0f, Hub.Y - 10.0f, 15.0f, Muted, CP::ETextAlign::Center, TEXT("Bold"));
	// The needle: the latest round trip while measuring, the average once done.
	const float Shown = bShowResult ? static_cast<float>(V.Average) : Latest >= 0 ? static_cast<float>(Latest)
		: 150.0f + 140.0f * FMath::Sin(Time * 3.0f);
	const float NeedleRad = FMath::DegreesToRadians(AngleOf(Shown));
	const FVector2D Tip = Hub + FVector2D(FMath::Cos(NeedleRad), FMath::Sin(NeedleRad)) * (Radius - 26.0f);
	CP::HudAbove(LT, 1).Line(Hub, Tip, Paper, 6.0f);
	CP::HudAbove(LT, 1).Disc(Hub, 16.0f, Paper);
	CP::HudAbove(LT, 2).Disc(Hub, 8.0f, bShowResult ? V.Color : Ice);
	// The number under it, and the word.
	if (Check.bNoRelay)
	{
		const FString None = TEXT("中継サーバーが設定されていません");
		LT.Text(None, LW * 0.5f, 380.0f, CP::HudFit(None, 26.0f, LW - 60.0f), Fire, CP::ETextAlign::Center, TEXT("Black"));
	}
	else if (bShowResult)
	{
		CP::HudText3D(CP::HudAbove(LT, 1), V.Valid > 0 ? FString::Printf(TEXT("%dms"), V.Average) : FString(TEXT("---")), LW * 0.5f, 350.0f, 56.0f,
			Paper, CP::ETextAlign::Center, TEXT("BlackItalic"), 3.0f);
		CP::HudText3D(CP::HudAbove(LT, 1), V.Word, LW * 0.5f, 432.0f, CP::HudFit(V.Word, 34.0f, LW - 60.0f), V.Color, CP::ETextAlign::Center,
			TEXT("Black"), 2.0f);
	}
	else
	{
		LT.Text(Latest >= 0 ? FString::Printf(TEXT("%dms"), Latest) : FString(TEXT("…")), LW * 0.5f, 360.0f, 52.0f, Paper, CP::ETextAlign::Center,
			TEXT("BlackItalic"));
		const int32 Back = Check.Rtts.Num() - Check.Rtts.FilterByPredicate([](const int32 Rtt) { return Rtt == -2; }).Num();
		const FString Measuring = FString::Printf(TEXT("はかっています  %d / %d"), Back, UChaosImpactSessionSubsystem::LineCheckSamples);
		LT.Text(Measuring, LW * 0.5f, 432.0f, 24.0f, Ice, CP::ETextAlign::Center, TEXT("Black"));
		CP::RoundedBox(LT, 80.0f, 470.0f, LW - 160.0f, 8.0f, 4.0f, WithAlpha(Paper, 0.12f));
		CP::RoundedBox(CP::HudAbove(LT, 1), 80.0f, 470.0f, (LW - 160.0f) * Back / FMath::Max(1, UChaosImpactSessionSubsystem::LineCheckSamples), 8.0f,
			4.0f, Ice);
	}

	// ---- Right: the numbers, each round trip, how it is connected, and what to do ----
	const float InR = EaseOut((Time - 0.18f) / 0.35f);
	constexpr float RW = 730.0f;
	constexpr float RH = 500.0f;
	const FGeometry RightSpace = MakeSkewed(Design, 760.0f + (1.0f - InR) * 80.0f, 196.0f, RW, RH, -0.06f);
	const CP::FPainter R{RightSpace, Elements, Layer, InR};
	OlPageCard(R, RW, RH, bShowResult ? V.Color : Ice);
	const CP::FPainter RT = CP::HudAbove(R, 2);
	struct FTile
	{
		const TCHAR* Label;
		FString Value;
		FLinearColor Color;
	};
	const bool bAny = bShowResult && V.Valid > 0;
	const FTile Tiles[] = {
		{TEXT("平均"), bAny ? FString::Printf(TEXT("%dms"), V.Average) : FString(TEXT("―")), bAny ? OlLineColor(V.Average) : Muted},
		{TEXT("最小 / 最大"), bAny ? FString::Printf(TEXT("%d / %dms"), V.Low, V.High) : FString(TEXT("―")), Paper},
		{TEXT("ゆらぎ"), bAny ? FString::Printf(TEXT("±%dms"), V.Jitter) : FString(TEXT("―")), bAny && V.Jitter > 30 ? Gold : Paper},
		{TEXT("パケットロス"), bShowResult ? FString::Printf(TEXT("%d%%"), V.LossPercent) : FString(TEXT("―")), bShowResult && V.LossPercent > 0 ? Fire : Paper}};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float X = 30.0f + (Index % 2) * 340.0f;
		const float Y = 30.0f + (Index / 2) * 112.0f;
		CP::RoundedBox(CP::HudAbove(R, 1), X, Y, 320.0f, 96.0f, 12.0f, WithAlpha(Paper, 0.06f));
		RT.Text(Tiles[Index].Label, X + 20.0f, Y + 12.0f, 17.0f, Muted, CP::ETextAlign::Left, TEXT("Bold"));
		CP::HudText3D(CP::HudAbove(RT, 1), Tiles[Index].Value, X + 20.0f, Y + 38.0f, CP::HudFit(Tiles[Index].Value, 36.0f, 280.0f, TEXT("BlackItalic")),
			Tiles[Index].Color, CP::ETextAlign::Left, TEXT("BlackItalic"), 2.0f);
	}
	// Each round trip, a bar (a red cross for one lost).
	RT.Text(FString::Printf(TEXT("%d回の測定"), UChaosImpactSessionSubsystem::LineCheckSamples), 30.0f, 262.0f, 16.0f, Muted, CP::ETextAlign::Left, TEXT("Bold"));
	const float GraphX = 30.0f;
	const float GraphY = 290.0f;
	const float GraphW = RW - 60.0f;
	constexpr float GraphH = 96.0f;
	CP::RoundedBox(CP::HudAbove(R, 1), GraphX, GraphY, GraphW, GraphH, 8.0f, WithAlpha(Paper, 0.05f));
	const int32 Count = FMath::Max(1, Check.Rtts.Num());
	const float BarSlot = GraphW / Count;
	for (int32 Index = 0; Index < Check.Rtts.Num(); ++Index)
	{
		const int32 Rtt = Check.Rtts[Index];
		const float X = GraphX + Index * BarSlot + BarSlot * 0.2f;
		if (Rtt >= 0)
		{
			const float BarH = FMath::Max(4.0f, (GraphH - 12.0f) * FMath::Clamp(Rtt / Scale, 0.0f, 1.0f));
			CP::RoundedBox(RT, X, GraphY + GraphH - 6.0f - BarH, BarSlot * 0.6f, BarH, 3.0f, OlLineColor(Rtt));
		}
		else if (Rtt == -1)
		{
			const FVector2D C(X + BarSlot * 0.3f, GraphY + GraphH * 0.5f);
			RT.Line(C + FVector2D(-8.0, -8.0), C + FVector2D(8.0, 8.0), Fire, 4.0f);
			RT.Line(C + FVector2D(-8.0, 8.0), C + FVector2D(8.0, -8.0), Fire, 4.0f);
		}
	}
	// How it is connected, and a word on what to do.
	const FString Transport = FString::Printf(TEXT("接続方式  %s"), *Sessions->GetRelayTransportName());
	RT.Text(Transport, 30.0f, 404.0f, CP::HudFit(Transport, 20.0f, RW - 60.0f, TEXT("Bold")), Paper, CP::ETextAlign::Left, TEXT("Bold"));
	const FString Advice = Check.bNoRelay ? FString(TEXT("インターネット対戦の設定がありません"))
		: bShowResult ? V.Advice : FString(TEXT("中継サーバーとのやりとりを16回はかります"));
	RT.Text(Advice, 30.0f, 440.0f, CP::HudFit(Advice, 20.0f, RW - 60.0f, TEXT("Bold")), bShowResult ? V.Color : Muted, CP::ETextAlign::Left, TEXT("Bold"));
}

void UChaosImpactMenuWidget::UpdateSeatPreviews(const float DeltaSeconds)
{
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const TArray<AChaosImpactPlayerController::FTourneySeat>& Seats = Owner->GetTourneySeats();
	FString For = Owner->GetTourneySeatTitle();
	for (const AChaosImpactPlayerController::FTourneySeat& Seat : Seats)
	{
		For += FString::Printf(TEXT("|%s/%d/%d/%d"), *Seat.Name, Seat.Character, Seat.Colour, Seat.CustomColours.Num());
	}
	if (DuelPreviewFor != For)
	{
		// Each place's person (a CPU too), on a stage of their own.
		CloseDuelPreviews();
		DuelPreviewFor = For;
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Parameters.ObjectFlags |= RF_Transient;
		for (int32 Index = 0; Index < Seats.Num(); ++Index)
		{
			AChaosImpactCharacterPreview* Preview = World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
				FVector(Index * 1600.0f, -136000.0f, 40000.0f), FRotator::ZeroRotator, Parameters);
			if (!Preview)
			{
				continue;
			}
			Preview->ShowLoadout(Seats[Index].Character, FMath::Max(Seats[Index].Colour, 0), PlayerAccents[Index % 4]);
			Preview->ShowCustomColours(Seats[Index].CustomColours);
			Preview->SetFacing(Seats.Num() == 2 ? (Index == 0 ? -24.0f : 24.0f) : -14.0f, 0.0f);
			FSlateBrush Brush;
			Brush.SetResourceObject(Preview->GetPicture());
			Brush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			DuelPreviews.Add(Preview);
			DuelPreviewBrushes.Add(Brush);
		}
	}
	for (AChaosImpactCharacterPreview* Preview : DuelPreviews)
	{
		if (IsValid(Preview))
		{
			Preview->Animate(DeltaSeconds);
			Preview->FilmNow();
		}
	}
}

void UChaosImpactMenuWidget::PaintTourneySeat(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time) const
{
	namespace CP = ChaosImpactPaint;
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (!Owner)
	{
		return;
	}
	const TArray<AChaosImpactPlayerController::FTourneySeat>& Seats = Owner->GetTourneySeats();
	const bool bMatch = Owner->IsTourneySeatForMatch();
	// The title: which match (or the tournament), and what to do.
	{
		const float In = EaseOut(Time / 0.3f);
		const CP::FPainter Title{CP::MakeAnchor(Design, 800.0f, 86.0f, FMath::Lerp(1.4f, 1.0f, In)), Elements, Layer + 4, In};
		const FString Name = Owner->GetTourneySeatTitle();
		CP::HudText3D(Title, Name, 0.0f, -46.0f, CP::HudFit(Name, 68.0f, 1300.0f, TEXT("BlackItalic")), Gold, CP::ETextAlign::Center, TEXT("BlackItalic"), 4.0f);
		const CP::FPainter Under{Design, Elements, Layer + 4, EaseOut((Time - 0.15f) / 0.3f)};
		const FString Line = bMatch ? TEXT("この試合で遊ぶ人は、自分のコントローラーのボタンを押してください（キーボードは何かのキー）")
			: TEXT("それぞれ、自分のコントローラーのボタンを押してください（キーボードは何かのキー）");
		Under.Text(Line, 800.0f, 150.0f, CP::HudFit(Line, 22.0f, 1400.0f, TEXT("Black")), Paper, CP::ETextAlign::Center, TEXT("Black"));
	}
	// A card per place.
	const int32 Count = FMath::Max(1, Seats.Num());
	const float CardW = Count <= 2 ? 420.0f : 330.0f;
	const float Gap = Count <= 2 ? 180.0f : 34.0f;
	const float CardH = 490.0f;
	const float Left = 800.0f - (Count * CardW + (Count - 1) * Gap) * 0.5f;
	const float CardTop = 200.0f;
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < Seats.Num(); ++Index)
	{
		const AChaosImpactPlayerController::FTourneySeat& Seat = Seats[Index];
		const bool bTaken = Seat.Device != -2 || Seat.bCPU;
		const FLinearColor Tone = Seat.bCPU ? Muted : PlayerAccents[Index % 4];
		const float In = EaseOut((Time - 0.1f - 0.07f * Index) / 0.35f);
		const float X = Left + Index * (CardW + Gap);
		const FGeometry CardSpace = MakeSkewed(Design, X, CardTop + (1.0f - In) * 40.0f, CardW, CardH, -0.06f);
		const CP::FPainter C{CardSpace, Elements, Layer, In};
		FLinearColor Deep = Tone * 0.18f;
		Deep.A = 0.97f;
		C.Box(14.0f, 16.0f, CardW, CardH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		CP::HudPoly(C, {FVector2D(0.0, 0.0), FVector2D(CardW, 0.0), FVector2D(CardW, CardH), FVector2D(0.0, CardH)},
			CP::MakeFill(Deep, FLinearColor(0.006f, 0.008f, 0.02f, 0.97f)));
		// The person, cut to the card.
		if (DuelPreviewBrushes.IsValidIndex(Index) && DuelPreviewBrushes[Index].GetResourceObject())
		{
			const float PicW = CardW + 40.0f;
			const float PicH = PicW * AChaosImpactCharacterPreview::PictureHeight / AChaosImpactCharacterPreview::PictureWidth;
			Elements.PushClip(FSlateClippingZone(CardSpace));
			FSlateDrawElement::MakeBox(Elements, Layer + 1, CardSpace.ToPaintGeometry(FVector2f(PicW, PicH),
				FSlateLayoutTransform(FVector2f(-20.0f, CardH - PicH - 40.0f))), &DuelPreviewBrushes[Index], ESlateDrawEffect::None,
				WithAlpha(FLinearColor::White, In * (Seat.bCPU ? 0.7f : 1.0f)));
			const CP::FPainter Fade{CardSpace, Elements, Layer + 1, In};
			for (int32 Step = 0; Step < 14; ++Step)
			{
				const float A = FMath::Square(1.0f - Step / 14.0f);
				Fade.Box(0.0f, CardH - (Step + 1) * 9.0f, CardW, 9.0f, FLinearColor(0.006f, 0.008f, 0.02f, 0.92f * A));
			}
			Elements.PopClip();
		}
		const CP::FPainter Up = CP::HudAbove(C, 2);
		CP::HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(CardW, 0.0), FVector2D(CardW, 8.0), FVector2D(0.0, 8.0)},
			CP::MakeFill(FMath::Lerp(Tone, Paper, 0.35f), Tone));
		Up.Outline(0.0f, 0.0f, CardW, CardH, WithAlpha(Paper, bTaken ? 0.3f : 0.12f), bTaken ? 2.0f : 1.0f);
		const CP::FPainter T = CP::HudAbove(C, 3);
		const FString Label = Seat.bCPU ? FString(TEXT("CPU")) : bMatch ? FString(Index == 0 ? TEXT("左") : TEXT("右")) : FString::Printf(TEXT("%dP"), Index + 1);
		const float LabelW = CP::HudMeasure(Label, 16.0f, TEXT("Black")) + 24.0f;
		CP::RoundedBox(T, 18.0f, 20.0f, LabelW, 28.0f, 14.0f, Tone);
		CP::HudAbove(T, 1).Text(Label, 18.0f + LabelW * 0.5f, 23.0f, 16.0f, Paper, CP::ETextAlign::Center, TEXT("Black"));
		CP::HudText3D(CP::HudAbove(T, 1), Seat.Name, 18.0f, 58.0f, CP::HudFit(Seat.Name, 40.0f, CardW - 36.0f), Paper, CP::ETextAlign::Left, TEXT("Black"), 3.0f);
		const FString CharacterName = ChaosImpactRoster::Get(Seat.Character).Name;
		T.Text(CharacterName, 20.0f, 108.0f, CP::HudFit(CharacterName, 18.0f, CardW - 40.0f, TEXT("Bold")), WithAlpha(Paper, 0.7f), CP::ETextAlign::Left, TEXT("Bold"));
		// Its controller: taken (which), or waiting for a button; a CPU needs none.
		const float BoxY = CardH - 76.0f;
		if (Seat.bCPU)
		{
			const FString Strength = FString::Printf(TEXT("CPU（%s）"), ChaosImpactMatch::GetCPULevelName(Seat.CPULevel));
			CP::RoundedBox(T, 18.0f, BoxY, CardW - 36.0f, 56.0f, 14.0f, WithAlpha(Paper, 0.08f));
			T.Text(Strength, CardW * 0.5f, BoxY + 14.0f, CP::HudFit(Strength, 22.0f, CardW - 60.0f), Muted, CP::ETextAlign::Center, TEXT("Black"));
		}
		else if (Seat.Device != -2)
		{
			const FString Device = Seat.Device == -1 ? FString(TEXT("キーボード")) : FString::Printf(TEXT("コントローラー %d"), Seat.Device);
			CP::HudPoly(T, {FVector2D(18.0, BoxY), FVector2D(CardW - 18.0f, BoxY), FVector2D(CardW - 18.0f, BoxY + 56.0f), FVector2D(18.0, BoxY + 56.0f)},
				CP::MakeFill(FLinearColor(0.3f, 0.95f, 0.5f, 1.0f), FLinearColor(0.08f, 0.6f, 0.28f, 1.0f)));
			CP::HudText3D(CP::HudAbove(T, 1), FString::Printf(TEXT("OK!  %s"), *Device), CardW * 0.5f, BoxY + 12.0f,
				CP::HudFit(FString::Printf(TEXT("OK!  %s"), *Device), 24.0f, CardW - 60.0f), Paper, CP::ETextAlign::Center, TEXT("Black"), 2.0f);
		}
		else
		{
			const float Breath = 0.55f + 0.45f * FMath::Sin(Time * 5.0f + Index);
			CP::RoundedBox(T, 18.0f, BoxY, CardW - 36.0f, 56.0f, 14.0f, WithAlpha(Paper, 0.06f + 0.06f * Breath));
			CP::HudAbove(T, 1).Outline(18.0f, BoxY, CardW - 36.0f, 56.0f, WithAlpha(Tone, Breath), 2.0f);
			const FString Ask = TEXT("ボタンを押して参加");
			T.Text(Ask, CardW * 0.5f, BoxY + 14.0f, CP::HudFit(Ask, 22.0f, CardW - 60.0f), WithAlpha(Paper, 0.6f + 0.4f * Breath), CP::ETextAlign::Center, TEXT("Black"));
		}
	}
	// VS between the two of a match.
	if (bMatch && Seats.Num() == 2)
	{
		const CP::FPainter Mid{Design, Elements, Layer + 6, EaseOut((Time - 0.25f) / 0.3f)};
		CP::HudText3D(Mid, TEXT("VS"), 800.0f, 392.0f, 96.0f, Paper, CP::ETextAlign::Center, TEXT("BlackItalic"), 5.0f);
	}
	// Everyone in: a moment, then it starts (any of them can still give theirs back).
	const double FullAt = Owner->GetTourneySeatsFullAt();
	const CP::FPainter Foot{Design, Elements, Layer + 6};
	if (FullAt > 0.0)
	{
		const float Left01 = FMath::Clamp(static_cast<float>((Now - FullAt) / 1.4), 0.0f, 1.0f);
		CP::HudText3D(Foot, bMatch ? TEXT("試合開始！") : TEXT("トーナメント開始！"), 800.0f, 712.0f, 44.0f, Gold, CP::ETextAlign::Center, TEXT("BlackItalic"), 3.0f);
		CP::RoundedBox(Foot, 600.0f, 780.0f, 400.0f, 10.0f, 5.0f, WithAlpha(Paper, 0.12f));
		CP::RoundedBox(CP::HudAbove(Foot, 1), 600.0f, 780.0f, 400.0f * Left01, 10.0f, 5.0f, Gold);
	}
	const FString Guide = bMatch ? TEXT("もう一度同じボタンで取り消し") : TEXT("もう一度同じボタンで取り消し　・　Esc：登録画面にもどる");
	Foot.Text(Guide, 800.0f, 830.0f, CP::HudFit(Guide, 17.0f, 1300.0f, TEXT("Bold")), Muted, CP::ETextAlign::Center, TEXT("Bold"));
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
		// The online menu's mosaic: ソロ / VS / 訓練場 across the top; トレーニング / カラーエディット / 設定 under them;
		// タイトルへ and the bar saying what the chosen one is along the bottom. (The indices stay as they were: 0 ソロ,
		// 1 VS, 2 トレーニング, 3 タイトルへ, 4 設定, 5 カラーエディット, 6 訓練場.)
		Entries.Add({FSlateRect(110, 190, 600, 490), TEXT("ソロモード"), TEXT(""), TEXT("SOLO"), Ice});
		Entries.Add({FSlateRect(630, 190, 1120, 490), TEXT("VSモード"), TEXT(""), TEXT("VERSUS"), Fire});
		Entries.Add({FSlateRect(110, 515, 700, 680), TEXT("トレーニング"), TEXT(""), TEXT("TRAINING"), Gold});
		Entries.Add({FSlateRect(110, 718, 400, 790), TEXT("タイトルへ"), TEXT(""), TEXT(""), Muted});
		Entries.Add({FSlateRect(1130, 515, 1490, 680), TEXT("設定"), TEXT("settings"), TEXT("SETTINGS"), FLinearColor(0.42f, 0.6f, 0.95f, 1.0f)});
		Entries.Add({FSlateRect(730, 515, 1100, 680), TEXT("カラーエディット"), TEXT("colour_edit"), TEXT("COLOR EDIT"), Violet});
		Entries.Add({FSlateRect(1150, 190, 1490, 490), TEXT("訓練場"), TEXT("dojo"), TEXT("DOJO"), FLinearColor(0.08f, 0.86f, 0.78f, 1.0f)});
		break;
	case EChaosImpactScreen::TrainingSetup:
	{
		// (Who plays with which device is decided on the entry screen that follows, by who presses first.)
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		for (int32 Players = 1; Players <= 4; ++Players)
		{
			const float X = 150.0f + (Players - 1) * 331.0f;
			Entries.Add({FSlateRect(X, 236, X + 305, 626), Players == 1 ? TEXT("PLAYER") : TEXT("PLAYERS"),
				TEXT(""), FString::FromInt(Players), PlayerAccents[Players - 1]});
		}
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
		Entries.Add({FSlateRect(110, 190, 560, 680), TEXT("ローカル"), TEXT(""), TEXT("LOCAL"), Ice});
		Entries.Add({FSlateRect(590, 190, 1040, 680), TEXT("通信"), TEXT(""), TEXT("ONLINE"), Fire});
		Entries.Add({FSlateRect(110, 718, 400, 790), TEXT("モード選択へ"), TEXT(""), TEXT(""), Muted});
		// (Index 3, after the back button: the cards' code reads 0 and 1 as before.)
		Entries.Add({FSlateRect(1070, 190, 1490, 680), TEXT("トーナメント"), TEXT("tourney"), TEXT("TOURNAMENT"), Gold});
		break;
	case EChaosImpactScreen::OnlinePlayers:
	{
		for (int32 Players = 1; Players <= 2; ++Players)
		{
			const float X = 482.0f + (Players - 1) * 331.0f;
			Entries.Add({FSlateRect(X, 236, X + 305, 626), Players == 1 ? TEXT("PLAYER") : TEXT("PLAYERS"),
				TEXT(""), FString::FromInt(Players), PlayerAccents[Players - 1]});
		}
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
		const AChaosImpactPlayerController* OnlineController =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bPair = OnlineController && OnlineController->GetRequestedLocalPlayerCount() >= 2;
		// ランダムマッチ 8人 / 1v1 / トーナメント across the top; 合言葉マッチ / 戦績 / 回線チェック under them; 戻る /
		// カラーエディット / なまえ along the bottom. (The indices stay as they were: 0 8人, 1 1v1, 2 合言葉, 3 戦績, 4 なまえ,
		// 5 戻る, 6 トーナメント, 7 カラーエディット, 8 回線チェック.)
		Entries.Add({FSlateRect(110, 196, 590, 470), TEXT("ランダムマッチ 8人"), TEXT("random"), TEXT("8 PLAYERS"), Fire});
		FMenuEntry Duel{FSlateRect(620, 196, 1100, 470), TEXT("ランダムマッチ 1v1"), TEXT("duel"), TEXT("1 VS 1"), Gold};
		Duel.bDisabled = bPair;
		Entries.Add(Duel);
		Entries.Add({FSlateRect(110, 500, 700, 680), TEXT("合言葉マッチ"), TEXT("private"), TEXT("ROOM"), Ice});
		Entries.Add({FSlateRect(730, 500, 1100, 680), TEXT("戦績"), TEXT("record"), TEXT("RECORD"), Violet});
		Entries.Add({FSlateRect(830, 718, 1490, 798),
			FString::Printf(TEXT("なまえ  %s%s"), Sessions ? *Sessions->GetPlayerName() : TEXT(""),
				bPair ? TEXT("  ＋(2)") : TEXT("")),
			TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(110, 726, 400, 790), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		Entries.Add({FSlateRect(1130, 196, 1490, 470), TEXT("トーナメント"), TEXT("tourney"), TEXT("TOURNAMENT"), Violet});
		Entries.Add({FSlateRect(430, 726, 800, 790), TEXT("カラーエディット"), TEXT("colour_edit"), TEXT(""), Violet});
		Entries.Add({FSlateRect(1130, 500, 1490, 680), TEXT("回線チェック"), TEXT("linecheck"), TEXT("NETWORK"), FLinearColor(0.2f, 0.9f, 0.5f, 1.0f)});
		break;
	}
	case EChaosImpactScreen::LineCheck:
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		FMenuEntry Again{FSlateRect(1010, 726, 1490, 800), TEXT("もう一度はかる"), TEXT("again"), TEXT(""), Gold};
		Again.bDisabled = Sessions && Sessions->GetLineCheck().bRunning;
		Entries.Add(Again);
		Entries.Add({FSlateRect(110, 730, 400, 794), TEXT("戻る"), TEXT("back"), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::TourneyList:
	{
		// Every public tournament, the soonest first: its name, when it starts, how it runs and its places.
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		static const TArray<FChaosImpactRoomListing> NoListings;
		const TArray<FChaosImpactRoomListing>& Listings = Sessions ? Sessions->GetRoomListings() : NoListings;
		TourneyRowListing.Reset();
		TArray<int32> Order;
		for (int32 Index = 0; Index < Listings.Num(); ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&Listings](const int32 A, const int32 B) { return Listings[A].TourneyStartsUnix < Listings[B].TourneyStartsUnix; });
		const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
		for (int32 Row = 0; Row < FMath::Min(Order.Num(), 5); ++Row)
		{
			const FChaosImpactRoomListing& Listing = Listings[Order[Row]];
			const bool bRunning = Listing.TourneyState == TEXT("running");
			const bool bDone = Listing.TourneyState == TEXT("done");
			const FDateTime Local = FDateTime::FromUnixTimestamp(Listing.TourneyStartsUnix) + (FDateTime::Now() - FDateTime::UtcNow());
			const int64 Left = Listing.TourneyStartsUnix - Now;
			const FString When = bDone ? FString(TEXT("終了")) : bRunning ? FString(TEXT("開催中（観戦で参加）"))
				: Left <= 0 ? FString::Printf(TEXT("%02d:%02d開始・参加者を待っています"), Local.GetHour(), Local.GetMinute())
				: FString::Printf(TEXT("%02d:%02d開始（あと%d分）"), Local.GetHour(), Local.GetMinute(), static_cast<int32>((Left + 59) / 60));
			const FString Detail = FString::Printf(TEXT("%s  ・  %s  ・  %s%s"), *When, Listing.TourneyMode == 1 ? TEXT("同時進行") : TEXT("1試合ずつ"),
				*Listing.HostName, Listing.TourneyCPUs > 0 ? *FString::Printf(TEXT("  ・  CPU%d人"), Listing.TourneyCPUs) : TEXT(""));
			FMenuEntry Entry{FSlateRect(200, 214 + Row * 92, 1400, 294 + Row * 92),
				Listing.TourneyTitle.IsEmpty() ? Listing.RoomName : Listing.TourneyTitle, Detail,
				bRunning ? FString(TEXT("観戦")) : FString::Printf(TEXT("%d/%d人"), Listing.Members, Listing.TourneyMaxPlayers),
				bRunning ? Gold : bDone ? Muted : Violet};
			Entry.bDisabled = bDone || (!bRunning && Listing.Members >= Listing.TourneyMaxPlayers) || !Sessions->GetJoinBlocker(Listing).IsEmpty();
			Entries.Add(Entry);
			TourneyRowListing.Add(Order[Row]);
		}
		Entries.Add({FSlateRect(1010, 714, 1450, 798), TEXT("大会を作る"), TEXT("create"), TEXT(""), Gold});
		Entries.Add({FSlateRect(560, 724, 960, 788), TEXT("さがしなおす"), TEXT("refresh"), TEXT(""), Ice});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT("back"), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::TourneyCreate:
	{
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		static const FChaosImpactTourneyNotice NoNotice;
		const FChaosImpactTourneyNotice& Notice = Controller ? Controller->GetPendingTourney() : NoNotice;
		const int32 Minutes = Controller ? Controller->GetPendingTourneyMinutes() : 15;
		const FDateTime Start = FDateTime::Now() + FTimespan::FromMinutes(Minutes);
		const FString Rows[] = {
			FString::Printf(TEXT("大会名　%s"), *Notice.Title),
			FString::Printf(TEXT("開始　＜  %02d:%02d（%d分後）  ＞"), Start.GetHour(), (Start.GetMinute() / 1) % 60, Minutes),
			FString::Printf(TEXT("進め方　＜  %s  ＞"), Notice.Mode == 1 ? TEXT("同時進行") : TEXT("1試合ずつ")),
			FString::Printf(TEXT("参加できる人数　＜  %d人まで  ＞"), Notice.MaxPlayers),
			FString::Printf(TEXT("CPU　＜  %d人  ＞"), Notice.CPUs),
			Notice.CPUs <= 0 ? FString(TEXT("CPUの強さ　（CPUなし）"))
				: FString::Printf(TEXT("CPUの強さ　＜  %s  ＞"), Notice.Level < 0 ? TEXT("おまかせ") : ChaosImpactMatch::GetCPULevelName(Notice.Level))};
		const FLinearColor Accents[] = {Paper, Gold, Ice, Fire, Fire, Violet};
		for (int32 Row = 0; Row < 6; ++Row)
		{
			FMenuEntry Entry{FSlateRect(430, 150 + Row * 70, 1170, 210 + Row * 70), Rows[Row], TEXT(""), TEXT(""), Accents[Row]};
			Entry.bDisabled = Row == 5 && Notice.CPUs <= 0;
			Entries.Add(Entry);
		}
		Entries.Add({FSlateRect(1030, 712, 1454, 800), TEXT("大会を作る"), TEXT("create"), TEXT(""), Gold});
		Entries.Add({FSlateRect(146, 724, 470, 788), TEXT("戻る"), TEXT("back"), TEXT(""), Muted});
		break;
	}
	case EChaosImpactScreen::OnlinePrivate:
		Entries.Add({FSlateRect(150, 236, 770, 652), TEXT("部屋をつくる"), TEXT(""), TEXT("CREATE"), Ice});
		Entries.Add({FSlateRect(830, 236, 1450, 652), TEXT("部屋をさがす"), TEXT(""), TEXT("SEARCH"), Fire});
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::OnlineRecord:
		Entries.Add({FSlateRect(150, 724, 470, 788), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		break;
	case EChaosImpactScreen::DuelFound:
	{
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		if (Sessions && Sessions->IsQueueMatched())
		{
			// Both said yes: nothing more to choose.
			FMenuEntry Starting{FSlateRect(560, 756, 1040, 832), TEXT("まもなく対戦開始"), TEXT("starting"), TEXT(""), Gold};
			Starting.bDisabled = true;
			Entries.Add(Starting);
			break;
		}
		const bool bAccepted = Sessions && Sessions->GetDuelOffer().bAccepted;
		FMenuEntry Accept{FSlateRect(430, 756, 910, 836), bAccepted ? TEXT("相手の返事を待っています") : TEXT("対戦する"),
			TEXT("accept"), TEXT(""), Gold};
		Accept.bDisabled = bAccepted;
		Entries.Add(Accept);
		Entries.Add({FSlateRect(950, 764, 1210, 828), TEXT("やめる"), TEXT("decline"), TEXT(""), Muted});
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
		if (!PauseConfirm.IsEmpty())
		{
			// The question's two answers, in its card (いいえ first: a stray press keeps the player where they are).
			Entries.Add({FSlateRect(560, 520, 790, 590), TEXT("いいえ"), TEXT("confirm_no"), TEXT(""), Ice});
			Entries.Add({FSlateRect(810, 520, 1040, 590), TEXT("はい"), TEXT("confirm_yes"), TEXT(""), Fire});
			break;
		}
		if (Controller && Controller->IsInMatchmadeMatch())
		{
			// A ランダムマッチ being played: nothing but going back to it, or 降参.
			Entries.Add({FSlateRect(490, 365, 1110, 427), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice});
			Entries.Add({FSlateRect(490, 445, 1110, 507), TEXT("降参する"), TEXT("surrender"), TEXT(""), Fire});
			break;
		}
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
				if (Controller->IsPrimaryLocalPlayerController())
				{
					Y += 80.0f;
					Entries.Add({FSlateRect(490, Y, 1110, Y + 62), TEXT("カラーエディット"), TEXT("colour_edit"), TEXT(""), Violet});
				}
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
			Entries.Add({FSlateRect(490, Y, 1110, Y + 62),
				PauseRoom && PauseRoom->bPublicTournament ? (Controller->IsOnlineRoomHost() ? TEXT("大会を中止する") : TEXT("大会からぬける"))
					: PauseRoom && PauseRoom->IsMatchmade() ? TEXT("部屋をぬける")
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

		// 訓練場: the lesson again, the list, or out.
		if (Controller && AChaosImpactDojo::IsDojoWorld(Controller->GetWorld()))
		{
			// (Two only: back to the lesson, or back to the lesson list.)
			Entries.Add({ FSlateRect(490, 285, 1110, 347), TEXT("再開"), TEXT("resume"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 365, 1110, 427), TEXT("レッスン選択へ戻る"), TEXT("dojo_list"), TEXT(""), Gold });
			break;
		}

		// ★ソロモードプレイ中のポーズメニュー（新規追加分岐）
		if (Controller && Controller->IsSoloMode())
		{
			Entries.Add({ FSlateRect(490, 285, 1110, 347), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 365, 1110, 427), TEXT("ステージをやり直す"), TEXT("solo_retry"), TEXT(""), Gold });
			Entries.Add({ FSlateRect(490, 445, 1110, 507), TEXT("ワールドマップへ"), TEXT("solo_world_map"), TEXT(""), Ice });
			Entries.Add({ FSlateRect(490, 525, 1110, 587), TEXT("モード選択へ"), TEXT("goto_mode"), TEXT(""), Fire });
			Entries.Add({ FSlateRect(490, 605, 1110, 667), TEXT("タイトル画面へ"), TEXT("goto_title"), TEXT(""), Muted });
			Entries.Add({ FSlateRect(490, 685, 1110, 747), TEXT("設定"), TEXT("settings"), TEXT(""), Violet });
			break;
		}

		Entries.Add({FSlateRect(490, 245, 1110, 307), TEXT("ゲームに戻る"), TEXT("resume"), TEXT(""), Ice});
		if (Controller && Controller->ShowsTrainingPauseEntries())
		{
			Entries.Add({FSlateRect(490, 325, 1110, 387), TEXT("トレーニング設定"), TEXT("training_settings"), TEXT(""), Ice});
			Entries.Add({FSlateRect(490, 405, 1110, 467), TEXT("トレーニングをリトライ"), TEXT("training_retry"), TEXT(""), Gold});
		}
		const float Below = Controller && Controller->ShowsTrainingPauseEntries() ? 485.0f : 325.0f;
		if (Controller && Controller->IsSearchingForRoom())
		{
			// Searching (in this machine's own training): out of the search, back to the online menu, but no way to
			// the title or the mode select from here.
			const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
			Entries.Add({FSlateRect(490, Below, 1110, Below + 62),
				Sessions && Sessions->IsQueueing() ? TEXT("さがすのをやめる") : TEXT("へやをさがすのをやめる"),
				TEXT("stop_search"), TEXT(""), Fire});
			Entries.Add({FSlateRect(490, Below + 80, 1110, Below + 142), TEXT("オンラインメニューへ"), TEXT("online_menu"), TEXT(""), Gold});
			Entries.Add({FSlateRect(490, Below + 160, 1110, Below + 222), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
			break;
		}
		if (Controller && Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusOnline)
		{
			// Back from an online room: the online menu first.
			Entries.Add({FSlateRect(490, Below, 1110, Below + 62), TEXT("オンラインメニューへ"), TEXT("online_menu"), TEXT(""), Gold});
			Entries.Add({FSlateRect(490, Below + 80, 1110, Below + 142), TEXT("モード選択へ"), TEXT("goto_mode"), TEXT(""), Fire});
			Entries.Add({FSlateRect(490, Below + 160, 1110, Below + 222), TEXT("タイトル画面へ"), TEXT("goto_title"), TEXT(""), Muted});
			Entries.Add({FSlateRect(490, Below + 240, 1110, Below + 302), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
			break;
		}
		Entries.Add({FSlateRect(490, Below, 1110, Below + 62), TEXT("モード選択へ"), TEXT("goto_mode"), TEXT(""), Fire});
		Entries.Add({FSlateRect(490, Below + 80, 1110, Below + 142), TEXT("タイトル画面へ"), TEXT("goto_title"), TEXT(""), Muted});
		Entries.Add({FSlateRect(490, Below + 160, 1110, Below + 222), TEXT("設定"), TEXT("settings"), TEXT(""), Violet});
		break;
	}
	case EChaosImpactScreen::TrainingSettings:
	{
		const AChaosImpactPlayerController* Controller =
			Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const int32 Players = Controller ? Controller->GetRequestedLocalPlayerCount() : 1;
		const bool bTargets = !Controller || Controller->AreTrainingTargetsEnabled();
		const int32 CPUCount = Controller ? Controller->GetTrainingCPUCount() : 0;
		Entries.Add({FSlateRect(450, 220, 1150, 282),
			FString::Printf(TEXT("プレイヤー人数：%d人"), Players), TEXT(""), TEXT(""), Ice});
		Entries.Add({FSlateRect(450, 300, 1150, 362),
			bTargets ? TEXT("マト：あり") : TEXT("マト：なし"), TEXT(""), TEXT(""), bTargets ? Gold : Muted});
		Entries.Add({FSlateRect(450, 380, 1150, 442),
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
	if (bBallSwitch)
	{
		// ボールスイッチ: the special balls in two rows of seven (Number: the kind), then すべてオン and もどる.
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const TArray<EChaosImpactBallType> Kinds = ChaosImpactBallSwitch::GetSwitchable();
		for (int32 Index = 0; Index < Kinds.Num(); ++Index)
		{
			const float Left = 110.0f + (Index % 7) * 199.0f - (Index / 7) * 22.0f * 0.06f;
			const float Top = 214.0f + (Index / 7) * 222.0f;
			const bool bOn = !Controller || ChaosImpactBallSwitch::IsOn(Controller->GetPendingMatchRules(), Kinds[Index]);
			Entries.Add({FSlateRect(Left, Top, Left + 181.0f, Top + 200.0f), ChaosImpactBallTypes::GetDisplayName(Kinds[Index]), TEXT("ball"),
				FString::FromInt(static_cast<int32>(Kinds[Index])), bOn ? ChaosImpactBallTypes::GetColor(Kinds[Index]) : Muted});
		}
		Entries.Add({FSlateRect(430, 726, 800, 790), TEXT("すべてオン"), TEXT("all_on"), TEXT(""), Gold});
		Entries.Add({FSlateRect(110, 726, 400, 790), TEXT("もどる"), TEXT("switch_back"), TEXT(""), Muted});
		break;
	}
	if (bModePicker)
	{
		// Every kind of match, in rows by kind (バトル / バトルロイヤル / そのほか): Number its mode, greyed out with the
		// reason when it cannot be chosen here.
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		static const int32 Grid[3][4] = {{0, 1, 2, 3}, {6, 7, 8, 9}, {4, 5, 10, AChaosImpactPlayerController::TournamentAllAtOnceChoice}};
		for (int32 GridRow = 0; GridRow < 3; ++GridRow)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				const int32 Mode = Grid[GridRow][Column];
				if (Mode < 0)
				{
					continue;
				}
				FChaosImpactMatchRules Kind;
				AChaosImpactPlayerController::ApplyMatchMode(Kind, Mode);
				const float Left = 300.0f + Column * 300.0f - GridRow * 148.0f * 0.1f;
				const float Top = 214.0f + GridRow * 158.0f;
				// (Short names: the row says which kind it is.)
				static const TCHAR* Names[] = {TEXT("個人戦"), TEXT("2チーム"), TEXT("3チーム"), TEXT("4チーム"), TEXT("クラッシュボール"),
					TEXT("デュエル 1対1"), TEXT("個人戦"), TEXT("2チーム"), TEXT("3チーム"), TEXT("4チーム"), TEXT("トーナメント 1試合ずつ"),
					TEXT("トーナメント 同時進行")};
				FMenuEntry Tile{FSlateRect(Left, Top, Left + 284.0f, Top + 140.0f), Names[Mode],
					TEXT("mode"), FString::FromInt(Mode), GridRow == 0 ? Fire : GridRow == 1 ? Gold : Violet};
				Tile.bDisabled = Controller && !Controller->GetMatchModeBlocker(FMath::Min(Mode, 10)).IsEmpty();
				Entries.Add(Tile);
			}
		}
		Entries.Add({FSlateRect(110, 726, 400, 790), TEXT("もどる"), TEXT("picker_back"), TEXT(""), Muted});
		break;
	}
	else
	{
		// Four rows of a setting each (its name, its value between ◀ ▶; "fixed" when this rule decides it), then 決定 and
		// 戻る. Title: the setting, Number: its value, Detail: "fixed" when it cannot be changed.
		const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const FChaosImpactMatchRules Rules = Controller ? Controller->GetPendingMatchRules() : FChaosImpactMatchRules();
		const auto Row = [this](const int32 Index, const TCHAR* Label, const FString& Value, const FLinearColor& Accent, const bool bFixed)
		{
			const float Top = 200.0f + Index * 92.0f;
			FMenuEntry Entry{FSlateRect(110.0f - Index * 92.0f * 0.12f, Top, 880.0f - Index * 92.0f * 0.12f, Top + 76.0f), Label,
				bFixed ? TEXT("fixed") : TEXT(""), Value, Accent};
			Entry.bDisabled = bFixed;
			Entries.Add(Entry);
		};
		// 0: how long (a tournament: how it runs).
		if (Rules.bTournament)
		{
			Row(0, TEXT("進め方"), FString(Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? TEXT("同時進行") : TEXT("1試合ずつ"))
				+ (Rules.Tournament.bPlacementMatches ? TEXT("・順位決定戦あり") : TEXT("")), Ice, false);
		}
		else if (Rules.bDuel)
		{
			Row(0, TEXT("試合時間"), FString::Printf(TEXT("1ラウンド%d秒"), FMath::RoundToInt(ChaosImpactMatch::DuelRoundSeconds)), Ice, true);
		}
		else if (Rules.bBattleRoyale)
		{
			Row(0, TEXT("試合時間"), TEXT("2分"), Ice, true);
		}
		else
		{
			Row(0, TEXT("試合時間"), FString::Printf(TEXT("%d分"), Rules.Minutes), Ice, Rules.bCrashBall);
		}
		// 1: the rule itself.
		Row(1, TEXT("ルール"), ChaosImpactMatch::DescribeRule(Rules).Replace(TEXT("  "), TEXT(" ")),
			Rules.bTournament ? Violet : Rules.bDuel ? Ice : Rules.bCrashBall ? Violet : Rules.IsTeamBattle() ? Gold : Fire, false);
		// 2: the CPUs (the duel: one player faces a CPU, two face each other).
		if (Rules.bTournament)
		{
			Row(2, TEXT("CPU"), FString::Printf(TEXT("%d人"), Rules.Tournament.CPUs.Num()), Rules.Tournament.CPUs.IsEmpty() ? Muted : Fire, false);
		}
		else if (Rules.bDuel)
		{
			Row(2, TEXT("相手"), Rules.CPUCount > 0 ? TEXT("CPU") : TEXT("プレイヤー同士"), Fire, true);
		}
		else
		{
			Row(2, TEXT("CPU"), FString::Printf(TEXT("%d人"), Rules.CPUCount), Rules.CPUCount > 0 ? Fire : Muted, false);
		}
		// 3: how strong (only with CPUs; otherwise greyed out and skipped by the cursor).
		const FLinearColor StrengthColor = Rules.CPULevel == ChaosImpactMatch::CPULevelWeak ? Ice
			: Rules.CPULevel == ChaosImpactMatch::CPULevelNormal ? Gold
			: Rules.CPULevel == ChaosImpactMatch::CPULevelStrong ? Fire : FLinearColor(0.78f, 0.36f, 1.0f, 1.0f);
		if (Rules.bTournament)
		{
			const int32 Level = Rules.Tournament.CPUs.IsEmpty() ? INDEX_NONE : Rules.Tournament.CPUs[0].Level;
			Row(3, TEXT("CPUの強さ"), Rules.Tournament.CPUs.IsEmpty() ? TEXT("CPUなし") : Level < 0 ? TEXT("おまかせ") : ChaosImpactMatch::GetCPULevelName(Level),
				StrengthColor, false);
			Entries.Last().bDisabled = Rules.Tournament.CPUs.IsEmpty();
		}
		else
		{
			Row(3, TEXT("CPUの強さ"), Rules.CPUCount > 0 ? ChaosImpactMatch::GetCPULevelName(Rules.CPULevel) : TEXT("CPUなし"), StrengthColor, false);
			Entries.Last().bDisabled = Rules.CPUCount <= 0;
		}
		Entries.Add({FSlateRect(1010, 718, 1490, 800), Rules.bDuel ? TEXT("試合開始") : TEXT("決定"), TEXT(""), TEXT(""), Gold});
		Entries.Add({FSlateRect(110, 726, 400, 790), TEXT("戻る"), TEXT(""), TEXT(""), Muted});
		// (Index 6, after 決定 and 戻る: their indices stay as they were.)
		const int32 On = ChaosImpactBallSwitch::CountOn(Rules);
		const int32 All = ChaosImpactBallSwitch::GetSwitchable().Num();
		Entries.Add({FSlateRect(430, 726, 980, 790), On >= All ? FString(TEXT("ボールスイッチ（すべてオン）"))
			: FString::Printf(TEXT("ボールスイッチ（%d / %d）"), On, All), TEXT("ball_switch"), TEXT(""), Violet});
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
		const AChaosImpactGameState* EndRoom = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		if (Controller && Controller->IsOnlineRoom() && EndRoom && EndRoom->IsMatchmade())
		{
			// A matched room ends here: the same two again (1v1, both asking), search again, or the online menu.
			const bool bDuel = EndRoom->Matchmade == EChaosImpactMatchmade::Duel;
			if (bDuel)
			{
				bool bAsked = false;
				const bool bHere = Controller->IsRematchOpponentHere(bAsked);
				const bool bMine = Controller->HasRequestedRematch();
				FMenuEntry Rematch{FSlateRect(230, 776, 630, 846), !bHere ? TEXT("相手は抜けました")
					: bMine ? TEXT("相手の入力を待っています") : bAsked ? TEXT("再戦する（相手はOK）") : TEXT("同じ相手と再戦"),
					TEXT("rematch_vote"), TEXT(""), Gold};
				Rematch.bDisabled = !bHere || bMine;
				Entries.Add(Rematch);
			}
			// Asked for the rematch (and the other player still here): nothing else until they answer (leaving now left
			// them waiting for someone gone).
			bool bAskedHere = false;
			const bool bWaitingForRematch = bDuel && Controller->HasRequestedRematch() && Controller->IsRematchOpponentHere(bAskedHere);
			if (bWaitingForRematch)
			{
				for (FMenuEntry& Earlier : Entries)
				{
					if (Earlier.Detail == TEXT("awards") || Earlier.Detail == TEXT("stats")) { Earlier.bDisabled = true; }
				}
			}
			FMenuEntry SearchAgain{bDuel ? FSlateRect(670, 776, 1070, 846) : FSlateRect(390, 776, 790, 846), TEXT("もう一度さがす"),
				TEXT("search_again"), TEXT(""), bDuel ? Ice : Gold};
			SearchAgain.bDisabled = bWaitingForRematch;
			Entries.Add(SearchAgain);
			FMenuEntry ToMenu{bDuel ? FSlateRect(1110, 776, 1510, 846) : FSlateRect(830, 776, 1230, 846), TEXT("メニューへ"),
				TEXT("online_menu"), TEXT(""), Muted};
			ToMenu.bDisabled = bWaitingForRematch;
			Entries.Add(ToMenu);
		}
		else if (Controller && Controller->IsOnlineRoom())
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
	if (Screen == EChaosImpactScreen::Pause && PauseConfirm.IsEmpty() && !Entries.IsEmpty())
	{
		// The pause menu: its items down the left panel, closer together the more there are.
		const int32 Count = Entries.Num();
		const float Step = FMath::Min(74.0f, 560.0f / Count);
		const float Height = FMath::Min(60.0f, Step - 10.0f);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Top = 272.0f + Index * Step;
			// (Following the panel's slanted edge.)
			const float Lean = (Top - 272.0f) * 0.178f;
			Entries[Index].Rect = FSlateRect(150.0f - Lean, Top, 660.0f - Lean, Top + Height);
		}
	}
}

void UChaosImpactMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (DojoInviteVideo)
	{
		// The invitation's picture: the lesson list, its cursor stepping down a group and on to the next group by itself,
		// the picked lesson's clip playing in its preview as it does there.
		const bool bShown = Screen == EChaosImpactScreen::ModeSelect && bDojoInvite && DojoInviteMenu;
		const double Clock = FPlatformTime::Seconds();
		if (bShown && Clock >= DojoInviteStepAt)
		{
			++DojoInviteSteps;
			if (DojoInviteSteps % 4 == 0)
			{
				DojoInviteMenu->SetTab(DojoInviteMenu->GetTab() + 1);
			}
			else
			{
				DojoInviteMenu->MoveSelection(1);
			}
			DojoInviteStepAt = Clock + 1.8;
		}
		DojoInviteVideo->Want(bShown ? DojoInviteMenu->GetSelectedClip() : FString());
		DojoInviteVideo->Tick();
	}
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
#if !UE_BUILD_SHIPPING
	// Development: -CIDevModePicker opens the rules screen's list of every kind of match (a screenshot of it).
	{
		static bool bDevPickerDone = false;
		if (!bDevPickerDone && Screen == EChaosImpactScreen::MatchRules && AnimationSeconds > 1.0f && FParse::Param(FCommandLine::Get(), TEXT("CIDevModePicker")))
		{
			bDevPickerDone = true;
			OpenModePicker(true);
		}
	}
	// Development: -CIDevColourEditBack=<from screen index>: opens カラーエディット from the online menu (or the pause menu) and
	// presses Escape in it, to check where it goes back to.
	{
		static int32 DevColourStep = 0;
		int32 DevColourFrom = -1;
		if (DevColourStep < 3 && FParse::Value(FCommandLine::Get(), TEXT("CIDevColourEditBack="), DevColourFrom) && AnimationSeconds > 1.5f)
		{
			AChaosImpactPlayerController* DevOwner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			if (DevColourStep == 0 && DevOwner && (Screen == EChaosImpactScreen::MultiReady || Screen == EChaosImpactScreen::Pause))
			{
				DevColourStep = 1;
				DevOwner->OpenColourEdit(Screen);
			}
			else if (DevColourStep == 1 && Screen == EChaosImpactScreen::ColourEdit)
			{
				DevColourStep = 2;
				const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
				HandleAnyUserKeyDown(Escape);
				UE_LOG(LogChaosImpact, Log, TEXT("Development: Escape pressed in the colour edit"));
			}
			else if (DevColourStep == 2)
			{
				DevColourStep = 3;
				UE_LOG(LogChaosImpact, Log, TEXT("Development: after the colour edit, the screen is %d"), static_cast<int32>(Screen));
			}
		}
	}
	// Development: -CIDevPauseConfirm=<choice> asks the pause menu's question for that choice (a screenshot of it).
	{
		static bool bDevConfirmDone = false;
		FString DevConfirm;
		if (!bDevConfirmDone && Screen == EChaosImpactScreen::Pause && AnimationSeconds > 1.0f
			&& FParse::Value(FCommandLine::Get(), TEXT("CIDevPauseConfirm="), DevConfirm))
		{
			bDevConfirmDone = true;
			PauseConfirm = DevConfirm;
			PauseConfirmAt = Now;
			BuildEntries();
			SelectedIndex = 0;
		}
	}
#endif
	// A 1v1 found settles while the training under its card is paused (the controller's own tick stopped): the card
	// goes from here when the other side said no, the time ran out, or the match is made.
	if (Screen == EChaosImpactScreen::DuelFound)
	{
		UpdateDuelPreviews(InDeltaTime);
		if (AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			Owner->UpdateDuelOfferScreen();
		}
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect)
	{
		CharacterSelect->Tick(InDeltaTime);
	}
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit)
	{
		ColourEdit->Tick(InDeltaTime);
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry)
	{
		TourneyEntry->Tick(InDeltaTime);
	}
	if (Screen == EChaosImpactScreen::TourneySeat)
	{
		UpdateSeatPreviews(InDeltaTime);
		if (AChaosImpactPlayerController* Seats = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
#if !UE_BUILD_SHIPPING
			// Development (-CIDevAutoSeat): the places are taken by themselves (the keyboard, then made-up pads) a moment in.
			if (FParse::Param(FCommandLine::Get(), TEXT("CIDevAutoSeat")) && AnimationSeconds > 2.5f && Seats->GetTourneySeatsFullAt() <= 0.0)
			{
				int32 FakePad = 9001;
				for (const AChaosImpactPlayerController::FTourneySeat& Seat : Seats->GetTourneySeats())
				{
					FakePad += Seat.Device != -2 ? 1 : 0;
				}
				for (const AChaosImpactPlayerController::FTourneySeat& Seat : Seats->GetTourneySeats())
				{
					if (!Seat.bCPU && Seat.Device == -2)
					{
						const bool bKeyboardFree = !Seats->GetTourneySeats().ContainsByPredicate([](const AChaosImpactPlayerController::FTourneySeat& Each) { return Each.Device == -1; });
						Seats->PressTourneySeat(bKeyboardFree, FakePad++);
						break;
					}
				}
			}
#endif
			Seats->UpdateTourneySeats();
		}
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
		const bool bHideLevel = ((Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen())
			|| (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && ColourEdit->IsOpen())
			|| (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && TourneyEntry->IsOpen()))
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
	if (Screen == EChaosImpactScreen::DuelFound || Screen == EChaosImpactScreen::MatchEnd || Screen == EChaosImpactScreen::LineCheck)
	{
		// The other player's answers change the choices (対戦する / 相手を待っています, the rematch).
		const FString Key = GetLiveEntriesKey();
		if (Key != LastLiveEntriesKey)
		{
			const bool bFirst = LastLiveEntriesKey.IsEmpty();
			LastLiveEntriesKey = Key;
			if (!bFirst)
			{
				const int32 PreviousSelection = SelectedIndex;
				RefreshEntries();
				SelectedIndex = FMath::Clamp(PreviousSelection, 0, FMath::Max(0, Entries.Num() - 1));
			}
		}
	}
#if !UE_BUILD_SHIPPING
	// Development (-CIAutoTourneyJoin): enters the first public tournament listed.
	if (Screen == EChaosImpactScreen::TourneyList && !TourneyRowListing.IsEmpty() && FParse::Param(FCommandLine::Get(), TEXT("CIAutoTourneyJoin")))
	{
		if (AChaosImpactPlayerController* AutoController = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			UE_LOG(LogTemp, Log, TEXT("Public tournament list: %d, entering the first"), TourneyRowListing.Num());
			const int32 First = TourneyRowListing[0];
			TourneyRowListing.Reset();
			AutoController->JoinRoomListing(First);
		}
	}
#endif
	if (Screen == EChaosImpactScreen::RoomList || Screen == EChaosImpactScreen::TourneyList)
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
	else if (Screen == EChaosImpactScreen::Pause)
	{
		const FVector2D ViewSize = AllottedGeometry.GetLocalSize();
		const float BackIn = EaseOut(T / 0.25f);
		Full.Box(0.0f, 0.0f, ViewSize.X, ViewSize.Y, FLinearColor(0.0f, 0.004f, 0.018f, 0.5f * BackIn));
		const ChaosImpactPaint::FPainter Back{DesignGeometry, OutDrawElements, BaseLayer + 1, BackIn};
		const float Slide = (1.0f - BackIn) * -160.0f;
		ChaosImpactPaint::HudPoly(Back, {FVector2D(-900.0f, 0.0), FVector2D(800.0f + Slide, 0.0), FVector2D(640.0f + Slide, 900.0),
			FVector2D(-900.0f, 900.0)}, ChaosImpactPaint::MakeFill(FLinearColor(0.03f, 0.05f, 0.1f, 0.93f), FLinearColor(0.004f, 0.006f, 0.016f, 0.96f)));
		const ChaosImpactPaint::FPainter Edge = ChaosImpactPaint::HudAbove(Back, 1);
		ChaosImpactPaint::HudPoly(Edge, {FVector2D(800.0f + Slide, 0.0), FVector2D(812.0f + Slide, 0.0), FVector2D(652.0f + Slide, 900.0),
			FVector2D(640.0f + Slide, 900.0)}, ChaosImpactPaint::MakeFill(FMath::Lerp(Ice, Paper, 0.3f), Ice));
		ChaosImpactPaint::HudPoly(Edge, {FVector2D(830.0f + Slide, 0.0), FVector2D(836.0f + Slide, 0.0), FVector2D(676.0f + Slide, 900.0),
			FVector2D(670.0f + Slide, 900.0)}, ChaosImpactPaint::MakeFill(Fire, FLinearColor(0.5f, 0.02f, 0.06f, 1.0f)));
		// Faint speed streaks across the panel.
		for (int32 Streak = 0; Streak < 6; ++Streak)
		{
			const float Y = 120.0f + Streak * 131.0f;
			Edge.Box(-900.0f, Y, 1400.0f - Streak * 60.0f, 1.0f, WithAlpha(Paper, 0.035f));
		}
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit)
	{
		const int32 EditLayer = ColourEdit->Paint(DesignGeometry, OutDrawElements, BaseLayer + 2);
		PaintEnterWipe(DesignGeometry, OutDrawElements, EditLayer + 1, T);
		return EditLayer + 2;
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu)
	{
		const int32 ListLayer = DojoMenu->Paint(DesignGeometry, OutDrawElements, BaseLayer + 2);
		PaintEnterWipe(DesignGeometry, OutDrawElements, ListLayer + 1, T);
		return ListLayer + 2;
	}
	if ((Screen == EChaosImpactScreen::ModeSelect && Entries.Num() == 7) || (Screen == EChaosImpactScreen::VSSelect && Entries.Num() == 4))
	{
		// The online menu's tiles: the chosen one drawn last (its glow over its neighbours), the bar along the bottom.
		const bool bMode = Screen == EChaosImpactScreen::ModeSelect;
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, bMode ? TEXT("MODE SELECT") : TEXT("VS MODE"), T);
		static const TCHAR* ModeKinds[] = {TEXT("solo"), TEXT("vs"), TEXT("training"), TEXT("back"), TEXT("settings"), TEXT("colour"), TEXT("dojo")};
		static const TCHAR* VersusKinds[] = {TEXT("local"), TEXT("online"), TEXT("vsback"), TEXT("tourney")};
		const auto TileOf = [&](const int32 Index, const FSlateRect& At)
		{
			ChaosImpactModeMenu::FTile Tile;
			Tile.Rect = At;
			Tile.Name = Entries[Index].Title;
			Tile.Kind = bMode ? ModeKinds[Index] : VersusKinds[Index];
			Tile.Big = Entries[Index].Number;
			Tile.Accent = Entries[Index].Accent;
			Tile.bDisabled = Entries[Index].bDisabled;
			return Tile;
		};
		int32 TileLayer = BaseLayer + 4;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if ((Index == SelectedIndex) != (Pass == 1))
				{
					continue;
				}
				const float Blend = SelectBlend.IsValidIndex(Index) ? SelectBlend[Index] : (Index == SelectedIndex ? 1.0f : 0.0f);
				const float EntryIn = EaseOut((T - 0.1f - 0.05f * Index) / 0.35f);
				const FSlateRect At = Entries[Index].Rect.OffsetBy(FVector2D(0.0f, (1.0f - EntryIn) * 60.0f));
				ChaosImpactModeMenu::PaintTile(DesignGeometry, OutDrawElements, TileLayer, TileOf(Index, At), Blend, PressedIndex == Index, EntryIn, T);
				TileLayer += 10;
			}
		}
		if (Entries.IsValidIndex(SelectedIndex))
		{
			const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			const bool bPad = Owner && Owner->IsUsingGamepad();
			const float BarIn = EaseOut((T - 0.35f) / 0.35f);
			ChaosImpactModeMenu::PaintInfoBar(DesignGeometry, OutDrawElements, TileLayer, TileOf(SelectedIndex, Entries[SelectedIndex].Rect),
				FSlateRect(430.0f + (1.0f - BarIn) * 80.0f, 718.0f, 1490.0f, 790.0f), bPad ? TEXT("A") : TEXT("Enter"), bPad ? TEXT("B") : TEXT("Esc"), BarIn, T);
		}
		if (bDojoInvite)
		{
			PaintDojoInvite(DesignGeometry, OutDrawElements, TileLayer + 4, static_cast<float>(FPlatformTime::Seconds() - DojoInviteAt));
			TileLayer += 160;
		}
		PaintEnterWipe(DesignGeometry, OutDrawElements, TileLayer + 10, T);
		return TileLayer + 12;
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry)
	{
		const int32 EntryLayer = TourneyEntry->Paint(DesignGeometry, OutDrawElements, BaseLayer + 2);
		PaintEnterWipe(DesignGeometry, OutDrawElements, EntryLayer + 1, T);
		return EntryLayer + 2;
	}
	if (Screen == EChaosImpactScreen::TourneySeat)
	{
		PaintTourneySeat(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
		return BaseLayer + 20;
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
		PaintOnlinePlayerCard(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
	}
	else if (Screen == EChaosImpactScreen::LineCheck)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("回線チェック"), T);
		PaintLineCheck(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
	}
	else if (Screen == EChaosImpactScreen::OnlinePrivate)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("合言葉マッチ"), T);
	}
	else if (Screen == EChaosImpactScreen::TourneyList)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("トーナメント"), T);
		const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
		if (!Sessions || Sessions->GetRoomListings().IsEmpty())
		{
			const FMenuPainter Note{DesignGeometry, OutDrawElements, BaseLayer + 3, EaseOut((T - 0.2f) / 0.3f)};
			Note.Text(TEXT("いま募集中の大会はありません"), 800.0f, 330.0f, 30.0f, Paper, ETextAlign::Center, TEXT("Black"));
			Note.Text(TEXT("右下の「大会を作る」から、開催時間とルールを決めて募集できます"), 800.0f, 386.0f, 20.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		}
	}
	if (Screen == EChaosImpactScreen::TourneyList)
	{
		// Each tournament's row: when it starts and how it runs under its name, its places on the right.
		const FMenuPainter Rows{DesignGeometry, OutDrawElements, BaseLayer + 40, EaseOut((T - 0.15f) / 0.3f)};
		for (int32 Row = 0; Row < TourneyRowListing.Num() && Entries.IsValidIndex(Row); ++Row)
		{
			const FMenuEntry& Entry = Entries[Row];
			const float Middle = static_cast<float>(Entry.Rect.Top + Entry.Rect.Bottom) * 0.5f;
			Rows.Text(Entry.Detail, static_cast<float>(Entry.Rect.Right) - 190.0f, Middle - 11.0f, 17.0f,
				WithAlpha(Paper, Entry.bDisabled ? 0.45f : 0.9f), ETextAlign::Right, TEXT("Bold"));
			Rows.Text(Entry.Number, static_cast<float>(Entry.Rect.Right) - 64.0f, Middle - 16.0f, 26.0f,
				Entry.bDisabled ? Muted : Paper, ETextAlign::Right, TEXT("BlackItalic"));
		}
	}
	if (Screen == EChaosImpactScreen::TourneyCreate)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("大会を作る"), T);
		const FMenuPainter Note{DesignGeometry, OutDrawElements, BaseLayer + 3, EaseOut((T - 0.2f) / 0.3f)};
		Note.Text(TEXT("開始時刻に2人以上そろっていれば始まります　・　全試合1対1・2本先取"), 800.0f, 584.0f, 19.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		Note.Text(TEXT("開始までは大会の部屋で練習しながら待てます（作った人は部屋を開けておいてください）"), 800.0f, 616.0f, 19.0f, Muted,
			ETextAlign::Center, TEXT("Bold"));
	}
	else if (Screen == EChaosImpactScreen::OnlineRecord)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("戦績"), T);
		PaintOnlineRecord(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
	}
	else if (Screen == EChaosImpactScreen::DuelFound)
	{
		PaintDuelFound(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
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
		const float PauseIn = EaseOut((T - 0.05f) / 0.3f);
		const ChaosImpactPaint::FPainter PauseTitle{DesignGeometry, OutDrawElements, BaseLayer + 3, PauseIn};
		ChaosImpactPaint::HudText3D(PauseTitle, TEXT("PAUSE"), 150.0f - (1.0f - PauseIn) * 60.0f, 62.0f, 94.0f, Paper, ChaosImpactPaint::ETextAlign::Left,
			TEXT("BlackItalic"), 5.0f);
		PauseTitle.Box(156.0f, 206.0f, 130.0f, 7.0f, Ice);
		PauseTitle.Box(290.0f, 206.0f, 64.0f, 7.0f, Fire);
		const FString Context = GetPauseContext();
		if (!Context.IsEmpty())
		{
			PauseTitle.Text(Context, 156.0f, 222.0f, ChaosImpactPaint::HudFit(Context, 22.0f, 560.0f), Gold, ChaosImpactPaint::ETextAlign::Left, TEXT("Black"));
		}
		PauseTitle.Text(TEXT("A / Enter：決定　　B / Esc：もどる"), 96.0f, 852.0f, 17.0f, WithAlpha(Paper, 0.55f), ChaosImpactPaint::ETextAlign::Left,
			TEXT("Bold"));
		if (!PauseConfirm.IsEmpty())
		{
			// 本当に〜よろしいですか？ on a card of its own, the answers under it.
			FString Question;
			FString Note;
			GetPauseConfirmText(Question, Note);
			const float Age = static_cast<float>(FPlatformTime::Seconds() - PauseConfirmAt);
			const float CardIn = EaseOut(Age / 0.22f);
			Full.Box(0.0f, 0.0f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, FLinearColor(0.0f, 0.0f, 0.02f, 0.6f * CardIn));
			const FGeometry Card = MakeSkewed(DesignGeometry, 430.0f, 330.0f + (1.0f - CardIn) * 24.0f, 740.0f, 290.0f, -0.08f);
			const FMenuPainter C{Card, OutDrawElements, BaseLayer + 4, CardIn};
			const ChaosImpactPaint::FPainter CardBody{Card, OutDrawElements, BaseLayer + 4, CardIn};
			C.Box(14.0f, 16.0f, 740.0f, 290.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			ChaosImpactPaint::HudPoly(CardBody, {FVector2D(0.0, 0.0), FVector2D(740.0, 0.0), FVector2D(740.0, 290.0), FVector2D(0.0, 290.0)},
				ChaosImpactPaint::MakeFill(FLinearColor(0.05f, 0.07f, 0.13f, 0.99f), FLinearColor(0.008f, 0.012f, 0.03f, 0.99f)));
			const FMenuPainter CardUp{Card, OutDrawElements, BaseLayer + 5, CardIn};
			ChaosImpactPaint::HudPoly(ChaosImpactPaint::FPainter{Card, OutDrawElements, BaseLayer + 5, CardIn},
				{FVector2D(0.0, 0.0), FVector2D(740.0, 0.0), FVector2D(740.0, 8.0), FVector2D(0.0, 8.0)},
				ChaosImpactPaint::MakeFill(FMath::Lerp(Fire, Paper, 0.3f), Fire));
			CardUp.Box(0.0f, 0.0f, 10.0f, 290.0f, Fire);
			CardUp.Outline(0.0f, 0.0f, 740.0f, 290.0f, WithAlpha(Paper, 0.16f), 2.0f);
			const FMenuPainter C2{Card, OutDrawElements, BaseLayer + 6, CardIn};
			C2.Text(TEXT("CONFIRM"), 40.0f, 24.0f, 18.0f, WithAlpha(Fire, 0.95f), ETextAlign::Left, TEXT("BlackItalic"));
			// (Sized to fit the card.)
			const float QuestionSize = FMath::Min(40.0f, 34.0f * 640.0f / FMath::Max(ChaosImpactPaint::HudMeasure(Question, 34.0f, TEXT("Black")), 1.0f));
			C2.Text(Question, 370.0f, 66.0f + (40.0f - QuestionSize) * 0.5f, QuestionSize, Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
			if (!Note.IsEmpty())
			{
				C2.Text(Note, 370.0f, 130.0f, ChaosImpactPaint::HudFit(Note, 22.0f, 640.0f, TEXT("Bold")), WithAlpha(Paper, 0.72f), ETextAlign::Center, TEXT("Bold"));
			}
		}
	}
	else if (Screen == EChaosImpactScreen::MatchRules)
	{
		PaintHeader(DesignGeometry, OutDrawElements, BaseLayer + 2, TEXT("RULE"), T);
		if (const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			namespace CP = ChaosImpactPaint;
			const FChaosImpactMatchRules& Rules = Controller->GetPendingMatchRules();
			const int32 Humans = ChaosImpactMatch::GetCompetingHumans(Rules, Controller->GetMatchHumanCount());
			const int32 CPUs = Rules.bTournament ? Rules.Tournament.CPUs.Num() : Rules.CPUCount;
			const int32 People = Rules.bTournament ? Controller->GetMatchHumanCount() : Humans;
			const int32 Total = People + CPUs;
			const FLinearColor ModeColor = Rules.bTournament || Rules.bCrashBall ? Violet : Rules.bDuel ? Ice : Rules.IsTeamBattle() ? Gold : Fire;
			// How the rows change, beside the title.
			if (!bModePicker && !bBallSwitch)
			{
				const FString Hint = TEXT("◀ ▶ で変更　・　ルールは決定で一覧から選べます");
				P.Text(Hint, 880.0f, 156.0f, ChaosImpactPaint::HudFit(Hint, 18.0f, 500.0f, TEXT("Bold")), Muted, ETextAlign::Right, TEXT("Bold"));
			}

			// Under the rows: who plays, as a sum (not while the list of kinds is open over the page).
			if (!bModePicker && !bBallSwitch)
			{
				const float In = EaseOut((T - 0.25f) / 0.35f);
				constexpr float SW = 818.0f;
				constexpr float SH = 112.0f;
				const CP::FPainter S{MakeSkewed(DesignGeometry, 62.0f - (1.0f - In) * 60.0f, 578.0f, SW, SH, -0.12f), OutDrawElements, BaseLayer + 3, In};
				S.Box(8.0f, 9.0f, SW, SH, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
				CP::HudPoly(S, {FVector2D(0.0, 0.0), FVector2D(SW, 0.0), FVector2D(SW, SH), FVector2D(0.0, SH)},
					CP::MakeFill(FLinearColor(0.04f, 0.055f, 0.1f, 0.94f), FLinearColor(0.008f, 0.012f, 0.026f, 0.96f)));
				const CP::FPainter ST = CP::HudAbove(S, 2);
				if (Rules.bSpectate && !Rules.bTournament)
				{
					ST.Text(TEXT("観戦"), 32.0f, 16.0f, 18.0f, Ice, CP::ETextAlign::Left, TEXT("Black"));
					const FString Watch = FString::Printf(TEXT("CPU %d人の試合をカメラで見る"), CPUs);
					CP::HudText3D(CP::HudAbove(ST, 1), Watch, 32.0f, 46.0f, CP::HudFit(Watch, 32.0f, SW - 64.0f), Paper, CP::ETextAlign::Left, TEXT("Black"), 2.0f);
				}
				else
				{
					struct FPart
					{
						const TCHAR* Label;
						int32 Count;
						FLinearColor Color;
					};
					const FPart Parts[] = {{TEXT("プレイヤー"), People, Ice}, {TEXT("CPU"), CPUs, CPUs > 0 ? Fire : Muted}, {TEXT("合計"), Total, Gold}};
					const TCHAR* Signs[] = {TEXT("＋"), TEXT("＝")};
					for (int32 Part = 0; Part < 3; ++Part)
					{
						const float X = 40.0f + Part * 270.0f;
						ST.Text(Parts[Part].Label, X, 16.0f, 17.0f, Muted, CP::ETextAlign::Left, TEXT("Bold"));
						CP::HudText3D(CP::HudAbove(ST, 1), FString::Printf(TEXT("%d"), Parts[Part].Count), X, 36.0f, 52.0f, Parts[Part].Color,
							CP::ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
						ST.Text(TEXT("人"), X + CP::HudMeasure(FString::FromInt(Parts[Part].Count), 52.0f, TEXT("BlackItalic")) + 10.0f, 66.0f, 22.0f, Paper,
							CP::ETextAlign::Left, TEXT("Black"));
						if (Part < 2)
						{
							ST.Text(Signs[Part], X + 210.0f, 44.0f, 34.0f, WithAlpha(Paper, 0.5f), CP::ETextAlign::Center, TEXT("Black"));
						}
					}
				}
			}

			// On the right: the kind of match chosen, what it is and how it is won (クラッシュボール has its own guide).
			if (bModePicker || bBallSwitch)
			{
			}
			else if (Rules.bCrashBall)
			{
				PaintCrashRuleGuide(DesignGeometry, OutDrawElements, BaseLayer + 2, T);
			}
			else
			{
				const float In = EaseOut((T - 0.18f) / 0.35f);
				constexpr float CW = 570.0f;
				constexpr float CH = 494.0f;
				const FGeometry CardSpace = MakeSkewed(DesignGeometry, 920.0f + (1.0f - In) * 80.0f, 196.0f, CW, CH, -0.06f);
				const CP::FPainter C{CardSpace, OutDrawElements, BaseLayer + 2, In};
				C.Box(14.0f, 16.0f, CW, CH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
				FLinearColor Deep = ModeColor * 0.2f;
				Deep.A = 0.96f;
				CP::HudPoly(C, {FVector2D(0.0, 0.0), FVector2D(CW, 0.0), FVector2D(CW, CH), FVector2D(0.0, CH)},
					CP::MakeFill(Deep, FLinearColor(0.008f, 0.01f, 0.025f, 0.97f)));
				const CP::FPainter CU = CP::HudAbove(C, 1);
				CP::HudPoly(CU, {FVector2D(0.0, 0.0), FVector2D(CW, 0.0), FVector2D(CW, 8.0), FVector2D(0.0, 8.0)},
					CP::MakeFill(FMath::Lerp(ModeColor, Paper, 0.35f), ModeColor));
				CU.Outline(0.0f, 0.0f, CW, CH, WithAlpha(Paper, 0.12f), 1.0f);
				const CP::FPainter CT = CP::HudAbove(C, 3);
				const FString English = Rules.bTournament ? TEXT("TOURNAMENT") : Rules.bDuel ? TEXT("DUEL") : Rules.bBattleRoyale ? TEXT("BATTLE ROYALE")
					: Rules.IsTeamBattle() ? TEXT("TEAM BATTLE") : TEXT("FREE FOR ALL");
				CT.Text(English, 34.0f, 26.0f, 18.0f, ModeColor, CP::ETextAlign::Left, TEXT("BlackItalic"));
				const FString Kind = ChaosImpactMatch::DescribeRule(Rules).Replace(TEXT("  "), TEXT(" "));
				CP::HudText3D(CP::HudAbove(CT, 1), Kind, 34.0f, 52.0f, CP::HudFit(Kind, 44.0f, CW - 68.0f), Paper, CP::ETextAlign::Left, TEXT("Black"), 3.0f);
				// The emblem.
				PaintModeEmblem(CP::HudAbove(C, 2), Rules, FVector2D(CW * 0.5f, 196.0), 72.0f, WithAlpha(FMath::Lerp(ModeColor, Paper, 0.3f), 0.85f), T);
				// How it goes, three points.
				TArray<FString> Points;
				if (Rules.bTournament)
				{
					Points.Add(Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? TEXT("同じ回戦の試合を全部いっしょに・CPU同士は裏で決着")
						: TEXT("1試合ずつ順番に・CPU同士は観るかとばすか選べる"));
					Points.Add(FString::Printf(TEXT("全試合1対1・%d本先取の勝ちぬき戦"), ChaosImpactMatch::DuelRoundsToWin));
					Points.Add(TEXT("待ち時間は練習／観戦を自由に"));
				}
				else if (Rules.bDuel)
				{
					Points.Add(Rules.CPUCount > 0 ? FString::Printf(TEXT("あなた 対 CPU（%s）"), ChaosImpactMatch::GetCPULevelName(Rules.CPULevel))
						: FString(TEXT("プレイヤー同士の1対1")));
					Points.Add(FString::Printf(TEXT("%d本先取・体力3・特殊ボールはスロットで%d種類"), ChaosImpactMatch::DuelRoundsToWin, ChaosImpactMatch::DuelBallPicks));
					Points.Add(FString::Printf(TEXT("時間切れは体力の多いほう、同じなら%d秒のサドンデス"), FMath::RoundToInt(ChaosImpactMatch::DuelSuddenDeathSeconds)));
				}
				else if (Rules.bBattleRoyale)
				{
					Points.Add(Rules.IsTeamBattle() ? TEXT("最後まで生き残ったチームの勝ち") : TEXT("最後の1人になれば勝利"));
					Points.Add(Rules.IsTeamBattle() ? TEXT("時間切れは生存人数 → 残りHP合計で判定") : TEXT("時間切れは残りHPで判定"));
					Points.Add(TEXT("同率首位だけでサドンデス・復活なし"));
				}
				else if (Rules.IsTeamBattle())
				{
					Points.Add(FString::Printf(TEXT("%dチーム・1チーム最大%d人"), Rules.TeamCount, ChaosImpactMatch::GetTeamCapacity(Rules.TeamCount, Total)));
					Points.Add(TEXT("チームの合計ポイントで勝負"));
					Points.Add(TEXT("敵に当てる +1pt ・ 撃破ボーナス +1pt"));
				}
				else
				{
					Points.Add(TEXT("ポイントが一番多い人の勝ち"));
					Points.Add(TEXT("敵に当てる +1pt ・ 撃破ボーナス +1pt"));
					Points.Add(FString::Printf(TEXT("%d分間の勝負"), Rules.Minutes));
				}
				for (int32 Index = 0; Index < Points.Num(); ++Index)
				{
					const float Y = 298.0f + Index * 42.0f;
					CT.Disc(FVector2D(44.0, Y + 14.0), 5.0f, ModeColor);
					CT.Text(Points[Index], 62.0f, Y, CP::HudFit(Points[Index], 21.0f, CW - 96.0f, TEXT("Bold")), Index == 0 ? Paper : WithAlpha(Paper, 0.8f),
						CP::ETextAlign::Left, TEXT("Bold"));
				}
				const FString Stage = Rules.bDuel || Rules.bTournament ? FString::Printf(TEXT("ステージ：%s"), ChaosImpactMatch::GetDuelStageName())
					: FString(TEXT("ステージは次の画面で選びます"));
				CT.Box(34.0f, CH - 60.0f, CW - 68.0f, 1.0f, WithAlpha(Paper, 0.14f));
				CT.Text(Stage, 34.0f, CH - 46.0f, CP::HudFit(Stage, 18.0f, CW - 68.0f, TEXT("Bold")), Muted, CP::ETextAlign::Left, TEXT("Bold"));
			}
			if (bBallSwitch)
			{
				// ボールスイッチ: the page dimmed under it, a heading, how many are on, and how to use it.
				const float Veil = EaseOut(static_cast<float>(FPlatformTime::Seconds() - BallSwitchAt) / 0.2f);
				FMenuPainter{AllottedGeometry, OutDrawElements, BaseLayer + 7}.Box(0.0f, 0.0f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y,
					FLinearColor(0.0f, 0.004f, 0.02f, 0.9f * Veil));
				const CP::FPainter Head{DesignGeometry, OutDrawElements, BaseLayer + 8, Veil};
				CP::HudText3D(Head, TEXT("ボールスイッチ"), 110.0f, 92.0f, 54.0f, Paper, CP::ETextAlign::Left, TEXT("Black"), 3.0f);
				const int32 On = ChaosImpactBallSwitch::CountOn(Rules);
				const int32 All = ChaosImpactBallSwitch::GetSwitchable().Num();
				const FString Count = FString::Printf(TEXT("オン %d / %d"), On, All);
				const float ChipX = 110.0f + CP::HudMeasure(TEXT("ボールスイッチ"), 54.0f) + 36.0f;
				CP::RoundedBox(Head, ChipX, 112.0f, CP::HudMeasure(Count, 22.0f) + 36.0f, 40.0f, 20.0f, On > 0 ? FLinearColor(0.12f, 0.6f, 0.32f, 1.0f) : Fire);
				CP::HudAbove(Head, 1).Text(Count, ChipX + 18.0f, 117.0f, 22.0f, Paper, CP::ETextAlign::Left, TEXT("Black"));
				const FString Note = On == 0 ? TEXT("全部オフ：ふつうのボールだけになります")
					: TEXT("オフにした特殊ボールは出てきません（デュエル・トーナメントのスロットにも）");
				Head.Text(Note, 1490.0f, 112.0f, CP::HudFit(Note, 18.0f, 740.0f, TEXT("Bold")), On == 0 ? Gold : Muted, CP::ETextAlign::Right, TEXT("Bold"));
				Head.Text(TEXT("十字 / 矢印：えらぶ　　A / Enter：オン・オフ　　B / Esc：もどる"), 1490.0f, 146.0f, 17.0f, Muted, CP::ETextAlign::Right,
					TEXT("Bold"));
			}
			if (bModePicker)
			{
				// The list of every kind: the page dimmed under it, a heading, and each row's kind on its left.
				const float Veil = EaseOut(static_cast<float>(FPlatformTime::Seconds() - ModePickerAt) / 0.2f);
				FMenuPainter{AllottedGeometry, OutDrawElements, BaseLayer + 7}.Box(0.0f, 0.0f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y,
					FLinearColor(0.0f, 0.004f, 0.02f, 0.9f * Veil));
				const CP::FPainter Head{DesignGeometry, OutDrawElements, BaseLayer + 8, Veil};
				CP::HudText3D(Head, TEXT("ルールをえらぶ"), 110.0f, 100.0f, 54.0f, Paper, CP::ETextAlign::Left, TEXT("Black"), 3.0f);
				Head.Text(TEXT("十字 / 矢印：えらぶ　　A / Enter：決定　　B / Esc：もどる"), 1490.0f, 128.0f, 17.0f, Muted, CP::ETextAlign::Right, TEXT("Bold"));
				const TCHAR* Kinds[] = {TEXT("バトル"), TEXT("バトルロイヤル"), TEXT("そのほか")};
				const FLinearColor KindColors[] = {Fire, Gold, Violet};
				for (int32 GridRow = 0; GridRow < 3; ++GridRow)
				{
					const float Top = 214.0f + GridRow * 158.0f;
					Head.Box(110.0f - GridRow * 15.0f, Top + 10.0f, 6.0f, 120.0f, KindColors[GridRow]);
					Head.Text(Kinds[GridRow], 128.0f - GridRow * 15.0f, Top + 54.0f, CP::HudFit(Kinds[GridRow], 24.0f, 150.0f), KindColors[GridRow],
						CP::ETextAlign::Left, TEXT("Black"));
				}
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
		PaintTeamCards(DesignGeometry, OutDrawElements, BaseLayer + 3, T);
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
		else if ((Screen == EChaosImpactScreen::ModeSelect || Screen == EChaosImpactScreen::VSSelect
			|| Screen == EChaosImpactScreen::OnlinePrivate) && (Index < 2 || (Screen == EChaosImpactScreen::VSSelect && Index == 3)))
		{
			PaintCard(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title,
				Entry.Accent, Blend, bPressed, EntryIn, T, 118.0f, 0);
		}
		else if (Screen == EChaosImpactScreen::MultiReady && !Entry.Number.IsEmpty())
		{
			PaintOnlineTile(DesignGeometry, OutDrawElements, BaseLayer + 3, Rect, Entry.Number, Entry.Title, Entry.Detail, Entry.Accent,
				Entry.bDisabled ? 0.0f : Blend, bPressed, Entry.bDisabled, EntryIn, T);
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
		else if (Screen == EChaosImpactScreen::MatchRules && bBallSwitch && Entry.Detail == TEXT("ball"))
		{
			// A special ball: its ball (greyed and struck through when off), its name, ON / OFF.
			namespace CP = ChaosImpactPaint;
			const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(FCString::Atoi(*Entry.Number));
			const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			const bool bOn = !Owner || ChaosImpactBallSwitch::IsOn(Owner->GetPendingMatchRules(), Kind);
			const float TileIn = EaseOut(static_cast<float>(FPlatformTime::Seconds() - BallSwitchAt - 0.015 * Index) / 0.25f);
			const float B = EaseOut(Blend);
			const float W = static_cast<float>(Rect.GetSize().X);
			const float H = static_cast<float>(Rect.GetSize().Y);
			const FLinearColor Color = ChaosImpactBallTypes::GetColor(Kind);
			const FGeometry Card = MakeSkewed(DesignGeometry, static_cast<float>(Rect.Left), static_cast<float>(Rect.Top) - 8.0f * B, W, H, -0.06f,
				(1.0f + 0.03f * B) * (bPressed ? 0.96f : 1.0f));
			const CP::FPainter Tile{Card, OutDrawElements, BaseLayer + 9, TileIn};
			Tile.Box(8.0f, 10.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			FLinearColor Deep = Color * (bOn ? 0.22f + 0.18f * B : 0.05f);
			Deep.A = 0.97f;
			CP::HudPoly(Tile, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)},
				CP::MakeFill(bOn ? Deep : FLinearColor(0.04f, 0.045f, 0.06f, 0.97f), FLinearColor(0.008f, 0.01f, 0.025f, 0.97f)));
			const CP::FPainter Up = CP::HudAbove(Tile, 1);
			Up.Box(0.0f, 0.0f, W, 6.0f, bOn ? Color : WithAlpha(Muted, 0.5f));
			const FVector2D Ball(W * 0.5f, H * 0.4f);
			if (bOn)
			{
				Up.Disc(Ball, 52.0f + 6.0f * B * (0.5f + 0.5f * FMath::Sin(T * 4.0f)), WithAlpha(Color, 0.2f + 0.15f * B));
			}
			const CP::FPainter Icon{Card, OutDrawElements, BaseLayer + 11, TileIn * (bOn ? 1.0f : 0.35f)};
			UChaosImpactChargeWidget::PaintBallIcon(Icon, Ball, 38.0f, true, Kind, 0.0f, T);
			const CP::FPainter Top = CP::HudAbove(Tile, 6);
			if (!bOn)
			{
				Top.Line(Ball + FVector2D(-46.0, 46.0), Ball + FVector2D(46.0, -46.0), FLinearColor(0.95f, 0.2f, 0.25f, 0.9f), 7.0f);
			}
			const FString Name = Entry.Title;
			Top.Text(Name, W * 0.5f, H - 74.0f, CP::HudFit(Name, 22.0f, W - 24.0f), bOn ? Paper : Muted, CP::ETextAlign::Center, TEXT("Black"));
			const FString State = bOn ? TEXT("ON") : TEXT("OFF");
			const float SW = 64.0f;
			CP::RoundedBox(Top, W * 0.5f - SW * 0.5f, H - 40.0f, SW, 26.0f, 13.0f, bOn ? FLinearColor(0.12f, 0.62f, 0.32f, 1.0f) : FLinearColor(0.3f, 0.32f, 0.38f, 1.0f));
			CP::HudAbove(Top, 1).Text(State, W * 0.5f, H - 38.0f, 16.0f, Paper, CP::ETextAlign::Center, TEXT("BlackItalic"));
			if (B > 0.01f)
			{
				CP::HudAbove(Top, 2).Outline(-5.0f, -5.0f, W + 10.0f, H + 10.0f, WithAlpha(Paper, B * (0.6f + 0.3f * FMath::Sin(T * 5.0f))), 3.0f);
			}
		}
		else if (Screen == EChaosImpactScreen::MatchRules && bBallSwitch)
		{
			PaintBar(DesignGeometry, OutDrawElements, BaseLayer + 9, Rect, Entry.Title, Entry.Accent, Blend, bPressed, false, 1.0f, T);
		}
		else if (Screen == EChaosImpactScreen::MatchRules && bModePicker && Entry.Detail == TEXT("mode"))
		{
			const float TileIn = EaseOut(static_cast<float>(FPlatformTime::Seconds() - ModePickerAt - 0.02 * Index) / 0.25f);
			const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			const int32 Mode = FCString::Atoi(*Entry.Number);
			PaintModeTile(DesignGeometry, OutDrawElements, BaseLayer + 9, Rect, Mode, Entry.Title, Entry.Accent, Blend, bPressed, Entry.bDisabled,
				Owner && Owner->GetMatchModeChoice() == Mode, Owner ? Owner->GetMatchModeBlocker(FMath::Min(Mode, 10)) : FString(), TileIn, T);
		}
		else if (Screen == EChaosImpactScreen::MatchRules && bModePicker)
		{
			PaintBar(DesignGeometry, OutDrawElements, BaseLayer + 9, Rect, Entry.Title, Entry.Accent, Blend, bPressed, Entry.bDisabled, 1.0f, T);
		}
		else if (Screen == EChaosImpactScreen::MatchRules && Index < 4)
		{
			PaintRuleRow(DesignGeometry, OutDrawElements, BaseLayer + 4, Rect, Entry.Title, Entry.Number, Entry.Accent, Blend, bPressed,
				Entry.bDisabled, Entry.Detail == TEXT("fixed"), EntryIn, T);
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
		else if (Screen == EChaosImpactScreen::Pause && PauseConfirm.IsEmpty())
		{
			const float ItemIn = EaseOut((T - 0.08f - 0.04f * Index) / 0.3f);
			const FSlateRect ItemRect = Rect.OffsetBy(FVector2D((1.0f - ItemIn) * -80.0f, 0.0f));
			PaintPauseItem(DesignGeometry, OutDrawElements, BaseLayer + 4, ItemRect, Entry.Title, Entry.Accent, Blend, bPressed,
				Entry.bDisabled, ItemIn, T, Index + 1);
		}
		else
		{
			PaintBar(DesignGeometry, OutDrawElements, BaseLayer + (Screen == EChaosImpactScreen::Pause ? 8 : 4), Rect, Entry.Title, Entry.Accent,
				Blend, bPressed, Entry.bDisabled, EntryIn, T);
		}
	}

	if (bFrontEnd && Screen != EChaosImpactScreen::Title)
	{
		PaintEnterWipe(DesignGeometry, OutDrawElements, BaseLayer + 6, T);
	}
	if (SecretChoice != INDEX_NONE)
	{
		namespace CP = ChaosImpactPaint;
		const float In = EaseOut(static_cast<float>(FPlatformTime::Seconds() - SecretChoiceAt) / 0.25f);
		const CP::FPainter Shade{AllottedGeometry, OutDrawElements, BaseLayer + 8, In};
		Shade.Box(0.0f, 0.0f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, FLinearColor(0.0f, 0.005f, 0.02f, 0.82f));
		const CP::FPainter Card{DesignGeometry, OutDrawElements, BaseLayer + 9, In}, Word{DesignGeometry, OutDrawElements, BaseLayer + 10, In};
		CP::HudText3D(Word, TEXT("どちらで遊ぶ？"), 800.0f, 214.0f, 54.0f, CP::Paper, CP::ETextAlign::Center, TEXT("BlackItalic"), 3.0f);
		const TCHAR* const Names[] = {TEXT("RPG"), TEXT("バトルロイヤル")};
		const TCHAR* const Notes[] = {TEXT("冒険と戦いの試作版"), TEXT("60人・ボールで最後の1チームへ")};
		const FLinearColor Tints[] = {CP::Ice, CP::Fire};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FBox2D R = SecretChoiceRect(Index);
			const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y), W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
			const bool bOn = SecretChoice == Index;
			FLinearColor Deep = Tints[Index] * 0.25f;
			Deep.A = 0.97f;
			CP::HudPoly(Card, {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
				bOn ? CP::MakeFill(Tints[Index] * FLinearColor(0.7f, 0.7f, 0.7f, 1.0f), Deep) : CP::MakeFill(FLinearColor(0.05f, 0.07f, 0.13f, 0.96f), FLinearColor(0.01f, 0.014f, 0.03f, 0.96f)));
			Word.Box(X, Y, W, 6.0f, Tints[Index]);
			if (bOn) { Word.Outline(X, Y, W, H, CP::Gold, 3.0f); }
			Word.Text(Names[Index], X + W * 0.5f, Y + 92.0f, CP::HudFit(Names[Index], 50.0f, W - 40.0f, TEXT("Black")), CP::Paper, CP::ETextAlign::Center, TEXT("Black"), 2.0f, CP::Ink);
			Word.Text(Notes[Index], X + W * 0.5f, Y + 176.0f, CP::HudFit(Notes[Index], 22.0f, W - 40.0f, TEXT("Bold")), CP::WithAlpha(CP::Paper, 0.85f), CP::ETextAlign::Center, TEXT("Bold"));
		}
		const FString Hint = TEXT("← →：えらぶ　　決定：はじめる　　戻る：閉じる");
		Word.Text(Hint, 800.0f, 660.0f, CP::HudFit(Hint, 20.0f, 1000.0f, TEXT("Bold")), CP::WithAlpha(CP::Paper, 0.7f), CP::ETextAlign::Center, TEXT("Bold"));
		return BaseLayer + 11;
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
	if (Screen == EChaosImpactScreen::MatchRules && bBallSwitch && Key != EKeys::Tab)
	{
		// Two rows of seven, then すべてオン (14) and もどる (15) along the bottom.
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
		const int32 Tiles = Entries.Num() - 2;
		if (SelectedIndex < Tiles)
		{
			const int32 Row = SelectedIndex / 7;
			int32 Column = SelectedIndex % 7;
			if (bLeft || bRight)
			{
				Column = (Column + (bRight ? 1 : 6)) % 7;
				SelectedIndex = FMath::Min(Row * 7 + Column, Tiles - 1);
			}
			else if (bUp)
			{
				SelectedIndex = Row == 0 ? Tiles + 1 : Column;
			}
			else
			{
				SelectedIndex = Row == 0 ? FMath::Min(7 + Column, Tiles - 1) : (Column <= 1 ? Tiles + 1 : Tiles);
			}
			if (SelectedIndex < Tiles)
			{
				LastBallSwitchTile = SelectedIndex;
			}
		}
		else if (bLeft || bRight)
		{
			SelectedIndex = SelectedIndex == Tiles ? Tiles + 1 : Tiles;
		}
		else
		{
			SelectedIndex = bUp ? FMath::Clamp(LastBallSwitchTile >= 7 ? LastBallSwitchTile : LastBallSwitchTile + 7, 0, Tiles - 1)
				: FMath::Clamp(LastBallSwitchTile % 7, 0, Tiles - 1);
		}
		return;
	}
	if (Screen == EChaosImpactScreen::MatchRules && bModePicker && Key != EKeys::Tab)
	{
		// Rows of 4 tiles, then もどる (index 12).
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
		const int32 RowStarts[] = {0, 4, 8, 12};
		const int32 RowLengths[] = {4, 4, 4, 1};
		int32 GridRow = SelectedIndex >= 12 ? 3 : SelectedIndex >= 8 ? 2 : SelectedIndex >= 4 ? 1 : 0;
		int32 Column = SelectedIndex - RowStarts[GridRow];
		if (bLeft || bRight)
		{
			Column = (Column + (bRight ? 1 : -1) + RowLengths[GridRow]) % RowLengths[GridRow];
		}
		else
		{
			GridRow = (GridRow + (bUp ? -1 : 1) + 4) % 4;
			Column = FMath::Min(Column, RowLengths[GridRow] - 1);
		}
		SelectedIndex = RowStarts[GridRow] + Column;
		return;
	}
	if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		Controller && (bLeft || bRight))
	{
		// Rule rows change their value sideways; team select moves P1 between teams.
		if (Screen == EChaosImpactScreen::TourneyCreate && SelectedIndex >= 1 && SelectedIndex <= 5)
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				Controller->AdjustTourneySetting(SelectedIndex, bRight ? 1 : -1);
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiValue);
				BuildEntries();
			}
			return;
		}
		if (Screen == EChaosImpactScreen::MatchRules && !bModePicker && !bBallSwitch && SelectedIndex <= 3)
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
		else if (bLeft || bRight)
		{
			// Round its own row (the last row may be short).
			const int32 RowStart = (SelectedIndex / 3) * 3;
			const int32 InRow = FMath::Min(3, Back - RowStart);
			SelectedIndex = RowStart + (SelectedIndex - RowStart + (bRight ? 1 : InRow - 1)) % InRow;
		}
		else if (bBack) { SelectedIndex = SelectedIndex < 3 ? Back : SelectedIndex - 3; }
		else { SelectedIndex = SelectedIndex + 3 >= Back ? Back : SelectedIndex + 3; }
	}
	else if (Screen == EChaosImpactScreen::MatchEnd && Key != EKeys::Tab)
	{
		NavigateResults(Key);
	}
	else if (Screen == EChaosImpactScreen::ModeSelect && Key != EKeys::Tab && Entries.Num() == 7)
	{
		// The mosaic: ソロ (0) VS (1) 訓練場 (6) / トレーニング (2) カラーエディット (5) 設定 (4) / タイトルへ (3).
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
		const bool bDown = Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down;
		const int32 ToRight[] = {1, 6, 5, 3, 2, 4, 0};
		const int32 ToLeft[] = {6, 0, 4, 3, 5, 2, 1};
		const int32 Down[] = {2, 5, 3, 0, 3, 3, 4};
		const int32 Up[] = {3, 3, 0, 2, 6, 1, 3};
		const int32 From = FMath::Clamp(SelectedIndex, 0, 6);
		if (bUp && From == 3)
		{
			// Up from タイトルへ: back to the tile last left for it.
			SelectedIndex = LastModeBottom >= 0 && LastModeBottom <= 6 && LastModeBottom != 3 ? LastModeBottom : 2;
		}
		else
		{
			if ((bDown && Down[From] == 3) || (bUp && Up[From] == 3))
			{
				LastModeBottom = From;
			}
			SelectedIndex = bRight ? ToRight[From] : bLeft ? ToLeft[From] : bUp ? Up[From] : bDown ? Down[From] : From;
		}
	}
	else if (Screen == EChaosImpactScreen::MultiReady && Key != EKeys::Tab && Entries.Num() == 9)
	{
		// Rows: 8人 (0), 1v1 (1), トーナメント (6) / 合言葉 (2), 戦績 (3), 回線チェック (8) / 戻る (5), カラーエディット (7), なまえ (4).
		const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
		const int32 ToRight[] = {1, 6, 3, 8, 5, 7, 0, 4, 2};
		const int32 ToLeft[] = {6, 0, 8, 2, 7, 4, 1, 5, 3};
		const int32 Down[] = {2, 3, 5, 7, 6, 0, 8, 1, 4};
		const int32 Up[] = {5, 7, 0, 1, 8, 2, 4, 3, 6};
		SelectedIndex = bRight ? ToRight[SelectedIndex] : bLeft ? ToLeft[SelectedIndex] : bUp ? Up[SelectedIndex] : Down[SelectedIndex];
	}
	else if (Screen == EChaosImpactScreen::LineCheck && Key != EKeys::Tab)
	{
		SelectedIndex = 1 - FMath::Clamp(SelectedIndex, 0, 1);
	}
	else if (Screen == EChaosImpactScreen::ModeSelect && Key != EKeys::Tab)
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
	else if (Screen == EChaosImpactScreen::VSSelect && Key != EKeys::Tab && Entries.Num() == 4)
	{
		// ローカル / 通信 / トーナメント across, the back button under them.
		const bool bHorizontal = Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
		const bool bForward = Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right;
		const int32 Across[] = {0, 1, 3};
		if (bHorizontal && SelectedIndex != 2)
		{
			const int32 At = SelectedIndex == 3 ? 2 : SelectedIndex;
			SelectedIndex = Across[(At + (bForward ? 1 : 2)) % 3];
		}
		else if (!bHorizontal)
		{
			SelectedIndex = SelectedIndex == 2 ? 0 : 2;
		}
	}
	else if ((Screen == EChaosImpactScreen::VSSelect || Screen == EChaosImpactScreen::SoloStageSelect
		|| Screen == EChaosImpactScreen::OnlinePrivate) && Key != EKeys::Tab)
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

FString UChaosImpactMenuWidget::GetPauseContext() const
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Controller)
	{
		return FString();
	}
	if (Controller->IsOnlineRoom() && Room)
	{
		if (Room->Matchmade == EChaosImpactMatchmade::Duel)
		{
			return TEXT("ランダムマッチ  1v1");
		}
		if (Room->Matchmade == EChaosImpactMatchmade::Random)
		{
			return TEXT("ランダムマッチ  8人");
		}
		if (Room->IsMatchmade())
		{
			return TEXT("ランクマッチ");
		}
		const FString Name = Room->RoomName.IsEmpty() ? FString::Printf(TEXT("あいことば %s"), *Room->RoomPassword) : Room->RoomName;
		return FString::Printf(TEXT("%s  ・  %s"), Room->bPublicTournament || Room->IsTournament() ? TEXT("トーナメント") : TEXT("合言葉マッチ"), *Name);
	}
	if (Controller->IsSearchingForRoom())
	{
		return TEXT("オンライン  ・  対戦相手をさがしています");
	}
	if (Controller->IsSoloWorldMap())
	{
		return TEXT("ソロ  ・  ワールドマップ");
	}
	if (Controller->IsSoloMode())
	{
		return TEXT("ソロ");
	}
	if (Controller->IsTrainingMode())
	{
		return TEXT("トレーニング");
	}
	return FString();
}

void UChaosImpactMenuWidget::GetPauseConfirmText(FString& OutQuestion, FString& OutNote) const
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bHost = Controller && Controller->IsOnlineRoomHost();
	OutNote.Reset();
	if (PauseConfirm == TEXT("leave"))
	{
		if (Room && Room->bPublicTournament)
		{
			OutQuestion = bHost ? TEXT("本当に大会を中止してもよろしいですか？") : TEXT("本当に大会から抜けてもよろしいですか？");
			OutNote = bHost ? TEXT("参加者全員が大会から抜けます") : Room->IsTournament() ? TEXT("このあとの試合はすべて不戦敗になります") : TEXT("");
		}
		else if (bHost && Room && !Room->IsMatchmade())
		{
			OutQuestion = TEXT("本当に部屋を解散してもよろしいですか？");
			OutNote = Room->IsTournament() ? TEXT("トーナメントは中止され、全員が部屋から抜けます") : TEXT("メンバー全員が部屋から抜けます");
		}
		else
		{
			OutQuestion = TEXT("本当に部屋から抜けてもよろしいですか？");
			OutNote = Room && Room->IsTournament() ? TEXT("このあとの試合はすべて不戦敗になります") : TEXT("");
		}
	}
	else if (PauseConfirm == TEXT("close_recruit"))
	{
		OutQuestion = TEXT("メンバーの募集を終了しますか？");
		OutNote = TEXT("このあとルールを決めます（募集はあとで再開できます）");
	}
	else if (PauseConfirm == TEXT("surrender"))
	{
		OutQuestion = TEXT("本当に降参しますか？");
		OutNote = Room && Room->IsDuel() ? TEXT("この試合は負けになります") : TEXT("この試合から抜けます");
	}
	else if (PauseConfirm == TEXT("stop_search"))
	{
		OutQuestion = TEXT("さがすのをやめますか？");
	}
	else if (PauseConfirm == TEXT("online_menu"))
	{
		OutQuestion = TEXT("オンラインメニューへ戻りますか？");
		OutNote = TEXT("さがすのをやめます");
	}
	else if (PauseConfirm == TEXT("goto_title"))
	{
		OutQuestion = TEXT("タイトル画面へ戻りますか？");
	}
	else if (PauseConfirm == TEXT("goto_mode"))
	{
		OutQuestion = TEXT("モード選択へ戻りますか？");
	}
}

FBox2D UChaosImpactMenuWidget::DojoInviteButton(const int32 Index)
{
	const float Left = Index == 0 ? 470.0f : 820.0f;
	return FBox2D(FVector2D(Left, 730.0f), FVector2D(Left + 310.0f, 796.0f));
}

void UChaosImpactMenuWidget::CloseDojoInvite(const bool bGo)
{
	bDojoInvite = false;
	ChaosImpactSfx::Play2D(this, bGo ? EChaosImpactSfx::UiConfirm : EChaosImpactSfx::UiBack);
	if (bGo)
	{
		if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer()))
		{
			Controller->OpenDojo();
		}
	}
	else
	{
		// Left on the mode select, on 訓練場.
		SelectedIndex = 6;
	}
}

bool UChaosImpactMenuWidget::HandleDojoInviteKey(const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	// (A press as it opens is not taken.)
	if (FPlatformTime::Seconds() - DojoInviteAt < 0.45)
	{
		return true;
	}
	const bool bSwap = ChaosImpactSettings::IsSwitchConfirmRight();
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::A || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Left
		|| Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right)
	{
		DojoInviteChoice = 1 - DojoInviteChoice;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiSelect);
	}
	else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == (bSwap ? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom))
	{
		CloseDojoInvite(DojoInviteChoice == 0);
	}
	else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == (bSwap ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right))
	{
		CloseDojoInvite(false);
	}
	return true;
}

void UChaosImpactMenuWidget::PaintDojoInvite(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const float Time) const
{
	namespace CP = ChaosImpactPaint;
	const float In = EaseOut(Time / 0.35f);
	const FLinearColor Teal(0.08f, 0.86f, 0.78f, 1.0f);
	// The menu dimmed under it.
	CP::FPainter{Design, Elements, Layer, In}.Box(-400.0f, -300.0f, 2400.0f, 1500.0f, FLinearColor(0.0f, 0.005f, 0.02f, 0.72f));
	// The card, rising in a little.
	const float Rise = (1.0f - In) * 40.0f;
	const float CX = 400.0f;
	const float CY = 86.0f + Rise;
	const float CW = 800.0f;
	const float CH = 736.0f;
	const CP::FPainter Card{Design, Elements, Layer + 1, In};
	CP::RoundedBox(Card, CX + 10.0f, CY + 14.0f, CW, CH, 34.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	CP::RoundedBox(Card, CX - 6.0f, CY - 6.0f, CW + 12.0f, CH + 12.0f, 38.0f, WithAlpha(Teal, 0.9f));
	CP::RoundedBox(CP::HudAbove(Card, 1), CX, CY, CW, CH, 32.0f, FLinearColor(0.035f, 0.06f, 0.13f, 0.98f));
	// Faint stripes across it, leaning.
	const CP::FPainter Stripes = CP::HudAbove(Card, 2);
	for (int32 Band = 0; Band < 9; ++Band)
	{
		const float X = CX + 40.0f + Band * 90.0f + FMath::Fmod(Time * 18.0f, 90.0f);
		if (X + 60.0f < CX + CW - 30.0f)
		{
			CP::HudPoly(Stripes, {FVector2D(X + 30.0f, CY + 20.0f), FVector2D(X + 60.0f, CY + 20.0f), FVector2D(X + 30.0f, CY + CH - 20.0f),
				FVector2D(X, CY + CH - 20.0f)}, CP::SolidFill(WithAlpha(Teal, 0.035f)), 1);
		}
	}
	// The ribbon across the top: the words, dark on white.
	const CP::FPainter Ribbon = CP::HudAbove(Card, 3);
	CP::HudPoly(Ribbon, {FVector2D(CX + 60.0f, CY + 26.0f), FVector2D(CX + CW - 60.0f, CY + 26.0f), FVector2D(CX + CW - 30.0f, CY + 70.0f),
		FVector2D(CX + CW - 60.0f, CY + 114.0f), FVector2D(CX + 60.0f, CY + 114.0f), FVector2D(CX + 30.0f, CY + 70.0f)},
		CP::MakeFill(Paper, FLinearColor(0.82f, 0.88f, 0.95f, 1.0f)), 4);
	CP::HudPoly(Ribbon, {FVector2D(CX + 30.0f, CY + 70.0f), FVector2D(CX + 60.0f, CY + 26.0f), FVector2D(CX + 74.0f, CY + 26.0f), FVector2D(CX + 44.0f, CY + 70.0f),
		FVector2D(CX + 74.0f, CY + 114.0f), FVector2D(CX + 60.0f, CY + 114.0f)}, CP::SolidFill(Teal), 1);
	CP::HudPoly(Ribbon, {FVector2D(CX + CW - 30.0f, CY + 70.0f), FVector2D(CX + CW - 60.0f, CY + 26.0f), FVector2D(CX + CW - 74.0f, CY + 26.0f),
		FVector2D(CX + CW - 44.0f, CY + 70.0f), FVector2D(CX + CW - 74.0f, CY + 114.0f), FVector2D(CX + CW - 60.0f, CY + 114.0f)}, CP::SolidFill(Teal), 1);
	const FString Head = TEXT("「訓練場」で あそんでみよう！");
	CP::HudAbove(Ribbon, 1).Text(Head, CX + CW * 0.5f, CY + 42.0f, CP::HudFit(Head, 36.0f, CW - 200.0f), CP::Navy, CP::ETextAlign::Center, TEXT("Black"));
	// The picture: the lessons' own clips, in a frame.
	const float VW = 620.0f;
	const float VH = VW * 9.0f / 16.0f;
	const float VX = CX + (CW - VW) * 0.5f;
	const float VY = CY + 140.0f;
	const CP::FPainter Frame = CP::HudAbove(Card, 3);
	CP::RoundedBox(Frame, VX - 8.0f, VY - 8.0f, VW + 16.0f, VH + 16.0f, 16.0f, WithAlpha(Teal, 0.95f));
	CP::RoundedBox(CP::HudAbove(Frame, 1), VX - 3.0f, VY - 3.0f, VW + 6.0f, VH + 6.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	if (DojoInviteMenu)
	{
		// (The list is laid out on the menu's 1600 x 900: drawn scaled into the frame, nothing of it outside.)
		const FGeometry Small = Design.MakeChild(FVector2f(1600.0f, 900.0f), FSlateLayoutTransform(VW / 1600.0f, FVector2f(VX, VY)));
		Elements.PushClip(FSlateClippingZone(Design.ToPaintGeometry(FVector2f(VW, VH), FSlateLayoutTransform(FVector2f(VX, VY)))));
		DojoInviteMenu->Paint(Small, Elements, Layer + 6);
		Elements.PopClip();
	}
	// What it is, and how far this player has come.
	const FString Line = TEXT("ルールと操作を、動画つきのレッスンで 学べる");
	const CP::FPainter Words = CP::HudAbove(Card, 4);
	Words.Text(Line, CX + CW * 0.5f, VY + VH + 22.0f, CP::HudFit(Line, 26.0f, CW - 80.0f), Paper, CP::ETextAlign::Center, TEXT("Black"));
	int32 Lessons = 0;
	int32 Cleared = 0;
	const TArray<FChaosImpactDojoCategory>& Groups = ChaosImpactDojo::GetCategories();
	for (int32 Group = 0; Group < Groups.Num(); ++Group)
	{
		Lessons += Groups[Group].Lessons.Num();
		Cleared += ChaosImpactDojo::CountCleared(Group);
	}
	const FString Progress = FString::Printf(TEXT("クリア %d / %d レッスン"), Cleared, Lessons);
	Words.Text(Progress, CX + CW * 0.5f, VY + VH + 64.0f, 17.0f, WithAlpha(Teal, 0.95f), CP::ETextAlign::Center, TEXT("Bold"));
	// The two choices.
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const bool bPad = Owner && Owner->IsUsingGamepad();
	const TCHAR* Labels[] = {TEXT("訓練場へ行く"), TEXT("あとで")};
	const TCHAR* Keys[] = {bPad ? TEXT("A") : TEXT("Enter"), bPad ? TEXT("B") : TEXT("Esc")};
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FBox2D Box = DojoInviteButton(Index);
		const bool bOn = Index == DojoInviteChoice;
		const float BX = static_cast<float>(Box.Min.X);
		const float BY = static_cast<float>(Box.Min.Y) + Rise - (bOn ? 3.0f : 0.0f);
		const float BW = static_cast<float>(Box.GetSize().X);
		const float BH = static_cast<float>(Box.GetSize().Y);
		const CP::FPainter Button = CP::HudAbove(Card, 5);
		if (bOn)
		{
			CP::RoundedBox(Button, BX - 6.0f - 4.0f * Pulse, BY - 6.0f - 4.0f * Pulse, BW + 12.0f + 8.0f * Pulse, BH + 12.0f + 8.0f * Pulse, BH * 0.5f + 8.0f,
				WithAlpha(Teal, 0.3f + 0.2f * Pulse));
		}
		CP::RoundedBox(CP::HudAbove(Button, 1), BX, BY + 5.0f, BW, BH, BH * 0.5f, WithAlpha(CP::Navy, 0.8f));
		CP::RoundedBox(CP::HudAbove(Button, 2), BX, BY, BW, BH, BH * 0.5f, bOn ? (Index == 0 ? Teal : Paper) : FLinearColor(0.08f, 0.12f, 0.22f, 1.0f));
		const FLinearColor Ink2 = bOn ? CP::Navy : Paper;
		const float KeyW = CP::HudMeasure(Keys[Index], 15.0f) + 18.0f;
		const float LabelSize = CP::HudFit(Labels[Index], 24.0f, BW - KeyW - 60.0f);
		const float LabelW = CP::HudMeasure(Labels[Index], LabelSize);
		const float StartX = BX + (BW - KeyW - 12.0f - LabelW) * 0.5f;
		CP::RoundedBox(CP::HudAbove(Button, 3), StartX, BY + (BH - 26.0f) * 0.5f, KeyW, 26.0f, 7.0f, bOn ? WithAlpha(CP::Navy, 0.85f) : FMath::Lerp(CP::Navy, Ice, 0.45f));
		CP::HudAbove(Button, 4).Text(Keys[Index], StartX + KeyW * 0.5f, BY + (BH - 15.0f * 1.38f) * 0.5f, 15.0f, Paper, CP::ETextAlign::Center, TEXT("Black"));
		CP::HudAbove(Button, 4).Text(Labels[Index], StartX + KeyW + 12.0f, BY + (BH - LabelSize * 1.38f) * 0.5f, LabelSize, Ink2, CP::ETextAlign::Left, TEXT("Black"));
	}
}

void UChaosImpactMenuWidget::OpenBallSwitch(const bool bOpen)
{
	bBallSwitch = bOpen;
	BallSwitchAt = FPlatformTime::Seconds();
	RefreshEntries();
	SelectedIndex = bOpen ? FMath::Clamp(LastBallSwitchTile, 0, Entries.Num() - 1) : 6;
	ChaosImpactSfx::Play2D(this, bOpen ? EChaosImpactSfx::UiOpen : EChaosImpactSfx::UiBack);
}

void UChaosImpactMenuWidget::OpenModePicker(const bool bOpen)
{
	const AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	bModePicker = bOpen;
	ModePickerAt = FPlatformTime::Seconds();
	RefreshEntries();
	if (bOpen)
	{
		// The cursor starts on the kind chosen now.
		const FString Current = FString::FromInt(Controller ? Controller->GetMatchModeChoice() : 0);
		SelectedIndex = FMath::Max(0, Entries.IndexOfByPredicate([&Current](const FMenuEntry& Entry) { return Entry.Number == Current; }));
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiOpen);
	}
	else
	{
		SelectedIndex = 1;
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
		else if (SelectedIndex == 5) { Controller->OpenColourEdit(EChaosImpactScreen::ModeSelect); }
		else if (SelectedIndex == 6) { Controller->OpenDojo(); }
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
		else if (SelectedIndex == 3) { Controller->BeginLocalTourney(); }
		else { GoBack(); }
		break;
	case EChaosImpactScreen::OnlinePlayers:
		if (SelectedIndex == 0 || SelectedIndex == 1)
		{
			Controller->PrepareTrainingControllerAssignment(SelectedIndex + 1);
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
		if (Entries[SelectedIndex].bDisabled)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		}
		else if (SelectedIndex == 0 || SelectedIndex == 1)
		{
			Controller->BeginMatchQueue(SelectedIndex == 0 ? EChaosImpactQueueMode::Random : EChaosImpactQueueMode::Duel);
		}
		else if (SelectedIndex == 2)
		{
			Controller->OpenOnlinePrivate();
		}
		else if (SelectedIndex == 3)
		{
			Controller->OpenOnlineRecord();
		}
		else if (SelectedIndex == 4)
		{
			Controller->BeginOnlineRename();
		}
		else if (SelectedIndex == 6)
		{
			Controller->OpenTourneyList();
		}
		else if (SelectedIndex == 7)
		{
			Controller->OpenColourEdit(EChaosImpactScreen::MultiReady);
		}
		else if (SelectedIndex == 8)
		{
			Controller->OpenLineCheck();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::TourneyList:
		if (Entries[SelectedIndex].bDisabled)
		{
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
		}
		else if (Entries[SelectedIndex].Detail == TEXT("create"))
		{
			Controller->OpenTourneyCreate();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("refresh"))
		{
			Controller->RefreshTourneyList();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("back"))
		{
			GoBack();
		}
		else if (TourneyRowListing.IsValidIndex(SelectedIndex))
		{
			Controller->JoinRoomListing(TourneyRowListing[SelectedIndex]);
		}
		break;
	case EChaosImpactScreen::TourneyCreate:
		if (SelectedIndex == 0)
		{
			Controller->EditTourneyTitle();
		}
		else if (SelectedIndex >= 1 && SelectedIndex <= 5)
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				Controller->AdjustTourneySetting(SelectedIndex, 1);
				BuildEntries();
			}
		}
		else if (Entries[SelectedIndex].Detail == TEXT("create"))
		{
			Controller->CreatePublicTourney();
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::OnlinePrivate:
		if (SelectedIndex == 0 || SelectedIndex == 1)
		{
			Controller->BeginOnlineFlow(SelectedIndex == 0);
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::OnlineRecord:
		GoBack();
		break;
	case EChaosImpactScreen::LineCheck:
		if (Entries[SelectedIndex].Detail == TEXT("again"))
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
				{
					Sessions->StartLineCheck();
				}
				RefreshEntries();
			}
		}
		else
		{
			GoBack();
		}
		break;
	case EChaosImpactScreen::DuelFound:
		if (Entries.IsValidIndex(SelectedIndex) && !Entries[SelectedIndex].bDisabled)
		{
			Controller->AnswerDuelOffer(Entries[SelectedIndex].Detail == TEXT("accept"));
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
		if (bBallSwitch)
		{
			const FString Choice = Entries[SelectedIndex].Detail;
			if (Choice == TEXT("ball"))
			{
				const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(FCString::Atoi(*Entries[SelectedIndex].Number));
				const bool bOn = ChaosImpactBallSwitch::IsOn(Controller->GetPendingMatchRules(), Kind);
				Controller->SetBallSwitchOn(Kind, !bOn);
				ChaosImpactSfx::Play2D(this, bOn ? EChaosImpactSfx::UiBack : EChaosImpactSfx::UiConfirm);
				BuildEntries();
			}
			else if (Choice == TEXT("all_on"))
			{
				Controller->SetAllBallsOn();
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm);
				BuildEntries();
			}
			else
			{
				OpenBallSwitch(false);
			}
			break;
		}
		if (bModePicker)
		{
			if (Entries[SelectedIndex].Detail == TEXT("mode"))
			{
				if (Entries[SelectedIndex].bDisabled)
				{
					ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiDeny);
					break;
				}
				Controller->SetMatchModeChoice(FCString::Atoi(*Entries[SelectedIndex].Number));
			}
			OpenModePicker(false);
		}
		else if (SelectedIndex == 1)
		{
			// The kind of match: every one listed to pick from.
			OpenModePicker(true);
		}
		else if (SelectedIndex <= 3)
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
		else if (SelectedIndex == 6)
		{
			OpenBallSwitch(true);
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
			if (!Entries[SelectedIndex].bDisabled)
			{
				SwitchResultsTab(Choice == TEXT("awards") ? 1 : 0);
			}
		}
		else if (Choice == TEXT("lobby"))
		{
			Controller->ReturnToLobbyFromResults();
		}
		else if (Choice == TEXT("rematch_vote"))
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				Controller->RequestRematch();
			}
		}
		else if (Choice == TEXT("search_again") || Choice == TEXT("online_menu"))
		{
			if (!Entries[SelectedIndex].bDisabled)
			{
				Controller->LeaveMatchmadeResults(Choice == TEXT("search_again"));
			}
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
		if (Entries[SelectedIndex].Detail == TEXT("confirm_no"))
		{
			// Not after all: back to the menu, on the choice that was asked about.
			const FString Asked = PauseConfirm;
			PauseConfirm.Reset();
			BuildEntries();
			SelectedIndex = FMath::Max(0, Entries.IndexOfByPredicate([&Asked](const FMenuEntry& Entry) { return Entry.Detail == Asked; }));
			break;
		}
		if (Entries[SelectedIndex].Detail == TEXT("confirm_yes"))
		{
			// Yes: what was asked about goes ahead (below, as if chosen).
			const FString Asked = PauseConfirm;
			PauseConfirm.Reset();
			BuildEntries();
			const int32 Asking = Entries.IndexOfByPredicate([&Asked](const FMenuEntry& Entry) { return Entry.Detail == Asked; });
			if (Asking == INDEX_NONE)
			{
				break;
			}
			SelectedIndex = Asking;
		}
		else
		{
			// Leaving the room, giving the match up, stopping the search, or (online) heading for the title: asked first.
			const FString Choice = Entries[SelectedIndex].Detail;
			const bool bOnline = Controller->IsOnlineRoom() || Controller->IsSearchingForRoom()
				|| Controller->GetPlayFlow() == EChaosImpactPlayFlow::VersusOnline;
			// (Ending the room's recruitment too: the list moves under the cursor as members come and go.)
			if (Choice == TEXT("leave") || Choice == TEXT("surrender") || Choice == TEXT("stop_search") || Choice == TEXT("close_recruit")
				|| (Choice == TEXT("online_menu") && Controller->IsSearchingForRoom())
				|| (bOnline && (Choice == TEXT("goto_title") || Choice == TEXT("goto_mode"))))
			{
				PauseConfirm = Choice;
				PauseConfirmAt = FPlatformTime::Seconds();
				BuildEntries();
				SelectedIndex = 0;
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiOpen);
				break;
			}
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
		else if (Entries[SelectedIndex].Detail == TEXT("surrender"))
		{
			Controller->Surrender();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("colour_edit"))
		{
			Controller->OpenColourEdit(EChaosImpactScreen::Pause);
		}
		else if (Entries[SelectedIndex].Detail == TEXT("online_menu"))
		{
			Controller->StopRoomSearch();
			Controller->ShowMenuScreen(EChaosImpactScreen::MultiReady);
		}
		else if (Entries[SelectedIndex].Detail == TEXT("stop_search"))
		{
			Controller->StopRoomSearch();
			Controller->ResumeGameplay();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("training_settings"))
		{
			Controller->ResumeGameplay();
			Controller->ToggleTrainingOverlay();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("training_retry"))
		{
			Controller->RetryTraining();
		}
		else if (Entries[SelectedIndex].Detail == TEXT("goto_mode") || Entries[SelectedIndex].Detail == TEXT("goto_title"))
		{
			Controller->ShowMenuScreen(Entries[SelectedIndex].Detail == TEXT("goto_mode")
				? EChaosImpactScreen::ModeSelect : EChaosImpactScreen::Title);
		}
		else if (Entries[SelectedIndex].Detail == TEXT("dojo_retry") || Entries[SelectedIndex].Detail == TEXT("dojo_list"))
		{
			if (AChaosImpactDojo* Dojo = AChaosImpactDojo::Find(Controller->GetWorld()))
			{
				if (Entries[SelectedIndex].Detail == TEXT("dojo_retry"))
				{
					Controller->ResumeGameplay();
					Dojo->RetryLesson();
				}
				else
				{
					Dojo->ReturnToList();
				}
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
			Controller->ToggleTrainingTargets();
			BuildEntries();
		}
		else if (SelectedIndex == 2)
		{
			Controller->ToggleTrainingCPU();
			BuildEntries();
		}
		else if (SelectedIndex == 3)
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
			else { bTrainingTeamsOpen = false; SelectedIndex = 6; }
			BuildEntries(); break;
		}
		if (bTrainingBallPickerOpen)
		{
			if (SelectedIndex < ChaosImpactBallTypes::Count) { Controller->SetTrainingSummonBallType(ChaosImpactBallTypes::FromIndex(SelectedIndex)); }
			CloseTrainingBallPicker(); break;
		}
		if (SelectedIndex == 0)
		{
			Controller->CycleTrainingPlayerCount();
			BuildEntries();
		}
		else if (SelectedIndex == 1)
		{
			Controller->ToggleTrainingTargets();
			BuildEntries();
		}
		else if (SelectedIndex == 2)
		{
			Controller->ToggleTrainingCPU();
			BuildEntries();
		}
		else if (SelectedIndex == 3)
		{
			OpenTrainingBallPicker();
		}
		else if (SelectedIndex == 4)
		{
			Controller->SummonTrainingBall();
		}
		else if (SelectedIndex == 5)
		{
			Controller->ToggleTrainingShoulderCamera();
			BuildEntries();
		}
		else if (SelectedIndex == 6)
		{
			bTrainingTeamsOpen = true; SelectedIndex = 0; BuildEntries();
		}
		else if (SelectedIndex == 7)
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
	// A back pressed on the screen before this one (closing it) can arrive here too, through the other way keys come
	// in: it must not go back a second time (the settings to the mode select, then on to the title).
	if (FPlatformTime::Seconds() - ScreenStartedAt < 0.2)
	{
		return;
	}
	// The same for a back that closes something within the screen (a list in the rules, the training menu's pickers),
	// where the screen itself does not change: the next back waits a moment, so one press never goes two steps.
	if (FPlatformTime::Seconds() - LastBackAt < 0.2)
	{
		return;
	}
	LastBackAt = FPlatformTime::Seconds();
	UE_LOG(LogChaosImpact, Verbose, TEXT("Menu back on screen %d"), static_cast<int32>(Screen));
	// A 1v1 found is answered with its own buttons only (a stray back must not end the search).
	if (Screen == EChaosImpactScreen::DuelFound)
	{
		return;
	}
	if (Screen == EChaosImpactScreen::TrainingOverlay && bTrainingTeamsOpen)
	{ bTrainingTeamsOpen = false; SelectedIndex = 6; BuildEntries(); ChaosImpactSfx::Play2D(this,EChaosImpactSfx::UiBack); return; }
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
			if (!PauseConfirm.IsEmpty())
			{
				const FString Asked = PauseConfirm;
				PauseConfirm.Reset();
				BuildEntries();
				SelectedIndex = FMath::Max(0, Entries.IndexOfByPredicate([&Asked](const FMenuEntry& Entry) { return Entry.Detail == Asked; }));
				break;
			}
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
			Controller->ShowMenuScreen(EChaosImpactScreen::OnlinePrivate);
			break;
		case EChaosImpactScreen::OnlinePrivate:
		case EChaosImpactScreen::OnlineRecord:
		case EChaosImpactScreen::LineCheck:
			Controller->ShowMenuScreen(EChaosImpactScreen::MultiReady);
			break;
		case EChaosImpactScreen::TourneyList:
			Controller->CloseTourneyList();
			break;
		case EChaosImpactScreen::TourneyCreate:
			Controller->OpenTourneyList();
			break;
		case EChaosImpactScreen::OnlineRoomName:
			if (Controller->IsEditingTourneyTitle())
			{
				Controller->SubmitRoomName(FString());
			}
			else if (Controller->IsRenamingRoom())
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
			if (bBallSwitch)
			{
				OpenBallSwitch(false);
				break;
			}
			if (bModePicker)
			{
				OpenModePicker(false);
				break;
			}
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

FBox2D UChaosImpactMenuWidget::SecretChoiceRect(const int32 Index) const
{
	return FBox2D(FVector2D(300.0 + Index * 520.0, 330.0), FVector2D(780.0 + Index * 520.0, 620.0));
}

void UChaosImpactMenuWidget::ConfirmSecretChoice()
{
	AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const int32 Choice = SecretChoice;
	SecretChoice = INDEX_NONE;
	if (Controller)
	{
		if (Choice == 1) { Controller->OpenBattleRoyaleIsland(); }
		else { Controller->OpenExplorationMap(TEXT("?CIRPGTitle=1")); }
	}
}

FReply UChaosImpactMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// (The 訓練場 invitation answers its keys itself, through HandleAnyUserKeyDown: nothing moves under it.)
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite)
	{
		return FReply::Handled();
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(InKeyEvent.GetKey(), InKeyEvent.GetInputDeviceId().GetId());
	if (SecretChoice != INDEX_NONE)
	{
		if (!InKeyEvent.IsRepeat())
		{
			if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right
				|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down) { SecretChoice = 1 - SecretChoice; }
			else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { ConfirmSecretChoice(); }
			else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right) { SecretChoice = INDEX_NONE; }
		}
		return FReply::Handled();
	}
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
			if (AChaosImpactPlayerController::HasBattleRoyaleIsland())
			{
				SecretChoice = 0;
				SecretChoiceAt = FPlatformTime::Seconds();
				return FReply::Handled();
			}
			if (AChaosImpactPlayerController* Controller = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
				Controller && Controller->OpenExplorationMap(TEXT("?CIRPGTitle=1")))
			{
				return FReply::Handled();
			}
		}
	}
	// (Escape and Backspace go back from the entry screen; they claim no place.)
	if (Screen == EChaosImpactScreen::ControllerAssignment && !Key.IsGamepadKey()
		&& !InKeyEvent.IsRepeat() && Key != EKeys::Escape && Key != EKeys::BackSpace)
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && ColourEdit->IsOpen() && ColourEdit->HandleCharacter(InCharEvent.GetCharacter()))
	{
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && TourneyEntry->IsOpen() && TourneyEntry->HandleCharacter(InCharEvent.GetCharacter()))
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

FSlateRect UChaosImpactMenuWidget::TeamCardRect(const int32 Team, const int32 Teams, const int32 Places, const bool bCrashBall)
{
	// As tall as its places need (the header, a row each, the line at its foot), in the middle of the room there is.
	constexpr float Left = 110.0f, Right = 1490.0f, Top = 244.0f, Bottom = 700.0f, Gap = 60.0f;
	const int32 Count = FMath::Max(Teams, 1);
	const float Width = (Right - Left - Gap * (Count - 1)) / Count;
	const float X = Left + Team * (Width + Gap);
	const float Height = FMath::Clamp(122.0f + FMath::Max(Places, 1) * 68.0f + (bCrashBall ? 74.0f : 46.0f), 300.0f, Bottom - Top);
	const float Y = Top + (Bottom - Top - Height) * 0.5f;
	return FSlateRect(X, Y, X + Width, Y + Height);
}

FSlateRect UChaosImpactMenuWidget::TeamCardRectNow(const int32 Team) const
{
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->IsTeamBattle())
	{
		return FSlateRect();
	}
	const int32 Teams = Match->Rules.TeamCount;
	const int32 Places = ChaosImpactMatch::GetTeamCapacity(Teams, Match->GetCompetitors(false).Num() + Match->Rules.CPUCount);
	return TeamCardRect(Team, Teams, Places, Match->Rules.bCrashBall);
}

AChaosImpactPlayerController* UChaosImpactMenuWidget::GetTeamMouseController() const
{
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const UGameInstance* Instance = GetGameInstance();
	const int32 Index = Owner ? Owner->GetLocalPlayerIndexForDevice(true, 0) : INDEX_NONE;
	const ULocalPlayer* Local = Instance && Instance->GetLocalPlayers().IsValidIndex(Index) ? Instance->GetLocalPlayers()[Index] : nullptr;
	return Local ? Cast<AChaosImpactPlayerController>(Local->GetPlayerController(GetWorld())) : nullptr;
}

int32 UChaosImpactMenuWidget::PaintTeamCards(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer,
	const float T) const
{
	namespace CP = ChaosImpactPaint;
	using CP::ETextAlign;
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->IsTeamBattle())
	{
		return Layer;
	}
	const int32 Teams = Match->Rules.TeamCount;
	const TArray<AChaosImpactPlayerState*> Humans = Match->GetCompetitors(false);
	const int32 Capacity = ChaosImpactMatch::GetTeamCapacity(Teams, Humans.Num() + Match->Rules.CPUCount);
	// Players on this machine, in local player order, so their rows can be marked P1-P4.
	TArray<const APlayerState*, TInlineAllocator<4>> LocalStates;
	if (const UGameInstance* OwningGameInstance = GetGameInstance())
	{
		for (const ULocalPlayer* LocalPlayer : OwningGameInstance->GetLocalPlayers())
		{
			const APlayerController* LocalController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
			LocalStates.Add(LocalController ? LocalController->PlayerState.Get() : nullptr);
		}
	}
	// Where the CPUs will go: the smallest team first, as the game puts them once the teams are confirmed.
	TArray<int32> Counts, Cpus;
	Counts.Init(0, Teams);
	Cpus.Init(0, Teams);
	for (const AChaosImpactPlayerState* Member : Humans)
	{
		++Counts[FMath::Clamp(Member->TeamIndex, 0, Teams - 1)];
	}
	for (int32 Cpu = 0; Cpu < Match->Rules.CPUCount; ++Cpu)
	{
		int32 Smallest = 0;
		for (int32 Team = 1; Team < Teams; ++Team)
		{
			Smallest = Counts[Team] + Cpus[Team] < Counts[Smallest] + Cpus[Smallest] ? Team : Smallest;
		}
		++Cpus[Smallest];
	}
	// Who changed team since the last frame (for the flash on the card they joined), and how long the frame was.
	const double PaintNow = FPlatformTime::Seconds();
	const float FrameSeconds = FMath::Clamp(static_cast<float>(PaintNow - TeamPaintedAt), 0.0f, 0.1f);
	TeamPaintedAt = PaintNow;
	TArray<double> JoinedAt;
	JoinedAt.Init(-100.0, Teams);
	for (const AChaosImpactPlayerState* Member : Humans)
	{
		const int32 Now = FMath::Clamp(Member->TeamIndex, 0, Teams - 1);
		TPair<int32, double>* Last = TeamLastChange.Find(Member);
		if (!Last)
		{
			TeamLastChange.Add(Member, TPair<int32, double>(Now, -100.0));
		}
		else if (Last->Key != Now)
		{
			*Last = TPair<int32, double>(Now, PaintNow);
		}
		const TPair<int32, double>& Change = TeamLastChange.FindChecked(Member);
		JoinedAt[Now] = FMath::Max(JoinedAt[Now], Change.Value);
	}
	const AChaosImpactPlayerController* MouseController = GetTeamMouseController();
	const APlayerState* MouseState = MouseController ? MouseController->PlayerState.Get() : nullptr;
	const AChaosImpactPlayerController* Owner = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const bool bMouse = MouseState && (!Owner || !Owner->IsPrimaryUsingGamepad() || LocalStates.Num() > 1);

	// What to do, in one line under the heading.
	{
		const float In = CP::EaseOut((T - 0.05f) / 0.3f);
		const CP::FPainter Strip{Design, Elements, Layer, In};
		const CP::FPainter StripText{Design, Elements, Layer + 1, In};
		const FString Line = FString::Printf(TEXT("◀ ▶ でチームをえらぶ（それぞれのコントローラーで）%s　・　CPU %d人は人数の少ないチームに入ります"),
			bMouse ? TEXT("・クリックでも入れます") : TEXT(""), Match->Rules.CPUCount);
		const float Size = CP::HudFit(Line, 21.0f, 1300.0f, TEXT("Bold"));
		const float Width = CP::HudMeasure(Line, Size, TEXT("Bold")) + 60.0f;
		CP::RoundedBox(Strip, 800.0f - Width * 0.5f, 190.0f, Width, 38.0f, 19.0f, CP::WithAlpha(CP::Ink, 0.8f));
		StripText.Text(Line, 800.0f, 190.0f + (38.0f - Size * 1.32f) * 0.5f, Size, CP::WithAlpha(CP::Paper, 0.85f), ETextAlign::Center, TEXT("Bold"));
	}

	for (int32 Team = 0; Team < Teams; ++Team)
	{
		const float In = CP::EaseOut((T - 0.1f - 0.07f * Team) / 0.38f);
		const FSlateRect Rect = TeamCardRect(Team, Teams, Capacity, Match->Rules.bCrashBall);
		const float W = Rect.GetSize().X, H = Rect.GetSize().Y;
		const FGeometry Card = CP::MakeSkewed(Design, Rect.Left, Rect.Top + (1.0f - In) * 90.0f, W, H, -0.05f);
		const CP::FPainter Shadow{Card, Elements, Layer, In};
		const CP::FPainter Body{Card, Elements, Layer + 1, In};
		const CP::FPainter Mid{Card, Elements, Layer + 2, In};
		const CP::FPainter Front{Card, Elements, Layer + 3, In};
		const CP::FPainter Top{Card, Elements, Layer + 4, In};
		const FLinearColor TeamColor = ChaosImpactMatch::GetTeamColor(Team);
		FLinearColor Deep = TeamColor * 0.22f;
		Deep.A = 1.0f;
		const bool bHover = bMouse && Rect.ContainsPoint(TeamMousePoint);
		const bool bMouseHere = MouseState && Humans.ContainsByPredicate([MouseState, Team](const AChaosImpactPlayerState* Member)
			{ return Member == MouseState && Member->TeamIndex == Team; });
		const float Glow = 0.5f + 0.5f * FMath::Sin(T * 2.4f + Team * 1.3f);

		// The card: a soft shadow, dark body washed with the team's colour from the top, a frame that lights on hover.
		CP::RoundedBox(Shadow, 12.0f, 16.0f, W, H, 24.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		CP::RoundedBox(Shadow, -4.0f, -4.0f, W + 8.0f, H + 8.0f, 27.0f, CP::WithAlpha(TeamColor, bHover ? 0.85f : 0.22f + 0.12f * Glow));
		CP::RoundedBox(Body, 0.0f, 0.0f, W, H, 24.0f, FLinearColor(0.016f, 0.022f, 0.045f, 0.98f));
		CP::HudTriangles(Mid, {FVector2D(0.0f, 100.0f), FVector2D(W, 100.0f), FVector2D(W, H * 0.72f), FVector2D(0.0f, H * 0.72f)},
			{CP::WithAlpha(TeamColor, 0.26f), CP::WithAlpha(TeamColor, 0.26f), CP::WithAlpha(TeamColor, 0.0f), CP::WithAlpha(TeamColor, 0.0f)},
			{0, 1, 2, 0, 2, 3});

		// The header: the team's colour, deep on the left to bright on the right, slanted stripes, its name big.
		CP::RoundedBox(Mid, 0.0f, 0.0f, W, 104.0f, 24.0f, FMath::Lerp(Deep, TeamColor, 0.45f));
		CP::HudTriangles(Front, {FVector2D(0.0f, 22.0f), FVector2D(W, 22.0f), FVector2D(W, 104.0f), FVector2D(0.0f, 104.0f)},
			{CP::WithAlpha(Deep, 0.0f), CP::WithAlpha(TeamColor, 0.55f), CP::WithAlpha(TeamColor, 0.3f), CP::WithAlpha(Deep, 0.4f)},
			{0, 1, 2, 0, 2, 3});
		CP::RoundedBox(Front, 8.0f, 7.0f, W - 16.0f, 34.0f, 16.0f, CP::WithAlpha(CP::Paper, 0.12f));
		// Someone just joined: the header flashes white and the frame burns brighter for a moment.
		const float Joined = FMath::Exp(-6.0f * static_cast<float>(PaintNow - JoinedAt[Team]));
		if (Joined > 0.01f)
		{
			CP::RoundedBox(Top, 0.0f, 0.0f, W, 104.0f, 24.0f, CP::WithAlpha(CP::Paper, 0.55f * Joined));
			CP::RoundedBox(Shadow, -8.0f * Joined - 4.0f, -8.0f * Joined - 4.0f, W + 16.0f * Joined + 8.0f, H + 16.0f * Joined + 8.0f, 28.0f,
				CP::WithAlpha(TeamColor, 0.8f * Joined));
		}
		{
			const FGeometry Stripes = CP::MakeSkewed(Card, W - 230.0f, 10.0f, 210.0f, 84.0f, -0.45f);
			const CP::FPainter S{Stripes, Elements, Layer + 3, In};
			for (int32 Stripe = 0; Stripe < 6; ++Stripe)
			{
				S.Box(30.0f + Stripe * 28.0f, 0.0f, 11.0f, 84.0f, CP::WithAlpha(CP::Paper, 0.06f + 0.05f * Glow));
			}
		}
		Front.Box(0.0f, 100.0f, W, 5.0f, TeamColor);
		const FString TeamName = ChaosImpactMatch::GetTeamName(Team);
		Top.Text(FString::Printf(TEXT("TEAM %d"), Team + 1), 26.0f, 10.0f, 15.0f, CP::WithAlpha(CP::Paper, 0.8f), ETextAlign::Left, TEXT("BlackItalic"));
		Top.Text(TeamName, 24.0f, 28.0f, CP::HudFit(TeamName, 46.0f, W - 170.0f, TEXT("BlackItalic")), CP::Paper, ETextAlign::Left, TEXT("BlackItalic"), 3.0f, CP::Ink);
		// How many people are in it.
		{
			const FString Count = FString::Printf(TEXT("%d/%d"), Counts[Team], Capacity);
			CP::RoundedBox(Front, W - 118.0f, 30.0f, 98.0f, 46.0f, 14.0f, CP::WithAlpha(CP::Ink, 0.6f));
			const float CountPop = 1.0f + 0.45f * FMath::Exp(-9.0f * static_cast<float>(PaintNow - JoinedAt[Team]));
			Top.Text(Count, W - 69.0f, 34.0f - 14.0f * (CountPop - 1.0f), 28.0f * CountPop, Counts[Team] >= Capacity ? CP::Fire : CP::Gold,
				ETextAlign::Center, TEXT("BlackItalic"));
		}

		// Its places: the people in it, then the CPUs that will fill it, then any room left.
		const bool bCrash = Match->Rules.bCrashBall;
		const float RowTop = 122.0f;
		const float Room = H - RowTop - (bCrash ? 74.0f : (bHover && !bMouseHere ? 46.0f : 16.0f));
		const float RowGap = 8.0f;
		const float RowH = FMath::Min(60.0f, (Room - RowGap * (Capacity - 1)) / FMath::Max(Capacity, 1));
		TArray<const AChaosImpactPlayerState*> Members;
		for (const AChaosImpactPlayerState* Member : Humans)
		{
			if (FMath::Clamp(Member->TeamIndex, 0, Teams - 1) == Team)
			{
				Members.Add(Member);
			}
		}
		for (int32 Row = 0; Row < Capacity; ++Row)
		{
			const float Y = RowTop + Row * (RowH + RowGap);
			const float RowIn = CP::EaseOut((T - 0.25f - 0.07f * Team - 0.05f * Row) / 0.3f);
			const CP::FPainter RBack{Card, Elements, Layer + 2, In * RowIn};
			const CP::FPainter RMid{Card, Elements, Layer + 3, In * RowIn};
			const CP::FPainter RTop{Card, Elements, Layer + 4, In * RowIn};
			const float X = 16.0f + (1.0f - RowIn) * 40.0f, RW = W - 32.0f;
			if (Members.IsValidIndex(Row))
			{
				const AChaosImpactPlayerState* Member = Members[Row];
				const FVector2D Target(Rect.Left + X, Rect.Top + Y);
				FVector2D& Shown = TeamRowShown.FindOrAdd(Member, Target);
				Shown = FMath::Lerp(Shown, Target, 1.0f - FMath::Exp(-13.0f * FrameSeconds));
				const FVector2D Off = Shown - Target;
				const bool bMoving = Off.SizeSquared() > 4.0f;
				const float Since = static_cast<float>(PaintNow - TeamLastChange.FindChecked(Member).Value);
				const FGeometry RowCard = CP::MakeSkewed(Design, Rect.Left + static_cast<float>(Off.X), Rect.Top + (1.0f - In) * 90.0f + static_cast<float>(Off.Y),
					W, H, -0.05f);
				const int32 RowLayer = bMoving ? Layer + 8 : Layer + 2;
				const CP::FPainter HBack{RowCard, Elements, RowLayer, In * RowIn};
				const CP::FPainter HMid{RowCard, Elements, RowLayer + 1, In * RowIn};
				const CP::FPainter HTop{RowCard, Elements, RowLayer + 2, In * RowIn};
				const int32 LocalIndex = LocalStates.IndexOfByKey(Member);
				const bool bLocal = LocalIndex >= 0;
				const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
				const FString Name = Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
				HBack.Box(X, Y, RW, RowH, bLocal ? CP::WithAlpha(FMath::Lerp(Deep, TeamColor, 0.55f), 0.95f) : FLinearColor(0.045f, 0.06f, 0.1f, 0.98f));
				HMid.Box(X, Y, 7.0f, RowH, TeamColor);
				CP::HudTriangles(HMid, {FVector2D(X + 7.0f, Y), FVector2D(X + RW * 0.6f, Y), FVector2D(X + RW * 0.6f, Y + RowH * 0.45f), FVector2D(X + 7.0f, Y + RowH * 0.45f)},
					{CP::WithAlpha(CP::Paper, 0.12f), CP::WithAlpha(CP::Paper, 0.0f), CP::WithAlpha(CP::Paper, 0.0f), CP::WithAlpha(CP::Paper, 0.04f)}, {0, 1, 2, 0, 2, 3});
				if (Since < 0.4f)
				{
					HTop.Box(X, Y, RW, RowH, CP::WithAlpha(CP::Paper, 0.45f * (1.0f - Since / 0.4f)));
				}
				float NameX = X + 22.0f;
				if (bLocal)
				{
					const FLinearColor Accent = CP::PlayerAccents[LocalIndex % 4];
					CP::RoundedBox(HMid, X + 18.0f, Y + (RowH - 30.0f) * 0.5f, 50.0f, 30.0f, 9.0f, Accent);
					HTop.Text(FString::Printf(TEXT("P%d"), LocalIndex + 1), X + 43.0f, Y + (RowH - 30.0f) * 0.5f + 1.0f, 20.0f, CP::Ink,
						ETextAlign::Center, TEXT("BlackItalic"));
					HTop.Outline(X, Y, RW, RowH, CP::WithAlpha(CP::Paper, 0.45f + 0.4f * FMath::Sin(T * 6.0f + LocalIndex)), 2.0f);
					// The arrows that move it, nudging the way it can go.
					const float Nudge = 3.0f * FMath::Sin(T * 5.0f);
					HTop.Text(TEXT("◀"), X - 14.0f - Nudge, Y + (RowH - 18.0f * 1.32f) * 0.5f, 18.0f, CP::Paper, ETextAlign::Center, TEXT("Black"));
					HTop.Text(TEXT("▶"), X + RW + 14.0f + Nudge, Y + (RowH - 18.0f * 1.32f) * 0.5f, 18.0f, CP::Paper, ETextAlign::Center, TEXT("Black"));
					NameX = X + 80.0f;
				}
				const FString Fighter = ChaosImpactRoster::Get(Member->CharacterIndex).Name;
				const float FighterSize = CP::HudFit(Fighter, 15.0f, RW * 0.38f, TEXT("Bold"));
				const float FighterW = CP::HudMeasure(Fighter, FighterSize, TEXT("Bold"));
				const float NameSize = CP::HudFit(Name, FMath::Min(26.0f, RowH * 0.5f), X + RW - FighterW - 22.0f - NameX, TEXT("Black"));
				HTop.Text(Name, NameX, Y + (RowH - NameSize * 1.32f) * 0.5f, NameSize, CP::Paper, ETextAlign::Left, TEXT("Black"), 1.0f, CP::Ink);
				HTop.Text(Fighter, X + RW - 14.0f, Y + (RowH - FighterSize * 1.32f) * 0.5f, FighterSize, CP::WithAlpha(CP::Paper, 0.6f), ETextAlign::Right, TEXT("Bold"));
			}
			else if (Row < Members.Num() + Cpus[Team])
			{
				RBack.Box(X, Y, RW, RowH, FLinearColor(0.03f, 0.04f, 0.07f, 0.95f));
				RMid.Box(X, Y, 7.0f, RowH, CP::WithAlpha(TeamColor, 0.45f));
				RTop.Text(TEXT("CPU"), X + 22.0f, Y + (RowH - 22.0f * 1.32f) * 0.5f, FMath::Min(22.0f, RowH * 0.45f), CP::WithAlpha(CP::Paper, 0.7f),
					ETextAlign::Left, TEXT("BlackItalic"));
				const FString Auto = TEXT("自動で入ります");
				RTop.Text(Auto, X + RW - 14.0f, Y + (RowH - 14.0f * 1.32f) * 0.5f, CP::HudFit(Auto, 14.0f, RW * 0.5f, TEXT("Bold")), CP::WithAlpha(CP::Muted, 0.9f),
					ETextAlign::Right, TEXT("Bold"));
			}
			else
			{
				RBack.Box(X, Y, RW, RowH, FLinearColor(0.02f, 0.026f, 0.05f, 0.6f));
				RMid.Outline(X, Y, RW, RowH, CP::WithAlpha(CP::Paper, 0.12f), 1.5f);
				RTop.Text(TEXT("あき"), X + RW * 0.5f, Y + (RowH - 17.0f * 1.32f) * 0.5f, 17.0f, CP::WithAlpha(CP::Muted, 0.6f), ETextAlign::Center, TEXT("Bold"));
			}
		}

		if (bCrash)
		{
			// クラッシュボール: the goal this team pushes the giant ball into, in the other team's colour.
			const FLinearColor Target = ChaosImpactMatch::GetTeamColor(1 - Team);
			Mid.Box(16.0f, H - 62.0f, W - 32.0f, 48.0f, CP::WithAlpha(Target, 0.2f));
			Front.Box(16.0f, H - 62.0f, 8.0f, 48.0f, Target);
			const FString Goal = FString::Printf(TEXT("%sのゴールへ巨大球を押し込め！"), ChaosImpactMatch::GetTeamName(1 - Team));
			Top.Text(Goal, W * 0.5f, H - 62.0f + (48.0f - 21.0f * 1.32f) * 0.5f, CP::HudFit(Goal, 21.0f, W - 60.0f, TEXT("Bold")), CP::Paper, ETextAlign::Center, TEXT("Bold"));
		}
		else if (bHover && !bMouseHere)
		{
			const FString Join = Counts[Team] >= Capacity ? FString(TEXT("満員です")) : FString(TEXT("クリックでこのチームへ"));
			Top.Text(Join, W * 0.5f, H - 38.0f, 18.0f, Counts[Team] >= Capacity ? CP::Fire : CP::Paper, ETextAlign::Center, TEXT("Black"));
		}
	}

	// VS between the cards, on their headers.
	for (int32 Team = 0; Team + 1 < Teams; ++Team)
	{
		const float In = CP::EaseOut((T - 0.3f - 0.07f * Team) / 0.3f);
		const FSlateRect A = TeamCardRect(Team, Teams, Capacity, Match->Rules.bCrashBall);
		const FSlateRect B = TeamCardRect(Team + 1, Teams, Capacity, Match->Rules.bCrashBall);
		const FVector2D Centre((A.Right + B.Left) * 0.5f, A.Top + 52.0f);
		const float Pop = 1.0f + 0.25f * FMath::Exp(-8.0f * FMath::Max(T - 0.3f, 0.0f));
		const CP::FPainter V{Design, Elements, Layer + 5, In};
		const CP::FPainter VText{Design, Elements, Layer + 6, In};
		V.Disc(Centre + FVector2D(3.0f, 4.0f), 34.0f * Pop, CP::WithAlpha(FLinearColor::Black, 0.5f));
		V.Disc(Centre, 34.0f * Pop, CP::Ink);
		V.Ring(Centre, 34.0f * Pop, CP::Gold, 3.0f);
		VText.Text(TEXT("VS"), Centre.X, Centre.Y - 18.0f * Pop, 27.0f * Pop, CP::Gold, ETextAlign::Center, TEXT("BlackItalic"));
	}
	return Layer + 11;
}

FReply UChaosImpactMenuWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (SecretChoice != INDEX_NONE)
	{
		const FVector2D At = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		for (int32 Index = 0; Index < 2; ++Index) { if (SecretChoiceRect(Index).IsInside(At)) { SecretChoice = Index; } }
		return FReply::Handled();
	}
	if (IsSettingsActive())
	{
		SettingsScreen->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect && CharacterSelect->IsOpen())
	{
		CharacterSelect->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	}
	if (Screen == EChaosImpactScreen::TeamSelect)
	{
		TeamMousePoint = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
	}
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit && ColourEdit->IsOpen())
	{
		ColourEdit->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()),
			InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton));
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu && DojoMenu->IsOpen() && !InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		DojoMenu->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	}
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite)
	{
		const FVector2D At = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (DojoInviteButton(Index).IsInside(At) && DojoInviteChoice != Index)
			{
				DojoInviteChoice = Index;
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiSelect);
			}
		}
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry && TourneyEntry->IsOpen()
		&& !InMouseEvent.GetCursorDelta().IsNearlyZero())
	{
		TourneyEntry->HandleMouseMove(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
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
	if (SecretChoice != INDEX_NONE)
	{
		const FVector2D At = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (SecretChoiceRect(Index).IsInside(At)) { SecretChoice = Index; ConfirmSecretChoice(); return FReply::Handled(); }
		}
		SecretChoice = INDEX_NONE;
		return FReply::Handled();
	}
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
	if (Screen == EChaosImpactScreen::ColourEdit && ColourEdit)
	{
		ColourEdit->HandleClick(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::DojoSelect && DojoMenu)
	{
		DojoMenu->HandleClick(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite)
	{
		const FVector2D At = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (DojoInviteButton(Index).IsInside(At))
			{
				CloseDojoInvite(Index == 0);
			}
		}
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::TourneyEntry && TourneyEntry)
	{
		TourneyEntry->HandleClick(ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::TourneySeat)
	{
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::CharacterSelect && CharacterSelect)
	{
		if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			CharacterSelect->HandleRightClick();
			return FReply::Handled();
		}
		const float Scale = DesignScale(InGeometry);
		const FVector2D Offset = (InGeometry.GetLocalSize() - FVector2D(1600, 900) * Scale) * 0.5f;
		CharacterSelect->HandleClick((InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()) - Offset) / Scale);
		return FReply::Handled();
	}
	if (Screen == EChaosImpactScreen::TeamSelect && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// A click on a team's card puts the mouse player in it.
		const FVector2D At = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
		const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		AChaosImpactPlayerController* Mouse = GetTeamMouseController();
		for (int32 Team = 0; Match && Mouse && Match->IsTeamBattle() && Team < Match->Rules.TeamCount; ++Team)
		{
			if (TeamCardRectNow(Team).ContainsPoint(At))
			{
				Mouse->SetOwnTeam(Team);
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiValue);
				return FReply::Handled();
			}
		}
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
	// (The invitation took the press: the tiles under it do not.)
	if (Screen == EChaosImpactScreen::ModeSelect && bDojoInvite)
	{
		return FReply::Handled();
	}
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
		// A rules row: a click on its left half (the ◀ side of its value) goes back a step, on its right half on.
		AChaosImpactPlayerController* RulesController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		if (bActivate && Screen == EChaosImpactScreen::MatchRules && !bModePicker && !bBallSwitch && Released <= 3 && Released != 1 && Entries.IsValidIndex(Released)
			&& RulesController)
		{
			SelectedIndex = Released;
			if (!Entries[Released].bDisabled)
			{
				const FSlateRect& Row = Entries[Released].Rect;
				const float ValueMid = Row.Left + (Row.Right - Row.Left) * 0.3f + (Row.Right - Row.Left) * 0.7f * 0.5f;
				const FVector2D Point = ToDesignPoint(InGeometry, InMouseEvent.GetScreenSpacePosition());
				RulesController->AdjustMatchRule(Released, Point.X < ValueMid ? -1 : 1);
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiValue);
				BuildEntries();
			}
			return FReply::Handled().ReleaseMouseCapture();
		}
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
