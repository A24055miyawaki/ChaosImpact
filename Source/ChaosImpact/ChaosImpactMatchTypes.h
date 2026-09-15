#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "ChaosImpactMatchTypes.generated.h"

/** Rules chosen before a VS match. Replicated on the game state, so every machine runs the same match. */
USTRUCT(BlueprintType)
struct FChaosImpactMatchRules
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 Minutes = 3;

	/** 0 = every player for themselves; 2-4 = team battle with that many teams. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 TeamCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 CPUCount = 1;

	bool IsTeamBattle() const { return TeamCount >= 2; }
};

namespace ChaosImpactMatch
{
	inline constexpr int32 MaxCompetitors = 8;
	inline constexpr int32 MaxTeams = 4;

	/** Match lengths offered on the rules screen, in minutes. */
	inline constexpr int32 MinuteChoices[] = {3, 4, 5};

	/** The offered length closest to Minutes. */
	inline int32 SanitizeMinutes(const int32 Minutes)
	{
		int32 Best = MinuteChoices[0];
		for (const int32 Choice : MinuteChoices)
		{
			if (FMath::Abs(Choice - Minutes) < FMath::Abs(Best - Minutes))
			{
				Best = Choice;
			}
		}
		return Best;
	}

	/** Opening: the camera flies over the stage, dives down to the player's own view, then Ready? until GO. */
	inline constexpr float FlyoverSeconds = 4.4f;
	inline constexpr float DiveSeconds = 1.3f;
	/** Ready? runs this long before GO, and starts once every machine has finished its own opening. */
	inline constexpr float ReadySeconds = 1.3f;
	/** Ready? starts anyway if a machine has not reported its opening finished this long after it should have. */
	inline constexpr float IntroWaitTimeoutSeconds = 6.0f;
	/** The shortest opening: flyover, dive and Ready? with no one to wait for. */
	inline constexpr float IntroSeconds = FlyoverSeconds + DiveSeconds + ReadySeconds;
	inline constexpr float ResultsSeconds = 12.0f;
	/** Local matches offer the rematch menu once the results have been revealed. */
	inline constexpr float ResultsRevealSeconds = 7.0f;

	/** Each hit on an opponent scores; knocking them out scores this bonus on top. */
	inline constexpr int32 HitPoints = 1;
	inline constexpr int32 KnockoutBonusPoints = 1;

	/** A lone player always gets at least one CPU, and every team needs at least one member. */
	inline int32 GetMinCPUCount(const int32 Humans, const int32 TeamCount)
	{
		const int32 SafeHumans = FMath::Max(Humans, 1);
		int32 Minimum = SafeHumans <= 1 ? 1 : 0;
		if (TeamCount >= 2)
		{
			Minimum = FMath::Max(Minimum, TeamCount - SafeHumans);
		}
		return FMath::Clamp(Minimum, 0, MaxCompetitors - SafeHumans);
	}

	inline int32 GetMaxCPUCount(const int32 Humans)
	{
		return FMath::Max(0, MaxCompetitors - FMath::Max(Humans, 1));
	}

	inline FChaosImpactMatchRules Sanitize(FChaosImpactMatchRules Rules, const int32 Humans)
	{
		Rules.Minutes = SanitizeMinutes(Rules.Minutes);
		Rules.TeamCount = Rules.TeamCount >= 2 ? FMath::Clamp(Rules.TeamCount, 2, MaxTeams) : 0;
		Rules.CPUCount = FMath::Clamp(Rules.CPUCount, GetMinCPUCount(Humans, Rules.TeamCount), GetMaxCPUCount(Humans));
		return Rules;
	}

	/** Mario Kart style: teams stay even, so with 8 players two teams hold 4 each and four teams 2 each. */
	inline int32 GetTeamCapacity(const int32 TeamCount, const int32 Competitors)
	{
		return TeamCount >= 2
			? FMath::Max(1, FMath::DivideAndRoundUp(FMath::Clamp(Competitors, 1, MaxCompetitors), TeamCount))
			: MaxCompetitors;
	}

	inline FLinearColor GetTeamColor(const int32 Team)
	{
		static const FLinearColor Colors[] =
		{
			FLinearColor(1.0f, 0.12f, 0.16f),
			FLinearColor(0.05f, 0.52f, 1.0f),
			FLinearColor(1.0f, 0.74f, 0.05f),
			FLinearColor(0.16f, 0.9f, 0.36f)
		};
		return Colors[FMath::Clamp(Team, 0, MaxTeams - 1)];
	}

	inline const TCHAR* GetTeamName(const int32 Team)
	{
		static const TCHAR* Names[] = {TEXT("レッド"), TEXT("ブルー"), TEXT("イエロー"), TEXT("グリーン")};
		return Names[FMath::Clamp(Team, 0, MaxTeams - 1)];
	}

	inline FString DescribeTeams(const int32 TeamCount)
	{
		return TeamCount >= 2 ? FString::Printf(TEXT("チーム戦  %dチーム"), TeamCount) : FString(TEXT("個人戦"));
	}

	/** URL options for a local match level; ReadOptions is the inverse. */
	inline FString ToOptions(const FChaosImpactMatchRules& Rules)
	{
		return FString::Printf(TEXT("CIMatch=1?CIMinutes=%d?CITeams=%d?CIMatchCPU=%d"),
			Rules.Minutes, Rules.TeamCount, Rules.CPUCount);
	}

	inline bool ReadOptions(const FURL& URL, FChaosImpactMatchRules& OutRules)
	{
		if (!URL.HasOption(TEXT("CIMatch=1")))
		{
			return false;
		}
		OutRules.Minutes = FCString::Atoi(URL.GetOption(TEXT("CIMinutes="), TEXT("3")));
		OutRules.TeamCount = FCString::Atoi(URL.GetOption(TEXT("CITeams="), TEXT("0")));
		OutRules.CPUCount = FCString::Atoi(URL.GetOption(TEXT("CIMatchCPU="), TEXT("1")));
		return true;
	}
}
