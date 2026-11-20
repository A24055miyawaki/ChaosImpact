#include "ChaosImpactFusionEffects.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDuelBallBalance.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	using EType = EChaosImpactBallType;

	/** A zone of Kind at At, small (Reach of its size), its look turned toward Flier's colour. */
	AChaosImpactHazardZone* FxZone(UWorld* World, const EType Kind, const FVector& At, APawn* Source, AActor* Spared,
		const TSharedPtr<FChaosImpactDuelThrowDamage>& Damage, const EType Flier, const float Reach)
	{
		// (Lightning keeps its own colour mostly: turned far, it read as something else.)
		const float Tint = Kind == EType::Thunder ? 0.25f : ChaosImpactFusion::TintSide;
		return AChaosImpactHazardZone::Detonate(World, Kind, At, Source, Spared, Reach, Damage, Flier, Tint, Reach);
	}

	void FxMine(UWorld* World, const FVector& Near, const FVector& Heading, APawn* Source, const EType Flier)
	{
		FVector Floor;
		if (AChaosImpactGadget::FindFloor(World, Near, Floor))
		{
			if (AChaosImpactGadget* Mine = AChaosImpactGadget::Place(World, EType::Mine, Floor, Heading, Source, Flier, ChaosImpactFusion::TintSide))
			{
				Mine->SetFusedWith(Flier);
			}
		}
	}

	/** The nearest one Source's fused things may strike within Reach of At. */
	AChaosImpactCharacter* NearestTarget(UWorld* World, const FVector& At, const float Reach, const APawn* Source, const AActor* Except = nullptr)
	{
		AChaosImpactCharacter* Best = nullptr;
		float BestDistance = Reach;
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			if (*It == Except || !ChaosImpactFusion::IsFusionTarget(World, *It, Source))
			{
				continue;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), At));
			if (Distance < BestDistance && FMath::Abs(It->GetActorLocation().Z - At.Z) < 400.0f)
			{
				Best = *It;
				BestDistance = Distance;
			}
		}
		return Best;
	}

	/** Everyone Source's fused things may strike within Reach of At, pushed: Push(Victim, Away) gives each one's push. */
	template <typename FPush>
	void PushAround(UWorld* World, const FVector& At, const float Reach, const APawn* Source, const AActor* Except, FPush&& Push)
	{
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			if (*It == Except || !ChaosImpactFusion::IsFusionTarget(World, *It, Source))
			{
				continue;
			}
			const FVector Away = (It->GetActorLocation() - At) * FVector(1.0f, 1.0f, 0.0f);
			if (Away.Size() <= Reach && FMath::Abs(It->GetActorLocation().Z - At.Z) < 350.0f)
			{
				Push(*It, Away.GetSafeNormal());
			}
		}
	}
}

bool ChaosImpactFusion::IsFusionTarget(const UWorld* World, const AActor* Candidate, const APawn* Source)
{
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Candidate);
	return Character && Character != Source && !Character->IsEliminated() && !AChaosImpactGameState::AreTeammates(World, Source, Character);
}

