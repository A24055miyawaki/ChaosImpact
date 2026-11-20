#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール, 2026-11-19 (Docs/合体ボール追加企画.md): every pair added (ノヴァ, プリズン, バリア, フック＋アンカー) and every
// 強化ボール thrown once at a CPU ahead: it joins, flies and ends without trouble; what it left is logged (FUSIONPLUS) and
// a frame of each is kept in Saved/FusionQA/Plus_*.png. Run on a training stage with one CPU:
// Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=1
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	using EType = EChaosImpactBallType;

	class FFusionPlusCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionPlusCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			const EType Specials[] = {EType::Fire, EType::Ice, EType::Thunder, EType::Black, EType::Wind, EType::Smoke, EType::Beam, EType::Snow,
				EType::Nova, EType::Simae, EType::Drive, EType::Prison, EType::Hook, EType::Anchor, EType::Swap, EType::Barrier, EType::Mine};
			const auto IsNew = [](const EType A, const EType B)
			{
				return A == B || A == EType::Nova || B == EType::Nova || A == EType::Prison || B == EType::Prison || A == EType::Barrier
					|| B == EType::Barrier || (A == EType::Hook && B == EType::Anchor) || (A == EType::Anchor && B == EType::Hook);
			};
			for (int32 A = 0; A < UE_ARRAY_COUNT(Specials); ++A)
			{
				for (int32 B = A; B < UE_ARRAY_COUNT(Specials); ++B)
				{
					if (IsNew(Specials[A], Specials[B]))
					{
						Pairs.Add({Specials[A], Specials[B]});
					}
				}
			}
			if (const TCHAR* Only = FCString::Strstr(FCommandLine::Get(), TEXT("-CIFusionPlusFrom=")))
			{
				Index = FMath::Clamp(FCString::Atoi(Only + 18), 0, Pairs.Num() - 1);
			}
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 900.0) { Test->AddError(FString::Printf(TEXT("FusionPlus stalled at pair %d"), Index)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own) { return false; }
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Own && !It->IsEliminated() && !(It->GetBallForm() && It->GetBallForm()->IsCaptured())) { CPU = *It; }
			}
			const FVector Ahead = Own->GetActorForwardVector().GetSafeNormal2D();
			if (Step == 0)
			{
				Test->TestEqual(TEXT("63 new: 46 pairs and 17 強化ボール"), Pairs.Num(), 63);
				Home = Own->GetActorLocation();
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			}
			if (Index >= Pairs.Num())
			{
				return true;
			}
			const EType A = Pairs[Index].Key;
			const EType B = Pairs[Index].Value;
			const EType Flier = ChaosImpactFusion::PickPrimary(A, B);
			const EType Rider = ChaosImpactFusion::PickSecondary(A, B);
			const FString Name = ChaosImpactFusion::GetFusionName(A, B);
			switch (Step)
			{
			case 1:
			{
				// Clear what the last one left; everyone back in place.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { if (!It->IsPickup()) { It->Destroy(); } }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactHook> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { It->Destroy(); }
				AChaosImpactGadget::ClearAll(World);
				Own->TeleportTo(Home, Own->GetActorRotation());
				Own->RefillHealth();
				if (CPU)
				{
					// (A still target: its own play stopped, so it neither dodges nor throws at the player.)
					if (AController* Brain = CPU->GetController())
					{
						Brain->SetActorTickEnabled(false);
					}
					CPU->RefillHealth();
					CPU->TeleportTo(Home + Ahead * 900.0f + FVector(0.0f, 0.0f, 20.0f), CPU->GetActorRotation());
				}
				Own->GiveBallsForTests(A, B);
				Own->RequestBallFusion();
				Test->TestTrue(FString::Printf(TEXT("%s joined"), *Name), Own->AreBallsFused());
				Test->TestEqual(FString::Printf(TEXT("%s's label"), *Name), ChaosImpactFusion::GetFusionLabel(Flier, Rider),
					A == B ? FString(TEXT("超")) + ChaosImpactBallTypes::GetDisplayName(A)
						: FString::Printf(TEXT("%s×%s"), ChaosImpactBallTypes::GetDisplayName(Flier), ChaosImpactBallTypes::GetDisplayName(Rider)));
				WaitUntil = Now + 0.7;
				Step = 2;
				return false;
			}
			case 2:
			{
				const FVector Aim = CPU ? (CPU->GetActorLocation() - Own->GetActorLocation()).GetSafeNormal2D() : Ahead;
				Test->TestTrue(FString::Printf(TEXT("%s goes"), *Name), Own->ThrowForTests(Aim, 0.6f));
				ThrownAt = Now;
				WaitUntil = Now + 1.3;
				Step = 3;
				return false;
			}
			case 3:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"),
					FString::Printf(TEXT("Plus_%02d_%s_%s.png"), Index, ChaosImpactBallTypes::GetInternalName(Flier), ChaosImpactBallTypes::GetInternalName(Rider))),
					false, false);
				WaitUntil = ThrownAt + (A == EType::Nova || B == EType::Nova ? 6.5 : 4.2);
				Step = 4;
				return false;
			case 4:
			{
				int32 Zones = 0;
				int32 Walls = 0;
				int32 Mines = 0;
				int32 Birds = 0;
				int32 Twisters = 0;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { ++Zones; }
				for (TActorIterator<AChaosImpactGadget> It(World); It; ++It) { Walls += It->IsBarrier() ? 1 : 0; Mines += It->IsMine() ? 1 : 0; }
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It) { ++Birds; }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { ++Twisters; }
				const bool bShut = CPU && CPU->GetBallForm() && CPU->GetBallForm()->IsCaptured();
				UE_LOG(LogTemp, Display, TEXT("FUSIONPLUS %02d %s (%s): zones %d walls %d mines %d birds %d tornadoes %d, cpu %s"), Index, *Name,
					*ChaosImpactFusion::GetFusionLabel(Flier, Rider), Zones, Walls, Mines, Birds, Twisters,
					!CPU ? TEXT("none") : CPU->IsEliminated() ? TEXT("out") : bShut ? TEXT("shut in") : *FString::Printf(TEXT("hp %.0f"), CPU->GetHealth()));
				++Index;
				// (Long enough for a knocked-out CPU to come back.)
				WaitUntil = Now + (CPU && CPU->IsEliminated() ? 3.5 : 0.4);
				Step = 1;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TArray<TPair<EType, EType>> Pairs;
		int32 Index = 0;
		int32 Step = 0;
		FVector Home = FVector::ZeroVector;
		double ThrownAt = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionPlusTest, "ChaosImpact.Training.FusionPlus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactFusionPlusTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionPlusCommand(this));
	return true;
}
#endif
