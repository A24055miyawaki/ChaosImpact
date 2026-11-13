#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "ChaosImpactTournament.h"
#include "ChaosImpactMatchTypes.generated.h"

/** CPU strength as Blueprints and the details panel show it (same order as ChaosImpactMatch::CPULevel*). */
UENUM(BlueprintType)
enum class EChaosImpactCPULevel : uint8
{
	Weak UMETA(DisplayName="よわい"),
	Normal UMETA(DisplayName="ふつう"),
	Strong UMETA(DisplayName="つよい"),
	Strongest UMETA(DisplayName="さいきょう")
};

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

	/** Local match: the players watch (spectator cameras) and only CPUs compete. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bSpectate = false;

	/** Which stage: 0..5 = ステージ1..ステージ6 on the stage select screen. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 StageIndex = 0;

	/** CPU strength: 0 よわい (for beginners), 1 ふつう, 2 つよい (the full CPU). Only offered with CPUs in the match. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	int32 CPULevel = 2;

	/** True when the VS rule is クラッシュボール (2-team 5-minute Giant Ball push battle). */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bCrashBall = false;

	/**
	 * ランクマッチ: one against one in rounds (first to ChaosImpactMatch::DuelRoundsToWin), 3 HP a round, normal balls
	 * only, on the duel arena (AChaosImpactDuelStage). No teams, no CPUs, no Minutes.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bDuel = false;
	/** One life, one round; last surviving player/team or most remaining HP at the bell. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bBattleRoyale = false;
	/** トーナメント (online rooms only): 1v1 duels in a bracket; Tournament holds how it runs and its CPUs. */
	UPROPERTY(BlueprintReadOnly, Category="Chaos Impact|Match")
	bool bTournament = false;
	UPROPERTY()
	FChaosImpactTournamentSettings Tournament;
	/** ボールスイッチ: the special balls switched off for this match (bit N: EChaosImpactBallType N; 0: every one on). */
	UPROPERTY()
	int32 OffBalls = 0;
	/**
	 * パーティルール (free-for-all only): 0 none, ChaosImpactMatch::PartyRuby ルビーラッシュ (collect the rubies lying
	 * about; hits shake some loose), ChaosImpactMatch::PartyCrown クラウンキープ (hold the crown for its counts).
	 */
	UPROPERTY()
	uint8 PartyRule = 0;

	bool IsTeamBattle() const { return !bDuel && (bCrashBall || TeamCount >= 2); }
	bool IsRubyRush() const { return PartyRule == 1 && !bDuel && !bBattleRoyale && !bTournament; }
	bool IsCrownKeep() const { return PartyRule == 2 && !bDuel && !bBattleRoyale && !bTournament; }
	bool IsParty() const { return IsRubyRush() || IsCrownKeep(); }
	bool IsCrashBall() const { return bCrashBall; }
	bool IsDuel() const { return bDuel; }
};

namespace ChaosImpactMatch
{
	inline constexpr int32 MaxCompetitors = 8;
	inline constexpr int32 MaxTeams = 4;
	inline constexpr float RoyaleSeconds = 120.f;
	inline constexpr float RoyaleSuddenDeathSeconds = 30.f;

	/** Stages offered on the stage select screen, in StageIndex order. */
	inline constexpr int32 StageCount = 6;

	inline int32 SanitizeStage(const int32 StageIndex)
	{
		return FMath::Clamp(StageIndex, 0, StageCount - 1);
	}

	/** Stages have no names yet: they are shown by number (ステージ1 .. ステージ6). */
	inline FString GetStageLabel(const int32 StageIndex)
	{
		return FString::Printf(TEXT("ステージ%d"), SanitizeStage(StageIndex) + 1);
	}