void ChaosImpactFusion::TickCarry(UWorld* World, const EChaosImpactBallType Flier, const EChaosImpactBallType Rider, const FVector& At,
	APawn* Source, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, FFusionCarry& Carry, const float Reach)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (!Carry.bStarted)
	{
		Carry.bStarted = true;
		Carry.LastDrop = At;
	}
	const float Gap = static_cast<float>(FVector::Dist2D(At, Carry.LastDrop));
	const double Now = World->GetTimeSeconds();
	switch (Rider)
	{
	case EType::Fire:
	{
		// A line of small fires on the way, as close together as a fire ball's own. Each burns out a few seconds after it
		// was laid, so a steered drive's line goes out from its tail as it is drawn on (操縦ファイア draws for its whole steering).
		const int32 Most = Flier == EType::Drive ? 80 : ChaosImpactBallTypes::FireTrailMaxPatches;
		if (Carry.Drops < Most && Gap > ChaosImpactBallTypes::FireTrailSpacing)
		{
			Carry.LastDrop = At;
			++Carry.Drops;
			if (AChaosImpactHazardZone* Patch = AChaosImpactHazardZone::SpawnFireTrail(World, At, Source, Carry.FireLeader.Get(), Damage))
			{
				if (!Carry.FireLeader.IsValid())
				{
					Carry.FireLeader = Patch;
				}
			}
		}
		break;
	}
	case EType::Ice:
		// Patches of frozen floor (catching whoever stands on them as they form).
		if (Carry.Drops < 6 && Gap > 240.0f)
		{
			Carry.LastDrop = At;
			++Carry.Drops;
			FxZone(World, EType::Ice, At, Source, nullptr, Damage, Flier, 0.35f * Reach);
		}
		break;
	case EType::Smoke:
		// A wall of smoke drawn along the way.
		if (Carry.Drops < 7 && Gap > 220.0f)
		{
			Carry.LastDrop = At;
			++Carry.Drops;
			FxZone(World, EType::Smoke, At, Source, nullptr, Damage, Flier, 0.4f * Reach);
		}
		break;
	case EType::Thunder:
		// A crackle that strikes someone near now and then (a ring lights first where it will).
		if (Carry.Zaps < 4 && Now >= Carry.NextZapAt)
		{
			if (AChaosImpactCharacter* Target = NearestTarget(World, At, 430.0f * Reach, Source))
			{
				Carry.NextZapAt = Now + 0.9;
				++Carry.Zaps;
				AChaosImpactFusionStrike::Spawn(World, EType::Thunder, Target->GetActorLocation(), 0.45f, Source, Damage, Flier);
			}
		}
		break;
	case EType::Wind:
		// A whirl round it pushing aside whoever it passes (once each, no hit).
		PushAround(World, At, 230.0f * Reach, Source, nullptr, [&Carry, &At](AChaosImpactCharacter* Victim, const FVector& Away)
		{
			if (!Carry.Touched.Contains(Victim))
			{
				Carry.Touched.Add(Victim);
				Victim->ApplyBlastKnockback(Away * 820.0f + FVector(0.0f, 0.0f, 320.0f));
			}
		});
		break;
	case EType::Mine:
		// A beam lays a line of mines along its way (光の地雷原).
		if (Flier == EType::Beam && Carry.Mines < 3 && Gap > 300.0f)
		{
			Carry.LastDrop = At;
			++Carry.Mines;
			FxMine(World, At, FVector::ForwardVector, Source, Flier);
		}
		break;
	default:
		break;
	}
}

bool ChaosImpactFusion::IsStrikeRider(const EChaosImpactBallType Rider)
{
	switch (Rider)
	{
	case EType::Fire: case EType::Ice: case EType::Smoke: case EType::Thunder: case EType::Wind: case EType::Black: case EType::Snow:
	case EType::Mine: case EType::Prison:
		return true;
	default:
		return false;
	}
}

