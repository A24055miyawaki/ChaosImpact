#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactSoloRoom.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Solo stage 8 with its boss, played through in the real level
	 * (-Map /Game/ChaosImpact/solo/Lvl_Solo_08_SnowForest?CISolo=1?CIStage=8?CITargets=0?CICPUCount=0):
	 * the shima-enaga stands in the clearing from the start, asleep and unhurt; it wakes as the player comes in; each of
	 * its moves is set off in turn (the slam leaves the player without their balls); at half health it powers up into
	 * the full-power model with its aura; knocked out, the room is cleared. Its attacks are made harmless so the player
	 * stays in to watch. Screenshots go to Saved/SimaeBossQA.
	 */
	class FSimaeBossCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSimaeBossCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (!World || !Player || Now < NextAt)
			{
				return false;
			}
			AChaosImpactSoloRoom* Room = RoomPtr.Get();
			AChaosImpactSimaeBoss* Boss = BossPtr.Get();
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimaeBossQA"), Name), true, false);
			};
			const auto Wait = [this, Now](const double Seconds) { NextAt = Now + Seconds; ++Step; };
			// The player a little way in front of the boss, the boss facing them.
			const auto StandBefore = [Player, Room](AChaosImpactSimaeBoss* Target, const float Distance)
			{
				// Toward the middle of the arena from the boss (never out past its walls).
				const FVector Middle = Room ? Room->RoomArea->GetComponentLocation() : Player->GetActorLocation();
				const FVector Toward = (Middle - Target->GetActorLocation()).GetSafeNormal2D();
				const FVector Side = Toward.IsNearlyZero() ? FVector(-1.0f, 0.0f, 0.0f) : Toward;
				Player->SetActorLocation(Target->GetActorLocation() + Side * Distance + FVector(0, 0, -100.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Player->GetCharacterMovement()->StopMovementImmediately();
			};
			if (Step > 1 && (!Room || (!Boss && Step < 40)))
			{
				Test->AddError(FString::Printf(TEXT("Lost the room or the boss at step %d."), Step));
				return true;
			}
			// Nobody is ever thrown out of the arena.
			if (Step > 4 && Step < 40 && Room && !bLeftArena)
			{
				const FVector Local = Room->RoomArea->GetComponentTransform().InverseTransformPositionNoScale(Player->GetActorLocation());
				const FVector Extent = Room->RoomArea->GetScaledBoxExtent();
				if (FMath::Abs(Local.X) > Extent.X + 60.0f || FMath::Abs(Local.Y) > Extent.Y + 60.0f)
				{
					bLeftArena = true;
					Test->AddError(FString::Printf(TEXT("Thrown out of the arena at step %d (%s)."), Step, *Player->GetActorLocation().ToCompactString()));
				}
			}
			// Warnings lie on the ground (never up in the air, on someone's head).
			const auto WarningsOnGround = [this, World, Player](const TCHAR* What)
			{
				const float Feet = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				int32 Count = 0;
				for (TActorIterator<AChaosImpactSimaeWarning> It(World); It; ++It)
				{
					++Count;
					const float Above = It->GetActorLocation().Z - Feet;
					if (Above > 60.0f)
					{
						Test->AddError(FString::Printf(TEXT("%s: a warning floats %.0f cm above the ground"), What, Above));
					}
				}
				Test->TestTrue(FString::Printf(TEXT("%s: its warning is shown"), What), Count > 0);
			};
			switch (Step)
			{
			case 0:
				if (!PC->IsGameplayActive())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It)
				{
					RoomPtr = *It;
				}
				Wait(1.0);
				return false;
			case 1:
			{
				for (TActorIterator<AChaosImpactSimaeBoss> It(World); It; ++It)
				{
					BossPtr = *It;
				}
				Boss = BossPtr.Get();
				Test->TestNotNull(TEXT("The room is there"), RoomPtr.Get());
				Test->TestNotNull(TEXT("The shima-enaga stands in the clearing from the start"), Boss);
				if (!Boss || !RoomPtr.IsValid())
				{
					return true;
				}
				Room = RoomPtr.Get();
				Test->TestTrue(TEXT("It waits asleep"), Boss->IsDormant());
				Test->TestEqual(TEXT("No minions first"), Room->MinionCount, 0);
				Test->TestEqual(TEXT("The room is still waiting"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Waiting);
				const float Before = Boss->GetHealth();
				Boss->TakeDamage(3.0f, FDamageEvent(), nullptr, nullptr);
				Test->TestEqual(TEXT("Asleep, it cannot be hurt"), Boss->GetHealth(), Before);
				Boss->bHarmless = true;
				Boss->bHoldMoves = true;
				// At the clearing's doorway, looking in.
				Player->SetActorLocation(Room->RespawnPoint->GetComponentLocation() + FVector(0, 0, 60.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Wait(1.5);
				return false;
			}
			case 2:
				Shot(TEXT("00_Waiting.png"));
				Wait(0.5);
				return false;
			case 3:
				Player->SetActorLocation(Room->EntryTrigger->GetComponentLocation() + FVector(0, 0, 10.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
				Wait(0.5);
				return false;
			case 4:
				Test->TestEqual(TEXT("Coming in starts the boss fight at once"), Room->GetPhase(), EChaosImpactSoloRoomPhase::Boss);
				Test->TestFalse(TEXT("The boss wakes"), Boss->IsDormant());
				Test->TestTrue(TEXT("The door shuts"), Room->AreDoorsClosed());
				StandBefore(Boss, 750.0f);
				Wait(1.2);
				return false;
			case 5:
			{
				Shot(TEXT("01_Awake.png"));
				// Two balls in hand, to see the slam knock them loose.
				for (int32 Index = 0; Index < 2; ++Index)
				{
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (AChaosImpactBall* Ball = World->SpawnActor<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Player->GetActorLocation(),
						FRotator::ZeroRotator, Params))
					{
						Ball->MakePickup();
						Ball->ConsumePickup(Player);
					}
				}
				Test->TestEqual(TEXT("The player holds two balls"), Player->GetCarriedBallCount(), 2);
				StandBefore(Boss, 500.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Slam);
				Wait(0.55);
				return false;
			}
			case 6:
				WarningsOnGround(TEXT("Slam"));
				Shot(TEXT("02_Slam_Warning.png"));
				Wait(0.55);
				return false;
			case 7:
				Shot(TEXT("03_Slam_Air.png"));
				Wait(0.2);
				return false;
			case 8:
				Shot(TEXT("04_Slam_Impact.png"));
				Wait(0.6);
				return false;
			case 9:
				Test->TestEqual(TEXT("The slam knocked every ball loose"), Player->GetCarriedBallCount(), 0);
				Shot(TEXT("05_Slam_After.png"));
				Wait(1.2);
				return false;
			case 10:
				// The balls already about (the pads' own), to tell the goukyu's apart afterwards.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					BallsBefore.Add(*It);
				}
				StandBefore(Boss, 650.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Throw);
				Wait(0.8);
				return false;
			case 11:
				WarningsOnGround(TEXT("Throw"));
				Shot(TEXT("06_Throw_Lane.png"));
				Wait(0.38);
				return false;
			case 12:
			{
				// It is on its way (nothing nearby stops it: it goes through walls and cover).
				int32 Flying = 0;
				for (TActorIterator<AChaosImpactSimaeGoukyu> It(World); It; ++It)
				{
					Flying += It->IsFlying() ? 1 : 0;
				}
				Test->TestTrue(TEXT("The goukyu is flying"), Flying > 0);
			}
				Shot(TEXT("07_Goukyu_Flying.png"));
				Wait(0.25);
				return false;
			case 13:
				Shot(TEXT("08_Goukyu_Flying2.png"));
				Wait(2.0);
				return false;
			case 14:
				// The spent goukyu is a ball lying inside the arena (even after flying out through a wall).
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->IsPickup() && !BallsBefore.Contains(*It))
					{
						const FVector Local = Room->RoomArea->GetComponentTransform().InverseTransformPositionNoScale(It->GetActorLocation());
						const FVector Extent = Room->RoomArea->GetScaledBoxExtent();
						Test->TestTrue(TEXT("A spent goukyu lies inside the arena"),
							FMath::Abs(Local.X) <= Extent.X + 60.0f && FMath::Abs(Local.Y) <= Extent.Y + 60.0f);
					}
				}
				StandBefore(Boss, 900.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Avalanche);
				Wait(1.3);
				return false;
			case 15:
				WarningsOnGround(TEXT("Avalanche"));
				Shot(TEXT("09_Avalanche_Warnings.png"));
				Wait(0.8);
				return false;
			case 16:
				Shot(TEXT("10_Avalanche_Falling.png"));
				Wait(0.5);
				return false;
			case 17:
				Shot(TEXT("11_Avalanche_Impacts.png"));
				Wait(1.5);
				return false;
			case 18:
				StandBefore(Boss, 1400.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Charge);
				Wait(0.6);
				return false;
			case 19:
				WarningsOnGround(TEXT("Charge"));
				Shot(TEXT("12_Charge_Lane.png"));
				Wait(0.55);
				return false;
			case 20:
				Shot(TEXT("13_Charge_Run.png"));
				Wait(2.0);
				return false;
			case 21:
			{
				// Down to half: the power-up.
				StandBefore(Boss, 420.0f);
				USkeletalMesh* NormalModel = Boss->GetMesh()->GetSkeletalMeshAsset();
				NormalModelPath = NormalModel ? NormalModel->GetPathName() : FString();
				Boss->TakeDamage(Boss->GetMaxHealth(), FDamageEvent(), nullptr, nullptr);
				Test->TestEqual(TEXT("One big hit stops at half health"), Boss->GetHealth(), Boss->GetMaxHealth() * 0.5f);
				Test->TestEqual(TEXT("…and it powers up"), Boss->GetMove(), EChaosImpactSimaeMove::PowerUp);
				const float Before = Boss->GetHealth();
				Boss->TakeDamage(1.0f, FDamageEvent(), nullptr, nullptr);
				Test->TestEqual(TEXT("It cannot be hurt while it powers up"), Boss->GetHealth(), Before);
				Wait(1.0);
				return false;
			}
			case 22:
				BeforeRelease = Player->GetActorLocation();
				Shot(TEXT("14_PowerUp_Gathering.png"));
				Wait(0.85);
				return false;
			case 23:
				Shot(TEXT("15_PowerUp_Release.png"));
				Wait(0.4);
				return false;
			case 24:
			{
				// The power-up's gust only pushes, feet down: a short slide, not a throw.
				const float Pushed = FVector::Dist2D(Player->GetActorLocation(), BeforeRelease);
				UE_LOG(LogTemp, Display, TEXT("SIMAEBOSS power-up pushed the player %.0f cm"), Pushed);
				Test->TestTrue(TEXT("The power-up's gust pushes the player a little"), Pushed > 30.0f && Pushed < 450.0f);
				Shot(TEXT("16_PowerUp_Burst.png"));
			}
				Wait(1.6);
				return false;
			case 25:
			{
				Test->TestTrue(TEXT("Full power"), Boss->IsFullPower());
				USkeletalMesh* Model = Boss->GetMesh()->GetSkeletalMeshAsset();
				Test->TestTrue(TEXT("…in the full-power model"), Model && Model->GetPathName().Contains(TEXT("SK_Boss_FullPower")));
				Test->TestNotEqual(TEXT("…not the normal one"), Model ? Model->GetPathName() : FString(), NormalModelPath);
				Test->TestTrue(TEXT("…wrapped in its aura"), Boss->GetAuraStrength() > 0.5f);
				// Close up to see the aura.
				StandBefore(Boss, 330.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::None);
				Wait(0.5);
				return false;
			}
			case 26:
			{
				// The aura's flames are really drawn round it.
				UProceduralMeshComponent* Flames = Boss->FindComponentByClass<UProceduralMeshComponent>();
				const FProcMeshSection* Section = Flames ? Flames->GetProcMeshSection(0) : nullptr;
				const UMaterialInterface* Look = Flames ? Flames->GetMaterial(0) : nullptr;
				UE_LOG(LogTemp, Display, TEXT("SIMAEBOSS aura flames: visible=%d vertices=%d bounds=%s material=%s"),
					Flames && Flames->IsVisible(), Section ? Section->ProcVertexBuffer.Num() : 0,
					Flames ? *Flames->Bounds.GetBox().ToString() : TEXT("-"), Look ? *Look->GetPathName() : TEXT("-"));
				Test->TestTrue(TEXT("The aura's flames are drawn"), Flames && Flames->IsVisible() && Section && Section->ProcVertexBuffer.Num() > 0);
			}
				Shot(TEXT("17_FullPower_Aura.png"));
				StandBefore(Boss, 330.0f);
				{
				// The aura close up: a camera in front of the boss, a little above (back to the player's next step).
					const FVector Chest = Boss->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f);
					const FVector Toward = (Player->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();
					const FVector From = Chest + Toward * 1300.0f + FVector(0.0f, 0.0f, 700.0f);
					FActorSpawnParameters Params;
					Params.ObjectFlags |= RF_Transient;
					if (ACameraActor* Close = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), From, (Chest - From).Rotation(), Params))
					{
						Close->GetCameraComponent()->bConstrainAspectRatio = false;
						PC->SetViewTarget(Close);
						CloseCamera = Close;
					}
				}
				Wait(0.4);
				return false;
			case 27:
				Shot(TEXT("18_FullPower_Aura2.png"));
				StandBefore(Boss, 1400.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Throw);
				Wait(0.65);
				return false;
			case 28:
				if (ACameraActor* Close = CloseCamera.Get())
				{
					PC->SetViewTarget(Player);
					Close->Destroy();
				}
				Shot(TEXT("19_FullPower_Lanes.png"));
				Wait(0.3);
				return false;
			case 29:
				Shot(TEXT("20_FullPower_Goukyu.png"));
				Wait(1.6);
				return false;
			case 30:
				StandBefore(Boss, 600.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Slam);
				Wait(0.82);
				return false;
			case 31:
				Shot(TEXT("21_FullPower_Slam.png"));
				Wait(0.6);
				return false;
			case 32:
				Shot(TEXT("22_FullPower_Slam_After.png"));
				Wait(2.0);
				return false;
			case 33:
				StandBefore(Boss, 900.0f);
				Boss->ForceMove(EChaosImpactSimaeMove::Avalanche);
				Wait(1.6);
				return false;
			case 34:
				Shot(TEXT("23_FullPower_Avalanche.png"));
				Wait(2.0);
				return false;
			case 35:
				Boss->ForceMove(EChaosImpactSimaeMove::None);
				Boss->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr);
				Test->TestTrue(TEXT("Knocked out"), Boss->IsEliminated());
				Wait(0.3);
				return false;
			case 36:
				Shot(TEXT("24_Defeated.png"));
				Step = 40;
				NextAt = Now + 0.5;
				return false;
			default:
				if (Room->GetPhase() != EChaosImpactSoloRoomPhase::Cleared)
				{
					return false;
				}
				Test->TestFalse(TEXT("Cleared: the door opens"), Room->AreDoorsClosed());
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		TWeakObjectPtr<ACameraActor> CloseCamera;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Step = 0;
		FString NormalModelPath;
		FVector BeforeRelease = FVector::ZeroVector;
		bool bLeftArena = false;
		TArray<TWeakObjectPtr<AChaosImpactBall>> BallsBefore;
		TWeakObjectPtr<AChaosImpactSoloRoom> RoomPtr;
		TWeakObjectPtr<AChaosImpactSimaeBoss> BossPtr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSimaeBossTest, "ChaosImpact.Solo.SimaeBoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSimaeBossTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSimaeBossCommand(this));
	return true;
}

#endif