	/**
	 * Each stage's own level: every match is played in its stage's level (a local match opens it; an online room moves
	 * there from its lobby and back). Content/ChaosImpact/Versus/Levels.
	 */
	inline FString GetStageLevel(const int32 StageIndex)
	{
		static const TCHAR* Levels[StageCount] = {
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage1"),
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage2"),
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage3"),
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage4"),
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage5"),
			TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage6")};
		return Levels[SanitizeStage(StageIndex)];
	}

	/** The 1-on-1 arena's own level (ランクマッチ and a local 1対1). */
	inline FString GetDuelLevel()
	{
		return TEXT("/Game/ChaosImpact/Versus/Levels/Lvl_VS_Duel");
	}

	/** The level a match with these rules is played in: the duel's arena, else its stage's own level. */
	inline FString GetMatchLevel(const FChaosImpactMatchRules& Rules)
	{
		return Rules.bDuel ? GetDuelLevel() : GetStageLevel(Rules.StageIndex);
	}

	/** CPU strengths offered on the rules screen, in CPULevel order. */
	inline constexpr int32 CPULevelCount = 4;
	inline constexpr int32 CPULevelWeak = 0;
	inline constexpr int32 CPULevelNormal = 1;
	inline constexpr int32 CPULevelStrong = 2;
	/** Plays the game out exactly: reads every ball's real flight, dodges all it can, throws only what cannot be escaped. */
	inline constexpr int32 CPULevelStrongest = 3;

	inline int32 SanitizeCPULevel(const int32 Level)
	{
		return FMath::Clamp(Level, 0, CPULevelCount - 1);
	}

	inline const TCHAR* GetCPULevelName(const int32 Level)
	{
		static const TCHAR* Names[] = {TEXT("よわい"), TEXT("ふつう"), TEXT("つよい"), TEXT("さいきょう")};
		return Names[SanitizeCPULevel(Level)];
	}

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

	// ---- ランクマッチ (the duel): rounds of 3 HP each, first to two rounds.
	inline constexpr int32 DuelRoundsToWin = 2;
	inline constexpr int32 DuelMaxRounds = 3;
	inline constexpr float DuelRoundSeconds = 90.0f;
	/** Level on health when a round's time runs out: the first hit within this long wins it, else it is drawn. */
	inline constexpr float DuelSuddenDeathSeconds = 30.0f;
	/** ROUND N, then FIGHT! (the first round: after the opening's Ready?). */
	inline constexpr float DuelRoundIntroSeconds = 2.0f;
	/** The round's end held on screen before the next one (or the results). */
	inline constexpr float DuelRoundOverSeconds = 3.4f;
	inline const TCHAR* GetDuelStageName() { return TEXT("デュエルアリーナ"); }
	/** The special balls of a duel: this many kinds, drawn by the ball slot at the start of every match. */
	inline constexpr int32 DuelBallPicks = 3;
	/** The ball slot: its reels spin and stop one by one, then the kinds drawn are shown before ROUND 1. */
	inline constexpr float DuelBallDrawSeconds = 4.8f;
	/** When each reel stops, from the start of the draw. */
	inline constexpr float DuelReelStopSeconds[DuelBallPicks] = {1.4f, 2.1f, 2.8f};

	/** Opening: the camera flies over the stage, dives down to the player's own view, then Ready? until GO. */
	inline constexpr float FlyoverSeconds = 4.4f;
	inline constexpr float DiveSeconds = 1.3f;
	/** Ready? runs this long before GO, and starts once every machine has finished its own opening. */
	inline constexpr float ReadySeconds = 1.3f;
	/** Ready? starts anyway if a machine has not reported its opening finished this long after it should have. */
	inline constexpr float IntroWaitTimeoutSeconds = 6.0f;
	/** Online: the VS card plays on every screen first (the opening's own clock starts after it). */
	inline constexpr float OnlineCardSeconds = 2.4f;
	/** The shortest opening: flyover, dive and Ready? with no one to wait for. */
	inline constexpr float IntroSeconds = FlyoverSeconds + DiveSeconds + ReadySeconds;
	inline constexpr float ResultsSeconds = 12.0f;
	/** Local matches offer the rematch menu once the results have been revealed. */
	inline constexpr float ResultsRevealSeconds = 7.0f;
	/** The results' graph: everyone's points this often from GO. */
	inline constexpr float HistoryStepSeconds = 2.0f;
	/** Online results automatically return after twenty seconds of the podium/results screen. */
	inline constexpr float OnlineResultsReturnSeconds = 20.0f;
	/** Server grace after the client countdown (the podium starts 1.5 seconds after FINISH). */
	inline constexpr float ResultsViewLimitSeconds = OnlineResultsReturnSeconds + 3.5f;

	/** Each hit on an opponent scores; knocking them out scores this bonus on top. */
	inline constexpr int32 HitPoints = 1;
	inline constexpr int32 KnockoutBonusPoints = 1;

	/**
	 * A lone player always gets at least one CPU, and every team needs at least one member. With no players
	 * (everyone watching) at least two CPUs play.
	 */
	inline int32 GetMinCPUCount(const int32 Humans, const int32 TeamCount)
	{
		if (Humans <= 0)
		{
			return FMath::Clamp(FMath::Max(2, TeamCount >= 2 ? TeamCount : 0), 0, MaxCompetitors);
		}
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
		return FMath::Max(0, MaxCompetitors - FMath::Max(Humans, 0));
	}

	/** Players who actually compete under these rules (none when they watch). */
	inline int32 GetCompetingHumans(const FChaosImpactMatchRules& Rules, const int32 Humans)
	{
		return Rules.bSpectate ? 0 : FMath::Max(Humans, 1);
	}

	inline constexpr int32 CrashBallMinutes = 3;

	// ---- パーティルール
	inline constexpr uint8 PartyRuby = 1;
	inline constexpr uint8 PartyCrown = 2;
	/** ルビーラッシュ: a ball hit shakes this share of the rubies loose (at least one); a knockout half of them. */
	inline constexpr float RubyHitDropShare = 0.2f;
	/** クラウンキープ: counts to hold the crown for (30 seconds in all); each count takes a little more than a second. */
	inline constexpr int32 CrownGoalCounts = 24;
	inline constexpr float CrownCountSeconds = 1.25f;
	/** クラウンキープ is played over three or five minutes. */
	inline constexpr int32 CrownMinuteChoices[] = {3, 5};
	inline int32 SanitizeCrownMinutes(const int32 Minutes) { return Minutes >= 4 ? 5 : 3; }

	/** How many rubies a hit (or a knockout) shakes loose from Held. */
	inline int32 GetRubyDrop(const int32 Held, const bool bKnockout)
	{
		if (Held <= 0)
		{
			return 0;
		}
		return bKnockout ? (Held + 1) / 2 : FMath::Clamp(FMath::RoundToInt(Held * RubyHitDropShare), 1, Held);
	}
	/** Overtime: how long the trailing team may go without hitting the giant ball (the HUD drains it). */
	inline constexpr float CrashOvertimeGraceSeconds = 4.0f;

	inline FChaosImpactMatchRules Sanitize(FChaosImpactMatchRules Rules, int32 Humans)
	{
		if (Rules.bTournament)
		{
			// Every match a duel; the CPUs listed in the tournament's set-up (at most to 32 entrants in all).
			Rules.PartyRule = 0;
			Rules.bDuel = true;
			Rules.bBattleRoyale = false;
			Rules.bCrashBall = false;
			Rules.bSpectate = false;
			Rules.TeamCount = 0;
			Rules.Minutes = 3;
			Rules.CPULevel = SanitizeCPULevel(Rules.CPULevel);
			const int32 MaxCPUs = FMath::Max(0, ChaosImpactTournament::MaxEntrants - FMath::Max(Humans, 0));
			if (Rules.Tournament.CPUs.Num() > MaxCPUs)
			{
				Rules.Tournament.CPUs.SetNum(MaxCPUs);
			}
			for (FChaosImpactTournamentCPU& CPU : Rules.Tournament.CPUs)
			{
				CPU.Name = CPU.Name.Left(10);
				CPU.Level = CPU.Level < 0 ? INDEX_NONE : SanitizeCPULevel(CPU.Level);
			}
			Rules.CPUCount = Rules.Tournament.CPUs.Num();
			return Rules;
		}
		if (Rules.bBattleRoyale)
		{
			Rules.PartyRule = 0;
			Rules.bDuel = false;
			Rules.bCrashBall = false;
			Rules.Minutes = 2;
			Rules.StageIndex = SanitizeStage(Rules.StageIndex);
			Rules.CPULevel = SanitizeCPULevel(Rules.CPULevel);
			Humans = GetCompetingHumans(Rules, Humans);
			Rules.TeamCount = Rules.TeamCount >= 2 ? FMath::Clamp(Rules.TeamCount, 2, MaxTeams) : 0;
			if (Rules.TeamCount == 3 && Humans > 6) { Rules.TeamCount = 2; }
			Rules.CPUCount = FMath::Clamp(Rules.CPUCount, GetMinCPUCount(Humans, Rules.TeamCount), GetMaxCPUCount(Humans));
			if (Rules.TeamCount >= 2)
			{
				const int32 MaxTotal = MaxCompetitors / Rules.TeamCount * Rules.TeamCount;
				const int32 Total = FMath::Clamp(FMath::DivideAndRoundUp(Humans + Rules.CPUCount, Rules.TeamCount) * Rules.TeamCount, Rules.TeamCount, MaxTotal);
				Rules.CPUCount = Total - Humans;
			}
			return Rules;
		}
		// The duel is for one or two players (more cannot share it): otherwise it falls back to a free-for-all.
		Rules.bDuel = Rules.bDuel && GetCompetingHumans(Rules, Humans) <= 2;
		if (Rules.bDuel)
		{
			// Two players and nobody else (a lone player gets a CPU to face).
			Rules.PartyRule = 0;
			Rules.bCrashBall = false;
			Rules.bSpectate = false;
			Rules.TeamCount = 0;
			Rules.Minutes = 3;
			Rules.CPUCount = Humans >= 2 ? 0 : 1;
			Rules.CPULevel = SanitizeCPULevel(Rules.CPULevel);
			return Rules;
		}
		Humans = GetCompetingHumans(Rules, Humans);
		// The party rules are everyone for themselves (and never クラッシュボール).
		Rules.PartyRule = Rules.PartyRule == PartyRuby || Rules.PartyRule == PartyCrown ? Rules.PartyRule : 0;
		Rules.bCrashBall = Rules.bCrashBall && Rules.PartyRule == 0;
		Rules.Minutes = Rules.bCrashBall ? CrashBallMinutes : Rules.PartyRule == PartyCrown ? SanitizeCrownMinutes(Rules.Minutes)
			: SanitizeMinutes(Rules.Minutes);
		Rules.StageIndex = SanitizeStage(Rules.StageIndex);
		Rules.CPULevel = SanitizeCPULevel(Rules.CPULevel);
		Rules.TeamCount = Rules.bCrashBall ? 2 : Rules.PartyRule != 0 ? 0
			: (Rules.TeamCount >= 2 ? FMath::Clamp(Rules.TeamCount, 2, MaxTeams) : 0);
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

	inline FString DescribeRule(const FChaosImpactMatchRules& Rules)
	{
		if (Rules.bTournament)
		{
			return (Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? FString(TEXT("トーナメント  同時進行"))
				: FString(TEXT("トーナメント  1試合ずつ"))) + (Rules.Tournament.bPlacementMatches ? TEXT("・順位決定戦") : TEXT(""));
		}
		if (Rules.bBattleRoyale)
		{
			return Rules.IsTeamBattle() ? FString::Printf(TEXT("バトルロイヤル  %dチーム"), Rules.TeamCount) : FString(TEXT("バトルロイヤル  個人戦"));
		}
		if (Rules.bDuel)
		{
			return FString::Printf(TEXT("1対1  %d本先取"), DuelRoundsToWin);
		}
		if (Rules.IsRubyRush())
		{
			return TEXT("ルビーラッシュ  個人戦");
		}
		if (Rules.IsCrownKeep())
		{
			return TEXT("クラウンキープ  個人戦");
		}
		return Rules.bCrashBall ? FString(TEXT("クラッシュボール  2チーム")) : DescribeTeams(Rules.TeamCount);
	}

	/** URL options for a local match level; ReadOptions is the inverse. */
	inline FString ToOptions(const FChaosImpactMatchRules& Rules)
	{
		return FString::Printf(TEXT("CIMatch=1?CIMinutes=%d?CITeams=%d?CIMatchCPU=%d?CISpectate=%d?CIStage=%d?CICPULevel=%d?CICrashBall=%d?CIDuel=%d?CIBattleRoyale=%d?CIOffBalls=%d?CIParty=%d"),
			Rules.Minutes, Rules.TeamCount, Rules.CPUCount, Rules.bSpectate ? 1 : 0, Rules.StageIndex, Rules.CPULevel, Rules.bCrashBall ? 1 : 0,
			Rules.bDuel ? 1 : 0, Rules.bBattleRoyale ? 1 : 0, Rules.OffBalls, static_cast<int32>(Rules.PartyRule));
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
		OutRules.bSpectate = FCString::Atoi(URL.GetOption(TEXT("CISpectate="), TEXT("0"))) != 0;
		OutRules.StageIndex = SanitizeStage(FCString::Atoi(URL.GetOption(TEXT("CIStage="), TEXT("0"))));
		OutRules.CPULevel = SanitizeCPULevel(FCString::Atoi(URL.GetOption(TEXT("CICPULevel="), TEXT("2"))));
		OutRules.bCrashBall = FCString::Atoi(URL.GetOption(TEXT("CICrashBall="), TEXT("0"))) != 0;
		OutRules.bDuel = FCString::Atoi(URL.GetOption(TEXT("CIDuel="), TEXT("0"))) != 0;
		OutRules.bBattleRoyale = FCString::Atoi(URL.GetOption(TEXT("CIBattleRoyale="), TEXT("0"))) != 0;
		OutRules.OffBalls = FCString::Atoi(URL.GetOption(TEXT("CIOffBalls="), TEXT("0")));
		OutRules.PartyRule = static_cast<uint8>(FMath::Clamp(FCString::Atoi(URL.GetOption(TEXT("CIParty="), TEXT("0"))), 0, 2));
		if (OutRules.bCrashBall)
		{
			OutRules.TeamCount = 2;
			OutRules.Minutes = CrashBallMinutes;
		}
		return true;
	}
}
