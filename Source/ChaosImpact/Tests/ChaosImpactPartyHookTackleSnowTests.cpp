#if WITH_DEV_AUTOMATION_TESTS
// ルビーラッシュ on this machine against one CPU: a hook gathers every ruby lying along its way, a tackle shakes a twentieth
// of the rubies loose, and a snowball rolled along the ground (by マグロ too) runs someone over (1 damage, knocked away).
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* HTWorld()
	{
		UWorld* Found = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				Found = Context.World();
			}
		}
		return Found;
	}

	void HTPlace(AChaosImpactCharacter* Character, const FVector& Floor, const FVector& Facing)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->SetActorLocationAndRotation(Floor + FVector(0, 0, HalfHeight + 2.0f), Facing.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}

	FVector HTFeet(const AChaosImpactCharacter* Character)
	{
		return Character->GetActorLocation() - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	class FPartyHookTackleSnowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyHookTackleSnowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = HTWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match)
			{
				Test->AddError(TEXT("Requires a game world"));
				return true;
			}
			if (APlayerController* Pausing = World->GetFirstPlayerController(); Pausing && Pausing->IsPaused())
			{
				Pausing->SetPause(false);
			}
			const double Now = World->GetTimeSeconds();
			if (FPlatformTime::Seconds() - Started > 180.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactPlayerState* OwnState = Own ? Own->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			if (!CPU.IsValid() && World)
			{
				for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
				{
					if (!It->IsPlayerControlled() && It->GetController()) { CPU = *It; }
				}
			}
			AChaosImpactCharacter* Other = CPU.Get();
			// Walking into the CPU with the snowball: input every frame until the run-over (or a few seconds).
			if (Step == 9 && Own && Other && Now < WaitUntil)
			{
				if (Other->GetHealth() < OtherHealth)
				{
					WaitUntil = Now;
				}
				else
				{
					// Facing the way it walks (the snowball is rolled in front of where it faces).
					Own->SetAIAimDirection(Walk);
					Own->SetActorRotation(Walk.Rotation());
					Own->AddMovementInput(Walk, 1.0f);
					if (++WalkFrames % 20 == 0)
					{
						FVector Center;
						float Radius = 0.0f;
						const bool bRolled = Own->GetRolledSnowball(false, Center, Radius);
						UE_LOG(LogTemp, Display, TEXT("SNOWRUN walking %.0f cm/s, rolled %d, ball %.0f from them (r %.0f), %.0f apart"),
							Own->GetVelocity().Size2D(), bRolled ? 1 : 0, FVector::Dist2D(Center, Other->GetActorLocation()), Radius,
							FVector::Dist2D(Own->GetActorLocation(), Other->GetActorLocation()));
					}
					return false;
				}
			}
			if (Now < WaitUntil)
			{
				// After the run-over: how fast they fly off.
				if (Step == 10 && Other)
				{
					MaxSpeed = FMath::Max(MaxSpeed, static_cast<float>(Other->GetVelocity().Size2D()));
				}
				// The hook's furthest reach, sampled as it flies.
				if (Hook.IsValid())
				{
					Farthest = FMath::Max(Farthest, static_cast<float>(FVector::Dist2D(Hook->GetHeadLocation(), HookFrom)));
				}
				return false;
			}
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = ChaosImpactMatch::PartyRuby;
				Rules.CPUCount = 1;
				Rules.CPULevel = 1;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			case 1:
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Field || !Own || !OwnState || !Other || World->GetTimeSeconds() < MatchSeenAt)
				{
					if (Match->Phase == EChaosImpactOnlinePhase::Match && MatchSeenAt == 0.0) { MatchSeenAt = Now + 1.5; }
					return false;
				}
				// The CPU stands still for the test (its brain taken out; put back at the end).
				if (AController* Brain = Other->GetController()) { CPUBrain = Brain; Brain->UnPossess(); }
				Other->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				HTPlace(Other, Field->GetNearestFieldPoint(Own->GetActorLocation() + FVector(0, 2500, 0)), FVector::ForwardVector);
				// Aim along the lying rubies where the most of them lie in a line.
				const FVector Feet = HTFeet(Own);
				int32 BestCount = 0;
				for (const FChaosImpactRubyGem& Aim : Field->GetGems())
				{
					if (Aim.IsCollected() || Aim.bDropped || FVector::Dist2D(Aim.To, Feet) > 900.0f || FVector::Dist2D(Aim.To, Feet) < 200.0f)
					{
						continue;
					}
					const FVector Dir = (FVector(Aim.To) - Feet).GetSafeNormal2D();
					// (Nothing solid in the way: the hook flies its whole length.)
					FHitResult Block;
					const FVector Up = Feet + FVector(0, 0, 100);
					FCollisionQueryParams Query(SCENE_QUERY_STAT(HookLine), false, Own);
					if (World->SweepSingleByObjectType(Block, Up, Up + Dir * (FVector::Dist2D(Aim.To, Feet) + 120.0f), FQuat::Identity,
						FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(20.0f), Query))
					{
						continue;
					}
					int32 Count = 0;
					for (const FChaosImpactRubyGem& Gem : Field->GetGems())
					{
						const FVector Rel = FVector(Gem.To) - Feet;
						const float Along = static_cast<float>(FVector::DotProduct(Rel, Dir));
						Count += !Gem.IsCollected() && Along > 0.0f && Along < 900.0f && FVector::Dist2D(Rel, Dir * Along) < 60.0f ? 1 : 0;
					}
					if (Count > BestCount) { BestCount = Count; Aim2 = Dir; }
				}
				if (BestCount < 2)
				{
					// Walk to the next cluster and look again.
					if (++Tries > 40)
					{
						Test->AddError(TEXT("No line of rubies found to throw the hook along"));
						return true;
					}
					const FChaosImpactRubyGem* Any = nullptr;
					for (const FChaosImpactRubyGem& Gem : Field->GetGems())
					{
						if (!Gem.IsCollected() && !Gem.bDropped && FVector::Dist2D(Gem.To, Feet) > 400.0f && (!Any || FMath::RandBool())) { Any = &Gem; }
					}
					if (Any) { HTPlace(Own, ChaosImpactParty::FindFloor(World, FVector(Any->To) + FVector(500, 0, 50), 100, 300, Spot) ? Spot : FVector(Any->To), FVector::ForwardVector); }
					WaitUntil = Now + 0.3;
					return false;
				}
				// Every ruby lying (landed) now, and the rubies held.
				Lying.Reset();
				for (const FChaosImpactRubyGem& Gem : Field->GetGems())
				{
					if (!Gem.IsCollected() && Now >= Gem.GetLandedAt() + 0.05) { Lying.Add(Gem.Id, FVector(Gem.To)); }
				}
				Before = OwnState->Points;
				HookFrom = Own->GetActorLocation();
				Own->SetActorRotation(Aim2.Rotation());
				Hook = AChaosImpactHook::Release(World, HookFrom + Aim2 * 40.0f, Aim2, Own);
				Test->TestTrue(TEXT("The hook flies"), Hook.IsValid());
				UE_LOG(LogTemp, Display, TEXT("HOOKRUBY thrown along a line of %d rubies"), BestCount);
				Farthest = 0.0f;
				WaitUntil = Now + 0.12;
				Step = 2;
				return false;
			}
			case 2:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/HookRubies.png")), true, false);
				WaitUntil = Now + 1.6;
				Step = 3;
				return false;
			case 3:
			{
				// Every ruby that lay within the hook's reach of the line it flew along is now the thrower's.
				const FVector From(HookFrom.X, HookFrom.Y, 0.0);
				const FVector To = From + Aim2 * Farthest;
				int32 Expected = 0;
				int32 Missed = 0;
				int32 Taken = 0;
				// (Taken ones are soon gone from the list: what lay there is remembered; one still lying there was missed.)
				for (const TPair<int32, FVector>& Was : Lying)
				{
					const FVector At(Was.Value.X, Was.Value.Y, 0.0);
					if (FVector::Dist2D(FMath::ClosestPointOnSegment(At, From, To), At) >= AChaosImpactHook::BallCatchRadius - 5.0f)
					{
						continue;
					}
					++Expected;
					const FChaosImpactRubyGem* Now2 = Field->GetGems().FindByPredicate([&Was](const FChaosImpactRubyGem& Gem) { return Gem.Id == Was.Key; });
					const bool bMine = !Now2 || (Now2->IsCollected() && Now2->Collector == Own);
					Missed += bMine ? 0 : 1;
					Taken += bMine ? 1 : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("HOOKRUBY reach %.0f: %d rubies on the way, %d taken, %d missed; held %d -> %d"),
					Farthest, Expected, Taken, Missed, Before, OwnState->Points);
				Test->TestTrue(FString::Printf(TEXT("Several rubies on the hook's way (%d)"), Expected), Expected >= 2);
				Test->TestEqual(TEXT("None left behind on the hook's way"), Missed, 0);
				Test->TestTrue(FString::Printf(TEXT("All of them counted (%d -> %d)"), Before, OwnState->Points), OwnState->Points - Before >= Expected);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/HookRubiesAfter.png")), true, false);
				// Next: the CPU tackles this player, holding 100.
				OwnState->Points = 100;
				OwnState->ForceNetUpdate();
				// Somewhere open: a field spot with nothing in the way for a few metres.
				FVector Floor = HTFeet(Own);
				PickOpen(World, Field, Floor, Aim2);
				const FVector Dir = Aim2;
				HTPlace(Own, Floor + Dir * 300.0f, -Dir);
				HTPlace(Other, Floor + Dir * 70.0f, Dir);
				Other->SetAIAimDirection(Dir);
				WaitUntil = Now + 0.3;
				Step = 4;
				return false;
			}
			case 4:
				HTPlace(Other, HTFeet(Own) - Aim2 * 230.0f, Aim2);
				Other->SetAIAimDirection(Aim2);
				UE_LOG(LogTemp, Display, TEXT("TACKLERUBY own %s cpu %s (%.0f apart), cpu on ground %d"), *Own->GetActorLocation().ToCompactString(),
					*Other->GetActorLocation().ToCompactString(), FVector::Dist(Own->GetActorLocation(), Other->GetActorLocation()),
					Other->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0);
				Test->TestTrue(TEXT("The CPU's tackle starts"), Other->GetTackle()->Request());
				WaitUntil = Now + 0.9;
				Step = 5;
				return false;
			case 5:
			{
				int32 Dropped = 0;
				for (const FChaosImpactRubyGem& Gem : Field->GetGems())
				{
					Dropped += Gem.bDropped && Gem.Dropper == Own ? Gem.Value : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("TACKLERUBY held 100 -> %d, %d shaken loose, health %.0f"), OwnState->Points, Dropped, Own->GetHealth());
				// (Then free to pick up rubies again: some of what lies about may be back already.)
				Test->TestTrue(FString::Printf(TEXT("A tackle takes rubies away (100 -> %d)"), OwnState->Points), OwnState->Points < 100);
				Test->TestEqual(TEXT("Five rubies fly out"), Dropped, 5);
				Test->TestEqual(TEXT("Twentieth of 7 is one at least"), ChaosImpactMatch::GetRubyTackleDrop(7), 1);
				Test->TestEqual(TEXT("Twentieth of 405"), ChaosImpactMatch::GetRubyTackleDrop(405), 20);
				Test->TestEqual(TEXT("Nothing from nothing"), ChaosImpactMatch::GetRubyTackleDrop(0), 0);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/TackleRubies.png")), true, false);
				// Next: マグロ rolls a snowball.
				OwnState->CharacterIndex = 5;
				OwnState->ForceNetUpdate();
				Own->ResetForOnlineMatch(Own->GetActorLocation(), Aim2.Rotation());
				WaitUntil = Now + 1.0;
				Step = 6;
				return false;
			}
			case 6:
			{
				AChaosImpactBall* Ball = World->SpawnActor<AChaosImpactBall>(Own->GetActorLocation() + FVector(0, 0, 500), FRotator::ZeroRotator);
				Ball->SetBallType(EChaosImpactBallType::Snow);
				Ball->MakePickup();
				Test->TestTrue(TEXT("Snowball picked up"), Own->TryPickupBall(Ball));
				Ball->Destroy();
				WaitUntil = Now + 0.4;
				Step = 7;
				return false;
			}
			case 7:
			{
				FVector Center;
				float Radius = 0.0f;
				Test->TestTrue(TEXT("マグロ rolls the snowball along the ground in front"), Own->GetRolledSnowball(false, Center, Radius));
				Test->TestTrue(TEXT("It lies on the ground"), FMath::Abs((Center.Z - Radius) - HTFeet(Own).Z) < 10.0f);
				// Standing still: nobody is run over.
				FVector Floor = HTFeet(Own);
				PickOpen(World, Field, Floor, Walk);
				HTPlace(Own, Floor, Walk);
				HTPlace(Other, Floor + Walk * 105.0f, -Walk);
				WalkFrom = Floor;
				OtherHealth = Other->GetHealth();
				WaitUntil = Now + 0.5;
				Step = 8;
				return false;
			}
			case 8:
				Test->TestEqual(TEXT("Not run over by a snowball standing still"), Other->GetHealth(), OtherHealth);
				// First a small snowball (just picked up): walked into them, nothing.
				Phase = 0;
				Step = 11;
				return false;
			case 11:
			{
				// Set up the next walk: the snowball's size for it, the CPU a little way ahead (after the cooldown).
				const float Sizes[] = {0.0f, 0.6f, 0.6f, 0.3f};
				Own->SetSnowGrowthForTests(0, Sizes[Phase]);
				HTPlace(Own, WalkFrom, Walk);
				HTPlace(Other, WalkFrom + Walk * 330.0f, -Walk);
				Other->GetCharacterMovement()->Velocity = FVector::ZeroVector;
				OtherHealth = Other->GetHealth();
				if (Phase == 1)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/SnowRoll.png")), true, false);
				}
				WaitUntil = Now + (Phase == 0 ? 2.5 : 4.0);
				Step = 9;
				return false;
			}
			case 9:
			{
				const float Growth = Own->GetSnowGrowth(0);
				const int32 Hits = Own->GetSnowRunOverHits(0);
				const bool bSnow = Own->GetCarriedBallCount() > 0 && Own->GetCarriedBallType(0) == EChaosImpactBallType::Snow;
				UE_LOG(LogTemp, Display, TEXT("SNOWRUN phase %d: health %.0f -> %.0f, snowball %s growth %.2f, %d run over"), Phase, OtherHealth,
					Other->GetHealth(), bSnow ? TEXT("kept") : TEXT("gone"), Growth, Hits);
				if (Phase == 0)
				{
					Test->TestEqual(TEXT("A small snowball runs nobody over"), Other->GetHealth(), OtherHealth);
				}
				else
				{
					Test->TestEqual(FString::Printf(TEXT("Run over %d: 1 damage"), Phase), Other->GetHealth(), OtherHealth - 1.0f);
					if (Phase < AChaosImpactCharacter::SnowRunOverBreakHits)
					{
						Test->TestTrue(FString::Printf(TEXT("Run over %d: back to its smallest"), Phase), bSnow && Growth < 0.02f);
						Test->TestEqual(FString::Printf(TEXT("Run over %d: counted"), Phase), Hits, Phase);
						Test->TestTrue(TEXT("It grows less big each time"), AChaosImpactCharacter::GetSnowGrowthCap(Phase) < AChaosImpactCharacter::GetSnowGrowthCap(Phase - 1)
							&& ChaosImpactBallTypes::IsSnowOverhead(ChaosImpactBallTypes::GetSnowScale(AChaosImpactCharacter::GetSnowGrowthCap(Phase))));
					}
					else
					{
						Test->TestFalse(TEXT("One run-over breaks it up"), bSnow);
					}
				}
				if (Phase == 1)
				{
					HitAt = Other->GetActorLocation();
					MaxSpeed = 0.0f;
					WaitUntil = Now + 0.6;
					Step = 10;
					return false;
				}
				if (Phase >= AChaosImpactCharacter::SnowRunOverBreakHits)
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/SnowBroke.png")), true, false);
					if (CPUBrain.IsValid()) { CPUBrain->Possess(Other); }
					GM->EndMatchForTests();
					return true;
				}
				++Phase;
				WaitUntil = Now + 1.1;
				Step = 11;
				return false;
			}
			case 10:
			{
				const float Moved = static_cast<float>(FVector::Dist2D(Other->GetActorLocation(), HitAt));
				UE_LOG(LogTemp, Display, TEXT("SNOWRUN knocked %.0f cm away, at up to %.0f cm/s"), Moved, MaxSpeed);
				Test->TestTrue(FString::Printf(TEXT("Knocked away (%.0f cm, %.0f cm/s)"), Moved, MaxSpeed), MaxSpeed > 600.0f);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/SnowRunOver.png")), true, false);
				if (Phase >= AChaosImpactCharacter::SnowRunOverBreakHits)
				{
					if (CPUBrain.IsValid()) { CPUBrain->Possess(Other); }
					GM->EndMatchForTests();
					return true;
				}
				++Phase;
				WaitUntil = Now + 0.6;
				Step = 11;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 Tries = 0;
		int32 WalkFrames = 0;
		int32 Before = 0;
		float Farthest = 0.0f;
		float OtherHealth = 0.0f;
		FVector Aim2 = FVector::ForwardVector;
		FVector Walk = FVector::ForwardVector;
		FVector HookFrom = FVector::ZeroVector;
		FVector HitAt = FVector::ZeroVector;
		FVector WalkFrom = FVector::ZeroVector;
		int32 Phase = 0;
		float MaxSpeed = 0.0f;
		FVector Spot = FVector::ZeroVector;
		TMap<int32, FVector> Lying;
		TWeakObjectPtr<AChaosImpactHook> Hook;
		TWeakObjectPtr<AChaosImpactCharacter> CPU;
		TWeakObjectPtr<AController> CPUBrain;

		/** A field spot (from Floor on) with clear floor and no wall for 7 m one way: that way out. */
		static void PickOpen(UWorld* World, const AChaosImpactRubyField* Field, FVector& Floor, FVector& Way)
		{
			for (const FVector& Point : Field->GetFieldPoints())
			{
				for (int32 Turn = 0; Turn < 8; ++Turn)
				{
					const FVector Dir = FRotator(0.0f, Turn * 45.0f, 0.0f).Vector();
					FHitResult Hit;
					const FVector Up = Point + FVector(0, 0, 60);
					if (World->SweepSingleByChannel(Hit, Up, Up + Dir * 700.0f, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(50.0f)))
					{
						continue;
					}
					bool bFloor = true;
					for (float Along = 100.0f; Along <= 700.0f && bFloor; Along += 100.0f)
					{
						FVector Under;
						bFloor = ChaosImpactParty::FindFloor(World, Point + Dir * Along, 40.0f, 60.0f, Under);
					}
					if (bFloor)
					{
						Floor = Point;
						Way = Dir;
						return;
					}
				}
			}
		}
		double MatchSeenAt = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

namespace
{
	/** クラウンキープ: a CPU holding the crown in front of this player, with counts, others with counts too (the head tags). */
	class FCrownHolderLookCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCrownHolderLookCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = HTWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match) { Test->AddError(TEXT("Requires a game world")); return true; }
			if (APlayerController* Pausing = World->GetFirstPlayerController(); Pausing && Pausing->IsPaused()) { Pausing->SetPause(false); }
			const double Now = World->GetTimeSeconds();
			if (FPlatformTime::Seconds() - Started > 120.0) { Test->AddError(TEXT("Timed out")); return true; }
			if (Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			TArray<AChaosImpactCharacter*> CPUs;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It) { if (!It->IsPlayerControlled() && It->GetController()) { CPUs.Add(*It); } }
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = ChaosImpactMatch::PartyCrown;
				Rules.CPUCount = 2;
				Rules.CPULevel = 1;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			case 1:
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Own || CPUs.Num() < 2) { return false; }
				Test->TestEqual(TEXT("30 counts to win"), ChaosImpactMatch::CrownGoalCounts, 30);
				Test->TestEqual(TEXT("Everyone starts with 30 to go"), ChaosImpactParty::GetShownCount(World, Own->GetPlayerState()), 30);
				for (AChaosImpactCharacter* CPU : CPUs) { if (AController* Brain = CPU->GetController()) { Brain->SetActorTickEnabled(false); } }
				AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
				Test->TestNotNull(TEXT("The crown"), Crown);
				if (!Crown) { return true; }
				const FVector Feet = HTFeet(Own);
				HTPlace(CPUs[0], Feet + FVector(-80, 260, 0), FVector::ForwardVector);
				HTPlace(CPUs[1], Feet + FVector(-80, -260, 0), FVector::ForwardVector);
				if (AChaosImpactPlayerState* State = CPUs[1]->GetPlayerState<AChaosImpactPlayerState>()) { State->Points = 9; }
				if (AChaosImpactPlayerState* State = Own->GetPlayerState<AChaosImpactPlayerState>()) { State->Points = 4; }
				Crown->TakeByHook(CPUs[0], CPUs[0]->GetActorLocation());
				WaitUntil = Now + 3.2;
				Step = 2;
				return false;
			}
			case 2:
			{
				const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
				Test->TestTrue(TEXT("The CPU holds the crown"), Crown && Crown->GetHolder() == CPUs[0]);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA/CrownHolderTags.png")), true, false);
				WaitUntil = Now + 0.5;
				Step = 3;
				return false;
			}
			case 3:
			{
				// A tackle knocks the crown off its holder (to the floor near by).
				GM->NotifyPartyTackle(CPUs[0]);
				const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
				Test->TestTrue(TEXT("Tackled: the crown fell"), Crown && Crown->IsOnFloor());
				if (Crown)
				{
					const float Off = static_cast<float>(FVector::Dist2D(FVector(Crown->GetState().To), CPUs[0]->GetActorLocation()));
					Test->TestTrue(FString::Printf(TEXT("...near the one tackled (%.0f)"), Off), Off < 900.0f);
				}
				WaitUntil = Now + 0.5;
				Step = 4;
				return false;
			}
			default:
				for (AChaosImpactCharacter* CPU : CPUs) { if (AController* Brain = CPU->GetController()) { Brain->SetActorTickEnabled(true); } }
				GM->EndMatchForTests();
				return true;
			}
		}
	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrownHolderLookTest, "ChaosImpact.Party.CrownHolderLook", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrownHolderLookTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrownHolderLookCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyHookTackleSnowTest, "ChaosImpact.Party.HookTackleSnow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyHookTackleSnowTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyHookTackleSnowCommand(this));
	return true;
}
#endif
