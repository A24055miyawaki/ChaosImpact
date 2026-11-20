#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール online (2026-11-17), run on a guest of a listen host's room (the room's lobby, where joining is allowed):
// the host puts two balls in the guest's hands; the guest joins them (吸引ドライブ: the drive goes in front), throws, and
// sees on its own screen the fused drive (its kind riding along, steered, drawing in), then the black hole it opens; then
// a fused tornado (ウィンド×ファイア) and its line of fire. Two processes: Tools as in the party test (the host started with
// ?listen on a training stage, the guest joining its loopback port with -ExecCmds="Automation RunTests ChaosImpact.Fusion.Online").
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FFusionOnlineCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionOnlineCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 330.0) { Test->AddError(FString::Printf(TEXT("Online fusion stalled at step %d"), Step)); return true; }
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			if (!World || World->GetNetMode() != NM_Client || Now < WaitUntil) { return false; }
			const AChaosImpactGameState* Room = World->GetGameState<AChaosImpactGameState>();
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Room || !Room->bOnlineRoom || !Own) { return false; }
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"), Name), true, false);
			};
			const FVector Ahead = Own->GetActorForwardVector().GetSafeNormal2D();
			switch (Step)
			{
			case 0:
			{
				// (Settled in the room's lobby first. The fused looks shown once here, on this screen only, so what they load and
				// build the first time - an editor build makes their shaders then - is not in the middle of the timed part.)
				const FTransform Spot(FRotator::ZeroRotator, Own->GetActorLocation() + FVector(0.0f, 0.0f, 300.0f));
				for (const EChaosImpactBallType Rider : {EChaosImpactBallType::Black, EChaosImpactBallType::Fire, EChaosImpactBallType::Thunder})
				{
					if (AChaosImpactBall* Warm = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Spot))
					{
						Warm->SetReplicates(false);
						Warm->SetBallType(EChaosImpactBallType::Drive);
						Warm->SetFusedType(Rider);
						Warm->FinishSpawning(Spot);
						Warm->SetLifeSpan(3.0f);
					}
				}
				WaitUntil = Now + 8.0;
				Step = 20;
				return false;
			}
			case 20:
				// (And one fused throw of its own first, not looked at: whatever it loads the first time is loaded then.)
				Own->ServerGiveBallsForTests(EChaosImpactBallType::Black, EChaosImpactBallType::Drive);
				WaitUntil = Now + 1.5;
				Step = 21;
				return false;
			case 21:
				Own->RequestBallFusion();
				WaitUntil = Now + 1.0;
				Step = 22;
				return false;
			case 22:
				Own->ThrowForTests(-Ahead, 0.3f);
				WaitUntil = Now + 14.0;
				Step = 1;
				return false;
			case 1:
				Test->TestTrue(TEXT("In the room's lobby"), Room->Phase == EChaosImpactOnlinePhase::Lobby && !Room->bVersusMatch);
				Own->ServerGiveBallsForTests(EChaosImpactBallType::Black, EChaosImpactBallType::Drive);
				WaitUntil = Now + 1.5;
				Step = 2;
				return false;
			case 2:
				Test->TestEqual(TEXT("The host put two in the guest's hands"), Own->GetCarriedBallCount(), 2);
				Test->TestTrue(TEXT("They can be joined in the room's lobby"), Own->CanFuseBalls());
				Own->RequestBallFusion();
				Test->TestTrue(TEXT("Joined on the guest's screen at once"), Own->AreBallsFused());
				WaitUntil = Now + 1.6;
				Step = 3;
				return false;
			case 3:
				Test->TestEqual(TEXT("The drive in front (as the host has it)"), Own->GetCarriedBallType(0), EChaosImpactBallType::Drive);
				Shot(TEXT("Online_Held.png"));
				Test->TestTrue(TEXT("The fused throw goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 0.8;
				StepAt = Now;
				Step = 4;
				return false;
			case 4:
			{
				// The host's ball, as this screen gets it: a drive with the black hole riding along, steered and drawing in.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Drive && Now - LoggedAt > 0.5)
					{
						UE_LOG(LogTemp, Display, TEXT("FUSIONONLINE drive here: role %d, rider %s, detonated %d, steered %d"),
							static_cast<int32>(It->GetLocalRole()), ChaosImpactBallTypes::GetInternalName(It->GetFusedType()),
							It->HasDetonated() ? 1 : 0, It->IsDragDrive() ? 1 : 0);
					}
					const bool bIt = It->GetLocalRole() == ROLE_SimulatedProxy && It->GetBallType() == EChaosImpactBallType::Drive
						&& It->GetFusedType() == EChaosImpactBallType::Black;
					bDriveSeen |= bIt;
					bDriveSteered |= bIt && It->IsDragDrive();
				}
				LoggedAt = Now - LoggedAt > 0.5 ? Now : LoggedAt;
				// (An editor build may stop for a while the first time to make an effect's shaders: it is waited out. Its
				// steering starts when the host's word of it arrives.)
				if (!bDriveSteered && Now - StepAt < 40.0) { return false; }
				Test->TestTrue(TEXT("Steered and drawing in on the guest's screen"), bDriveSteered);
				Test->TestTrue(TEXT("The host's fused drive reached the guest"), bDriveSeen);
				Test->TestEqual(TEXT("Both balls left the hands"), Own->GetCarriedBallCount(), 0);
				Shot(TEXT("Online_Drag.png"));
				Step = 5;
				StepAt = Now;
				return false;
			}
			case 5:
			{
				bool bBlack = false;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					bBlack |= It->GetZoneType() == EChaosImpactBallType::Black;
				}
				if (!bBlack && Now - StepAt < 30.0) { return false; }
				Test->TestTrue(TEXT("Its black hole opened on the guest's screen too"), bBlack);
				Shot(TEXT("Online_Hole.png"));
				WaitUntil = Now + 2.0;
				Step = 6;
				return false;
			}
			case 6:
				Own->ServerGiveBallsForTests(EChaosImpactBallType::Fire, EChaosImpactBallType::Wind);
				WaitUntil = Now + 1.2;
				Step = 7;
				return false;
			case 7:
				Own->RequestBallFusion();
				Test->TestEqual(TEXT("The wind in front"), Own->GetCarriedBallType(0), EChaosImpactBallType::Wind);
				WaitUntil = Now + 1.2;
				Step = 8;
				return false;
			case 8:
				Test->TestTrue(TEXT("The fused tornado goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 3.2;
				Step = 9;
				return false;
			case 9:
			{
				int32 Fires = 0;
				bool bTornado = false;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { Fires += It->GetZoneType() == EChaosImpactBallType::Fire ? 1 : 0; }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { bTornado = true; }
				UE_LOG(LogTemp, Display, TEXT("FUSIONONLINE tornado %d, fires %d"), bTornado ? 1 : 0, Fires);
				Test->TestTrue(TEXT("The fused tornado is on the guest's screen"), bTornado);
				Test->TestTrue(TEXT("Its line of fire too"), Fires >= 3);
				Shot(TEXT("Online_Tornado.png"));
				// 雷フック: the hook flies (the thunder riding), as fast on this screen as on the host's.
				Own->ServerGiveBallsForTests(EChaosImpactBallType::Thunder, EChaosImpactBallType::Hook);
				WaitUntil = Now + 1.5;
				Step = 10;
				return false;
			}
			case 10:
				Own->RequestBallFusion();
				Test->TestEqual(TEXT("The hook in front"), Own->GetCarriedBallType(0), EChaosImpactBallType::Hook);
				WaitUntil = Now + 1.2;
				Step = 11;
				return false;
			case 11:
				Test->TestTrue(TEXT("The thunder hook goes"), Own->ThrowForTests(Ahead, 0.3f));
				WaitUntil = Now + 0.35;
				Step = 12;
				return false;
			case 12:
			{
				bool bHook = false;
				for (TActorIterator<AChaosImpactHook> It(World); It; ++It)
				{
					bHook = true;
					UE_LOG(LogTemp, Display, TEXT("FUSIONONLINE hook: rider %s, speed %.0f, rebound at %.0f"),
						ChaosImpactBallTypes::GetInternalName(It->GetFusedType()), It->GetOutSpeed(), It->GetBounceDistance());
					Test->TestEqual(TEXT("The thunder rides the hook here too"), It->GetFusedType(), EChaosImpactBallType::Thunder);
					Test->TestTrue(TEXT("As fast here"), It->GetOutSpeed() > AChaosImpactHook::OutSpeed * 1.3f);
				}
				Test->TestTrue(TEXT("The host's hook reached the guest"), bHook);
				Shot(TEXT("Online_ThunderHook.png"));
				WaitUntil = Now + 1.0;
				Step = 13;
				return false;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		double StepAt = 0.0;
		double LoggedAt = 0.0;
		bool bDriveSeen = false;
		bool bDriveSteered = false;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionOnlineTest, "ChaosImpact.Fusion.Online",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactFusionOnlineTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionOnlineCommand(this));
	return true;
}
#endif
