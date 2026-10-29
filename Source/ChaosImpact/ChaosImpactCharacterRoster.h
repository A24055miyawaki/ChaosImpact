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
	/** Short character bio shown under the character's name on the character select screen. */
	const TCHAR* DescriptionLine1 = TEXT("");
	const TCHAR* DescriptionLine2 = TEXT("");
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
		{TEXT("スマイリー"), TEXT("/Game/ChaosImpact/Character"), TEXT("Player"), EChaosImpactModelKind::Limbs, true,
			TEXT("「笑顔」から生まれた存在"), TEXT("笑顔以外を知らない。家族も。")},
		{TEXT("シマエナガ"), TEXT("/Game/ChaosImpact/Simae"), TEXT("Simae"), EChaosImpactModelKind::Skinned, true,
			TEXT("「シマエナガ」から生まれた存在。"), TEXT("実は小さなシマエナガの集合体。")},
		{TEXT("ドッジ妖精"), TEXT("/Game/ChaosImpact/Fairy"), TEXT("Fairy"), EChaosImpactModelKind::Skinned, false,
			TEXT("「ドッチボール」から生まれた存在。"), TEXT("シマエナガとは従妹。")},
		{TEXT("ウッドウーマン"), TEXT("/Game/ChaosImpact/Wood"), TEXT("Wood"), EChaosImpactModelKind::Skinned, false,
			TEXT("「木」から生まれた存在。"), TEXT("よくカブトムシがついている。")},
		{TEXT("アントキッズ"), TEXT("/Game/ChaosImpact/Ant"), TEXT("Ant"), EChaosImpactModelKind::Skinned, false,
			TEXT("実はアリではなくシロアリであり、"), TEXT("父の浮気相手との子供。")},
		{TEXT("マグロ"), TEXT("/Game/ChaosImpact/Fish"), TEXT("Fish"), EChaosImpactModelKind::Authored, false,
			TEXT("マグロ。"), TEXT("")},
		{TEXT("ヒーロー"), TEXT("/Game/ChaosImpact/Hero"), TEXT("Hero"), EChaosImpactModelKind::Skinned, false,
			TEXT("我らが主人公。身に着けているのは、"), TEXT("宇宙服風の普段着。結構着心地がいい。")},
		{TEXT("Robo"), TEXT("/Game/ChaosImpact/Robo"), TEXT("Robo"), EChaosImpactModelKind::Skinned, false,
			TEXT("ドッチ・ボール大統領に作られた殺戮兵器。"), TEXT("今現在は休暇中。")},
		{TEXT("しんきんぐ"), TEXT("/Game/ChaosImpact/Emoji"), TEXT("Emoji"), EChaosImpactModelKind::Floating, false,
			TEXT("大統領のスマホから生まれた。すべてに疑問を"), TEXT("浮かべており、大統領に疑念を抱き離反した。")},
		{TEXT("サカサ・ニコラ"), TEXT("/Game/ChaosImpact/Reverse"), TEXT("Reverse"), EChaosImpactModelKind::Handstand, false,
			TEXT("ドッチ・ボール大統領の熱狂的な追っかけ。"), TEXT("いつも頭に血が上っており、キレやすい。")},
		{TEXT("マグ・カープ"), TEXT("/Game/ChaosImpact/Cap"), TEXT("Cap"), EChaosImpactModelKind::Skinned, false,
			TEXT("西暦1923生産のアンティーク物。"), TEXT("広島のファン。")},
		{TEXT("究極電子生命体 セル"), TEXT("/Game/ChaosImpact/Cell"), TEXT("Cell"), EChaosImpactModelKind::Skinned, false,
			TEXT("「ボク、セル！」が口癖の電子体。"), TEXT("データを取り込むことで際限なく強くなれる。")},
		{TEXT("人々の子守歌"), TEXT("/Game/ChaosImpact/Cat"), TEXT("Cat"), EChaosImpactModelKind::Skinned, false,
			TEXT("黒目に見えるところは空洞。ハイライトはすべて目。"), TEXT("将来の夢は、この世すべての人間を救済すること。")},
		{TEXT("バードストライカー"), TEXT("/Game/ChaosImpact/Kawaii"), TEXT("Kawaii"), EChaosImpactModelKind::Authored, false,
			TEXT("かつて名を馳せたが、ドッチ・ボール大統領に"), TEXT("クチバシを縮められ引退を決意。油淋鶏が好き。")},
	};
	inline constexpr int32 HeroIndex = 6;
	inline constexpr int32 RoboIndex = 7;
	inline constexpr int32 EmojiIndex = 8;
	inline constexpr int32 ReverseIndex = 9;
	inline constexpr int32 CapIndex = 10;
	inline constexpr int32 CellIndex = 11;
	inline constexpr int32 CatIndex = 12;
	inline constexpr int32 BirdIndex = 13;

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
