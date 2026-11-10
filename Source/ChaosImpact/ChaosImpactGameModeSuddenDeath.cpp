#include "ChaosImpactGameMode.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSuddenDeathZone.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "TimerManager.h"

// A duel's (or a tournament arena's) sudden death: the floor closes in as a circle, special balls of three kinds keep
// appearing on what is left of it, and the bell gives it to whoever is nearer the middle.

namespace
{
	/** The kinds a sudden death may pick from: quick to use and each one a hit (no nova's long charge, no prison or smoke). */
	const EChaosImpactBallType SuddenDeathKinds[] = {
		EChaosImpactBallType::Fire, EChaosImpactBallType::Ice, EChaosImpactBallType::Thunder, EChaosImpactBallType::Black,
		EChaosImpactBallType::Wind, EChaosImpactBallType::Beam, EChaosImpactBallType::Snow, EChaosImpactBallType::Simae,
		EChaosImpactBallType::Drive};
}

APawn* AChaosImpactGameMode::GetSuddenDeathPawn(const int32 Arena, const int32 Side) const
{
	return Arena == INDEX_NONE ? GetDuelPawn(Side) : GetArenaPawn(Arena, Side);
}

void AChaosImpactGameMode::BeginSuddenDeath(const int32 Arena, const FVector& Center, const float HalfExtent)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !GetWorld())
	{
		return;
	}
	EndSuddenDeath(Arena);
	FSuddenDeath& Sudden = SuddenDeaths.Add(Arena);
	Sudden.Center = Center;
	Sudden.HalfExtent = HalfExtent;
	Sudden.NextBallAt = Match->GetServerWorldTimeSeconds() + 0.6;
	// Three kinds, picked now (each sudden death its own).
	TArray<EChaosImpactBallType> Pool(SuddenDeathKinds, UE_ARRAY_COUNT(SuddenDeathKinds));
	for (int32 Pick = 0; Pick < 3 && !Pool.IsEmpty(); ++Pick)
	{
		Sudden.Kinds.Add(Pool[FMath::RandHelper(Pool.Num())]);
		Pool.Remove(Sudden.Kinds.Last());
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AChaosImpactSuddenDeathZone* Zone = GetWorld()->SpawnActor<AChaosImpactSuddenDeathZone>(AChaosImpactSuddenDeathZone::StaticClass(),
		Center, FRotator::ZeroRotator, Parameters))
	{
		Zone->Begin(HalfExtent, Match->GetServerWorldTimeSeconds(), ChaosImpactMatch::DuelSuddenDeathSeconds);
		Sudden.Zone = Zone;
	}
	if (!GetWorldTimerManager().IsTimerActive(SuddenDeathTimer))
	{
		GetWorldTimerManager().SetTimer(SuddenDeathTimer, this, &AChaosImpactGameMode::UpdateSuddenDeaths, 0.1f, true);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Sudden death (%s): the floor closes in; balls of %s, %s and %s"),
		Arena == INDEX_NONE ? TEXT("duel") : *FString::Printf(TEXT("arena %d"), Arena),
		Sudden.Kinds.IsValidIndex(0) ? *StaticEnum<EChaosImpactBallType>()->GetNameStringByValue(static_cast<int64>(Sudden.Kinds[0])) : TEXT("-"),
		Sudden.Kinds.IsValidIndex(1) ? *StaticEnum<EChaosImpactBallType>()->GetNameStringByValue(static_cast<int64>(Sudden.Kinds[1])) : TEXT("-"),
		Sudden.Kinds.IsValidIndex(2) ? *StaticEnum<EChaosImpactBallType>()->GetNameStringByValue(static_cast<int64>(Sudden.Kinds[2])) : TEXT("-"));
}

void AChaosImpactGameMode::EndSuddenDeath(const int32 Arena)
{
	if (FSuddenDeath* Sudden = SuddenDeaths.Find(Arena))
	{
		if (AChaosImpactSuddenDeathZone* Zone = Sudden->Zone.Get())
		{
			Zone->Destroy();
		}
		SuddenDeaths.Remove(Arena);
	}
	if (SuddenDeaths.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(SuddenDeathTimer);
	}
}

int32 AChaosImpactGameMode::JudgeSuddenDeathByMiddle(const int32 Arena) const
{
	const FSuddenDeath* Sudden = SuddenDeaths.Find(Arena);
	float Distance[2] = {TNumericLimits<float>::Max(), TNumericLimits<float>::Max()};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetSuddenDeathPawn(Arena, Side));
		if (Character && !Character->IsEliminated() && Sudden)
		{
			Distance[Side] = static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Sudden->Center));
		}
	}
	if (FMath::IsNearlyEqual(Distance[0], Distance[1], 1.0f))
	{
		return INDEX_NONE;
	}
	return Distance[0] < Distance[1] ? 0 : 1;
}

