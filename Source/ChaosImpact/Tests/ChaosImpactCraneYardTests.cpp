#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCraneYardStage.h"
#include "ChaosImpactHook.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** The course as laid out: every hop within the anchor's reach, never a climb the anchor cannot make. */
	bool CheckLayout(FAutomationTestBase& Test)
	{
		const TArray<AChaosImpactCraneYardStage::FPlatform>& Platforms = AChaosImpactCraneYardStage::GetPlatforms();
		Test.TestTrue(TEXT("A long course (30 stacks or more)"), Platforms.Num() >= 30);
		Test.TestTrue(TEXT("Enemies along it"), AChaosImpactCraneYardStage::GetSnipers().Num() >= 10);
		for (int32 Index = 1; Index < Platforms.Num(); ++Index)
		{
			const float Climb = Platforms[Index].Top - Platforms[Index - 1].Top;
			Test.TestTrue(FString::Printf(TEXT("Hop %d climbs no more than the anchor allows (%.0f)"), Index, Climb), Climb <= 90.0f);
		}
		return true;
	}

	/**
	 * In Lvl_Solo_04_CraneYard: the stage built, the player on the girder with an anchor in hand; every hop thrown for
	 * real (the anchor released from where the last one put them down, at the next post) lands them on the next stack, to
	 * the goal; falling sends them back to the start; the enemies fire drive balls. Screenshots on the way.
	 */
	class FCraneYardCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCraneYardCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 360.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d (hop %d)."), Step, Hop));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCraneYardStage* Stage = nullptr;
			for (TActorIterator<AChaosImpactCraneYardStage> It(World); It; ++It)
			{
				Stage = *It;
			}
			if (!Player || !Stage || Now < NextAt)
			{
				return false;
			}
			const TArray<AChaosImpactCraneYardStage::FPlatform>& Platforms = AChaosImpactCraneYardStage::GetPlatforms();
			const float Half = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			switch (Step)
			{
			case 0:
				// On the girder, an anchor in hand, the enemies standing (holding fire for the run through).
				Stage->SetSnipersQuiet(true);
				if (World->GetTimeSeconds() < 3.0) { return false; }
				Test->TestEqual(TEXT("Not shot at on the girder"), Player->GetHealth(), Player->GetMaxHealth());
				Test->TestTrue(TEXT("Starts on the girder"), FVector::Dist(Player->GetActorLocation() - FVector(0.0f, 0.0f, Half), Stage->GetStartPoint()) < 150.0f);
				Test->TestTrue(TEXT("An anchor in hand"), Player->GetCarriedBallCount() == 1 && Player->GetCarriedBallType(0) == EChaosImpactBallType::Anchor);
				Test->TestEqual(TEXT("Every enemy standing"), Stage->GetSniperCharacters().FilterByPredicate([](const AChaosImpactCharacter* C) { return C != nullptr; }).Num(),
					AChaosImpactCraneYardStage::GetSnipers().Num());
				Shot(TEXT("start.png"));
				Stage->SetSnipersQuiet(true);
				Stage->SetHazardsParked(true);
				Step = 20;
				View = 0;
				Hop = 0;
				NextAt = Now + 0.5;
				return false;
			case 1:
			{
				// From where the last anchor put them (the girder first), an anchor at the next post.
				const FVector Feet = Stage->GetLandingPoint(Hop);
				Player->SetActorLocation(Feet + FVector(0.0f, 0.0f, Half + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Player->GetCharacterMovement()->StopMovementImmediately();
				const FVector Post = Stage->GetPostFoot(Hop + 1);
				const FVector Way = (Post - Feet).GetSafeNormal2D();
				Player->SetActorRotation(Way.Rotation());
				const FVector Hand = Player->GetActorLocation() + Way * 90.0f + FVector(0.0f, 0.0f, 55.0f);
				AChaosImpactHook* Anchor = AChaosImpactHook::Release(World, Hand, Way, Player, true);
				// (What the player's own throw does: the anchor is theirs to ride.)
				Player->ShowOwnHook(Anchor);
				Test->TestTrue(FString::Printf(TEXT("Hop %d: the anchor takes hold on the next stack"), Hop + 1),
					Anchor && Stage->IsOverPlatform(Hop + 1, Anchor->GetZipTarget(), 30.0f));
				if (Hop == 3 || Hop == 14 || Hop == 19 || Hop == 28)
				{
					Shot(FString::Printf(TEXT("hop_%02d.png"), Hop + 1));
					UE_LOG(LogTemp, Display, TEXT("CRANEYARD camera arm %.0f socket %s (view target %s)"), Player->GetCameraBoom()->TargetArmLength, *Player->GetCameraBoom()->SocketOffset.ToCompactString(),
						*GetNameSafe(PC->GetViewTarget()));
				}
				Step = 2;
				NextAt = Now + 0.3;
				return false;
			}
			case 2:
			{
				// Wait for the zip to set them down (the anchor gone, on their feet).
				bool bAnchorOut = false;
				for (TActorIterator<AChaosImpactHook> It(World); It; ++It)
				{
					bAnchorOut |= It->GetSourcePawn() == Player;
				}
				if (bAnchorOut || Player->IsAnchorZipping() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return false;
				}
				const bool bOn = Stage->IsOverPlatform(Hop + 1, Player->GetActorLocation(), 10.0f);
				Test->TestTrue(FString::Printf(TEXT("Hop %d: standing on the next stack"), Hop + 1), bOn);
				Test->TestEqual(FString::Printf(TEXT("Hop %d: never fell"), Hop + 1), Stage->GetFallCount(), 0);
				if (!bOn)
				{
					UE_LOG(LogTemp, Warning, TEXT("CRANEYARD hop %d ended at %s (landing %s)"), Hop + 1, *Player->GetActorLocation().ToCompactString(),
						*Stage->GetLandingPoint(Hop + 1).ToCompactString());
				}
				if (Hop == 15 || Hop == 20)
				{
					Shot(FString::Printf(TEXT("landed_%02d.png"), Hop + 1));
				}
				++Hop;
				if (Hop >= Platforms.Num() - 1)
				{
					Step = 3;
					NextAt = Now + 0.6;
					return false;
				}
				Step = 1;
				NextAt = Now + 0.15;
				return false;
			}
			case 3:
				Test->TestTrue(TEXT("The goal reached on the last stack"), Stage->HasReachedGoal());
				Test->TestEqual(TEXT("The arrow followed to the last stack"), Stage->GetReachedPlatform(), Platforms.Num() - 1);
				Stage->SetHazardsParked(false);
				Shot(TEXT("goal.png"));
				Step = 4;
				NextAt = Now + 1.0;
				return false;
			case 4:
			{
				// A fall from stack 2 (stood on first): one health less, back on stack 2.
				Stage->RestartForTests();
				Player->SetActorLocation(Stage->GetLandingPoint(2) + FVector(0.0f, 0.0f, Half + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
				HealthBefore = Player->GetHealth();
				Step = 30;
				NextAt = Now + 0.6;
				return false;
			}
			case 30:
			{
				const FVector Off = Stage->GetActorTransform().TransformPosition(FVector(3750.0f, -900.0f, 1300.0f));
				Player->SetActorLocation(Off, false, nullptr, ETeleportType::TeleportPhysics);
				Step = 31;
				NextAt = Now + 2.5;
				return false;
			}
			case 31:
				Test->TestEqual(TEXT("A fall costs one health"), Player->GetHealth(), HealthBefore - 1.0f);
				Test->TestTrue(TEXT("Back on the stack thrown from"), Stage->IsOverPlatform(2, Player->GetActorLocation(), 20.0f));
				// Falling until no health is left: back to the girder, health full.
				for (int32 Fall = 0; Fall < 5 && Player->GetHealth() > 1.0f; ++Fall)
				{
					Player->LoseHealthToStage(1.0f);
				}
				Player->SetActorLocation(Stage->GetActorTransform().TransformPosition(FVector(3750.0f, -900.0f, 1300.0f)), false, nullptr,
					ETeleportType::TeleportPhysics);
				Step = 5;
				NextAt = Now + 2.5;
				return false;
			case 5:
				Test->TestTrue(TEXT("A fall counted"), Stage->GetFallCount() >= 2);
				Test->TestTrue(TEXT("Back on the girder with no health left"),
					FVector::Dist2D(Player->GetActorLocation(), Stage->GetStartPoint()) < 200.0f);
				Test->TestEqual(TEXT("Health full again"), Player->GetHealth(), Player->GetMaxHealth());
				Test->TestTrue(TEXT("Still an anchor in hand"), Player->GetCarriedBallCount() >= 1);
				// The enemies: the player on stack 4 for a while, they open fire.
				Stage->SetSnipersQuiet(false);
				Player->SetActorLocation(Stage->GetLandingPoint(4) + FVector(0.0f, 0.0f, Half + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Step = 6;
				NextAt = Now + 1.6;
				return false;
			case 6:
				Shot(TEXT("sniper_warning.png"));
				Step = 7;
				NextAt = Now + 3.0;
				return false;
			case 20:
			{
				// Views from above of the whole yard (a camera of the test's own), then back to the player's view.
				static const FVector Eyes[][2] = {
					{FVector(-4500.0f, -6500.0f, 6500.0f), FVector(6000.0f, 4000.0f, 1000.0f)},
					{FVector(16000.0f, -5000.0f, 5500.0f), FVector(6000.0f, 6000.0f, 1000.0f)},
					{FVector(6000.0f, 6600.0f, 4200.0f), FVector(6000.0f, 9500.0f, 900.0f)},
					{FVector(-4000.0f, 9000.0f, 3600.0f), FVector(2000.0f, 13500.0f, 900.0f)},
					{FVector(3200.0f, -1500.0f, 2600.0f), FVector(6000.0f, 400.0f, 1700.0f)},
					{FVector(9000.0f, 21000.0f, 3000.0f), FVector(6000.0f, 11000.0f, 800.0f)},
				};
				if (!Camera)
				{
					Camera = World->SpawnActor<ACameraActor>();
				}
				if (View < static_cast<int32>(UE_ARRAY_COUNT(Eyes)))
				{
					const FVector Eye = Stage->GetActorTransform().TransformPosition(Eyes[View][0]);
					const FVector At = Stage->GetActorTransform().TransformPosition(Eyes[View][1]);
					Camera->SetActorLocationAndRotation(Eye, (At - Eye).Rotation());
					Camera->GetCameraComponent()->SetFieldOfView(70.0f);
					PC->SetViewTarget(Camera);
					if (View > 0)
					{
						Shot(FString::Printf(TEXT("overview_%d.png"), View - 1));
					}
					++View;
					NextAt = Now + 0.8;
					return false;
				}
				Shot(FString::Printf(TEXT("overview_%d.png"), View - 1));
				Step = 21;
				NextAt = Now + 0.6;
				return false;
			}
			case 21:
				PC->SetViewTarget(Player);
				Step = 1;
				Hop = 0;
				NextAt = Now + 0.5;
				return false;
			default:
			{
				int32 Drives = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					Drives += It->GetBallType() == EChaosImpactBallType::Drive ? 1 : 0;
				}
				bool bAnyDriving = false;
				for (AChaosImpactCharacter* Enemy : Stage->GetSniperCharacters())
				{
					bAnyDriving |= Enemy && Enemy->IsDriving();
				}
				UE_LOG(LogTemp, Display, TEXT("CRANEYARD drive balls out %d, driving %d, falls %d"), Drives, bAnyDriving ? 1 : 0, Stage->GetFallCount());
				Test->TestTrue(TEXT("The enemies fire drive balls"), Drives > 0 || bAnyDriving || Stage->GetFallCount() > 1 || Player->IsEliminated());
				Shot(TEXT("sniper_fire.png"));
				return true;
			}
			}
		}

	private:
		void Shot(const FString& Name) const
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CraneYardQA"), Name), true, false);
		}

		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		int32 Hop = 0;
		int32 View = 0;
		float HealthBefore = 0.0f;
		ACameraActor* Camera = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCraneYardLayoutTest, "ChaosImpact.Solo.CraneYardLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCraneYardLayoutTest::RunTest(const FString& Parameters)
{
	return CheckLayout(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCraneYardTest, "ChaosImpact.Solo.CraneYard",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactCraneYardTest::RunTest(const FString& Parameters)
{
	CheckLayout(*this);
	ADD_LATENT_AUTOMATION_COMMAND(FCraneYardCommand(this));
	return true;
}

#endif
