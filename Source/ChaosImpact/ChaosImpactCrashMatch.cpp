#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactCrashBall.h"
#include "ChaosImpactStageBase.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

void AChaosImpactGameMode::ClearCrashMatch()
{
	GetWorldTimerManager().ClearTimer(CrashMatchTimer);
	if (auto* Match = GetGameState<AChaosImpactGameState>())
	{
		if (IsValid(Match->Crash.Ball)) { Match->Crash.Ball->Destroy(); }
		Match->Crash = FChaosImpactCrashMatchState();
		Match->ForceNetUpdate();
	}
	for (AChaosImpactCrashGoal* Goal : CrashGoals) { if (IsValid(Goal)) { Goal->Destroy(); } }
	CrashGoals.Reset();
	CrashLastDirectHit[0] = CrashLastDirectHit[1] = -100.;
	CrashOvertimeRetreatSince = -1.;
}

void AChaosImpactGameMode::SetupCrashMatch()
{
	ClearCrashMatch();
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !VersusStage) { return; }
	VersusStage->ConfigureCrashBallMode(Match->Rules.bCrashBall);
	if (!Match->Rules.bCrashBall) { return; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FTransform Transform = VersusStage->GetCrashBallGoalTransform(Team);
		auto* Goal = GetWorld()->SpawnActorDeferred<AChaosImpactCrashGoal>(AChaosImpactCrashGoal::StaticClass(),
			Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Goal)
		{
			Goal->SetTeamIndex(Team);
			Goal->OpeningWidth = VersusStage->CrashGoalWidth;
			Goal->OpeningHeight = VersusStage->CrashGoalHeight;
			Goal->FinishSpawning(Transform);
			CrashGoals.Add(Goal);
		}
	}
	Match->Crash.Ball = GetWorld()->SpawnActor<AChaosImpactCrashBall>(AChaosImpactCrashBall::StaticClass(),
		VersusStage->GetCrashBallStartFloorLocation(), FRotator::ZeroRotator, Params);
	if (Match->Crash.Ball) { Match->Crash.Ball->ResetToStartLocation(VersusStage->GetCrashBallStartFloorLocation(), VersusStage); }
	Match->ForceNetUpdate();
}

FVector AChaosImpactGameMode::GetCrashPushDirection(const int32 TeamIndex, const FVector& Location) const
{
	return VersusStage ? VersusStage->GetCrashBallPushDirection(TeamIndex, Location) : FVector::ForwardVector;
}

TArray<FVector> AChaosImpactGameMode::GetCrashTeamSpawnPoints(const int32 Team) const
{
	TArray<FVector> All = VersusStage ? VersusStage->GetSpawnPoints() : TArray<FVector>();
	if (!VersusStage || (Team != 0 && Team != 1)) { return All; }
	// A team's half is the side of its own goal: it starts and comes back there, defending first.
	const FVector Middle = VersusStage->GetCrashBallStartFloorLocation();
	const FVector Home = (VersusStage->GetCrashBallGoalTransform(Team).GetLocation() - Middle).GetSafeNormal2D();
	TArray<FVector> Own;
	for (const FVector& Point : All)
	{
		if (FVector::DotProduct(Point - Middle, Home) > 250.f) { Own.Add(Point); }
	}
	return Own.IsEmpty() ? All : Own;
}

void AChaosImpactGameMode::NotifyCrashBallHit(APawn* Thrower, const int32 TeamIndex, const bool bCharged, const FVector& PushDirection)
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || Match->Phase != EChaosImpactOnlinePhase::Match || TeamIndex < 0 || TeamIndex > 1) { return; }
	const double Now = Match->GetServerWorldTimeSeconds();
	CrashLastDirectHit[TeamIndex] = Now;
	if (Match->Crash.bOvertime && TeamIndex == Match->Crash.OvertimeTeam)
	{
		Match->Crash.OvertimeHitAt = Now;
	}
	// Only a push toward the opponents' goal counts for the thrower (a stray hit the wrong way earns nothing).
	const auto* Ball = Match->GetCrashBall();
	auto* Member = Thrower ? Thrower->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	if (Ball && Member && FVector::DotProduct(PushDirection.GetSafeNormal2D(), GetCrashPushDirection(TeamIndex, Ball->GetActorLocation())) > .3f)
	{
		++Member->CrashPushes;
		AwardMatchPoints(Member, 1, false);
	}
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::NotifyCrashBallRecovered()
{
	if (auto* Match = GetGameState<AChaosImpactGameState>())
	{
		Match->Crash.LastRecoveryAt = Match->GetServerWorldTimeSeconds();
		Match->ForceNetUpdate();
	}
}

