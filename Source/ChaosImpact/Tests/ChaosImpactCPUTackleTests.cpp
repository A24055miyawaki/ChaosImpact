#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
class FCPUTackleCommand : public IAutomationLatentCommand
{
public:
    explicit FCPUTackleCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>90) { Test->AddError(FString::Printf(TEXT("CPU tackle timeout stage %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        {
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CICPUCount=1?CITargets=0?CILocalPlayers=1"));
            Stage=1; return false;
        }
        auto* PC=Cast<AChaosImpactPlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* Player=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
        AChaosImpactCPUController* CPU=nullptr;
        for(TActorIterator<AChaosImpactCPUController> It(W);It;++It) { if(It->GetPawn()) { CPU=*It; break; } }
        auto* Bot=CPU?Cast<AChaosImpactCharacter>(CPU->GetPawn()):nullptr;
        if(!Player || !Bot || !PC->IsGameplayActive()) { return false; }
        const float Now=W->GetTimeSeconds();
        if(Stage>=2 && Stage<=6 && Bot->GetTackle()->IsActive()) { bForbiddenRush=true; }
        if(Stage==7 && Bot->GetTackle()->IsActive()) { bSawRush=true; }
        if(Stage>=8 && Now>HitAt+.25f && Bot->GetTackle()->IsActive()) { bRepeatedRush=true; }
        if(Now<Next) { return false; }
        auto Give=[&]()
        {
            auto* Ball=W->SpawnActor<AChaosImpactBall>(Player->GetActorLocation()+FVector(0,900,0),FRotator::ZeroRotator);
            Ball->MakePickup(); Test->TestTrue(TEXT("Player gets a held ball"),Ball->ConsumePickup(Player));
        };
        switch(Stage)
        {
        case 1:
            if(!Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
            Origin=Player->GetActorLocation();
            Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Bot->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
            CPU->SetDifficulty(ChaosImpactMatch::CPULevelStrong);
            Bot->GetCharacterMovement()->MaxWalkSpeed=0; // Keep the opportunity nearby; actual launches still move it.
            Bot->SetTrainingMenuFrozen(true);
            Player->SoloTeam=Bot->SoloTeam=-1;
            Give(); Give(); Player->BeginThrowInput();
            Stage=2; Next=Now+1.f; return false;
        case 2:
            Test->TestFalse(TEXT("Frozen CPU never tackles"),bForbiddenRush);
            Bot->SetTrainingMenuFrozen(false);
            Player->SoloTeam=Bot->SoloTeam=0;
            Stage=3; Next=Now+.65f; return false;
        case 3:
        {
            Test->TestFalse(TEXT("CPU never tackles a teammate"),bForbiddenRush);
            Player->SoloTeam=Bot->SoloTeam=-1;
            auto* Wall=W->SpawnActor<AStaticMeshActor>(Origin+FVector(90,0,0),FRotator::ZeroRotator);
            auto* Mesh=Wall->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetWorldScale3D(FVector(.20f,4,3)); Mesh->SetCollisionProfileName(TEXT("BlockAll")); Barrier=Wall;
            Stage=4; Next=Now+.65f; return false;
        }
        case 4:
            Test->TestFalse(TEXT("CPU does not tackle through a wall"),bForbiddenRush);
            if(Barrier.IsValid()) { Barrier->Destroy(); }
            Bot->GetTackle()->StaminaCost=100.f;
            Stage=5; Next=Now+.65f; return false;
        case 5:
            Test->TestFalse(TEXT("CPU cannot tackle without enough stamina"),bForbiddenRush);
            Bot->GetTackle()->StaminaCost=1.f;
            // Re-establish a clean, grounded close-range opportunity after the guard checks.
            Bot->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
            Bot->GetCharacterMovement()->MaxWalkSpeed=Player->GetCharacterMovement()->MaxWalkSpeed;
            Bot->SetAIAimDirection(FVector::ForwardVector); // Initially looks away from the player.
            Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Give(); Give(); Player->BeginThrowInput();
            Stage=6; Next=Now+.02f; return false;
        case 6:
            Test->TestFalse(TEXT("CPU has a reaction delay, not instant input reading"),bForbiddenRush);
            Stage=7; Next=Now; HitDeadline=Now+3.f; return false;
        case 7:
            if(Player->GetCarriedBallCount()==2 && Now<HitDeadline) { return false; }
            Test->TestTrue(TEXT("CPU autonomously starts the real tackle"),bSawRush);
            Test->TestEqual(TEXT("CPU tackle drops exactly one player ball"),Player->GetCarriedBallCount(),1);
            Test->TestFalse(TEXT("CPU tackle interrupts player charging"),Player->IsChargingThrow());
            Test->TestEqual(TEXT("CPU tackle has no HP damage"),Player->GetHealth(),3.f);
            Test->TestTrue(TEXT("CPU pays stamina for its tackle"),Bot->GetStamina()<4.2f);
            HitAt=Now; Stage=8; Next=Now+.55f; return false;
        case 8:
            // Bring the target back into reach, but do not reset the controller's tactical cooldown.
            Bot->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
            Bot->GetCharacterMovement()->MaxWalkSpeed=0;
            Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Give(); Give();
            Stage=9; Next=Now+.8f; return false;
        case 9:
            Test->TestFalse(TEXT("CPU does not spam tackles even with another target in reach"),bRepeatedRush);
            Bot->GetCharacterMovement()->MaxWalkSpeed=Player->GetCharacterMovement()->MaxWalkSpeed;
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    int32 Stage=0;
    double Started=FPlatformTime::Seconds();
    float Next=0,HitDeadline=0,HitAt=0;
    FVector Origin;
    TWeakObjectPtr<AStaticMeshActor> Barrier;
    bool bForbiddenRush=false,bSawRush=false,bRepeatedRush=false;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPUTackleTest,"ChaosImpact.Tackle.CPU",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCPUTackleTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FCPUTackleCommand(this)); return true; }
#endif