void ChaosImpactFusion::OnStrike(UWorld* World, const EChaosImpactBallType Flier, const EChaosImpactBallType Rider,
	AChaosImpactCharacter* Victim, const FVector& Heading, APawn* Source, TSharedPtr<FChaosImpactDuelThrowDamage> Damage,
	FFusionCarry* Carry)
{
	if (!World || World->GetNetMode() == NM_Client || !IsFusionTarget(World, Victim, Source))
	{
		return;
	}
	const FVector At = Victim->GetActorLocation();
	FVector Way = Heading.GetSafeNormal2D();
	if (Way.IsNearlyZero())
	{
		Way = Source ? (At - Source->GetActorLocation()).GetSafeNormal2D() : FVector::ForwardVector;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Fusion strike: %s with %s on %s"), ChaosImpactBallTypes::GetInternalName(Flier),
		ChaosImpactBallTypes::GetInternalName(Rider), *Victim->GetName());
	switch (Rider)
	{
	case EType::Fire:
		// Set burning: a fire under their feet.
		AChaosImpactHazardZone::SpawnFireTrail(World, At, Source, nullptr, Damage);
		break;
	case EType::Ice:
		Victim->ApplyIceFreeze(1.2f);
		break;
	case EType::Smoke:
		Victim->ApplyBlind(3.0f);
		break;
	case EType::Thunder:
		// A shock on them (its lightning catches whoever stands close; under the one-hit rule not them again).
		FxZone(World, EType::Thunder, At, Source, SparedVictim(World, Victim), Damage, Flier, 0.4f);
		break;
	case EType::Wind:
	{
		// Blown aside, off the way it was going.
		const FVector Side = FVector::CrossProduct(Way, FVector::UpVector) * (FMath::RandBool() ? 1.0f : -1.0f);
		Victim->ApplyBlastKnockback((Side * 0.85f + Way * 0.35f).GetSafeNormal() * 980.0f + FVector(0.0f, 0.0f, 380.0f));
		break;
	}
	case EType::Black:
		// Whoever stands near them is drawn in to them.
		// (Under the one-hit rule no black hole of its own on them: its burning middle would hit them a second time.)
		PushAround(World, At, 460.0f, Source, Victim, [](AChaosImpactCharacter* Other, const FVector& Away)
		{
			Other->ApplyBlastKnockback(-Away * 680.0f + FVector(0.0f, 0.0f, 160.0f));
		});
		if (!IsOneHitRule(World))
		{
			FxZone(World, EType::Black, At, Source, nullptr, Damage, Flier, 0.35f);
		}
		break;
	case EType::Snow:
		// Buried in snow for a moment.
		Victim->ApplyIceFreeze(0.8f);
		FxZone(World, EType::Snow, At, Source, SparedVictim(World, Victim), Damage, Flier, 0.4f);
		break;
	case EType::Swap:
		if (!Carry || !Carry->bSwapped)
		{
			if (Carry)
			{
				Carry->bSwapped = true;
			}
			if (AChaosImpactCharacter* Thrower = Cast<AChaosImpactCharacter>(Source))
			{
				Thrower->SwapPlacesWith(Victim);
			}
		}
		break;
	case EType::Mine:
		// A mine stuck on them (it goes off a moment later: a dash gets clear).
		if (!Carry || Carry->Mines < 2)
		{
			if (Carry)
			{
				++Carry->Mines;
			}
			FxMine(World, At, Way, Source, Flier);
		}
		break;
	case EType::Simae:
		if (!Carry || !Carry->bFlock)
		{
			if (Carry)
			{
				Carry->bFlock = true;
			}
			AChaosImpactSimaeBird::ReleaseFlock(World, At, Source, Victim, Damage);
		}
		break;
	case EType::Prison:
		if (Flier == EType::Wind)
		{
			// 竜巻の檻: shut in as the whirl lets them go.
			FTimerHandle Handle;
			TWeakObjectPtr<AChaosImpactCharacter> WeakVictim(Victim);
			TWeakObjectPtr<APawn> WeakSource(Source);
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakVictim, WeakSource]()
			{
				Imprison(WeakVictim.Get(), WeakSource.Get());
			}), 0.9f, false);
		}
		else
		{
			// 一列の檻, 捕獲フック, 鳥かご, 護送の檻, 檻の体当たり…: shut in on the spot.
			Imprison(Victim, Source);
		}
		break;
	default:
		break;
	}
}

void ChaosImpactFusion::OnBounce(UWorld* World, const EChaosImpactBallType Flier, const EChaosImpactBallType Rider, const FVector& At,
	APawn* Source, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, FFusionCarry& Carry)
{
	if (!World || World->GetNetMode() == NM_Client || Carry.Bounces >= 6)
	{
		return;
	}
	++Carry.Bounces;
	switch (Rider)
	{
	case EType::Fire:
		AChaosImpactHazardZone::SpawnFireTrail(World, At, Source, Carry.FireLeader.Get(), Damage);
		break;
	case EType::Ice:
		FxZone(World, EType::Ice, At, Source, nullptr, Damage, Flier, 0.35f);
		break;
	case EType::Smoke:
		FxZone(World, EType::Smoke, At, Source, nullptr, Damage, Flier, 0.4f);
		break;
	case EType::Black:
		FxZone(World, EType::Black, At, Source, nullptr, Damage, Flier, 0.32f);
		break;
	case EType::Snow:
		FxZone(World, EType::Snow, At, Source, nullptr, Damage, Flier, 0.45f);
		break;
	case EType::Thunder:
		FxZone(World, EType::Thunder, At, Source, nullptr, Damage, Flier, 0.4f);
		break;
	case EType::Mine:
		if (Carry.Mines < 2)
		{
			++Carry.Mines;
			FxMine(World, At, FVector::ForwardVector, Source, Flier);
		}
		break;
	case EType::Wind:
		PushAround(World, At, 260.0f, Source, nullptr, [](AChaosImpactCharacter* Victim, const FVector& Away)
		{
			Victim->ApplyBlastKnockback(Away * 760.0f + FVector(0.0f, 0.0f, 300.0f));
		});
		break;
	case EType::Simae:
		if (!Carry.bFlock)
		{
			Carry.bFlock = true;
			AChaosImpactSimaeBird::ReleaseFlock(World, At, Source, nullptr, Damage);
		}
		break;
	case EType::Nova:
		// 雷神ノヴァ: lightning crashes down round every rebound.
		FxZone(World, EType::Thunder, At, Source, nullptr, Damage, Flier, 0.75f);
		break;
	default:
		break;
	}
}

