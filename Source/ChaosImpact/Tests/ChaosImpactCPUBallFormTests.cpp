#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
class FCPUBallFormCommand : public IAutomationLatentCommand
{
public:
    explicit FCPUBallFormCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>100) { Test->AddError(FString::Printf(TEXT("CPU ball form timeout %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        {
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CICPUCount=2?CITargets=0?CILocalPlayers=1"));
            Stage=1; return false;
        }
        auto* PC=Cast<AChaosImpactPlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* P=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
        if(!P || !PC->IsGameplayActive()) { return false; }
        const float Now=W->GetTimeSeconds();
        if(Now<Next) { return false; }
        auto CleanBalls=[&]()
        {
            for(TActorIterator<AChaosImpactBall> It(W);It;++It) { if(!Cast<AChaosImpactBuddyBall>(*It)) { It->Destroy(); } }
        };
        auto Give=[&]()
        {
            auto* B=W->SpawnActor<AChaosImpactBall>(P->GetActorLocation()+FVector(0,900,0),FRotator::ZeroRotator);
            B->MakePickup(); Test->TestTrue(TEXT("Give full-hand test ball"),B->ConsumePickup(P));
        };
        switch(Stage)
        {
        case 1:
        {
            if(Now<2 || !P->GetCharacterMovement()->IsMovingOnGround()) { return false; }
            TArray<AChaosImpactCPUController*> CPUs;
            for(TActorIterator<AChaosImpactCPUController> It(W);It;++It) { if(It->GetPawn()) { CPUs.Add(*It); } }
            if(CPUs.Num()!=2) { return false; }
            CPU=CPUs[0]; Ally=Cast<AChaosImpactCharacter>(CPU->GetPawn());
            Enemy=Cast<AChaosImpactCharacter>(CPUs[1]->GetPawn()); CPUs[1]->SetActorTickEnabled(false);
            CPU->SetDifficulty(ChaosImpactMatch::CPULevelStrong);
            for(TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
            CleanBalls(); Origin=P->GetActorLocation();
            P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Ally->ResetForOnlineMatch(Origin+FVector(280,0,0),FRotator::ZeroRotator);
            Enemy->ResetForOnlineMatch(Origin+FVector(1200,0,0),FRotator::ZeroRotator);
            P->SoloTeam=Ally->SoloTeam=Enemy->SoloTeam=-1;
            Test->TestTrue(TEXT("Training supports teammate ball forms"),P->GetBallForm()->IsAvailable());
            Stage=2; Next=Now+.8f; return false;
        }
        case 2:
            Test->TestFalse(TEXT("CPU never curls for an enemy in individual training"),Ally->IsInBallForm());
            P->SoloTeam=Ally->SoloTeam=0; Enemy->SoloTeam=1;
            P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Ally->ResetForOnlineMatch(Origin+FVector(220,0,0),FRotator::ZeroRotator);
            Give(); Give();
            Stage=3; Next=Now+.8f; return false;
        case 3:
            Test->TestFalse(TEXT("CPU does not curl for a teammate with full hands"),Ally->IsInBallForm());
            P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator); CleanBalls();
            Ally->ResetForOnlineMatch(Origin+FVector(280,0,0),FRotator::ZeroRotator);
            Stage=4; Deadline=Now+5.f; Next=Now; return false;
        case 4:
        {
            auto* Buddy=P->GetBallForm()->GetHeldBall(0);
            if(!Buddy && Now<Deadline) { return false; }
            if(!Test->TestNotNull(TEXT("CPU approaches, curls and is picked up by its teammate"),Buddy)) { return true; }
            Test->TestTrue(TEXT("The passenger is the CPU, not a replacement projectile"),Buddy->GetPassenger()==Ally.Get());
            Test->TestTrue(TEXT("CPU retains its own controller while held"),Ally->GetController()==CPU.Get());
            Test->TestEqual(TEXT("One ball slot is used"),P->GetCarriedBallCount(),1);
            Test->TestEqual(TEXT("CPU cannot perform normal movement while curled"),static_cast<int32>(Ally->GetCharacterMovement()->MovementMode),static_cast<int32>(MOVE_None));
            P->SetAIAimDirection(FVector::ForwardVector); P->BeginThrowInput();
            Stage=5; Next=Now+.3f; return false;
        }
        case 5:
            P->EndThrowInput();
            Stage=6; Next=Now+.4f; return false;
        case 6:
            Test->TestEqual(TEXT("Human can throw the CPU using ordinary throwing"),P->GetCarriedBallCount(),0);
            Stage=7; Deadline=Now+5.f; Next=Now; return false;
        case 7:
            if(Ally->IsInBallForm() && Now<Deadline) { return false; }
            Test->TestFalse(TEXT("Thrown CPU uncurls automatically and is never left stuck"),Ally->IsInBallForm());
            Test->TestTrue(TEXT("Normal CPU movement is restored"),Ally->GetCharacterMovement()->MovementMode!=MOVE_None);
            Test->TestTrue(TEXT("CPU returns with its original controller"),Ally->GetController()==CPU.Get());
            // Externally request a form, without a waiting teammate, to exercise timeout recovery too.
            Ally->ResetForOnlineMatch(Origin+FVector(500,0,0),FRotator::ZeroRotator);
            Stage=8; Next=Now+.6f; return false;
        case 8:
            Test->TestTrue(TEXT("CPU can still use the common ball-form action"),Ally->GetBallForm()->Toggle());
            Stage=9; Next=Now+3.5f; return false;
        case 9:
            Test->TestFalse(TEXT("Uncollected CPU does not wait forever as a ball"),Ally->IsInBallForm());
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds();
    int32 Stage=0;
    float Next=0,Deadline=0;
    FVector Origin;
    TWeakObjectPtr<AChaosImpactCPUController> CPU;
    TWeakObjectPtr<AChaosImpactCharacter> Ally,Enemy;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPUBallFormTest,"ChaosImpact.Training.CPUBallForm",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCPUBallFormTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FCPUBallFormCommand(this)); return true; }
#endif
