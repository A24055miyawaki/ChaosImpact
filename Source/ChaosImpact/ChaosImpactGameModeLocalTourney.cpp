// ローカルトーナメント: the game mode's side (see ChaosImpactLocalTourney.h for how it is played).

#include "ChaosImpactGameMode.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactLocalTourney.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "JoyShockBlueprintLibrary.h"
#include "JoyShockTypes.h"
#include "TimerManager.h"

namespace
{
	TArray<AChaosImpactPlayerController*> LtLocalControllers(const UWorld* World)
	{
		TArray<AChaosImpactPlayerController*> Controllers;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return Controllers;
		}
		for (const ULocalPlayer* Local : GameInstance->GetLocalPlayers())
		{
			if (AChaosImpactPlayerController* Controller = Local ? Cast<AChaosImpactPlayerController>(Local->GetPlayerController(World)) : nullptr)
			{
				Controllers.Add(Controller);
			}
		}
		return Controllers;
	}

	void LtDress(AChaosImpactPlayerState* State, const FChaosImpactTournamentEntrant& Who)
	{
		State->SetPlayerName(Who.Name);
		State->CharacterIndex = ChaosImpactRoster::ClampIndex(Who.Character);
		State->ColourChoice = Who.Colour;
		State->CustomColours = Who.CustomColours;
		State->bSpectating = false;
		State->TournamentWait = EChaosImpactTournamentWait::Bracket;
		State->ForceNetUpdate();
	}
}

void AChaosImpactGameMode::BeginLocalTournament()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	const UChaosImpactLocalTourneySubsystem* Data = UChaosImpactLocalTourneySubsystem::Get(this);
	if (!Match || !Data || Match->IsTournament() || !GetWorld())
	{
		return;
	}
	Match->bLocalTournament = true;
	FChaosImpactMatchRules Rules;
	Rules.bTournament = true;
	Rules.bDuel = true;
	Rules.Tournament = Data->MakeSettings();
	Match->Rules = Rules;
	TArray<FChaosImpactTournamentEntrant> People = Data->MakeEntrants();
	const TArray<AChaosImpactPlayerController*> Locals = LtLocalControllers(GetWorld());
	if (Locals.IsEmpty())
	{
		return;
	}
	// The first player is the one in charge (the CPU matches' watch or skip, as a room's host).
	if (AChaosImpactPlayerState* First = Locals[0]->GetPlayerState<AChaosImpactPlayerState>())
	{
		First->bRoomHost = true;
	}
	if (Data->IsAllAtOnce())
	{
		// 同時進行: this machine's players are the people, each with their own controller, for the whole tournament.
		for (int32 Index = 0; Index < Locals.Num(); ++Index)
		{
			if (AChaosImpactPlayerState* State = Locals[Index]->GetPlayerState<AChaosImpactPlayerState>())
			{
				if (People.IsValidIndex(Index))
				{
					LtDress(State, People[Index]);
				}
				else
				{
					State->bSpectating = true;
				}
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Local tournament (all at once): %d people on %d local players"), People.Num(), Locals.Num());
		StartTournament();
		return;
	}
	// 1試合ずつ: the people are nobody's yet; this machine has one player, watching, until a match seats its people.
	if (AChaosImpactPlayerState* First = Locals[0]->GetPlayerState<AChaosImpactPlayerState>())
	{
		First->bSpectating = true;
		First->SetPlayerName(TEXT("観戦"));
	}
	bUseLocalSeatDevices = true;
	LocalSeatKeyboard = 0;
	LocalSeatPadDevices.Reset();
	UE_LOG(LogChaosImpact, Log, TEXT("Local tournament (one match at a time): %d people"), People.Num());
	StartTournament(&People);
}

bool AChaosImpactGameMode::IsLocalSeatPending(const int32 MatchIndex) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bLocalTournament || Match->Tournament.IsSimultaneous() || MatchIndex == LocalSeatedMatch
		|| !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return false;
	}
	const FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentMatch& Each = Tournament.Matches[MatchIndex];
	if (Each.State != EChaosImpactTournamentMatchState::Calling)
	{
		return false;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Tournament.Entrants.IsValidIndex(Each.Entrants[Side]) && !Tournament.Entrants[Each.Entrants[Side]].bCPU)
		{
			return true;
		}
	}
	return false;
}

