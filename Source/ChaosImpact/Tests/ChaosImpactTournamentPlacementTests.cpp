#include "ChaosImpactTournament.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Plays a whole bracket the way the game mode does, round by round (byes and empty sides decided at a round's start). */
	bool TnPlayThrough(FAutomationTestBase& Test, const int32 Count, const bool bPlacement, const int32 Seed)
	{
		TArray<FChaosImpactTournamentEntrant> Entrants;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FChaosImpactTournamentEntrant Entrant;
			Entrant.Name = FString::Printf(TEXT("E%d"), Index + 1);
			Entrant.bCPU = true;
			Entrants.Add(Entrant);
		}
		FChaosImpactTournamentSettings Settings;
		Settings.bPlacementMatches = bPlacement;
		FChaosImpactTournamentState State = ChaosImpactTournament::Build(Entrants, Settings, TEXT("Test"));
		FRandomStream Random(Seed);
		double Now = 0.0;
		for (int32 Round = 1; Round <= State.Rounds && !State.bFinished; ++Round)
		{
			for (int32 Index = 0; Index < State.Matches.Num() && !State.bFinished; ++Index)
			{
				FChaosImpactTournamentMatch& Match = State.Matches[Index];
				if (Match.Round != Round || Match.IsOver())
				{
					continue;
				}
				const bool bA = State.Entrants.IsValidIndex(Match.Entrants[0]);
				const bool bB = State.Entrants.IsValidIndex(Match.Entrants[1]);
				Match.Winner = bA && bB ? Random.RandRange(0, 1) : bA ? 0 : bB ? 1 : INDEX_NONE;
				Match.bBye = !(bA && bB);
				ChaosImpactTournament::Advance(State, Index, Now += 1.0);
			}
		}
		if (!Test.TestTrue(FString::Printf(TEXT("%d entrants (placement %d): finished"), Count, bPlacement), State.bFinished))
		{
			return false;
		}
		TSet<int32> Places;
		bool bAllPlaced = true;
		bool bUnique = true;
		for (const FChaosImpactTournamentEntrant& Entrant : State.Entrants)
		{
			bAllPlaced &= Entrant.Place > 0;
			if (bPlacement)
			{
				bUnique &= !Places.Contains(Entrant.Place);
				Places.Add(Entrant.Place);
			}
		}
		Test.TestTrue(FString::Printf(TEXT("%d entrants (placement %d): everyone placed"), Count, bPlacement), bAllPlaced);
		Test.TestTrue(FString::Printf(TEXT("%d entrants (placement %d): champion is 1st"), Count, bPlacement),
			State.Entrants.IsValidIndex(State.Champion) && State.Entrants[State.Champion].Place == 1);
		if (bPlacement)
		{
			Test.TestTrue(FString::Printf(TEXT("%d entrants: every place different"), Count), bUnique);
			// A full bracket: exactly the places 1..N.
			if (FMath::IsPowerOfTwo(Count))
			{
				bool bExact = true;
				for (int32 Place = 1; Place <= Count; ++Place) { bExact &= Places.Contains(Place); }
				Test.TestTrue(FString::Printf(TEXT("%d entrants: places 1 to %d"), Count, Count), bExact);
			}
			// Every match ends in the tournament's last round at the latest.
			for (const FChaosImpactTournamentMatch& Match : State.Matches)
			{
				Test.TestTrue(TEXT("no match after the last round"), Match.Round <= State.Rounds);
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTournamentPlacementTest, "ChaosImpact.Tournament.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactTournamentPlacementTest::RunTest(const FString&)
{
	for (int32 Count = 2; Count <= 32; ++Count)
	{
		for (int32 Seed = 1; Seed <= 4; ++Seed)
		{
			TnPlayThrough(*this, Count, true, Seed * 31 + Count);
			TnPlayThrough(*this, Count, false, Seed * 17 + Count);
		}
	}
	// The names of the places' matches.
	TArray<FChaosImpactTournamentEntrant> Eight;
	Eight.SetNum(8);
	FChaosImpactTournamentSettings Settings;
	Settings.bPlacementMatches = true;
	const FChaosImpactTournamentState State = ChaosImpactTournament::Build(Eight, Settings, TEXT("Names"));
	TArray<FString> Names;
	for (int32 Index = 0; Index < State.Matches.Num(); ++Index)
	{
		Names.Add(FString::Printf(TEXT("R%d:%s"), State.Matches[Index].Round, *State.GetMatchName(Index)));
	}
	AddInfo(FString::Printf(TEXT("8 entrants: %d matches: %s"), State.Matches.Num(), *FString::Join(Names, TEXT(", "))));
	TestEqual(TEXT("8 entrants with placement: 12 matches"), State.Matches.Num(), 12);
	return true;
}

#endif