void AChaosImpactGameMode::UpdateSuddenDeaths()
{
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	UWorld* World = GetWorld();
	if (!Match || !World)
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	TArray<TPair<int32, int32>> Decided;
	for (TPair<int32, FSuddenDeath>& Entry : SuddenDeaths)
	{
		const int32 Arena = Entry.Key;
		FSuddenDeath& Sudden = Entry.Value;
		const AChaosImpactSuddenDeathZone* Zone = Sudden.Zone.Get();
		if (!Zone)
		{
			continue;
		}
		// Outside the circle for a moment: that round is lost (both out: the one out longer, then the one further off).
		float Away[2] = {0.0f, 0.0f};
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(GetSuddenDeathPawn(Arena, Side));
			const bool bOut = Character && !Character->IsEliminated() && !Zone->IsInside(Character->GetActorLocation());
			Sudden.OutSeconds[Side] = bOut ? Sudden.OutSeconds[Side] + 0.1f : 0.0f;
			Away[Side] = Character ? static_cast<float>(FVector::Dist2D(Character->GetActorLocation(), Sudden.Center)) : 0.0f;
		}
		const bool bLost[2] = {Sudden.OutSeconds[0] >= AChaosImpactSuddenDeathZone::OutSeconds,
			Sudden.OutSeconds[1] >= AChaosImpactSuddenDeathZone::OutSeconds};
		if (bLost[0] || bLost[1])
		{
			int32 Loser = bLost[0] && !bLost[1] ? 0 : bLost[1] && !bLost[0] ? 1
				: Sudden.OutSeconds[0] != Sudden.OutSeconds[1] ? (Sudden.OutSeconds[0] > Sudden.OutSeconds[1] ? 0 : 1)
				: (Away[0] >= Away[1] ? 0 : 1);
			Decided.Emplace(Arena, 1 - Loser);
			continue;
		}
		// Special balls of its three kinds keep appearing on the floor still in play (a few at a time).
		Sudden.Balls.RemoveAll([](const TWeakObjectPtr<AChaosImpactBall>& Ball) { return !Ball.IsValid() || !Ball->IsPickup(); });
		if (Now < Sudden.NextBallAt || Sudden.Kinds.IsEmpty() || Sudden.Balls.Num() >= SuddenDeathMaxBalls)
		{
			continue;
		}
		Sudden.NextBallAt = Now + SuddenDeathBallEvery;
		const float Radius = FMath::Min(Zone->GetRadius(), Sudden.HalfExtent) * 0.85f;
		if (Radius < 120.0f)
		{
			continue;
		}
		const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
		const float Reach = Radius * FMath::Sqrt(FMath::FRand());
		FVector Spot = Sudden.Center + FVector(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach, 0.0f);
		// On the floor there (found from above; nothing comes where there is none).
		FHitResult Floor;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SuddenDeathBall), false);
		if (!World->LineTraceSingleByObjectType(Floor, Spot + FVector(0.0f, 0.0f, 1500.0f), Spot - FVector(0.0f, 0.0f, 1500.0f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query))
		{
			continue;
		}
		Spot = Floor.ImpactPoint + FVector(0.0f, 0.0f, 45.0f);
		const FTransform Where(FRotator::ZeroRotator, Spot);
		if (AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Ball->SetBallType(Sudden.Kinds[FMath::RandHelper(Sudden.Kinds.Num())]);
			Ball->FinishSpawning(Where);
			Ball->MakePickup();
			Sudden.Balls.Add(Ball);
			UE_LOG(LogChaosImpact, Log, TEXT("Sudden death: a %s ball on the floor (%d lying)"), *StaticEnum<EChaosImpactBallType>()->GetNameStringByValue(static_cast<int64>(Ball->GetBallType())), Sudden.Balls.Num());
		}
	}
	for (const TPair<int32, int32>& Result : Decided)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Sudden death (%s): out of the circle; side %d takes it"),
			Result.Key == INDEX_NONE ? TEXT("duel") : *FString::Printf(TEXT("arena %d"), Result.Key), Result.Value);
		if (Result.Key == INDEX_NONE)
		{
			EndDuelRound(Result.Value, EChaosImpactDuelEnd::SuddenDeath);
		}
		else
		{
			EndArenaRound(Result.Key, Result.Value, EChaosImpactDuelEnd::SuddenDeath);
		}
	}
}
