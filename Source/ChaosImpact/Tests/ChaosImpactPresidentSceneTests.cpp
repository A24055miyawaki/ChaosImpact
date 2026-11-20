#if WITH_DEV_AUTOMATION_TESTS
// ラスボス ドッチ・ボール大統領 (2026-11-19), played through on solo stage 30 in the real level: the corridor, the arrival film
// (frames along it), the words before (each line), the leap into the arena, every move (six throws, the beam, the car, the
// power-up and its six novas, spent), beaten, the words after, the ending (frames along it), the stage cleared.
// Frames in Saved/PresidentQA. Run: /Game/ChaosImpact/solo/Lvl_Solo_30_WhiteBallHouse?CISolo=1?CIStage=30
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBossScene.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactMusic.h"
#include "Engine/GameInstance.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPresidentBoss.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactWhiteHall.h"
#include "Components/BoxComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

namespace
{
	class FPresidentSceneCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPresidentSceneCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 400.0)
			{
				Test->AddError(FString::Printf(TEXT("President test stalled at step %d"), Step));
				return true;
			}
			if (!World || Now < WaitUntil)
			{
				return false;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own)
			{
				return false;
			}
			AChaosImpactSoloRoom* Room = nullptr;
			for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It) { Room = *It; }
			AChaosImpactPresidentBoss* Boss = nullptr;
			for (TActorIterator<AChaosImpactPresidentBoss> It(World); It; ++It) { Boss = *It; }
			AChaosImpactBossScene* Scene = AChaosImpactBossScene::GetActive(World);
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PresidentQA"), Name + TEXT(".png")), true, false);
			};
			const auto Wait = [this, Now](const double Seconds) { WaitUntil = Now + Seconds; };
			// A camera close on the President (to see its motions and the balls in its hands), and back.
			const auto CloseOn = [this, World, PC, Boss]()
			{
				if (!Boss || !PC)
				{
					return;
				}
				const FVector Face = Boss->GetFace();
				const FVector From = Face + Boss->GetActorForwardVector() * 1500.0f + FVector(0.0f, 0.0f, -150.0f);
				FActorSpawnParameters Params;
				if (ACameraActor* Close = World->SpawnActor<ACameraActor>(From, (Face - FVector(0.0f, 0.0f, 150.0f) - From).Rotation(), Params))
				{
					Close->GetCameraComponent()->bConstrainAspectRatio = false;
					PC->SetViewTarget(Close);
					CloseCamera = Close;
				}
			};
			const auto Back = [this, PC, Own]()
			{
				if (ACameraActor* Close = CloseCamera.Get())
				{
					PC->SetViewTarget(Own);
					Close->Destroy();
				}
			};
			if ((Step >= 6 && Step <= 19) || Step >= 70)
			{
				// (Filming the moves: the player kept on its feet whatever hits it.)
				Own->RefillHealth();
			}
			if (Boss && (Step == 6 || Step == 7 || Step == 70 || Step == 71))
			{
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetOwner() != Boss)
					{
						continue;
					}
					const FVector Off = It->GetActorLocation() - Own->GetActorLocation();
					FVector2D& Pass = Passes.FindOrAdd(*It, FVector2D(1.0e9, 0.0));
					if (Off.Size() < Pass.X)
					{
						Pass = FVector2D(Off.Size(), Off.Z + Own->GetSimpleCollisionHalfHeight());
					}
				}
			}
			if (Step == 8 && Passes.Num() > 0)
			{
				for (const TPair<TWeakObjectPtr<AActor>, FVector2D>& Pass : Passes)
				{
					UE_LOG(LogTemp, Display, TEXT("PRESQA throw pass %.0f cm, height %.0f over feet"), Pass.Value.X, Pass.Value.Y);
				}
				Passes.Reset();
			}
			switch (Step)
			{
			case 0:
				Wait(4.0);
				Step = 1;
				Hits = AChaosImpactCharacter::OnAnyDamaged.AddLambda([](AChaosImpactCharacter* Who, const float Amount, AActor* Causer, APawn* Source)
				{
					UE_LOG(LogTemp, Display, TEXT("PRESQA hit %s for %.1f by %s (%s), health now %.1f"), *GetNameSafe(Who), Amount, *GetNameSafe(Causer),
						*GetNameSafe(Source), Who ? Who->GetHealth() : -1.0f);
				});
				return false;
			case 1:
				Test->TestNotNull(TEXT("The hall is there"), AChaosImpactWhiteHall::Find(World));
				Test->TestTrue(TEXT("The President waits on its throne"), Room && Boss && Boss->IsDormant());
				Shot(TEXT("P_00_Corridor"));
				Wait(1.0);
				Step = 2;
				return false;
			case 2:
				if (!Room || !Boss)
				{
					Test->AddError(TEXT("No room or President"));
					return true;
				}
				Own->TeleportTo(Room->EntryTrigger->GetComponentLocation() + FVector(0.0f, 0.0f, 60.0f), Own->GetActorRotation());
				Room->StartRoom(Own);
				SceneStarted = Now;
				Frame = 0;
				Step = 3;
				return false;
			case 3:
			{
				static const float Times[] = {0.6f, 1.9f, 2.9f, 4.6f, 6.6f, 8.6f, 11.4f, 12.8f, 14.6f, 17.4f, 18.9f, 19.9f, 22.6f, 24.2f};
				const float T = static_cast<float>(Now - SceneStarted);
				if (Frame < UE_ARRAY_COUNT(Times) && T >= Times[Frame])
				{
					Shot(FString::Printf(TEXT("P_Film_%02d"), Frame));
					++Frame;
				}
				if (Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::TalkBefore)
				{
					Step = 4;
					LineShot = -1;
					bLineShot = false;
					Wait(0.3);
				}
				else if (T > 45.0f)
				{
					Test->AddError(TEXT("The film never reached the talk"));
					return true;
				}
				return false;
			}
			case 4:
			case 20:
			{
				const bool bAfter = Step == 20;
				const AChaosImpactBossScene::EPart Talk = bAfter ? AChaosImpactBossScene::EPart::TalkAfter : AChaosImpactBossScene::EPart::TalkBefore;
				if (bAfter && Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::Beaten)
				{
					// (Still falling, beaten: its words come after.)
					Wait(0.25);
					return false;
				}
				if (!Scene || Scene->GetPart() != Talk)
				{
					Step = bAfter ? 21 : 5;
					SceneStarted = Now;
					Frame = 0;
					return false;
				}
				if (LineShot != Scene->GetLineIndex())
				{
					LineShot = Scene->GetLineIndex();
					bLineShot = false;
					Wait(3.2);
					return false;
				}
				if (!bLineShot)
				{
					bLineShot = true;
					Shot(FString::Printf(TEXT("P_%s_%d"), bAfter ? TEXT("After") : TEXT("Before"), LineShot));
					Wait(1.2);
					return false;
				}
				Scene->RequestNext();
				Wait(0.3);
				return false;
			}
			case 5:
			{
				// The leap down into the arena.
				const float T = static_cast<float>(Now - SceneStarted);
				if (Frame == 0 && T >= 0.5f)
				{
					Shot(TEXT("P_Leap"));
					++Frame;
				}
				if (Scene && Scene->GetPart() == AChaosImpactBossScene::EPart::Fight && T > 1.0f)
				{
					Test->TestTrue(TEXT("The fight is on: the President awake, the player free"), Boss && !Boss->IsDormant() && !Own->IsTrainingMenuFrozen());
					Own->SetCanBeDamaged(false);
					if (Boss)
					{
						Boss->bHarmless = true;
						Boss->bHoldMoves = true;
					}
					Wait(1.5);
					Step = 6;
				}
				else if (T > 8.0f)
				{
					Test->AddError(TEXT("Never reached the fight"));
					return true;
				}
				return false;
			}
			case 6:
				Shot(TEXT("P_Fight"));
				if (Boss) { Boss->ForceMove(EChaosImpactPresidentMove::Throws); }
				Wait(1.5);
				Step = 7;
				return false;
			case 7:
				Shot(TEXT("P_Throws"));
				// The fight's own song (タクティカルエンカウンター) once the fight is on.
				if (const UChaosImpactMusic* Music = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UChaosImpactMusic>() : nullptr)
				{
					UE_LOG(LogTemp, Display, TEXT("PRESQA music %s"), *Music->GetCurrentTrackName());
					Test->TestTrue(TEXT("final boss music in the fight"), Music->GetCurrent() == EChaosImpactBgm::FinalBoss);
				}
				CloseOn();
				Wait(0.5);
				Step = 70;
				return false;
			case 70:
				Shot(TEXT("P_Throws_Close"));
				Wait(0.5);
				Step = 71;
				return false;
			case 71:
				Back();
				Wait(2.5);
				Step = 8;
				return false;
			case 8:
				if (Boss) { Boss->ForceMove(EChaosImpactPresidentMove::Beam); }
				Wait(0.8);
				Step = 9;
				return false;
			case 9:
				Shot(TEXT("P_Beam_Warn"));
				Wait(0.9);
				Step = 10;
				return false;
			case 10:
				Shot(TEXT("P_Beam_Fire"));
				CloseOn();
				Wait(0.3);
				Step = 100;
				return false;
			case 100:
				Shot(TEXT("P_Beam_Close"));
				Wait(0.5);
				Step = 101;
				return false;
			case 101:
				Back();
				Wait(2.7);
				Step = 11;
				return false;
			case 11:
				if (Boss) { Boss->ForceMove(EChaosImpactPresidentMove::Car); }
				Wait(0.8);
				Step = 12;
				return false;
			case 12:
				Shot(TEXT("P_Car_Change"));
				Wait(1.6);
				Step = 13;
				return false;
			case 13:
				Test->TestTrue(TEXT("The car rush is seen from straight above"), Own->IsStageOverheadView());
				Shot(TEXT("P_Car_Warn"));
				Wait(1.3);
				Step = 14;
				return false;
			case 14:
				Shot(TEXT("P_Car_Rush"));
				Wait(8.5);
				Step = 15;
				return false;
			case 15:
				Test->TestFalse(TEXT("…and back to the usual camera after"), Own->IsStageOverheadView());
				if (Boss)
				{
					Test->TestEqual(TEXT("Back from its car"), Boss->GetMove(), EChaosImpactPresidentMove::None);
					Boss->TakeDamage(Boss->GetMaxHealth(), FDamageEvent(), nullptr, nullptr);
					Test->TestEqual(TEXT("One big hit stops at half"), Boss->GetHealth(), Boss->GetMaxHealth() * 0.5f);
					Test->TestEqual(TEXT("…and it powers up"), Boss->GetMove(), EChaosImpactPresidentMove::PowerUp);
				}
				Wait(1.2);
				Step = 16;
				return false;
			case 16:
				Test->TestFalse(TEXT("Powering up, it cannot be hit at all"), Boss && Boss->CanBeDamaged());
				Shot(TEXT("P_PowerUp"));
				Wait(3.0);
				Step = 17;
				return false;
			case 17:
				Test->TestEqual(TEXT("It opens with its novas"), Boss ? Boss->GetMove() : EChaosImpactPresidentMove::None, EChaosImpactPresidentMove::NovaStorm);
				Shot(TEXT("P_Novas"));
				Wait(2.6);
				Step = 18;
				return false;
			case 18:
				Shot(TEXT("P_Novas2"));
				Wait(1.2);
				Step = 180;
				return false;
			case 180:
				// (The VS nova's ball of fire, spreading out.)
				Shot(TEXT("P_Novas3"));
				Wait(1.0);
				NovaWaits = 0;
				Step = 181;
				return false;
			case 181:
				// (Its last nova's blast over: then it comes down, spent. Waited for, a moment at a time.)
				if (Boss && Boss->GetMove() == EChaosImpactPresidentMove::NovaStorm && ++NovaWaits < 60)
				{
					Wait(0.25);
					return false;
				}
				Step = 19;
				return false;
			case 19:
				Test->TestEqual(TEXT("Spent after its novas"), Boss ? Boss->GetMove() : EChaosImpactPresidentMove::None, EChaosImpactPresidentMove::Stunned);
				Shot(TEXT("P_Stunned"));
				if (Boss)
				{
					Boss->TakeDamage(Boss->GetMaxHealth(), FDamageEvent(), nullptr, nullptr);
					Test->TestTrue(TEXT("Beaten, not gone"), Boss->IsBeaten() && !Boss->IsEliminated());
				}
				Wait(3.0);
				Step = 20;
				LineShot = -1;
				return false;
			case 21:
			{
				// The ending.
				static const float Times[] = {2.0f, 5.5f, 9.0f, 16.0f, 24.0f, 32.0f, 40.0f, 48.5f, 53.5f};
				const float T = static_cast<float>(Now - SceneStarted);
				if (Frame < UE_ARRAY_COUNT(Times) && T >= Times[Frame])
				{
					Shot(FString::Printf(TEXT("P_End_%02d"), Frame));
					++Frame;
				}
				if (!Scene)
				{
					Wait(2.0);
					Step = 22;
				}
				else if (T > 110.0f)
				{
					Test->AddError(TEXT("The ending never finished"));
					return true;
				}
				return false;
			}
			case 22:
			{
				const AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC);
				Test->TestTrue(TEXT("Gone, and the stage cleared"), Boss == nullptr && Player && Player->GetSoloClearedAt() >= 0.0);
				Shot(TEXT("P_Clear"));
				Wait(0.6);
				Step = 23;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		int32 Step = 0;
		int32 Frame = 0;
		int32 LineShot = -1;
		bool bLineShot = false;
		double SceneStarted = 0.0;
		double WaitUntil = 0.0;
		double Started = FPlatformTime::Seconds();
		FDelegateHandle Hits;
		TWeakObjectPtr<ACameraActor> CloseCamera;
		/** The thrown balls' nearest pass by the player (cm, and the ball's height over the player's feet then). */
		TMap<TWeakObjectPtr<AActor>, FVector2D> Passes;
		int32 NovaWaits = 0;
	public:
		virtual ~FPresidentSceneCommand() override { AChaosImpactCharacter::OnAnyDamaged.Remove(Hits); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPresidentSceneTest, "ChaosImpact.Solo.President",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FChaosImpactPresidentSceneTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPresidentSceneCommand(this));
	return true;
}
#endif
