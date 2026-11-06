// トーナメント: the game mode's side (see ChaosImpactTournament.h for how a tournament is played).

#include "ChaosImpactGameMode.h"

static_assert(ChaosImpactTournament::OpeningSeconds == ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds,
	"A tournament match's opening lasts as long as the duel opening's camera.");
#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactDuelStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactSpectatorPawn.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactTrainingArena.h"
#include "ChaosImpactTrainingTarget.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

namespace
{
	/** Where the practice areas stand, away from the lobby and the arenas. */
	const FVector TnPracticeOffset(0.0f, -60000.0f, 0.0f);
	/** The practice areas' ball pads, around their middle. */
	const FVector TnPracticePadOffsets[] = {FVector(420.0f, 0.0f, 0.0f), FVector(-620.0f, 680.0f, 0.0f), FVector(-620.0f, -680.0f, 0.0f),
		FVector(1450.0f, 1300.0f, 0.0f), FVector(1900.0f, -1550.0f, 0.0f), FVector(-1900.0f, 1500.0f, 0.0f)};

	FString TnMachineKey(const AChaosImpactPlayerState* Member)
	{
		return !Member ? FString(TEXT("?")) : Member->bHostMachine ? FString(TEXT("#host")) : Member->MachineToken.IsEmpty()
			? FString::Printf(TEXT("#%s"), *Member->GetPlayerName()) : Member->MachineToken;
	}

	bool TnIsInside(const FVector& Location, const FVector& Center, const float HalfExtent)
	{
		const FVector Offset = Location - Center;
		return FMath::Abs(Offset.X) <= HalfExtent + 900.0f && FMath::Abs(Offset.Y) <= HalfExtent + 900.0f && FMath::Abs(Offset.Z) <= 4000.0f;
	}
}

void AChaosImpactGameMode::StartTournament(const TArray<FChaosImpactTournamentEntrant>* People)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld() || Match->IsTournament())
	{
		return;
	}
	const FChaosImpactMatchRules Rules = ChaosImpactMatch::Sanitize(Match->Rules, Match->CountHumanMembers());
	TournamentRandom.Initialize(FMath::Rand());

	// People: everyone in the room who plays (or the people given); then the CPUs as the host set them up.
	TArray<FChaosImpactTournamentEntrant> Entrants;
	if (People)
	{
		Entrants = *People;
	}
	for (AChaosImpactPlayerState* Member : People ? TArray<AChaosImpactPlayerState*>() : Match->GetCompetitors(false))
	{
		FChaosImpactTournamentEntrant Entrant;
		Entrant.Name = Member->GetPlayerName();
		Entrant.Character = ChaosImpactRoster::ClampIndex(Member->CharacterIndex);
		Entrant.Colour = FMath::Max(Member->ColourChoice, 0);
		Entrant.CustomColours = Member->CustomColours;
		Entrant.Member = Member;
		Entrants.Add(Entrant);
	}
	for (int32 Index = 0; Index < Rules.Tournament.CPUs.Num() && Entrants.Num() < ChaosImpactTournament::MaxEntrants; ++Index)
	{
		const FChaosImpactTournamentCPU& CPU = Rules.Tournament.CPUs[Index];
		FChaosImpactTournamentEntrant Entrant;
		Entrant.bCPU = true;
		Entrant.Name = CPU.Name.IsEmpty() ? FString::Printf(TEXT("CPU%d"), Index + 1) : CPU.Name;
		Entrant.Character = CPU.Character >= 0 ? ChaosImpactRoster::ClampIndex(CPU.Character)
			: TournamentRandom.RandRange(0, ChaosImpactRoster::Num() - 1);
		Entrant.Colour = TournamentRandom.RandRange(0, ChaosImpactRoster::ColourCount - 1);
		Entrant.CPULevel = static_cast<uint8>(CPU.Level >= 0 ? ChaosImpactMatch::SanitizeCPULevel(CPU.Level)
			: TournamentRandom.RandRange(0, ChaosImpactMatch::CPULevelCount - 1));
		Entrants.Add(Entrant);
	}
	if (Entrants.Num() < ChaosImpactTournament::MinEntrants)
	{
		UE_LOG(LogChaosImpact, Warning, TEXT("Tournament: only %d entrant(s), not started"), Entrants.Num());
		SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
		Match->bRulesDecided = false;
		return;
	}
	// The draw: everyone in a random order (the seeds).
	for (int32 Index = Entrants.Num() - 1; Index > 0; --Index)
	{
		Entrants.Swap(Index, TournamentRandom.RandRange(0, Index));
	}
	const FString Title = Match->RoomName.IsEmpty() ? FString(TEXT("トーナメント")) : Match->RoomName;
	Match->Tournament = ChaosImpactTournament::Build(MoveTemp(Entrants), Rules.Tournament, Title);
	const double Now = Match->GetServerWorldTimeSeconds();
	Match->Tournament.StartedAt = Now;
	Match->TournamentArenas.Reset();
	Match->bVersusMatch = false;
	Match->bRulesDecided = false;
	Match->TournamentStartsAt = 0.0;
	ClearReady();
	SetMatchPhase(EChaosImpactOnlinePhase::Tournament, 0.0f);
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(false);
	}
	// The arenas stand in a row where VS stages go, on the players' floor.
	TournamentArenaOrigin = VersusStageLocation;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const APlayerController* Host = GameInstance->GetFirstLocalPlayerController(GetWorld()))
		{
			if (const ACharacter* HostCharacter = Cast<ACharacter>(Host->GetPawn()))
			{
				TournamentArenaOrigin.Z = HostCharacter->GetActorLocation().Z - HostCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			}
		}
	}
	// Everyone to their practice area (or, watching the room, over the first arena) while the bracket is shown.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Member = It->Get())
		{
			if (AChaosImpactPlayerState* State = Member->GetPlayerState<AChaosImpactPlayerState>())
			{
				State->TournamentWait = EChaosImpactTournamentWait::Bracket;
				State->TournamentWatchMatch = INDEX_NONE;
			}
			PlaceTournamentMember(Member);
		}
	}
	TournamentRoundBreakUntil = Now + ChaosImpactTournament::RoundBreakSeconds;
	TournamentAskUntil = 0.0;
	TournamentCPUDecideAt.Reset();
	Match->ForceNetUpdate();
	GetWorldTimerManager().SetTimer(TournamentTimer, this, &AChaosImpactGameMode::TickTournament, 0.1f, true);
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this))
	{
		Sessions->SetTourneyState(TEXT("running"));
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament started: %d entrants, bracket of %d (%d rounds), %s"),
		Match->Tournament.Entrants.Num(), Match->Tournament.BracketSize, Match->Tournament.Rounds,
		Match->Tournament.IsSimultaneous() ? TEXT("all at once") : TEXT("one match at a time"));
}

