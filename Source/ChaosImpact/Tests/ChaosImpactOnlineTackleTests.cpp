#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
// Run on a real listen server and a real client. Host: ?listen?CITraining=1 -CIAutoVersus=0:0:15.
class FOnlineTackleCommand : public IAutomationLatentCommand
{
public:
    explicit FOnlineTackleCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Start>120) { Test->AddError(FString::Printf(TEXT("Online tackle timeout %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World()&&C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W || W->GetNetMode()==NM_Standalone) { return false; }
        if(W->GetNetMode()==NM_ListenServer && Stage==5) { return W->GetTimeSeconds()>=Next; }
        auto* Match=W->GetGameState<AChaosImpactGameState>();
        if(!Match || !Match->bVersusMatch || Match->Phase!=EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked()) { return false; }
        AChaosImpactCharacter* Host=nullptr; AChaosImpactCharacter* Remote=nullptr;
        for(TActorIterator<AChaosImpactCharacter> It(W);It;++It)
        { auto* PS=It->GetPlayerState<AChaosImpactPlayerState>(); if(PS && !PS->IsABot()) { if(PS->bRoomHost) { Host=*It; } else { Remote=*It; } } }
        if(!Host || !Remote) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        if(Now>=NextLog)
        {
            UE_LOG(LogTemp,Display,TEXT("TACKLENET mode=%d step=%d hostballs=%d remoteBalls=%d hostcharge=%d remotecharge=%d remoteBusy=%d host=%s remote=%s vel=%s ground=%d stamina=%.1f"),
                static_cast<int32>(W->GetNetMode()),Stage,Host->GetCarriedBallCount(),Remote->GetCarriedBallCount(),Host->IsChargingThrow(),Remote->IsChargingThrow(),Remote->GetTackle()->IsBusy(),
                *Host->GetActorLocation().ToCompactString(),*Remote->GetActorLocation().ToCompactString(),*Remote->GetVelocity().ToCompactString(),Remote->GetCharacterMovement()->IsMovingOnGround(),Remote->GetStamina());
            NextLog=Now+2;
        }
        auto Give=[&](AChaosImpactCharacter* To)
        { auto* B=W->SpawnActor<AChaosImpactBall>(To->GetActorLocation()+FVector(0,1500,0),FRotator::ZeroRotator); B->MakePickup(); Test->TestTrue(TEXT("Server supplies test ball"),B->ConsumePickup(To)); };
        if(W->GetNetMode()==NM_ListenServer)
        {
            switch(Stage)
            {
            case 0:
                // The centre offset can be inside a stage prop; start on the real, known-clear spawn floor.
                Origin=Host->GetActorLocation();
                Host->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator(0,180,0));
                Give(Host); Give(Host); Host->BeginThrowInput(); Stage=1; break;
            case 1:
                if(Host->GetCarriedBallCount()!=1 || Host->IsChargingThrow()) { return false; }
                Test->TestEqual(TEXT("Remote tackle doesn't hurt the host"),Host->GetHealth(),3.f);
                Stage=2; Next=Now+1.5; break;
            case 2:
                Remote->ResetForOnlineMatch(Origin+FVector(400,0,0),FRotator::ZeroRotator);
                Host->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
                Give(Remote); Give(Remote);
                // Fixture-only grant: normal gameplay grants these through the owner's pickup claim RPC.
                { struct { int32 Count; uint8 Types; } Args{Remote->GetCarriedBallCount(),Remote->GetCarriedBallTypesPacked()};
                  Remote->ProcessEvent(Remote->FindFunctionChecked(TEXT("ClientBallCountReset")),&Args); }
                Stage=3; break;
            case 3:
                if(!Remote->IsChargingThrow()) { return false; }
                Host->SetAIAimDirection(FVector::ForwardVector);
                Test->TestTrue(TEXT("Host tackles remote victim"),Host->GetTackle()->Request()); Stage=4; break;
            case 4:
                if(Remote->GetCarriedBallCount()!=1 || Remote->IsChargingThrow()) { return false; }
                Test->TestEqual(TEXT("Host tackle doesn't hurt remote player"),Remote->GetHealth(),3.f);
                Stage=5; Next=Now+3; break;
            }
        }
        else
        {
            switch(Stage)
            {
            case 0:
                if(Host->GetCarriedBallCount()!=2 || !Remote->GetTackle()->CanStart()) { return false; }
                Remote->SetAIAimDirection(-FVector::ForwardVector);
                Test->TestTrue(TEXT("Remote input requests an authoritative tackle"),Remote->GetTackle()->Request());
                Stage=1; break;
            case 1:
                if(Host->GetCarriedBallCount()!=1) { return false; }
                Test->TestEqual(TEXT("Remote sees exactly one host ball dropped"),Host->GetCarriedBallCount(),1);
                Stage=2; break;
            case 2:
                if(Remote->GetCarriedBallCount()!=2 || Remote->GetTackle()->IsBusy()) { return false; }
                Remote->BeginThrowInput(); Test->TestTrue(TEXT("Remote victim charges on its own screen"),Remote->IsChargingThrow());
                Stage=3; break;
            case 3:
                if(Remote->GetCarriedBallCount()!=1 || Remote->IsChargingThrow()) { return false; }
                if(Remote->GetTackle()->GetStunDeadline()<=0) { return false; }
                // A shader/asset hitch can consume the entire 0.28 s before this assertion frame.
                Test->TestTrue(TEXT("Owning client receives the authoritative stun deadline"),Remote->GetTackle()->GetStunDeadline()>0);
                Test->TestEqual(TEXT("Disarm doesn't reduce HP"),Remote->GetHealth(),3.f);
                return true;
            }
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Start=FPlatformTime::Seconds(),Next=0,NextLog=0;
    int32 Stage=0;
    FVector Origin=FVector::ZeroVector;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineTackleTest,"ChaosImpact.Online.Tackle",
 EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FOnlineTackleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FOnlineTackleCommand(this)); return true; }
#endif
