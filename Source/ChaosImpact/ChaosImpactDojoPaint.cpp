#include "ChaosImpactDojo.h"

#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactDojoVideo.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSuddenDeathZone.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactTrainingTarget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Engine/World.h"

// The dojo's HUD: its slides (each with a little moving picture), the task card with its count, the flashes as it
// counts down, CLEAR!, and the arrow pointing at the part of the HUD a lesson is about.

using namespace ChaosImpactPaint;

namespace
{
	const FLinearColor DojoPlayer(0.12f, 0.62f, 1.0f, 1.0f);
	const FLinearColor DojoCPU(1.0f, 0.22f, 0.2f, 1.0f);
	const FLinearColor DojoFloorTop(0.09f, 0.22f, 0.34f, 1.0f);
	const FLinearColor DojoFloorBottom(0.04f, 0.1f, 0.18f, 1.0f);

	/** A key cap: a dark slanted chip with an ice rim and the key's name. Returns its width. */
	float DojoKeyCap(const FPainter& P, const float X, const float Y, const FString& Key, const float Size)
	{
		const float W = HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f;
		const float H = Size * 1.55f;
		const float Lean = H * 0.22f;
		HudShard(P, {FVector2D(X + Lean, Y), FVector2D(X + W + Lean, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
			MakeFill(FLinearColor(0.12f, 0.17f, 0.32f, 0.97f), FLinearColor(0.03f, 0.05f, 0.12f, 0.97f)), 2.0f, FVector2D(2.0, 3.0),
			FMath::Lerp(Navy, Ice, 0.55f));
		HudAbove(P, 3).Text(Key, X + W * 0.5f + Lean * 0.5f, Y + (H - Size * 1.38f) * 0.5f, Size, Paper, ETextAlign::Center, TEXT("Black"));
		return W + Lean;
	}

	float DojoKeyCapWidth(const FString& Key, const float Size)
	{
		const float H = Size * 1.55f;
		return HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f + H * 0.22f;
	}

	/** The first key set for an action ("" when none is). */
	FString DojoFirstKey(const AChaosImpactPlayerController* Controller, const EChaosImpactAction Action)
	{
		const FString All = ChaosImpactSettings::DescribeAction(Controller, Action);
		if (All == TEXT("－"))
		{
			return FString();
		}
		FString First;
		return All.Split(TEXT(" / "), &First, nullptr) ? First : All;
	}

	/** The keys of a lesson's task, as this player has them (the first key of each; a stick once for the four moves),
	 *  in the control type set (the simple and mouse types tackle with both buttons, put balls down with a hold...). */
	TArray<FString> DojoKeys(const AChaosImpactPlayerController* Controller, const FChaosImpactDojoLesson& Lesson)
	{
		using namespace ChaosImpactSettings;
		TArray<FString> Out;
		const bool bPad = Controller && Controller->IsUsingGamepad();
		const EChaosImpactControlScheme Scheme = Controller ? GetPlayerControlScheme(Controller) : EChaosImpactControlScheme::Normal;
		const bool bSimple = !bPad && Scheme == EChaosImpactControlScheme::Simple;
		const bool bMouseOnly = !bPad && IsMouseOnlyScheme(Scheme);
		const bool bEasy = bSimple || bMouseOnly;
		for (const EChaosImpactAction Action : Lesson.TaskKeys)
		{
			const bool bMove = Action == EChaosImpactAction::MoveUp || Action == EChaosImpactAction::MoveDown
				|| Action == EChaosImpactAction::MoveLeft || Action == EChaosImpactAction::MoveRight;
			FString Key;
			if (bMove && bPad)
			{
				const bool bSwapped = GetProfile(GetPlayerProfile(Controller)).bSwapSticks;
				Key = bSwapped ? TEXT("右スティック") : TEXT("左スティック");
			}
			else if (bMove && Scheme == EChaosImpactControlScheme::MouseOnly && !bPad)
			{
				Key = TEXT("右クリック");
			}
			else if (bMove && Scheme == EChaosImpactControlScheme::MouseHold && !bPad)
			{
				Key = TEXT("右クリック長押し");
			}
			else if (bEasy && Action == EChaosImpactAction::Tackle)
			{
				Key = TEXT("左＋右クリック");
			}
			else if (bEasy && Lesson.Goal == EChaosImpactDojoGoal::BallForm)
			{
				const FString Form = DojoFirstKey(Controller, bSimple ? EChaosImpactAction::SwapBall : EChaosImpactAction::Dash);
				Key = Form.IsEmpty() ? FString() : Form + TEXT("長押し");
			}
			else if (bSimple && Action == EChaosImpactAction::DropBall)
			{
				const FString Swap = DojoFirstKey(Controller, EChaosImpactAction::SwapBall);
				Key = Swap.IsEmpty() ? FString() : Swap + TEXT("長押し");
			}
			else if (bMouseOnly && Action == EChaosImpactAction::DropBall)
			{
				Key = TEXT("ボール枠を右長押し");
			}
			else if (bMouseOnly && Action == EChaosImpactAction::SwapBall)
			{
				Key = TEXT("ホイール");
			}
			else
			{
				FString All = ChaosImpactSettings::DescribeAction(Controller, Action);
				if (All == TEXT("－"))
				{
					continue;
				}
				FString First;
				Key = All.Split(TEXT(" / "), &First, nullptr) ? First : All;
			}
			Out.AddUnique(Key);
		}
		return Out;
	}

	FString DojoConfirmKey(const AChaosImpactPlayerController* Controller)
	{
		return Controller && Controller->IsUsingGamepad() ? FString(TEXT("A")) : FString(TEXT("Enter"));
	}

	FString DojoBackKey(const AChaosImpactPlayerController* Controller)
	{
		return Controller && Controller->IsUsingGamepad() ? FString(TEXT("B")) : FString(TEXT("BackSpace"));
	}

	// ----------------------------------------------------------------------------------------------- The pictures

	/** A little round character seen from above: its body, a darker rim, and eyes looking its way. */
	void ArtChar(const FPainter& P, const FVector2D& At, const float R, const FLinearColor& Color, const FVector2D& Facing, const float Alpha = 1.0f)
	{
		const FPainter A{P.Geometry, P.Elements, P.Layer, P.Alpha * Alpha};
		A.Disc(At + FVector2D(R * 0.12f, R * 0.22f), R * 1.02f, FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
		A.Disc(At, R * 1.12f, Navy);
		A.Disc(At, R, Color);
		A.Disc(At - FVector2D(R * 0.25f, R * 0.3f), R * 0.45f, WithAlpha(FLinearColor::White, 0.25f));
		const FVector2D Face = Facing.IsNearlyZero() ? FVector2D(0.0, -1.0) : Facing.GetSafeNormal();
		const FVector2D Side(-Face.Y, Face.X);
		for (const float S : {-1.0f, 1.0f})
		{
			HudAbove(A, 1).Disc(At + Face * R * 0.42f + Side * S * R * 0.3f, R * 0.17f, Ink);
		}
	}

	void ArtBall(const FPainter& P, const FVector2D& At, const float R, const EChaosImpactBallType Type, const float Time, const float Alpha = 1.0f)
	{
		const FPainter A{P.Geometry, P.Elements, P.Layer + 2, P.Alpha * Alpha};
		UChaosImpactChargeWidget::PaintBallIcon(A, At, R, true, Type, 0.0f, Time);
	}

	void ArtArrow(const FPainter& P, const FVector2D& From, const FVector2D& To, const FLinearColor& Color, const float Width)
	{
		const FVector2D Way = (To - From).GetSafeNormal();
		const FVector2D Side(-Way.Y, Way.X);
		P.Line(From, To, Color, Width);
		const float Head = Width * 3.2f;
		P.Line(To, To - Way * Head + Side * Head * 0.7f, Color, Width);
		P.Line(To, To - Way * Head - Side * Head * 0.7f, Color, Width);
	}

	/** A label in the picture (white with an outline). */
	void ArtLabel(const FPainter& P, const FString& Text, const FVector2D& At, const float Size, const FLinearColor& Color = Paper)
	{
		HudAbove(P, 4).Text(Text, At.X, At.Y - Size * 0.7f, Size, Color, ETextAlign::Center, TEXT("Black"), FMath::Max(2.0f, Size * 0.12f), Navy);
	}

	/** A hit, smooth: a soft glow and a clean ring round it (never a jagged burst). Same arguments as HudBurst. */
	void SoftBurst(const FPainter& P, const FVector2D& C, const float Inner, const float Outer, const int32 /*Spikes*/, const FHudFill& Fill,
		const float /*Spin*/ = 0.0f, const FVector2D& /*Stretch*/ = FVector2D(1.0, 1.0), const float /*Outline*/ = 0.0f,
		const FVector2D& /*Drop*/ = FVector2D::ZeroVector, const float /*Seed*/ = 0.0f)
	{
		const FLinearColor Color = Fill.At(0.5f);
		P.Disc(C, Outer, WithAlpha(Color, 0.22f));
		P.Disc(C, FMath::Lerp(Inner, Outer, 0.5f), WithAlpha(Color, 0.3f));
		HudAbove(P, 1).Ring(C, Outer, WithAlpha(Color, 0.9f), FMath::Max(2.0f, Outer * 0.08f));
	}

	float Loop(const float Time, const float Period)
	{
		return FMath::Frac(Time / Period);
	}

	float Smooth(const float T)
	{
		const float C = FMath::Clamp(T, 0.0f, 1.0f);
		return C * C * (3.0f - 2.0f * C);
	}

	FVector2D LerpV(const FVector2D& A, const FVector2D& B, const float T)
	{
		return A + (B - A) * T;
	}
}

void AChaosImpactDojo::PaintArt(const FPainter& Paint, const EChaosImpactDojoArt Art, const EChaosImpactBallType Ball, const float X, const float Y,
	const float W, const float H, const float Time)
{
	// Behind the demo video (seen only before its clip is playing): a plain panel saying what goes there.
	RoundedBox(Paint, X, Y, W, H, H * 0.06f, Navy);
	RoundedBox(HudAbove(Paint, 1), X + 3.0f, Y + 3.0f, W - 6.0f, H - 6.0f, H * 0.05f, DojoFloorBottom);
	const FString Label = TEXT("デモ動画");
	const float Size = HudFit(Label, H * 0.11f, W * 0.8f);
	HudAbove(Paint, 2).Text(Label, X + W * 0.5f, Y + H * 0.5f - Size * 0.69f, Size, WithAlpha(Paper, 0.55f), ETextAlign::Center, TEXT("Black"));
}

// ---------------------------------------------------------------------------------------------------------------------
// The HUD over play
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
	/** The lesson's tag, top left: its group's colour and sign, its number and name. */
	void PaintLessonTag(const FPainter& P, const FChaosImpactDojoCategory& Group, const int32 GroupIndex, const int32 Index, const FString& Name,
		const float S, const float Time)
	{
		const FString Number = FString::Printf(TEXT("LESSON %d"), Index + 1);
		const float NameSize = HudFit(Name, 26.0f * S, 380.0f * S);
		const float H = 58.0f * S;
		const float Top = 16.0f * S;
		const float BlockW = 150.0f * S;
		const float W = BlockW + HudMeasure(Name, NameSize) + 70.0f * S;
		HudShard(P, {FVector2D(0.0, Top), FVector2D(W + 18.0f * S, Top), FVector2D(W, Top + H), FVector2D(0.0, Top + H)},
			MakeFill(FLinearColor(0.08f, 0.12f, 0.25f, 0.95f), FLinearColor(0.02f, 0.04f, 0.1f, 0.95f)), 2.5f, FVector2D(4.0, 5.0), Navy);
		HudShard(HudAbove(P, 3), {FVector2D(0.0, Top), FVector2D(BlockW + 14.0f * S, Top), FVector2D(BlockW, Top + H), FVector2D(0.0, Top + H)},
			MakeFill(Group.Color * 1.1f, Group.Color * 0.6f), 0.0f, FVector2D::ZeroVector, Group.Color);
		HudAbove(P, 6).Text(Group.Name, BlockW * 0.5f + 4.0f * S, Top + 6.0f * S, HudFit(Group.Name, 17.0f * S, BlockW - 20.0f * S), Paper,
			ETextAlign::Center, TEXT("Black"));
		HudAbove(P, 6).Text(Number, BlockW * 0.5f + 2.0f * S, Top + 32.0f * S, 12.0f * S, WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("BlackItalic"));
		HudAbove(P, 6).Text(Name, BlockW + 26.0f * S, Top + (H - NameSize * 1.38f) * 0.5f, NameSize, Paper, ETextAlign::Left, TEXT("Black"));
	}

	/** Stars of light bursting out from C (T: 0 to 1 over their life). */
	void PaintSparks(const FPainter& P, const FVector2D& C, const float T, const float Reach, const FLinearColor& Color, const int32 Count, const float Seed)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float A = (Index + Seed) * UE_TWO_PI / Count + Seed;
			const FVector2D Way(FMath::Cos(A), FMath::Sin(A));
			const float D = Reach * EaseOut(T) * (0.7f + 0.3f * FMath::Frac(Index * 0.618f + Seed));
			P.Disc(C + Way * D, FMath::Max(0.5f, (1.0f - T) * Reach * 0.05f), WithAlpha(Color, 1.0f - T));
		}
	}
}

