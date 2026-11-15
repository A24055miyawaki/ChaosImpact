// パーティルール in a VS match: ルビーラッシュ's rubies and クラウンキープ's crown, kept by the host (see ChaosImpactParty.h).
#include "ChaosImpactGameMode.h"
#include "ChaosImpact.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactStageBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void AChaosImpactGameMode::ClearPartyMatch()
{
	GetWorldTimerManager().ClearTimer(PartyMatchTimer);
	if (AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>())
	{
		if (IsValid(Match->RubyField))
		{
			Match->RubyField->Destroy();
		}
		if (IsValid(Match->PartyCrown))
		{
			Match->PartyCrown->Destroy();
		}
		Match->RubyField = nullptr;
		Match->PartyCrown = nullptr;
		Match->ForceNetUpdate();
	}
	PartyHitDropAt.Reset();
	PartyFieldPoints.Reset();
}

TArray<FVector> AChaosImpactGameMode::BuildPartyFieldPoints() const
{
	TArray<FVector> Points;
	UWorld* World = GetWorld();
	if (!VersusStage || !World)
	{
		return Points;
	}
	// Floor any player can walk to: round the stage's start points and ball spots, on the same level as one of them and
	// in plain sight of it, with room to stand. A grid over the whole square, so they spread evenly.
	TArray<FVector> Seeds;
	TArray<FVector> Starts = VersusStage->GetSpawnPoints();
	Starts.Append(VersusStage->GetBallPoints());
	for (const FVector& Start : Starts)
	{
		FVector Floor;
		if (ChaosImpactParty::FindFloor(World, Start, 150.0f, 500.0f, Floor) && !VersusStage->IsOutside(Floor))
		{
			Seeds.Add(Floor);
		}
	}
	if (Seeds.IsEmpty())
	{
		return Points;
	}
	const FVector Centre = VersusStage->GetCenter();
	const float Half = VersusStage->GetHalfExtent();
	constexpr float Step = 230.0f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyField), false);
	// A grid over the whole square: the floor in each square (on a level near one of the spots round it)...
	TMap<FIntPoint, FVector> Cells;
	const int32 Count = FMath::FloorToInt32((2.0f * Half - 280.0f) / Step) + 1;
	for (int32 IX = 0; IX < Count; ++IX)
	{
		for (int32 IY = 0; IY < Count; ++IY)
		{
			const FVector Spot = Centre + FVector(-Half + 140.0f + IX * Step, -Half + 140.0f + IY * Step, 0.0f);
			// The seeds nearest it, nearest first.
			TArray<const FVector*, TInlineAllocator<64>> Near;
			for (const FVector& Seed : Seeds)
			{
				if (FVector::DistSquared2D(Seed, Spot) < FMath::Square(1900.0f))
				{
					Near.Add(&Seed);
				}
			}
			Near.Sort([&Spot](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, Spot) < FVector::DistSquared2D(B, Spot); });
			for (int32 Index = 0; Index < FMath::Min(Near.Num(), 3); ++Index)
			{
				const FVector& Seed = *Near[Index];
				FVector Floor;
				if (ChaosImpactParty::FindFloor(World, FVector(Spot.X, Spot.Y, Seed.Z), 240.0f, 480.0f, Floor)
					&& FMath::Abs(Floor.Z - Seed.Z) < 400.0f && !VersusStage->IsOutside(Floor) && ChaosImpactParty::IsStandingRoom(World, Floor))
				{
					Cells.Add(FIntPoint(IX, IY), Floor);
					break;
				}
			}
		}
	}
	// ...kept only where a player can get to from the spots, square by square: up no more than a jump (ClimbableRise),
	// down any drop, and nothing too tall to get over in between. Tops of walls, containers and ledges are left out.
	const auto CanStep = [World, &Query](const FVector& From, const FVector& To)
	{
		if (To.Z - From.Z > ChaosImpactParty::ClimbableRise || From.Z - To.Z > 600.0f)
		{
			return false;
		}
		const float Over = static_cast<float>(FMath::Max(From.Z, To.Z)) + ChaosImpactParty::ClimbableRise + 40.0f;
		FHitResult Blocked;
		return !World->LineTraceSingleByObjectType(Blocked, FVector(From.X, From.Y, Over), FVector(To.X, To.Y, Over),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query);
	};
	TSet<FIntPoint> Reached;
	TArray<FIntPoint> Open;
	for (const TPair<FIntPoint, FVector>& Cell : Cells)
	{
		for (const FVector& Seed : Seeds)
		{
			if (FVector::DistSquared2D(Seed, Cell.Value) < FMath::Square(Step * 1.5f) && CanStep(Seed, Cell.Value))
			{
				Reached.Add(Cell.Key);
				Open.Add(Cell.Key);
				break;
			}
		}
	}
	while (!Open.IsEmpty())
	{
		const FIntPoint Here = Open.Pop(EAllowShrinking::No);
		const FVector& From = Cells[Here];
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				const FIntPoint There(Here.X + DX, Here.Y + DY);
				const FVector* To = (DX || DY) && !Reached.Contains(There) ? Cells.Find(There) : nullptr;
				if (To && CanStep(From, *To))
				{
					Reached.Add(There);
					Open.Add(There);
				}
			}
		}
	}
	for (const TPair<FIntPoint, FVector>& Cell : Cells)
	{
		if (Reached.Contains(Cell.Key))
		{
			Points.Add(Cell.Value);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Party field: %d of %d floor squares can be walked to"), Points.Num(), Cells.Num());
	// (A stage with no open floor round its spots at all: the spots themselves.)
	if (Points.Num() < 12)
	{
		Points.Append(Seeds);
	}
	return Points;
}

TArray<FVector> AChaosImpactGameMode::GetPartyEdgeStarts(const int32 Count) const
{
	TArray<FVector> Starts;
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (Count <= 0 || PartyFieldPoints.IsEmpty() || !Match)
	{
		return Starts;
	}
	const FVector Middle = IsValid(Match->PartyCrown) ? FVector(Match->PartyCrown->GetState().To) : FVector(Match->StageCenter);
	// One slice of the circle each (turned at random, so nobody always gets the same corner): the open spot furthest out
	// in it, a little way off any other start.
	const float Turn = FMath::FRandRange(0.0f, UE_TWO_PI);
	const float Slice = UE_TWO_PI / Count;
	// Not right at the edge: about six tenths of the way out from the crown to the furthest floor.
	float Furthest = 0.0f;
	for (const FVector& Point : PartyFieldPoints)
	{
		Furthest = FMath::Max(Furthest, static_cast<float>(FVector::Dist2D(Point, Middle)));
	}
	const float Wanted = FMath::Max(Furthest * ChaosImpactParty::CrownStartShare, 900.0f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Aim = Turn + Index * Slice;
		const FVector2D Way(FMath::Cos(Aim), FMath::Sin(Aim));
		const FVector* Best = nullptr;
		float BestScore = -UE_BIG_NUMBER;
		for (const FVector& Point : PartyFieldPoints)
		{
			const FVector2D Offset(Point.X - Middle.X, Point.Y - Middle.Y);
			const float Distance = static_cast<float>(Offset.Size());
			if (Distance < 1.0f)
			{
				continue;
			}
			// Inside its slice (a little leeway), near the wanted distance, and not on top of another start.
			const float Along = static_cast<float>(FVector2D::DotProduct(Offset / Distance, Way));
			if (Along < FMath::Cos(Slice * 0.6f))
			{
				continue;
			}
			bool bClear = true;
			for (const FVector& Taken : Starts)
			{
				bClear &= FVector::DistSquared2D(Taken, Point) > FMath::Square(320.0f);
			}
			const float Score = -FMath::Abs(Distance - Wanted) + Along * 300.0f;
			if (bClear && Score > BestScore)
			{
				BestScore = Score;
				Best = &Point;
			}
		}
		if (Best)
		{
			Starts.Add(*Best);
		}
	}
	return Starts;
}

void AChaosImpactGameMode::SetupPartyMatch()
{
	ClearPartyMatch();
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !VersusStage || !Match->Rules.IsParty() || !GetWorld())
	{
		return;
	}
	PartyStream.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	PartyFieldPoints = BuildPartyFieldPoints();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Centre = VersusStage->GetCenter();
	if (Match->Rules.IsRubyRush())
	{
		AChaosImpactRubyField* Field = GetWorld()->SpawnActor<AChaosImpactRubyField>(AChaosImpactRubyField::StaticClass(), Centre, FRotator::ZeroRotator, Params);
		if (Field)
		{
			const int32 Competitors = Match->Rules.CPUCount + ChaosImpactMatch::GetCompetingHumans(Match->Rules, Match->CountHumanMembers());
			Field->SetupField(VersusStage, PartyFieldPoints,
				ChaosImpactParty::FieldRubiesBase + ChaosImpactParty::FieldRubiesPerCompetitor * FMath::Clamp(Competitors, 2, ChaosImpactMatch::MaxCompetitors));
			Match->RubyField = Field;
		}
	}
	else
	{
		// The crown starts in the middle of the stage (the open floor nearest it), falling in over the opening.
		const FVector Floor = ChaosImpactParty::NearestPoint(PartyFieldPoints, Centre);
		AChaosImpactCrown* Crown = GetWorld()->SpawnActor<AChaosImpactCrown>(AChaosImpactCrown::StaticClass(), Floor, FRotator::ZeroRotator, Params);
		if (Crown)
		{
			Crown->PlaceAt(Floor, 1.6f);
			Match->PartyCrown = Crown;
		}
	}
	Match->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("Party match set up: %s, %d floor spots"), *ChaosImpactMatch::DescribeRule(Match->Rules), PartyFieldPoints.Num());
}

