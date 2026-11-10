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

int32 FChaosImpactTournamentState::GroupMatchIndex(const int32 InGroup, const int32 InGroupRound, const int32 Slot) const
{
	if (!Groups.IsValidIndex(InGroup))
	{
		return InGroup == 0 ? MatchIndex(InGroupRound, Slot) : INDEX_NONE;
	}
	const FChaosImpactTournamentGroup& Part = Groups[InGroup];
	const int32 PartRounds = static_cast<int32>(FMath::FloorLog2(FMath::Max(Part.Size, 2)));
	if (InGroupRound < 1 || InGroupRound > PartRounds || Slot < 0 || Slot >= (Part.Size >> InGroupRound))
	{
		return INDEX_NONE;
	}
	int32 Index = Part.FirstMatch;
	for (int32 Earlier = 1; Earlier < InGroupRound; ++Earlier)
	{
		Index += Part.Size >> Earlier;
	}
	Index += Slot;
	return Matches.IsValidIndex(Index) ? Index : INDEX_NONE;
}

int32 FChaosImpactTournamentState::FindGroup(const int32 TopPlace, const int32 Size) const
{
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		if (Groups[Index].TopPlace == TopPlace && Groups[Index].Size == Size)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FString FChaosImpactTournamentState::GetMatchName(const int32 InMatch) const
{
	if (!Matches.IsValidIndex(InMatch))
	{
		return FString();
	}
	const FChaosImpactTournamentMatch& Match = Matches[InMatch];
	if (Match.Group <= 0 || !Groups.IsValidIndex(Match.Group))
	{
		return GetRoundName(Match.Round);
	}
	const FChaosImpactTournamentGroup& Part = Groups[Match.Group];
	const int32 FromEnd = static_cast<int32>(FMath::FloorLog2(FMath::Max(Part.Size, 2))) - Match.GroupRound;
	if (FromEnd <= 0)
	{
		return FString::Printf(TEXT("%d位決定戦"), Part.TopPlace);
	}
	return FString::Printf(TEXT("%d〜%d位 %s"), Part.TopPlace, Part.TopPlace + Part.Size - 1,
		FromEnd == 1 ? TEXT("準決勝") : FromEnd == 2 ? TEXT("準々決勝") : *FString::Printf(TEXT("%d回戦"), Match.GroupRound));
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
			Match.Group = 0;
			Match.GroupRound = InRound;
			State.Matches.Add(Match);
		}
	}
	{
		FChaosImpactTournamentGroup Main;
		Main.TopPlace = 1;
		Main.Size = State.BracketSize;
		Main.StartRound = 1;
		Main.FirstMatch = 0;
		State.Groups.Add(Main);
	}
	// 順位決定戦: the losers of each round of each part play on for the places between, the round after, as a knockout
	// of their own (whose losers do the same). Every part ends in the tournament's last round.
	if (Settings.bPlacementMatches)
	{
		for (int32 Parent = 0; Parent < State.Groups.Num(); ++Parent)
		{
			const FChaosImpactTournamentGroup Part = State.Groups[Parent];
			const int32 PartRounds = static_cast<int32>(FMath::FloorLog2(FMath::Max(Part.Size, 2)));
			for (int32 PartRound = 1; PartRound <= PartRounds; ++PartRound)
			{
				const int32 Losers = Part.Size >> PartRound;
				if (Losers < 2)
				{
					continue;
				}
				FChaosImpactTournamentGroup Below;
				Below.TopPlace = Part.TopPlace + Losers;
				Below.Size = Losers;
				Below.StartRound = Part.StartRound + PartRound;
				Below.FirstMatch = State.Matches.Num();
				const int32 BelowIndex = State.Groups.Add(Below);
				for (int32 BelowRound = 1; BelowRound <= static_cast<int32>(FMath::FloorLog2(Losers)); ++BelowRound)
				{
					for (int32 Slot = 0; Slot < (Losers >> BelowRound); ++Slot)
					{
						FChaosImpactTournamentMatch Match;
						Match.Round = Below.StartRound + BelowRound - 1;
						Match.Slot = Slot;
						Match.Group = BelowIndex;
						Match.GroupRound = BelowRound;
						State.Matches.Add(Match);
					}
				}
			}
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
	const int32 Winner = Match.WinnerEntrant();
	FChaosImpactTournamentGroup Part;
	Part.Size = State.BracketSize;
	if (State.Groups.IsValidIndex(Match.Group))
	{
		Part = State.Groups[Match.Group];
	}
	const int32 PartRounds = static_cast<int32>(FMath::FloorLog2(FMath::Max(Part.Size, 2)));
	// Its two feeders (this match and its neighbour) both decided: the match they feed can be played (or, with nobody on
	// one side, decided).
	const auto MarkReady = [&State, &Match](FChaosImpactTournamentMatch& Into)
	{
		const int32 Feeder0 = State.GroupMatchIndex(Match.Group, Match.GroupRound, (Match.Slot / 2) * 2);
		const int32 Feeder1 = State.GroupMatchIndex(Match.Group, Match.GroupRound, (Match.Slot / 2) * 2 + 1);
		if (State.Matches.IsValidIndex(Feeder0) && State.Matches[Feeder0].IsOver() && State.Matches.IsValidIndex(Feeder1)
			&& State.Matches[Feeder1].IsOver())
		{
			Into.State = EChaosImpactTournamentMatchState::Ready;
		}
	};
	// The loser: on to the matches for the places below (順位決定戦), or the place lost at.
	{
		const int32 Losers = Part.Size >> Match.GroupRound;
		const int32 Below = State.Settings.bPlacementMatches && Losers >= 2 ? State.FindGroup(Part.TopPlace + Losers, Losers) : INDEX_NONE;
		const int32 Into = Below != INDEX_NONE ? State.GroupMatchIndex(Below, 1, Match.Slot / 2) : INDEX_NONE;
		if (State.Matches.IsValidIndex(Into))
		{
			State.Matches[Into].Entrants[Match.Slot % 2] = Loser;
			MarkReady(State.Matches[Into]);
		}
		else if (State.Entrants.IsValidIndex(Loser) && State.Entrants[Loser].Place == 0)
		{
			// (Without 順位決定戦 the same as PlaceForLoss: 2nd, 3rd, 5th, 9th...)
			State.Entrants[Loser].Place = Part.TopPlace + Losers;
		}
	}
	// The winner: on to the part's next round, or, its last, the part's best place (the tournament's: the champion).
	if (Match.GroupRound >= PartRounds)
	{
		if (State.Entrants.IsValidIndex(Winner) && (State.Entrants[Winner].Place == 0 || Match.Group == 0))
		{
			State.Entrants[Winner].Place = Part.TopPlace;
		}
		if (Match.Group == 0)
		{
			State.Champion = Winner;
		}
	}
	else
	{
		const int32 NextIndex = State.GroupMatchIndex(Match.Group, Match.GroupRound + 1, Match.Slot / 2);
		if (State.Matches.IsValidIndex(NextIndex))
		{
			State.Matches[NextIndex].Entrants[Match.Slot % 2] = Winner;
			MarkReady(State.Matches[NextIndex]);
		}
	}
	// Over once every match is (the final, and every place's).
	for (const FChaosImpactTournamentMatch& Each : State.Matches)
	{
		if (!Each.IsOver())
		{
			return false;
		}
	}
	State.bFinished = true;
	State.FinishedAt = Now;
	return true;
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