void AChaosImpactGameMode::ScheduleTournament(const double StartsAt)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !IsOnlineRoom())
	{
		return;
	}
	Match->TournamentStartsAt = StartsAt;
	Match->bPublicTournament = true;
	Match->ForceNetUpdate();
	GetWorldTimerManager().SetTimer(TournamentScheduleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		AChaosImpactGameState* Room = GetGameState<AChaosImpactGameState>();
		if (!Room || Room->IsTournament() || Room->TournamentStartsAt <= 0.0 || Room->Phase != EChaosImpactOnlinePhase::Lobby)
		{
			return;
		}
		// From its start time on, as soon as two people are in the room.
		if (Room->GetServerWorldTimeSeconds() >= Room->TournamentStartsAt && Room->GetCompetitors(false).Num() >= 2)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Public tournament: start time reached with %d people"), Room->GetCompetitors(false).Num());
			StartTournament();
		}
	}), 1.0f, true);
}

void AChaosImpactGameMode::TickTournament()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament())
	{
		GetWorldTimerManager().ClearTimer(TournamentTimer);
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	const double Now = Match->GetServerWorldTimeSeconds();
	for (int32 Arena = 0; Arena < Match->TournamentArenas.Num(); ++Arena)
	{
		UpdateTournamentArena(Arena);
	}
	if (Tournament.bFinished)
	{
		if (Now >= Tournament.FinishedAt + ChaosImpactTournament::ChampionSeconds)
		{
			EndTournament();
		}
		return;
	}
	// Two CPUs decided in the background (a round played all at once).
	TArray<int32> Decided;
	for (const TPair<int32, double>& Pending : TournamentCPUDecideAt)
	{
		if (Now >= Pending.Value)
		{
			Decided.Add(Pending.Key);
		}
	}
	for (const int32 Index : Decided)
	{
		TournamentCPUDecideAt.Remove(Index);
		ChaosImpactTournament::DecideCPUMatch(Tournament, Index, TournamentRandom);
		DecideTournamentMatch(Index, Tournament.Matches[Index].Winner, false, false);
		if (!Match->IsTournament() || Tournament.bFinished)
		{
			return;
		}
	}
	// Called players arrive in their arena.
	for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
	{
		if (Tournament.Matches[Index].State == EChaosImpactTournamentMatchState::Calling
			&& Now >= Tournament.Matches[Index].CalledAt + ChaosImpactTournament::CallSeconds && !IsLocalSeatPending(Index))
		{
			BeginTournamentMatch(Index);
		}
	}
	if (Tournament.Round == 0)
	{
		if (Now >= TournamentRoundBreakUntil)
		{
			StartTournamentRound();
		}
		return;
	}
	bool bRoundOver = true;
	for (const FChaosImpactTournamentMatch& Each : Tournament.Matches)
	{
		bRoundOver &= Each.Round != Tournament.Round || Each.IsOver();
	}
	if (!bRoundOver)
	{
		ScheduleTournamentMatches();
		return;
	}
	// The round is over: a moment for the bracket, then the next.
	if (TournamentRoundBreakUntil <= 0.0)
	{
		TournamentRoundBreakUntil = Now + ChaosImpactTournament::RoundBreakSeconds;
		Tournament.RoundStartedAt = TournamentRoundBreakUntil;
		Match->ForceNetUpdate();
	}
	else if (Now >= TournamentRoundBreakUntil)
	{
		StartTournamentRound();
	}
}

void AChaosImpactGameMode::StartTournamentRound()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament())
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	if (Tournament.Round >= Tournament.Rounds)
	{
		return;
	}
	++Tournament.Round;
	TournamentRoundBreakUntil = 0.0;
	Tournament.RoundStartedAt = Match->GetServerWorldTimeSeconds();
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: %s"), *Tournament.GetRoundName(Tournament.Round));
	// Byes and walk-overs: nobody (or nobody still in) on a side.
	for (int32 Index = 0; Index < Tournament.Matches.Num() && Match->IsTournament() && !Tournament.bFinished; ++Index)
	{
		const FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
		if (Each.Round != Tournament.Round || Each.IsOver())
		{
			continue;
		}
		const bool bPresent[2] = {Tournament.Entrants.IsValidIndex(Each.Entrants[0]) && !Tournament.Entrants[Each.Entrants[0]].bForfeited,
			Tournament.Entrants.IsValidIndex(Each.Entrants[1]) && !Tournament.Entrants[Each.Entrants[1]].bForfeited};
		if (bPresent[0] && bPresent[1])
		{
			continue;
		}
		const bool bBye = !Tournament.Entrants.IsValidIndex(Each.Entrants[0]) || !Tournament.Entrants.IsValidIndex(Each.Entrants[1]);
		DecideTournamentMatch(Index, bPresent[0] ? 0 : bPresent[1] ? 1 : INDEX_NONE, !bBye, bBye);
	}
	Match->ForceNetUpdate();
	if (Match->IsTournament() && !Tournament.bFinished)
	{
		ScheduleTournamentMatches();
	}
}