void AChaosImpactDojo::PaintHud(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer,
	const AChaosImpactPlayerController* Viewer, const float Scale) const
{
	const FChaosImpactDojoLesson* L = GetLesson();
	if (!L || Phase == EChaosImpactDojoPhase::Idle || !GetWorld() || bRecording)
	{
		return;
	}
	const FVector2D Size(Geometry.GetLocalSize());
	LastHudSize = Size;
	const float W = static_cast<float>(Size.X);
	const float H = static_cast<float>(Size.Y);
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
	const FPainter P{Geometry, Elements, Layer, 1.0f};
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	if (!All.IsValidIndex(Category))
	{
		return;
	}
	if (Phase == EChaosImpactDojoPhase::Intro)
	{
		PaintSlides(HudAbove(P, 2), W, H, Time);
		if (L->Slides.IsValidIndex(Slide) && L->Slides[Slide].Pointer != EChaosImpactDojoPointer::None)
		{
			PaintPointer(HudAbove(P, 70), L->Slides[Slide].Pointer, W, H, Scale, Time);
		}
		return;
	}
	// (In a mock match its own HUD keeps that corner: its scoreboard.)
	if (!IsMatchWorld())
	{
		PaintLessonTag(HudAbove(P, 2), All[Category], Category, LessonIndex, L->Name, S, Time);
	}
	if (Phase != EChaosImpactDojoPhase::Clear)
	{
		PaintObjectives(HudAbove(P, 6), Viewer, W, H, Time);
	}
	PaintTask(HudAbove(P, 10), Viewer, W, H, Time);
	if (Phase == EChaosImpactDojoPhase::Play)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (L->Pointer != EChaosImpactDojoPointer::None && Now - PhaseAt < 6.0)
		{
			PaintPointer(HudAbove(P, 70), L->Pointer, W, H, Scale, Time);
		}
		// START!: the first moment of play.
		const float SinceStart = static_cast<float>(Now - PhaseAt);
		if (SinceStart < 1.0f)
		{
			const float Pop = 1.0f + 0.5f * FMath::Max(0.0f, 1.0f - SinceStart / 0.15f);
			const FPainter Go{Geometry, Elements, Layer + 30, FMath::Clamp((1.0f - SinceStart) / 0.3f, 0.0f, 1.0f)};
			const float BandH = 130.0f * S * FMath::Min(SinceStart / 0.12f, 1.0f);
			Go.Box(0.0f, H * 0.42f - BandH * 0.5f, W, BandH, WithAlpha(All[Category].Color, 0.55f));
			Go.Box(0.0f, H * 0.42f - BandH * 0.5f, W, 4.0f * S, WithAlpha(Paper, 0.8f));
			Go.Box(0.0f, H * 0.42f + BandH * 0.5f - 4.0f * S, W, 4.0f * S, WithAlpha(Paper, 0.8f));
			HudText3D(HudAbove(Go, 4), TEXT("START!"), W * 0.5f, H * 0.42f - 52.0f * S * Pop, 80.0f * S * Pop, Paper, ETextAlign::Center);
		}
		// あと N: big in the middle as it counts down, a ring bursting out behind it.
		const float SinceCount = static_cast<float>(Now - CountAt);
		if (SinceCount < 1.15f && Done < L->Count && Done > 0)
		{
			const int32 Left = L->Count - Done;
			const FLinearColor Color = Left == 1 ? Gold : Paper;
			const float Pop = 1.0f + 0.35f * FMath::Max(0.0f, 1.0f - SinceCount / 0.16f);
			const FPainter Flash{Geometry, Elements, Layer + 30, FMath::Clamp((1.15f - SinceCount) / 0.35f, 0.0f, 1.0f)};
			const FVector2D C(W * 0.5f, H * 0.38f);
			Flash.Ring(C, 60.0f * S + 220.0f * S * EaseOut(FMath::Min(SinceCount / 0.6f, 1.0f)), WithAlpha(Color, 0.6f * (1.0f - SinceCount)), 6.0f * S);
			PaintSparks(HudAbove(Flash, 1), C, FMath::Min(SinceCount / 0.7f, 1.0f), 260.0f * S, Color, 14, static_cast<float>(Done));
			const FString Text = Left == 1 ? FString::Printf(TEXT("あと1%s！"), *L->Unit) : FString::Printf(TEXT("あと %d %s"), Left, *L->Unit);
			HudText3D(HudAbove(Flash, 3), Text, C.X, C.Y - 40.0f * S * Pop, 72.0f * S * Pop, Color, ETextAlign::Center);
		}
		// A miss: a red banner sliding in, shaking, with what to do instead.
		const float SinceFail = static_cast<float>(Now - FailAt);
		if (SinceFail < 1.9f)
		{
			const float Alpha = FMath::Clamp((1.9f - SinceFail) / 0.4f, 0.0f, 1.0f);
			const FPainter Miss{Geometry, Elements, Layer + 32, Alpha};
			const float SlideIn = EaseOut(FMath::Clamp(SinceFail / 0.18f, 0.0f, 1.0f));
			const float MissSize = HudFit(FailText, 38.0f * S, W * 0.7f);
			const float Shake = SinceFail < 0.35f ? FMath::Sin(SinceFail * 70.0f) * 8.0f * S * (1.0f - SinceFail / 0.35f) : 0.0f;
			const float BW = HudMeasure(FailText, MissSize) + 120.0f * S;
			const float BX = W * 0.5f - BW * 0.5f + (1.0f - SlideIn) * -W * 0.3f + Shake;
			const float BY = H * 0.2f;
			HudShard(Miss, {FVector2D(BX + 22.0f * S, BY), FVector2D(BX + BW, BY), FVector2D(BX + BW - 22.0f * S, BY + 70.0f * S), FVector2D(BX, BY + 70.0f * S)},
				MakeFill(FLinearColor(0.95f, 0.18f, 0.2f, 0.97f), FLinearColor(0.55f, 0.04f, 0.08f, 0.97f)), 3.0f, FVector2D(5.0, 6.0), Navy);
			HudText3D(HudAbove(Miss, 4), FailText, BX + BW * 0.5f, BY + (70.0f * S - MissSize * 1.38f) * 0.5f, MissSize, Paper, ETextAlign::Center);
		}
		// The sudden death lesson: out of the circle, its one-second gauge.
		if (L->Goal == EChaosImpactDojoGoal::StayInZone && OutTime > 0.0f)
		{
			const FPainter Out = HudAbove(P, 34);
			const FVector2D C(W * 0.5f, H * 0.62f);
			Out.Ring(C, 44.0f * S, WithAlpha(Ink, 0.7f), 10.0f * S);
			Out.Arc(C, 44.0f * S, -90.0f, -90.0f + 360.0f * FMath::Clamp(OutTime / AChaosImpactSuddenDeathZone::OutSeconds, 0.0f, 1.0f),
				FLinearColor(1.0f, 0.3f, 0.2f, 1.0f), 10.0f * S);
			HudText3D(Out, TEXT("円にもどれ！"), C.X, C.Y + 58.0f * S, 32.0f * S, FLinearColor(1.0f, 0.4f, 0.3f, 1.0f), ETextAlign::Center);
		}
	}
	if (Phase == EChaosImpactDojoPhase::Clear)
	{
		PaintClear(HudAbove(P, 50), W, H, Time);
		PaintClearMenu(HudAbove(P, 80), Viewer, W, H, Time);
	}
}

