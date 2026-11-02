#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactBallTypes.h"

/**
 * RPG試作の調整値をまとめた場所。数値を変えたいときは、まずここを見る。
 *
 * - 能力（チューン・固有特性・バッジ・ギア）の名前・説明・抽選範囲は RPGEquipment.cpp のカタログ。
 * - ここは「それ以外の全体の数値」：成長曲線、ボールの基準値、強化・チューン・分解の費用、ドロップ率、安全上限。
 * - 割合は特に書かない限り「%の数値」（20 = 20%）ではなく倍率（0.2）で持つ。名前の末尾が Percent のものだけ%値。
 * - 手順と各値の意味は Docs/RPGPrototype/RPGシステム調整ガイド.md。
 *
 * 取り外し可能な試作専用。本編VS・ソロ・トレーニングの数値には影響しない。
 */
namespace RPGTuning
{
    /** Down (two or more players): holding the interact button this long gives up waiting and respawns at once. */
    inline constexpr float GiveUpHoldSeconds = 1.0f;
    /**
     * Tune shards (チューンの欠片): putting a tune on a ball takes one, raising its rank takes as many as its rank now.
     * One for every level gained; bosses, gold (and sometimes silver) chests, harder quests and now and then an enemy give more.
     */
    inline constexpr int32 ShardsPerTune = 1;
    inline constexpr int32 ShardsPerBoss = 2;
    inline constexpr float ShardDropChance = 0.03f;
    inline constexpr float SilverChestShardChance = 0.35f;
    inline constexpr int32 ShardCap = 9999;
    /**
     * The dojo (2026-11): tackle and dash levels 2-4. Each step costs rubies and tune shards and needs a hero level;
     * level 3 also needs a boss beaten (its bit in the record of bosses beaten; -1 none).
     */
    struct FSkillStep { int32 Rubies; int32 Shards; int32 MinLevel; int32 Boss; const TCHAR* Name; const TCHAR* Effect; };
    inline const FSkillStep TackleSteps[3] = {
        {300, 0, 3, -1, TEXT("強打タックル"), TEXT("タックルが敵にダメージを与える")},
        {900, 2, 6, 0, TEXT("岩砕きタックル"), TEXT("ひびの入った大岩を砕き、当てた敵を少し気絶させる")},
        {2400, 4, 14, -1, TEXT("疾風タックル"), TEXT("タックルを続けて出せるようになり、ダメージも上がる")}};
    inline const FSkillStep DashSteps[3] = {
        {300, 0, 3, -1, TEXT("ロングダッシュ"), TEXT("ダッシュの距離が大きく伸び、ゲージの消費も減る")},
        {1000, 2, 8, 1, TEXT("跳躍ダッシュ"), TEXT("跳躍台からダッシュすると谷を跳び越えられる")},
        {2600, 4, 16, -1, TEXT("残像ダッシュ"), TEXT("ダッシュの後に残像を残し、敵の狙いをそらす")}};
    /** A tackle's hit at level 2 and up: this plus this per hero level (more at level 4). */
    inline constexpr float TackleDamageBase = 14.0f;
    inline constexpr float TackleDamagePerLevel = 3.0f;
    /** A boss's health for each player more near it when the fight starts (two players: x1.6, three: x2.2 ...). */
    inline constexpr float BossHPPerExtraPlayer = 0.6f;
    /** Multiplayer quests: a teammate this close to the enemy at its defeat counts as fighting together. */
    inline constexpr float TeamQuestRadius = 3200.0f;
	// ---------------------------------------------------------------- プレイヤー成長

