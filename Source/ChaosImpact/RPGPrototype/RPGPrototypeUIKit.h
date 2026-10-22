#pragma once

#include "RPGPrototypeHUDStyle.h"
#include "RPGPrototype.h"
#include "RPGEquipment.h"

/**
 * RPG試作メニューの共通部品。色・パネル・ボタン・チップ・キー表示・文字の折り返し・アイテムアイコン。
 *
 * FCanvas は「描く」と「クリック領域を集める」を同じコードで行う。レイアウト関数を描画なし（Geometry=null）で
 * 呼ぶとクリック判定だけが得られるので、見た目と押せる場所が必ず一致する。
 *
 * ユニティビルド対策：このヘッダーの名前は RPGPrototypeUI に閉じ込め、.cpp では名前空間の別名
 * （namespace RPGUI = RPGPrototypeUI;）で呼ぶ。ファイル全体への using namespace はしない。
 */
namespace RPGPrototypeUI
{
using namespace ChaosImpactPaint;

// ---------------------------------------------------------------- 色

inline const FLinearColor TextMain(0.96f, 0.975f, 1.0f, 1.0f);
inline const FLinearColor TextSub(0.70f, 0.76f, 0.86f, 1.0f);
inline const FLinearColor TextDim(0.46f, 0.52f, 0.63f, 1.0f);
inline const FLinearColor Good(0.42f, 1.0f, 0.64f, 1.0f);
inline const FLinearColor Bad(1.0f, 0.44f, 0.40f, 1.0f);
inline const FLinearColor Highlight(1.0f, 0.74f, 0.16f, 1.0f);
inline const FLinearColor Cyan(0.12f, 0.8f, 1.0f, 1.0f);
inline const FLinearColor NewColor(1.0f, 0.36f, 0.30f, 1.0f);

inline FLinearColor RarityColor(const ERPGPrototypeRarity Rarity)
{
	switch (Rarity)
	{
	case ERPGPrototypeRarity::Rare: return FLinearColor(0.30f, 0.72f, 1.0f, 1.0f);
	case ERPGPrototypeRarity::Unique: return FLinearColor(0.76f, 0.50f, 1.0f, 1.0f);
	case ERPGPrototypeRarity::Legend: return FLinearColor(1.0f, 0.76f, 0.18f, 1.0f);
	default: return FLinearColor(0.82f, 0.86f, 0.92f, 1.0f);
	}
}
inline const TCHAR* RarityName(const ERPGPrototypeRarity Rarity)
{
	switch (Rarity)
	{
	case ERPGPrototypeRarity::Rare: return TEXT("レア");
	case ERPGPrototypeRarity::Unique: return TEXT("ユニーク");
	case ERPGPrototypeRarity::Legend: return TEXT("レジェンド");
	default: return TEXT("ノーマル");
	}
}
/** The equipment slot (0-9) an item of this kind goes into; specials report 1. */
inline int32 SlotForItem(const FRPGPrototypeItem& Item)
{
	if (Item.Kind == ERPGItemKind::Ball) { return Item.Type == EChaosImpactBallType::Normal ? 0 : 1; }
	if (RPGEquipment::IsGear(Item.Kind)) { return static_cast<int32>(Item.Kind) - 2; }
	return static_cast<int32>(Item.Kind) + 6;
}

// ---------------------------------------------------------------- キャンバス

struct FCanvas
{
	const FGeometry* Geometry = nullptr;
	FSlateWindowElementList* Elements = nullptr;
	int32 Layer = 0;
	float Time = 0.0f;
	/** The action under the pointer (from a layout pass), drawn lighter. */
	int32 Hover = 0;
	bool bGamepad = false;
	/** While a dialog is open only its own buttons are clickable. */
	bool bModalOpen = false;
	bool bInModal = false;
	TArray<TPair<FBox2D, int32>> Regions;

