#include "ChaosImpactModeMenu.h"

#include "ChaosImpactPaint.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

using namespace ChaosImpactPaint;

namespace
{
	/** The online menu's lean. */
	constexpr float TileShear = -0.12f;

	FGeometry Lean(const FGeometry& Parent, const float X, const float Y, const float W, const float H, const float Scale = 1.0f)
	{
		const FSlateRenderTransform Render(TMatrix2x2<float>(Scale, 0.0f, Scale * TileShear, Scale));
		return Parent.MakeChild(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y)), Render, FVector2f(0.5f, 0.5f));
	}

	TArray<FVector2D> TileBox(const float X, const float Y, const float W, const float H)
	{
		return {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)};
	}

	/** A key cap (dark chip, ice rim, the key's name). Returns its width. */
	float KeyCap(const FPainter& P, const float X, const float Y, const FString& Key, const float Size)
	{
		const float W = HudMeasure(Key, Size, TEXT("Black")) + Size * 1.1f;
		const float H = Size * 1.6f;
		RoundedBox(P, X, Y, W, H, H * 0.28f, FMath::Lerp(Navy, Ice, 0.45f));
		RoundedBox(HudAbove(P, 1), X + 2.0f, Y + 2.0f, W - 4.0f, H - 5.0f, H * 0.25f, FLinearColor(0.08f, 0.12f, 0.24f, 1.0f));
		HudAbove(P, 2).Text(Key, X + W * 0.5f, Y + (H - Size * 1.38f) * 0.5f - 1.0f, Size, Paper, ETextAlign::Center, TEXT("Black"));
		return W;
	}

	/** A small figure: a round head over a rounded body. */
	void Figure(const FPainter& P, const FVector2D& Feet, const float Size, const FLinearColor& Color)
	{
		RoundedBox(P, Feet.X - Size * 0.32f, Feet.Y - Size * 0.78f, Size * 0.64f, Size * 0.78f, Size * 0.26f, Color);
		P.Disc(Feet + FVector2D(0.0, -Size * 1.05f), Size * 0.27f, Color);
	}

	/** Each tile's own moving sign (B: how chosen; brighter and livelier once it is). */
	void Emblem(const FPainter& Icon, const FString& Kind, const FVector2D& At, const float Unit, const FLinearColor& Mark, const float B,
		const float Time)
	{
		const float Live = 0.35f + 0.65f * B;
		if (Kind == TEXT("solo"))
		{
			// The route through the stages: stops joined by a path, the one reached pulsing, a flag at the end.
			const FVector2D Stops[] = {At + FVector2D(-Unit * 0.95f, Unit * 0.55f), At + FVector2D(-Unit * 0.35f, Unit * 0.05f),
				At + FVector2D(Unit * 0.2f, Unit * 0.45f), At + FVector2D(Unit * 0.75f, -Unit * 0.2f)};
			for (int32 Index = 1; Index < UE_ARRAY_COUNT(Stops); ++Index)
			{
				for (int32 Dot = 0; Dot < 5; ++Dot)
				{
					Icon.Disc(FMath::Lerp(Stops[Index - 1], Stops[Index], (Dot + 0.5f) / 5.0f), Unit * 0.035f, WithAlpha(Mark, Mark.A * 0.8f));
				}
			}
			const int32 Reached = FMath::FloorToInt(Time * 0.8f * Live) % 3;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Stops); ++Index)
			{
				Icon.Disc(Stops[Index], Unit * (Index == Reached ? 0.16f : 0.12f), Mark);
				if (Index == Reached)
				{
					const float Wave = FMath::Frac(Time * 1.2f);
					Icon.Ring(Stops[Index], Unit * (0.18f + 0.22f * Wave), WithAlpha(Mark, Mark.A * (1.0f - Wave)), 3.0f);
				}
			}
			const FVector2D Pole = Stops[3] + FVector2D(Unit * 0.05f, -Unit * 0.1f);
			Icon.Line(Pole, Pole + FVector2D(0.0, -Unit * 0.75f), Mark, 4.0f);
			const float Wave = FMath::Sin(Time * 5.0f) * Unit * 0.05f * Live;
			HudPoly(Icon, {Pole + FVector2D(0.0, -Unit * 0.75f), Pole + FVector2D(Unit * 0.5f, -Unit * 0.62f + Wave),
				Pole + FVector2D(0.0, -Unit * 0.46f)}, SolidFill(WithAlpha(Gold, Mark.A)), 1);
		}
		else if (Kind == TEXT("vs"))
		{
			// Two players facing, a ball going back and forth between them.
			Figure(Icon, At + FVector2D(-Unit * 0.85f, Unit * 0.62f), Unit * 0.7f, WithAlpha(FMath::Lerp(Ice, Paper, 0.3f), Mark.A));
			Figure(Icon, At + FVector2D(Unit * 0.85f, Unit * 0.62f), Unit * 0.7f, WithAlpha(FMath::Lerp(Fire, Paper, 0.25f), Mark.A));
			const float Phase = FMath::Frac(Time * 0.7f * Live);
			const float Side = Phase < 0.5f ? Phase * 2.0f : 2.0f - Phase * 2.0f;
			const float X = FMath::Lerp(-Unit * 0.55f, Unit * 0.55f, Side);
			const float Y = -Unit * 0.15f - FMath::Sin(Side * UE_PI) * Unit * 0.35f;
			Icon.Disc(At + FVector2D(X, Y), Unit * 0.14f, WithAlpha(Paper, Mark.A));
			Icon.Disc(At + FVector2D(X - Unit * 0.04f, Y - Unit * 0.04f), Unit * 0.05f, WithAlpha(FLinearColor::White, Mark.A));
		}
		else if (Kind == TEXT("dojo"))
		{
			// A target, a ball arriving in its middle again and again.
			Icon.Ring(At, Unit * 0.78f, Mark, 6.0f);
			Icon.Ring(At, Unit * 0.5f, Mark, 6.0f);
			Icon.Disc(At, Unit * 0.2f, Mark);
			const float Phase = FMath::Frac(Time * 0.6f * Live);
			const FVector2D From = At + FVector2D(-Unit * 1.3f, Unit * 0.9f);
			const FVector2D Ball = FMath::Lerp(From, At, EaseOut(FMath::Min(Phase / 0.55f, 1.0f)));
			if (Phase < 0.55f)
			{
				Icon.Line(From, Ball, WithAlpha(Mark, Mark.A * 0.4f), 3.0f);
				Icon.Disc(Ball, Unit * 0.11f, WithAlpha(Paper, Mark.A));
			}
			else
			{
				const float Hit = (Phase - 0.55f) / 0.45f;
				Icon.Ring(At, Unit * (0.25f + 0.8f * Hit), WithAlpha(Gold, Mark.A * (1.0f - Hit)), 4.0f);
			}
		}
		else if (Kind == TEXT("training"))
		{
			// A throw arcing over to a dummy on its stand.
			const FVector2D From = At + FVector2D(-Unit * 1.0f, Unit * 0.5f);
			const FVector2D To = At + FVector2D(Unit * 0.6f, Unit * 0.0f);
			for (int32 Dot = 0; Dot <= 10; ++Dot)
			{
				const float S = Dot / 10.0f;
				const FVector2D Point = FMath::Lerp(From, To, S) + FVector2D(0.0, -FMath::Sin(S * UE_PI) * Unit * 0.7f);
				Icon.Disc(Point, Unit * 0.03f, WithAlpha(Mark, Mark.A * 0.7f));
			}
			const float Phase = FMath::Frac(Time * 0.55f * Live);
			const FVector2D Ball = FMath::Lerp(From, To, Phase) + FVector2D(0.0, -FMath::Sin(Phase * UE_PI) * Unit * 0.7f);
			Icon.Disc(Ball, Unit * 0.12f, WithAlpha(Paper, Mark.A));
			RoundedBox(Icon, To.X + Unit * 0.12f, To.Y - Unit * 0.45f, Unit * 0.32f, Unit * 0.8f, Unit * 0.14f, Mark);
			Icon.Disc(To + FVector2D(Unit * 0.28f, -Unit * 0.6f), Unit * 0.17f, Mark);
			Icon.Box(To.X + Unit * 0.24f, To.Y + Unit * 0.35f, Unit * 0.08f, Unit * 0.4f, Mark);
			Icon.Box(To.X + Unit * 0.02f, To.Y + Unit * 0.72f, Unit * 0.52f, Unit * 0.08f, Mark);
		}
		else if (Kind == TEXT("colour"))
		{
			// Paint dots turning round a palette.
			Icon.Ring(At, Unit * 0.62f, Mark, 5.0f);
			const FLinearColor Dots[] = {Fire, Gold, FLinearColor(0.2f, 0.9f, 0.5f, 1.0f), Ice, Violet};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Dots); ++Index)
			{
				const float Angle = Time * 0.6f * Live + Index * UE_TWO_PI / UE_ARRAY_COUNT(Dots);
				const FVector2D Spot = At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Unit * 0.62f;
				Icon.Disc(Spot, Unit * 0.17f, Navy);
				Icon.Disc(Spot, Unit * 0.14f, WithAlpha(Dots[Index], Mark.A + 0.1f));
			}
			Icon.Disc(At, Unit * 0.2f, WithAlpha(Paper, Mark.A));
		}
		else if (Kind == TEXT("settings"))
		{
			// Three sliders, their knobs gliding.
			for (int32 Row = 0; Row < 3; ++Row)
			{
				const float Y = At.Y - Unit * 0.55f + Row * Unit * 0.55f;
				RoundedBox(Icon, At.X - Unit * 0.9f, Y - Unit * 0.05f, Unit * 1.8f, Unit * 0.1f, Unit * 0.05f, WithAlpha(Mark, Mark.A * 0.6f));
				const float Knob = 0.5f + 0.38f * FMath::Sin(Time * (0.8f + Row * 0.35f) * Live + Row * 1.7f);
				Icon.Disc(FVector2D(At.X - Unit * 0.9f + Unit * 1.8f * Knob, Y), Unit * 0.16f, Mark);
			}
		}
		else if (Kind == TEXT("local"))
		{
			// Four players side by side, each in its colour, bobbing in turn.
			const FLinearColor Seats[] = {Fire, Ice, Gold, FLinearColor(0.2f, 0.9f, 0.5f, 1.0f)};
			for (int32 Seat = 0; Seat < 4; ++Seat)
			{
				const float Bob = FMath::Max(0.0f, FMath::Sin(Time * 4.0f * Live - Seat * 0.9f)) * Unit * 0.14f * Live;
				Figure(Icon, At + FVector2D(-Unit * 0.96f + Seat * Unit * 0.64f, Unit * 0.66f - Bob), Unit * 0.6f,
					WithAlpha(FMath::Lerp(Seats[Seat], Paper, 0.2f), Mark.A + 0.05f));
			}
		}
		else if (Kind == TEXT("online"))
		{
			// The globe, and signals going out from it.
			Icon.Ring(At, Unit * 0.66f, Mark, 5.0f);
			for (int32 Meridian = 0; Meridian < 2; ++Meridian)
			{
				const float Squash = Meridian == 0 ? 0.35f : 0.7f;
				TArray<FVector2D> Points;
				for (int32 Step = 0; Step <= 24; ++Step)
				{
					const float Angle = UE_TWO_PI * Step / 24.0f;
					Points.Add(At + FVector2D(FMath::Cos(Angle) * Unit * 0.66f * Squash, FMath::Sin(Angle) * Unit * 0.66f));
				}
				HudStroke(Icon, Points, Mark, 3.0f);
			}
			Icon.Line(At + FVector2D(-Unit * 0.66f, 0.0), At + FVector2D(Unit * 0.66f, 0.0), Mark, 3.0f);
			for (int32 Wave = 0; Wave < 2; ++Wave)
			{
				const float Out = FMath::Frac(Time * 0.7f * Live + Wave * 0.5f);
				Icon.Arc(At, Unit * (0.85f + 0.45f * Out), -70.0f, -20.0f, WithAlpha(Mark, Mark.A * (1.0f - Out)), 4.0f);
			}
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
			const float Lit = 0.5f + 0.5f * B;
			Icon.Line(FVector2D(R, At.Y), FVector2D(R + Unit * 0.5f, At.Y), WithAlpha(Gold, Lit), 5.0f);
			Icon.Disc(FVector2D(R + Unit * 0.66f, At.Y), Unit * (0.16f + 0.03f * FMath::Sin(Time * 4.0f) * B), WithAlpha(Gold, Lit));
		}
	}
}

