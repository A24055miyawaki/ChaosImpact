#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactMatchTypes.h"

/** One playable character: what the menus call it and where its model lives. */
struct FChaosImpactCharacterInfo
{
	/** Shown on the select screen. */
	const TCHAR* Name;
	/** Content folder holding the model. */
	const TCHAR* AssetFolder;
	/**
	 * Asset name part: "Player" means SM_Player_Body, SK_Player_ArmL/ArmR/LegL/LegR, M_CI_Player and
	 * T_Player_Red/Blue/Yellow/Green. A new character with the same kind of model only needs a new line below.
	 */
	const TCHAR* AssetPrefix;
	/**
	 * False for a model that is a single rigid mesh (SM_<Prefix>_Body with M_CI_<Prefix>, coloured through its
	 * BodyColor parameter): it bobs and leans with the animation but has no limbs to pose.
	 */
	bool bPosedLimbs = true;
};

namespace ChaosImpactRoster
{
	/** Every character comes in these colours, in team order (レッド, ブルー, イエロー, グリーン). */
	inline constexpr int32 ColourCount = 4;
	inline const TCHAR* const ColourNames[ColourCount] = {TEXT("レッド"), TEXT("ブルー"), TEXT("イエロー"), TEXT("グリーン")};
	inline const TCHAR* const ColourTextureSuffixes[ColourCount] = {TEXT("Red"), TEXT("Blue"), TEXT("Yellow"), TEXT("Green")};

	/** Characters that can be chosen, in menu order. Add new characters here. */
	inline const FChaosImpactCharacterInfo Characters[] =
	{
		{TEXT("スマイリー"), TEXT("/Game/ChaosImpact/Character"), TEXT("Player")},
		// Temporary model (muscle_2.fbx) for checking how a heavy mesh performs; no name yet.
		{TEXT(""), TEXT("/Game/ChaosImpact/Muscle"), TEXT("Muscle"), false},
	};

	/** Tiles on the select screen; the ones past the roster show as locked, so it reads as a roster that grows. */
	inline constexpr int32 TileCount = 4;

	inline int32 Num()
	{
		return UE_ARRAY_COUNT(Characters);
	}

	inline int32 ClampIndex(const int32 Index)
	{
		return FMath::Clamp(Index, 0, Num() - 1);
	}

	inline const FChaosImpactCharacterInfo& Get(const int32 Index)
	{
		return Characters[ClampIndex(Index)];
	}

	inline FLinearColor GetColourSwatch(const int32 Colour)
	{
		return ChaosImpactMatch::GetTeamColor(FMath::Clamp(Colour, 0, ColourCount - 1));
	}
}