	bool IsPainting() const { return Geometry && Elements; }
	/** Level 0 = panels, 1 = cards and buttons on them, 2 = dialogs. Offsets 0-2 shapes, 3-5 icons, 6-7 text. */
	FPainter At(const int32 Level, const int32 Offset) const { return FPainter{*Geometry, *Elements, Layer + Level * 10 + Offset}; }
	void Hit(const float X, const float Y, const float W, const float H, const int32 Action)
	{
		if (Action != 0 && (!bModalOpen || bInModal)) { Regions.Emplace(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), Action); }
	}
	int32 ActionAt(const FVector2D& Point) const
	{
		for (int32 Index = Regions.Num() - 1; Index >= 0; --Index)
		{
			if (Regions[Index].Key.IsInside(Point)) { return Regions[Index].Value; }
		}
		return 0;
	}
	bool IsHovered(const int32 Action) const { return Action != 0 && Hover == Action; }
};

// ---------------------------------------------------------------- 文字

inline float Measure(const FString& Text, const float Size, const FName Face = TEXT("Bold")) { return HudMeasure(Text, Size, Face); }

inline void Label(const FCanvas& C, const int32 Level, const FString& Text, const float X, const float Y, const float Size,
	const FLinearColor& Color, const ETextAlign Align = ETextAlign::Left, const FName Face = TEXT("Bold"))
{
	if (C.IsPainting()) { C.At(Level, 6).Text(Text, X, Y, Size, Color, Align, Face); }
}

/** Text shortened with "…" to fit Width. */
inline FString Fit(const FString& Text, const float Width, const float Size, const FName Face = TEXT("Bold"))
{
	if (Measure(Text, Size, Face) <= Width) { return Text; }
	FString Out = Text;
	while (Out.Len() > 1 && Measure(Out + TEXT("…"), Size, Face) > Width) { Out.LeftChopInline(1); }
	return Out + TEXT("…");
}

/** Lines of Text broken at a measured width (Japanese has no spaces, so per character; "\n" also breaks). */
inline TArray<FString> WrapLines(const FString& Text, const float Width, const float Size, const FName Face = TEXT("Bold"))
{
	TArray<FString> Lines;
	FString Line;
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (Char == TEXT('\n')) { Lines.Add(Line); Line.Empty(); continue; }
		const FString Next = Line + Char;
		if (!Line.IsEmpty() && Measure(Next, Size, Face) > Width)
		{
			// Keep closing punctuation on the line it closes.
			if (Char == TEXT('。') || Char == TEXT('、') || Char == TEXT('）') || Char == TEXT('」')) { Line = Next; Lines.Add(Line); Line.Empty(); continue; }
			Lines.Add(Line); Line.Empty(); Line.AppendChar(Char);
		}
		else { Line = Next; }
	}
	if (!Line.IsEmpty()) { Lines.Add(Line); }
	return Lines;
}

/** Wrapped text; returns the lines drawn. The last line gets "…" when the text does not fit in MaxLines. */
inline int32 Wrap(const FCanvas& C, const int32 Level, const FString& Text, const float X, const float Y, const float Width, const float Size,
	const FLinearColor& Color, const int32 MaxLines = 3, const float LineHeight = 0.0f)
{
	TArray<FString> Lines = WrapLines(Text, Width, Size);
	const float Step = LineHeight > 0.0f ? LineHeight : Size + 7.0f;
	const int32 Count = FMath::Min(Lines.Num(), MaxLines);
	for (int32 Row = 0; Row < Count; ++Row)
	{
		FString Line = Lines[Row];
		if (Row == Count - 1 && Lines.Num() > Count) { Line = Fit(Line + TEXT("…………"), Width, Size); }
		Label(C, Level, Line, X, Y + Row * Step, Size, Color);
	}
	return Count;
}

// ---------------------------------------------------------------- パネル・ボタン

enum class EStyle : uint8 { Panel, Card, Hover, Selected, Primary, PrimaryHover, Disabled, Danger };

