#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactMatchTypes.h"

/** How a character's model is built and animated. */
enum class EChaosImpactModelKind : uint8
{
	/** SM_<Prefix>_Body plus four separately skinned limbs, coloured with T_<Prefix>_<Colour>. */
	Limbs,
	/** SM_<Prefix>_Body alone: bobs and leans with the animation, no limbs. */
	Rigid,
	/** SK_<Prefix>, one skinned mesh on a HumanIK skeleton (Character1_*) that follows the game's animation. */
	Skinned,
	/** A non-humanoid skeletal mesh with its own authored action clips. */
	Authored,
	/** A static hovering body: no locomotion deformation; carries balls on mesh sockets. */
	Floating,
	/** The authored model's lower limbs are hands; its upper limbs are feet used to carry and throw. */
	Handstand,
};

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
	 * Rigid and skinned models keep their own slot materials and are coloured through their BodyColor parameter
	 * (which the material applies to the red parts of the texture).
	 */
	EChaosImpactModelKind Kind = EChaosImpactModelKind::Limbs;
	/** False: preserve the supplied texture and skip the colour selection step. HUD/team identity still has a colour. */
	bool bColourVariants = true;
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
		{TEXT("シマエナガ"), TEXT("/Game/ChaosImpact/Simae"), TEXT("Simae"), EChaosImpactModelKind::Skinned},
		{TEXT("ドッジ妖精"), TEXT("/Game/ChaosImpact/Fairy"), TEXT("Fairy"), EChaosImpactModelKind::Skinned, false},
		{TEXT("ウッドウーマン"), TEXT("/Game/ChaosImpact/Wood"), TEXT("Wood"), EChaosImpactModelKind::Skinned, false},
		{TEXT("アントキッズ"), TEXT("/Game/ChaosImpact/Ant"), TEXT("Ant"), EChaosImpactModelKind::Skinned, false},
		{TEXT("マグロ"), TEXT("/Game/ChaosImpact/Fish"), TEXT("Fish"), EChaosImpactModelKind::Authored, false},
		{TEXT("ヒーロー"), TEXT("/Game/ChaosImpact/Hero"), TEXT("Hero"), EChaosImpactModelKind::Skinned, false},
		{TEXT("Robo"), TEXT("/Game/ChaosImpact/Robo"), TEXT("Robo"), EChaosImpactModelKind::Skinned, false},
		{TEXT("しんきんぐ"), TEXT("/Game/ChaosImpact/Emoji"), TEXT("Emoji"), EChaosImpactModelKind::Floating, false},
		{TEXT("サカサ・ニコラ"), TEXT("/Game/ChaosImpact/Reverse"), TEXT("Reverse"), EChaosImpactModelKind::Handstand, false},
	};
	inline constexpr int32 HeroIndex = 6;
	inline constexpr int32 RoboIndex = 7;
	inline constexpr int32 EmojiIndex = 8;
	inline constexpr int32 ReverseIndex = 9;

	/** Tiles on the select screen; the ones past the roster show as locked, so it reads as a roster that grows. */
	inline constexpr int32 TileCount = ((UE_ARRAY_COUNT(Characters) + 3) / 4) * 4;

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
