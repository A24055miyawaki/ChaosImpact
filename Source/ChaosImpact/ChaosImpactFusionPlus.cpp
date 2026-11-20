// 合体ボール, 2026-11-19 (Docs/合体ボール追加企画.md): the pairs added last, and the 強化ボール (two of the same: 超〇〇).
//  - ノヴァ: never weaker than a nova. Charged over the head and dropped as ever with most (the other's effect round its
//    blast); with those that change how it goes (ビーム, ドライブ, アンカー, フック, ウィンド, サンダー, スノー) they fly and every
//    hit is a nova's, and a full nova's blast goes off where they end.
//  - プリズン: whoever it reaches is shut in a shell (the flier's own effect with it).
//  - バリア: walls with the other's nature (炎の壁, 電撃バリア…).
//  - グラップル (フック＋アンカー): the anchor bites into the first opponent on its line and its thrower rams them.
//  - 強化ボール: each kind bigger, longer, or more of it.
// Everything here is the server's.

#include "ChaosImpactFusionEffects.h"

#include "ChaosImpact.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	using EType = EChaosImpactBallType;

	AChaosImpactHazardZone* Zone(UWorld* World, const EType Kind, const FVector& At, APawn* Source, AActor* Spared,
		const TSharedPtr<FChaosImpactDuelThrowDamage>& Damage, const EType Tint, const float Reach)
	{
		return AChaosImpactHazardZone::Detonate(World, Kind, At, Source, Spared, Reach, Damage, Tint,
			Kind == EType::Thunder ? 0.25f : ChaosImpactFusion::TintSide, Reach);
	}

	/** Source's targets within Radius of At (and beyond MinDistance), nearest first. */
	TArray<AChaosImpactCharacter*> TargetsAround(UWorld* World, const FVector& At, const float Radius, const APawn* Source,
		const float MinDistance = 0.0f, const AActor* Except = nullptr)
	{
		TArray<AChaosImpactCharacter*> Found;
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			if (*It == Except || !ChaosImpactFusion::IsFusionTarget(World, *It, Source))
			{
				continue;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), At));
			if (Distance <= Radius && Distance >= MinDistance && FMath::Abs(It->GetActorLocation().Z - At.Z) < 600.0f)
			{
				Found.Add(*It);
			}
		}
		Found.Sort([&At](const AChaosImpactCharacter& A, const AChaosImpactCharacter& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), At) < FVector::DistSquared2D(B.GetActorLocation(), At);
		});
		return Found;
	}

	void PlaceMine(UWorld* World, const FVector& Near, const FVector& Heading, APawn* Source, const EType With)
	{
		FVector Floor;
		if (AChaosImpactGadget::FindFloor(World, Near, Floor))
		{
			if (AChaosImpactGadget* Mine = AChaosImpactGadget::Place(World, EType::Mine, Floor, Heading, Source, With,
				With == EType::Normal ? 0.0f : ChaosImpactFusion::TintSide, true); Mine && With != EType::Normal)
			{
				Mine->SetFusedWith(With);
			}
		}
	}

	/** Something done Seconds from now (the server's world clock), if the world is still there. */
	void Later(UWorld* World, const float Seconds, TFunction<void(UWorld*)> Then)
	{
		if (!World)
		{
			return;
		}
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Then = MoveTemp(Then)]()
		{
			if (UWorld* Still = WeakWorld.Get())
			{
				Then(Still);
			}
		}), FMath::Max(Seconds, 0.01f), false);
	}

	FVector Turned(const FVector& Way, const float Degrees)
	{
		return Way.RotateAngleAxis(Degrees, FVector::UpVector).GetSafeNormal2D();
	}

	FVector FlatWay(const FVector& Way, const FVector& At, const APawn* Source)
	{
		FVector Flat = Way.GetSafeNormal2D();
		if (Flat.IsNearlyZero() && Source)
		{
			Flat = (At - Source->GetActorLocation()).GetSafeNormal2D();
		}
		return Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
	}

	/** Their places passed round: each goes where the next one stood (the last where the first did). */
	void RotatePlaces(const TArray<AChaosImpactCharacter*>& Who)
	{
		if (Who.Num() < 2)
		{
			return;
		}
		// (Swapped along the line: the first ends where the second stood, the second where the third did… the last where
		// the first did.)
		for (int32 Index = 0; Index + 1 < Who.Num(); ++Index)
		{
			Who[Index]->SwapPlacesWith(Who[Index + 1]);
		}
	}
}

