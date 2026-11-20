#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール, more (2026-11-17): how a hook or an anchor flies changed by what rides with it (雷フック rebounds once off a wall
// and goes out faster, 電光石火 flies faster, 貫通フック goes through walls, 光の跳躍 through a thin wall, 雪玉フック and
// 雪玉アンカー catch and ram wider), 極太光線 thick even unrolled, 操縦雪玉 drawn as big as the snowball, a CPU joining a
// pair in a match with the rule on, the rule carried to a local match's own level, the 訓練場 lesson, and the wheel's swap
// option. Frames in Saved/FusionQA/More_*.png. Run on a training stage with one CPU:
// Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=1
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDojoLessons.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactStageBase.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FFusionMoreCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionMoreCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.0) { Test->AddError(FString::Printf(TEXT("More test stalled at step %d"), Step)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			if (!Own || !Match) { return false; }
			Own->RefillHealth();
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (!It->IsPlayerControlled() && It->GetController()) { CPU = *It; }
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"), Name + TEXT(".png")), true, false);
			};
			const auto ClearHooks = [World]()
			{
				for (TActorIterator<AChaosImpactHook> It(World); It; ++It) { It->Destroy(); }
			};
			// A way from the player with a wall in it, between Near and Far away (none: zero).
			const auto WallWay = [World, Own, CPU](const float Near, const float Far, float& OutDistance)
			{
				FCollisionQueryParams Query(SCENE_QUERY_STAT(FusionMoreTest), false, Own);
				const FVector From = Own->GetActorLocation();
				// (Never toward the CPU: it would be caught first.)
				const FVector ToCPU = CPU ? (CPU->GetActorLocation() - From).GetSafeNormal2D() : FVector::ZeroVector;
				for (int32 Turn = 0; Turn < 16; ++Turn)
				{
					const FVector Way = FRotator(0.0f, Turn * 22.5f, 0.0f).Vector();
					if (FVector::DotProduct(Way, ToCPU) > 0.7f)
					{
						continue;
					}
					FHitResult Hit;
					if (World->LineTraceSingleByObjectType(Hit, From, From + Way * Far, FCollisionObjectQueryParams(ECC_WorldStatic), Query)
						&& Hit.Distance > Near && FMath::Abs(Hit.ImpactNormal.Z) < 0.3f)
					{
						OutDistance = static_cast<float>(Hit.Distance);
						return Way;
					}
				}
				return FVector::ZeroVector;
			};
			switch (Step)
			{
			case 0:
			{
				// (The CPU stands still until its own step: its throws would cover the pictures.)
				if (CPU && CPU->GetController()) { CPU->GetController()->SetActorTickEnabled(false); }
				// (Settled first.) The rule carried to a local match's own level; the 訓練場 lesson; the wheel's option.
				FChaosImpactMatchRules Rules;
				Rules.bFusionBalls = true;
				FChaosImpactMatchRules Read;
				Test->TestTrue(TEXT("A local match's options read back"), ChaosImpactMatch::ReadOptions(
					FURL(nullptr, *(TEXT("/Game/X?") + ChaosImpactMatch::ToOptions(Rules)), TRAVEL_Absolute), Read));
				Test->TestTrue(TEXT("合体ボール あり reaches the match's own level"), Read.bFusionBalls);
				bool bLesson = false;
				for (const FChaosImpactDojoCategory& Group : ChaosImpactDojo::GetCategories())
				{
					for (const FChaosImpactDojoLesson& Lesson : Group.Lessons)
					{
						bLesson |= Lesson.Id == TEXT("x_fusion") && Lesson.bFusion && Lesson.Goal == EChaosImpactDojoGoal::FusionHit
							&& ChaosImpactFusion::CanFuseTypes(Lesson.Ball, Lesson.SecondBall);
					}
				}
				Test->TestTrue(TEXT("訓練場 has the 合体ボール lesson, with a pair that joins"), bLesson);
				Test->TestFalse(TEXT("The wheel's swap is off at first"), FChaosImpactProfile().bWheelSwap);
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			}
			case 1:
			{
				// 雷フック: the hook flies (thunder riding), faster, rebounding off the wall in front once.
				Test->TestEqual(TEXT("The hook flies with thunder"), ChaosImpactFusion::PickPrimary(EChaosImpactBallType::Thunder,
					EChaosImpactBallType::Hook), EChaosImpactBallType::Hook);
				Test->TestEqual(TEXT("The anchor flies with the light"), ChaosImpactFusion::PickPrimary(EChaosImpactBallType::Anchor,
					EChaosImpactBallType::Beam), EChaosImpactBallType::Anchor);
				float Distance = 0.0f;
				FVector Way = WallWay(300.0f, 900.0f, Distance);
				Way = Way.IsZero() ? WallWay(300.0f, 2000.0f, Distance) : Way;
				Test->TestFalse(TEXT("A wall to rebound off"), Way.IsZero());
				AChaosImpactHook* Hook = AChaosImpactHook::Release(World, Own->GetActorLocation(), Way, Own, false, EChaosImpactBallType::Thunder);
				ShotDelay = 0.3f;
				if (Hook)
				{
					// (The picture just after the rebound, its chain bent at the wall.)
					ShotDelay = (Hook->GetBounceDistance() + 350.0f) / Hook->GetOutSpeed();
					UE_LOG(LogTemp, Display, TEXT("FUSIONMORE thunder hook: wall %.0f, rebound at %.0f, speed %.0f"), Distance,
						Hook->GetBounceDistance(), Hook->GetOutSpeed());
					Test->TestTrue(TEXT("It rebounds off the wall"), Hook->GetBounceDistance() > 0.0f);
					Test->TestTrue(TEXT("Faster out"), Hook->GetOutSpeed() > AChaosImpactHook::OutSpeed * 1.3f);
				}
				WaitUntil = Now + ShotDelay;
				Step = 2;
				return false;
			}
			case 2:
				Shot(TEXT("More_ThunderHook"));
				WaitUntil = Now + 1.4;
				Step = 3;
				return false;
			case 3:
			{
				ClearHooks();
				// (Nothing lying about for it to catch on the way.)
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { if (It->IsPickup()) { It->Destroy(); } }
				for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It) { It->Destroy(); }
				// 貫通フック: no wall stops it.
				float Distance = 0.0f;
				const FVector Way = WallWay(300.0f, 2000.0f, Distance);
				Test->TestFalse(TEXT("A wall to go through"), Way.IsZero());
				AChaosImpactHook* Hook = AChaosImpactHook::Release(World, Own->GetActorLocation(), Way, Own, false, EChaosImpactBallType::Beam);
				Test->TestTrue(TEXT("貫通フック passes walls"), Hook && Hook->PassesWalls());
				PierceWall = Distance;
				PierceHook = Hook;
				StepAt = Now;
				PierceOut = 0.0f;
				Step = 30;
				return false;
			}
			case 30:
				// (Its head's furthest out, frame by frame, for a moment.)
				if (AChaosImpactHook* Hook = PierceHook.Get())
				{
					PierceOut = FMath::Max(PierceOut, static_cast<float>(FVector::Dist2D(Hook->GetHeadLocation(), Own->GetActorLocation())));
				}
				if (Now - StepAt < 0.9) { return false; }
				Step = 4;
				return false;
			case 4:
			{
				UE_LOG(LogTemp, Display, TEXT("FUSIONMORE pierce hook: wall %.0f, head out at most %.0f (phase %d)"), PierceWall, PierceOut,
					PierceHook.IsValid() ? static_cast<int32>(PierceHook->GetPhase()) : -1);
				Test->TestTrue(TEXT("Its head went on past the wall"), PierceOut > PierceWall + 100.0f);
				Shot(TEXT("More_PierceHook"));
				ClearHooks();
				// 雪玉フック: wider; 電光石火: faster; 光の跳躍: through a thin wall (logged: it needs one on this stage).
				AChaosImpactHook* Snow = AChaosImpactHook::Release(World, Own->GetActorLocation(), Own->GetActorForwardVector(), Own, false,
					EChaosImpactBallType::Snow, 1.8f);
				Test->TestTrue(TEXT("雪玉フック catches wider"), Snow && Snow->GetCatchRadius() > AChaosImpactHook::CatchRadius + 80.0f);
				float Distance = 0.0f;
				const FVector Way = WallWay(300.0f, 1800.0f, Distance);
				AChaosImpactHook* Quick = AChaosImpactHook::Release(World, Own->GetActorLocation(), Way, Own, true, EChaosImpactBallType::Thunder);
				Test->TestTrue(TEXT("電光石火 flies its thrower faster"), Quick && Quick->GetZipSpeed() > AChaosImpactHook::ZipSpeed * 1.5f);
				if (Quick) { Quick->Destroy(); }
				int32 Through = 0;
				for (int32 Turn = 0; Turn < 16; ++Turn)
				{
					if (AChaosImpactHook* Leap = AChaosImpactHook::Release(World, Own->GetActorLocation(), FRotator(0.0f, Turn * 22.5f, 0.0f).Vector(),
						Own, true, EChaosImpactBallType::Beam))
					{
						Through += Leap->IsPhaseAnchor() ? 1 : 0;
						for (TActorIterator<AChaosImpactStageBase> Stage(World); Stage && Leap->IsPhaseAnchor(); ++Stage)
						{
							Test->TestFalse(TEXT("Through an inner wall only (it lands inside the stage)"), Stage->IsOutside(Leap->GetZipTarget()));
							break;
						}
						Leap->Destroy();
					}
				}
				UE_LOG(LogTemp, Display, TEXT("FUSIONMORE light leap: %d of 16 ways go through a wall"), Through);
				// 光の跳躍 for real: a thin wall (30 cm) put up in front, the anchor thrown at it, its thrower through it.
				float Open = 0.0f;
				LeapWay = Own->GetActorForwardVector().GetSafeNormal2D();
				for (int32 Turn = 0; Turn < 16; ++Turn)
				{
					const FVector Try = FRotator(0.0f, Turn * 22.5f, 0.0f).Vector();
					FHitResult Hit;
					FCollisionQueryParams Query(SCENE_QUERY_STAT(FusionMoreOpen), false, Own);
					const float Clear = World->SweepSingleByObjectType(Hit, Own->GetActorLocation(), Own->GetActorLocation() + Try * 1600.0f,
						FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeCapsule(50.0f, 80.0f), Query)
						? static_cast<float>(Hit.Distance) : 1600.0f;
					if (Clear > Open) { Open = Clear; LeapWay = Try; }
				}
				LeapFrom = Own->GetActorLocation();
				const FVector WallAt = LeapFrom + LeapWay * 450.0f;
				if (AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(WallAt, LeapWay.Rotation()))
				{
					Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					Wall->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
					Wall->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					Wall->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
					Wall->SetActorScale3D(FVector(0.3f, 4.0f, 3.0f));
					LeapWall = Wall;
				}
				UE_LOG(LogTemp, Display, TEXT("FUSIONMORE light leap: open way %.0f"), Open);
				WaitUntil = Now + 0.3;
				Step = 40;
				return false;
			}
			case 40:
			{
				ClearHooks();
				AChaosImpactHook* Leap = AChaosImpactHook::Release(World, Own->GetActorLocation(), LeapWay, Own, true, EChaosImpactBallType::Beam);
				Test->TestTrue(TEXT("光の跳躍 goes through the thin wall"), Leap && Leap->IsPhaseAnchor());
				if (Leap)
				{
					UE_LOG(LogTemp, Display, TEXT("FUSIONMORE light leap: lands %.0f on"), FVector::Dist2D(Leap->GetZipTarget(), LeapFrom));
				}
				WaitUntil = Now + 0.5;
				Step = 41;
				return false;
			}
			case 41:
				Shot(TEXT("More_LightLeap"));
				WaitUntil = Now + 1.2;
				Step = 42;
				return false;
			case 42:
			{
				const float Past = static_cast<float>(FVector::DotProduct(Own->GetActorLocation() - LeapFrom, LeapWay));
				UE_LOG(LogTemp, Display, TEXT("FUSIONMORE light leap: thrower %.0f on (the wall at 450)"), Past);
				Test->TestTrue(TEXT("Its thrower went through to the far side"), Past > 520.0f);
				if (AActor* Wall = LeapWall.Get()) { Wall->Destroy(); }
				ClearHooks();
				// (The 雪玉フック's head for the picture.)
				AChaosImpactHook::Release(World, Own->GetActorLocation(), -LeapWay, Own, false, EChaosImpactBallType::Snow, 1.8f);
				WaitUntil = Now + 0.15;
				Step = 5;
				return false;
			}
			case 5:
				Shot(TEXT("More_SnowHook"));
				WaitUntil = Now + 0.2;
				Step = 50;
				return false;
			case 50:
				ClearHooks();
				// 操縦雪玉: the steered ball as big as the snowball.
				Own->GiveBallsForTests(EChaosImpactBallType::Snow, EChaosImpactBallType::Drive);
				Own->SetSnowGrowthForTests(0, 1.0f);
				Own->RequestBallFusion();
				WaitUntil = Now + 1.2;
				Step = 6;
				return false;
			case 6:
				Test->TestTrue(TEXT("操縦雪玉 goes"), Own->ThrowForTests(Own->GetActorForwardVector(), 0.0f));
				WaitUntil = Now + 0.6;
				Step = 7;
				return false;
			case 7:
			{
				bool bSeen = false;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Drive && It->GetFusedType() == EChaosImpactBallType::Snow)
					{
						bSeen = true;
						UE_LOG(LogTemp, Display, TEXT("FUSIONMORE drive snow size %.2f"), It->GetSnowScale());
						Test->TestTrue(TEXT("As big as the snowball"), It->GetSnowScale() > 1.5f);
					}
				}
				Test->TestTrue(TEXT("The steered snowball is out"), bSeen);
				Shot(TEXT("More_DriveSnow"));
				// 極太光線, the snow not rolled.
				Own->GiveBallsForTests(EChaosImpactBallType::Snow, EChaosImpactBallType::Beam);
				Own->SetSnowGrowthForTests(0, 0.0f);
				Own->RequestBallFusion();
				WaitUntil = Now + 5.0;
				Step = 8;
				return false;
			}
			case 8:
				Test->TestTrue(TEXT("極太光線 goes"), Own->ThrowForTests(Own->GetActorForwardVector(), 0.3f));
				WaitUntil = Now + 0.3;
				Step = 9;
				return false;
			case 9:
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Beam && It->GetFusedType() == EChaosImpactBallType::Snow && !It->HasDetonated())
					{
						UE_LOG(LogTemp, Display, TEXT("FUSIONMORE unrolled beam width %.2f"), It->GetBeamWidthScale());
						Test->TestTrue(TEXT("Thick even unrolled"), It->GetBeamWidthScale() >= AChaosImpactBall::FusionBeamMinWidth);
					}
				}
				Shot(TEXT("More_BeamSnow"));
				// A CPU joins a pair (a match with 合体ボール あり).
				if (CPU && CPU->GetController()) { CPU->GetController()->SetActorTickEnabled(true); }
				Match->bVersusMatch = true;
				Match->Rules.bFusionBalls = true;
				StepAt = Now;
				Step = 10;
				return false;
			case 10:
				if (!CPU) { Test->AddError(TEXT("No CPU")); Step = 99; return false; }
				if (CPU->AreBallsFused())
				{
					UE_LOG(LogTemp, Display, TEXT("FUSIONMORE CPU joined after %.1f s"), Now - StepAt);
					Shot(TEXT("More_CPUFused"));
					Step = 99;
					return false;
				}
				if (Now - StepAt > 40.0)
				{
					Test->AddError(TEXT("The CPU never joined a pair"));
					Step = 99;
					return false;
				}
				if (Now - LoggedAt > 1.0)
				{
					LoggedAt = Now;
					UE_LOG(LogTemp, Display, TEXT("FUSIONMORE CPU: balls %d, can join %d, charging %d"), CPU->GetCarriedBallCount(),
						CPU->CanFuseBalls() ? 1 : 0, CPU->IsChargingThrow() ? 1 : 0);
				}
				if (CPU->GetCarriedBallCount() < 2 || !CPU->CanFuseBalls())
				{
					// (A pair that joins, put in its hands before it starts on a throw.)
					if (CPU->IsChargingThrow()) { CPU->CancelChargingThrow(); }
					CPU->GiveBallsForTests(EChaosImpactBallType::Fire, EChaosImpactBallType::Ice);
				}
				WaitUntil = Now + 0.1;
				return false;
			default:
				Match->bVersusMatch = false;
				Match->Rules.bFusionBalls = false;
				ClearHooks();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		double StepAt = 0.0;
		double LoggedAt = 0.0;
		int32 Step = 0;
		float PierceWall = 0.0f;
		float PierceOut = 0.0f;
		float ShotDelay = 0.3f;
		TWeakObjectPtr<AChaosImpactHook> PierceHook;
		TWeakObjectPtr<AActor> LeapWall;
		FVector LeapWay = FVector::ForwardVector;
		FVector LeapFrom = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionMoreTest, "ChaosImpact.Training.FusionMore",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactFusionMoreTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionMoreCommand(this));
	return true;
}
#endif
