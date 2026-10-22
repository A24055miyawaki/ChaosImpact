#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

namespace
{
// Run on BOTH a real listen server and a connected client. No simulated net roles/worlds.
// Host: ?listen?CITraining=1?CITargets=0?CICPUCount=0 -CIAutoVersus=2:1:15
//       -ExecCmds="Automation RunTests ChaosImpact.Online.BuddyBall"
// Client: 127.0.0.1 -ExecCmds="Automation RunTests ChaosImpact.Online.BuddyBall"
class FOnlineBuddyCommand : public IAutomationLatentCommand
{
public:
    explicit FOnlineBuddyCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150)
        { Test->AddError(FString::Printf(TEXT("Online buddy timeout, stage %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W || W->GetNetMode()==NM_Standalone) { return false; }
        // The client exits when its assertions finish; completing the host must not require it to remain joined.
        if(W->GetNetMode()==NM_ListenServer && Stage==16)
        {
            if(W->GetTimeSeconds()<Next) { return false; }
            UE_LOG(LogTemp,Display,TEXT("ONLINEBUDDY HOST complete")); return true;
        }
        auto* Match=W->GetGameState<AChaosImpactGameState>();
        if(!Match || !Match->bVersusMatch || Match->Phase!=EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked()) { return false; }
        AChaosImpactCharacter* Host=nullptr; AChaosImpactCharacter* Remote=nullptr; AChaosImpactCharacter* Enemy=nullptr;
        for(TActorIterator<AChaosImpactCharacter> It(W);It;++It)
        {
            auto* PS=It->GetPlayerState<AChaosImpactPlayerState>(); if(!PS) { continue; }
            if(PS->IsABot()) { Enemy=*It; if(It->GetController()) { It->GetController()->SetActorTickEnabled(false); } }
            else if(PS->bRoomHost) { Host=*It; } else { Remote=*It; }
        }
        if(!Host || !Remote || !Enemy) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        if(Now>=NextLog)
        {
            const auto* HPC=Cast<AChaosImpactPlayerController>(Host->GetController());
            UE_LOG(LogTemp,Display,TEXT("ONLINEBUDDY mode=%d stage=%d host balls=%d charging=%d playing=%d remote form=%d host form=%d"),
                static_cast<int32>(W->GetNetMode()),Stage,Host->GetCarriedBallCount(),Host->IsChargingThrow(),
                HPC && HPC->IsGameplayActive(),Remote->IsInBallForm(),Host->IsInBallForm());
            NextLog=Now+4;
        }
        if(W->GetNetMode()==NM_ListenServer)
        {
            switch(Stage)
            {
            case 0:
                for(TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
                Origin=FVector(Match->StageCenter.X-600,Match->StageCenter.Y-600,Host->GetActorLocation().Z);
                Host->GetPlayerState<AChaosImpactPlayerState>()->TeamIndex=0;
                Remote->GetPlayerState<AChaosImpactPlayerState>()->TeamIndex=0;
                Enemy->GetPlayerState<AChaosImpactPlayerState>()->TeamIndex=1;
                Host->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(Origin+FVector(0,600,0),FRotator::ZeroRotator);
                Enemy->ResetForOnlineMatch(Origin+FVector(0,-500,0),FRotator::ZeroRotator);
                Host->TakeDamage(1,FDamageEvent(),Enemy->GetController(),Enemy);
                Remote->TakeDamage(2,FDamageEvent(),Enemy->GetController(),Enemy);
                Stage=1; break;
            case 1:
                if(!Remote->IsInBallForm()) { return false; }
                Test->TestTrue(TEXT("Client toggle RPC created a server passenger ball"),Remote->GetBallForm()->GetFormBall()->GetPassenger()==Remote);
                Buddy=Remote->GetBallForm()->GetFormBall();
                Test->TestTrue(TEXT("Host picks up the remote teammate"),Buddy->ConsumePickup(Host));
                Stage=2; Next=Now+1.5; break;
            case 2:
                Host->SetAIAimDirection(FVector::ForwardVector); Host->BeginThrowInput();
                Test->TestTrue(TEXT("Host starts a charged buddy throw"),Host->IsChargingThrow());
                Stage=3; Next=Now+.6; break;
            case 3:
                Host->EndThrowInput(); Stage=4; Next=Now+.4; break;
            case 4:
                Test->TestTrue(TEXT("Host launches the same replicated passenger"),Buddy.IsValid() && Buddy->IsLaunched());
                Stage=5; break;
            case 5:
                if(Remote->IsInBallForm()) { return false; }
                Test->TestTrue(TEXT("Remote uncurl restores collision"),Remote->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
                Stage=6; Next=Now+1; break;
            case 6:
                // Reverse roles: client now carries and throws the host through the ordinary input/RPC path.
                Host->ResetForOnlineMatch(Origin+FVector(0,700,0),FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(Origin+FVector(0,600,0),FRotator::ZeroRotator);
                Stage=7; Next=Now+.6; break;
            case 7:
                Test->TestTrue(TEXT("Host can curl too"),Host->GetBallForm()->Toggle());
                Buddy=Host->GetBallForm()->GetFormBall(); Stage=8; break;
            case 8:
                if(!Buddy.IsValid() || Buddy->GetCarrier()!=Remote) { return false; }
                Test->TestEqual(TEXT("Server adds exactly one buddy slot to remote player"),Remote->GetCarriedBallCount(),1);
                Stage=9; break;
            case 9:
                if(!Buddy.IsValid() || !Buddy->IsLaunched()) { return false; }
                Test->TestEqual(TEXT("Remote throw consumes its server slot"),Remote->GetCarriedBallCount(),0);
                Stage=10; break;
            case 10:
                if(Host->IsInBallForm()) { return false; }
                Test->TestTrue(TEXT("Host uncurls after client throw"),Host->GetCharacterMovement()->MovementMode!=MOVE_None);
                Stage=11; break;
            case 11:
                if(!Remote->IsInBallForm()) { return false; }
                Buddy=Remote->GetBallForm()->GetFormBall();
                OtherBallsBeforePickup=Host->GetCarriedBallCount();
                Test->TestTrue(TEXT("Host collects a third transform"),Buddy->ConsumePickup(Host));
                Stage=12; break;
            case 12:
                if(Remote->IsInBallForm()) { return false; }
                Test->TestEqual(TEXT("Owner uncurl removes only the buddy, retaining any ordinary ball"),Host->GetCarriedBallCount(),OtherBallsBeforePickup);
                Test->TestTrue(TEXT("No passenger reference left in either host hand"),!Host->GetBallForm()->GetHeldBall(0) && !Host->GetBallForm()->GetHeldBall(1));
                // Final cleanup case: the held passenger is KO'd, not thrown.
                Host->ResetForOnlineMatch(Origin+FVector(0,700,0),FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(Origin+FVector(0,600,0),FRotator::ZeroRotator);
                Stage=13; Next=Now+.6; break;
            case 13:
                Test->TestTrue(TEXT("Host curls before passenger KO"),Host->GetBallForm()->Toggle());
                Buddy=Host->GetBallForm()->GetFormBall(); Stage=14; break;
            case 14:
                if(!Buddy.IsValid() || Buddy->GetCarrier()!=Remote) { return false; }
                Stage=15; Next=Now+1; break;
            case 15:
                Host->TakeDamage(3,FDamageEvent(),Enemy->GetController(),Enemy);
                Test->TestTrue(TEXT("Passenger damage produces KO, not an immortal ball"),Host->IsEliminated() && !Host->IsInBallForm());
                Test->TestEqual(TEXT("KO removes passenger from the server carrier"),Remote->GetCarriedBallCount(),0);
                Stage=16; Next=Now+2; break;
            }
        }
        else
        {
            auto* PC=Cast<AChaosImpactPlayerController>(Remote->GetController());
            UChaosImpactChargeWidget* HUD=nullptr;
            for(TObjectIterator<UChaosImpactChargeWidget> It;It;++It)
            { if(It->GetWorld()==W && It->GetOwningPlayer()==PC && It->IsInViewport()) { HUD=*It; break; } }
            ChaosImpactCombatHealth::FPresentation Health;
            switch(Stage)
            {
            case 0:
                if(!HUD || !PC->IsGameplayActive() || Host->GetHealth()!=2 || Remote->GetHealth()!=1
                    || !AChaosImpactGameState::AreTeammates(W,Host,Remote)) { return false; }
                Test->TestTrue(TEXT("Online remote HP changes show an opponent-style gauge"),HUD->GetCombatHealthPresentation(Host,Health) && Health.Remaining==2);
                Test->TestFalse(TEXT("Online own overhead gauge stays hidden"),HUD->GetCombatHealthPresentation(Remote,Health));
                Test->TestTrue(TEXT("Online own last HP warning remains"),HUD->IsLastHealthWarningActive());
                Test->TestTrue(TEXT("Client can request ball form"),Remote->GetBallForm()->Toggle()); Stage=1; break;
            case 1:
                if(!Remote->IsInBallForm()) { return false; }
                Buddy=Remote->GetBallForm()->GetFormBall();
                Test->TestTrue(TEXT("Passenger reference replicates"),Buddy->GetPassenger()==Remote);
                Stage=2; break;
            case 2:
                if(!Buddy.IsValid() || Buddy->GetCarrier()!=Host || Host->GetBallForm()->GetHeldBall(0)!=Buddy.Get()) { return false; }
                Test->TestFalse(TEXT("Held buddy is visible, not hidden behind a normal-ball preview"),Buddy->IsHidden());
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BuddyBallQA/Online-Held.png")),true,false);
                Stage=3; break;
            case 3:
                if(!Buddy.IsValid() || !Buddy->IsLaunched()) { return false; }
                Test->TestTrue(TEXT("Client sees flight velocity"),Buddy->GetBallVelocity().Size()>100);
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BuddyBallQA/Online-Flight.png")),true,false);
                Stage=4; break;
            case 4:
                if(Remote->IsInBallForm()) { Remote->GetBallForm()->Steer(FVector(1,.5f,0)); return false; }
                Test->TestTrue(TEXT("Client restores its normal collision after flight"),Remote->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
                Stage=5; break;
            case 5:
                if(!Host->IsInBallForm() || !Remote->GetBallForm()->GetHeldBall(0) || Remote->GetCarriedBallCount()!=1) { return false; }
                Buddy=Remote->GetBallForm()->GetHeldBall(0);
                Test->TestTrue(TEXT("Client carries the host's actual body"),Buddy->GetPassenger()==Host && Buddy->GetCarrier()==Remote);
                Remote->SetAIAimDirection(FVector::ForwardVector); Remote->BeginThrowInput();
                Stage=6; Next=Now+.6; break;
            case 6:
                Remote->EndThrowInput(); Stage=7; break;
            case 7:
                if(!Buddy.IsValid() || !Buddy->IsLaunched()) { return false; }
                Test->TestFalse(TEXT("Remote throw doesn't destroy its replicated teammate"),Buddy->IsActorBeingDestroyed());
                Stage=8; break;
            case 8:
                if(Host->IsInBallForm()) { return false; }
                Test->TestEqual(TEXT("Client inventory resolves without a duplicate normal ball"),Remote->GetCarriedBallCount(),0);
                Test->TestTrue(TEXT("Client requests a third curl"),Remote->GetBallForm()->Toggle());
                Stage=9; break;
            case 9:
                if(!Remote->IsInBallForm() || !Remote->GetBallForm()->GetFormBall()->GetCarrier()) { return false; }
                Test->TestTrue(TEXT("Client uncurls while held via its owned component RPC"),Remote->GetBallForm()->Toggle());
                Stage=10; break;
            case 10:
                if(Remote->IsInBallForm() || Host->GetBallForm()->GetHeldBall(0) || Host->GetBallForm()->GetHeldBall(1)) { return false; }
                Test->TestTrue(TEXT("Held uncurl clears both machines' slot references"),!Host->GetBallForm()->GetHeldBall(0));
                Stage=11; break;
            case 11:
                if(!Host->IsEliminated()) { return false; }
                if(Remote->GetBallForm()->GetHeldBall(0) || Remote->GetCarriedBallCount()!=0) { return false; }
                Test->TestFalse(TEXT("KO passenger is no longer a ball on the client"),Host->IsInBallForm());
                UE_LOG(LogTemp,Display,TEXT("ONLINEBUDDY CLIENT complete")); return true;
            }
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0,NextLog=0;
    int32 Stage=0;
    int32 OtherBallsBeforePickup=0;
    FVector Origin=FVector::ZeroVector;
    TWeakObjectPtr<AChaosImpactBuddyBall> Buddy;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineBuddyTest,"ChaosImpact.Online.BuddyBall",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FOnlineBuddyTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FOnlineBuddyCommand(this)); return true; }
#endif
