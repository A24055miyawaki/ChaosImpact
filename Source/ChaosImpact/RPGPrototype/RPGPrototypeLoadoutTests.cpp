#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototypeTestLoadout.h"
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "RPGTuning.h"
#include "RPGAdventure.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGLoadoutCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGLoadoutCommand(FAutomationTestBase* T):Test(T) { Slot=TEXT("CI_RPG_Loadout_QA_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Loadout timeout %d"),Stage)); UGameplayStatics::DeleteGameInSlot(Slot,0); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        if(Stage==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0)); auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>(); auto* World=ARPGAdventureWorld::Find(W);
        if(!P || !Mode || !World || !P->GetPrototypeHUD() || !Cast<AChaosImpactPlayerController>(P->GetController())->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        auto* HUD=P->GetPrototypeHUD(); const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        switch(Stage)
        {
        case 1:
        {
            OriginalMini=P->UsesMiniMenu(); if(OriginalMini) { P->ToggleMenuStyle(); }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            for(TActorIterator<ARPGPrototypeBoss> It(W);It;++It) { Boss=*It; }
            if(!Test->TestTrue(TEXT("Far-field Maguro is placed"),Boss.IsValid())) { return true; }
            Test->TestTrue(TEXT("Maguro is in the ruins, not colony"),Boss->GetActorLocation().X>12500 && !World->IsInColony(Boss->GetActorLocation()));
            Test->TestEqual(TEXT("Maguro starts at level 10"),Boss->GetRPGLevel(),10);
            Test->TestFalse(TEXT("Maguro does not engage from colony"),Boss->IsBossActive());
            for(TActorIterator<APointLight> It(W);It;++It) { if(It->GetFolderPath().ToString()==TEXT("RPGAdventure/Lighting")) { Test->TestTrue(TEXT("Decorative lights no longer overexpose colony"),It->GetLightComponent()->Intensity<=45.1f); } }
            bool Bloom=false; for(TActorIterator<APostProcessVolume> It(W);It;++It) { if(It->bUnbound && It->Settings.bOverride_BloomIntensity && It->Settings.BloomIntensity<=.051f) { Bloom=true; } }
            Test->TestTrue(TEXT("Adventure has restrained bloom"),Bloom);
            for(int32 S=0;S<4;++S) { Test->TestFalse(TEXT("No free starter gear"),P->GetGear(S).IsValid()); }
            RPGPrototypeTest::SupplySpecials(P);
            P->SetBagOpen(true);
            for(int32 S=0;S<4;++S)
            { const int32 Index=P->GetBag().Num(); P->AddLoot(FRPGPrototypeItem::MakeGear(static_cast<ERPGItemKind>(5+S),1,ERPGPrototypeRarity::Normal,0)); P->EquipBagItem(Index,1); }
            P->SetBagOpen(false);
            Shot(TEXT("loadout-colony-lighting.png")); Stage=2; Next=Now+.2; return false;
        }
        case 2:
        {
            P->SetBagOpen(true);
            for(int32 S=0;S<3;++S) { auto I=FRPGPrototypeItem::MakeBadge(static_cast<ERPGItemKind>(S+1),1,ERPGPrototypeRarity::Rare,0); P->AddLoot(I); P->EquipBagItem(P->GetBag().Num()-1,1); }
            for(int32 S=0;S<10;++S) { P->SelectEquipment(S); Test->TestEqual(TEXT("All ten slots selectable at level 1"),P->EquipmentSelection,S); }
            P->AddLoot(FRPGPrototypeItem::MakeGear(ERPGItemKind::Glove,1,ERPGPrototypeRarity::Rare,2)); NewGlove=P->GetBag().Num()-1;
            P->SelectEquipment(3); Test->TestEqual(TEXT("Click maps to glove slot"),HUD->BagActionAt({80,350},1280,720),703);
            P->NoticeUntil=0; Stage=3; Next=Now+.4; return false;
        }
        case 3:
            Shot(TEXT("loadout-equipment-full.png")); Stage=30; Next=Now+.2; return false;
        case 30:
        {
            HUD->ActivateBagAction(710); Test->TestTrue(TEXT("Change opens only compatible candidates"),P->bChoosingEquipment);
            for(int32 I:P->FilteredBag()) { Test->TestEqual(TEXT("No other equipment type mixed into glove picker"),P->GetBag()[I].Kind,ERPGItemKind::Glove); }
            P->BagSelection=NewGlove; P->NoticeUntil=0; Stage=4; Next=Now+.3; return false;
        }
        case 4:
            Shot(TEXT("loadout-change-full.png")); Stage=40; Next=Now+.2; return false;
        case 40:
        {
            const float Before=P->GetDisplayedPower(P->GetAttack(0)); HUD->ActivateBagAction(711);
            Test->TestEqual(TEXT("Confirm equips the selected glove"),P->GetGear(0).Id,P->GetBag()[NewGlove].Id);
            Test->TestTrue(TEXT("Attack power display includes passive gear bonus"),P->GetDisplayedPower(P->GetAttack(0))>Before);
            const float Display=P->GetDisplayedPower(P->GetAttack(0)); P->SelectAttack(0);
            Test->TestTrue(TEXT("Baseline displayed power matches attack power before charge/conditional effects"),FMath::IsNearlyEqual(P->ConsumeAttack(),Display,.001f));
            HUD->ActivateBagAction(14); HUD->ActivateBagAction(721); Test->TestEqual(TEXT("Bag and equipment are separate pages"),P->BagPage,4);
            for(int32 I:P->FilteredBag()) { Test->TestTrue(TEXT("Gear bag never mixes balls or badges"),RPGEquipment::IsGear(P->GetBag()[I].Kind)); }
            P->NoticeUntil=0; Stage=5; Next=Now+.3; return false;
        }
        case 5:
            Shot(TEXT("loadout-bag-gears.png")); Stage=50; Next=Now+.2; return false;
        case 50:
        {
            for(TActorIterator<ARPGPrototypeMerchant> It(W);It;++It) { P->SetBagOpen(false); P->SetActorLocation(It->GetActorLocation()+FVector(210,0,110)); Test->TestTrue(TEXT("Merchant remains available"),P->OpenMerchant(*It)); break; }
            P->AddRubies(1000); const float Before=P->GetDisplayedPower(P->GetAttack(0));
            Test->TestTrue(TEXT("Normal ball can be upgraded"),P->UpgradeItem(0)); Test->TestTrue(TEXT("Normal-ball upgrade raises visible power"),P->GetDisplayedPower(P->GetAttack(0))>Before);
            Test->TestTrue(TEXT("Gear save succeeds"),P->SaveTestProgress(Slot)); const FGuid GearID=P->GetGear(0).Id;
            P->EquipBagItem(3,1); Test->TestTrue(TEXT("Gear load succeeds"),P->LoadTestProgress(Slot)); Test->TestEqual(TEXT("Gear selection restored"),P->GetGear(0).Id,GearID);
            auto* Save=Cast<URPGPrototypeSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
            Save->GearIds[0]=Save->GearIds[1]; UGameplayStatics::SaveGameToSlot(Save,Slot,0);
            Test->TestFalse(TEXT("Wrong gear type in slot rejected atomically"),P->LoadTestProgress(Slot)); Test->TestEqual(TEXT("Rejected save preserves gear"),P->GetGear(0).Id,GearID);
            Save->Version=1; Save->GearIds.Empty(); UGameplayStatics::SaveGameToSlot(Save,Slot,0);
            Test->TestTrue(TEXT("Old version-1 saves without gear migrate"),P->LoadTestProgress(Slot)); Test->TestFalse(TEXT("Old saves do not invent or duplicate equipment"),P->GetGear(0).IsValid());
            P->EquipBagItem(NewGlove,1); P->SwitchInventoryPage(0); P->SelectEquipment(0); P->NoticeUntil=0; Stage=6; Next=Now+.4; return false;
        }
        case 6:
            Shot(TEXT("loadout-normal-power.png")); Stage=61; Next=Now+.2; return false;
        case 61:
        {
            // Bag usability: NEW marks, order, lock, bulk salvage and click regions of the full menu.
            P->SwitchInventoryPage(ERPGBagPage::Items); P->BagFilter=ERPGBagFilter::Ball;
            const int32 Unseen=P->GetUnseenCount();
            const int32 NormalIndex=P->GetBag().Num(); P->AddLoot(FRPGPrototypeItem::Make(EChaosImpactBallType::Wind,1,ERPGPrototypeRarity::Normal));
            const int32 RareIndex=P->GetBag().Num(); P->AddLoot(FRPGPrototypeItem::Make(EChaosImpactBallType::Beam,1,ERPGPrototypeRarity::Rare));
            const int32 LegendIndex=P->GetBag().Num(); P->AddLoot(FRPGPrototypeItem::Make(EChaosImpactBallType::Snow,2,ERPGPrototypeRarity::Legend));
            Test->TestEqual(TEXT("Picked-up items are NEW"),P->GetUnseenCount(),Unseen+3);
            Test->TestTrue(TEXT("Category tab knows its NEW items"),P->CountInFilter(ERPGBagFilter::Ball,true)>=3);
            P->BagSort=ERPGBagSort::Rarity; auto List=P->FilteredBag();
            Test->TestEqual(TEXT("Rarity order puts the legend first"),P->GetBag()[List[0]].Rarity,ERPGPrototypeRarity::Legend);
            P->BagSort=ERPGBagSort::Newest; List=P->FilteredBag(); Test->TestEqual(TEXT("Newest order puts the last pickup first"),List[0],LegendIndex);
            P->BagSort=ERPGBagSort::Strength; List=P->FilteredBag(); bool Ordered=true;
            for(int32 I=1;I<List.Num();++I) { Ordered&=P->GetDisplayedPower(P->GetBag()[List[I-1]])>=P->GetDisplayedPower(P->GetBag()[List[I]])-.01f; }
            Test->TestTrue(TEXT("Strength order is by displayed power"),Ordered);
            P->BagSort=ERPGBagSort::Acquired;
            Test->TestTrue(TEXT("Lock toggles"),P->ToggleItemLock(NormalIndex) && P->GetBag()[NormalIndex].bLocked);
            Test->TestFalse(TEXT("Locked item cannot be salvaged"),P->SalvageItem(NormalIndex)); Test->TestEqual(TEXT("No salvage dialog for a locked item"),P->PendingSalvage,INDEX_NONE);
            const auto Before=P->BulkSalvageCandidates();
            Test->TestFalse(TEXT("Unseen items are kept out of bulk salvage"),Before.Contains(RareIndex));
            P->MarkSeen(NormalIndex); P->MarkSeen(RareIndex); P->MarkSeen(LegendIndex);
            const auto Candidates=P->BulkSalvageCandidates();
            Test->TestTrue(TEXT("Bulk salvage takes the seen rare"),Candidates.Contains(RareIndex));
            Test->TestFalse(TEXT("Bulk salvage keeps locked items"),Candidates.Contains(NormalIndex));
            Test->TestFalse(TEXT("Bulk salvage keeps legends"),Candidates.Contains(LegendIndex));
            for(int32 Index:Candidates) { Test->TestFalse(TEXT("Bulk salvage keeps equipment"),P->IsEquipped(P->GetBag()[Index].Id)); }
            int32 Gain=0; for(int32 Index:Candidates) { Gain+=P->GetBag()[Index].SalvageValue(); }
            const FGuid LegendId=P->GetBag()[LegendIndex].Id, LockedId=P->GetBag()[NormalIndex].Id; const int32 Money=P->GetRubies(),Count=P->GetBag().Num();
            Test->TestTrue(TEXT("Bulk salvage asks first"),P->RequestBulkSalvage() && P->HasConfirmation());
            HUD->ActivateBagAction(RPGMenuAction::DialogOk);
            Test->TestEqual(TEXT("Bulk salvage removes exactly the candidates"),P->GetBag().Num(),Count-Candidates.Num());
            Test->TestEqual(TEXT("Bulk salvage pays their total"),P->GetRubies(),Money+Gain);
            Test->TestTrue(TEXT("Locked and legend items remain"),P->GetBag().ContainsByPredicate([&](const FRPGPrototypeItem& I){return I.Id==LegendId;}) && P->GetBag().ContainsByPredicate([&](const FRPGPrototypeItem& I){return I.Id==LockedId;}));
            // Painted controls are the click targets (one layout for both).
            Test->TestEqual(TEXT("Sort button click"),HUD->BagActionAt({100,160},1280,720),static_cast<int32>(RPGMenuAction::Sort));
            Test->TestEqual(TEXT("First list row click"),HUD->BagActionAt({120,210},1280,720),static_cast<int32>(RPGMenuAction::RowBase));
            Test->TestEqual(TEXT("Category tab click"),HUD->BagActionAt({100,115},1280,720),static_cast<int32>(RPGMenuAction::FilterBase));
            P->BagSelection=P->GetBag().IndexOfByPredicate([&](const FRPGPrototypeItem& I){return I.Id==LegendId;}); P->BagSort=ERPGBagSort::Rarity; P->NoticeUntil=0;
            Stage=62; Next=Now+.4; return false;
        }
        case 62:
            Shot(TEXT("bag-full-rarity.png")); Stage=63; Next=Now+.3; return false;
        case 63:
            P->SwitchInventoryPage(ERPGBagPage::Settings); P->NoticeUntil=0; Stage=64; Next=Now+.3; return false;
        case 64:
            Shot(TEXT("bag-full-settings.png")); Stage=65; Next=Now+.3; return false;
        case 65:
            P->BagSort=ERPGBagSort::Acquired; P->SwitchInventoryPage(0); P->SelectEquipment(0); Stage=60; Next=Now+.2; return false;
        case 60:
            P->SetBagOpen(false); Mode->SetLocalPlayerCount(2); P->SetBagOpen(true); P->SelectEquipment(3); P->NoticeUntil=0; Stage=7; Next=Now+.5; return false;
        case 7:
        {
            Test->TestEqual(TEXT("Mini equipment hitboxes match painted slots"),HUD->MiniActionAt({35,230}),703);
            auto* Other=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1)); Test->TestTrue(TEXT("Only owner is frozen"),Other && !Other->IsBagOpen() && !Other->IsTrainingMenuFrozen() && !W->IsPaused());
            Shot(TEXT("loadout-equipment-split.png")); Stage=70; Next=Now+.2; return false;
        }
        case 70:
            P->BeginEquipmentChange(); P->BagSelection=NewGlove; P->NoticeUntil=0; Stage=8; Next=Now+.3; return false;
        case 8:
            Shot(TEXT("loadout-change-split.png")); Stage=80; Next=Now+.2; return false;
        case 80:
            P->SetBagOpen(false); Mode->SetLocalPlayerCount(1); Boss->SetTrainingMenuFrozen(false); P->SetActorLocation(Boss->GetActorLocation()+FVector(-850,0,0)); Stage=9; Next=Now+1.2; return false;
        case 9:
            Test->TestTrue(TEXT("Placed Maguro engages near ruins"),Boss->IsBossActive()); Shot(TEXT("loadout-frontier-boss.png")); Stage=90; Next=Now+.2; return false;
        case 90:
            if(P->UsesMiniMenu()!=OriginalMini) { P->ToggleMenuStyle(); } UGameplayStatics::DeleteGameInSlot(Slot,0); return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds(),Next=0; int32 Stage=0,NewGlove=0; bool OriginalMini=false; FString Slot; TWeakObjectPtr<ARPGPrototypeBoss> Boss;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLoadoutTest,"ChaosImpact.RPGPrototype.LoadoutFrontier",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGLoadoutTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGLoadoutCommand(this)); return true; }
#endif