	/** バッグに入る持ち物の最大数（装備中の物も含む）。セーブの検証にも使う。 */
	inline constexpr int32 BagCapacity = 60;
	/** レベル・アイテムレベルの上限（暴走防止）。 */
	inline constexpr int32 MaxLevel = 10000;
	/** レベル1つで増える最大HP（基準HPは ARPGPrototypeCharacter::PlayerHP）。 */
	inline constexpr float HPPerLevel = 10.0f;
	/** 「最大HP +%」（タフネス）の合計上限。 */
	inline constexpr float MaxHPBonusCapPercent = 60.0f;
	/** 次のレベルまでの経験値 = XPBase + (Lv-1) × XPPerLevel。 */
	inline constexpr int32 XPBase = 20;
	inline constexpr int32 XPPerLevel = 12;
	inline int32 RequiredXP(const int32 Level) { return XPBase + (FMath::Max(1, Level) - 1) * XPPerLevel; }
	/** 敵1体で得る経験値。 */
	inline constexpr int32 KillXP = 1;
	/**
	 * 敵1体で得るルビー = 乱数(KillRubyMin～KillRubyMax) + プレイヤーLv × KillRubyPerLevel（切り捨て）。その後バッジ補正。
	 * 2026-11 に減らした（以前は 3～6 + Lv。倒す数もLvで増えるので、Lvが上がるほど強化が安くなりすぎていた）。
	 */
	inline constexpr int32 KillRubyMin = 2;
	inline constexpr int32 KillRubyMax = 4;
	inline constexpr float KillRubyPerLevel = 0.25f;
	inline int32 KillRubies(const int32 Level) { return FMath::RandRange(KillRubyMin, KillRubyMax) + FMath::FloorToInt(FMath::Max(1, Level) * KillRubyPerLevel); }
	/** ボス撃破時のルビー = ボスLv × これ。 */
	inline constexpr int32 BossRubyPerLevel = 8;
	/** 宝箱のルビー = 宝箱ごとの範囲（RubyMin～RubyMax）× これ。 */
	inline constexpr float ChestRubyScale = 0.4f;
	inline int32 ChestRubies(const int32 Raw) { return FMath::RoundToInt(FMath::Max(0, Raw) * ChestRubyScale); }
	/** 倒れて復活地点に戻ると、持っているルビーのこの割合を失う（ダウン中に起こしてもらえば失わない）。 */
	inline constexpr float DeathRubyLoss = 0.2f;

	// ---------------------------------------------------------------- 敵

	/** 敵の最大HP = 基準HP(EnemyHP) × (1 + (Lv-1) × これ)。 */
	inline constexpr float EnemyHPPerLevel = 0.17f;
	/** 敵の投球の威力 = EnemyPowerBase + (Lv-1) × EnemyPowerPerLevel。 */
	inline constexpr float EnemyPowerBase = 8.0f;
	inline constexpr float EnemyPowerPerLevel = 2.4f;
	/** An elite: this much more health and power, and this much bigger. */
	inline constexpr float EliteHP = 2.6f;
	inline constexpr float ElitePower = 1.4f;
	inline constexpr float EliteScale = 1.3f;
	/** From this level a group may have an elite (this often). */
	inline constexpr int32 EliteFromLevel = 4;
	inline constexpr float EliteChance = 0.3f;
	/** A named one (an elite with a name, one at a time in the world): this much more again, and how often a group has one. */
	inline constexpr float NamedHP = 1.8f;
	inline constexpr float NamedPower = 1.2f;
	inline constexpr float NamedScale = 1.45f;
	inline constexpr int32 NamedFromLevel = 4;
	inline constexpr float NamedChance = 0.06f;
	/** Tune shards for beating a named one (its drop is a boss's). */
	inline constexpr int32 ShardsPerNamed = 2;
	/** Field events: a storm's enemies hit this much harder and drop this much more often; the caravan's price cut. */
	inline constexpr float StormEnemyPower = 1.25f;
	inline constexpr float StormDropFactor = 1.5f;
	inline constexpr float CaravanDiscount = 0.3f;
	inline constexpr float EnemyThrowCooldown = 2.8f;

	// ---------------------------------------------------------------- ボールの基準値

	/** ボールの威力 = BallBasePower × 種類の倍率 × (1 + (アイテムLv-1) × PowerPerItemLevel) × (1 + レア度段階 × PowerPerRarity)。 */
	inline constexpr float BallBasePower = 18.0f;
	inline constexpr float PowerPerItemLevel = 0.09f;
	inline constexpr float PowerPerRarity = 0.06f;