inline FHudFill StyleFill(const EStyle Style)
{
	switch (Style)
	{
	case EStyle::Card: return MakeFill(FLinearColor(0.125f, 0.16f, 0.255f, 1.0f), FLinearColor(0.055f, 0.075f, 0.14f, 1.0f));
	case EStyle::Hover: return MakeFill(FLinearColor(0.18f, 0.235f, 0.37f, 1.0f), FLinearColor(0.08f, 0.11f, 0.2f, 1.0f));
	case EStyle::Selected: return MakeFill(FLinearColor(0.08f, 0.36f, 0.6f, 1.0f), FLinearColor(0.025f, 0.12f, 0.3f, 1.0f));
	case EStyle::Primary: return MakeFill(FLinearColor(1.0f, 0.8f, 0.3f, 1.0f), FLinearColor(0.95f, 0.43f, 0.06f, 1.0f));
	case EStyle::PrimaryHover: return MakeFill(FLinearColor(1.0f, 0.9f, 0.5f, 1.0f), FLinearColor(1.0f, 0.55f, 0.12f, 1.0f));
	case EStyle::Disabled: return MakeFill(FLinearColor(0.075f, 0.085f, 0.115f, 1.0f), FLinearColor(0.035f, 0.042f, 0.065f, 1.0f));
	case EStyle::Danger: return MakeFill(FLinearColor(0.5f, 0.09f, 0.13f, 1.0f), FLinearColor(0.24f, 0.03f, 0.07f, 1.0f));
	default: return MakeFill(FLinearColor(0.07f, 0.095f, 0.17f, 0.95f), FLinearColor(0.022f, 0.032f, 0.07f, 0.95f));
	}
}

/**
 * A slanted card in the battle HUD's style (navy outline, dark extrusion). Accent draws the bright top edge,
 * Stripe (alpha > 0) a coloured band along the slanted left edge, like the pause menu rows.
 */
inline void Panel(const FCanvas& C, const int32 Level, const float X, const float Y, const float W, const float H, const EStyle Style,
	const FLinearColor& Accent = FLinearColor::Transparent, const FLinearColor& Stripe = FLinearColor::Transparent, float Cut = -1.0f)
{
	if (!C.IsPainting() || W <= 0.0f || H <= 0.0f) { return; }
	Cut = Cut < 0.0f ? FMath::Min(10.0f, H * 0.22f) : Cut;
	const TArray<FVector2D> Shape{FVector2D(X + Cut, Y), FVector2D(X + W, Y), FVector2D(X + W - Cut, Y + H), FVector2D(X, Y + H)};
	const bool bBig = Level == 0;
	HudShard(C.At(Level, 0), Shape, StyleFill(Style), bBig ? 2.5f : 2.0f, bBig ? FVector2D(4.0, 5.0) : FVector2D(3.0, 4.0));
	const FPainter Deco = C.At(Level, 3);
	if (Stripe.A > 0.01f)
	{
		HudPoly(Deco, {FVector2D(X + Cut, Y), FVector2D(X + Cut + 5.0f, Y), FVector2D(X + 5.0f, Y + H), FVector2D(X, Y + H)}, SolidFill(Stripe), 1);
	}
	if (Accent.A > 0.01f)
	{
		Deco.Line(FVector2D(X + Cut + 1.0f, Y + 1.5f), FVector2D(X + W - 2.0f, Y + 1.5f), Accent, Style == EStyle::Selected ? 3.0f : 2.0f);
	}
	if (Style == EStyle::Selected)
	{
		// A soft inner glow along the bottom so the selection reads even in peripheral vision.
		Deco.Box(X + 6.0f, Y + H - 4.0f, W - Cut - 8.0f, 2.0f, WithAlpha(Cyan, 0.55f));
	}
}

/** A small keyboard key / pad button cap. Returns its width. */
inline float Keycap(const FCanvas& C, const int32 Level, const FString& Key, const float X, const float Y, const float Height = 22.0f)
{
	if (Key.IsEmpty()) { return 0.0f; }
	const float Size = Height * 0.58f;
	const float W = FMath::Max(Height, Measure(Key, Size) + Height * 0.6f);
	if (C.IsPainting())
	{
		RoundedBox(C.At(Level, 3), X, Y + 2.0f, W, Height, 5.0f, WithAlpha(Extrude, 0.9f));
		RoundedBox(C.At(Level, 4), X, Y, W, Height, 5.0f, FLinearColor(0.86f, 0.9f, 0.97f, 1.0f));
		C.At(Level, 6).Text(Key, X + W * 0.5f, Y + (Height - Size) * 0.5f - Size * 0.18f, Size, Navy, ETextAlign::Center, TEXT("Bold"));
	}
	return W;
}