void AChaosImpactGameMode::ScheduleTournamentMatches()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament() || Match->Tournament.bFinished)
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	const double Now = Match->GetServerWorldTimeSeconds();
	const auto BothCPUs = [&Tournament](const FChaosImpactTournamentMatch& Each)
	{
		return Tournament.Entrants.IsValidIndex(Each.Entrants[0]) && Tournament.Entrants[Each.Entrants[0]].bCPU
			&& Tournament.Entrants.IsValidIndex(Each.Entrants[1]) && Tournament.Entrants[Each.Entrants[1]].bCPU;
	};
	if (Tournament.IsSimultaneous())
	{
		for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
		{
			FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
			if (Each.Round != Tournament.Round || Each.State != EChaosImpactTournamentMatchState::Ready)
			{
				continue;
			}
			if (BothCPUs(Each))
			{
				// Decided without being played, after about as long as a match takes.
				Each.State = EChaosImpactTournamentMatchState::Live;
				Each.CalledAt = Now;
				TournamentCPUDecideAt.Add(Index, Now + TournamentRandom.FRandRange(14.0f, 32.0f));
			}
			else
			{
				CallTournamentMatch(Index);
			}
		}
		Match->ForceNetUpdate();
		return;
	}
	// One at a time.
	if (Tournament.AskingMatch != INDEX_NONE)
	{
		if (Now >= TournamentAskUntil)
		{
			// Nobody chose: it is watched.
			const int32 Asked = Tournament.AskingMatch;
			Tournament.AskingMatch = INDEX_NONE;
			CallTournamentMatch(Asked);
		}
		return;
	}
	for (const FChaosImpactTournamentMatch& Each : Tournament.Matches)
	{
		if (Each.Round == Tournament.Round && Each.IsPlaying())
		{
			return;
		}
	}
	for (int32 Index = 0; Index < Tournament.Matches.Num(); ++Index)
	{
		FChaosImpactTournamentMatch& Each = Tournament.Matches[Index];
		if (Each.Round != Tournament.Round || Each.State != EChaosImpactTournamentMatchState::Ready)
		{
			continue;
		}
		if (BothCPUs(Each))
		{
			if (Tournament.Settings.bSkipCPUMatches)
			{
				ChaosImpactTournament::DecideCPUMatch(Tournament, Index, TournamentRandom);
				DecideTournamentMatch(Index, Each.Winner, false, false);
				return;
			}
			// The host chooses to watch or skip it.
			Tournament.AskingMatch = Index;
			TournamentAskUntil = Now + ChaosImpactTournament::AskSeconds;
			Match->ForceNetUpdate();
			return;
		}
		CallTournamentMatch(Index);
		return;
	}
}

void AChaosImpactGameMode::AnswerTournamentCPUMatch(APlayerController* Member, const int32 Choice)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	const AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!Match || !Match->IsTournament() || !State || !State->bRoomHost || Match->Tournament.AskingMatch == INDEX_NONE)
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	const int32 Asked = Tournament.AskingMatch;
	Tournament.AskingMatch = INDEX_NONE;
	if (Choice == 0)
	{
		CallTournamentMatch(Asked);
		return;
	}
	Tournament.Settings.bSkipCPUMatches = Choice == 2;
	ChaosImpactTournament::DecideCPUMatch(Tournament, Asked, TournamentRandom);
	DecideTournamentMatch(Asked, Tournament.Matches[Asked].Winner, false, false);
}

void AChaosImpactGameMode::DecideTournamentMatch(const int32 MatchIndex, const int32 WinnerSide, const bool bForfeit, const bool bBye)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament() || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	FChaosImpactTournamentMatch& Decided = Tournament.Matches[MatchIndex];
	if (Decided.IsOver())
	{
		return;
	}
	Decided.Winner = WinnerSide == 0 || WinnerSide == 1 ? WinnerSide : INDEX_NONE;
	Decided.bForfeit = bForfeit;
	Decided.bBye = bBye;
	if ((bForfeit || bBye) && Decided.Winner != INDEX_NONE)
	{
		Decided.Wins[Decided.Winner] = ChaosImpactMatch::DuelRoundsToWin;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	const bool bChampion = ChaosImpactTournament::Advance(Tournament, MatchIndex, Now);
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: %s match %d decided%s%s: %s"), *Tournament.GetRoundName(Decided.Round), Decided.Slot + 1,
		bForfeit ? TEXT(" (walk-over)") : TEXT(""), bBye ? TEXT(" (bye)") : TEXT(""),
		Tournament.Entrants.IsValidIndex(Decided.WinnerEntrant()) ? *Tournament.Entrants[Decided.WinnerEntrant()].Name : TEXT("nobody"));
	if (bChampion)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Tournament: champion %s"),
			Tournament.Entrants.IsValidIndex(Tournament.Champion) ? *Tournament.Entrants[Tournament.Champion].Name : TEXT("nobody"));
		// Everyone sees the champion: off the practice floors and out of the arenas, onto the bracket.
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (AChaosImpactPlayerState* State = It->Get() ? It->Get()->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
			{
				State->TournamentWait = EChaosImpactTournamentWait::Bracket;
			}
		}
	}
	Match->ForceNetUpdate();
}

int32 AChaosImpactGameMode::AcquireTournamentArena()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Match->TournamentArenas.Num(); ++Index)
	{
		if (Match->TournamentArenas[Index].Match == INDEX_NONE && TournamentArenaReleaseAt.IsValidIndex(Index)
			&& TournamentArenaReleaseAt[Index] <= 0.0)
		{
			return Index;
		}
	}
	// A new duel arena beside the others.
	const int32 Index = Match->TournamentArenas.Num();
	const FVector Origin = TournamentArenaOrigin + FVector(Index * ChaosImpactTournament::ArenaSpacing, 0.0f, 0.0f);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* StageClass = DuelStageClass ? DuelStageClass.Get() : AChaosImpactDuelStage::StaticClass();
	AChaosImpactStageBase* Stage = GetWorld()->SpawnActor<AChaosImpactStageBase>(StageClass, Origin, FRotator::ZeroRotator, Parameters);
	if (!Stage)
	{
		return INDEX_NONE;
	}
	FChaosImpactTournamentArena Arena;
	Arena.Center = Stage->GetCenter();
	Arena.HalfExtent = Stage->GetHalfExtent();
	Match->TournamentArenas.Add(Arena);
	TournamentStages.Add(Stage);
	TournamentArenaSpawners.AddDefaulted();
	TournamentArenaCPUs.AddDefaulted();
	TournamentKnockoutPending.Add(false);
	TournamentArenaReleaseAt.Add(0.0);
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: arena %d made"), Index);
	return Index;
}