bool ChaosImpactFusion::ApplyEndVariant(UWorld* World, const EChaosImpactBallType Flier, const EChaosImpactBallType Rider, const FVector& At,
	APawn* Source, AActor* Victim, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, const float Reach)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	const auto Ring = [&](const int32 Count, const float Radius, TFunctionRef<void(const FVector&)> Each)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float A = UE_TWO_PI * Index / Count;
			Each(At + FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * Radius * Reach);
		}
	};
	const auto Strikes = [&](const int32 Count, const float Radius, const float First, const float Step)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float A = FMath::FRandRange(0.0f, UE_TWO_PI);
			const float D = FMath::FRandRange(0.2f, 1.0f) * Radius * Reach;
			AChaosImpactFusionStrike::Spawn(World, EType::Thunder, At + FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * D,
				First + Step * Index, Source, Damage, Flier);
		}
	};
	AChaosImpactCharacter* Struck = Cast<AChaosImpactCharacter>(Victim);
	// (What the ball struck is left out of these bursts only under the one-hit rule.)
	AActor* const Spared = SparedVictim(World, Victim);
	// A swap: what rides along comes where the other one landed (and for smoke, where both stand).
	if (Flier == EType::Swap && Struck && !Struck->IsEliminated())
	{
		const FVector Landed = Struck->GetActorLocation();
		switch (Rider)
		{
		case EType::Ice:
			Struck->ApplyIceFreeze(1.4f);
			FxZone(World, EType::Ice, Landed, Source, Spared, Damage, Flier, 0.6f);
			return true;
		case EType::Smoke:
			FxZone(World, EType::Smoke, Landed, Source, Spared, Damage, Flier, 0.8f);
			if (Source)
			{
				FxZone(World, EType::Smoke, Source->GetActorLocation(), Source, Spared, Damage, Flier, 0.8f);
			}
			return true;
		case EType::Mine:
			FxMine(World, Landed, FVector::ForwardVector, Source, Flier);
			return true;
		case EType::Fire: case EType::Black:
			FxZone(World, Rider, Landed, Source, Spared, Damage, Flier, 1.0f);
			return true;
		default:
			break;
		}
	}
	// A steered swap that met nobody: its thrower is taken to where it ended (操縦スワップ).
	if (Flier == EType::Drive && Rider == EType::Swap && !Struck && Source)
	{
		Source->TeleportTo(At + FVector(0.0f, 0.0f, 90.0f), Source->GetActorRotation());
		return true;
	}
	if (Flier == EType::Fire && Rider == EType::Ice)
	{
		// 蒸気爆発: the two meet in a burst of white steam that hides the view.
		FxZone(World, EType::Smoke, At, Source, Spared, Damage, EType::Ice, 1.1f * Reach);
		return true;
	}
	if (Flier == EType::Fire && Rider == EType::Black)
	{
		// 火炎の渦: the black hole's whole round burning.
		FxZone(World, EType::Black, At, Source, Spared, Damage, Flier, Reach);
		Ring(8, 230.0f, [&](const FVector& Spot) { AChaosImpactHazardZone::SpawnFireTrail(World, Spot + FVector(0.0f, 0.0f, 60.0f), Source, nullptr, Damage); });
		return true;
	}
	if (Flier == EType::Ice && Rider == EType::Black)
	{
		// 凍てつく渦: frozen floor round the black hole (hard to get out of).
		FxZone(World, EType::Black, At, Source, Spared, Damage, Flier, Reach);
		Ring(6, 260.0f, [&](const FVector& Spot) { FxZone(World, EType::Ice, Spot, Source, Spared, Damage, Flier, 0.32f); });
		return true;
	}
	if (Flier == EType::Thunder && Rider == EType::Black)
	{
		// 雷の渦: lightning striking inside the black hole.
		FxZone(World, EType::Black, At, Source, Spared, Damage, Flier, Reach);
		Strikes(3, 260.0f, 0.6f, 0.6f);
		return true;
	}
	if (Flier == EType::Thunder && Rider == EType::Smoke)
	{
		// 雷雲: a storm cloud, its lightning striking inside now and then (each lit first).
		FxZone(World, EType::Smoke, At, Source, Spared, Damage, Flier, Reach);
		Strikes(4, 300.0f, 0.7f, 0.55f);
		return true;
	}
	if (Flier == EType::Black && Rider == EType::Smoke)
	{
		// 闇の霧: smoke drawn in to its middle (the black hole under it).
		FxZone(World, EType::Smoke, At, Source, Spared, Damage, Flier, 1.15f * Reach);
		return true;
	}
	if (Flier == EType::Ice && Rider == EType::Smoke)
	{
		// 冷気の霧: freezing fog, its floor slippery.
		FxZone(World, EType::Smoke, At, Source, Spared, Damage, Flier, Reach);
		Ring(4, 170.0f, [&](const FVector& Spot) { FxZone(World, EType::Ice, Spot, Source, Spared, Damage, Flier, 0.3f); });
		return true;
	}
	return false;
}

