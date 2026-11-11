#pragma once

#include "CoreMinimal.h"

class FSlateWindowElementList;
struct FGeometry;

/**
 * The mode select (ソロ / VS / 訓練場 / トレーニング / カラーエディット / 設定 / タイトルへ) and the VS mode screen (ローカル /
 * 通信 / トーナメント / 戻る), drawn as the online menu's leaning tiles: a big English name, the Japanese one, a moving
 * emblem, a tagline; the chosen one lifted, glowing, a light sweeping over it; along the bottom what it is and the keys.
 */
namespace ChaosImpactModeMenu
{
	struct FTile
	{
		FSlateRect Rect;
		FString Name;
		/** solo, vs, dojo, training, colour, settings, back (to the title), local, online, tourney, vsback. */
		FString Kind;
		FString Big;
		FLinearColor Accent = FLinearColor::White;
		bool bDisabled = false;
	};

	/** One tile (Blend 0-1: how chosen it is; Alpha: its way in). */
	void PaintTile(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, const FTile& Tile, float Blend, bool bPressed,
		float Alpha, float Time);

	/** The bar along the bottom: the chosen tile, what it does, and the keys (confirm / back). */
	void PaintInfoBar(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, const FTile& Tile, const FSlateRect& Rect,
		const FString& ConfirmKey, const FString& BackKey, float Alpha, float Time);

	/** A short line on the tile, and the longer one in the bar. */
	FString Tagline(const FString& Kind);
	FString Detail(const FString& Kind);
}