void AChaosImpactGameMode::CallTournamentMatch(const int32 MatchIndex)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament() || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	const int32 Arena = AcquireTournamentArena();
	if (Arena == INDEX_NONE)
	{
		return;
	}
	FChaosImpactTournamentMatch& Called = Match->Tournament.Matches[MatchIndex];
	FChaosImpactTournamentArena& Place = Match->TournamentArenas[Arena];
	Place.Match = MatchIndex;
	Place.Duel = FChaosImpactDuelState();
	Called.State = EChaosImpactTournamentMatchState::Calling;
	Called.Arena = Arena;
	Called.CalledAt = Match->GetServerWorldTimeSeconds();
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: %s match %d called to arena %d"), *Match->Tournament.GetRoundName(Called.Round),
		Called.Slot + 1, Arena);
}

APawn* AChaosImpactGameMode::BringEntrantToArena(const int32 Entrant, const int32 Arena, const int32 Side)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Tournament.Entrants.IsValidIndex(Entrant) || !TournamentStages.IsValidIndex(Arena) || !GetWorld())
	{
		return nullptr;
	}
	const FChaosImpactTournamentEntrant& Who = Match->Tournament.Entrants[Entrant];
	AChaosImpactStageBase* Stage = TournamentStages[Arena];
	const TArray<FVector> Starts = Stage->GetSpawnPoints();
	const FVector Floor = Starts.IsValidIndex(Side) ? Starts[Side] : Stage->GetCenter();
	const FRotator Facing(0.0f, (Stage->GetCenter() - Floor).Rotation().Yaw, 0.0f);
	if (Who.bCPU)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AChaosImpactCPUController* CPU = GetWorld()->SpawnActor<AChaosImpactCPUController>(AChaosImpactCPUController::StaticClass(),
			Floor, Facing, Parameters);
		if (!CPU)
		{
			return nullptr;
		}
		CPU->MarkAsMatchCPU();
		if (AChaosImpactPlayerState* State = CPU->GetPlayerState<AChaosImpactPlayerState>())
		{
			State->SetPlayerName(Who.Name);
			State->CharacterIndex = Who.Character;
			State->ColourChoice = Who.Colour;
			State->ForceNetUpdate();
		}
		APawn* Pawn = SpawnDefaultPawnAtTransform(CPU, FTransform(Facing, Floor + FVector(0.0f, 0.0f, 110.0f)));
		if (!Pawn)
		{
			CPU->Destroy();
			return nullptr;
		}
		CPU->Possess(Pawn);
		CPU->SetDifficulty(Who.CPULevel);
		if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Pawn))
		{
			const FVector Location = Floor + FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
			Character->SetTrainingStartTransform(Location, Facing);
			Character->ResetForOnlineMatch(Location, Facing);
		}
		TournamentArenaCPUs[Arena].Add(CPU);
		return Pawn;
	}
	AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(Who.Member.Get());
	APlayerController* Controller = Member ? Cast<APlayerController>(Member->GetOwner()) : nullptr;
	if (!Controller)
	{
		return nullptr;
	}
	if (!Cast<AChaosImpactCharacter>(Controller->GetPawn()))
	{
		// Watching: back into their own character.
		if (APawn* Camera = Controller->GetPawn())
		{
			Controller->UnPossess();
			Camera->Destroy();
		}
		Member->TournamentRole = EChaosImpactTournamentRole::Competitor;
		RestartPlayer(Controller);
	}
	AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Controller->GetPawn());
	if (!Character)
	{
		return nullptr;
	}
	const FVector Location = Floor + FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
	Character->ResetForOnlineMatch(Location, Facing);
	Member->TournamentRole = EChaosImpactTournamentRole::Competitor;
	Member->TournamentArena = Arena;
	Member->ForceNetUpdate();
	return Character;
}

void AChaosImpactGameMode::BeginTournamentMatch(const int32 MatchIndex)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament() || !Match->Tournament.Matches.IsValidIndex(MatchIndex) || !GetWorld())
	{
		return;
	}
	FChaosImpactTournamentMatch& Begun = Match->Tournament.Matches[MatchIndex];
	const int32 Arena = Begun.Arena;
	if (Begun.State != EChaosImpactTournamentMatchState::Calling || !Match->TournamentArenas.IsValidIndex(Arena))
	{
		return;
	}
	ClearArenaBalls(Arena);
	FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	Duel = FChaosImpactDuelState();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (APawn* Pawn = BringEntrantToArena(Begun.Entrants[Side], Arena, Side))
		{
			Duel.Sides[Side] = Pawn->GetPlayerState();
		}
	}
	if (!Duel.Sides[0] || !Duel.Sides[1])
	{
		// Someone could not be brought in (they just left): the other one goes through.
		const int32 Winner = Duel.Sides[0] ? 0 : Duel.Sides[1] ? 1 : INDEX_NONE;
		Begun.State = EChaosImpactTournamentMatchState::Live;
		ReleaseTournamentArena(Arena);
		DecideTournamentMatch(MatchIndex, Winner, true, false);
		return;
	}
	// Its own three special balls (as a ランクマッチ: every kind but the nova and the prison).
	TArray<uint8> Pool;
	for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
	{
		const EChaosImpactBallType Kind = static_cast<EChaosImpactBallType>(Type);
		if (Kind != EChaosImpactBallType::Nova && Kind != EChaosImpactBallType::Prison)
		{
			Pool.Add(static_cast<uint8>(Type));
		}
	}
	for (int32 Pick = 0; Pick < ChaosImpactMatch::DuelBallPicks && !Pool.IsEmpty(); ++Pick)
	{
		Duel.BallPicks.Add(Pool[TournamentRandom.RandRange(0, Pool.Num() - 1)]);
		Pool.Remove(Duel.BallPicks.Last());
	}
	// The arena's pads: normal balls only until the slot has shown this match's kinds.
	for (const TWeakObjectPtr<AChaosImpactBallSpawner>& Old : TournamentArenaSpawners[Arena])
	{
		if (Old.IsValid())
		{
			Old->Destroy();
		}
	}
	TournamentArenaSpawners[Arena].Reset();
	for (const FVector& Point : TournamentStages[Arena]->GetBallPoints())
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Point + FVector(0.0f, 0.0f, 3.0f));
		if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActorDeferred<AChaosImpactBallSpawner>(AChaosImpactBallSpawner::StaticClass(),
			SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Spawner->SetAlwaysActive(true);
			for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
			{
				Spawner->SetChanceWeight(static_cast<EChaosImpactBallType>(Type), 0.0f);
			}
			Spawner->FinishSpawning(SpawnTransform);
			TournamentArenaSpawners[Arena].Add(Spawner);
		}
	}
	Begun.State = EChaosImpactTournamentMatchState::Live;
	TournamentArenaReleaseAt[Arena] = 0.0;
	// Every match opens as a match does: its round's name and how it is won, then the ball slot.
	SetArenaStep(Arena, EChaosImpactDuelStep::Opening, ChaosImpactTournament::OpeningSeconds);
	// Spectators asking for this match (or for any) come to it.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Viewer = It->Get();
		const AChaosImpactPlayerState* State = Viewer ? Viewer->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (State && State->TournamentRole == EChaosImpactTournamentRole::Spectator
			&& (State->TournamentWatchMatch == MatchIndex || !Match->TournamentArenas.IsValidIndex(State->TournamentArena)
				|| Match->TournamentArenas[State->TournamentArena].Match == INDEX_NONE))
		{
			PlaceAsTournamentSpectator(Viewer, Arena);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: %s match %d begins in arena %d: %s vs %s"), *Match->Tournament.GetRoundName(Begun.Round),
		Begun.Slot + 1, Arena, *Duel.Sides[0]->GetPlayerName(), *Duel.Sides[1]->GetPlayerName());
}

