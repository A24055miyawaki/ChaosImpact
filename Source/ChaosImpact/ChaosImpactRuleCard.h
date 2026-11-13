#pragma once

#include "CoreMinimal.h"

struct FChaosImpactMatchRules;
namespace ChaosImpactPaint { struct FPainter; }

/**
 * A kind of match described for players: its emblem, its name in English, its colour and how it goes (the rules screen's
 * card, and the full view of the decided rules a room's members can open while they wait: ルール確認).
 */
namespace ChaosImpactRuleCard
{
	/** The emblem for a kind of match (everyone for themselves, teams, 1 v 1, battle royale, tournament, the party rules). */
	void PaintEmblem(const ChaosImpactPaint::FPainter& Icon, const FChaosImpactMatchRules& Rules, const FVector2D& At, float Unit,
		const FLinearColor& Mark, float Time);
	/** How it goes, in three short lines. Total: everyone playing (players and CPUs). */
	TArray<FString> GetPoints(const FChaosImpactMatchRules& Rules, int32 Total);
	FString GetEnglish(const FChaosImpactMatchRules& Rules);
	FLinearColor GetColor(const FChaosImpactMatchRules& Rules);
}