bool ChaosImpactFusion::Imprison(AChaosImpactCharacter* Victim, APawn* Source)
{
	AChaosImpactCharacter* Captor = Cast<AChaosImpactCharacter>(Source);
	if (!Victim || !Captor || !Victim->GetBallForm() || Victim->IsEliminated())
	{
		return false;
	}
	const bool bShut = Victim->GetBallForm()->Capture(Captor);
	UE_LOG(LogChaosImpact, Log, TEXT("Fusion prison: %s %s"), *Victim->GetName(), bShut ? TEXT("shut in") : TEXT("not shut in"));
	return bShut;
}

int32 ChaosImpactFusion::ImprisonAround(UWorld* World, const FVector& At, const float Radius, APawn* Source, const int32 Most,
	const float MinDistance, const AActor* Except)
{
	int32 Shut = 0;
	for (AChaosImpactCharacter* One : TargetsAround(World, At, Radius, Source, MinDistance, Except))
	{
		if (Shut >= Most)
		{
			break;
		}
		Shut += Imprison(One, Source) ? 1 : 0;
	}
	return Shut;
}

AChaosImpactGadget* ChaosImpactFusion::PlaceFusedBarrier(UWorld* World, const FVector& Near, const FVector& Heading, APawn* Source,
	const EChaosImpactBallType With, const float LifeScale, const int32 Hits)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	const FVector Way = FlatWay(Heading, Near, Source);
	const FVector Across = FVector::CrossProduct(Way, FVector::UpVector).GetSafeNormal();
	const auto One = [&](const FVector& Spot, const FVector& Facing, const float Life, const int32 Took) -> AChaosImpactGadget*
	{
		FVector Floor;
		if (!AChaosImpactGadget::FindFloor(World, Spot, Floor))
		{
			return nullptr;
		}
		AChaosImpactGadget* Wall = AChaosImpactGadget::Place(World, EType::Barrier, Floor, Facing, Source, With,
			With == EType::Normal || With == EType::Barrier ? 0.0f : TintSide);
		if (Wall)
		{
			Wall->SetBarrierVariant(With, Life, Took);
		}
		return Wall;
	};
	switch (With)
	{
	case EType::Beam:
	{
		// 長い光の壁: three walls end to end along the way it was thrown (a long thin wall).
		AChaosImpactGadget* First = nullptr;
		for (int32 Index = -1; Index <= 1; ++Index)
		{
			AChaosImpactGadget* Wall = One(Near + Way * (Index * (AChaosImpactGadget::BarrierWidth - 20.0f)), Across, LifeScale, Hits);
			First = First ? First : Wall;
		}
		return First;
	}
	case EType::Ice: case EType::Snow:
		// 氷の壁・雪の壁: stops two balls, lasts longer.
		return One(Near, Way, LifeScale * 1.5f, FMath::Max(Hits, 2));
	case EType::Fire:
	{
		// 炎の壁: burning floor along both its sides.
		AChaosImpactGadget* Wall = One(Near, Way, LifeScale, Hits);
		for (const float Side : {-1.0f, 1.0f})
		{
			for (const float Along : {-110.0f, 0.0f, 110.0f})
			{
				AChaosImpactHazardZone::SpawnFireTrail(World, Near + Way * Side * 120.0f + Across * Along + FVector(0.0f, 0.0f, 60.0f), Source,
					nullptr, nullptr);
			}
		}
		return Wall;
	}
	case EType::Smoke:
	{
		// 煙の壁: smoke on its thrower's side (they are hard to see behind it).
		AChaosImpactGadget* Wall = One(Near, Way, LifeScale, Hits);
		Zone(World, EType::Smoke, Near - Way * 200.0f, Source, nullptr, nullptr, EType::Barrier, 0.9f);
		return Wall;
	}
	case EType::Wind:
		// 風の壁: balls glance off it and walkers are blown back; it stands its whole time.
		return One(Near, Way, LifeScale, 99);
	default:
		return One(Near, Way, LifeScale, Hits);
	}
}