void AChaosImpactGameMode::SetArenaStep(const int32 Arena, const EChaosImpactDuelStep Step, const float Seconds)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena))
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	Duel.Step = Step;
	Duel.StepStartedAt = Now;
	Duel.StepEndsAt = Seconds > 0.0f ? Now + Seconds : 0.0;
	Match->ForceNetUpdate();
}

APawn* AChaosImpactGameMode::GetArenaPawn(const int32 Arena, const int32 Side) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	const APlayerState* Member = Match && Match->TournamentArenas.IsValidIndex(Arena) && (Side == 0 || Side == 1)
		? Match->TournamentArenas[Arena].Duel.Sides[Side].Get() : nullptr;
	return Member ? Member->GetPawn() : nullptr;
}

void AChaosImpactGameMode::PlaceArenaDuellists(const int32 Arena)
{
	if (!TournamentStages.IsValidIndex(Arena) || !IsValid(TournamentStages[Arena]))
	{
		return;
	}
	const AChaosImpactStageBase* Stage = TournamentStages[Arena];
	const TArray<FVector> Starts = Stage->GetSpawnPoints();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetArenaPawn(Arena, Side));
		if (!Character || !Starts.IsValidIndex(Side))
		{
			continue;
		}
		const FVector Location = Starts[Side] + FVector(0.0f, 0.0f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		Character->ResetForOnlineMatch(Location, FRotator(0.0f, (Stage->GetCenter() - Starts[Side]).Rotation().Yaw, 0.0f));
	}
}

void AChaosImpactGameMode::StartArenaRound(const int32 Arena, const int32 Round)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena))
	{
		return;
	}
	FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	Duel.Round = Round;
	Duel.RoundWinner = INDEX_NONE;
	Duel.RoundEnd = EChaosImpactDuelEnd::None;
	if (Round > 1)
	{
		ClearArenaBalls(Arena);
	}
	PlaceArenaDuellists(Arena);
	SetArenaStep(Arena, EChaosImpactDuelStep::RoundIntro, ChaosImpactMatch::DuelRoundIntroSeconds);
}

void AChaosImpactGameMode::UpdateTournamentArena(const int32 Arena)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena))
	{
		return;
	}
	const FChaosImpactTournamentArena& Place = Match->TournamentArenas[Arena];
	if (!Match->Tournament.Matches.IsValidIndex(Place.Match)
		|| Match->Tournament.Matches[Place.Match].State != EChaosImpactTournamentMatchState::Live)
	{
		return;
	}
	if (TournamentKnockoutPending.IsValidIndex(Arena) && TournamentKnockoutPending[Arena])
	{
		TournamentKnockoutPending[Arena] = false;
		EvaluateArenaKnockouts(Arena);
	}
	const FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	const double Now = Match->GetServerWorldTimeSeconds();
	if (Duel.StepEndsAt <= 0.0 || Now < Duel.StepEndsAt)
	{
		return;
	}
	switch (Duel.Step)
	{
	case EChaosImpactDuelStep::Opening:
		SetArenaStep(Arena, EChaosImpactDuelStep::BallDraw, ChaosImpactMatch::DuelBallDrawSeconds);
		break;
	case EChaosImpactDuelStep::BallDraw:
		ApplyArenaBallChances(Arena, true);
		StartArenaRound(Arena, 1);
		break;
	case EChaosImpactDuelStep::RoundIntro:
		SetArenaStep(Arena, EChaosImpactDuelStep::Fight, GetDuelRoundSeconds());
		break;
	case EChaosImpactDuelStep::Fight:
	{
		// Time: more health left takes the round, level goes to sudden death.
		float Health[2] = {0.0f, 0.0f};
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetArenaPawn(Arena, Side));
			Health[Side] = Character && !Character->IsEliminated() ? Character->GetHealth() : 0.0f;
		}
		if (!FMath::IsNearlyEqual(Health[0], Health[1]))
		{
			EndArenaRound(Arena, Health[0] > Health[1] ? 0 : 1, EChaosImpactDuelEnd::Time);
		}
		else
		{
			SetArenaStep(Arena, EChaosImpactDuelStep::SuddenDeath, ChaosImpactMatch::DuelSuddenDeathSeconds);
		}
		break;
	}
	case EChaosImpactDuelStep::SuddenDeath:
		EndArenaRound(Arena, INDEX_NONE, EChaosImpactDuelEnd::Draw);
		break;
	case EChaosImpactDuelStep::RoundOver:
	{
		FChaosImpactDuelState& Over = Match->TournamentArenas[Arena].Duel;
		// A tournament match needs a winner: drawn rounds are played again until someone has two (a tenth is a coin).
		if (Over.Wins[0] < ChaosImpactMatch::DuelRoundsToWin && Over.Wins[1] < ChaosImpactMatch::DuelRoundsToWin && Over.Round < 9)
		{
			StartArenaRound(Arena, Over.Round + 1);
			break;
		}
		Over.MatchWinner = Over.Wins[0] > Over.Wins[1] ? 0 : Over.Wins[1] > Over.Wins[0] ? 1 : TournamentRandom.RandRange(0, 1);
		SetArenaStep(Arena, EChaosImpactDuelStep::Finished, ChaosImpactTournament::MatchOverSeconds);
		break;
	}
	case EChaosImpactDuelStep::Finished:
		FinishTournamentMatch(Place.Match);
		break;
	default:
		break;
	}
}

