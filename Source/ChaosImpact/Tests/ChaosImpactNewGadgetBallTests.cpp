#if WITH_DEV_AUTOMATION_TESTS
// スワップ, バリア, マイン: a VS match on this machine against one CPU standing still.
//  - swap: the two change places, nobody hurt.
//  - barrier: a wall stands where it lands; the next ball stops at it and breaks it; the one after gets through.
//  - mine: its owner does not set it off; an opponent does, and takes 1 damage and a push (its owner is not hurt).
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGadget.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
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
	class FNewGadgetBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNewGadgetBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			if (FPlatformTime::Seconds() - Started > 150.0)
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
			if (Other)
			{
				MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Other->GetVelocity().Size2D()));
			}
			if (Now < WaitUntil)
			{
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NewBallsQA"), Name), true, false);
			};
			const auto Throw = [World, Own, this](const EChaosImpactBallType Type, const bool bLob = false)
			{
				const FTransform At(FRotator::ZeroRotator, Own->GetActorLocation() + Dir * 70.0f);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, Own, Own,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(Type);
				Ball->FinishSpawning(At);
				// (Lobbed: it comes down on the floor three metres or so ahead.)
				if (bLob) { Ball->Launch(Dir, 420.0f, EChaosImpactBallFlightMode::Arc, 250.0f); }
				else { Ball->Launch(Dir, 1700.0f, EChaosImpactBallFlightMode::Straight, 0.0f); }
			};
			// (Thrown by Who, along Way.)
			const auto ThrowAs = [World](AChaosImpactCharacter* Who, const FVector& Way)
			{
				const FTransform At(FRotator::ZeroRotator, Who->GetActorLocation() + Way * 70.0f);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, Who, Who,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(EChaosImpactBallType::Normal);
				Ball->FinishSpawning(At);
				Ball->Launch(Way, 1700.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
			};
			const auto FindGadget = [World](const bool bMine) -> AChaosImpactGadget*
			{
				for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
				{
					if (It->IsMine() == bMine && !It->HasEnded()) { return *It; }
				}
				return nullptr;
			};
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
				if (AController* Brain = Other->GetController()) { Brain->UnPossess(); }
				Other->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				if (!PickOpen(World, Own, Floor, Dir))
				{
					Test->AddError(TEXT("No open floor found"));
					return true;
				}
				// Nothing else lying about to get in the way.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				Place(Own, Floor, Dir);
				Place(Other, Floor + Dir * 420.0f, -Dir);
				WaitUntil = Now + 0.5;
				Step = 2;
				return false;
			}
			case 2:
				// --- Swap --- (not while the CPU is still in a dash it began before its brain was taken out: a dash is untouchable)
				if (Other->IsDashing())
				{
					return false;
				}
				OwnBefore = Own->GetActorLocation();
				OtherBefore = Other->GetActorLocation();
				Health = Other->GetHealth();
				OwnHealth = Own->GetHealth();
				Throw(EChaosImpactBallType::Swap);
				WaitUntil = Now + 0.45;
				Step = 3;
				return false;
			case 3:
				Shot(TEXT("Swap.png"));
				WaitUntil = Now + 0.6;
				Step = 4;
				return false;
			case 4:
				UE_LOG(LogTemp, Display, TEXT("NEWBALLS swap: own %.0f cm from the CPU's old place, CPU %.0f cm from this player's; health %.0f -> %.0f"),
					FVector::Dist2D(Own->GetActorLocation(), OtherBefore), FVector::Dist2D(Other->GetActorLocation(), OwnBefore), Health, Other->GetHealth());
				Test->TestTrue(TEXT("Swap: this player stands where the CPU stood"), FVector::Dist2D(Own->GetActorLocation(), OtherBefore) < 60.0f);
				Test->TestTrue(TEXT("Swap: the CPU stands where this player stood"), FVector::Dist2D(Other->GetActorLocation(), OwnBefore) < 60.0f);
				Test->TestEqual(TEXT("Swap: one hit"), Other->GetHealth(), Health - 1.0f);
				// --- Barrier ---
				Place(Own, Floor, Dir);
				Place(Other, Floor + Dir * 700.0f, -Dir);
				WaitUntil = Now + 0.4;
				Step = 5;
				return false;
			case 5:
				Throw(EChaosImpactBallType::Barrier, true);
				WaitUntil = Now + 1.2;
				Step = 6;
				return false;
			case 6:
			{
				AChaosImpactGadget* Wall = FindGadget(false);
				Test->TestNotNull(TEXT("Barrier: a wall stands where it landed"), Wall);
				if (!Wall)
				{
					return true;
				}
				Barrier = Wall;
				const float Along = static_cast<float>(FVector::DotProduct(Wall->GetActorLocation() - Floor, Dir));
				UE_LOG(LogTemp, Display, TEXT("NEWBALLS barrier: %.0f cm ahead, facing %.2f"), Along,
					FMath::Abs(FVector::DotProduct(Wall->GetActorForwardVector(), Dir)));
				Test->TestTrue(TEXT("Barrier: between the two"), Along > 120.0f && Along < 650.0f);
				Test->TestTrue(TEXT("Barrier: across the throw"), FMath::Abs(FVector::DotProduct(Wall->GetActorForwardVector(), Dir)) > 0.9f);
				Shot(TEXT("Barrier.png"));
				// The CPU walks through it? Its body is not blocked: checked with a sweep of its capsule across the wall.
				FHitResult Blocked;
				const FVector Body = Wall->GetActorLocation() + FVector(0.0f, 0.0f, 95.0f);
				const bool bBlocksPawn = World->SweepSingleByChannel(Blocked, Body - Dir * 150.0f, Body + Dir * 150.0f, FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(34.0f, 88.0f)) && Blocked.GetActor() == Wall;
				Test->TestFalse(TEXT("Barrier: people pass through"), bBlocksPawn);
				Health = Other->GetHealth();
				WaitUntil = Now + 0.3;
				Step = 7;
				return false;
			}
			case 7:
				// Its own thrower's ball flies through it (and hits the CPU behind), the wall standing.
				Throw(EChaosImpactBallType::Normal);
				WaitUntil = Now + 0.9;
				Step = 8;
				return false;
			case 8:
			{
				Test->TestEqual(TEXT("Barrier: its own side's ball goes through (the CPU behind is hit)"), Other->GetHealth(), Health - 1.0f);
				Test->TestTrue(TEXT("Barrier: still standing after its own side's ball"), Barrier.IsValid() && !Barrier->HasEnded());
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				// The opponent's ball stops at it (no damage behind) and breaks it.
				OwnHealth = Own->GetHealth();
				ThrowAs(Other, -Dir);
				WaitUntil = Now + 0.9;
				Step = 80;
				return false;
			}
			case 80:
				Test->TestEqual(TEXT("Barrier: the opponent's ball stopped at it (no damage behind)"), Own->GetHealth(), OwnHealth);
				Test->TestTrue(TEXT("Barrier: broken by that one ball"), !Barrier.IsValid() || (Barrier->HasEnded() && Barrier->GetState().bBurst));
				Shot(TEXT("BarrierBroken.png"));
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				// A new wall, and the CPU walks into it.
				Place(Own, Floor, Dir);
				Place(Other, Floor + Dir * 700.0f, -Dir);
				Throw(EChaosImpactBallType::Barrier, true);
				WaitUntil = Now + 1.4;
				Step = 81;
				return false;
			case 81:
			{
				AChaosImpactGadget* Wall = FindGadget(false);
				Test->TestNotNull(TEXT("Barrier: a second wall stands"), Wall);
				if (!Wall)
				{
					return true;
				}
				Barrier = Wall;
				// (Full health: the 1 it takes leaves it standing, to be thrown back.)
				Other->RefillHealth();
				Health = Other->GetHealth();
				// (Its body against the wall, as walking into it brings it.)
				Place(Other, Wall->GetActorLocation() + Dir * 20.0f, -Dir);
				MaxSpeed = 0.0f;
				WaitUntil = Now + 0.6;
				Step = 9;
				return false;
			}
			case 9:
			{
				Test->TestEqual(TEXT("Barrier: the opponent walking into it takes 1"), Other->GetHealth(), Health - 1.0f);
				Test->TestTrue(TEXT("Barrier: …is thrown back"), MaxSpeed > 400.0f);
				Test->TestTrue(TEXT("Barrier: …and it breaks"), !Barrier.IsValid() || (Barrier->HasEnded() && Barrier->GetState().bBurst));
				// A short throw goes down at once (no floating up first); a full one is lobbed.
				float Along = 0.0f, Up = 0.0f;
				EChaosImpactBallFlightMode Mode = EChaosImpactBallFlightMode::Arc;
				bool bOverhead = false;
				Own->GetThrowFlight(0.0f, EChaosImpactBallType::Barrier, 1.0f, Along, Up, Mode, bOverhead);
				Test->TestTrue(TEXT("Barrier: a short throw is thrown down"), Up < 0.0f);
				Own->GetThrowFlight(1.0f, EChaosImpactBallType::Barrier, 1.0f, Along, Up, Mode, bOverhead);
				Test->TestTrue(TEXT("Barrier: a full one is lobbed"), Up > 0.0f);
				Other->RefillHealth();
				// --- Mine ---
				Place(Own, Floor, Dir);
				Place(Other, Floor + Dir * 900.0f, -Dir);
				WaitUntil = Now + 0.4;
				Step = 10;
				return false;
			}
			case 10:
				Throw(EChaosImpactBallType::Mine, true);
				WaitUntil = Now + 1.2;
				Step = 11;
				return false;
			case 11:
			{
				AChaosImpactGadget* Laid = FindGadget(true);
				Test->TestNotNull(TEXT("Mine: it lies where it landed"), Laid);
				if (!Laid)
				{
					return true;
				}
				Mine = Laid;
				// Its owner stands on it, well after it is armed.
				Place(Own, Laid->GetActorLocation(), Dir);
				WaitUntil = Now + AChaosImpactGadget::MineArmSeconds + 0.6;
				Step = 12;
				return false;
			}
			case 12:
				Test->TestTrue(TEXT("Mine: its owner does not set it off"), Mine.IsValid() && !Mine->HasEnded() && Mine->GetState().TriggeredAt <= 0.0);
				Shot(TEXT("MineArmed.png"));
				if (!Mine.IsValid())
				{
					return true;
				}
				// (The swap and the barrier's ball have hit it already: full health again, so the blast hurts without a KO.)
				Other->RefillHealth();
				Health = Other->GetHealth();
				OwnHealth = Own->GetHealth();
				Place(Other, Mine->GetActorLocation() + Dir * 110.0f, -Dir);
				MaxSpeed = 0.0f;
				WaitUntil = Now + 0.35;
				Step = 13;
				return false;
			case 13:
				Test->TestTrue(TEXT("Mine: an opponent near sets it off"), Mine.IsValid() && Mine->GetState().TriggeredAt > 0.0);
				Shot(TEXT("MineFuse.png"));
				WaitUntil = Now + AChaosImpactGadget::MineFuseSeconds - 0.2;
				Step = 14;
				return false;
			case 14:
				Shot(TEXT("MineBlast.png"));
				WaitUntil = Now + 0.8;
				Step = 15;
				return false;
			case 15:
			{
				int32 Left = 0;
				for (TActorIterator<AChaosImpactGadget> It(World); It; ++It) { Left += It->HasEnded() ? 0 : 1; }
				UE_LOG(LogTemp, Display, TEXT("NEWBALLS mine: CPU health %.0f -> %.0f, knocked at up to %.0f cm/s; owner %.0f -> %.0f; gadgets left %d"), Health,
					Other->GetHealth(), MaxSpeed, OwnHealth, Own->GetHealth(), Left);
				Test->TestEqual(TEXT("Mine: 1 damage to the opponent"), Other->GetHealth(), Health - 1.0f);
				Test->TestTrue(FString::Printf(TEXT("Mine: knocked away (%.0f cm/s)"), MaxSpeed), MaxSpeed > 500.0f);
				Test->TestEqual(TEXT("Mine: its owner, standing on it, is not hurt"), Own->GetHealth(), OwnHealth);
				Test->TestEqual(TEXT("Nothing left standing"), Left, 0);
				GM->EndMatchForTests();
				return true;
			}
			default:
				return true;
			}
		}

	private:
		static void Place(AChaosImpactCharacter* Character, const FVector& OnFloor, const FVector& Facing)
		{
			const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Character->SetActorLocationAndRotation(OnFloor + FVector(0, 0, HalfHeight + 2.0f), Facing.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
			Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		}

		/** A floor spot with clear floor and no wall for 11 m one way. */
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
						if (World->SweepSingleByObjectType(Hit, Up, Up + Way * 1100.0f, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
							FCollisionShape::MakeSphere(45.0f)))
						{
							continue;
						}
						bool bFloor = true;
						for (float Along = 100.0f; Along <= 1100.0f && bFloor; Along += 100.0f)
						{
							FVector Under;
							bFloor = ChaosImpactParty::FindFloor(World, Floor + Way * Along, 40.0f, 60.0f, Under) && FMath::Abs(Under.Z - Floor.Z) < 5.0f;
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
		float OwnHealth = 0.0f;
		float MaxSpeed = 0.0f;
		FVector Floor = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		FVector OwnBefore = FVector::ZeroVector;
		FVector OtherBefore = FVector::ZeroVector;
		TWeakObjectPtr<AChaosImpactCharacter> CPU;
		TWeakObjectPtr<AChaosImpactGadget> Barrier;
		TWeakObjectPtr<AChaosImpactGadget> Mine;
		double MatchAt = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNewGadgetBallTest, "ChaosImpact.Combat.NewGadgetBalls", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FNewGadgetBallTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNewGadgetBallCommand(this));
	return true;
}
#endif
