#pragma once

#include "CoreMinimal.h"
#include "ChaosImpactTournament.generated.h"

class APlayerState;

/**
 * トーナメント: an online room's single-elimination tournament of 1v1 duels (the duel's rules: 3 HP a round, first to
 * two rounds, three special balls drawn for each match). 2 to 32 entrants, people (up to the room's 8) and CPUs.
 *
 * The whole tournament is played in the room's lobby world, without changing level: every match has its own duel
 * arena far from the others (AChaosImpactGameMode places them in a row), and every machine has its own practice
 * area. Whoever is not playing chooses, whenever they like, to practise, to watch any match being played, or to look
 * at the bracket; when their match comes up they are called into its arena.
 *
 * Two ways to run it (the room's host chooses):
 *  - one match at a time: everyone can watch the one match being played; a match between two CPUs can be watched or
 *    skipped (or all of them skipped);
 *  - all the round's matches at once, each in its own arena; matches between two CPUs are decided without being
 *    played.
 * A round starts once every match of the one before is over. Leaving (or losing the connection) loses the match
 * being played and any still to come; someone who comes into the room during a tournament can only watch.
 */

UENUM()
enum class EChaosImpactTournamentMode : uint8
{
	OneAtATime,
	Simultaneous
};

UENUM()
enum class EChaosImpactTournamentMatchState : uint8
{
	/** Its entrants are not both known yet. */
	Waiting,
	/** Both known; not started. */
	Ready,
	/** The players have been called: they are on their way to the arena (a few seconds). */
	Calling,
	/** Being played in its arena (or, two CPUs in a simultaneous round, being decided). */
	Live,
	Done
};

/** What a player who is not playing chose to do meanwhile. */
UENUM()
enum class EChaosImpactTournamentWait : uint8
{
	Practice,
	Spectate,
	Bracket
};

/** What a room member is doing in the tournament right now (on the player state). */
UENUM()
enum class EChaosImpactTournamentRole : uint8
{
	None,
	/** In an arena, playing their match. */
	Competitor,
	/** In their machine's practice area (or looking at the bracket from it). */
	Practice,
	/** Watching an arena through a spectator camera. */
	Spectator
};

/** One CPU entrant as the host set it up (a character or strength below zero: chosen at random). */
USTRUCT()
struct FChaosImpactTournamentCPU
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() int32 Character = INDEX_NONE;
	UPROPERTY() int32 Level = INDEX_NONE;
};

/** A tournament's set-up: how it runs and its CPU entrants (people are everyone in the room who plays). */
USTRUCT()
struct FChaosImpactTournamentSettings
{
	GENERATED_BODY()
	UPROPERTY() EChaosImpactTournamentMode Mode = EChaosImpactTournamentMode::OneAtATime;
	UPROPERTY() TArray<FChaosImpactTournamentCPU> CPUs;
	/** One match at a time: matches between two CPUs are skipped (decided) instead of played. */
	UPROPERTY() bool bSkipCPUMatches = false;
	/** 順位決定戦: those who lose play on, among themselves, for every place (3rd, 5th, 7th...). Off: they share the place they
	 * lost at (two 3rds, four 5ths...). */
	UPROPERTY() bool bPlacementMatches = false;
};

USTRUCT()
struct FChaosImpactTournamentEntrant
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() int32 Character = 0;
	UPROPERTY() int32 Colour = 0;
	UPROPERTY() TArray<uint8> CustomColours;
	UPROPERTY() bool bCPU = false;
	UPROPERTY() uint8 CPULevel = 2;
	/** The person's room member (null for a CPU, or once they left). */
	UPROPERTY() TObjectPtr<APlayerState> Member;
	/** Left the room (or lost the connection): every match still to come is lost. */
	UPROPERTY() bool bForfeited = false;
	/** 1 = the champion, 2 the final's loser, 3 the semi-finals' losers, 5 ... (0 while still in). */
	UPROPERTY() int32 Place = 0;
};