/** A rounded label. Solid: filled with Color and dark text; otherwise a tinted pill. Returns its width. */
inline float Chip(const FCanvas& C, const int32 Level, const FString& Text, float X, const float Y, const FLinearColor& Color,
	const float Size = 12.0f, const bool bSolid = false, const ETextAlign Align = ETextAlign::Left)
{
	if (Text.IsEmpty()) { return 0.0f; }
	const float W = Measure(Text, Size) + Size * 1.1f, H = Size + 9.0f;
	if (Align == ETextAlign::Right) { X -= W; }
	else if (Align == ETextAlign::Center) { X -= W * 0.5f; }
	if (C.IsPainting())
	{
		RoundedBox(C.At(Level, 4), X, Y, W, H, H * 0.5f, bSolid ? Color : WithAlpha(Color, 0.18f));
		if (!bSolid) { RoundedBox(C.At(Level, 3), X - 1.0f, Y - 1.0f, W + 2.0f, H + 2.0f, H * 0.5f + 1.0f, WithAlpha(Color, 0.45f)); }
		C.At(Level, 6).Text(Text, X + W * 0.5f, Y + 3.0f, Size, bSolid ? Navy : Color, ETextAlign::Center, TEXT("Bold"));
	}
	return W;
}

/** A clickable button. Key (optional) is shown as a keycap at its right end. */
inline void Button(FCanvas& C, const int32 Level, const float X, const float Y, const float W, const float H, const FString& Text,
	const int32 Action, const bool bPrimary = false, const bool bEnabled = true, const FString& Key = FString(), const bool bSelected = false)
{
	if (bEnabled) { C.Hit(X, Y, W, H, Action); }
	if (!C.IsPainting()) { return; }
	const bool bHover = bEnabled && C.IsHovered(Action);
	const EStyle Style = !bEnabled ? EStyle::Disabled : bPrimary ? (bHover ? EStyle::PrimaryHover : EStyle::Primary)
		: bSelected ? EStyle::Selected : bHover ? EStyle::Hover : EStyle::Card;
	Panel(C, Level, X, Y, W, H, Style, bPrimary || !bEnabled ? FLinearColor::Transparent : bSelected ? Highlight : WithAlpha(Cyan, 0.6f));
	const float Size = H >= 44.0f ? 19.0f : H >= 34.0f ? 16.0f : 14.0f;
	const float KeyW = Key.IsEmpty() || !bEnabled ? 0.0f : FMath::Max(H * 0.62f, Measure(Key, H * 0.62f * 0.58f) + H * 0.62f * 0.6f) + 10.0f;
	const FLinearColor TextColor = !bEnabled ? TextDim : bPrimary ? Navy : TextMain;
	Label(C, Level, Fit(Text, W - KeyW - (KeyW > 0.0f ? 24.0f : 8.0f), Size), X + (W - KeyW) * 0.5f + 2.0f, Y + (H - Size) * 0.5f - Size * 0.2f, Size, TextColor, ETextAlign::Center);
	if (KeyW > 0.0f) { Keycap(C, Level, Key, X + W - KeyW - 4.0f, Y + H * 0.19f, H * 0.62f); }
}

/** A section title with a thin rule to its right. */
inline void Section(const FCanvas& C, const int32 Level, const FString& Text, const float X, const float Y, const float W, const FLinearColor& Color = TextSub)
{
	if (!C.IsPainting()) { return; }
	Label(C, Level, Text, X, Y, 15.0f, Color);
	const float TextW = Measure(Text, 15.0f) + 10.0f;
	C.At(Level, 3).Box(X + TextW, Y + 11.0f, FMath::Max(0.0f, W - TextW), 1.5f, WithAlpha(Color, 0.35f));
}

/** "▲8" / "▼3" / "±0" in green, red or grey. Returns its width. */
inline float Delta(const FCanvas& C, const int32 Level, const float Value, const float X, const float Y, const float Size,
	const ETextAlign Align = ETextAlign::Left, const FString& Suffix = FString())
{
	const int32 Rounded = FMath::RoundToInt(Value);
	const FString Text = Rounded > 0 ? FString::Printf(TEXT("▲%d%s"), Rounded, *Suffix) : Rounded < 0 ? FString::Printf(TEXT("▼%d%s"), -Rounded, *Suffix) : TEXT("±0");
	Label(C, Level, Text, X, Y, Size, Rounded > 0 ? Good : Rounded < 0 ? Bad : TextDim, Align);
	return Measure(Text, Size);
}

