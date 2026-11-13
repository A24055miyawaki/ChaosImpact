#include "ChaosImpactGameState.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactCharacter.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

void AChaosImpactPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactPlayerState, JoinOrder);
	DOREPLIFETIME(AChaosImpactPlayerState, CharacterIndex);
	DOREPLIFETIME(AChaosImpactPlayerState, ColourChoice);
	DOREPLIFETIME(AChaosImpactPlayerState, CustomColours);
	DOREPLIFETIME(AChaosImpactPlayerState, PaintKey);
	DOREPLIFETIME(AChaosImpactPlayerState, bRoomHost);
	DOREPLIFETIME(AChaosImpactPlayerState, bSpectating);
	DOREPLIFETIME(AChaosImpactPlayerState, Knockouts);
	DOREPLIFETIME(AChaosImpactPlayerState, Points);
	DOREPLIFETIME(AChaosImpactPlayerState, CrashPushes);
	DOREPLIFETIME(AChaosImpactPlayerState, TeamIndex);
	DOREPLIFETIME(AChaosImpactPlayerState, bHostMachine);
	DOREPLIFETIME(AChaosImpactPlayerState, bSecondOfMachine);
	DOREPLIFETIME(AChaosImpactPlayerState, bReadyForMatch);
	DOREPLIFETIME(AChaosImpactPlayerState, Throws);
	DOREPLIFETIME(AChaosImpactPlayerState, Hits);
	DOREPLIFETIME(AChaosImpactPlayerState, TimesHit);
	DOREPLIFETIME(AChaosImpactPlayerState, Dodges);
	DOREPLIFETIME(AChaosImpactPlayerState, SpecialThrows);
	DOREPLIFETIME(AChaosImpactPlayerState, DriveHits);
	DOREPLIFETIME(AChaosImpactPlayerState, LongestHit);
	DOREPLIFETIME(AChaosImpactPlayerState, PointsHistory);
	DOREPLIFETIME(AChaosImpactPlayerState, bViewingResults);
	DOREPLIFETIME(AChaosImpactPlayerState, bWantsRematch);
	DOREPLIFETIME(AChaosImpactPlayerState, TournamentRole);
	DOREPLIFETIME(AChaosImpactPlayerState, TournamentArena);
	DOREPLIFETIME(AChaosImpactPlayerState, PracticeCenter);
}

void AChaosImpactPlayerState::ResetMatchStats()
{
	Points = 0;
	CrashPushes = 0;
	Knockouts = 0;
	Throws = 0;
	Hits = 0;
	TimesHit = 0;
	Dodges = 0;
	SpecialThrows = 0;
	DriveHits = 0;
	LongestHit = 0;
	PointsHistory.Reset();
}

void AChaosImpactGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactGameState, bOnlineRoom);
	DOREPLIFETIME(AChaosImpactGameState, bRoomMatchLevel);
	DOREPLIFETIME(AChaosImpactGameState, Phase);
	DOREPLIFETIME(AChaosImpactGameState, PhaseEndsAt);
	DOREPLIFETIME(AChaosImpactGameState, PhaseStartedAt);
	DOREPLIFETIME(AChaosImpactGameState, RoomPassword);
	DOREPLIFETIME(AChaosImpactGameState, bRecruitmentClosed);
	DOREPLIFETIME(AChaosImpactGameState, bVersusMatch);
	DOREPLIFETIME(AChaosImpactGameState, Rules);
	DOREPLIFETIME(AChaosImpactGameState, Crash);
	DOREPLIFETIME(AChaosImpactGameState, RubyField);
	DOREPLIFETIME(AChaosImpactGameState, PartyCrown);
	DOREPLIFETIME(AChaosImpactGameState, StageCenter);
	DOREPLIFETIME(AChaosImpactGameState, StageHalfExtent);
	DOREPLIFETIME(AChaosImpactGameState, ReadyStartedAt);
	DOREPLIFETIME(AChaosImpactGameState, RoomName);
	DOREPLIFETIME(AChaosImpactGameState, bRulesDecided);
	DOREPLIFETIME(AChaosImpactGameState, ReadyDeadline);
	DOREPLIFETIME(AChaosImpactGameState, Matchmade);
	DOREPLIFETIME(AChaosImpactGameState, MatchmadeExpected);
	DOREPLIFETIME(AChaosImpactGameState, MatchmadeDeadline);
	DOREPLIFETIME(AChaosImpactGameState, Duel);
	DOREPLIFETIME(AChaosImpactGameState, Royale);
	DOREPLIFETIME(AChaosImpactGameState, Tournament);
	DOREPLIFETIME(AChaosImpactGameState, TournamentArenas);
	DOREPLIFETIME(AChaosImpactGameState, TournamentStartsAt);
	DOREPLIFETIME(AChaosImpactGameState, bPublicTournament);
	DOREPLIFETIME(AChaosImpactGameState, PublicTournamentMaxPlayers);
}

