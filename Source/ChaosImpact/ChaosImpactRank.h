#pragma once

#include "CoreMinimal.h"

/** This player's preseason rank, as the relay keeps it (Tools/OnlineRelay/ci_relay.py, RankBook). */
struct FChaosImpactRankInfo
{
	bool bKnown = false;
	int32 Mmr = 1500;
	int32 Games = 0;
	int32 Wins = 0;
	int32 Losses = 0;
	int32 Draws = 0;
	int32 Best = 1500;
	/** Placement matches still to play (the tier shows as 認定中 until they are done). */
	int32 Placement = 10;
	/** The last results, oldest first: "W", "L" or "D". */
	TArray<FString> Recent;
};

/** What became of a ランクマッチ's result on the relay. */
enum class EChaosImpactRankReportState : uint8
{
	None,
	/** Sent; the relay has not answered yet. */
	Sending,
	/** The relay waits for the other player's report. */
	Pending,
	Done,
	/** The two reports disagreed: the match does not count. */
	Void,
	/** The relay never answered: nothing was recorded. */
	Failed
};

struct FChaosImpactRankReport
{
	EChaosImpactRankReportState State = EChaosImpactRankReportState::None;
	FString Result;
	int32 Before = 0;
	int32 After = 0;
	int32 Delta = 0;
	double ChangedAt = 0.0;
};

/** The tiers the MMR is shown as (Docs/ランクマッチ_専用サーバー設計.md 3-3). Their bounds will follow the preseason. */
namespace ChaosImpactRank
{
	struct FTier
	{
		const TCHAR* Name;
		int32 Floor;
		FLinearColor Color;
	};

	inline const FTier* GetTiers(int32& OutCount)
	{
		static const FTier Tiers[] =
		{
			{TEXT("ルーキー"), 0, FLinearColor(0.46f, 0.72f, 0.62f)},
			{TEXT("ブロンズ"), 1300, FLinearColor(0.86f, 0.5f, 0.24f)},
			{TEXT("シルバー"), 1450, FLinearColor(0.76f, 0.84f, 0.95f)},
			{TEXT("ゴールド"), 1550, FLinearColor(1.0f, 0.76f, 0.12f)},
			{TEXT("プラチナ"), 1650, FLinearColor(0.42f, 0.95f, 0.92f)},
			{TEXT("チャンピオン"), 1800, FLinearColor(1.0f, 0.22f, 0.36f)}
		};
		OutCount = UE_ARRAY_COUNT(Tiers);
		return Tiers;
	}

	inline int32 GetTierIndex(const int32 Mmr)
	{
		int32 Count = 0;
		const FTier* Tiers = GetTiers(Count);
		int32 Index = 0;
		for (int32 Tier = 0; Tier < Count; ++Tier)
		{
			Index = Mmr >= Tiers[Tier].Floor ? Tier : Index;
		}
		return Index;
	}

	/** The tier's span: rookies are counted from 1100, champions up to 2000, for the gauge. */
	inline void GetTierSpan(const int32 Mmr, int32& OutFloor, int32& OutCeiling)
	{
		int32 Count = 0;
		const FTier* Tiers = GetTiers(Count);
		const int32 Index = GetTierIndex(Mmr);
		OutFloor = Index == 0 ? 1100 : Tiers[Index].Floor;
		OutCeiling = Index + 1 < Count ? Tiers[Index + 1].Floor : 2000;
	}

	inline FLinearColor GetTierColor(const int32 Mmr)
	{
		int32 Count = 0;
		return GetTiers(Count)[GetTierIndex(Mmr)].Color;
	}

	/** Each tier but the top one has three steps, III (just in) to I (about to go up). */
	inline int32 GetDivision(const int32 Mmr)
	{
		int32 Count = 0;
		GetTiers(Count);
		if (GetTierIndex(Mmr) == Count - 1)
		{
			return 0;
		}
		int32 Floor = 0;
		int32 Ceiling = 0;
		GetTierSpan(Mmr, Floor, Ceiling);
		const float Along = FMath::Clamp(static_cast<float>(Mmr - Floor) / FMath::Max(1, Ceiling - Floor), 0.0f, 0.999f);
		return 3 - FMath::FloorToInt(Along * 3.0f);
	}

	/** 0-1 through the tier (for its gauge). */
	inline float GetTierProgress(const int32 Mmr)
	{
		int32 Floor = 0;
		int32 Ceiling = 0;
		GetTierSpan(Mmr, Floor, Ceiling);
		return FMath::Clamp(static_cast<float>(Mmr - Floor) / FMath::Max(1, Ceiling - Floor), 0.0f, 1.0f);
	}

	/** "ゴールド II", "チャンピオン", or 認定中 while placement matches remain. */
	inline FString GetTierLabel(const FChaosImpactRankInfo& Info)
	{
		if (Info.Placement > 0)
		{
			return FString::Printf(TEXT("認定中 %d/10"), 10 - Info.Placement);
		}
		int32 Count = 0;
		const FTier* Tiers = GetTiers(Count);
		static const TCHAR* Steps[] = {TEXT(""), TEXT(" I"), TEXT(" II"), TEXT(" III")};
		return FString(Tiers[GetTierIndex(Info.Mmr)].Name) + Steps[GetDivision(Info.Mmr)];
	}

	inline FString GetTierLabel(const int32 Mmr)
	{
		FChaosImpactRankInfo Info;
		Info.Mmr = Mmr;
		Info.Placement = 0;
		return GetTierLabel(Info);
	}
}