void AChaosImpactGameMode::UpdatePartyMatch()
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Rules.IsCrownKeep() || Match->Phase != EChaosImpactOnlinePhase::Match)
	{
		return;
	}
	AChaosImpactCrown* Crown = Match->PartyCrown;
	if (!IsValid(Crown))
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	const FChaosImpactCrownState& State = Crown->GetState();
	AChaosImpactCharacter* Holder = State.Holder;
	if (!State.HolderState)
	{
		return;
	}
	// Its holder gone (left the room) or down without a hit (nothing should do that): it falls where they were.
	if (!IsValid(Holder) || Holder->IsEliminated())
	{
		Crown->DropFromHolder(ChaosImpactParty::FindLandSpot(GetWorld(), VersusStage, PartyFieldPoints, PartyCrownLastHolderAt, 150.0f, 400.0f, PartyStream));
		return;
	}
	PartyCrownLastHolderAt = Holder->GetActorLocation();
	AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State.HolderState);
	// A count every ChaosImpactMatch::CrownCountSeconds while it is worn; the goal ends the match at once.
	double CountFrom = State.CountFrom;
	while (Member && Now - CountFrom >= ChaosImpactMatch::CrownCountSeconds)
	{
		CountFrom += ChaosImpactMatch::CrownCountSeconds;
		++Member->Points;
		Member->ForceNetUpdate();
		Crown->StartNextCount(CountFrom);
		if (Member->Points >= ChaosImpactMatch::CrownGoalCounts)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("Party: %s kept the crown for all %d counts"), *Member->GetPlayerName(), ChaosImpactMatch::CrownGoalCounts);
			Crown->Freeze();
			EndMatch();
			return;
		}
	}
}