	/**
	 * 種類ごとの威力倍率と再使用時間（秒）。威力が低い球は範囲・妨害・追尾など別の長所で釣り合わせている。
	 * 新しい球を足すときはここと RPGEquipment::BallRoster / カタログに1行ずつ足す。
	 */
	struct FBallArchetype
	{
		EChaosImpactBallType Type;
		float PowerScale;
		float Cooldown;
		const TCHAR* Role;
	};
	inline constexpr FBallArchetype BallArchetypes[] = {
		{EChaosImpactBallType::Normal,  1.0f, 0.55f, TEXT("いつでも投げられる基本攻撃")},
		{EChaosImpactBallType::Fire,    1.1f, 5.0f,  TEXT("直撃＋爆風＋燃焼の火力役")},
		{EChaosImpactBallType::Ice,     0.9f, 5.0f,  TEXT("減速と凍結で足止めする役")},
		{EChaosImpactBallType::Thunder, 1.1f, 6.0f,  TEXT("高速・反射・連鎖で複数を削る役")},
		{EChaosImpactBallType::Black,   0.6f, 7.0f,  TEXT("吸引でまとめる・継続ダメージ")},
		{EChaosImpactBallType::Wind,    0.7f, 6.0f,  TEXT("押し出しと場所取り")},
		{EChaosImpactBallType::Smoke,   0.3f, 6.0f,  TEXT("照準妨害と自衛（威力はほぼ無い）")},
		{EChaosImpactBallType::Beam,    0.9f, 5.0f,  TEXT("直線貫通の射撃")},
		{EChaosImpactBallType::Snow,    1.0f, 6.0f,  TEXT("持って育てる重い一撃")},
		{EChaosImpactBallType::Simae,   0.4f, 8.0f,  TEXT("自動追尾の小鳥で分散攻撃")},
		{EChaosImpactBallType::Drive,   1.1f, 8.0f,  TEXT("操縦して当てる長射程")},
	};
	inline const FBallArchetype& BallArchetype(const EChaosImpactBallType Type)
	{
		for (const FBallArchetype& Entry : BallArchetypes)
		{
			if (Entry.Type == Type)
			{
				return Entry;
			}
		}
		return BallArchetypes[0];
	}

	/** 再使用時間を短くする効果（再装填・小雪連投）の合計で、元の何倍までしか短くならないか。 */
	inline constexpr float CooldownFloor = 0.55f;
	/** バッジ「リチャージ」で特殊球の待ち時間が元の何倍まで短くなるか。 */
	inline constexpr float SpecialCooldownBadgeFloor = 0.6f;

	// ---------------------------------------------------------------- 強化（ルビー商人）

	/** ノーマル～ユニークの強化上限。レジェンドは上限なし（上限を越えた分は伸びが小さい）。 */
	inline constexpr int32 EnhanceCap = 10;
	/** ボール：強化1回で威力 +6%（上限まで）、上限を越えたレジェンドは +2.5%ずつ。 */
	inline constexpr float EnhancePowerStep = 0.06f;
	inline constexpr float EnhancePowerStepOverCap = 0.025f;
	/** ギア・バッジ：効果値の伸び = 1 + 強化×0.015 + √(上限越え)×0.005 + (Lv-1)×0.003（Lvは30まで）、最大1.8倍。 */
	inline constexpr float GearEnhanceStep = 0.015f;
	inline constexpr float GearOverCapStep = 0.005f;
	inline constexpr float GearLevelStep = 0.003f;
	inline constexpr int32 GearLevelStepCap = 30;
	inline constexpr float GearGrowthCap = 1.8f;
	inline float GearGrowth(const int32 Enhancement, const int32 Level)
	{
		return FMath::Min(GearGrowthCap, 1.0f + FMath::Min(Enhancement, EnhanceCap) * GearEnhanceStep
			+ GearOverCapStep * FMath::Sqrt(static_cast<float>(FMath::Max(0, Enhancement - EnhanceCap)))
			+ FMath::Min(Level - 1, GearLevelStepCap) * GearLevelStep);
	}

	// ---------------------------------------------------------------- チューン

	/** チューンのランク上限と、ランク1つで効果値が元の何%増えるか（個数系は+1ずつ）。 */
	inline constexpr int32 TuneMaxRank = 3;
	inline constexpr float TuneRankStep = 0.15f;
	/** 個数系の上限（羽数・つつき回数は専用、それ以外の貫通・連鎖・欠片などはCountCap）。 */
	inline constexpr float CountCap = 6.0f;
	inline constexpr float BirdCountCap = 14.0f;
	inline constexpr float BirdPeckCap = 3.0f;
	/** 商人が一度に見せるチューン候補の数。 */
	inline constexpr int32 TuneOfferCount = 3;
	/** 固有特性の数（ノーマル・レア・ユニーク・レジェンド）。 */
	inline constexpr int32 IntrinsicCount[] = {0, 0, 1, 2};