void ChaosImpactFusion::ApplyMineVariant(UWorld* World, const EChaosImpactBallType Flier, const FVector& At, APawn* Source,
	AChaosImpactCharacter* Victim)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const TSharedPtr<FChaosImpactDuelThrowDamage> Damage = FChaosImpactDuelThrowDamage::Create(World);
	AChaosImpactCharacter* const Spared = IsOneHitRule(World) ? Victim : nullptr;
	UE_LOG(LogChaosImpact, Log, TEXT("Fusion mine: %s goes off with it"), ChaosImpactBallTypes::GetInternalName(Flier));
	switch (Flier)
	{
	case EType::Fire:
		// 焼夷地雷: burning floor after.
		FxZone(World, EType::Fire, At, Source, Spared, Damage, Flier, 0.8f);
		break;
	case EType::Ice:
		// 冷凍地雷: frozen where it went off.
		FxZone(World, EType::Ice, At, Source, Spared, Damage, Flier, 0.7f);
		if (Victim)
		{
			Victim->ApplyIceFreeze(1.4f);
		}
		break;
	case EType::Black:
		// 吸引地雷: a black hole opens on it.
		FxZone(World, EType::Black, At, Source, Spared, Damage, Flier, 0.6f);
		break;
	case EType::Smoke:
		FxZone(World, EType::Smoke, At, Source, Spared, Damage, Flier, 0.8f);
		break;
	case EType::Thunder:
		// 連鎖地雷: its lightning jumps to the next one near.
		FxZone(World, EType::Thunder, At, Source, Spared, Damage, Flier, 0.6f);
		if (AChaosImpactCharacter* Next = NearestTarget(World, At, 700.0f, Source, Victim))
		{
			AChaosImpactFusionStrike::Spawn(World, EType::Thunder, Next->GetActorLocation(), 0.35f, Source, Damage, Flier);
		}
		break;
	case EType::Wind:
		// つむじ地雷: a little tornado instead of a push.
		AChaosImpactTornado::Release(World, At + FVector(0.0f, 0.0f, 20.0f), FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f),
			Source, EType::Normal, EType::Mine, ChaosImpactFusion::TintSide);
		break;
	case EType::Snow:
		FxZone(World, EType::Snow, At, Source, Spared, Damage, Flier, 1.4f);
		break;
	case EType::Simae:
		// 鳥の巣地雷: a flock bursts out.
		AChaosImpactSimaeBird::ReleaseFlock(World, At, Source, Victim, Damage);
		break;
	case EType::Prison:
		// 檻の地雷: whoever set it off is shut in.
		Imprison(Victim, Source);
		break;
	case EType::Swap:
		// 入れ替え地雷: whoever set it off changes places with its owner.
		if (AChaosImpactCharacter* Owner = Cast<AChaosImpactCharacter>(Source); Owner && Victim && !Victim->IsEliminated())
		{
			Owner->SwapPlacesWith(Victim);
		}
		break;
	default:
		break;
	}
}