USTRUCT()
struct FChaosImpactTournamentMatch
{
	GENERATED_BODY()
	/** 1-based round, and the match's place in it from the top of the bracket. */
	UPROPERTY() int32 Round = 1;
	UPROPERTY() int32 Slot = 0;
	/** Entrant indices of side 0 and side 1 (INDEX_NONE: nobody, a bye, or not known yet). */
	UPROPERTY() int32 Entrants[2] = {INDEX_NONE, INDEX_NONE};
	UPROPERTY() EChaosImpactTournamentMatchState State = EChaosImpactTournamentMatchState::Waiting;
	/** The winner's side once Done (INDEX_NONE: nobody went through, both sides empty). */
	UPROPERTY() int32 Winner = INDEX_NONE;
	UPROPERTY() int32 Wins[2] = {0, 0};
	/** The arena it is played in while Calling or Live (INDEX_NONE: decided without one). */
	UPROPERTY() int32 Arena = INDEX_NONE;
	/** Won because the other side left; won without playing (nobody on the other side); decided without playing. */
	UPROPERTY() bool bForfeit = false;
	UPROPERTY() bool bBye = false;
	UPROPERTY() bool bDecided = false;
	/** Server time it was called and decided. */
	UPROPERTY() double CalledAt = 0.0;
	/** Which part of the bracket it belongs to (0: the tournament itself; others: matches for the places below), and
	 * its round within that part. Slot is its place in that round of that part. */
	UPROPERTY() int32 Group = 0;
	UPROPERTY() int32 GroupRound = 1;
	UPROPERTY() double EndedAt = 0.0;

	bool IsOver() const { return State == EChaosImpactTournamentMatchState::Done; }
	bool IsPlaying() const { return State == EChaosImpactTournamentMatchState::Calling || State == EChaosImpactTournamentMatchState::Live; }
	int32 WinnerEntrant() const { return Winner == 0 || Winner == 1 ? Entrants[Winner] : INDEX_NONE; }
	int32 LoserEntrant() const { return Winner == 0 || Winner == 1 ? Entrants[1 - Winner] : INDEX_NONE; }
	int32 SideOf(const int32 Entrant) const { return Entrant == INDEX_NONE ? INDEX_NONE : Entrants[0] == Entrant ? 0 : Entrants[1] == Entrant ? 1 : INDEX_NONE; }
};

/** The whole tournament, replicated on the game state. */
/**
 * A part of the bracket, itself a knockout: the tournament (places 1 to BracketSize), or, with 順位決定戦, the losers of a
 * round of another part playing for the places between (3rd place: the semi-finals' losers; 5th to 8th: the
 * quarter-finals' losers, whose own losers then play for 7th...).
 */
USTRUCT()
struct FChaosImpactTournamentGroup
{
	GENERATED_BODY()
	/** The best place it decides, and how many places (a power of two). */
	UPROPERTY() int32 TopPlace = 1;
	UPROPERTY() int32 Size = 2;
	/** The tournament's round its first round is played in, and its first match in Matches. */
	UPROPERTY() int32 StartRound = 1;
	UPROPERTY() int32 FirstMatch = 0;
};

USTRUCT()
struct FChaosImpactTournamentState
{
	GENERATED_BODY()
	UPROPERTY() bool bActive = false;
	/** The champion is decided (the tournament stays on show until the room goes back to its lobby). */
	UPROPERTY() bool bFinished = false;
	UPROPERTY() FChaosImpactTournamentSettings Settings;
	UPROPERTY() FString Title;
	/** Entrants in seed order. */
	UPROPERTY() TArray<FChaosImpactTournamentEntrant> Entrants;
	/** Every match of the bracket, round 1 first (BracketSize - 1 of them). */
	UPROPERTY() TArray<FChaosImpactTournamentMatch> Matches;
	/** The parts of the bracket: [0] the tournament itself, then (順位決定戦) those for the places below. */
	UPROPERTY() TArray<FChaosImpactTournamentGroup> Groups;
	/** A power of two: the first round's places (empty places are byes). */
	UPROPERTY() int32 BracketSize = 2;
	UPROPERTY() int32 Rounds = 1;
	/** The round being played (1-based; 0 before the first). */
	UPROPERTY() int32 Round = 0;
	UPROPERTY() int32 Champion = INDEX_NONE;
	/** Server times: the start, the current round's start, the end. */
	UPROPERTY() double StartedAt = 0.0;
	UPROPERTY() double RoundStartedAt = 0.0;
	UPROPERTY() double FinishedAt = 0.0;
	/** One match at a time, two CPUs next: the host chooses to watch or skip it (the match waiting for that). */
	UPROPERTY() int32 AskingMatch = INDEX_NONE;