	// ---------------------------------------------------------------- ルビーの費用・分解

	/** 分解で得るルビー = (2 + Lv + 強化×3) × レア度倍率。 */
	inline constexpr int64 SalvageBase = 2;
	inline constexpr int64 SalvagePerLevel = 1;
	inline constexpr int64 SalvagePerEnhance = 3;
	inline constexpr int64 SalvageRarityMultiplier[] = {1, 2, 4, 8};
	/**
	 * 強化費用 = (30 + Lv×6) × (1 + 現在の強化値×0.5)。+1 は安く、+10 に近づくほど高い
	 * （Lv10 の装備で +0→+1 が90、+9→+10 が495、+10 まで合計 2925。どのLvでも敵およそ270体分）。
	 */
	inline constexpr int64 UpgradeBase = 30;
	inline constexpr int64 UpgradePerLevel = 6;
	inline constexpr float UpgradeGrowth = 0.5f;
	/** チューンを付ける費用 = 20 + Lv×3 + レア度段階×10。ランクUPは この費用 × 現在のランク。 */
	inline constexpr int64 TuneBase = 20;
	inline constexpr int64 TunePerLevel = 3;
	inline constexpr int64 TunePerRarity = 10;
	// ---------------------------------------------------------------- 商人のショップ（RPGShop.cpp）

	/** 商人のレベル（1～5）：商人に払ったルビー（強化・チューン・買い物）の合計がこの値に届くと上がる。 */
	inline constexpr int32 MerchantMaxLevel = 5;
	inline constexpr int32 MerchantLevelXP[MerchantMaxLevel] = {0, 600, 2500, 7000, 16000};
	inline int32 MerchantLevelFor(const int32 Spent)
	{
		int32 Level = 1;
		for (int32 Index = 1; Index < MerchantMaxLevel; ++Index) { if (Spent >= MerchantLevelXP[Index]) { Level = Index + 1; } }
		return Level;
	}
	/** 商人Lvごとの品のレア度（上から順に判定する累積確率：レジェンド・ユニーク・レア、残りはノーマル）。 */
	struct FShopOdds { float Legend; float Unique; float Rare; };
	inline constexpr FShopOdds ShopOdds[MerchantMaxLevel] = {{0, 0, .25f}, {0, .08f, .4f}, {.02f, .18f, .45f}, {.05f, .3f, .45f}, {.12f, .4f, .4f}};
	/** 品数と入れかわる間隔（実時間の秒）。行商の隊商は来るたびに別の品ぞろえで、商人Lv+1 の品が並ぶ。 */
	inline constexpr int32 ShopStockCount = 4;
	inline constexpr int32 CaravanStockCount = 5;
	inline constexpr int32 ShopRotateSeconds = 600;
	/** ボールの値段 = (40 + Lv×10) × レア度倍率 × (1 - 割引)。 */
	inline constexpr int64 ShopBase = 40;
	inline constexpr int64 ShopPerLevel = 10;
	inline constexpr float ShopRarityMultiplier[] = {1, 2.5f, 6, 14};

	/** 費用・所持ルビーの上限（オーバーフロー防止）。 */
	inline constexpr int64 CostCap = 100000000;
	inline constexpr int64 RubyCap = 1000000000;

	// ---------------------------------------------------------------- ドロップ

	/** 通常の敵がアイテムを落とした時のレア度（上から順に判定する累積確率）。 */
	inline constexpr float EnemyLegendChance = 0.02f;
	inline constexpr float EnemyUniqueChance = 0.10f;
	inline constexpr float EnemyRareChance = 0.28f;
	/** ボスの確定報酬のレア度（残りはレア）。 */
	inline constexpr float BossLegendChance = 0.08f;
	inline constexpr float BossUniqueChance = 0.37f;
	/** 落とした物の種類：バッジ30%、ギア25%、残り45%がボール。 */
	inline constexpr float DropBadgeChance = 0.30f;
	inline constexpr float DropGearChance = 0.25f;
	/** スポーン地点が無い敵のドロップ率（地点ごとの値は ARPGPrototypeSpawnPoint::DropChance）。 */
	inline constexpr float DefaultDropChance = 0.25f;

	// ---------------------------------------------------------------- 戦闘の安全上限