void AChaosImpactDojo::PaintClearMenu(const FPainter& P, const AChaosImpactPlayerController* Viewer, const float W, const float H, const float Time) const
{
	const float Since = static_cast<float>(GetWorld()->GetTimeSeconds() - PhaseAt - ClearMenuDelay);
	if (Since < 0.0f)
	{
		return;
	}
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	const FLinearColor Accent = All.IsValidIndex(Category) ? All[Category].Color : Gold;
	int32 NextCategory = 0;
	int32 NextLesson = 0;
	const bool bNext = FindNextLesson(NextCategory, NextLesson);
	const TCHAR* Labels[] = {TEXT("次へ進む"), TEXT("もう一度やる"), TEXT("レッスン選択へ戻る")};
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// Each rising in after the one before it.
		const float In = EaseOut(FMath::Clamp((Since - 0.06f * Index) / 0.28f, 0.0f, 1.0f));
		const bool bOff = Index == 0 && !bNext;
		const bool bOn = !bOff && Index == ClearChoice;
		const FBox2D Box = ClearButton(Index, W, H);
		const float BX = static_cast<float>(Box.Min.X);
		const float BW = static_cast<float>(Box.GetSize().X);
		const float BH = static_cast<float>(Box.GetSize().Y);
		const float BY = static_cast<float>(Box.Min.Y) + (1.0f - In) * 30.0f * S - (bOn ? 4.0f * S : 0.0f);
		const FPainter Button{P.Geometry, P.Elements, P.Layer, In * (bOff ? 0.35f : 1.0f)};
		if (bOn)
		{
			RoundedBox(Button, BX - (5.0f + 4.0f * Pulse) * S, BY - (5.0f + 4.0f * Pulse) * S, BW + (10.0f + 8.0f * Pulse) * S,
				BH + (10.0f + 8.0f * Pulse) * S, (BH * 0.5f) + 8.0f * S, WithAlpha(Accent, 0.35f + 0.2f * Pulse));
		}
		RoundedBox(HudAbove(Button, 1), BX, BY + 5.0f * S, BW, BH, BH * 0.5f, WithAlpha(Navy, 0.8f));
		RoundedBox(HudAbove(Button, 2), BX, BY, BW, BH, BH * 0.5f, bOn ? Accent : FLinearColor(0.06f, 0.09f, 0.18f, 0.96f));
		RoundedBox(HudAbove(Button, 3), BX + 3.0f * S, BY + 3.0f * S, BW - 6.0f * S, BH * 0.42f, BH * 0.4f,
			WithAlpha(FLinearColor::White, bOn ? 0.22f : 0.06f));
		const FString Label = Labels[Index];
		const float Size = HudFit(Label, 26.0f * S, BW - 40.0f * S);
		HudAbove(Button, 5).Text(Label, BX + BW * 0.5f, BY + (BH - Size * 1.38f) * 0.5f, Size, bOn ? Navy : Paper, ETextAlign::Center, TEXT("Black"));
	}
	// How to choose, under the buttons.
	const FPainter Hint{P.Geometry, P.Elements, P.Layer, EaseOut(FMath::Clamp((Since - 0.2f) / 0.3f, 0.0f, 1.0f))};
	const FString Key = DojoConfirmKey(Viewer);
	const FString Words = TEXT("で決定");
	const float CapW = DojoKeyCapWidth(Key, 15.0f * S);
	const float WordsW = HudMeasure(Words, 17.0f * S);
	const float HX = W * 0.5f - (CapW + 8.0f * S + WordsW) * 0.5f;
	const float HY = static_cast<float>(ClearButton(0, W, H).Max.Y) + 22.0f * S;
	DojoKeyCap(HudAbove(Hint, 1), HX, HY, Key, 15.0f * S);
	HudAbove(Hint, 1).Text(Words, HX + CapW + 8.0f * S, HY, 17.0f * S, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
}

