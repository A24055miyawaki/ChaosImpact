#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGWorldLayout.h"
#include "RPGPrototypeHUD.h"
#include "RPGPrototypeHUDStyle.h"
#include "RPGPrototypeTestLoadout.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGSharedScreenCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGSharedScreenCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Shared screen timeout %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=2?CIKeyboardPlayer=1?CISharedScreen=1")); Stage=1; return false; }
        auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>();
        auto* PC=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* PC2=UGameplayStatics::GetPlayerController(W,1);
        auto* A=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
        auto* B=PC2?Cast<ARPGPrototypeCharacter>(PC2->GetPawn()):nullptr;
        if(!Mode || !A || (Stage<7 && !B) || !A->GetPrototypeHUD() || !PC->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds();
        if(Stage==3 && Now<Next) { B->AddMovementInput(FVector(1,0,0)); }
        if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        switch(Stage)
        {
        case 1:
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            A->SetPrototypeAutomatedInput(true); B->SetPrototypeAutomatedInput(true);
            A->SetActorLocation(FVector(3600,250,RPGWorld::Height(3600,250)+120)); B->SetActorLocation(FVector(4050,200,RPGWorld::Height(4050,200)+120));
            RPGPrototypeTest::SupplySpecials(A); RPGPrototypeTest::SupplySpecials(B);
            A->SelectAttack(1); B->SelectAttack(2);
            Stage=2; Next=Now+.7; return false;
        case 2:
        {
            Test->TestTrue(TEXT("Shared setting enabled"),Mode->UsesSharedScreen());
            Test->TestTrue(TEXT("Both controllers use the same camera"),PC->GetViewTarget()==PC2->GetViewTarget() && PC->GetViewTarget()!=A);
            Test->TestEqual(TEXT("Exactly one screen layout"),W->GetGameViewport()->GetCurrentSplitscreenConfiguration(),ESplitScreenType::None);
            for(auto* Local:W->GetGameInstance()->GetLocalPlayers()) { Test->TestTrue(TEXT("Every player has full-screen projection geometry"),Local->Size.Equals(FVector2D(1,1),.01)); }
            FVector Ray,Direction;
            Test->TestTrue(TEXT("Mouse projection works for P2 as well as P1"),PC2->DeprojectScreenPositionToWorld(800,450,Ray,Direction));
            Test->TestFalse(TEXT("Keyboard can be assigned to P2 in shared mode"),Cast<AChaosImpactPlayerController>(PC2)->IsUsingGamepad());
            Test->TestTrue(TEXT("P2 HUD has nonzero full-screen geometry"),B->GetPrototypeHUD() && B->GetPrototypeHUD()->GetCachedGeometry().GetLocalSize().X>900);
            Shot(TEXT("rpg-shared-two.png"));
            Stage=21; Next=Now+.2; return false;
        }
        case 21:
            B->SetActorLocation(RPGWorld::AreaPoint(TEXT("Quarry"))+FVector(0,0,120)); Stage=20; Next=Now+.4; return false;
        case 20:
            Test->TestTrue(TEXT("A separated player safely warps near P1"),FVector::Dist2D(A->GetActorLocation(),B->GetActorLocation())<650);
            Test->TestFalse(TEXT("Warp is not a KO"),B->IsEliminated());
            A->SetBagOpen(true); A->BagPage=3; MoveStart=B->GetActorLocation(); Stage=3; Next=Now+.6; return false;
        case 3:
            Test->TestTrue(TEXT("Owner menu freezes only owner in shared mode"),A->IsTrainingMenuFrozen() && !B->IsTrainingMenuFrozen() && FVector::Dist2D(MoveStart,B->GetActorLocation())>80);
            Test->TestFalse(TEXT("Shared menu does not pause world"),W->IsPaused());
            Shot(TEXT("rpg-shared-menu.png")); Stage=30; Next=Now+.2; return false;
        case 30:
            A->GetPrototypeHUD()->ActivateBagAction(404);
            Test->TestFalse(TEXT("Settings menu switches to split without resetting map"),Mode->UsesSharedScreen());
            A->SetBagOpen(false); B->SetActorLocation(FVector(9200,-2900,RPGWorld::Height(9200,-2900)+120));
            Stage=4; Next=Now+.5; return false;
        case 4:
            Test->TestFalse(TEXT("Split mode restored"),Mode->UsesSharedScreen());
            for(auto* Local:W->GetGameInstance()->GetLocalPlayers()) { Test->TestTrue(TEXT("Two-player split is vertical"),Local->Size.Equals(FVector2D(.5,1),.01)); }
            Test->TestTrue(TEXT("Independent pawn cameras restored"),PC->GetViewTarget()==A && PC2->GetViewTarget()==B);
            Test->TestTrue(TEXT("Split play has no distance warp"),FVector::Dist2D(A->GetActorLocation(),B->GetActorLocation())>4000);
            B->SetBagOpen(true); B->BagPage=3; B->GetPrototypeHUD()->ActivateBagAction(404);
            Test->TestFalse(TEXT("Only P1 may change the screen mode"),Mode->UsesSharedScreen()); B->SetBagOpen(false);
            A->SetBagOpen(true); A->BagPage=3; A->GetPrototypeHUD()->ActivateBagAction(404); A->SetBagOpen(false);
            Mode->SetLocalPlayerCount(4); Stage=5; Next=Now+.7; return false;
        case 5:
            Test->TestEqual(TEXT("Four players share one view"),Mode->GetLocalPlayerCount(),4);
            for(int32 I=0;I<4;++I)
            { auto* C=UGameplayStatics::GetPlayerController(W,I); Test->TestTrue(TEXT("All four cameras identical"),C && C->GetViewTarget()==PC->GetViewTarget());
              auto* P=C?Cast<ARPGPrototypeCharacter>(C->GetPawn()):nullptr; Test->TestTrue(TEXT("Each player has a HUD"),P && P->GetPrototypeHUD() && P->GetPrototypeHUD()->GetCachedGeometry().GetLocalSize().X>900); }
            for(int32 I=2;I<4;++I)
            { auto* C=UGameplayStatics::GetPlayerController(W,I); auto* P=Cast<ARPGPrototypeCharacter>(C->GetPawn()); RPGPrototypeTest::SupplySpecials(P); P->SelectAttack(I-1); }
            A->BeginThrowInput(); A->EndThrowInput(); B->BeginThrowInput();
            Stage=50; Next=Now+.7; return false;
        case 50:
            Shot(TEXT("rpg-shared-four.png")); Stage=6; Next=Now+.2; return false;
        case 6:
            B->RequestCancelThrow();
            Mode->SetLocalPlayerCount(3); Stage=60; Next=Now+.4; return false;
        case 60:
            Test->TestEqual(TEXT("Three players also share one view"),Mode->GetLocalPlayerCount(),3);
            for(int32 I=0;I<3;++I)
            { auto* C=UGameplayStatics::GetPlayerController(W,I); Test->TestTrue(TEXT("Three-player common camera"),C && C->GetViewTarget()==PC->GetViewTarget()); }
            Shot(TEXT("rpg-shared-three.png")); Stage=61; Next=Now+.2; return false;
        case 61:
            Mode->SetSharedScreen(false); Mode->SetLocalPlayerCount(1);
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
            Stage=7; return false;
        case 7:
            return W->GetMapName().Contains(TEXT("Lvl_RPGVSStage1"));
        }
        return false;
    }
private:
    FAutomationTestBase* Test; int32 Stage=0; FVector MoveStart;
    double Started=FPlatformTime::Seconds(),Next=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSharedScreenTest,"ChaosImpact.RPGPrototype.SharedScreen",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGSharedScreenTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGSharedScreenCommand(this)); return true; }
#endif
