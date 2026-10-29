#include "ChaosImpactSoloStages.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
	using EKind = EChaosImpactSoloStageKind;

	TArray<FChaosImpactSoloStageInfo> MakeStages()
	{
		TArray<FChaosImpactSoloStageInfo> S;
		const auto Add = [&S](const int32 Number, const int32 Map, const TCHAR* Name, const TCHAR* Area, const TCHAR* Blurb,
			const EKind Kind, const TArray<int32>& Requires, const TCHAR* Level = TEXT(""))
		{
			FChaosImpactSoloStageInfo& Info = S.AddDefaulted_GetRef();
			Info.Number = Number;
			Info.Map = Map;
			Info.Name = Name;
			Info.Area = Area;
			Info.Blurb = Blurb;
			Info.Kind = Kind;
			Info.Requires = Requires;
			Info.Level = Level;
		};
		// ---- Map 1: the world the United States of Dett Ball has occupied.
		Add(1, 1, TEXT("宣言の夜"), TEXT("ネオン街"), TEXT("街頭ビジョンに大統領。ボールを没収しに来た合衆国兵を追い払え！"),
			EKind::Normal, {}, TEXT("/Game/ChaosImpact/solo/Lvl_SoloStage1"));
		const TCHAR* Sea = TEXT("潮の占領地");
		Add(2, 1, TEXT("霧の桟橋"), Sea, TEXT("霧で遠くが見えない。近くの敵から確実に。"), EKind::Normal, {1}, TEXT("/Game/ChaosImpact/solo/Lvl_Solo_02_MistyPier"));
		Add(3, 1, TEXT("朝どれ大漁市場"), Sea, TEXT("床を大きな魚がびちびち跳ね回る。潮が満ちると低い足場が沈む。"), EKind::Normal, {2}, TEXT("/Game/ChaosImpact/solo/Lvl_Solo_03_FishMarket"));
		Add(4, 1, TEXT("クレーン操車場"), Sea, TEXT("動くクレーンがコンテナを落としてくる。"), EKind::Normal, {3});
		Add(5, 1, TEXT("沈んだ灯台"), Sea, TEXT("真っ暗。灯台の光に入った敵だけが見える。"), EKind::Normal, {4});
		Add(6, 1, TEXT("沖の的当て"), Sea, TEXT("チャレンジ：時間内に浮かぶ的を全部倒せ。"), EKind::Challenge, {5});
		Add(7, 1, TEXT("港の司令塔"), Sea, TEXT("潮の占領地を仕切る四天王のひとり。"), EKind::Boss, {6});
		const TCHAR* Forest = TEXT("霧氷の森");
		Add(8, 1, TEXT("粉雪の小径"), Forest, TEXT("雪をかぶった木が射線をさえぎる。回り込んで当てろ。"), EKind::Normal, {1},
			TEXT("/Game/ChaosImpact/solo/Lvl_Solo_08_SnowForest"));
		Add(9, 1, TEXT("ぬくぬく地下迷宮"), Forest, TEXT("雪の下に広がるアリの巣。狭い通路で小さな兵隊たちの群れと戦う。"), EKind::Normal, {8});
		Add(10, 1, TEXT("ふかふか雪原"), Forest, TEXT("雪が深くて足が遅くなる。雪だるまの陰に注意。"), EKind::Normal, {9});
		Add(11, 1, TEXT("星降る舞踏会"), Forest, TEXT("ひらひらよけまくる妖精たちに当ててみろ。"), EKind::Normal, {10});
		Add(12, 1, TEXT("千年樹のぼり"), Forest, TEXT("チャレンジ：霧氷をまとった巨木の段々を登りながら勝ち抜け。"), EKind::Challenge, {11});
		Add(13, 1, TEXT("森の司令塔"), Forest, TEXT("雪の森を占領した四天王のひとり。"), EKind::Boss, {12});
		Add(14, 1, TEXT("雲海の参道"), TEXT("天空の参道"), TEXT("潮と森の司令塔を倒すと開く、神殿への道。"), EKind::Normal, {7, 13},
			TEXT("/Game/ChaosImpact/solo/Lvl_Solo_14_CloudPath"));
		Add(15, 1, TEXT("空中神殿"), TEXT("天空の参道"), TEXT("門番を倒せば天空の門が開き、合衆国本土へ渡れる。"), EKind::Story, {14},
			TEXT("/Game/ChaosImpact/solo/Lvl_SoloStage2"));
		Add(ChaosImpactSolo::GateToMap2, 1, TEXT("天空の門"), TEXT("天空の参道"), TEXT("この先、デットボール合衆国。このマップを全部クリアすると開く。"),
			EKind::Gate, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
		// ---- Map 2: the United States of Dett Ball itself.
		Add(ChaosImpactSolo::GateToMap1, 2, TEXT("天空の門"), TEXT("合衆国の国境"), TEXT("占領された世界へ戻る門。"), EKind::Gate, {15});
		const TCHAR* Ice = TEXT("合衆国 氷の州");
		Add(16, 2, TEXT("雪原の駅"), Ice, TEXT("雪で足がゆっくりに。氷のボールが登場。"), EKind::Normal, {15});
		Add(17, 2, TEXT("氷晶の洞窟"), Ice, TEXT("床がつるつる滑る。"), EKind::Normal, {16});
		Add(18, 2, TEXT("氷鏡の回廊"), Ice, TEXT("氷の壁でボールが跳ね返る。反射で当てろ。"), EKind::Normal, {17});
		Add(19, 2, TEXT("吹雪の峠"), Ice, TEXT("横風でボールが流される。"), EKind::Normal, {18});
		Add(20, 2, TEXT("氷像割り"), Ice, TEXT("チャレンジ：時間内に氷像を全部壊せ。"), EKind::Challenge, {19});
		Add(21, 2, TEXT("氷の州庁"), Ice, TEXT("氷の州を治める四天王のひとり。"), EKind::Boss, {20});
		const TCHAR* Fire = TEXT("合衆国 炎の州");
		Add(22, 2, TEXT("湯けむり温泉街"), Fire, TEXT("湯けむりが時々視界をふさぐ。"), EKind::Normal, {15});
		Add(23, 2, TEXT("溶岩の鋳造所"), Fire, TEXT("ベルトコンベアで運ばれる。"), EKind::Normal, {22});
		Add(24, 2, TEXT("噴火口の足場"), Fire, TEXT("決まった間隔で溶岩が噴き出す。"), EKind::Normal, {23});
		Add(25, 2, TEXT("炎の闘技場"), Fire, TEXT("休みなしの連続ウェーブ。"), EKind::Normal, {24});
		Add(26, 2, TEXT("炎と氷"), Fire, TEXT("チャレンジ：炎の敵は氷で、氷の敵は炎で。"), EKind::Challenge, {25});
		Add(27, 2, TEXT("炎の州庁"), Fire, TEXT("炎の州を治める四天王のひとり。"), EKind::Boss, {26});
		const TCHAR* Capital = TEXT("合衆国 首都");
		Add(28, 2, TEXT("ボール没収工場"), Capital, TEXT("世界中から奪われたボールを取り返せ！"), EKind::Story, {21, 27});
		Add(29, 2, TEXT("首都の大通り"), Capital, TEXT("合衆国軍の総攻撃。"), EKind::Normal, {28});
		Add(30, 2, TEXT("ホワイトボールハウス"), Capital, TEXT("大統領ドッチ・ボールとの決戦。"), EKind::Story, {29});
		return S;
	}
}