void AChaosImpactDojo::PaintSlides(const FPainter& P, const float W, const float H, const float Time) const
{
	const FChaosImpactDojoLesson* L = GetLesson();
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	if (!L || !L->Slides.IsValidIndex(Slide) || !All.IsValidIndex(Category))
	{
		return;
	}
	const FLinearColor Accent = All[Category].Color;
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	const double Now = GetWorld()->GetTimeSeconds();
	const float SinceOpen = static_cast<float>(Now - PhaseAt);
	const float SinceSlide = static_cast<float>(Now - SlideAt);
	const float Open = EaseOut(FMath::Clamp(SinceOpen / 0.3f, 0.0f, 1.0f));
	const float Turn = EaseOut(FMath::Clamp(SinceSlide / 0.26f, 0.0f, 1.0f));
	const FChaosImpactDojoSlide& Shown = L->Slides[Slide];

	// The game dimmed behind, the group's colour in a soft band.
	P.Box(0.0f, 0.0f, W, H, FLinearColor(0.0f, 0.01f, 0.03f, 0.62f * Open));
	P.Box(0.0f, H * 0.5f - 300.0f * S, W, 600.0f * S, WithAlpha(Accent, 0.06f * Open));

	// The panel: a header (the lesson's name, where in its slides this is), the clip, the line under it.
	const float PW = FMath::Min(W * 0.74f, 1360.0f * S);
	const float VideoW = PW - 56.0f * S;
	const float VideoH = VideoW * 9.0f / 16.0f;
	const float HeadH = 70.0f * S;
	const float CaptionH = 112.0f * S;
	const float PH = HeadH + VideoH + CaptionH + 92.0f * S;
	const float PX = (W - PW) * 0.5f;
	const float PY = (H - PH) * 0.5f + (1.0f - Open) * 40.0f * S;
	const FPainter Panel{P.Geometry, P.Elements, P.Layer + 2, Open};
	HudShard(Panel, {FVector2D(PX + 16.0f * S, PY), FVector2D(PX + PW, PY), FVector2D(PX + PW - 16.0f * S, PY + PH), FVector2D(PX, PY + PH)},
		MakeFill(FLinearColor(0.09f, 0.13f, 0.26f, 0.98f), FLinearColor(0.02f, 0.035f, 0.09f, 0.98f)), 3.0f, FVector2D(8.0, 10.0), Navy);
	// Header.
	{
		const FPainter Head = HudAbove(Panel, 4);
		const FString Number = FString::Printf(TEXT("LESSON %d"), LessonIndex + 1);
		const float NumberW = HudMeasure(Number, 16.0f * S, TEXT("BlackItalic")) + 30.0f * S;
		HudShard(Head, {FVector2D(PX + 28.0f * S + 10.0f * S, PY + 18.0f * S), FVector2D(PX + 28.0f * S + NumberW + 10.0f * S, PY + 18.0f * S),
			FVector2D(PX + 28.0f * S + NumberW, PY + 52.0f * S), FVector2D(PX + 28.0f * S, PY + 52.0f * S)}, MakeFill(Accent * 1.1f, Accent * 0.6f),
			0.0f, FVector2D::ZeroVector, Accent);
		HudAbove(Head, 3).Text(Number, PX + 33.0f * S + NumberW * 0.5f, PY + 23.0f * S, 16.0f * S, Paper, ETextAlign::Center, TEXT("BlackItalic"));
		const float NameSize = HudFit(L->Name, 32.0f * S, PW - NumberW - 220.0f * S);
		HudAbove(Head, 3).Text(L->Name, PX + 46.0f * S + NumberW, PY + 15.0f * S, NameSize, Paper, ETextAlign::Left, TEXT("Black"));
		const FString Page = FString::Printf(TEXT("%d / %d"), Slide + 1, L->Slides.Num());
		HudAbove(Head, 3).Text(Page, PX + PW - 40.0f * S, PY + 19.0f * S, 24.0f * S, WithAlpha(Paper, 0.8f), ETextAlign::Right, TEXT("BlackItalic"));
	}
	// The clip in its frame (sliding in on each new slide); the drawn picture until it has frames.
	const float VX = PX + 28.0f * S + (1.0f - Turn) * 50.0f * S;
	const float VY = PY + HeadH;
	const FPainter Frame{P.Geometry, P.Elements, P.Layer + 8, Open * Turn};
	RoundedBox(Frame, VX - 8.0f * S, VY - 8.0f * S, VideoW + 16.0f * S, VideoH + 16.0f * S, 16.0f * S, WithAlpha(Accent, 0.9f));
	RoundedBox(HudAbove(Frame, 1), VX - 3.0f * S, VY - 3.0f * S, VideoW + 6.0f * S, VideoH + 6.0f * S, 12.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	const FSlateBrush* Picture = Video ? Video->GetBrush() : nullptr;
	if (Picture)
	{
		FSlateDrawElement::MakeBox(P.Elements, P.Layer + 12, P.Geometry.ToPaintGeometry(FVector2f(VideoW, VideoH), FSlateLayoutTransform(FVector2f(VX, VY))),
			Picture, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, Frame.Alpha));
	}
	else
	{
		PaintArt(HudAbove(Frame, 4), Shown.Art, L->Ball, VX, VY, VideoW, VideoH, Time);
	}
	// Its progress, a red line along its foot.
	const float Played = Video && Picture ? Video->GetProgress() : FMath::Frac(Time / 3.2f);
	HudAbove(Frame, 20).Box(VX, VY + VideoH - 6.0f * S, VideoW, 6.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	HudAbove(Frame, 21).Box(VX, VY + VideoH - 6.0f * S, VideoW * Played, 6.0f * S, FLinearColor(1.0f, 0.18f, 0.22f, 1.0f));
	// The line: big, white, in its own band, faded up a beat after the picture.
	{
		const float CY = VY + VideoH + 18.0f * S;
		const FPainter Line{P.Geometry, P.Elements, P.Layer + 20, Open * EaseOut(FMath::Clamp((SinceSlide - 0.08f) / 0.25f, 0.0f, 1.0f))};
		RoundedBox(Line, PX + 28.0f * S, CY, VideoW, CaptionH - 10.0f * S, 14.0f * S, FLinearColor(0.96f, 0.97f, 1.0f, 0.97f));
		RoundedBox(HudAbove(Line, 1), PX + 28.0f * S, CY, 10.0f * S, CaptionH - 10.0f * S, 5.0f * S, Accent);
		const float TextSize = HudFit(Shown.Text, 36.0f * S, VideoW - 80.0f * S);
		HudAbove(Line, 2).Text(Shown.Text, PX + 28.0f * S + VideoW * 0.5f, CY + (CaptionH - 10.0f * S - TextSize * 1.38f) * 0.5f, TextSize,
			FLinearColor(0.03f, 0.05f, 0.12f, 1.0f), ETextAlign::Center, TEXT("Black"));
	}
	// The foot: where in the slides (dots), and the keys to go on or back.
	{
		const FPainter Foot = HudAbove(Panel, 30);
		const float FY = PY + PH - 50.0f * S;
		const int32 Count = L->Slides.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D Dot(PX + PW * 0.5f + (Index - (Count - 1) * 0.5f) * 30.0f * S, FY + 14.0f * S);
			const bool bHere = Index == Slide;
			if (bHere)
			{
				RoundedBox(Foot, Dot.X - 16.0f * S, Dot.Y - 6.0f * S, 32.0f * S, 12.0f * S, 6.0f * S, Accent);
			}
			else
			{
				Foot.Disc(Dot, 6.0f * S, WithAlpha(Paper, Index < Slide ? 0.75f : 0.25f));
			}
		}
		const AChaosImpactPlayerController* Viewer = GetPlayer();
		const bool bLast = Slide + 1 >= Count;
		const float Pulse = 0.6f + 0.4f * FMath::Sin(Time * 5.0f);
		float X = PX + PW - 48.0f * S;
		const FString Next = bLast ? TEXT("課題へ") : TEXT("つぎへ");
		X -= HudMeasure(Next, 24.0f * S) ;
		HudAbove(Foot, 2).Text(Next, X, FY, 24.0f * S, WithAlpha(bLast ? Gold : Paper, 0.75f + 0.25f * Pulse), ETextAlign::Left, TEXT("Black"));
		const FString Key = Viewer && Viewer->IsUsingGamepad() ? TEXT("A") : TEXT("Enter");
		X -= DojoKeyCapWidth(Key, 17.0f * S) + 10.0f * S;
		DojoKeyCap(HudAbove(Foot, 2), X, FY + 2.0f * S, Key, 17.0f * S);
		if (Slide > 0)
		{
			const FString BackKey = Viewer && Viewer->IsUsingGamepad() ? TEXT("B") : TEXT("BackSpace");
			const float BX = PX + 48.0f * S;
			const float CapW = DojoKeyCap(HudAbove(Foot, 2), BX, FY + 2.0f * S, BackKey, 15.0f * S);
			HudAbove(Foot, 2).Text(TEXT("もどる"), BX + CapW + 10.0f * S, FY + 2.0f * S, 20.0f * S, WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Black"));
		}
	}
}

void AChaosImpactDojo::PaintTask(const FPainter& P, const AChaosImpactPlayerController* Viewer, const float W, const float H, const float Time) const
{
	const FChaosImpactDojoLesson* L = GetLesson();
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	if (!L || !All.IsValidIndex(Category))
	{
		return;
	}
	const FLinearColor Accent = All[Category].Color;
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f) * 1.12f;
	const bool bReady = Phase == EChaosImpactDojoPhase::Ready;
	const double Now = GetWorld()->GetTimeSeconds();
	const float In = EaseOut(FMath::Clamp(static_cast<float>(Now - PhaseAt) / 0.32f, 0.0f, 1.0f));
	const float SinceCount = static_cast<float>(Now - CountAt);
	const float Flash = FMath::Max(0.0f, 1.0f - SinceCount / 0.35f);
	// The card at the top right, under the corner buttons: the task, its keys; at its right, the count in a ring.
	const float CardW = 600.0f * S;
	const float CardH = 128.0f * S;
	// (Left of the column of corner buttons the mouse styles have at the right.)
	const bool bCornerColumn = Viewer && !Viewer->IsUsingGamepad()
		&& ChaosImpactSettings::GetPlayerControlScheme(Viewer) != EChaosImpactControlScheme::Normal;
	const float X = W - CardW - (bCornerColumn ? 250.0f : 28.0f) * S + (1.0f - In) * 90.0f * S;
	// (Under the clock in a mock match.)
	const float Y = (IsMatchWorld() ? 168.0f : 76.0f) * S;
	const FPainter Card{P.Geometry, P.Elements, P.Layer, In};
	const TArray<FVector2D> Shape = {FVector2D(X + 18.0f * S, Y), FVector2D(X + CardW, Y), FVector2D(X + CardW - 18.0f * S, Y + CardH), FVector2D(X, Y + CardH)};
	if (Flash > 0.0f)
	{
		HudPoly(Card, HudInflate(Shape, 10.0f * S * Flash), SolidFill(WithAlpha(Accent, 0.5f * Flash)), 1);
	}
	HudShard(HudAbove(Card, 1), Shape, MakeFill(FLinearColor(0.1f, 0.15f, 0.29f, 0.96f), FLinearColor(0.025f, 0.04f, 0.1f, 0.96f)), 2.5f,
		FVector2D(5.0, 6.0), Navy);
	// Its label tab, in the group's colour.
	{
		const float TW = 96.0f * S;
		HudShard(HudAbove(Card, 4), {FVector2D(X + 30.0f * S, Y - 14.0f * S), FVector2D(X + 30.0f * S + TW, Y - 14.0f * S),
			FVector2D(X + 22.0f * S + TW, Y + 14.0f * S), FVector2D(X + 22.0f * S, Y + 14.0f * S)}, MakeFill(Accent * 1.1f, Accent * 0.6f), 2.0f,
			FVector2D(2.0, 3.0), Navy);
		HudAbove(Card, 7).Text(TEXT("課題"), X + 26.0f * S + TW * 0.5f, Y - 12.0f * S, 18.0f * S, Paper, ETextAlign::Center, TEXT("Black"));
	}
	const float RingR = 50.0f * S;
	const FVector2D RingC(X + CardW - 26.0f * S - RingR - 8.0f * S, Y + CardH * 0.5f);
	const float TextW = CardW - 2.0f * RingR - 90.0f * S;
	const float TaskSize = HudFit(L->Task, 27.0f * S, TextW);
	HudAbove(Card, 6).Text(L->Task, X + 30.0f * S, Y + 22.0f * S, TaskSize, Paper, ETextAlign::Left, TEXT("Black"));
	float KeyX = X + 30.0f * S;
	const float KeyY = Y + 72.0f * S;
	// (A lesson on a control this control type does not have: a note in place of the keys.)
	const FString Missing = GetMissingControl();
	if (!Missing.IsEmpty())
	{
		const FString Note = FString::Printf(TEXT("いまの操作タイプには「%s」がありません（設定の操作タイプ「ノーマル」で使えます）"), *Missing);
		const float NoteSize = HudFit(Note, 16.0f * S, TextW);
		HudAbove(Card, 6).Text(Note, X + 30.0f * S, KeyY + 2.0f * S, NoteSize, FLinearColor(1.0f, 0.82f, 0.4f, 1.0f), ETextAlign::Left, TEXT("Black"));
	}
	for (const FString& Key : Missing.IsEmpty() ? DojoKeys(Viewer, *L) : TArray<FString>())
	{
		if (KeyX + DojoKeyCapWidth(Key, 16.0f * S) > X + 30.0f * S + TextW)
		{
			break;
		}
		KeyX += DojoKeyCap(HudAbove(Card, 6), KeyX, KeyY, Key, 16.0f * S) + 7.0f * S;
	}
	// The count: a ring filling as it is done, how many are left big inside it, the unit under.
	{
		const int32 Left = FMath::Max(L->Count - Done, 0);
		const float Share = L->Count > 0 ? static_cast<float>(Done) / L->Count : 0.0f;
		const FPainter Ring = HudAbove(Card, 6);
		Ring.Disc(RingC, RingR + 6.0f * S, Navy);
		Ring.Disc(RingC, RingR, FLinearColor(0.02f, 0.03f, 0.08f, 1.0f));
		HudAbove(Ring, 1).Ring(RingC, RingR - 6.0f * S, FLinearColor(0.1f, 0.15f, 0.3f, 1.0f), 8.0f * S);
		HudAbove(Ring, 2).Arc(RingC, RingR - 6.0f * S, -90.0f, -90.0f + 360.0f * Share, Left <= 1 ? Gold : Accent, 8.0f * S);
		const float Pop = 1.0f + 0.35f * Flash;
		HudAbove(Ring, 5).Text(TEXT("あと"), RingC.X, RingC.Y - 40.0f * S, 12.0f * S, WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Black"));
		HudText3D(HudAbove(Ring, 3), FString::FromInt(Left), RingC.X, RingC.Y - 27.0f * S * Pop, 38.0f * S * Pop, Left <= 1 ? Gold : Paper,
			ETextAlign::Center);
		HudAbove(Ring, 5).Text(L->Unit, RingC.X, RingC.Y + 19.0f * S, 12.0f * S, WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Black"));
	}
	// Thrown from the spot: a chip beside the 課題 tab (why walking does nothing here).
	if (IsRootedLesson(*L))
	{
		const FString Rule = TEXT("その場から");
		const float RW = HudMeasure(Rule, 16.0f * S) + 30.0f * S;
		const float RX = X + 140.0f * S;
		RoundedBox(HudAbove(Card, 4), RX, Y - 16.0f * S, RW, 32.0f * S, 16.0f * S, FLinearColor(0.02f, 0.03f, 0.08f, 1.0f));
		HudAbove(Card, 5).Text(Rule, RX + RW * 0.5f, Y - 16.0f * S + (32.0f * S - 16.0f * S * 1.38f) * 0.5f, 16.0f * S, Ice, ETextAlign::Center,
			TEXT("Black"));
	}
	// A time limit: how long is left (red in the last five seconds), or in the start screen what it is.
	if (L->TimeLimit > 0.0f)
	{
		const float Left = bReady ? L->TimeLimit : FMath::Max(0.0f, L->TimeLimit - static_cast<float>(Now - TaskStartAt));
		const bool bHurry = !bReady && Left < 5.0f;
		const FString Clock = bReady ? FString::Printf(TEXT("制限 %d秒"), FMath::RoundToInt(L->TimeLimit)) : FString::Printf(TEXT("のこり %.1f秒"), Left);
		const float ClockSize = 18.0f * S * (bHurry ? 1.0f + 0.08f * FMath::Abs(FMath::Sin(Time * 10.0f)) : 1.0f);
		const float CW = HudMeasure(Clock, ClockSize) + 30.0f * S;
		const float CX = X + CardW - CW - 30.0f * S;
		RoundedBox(HudAbove(Card, 4), CX, Y - 16.0f * S, CW, 32.0f * S, 16.0f * S, bHurry ? FLinearColor(0.95f, 0.15f, 0.2f, 1.0f) : FLinearColor(0.02f, 0.03f, 0.08f, 1.0f));
		HudAbove(Card, 5).Text(Clock, CX + CW * 0.5f, Y - 16.0f * S + (32.0f * S - ClockSize * 1.38f) * 0.5f, ClockSize, Paper, ETextAlign::Center, TEXT("Black"));
	}
	else if (L->bNoDamage)
	{
		const FString Rule = TEXT("ノーダメージ");
		const float CW = HudMeasure(Rule, 16.0f * S) + 30.0f * S;
		const float CX = X + CardW - CW - 30.0f * S;
		RoundedBox(HudAbove(Card, 4), CX, Y - 16.0f * S, CW, 32.0f * S, 16.0f * S, FLinearColor(0.95f, 0.15f, 0.2f, 1.0f));
		HudAbove(Card, 5).Text(Rule, CX + CW * 0.5f, Y - 16.0f * S + (32.0f * S - 16.0f * S * 1.38f) * 0.5f, 16.0f * S, Paper, ETextAlign::Center, TEXT("Black"));
	}
	// Under the card: one pip for each to do, lit as they are done.
	if (L->Count > 1 && L->Count <= 14)
	{
		const float PipY = Y + CardH + 16.0f * S;
		for (int32 Index = 0; Index < L->Count; ++Index)
		{
			const FVector2D C(X + CardW - 40.0f * S - (L->Count - 1 - Index) * 26.0f * S, PipY);
			const bool bDone = Index < Done;
			const float Pop = bDone && Index == Done - 1 ? 1.0f + 0.6f * Flash : 1.0f;
			HudShard(HudAbove(Card, 2), {C + FVector2D(0.0, -9.0f * S * Pop), C + FVector2D(9.0f * S * Pop, 0.0), C + FVector2D(0.0, 9.0f * S * Pop),
				C + FVector2D(-9.0f * S * Pop, 0.0)}, SolidFill(bDone ? Gold : FLinearColor(0.08f, 0.12f, 0.24f, 0.95f)), 2.0f, FVector2D::ZeroVector,
				bDone ? FLinearColor(1.0f, 0.95f, 0.6f, 1.0f) : WithAlpha(Paper, 0.35f));
		}
	}
	if (bReady)
	{
		// The start: a button under the card, breathing; and the slides again.
		const float BY = Y + CardH + (L->Count > 1 && L->Count <= 14 ? 36.0f : 16.0f) * S;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 4.5f);
		const float BW = 330.0f * S;
		const float BX = X + CardW - BW;
		const TArray<FVector2D> Button = {FVector2D(BX + 16.0f * S, BY), FVector2D(BX + BW, BY), FVector2D(BX + BW - 16.0f * S, BY + 58.0f * S),
			FVector2D(BX, BY + 58.0f * S)};
		HudPoly(Card, HudInflate(Button, (4.0f + 6.0f * Pulse) * S), SolidFill(WithAlpha(Accent, 0.2f + 0.25f * Pulse)), 1);
		HudShard(HudAbove(Card, 1), Button, MakeFill(Accent * 1.1f, Accent * 0.55f), 2.5f, FVector2D(4.0, 5.0), Paper);
		const FString Key = Viewer && Viewer->IsUsingGamepad() ? TEXT("A") : TEXT("Enter");
		const float CapW = DojoKeyCap(HudAbove(Card, 6), BX + 34.0f * S, BY + 14.0f * S, Key, 17.0f * S);
		HudAbove(Card, 6).Text(Missing.IsEmpty() ? TEXT("はじめる！") : TEXT("とばす"), BX + 46.0f * S + CapW, BY + 10.0f * S, 28.0f * S, Paper,
			ETextAlign::Left, TEXT("Black"));
		const FString BackKey = Viewer && Viewer->IsUsingGamepad() ? TEXT("B") : TEXT("BackSpace");
		const float AgainY = BY + 72.0f * S;
		const float AgainCap = DojoKeyCapWidth(BackKey, 13.0f * S);
		const FString Again = TEXT("説明をもう一度");
		const float AgainX = X + CardW - HudMeasure(Again, 16.0f * S) - AgainCap - 30.0f * S;
		DojoKeyCap(HudAbove(Card, 6), AgainX, AgainY, BackKey, 13.0f * S);
		HudAbove(Card, 6).Text(Again, AgainX + AgainCap + 8.0f * S, AgainY, 16.0f * S, WithAlpha(Paper, 0.75f), ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
	}
}

void AChaosImpactDojo::PaintClear(const FPainter& P, const float W, const float H, const float Time) const
{
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	const float Since = static_cast<float>(GetWorld()->GetTimeSeconds() - PhaseAt);
	const float In = EaseOut(FMath::Clamp(Since / 0.25f, 0.0f, 1.0f));
	// (Kept up while its choices are shown under it.)
	const float Out = 1.0f;
	const FPainter Clear{P.Geometry, P.Elements, P.Layer, Out};
	const FVector2D C(W * 0.5f, H * 0.46f);
	const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
	const FLinearColor Accent = All.IsValidIndex(Category) ? All[Category].Color : Gold;

	// The band across the middle, opening out.
	const float BandH = 300.0f * S * In;
	Clear.Box(0.0f, C.Y - BandH * 0.5f, W, BandH, FLinearColor(0.0f, 0.015f, 0.05f, 0.72f));
	Clear.Box(0.0f, C.Y - BandH * 0.5f, W, 4.0f * S, WithAlpha(Gold, 0.9f));
	Clear.Box(0.0f, C.Y + BandH * 0.5f - 4.0f * S, W, 4.0f * S, WithAlpha(Gold, 0.9f));
	// A soft glow behind the word, and rings opening out from it.
	{
		const FPainter Glow = HudAbove(Clear, 1);
		Glow.Disc(C, 260.0f * S * In, WithAlpha(Gold, 0.12f));
		Glow.Disc(C, 170.0f * S * In, WithAlpha(Gold, 0.14f));
		for (int32 Wave = 0; Wave < 3; ++Wave)
		{
			const float T = FMath::Frac(Since * 0.7f + Wave / 3.0f);
			Glow.Ring(C, (180.0f + 520.0f * T) * S, WithAlpha(Wave % 2 == 0 ? Gold : Accent, 0.55f * (1.0f - T)), 5.0f * S);
		}
	}
	// CLEAR!, each letter popping in after the one before it.
	{
		const FString Word = TEXT("CLEAR!");
		const float Size = 158.0f * S;
		float Total = 0.0f;
		TArray<float> Widths;
		for (const TCHAR Letter : Word)
		{
			Widths.Add(HudMeasure(FString(1, &Letter), Size, TEXT("BlackItalic")) * 0.96f);
			Total += Widths.Last();
		}
		float LX = C.X - Total * 0.5f;
		for (int32 Index = 0; Index < Word.Len(); ++Index)
		{
			const float T = FMath::Clamp((Since - 0.06f * Index) / 0.22f, 0.0f, 1.0f);
			if (T > 0.0f)
			{
				// Overshoot then settle.
				const float Pop = T < 0.6f ? FMath::Lerp(2.2f, 0.88f, T / 0.6f) : FMath::Lerp(0.88f, 1.0f, (T - 0.6f) / 0.4f);
				const float LetterSize = Size * Pop;
				const FLinearColor Color = FMath::Lerp(Paper, FLinearColor(1.0f, 0.95f, 0.7f, 1.0f), 0.5f + 0.5f * FMath::Sin(Time * 6.0f + Index));
				const FPainter Letter{Clear.Geometry, Clear.Elements, Clear.Layer + 8, Clear.Alpha * FMath::Min(T * 3.0f, 1.0f)};
				HudText3D(Letter, FString(1, &Word[Index]), LX + Widths[Index] * 0.5f, C.Y - LetterSize * 0.72f, LetterSize, Color, ETextAlign::Center);
			}
			LX += Widths[Index];
		}
		// A shine sweeping across.
		const float Sweep = FMath::Fmod(Since * 0.9f, 1.4f);
		if (Sweep < 1.0f)
		{
			const float SX = C.X - Total * 0.6f + Total * 1.2f * Sweep;
			HudPoly(HudAbove(Clear, 12), {FVector2D(SX, C.Y - 110.0f * S), FVector2D(SX + 40.0f * S, C.Y - 110.0f * S), FVector2D(SX - 20.0f * S, C.Y + 90.0f * S),
				FVector2D(SX - 60.0f * S, C.Y + 90.0f * S)}, SolidFill(WithAlpha(FLinearColor::White, 0.18f)), 1);
		}
	}
	// Confetti: bits of colour tumbling down across the screen.
	{
		const FLinearColor Colors[] = {Gold, Accent, Paper, FLinearColor(1.0f, 0.3f, 0.5f, 1.0f), FLinearColor(0.3f, 0.9f, 0.6f, 1.0f)};
		const FPainter Bits = HudAbove(Clear, 14);
		for (int32 Bit = 0; Bit < 56; ++Bit)
		{
			const float Seed = FMath::Frac(Bit * 0.6180339f);
			const float Seed2 = FMath::Frac(Bit * 0.3819660f + 0.17f);
			const float Fall = Since * (260.0f + 220.0f * Seed2) * S;
			const float BX = W * Seed + FMath::Sin(Since * (2.0f + Seed * 3.0f) + Bit) * 40.0f * S;
			const float BY = -40.0f * S - 400.0f * S * Seed2 + Fall + (Since < 0.3f ? 0.0f : 0.0f);
			if (BY > H + 20.0f * S)
			{
				continue;
			}
			const float A = Since * (3.0f + 5.0f * Seed) + Bit;
			const FVector2D U(FMath::Cos(A), FMath::Sin(A));
			const FVector2D V(-U.Y, U.X);
			const float BW2 = 9.0f * S;
			const float BH2 = 4.0f * S * (0.4f + 0.6f * FMath::Abs(FMath::Sin(Since * 6.0f + Bit)));
			const FVector2D At(BX, BY);
			HudPoly(Bits, {At - U * BW2 - V * BH2, At + U * BW2 - V * BH2, At + U * BW2 + V * BH2, At - U * BW2 + V * BH2},
				SolidFill(Colors[Bit % UE_ARRAY_COUNT(Colors)]), 1);
		}
	}
	// The group completed: said under it (nothing else is).
	const int32 Cleared = ChaosImpactDojo::CountCleared(Category);
	const bool bAll = All.IsValidIndex(Category) && Cleared >= All[Category].Lessons.Num() && !ChaosImpactDojo::GetLesson(Category, LessonIndex + 1);
	if (bAll)
	{
		const FString Line = FString::Printf(TEXT("「%s」コンプリート！"), *All[Category].Name);
		const float LineIn = EaseOut(FMath::Clamp((Since - 0.45f) / 0.3f, 0.0f, 1.0f));
		const FPainter Below{Clear.Geometry, Clear.Elements, Clear.Layer + 16, Clear.Alpha * LineIn};
		const float LineSize = HudFit(Line, 34.0f * S, W * 0.7f);
		const float LW = HudMeasure(Line, LineSize) + 80.0f * S;
		const float LY = C.Y + 110.0f * S + (1.0f - LineIn) * 30.0f * S;
		RoundedBox(Below, C.X - LW * 0.5f, LY, LW, 56.0f * S, 28.0f * S, Gold);
		HudAbove(Below, 2).Text(Line, C.X, LY + (56.0f * S - LineSize * 1.38f) * 0.5f, LineSize, Navy, ETextAlign::Center, TEXT("Black"));
	}
}

void AChaosImpactDojo::PaintPointer(const FPainter& P, const EChaosImpactDojoPointer Pointer, const float W, const float H, const float Scale,
	const float Time) const
{
	// Where those parts of the HUD are (the charge widget's layout: vitals at the bottom left, balls at the bottom right).
	FVector2D Spot;
	FString Label;
	switch (Pointer)
	{
	case EChaosImpactDojoPointer::Health: Spot = FVector2D(300.0f * Scale, H - 150.0f * Scale); Label = TEXT("HP"); break;
	case EChaosImpactDojoPointer::Stamina: Spot = FVector2D(300.0f * Scale, H - 92.0f * Scale); Label = TEXT("スタミナ"); break;
	case EChaosImpactDojoPointer::Balls: Spot = FVector2D(W - 170.0f * Scale, H - 175.0f * Scale); Label = TEXT("持っているボール"); break;
	default: return;
	}
	// A red arrow bobbing down onto it, a ring pulsing round it, and what it is.
	const float Bob = FMath::Abs(FMath::Sin(Time * 4.0f)) * 18.0f * Scale;
	const FVector2D Tip = Spot - FVector2D(0.0, 30.0f * Scale + Bob);
	const float A = 38.0f * Scale;
	const FLinearColor Red(1.0f, 0.14f, 0.16f, 1.0f);
	for (int32 Wave = 0; Wave < 2; ++Wave)
	{
		const float T = FMath::Frac(Time * 0.9f + Wave * 0.5f);
		P.Ring(Spot, (40.0f + 40.0f * T) * Scale, WithAlpha(Red, 0.8f * (1.0f - T)), 4.0f * Scale);
	}
	HudShard(HudAbove(P, 2), {Tip, Tip + FVector2D(A, -A), Tip + FVector2D(A * 0.42f, -A), Tip + FVector2D(A * 0.42f, -A * 2.3f),
		Tip + FVector2D(-A * 0.42f, -A * 2.3f), Tip + FVector2D(-A * 0.42f, -A), Tip + FVector2D(-A, -A)},
		MakeFill(FLinearColor(1.0f, 0.35f, 0.3f, 1.0f), Red), 3.0f, FVector2D(3.0, 4.0), FLinearColor::White);
	const float LabelSize = 20.0f * Scale;
	const float LW = HudMeasure(Label, LabelSize) + 28.0f * Scale;
	const FVector2D LabelAt = Tip + FVector2D(-LW * 0.5f, -A * 2.3f - 44.0f * Scale);
	RoundedBox(HudAbove(P, 2), LabelAt.X, LabelAt.Y, LW, 36.0f * Scale, 18.0f * Scale, Red);
	HudAbove(P, 4).Text(Label, LabelAt.X + LW * 0.5f, LabelAt.Y + (36.0f * Scale - LabelSize * 1.38f) * 0.5f, LabelSize, Paper, ETextAlign::Center, TEXT("Black"));
}

void AChaosImpactDojo::PaintObjectives(const FPainter& P, const AChaosImpactPlayerController* Viewer, const float W, const float H, const float Time) const
{
	if (!Viewer || !GetWorld())
	{
		return;
	}
	const float S = FMath::Min(W / 1920.0f, H / 1080.0f);
	struct FMark
	{
		FVector At;
		FString Label;
		FLinearColor Color;
		bool bMain;
	};
	TArray<FMark> Marks;
	if (Rings.IsValidIndex(RingAt))
	{
		Marks.Add({Rings[RingAt] + FVector(0.0f, 0.0f, 40.0f), TEXT("輪"), Ice, true});
	}
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		if (Targets[Index].IsValid() && !Targets[Index]->IsDefeated())
		{
			Marks.Add({Targets[Index]->GetActorLocation() + FVector(0.0f, 0.0f, 170.0f), TEXT("マト"), Fire, false});
		}
	}
	for (const FDojoCPU& CPU : CPUs)
	{
		if (CPU.Pawn.IsValid() && !CPU.Pawn->IsEliminated())
		{
			Marks.Add({CPU.Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 130.0f), TEXT("CPU"), Fire, false});
		}
	}
	for (const TWeakObjectPtr<AChaosImpactBall>& Ball : Pickups)
	{
		if (Ball.IsValid() && Ball->IsPickup())
		{
			Marks.Add({Ball->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), TEXT("ボール"), Gold, true});
		}
	}
	// (The parts only in the nova lesson: a match-like lesson's stage puts out its own, not asked for there.)
	for (TActorIterator<AChaosImpactNovaPart> It(GetWorld()); It && GetLesson() && GetLesson()->Goal == EChaosImpactDojoGoal::NovaBuild; ++It)
	{
		Marks.Add({It->GetActorLocation(), ChaosImpactNovaParts::GetName(It->GetPart()), FLinearColor(0.45f, 0.75f, 1.0f, 1.0f), true});
	}
	if (Marks.IsEmpty())
	{
		return;
	}
	// The viewport's pixels to this HUD's units.
	FVector2D Viewport(W, H);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Viewport);
	}
	const FVector2D ToHud(W / FMath::Max(Viewport.X, 1.0), H / FMath::Max(Viewport.Y, 1.0));
	// Kept clear of the lesson's tag and card at the top and the HUD at the bottom.
	const FBox2D Room(FVector2D(70.0f * S, 120.0f * S), FVector2D(W - 70.0f * S, H - 200.0f * S));
	const FVector2D Mid = Room.GetCenter();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.0f);
	for (const FMark& Mark : Marks)
	{
		FVector2D Screen;
		const bool bFront = Viewer->ProjectWorldLocationToScreen(Mark.At, Screen, false);
		Screen *= ToHud;
		if (!bFront)
		{
			Screen = Mid - (Screen - Mid) * 4.0f;
		}
		if (bFront && Room.IsInside(Screen))
		{
			// On screen: a mark bobbing over the main things (the next ring, a ball, a part).
			if (Mark.bMain)
			{
				const float Bob = FMath::Abs(FMath::Sin(Time * 4.0f)) * 10.0f * S;
				const FVector2D Tip = Screen - FVector2D(0.0, 18.0f * S + Bob);
				HudShard(P, {Tip, Tip + FVector2D(16.0f * S, -18.0f * S), Tip + FVector2D(-16.0f * S, -18.0f * S)}, SolidFill(Mark.Color), 2.0f,
					FVector2D(2.0, 3.0), Navy);
				const float LabelSize = 15.0f * S;
				HudAbove(P, 3).Text(Mark.Label, Tip.X, Tip.Y - 18.0f * S - LabelSize * 1.5f, LabelSize, Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Navy);
			}
			continue;
		}
		// Off screen: a badge at the edge, an arrow out of it pointing the way.
		const FVector2D Way = Screen - Mid;
		if (Way.IsNearlyZero())
		{
			continue;
		}
		const FVector2D Half = Room.GetExtent();
		const double Reach = FMath::Min(Way.X != 0.0 ? Half.X / FMath::Abs(Way.X) : 1.0e9, Way.Y != 0.0 ? Half.Y / FMath::Abs(Way.Y) : 1.0e9);
		const FVector2D Edge = Mid + Way * Reach;
		const FVector2D Dir = Way.GetSafeNormal();
		const FVector2D Side(-Dir.Y, Dir.X);
		const float R = 30.0f * S;
		const FVector2D Badge = Edge - Dir * (R + 14.0f * S);
		P.Disc(Badge, R + 4.0f * S, Navy);
		P.Disc(Badge, R, WithAlpha(Mark.Color, 0.85f + 0.15f * Pulse));
		const FVector2D Tip = Badge + Dir * (R + 22.0f * S + 5.0f * S * Pulse);
		HudShard(HudAbove(P, 1), {Tip, Badge + Dir * (R + 4.0f * S) + Side * 13.0f * S, Badge + Dir * (R + 4.0f * S) - Side * 13.0f * S},
			SolidFill(Mark.Color), 2.0f, FVector2D::ZeroVector, Navy);
		const float LabelSize = HudFit(Mark.Label, 15.0f * S, R * 1.7f);
		HudAbove(P, 3).Text(Mark.Label, Badge.X, Badge.Y - LabelSize * 0.7f, LabelSize, Navy, ETextAlign::Center, TEXT("Black"));
	}
}
