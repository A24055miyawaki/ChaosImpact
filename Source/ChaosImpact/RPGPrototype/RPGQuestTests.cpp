#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Engine/DamageEvents.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGQuestCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGQuestCommand(FAutomationTestBase* T):Test(T) { Slot=TEXT("CI_RPG_Quest_QA_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Quest test timeout %d"),Stage)); UGameplayStatics::DeleteGameInSlot(Slot,0); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        if(Stage==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0)); auto* Place=ARPGAdventureWorld::Find(W);
        if(!P || !Place || !P->GetPrototypeHUD() || !Cast<AChaosImpactPlayerController>(P->GetController())->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        const auto GiverActor=[&](int32 Id)->ARPGQuestGiver* { for(TActorIterator<ARPGQuestGiver> It(W);It;++It) { if(It->GiverId==Id) { return *It; } } return nullptr; };
        switch(Stage)
        {
        case 1:
        {
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            int32 Givers=0; for(TActorIterator<ARPGQuestGiver> It(W);It;++It) { ++Givers; }
            Test->TestEqual(TEXT("Every person who gives quests stands in the field"),Givers,RPGQuests::Givers().Num()-1);
            Test->TestEqual(TEXT("Quest table covers all saved quests"),P->Quests.Num(),RPGQuests::Num());
            Test->TestEqual(TEXT("A quest that needs another first is locked"),RPGQuests::State(P,3),ERPGQuestState::Locked);
            Test->TestEqual(TEXT("Field quests are hidden until found"),RPGQuests::State(P,6),ERPGQuestState::Hidden);
            auto* Guard=GiverActor(1); if(!Test->TestNotNull(TEXT("Gate guard"),Guard)) { return true; }
            P->SetActorLocation(Guard->GetActorLocation()+FVector(-160,0,100),false,nullptr,ETeleportType::TeleportPhysics);
            Stage=2; Next=Now+.4; return false;
        }
        case 2:
        {
            Test->TestTrue(TEXT("Talking to a person opens their quests"),P->InteractAdventure() && P->IsQuestMenuOpen() && P->QuestGiver==1);
            Test->TestTrue(TEXT("The guard offers the treasure quest"),P->QuestPanelList().Contains(4) && RPGQuests::State(P,4)==ERPGQuestState::Offered);
            P->QuestSelection=4; P->NoticeUntil=0; Stage=3; Next=Now+.6; return false;
        }
        case 3:
            Shot(TEXT("quest-talk-offer.png")); Stage=4; Next=Now+.2; return false;
        case 4:
        {
            P->RequestQuestPrimary();
            Test->TestEqual(TEXT("Accepting from the person takes the quest on"),RPGQuests::State(P,4),ERPGQuestState::Active);
            P->SetQuestMenuOpen(false);
            for(int32 I=0;I<3;++I) { P->QuestEvent(ERPGQuestEvent::ChestOpened,P->GetActorLocation()); }
            Test->TestEqual(TEXT("Opening three chests finishes the treasure quest"),RPGQuests::State(P,4),ERPGQuestState::Ready);
            Money=P->GetRubies(); Items=P->GetBag().Num();
            P->InteractAdventure(); P->QuestSelection=4; P->RequestQuestPrimary();
            Test->TestEqual(TEXT("Reporting to the giver completes it"),RPGQuests::State(P,4),ERPGQuestState::Done);
            Test->TestEqual(TEXT("Report pays the table's rubies once"),P->GetRubies(),Money+RPGQuests::Get(4).Rubies);
            Test->TestEqual(TEXT("Report gives the reward item"),P->GetBag().Num(),Items+1);
            P->RequestQuestPrimary(); Test->TestEqual(TEXT("No second reward"),P->GetRubies(),Money+RPGQuests::Get(4).Rubies);
            P->SetQuestMenuOpen(false);
            // A quest found in the field: lit beacon -> journal -> asked once -> accepted.
            P->QuestEvent(ERPGQuestEvent::BeaconLit,P->GetActorLocation(),0);
            Test->TestEqual(TEXT("Lighting a beacon finds the beacon quest"),RPGQuests::State(P,6),ERPGQuestState::Offered);
            Test->TestEqual(TEXT("Finding is not accepting"),P->Quests[6].Stage,0);
            P->QuestEvent(ERPGQuestEvent::RegionEntered,P->GetActorLocation(),0,TEXT("水脈峡谷・入口"));
            Test->TestEqual(TEXT("Entering the canyon finds the explore quest"),RPGQuests::State(P,8),ERPGQuestState::Offered);
            P->AddLoot(FRPGPrototypeItem::Make(EChaosImpactBallType::Fire,1,ERPGPrototypeRarity::Rare));
            Test->TestEqual(TEXT("A first special ball finds its quest"),RPGQuests::State(P,10),ERPGQuestState::Offered);
            Test->TestTrue(TEXT("Journal opens anywhere"),P->OpenQuestJournal() && P->QuestGiver==INDEX_NONE);
            P->SetQuestTab(1); Test->TestTrue(TEXT("Found quests are listed as available"),P->QuestPanelList().Contains(6));
            P->QuestSelection=6; P->RequestQuestPrimary();
            Test->TestTrue(TEXT("The journal asks before taking a quest on"),P->bQuestConfirm && P->Quests[6].Stage==0);
            P->NoticeUntil=0; Stage=5; Next=Now+.5; return false;
        }
        case 5:
            Shot(TEXT("quest-journal-confirm.png")); Stage=6; Next=Now+.2; return false;
        case 6:
        {
            P->HandleQuestKey(EKeys::Enter);
            Test->TestEqual(TEXT("Confirmed field quest is active"),RPGQuests::State(P,6),ERPGQuestState::Active);
            P->SetQuestTab(0); Test->TestTrue(TEXT("Journal shows what is in progress"),P->QuestPanelList().Contains(6));
            P->QuestSelection=6; P->NoticeUntil=0; Stage=7; Next=Now+.5; return false;
        }
        case 7:
            Shot(TEXT("quest-journal-active.png")); Stage=71; Next=Now+.3; return false;
        case 71:
        {
            P->HandleQuestKey(EKeys::Escape); Test->TestFalse(TEXT("Escape closes the journal"),P->IsQuestMenuOpen());
            Money=P->GetRubies();
            P->QuestEvent(ERPGQuestEvent::BeaconLit,P->GetActorLocation(),1); P->QuestEvent(ERPGQuestEvent::BeaconLit,P->GetActorLocation(),1);
            Test->TestEqual(TEXT("The same beacon counts once"),P->Quests[6].Count,1);
            P->QuestEvent(ERPGQuestEvent::BeaconLit,P->GetActorLocation(),2); P->QuestEvent(ERPGQuestEvent::BeaconLit,P->GetActorLocation(),3);
            Test->TestEqual(TEXT("Field quest pays out where it is finished"),RPGQuests::State(P,6),ERPGQuestState::Done);
            Test->TestEqual(TEXT("Field quest reward"),P->GetRubies(),Money+RPGQuests::Get(6).Rubies);
            // Tracked quest on the HUD: take on the explore quest and visit one place.
            P->OpenQuestJournal(); P->SetQuestTab(1); P->QuestSelection=8; P->RequestQuestPrimary(); P->HandleQuestKey(EKeys::Enter); P->SetQuestMenuOpen(false);
            P->QuestEvent(ERPGQuestEvent::RegionEntered,P->GetActorLocation(),0,TEXT("峡谷の見晴らし台"));
            Test->TestEqual(TEXT("Explore counts a listed place"),P->Quests[8].Count,1);
            Test->TestEqual(TEXT("Newly accepted quest is tracked when none is"),P->TrackedQuest,8);
            P->NoticeUntil=0; Stage=8; Next=Now+.5; return false;
        }
        case 8:
        {
            Shot(TEXT("quest-tracker.png"));
            Test->TestTrue(TEXT("Quest progress saves"),P->SaveTestProgress(Slot));
            P->Quests[6]=FRPGAdventureProgress{}; P->Quests[8]=FRPGAdventureProgress{};
            Test->TestTrue(TEXT("Quest progress loads"),P->LoadTestProgress(Slot));
            Test->TestEqual(TEXT("Finished field quest restored"),P->Quests[6].Stage,3);
            Test->TestTrue(TEXT("Found/started explore quest restored"),P->Quests[8].Stage==1 && P->Quests[8].Count==1);
            auto* Board=(ARPGAdventureNode*)nullptr; for(TActorIterator<ARPGAdventureNode> It(W);It;++It) { if(It->Kind==ERPGAdventureNodeKind::Board) { Board=*It; } }
            if(Board) { P->SetActorLocation(Board->GetActorLocation()+FVector(-180,0,120),false,nullptr,ETeleportType::TeleportPhysics); }
            Stage=9; Next=Now+.4; return false;
        }
        case 9:
            Test->TestTrue(TEXT("The board still opens its own quests"),P->InteractAdventure() && P->QuestGiver==0 && P->QuestPanelList().Contains(0));
            P->QuestSelection=0; P->NoticeUntil=0; Stage=10; Next=Now+.6; return false;
        case 10:
            Shot(TEXT("quest-board.png")); P->SetQuestMenuOpen(false);
            // Multiplayer: every player has their own quests, and a kill counts for everyone who helped.
            W->GetAuthGameMode<ARPGPrototypeGameMode>()->SetLocalPlayerCount(2); Stage=11; Next=Now+.6; return false;
        case 11:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1)); if(!B) { return false; }
            Test->TestTrue(TEXT("Player 1 takes the hunt"),P->AcceptQuest(0));
            Test->TestTrue(TEXT("Player 2 can take the same hunt"),B->AcceptQuest(0));
            const auto FindTarget=[&]()->ARPGPrototypeCharacter* { for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt() && !It->IsEliminated() && !Cast<ARPGPrototypeBoss>(*It) && !Cast<ARPGSimaeBody>(*It)) { return *It; } } return nullptr; };
            ARPGPrototypeCharacter* Target=FindTarget();
            if(!Target)
            {
                // Wake one field camp (outside the colony and the snowfield) as the fixture, like the adventure test.
                auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>(); const FVector Back=P->GetActorLocation();
                for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It)
                {
                    if(!It->bEncounterGroup || It->GetActorLocation().X<5000 || !ARPGSnowField::RegionAt(W,It->GetActorLocation()).IsEmpty()) { continue; }
                    P->SetActorLocation(It->GetActorLocation()+FVector(1200,0,0),false,nullptr,ETeleportType::TeleportPhysics);
                    It->bEnabled=true; It->TickEncounter(Mode,W->GetTimeSeconds()); It->bEnabled=false;
                    P->SetActorLocation(Back,false,nullptr,ETeleportType::TeleportPhysics);
                    break;
                }
                Target=FindTarget(); if(Target) { Target->SetTrainingMenuFrozen(true); }
            }
            if(!Test->TestNotNull(TEXT("An enemy to beat together"),Target)) { return true; }
            Target->TakeDamage(1,FDamageEvent(),B->GetController(),B);
            Target->TakeDamage(Target->GetMaxHealth()*3,FDamageEvent(),P->GetController(),P);
            Stage=12; Next=Now+.5; return false;
        }
        case 12:
        {
            auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            Test->TestEqual(TEXT("The finisher's hunt counts"),P->Quests[0].Count,1);
            Test->TestTrue(TEXT("The helper's hunt counts too"),B && B->Quests[0].Count==1);
            W->GetAuthGameMode<ARPGPrototypeGameMode>()->SetLocalPlayerCount(1);
            UGameplayStatics::DeleteGameInSlot(Slot,0); return true;
        }
        }
        return false;
    }
private:
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds(),Next=0; int32 Stage=0,Money=0,Items=0; FString Slot;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGQuestTest,"ChaosImpact.RPGPrototype.Quests",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGQuestTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGQuestCommand(this)); return true; }
#endif
