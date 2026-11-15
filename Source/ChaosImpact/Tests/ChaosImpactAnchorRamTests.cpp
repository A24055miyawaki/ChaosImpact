#if WITH_DEV_AUTOMATION_TESTS
// Flying on an anchor: a trail shows on the flyer, and whoever it meets on the way takes 1 damage and is knocked along
// (once a flight). A VS match on this machine against one CPU standing in the way.
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactParty.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FAnchorRamCommand : public IAutomationLatentCommand
	{
	public:
		explicit FAnchorRamCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match)
			{
				Test->AddError(TEXT("Requires a game world"));
				return true;
			}
			if (APlayerController* Pausing = World->GetFirstPlayerController(); Pausing && Pausing->IsPaused()) { Pausing->SetPause(false); }
			const double Now = World->GetTimeSeconds();
			if (FPlatformTime::Seconds() - Started > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!CPU.IsValid())
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (!It->IsPlayerControlled() && It->GetController()) { CPU = *It; }
				}
			}
			AChaosImpactCharacter* Other = CPU.Get();
			// While flying: the trail on, its picture once.
			if (Step == 3 && Own && Own->IsAnchorZipShown())
			{
				if (!bSawZip)
				{
					ZipSeenAt = Now;
				}
				bSawZip = true;
				if (!bShot && Now - ZipSeenAt > 0.18)
				{
					bShot = true;
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AnchorQA/AnchorZip.png")), true, false);
				}
				if (Other)
				{
					MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Other->GetVelocity().Size2D()));
				}
			}
			if (Now < WaitUntil)
			{
				return false;
			}
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.CPUCount = 1;
				Rules.CPULevel = 1;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			case 1:
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Own || !Other)
				{
					return false;
				}
				if (MatchAt == 0.0) { MatchAt = Now + 1.5; return false; }
				if (Now < MatchAt) { return false; }
				// The CPU stands still in the way (its brain out; its body still falls and is knocked about).
				if (AController* Brain = Other->GetController()) { Brain->UnPossess(); }
				Other->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				// Somewhere with open floor ahead for a long way.
				FVector Floor = FVector::ZeroVector;
				FVector Way = FVector::ForwardVector;
				if (!PickOpen(World, Own, Floor, Way))
				{
					Test->AddError(TEXT("No open floor found"));
					return true;
				}
				Dir = Way;
				Place(Own, Floor, Dir);
				Place(Other, Floor + Dir * 420.0f, -Dir);
				Health = Other->GetHealth();
				WaitUntil = Now + 0.5;
				Step = 2;
				return false;
			}
			case 2:
				// Thrown along the floor ahead: it bites in far beyond the CPU and pulls this player across.
				Own->SetActorRotation(Dir.Rotation());
				Test->TestNotNull(TEXT("The anchor flies"), AChaosImpactHook::Release(World, Own->GetActorLocation() + Dir * 40.0f, Dir, Own, true));
				MaxSpeed = 0.0f;
				WaitUntil = Now + 3.0;
				Step = 3;
				return false;
			case 3:
			{
				UE_LOG(LogTemp, Display, TEXT("ANCHORRAM zipped %d, health %.0f -> %.0f, knocked at up to %.0f cm/s"), bSawZip ? 1 : 0, Health,
					Other->GetHealth(), MaxSpeed);
				Test->TestTrue(TEXT("This player flew on the anchor (the trail shown)"), bSawZip);
				Test->TestEqual(TEXT("The one in the way took 1 damage"), Other->GetHealth(), Health - 1.0f);
				Test->TestTrue(FString::Printf(TEXT("Knocked away (%.0f cm/s)"), MaxSpeed), MaxSpeed > 600.0f);
				Test->TestFalse(TEXT("The flight is over"), Own->IsAnchorZipShown());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AnchorQA/AnchorRamAfter.png")), true, false);
				// Once a flight: standing in the way of the next one (the damage taken again), but only once.
				Health = Other->GetHealth();
				Place(Own, Own->GetActorLocation() - FVector(0, 0, Own->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), -Dir);
				WaitUntil = Now + 0.6;
				Step = 4;
				return false;
			}
			case 4:
				if (AController* Brain = Own->GetController()) { (void)Brain; }
				GM->EndMatchForTests();
				return true;
			default:
				return true;
			}
		}

	private:
		static void Place(AChaosImpactCharacter* Character, const FVector& Floor, const FVector& Facing)
		{
			const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Character->SetActorLocationAndRotation(Floor + FVector(0, 0, HalfHeight + 2.0f), Facing.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		}

		/** A floor spot with clear floor and no wall for 9 m one way. */
		static bool PickOpen(UWorld* World, const AChaosImpactCharacter* Own, FVector& OutFloor, FVector& OutWay)
		{
			const FVector Start = Own->GetActorLocation();
			for (int32 Ring = 0; Ring < 12; ++Ring)
			{
				for (int32 Turn = 0; Turn < 8; ++Turn)
				{
					const FVector Probe = Start + FRotator(0.0f, Turn * 45.0f, 0.0f).Vector() * (Ring * 250.0f);
					FVector Floor;
					if (!ChaosImpactParty::FindFloor(World, Probe, 200.0f, 400.0f, Floor))
					{
						continue;
					}
					for (int32 Face = 0; Face < 8; ++Face)
					{
						const FVector Way = FRotator(0.0f, Face * 45.0f, 0.0f).Vector();
						FHitResult Hit;
						const FVector Up = Floor + FVector(0, 0, 70);
						if (World->SweepSingleByObjectType(Hit, Up, Up + Way * 900.0f, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
							FCollisionShape::MakeSphere(45.0f)))
						{
							continue;
						}
						bool bFloor = true;
						for (float Along = 100.0f; Along <= 900.0f && bFloor; Along += 100.0f)
						{
							FVector Under;
							bFloor = ChaosImpactParty::FindFloor(World, Floor + Way * Along, 40.0f, 60.0f, Under);
						}
						if (bFloor)
						{
							OutFloor = Floor;
							OutWay = Way;
							return true;
						}
					}
				}
			}
			return false;
		}

		FAutomationTestBase* Test;
		int32 Step = 0;
		float Health = 0.0f;
		float MaxSpeed = 0.0f;
		bool bSawZip = false;
		bool bShot = false;
		FVector Dir = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactCharacter> CPU;
		double MatchAt = 0.0;
		double ZipSeenAt = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnchorRamTest, "ChaosImpact.Combat.AnchorRam", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAnchorRamTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FAnchorRamCommand(this));
	return true;
}
#endif