/** A thin scroll bar: First..First+Visible of Total. */
inline void ScrollBar(const FCanvas& C, const int32 Level, const float X, const float Y, const float H, const int32 First, const int32 Visible, const int32 Total)
{
	if (!C.IsPainting() || Total <= Visible || Total <= 0) { return; }
	RoundedBox(C.At(Level, 3), X, Y, 5.0f, H, 2.5f, WithAlpha(TextDim, 0.25f));
	const float ThumbH = FMath::Max(18.0f, H * Visible / Total);
	const float ThumbY = Y + (H - ThumbH) * First / FMath::Max(1, Total - Visible);
	RoundedBox(C.At(Level, 4), X, ThumbY, 5.0f, ThumbH, 2.5f, WithAlpha(Cyan, 0.85f));
}

/** The menu's backdrop: a dark veil with the title logo's ice (left) and fire (right) bands, kept faint behind text. */
inline void Backdrop(const FCanvas& C, const float W, const float H)
{
	if (!C.IsPainting()) { return; }
	const FPainter Back = C.At(0, -3);
	Back.Box(0.0f, 0.0f, W, H, FLinearColor(0.012f, 0.018f, 0.04f, 0.95f));
	const FPainter Bands = C.At(0, -2);
	HudPoly(Bands, {FVector2D(0, 0), FVector2D(250, 0), FVector2D(70, H), FVector2D(0, H)},
		MakeFill(FLinearColor(0.02f, 0.18f, 0.36f, 0.55f), FLinearColor(0.01f, 0.07f, 0.16f, 0.3f)), 4);
	HudPoly(Bands, {FVector2D(W - 70, 0), FVector2D(W, 0), FVector2D(W, H), FVector2D(W - 250, H)},
		MakeFill(FLinearColor(0.3f, 0.02f, 0.08f, 0.3f), FLinearColor(0.38f, 0.03f, 0.1f, 0.55f)), 4);
	Bands.Line(FVector2D(250, 0), FVector2D(70, H), WithAlpha(Cyan, 0.45f), 2.0f);
	Bands.Line(FVector2D(W - 70, 0), FVector2D(W - 250, H), WithAlpha(FLinearColor(1.0f, 0.25f, 0.35f, 1.0f), 0.45f), 2.0f);
	for (int32 Row = 1; Row < 12; ++Row) { Bands.Box(0.0f, H * Row / 12.0f, W, 1.0f, FLinearColor(1, 1, 1, 0.018f)); }
}

/** Dims everything below a dialog. */
inline void DialogVeil(const FCanvas& C, const float X, const float Y, const float W, const float H)
{
	if (C.IsPainting()) { C.At(2, -2).Box(X, Y, W, H, FLinearColor(0.0f, 0.004f, 0.012f, 0.82f)); }
}

/** A ruby gem and amount. Returns its width. */
inline float Rubies(const FCanvas& C, const int32 Level, const int32 Amount, float X, const float Y, const float Size, const ETextAlign Align = ETextAlign::Left)
{
	const FString Text = FString::FormatAsNumber(Amount);
	const float W = Size * 0.9f + 6.0f + Measure(Text, Size);
	if (Align == ETextAlign::Right) { X -= W; }
	if (C.IsPainting())
	{
		const FVector2D Gem(X + Size * 0.45f, Y + Size * 0.62f);
		const float R = Size * 0.45f;
		HudPoly(C.At(Level, 4), {Gem + FVector2D(0, -R), Gem + FVector2D(R, 0), Gem + FVector2D(0, R), Gem + FVector2D(-R, 0)}, FireFill, 3);
		C.At(Level, 5).Line(Gem + FVector2D(-R * 0.5f, -R * 0.1f), Gem + FVector2D(0, -R * 0.6f), WithAlpha(TextMain, 0.8f), 1.5f);
		Label(C, Level, Text, X + Size * 0.9f + 6.0f, Y, Size, Highlight);
	}
	return W;
}

