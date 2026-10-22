#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCombatHealth.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatHealthStateTest,"ChaosImpact.UI.CombatHealth.State",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCombatHealthStateTest::RunTest(const FString&)
{
    using namespace ChaosImpactCombatHealth;
    FState State; FPresentation P;
    State.Update(3,3,false,10); TestFalse(TEXT("Fresh full health stays unobtrusive"),State.Present(10,P));
    State.Update(2,3,false,11); TestTrue(TEXT("Damage opens gauge"),State.Present(11.1,P));
    TestEqual(TEXT("Three slots"),P.Cells,3); TestEqual(TEXT("One slot removed"),P.Remaining,2);
    TestEqual(TEXT("One broken shard group"),P.Breaks.Num(),1); TestEqual(TEXT("Rightmost cell breaks first"),P.Breaks[0].Cell,2);
    State.Update(1,3,false,11.2); State.Present(11.21,P);
    TestEqual(TEXT("Rapid successive hits retain both breaks"),P.Breaks.Num(),2);
    TestTrue(TEXT("Last point is critical"),P.Critical);
    State.Update(1,3,false,30); TestTrue(TEXT("Critical persists after normal timeout"),State.Present(30,P));
    TestEqual(TEXT("Old particles expire"),P.Breaks.Num(),0);
    State.Update(0,3,true,31); TestTrue(TEXT("KO keeps final break briefly"),State.Present(31.1,P));
    TestFalse(TEXT("KO is not a low-health warning"),P.Critical); TestEqual(TEXT("KO empties all cells"),P.Remaining,0);
    TestFalse(TEXT("KO indicator fades out"),State.Present(31.8,P));
    State.Update(3,3,false,35); TestFalse(TEXT("Respawn clears history"),State.Present(35,P));
    State.Update(0,3,true,36); State.Present(36,P); TestEqual(TEXT("Instant KO shatters every lost cell"),P.Breaks.Num(),3);
    FState ReplicatedArrival; ReplicatedArrival.Update(1,3,false,50); ReplicatedArrival.Present(50,P);
    TestTrue(TEXT("Already injured remote arrival is still marked critical"),P.Critical);
    TestEqual(TEXT("Arrival does not fabricate a damage animation"),P.Breaks.Num(),0);
    FState Normal; Normal.Update(3,3,false,0); Normal.Update(2,3,false,1);
    TestTrue(TEXT("Normal gauge fades gradually"),Normal.Present(4.8,P) && P.Alpha>0 && P.Alpha<1);
    TestFalse(TEXT("Normal gauge disappears after four seconds"),Normal.Present(5.1,P));
    return true;
}

