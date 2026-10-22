#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGWorldLayout.h"
#include "RPGAdventure.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGDungeonCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGDungeonCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(TEXT("Cave/checkpoint timeout")); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=2?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* PC=UGameplayStatics::GetPlayerController(W,0); auto* PC2=UGameplayStatics::GetPlayerController(W,1);
        auto* A=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
        auto* B=PC2?Cast<ARPGPrototypeCharacter>(PC2->GetPawn()):nullptr;
        if(!A || (Stage<6 && !B) || !Cast<AChaosImpactPlayerController>(PC)->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        switch(Stage)
        {
        case 1:
        {
            A->SetPrototypeAutomatedInput(true); B->SetPrototypeAutomatedInput(true);
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            for(TActorIterator<ARPGAdventureCheckpoint> It(W);It;++It)
            { if(It->DisplayName==TEXT("水晶洞窟・入口の灯り")) { Entry=*It; } if(It->DisplayName==TEXT("ボス前の灯り")) { Last=*It; } }
            for(TActorIterator<ARPGPrototypeBoss> It(W);It;++It) { Boss=*It; }
            if(!Test->TestTrue(TEXT("Cave has entry, pre-boss checkpoints and exactly existing boss"),Entry.IsValid() && Last.IsValid() && Boss.IsValid())) { return true; }
            Test->TestTrue(TEXT("Boss moved to dungeon depths"),Boss->GetActorLocation().X>38000);
            Test->TestFalse(TEXT("Checkpoint never activates remotely"),Entry->TryActivate(A));
            // Every road of the cave (and the way in from the garden), along its centre line.
            TArray<TArray<FVector>> Routes;
            for(const auto& Road:RPGWorld::Roads()) { if(FString(Road.Region).StartsWith(TEXT("水晶洞窟")) || Road.Points[0].X>29000) { Routes.Add(Road.Points); } }
            int32 Missing=0,Blocked=0; float MaxStep=0; FVector WorstStep=FVector::ZeroVector;
            for(const auto& Route:Routes)
            {
                float PreviousZ=960;
                for(int32 I=0;I+1<Route.Num();++I)
                {
                    const int32 N=FMath::CeilToInt(FVector::Dist2D(Route[I],Route[I+1])/50);
                    for(int32 J=0;J<=N;++J)
                    {
                        const FVector At=FMath::Lerp(Route[I],Route[I+1],static_cast<float>(J)/N); FHitResult H;
                        if(!W->LineTraceSingleByObjectType(H,At+FVector(0,0,1500),At-FVector(0,0,1800),FCollisionObjectQueryParams(ECC_WorldStatic))) { ++Missing; continue; }
                        const float Step=FMath::Abs(static_cast<float>(H.ImpactPoint.Z)-PreviousZ);
                        if(Step>MaxStep) { MaxStep=Step; WorstStep=At; }
                        PreviousZ=H.ImpactPoint.Z;
                        // Capsule clearance catches side walls that a center-only floor trace misses.
                        if(W->OverlapAnyTestByObjectType(At+FVector(0,0,100),FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeCapsule(42,92)))
                        { if(Blocked<16) { Test->AddInfo(FString::Printf(TEXT("Cave blocked capsule at %s, floor hit %s"),*At.ToString(),*GetNameSafe(H.GetActor()))); } ++Blocked; }
                    }
                }
            }
            Test->TestEqual(TEXT("Cave paths have no floor gaps"),Missing,0);
            Test->TestEqual(TEXT("Cave corridors clear a player capsule"),Blocked,0);
            Test->TestTrue(FString::Printf(TEXT("Cave floors remain aligned (max step %.1f at %s)"),MaxStep,*WorstStep.ToString()),MaxStep<=40.1f);
            A->SetActorLocation(Entry->GetActorLocation()+FVector(100,0,120));
            Test->TestTrue(TEXT("Nearby player activates entry checkpoint"),Entry->TryActivate(A));
            Test->TestFalse(TEXT("Teammate checkpoint stays independent"),B->ActiveAdventureCheckpoint.IsValid());
            A->TakeDamage(5,FDamageEvent(),Boss->GetController(),Boss.Get()); const float Hurt=A->GetHealth();
            Test->TestFalse(TEXT("Standing on checkpoint does not repeatedly heal"),Entry->TryActivate(A));
            Test->TestEqual(TEXT("No repeated free healing"),A->GetHealth(),Hurt);
            A->SetActorLocation(FVector(31200,400,-1100)); A->TickAdventure(.016f);
            Test->TestTrue(TEXT("Falling returns only player to active checkpoint"),FVector::Dist2D(A->GetActorLocation(),Entry->RespawnFor(0))<60);
            Test->TestTrue(TEXT("A fall costs a little HP"),A->GetHealth()<Hurt);
            // Two players: a knockout is a down first; both down at once is the real knockout.
            A->TakeDamage(10000,FDamageEvent(),Boss->GetController(),Boss.Get());
            Test->TestTrue(TEXT("With a teammate a knockout is a down"),A->IsDowned() && !A->IsEliminated());
            B->SetActorLocation(Entry->GetActorLocation()+FVector(2600,0,120));
            B->TakeDamage(10000,FDamageEvent(),Boss->GetController(),Boss.Get());
            Stage=101; Next=Now+.3; return false;
        }
        case 101:
        {
            Test->TestTrue(TEXT("Real KO starts respawn"),A->IsEliminated() && B && B->IsEliminated());
            Stage=2; Next=Now+5; return false;
        }
        case 2:
            Test->TestFalse(TEXT("Player revived"),A->IsEliminated());
            Test->TestTrue(TEXT("Real KO respawns at checkpoint rather than colony"),FVector::Dist2D(A->GetActorLocation(),Entry->RespawnFor(0))<60);
            A->SetActorLocation(Last->GetActorLocation()+FVector(100,0,120)); Last->TryActivate(A);
            B->SetActorLocation(Entry->GetActorLocation()+FVector(100,0,120)); Entry->TryActivate(B);
            Test->TestTrue(TEXT("Players can select different checkpoints"),A->ActiveAdventureCheckpoint.Get()==Last.Get() && B->ActiveAdventureCheckpoint.Get()==Entry.Get());
            A->SetActorLocation(FVector(36400,400,-1100)); B->SetActorLocation(FVector(31200,400,-1100));
            A->TickAdventure(.016f); B->TickAdventure(.016f);
            Test->TestTrue(TEXT("Two players fall back to their own different checkpoints"),FVector::Dist2D(A->GetActorLocation(),Last->RespawnFor(0))<60 && FVector::Dist2D(B->GetActorLocation(),Entry->RespawnFor(1))<60);
            Test->TestFalse(TEXT("Boss does not attack the pre-boss checkpoint"),Boss->IsBossActive());
            Stage=3; Next=Now+.5; return false;
        case 3:
            Shot(TEXT("adventure-cave-checkpoints-split.png")); Stage=4; Next=Now+.2; return false;
        case 4:
            W->GetAuthGameMode<ARPGPrototypeGameMode>()->SetLocalPlayerCount(1);
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("CaveHall"))+FVector(0,0,120)); Stage=6; Next=Now+.6; return false;
        case 6:
            Shot(TEXT("adventure-cave-entry.png")); Stage=7; Next=Now+.2; return false;
        case 7:
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Crystal"))+FVector(0,0,120)); Stage=8; Next=Now+.6; return false;
        case 8:
            Shot(TEXT("adventure-cave-hall.png")); Stage=9; Next=Now+.2; return false;
        case 9:
            A->SetActorLocation(FVector(39800,200,RPGWorld::Height(39800,200)+120)); Boss->SetTrainingMenuFrozen(false);
            Stage=10; Next=Now+.7; return false;
        case 10:
            Test->TestTrue(TEXT("Maguro encounter starts when entering the final chamber"),Boss->IsBossActive());
            Shot(TEXT("adventure-cave-boss.png")); Stage=11; Next=Now+.2; return false;
        case 11:
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
            Stage=12; return false;
        case 12:
            return W->GetMapName().Contains(TEXT("Lvl_RPGVSStage1"));
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0;
    TWeakObjectPtr<ARPGAdventureCheckpoint> Entry,Last;
    TWeakObjectPtr<ARPGPrototypeBoss> Boss;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonTest,"ChaosImpact.RPGPrototype.DungeonCheckpoint",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGDungeonTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGDungeonCommand(this)); return true; }
#endif
