#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCrashBall.h"
#include "ChaosImpactCyberStage.h"
#include "ChaosImpactForgeStage.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSnowStage.h"
#include "ChaosImpactSplashStage.h"
#include "ChaosImpactStageBase.h"
#include "ChaosImpactTempleStage.h"
#include "ChaosImpactVersusStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

/**
 * Development: for each stage, the goals tried at opposite corners (along both diagonals, from far out inwards) with the
 * Giant Ball pushed there for real; logs the furthest spot where both teams reach the other's goal ("DIAGPROBE").
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrashDiagonalProbe, "ChaosImpact.CrashBall.DiagonalProbe",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrashDiagonalProbe::RunTest(const FString&)
{
	UWorld* World = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
	}
	if (!World) { AddError(TEXT("Run this test in a game world.")); return false; }
	auto* Match = World->GetGameState<AChaosImpactGameState>();
	if (!Match) { AddError(TEXT("Requires ChaosImpact game state.")); return false; }
	const bool OldVersus = Match->bVersusMatch;
	const auto OldPhase = Match->Phase;
	const auto OldRules = Match->Rules;
	Match->bVersusMatch = true;
	Match->Phase = EChaosImpactOnlinePhase::Match;
	Match->Rules.bCrashBall = true;
	TArray<UClass*> Classes = {AChaosImpactVersusStage::StaticClass(), AChaosImpactSplashStage::StaticClass(),
		AChaosImpactTempleStage::StaticClass(), AChaosImpactCyberStage::StaticClass(),
		AChaosImpactSnowStage::StaticClass(), AChaosImpactForgeStage::StaticClass()};
	FString Only;
	FParse::Value(FCommandLine::Get(), TEXT("CIDiagStages="), Only);
	(void)Only;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 Index = 0; Index < Classes.Num(); ++Index)
	{
		if (!Only.IsEmpty() && !Only.Contains(FString::FromInt(Index + 1)))
		{
			continue;
		}
		auto* Stage = World->SpawnActor<AChaosImpactStageBase>(Classes[Index], FVector(60000 + Index * 12000, 0, 0), FRotator::ZeroRotator, Params);
		if (!Stage) { continue; }
		Stage->ConfigureCrashBallMode(true);
		Stage->Team0AttackWaypoints.Reset();
		Stage->Team1AttackWaypoints.Reset();
		const FVector Start = Stage->CrashBallStartOffset;
		const auto FloorAt = [&](const FVector2D& Local, float& OutZ)
		{
			const FVector Top = Stage->GetActorTransform().TransformPosition(FVector(Local, 2000.0f));
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(DiagProbe), false);
			if (World->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query)
				&& Hit.ImpactNormal.Z > 0.8f)
			{
				OutZ = static_cast<float>(Stage->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint).Z);
				return true;
			}
			return false;
		};
		// The floor as the Giant Ball sees it: a grid of cells (floor height, clear of walls for a ball), then paths
		// found on it from the middle out to corner spots.
		constexpr float Cell = 150.0f;
		constexpr int32 N = 61;
		const float Origin = -Cell * (N / 2);
		TArray<float> Height;
		TArray<uint8> Open;
		const float Radius = 135.0f;
		const auto BuildGrid = [&]()
		{
			Height.Init(0.0f, N * N);
			Open.Init(0, N * N);
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const FVector2D Local(Origin + X * Cell, Origin + Y * Cell);
					float Z = 0.0f;
					if (!FloorAt(Local, Z))
					{
						continue;
					}
					const FVector World3 = Stage->GetActorTransform().TransformPosition(FVector(Local, Z + Radius + 12.0f));
					if (Stage->IsOutside(World3))
					{
						continue;
					}
					FCollisionQueryParams Query(SCENE_QUERY_STAT(DiagProbeClear), false);
					if (World->OverlapAnyTestByObjectType(World3, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
						FCollisionShape::MakeSphere(Radius - 10.0f), Query))
					{
						continue;
					}
					Height[Y * N + X] = Z;
					Open[Y * N + X] = 1;
				}
			}
		};
		BuildGrid();
		const auto At = [&](const int32 X, const int32 Y) { return X >= 0 && Y >= 0 && X < N && Y < N && Open[Y * N + X]; };
		// A cell next to a wall costs more (paths keep off the walls).
		const auto Cost = [&](const int32 X, const int32 Y)
		{
			int32 Shut = 0;
			for (int32 DY = -2; DY <= 2; ++DY) { for (int32 DX = -2; DX <= 2; ++DX) { Shut += At(X + DX, Y + DY) ? 0 : 1; } }
			return 1.0f + Shut * 0.6f;
		};
		const auto CellOf = [&](const FVector2D& Local) { return FIntPoint(FMath::RoundToInt((Local.X - Origin) / Cell), FMath::RoundToInt((Local.Y - Origin) / Cell)); };
		const auto Path = [&](const FIntPoint From, const FIntPoint To, TArray<FIntPoint>& Out)
		{
			TArray<float> Best;
			TArray<int32> Came;
			Best.Init(MAX_flt, N * N);
			Came.Init(-1, N * N);
			TArray<int32> Open2;
			Best[From.Y * N + From.X] = 0.0f;
			Open2.Add(From.Y * N + From.X);
			while (!Open2.IsEmpty())
			{
				int32 Pick = 0;
				float PickF = MAX_flt;
				for (int32 I = 0; I < Open2.Num(); ++I)
				{
					const int32 C = Open2[I];
					const float F = Best[C] + FVector2D::Distance(FVector2D(C % N, C / N), FVector2D(To.X, To.Y));
					if (F < PickF) { PickF = F; Pick = I; }
				}
				const int32 Cur = Open2[Pick];
				Open2.RemoveAtSwap(Pick);
				const int32 CX = Cur % N, CY = Cur / N;
				if (CX == To.X && CY == To.Y) { break; }
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						const int32 NX = CX + DX, NY = CY + DY;
						if ((DX == 0 && DY == 0) || !At(NX, NY) || FMath::Abs(Height[NY * N + NX] - Height[Cur]) > 80.0f) { continue; }
						if (DX != 0 && DY != 0 && (!At(CX + DX, CY) || !At(CX, CY + DY))) { continue; }
						const float G = Best[Cur] + FMath::Sqrt(static_cast<float>(DX * DX + DY * DY)) * Cost(NX, NY);
						if (G < Best[NY * N + NX]) { Best[NY * N + NX] = G; Came[NY * N + NX] = Cur; Open2.AddUnique(NY * N + NX); }
					}
				}
			}
			if (Came[To.Y * N + To.X] < 0) { return false; }
			Out.Reset();
			for (int32 C = To.Y * N + To.X; C >= 0; C = Came[C]) { Out.Insert(FIntPoint(C % N, C / N), 0); if (C == From.Y * N + From.X) { break; } }
			return true;
		};
		const auto Roomy = [&](const FIntPoint C) { for (int32 DY = -1; DY <= 1; ++DY) { for (int32 DX = -1; DX <= 1; ++DX) { if (!At(C.X + DX, C.Y + DY)) { return false; } } } return true; };
		// (A map of the grid in the log: # shut, . open, digits the height in metres.)
		for (int32 Y = N - 1; Y >= 0; Y -= 2)
		{
			FString Row;
			for (int32 X = 0; X < N; ++X) { Row += At(X, Y) ? (Height[Y * N + X] > 50.0f ? TCHAR('0' + FMath::Clamp(FMath::RoundToInt(Height[Y * N + X] / 100.0f), 0, 9)) : TCHAR('.')) : TCHAR('#'); }
			UE_LOG(LogTemp, Display, TEXT("DIAGMAP %d %s"), Index + 1, *Row);
		}
		const FIntPoint Middle = CellOf(FVector2D(Start));
		for (int32 Diagonal = 0; Diagonal < 2; ++Diagonal)
		{
			const FVector2D Way = Diagonal == 0 ? FVector2D(1.0f, 1.0f).GetSafeNormal() : FVector2D(1.0f, -1.0f).GetSafeNormal();
			// Candidates: roomy cells within 22 degrees of the diagonal, furthest first; the other team's goal mirrored
			// through the middle.
			// Candidates along the diagonal (or a few degrees off it), furthest first; the other team's goal mirrored
			// through the middle. Each is tried with the ground round the goals cleared, as the rule clears it.
			TArray<TPair<float, FIntPoint>> Candidates;
			for (float Reach = 3400.0f; Reach >= 2400.0f; Reach -= 150.0f)
			{
				for (const float Turn : {0.0f, 6.0f, -6.0f})
				{
					Candidates.Add(TPair<float, FIntPoint>(Reach, CellOf(FVector2D(Start) - Way.GetRotated(Turn) * Reach)));
				}
			}
			int32 Tries = 0;
			for (const TPair<float, FIntPoint>& Candidate : Candidates)
			{
				const float Reach = Candidate.Key;
				const FIntPoint CA = Candidate.Value;
				const FIntPoint CB(2 * Middle.X - CA.X, 2 * Middle.Y - CA.Y);
				Stage->ConfigureCrashBallMode(false);
				Stage->Team0GoalOffset = FVector(Origin + CA.X * Cell, Origin + CA.Y * Cell, 0.0f);
				Stage->Team1GoalOffset = FVector(Origin + CB.X * Cell, Origin + CB.Y * Cell, 0.0f);
				Stage->ConfigureCrashBallMode(true);
				float ZA = 0.0f, ZB = 0.0f;
				if (!FloorAt(FVector2D(Stage->Team0GoalOffset), ZA) || !FloorAt(FVector2D(Stage->Team1GoalOffset), ZB))
				{
					continue;
				}
				Stage->ConfigureCrashBallMode(false);
				Stage->Team0GoalOffset.Z = ZA;
				Stage->Team1GoalOffset.Z = ZB;
				Stage->ConfigureCrashBallMode(true);
				BuildGrid();
				if (!Roomy(CA) || !Roomy(CB) || ++Tries > 24)
				{
					if (Tries > 24) { break; }
					continue;
				}
				TArray<FIntPoint> PA, PB;
				if (!Path(Middle, CA, PA) || !Path(Middle, CB, PB))
				{
					continue;
				}
				// The paths pulled tight into a few waypoints (corners only).
				const auto Waypoints = [&](const TArray<FIntPoint>& P)
				{
					TArray<FVector> Out;
					for (int32 I = 1; I + 1 < P.Num(); ++I)
					{
						const FIntPoint D0 = P[I] - P[I - 1];
						const FIntPoint D1 = P[I + 1] - P[I];
						if (D0 != D1) { Out.Add(FVector(Origin + P[I].X * Cell, Origin + P[I].Y * Cell, Height[P[I].Y * N + P[I].X])); }
					}
					// (Thin out ones that are close together.)
					TArray<FVector> Thin;
					for (const FVector& W : Out) { if (Thin.IsEmpty() || FVector::Dist2D(Thin.Last(), W) > 450.0f) { Thin.Add(W); } }
					return Thin;
				};
				const FVector GA(Origin + CA.X * Cell, Origin + CA.Y * Cell, Height[CA.Y * N + CA.X]);
				const FVector GB(Origin + CB.X * Cell, Origin + CB.Y * Cell, Height[CB.Y * N + CB.X]);
				Stage->Team0GoalOffset = GA;
				Stage->Team1GoalOffset = GB;
				Stage->Team0AttackWaypoints = Waypoints(PB);
				Stage->Team1AttackWaypoints = Waypoints(PA);
				bool bBoth = true;
				for (int32 Attack = 0; Attack < 2 && bBoth; ++Attack)
				{
					const FTransform Transform = Stage->GetCrashBallGoalTransform(1 - Attack);
					auto* Goal = World->SpawnActorDeferred<AChaosImpactCrashGoal>(AChaosImpactCrashGoal::StaticClass(), Transform,
						nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Goal->TeamIndex = 1 - Attack;
					Goal->FinishSpawning(Transform);
					auto* Ball = World->SpawnActor<AChaosImpactCrashBall>(AChaosImpactCrashBall::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
					Ball->ResetToStartLocation(Stage->GetCrashBallStartFloorLocation(), Stage);
					Ball->SetPullGoals({Goal});
					bool bScored = false;
					for (int32 Frame = 0; Frame < 3000 && !bScored; ++Frame)
					{
						const FVector Push = Stage->GetCrashBallPushDirection(Attack, Ball->GetActorLocation());
						if (Frame % 18 == 0) { Ball->ApplySpecialEffectPush(nullptr, nullptr, Ball->GetActorLocation() - Push * 500, Push, 1.f, false); }
						Ball->Tick(1.f / 60.f);
						bScored = Goal->IsBallScored(Ball);
					}
					bBoth &= bScored;
					Ball->Destroy();
					Goal->Destroy();
				}
				if (bBoth)
				{
					const auto List = [](const TArray<FVector>& W)
					{
						FString Out;
						for (const FVector& P : W) { Out += FString::Printf(TEXT("FVector(%.0f, %.0f, %.0f), "), P.X, P.Y, P.Z); }
						return Out;
					};
					UE_LOG(LogTemp, Display, TEXT("DIAGPROBE stage %d diagonal %d reach %.0f | Team0GoalOffset = FVector(%.0f, %.0f, %.0f); Team1GoalOffset = FVector(%.0f, %.0f, %.0f); Team0AttackWaypoints = {%s}; Team1AttackWaypoints = {%s};"),
						Index + 1, Diagonal, Reach, GA.X, GA.Y, GA.Z, GB.X, GB.Y, GB.Z, *List(Stage->Team0AttackWaypoints), *List(Stage->Team1AttackWaypoints));
					break;
				}
			}
		}
		Stage->ConfigureCrashBallMode(false);
		Stage->Destroy();
	}
	Match->bVersusMatch = OldVersus;
	Match->Phase = OldPhase;
	Match->Rules = OldRules;
	return true;
}

#endif