// ---- The warned strike.

AChaosImpactFusionStrike::AChaosImpactFusionStrike()
{
	bReplicates = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AChaosImpactFusionStrike* AChaosImpactFusionStrike::Spawn(UWorld* World, const EChaosImpactBallType Kind, const FVector& At, const float Delay,
	APawn* Source, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, const EChaosImpactBallType Tint, const float Reach)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	// On the floor under it.
	FVector Floor = At;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FusionStrikeFloor), false);
	if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0f, 0.0f, 60.0f), At - FVector(0.0f, 0.0f, 900.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query))
	{
		Floor = Hit.ImpactPoint;
	}
	const FTransform Where(FRotator::ZeroRotator, Floor);
	AChaosImpactFusionStrike* Strike = World->SpawnActorDeferred<AChaosImpactFusionStrike>(StaticClass(), Where, nullptr, Source,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Strike)
	{
		return nullptr;
	}
	Strike->Kind = Kind;
	Strike->Delay = FMath::Max(0.15f, Delay);
	Strike->Tint = Tint;
	Strike->Reach = Reach;
	Strike->Source = Source;
	Strike->Damage = Damage;
	Strike->FinishSpawning(Where);
	return Strike;
}

void AChaosImpactFusionStrike::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(Delay + 2.0f);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Where it will strike: a ring of its colour closing in on the spot, a glow growing under it.
	if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(this, GetActorLocation() + FVector(0.0f, 0.0f, 4.0f)))
	{
		const FLinearColor Color = ChaosImpactBallTypes::GetColor(Kind);
		FChaosImpactFxPiece Ring;
		Ring.Look = EChaosImpactFxLook::Ring;
		Ring.Life = Delay + 0.08f;
		Ring.SizeFrom = 260.0f * Reach / 0.45f;
		Ring.SizeTo = 140.0f * Reach / 0.45f;
		Ring.Color = Color;
		Ring.CoreColor = FLinearColor::White;
		Ring.Intensity = 4.0f;
		Ring.Attack = 0.2f;
		Ring.Hold = 0.85f;
		Ring.RingFrom = 0.9f;
		Ring.RingTo = 0.75f;
		Ring.RingWidth = 0.08f;
		Fx->Add(Ring);
		FChaosImpactFxPiece Glow;
		Glow.Look = EChaosImpactFxLook::GroundGlow;
		Glow.Life = Delay + 0.08f;
		Glow.SizeFrom = 80.0f;
		Glow.SizeTo = 230.0f * Reach / 0.45f;
		Glow.Color = Color;
		Glow.CoreColor = FLinearColor::White;
		Glow.Intensity = 2.5f;
		Glow.Attack = 0.85f;
		Fx->Add(Glow);
	}
}

void AChaosImpactFusionStrike::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (!HasAuthority() || bStruck || Age < Delay)
	{
		return;
	}
	bStruck = true;
	AChaosImpactHazardZone::Detonate(GetWorld(), Kind, GetActorLocation() + FVector(0.0f, 0.0f, 40.0f), Source.Get(), nullptr, Reach,
		Damage, Tint, Kind == EChaosImpactBallType::Thunder ? 0.25f : ChaosImpactFusion::TintSide, Reach);
	SetLifeSpan(0.6f);
}

void AChaosImpactFusionStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AChaosImpactFusionStrike, Kind, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactFusionStrike, Delay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactFusionStrike, Tint, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AChaosImpactFusionStrike, Reach, COND_InitialOnly);
}