int32 AChaosImpactGameState::GetTournamentArenaAt(const FVector& Location) const
{
	if (!Tournament.bActive)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < TournamentArenas.Num(); ++Index)
	{
		const FChaosImpactTournamentArena& Arena = TournamentArenas[Index];
		const FVector Offset = Location - FVector(Arena.Center);
		const float Reach = Arena.HalfExtent + 900.0f;
		if (FMath::Abs(Offset.X) <= Reach && FMath::Abs(Offset.Y) <= Reach && FMath::Abs(Offset.Z) <= 4000.0f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

const FChaosImpactDuelState* AChaosImpactGameState::FindDuelAt(const FVector& Location) const
{
	if (Tournament.bActive)
	{
		const int32 Arena = GetTournamentArenaAt(Location);
		return TournamentArenas.IsValidIndex(Arena) && TournamentArenas[Arena].Match != INDEX_NONE ? &TournamentArenas[Arena].Duel : nullptr;
	}
	return IsDuel() ? &Duel : nullptr;
}

const FChaosImpactDuelState* AChaosImpactGameState::FindDuelFor(const APlayerState* Member) const
{
	if (Tournament.bActive)
	{
		const AChaosImpactPlayerState* State = Cast<AChaosImpactPlayerState>(Member);
		const int32 Arena = State ? State->TournamentArena : INDEX_NONE;
		return TournamentArenas.IsValidIndex(Arena) && TournamentArenas[Arena].Match != INDEX_NONE ? &TournamentArenas[Arena].Duel : nullptr;
	}
	return IsDuel() ? &Duel : nullptr;
}

bool AChaosImpactGameState::IsDuelLiveAt(const FVector& Location) const
{
	if (Tournament.bActive)
	{
		const FChaosImpactDuelState* Arena = FindDuelAt(Location);
		return Arena && IsDuelStepLive(*Arena);
	}
	return IsDuelLive();
}

float AChaosImpactGameState::GetDuelStepRemaining(const FChaosImpactDuelState& Of) const
{
	return Of.StepEndsAt > 0.0 ? FMath::Max(0.0f, static_cast<float>(Of.StepEndsAt - GetServerWorldTimeSeconds())) : 0.0f;
}

float AChaosImpactGameState::GetDuelStepElapsed(const FChaosImpactDuelState& Of) const
{
	return FMath::Max(0.0f, static_cast<float>(GetServerWorldTimeSeconds() - Of.StepStartedAt));
}

const FChaosImpactRoyaleEntry* AChaosImpactGameState::GetRoyaleEntry(const APlayerState* Member) const
{
	return Member ? Royale.Entries.FindByPredicate([Member](const auto& Entry) { return Entry.Member == Member; }) : nullptr;
}

bool AChaosImpactGameState::IsRoyaleEliminated(const APlayerState* Member) const
{
	const auto* Entry = GetRoyaleEntry(Member);
	return IsBattleRoyale() && Entry && Entry->bOut;
}

float AChaosImpactGameState::GetRoyaleTeamHealth(const int32 Team) const
{
	float Health = 0.f;
	for (const auto& Entry : Royale.Entries) { if (!Entry.bOut && Entry.Team == Team) { Health += Entry.Health; } }
	return Health;
}

bool AChaosImpactGameState::IsDuelLive() const
{
	return IsDuel() && Phase == EChaosImpactOnlinePhase::Match
		&& (Duel.Step == EChaosImpactDuelStep::Fight || Duel.Step == EChaosImpactDuelStep::SuddenDeath);
}

float AChaosImpactGameState::GetDuelStepRemaining() const
{
	return Duel.StepEndsAt > 0.0 ? FMath::Max(0.0f, static_cast<float>(Duel.StepEndsAt - GetServerWorldTimeSeconds())) : 0.0f;
}

float AChaosImpactGameState::GetDuelStepElapsed() const
{
	return FMath::Max(0.0f, static_cast<float>(GetServerWorldTimeSeconds() - Duel.StepStartedAt));
}

APlayerState* AChaosImpactGameState::GetDuelOpponent(const APlayerState* Member) const
{
	const int32 Side = Duel.SideOf(Member);
	return Side == INDEX_NONE ? nullptr : Duel.Sides[1 - Side].Get();
}

float AChaosImpactGameState::GetIntroElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	if (Phase != EChaosImpactOnlinePhase::Intro || !World)
	{
		return GetPhaseElapsedSeconds();
	}
	const double LocalNow = World->GetTimeSeconds();
	if (IntroSeenForStart != PhaseStartedAt)
	{
		IntroSeenForStart = PhaseStartedAt;
		IntroSeenLocalTime = LocalNow;
		// Arriving a moment late stays in step with the host; arriving much later starts from the top.
		IntroSeenOffset = FMath::Min(GetPhaseElapsedSeconds(), 0.3f);
	}
	// Online the VS card comes first (see AChaosImpactPlayerController::UpdateOnlineVersusCard): the flyover starts
	// after it, so this is negative while the card plays.
	return static_cast<float>(LocalNow - IntroSeenLocalTime) + IntroSeenOffset - (bOnlineRoom ? ChaosImpactMatch::OnlineCardSeconds : 0.0f);
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetMembersInJoinOrder() const
{
	TArray<AChaosImpactPlayerState*> Members;
	for (APlayerState* State : PlayerArray)
	{
		if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
			Member && !Member->IsInactive() && !Member->IsABot() && !Member->bSpectating)
		{
			Members.Add(Member);
		}
	}
	Members.Sort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
	{
		if (A.bRoomHost != B.bRoomHost)
		{
			return A.bRoomHost;
		}
		return A.JoinOrder < B.JoinOrder;
	});
	return Members;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetSpectators() const
{
	TArray<AChaosImpactPlayerState*> Spectators;
	for (APlayerState* State : PlayerArray)
	{
		if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
			Member && !Member->IsInactive() && !Member->IsABot() && Member->bSpectating)
		{
			Spectators.Add(Member);
		}
	}
	return Spectators;
}