float ChaosImpactFusion::GetNovaRideScale(const APawn* Source)
{
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Source);
	const float Scale = Character ? Character->GetFusedNovaScale() : 0.0f;
	return Scale > 0.0f ? Scale : ChaosImpactBallTypes::GetNovaScale(0.7f);
}

void ChaosImpactFusion::NovaBlast(UWorld* World, const FVector& At, APawn* Source, const float Scale, const EChaosImpactBallType Tint)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Fusion nova blast at %s (scale %.1f)"), *At.ToCompactString(), Scale);
	AChaosImpactHazardZone::Detonate(World, EType::Nova, At, Source, nullptr, Scale, nullptr, Tint, Tint == EType::Normal ? 0.0f : TintSide);
}

void ChaosImpactFusion::TickNovaCharge(AChaosImpactCharacter* Charger, const EChaosImpactBallType Rider, const float DeltaSeconds,
	const bool bJustStarted)
{
	UWorld* World = Charger ? Charger->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const FVector At = Charger->GetActorLocation();
	if (Rider == EType::Black)
	{
		// 特異点ノヴァ: opponents near are drawn in while it is charged (a dash gets clear).
		for (AChaosImpactCharacter* One : TargetsAround(World, At, 1300.0f, Charger, 160.0f))
		{
			if (!One->IsDashing())
			{
				const FVector In = (At - One->GetActorLocation()).GetSafeNormal2D();
				One->LaunchCharacter(In * 260.0f, false, false);
			}
		}
	}
	else if ((Rider == EType::Barrier) && bJustStarted)
	{
		// 守護ノヴァ: walls stand round the one charging it, facing out.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector Out = FVector::ForwardVector.RotateAngleAxis(45.0f + 90.0f * Index, FVector::UpVector);
			PlaceFusedBarrier(World, At + Out * 230.0f, Out, Charger, EType::Nova);
		}
	}
}