	bool IsSimultaneous() const { return Settings.Mode == EChaosImpactTournamentMode::Simultaneous; }
	int32 MatchesInRound(const int32 InRound) const { return BracketSize >> FMath::Clamp(InRound, 1, 6); }
	/** Index into Matches of a round's match (INDEX_NONE past the bracket). */
	int32 MatchIndex(int32 InRound, int32 Slot) const;
	/** The entrant's match in a round (INDEX_NONE if they are not in it). */
	int32 FindMatch(int32 Entrant, int32 InRound) const;
	/** The match the entrant plays next or is playing (INDEX_NONE once they are out or the tournament is over). */
	int32 FindCurrentMatch(int32 Entrant) const;
	int32 FindEntrant(const APlayerState* Member) const;
	/** The round's name as shown: 決勝, 準決勝, 準々決勝, else N回戦. */
	FString GetRoundName(int32 InRound) const;
	/** A match's name as shown: the round's (決勝, 準決勝...), or the places it is for (3位決定戦, 5〜8位 準決勝...). */
	FString GetMatchName(int32 InMatch) const;
	/** Index into Matches of a part's match (INDEX_NONE when there is none). */
	int32 GroupMatchIndex(int32 InGroup, int32 InGroupRound, int32 Slot) const;
	/** The part deciding these places (INDEX_NONE when there is none). */
	int32 FindGroup(int32 TopPlace, int32 Size) const;
	bool IsEntrantOut(int32 Entrant) const;
};

namespace ChaosImpactTournament
{
	inline constexpr int32 MinEntrants = 2;
	inline constexpr int32 MaxEntrants = 32;
	/** How far apart the arenas stand (and the practice areas). */
	inline constexpr float ArenaSpacing = 9000.0f;
	inline constexpr float PracticeSpacing = 11000.0f;
	/** Each match's opening in its arena (the round's name, how it is won) before the ball slot. */
	/** (The duel opening's flyover and dive, ChaosImpactMatch::FlyoverSeconds + DiveSeconds: checked where both are seen.) */
	inline constexpr float OpeningSeconds = 4.4f + 1.3f;
	/** Called players arrive in their arena after this long (the call is shown meanwhile). */
	inline constexpr float CallSeconds = 3.5f;
	/** The winner's banner in the arena before everyone goes back. */
	inline constexpr float MatchOverSeconds = 5.0f;
	/** Between rounds: the bracket's advance is shown. */
	inline constexpr float RoundBreakSeconds = 6.0f;
	/** The champion's moment before the room goes back to its lobby. */
	inline constexpr float ChampionSeconds = 18.0f;
	/** One match at a time: how long the host has to choose to watch or skip two CPUs (watching if nothing chosen). */
	inline constexpr float AskSeconds = 10.0f;

	/** The bracket for Count entrants: the first round's places, a power of two (at least 2). */
	int32 BracketSizeFor(int32 Count);
	/** Seeds in bracket order (1-based): the top seeds meet the byes, and the best two only in the final. */
	TArray<int32> SeedOrder(int32 BracketSize);
	/** A new tournament for these entrants (already in seed order): every match laid out, the byes decided. */
	FChaosImpactTournamentState Build(TArray<FChaosImpactTournamentEntrant> Entrants, const FChaosImpactTournamentSettings& Settings,
		const FString& Title);
	/**
	 * A match decided: its winner goes through to the next round's match (whose side follows from its slot), losers get
	 * their place; a next match left with nobody against someone who went through is decided for them, and so on.
	 * Returns true when this decided the champion.
	 */
	bool Advance(FChaosImpactTournamentState& State, int32 MatchIndex, double Now);
	/** The place of a loser in round InRound of a tournament with Rounds rounds: 2, 3, 5, 9, 17. */
	int32 PlaceForLoss(int32 Rounds, int32 InRound);
	/** Two CPUs, decided without playing: the stronger one more often, by rounds won like a played match. */
	void DecideCPUMatch(FChaosImpactTournamentState& State, int32 MatchIndex, FRandomStream& Random);
}