void AChaosImpactGameMode::EndArenaRound(const int32 Arena, const int32 WinnerSide, const EChaosImpactDuelEnd How)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena) || !AChaosImpactGameState::IsDuelStepLive(Match->TournamentArenas[Arena].Duel))
	{
		return;
	}
	FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	Duel.RoundWinner = WinnerSide == 0 || WinnerSide == 1 ? WinnerSide : INDEX_NONE;
	Duel.RoundEnd = How;
	if (Duel.RoundWinner != INDEX_NONE)
	{
		++Duel.Wins[Duel.RoundWinner];
	}
	Duel.RoundResults.Add(static_cast<int8>(Duel.RoundWinner != INDEX_NONE ? Duel.RoundWinner : -1));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetArenaPawn(Arena, Side)))
		{
			Character->CancelChargingThrow();
		}
	}
	SetArenaStep(Arena, EChaosImpactDuelStep::RoundOver, ChaosImpactMatch::DuelRoundOverSeconds);
}

void AChaosImpactGameMode::EvaluateArenaKnockouts(const int32 Arena)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena) || !AChaosImpactGameState::IsDuelStepLive(Match->TournamentArenas[Arena].Duel))
	{
		return;
	}
	bool bDown[2] = {false, false};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetArenaPawn(Arena, Side));
		bDown[Side] = !Character || Character->IsEliminated();
	}
	if (bDown[0] && bDown[1])
	{
		EndArenaRound(Arena, INDEX_NONE, EChaosImpactDuelEnd::DoubleKnockout);
	}
	else if (bDown[0] || bDown[1])
	{
		EndArenaRound(Arena, bDown[0] ? 1 : 0, EChaosImpactDuelEnd::Knockout);
	}
}

void AChaosImpactGameMode::FinishTournamentMatch(const int32 MatchIndex)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Tournament.Matches.IsValidIndex(MatchIndex))
	{
		return;
	}
	FChaosImpactTournamentMatch& Finished = Match->Tournament.Matches[MatchIndex];
	const int32 Arena = Finished.Arena;
	int32 Winner = INDEX_NONE;
	if (Match->TournamentArenas.IsValidIndex(Arena))
	{
		const FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
		Finished.Wins[0] = Duel.Wins[0];
		Finished.Wins[1] = Duel.Wins[1];
		Winner = Duel.MatchWinner;
		ReleaseTournamentArena(Arena);
	}
	DecideTournamentMatch(MatchIndex, Winner, false, false);
	if (Match->bLocalTournament && MatchIndex == LocalSeatedMatch)
	{
		UnseatLocalTournament();
	}
}

void AChaosImpactGameMode::ReleaseTournamentArena(const int32 Arena)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena) || !GetWorld())
	{
		return;
	}
	FChaosImpactTournamentArena& Place = Match->TournamentArenas[Arena];
	TArray<APlayerController*> Players;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (APlayerState* Member = Place.Duel.Sides[Side].Get())
		{
			if (APlayerController* Controller = Cast<APlayerController>(Member->GetOwner()))
			{
				Players.Add(Controller);
			}
		}
	}
	for (const TWeakObjectPtr<AController>& CPU : TournamentArenaCPUs[Arena])
	{
		if (CPU.IsValid())
		{
			if (APawn* Pawn = CPU->GetPawn())
			{
				Pawn->Destroy();
			}
			CPU->Destroy();
		}
	}
	TournamentArenaCPUs[Arena].Reset();
	ClearArenaBalls(Arena);
	for (const TWeakObjectPtr<AChaosImpactBallSpawner>& Spawner : TournamentArenaSpawners[Arena])
	{
		if (Spawner.IsValid())
		{
			if (AChaosImpactBall* Ball = Spawner->GetActiveBall())
			{
				Ball->Destroy();
			}
			Spawner->Destroy();
		}
	}
	TournamentArenaSpawners[Arena].Reset();
	if (Match->Tournament.Matches.IsValidIndex(Place.Match))
	{
		Match->Tournament.Matches[Place.Match].Arena = INDEX_NONE;
	}
	Place.Match = INDEX_NONE;
	Place.Duel = FChaosImpactDuelState();
	TournamentArenaReleaseAt[Arena] = 0.0;
	Match->ForceNetUpdate();
	// Its players back to what they chose; anyone watching here to another match being played.
	for (APlayerController* Controller : Players)
	{
		if (AChaosImpactPlayerState* State = Controller->GetPlayerState<AChaosImpactPlayerState>())
		{
			State->TournamentRole = EChaosImpactTournamentRole::None;
			State->TournamentArena = INDEX_NONE;
		}
		PlaceTournamentMember(Controller);
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Viewer = It->Get();
		const AChaosImpactPlayerState* State = Viewer ? Viewer->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (State && State->TournamentRole == EChaosImpactTournamentRole::Spectator && State->TournamentArena == Arena)
		{
			const int32 Other = PickArenaToWatch(INDEX_NONE);
			if (Other != INDEX_NONE && Other != Arena)
			{
				PlaceAsTournamentSpectator(Viewer, Other);
			}
		}
	}
}

void AChaosImpactGameMode::ClearArenaBalls(const int32 Arena)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena) || !GetWorld())
	{
		return;
	}
	const FVector Center = Match->TournamentArenas[Arena].Center;
	const float HalfExtent = Match->TournamentArenas[Arena].HalfExtent;
	const auto ClearOf = [this, &Center, HalfExtent](UClass* Class)
	{
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(GetWorld(), Class); It; ++It)
		{
			if (TnIsInside(It->GetActorLocation(), Center, HalfExtent))
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* Actor : Doomed)
		{
			Actor->Destroy();
		}
	};
	ClearOf(AChaosImpactSimaeBird::StaticClass());
	ClearOf(AChaosImpactSimaeFeatherBurst::StaticClass());
	ClearOf(AChaosImpactBall::StaticClass());
	ClearOf(AChaosImpactHazardZone::StaticClass());
	ClearOf(AChaosImpactTornado::StaticClass());
}

