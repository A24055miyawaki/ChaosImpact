#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
/** Run on a real listen server (-CIAutoVersus=0:1:15) AND a connected client, never fake net roles. */
class FOnlinePrisonCommand : public IAutomationLatentCommand
{
public:
	explicit FOnlinePrisonCommand(FAutomationTestBase* T):Test(T) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds()-Started>140) { Test->AddError(FString::Printf(TEXT("Online prison timeout at %d"),Step)); return true; }
		UWorld* W=nullptr;
		for (const auto& C:GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if (!W || W->GetNetMode()==NM_Standalone) { return false; }
		const double Now=W->GetTimeSeconds();
		if (Now<Next) { return false; }
		if (W->GetNetMode()==NM_ListenServer && Step==6) { return true; }
		auto* Match=W->GetGameState<AChaosImpactGameState>();
		if (!Match || !Match->bVersusMatch || Match->Phase!=EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked()) { return false; }
		AChaosImpactCharacter* Host=nullptr; AChaosImpactCharacter* Remote=nullptr; AChaosImpactCharacter* Enemy=nullptr;
		for (TActorIterator<AChaosImpactCharacter> It(W);It;++It)
		{
			auto* PS=It->GetPlayerState<AChaosImpactPlayerState>(); if (!PS) { continue; }
			if (PS->IsABot()) { Enemy=*It; if (It->GetController()) { It->GetController()->SetActorTickEnabled(false); } }
			else if (PS->bRoomHost) { Host=*It; } else { Remote=*It; }
		}
		if (!Host || !Remote || !Enemy) { return false; }
		if (Now>LogAt) { UE_LOG(LogTemp,Display,TEXT("PRISONNET mode=%d step=%d hostForm=%d remoteForm=%d stock=%d/%d hp=%.0f/%.0f/%.0f"),
			static_cast<int32>(W->GetNetMode()),Step,Host->GetBallForm()->IsCaptured(),Remote->GetBallForm()->IsCaptured(),
			Host->GetCarriedBallCount(),Remote->GetCarriedBallCount(),Host->GetHealth(),Remote->GetHealth(),Enemy->GetHealth()); LogAt=Now+3; }
		if (W->GetNetMode()==NM_ListenServer)
		{
			switch (Step)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
				Origin=FVector(Match->StageCenter.X-400,Match->StageCenter.Y-300,Host->GetActorLocation().Z);
				Host->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
				Remote->ResetForOnlineMatch(Origin+FVector(550,0,0),FRotator::ZeroRotator);
				Enemy->ResetForOnlineMatch(Origin+FVector(900,200,0),FRotator::ZeroRotator);
				FActorSpawnParameters P; P.Owner=Host; P.Instigator=Host;
				auto* Ball=W->SpawnActor<AChaosImpactBall>(Remote->GetActorLocation()-FVector(200,0,0),FRotator::ZeroRotator,P);
				Ball->SetBallType(EChaosImpactBallType::Prison); Ball->Launch(FVector::ForwardVector,950,EChaosImpactBallFlightMode::Straight,0);
				++Step; break;
			}
			case 1:
				if (!Remote->GetBallForm()->IsCaptured()) { return false; }
				Test->TestEqual(TEXT("Remote-reported prison hit deals no initial HP"),Remote->GetHealth(),3.f);
				Shell=Remote->GetBallForm()->GetFormBall(); Next=Now+.8; ++Step; break;
			case 2:
				if (!Shell.IsValid() || !Shell->IsPickupAvailable()) { return false; }
				Test->TestTrue(TEXT("Host can carry remote captive"),Shell->ConsumePickup(Host));
				++Step; Next=Now+Shell->GetCaptureSecondsLeft()+.3; break;
			case 3:
				Test->TestFalse(TEXT("Server timeout frees remote captive"),Remote->IsInBallForm());
				Test->TestEqual(TEXT("Server clears host stock after timeout"),Host->GetCarriedBallCount(),0);
				Test->TestTrue(TEXT("FFA also permits capture of host"),Host->GetBallForm()->Capture(Remote));
				Shell=Host->GetBallForm()->GetFormBall();
				Remote->ResetForOnlineMatch(Origin+FVector(80,0,0),FRotator::ZeroRotator);
				Next=Now+.8; ++Step; break;
			case 4:
				if (!Shell.IsValid()) { Test->AddError(TEXT("Host shell vanished before throw")); return true; }
				if (!Shell->IsLaunched()) { return false; }
				Test->TestTrue(TEXT("Remote throw request launches real held captive"),Shell->GetThrowingPawn()==Remote);
				Test->TestEqual(TEXT("Remote throw removes exactly one inventory slot"),Remote->GetCarriedBallCount(),0);
				Shell->SetActorLocation(Enemy->GetActorLocation()-FVector(160,0,0));
				Shell->Launch(FVector::ForwardVector,1200,EChaosImpactBallFlightMode::Straight,0);
				++Step; Next=Now+.4; break;
			case 5:
				Test->TestFalse(TEXT("Thrown host is released"),Host->IsInBallForm());
				Test->TestEqual(TEXT("Host captive loses one HP"),Host->GetHealth(),2.f);
				Test->TestEqual(TEXT("Target loses one HP"),Enemy->GetHealth(),2.f);
				Test->TestEqual(TEXT("Actual remote thrower earns both hit points"),Remote->GetPlayerState<AChaosImpactPlayerState>()->Points,2);
				Test->TestEqual(TEXT("Captive is not credited for being thrown"),Host->GetPlayerState<AChaosImpactPlayerState>()->Points,0);
				++Step; Next=Now+3; break;
			}
		}
		else
		{
			switch (Step)
			{
			case 0:
				if (!Remote->GetBallForm()->IsCaptured()) { return false; }
				Shell=Remote->GetBallForm()->GetFormBall();
				if (!Shell.IsValid() || Shell->GetCaptureSecondsLeft()<=0) { return false; }
				Test->TestFalse(TEXT("Owning client cannot escape by toggling"),Remote->GetBallForm()->Toggle());
				Test->TestTrue(TEXT("Absolute countdown replicates"),Shell->GetCaptureSecondsLeft()<=5);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("PrisonBallQA/Online-Captured.png")),true,false);
				++Step; break;
			case 1:
				if (!Shell.IsValid() || Shell->GetCarrier()!=Host) { return false; }
				Test->TestTrue(TEXT("Host carried slot replicates to passenger"),Host->GetBallForm()->GetHeldBall(0)==Shell.Get());
				++Step; break;
			case 2:
				if (Remote->IsInBallForm() || Host->GetCarriedBallCount()!=0) { return false; }
				Test->TestEqual(TEXT("Client timeout leaves HP intact"),Remote->GetHealth(),3.f);
				++Step; break;
			case 3:
				if (!Host->GetBallForm()->IsCaptured()) { return false; }
				Shell=Host->GetBallForm()->GetFormBall();
				if (!Shell.IsValid() || Shell->GetCarrier()!=Remote || Remote->GetCarriedBallCount()!=1) { return false; }
				Remote->SetAIAimDirection(FVector::ForwardVector); Remote->BeginThrowInput();
				++Step; Next=Now+.15; break;
			case 4:
				Remote->EndThrowInput(); ++Step; break;
			case 5:
				if (Host->IsInBallForm() || Host->GetHealth()!=2 || Enemy->GetHealth()!=2) { return false; }
				Test->TestEqual(TEXT("Client sees corrected empty inventory"),Remote->GetCarriedBallCount(),0);
				Test->TestEqual(TEXT("Client sees both points awarded to actual thrower"),Remote->GetPlayerState<AChaosImpactPlayerState>()->Points,2);
				return true;
			}
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	int32 Step=0;
	double Started=FPlatformTime::Seconds(),Next=0,LogAt=0;
	FVector Origin;
	TWeakObjectPtr<AChaosImpactBuddyBall> Shell;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlinePrisonTest,"ChaosImpact.PrisonBall.Online",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FOnlinePrisonTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FOnlinePrisonCommand(this)); return true; }
#endif