FString ChaosImpactModeMenu::Tagline(const FString& Kind)
{
	return Kind == TEXT("solo") ? TEXT("ひとりで挑むストーリー")
		: Kind == TEXT("vs") ? TEXT("みんなで対戦")
		: Kind == TEXT("dojo") ? TEXT("ルールと操作をレッスンで")
		: Kind == TEXT("training") ? TEXT("自由に練習")
		: Kind == TEXT("colour") ? TEXT("キャラの色をつくる")
		: Kind == TEXT("settings") ? TEXT("操作・画面・音")
		: Kind == TEXT("local") ? TEXT("この1台でいっしょに")
		: Kind == TEXT("online") ? TEXT("インターネットで対戦")
		: Kind == TEXT("tourney") ? TEXT("トーナメントをひらく") : TEXT("");
}

FString ChaosImpactModeMenu::Detail(const FString& Kind)
{
	return Kind == TEXT("solo") ? TEXT("デットボール合衆国のステージを、ひとつずつ攻略していく1人用モード")
		: Kind == TEXT("vs") ? TEXT("ローカル・通信・トーナメントから選んで対戦する")
		: Kind == TEXT("dojo") ? TEXT("基本から応用、ボール、対戦ルール、チャレンジまで。動画つきのレッスンで覚える")
		: Kind == TEXT("training") ? TEXT("CPUやマトを置いて、好きなだけ練習できる")
		: Kind == TEXT("colour") ? TEXT("キャラクターのカスタムカラーをつくって保存する")
		: Kind == TEXT("settings") ? TEXT("操作タイプやキー、画面、音量などを変える")
		: Kind == TEXT("back") ? TEXT("タイトル画面にもどる")
		: Kind == TEXT("local") ? TEXT("1台で最大4人まで。CPUを入れて対戦もできる")
		: Kind == TEXT("online") ? TEXT("ランダムマッチ・合言葉マッチ・トーナメント・戦績")
		: Kind == TEXT("tourney") ? TEXT("この1台でトーナメントをひらく。1対1の勝ちぬき戦")
		: Kind == TEXT("vsback") ? TEXT("モード選択にもどる") : TEXT("");
}