int32 AChaosImpactGameMode::GetLocalSeatRequest() const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->bLocalTournament || !Match->IsTournament())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Match->Tournament.Matches.Num(); ++Index)
	{
		if (IsLocalSeatPending(Index))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void AChaosImpactGameMode::SeatLocalTournamentMatch(const int32 MatchIndex, const TArray<int32>& SideDevices)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !IsLocalSeatPending(MatchIndex) || !GetWorld())
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	const FChaosImpactTournamentMatch& Seated = Tournament.Matches[MatchIndex];
	// The sides with people, in order: this machine's players 1 (and 2).
	TArray<int32> Sides;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Tournament.Entrants.IsValidIndex(Seated.Entrants[Side]) && !Tournament.Entrants[Seated.Entrants[Side]].bCPU)
		{
			Sides.Add(Side);
		}
	}
	int32 Keyboard = INDEX_NONE;
	TArray<int32> Pads;
	for (int32 Seat = 0; Seat < Sides.Num(); ++Seat)
	{
		const int32 Device = SideDevices.IsValidIndex(Sides[Seat]) ? SideDevices[Sides[Seat]] : INDEX_NONE;
		if (Device == -1)
		{
			Keyboard = Seat;
		}
		else if (Device >= 0)
		{
			Pads.Add(Device);
		}
	}
	bUseLocalSeatDevices = true;
	LocalSeatPadDevices = Pads;
	LocalSeatKeyboard = Keyboard;
	SetTrainingLocalPlayerCountLive(Sides.Num());
	const TArray<AChaosImpactPlayerController*> Locals = LtLocalControllers(GetWorld());
	for (int32 Seat = 0; Seat < Sides.Num() && Seat < Locals.Num(); ++Seat)
	{
		const int32 Entrant = Seated.Entrants[Sides[Seat]];
		if (AChaosImpactPlayerState* State = Locals[Seat]->GetPlayerState<AChaosImpactPlayerState>())
		{
			LtDress(State, Tournament.Entrants[Entrant]);
			Tournament.Entrants[Entrant].Member = State;
		}
	}
	if (!Locals.IsEmpty())
	{
		Locals[0]->SetLocalInputRouting(Sides.Num(), Keyboard, Pads);
	}
	LocalSeatedMatch = MatchIndex;
	UE_LOG(LogChaosImpact, Log, TEXT("Local tournament: %s match %d seated (%d player(s), keyboard %d, %d pad(s))"),
		*Tournament.GetRoundName(Seated.Round), Seated.Slot + 1, Sides.Num(), Keyboard, Pads.Num());
	BeginTournamentMatch(MatchIndex);
}

void AChaosImpactGameMode::UnseatLocalTournament()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return;
	}
	// Nobody plays anyone any more (before the second player goes, or their leaving would count as a walk-over).
	const TArray<AChaosImpactPlayerController*> Before = LtLocalControllers(GetWorld());
	for (FChaosImpactTournamentEntrant& Entrant : Match->Tournament.Entrants)
	{
		for (const AChaosImpactPlayerController* Local : Before)
		{
			if (Entrant.Member == Local->PlayerState)
			{
				Entrant.Member = nullptr;
			}
		}
	}
	LocalSeatedMatch = INDEX_NONE;
	bUseLocalSeatDevices = true;
	LocalSeatPadDevices.Reset();
	LocalSeatKeyboard = 0;
	SetTrainingLocalPlayerCountLive(1);
	const TArray<AChaosImpactPlayerController*> Locals = LtLocalControllers(GetWorld());
	if (Locals.IsEmpty())
	{
		return;
	}
	// The one left watches, worked by the keyboard and every pad.
	for (const FJSL4UControllerInfo& Pad : UJoyShockLibrary::JSL4UGetAllConnectedControllers())
	{
		UJoyShockLibrary::JSL4UAssignControllerToPlayer(Pad, Locals[0]);
	}
	Locals[0]->SetLocalInputRouting(1, 0, TArray<int32>());
	if (AChaosImpactPlayerState* First = Locals[0]->GetPlayerState<AChaosImpactPlayerState>())
	{
		First->bSpectating = true;
		First->SetPlayerName(TEXT("観戦"));
		First->TournamentRole = EChaosImpactTournamentRole::None;
		First->TournamentArena = INDEX_NONE;
		First->ForceNetUpdate();
	}
	PlaceTournamentMember(Locals[0]);
	UE_LOG(LogChaosImpact, Log, TEXT("Local tournament: the match's players handed their controllers back"));
}