bool ChaosImpactFusion::ApplyPlusEnd(UWorld* World, const EChaosImpactBallType Flier, const EChaosImpactBallType Rider, const FVector& At,
	const FVector& Heading, APawn* Source, AActor* Victim, TSharedPtr<FChaosImpactDuelThrowDamage> Damage, UClass* BallClass, const float Reach)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	const FVector Way = FlatWay(Heading, At, Source);
	AActor* const Spared = SparedVictim(World, Victim);
	AChaosImpactCharacter* Struck = Cast<AChaosImpactCharacter>(Victim);
	TWeakObjectPtr<APawn> WeakSource(Source);

	// ---- 強化ボール (two of the same).
	if (Flier == Rider)
	{
		switch (Flier)
		{
		case EType::Fire:
			// 獄炎: a wide fire, and a ring of burning floor round it.
			Zone(World, EType::Fire, At, Source, Spared, Damage, EType::Fire, 1.7f);
			for (int32 Index = 0; Index < 8; ++Index)
			{
				AChaosImpactHazardZone::SpawnFireTrail(World, At + Turned(Way, 45.0f * Index) * 320.0f + FVector(0.0f, 0.0f, 60.0f), Source, nullptr, Damage);
			}
			return true;
		case EType::Ice:
			// 絶対零度: a wide freeze, everyone near held long.
			Zone(World, EType::Ice, At, Source, Spared, Damage, EType::Ice, 1.8f);
			for (AChaosImpactCharacter* One : TargetsAround(World, At, 650.0f, Source))
			{
				One->ApplyIceFreeze(2.5f);
			}
			return true;
		case EType::Thunder:
			// 迅雷 (it split on its way): a wide crack of lightning where it ends.
			Zone(World, EType::Thunder, At, Source, Spared, Damage, EType::Thunder, 1.4f);
			return true;
		case EType::Black:
		{
			// 特異点: a wide, strong black hole that bursts as it closes.
			Zone(World, EType::Black, At, Source, Spared, Damage, EType::Black, 1.8f);
			Later(World, 3.2f, [At, WeakSource, Damage](UWorld* Still)
			{
				Zone(Still, EType::Fire, At, WeakSource.Get(), nullptr, Damage, EType::Black, 1.5f);
			});
			return true;
		}
		case EType::Smoke:
			// 濃霧: wide and long.
			Zone(World, EType::Smoke, At, Source, Spared, Damage, EType::Smoke, 1.9f);
			return true;
		case EType::Snow:
			Zone(World, EType::Snow, At, Source, Spared, Damage, EType::Snow, 1.6f);
			return true;
		case EType::Simae:
			// 大群: a second flock (the ball's own burst let out the first).
			return false;
		case EType::Prison:
			// 連鎖の檻: the one nearest to whoever it shut in is shut in too.
			ImprisonAround(World, Struck ? Struck->GetActorLocation() : At, 520.0f, Source, 1, 0.0f, Struck);
			return true;
		case EType::Swap:
			// ぐるぐる入れ替え: the one it struck (at its thrower's old place now) and the one nearest them pass places round.
			if (Struck && !Struck->IsEliminated())
			{
				TArray<AChaosImpactCharacter*> Round{Struck};
				for (AChaosImpactCharacter* One : TargetsAround(World, Struck->GetActorLocation(), 1100.0f, Source, 0.0f, Struck))
				{
					Round.Add(One);
					if (Round.Num() >= 3)
					{
						break;
					}
				}
				RotatePlaces(Round);
			}
			return true;
		case EType::Barrier:
			// 要塞: a U of walls (the middle one and two wings), each stopping two balls, standing longer.
			if (!Struck)
			{
				const FVector Across = FVector::CrossProduct(Way, FVector::UpVector).GetSafeNormal();
				PlaceFusedBarrier(World, At, Way, Source, EType::Barrier, 1.5f, 2);
				for (const float Side : {-1.0f, 1.0f})
				{
					const FVector Wing = Turned(Way, Side * 70.0f);
					PlaceFusedBarrier(World, At + Across * Side * (AChaosImpactGadget::BarrierWidth * 0.55f) - Way * 110.0f, Wing, Source,
						EType::Barrier, 1.5f, 2);
				}
			}
			return true;
		case EType::Mine:
			// 地雷原: two more mines spread out ahead (three in all).
			for (const float Side : {-38.0f, 38.0f})
			{
				PlaceMine(World, At + Turned(Way, Side) * 280.0f, Way, Source, EType::Normal);
			}
			return true;
		case EType::Nova:
		{
			// 超新星: a second, wider blast where the first one was, once it has died down.
			const float Scale = Reach;
			Later(World, 2.4f, [At, WeakSource, Scale](UWorld* Still)
			{
				NovaBlast(Still, At, WeakSource.Get(), Scale * 1.25f, EType::Nova);
			});
			return true;
		}
		default:
			// 双竜巻, 三叉光線, 大雪玉, ターボドライブ, ダブルフック, 二段アンカー: all in how it flies.
			return true;
		}
	}

	// ---- A ノヴァ riding along on something that flies: a full nova's blast where it ends (never less than a nova).
	if (Rider == EType::Nova)
	{
		FVector Floor;
		if (AChaosImpactGadget::FindFloor(World, At, Floor) || Flier != EType::Beam)
		{
			NovaBlast(World, Flier == EType::Beam ? Floor : At, Source, GetNovaRideScale(Source), Flier);
		}
		return true;
	}

	// ---- ノヴァ that flies (dropped as ever): the other's effect round its blast.
	if (Flier == EType::Nova)
	{
		const float Scale = Reach > 1.0f ? Reach : GetNovaRideScale(Source);
		const float Blast = ChaosImpactBallTypes::GetNovaBlastRadius(Scale);
		const auto Ring = [&](const int32 Count, const float Radius, TFunctionRef<void(const FVector&, const FVector&)> Each)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector Out = FVector::ForwardVector.RotateAngleAxis(360.0f * Index / Count, FVector::UpVector);
				Each(At + Out * Radius, Out);
			}
		};
		switch (Rider)
		{
		case EType::Fire:
			// 太陽ノヴァ: rings of burning floor spreading out from the blast.
			Ring(12, Blast * 0.95f, [&](const FVector& Spot, const FVector&) { AChaosImpactHazardZone::SpawnFireTrail(World, Spot + FVector(0, 0, 60), Source, nullptr, Damage); });
			Later(World, 0.7f, [At, Blast, WeakSource, Damage](UWorld* Still)
			{
				for (int32 Index = 0; Index < 16; ++Index)
				{
					const FVector Out = FVector::ForwardVector.RotateAngleAxis(22.5f * Index + 11.0f, FVector::UpVector);
					AChaosImpactHazardZone::SpawnFireTrail(Still, At + Out * Blast * 1.25f + FVector(0, 0, 60), WeakSource.Get(), nullptr, Damage);
				}
			});
			return true;
		case EType::Ice:
			// 氷河ノヴァ: the whole round frozen, wider than the blast; whoever is just outside held.
			Zone(World, EType::Ice, At, Source, nullptr, Damage, EType::Nova, FMath::Clamp(Blast / 300.0f, 1.0f, 5.0f));
			for (AChaosImpactCharacter* One : TargetsAround(World, At, Blast * 1.5f, Source))
			{
				One->ApplyIceFreeze(2.2f);
			}
			return true;
		case EType::Black:
			// 特異点ノヴァ: a huge black hole where it was.
			Zone(World, EType::Black, At, Source, nullptr, Damage, EType::Nova, FMath::Clamp(Blast / 420.0f, 1.2f, 4.0f));
			return true;
		case EType::Smoke:
			// 暗雲ノヴァ: a wide smoke over the blast (the field still shows through it).
			Zone(World, EType::Smoke, At, Source, nullptr, Damage, EType::Nova, FMath::Clamp(Blast / 340.0f, 1.0f, 3.2f));
			return true;
		case EType::Simae:
			// 大群ノヴァ: flock after flock out of the blast.
			Ring(4, Blast * 0.6f, [&](const FVector& Spot, const FVector&) { AChaosImpactSimaeBird::ReleaseFlock(World, Spot + FVector(0, 0, 80), Source, nullptr, Damage); });
			return true;
		case EType::Prison:
			// 大監獄ノヴァ: those just outside the blast are shut in.
			ImprisonAround(World, At, Blast * 1.6f, Source, 8, Blast * 0.9f);
			return true;
		case EType::Swap:
		{
			// シャッフルノヴァ: those round the blast change places with each other.
			TArray<AChaosImpactCharacter*> Round = TargetsAround(World, At, Blast * 1.8f, Source, Blast * 0.9f);
			RotatePlaces(Round);
			return true;
		}
		case EType::Barrier:
			// 守護ノヴァ: a ring of walls round the blast, facing out.
			Ring(6, Blast * 1.1f, [&](const FVector& Spot, const FVector& Out) { PlaceFusedBarrier(World, Spot, Out, Source, EType::Nova); });
			return true;
		case EType::Mine:
			// 機雷ノヴァ: mines strewn round the blast's edge.
			Ring(8, Blast * 1.15f, [&](const FVector& Spot, const FVector& Out) { PlaceMine(World, Spot, Out, Source, EType::Nova); });
			return true;
		default:
			return false;
		}
	}

	// ---- プリズン riding along: whoever it reached is shut in, with the flier's own effect.
	if (Rider == EType::Prison)
	{
		AChaosImpactCharacter* Target = Struck;
		if (!Target)
		{
			TArray<AChaosImpactCharacter*> Near = TargetsAround(World, At, 320.0f * Reach, Source);
			Target = Near.Num() > 0 ? Near[0] : nullptr;
		}
		switch (Flier)
		{
		case EType::Black:
			// 重力の檻: everyone its black hole draws in is shut in as it closes.
			Later(World, 1.6f, [At, WeakSource](UWorld* Still) { ImprisonAround(Still, At, 520.0f, WeakSource.Get(), 4); });
			return true;
		case EType::Thunder:
			// 電気の檻: the one struck, and the next one near.
			Imprison(Target, Source);
			ImprisonAround(World, At, 650.0f, Source, 1, 0.0f, Target);
			return true;
		case EType::Smoke:
			if (Target)
			{
				Target->ApplyBlind(4.0f);
			}
			Imprison(Target, Source);
			return true;
		case EType::Ice:
			Imprison(Target, Source);
			Zone(World, EType::Ice, At, Source, nullptr, Damage, EType::Prison, 0.6f);
			return true;
		case EType::Fire:
			Imprison(Target, Source);
			AChaosImpactHazardZone::SpawnFireTrail(World, At + FVector(0, 0, 60), Source, nullptr, Damage);
			return true;
		case EType::Snow:
			Imprison(Target, Source);
			Zone(World, EType::Snow, At, Source, nullptr, Damage, EType::Prison, 0.6f);
			return true;
		case EType::Anchor:
			// 檻ジャンプ: whoever is near where its thrower came down.
			ImprisonAround(World, At, 380.0f, Source, 2);
			return true;
		default:
			// 身代わりの檻 (the one swapped), 護送の檻, 捕獲フック, 一列の檻, 鳥かご, 竜巻の檻…
			Imprison(Target, Source);
			return true;
		}
	}

	// ---- バリア riding along: a wall with the flier's nature.
	if (Rider == EType::Barrier)
	{
		switch (Flier)
		{
		case EType::Beam:
			// 長い光の壁: put up as the light went out.
		case EType::Hook:
			// 守りのフック: its wall stood in front of its thrower as it went out.
			return true;
		case EType::Swap:
			// 入れ替え壁: between the two who changed places.
			if (Struck && Source)
			{
				const FVector Between = (Struck->GetActorLocation() + Source->GetActorLocation()) * 0.5f;
				PlaceFusedBarrier(World, Between, (Struck->GetActorLocation() - Source->GetActorLocation()), Source, EType::Swap);
			}
			return true;
		case EType::Prison:
		{
			// 牢屋の壁: four walls round where it came down; whoever is inside cannot walk out until they go.
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FVector Out = Turned(Way, 90.0f * Index);
				PlaceFusedBarrier(World, At + Out * (AChaosImpactGadget::BarrierWidth * 0.5f), Out, Source, EType::Prison);
			}
			return true;
		}
		default:
			if (!Struck)
			{
				PlaceFusedBarrier(World, At, Way, Source, Flier);
			}
			else
			{
				PlaceFusedBarrier(World, Struck->GetActorLocation() - Way * 160.0f, Way, Source, Flier);
			}
			return true;
		}
	}

	// ---- 地雷付きバリア: a mine on the far side of the wall it put up.
	if (Flier == EType::Barrier && Rider == EType::Mine)
	{
		PlaceMine(World, At + Way * 170.0f, Way, Source, EType::Barrier);
		return true;
	}

	// ---- グラップル: the anchor bit into someone and its thrower rammed them (the ram is the hit).
	if (Flier == EType::Anchor && Rider == EType::Hook)
	{
		return true;
	}
	return false;
}