int32 ChaosImpactSolo::GetTier(const int32 Number)
{
	// Map 1: the sea (2-7) and the forest (8-13) both open off stage 1, then the path and the temple (14, 15).
	// Map 2: the ice (16-21) and fire (22-27) states both open off the temple, then the capital (28-30).
	if (Number <= 1) { return 0; }
	if (Number <= 7) { return Number - 1; }
	if (Number <= 13) { return Number - 7; }
	if (Number <= 15) { return Number - 7; }
	if (Number <= 21) { return Number - 7; }
	if (Number <= 27) { return Number - 13; }
	return Number - 13;
}

ChaosImpactSolo::FDifficulty ChaosImpactSolo::GetDifficulty(const int32 Number)
{
	using ELevel = EChaosImpactCPULevel;
	// Minions, at once, weak, strong, the room's boss.
	static const FDifficulty Tiers[] = {
		{3, 2, 3, 0, ELevel::Normal},   // 1: learning to play
		{4, 2, 2, 0, ELevel::Normal},
		{4, 3, 1, 0, ELevel::Strong},
		{5, 3, 0, 0, ELevel::Strong},
		{5, 3, 0, 1, ELevel::Strong},
		{5, 3, 0, 1, ELevel::Strong},
		{5, 3, 0, 2, ELevel::Strong},
		{6, 3, 0, 2, ELevel::Strongest}, // 14: the cloud path
		{6, 4, 0, 2, ELevel::Strongest}, // 15: the temple's gatekeeper
		{6, 4, 0, 3, ELevel::Strongest},
		{6, 4, 0, 4, ELevel::Strongest},
	};
	return Tiers[FMath::Clamp(GetTier(Number), 0, static_cast<int32>(UE_ARRAY_COUNT(Tiers)) - 1)];
}

const TArray<FChaosImpactSoloStageInfo>& ChaosImpactSolo::GetStages()
{
	static const TArray<FChaosImpactSoloStageInfo> Stages = MakeStages();
	return Stages;
}

const FChaosImpactSoloStageInfo* ChaosImpactSolo::FindStage(const int32 Number)
{
	return GetStages().FindByPredicate([Number](const FChaosImpactSoloStageInfo& Info) { return Info.Number == Number; });
}

