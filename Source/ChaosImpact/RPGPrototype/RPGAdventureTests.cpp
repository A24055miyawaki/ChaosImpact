#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGSnowField.h"
#include "RPGPrototypeHUD.h"
#include "RPGAdventure.h"
#include "RPGWorldLayout.h"
#include "Engine/StaticMeshActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

namespace
{
class FRPGAdventureCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGAdventureCommand(FAutomationTestBase* T):Test(T) { Slot=TEXT("CI_RPG_Adventure_QA_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150) { Test->AddError(FString::Printf(TEXT("Adventure timeout stage %d"),Stage)); UGameplayStatics::DeleteGameInSlot(Slot,0); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=2?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* Place=ARPGAdventureWorld::Find(W); auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>();
        auto* PC1=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* PC2=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,1));
        auto* A=PC1?Cast<ARPGPrototypeCharacter>(PC1->GetPawn()):nullptr; auto* B=PC2?Cast<ARPGPrototypeCharacter>(PC2->GetPawn()):nullptr;
        if(!Place || !Mode || !A || !A->GetPrototypeHUD() || (Stage<5 && (!B || !B->GetPrototypeHUD())) || !PC1->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds();
        if(Stage==3 && Now<Next) { A->AddMovementInput(FVector(1,.1f,0)); }
        if(Stage==83 && Now<Next) { A->AddMovementInput(FVector(1,0,0)); }
        if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        switch(Stage)
        {
        case 1:
        {
            Test->TestTrue(TEXT("Colony start is inside safe area"),Place->IsInColony(A->GetActorLocation()));
            int32 TerrainTiles=0;
            for(TActorIterator<AStaticMeshActor> It(W);It;++It) { if(It->Tags.Contains(TEXT("RPGWorld")) && It->GetStaticMeshComponent()->GetStaticMesh()) { ++TerrainTiles; } }
            Test->TestTrue(TEXT("Terrain tiles were saved into the level"),TerrainTiles>=100);
            Test->TestTrue(TEXT("Map uses persistent individually editable scenery"),Place->bUseEditableScenery);
            int32 EditableCount=0;
            for(TActorIterator<ARPGAdventureSceneryPiece> It(W);It;++It) { ++EditableCount; }
            Test->TestTrue(TEXT("Editable buildings/trees/rocks survive gameplay load"),EditableCount>300);
            const auto* CDO=Cast<ARPGPrototypeCharacter>(A->GetClass()->GetDefaultObject());
            Test->TestTrue(TEXT("Adventure camera is only 18 percent farther"),FMath::IsNearlyEqual(A->GetCameraBoom()->TargetArmLength,CDO->GetCameraBoom()->TargetArmLength*1.18f,1.0f));
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { Test->TestFalse(TEXT("No enemy point in colony"),Place->IsInColony(It->GetActorLocation())); It->bEnabled=false; }
            // Distant encounter camps intentionally do not exist while both players are in the colony.
            // Load one actual camp as this test's frozen quest/damage fixture, then return to the safe start.
            const FVector SafeStart=A->GetActorLocation();
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It)
            {
                if(!It->bEncounterGroup || It->GetActorLocation().X<5000 || !ARPGSnowField::RegionAt(W,It->GetActorLocation()).IsEmpty()) { continue; }
                A->SetActorLocation(It->GetActorLocation()+FVector(1200,0,0),false,nullptr,ETeleportType::TeleportPhysics);
                It->bEnabled=true; It->TickEncounter(Mode,W->GetTimeSeconds()); It->bEnabled=false;
                A->SetActorLocation(SafeStart,false,nullptr,ETeleportType::TeleportPhysics);
                break;
            }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); if(!Cast<ARPGPrototypeBoss>(*It) && !Cast<ARPGSimaeBody>(*It)) { Enemies.Add(*It); } } }
            for(TActorIterator<ARPGAdventureNode> It(W);It;++It)
            {
                if(It->Kind==ERPGAdventureNodeKind::Board) { Board=*It; }
                if(It->Kind==ERPGAdventureNodeKind::Supply) { Supplies.Add(*It); }
                if(It->Kind==ERPGAdventureNodeKind::Survey) { Surveys.Add(*It); }
            }
            if(!Test->TestTrue(TEXT("Board, three supply boxes and two survey landmarks"),Board.IsValid() && Supplies.Num()==3 && Surveys.Num()==2)) { return true; }
            if(!Test->TestTrue(TEXT("Five frozen camp enemies for quest and damage checks"),Enemies.Num()>=5)) { return true; }
            A->SetPrototypeAutomatedInput(true); B->SetPrototypeAutomatedInput(true);
            Test->TestEqual(TEXT("Colony rejects enemy damage"),A->TakeDamage(10,FDamageEvent(),Enemies[0]->GetController(),Enemies[0].Get()),0.0f);
            // Every road of the map, along its centre line (over the bridges where it crosses the brook).
            TArray<TArray<FVector>> Routes;
            for(const auto& Road:RPGWorld::Roads()) { Routes.Add(Road.Points); }
            int32 Missing=0,Problems=0; float BiggestStep=0; FVector ProblemAt=FVector::ZeroVector;
            for(const auto& Route:Routes)
            {
                bool Previous=false; float Last=0;
                for(int32 Segment=0;Segment+1<Route.Num();++Segment)
                {
                    const int32 Samples=FMath::CeilToInt(FVector::Dist2D(Route[Segment],Route[Segment+1])/50);
                    for(int32 S=0;S<=Samples;++S)
                    {
                        const FVector At=FMath::Lerp(Route[Segment],Route[Segment+1],static_cast<float>(S)/Samples); FHitResult H;
                        if(!W->LineTraceSingleByObjectType(H,At+FVector(0,0,1800),At-FVector(0,0,1800),FCollisionObjectQueryParams(ECC_WorldStatic))) { ++Missing; Previous=false; continue; }
                        if(Previous)
                        {
                            const float Difference=FMath::Abs(static_cast<float>(H.ImpactPoint.Z)-Last);
                            if(Difference>BiggestStep) { BiggestStep=Difference; ProblemAt=At; }
                            if(Difference>45.1f && ++Problems<=30) { UE_LOG(LogTemp,Display,TEXT("ROUTESTEP %.0f at %s on %s"),Difference,*At.ToCompactString(),*GetNameSafe(H.GetActor())); }
                        }
                        Previous=true; Last=H.ImpactPoint.Z;
                    }
                }
            }
            Test->TestEqual(TEXT("All route centerlines have solid ground"),Missing,0);
            Test->TestTrue(FString::Printf(TEXT("Walking routes use traversable steps (max %.1f at %s)"),BiggestStep,*ProblemAt.ToString()),BiggestStep<=45.1f);
            A->SetActorLocation(Board->GetActorLocation()+FVector(-180,0,120));
            Test->TestTrue(TEXT("E-style interaction opens personal board"),A->InteractAdventure());
            for(int32 I=0;I<3;++I) { A->ActivateQuest(I); }
            Test->TestTrue(TEXT("Board freezes only reader"),A->IsTrainingMenuFrozen() && !B->IsTrainingMenuFrozen() && !W->IsPaused());
            Test->TestEqual(TEXT("Other player did not accept the quest"),B->Quests[0].Stage,0);
            A->NoticeUntil=0; Stage=2; Next=Now+.4; return false;
        }
        case 2:
            Shot(TEXT("adventure-board-split.png")); Stage=20; Next=Now+.15; return false;
        case 20:
        {
            A->SetQuestMenuOpen(false);
            B->SetActorLocation(Board->GetActorLocation()+FVector(-180,0,120)); B->InteractAdventure(); B->ActivateQuest(1); B->SetQuestMenuOpen(false);
            for(int32 I=0;I<4;++I) { Enemies[I]->TakeDamage(10000,FDamageEvent(),PC1,A); }
            Test->TestEqual(TEXT("Actual kills advance own patrol quest"),A->Quests[0].Stage,2);
            Test->TestEqual(TEXT("Uninvolved player's patrol quest unchanged"),B->Quests[0].Stage,0);
            A->SetActorLocation(FVector(2600,0,RPGWorld::Height(2600,0)+120)); MoveStart=A->GetActorLocation(); Stage=3; Next=Now+4; return false;
        }
        case 3:
        {
            Test->TestTrue(TEXT("Player actually walks out along gate road"),A->GetActorLocation().X>MoveStart.X+1000);
            Test->TestFalse(TEXT("Field is outside colony"),Place->IsInColony(A->GetActorLocation()));
            Test->TestTrue(TEXT("Outside damage uses RPG combat"),A->TakeDamage(8,FDamageEvent(),Enemies.Last()->GetController(),Enemies.Last().Get())>0);
            Shot(TEXT("adventure-gate.png"));
            for(int32 I=0;I<Supplies.Num();++I)
            {
                auto* Box=Supplies[I].Get(); A->SetActorLocation(Box->GetActorLocation()+FVector(-140,0,120));
                Test->TestTrue(TEXT("Supply can be collected by owner of active quest"),Box->Use(A));
                Test->TestFalse(TEXT("Same player cannot count box twice"),Box->Use(A));
                if(I==0)
                { B->SetActorLocation(Box->GetActorLocation()+FVector(-140,0,120)); Test->TestTrue(TEXT("Same physical supply independently collectible by teammate"),Box->Use(B)); }
            }
            Test->TestEqual(TEXT("Three unique supplies ready to report"),A->Quests[1].Stage,2);
            Test->TestEqual(TEXT("Teammate collected only their one box"),B->Quests[1].Count,1);
            for(auto Survey:Surveys) { A->SetActorLocation(Survey->GetActorLocation()+FVector(-140,0,120)); A->TickAdventure(.016f); }
            Test->TestEqual(TEXT("Both landmarks advance survey"),A->Quests[2].Stage,2);
            Test->TestEqual(TEXT("Teammate's unaccepted survey unchanged"),B->Quests[2].Stage,0);
            A->NoticeUntil=B->NoticeUntil=0; Stage=4; Next=Now+.4; return false;
        }
        case 4:
            Shot(TEXT("adventure-ruins.png")); Stage=40; Next=Now+.15; return false;
        case 40:
        {
            A->SetActorLocation(Board->GetActorLocation()+FVector(-180,0,120)); A->InteractAdventure();
            const int32 Money=A->GetRubies(),Items=A->GetBag().Num();
            for(int32 I=0;I<3;++I) { A->ActivateQuest(I); }
            Test->TestEqual(TEXT("Three rewards added to own bag"),A->GetBag().Num(),Items+3);
            Test->TestEqual(TEXT("Quest ruby rewards exactly once"),A->GetRubies(),Money+360);
            for(int32 I=0;I<3;++I) { A->ActivateQuest(I); }
            Test->TestEqual(TEXT("Repeated report cannot duplicate rewards"),A->GetRubies(),Money+360);
            A->SetQuestMenuOpen(false); Test->TestTrue(TEXT("Quest progress can be saved"),A->SaveTestProgress(Slot));
            A->Quests[2]=FRPGAdventureProgress{};
            Test->TestTrue(TEXT("Quest progress can be loaded"),A->LoadTestProgress(Slot));
            Test->TestEqual(TEXT("Completed quest restored"),A->Quests[2].Stage,3);
            Test->TestEqual(TEXT("Load does not change teammate's quest"),B->Quests[1].Count,1);
            A->NoticeUntil=0; Mode->SetLocalPlayerCount(1); A->SetActorLocation(Place->PlayerSpawn()); Stage=5; Next=Now+.5; return false;
        }
        case 5:
            Shot(TEXT("adventure-colony.png")); Stage=50; Next=Now+.15; return false;
        case 50:
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Meadow"))+FVector(0,0,120)); Stage=6; Next=Now+.5; return false;
        case 6:
            Shot(TEXT("adventure-riverside.png")); Stage=60; Next=Now+.15; return false;
        case 60:
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Tower"))+FVector(0,0,120)); Stage=7; Next=Now+.5; return false;
        case 7:
            Shot(TEXT("adventure-watchtower.png")); Stage=70; Next=Now+.15; return false;
        case 70:
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Quarry"))+FVector(0,0,120)); Stage=8; Next=Now+.5; return false;
        case 8:
            Shot(TEXT("adventure-quarry.png")); Stage=80; Next=Now+.15; return false;
        case 80:
        {
            auto* Camera=W->SpawnActor<ACameraActor>(FVector(-6000,0,24000),FRotator(-62,0,0));
            Camera->GetCameraComponent()->FieldOfView=85; PC1->SetViewTarget(Camera);
            A->GetPrototypeHUD()->SetRenderOpacity(0); Stage=81; Next=Now+.5; return false;
        }
        case 81:
            Shot(TEXT("adventure-layout-overview.png")); Stage=82; Next=Now+.15; return false;
        case 82:
            PC1->SetViewTarget(A); A->GetPrototypeHUD()->SetRenderOpacity(1);
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Ruins"))+FVector(0,0,120)); MoveStart=A->GetActorLocation();
            Stage=83; Next=Now+3; return false;
        case 83:
            Test->TestTrue(TEXT("Walks from the ruins towards the canyon gate"),A->GetActorLocation().X>MoveStart.X+800);
            A->SetActorLocation(FVector(22400,-3700,RPGWorld::Height(22400,-3700)+120)); Stage=84; Next=Now+.6; return false;
        case 84:
            Shot(TEXT("adventure-extension-upper.png")); Stage=85; Next=Now+.2; return false;
        case 85:
            A->SetActorLocation(FVector(22000,2700,RPGWorld::Height(22000,2700)+120)); Stage=86; Next=Now+.6; return false;
        case 86:
            Shot(TEXT("adventure-extension-waterway.png")); Stage=87; Next=Now+.2; return false;
        case 87:
            A->SetActorLocation(RPGWorld::AreaPoint(TEXT("Sluice"))+FVector(0,0,120)); Stage=88; Next=Now+.6; return false;
        case 88:
            Shot(TEXT("adventure-extension-sluice.png")); Stage=89; Next=Now+.2; return false;
        case 89:
        {
            auto* Camera=W->SpawnActor<ACameraActor>(FVector(17000,-500,16000),FRotator(-55,0,0));
            Camera->GetCameraComponent()->FieldOfView=85; PC1->SetViewTarget(Camera);
            A->GetPrototypeHUD()->SetRenderOpacity(0); Stage=90; Next=Now+.6; return false;
        }
        case 90:
            Shot(TEXT("adventure-extension-overview.png")); Stage=91; Next=Now+.2; return false;
        case 91:
            PC1->SetViewTarget(A); A->GetPrototypeHUD()->SetRenderOpacity(1);
            UGameplayStatics::DeleteGameInSlot(Slot,0); return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0;
    FString Slot;
    FVector MoveStart;
    TWeakObjectPtr<ARPGAdventureNode> Board;
    TArray<TWeakObjectPtr<ARPGAdventureNode>> Supplies,Surveys;
    TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> Enemies;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWorldAdventureTest,"ChaosImpact.RPGPrototype.WorldAdventure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGWorldAdventureTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGAdventureCommand(this)); return true; }
#endif