int32 AChaosImpactGameState::CountHumanMembers() const
{
	return GetMembersInJoinOrder().Num();
}

int32 AChaosImpactGameState::CountMachines() const
{
	int32 Machines = 0;
	for (const AChaosImpactPlayerState* Member : GetMembersInJoinOrder())
	{
		Machines += Member->bSecondOfMachine ? 0 : 1;
	}
	return Machines;
}

int32 AChaosImpactGameState::CountReadyMembers() const
{
	int32 Ready = 0;
	for (const AChaosImpactPlayerState* Member : GetMembersInJoinOrder())
	{
		Ready += Member->bReadyForMatch ? 1 : 0;
	}
	return Ready;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetCompetitors(const bool bIncludeCPUs) const
{
	TArray<AChaosImpactPlayerState*> Competitors = GetMembersInJoinOrder();
	if (bIncludeCPUs)
	{
		TArray<AChaosImpactPlayerState*> CPUs;
		for (APlayerState* State : PlayerArray)
		{
			if (AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
				Member && Member->IsABot() && !Member->IsInactive())
			{
				CPUs.Add(Member);
			}
		}
		CPUs.Sort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
		{
			return A.GetPlayerName() < B.GetPlayerName();
		});
		Competitors.Append(CPUs);
	}
	return Competitors;
}