void AChaosImpactGameMode::ApplyArenaBallChances(const int32 Arena, const bool bWithPicks)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->TournamentArenas.IsValidIndex(Arena) || !TournamentArenaSpawners.IsValidIndex(Arena))
	{
		return;
	}
	const FChaosImpactDuelState& Duel = Match->TournamentArenas[Arena].Duel;
	const AChaosImpactBallSpawner* Defaults = GetDefault<AChaosImpactBallSpawner>();
	for (const TWeakObjectPtr<AChaosImpactBallSpawner>& Spawner : TournamentArenaSpawners[Arena])
	{
		if (!Spawner.IsValid())
		{
			continue;
		}
		for (int32 Type = 1; Type < ChaosImpactBallTypes::Count; ++Type)
		{
			const bool bDrawn = bWithPicks && Duel.BallPicks.Contains(static_cast<uint8>(Type));
			const float Weight = Defaults->GetChanceWeight(static_cast<EChaosImpactBallType>(Type));
			Spawner->SetChanceWeight(static_cast<EChaosImpactBallType>(Type), bDrawn ? FMath::Max(Weight, 0.05f) : 0.0f);
		}
	}
}

int32 AChaosImpactGameMode::PickArenaToWatch(const int32 WantedMatch) const
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match)
	{
		return INDEX_NONE;
	}
	if (Match->Tournament.Matches.IsValidIndex(WantedMatch) && Match->TournamentArenas.IsValidIndex(Match->Tournament.Matches[WantedMatch].Arena))
	{
		return Match->Tournament.Matches[WantedMatch].Arena;
	}
	for (int32 Index = 0; Index < Match->TournamentArenas.Num(); ++Index)
	{
		if (Match->TournamentArenas[Index].Match != INDEX_NONE)
		{
			return Index;
		}
	}
	return Match->TournamentArenas.IsEmpty() ? INDEX_NONE : 0;
}

void AChaosImpactGameMode::SetTournamentWait(APlayerController* Member, const EChaosImpactTournamentWait Wait, const int32 WatchMatch)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!Match || !Match->IsTournament() || !State)
	{
		return;
	}
	State->TournamentWait = Wait;
	State->TournamentWatchMatch = WatchMatch;
	// Playing: it applies once their match is over.
	if (State->TournamentRole != EChaosImpactTournamentRole::Competitor)
	{
		PlaceTournamentMember(Member);
	}
}

void AChaosImpactGameMode::PlaceTournamentMember(APlayerController* Member)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!Match || !Match->IsTournament() || !State || State->IsABot() || State->TournamentRole == EChaosImpactTournamentRole::Competitor)
	{
		return;
	}
	// Someone who only watches the room (or came in during the tournament) only watches.
	const bool bWatchOnly = State->bSpectating;
	if (bWatchOnly || State->TournamentWait == EChaosImpactTournamentWait::Spectate)
	{
		const int32 Arena = PickArenaToWatch(State->TournamentWatchMatch);
		if (Arena != INDEX_NONE || bWatchOnly)
		{
			PlaceAsTournamentSpectator(Member, FMath::Max(Arena, 0));
			return;
		}
	}
	PlaceInPractice(Member);
}

FVector AChaosImpactGameMode::GetPracticeCenter(const AChaosImpactPlayerState* Member)
{
	const FString Key = TnMachineKey(Member);
	if (const TWeakObjectPtr<AChaosImpactTrainingArena>* Known = TournamentPractice.Find(Key); Known && Known->IsValid())
	{
		return (*Known)->GetCenter();
	}
	const FVector Center = TournamentArenaOrigin + TnPracticeOffset + FVector(TournamentPractice.Num() * ChaosImpactTournament::PracticeSpacing, 0.0f, 0.0f);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AChaosImpactTrainingArena* Area = GetWorld()->SpawnActor<AChaosImpactTrainingArena>(AChaosImpactTrainingArena::StaticClass(), Center,
		FRotator::ZeroRotator, Parameters);
	TournamentPractice.Add(Key, Area);
	if (Area)
	{
		TournamentPracticeActors.Add(Area);
		for (const FVector& Offset : TnPracticePadOffsets)
		{
			if (AChaosImpactBallSpawner* Spawner = GetWorld()->SpawnActor<AChaosImpactBallSpawner>(AChaosImpactBallSpawner::StaticClass(),
				Center + Offset + FVector(0.0f, 0.0f, 3.0f), FRotator::ZeroRotator, Parameters))
			{
				Spawner->SetAlwaysActive(true);
				TournamentPracticeActors.Add(Spawner);
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Tournament: practice area %d made for %s"), TournamentPractice.Num() - 1, *Key);
	}
	return Center;
}

void AChaosImpactGameMode::PlaceInPractice(APlayerController* Member)
{
	AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!State || !GetWorld())
	{
		return;
	}
	if (!Cast<AChaosImpactCharacter>(Member->GetPawn()))
	{
		if (APawn* Camera = Member->GetPawn())
		{
			Member->UnPossess();
			Camera->Destroy();
		}
		State->TournamentRole = EChaosImpactTournamentRole::Practice;
		RestartPlayer(Member);
	}
	const FVector Center = GetPracticeCenter(State);
	State->TournamentRole = EChaosImpactTournamentRole::Practice;
	State->TournamentArena = INDEX_NONE;
	State->PracticeCenter = Center;
	State->ForceNetUpdate();
	if (AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn()))
	{
		// A machine's second player beside the first.
		const FVector Location = Center + FVector(-300.0f, State->bSecondOfMachine ? 260.0f : -260.0f,
			Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f);
		const FRotator Facing(0.0f, 0.0f, 0.0f);
		Character->SetTrainingStartTransform(Location, Facing);
		Character->ResetForOnlineMatch(Location, Facing);
	}
}