void AChaosImpactGameMode::NotifyPartyHit(AChaosImpactCharacter* Victim, AActor* DamageCauser, const bool bKnockout, const bool bLeftStage)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Victim || !Match->Rules.IsParty() || Match->Phase != EChaosImpactOnlinePhase::Match || !GetWorld())
	{
		return;
	}
	const double Now = Match->GetServerWorldTimeSeconds();
	// Knocked off the stage: whatever falls from them lands on the open floor nearest where they went.
	const FVector Where = Victim->GetActorLocation();
	const FVector Origin = bLeftStage ? ChaosImpactParty::NearestPoint(PartyFieldPoints, Where) + FVector(0, 0, 90) : Where;
	if (Match->Rules.IsRubyRush())
	{
		AChaosImpactPlayerState* Member = Victim->GetPlayerState<AChaosImpactPlayerState>();
		AChaosImpactRubyField* Field = Match->RubyField;
		if (!Member || !IsValid(Field) || Member->Points <= 0)
		{
			return;
		}
		// (A burst of hits, a fire zone's burns, shakes them loose once; a knockout always does.)
		double& Last = PartyHitDropAt.FindOrAdd(Victim);
		if (!bKnockout && Now - Last < ChaosImpactParty::HitDropCooldownSeconds)
		{
			return;
		}
		Last = Now;
		const int32 Amount = ChaosImpactMatch::GetRubyDrop(Member->Points, bKnockout);
		Member->Points -= Amount;
		Member->ForceNetUpdate();
		Field->Scatter(Origin, Amount, Victim);
		UE_LOG(LogChaosImpact, Log, TEXT("Party: %s dropped %d rubies (%s), %d left"), *Member->GetPlayerName(), Amount,
			bKnockout ? TEXT("knocked out") : TEXT("hit"), Member->Points);
		return;
	}
	AChaosImpactCrown* Crown = Match->PartyCrown;
	if (!IsValid(Crown) || Crown->GetState().Holder != Victim)
	{
		return;
	}
	// A hook's catch takes it straight to the hook's thrower; any other hit knocks it to the floor near by.
	const AChaosImpactHook* Hook = Cast<AChaosImpactHook>(DamageCauser);
	AChaosImpactCharacter* Hooker = Hook && !Hook->IsAnchor() ? Cast<AChaosImpactCharacter>(Hook->GetSourcePawn()) : nullptr;
	if (Hooker && !bLeftStage && Crown->CanBeHooked(Hooker))
	{
		Crown->TakeByHook(Hooker, Crown->GetShownLocation());
		return;
	}
	Crown->DropFromHolder(ChaosImpactParty::FindLandSpot(GetWorld(), VersusStage, PartyFieldPoints, Origin, 380.0f, 650.0f, PartyStream));
}

