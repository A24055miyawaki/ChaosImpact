#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGSnowField.h"
#include "RPGPrototypeHUD.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGLocalSessionCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGLocalSessionCommand(FAutomationTestBase* InTest):Test(InTest)
    { Slot=TEXT("CI_RPG_QA_")+FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>100) { Test->AddError(FString::Printf(TEXT("Local RPG timeout stage %d"),Stage)); CleanupSave(); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>();
        auto* First=Cast<ARPGPrototypePlayerController>(W->GetFirstPlayerController());
        if(!Mode || !First || !First->IsGameplayActive()) { return false; }
        auto* P1=Cast<ARPGPrototypeCharacter>(First->GetPawn());
        if(!P1 || !P1->GetPrototypeHUD()) { return false; }
        if(Stage==2 && W->GetTimeSeconds()<Next)
        { if(auto* Moving=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1))) { Moving->AddMovementInput(FVector(0,1,0)); } }
        if(W->GetTimeSeconds()<Next) { return false; }
        auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        if(Stage==0)
        {
            OriginalMini=P1->UsesMiniMenu();
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; It->DropChance=1; }
            // Camps out in the field are only made when someone comes near: one is made now, as this test's enemies.
            if(auto* Nearest=[&]() { ARPGPrototypeSpawnPoint* Best=nullptr; for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It)
                { if(It->bEncounterGroup && (!Best || It->GetActorLocation().Size2D()<Best->GetActorLocation().Size2D())) { Best=*It; } } return Best; }())
            {
                const FVector Safe=P1->GetActorLocation();
                P1->SetActorLocation(Nearest->GetActorLocation()+FVector(0,0,0),false,nullptr,ETeleportType::TeleportPhysics);
                Nearest->bEnabled=true; Nearest->TickEncounter(Mode,W->GetTimeSeconds()); Nearest->bEnabled=false;
                P1->SetActorLocation(Safe,false,nullptr,ETeleportType::TeleportPhysics);
            }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); if(!Cast<ARPGPrototypeBoss>(*It) && !Cast<ARPGSimaeBody>(*It)) { Enemies.Add(*It); } } }
            if(!Test->TestTrue(TEXT("Three test enemies exist"),Enemies.Num()>=3)) { return true; }
            Mode->SetLocalPlayerCount(4); Stage=1; Next=W->GetTimeSeconds()+.7; return false;
        }
        auto* GI=W->GetGameInstance();
        if(Stage<5 && GI->GetLocalPlayers().Num()!=4) { Test->AddError(TEXT("Four LocalPlayers were not created")); CleanupSave(); return true; }
        auto* PC2=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,1));
        auto* P2=PC2?Cast<ARPGPrototypeCharacter>(PC2->GetPawn()):nullptr;
        auto* P3=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,2));
        if(Stage<8 && (!P2 || !P3 || !P2->GetPrototypeHUD())) { return false; }
        switch(Stage)
        {
        case 1:
        {
            for(auto* L:GI->GetLocalPlayers())
            { Test->TestTrue(TEXT("Four-way viewport has half width and height"),L->Size.Equals(FVector2D(.5,.5),.01)); }
            Test->TestTrue(TEXT("Native RPG controller used for P2"),PC2!=nullptr);
            Test->TestTrue(TEXT("Split players always use mini menus"),P1->UsesMiniMenu() && P2->UsesMiniMenu());
            Test->TestNotEqual(TEXT("Test save slots are per-player"),P1->GetTestSaveSlot(),P2->GetTestSaveSlot());
            P1->SetPrototypeAutomatedInput(true); P2->SetPrototypeAutomatedInput(true); P3->SetPrototypeAutomatedInput(true);
            P1->SetActorLocation(FVector(-2650,0,120)); P2->SetActorLocation(FVector(-2650,180,120)); P3->SetActorLocation(FVector(-2650,-180,120));
            First->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::Tab,IE_Pressed,FPlatformTime::Cycles64()));
            Test->TestTrue(TEXT("Tab opens only P1's menu"),P1->IsBagOpen() && !P2->IsBagOpen());
            Test->TestTrue(TEXT("Menu freezes owner but not teammate"),P1->IsTrainingMenuFrozen() && !P2->IsTrainingMenuFrozen());
            Test->TestFalse(TEXT("Mini menu does not pause world"),W->IsPaused());
            Test->TestFalse(TEXT("Mini HUD cannot steal another viewport's input"),P1->GetPrototypeHUD()->IsVisible() && P1->GetPrototypeHUD()->GetVisibility()==ESlateVisibility::Visible);
            P2Start=P2->GetActorLocation(); P2->GetCharacterMovement()->Velocity=FVector(0,180,0);
            Enemies[0]->TakeDamage(1,FDamageEvent(),First,P1);
            WorldTime=W->GetTimeSeconds(); Shot(TEXT("rpg-four-mini.png")); Stage=2; Next=W->GetTimeSeconds()+.8; return false;
        }
        case 2:
        {
            Test->TestEqual(TEXT("Mini rise animation finishes"),P1->GetPrototypeHUD()->GetMiniRevealAlpha(),1.0f);
            Shot(TEXT("rpg-four-mini.png"));
            Test->TestTrue(TEXT("World time continues with personal bag"),W->GetTimeSeconds()>WorldTime+.5);
            Test->TestFalse(TEXT("Other player was not frozen"),P2->IsTrainingMenuFrozen());
            Test->TestTrue(TEXT("Other player can still move"),FVector::DistSquared2D(P2Start,P2->GetActorLocation())>1);
            Enemies[0]->TakeDamage(10000,FDamageEvent(),PC2,P2);
            int32 N1=0,N2=0,N3=0;
            for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It)
            {
                N1+=It->IsVisibleTo(P1); N2+=It->IsVisibleTo(P2); N3+=It->IsVisibleTo(P3);
                Test->TestTrue(TEXT("Private mesh uses owner-only visibility"),It->Mesh->bOnlyOwnerSee);
                Test->TestEqual(TEXT("Private drop belongs to its recipient pawn"),It->GetOwner(),static_cast<AActor*>(It->Recipient));
                if(It->Recipient==P2) { PersonalDrop=*It; }
            }
            Test->TestEqual(TEXT("Assist receives own independently rolled drop"),N1,1);
            Test->TestEqual(TEXT("Killer receives own independently rolled drop"),N2,1);
            Test->TestEqual(TEXT("Uninvolved player receives no drop"),N3,0);
            if(PersonalDrop.IsValid())
            {
                P3->SetActorLocation(PersonalDrop->GetActorLocation()+FVector(0,0,75));
                Test->TestFalse(TEXT("Other player cannot collect private loot"),PersonalDrop->Collect(P3));
                Test->TestNull(TEXT("Other player's nearby-loot search excludes private drops"),P3->FindNearbyLoot());
            }
            Test->TestEqual(TEXT("Assist also gets one XP"),P1->GetXP(),1);
            Test->TestEqual(TEXT("Killer gets one XP"),P2->GetXP(),1);
            Test->TestEqual(TEXT("Uninvolved player gets no XP"),P3->GetXP(),0);
            P1->SetBagOpen(false);
            PC2->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(1),EKeys::Gamepad_Special_Left,IE_Pressed,FPlatformTime::Cycles64()));
            Test->TestTrue(TEXT("P2 pad opens P2 bag, not P1"),P2->IsBagOpen() && !P1->IsBagOpen());
            P2->RPGTestKit();
            for(int32 I=3;I<P2->GetBag().Num();++I)
            {
                const auto& Item=P2->GetBag()[I];
                if(Item.Kind!=ERPGItemKind::Ball) { P2->EquipBagItem(I,1); }
                if(Item.Kind==ERPGItemKind::Ball && Item.Type==EChaosImpactBallType::Thunder) { P2->EquipBagItem(I,1); }
            }
            auto Tuned=FRPGPrototypeItem::Make(EChaosImpactBallType::Thunder,2,ERPGPrototypeRarity::Legend);
            Tuned.Enhancement=11; Tuned.TuneOfferGeneration=7;
            const auto& Pool=RPGEquipment::Tunes(Tuned.Type);
            Tuned.Tunes={{Pool[0].Key,Pool[0].Min,3},{Pool[1].Key,Pool[1].Max,2},{Pool.Last().Key,Pool.Last().Min,1}};
            const int32 TunedIndex=P2->GetBag().Num(); P2->AddLoot(Tuned); P2->EquipBagItem(TunedIndex,1);
            for(int32 I=0;I<20;++I) { P2->AwardKillXP(); }
            SavedLevel=P2->GetRPGLevel(); SavedXP=P2->GetXP();
            P2->SetBagOpen(false); P2->SelectAttack(1); P2->ConsumeAttack(); P2->RequestAIDash(FVector::ForwardVector); P2->SetBagOpen(true); P2->BagPage=3;
            SavedBag=P2->GetBag().Num(); SavedMoney=P2->GetRubies(); SavedHP=P2->GetHealth(); SavedID=P2->GetAttack(1).Id;
            const float SavedStamina=P2->GetStamina();
            SavedCooldown=P2->GetCooldownLeft(1); P1Money=P1->GetRubies();
            Test->TestTrue(TEXT("Save own equipment and rolled traits"),P2->SaveTestProgress(Slot));
            P2->AddRubies(999); P2->AddLoot(FRPGPrototypeItem::Make(EChaosImpactBallType::Wind));
            P2->EquipBagItem(1,1);
            Test->TestTrue(TEXT("Load own equipment and rolled traits"),P2->LoadTestProgress(Slot));
            Test->TestEqual(TEXT("Bag restored"),P2->GetBag().Num(),SavedBag);
            Test->TestEqual(TEXT("Rubies restored"),P2->GetRubies(),SavedMoney);
            Test->TestEqual(TEXT("Equipped special GUID restored"),P2->GetAttack(1).Id,SavedID);
            Test->TestEqual(TEXT("Player level restored"),P2->GetRPGLevel(),SavedLevel);
            Test->TestEqual(TEXT("XP restored"),P2->GetXP(),SavedXP);
            Test->TestEqual(TEXT("Selected attack restored"),P2->GetSelectedAttack(),1);
            Test->TestEqual(TEXT("Stamina restored"),P2->GetStamina(),SavedStamina);
            Test->TestEqual(TEXT("Enhancement beyond +10 restored"),P2->GetAttack(1).Enhancement,11);
            Test->TestEqual(TEXT("Three tunes restored"),P2->GetAttack(1).Tunes.Num(),3);
            if(P2->GetAttack(1).Tunes.Num()==3)
            { Test->TestEqual(TEXT("Tune random roll restored"),P2->GetAttack(1).Tunes[0].Roll,Tuned.Tunes[0].Roll); Test->TestEqual(TEXT("Tune rank restored"),P2->GetAttack(1).Tunes[0].Rank,3); }
            Test->TestEqual(TEXT("Next tune candidate generation restored"),P2->GetAttack(1).TuneOfferGeneration,7);
            Test->TestEqual(TEXT("HP restored"),P2->GetHealth(),SavedHP);
            Test->TestTrue(TEXT("Cooldown restored, not reset by loading"),FMath::Abs(P2->GetCooldownLeft(1)-SavedCooldown)<.1);
            Test->TestEqual(TEXT("Teammate's rubies unchanged by load"),P1->GetRubies(),P1Money);
            Test->TestTrue(TEXT("Load did not rewind world time"),W->GetTimeSeconds()>WorldTime);
            auto* Broken=Cast<URPGPrototypeSave>(UGameplayStatics::LoadGameFromSlot(Slot,0));
            if(Broken) { Broken->Version=999; UGameplayStatics::SaveGameToSlot(Broken,Slot,0); }
            Test->TestFalse(TEXT("Unsupported save rejected"),P2->LoadTestProgress(Slot));
            Test->TestEqual(TEXT("Rejected save leaves current bag intact"),P2->GetBag().Num(),SavedBag);
            Test->TestEqual(TEXT("Rejected save leaves current equipment intact"),P2->GetAttack(1).Id,SavedID);
            Mode->AssistSeconds=.1f; Enemies[1]->TakeDamage(1,FDamageEvent(),PC2,P2);
            Stage=3; Next=W->GetTimeSeconds()+.8; return false;
        }
        case 3:
        {
            const int32 XP2=P2->GetXP(); const int32 Before=LootFor(W,P2);
            Enemies[1]->TakeDamage(10000,FDamageEvent(),First,P1);
            Test->TestEqual(TEXT("Expired assist gets no new drop"),LootFor(W,P2),Before);
            Test->TestEqual(TEXT("Expired assist gets no XP"),P2->GetXP(),XP2);
            Mode->AssistSeconds=20;
            Mode->LootLifetimeSeconds=1;
            Expiring=Mode->SpawnLoot(P1->GetActorLocation()+FVector(100,0,0),FRPGPrototypeItem::Make(EChaosImpactBallType::Fire),P1);
            Mode->LootLifetimeSeconds=60;
            P2->SetBagOpen(false); P1->SetBagOpen(true); P1->BagPage=3;
            Test->TestEqual(TEXT("Mini settings can be clicked"),P1->GetPrototypeHUD()->MiniActionAt(FVector2D(220,128)),400);
            Shot(TEXT("rpg-four-settings.png")); Stage=4; Next=W->GetTimeSeconds()+1.2; return false;
        }
        case 4:
            Test->TestFalse(TEXT("Uncollected drops expire even while menus are open"),Expiring.IsValid());
            Shot(TEXT("rpg-four-settings.png")); Mode->SetLocalPlayerCount(3);
            Stage=5; Next=W->GetTimeSeconds()+.4; return false;
        case 5:
            Test->TestEqual(TEXT("Three-player layout supported"),Mode->GetLocalPlayerCount(),3);
            Test->TestTrue(TEXT("Three-player top viewport spans full width"),GI->GetLocalPlayers()[0]->Size.Equals(FVector2D(1,.5),.01));
            Mode->SetLocalPlayerCount(2); P1->BagPage=0; P1->NoticeUntil=0;
            Stage=8; Next=W->GetTimeSeconds()+.4; return false;
        case 8:
        {
            Test->TestEqual(TEXT("Two local players remain"),Mode->GetLocalPlayerCount(),2);
            for(auto* L:GI->GetLocalPlayers()) { Test->TestTrue(TEXT("Two-player vertical split"),L->Size.Equals(FVector2D(.5,1),.01)); }
            P1->NoticeUntil=0; Shot(TEXT("rpg-two-mini.png"));
            Stage=9; Next=W->GetTimeSeconds()+.4; return false;
        }
        case 9:
            P1->SetBagOpen(false); Mode->SetLocalPlayerCount(1);
            Test->TestEqual(TEXT("Can return to single-player"),Mode->GetLocalPlayerCount(),1);
            if(P1->UsesMiniMenu()) { P1->ToggleMenuStyle(); }
            P1->SetBagOpen(true); P1->BagPage=3; P1->NoticeUntil=0;
            Test->TestFalse(TEXT("Solo full-screen menu selectable"),P1->UsesMiniMenu());
            Shot(TEXT("rpg-solo-settings.png")); Stage=10; Next=W->GetTimeSeconds()+.4; return false;
        case 10:
            P1->ToggleMenuStyle(); Test->TestTrue(TEXT("Solo mini menu selectable"),P1->UsesMiniMenu());
            P1->BagPage=0; P1->NoticeUntil=0; Shot(TEXT("rpg-solo-mini.png"));
            Stage=11; Next=W->GetTimeSeconds()+.4; return false;
        case 11:
            Shot(TEXT("rpg-solo-mini.png")); P1->SetBagOpen(false);
            Test->TestTrue(TEXT("Close retains panel for exit animation"),P1->GetPrototypeHUD()->GetMiniRevealAlpha()>0);
            Stage=12; Next=W->GetTimeSeconds()+.35; return false;
        case 12:
            Test->TestEqual(TEXT("Mini pull-down animation finishes"),P1->GetPrototypeHUD()->GetMiniRevealAlpha(),0.0f);
            if(P1->UsesMiniMenu()!=OriginalMini) { P1->ToggleMenuStyle(); }
            CleanupSave(); Mode->ResetPrototype(); return true;
        }
        return false;
    }
private:
    static int32 LootFor(UWorld* W,ARPGPrototypeCharacter* P)
    { int32 N=0; for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It) { N+=It->IsVisibleTo(P); } return N; }
    void CleanupSave() { UGameplayStatics::DeleteGameInSlot(Slot,0); }
    FAutomationTestBase* Test;
    FString Slot;
    double Started=FPlatformTime::Seconds(),Next=0,WorldTime=0;
    int32 Stage=0,SavedBag=0,SavedMoney=0,P1Money=0,SavedLevel=0,SavedXP=0;
    float SavedHP=0,SavedCooldown=0;
    FGuid SavedID;
    bool OriginalMini=false;
    FVector P2Start;
    TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> Enemies;
    TWeakObjectPtr<ARPGPrototypeLoot> PersonalDrop,Expiring;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLocalSessionTest,"ChaosImpact.RPGPrototype.LocalSession",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGLocalSessionTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FRPGLocalSessionCommand(this)); return true; }
#endif