void AChaosImpactGameMode::PlaceAsTournamentSpectator(APlayerController* Member, const int32 Arena)
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (!Match || !State || !GetWorld())
	{
		return;
	}
	if (State->TournamentRole == EChaosImpactTournamentRole::Spectator && State->TournamentArena == Arena
		&& Cast<AChaosImpactSpectatorPawn>(Member->GetPawn()))
	{
		return;
	}
	if (APawn* Old = Member->GetPawn())
	{
		Member->UnPossess();
		Old->Destroy();
	}
	const FVector Target = Match->TournamentArenas.IsValidIndex(Arena) ? FVector(Match->TournamentArenas[Arena].Center)
		: TournamentArenaOrigin;
	const FVector Location = Target + FVector(0.0f, -2600.0f, 2000.0f);
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.Owner = Member;
	Parameters.ObjectFlags |= RF_Transient;
	State->TournamentRole = EChaosImpactTournamentRole::Spectator;
	State->TournamentArena = Arena;
	State->ForceNetUpdate();
	if (AChaosImpactSpectatorPawn* Camera = GetWorld()->SpawnActor<AChaosImpactSpectatorPawn>(AChaosImpactSpectatorPawn::StaticClass(),
		Location, (Target - Location).Rotation(), Parameters))
	{
		Member->Possess(Camera);
	}
}

void AChaosImpactGameMode::ForfeitTournamentEntrant(const int32 Entrant)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->IsTournament() || !Match->Tournament.Entrants.IsValidIndex(Entrant) || Match->Tournament.bFinished)
	{
		return;
	}
	FChaosImpactTournamentState& Tournament = Match->Tournament;
	Tournament.Entrants[Entrant].bForfeited = true;
	Tournament.Entrants[Entrant].Member = nullptr;
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament: %s left (walk-over)"), *Tournament.Entrants[Entrant].Name);
	const int32 Current = Tournament.FindCurrentMatch(Entrant);
	if (!Tournament.Matches.IsValidIndex(Current))
	{
		Match->ForceNetUpdate();
		return;
	}
	FChaosImpactTournamentMatch& Lost = Tournament.Matches[Current];
	const int32 Side = Lost.SideOf(Entrant);
	if (Lost.State == EChaosImpactTournamentMatchState::Waiting || Side == INDEX_NONE)
	{
		// Decided when its round starts.
		Match->ForceNetUpdate();
		return;
	}
	if (Lost.State == EChaosImpactTournamentMatchState::Live && Lost.Arena == INDEX_NONE)
	{
		TournamentCPUDecideAt.Remove(Current);
	}
	if (Match->TournamentArenas.IsValidIndex(Lost.Arena))
	{
		FChaosImpactDuelState& Duel = Match->TournamentArenas[Lost.Arena].Duel;
		Duel.Sides[Side] = nullptr;
		ReleaseTournamentArena(Lost.Arena);
	}
	DecideTournamentMatch(Current, 1 - Side, true, false);
}

void AChaosImpactGameMode::ClearTournamentWorld()
{
	for (int32 Arena = 0; Arena < TournamentStages.Num(); ++Arena)
	{
		if (TournamentArenaCPUs.IsValidIndex(Arena))
		{
			for (const TWeakObjectPtr<AController>& CPU : TournamentArenaCPUs[Arena])
			{
				if (CPU.IsValid())
				{
					if (APawn* Pawn = CPU->GetPawn())
					{
						Pawn->Destroy();
					}
					CPU->Destroy();
				}
			}
		}
		ClearArenaBalls(Arena);
		if (TournamentArenaSpawners.IsValidIndex(Arena))
		{
			for (const TWeakObjectPtr<AChaosImpactBallSpawner>& Spawner : TournamentArenaSpawners[Arena])
			{
				if (Spawner.IsValid())
				{
					if (AChaosImpactBall* Ball = Spawner->GetActiveBall())
					{
						Ball->Destroy();
					}
					Spawner->Destroy();
				}
			}
		}
		if (IsValid(TournamentStages[Arena]))
		{
			TournamentStages[Arena]->Destroy();
		}
	}
	TournamentStages.Reset();
	TournamentArenaSpawners.Reset();
	TournamentArenaCPUs.Reset();
	TournamentKnockoutPending.Reset();
	TournamentArenaReleaseAt.Reset();
	TournamentCPUDecideAt.Reset();
	for (AActor* Actor : TournamentPracticeActors)
	{
		if (IsValid(Actor))
		{
			if (AChaosImpactBallSpawner* Spawner = Cast<AChaosImpactBallSpawner>(Actor))
			{
				if (AChaosImpactBall* Ball = Spawner->GetActiveBall())
				{
					Ball->Destroy();
				}
			}
			Actor->Destroy();
		}
	}
	TournamentPracticeActors.Reset();
	TournamentPractice.Reset();
}

void AChaosImpactGameMode::EndTournament()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(TournamentTimer);
	ClearTournamentWorld();
	// The bracket stays to be looked at in the lobby; the tournament itself is over.
	Match->Tournament.bActive = false;
	Match->TournamentArenas.Reset();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Member = It->Get();
		AChaosImpactPlayerState* State = Member ? Member->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
		if (!State || State->IsABot())
		{
			continue;
		}
		State->TournamentRole = EChaosImpactTournamentRole::None;
		State->TournamentArena = INDEX_NONE;
		State->ForceNetUpdate();
		// Back into the lobby as each one was: a character, or the room's spectator camera.
		if (APawn* Old = Member->GetPawn())
		{
			Member->UnPossess();
			Old->Destroy();
		}
		RestartPlayer(Member);
	}
	for (TActorIterator<AChaosImpactTrainingTarget> It(GetWorld()); It; ++It)
	{
		It->SetTrainingEnabled(true);
	}
	Match->bRulesDecided = false;
	if (UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this); Sessions && Match->bPublicTournament)
	{
		Sessions->SetTourneyState(TEXT("done"));
	}
	if (Match->bLocalTournament)
	{
		// ローカルトーナメント: back to its registration, to play another.
		Match->bLocalTournament = false;
		bUseLocalSeatDevices = false;
		LocalSeatedMatch = INDEX_NONE;
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			if (AChaosImpactPlayerController* First = Cast<AChaosImpactPlayerController>(GameInstance->GetFirstLocalPlayerController(GetWorld())))
			{
				First->OnLocalTournamentOver();
			}
		}
		SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
		UE_LOG(LogChaosImpact, Log, TEXT("Local tournament over: back to its registration"));
		return;
	}
	ReopenRecruitment();
	SetMatchPhase(EChaosImpactOnlinePhase::Lobby, 0.0f);
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Tournament over: back to the lobby"));
}