	/** 与ダメージ補正の合計上限（%）。 */
	inline constexpr float DamageBonusCapPercent = 200.0f;
	/** 被ダメージ軽減の合計上限（倍率）。 */
	inline constexpr float DamageReductionCap = 0.65f;
	/** 「受けるHP回復量+%」の上限（%）。 */
	inline constexpr float HealBonusCapPercent = 40.0f;
	/** 移動速度の倍率の範囲（減速・加速の合計後）。 */
	inline constexpr float MoveSpeedMin = 0.25f;
	inline constexpr float MoveSpeedMax = 1.8f;
	/** ダッシュ待ち時間・チャージ時間は元の何倍まで短くなるか。 */
	inline constexpr float DashCooldownFloor = 0.5f;
	inline constexpr float ChargeTimeFloor = 0.5f;
	/** 「非戦闘中」とみなすまでの、攻撃も被弾もしない秒数。 */
	inline constexpr double OutOfCombatSeconds = 6.0;
	/** ノックバックの最大距離（cm）。ボスは20%だけ受ける。 */
	inline constexpr float KnockbackMax = 300.0f;
	inline constexpr float BossKnockbackScale = 0.2f;
	/** 凍結時間の範囲（秒）。 */
	inline constexpr float FreezeMin = 0.35f;
	inline constexpr float FreezeMax = 1.3f;
	/** 足止め（感電・縫い止め）の最大秒数。 */
	inline constexpr float StunMax = 0.5f;

	// ---------------------------------------------------------------- 雪原のシマエナガ（RPGSnowField）

	/** ボスのレベルとHP（RPGの数値。ソロ版のボス本体のHPとは比率で連動）。 */
	inline constexpr int32 SimaeBossLevel = 12;
	inline constexpr float SimaeBossHP = 3600.0f;
	/** ボスの攻撃1回が当たった時のダメージ（ソロ版の「1ダメージ」をこの値に読み替え）。フルパワーは1.2倍。 */
	inline constexpr float SimaeHitDamage = 26.0f;
	/** 叩きつけで「ボールを全部落とす」代わりに、全部の球が使えなくなる秒数。 */
	inline constexpr float SimaeDropBallsSeconds = 1.5f;

	// ---------------------------------------------------------------- ダウンと救助（RPGRevive.cpp。プレイヤーが2人以上の時）

	/** HPが0になってから、起こしてもらえる秒数。過ぎると今まで通り倒れて復活待ち。 */
	inline constexpr float DownSeconds = 30.0f;
	/** 仲間を起こすのに長押しする秒数（その間に起こす人が攻撃を受けると最初から）。 */
	inline constexpr float ReviveHoldSeconds = 3.0f;
	/** 起こせる距離（cm）。 */
	inline constexpr float ReviveRadius = 230.0f;
	/** 起きた時のHP（最大HPに対する割合）と、起きた直後に攻撃を受けない秒数。 */
	inline constexpr float ReviveHealthFraction = 1.0f / 3.0f;
	inline constexpr float ReviveGraceSeconds = 1.5f;

	// ---------------------------------------------------------------- ステージの外に落ちた時

	/** 最後に立っていた所からこれだけ下に落ちたら「外に落ちた」（cm）。 */
	inline constexpr float FallDepth = 700.0f;
	/** 落ちた時のダメージ（最大HPの%）。これで倒れることはない（HP1で止まる）。 */
	inline constexpr float FallDamagePercent = 15.0f;

	// ---------------------------------------------------------------- 回復（RPGHealing.cpp）

	/** 敵がハートを落とす確率、ボスが落とす数、ハート1つの回復量（最大HPの%）、消えるまでの秒数。 */
	inline constexpr float HeartDropChance = 0.15f;
	inline constexpr int32 BossHearts = 3;
	inline constexpr float HeartHealPercent = 25.0f;
	inline constexpr float HeartLifetime = 30.0f;
	/** いやし草：回復量（最大HPの%）と、食べてから実がまたなるまでの秒数。 */
	inline constexpr float HerbHealPercent = 35.0f;
	inline constexpr float HerbRegrowSeconds = 60.0f;
	/** 休息の灯りのそば：戦っていない時、毎秒回復する量（最大HPの%）と範囲（cm）。 */
	inline constexpr float BeaconRestPercentPerSecond = 6.0f;
	inline constexpr float BeaconRestRadius = 450.0f;
}