namespace
{
UChaosImpactChargeWidget* HealthHUD(APlayerController* PC)
{
    for(TObjectIterator<UChaosImpactChargeWidget> It;It;++It)
    { if(It->GetOwningPlayer()==PC && It->GetWorld()==PC->GetWorld() && It->IsInViewport()) { return *It; } }
    return nullptr;
}
class FCombatHealthGameplayCommand : public IAutomationLatentCommand
{
public:
    FCombatHealthGameplayCommand(FAutomationTestBase* InTest,bool InVersus,int32 InPlayers=2):Test(InTest),Versus(InVersus),Players(InPlayers) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150) { Test->AddError(FString::Printf(TEXT("Combat HP timeout stage %d"),Stage)); Cleanup(); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        {
            FString Options=FString::Printf(TEXT("CITraining=1?CITargets=0?CICPUCount=1?CILocalPlayers=%d?CIKeyboardPlayer=0"),Players);
            if(Versus) { Options+=TEXT("?CIVersus=1?CIMatch=1?CIMatchCPU=1?CITeams=0?CIMinutes=5?CIStage=0"); }
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,Options); Stage=1; return false;
        }
        auto* PC=Cast<AChaosImpactPlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* PC2=UGameplayStatics::GetPlayerController(W,1);
        auto* A=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
        auto* B=PC2?Cast<AChaosImpactCharacter>(PC2->GetPawn()):nullptr;
        auto* Match=W->GetGameState<AChaosImpactGameState>();
        if(!A || !B || !PC->IsGameplayActive() || !HealthHUD(PC) || !HealthHUD(PC2)) { return false; }
        if(Versus && (!Match || !Match->bVersusMatch || Match->Phase!=EChaosImpactOnlinePhase::Match || Match->IsMatchInputLocked())) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        const auto Shot=[&](const TCHAR* Name)
        { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("CombatHealthQA"),FString(Versus?TEXT("VS-"):Players==4?TEXT("Training4P-"):TEXT("Training-"))+Name),true,false); };
        auto* HUD=HealthHUD(PC); auto* HUD2=HealthHUD(PC2);
        ChaosImpactCombatHealth::FPresentation P,Q;
        switch(Stage)
        {
        case 1:
        {
            for(TActorIterator<AChaosImpactCPUController> It(W);It;++It)
            { It->SetActorTickEnabled(false); if(!CPU.IsValid()) { CPU=Cast<AChaosImpactCharacter>(It->GetPawn()); } }
            if(!CPU.IsValid()) { return false; }
            OriginalA=A->GetActorLocation(); OriginalB=B->GetActorLocation(); OriginalCPU=CPU->GetActorLocation();
            A->ResetForOnlineMatch(OriginalA,A->GetActorRotation()); B->ResetForOnlineMatch(OriginalB,B->GetActorRotation());
            CPU->ResetForOnlineMatch(OriginalCPU,CPU->GetActorRotation());
            const FVector Right=FRotationMatrix(A->GetCameraBoom()->GetComponentRotation()).GetUnitAxis(EAxis::Y).GetSafeNormal2D();
            CPU->SetActorLocation(OriginalA+FVector(330,0,0)+Right*200); B->SetActorLocation(OriginalA+Right*400);
            for(int32 I=2;I<Players;++I)
            { if(auto* Other=UGameplayStatics::GetPlayerPawn(W,I)) { Other->SetActorLocation(OriginalA+Right*(I==2?-150:550)); } }
            for(TActorIterator<AChaosImpactCharacter> It(W);It;++It) { It->SetTrainingMenuFrozen(true); }
            PlayerA=A; PlayerB=B; Stage=2; Next=Now+.7; return false;
        }
        case 2:
            Test->TestFalse(TEXT("Fresh CPU has no always-on bar"),HUD->GetCombatHealthPresentation(CPU.Get(),P));
            Test->TestFalse(TEXT("Full health has no own warning"),HUD->IsLastHealthWarningActive());
            Test->TestEqual(TEXT("A genuine opponent damage event consumes one HP"),CPU->TakeDamage(1,FDamageEvent(),PC,A),1.f);
            Stage=3; Next=Now+.12; return false;
        case 3:
            Test->TestTrue(TEXT("P1 sees damaged opponent"),HUD->GetCombatHealthPresentation(CPU.Get(),P));
            Test->TestTrue(TEXT("P2 also sees damaged opponent in its own view"),HUD2->GetCombatHealthPresentation(CPU.Get(),Q));
            Test->TestEqual(TEXT("Three visual cells"),P.Cells,3); Test->TestEqual(TEXT("Opponent has two cells left"),P.Remaining,2);
            Test->TestTrue(TEXT("Broken cell effect exists in both views"),P.Breaks.Num()==1 && Q.Breaks.Num()==1);
            Shot(TEXT("2HP-shatter.png")); Stage=4; Next=Now+4.2; return false;
        case 4:
        {
            Test->TestFalse(TEXT("Normal damaged opponent bar times out"),HUD->GetCombatHealthPresentation(CPU.Get(),P));
            const FVector Direction=FVector(0,1,0); const FTransform At(CPU->GetActorLocation()-Direction*230);
            auto* Ball=W->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),At,A,A,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Ball->FinishSpawning(At); Ball->Launch(Direction,1800,EChaosImpactBallFlightMode::Straight,0); Projectile=Ball;
            BallAt=Now; Stage=5; Next=Now+.16; return false;
        }
        case 5:
            if(CPU->GetHealth()>1 && Now-BallAt<2) { return false; }
            Test->TestEqual(TEXT("A thrown normal ball actually hits the opponent"),CPU->GetHealth(),1.f);
            Test->TestTrue(TEXT("Actual hit shows critical overhead gauge"),HUD->GetCombatHealthPresentation(CPU.Get(),P) && P.Critical);
            Test->TestTrue(TEXT("Other split view receives the same actual HP"),HUD2->GetCombatHealthPresentation(CPU.Get(),Q) && Q.Critical && Q.Remaining==1);
            for(int32 I=2;I<Players;++I)
            { auto* ExtraHUD=HealthHUD(UGameplayStatics::GetPlayerController(W,I)); Test->TestTrue(TEXT("3P and 4P also see the damaged opponent"),ExtraHUD && ExtraHUD->GetCombatHealthPresentation(CPU.Get(),Q) && Q.Critical); }
            Test->TestEqual(TEXT("Enemy damage can put P1 on last point"),A->TakeDamage(2,FDamageEvent(),CPU->GetController(),CPU.Get()),2.f);
            Test->TestEqual(TEXT("Enemy damage can put P2 on last point"),B->TakeDamage(2,FDamageEvent(),CPU->GetController(),CPU.Get()),2.f);
            Stage=6; Next=Now+.2; return false;
        case 6:
            Test->TestFalse(TEXT("P1 never sees its own overhead health"),HUD->GetCombatHealthPresentation(A,P));
            Test->TestFalse(TEXT("P2 never sees its own overhead health"),HUD2->GetCombatHealthPresentation(B,Q));
            Test->TestTrue(TEXT("P2 still sees injured P1"),HUD2->GetCombatHealthPresentation(A,P) && P.Critical);
            Test->TestTrue(TEXT("P1 still sees injured P2"),HUD->GetCombatHealthPresentation(B,Q) && Q.Critical);
            Test->TestTrue(TEXT("P1 has an explicit last-HP warning"),HUD->IsLastHealthWarningActive());
            Test->TestTrue(TEXT("P2 has its own last-HP warning"),HUD2->IsLastHealthWarningActive());
            Shot(TEXT("1HP-critical-split.png")); Stage=7; Next=Now+4.2; return false;
        case 7:
            Test->TestTrue(TEXT("Last point stays visible after four seconds"),HUD->GetCombatHealthPresentation(CPU.Get(),P) && P.Critical);
            Test->TestTrue(TEXT("KO still uses the original three-hit rule"),CPU->TakeDamage(1,FDamageEvent(),PC,A)==1 && CPU->IsEliminated());
            Stage=8; Next=Now+.12; return false;
        case 8:
            Test->TestTrue(TEXT("Final cell shatters on KO"),HUD->GetCombatHealthPresentation(CPU.Get(),P) && P.Remaining==0 && !P.Critical && !P.Breaks.IsEmpty());
            Shot(TEXT("KO-final-cell.png")); Stage=9; Next=Now+.8; return false;
        case 9:
            Test->TestFalse(TEXT("KO gauge does not linger until respawn"),HUD->GetCombatHealthPresentation(CPU.Get(),P));
            A->ResetForOnlineMatch(OriginalA,A->GetActorRotation()); B->ResetForOnlineMatch(OriginalB,B->GetActorRotation());
            Stage=10; Next=Now+.2; return false;
        case 10:
            Test->TestFalse(TEXT("Health reset clears P1 warning"),HUD->IsLastHealthWarningActive());
            Test->TestFalse(TEXT("Health reset clears P2 warning"),HUD2->IsLastHealthWarningActive());
            Cleanup(); return true;
        }
        return false;
    }