void AChaosImpactGameMode::UpdateCrashMatch()
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Rules.bCrashBall || Match->Phase != EChaosImpactOnlinePhase::Match || !VersusStage) { return; }
	auto* Ball = Match->GetCrashBall();
	if (!IsValid(Ball)) { EndMatch(); return; }
	auto& State = Match->Crash;
	const double Now = Match->GetServerWorldTimeSeconds();
	State.LastPushTeam = Ball->GetLastHitTeamIndex();
	if (!Ball->IsRecovering())
	{
		const int32 OldLead = Match->GetCrashLeader();
		int32 Red, Blue;
		VersusStage->ComputeCrashBallCounts(Ball->GetActorLocation(), Red, Blue, State.Progress);
		// A count only ever goes down: it is the furthest the team has pushed.
		if (Red < State.RedCount) { State.RedCount = Red; State.CountDroppedAt[0] = Now; }
		if (Blue < State.BlueCount) { State.BlueCount = Blue; State.CountDroppedAt[1] = Now; }
		if (OldLead != Match->GetCrashLeader()) { State.LastLeadChangedAt = Now; }
		for (AChaosImpactCrashGoal* Goal : CrashGoals)
		{
			if (IsValid(Goal) && Goal->IsBallScored(Ball))
			{
				State.Winner = Goal->GetScoringTeamIndex();
				(State.Winner == 0 ? State.RedCount : State.BlueCount) = 0;
				State.CountDroppedAt[State.Winner] = Now;
				State.bKnockout = true;
				Goal->TriggerGoalScoredEffect();
				Match->ForceNetUpdate();
				EndMatch();
				return;
			}
		}
	}
	if (State.bOvertime)
	{
		// Overtime lasts while the trailing team keeps the ball in the opponents' half and keeps hitting it.
		const int32 Team = State.OvertimeTeam;
		const bool bAdvancing = Team == 0 ? State.Progress > 0.f : State.Progress < 0.f;
		CrashOvertimeRetreatSince = bAdvancing ? -1. : (CrashOvertimeRetreatSince < 0. ? Now : CrashOvertimeRetreatSince);
		if (Match->GetCrashLeader() == Team)
		{
			State.Winner = Team;
			EndMatch();
			return;
		}
		if (Now - CrashLastDirectHit[Team] > CrashOvertimeGraceSeconds
			|| (CrashOvertimeRetreatSince >= 0. && Now - CrashOvertimeRetreatSince > CrashOvertimeGraceSeconds)
			|| Now - State.OvertimeStartedAt > CrashOvertimeMaxSeconds)
		{
			State.Winner = Match->GetCrashLeader();
			EndMatch();
			return;
		}
	}
	Match->ForceNetUpdate();
}

void AChaosImpactGameMode::HandleMatchTimeExpired()
{
	auto* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Rules.bCrashBall) { EndMatch(); return; }
	UpdateCrashMatch();
	if (Match->Phase != EChaosImpactOnlinePhase::Match) { return; }
	auto& State = Match->Crash;
	const int32 Lead = Match->GetCrashLeader();
	// The team behind (or, level, the team whose half the ball is not in) gets overtime while it is attacking.
	const int32 Attack = Lead != INDEX_NONE ? 1 - Lead : State.Progress > 0.f ? 0 : State.Progress < 0.f ? 1 : INDEX_NONE;
	const bool bEnemyTerritory = Attack == 0 ? State.Progress > 0.f : State.Progress < 0.f;
	const double Now = Match->GetServerWorldTimeSeconds();
	if (Attack != INDEX_NONE && bEnemyTerritory && Now - CrashLastDirectHit[Attack] <= CrashOvertimeGraceSeconds)
	{
		State.bOvertime = true;
		State.OvertimeTeam = Attack;
		State.OvertimeStartedAt = Now;
		State.OvertimeHitAt = CrashLastDirectHit[Attack];
		CrashOvertimeRetreatSince = -1.;
		Match->ForceNetUpdate();
		return;
	}
	State.Winner = Lead;
	EndMatch();
}