// ---------------------------------------------------------------- アイコン

/** The glyph of an equipment slot (0-9): ball outline, glove, shoe, guard, pouch, heart, bolt, target. */
inline void SlotGlyph(const FCanvas& C, const int32 Level, const FVector2D& At, const float R, const int32 Slot, const bool bDim)
{
	if (!C.IsPainting()) { return; }
	FPainter F = C.At(Level, 4);
	F.Alpha = bDim ? 0.32f : 1.0f;
	const auto Shape = [&](TArray<FVector2D> Points, const FHudFill& Fill)
	{
		for (FVector2D& Point : Points) { Point = At + Point * R; }
		HudShard(F, Points, Fill, 1.5f, FVector2D(1.0, 2.0));
	};
	FPainter Above = F;
	Above.Layer += 3;
	switch (Slot)
	{
	case 3: Shape({{-.65f, .5f}, {-.75f, -.15f}, {-.5f, -.22f}, {-.44f, -.7f}, {-.2f, -.75f}, {-.1f, -.22f}, {.02f, -.85f}, {.26f, -.83f}, {.3f, -.18f}, {.5f, -.65f}, {.7f, -.55f}, {.6f, .35f}, {.3f, .72f}, {-.3f, .75f}}, SilverFill); break;
	case 4: Shape({{-.5f, -.7f}, {.1f, -.7f}, {.2f, -.1f}, {.8f, .25f}, {.85f, .58f}, {-.75f, .58f}, {-.8f, .25f}, {-.5f, .1f}}, IceFill); break;
	case 5:
		Shape({{0, -.85f}, {.75f, -.5f}, {.6f, .35f}, {0, .85f}, {-.6f, .35f}, {-.75f, -.5f}}, SilverFill);
		Above.Line(At + FVector2D(-R * .35f, 0), At + FVector2D(R * .35f, 0), Navy, 3.0f);
		Above.Line(At + FVector2D(0, -R * .35f), At + FVector2D(0, R * .35f), Navy, 3.0f);
		break;
	case 6:
		Shape({{-.65f, -.45f}, {.65f, -.45f}, {.78f, .7f}, {-.78f, .7f}}, NightFill);
		Above.Line(At + FVector2D(-R * .4f, -R * .45f), At + FVector2D(-R * .2f, -R * .8f), SilverFill.Top, 3.0f);
		Above.Line(At + FVector2D(-R * .2f, -R * .8f), At + FVector2D(R * .35f, -R * .65f), SilverFill.Top, 3.0f);
		Above.Box(At.X - R * .18f, At.Y - R * .1f, R * .36f, R * .3f, Highlight);
		break;
	case 7: Shape({{0, -.35f}, {.4f, -.7f}, {.8f, -.3f}, {.65f, .25f}, {0, .8f}, {-.65f, .25f}, {-.8f, -.3f}, {-.4f, -.7f}}, FireFill); break;
	case 8: Shape({{.18f, -.9f}, {-.7f, .12f}, {-.12f, .12f}, {-.25f, .9f}, {.75f, -.18f}, {.12f, -.18f}}, IceFill); break;
	case 9:
		F.Ring(At, R * .65f, Highlight, 3.0f); F.Disc(At, R * .18f, TextMain);
		F.Line(At - FVector2D(R, 0), At + FVector2D(R, 0), Cyan, 2.0f); F.Line(At - FVector2D(0, R), At + FVector2D(0, R), Cyan, 2.0f);
		break;
	default:
		RPGPrototypePaint::Ball(F, At, R, EChaosImpactBallType::Normal, C.Time, false);
		break;
	}
}