private:
    void Cleanup()
    {
        if(Projectile.IsValid()) { Projectile->Destroy(); }
        for(auto P:{PlayerA,PlayerB,CPU}) { if(P.IsValid()) { P->SetTrainingMenuFrozen(false); } }
        if(PlayerA.IsValid()) { for(TActorIterator<AChaosImpactCharacter> It(PlayerA->GetWorld());It;++It) { It->SetTrainingMenuFrozen(false); } }
    }
    FAutomationTestBase* Test; bool Versus=false;
    int32 Stage=0,Players=2; double Started=FPlatformTime::Seconds(),Next=0,BallAt=0;
    FVector OriginalA,OriginalB,OriginalCPU;
    TWeakObjectPtr<AChaosImpactCharacter> PlayerA,PlayerB,CPU;
    TWeakObjectPtr<AChaosImpactBall> Projectile;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatHealthTrainingTest,"ChaosImpact.UI.CombatHealth.Training",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCombatHealthTrainingTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FCombatHealthGameplayCommand(this,false)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatHealthVersusTest,"ChaosImpact.UI.CombatHealth.Versus",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCombatHealthVersusTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FCombatHealthGameplayCommand(this,true)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatHealthFourPlayersTest,"ChaosImpact.UI.CombatHealth.FourPlayers",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCombatHealthFourPlayersTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FCombatHealthGameplayCommand(this,false,4)); return true; }
#endif