TArray<AChaosImpactPlayerState*> AChaosImpactGameState::GetRanking() const
{
	TArray<AChaosImpactPlayerState*> Ranking = GetCompetitors(true);
	const bool bDuel = IsDuel();
	// クラウンキープ: level on counts, whoever wears the crown is ahead.
	const APlayerState* CrownHolder = IsCrownKeep() && IsValid(PartyCrown) ? PartyCrown->GetHolderState() : nullptr;
	Ranking.StableSort([this, bDuel, CrownHolder](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
	{
		if (IsBattleRoyale())
		{
			const auto* EA = GetRoyaleEntry(&A); const auto* EB = GetRoyaleEntry(&B);
			const int32 RA = EA && EA->Place > 0 ? EA->Place : 1;
			const int32 RB = EB && EB->Place > 0 ? EB->Place : 1;
			return RA < RB;
		}
		// A duel goes by the rounds won first.
		const int32 SideA = bDuel ? Duel.SideOf(&A) : INDEX_NONE;
		const int32 SideB = bDuel ? Duel.SideOf(&B) : INDEX_NONE;
		if (SideA != INDEX_NONE && SideB != INDEX_NONE && Duel.Wins[SideA] != Duel.Wins[SideB])
		{
			return Duel.Wins[SideA] > Duel.Wins[SideB];
		}
		if (A.Points != B.Points)
		{
			return A.Points > B.Points;
		}
		if (CrownHolder && (&A == CrownHolder || &B == CrownHolder))
		{
			return &A == CrownHolder;
		}
		return A.Knockouts > B.Knockouts;
	});
	return Ranking;
}

int32 AChaosImpactGameState::GetTeamPoints(const int32 Team) const
{
	int32 Total = 0;
	for (const AChaosImpactPlayerState* Member : GetCompetitors(true))
	{
		Total += Member->TeamIndex == Team ? Member->Points : 0;
	}
	return Total;
}

float AChaosImpactGameState::GetPhaseRemainingSeconds() const
{
	return FMath::Max(0.0f, static_cast<float>(PhaseEndsAt - GetServerWorldTimeSeconds()));
}

float AChaosImpactGameState::GetPhaseElapsedSeconds() const
{
	return FMath::Max(0.0f, static_cast<float>(GetServerWorldTimeSeconds() - PhaseStartedAt));
}

bool AChaosImpactGameState::IsMatchInputLocked() const
{
	// A matched room waits behind its own screen until the match, and closes after it: nobody plays in its lobby.
	if (IsMatchmade() && (!bVersusMatch || Phase == EChaosImpactOnlinePhase::Lobby || Phase == EChaosImpactOnlinePhase::Starting))
	{
		return true;
	}
	if (!bVersusMatch)
	{
		return false;
	}
	if (Rules.bDuel && Phase == EChaosImpactOnlinePhase::Match && !IsDuelLive())
	{
		return true;
	}
	switch (Phase)
	{
	case EChaosImpactOnlinePhase::TeamSelect:
	case EChaosImpactOnlinePhase::Results:
		return true;
	case EChaosImpactOnlinePhase::Intro:
		// Held until Ready? has run; judged by server time, so GO frees everyone at the same moment
		// even before the phase change itself arrives.
		return ReadyStartedAt <= 0.0 || GetServerWorldTimeSeconds() < PhaseEndsAt;
	default:
		return false;
	}
}

bool AChaosImpactGameState::AreTeammates(const UWorld* World, const AActor* A, const AActor* B)
{
	if (!A || !B || A == B)
	{
		return false;
	}
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	if (Match && Match->IsTournament())
	{
		// A tournament: only those in the same arena face each other; anyone elsewhere (another match, a practice
		// area) is left alone, as a teammate would be.
		const int32 ArenaA = Match->GetTournamentArenaAt(A->GetActorLocation());
		const int32 ArenaB = Match->GetTournamentArenaAt(B->GetActorLocation());
		if (ArenaA != ArenaB)
		{
			return true;
		}
		if (ArenaA != INDEX_NONE)
		{
			return false;
		}
	}
	if (!Match || !Match->bVersusMatch)
	{
		// Solo mode and training: sides given on the characters themselves (placed enemies are all on one).
		const AChaosImpactCharacter* CharacterA = Cast<AChaosImpactCharacter>(A);
		const AChaosImpactCharacter* CharacterB = Cast<AChaosImpactCharacter>(B);
		return CharacterA && CharacterB && CharacterA->SoloTeam >= 0 && CharacterA->SoloTeam == CharacterB->SoloTeam;
	}
	if (!Match->IsTeamBattle())
	{
		return false;
	}
	const APawn* PawnA = Cast<APawn>(A);
	const APawn* PawnB = Cast<APawn>(B);
	const AChaosImpactPlayerState* StateA = PawnA ? PawnA->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const AChaosImpactPlayerState* StateB = PawnB ? PawnB->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	return StateA && StateB && StateA->TeamIndex >= 0 && StateA->TeamIndex == StateB->TeamIndex;
}
