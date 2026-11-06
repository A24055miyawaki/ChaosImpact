#include "ChaosImpactTournament.h"

#include "GameFramework/PlayerState.h"

int32 FChaosImpactTournamentState::MatchIndex(const int32 InRound, const int32 Slot) const
{
	if (InRound < 1 || InRound > Rounds || Slot < 0 || Slot >= MatchesInRound(InRound))
	{
		return INDEX_NONE;
	}
	int32 Index = 0;
	for (int32 Earlier = 1; Earlier < InRound; ++Earlier)
	{
		Index += MatchesInRound(Earlier);
	}
	return Index + Slot;
}

int32 FChaosImpactTournamentState::FindMatch(const int32 Entrant, const int32 InRound) const
{
	for (int32 Index = 0; Index < Matches.Num(); ++Index)
	{
		if (Matches[Index].Round == InRound && Matches[Index].SideOf(Entrant) != INDEX_NONE)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FChaosImpactTournamentState::FindCurrentMatch(const int32 Entrant) const
{
	if (!Entrants.IsValidIndex(Entrant) || bFinished || IsEntrantOut(Entrant))
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Matches.Num(); ++Index)
	{
		const FChaosImpactTournamentMatch& Match = Matches[Index];
		if (!Match.IsOver() && Match.SideOf(Entrant) != INDEX_NONE)
		{
			return Index;
		}
	}
	// Through to a match whose other side is not known yet.
	for (int32 InRound = 1; InRound <= Rounds; ++InRound)
	{
		const int32 Index = FindMatch(Entrant, InRound);
		if (Index != INDEX_NONE && !Matches[Index].IsOver())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FChaosImpactTournamentState::FindEntrant(const APlayerState* Member) const
{
	if (!Member)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Entrants.Num(); ++Index)
	{
		if (Entrants[Index].Member == Member)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FString FChaosImpactTournamentState::GetRoundName(const int32 InRound) const
{
	const int32 FromEnd = Rounds - InRound;
	return FromEnd == 0 ? FString(TEXT("決勝")) : FromEnd == 1 ? FString(TEXT("準決勝")) : FromEnd == 2 ? FString(TEXT("準々決勝"))
		: FString::Printf(TEXT("%d回戦"), InRound);
}

bool FChaosImpactTournamentState::IsEntrantOut(const int32 Entrant) const
{
	return !Entrants.IsValidIndex(Entrant) || Entrants[Entrant].Place > 1 || Entrants[Entrant].bForfeited;
}

int32 ChaosImpactTournament::BracketSizeFor(const int32 Count)
{
	int32 Size = 2;
	while (Size < Count && Size < MaxEntrants)
	{
		Size *= 2;
	}
	return Size;
}

TArray<int32> ChaosImpactTournament::SeedOrder(const int32 BracketSize)
{
	TArray<int32> Order = {1, 2};
	while (Order.Num() < BracketSize)
	{
		const int32 Next = Order.Num() * 2;
		TArray<int32> Wider;
		for (const int32 Seed : Order)
		{
			Wider.Add(Seed);
			Wider.Add(Next + 1 - Seed);
		}
		Order = MoveTemp(Wider);
	}
	return Order;
}

int32 ChaosImpactTournament::PlaceForLoss(const int32 Rounds, const int32 InRound)
{
	return (1 << FMath::Max(Rounds - InRound, 0)) + 1;
}

FChaosImpactTournamentState ChaosImpactTournament::Build(TArray<FChaosImpactTournamentEntrant> Entrants,
	const FChaosImpactTournamentSettings& Settings, const FString& Title)
{
	FChaosImpactTournamentState State;
	if (Entrants.Num() > MaxEntrants)
	{
		Entrants.SetNum(MaxEntrants);
	}
	State.bActive = true;
	State.Settings = Settings;
	State.Title = Title;
	State.Entrants = MoveTemp(Entrants);
	State.BracketSize = BracketSizeFor(State.Entrants.Num());
	State.Rounds = FMath::RoundToInt(FMath::Log2(static_cast<float>(State.BracketSize)));
	for (int32 InRound = 1; InRound <= State.Rounds; ++InRound)
	{
		for (int32 Slot = 0; Slot < State.MatchesInRound(InRound); ++Slot)
		{
			FChaosImpactTournamentMatch Match;
			Match.Round = InRound;
			Match.Slot = Slot;
			State.Matches.Add(Match);
		}
	}
	// The first round from the seed order: seeds past the entrants are byes (each meets one of the top seeds).
	const TArray<int32> Order = SeedOrder(State.BracketSize);
	for (int32 Slot = 0; Slot < State.MatchesInRound(1); ++Slot)
	{
		FChaosImpactTournamentMatch& Match = State.Matches[Slot];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Seed = Order[Slot * 2 + Side];
			Match.Entrants[Side] = Seed <= State.Entrants.Num() ? Seed - 1 : INDEX_NONE;
		}
		Match.State = EChaosImpactTournamentMatchState::Ready;
	}
	return State;
}

bool ChaosImpactTournament::Advance(FChaosImpactTournamentState& State, const int32 MatchIndex, const double Now)
{
	if (!State.Matches.IsValidIndex(MatchIndex))
	{
		return false;
	}
	FChaosImpactTournamentMatch& Match = State.Matches[MatchIndex];
	Match.State = EChaosImpactTournamentMatchState::Done;
	Match.Arena = INDEX_NONE;
	Match.EndedAt = Now;
	const int32 Loser = Match.LoserEntrant();
	if (State.Entrants.IsValidIndex(Loser) && State.Entrants[Loser].Place == 0)
	{
		State.Entrants[Loser].Place = PlaceForLoss(State.Rounds, Match.Round);
	}
	const int32 Winner = Match.WinnerEntrant();
	if (Match.Round >= State.Rounds)
	{
		State.Champion = Winner;
		if (State.Entrants.IsValidIndex(Winner))
		{
			State.Entrants[Winner].Place = 1;
		}
		State.bFinished = true;
		State.FinishedAt = Now;
		return true;
	}
	const int32 NextIndex = State.MatchIndex(Match.Round + 1, Match.Slot / 2);
	if (!State.Matches.IsValidIndex(NextIndex))
	{
		return false;
	}
	FChaosImpactTournamentMatch& Next = State.Matches[NextIndex];
	Next.Entrants[Match.Slot % 2] = Winner;
	// Both of its feeders decided: it can be played (or, with nobody on one side, decided).
	const int32 Feeder0 = State.MatchIndex(Match.Round, (Match.Slot / 2) * 2);
	const int32 Feeder1 = State.MatchIndex(Match.Round, (Match.Slot / 2) * 2 + 1);
	if (State.Matches.IsValidIndex(Feeder0) && State.Matches[Feeder0].IsOver() && State.Matches.IsValidIndex(Feeder1)
		&& State.Matches[Feeder1].IsOver())
	{
		Next.State = EChaosImpactTournamentMatchState::Ready;
	}
	return false;
}

void ChaosImpactTournament::DecideCPUMatch(FChaosImpactTournamentState& State, const int32 MatchIndex, FRandomStream& Random)
{
	if (!State.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	FChaosImpactTournamentMatch& Match = State.Matches[MatchIndex];
	const auto Strength = [&State](const int32 Entrant)
	{
		return State.Entrants.IsValidIndex(Entrant) ? 1.0f + State.Entrants[Entrant].CPULevel : 0.0f;
	};
	const float A = Strength(Match.Entrants[0]);
	const float B = Strength(Match.Entrants[1]);
	// Round by round, the stronger one taking each a little more often.
	const float ChanceA = A + B > 0.0f ? 0.5f + 0.18f * (A - B) / FMath::Max(A + B, 1.0f) * 2.0f : 0.5f;
	Match.Wins[0] = Match.Wins[1] = 0;
	while (Match.Wins[0] < 2 && Match.Wins[1] < 2)
	{
		++Match.Wins[Random.FRand() < ChanceA ? 0 : 1];
	}
	Match.Winner = Match.Wins[0] > Match.Wins[1] ? 0 : 1;
	Match.bDecided = true;
}