/** An item's icon on a round plate ringed in its rarity colour. */
inline void ItemIcon(const FCanvas& C, const int32 Level, const FVector2D& At, const float R, const FRPGPrototypeItem& Item, const bool bPlate = true)
{
	if (!C.IsPainting()) { return; }
	if (bPlate)
	{
		const FPainter Plate = C.At(Level, 3);
		Plate.Disc(At + FVector2D(2, 3), R * 1.32f, WithAlpha(Extrude, 0.9f));
		Plate.Disc(At, R * 1.32f, Navy);
		Plate.Disc(At, R * 1.22f, FLinearColor(0.03f, 0.05f, 0.1f, 1.0f));
		if (Item.IsValid()) { Plate.Ring(At, R * 1.22f, WithAlpha(RarityColor(Item.Rarity), 0.9f), 2.0f); }
	}
	if (!Item.IsValid()) { return; }
	if (Item.Kind == ERPGItemKind::Ball) { RPGPrototypePaint::Ball(C.At(Level, 4), At, R * 0.86f, Item.Type, C.Time); }
	else { SlotGlyph(C, Level, At, R * 0.78f, SlotForItem(Item), false); }
}

// ---------------------------------------------------------------- アイテムの説明

/** A catalog description with every number replaced by "？" (tune offers before they are bought). */
inline FString HiddenDescription(const FRPGModDefinition& Definition)
{
	FString Text = Definition.Description;
	for (const TCHAR* Format : {TEXT("%.0f"), TEXT("%.1f"), TEXT("%.2f")}) { Text.ReplaceInline(Format, TEXT("？")); }
	Text.ReplaceInline(TEXT("%%"), TEXT("%"));
	FString Result;
	bool bInNumber = false;
	for (const TCHAR Char : Text)
	{
		const bool bNumber = (Char >= TEXT('0') && Char <= TEXT('9')) || (Char == TEXT('.') && bInNumber);
		if (bNumber) { if (!bInNumber) { Result += TEXT("？"); } bInNumber = true; }
		else { Result.AppendChar(Char); bInNumber = false; }
	}
	return Result;
}

/** A gear or badge effect at its actual (grown) value. */
inline FString ActualDescription(const FRPGPrototypeItem& Item, FRPGAffix Affix)
{
	if (Item.Kind != ERPGItemKind::Ball) { Affix.Roll = Item.Mod(Affix.Key); Affix.Rank = 1; }
	return RPGEquipment::Describe(Affix);
}

struct FDetailSection
{
	FString Title;
	FString Body;
	FLinearColor Color = TextSub;
	bool bEmpty = false;
};

/** Everything an item does, in reading order: base effect, intrinsic traits, then the three tune sockets. */
inline TArray<FDetailSection> DetailSections(const FRPGPrototypeItem& Item)
{
	TArray<FDetailSection> Out;
	if (!Item.IsValid()) { return Out; }
	Out.Add({TEXT("基本効果"), Item.Kind == ERPGItemKind::Ball ? RPGEquipment::BaseEffectDescription(Item.Type) : ActualDescription(Item, Item.Badge), Cyan});
	for (const FRPGAffix& Affix : Item.Intrinsics)
	{
		if (const FRPGModDefinition* Definition = RPGEquipment::Find(Affix.Key))
		{ Out.Add({TEXT("固有  ") + Definition->Name, ActualDescription(Item, Affix), RarityColor(Item.Rarity)}); }
	}
	if (Item.Kind == ERPGItemKind::Ball)
	{
		static const TCHAR* Sockets[] = {TEXT("専用1"), TEXT("専用2"), TEXT("共通")};
		for (int32 Socket = 0; Socket < 3; ++Socket)
		{
			const FRPGAffix Affix = Item.Tunes.IsValidIndex(Socket) ? Item.Tunes[Socket] : FRPGAffix{};
			const FRPGModDefinition* Definition = RPGEquipment::Find(Affix.Key);
			if (Definition)
			{
				Out.Add({FString::Printf(TEXT("チューン %s  %s  ランク%d"), Sockets[Socket], *Definition->Name, Affix.Rank),
					RPGEquipment::Describe(Affix), Highlight});
			}
			else { Out.Add({FString::Printf(TEXT("チューン %s  未装着"), Sockets[Socket]), TEXT("ルビー商人で付けられます"), TextDim, true}); }
		}
	}
	return Out;
}

/** One-line summary of an item for compact places (cards, comparisons). */
inline FString ShortEffect(const FRPGPrototypeItem& Item)
{
	if (!Item.IsValid()) { return TEXT("未装備"); }
	return Item.Kind == ERPGItemKind::Ball ? RPGEquipment::BaseEffectDescription(Item.Type) : ActualDescription(Item, Item.Badge);
}
}
