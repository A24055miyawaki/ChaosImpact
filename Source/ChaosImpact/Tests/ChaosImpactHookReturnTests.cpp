#if WITH_DEV_AUTOMATION_TESTS
// The hook catches on its way back too: an opponent who steps into its line as it comes back (it has caught nothing
// yet) takes the hit and is reeled in.
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactTrainingArena.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FHookReturnCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHookReturnCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90.0) { Test->AddError(FString::Printf(TEXT("Hook return test stalled at %d"), Step)); return true; }
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own || Now < WaitUntil) { return false; }
			AChaosImpactHook* Hook = nullptr;
			for (TActorIterator<AChaosImpactHook> It(World); It; ++It) { Hook = *It; }
			const auto Place = [](AChaosImpactCharacter* Who, const FVector& At)
			{
				Who->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
				Who->GetCharacterMovement()->Velocity = FVector::ZeroVector;
			};
			switch (Step)
			{
			case 0:
			{
				if (!Own->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Own && !It->IsPlayerControlled()) { Other = *It; break; }
				}
				if (!Other.IsValid()) { Test->AddError(TEXT("No CPU to hook")); return true; }
				// Nothing lying about for the hook to catch instead.
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It) { It->Destroy(); }
				// (Its brain kept, but still: it stands where it is put.)
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				FVector Centre = Own->GetActorLocation();
				for (TActorIterator<AChaosImpactTrainingArena> It(World); It; ++It) { Centre = It->GetCenter() + FVector(0, 0, 100); break; }
				// The way with the longest clear run: the hook goes out its full length (or to a wall) and comes back.
				float Best = -1.0f;
				for (int32 Turn = 0; Turn < 16; ++Turn)
				{
					const FVector Way = FVector::ForwardVector.RotateAngleAxis(Turn * 22.5f, FVector::UpVector);
					FHitResult Hit;
					const float Free = World->LineTraceSingleByChannel(Hit, Centre, Centre + Way * 2600.0f, ECC_WorldStatic) ? Hit.Distance : 2600.0f;
					if (Free > Best) { Best = Free; Dir = Way; }
				}
				Place(Own, Centre);
				// Anyone else well out of the way (behind the thrower).
				int32 Aside = 0;
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (*It != Own && *It != Other.Get())
					{
						Place(*It, Centre - Dir * 900.0f + FVector::CrossProduct(FVector::UpVector, Dir) * (700.0f + 200.0f * Aside++));
					}
				}
				// Off to the side, out of its line on the way out.
				Place(Other.Get(), Centre + FVector::CrossProduct(FVector::UpVector, Dir) * 600.0f + Dir * 500.0f);
				Health = Other->GetHealth();
				WaitUntil = Now + 0.4;
				Step = 1;
				return false;
			}
			case 1:
				Own->SetActorRotation(Dir.Rotation());
				AChaosImpactHook::Release(World, Own->GetActorLocation() + Dir * 60.0f, Dir, Own);
				Step = 2;
				return false;
			case 2:
				if (!Hook)
				{
					Test->AddError(TEXT("The hook came back without ever turning"));
					return true;
				}
				if (Hook->GetPhase() != EChaosImpactHookPhase::Back)
				{
					Test->TestTrue(TEXT("Nothing caught on the way out"), Hook->GetPhase() == EChaosImpactHookPhase::Out);
					return false;
				}
				// Coming back: the opponent steps into its line, between the head and the hand.
				{
					const FVector Head = Hook->GetHeadLocation();
					const FVector Hand = Own->GetActorLocation();
					FVector Into = FMath::Lerp(Head, Hand, 0.4f);
					Into.Z = Other->GetActorLocation().Z;
					Place(Other.Get(), Into);
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HookQA"), TEXT("HookReturn.png")), true, false);
				}
				PlacedAt = Now;
				Step = 3;
				return false;
			case 3:
				// (Until it gets there: it comes back at its own speed.)
				if (!(Hook && Hook->IsReeling()) && Now - PlacedAt < 2.0)
				{
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("HOOKRETURN other %s health %.0f -> %.0f, hook phase %d"), *Other->GetName(), Health, Other->GetHealth(),
					Hook ? static_cast<int32>(Hook->GetPhase()) : -1);
				Test->TestEqual(TEXT("Caught on the way back: one hit"), Other->GetHealth(), Health - 1.0f);
				Test->TestTrue(TEXT("And reeled in"), Hook && Hook->IsReeling());
				Before = static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), Own->GetActorLocation()));
				WaitUntil = Now + 1.2;
				Step = 4;
				return false;
			case 4:
			{
				const float After = static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), Own->GetActorLocation()));
				UE_LOG(LogTemp, Display, TEXT("HOOKRETURN pulled from %.0f to %.0f cm"), Before, After);
				Test->TestTrue(FString::Printf(TEXT("Pulled in to the thrower (%.0f -> %.0f cm)"), Before, After), After < 400.0f);
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Step = 0;
		TWeakObjectPtr<AChaosImpactCharacter> Other;
		FVector Dir = FVector::ForwardVector;
		float Health = 0.0f;
		float Before = 0.0f;
		double PlacedAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHookReturnTest, "ChaosImpact.Combat.HookReturnCatch", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHookReturnTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHookReturnCommand(this));
	return true;
}
#endif
