#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール's own effects (2026-11-17): 操縦ファイア draws its line of fire for its whole steering, the line going out
// from its tail; 極太光線 (ビーム×スノー) is as wide as the snow; ドライブ×ビーム passes through walls while steered;
// 炎雷 (サンダー×ファイア) leaves fires where it rebounds; the rules' 合体ボール row lets it be used in a match. Frames in
// Saved/FusionQA/Special_*.png. Run on a training stage without CPUs: Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=0
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactTornado.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FFusionSpecialCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionSpecialCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 150.0) { Test->AddError(FString::Printf(TEXT("Special test stalled at step %d"), Step)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
			if (!Own || !Match) { return false; }
			Own->RefillHealth();
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"), Name + TEXT(".png")), true, false);
			};
			const auto Zones = [World](const EChaosImpactBallType Kind)
			{
				int32 Count = 0;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { Count += It->GetZoneType() == Kind ? 1 : 0; }
				return Count;
			};
			const auto Clear = [World]()
			{
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { if (!It->IsPickup()) { It->Destroy(); } }
			};
			const auto Ready = [&](const EChaosImpactBallType A, const EChaosImpactBallType B)
			{
				Clear();
				Own->GiveBallsForTests(A, B);
				Own->RequestBallFusion();
				Test->TestTrue(FString::Printf(TEXT("%s and %s joined"), ChaosImpactBallTypes::GetInternalName(A), ChaosImpactBallTypes::GetInternalName(B)),
					Own->AreBallsFused());
			};
			const auto Thrown = [World](const EChaosImpactBallType Flier, const EChaosImpactBallType Rider) -> AChaosImpactBall*
			{
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == Flier && It->GetFusedType() == Rider && !It->HasDetonated()) { return *It; }
				}
				return nullptr;
			};
			const FVector Ahead = Own->GetActorForwardVector().GetSafeNormal2D();
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 3.0;
				Step = 100;
				return false;
			case 100:
				// The rules page with its 合体ボール row (a local match's).
				if (AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC))
				{
					Player->OpenMatchRules(EChaosImpactScreen::Playing);
				}
				WaitUntil = Now + 2.0;
				Step = 101;
				return false;
			case 101:
				Shot(TEXT("Special_Rules"));
				WaitUntil = Now + 0.5;
				Step = 102;
				return false;
			case 102:
				if (AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC))
				{
					Player->CancelMatchRules();
				}
				WaitUntil = Now + 1.0;
				Step = 1;
				return false;
			case 1:
			{
				// The rules' 合体ボール: off, no joining in a match; on, joining.
				const bool bWasMatch = Match->bVersusMatch;
				Own->GiveBallsForTests(EChaosImpactBallType::Fire, EChaosImpactBallType::Ice);
				Match->bVersusMatch = true;
				Match->Rules.bFusionBalls = false;
				Test->TestFalse(TEXT("合体ボール なし: no joining in a match"), Own->CanFuseBalls());
				Match->Rules.bFusionBalls = true;
				Test->TestTrue(TEXT("合体ボール あり: joining in a (local) match"), Own->CanFuseBalls());
				Match->bVersusMatch = bWasMatch;
				Match->Rules.bFusionBalls = false;
				// 操縦ファイア.
				Ready(EChaosImpactBallType::Fire, EChaosImpactBallType::Drive);
				WaitUntil = Now + 1.0;
				Step = 2;
				return false;
			}
			case 2:
				Test->TestTrue(TEXT("操縦ファイア goes"), Own->ThrowForTests(Ahead, 0.0f));
				StepAt = Now;
				WaitUntil = Now + 2.6;
				Step = 3;
				return false;
			case 3:
				FireEarly = Zones(EChaosImpactBallType::Fire);
				Shot(TEXT("Special_DriveFire_1"));
				WaitUntil = Now + 2.2;
				Step = 4;
				return false;
			case 4:
			{
				const int32 FireLate = Zones(EChaosImpactBallType::Fire);
				UE_LOG(LogTemp, Display, TEXT("FUSIONSPECIAL drive fire: %d fires at 2.6 s, %d at 4.8 s"), FireEarly, FireLate);
				Test->TestTrue(TEXT("Its line of fire is still being drawn late in its steering"), FireLate >= 6 && (Thrown(EChaosImpactBallType::Drive,
					EChaosImpactBallType::Fire) == nullptr || FireLate > 0));
				Shot(TEXT("Special_DriveFire_2"));
				// 極太光線.
				Ready(EChaosImpactBallType::Beam, EChaosImpactBallType::Snow);
				Own->SetSnowGrowthForTests(0, 1.0f);
				WaitUntil = Now + 1.2;
				Step = 5;
				return false;
			}
			case 5:
				Test->TestTrue(TEXT("極太光線 goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 0.35;
				Step = 6;
				return false;
			case 6:
			{
				if (const AChaosImpactBall* Beam = Thrown(EChaosImpactBallType::Beam, EChaosImpactBallType::Snow))
				{
					UE_LOG(LogTemp, Display, TEXT("FUSIONSPECIAL beam x snow width %.2f"), Beam->GetBeamWidthScale());
					Test->TestEqual(TEXT("As thick as it goes, the snow rolled big"), Beam->GetBeamWidthScale(), AChaosImpactBall::FusionBeamMaxWidth);
				}
				else
				{
					Test->AddError(TEXT("No beam with snow in flight"));
				}
				Shot(TEXT("Special_BeamSnow"));
				// ドライブ×ビーム.
				Ready(EChaosImpactBallType::Beam, EChaosImpactBallType::Drive);
				WaitUntil = Now + 1.0;
				Step = 7;
				return false;
			}
			case 7:
				Test->TestTrue(TEXT("ドライブ×ビーム goes"), Own->ThrowForTests(Ahead, 0.0f));
				WaitUntil = Now + 0.8;
				Step = 8;
				return false;
			case 8:
			{
				if (const AChaosImpactBall* Drive = Thrown(EChaosImpactBallType::Drive, EChaosImpactBallType::Beam))
				{
					Test->TestTrue(TEXT("Steered, it passes through walls"), Drive->IsWallDrive());
					const USphereComponent* Sphere = Drive->FindComponentByClass<USphereComponent>();
					Test->TestTrue(TEXT("Walls do not stop it"), Sphere && Sphere->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Ignore);
				}
				else
				{
					Test->AddError(TEXT("No drive with a beam in flight"));
				}
				Shot(TEXT("Special_DriveBeam"));
				WaitUntil = Now + 5.8;
				Step = 9;
				return false;
			}
			case 9:
				// 炎雷.
				Ready(EChaosImpactBallType::Fire, EChaosImpactBallType::Thunder);
				WaitUntil = Now + 1.0;
				Step = 10;
				return false;
			case 10:
				Test->TestTrue(TEXT("炎雷 goes"), Own->ThrowForTests(Ahead, 0.3f));
				Shot(TEXT("Special_ThunderFire_0"));
				WaitUntil = Now + 2.2;
				Step = 11;
				return false;
			case 11:
			{
				const int32 Fires = Zones(EChaosImpactBallType::Fire);
				UE_LOG(LogTemp, Display, TEXT("FUSIONSPECIAL thunder fire: %d fires"), Fires);
				Test->TestTrue(TEXT("Fires where it rebounded (and burst)"), Fires >= 2);
				Shot(TEXT("Special_ThunderFire"));
				// 雷の渦.
				Ready(EChaosImpactBallType::Black, EChaosImpactBallType::Thunder);
				WaitUntil = Now + 1.0;
				Step = 12;
				return false;
			}
			case 12:
				Test->TestTrue(TEXT("雷の渦 goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 4.0;
				Step = 13;
				return false;
			case 13:
				Shot(TEXT("Special_ThunderBlack"));
				// 雷雲竜巻: the tornado flies, at a thunder ball's speed.
				Ready(EChaosImpactBallType::Thunder, EChaosImpactBallType::Wind);
				Test->TestEqual(TEXT("The tornado goes in front"), Own->GetCarriedBallType(0), EChaosImpactBallType::Wind);
				WaitUntil = Now + 1.0;
				Step = 14;
				return false;
			case 14:
				Test->TestTrue(TEXT("雷雲竜巻 goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 0.8;
				Step = 15;
				return false;
			case 15:
			{
				bool bFast = false;
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					UE_LOG(LogTemp, Display, TEXT("FUSIONSPECIAL thunder tornado speed %.0f"), It->GetTravelSpeed());
					bFast |= It->GetTravelSpeed() > AChaosImpactTornado::TravelSpeed * 3.0f;
				}
				Test->TestTrue(TEXT("As fast as thunder"), bFast);
				Shot(TEXT("Special_ThunderWind"));
				WaitUntil = Now + 3.5;
				Step = 16;
				return false;
			}
			default:
				Clear();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		double StepAt = 0.0;
		int32 Step = 0;
		int32 FireEarly = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionSpecialTest, "ChaosImpact.Training.FusionSpecial",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactFusionSpecialTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionSpecialCommand(this));
	return true;
}
#endif