void AChaosImpactGameMode::NotifyPartyTackle(AChaosImpactCharacter* Victim)
{
	AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Victim || !Match->Rules.IsParty() || Match->Phase != EChaosImpactOnlinePhase::Match || Victim->IsEliminated() || !GetWorld())
	{
		return;
	}
	// クラウンキープ: a tackle knocks the crown off as a hit does (to the floor near by).
	if (Match->Rules.IsCrownKeep())
	{
		AChaosImpactCrown* Crown = Match->PartyCrown;
		if (IsValid(Crown) && Crown->GetState().Holder == Victim)
		{
			Crown->DropFromHolder(ChaosImpactParty::FindLandSpot(GetWorld(), VersusStage, PartyFieldPoints, Victim->GetActorLocation(), 380.0f, 650.0f, PartyStream));
			UE_LOG(LogChaosImpact, Log, TEXT("Party: %s was tackled: the crown fell"), *Victim->GetName());
		}
		return;
	}
	AChaosImpactPlayerState* Member = Victim->GetPlayerState<AChaosImpactPlayerState>();
	AChaosImpactRubyField* Field = Match->RubyField;
	if (!Member || !IsValid(Field) || Member->Points <= 0)
	{
		return;
	}
	const int32 Amount = ChaosImpactMatch::GetRubyTackleDrop(Member->Points);
	Member->Points -= Amount;
	Member->ForceNetUpdate();
	Field->Scatter(Victim->GetActorLocation(), Amount, Victim);
	UE_LOG(LogChaosImpact, Log, TEXT("Party: %s dropped %d rubies (tackled), %d left"), *Member->GetPlayerName(), Amount, Member->Points);
}

TArray<FVector> AChaosImpactGameMode::GetPartyExtraBallPoints(const TArray<FVector>& Existing) const
{
	TArray<FVector> Added;
	const AChaosImpactGameState* Match = GetGameState<AChaosImpactGameState>();
	if (!Match || !Match->Rules.IsCrownKeep() || PartyFieldPoints.IsEmpty())
	{
		return Added;
	}
	const FVector Middle = IsValid(Match->PartyCrown) ? FVector(Match->PartyCrown->GetState().To) : FVector(Match->StageCenter);
	// As many again as the stage has (six at least), each in the most open floor left: furthest from every pad so far.
	const int32 Wanted = FMath::Max(ChaosImpactParty::CrownExtraBallPadsMin, Existing.Num());
	TArray<FVector> Chosen = Existing;
	for (int32 Pad = 0; Pad < Wanted; ++Pad)
	{
		const FVector* Best = nullptr;
		double BestGap = 0.0;
		for (const FVector& Point : PartyFieldPoints)
		{
			if (FVector::DistSquared2D(Point, Middle) < FMath::Square(450.0f))
			{
				continue;
			}
			double Gap = UE_BIG_NUMBER;
			for (const FVector& Taken : Chosen)
			{
				Gap = FMath::Min(Gap, FVector::DistSquared2D(Point, Taken));
			}
			if (Gap > BestGap)
			{
				BestGap = Gap;
				Best = &Point;
			}
		}
		if (!Best || BestGap < FMath::Square(350.0))
		{
			break;
		}
		Added.Add(*Best);
		Chosen.Add(*Best);
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Party: %d more ball pads for the crown keep (%d on the stage)"), Added.Num(), Existing.Num());
	return Added;
}