void ChaosImpactModeMenu::PaintTile(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FTile& Tile,
	const float Blend, const bool bPressed, const float Alpha, const float Time)
{
	const float W = static_cast<float>(Tile.Rect.Right - Tile.Rect.Left);
	const float H = static_cast<float>(Tile.Rect.Bottom - Tile.Rect.Top);
	const float B = EaseOut(Blend);
	const FLinearColor Accent = Tile.Accent;
	const bool bBack = Tile.Kind == TEXT("back") || Tile.Kind == TEXT("vsback");

	// A way back: a slim leaning bar with an arrow, filling white once chosen.
	if (bBack)
	{
		const FGeometry Bar = Lean(Design, static_cast<float>(Tile.Rect.Left) + 10.0f * B, static_cast<float>(Tile.Rect.Top), W, H,
			bPressed ? 0.96f : 1.0f);
		const FPainter P{Bar, Elements, Layer, Alpha};
		P.Box(8.0f, 9.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		HudPoly(P, TileBox(0.0f, 0.0f, W, H), MakeFill(FLinearColor(0.05f, 0.07f, 0.12f, 0.97f), FLinearColor(0.012f, 0.016f, 0.03f, 0.97f)));
		HudAbove(P, 1).Box(0.0f, 0.0f, W * B, H, WithAlpha(Paper, 0.95f));
		HudAbove(P, 2).Box(0.0f, 0.0f, 8.0f, H, B > 0.5f ? Ink : Muted);
		const FLinearColor Ink2 = FMath::Lerp(Paper, Ink, B);
		const FVector2D Arrow(34.0f, H * 0.5f);
		const FPainter Top = HudAbove(P, 3);
		Top.Line(Arrow + FVector2D(12.0, -12.0), Arrow, Ink2, 5.0f);
		Top.Line(Arrow, Arrow + FVector2D(12.0, 12.0), Ink2, 5.0f);
		const float Size = HudFit(Tile.Name, 24.0f, W - 80.0f);
		Top.Text(Tile.Name, 60.0f, (H - Size * 1.38f) * 0.5f, Size, Ink2, ETextAlign::Left, TEXT("Black"));
		return;
	}

	const float Lift = 12.0f * B;
	const FGeometry Card = Lean(Design, static_cast<float>(Tile.Rect.Left), static_cast<float>(Tile.Rect.Top) - Lift, W, H,
		(1.0f + 0.02f * B) * (bPressed ? 0.97f : 1.0f));
	const FPainter P{Card, Elements, Layer, Alpha * (Tile.bDisabled ? 0.5f : 1.0f)};
	const FPainter Up = HudAbove(P, 1);
	const FPainter Top = HudAbove(P, 4);
	const bool bBig = H >= 240.0f;

	// Its glow once chosen, its shadow, its body: the accent deep at the top fading to night.
	if (B > 0.01f)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 4.0f);
		RoundedBox(HudAbove(P, -1), -14.0f, -14.0f, W + 28.0f, H + 28.0f, 22.0f, WithAlpha(Accent, (0.18f + 0.12f * Pulse) * B));
	}
	P.Box(14.0f, 16.0f + Lift * 0.6f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	FLinearColor Deep = Accent * (0.2f + 0.24f * B);
	Deep.A = 0.97f;
	HudPoly(P, TileBox(0.0f, 0.0f, W, H), MakeFill(Deep, FLinearColor(0.008f, 0.01f, 0.025f, 0.97f)));
	// Big faint English name across the back, low and to the right (kept inside the tile).
	{
		const float Ghost = HudFit(Tile.Big, FMath::Min(H * 0.5f, 200.0f), W - 40.0f, TEXT("BlackItalic"));
		Up.Text(Tile.Big, W - 18.0f - (1.0f - B) * 10.0f, H - 14.0f - Ghost * 1.3f, Ghost, WithAlpha(Accent, 0.06f + 0.05f * B), ETextAlign::Right,
			TEXT("BlackItalic"));
	}
	// Speed streaks leaning with it.
	for (int32 Band = 0; Band < 3; ++Band)
	{
		Up.Box(W * 0.56f + Band * 52.0f + B * 14.0f, 0.0f, 20.0f - Band * 5.0f, H, WithAlpha(Accent, 0.05f + 0.1f * B));
	}
	// A light sweeping over the chosen one now and then (kept inside the tile).
	if (B > 0.01f)
	{
		const float Sweep = FMath::Frac(Time / 2.6f);
		const float X = -120.0f + (W + 240.0f) * EaseOut(FMath::Min(Sweep / 0.45f, 1.0f));
		if (Sweep < 0.45f)
		{
			TArray<FVector2D> Band = {FVector2D(X, 0.0), FVector2D(X + 60.0f, 0.0), FVector2D(X + 10.0f, H), FVector2D(X - 50.0f, H)};
			Band = HudClip(HudClip(Band, 0.0, true, true), W, false, true);
			if (Band.Num() >= 3)
			{
				HudPoly(HudAbove(P, 2), Band, SolidFill(WithAlpha(FLinearColor::White, 0.1f * B)), 1);
			}
		}
	}
	// The top's bright edge and the bottom's accent bar.
	HudPoly(Up, TileBox(0.0f, 0.0f, W, 6.0f), MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
	Up.Box(0.0f, H - 10.0f, W, 10.0f, Accent);
	Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.1f), 1.0f);

	// The emblem: in the middle of a tall tile, toward the right of a wide one, low on the right (and fainter) on a narrow one.
	const bool bTall = H >= 400.0f;
	const bool bNarrow = !bTall && W < 440.0f;
	const float Unit = bTall ? FMath::Min(W * 0.24f, 130.0f) : FMath::Min(H * (bBig ? 0.3f : 0.36f), bBig ? 104.0f : 78.0f);
	const FVector2D At = bTall ? FVector2D(W * 0.5f, H * 0.56f)
		: bNarrow ? FVector2D(W - Unit * 1.1f, H * 0.6f) : FVector2D(W - Unit * 1.35f, bBig ? H * 0.45f : H * 0.5f);
	const FLinearColor Mark = WithAlpha(FMath::Lerp(Accent, Paper, 0.35f), bNarrow ? 0.14f + 0.36f * B : 0.4f + 0.55f * B);
	Emblem(HudAbove(P, 3), Tile.Kind, At, Unit, Mark, B, Time);

	// Its name: big in English, then in Japanese, then its tagline at the foot.
	const float TextW = bNarrow || bTall ? W - 60.0f : W - Unit * 2.7f - 30.0f;
	const float BigSize = HudFit(Tile.Big, bBig ? 56.0f : 40.0f, TextW, TEXT("BlackItalic"));
	HudText3D(Top, Tile.Big, 32.0f, bBig ? 26.0f : 18.0f, BigSize, FMath::Lerp(WithAlpha(Paper, 0.85f), Paper, B), ETextAlign::Left,
		TEXT("BlackItalic"), 3.0f);
	const float NameSize = HudFit(Tile.Name, bBig ? 34.0f : 26.0f, TextW);
	Top.Text(Tile.Name, 34.0f, (bBig ? 34.0f : 24.0f) + BigSize * 1.25f, NameSize, Paper, ETextAlign::Left, TEXT("Black"));
	const FString Line = Tagline(Tile.Kind);
	if (!Line.IsEmpty() && H >= 150.0f)
	{
		const float LineSize = HudFit(Line, bBig ? 20.0f : 17.0f, W - 60.0f, TEXT("Bold"));
		Top.Text(Line, 35.0f, H - 22.0f - LineSize * 1.6f, LineSize, WithAlpha(Paper, 0.6f + 0.35f * B), ETextAlign::Left, TEXT("Bold"));
	}
	// Chosen: a white frame breathing round it.
	if (B > 0.01f)
	{
		HudAbove(P, 6).Outline(-6.0f, -6.0f, W + 12.0f, H + 12.0f, WithAlpha(Paper, B * (0.6f + 0.3f * FMath::Sin(Time * 5.0f))), 3.0f);
	}
}