FString ChaosImpactSolo::GetLevelPackage(const int32 Number)
{
	const FChaosImpactSoloStageInfo* Info = FindStage(Number);
	return Info ? FString(Info->Level) : FString();
}

FString ChaosImpactSolo::GetMapPackage(const int32 Map)
{
	return FString::Printf(TEXT("/Game/ChaosImpact/solo/WorldMap/Lvl_SoloWorldMap%d"), FMath::Clamp(Map, 1, 2));
}

namespace
{
	/** The file chosen on solo mode's title (kept while the game runs, across levels). */
	int32 GSoloActiveSlot = 1;
	/** Before there were three files, the one progress was saved here: it becomes file 1. */
	const TCHAR* const LegacySlotName = TEXT("ChaosImpactSoloProgress");
}

int32 UChaosImpactSoloProgress::GetActiveSlot()
{
	return GSoloActiveSlot;
}

void UChaosImpactSoloProgress::SetActiveSlot(const int32 Slot)
{
	GSoloActiveSlot = FMath::Clamp(Slot, 1, SlotCount);
}

FString UChaosImpactSoloProgress::GetSlotName(const int32 Slot)
{
	const int32 Clamped = FMath::Clamp(Slot, 1, SlotCount);
	return GIsAutomationTesting ? FString::Printf(TEXT("ChaosImpactSoloProgress_Test_%d"), Clamped)
		: FString::Printf(TEXT("ChaosImpactSoloProgress_%d"), Clamped);
}

FString UChaosImpactSoloProgress::GetSlotName()
{
	return GetSlotName(GSoloActiveSlot);
}

bool UChaosImpactSoloProgress::DoesSlotExist(const int32 Slot)
{
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(Slot), 0)
		|| (Slot == 1 && !GIsAutomationTesting && UGameplayStatics::DoesSaveGameExist(LegacySlotName, 0));
}

UChaosImpactSoloProgress* UChaosImpactSoloProgress::LoadSlot(const int32 Slot)
{
	FString Name = GetSlotName(Slot);
	if (!UGameplayStatics::DoesSaveGameExist(Name, 0) && Slot == 1 && !GIsAutomationTesting)
	{
		Name = LegacySlotName;
	}
	if (UGameplayStatics::DoesSaveGameExist(Name, 0))
	{
		if (UChaosImpactSoloProgress* Saved = Cast<UChaosImpactSoloProgress>(UGameplayStatics::LoadGameFromSlot(Name, 0)))
		{
			return Saved;
		}
	}
	return Cast<UChaosImpactSoloProgress>(UGameplayStatics::CreateSaveGameObject(StaticClass()));
}

void UChaosImpactSoloProgress::DeleteSlot(const int32 Slot)
{
	UGameplayStatics::DeleteGameInSlot(GetSlotName(Slot), 0);
	if (Slot == 1 && !GIsAutomationTesting)
	{
		UGameplayStatics::DeleteGameInSlot(LegacySlotName, 0);
	}
}

UChaosImpactSoloProgress* UChaosImpactSoloProgress::Load()
{
	return LoadSlot(GSoloActiveSlot);
}

void UChaosImpactSoloProgress::Save()
{
	LastPlayed = FDateTime::Now();
	UGameplayStatics::SaveGameToSlot(this, GetSlotName(), 0);
}

void UChaosImpactSoloProgress::ResetAll()
{
	Cleared.Reset();
	Shown.Reset();
	LastNode = 1;
	bUnlockAll = false;
	Save();
}

bool UChaosImpactSoloProgress::IsAvailable(const int32 Number) const
{
	const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Number);
	if (!Info)
	{
		return false;
	}
	if (bUnlockAll)
	{
		return true;
	}
	for (const int32 Need : Info->Requires)
	{
		if (!IsCleared(Need))
		{
			return false;
		}
	}
	return true;
}

bool UChaosImpactSoloProgress::IsRevealed(const int32 Number) const
{
	if (IsAvailable(Number) || IsCleared(Number))
	{
		return true;
	}
	const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Number);
	if (Info && Info->Kind == EChaosImpactSoloStageKind::Gate)
	{
		// A gate shows once the stage it stands behind (the last it needs) is cleared, sealed until all are.
		return Info->Requires.Num() && IsCleared(Info->Requires.Last());
	}
	return Info && Info->Requires.ContainsByPredicate([this](const int32 Need) { return IsCleared(Need); });
}

void UChaosImpactSoloProgress::MarkCleared(const int32 Number)
{
	Cleared.AddUnique(Number);
}

int32 UChaosImpactSoloProgress::CountCleared(const int32 Map) const
{
	int32 Count = 0;
	for (const int32 Number : Cleared)
	{
		const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(Number);
		Count += Info && Info->Map == Map && Info->Kind != EChaosImpactSoloStageKind::Gate ? 1 : 0;
	}
	return Count;
}
