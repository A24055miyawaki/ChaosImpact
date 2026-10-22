#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "RPGTreasureChest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGTreasureCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGTreasureCommand(FAutomationTestBase* T):Test(T) { Slot=TEXT("CI_RPG_Chest_QA_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(TEXT("Treasure timeout")); UGameplayStatics::DeleteGameInSlot(Slot,0); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=2?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* PC=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* A=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
        auto* PC2=UGameplayStatics::GetPlayerController(W,1); auto* B=PC2?Cast<ARPGPrototypeCharacter>(PC2->GetPawn()):nullptr;
        if(!A || !B || !A->GetPrototypeHUD() || !B->GetPrototypeHUD() || !PC->IsGameplayActive() || W->GetTimeSeconds()<Next) { return false; }
        if(Stage==1)
        {
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            TSet<FName> Ids; int32 Count=0;
            for(TActorIterator<ARPGTreasureChest> It(W);It;++It)
            { ++Count; Test->TestFalse(TEXT("Chest IDs unique and nonempty"),It->ChestId.IsNone() || Ids.Contains(It->ChestId)); Ids.Add(It->ChestId);
              if(It->ChestId==TEXT("FrontierTreasure_001")) { First=*It; } if(It->ChestId==TEXT("FrontierTreasure_002")) { Second=*It; } }
            if(!Test->TestTrue(TEXT("At least 32 chests placed across the map"),Count>=32 && First.IsValid() && Second.IsValid())) { return true; }
            int32 GoldChests=0; for(TActorIterator<ARPGTreasureChest> It(W);It;++It) { GoldChests+=It->Style==ERPGChestStyle::Gold; }
            Test->TestTrue(TEXT("Gold chests wait in the secret places"),GoldChests>=5);
            for(auto* P:{A,B})
            {
                P->SetPrototypeAutomatedInput(true);
                Test->TestEqual(TEXT("Starts with only the permanent normal core"),P->GetBag().Num(),1);
                Test->TestTrue(TEXT("Normal attack available"),P->GetAttack(0).IsValid());
                Test->TestFalse(TEXT("Two special slots empty"),P->GetAttack(1).IsValid() || P->GetAttack(2).IsValid());
                for(int32 I=0;I<4;++I) { Test->TestFalse(TEXT("No free gear"),P->GetGear(I).IsValid()); }
                for(int32 I=0;I<3;++I) { Test->TestFalse(TEXT("No free badges"),P->GetBadge(I).IsValid()); }
            }
            Test->TestTrue(TEXT("Empty slots can be saved"),A->SaveTestProgress(Slot));
            Test->TestTrue(TEXT("Empty slots can be loaded"),A->LoadTestProgress(Slot));
            A->SetBagOpen(true); Stage=2; Next=W->GetTimeSeconds()+.5; return false;
        }
        if(Stage==2)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA/treasure-empty-start.png")),true,false);
            Stage=20; Next=W->GetTimeSeconds()+.2; return false;
        }
        if(Stage==20)
        {
            A->SetBagOpen(false); A->SetActorLocation(First->GetActorLocation()+FVector(0,160,120));
            Money=A->GetRubies();
            Test->TestTrue(TEXT("Interaction opens chest"),A->InteractAdventure());
            Test->TestEqual(TEXT("Nothing goes straight into the bag"),A->GetBag().Num(),1);
            Test->TestEqual(TEXT("Rubies are not added until picked up"),A->GetRubies(),Money);
            int32 Thrown=0,Gems=0;
            for(TActorIterator<ARPGRubyGem> It(W);It;++It) { if(It->IsFor(A)) { Thrown+=It->GetValue(); ++Gems; } }
            Test->TestTrue(TEXT("The chest's rubies fly out as a handful of gems"),Gems>=2 && Gems<=16);
            Test->TestTrue(TEXT("Thrown rubies are in the chest's range"),Thrown>=First->RubyMin && Thrown<=First->RubyMax);
            int32 Items=0; for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It) { Items+=It->bChestLoot && It->IsVisibleTo(A); }
            Test->TestEqual(TEXT("The chest's item flies out to be picked up"),Items,1);
            Test->TestFalse(TEXT("Same player cannot reopen"),First->Open(A));
            Expected=Thrown; Stage=205; Next=W->GetTimeSeconds()+.3; return false;
        }
        if(Stage==205)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA/treasure-burst.png")),true,false);
            Stage=21; Next=W->GetTimeSeconds()+2.3; return false;
        }
        if(Stage==21)
        {
            // Walk over each ruby in turn: they are collected by walking near, with no button.
            for(TActorIterator<ARPGRubyGem> It(W);It;++It)
            {
                if(!It->IsFor(A)) { continue; }
                if(!It->HasLanded()) { UE_LOG(LogTemp,Display,TEXT("GEMDBG at %s chest %s"),*It->GetActorLocation().ToString(),*First->GetActorLocation().ToString()); }
                Test->TestTrue(TEXT("Rubies have landed by now"),It->HasLanded());
                A->SetActorLocation(It->GetActorLocation()+FVector(0,0,70),false,nullptr,ETeleportType::TeleportPhysics);
                Next=W->GetTimeSeconds()+.2; return false;
            }
            Test->TestEqual(TEXT("Every thrown ruby was collected by walking over it"),A->GetRubies()-Money,Expected);
            ARPGPrototypeLoot* Loot=nullptr; for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It) { if(It->bChestLoot && It->IsVisibleTo(A)) { Loot=*It; } }
            if(!Test->TestNotNull(TEXT("Chest item lies on the ground"),Loot)) { return true; }
            Test->TestTrue(TEXT("Chest item does not expire"),Loot->GetLifeSpan()<=0);
            A->SetActorLocation(Loot->GetActorLocation()+FVector(60,0,40),false,nullptr,ETeleportType::TeleportPhysics);
            Stage=22; Next=W->GetTimeSeconds()+.2; return false;
        }
        if(Stage==22)
        {
            Test->TestTrue(TEXT("The item is picked up with the pick-up button"),A->PickupNearbyLoot());
            Test->TestEqual(TEXT("Chest guarantees an item"),A->GetBag().Num(),2);
            Test->TestFalse(TEXT("Loot is not auto-equipped"),A->GetAttack(1).IsValid());
            Test->TestFalse(TEXT("Other player's claim unchanged"),First->IsOpenedBy(B));
            A->SetBagOpen(true); Test->TestTrue(TEXT("Loot can equip into empty special slot"),A->EquipBagItem(1,1)); A->SetBagOpen(false);
            for(const auto& Item:A->GetBag()) { Test->TestTrue(TEXT("Empty old slot never creates invalid bag item"),Item.IsValid()); }
            Test->TestTrue(TEXT("Claims and acquired gear save"),A->SaveTestProgress(Slot));
            A->OpenedChestIds.Empty(); Test->TestTrue(TEXT("Claims restore"),A->LoadTestProgress(Slot));
            Test->TestTrue(TEXT("Opened chest remains claimed after load"),First->IsOpenedBy(A));
            Test->TestTrue(TEXT("Still-empty second special survives save"),!A->GetAttack(2).IsValid());
            B->SetActorLocation(First->GetActorLocation()+FVector(0,-160,120));
            const int32 BeforeOpen=B->GetRubies(),BeforeItems=B->GetBag().Num();
            Test->TestTrue(TEXT("Teammate independently opens the same chest"),First->Open(B));
            // Saving at once: what the chest threw out is taken rather than lost with the saved claim.
            Test->TestTrue(TEXT("Save right after opening"),B->SaveTestProgress(Slot+TEXT("_B")));
            Test->TestTrue(TEXT("Save collects the chest's rubies"),B->GetRubies()>BeforeOpen);
            Test->TestEqual(TEXT("Save collects the chest's item"),B->GetBag().Num(),BeforeItems+1);
            int32 Left=0; for(TActorIterator<ARPGRubyGem> It(W);It;++It) { Left+=It->IsFor(B); }
            Test->TestEqual(TEXT("Nothing of B's left lying about after the save"),Left,0);
            UGameplayStatics::DeleteGameInSlot(Slot+TEXT("_B"),0);
            const int32 OtherMoney=B->GetRubies();
            while(B->GetBag().Num()<60) { B->AddLoot(FRPGPrototypeItem::MakeBadge(ERPGItemKind::LifeBadge)); }
            B->SetActorLocation(Second->GetActorLocation()+FVector(0,160,120));
            Test->TestFalse(TEXT("Full bag preserves closed chest"),Second->Open(B));
            Test->TestFalse(TEXT("Full bag cannot consume claim"),Second->IsOpenedBy(B));
            Test->TestEqual(TEXT("Full bag cannot farm ruby-only payouts"),B->GetRubies(),OtherMoney);
            B->SetActorLocation(First->GetActorLocation()+FVector(0,-160,120));
            Stage=3; Next=W->GetTimeSeconds()+.7; return false;
        }
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA/treasure-open-split.png")),true,false);
        UGameplayStatics::DeleteGameInSlot(Slot,0); return true;
    }
private:
    FAutomationTestBase* Test; FString Slot; int32 Stage=0;
    double Started=FPlatformTime::Seconds(),Next=0;
    TWeakObjectPtr<ARPGTreasureChest> First,Second;
    int32 Money=0,Expected=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTreasureTest,"ChaosImpact.RPGPrototype.Treasure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGTreasureTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGTreasureCommand(this)); return true; }
#endif