void ChaosImpactModeMenu::PaintInfoBar(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const FTile& Tile,
	const FSlateRect& Area, const FString& ConfirmKey, const FString& BackKey, const float Alpha, const float Time)
{
	const float W = static_cast<float>(Area.Right - Area.Left);
	const float H = static_cast<float>(Area.Bottom - Area.Top);
	const FGeometry Bar = Lean(Design, static_cast<float>(Area.Left), static_cast<float>(Area.Top), W, H);
	const FPainter P{Bar, Elements, Layer, Alpha};
	P.Box(8.0f, 9.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	HudPoly(P, TileBox(0.0f, 0.0f, W, H), MakeFill(FLinearColor(0.035f, 0.05f, 0.1f, 0.96f), FLinearColor(0.01f, 0.014f, 0.03f, 0.96f)));
	const FPainter Up = HudAbove(P, 1);
	Up.Box(0.0f, 0.0f, 10.0f, H, Tile.Accent);
	Up.Box(10.0f, 0.0f, W - 10.0f, 3.0f, WithAlpha(Tile.Accent, 0.5f));
	// The keys on the right.
	const float KeySize = 15.0f;
	const FString Confirm = TEXT("決定");
	const FString Back = TEXT("戻る");
	const float ConfirmW = HudMeasure(ConfirmKey, KeySize) + KeySize * 1.1f;
	const float BackW = HudMeasure(BackKey, KeySize) + KeySize * 1.1f;
	const float WordW = HudMeasure(Confirm, 16.0f, TEXT("Bold"));
	const float KeysW = ConfirmW + 8.0f + WordW + 22.0f + BackW + 8.0f + WordW;
	float KX = W - 24.0f - KeysW;
	const float KY = (H - KeySize * 1.6f) * 0.5f;
	const FPainter Keys = HudAbove(P, 2);
	KX += KeyCap(Keys, KX, KY, ConfirmKey, KeySize) + 8.0f;
	Keys.Text(Confirm, KX, KY + 1.0f, 16.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
	KX += WordW + 22.0f;
	KX += KeyCap(Keys, KX, KY, BackKey, KeySize) + 8.0f;
	Keys.Text(Back, KX, KY + 1.0f, 16.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
	// The chosen tile: its name, and what it is.
	const float TextW = W - KeysW - 80.0f;
	const float NameSize = HudFit(Tile.Name, 22.0f, TextW * 0.4f);
	const FPainter Words = HudAbove(P, 2);
	Words.Text(Tile.Name, 32.0f, (H - NameSize * 1.38f) * 0.5f, NameSize, Paper, ETextAlign::Left, TEXT("Black"));
	const float NameW = HudMeasure(Tile.Name, NameSize);
	const FString Line = Detail(Tile.Kind);
	const float LineSize = HudFit(Line, 16.0f, TextW - NameW - 30.0f, TEXT("Bold"));
	Words.Text(Line, 32.0f + NameW + 22.0f, (H - LineSize * 1.38f) * 0.5f, LineSize, WithAlpha(Paper, 0.72f), ETextAlign::Left, TEXT("Bold"));
}
