#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototypeTestLoadout.h"
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGBossPlayCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGBossPlayCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150) { Test->AddError(FString::Printf(TEXT("Boss test timeout stage %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController());
        auto* P=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
        if(!PC || !PC->GetPawn()) { return false; }
        if(!W->GetAuthGameMode<ARPGPrototypeGameMode>() || !P) { if(!Travel) { Travel=PC->OpenExplorationMap(); } return false; }
        if(!PC->IsGameplayActive() || !P->GetPrototypeHUD()) { return false; }
        const double Now=W->GetTimeSeconds();
        auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        if(Stage==0)
        {
            int32 Count=0; for(TActorIterator<ARPGPrototypeBoss> It(W);It;++It) { Boss=*It; ++Count; }
            if(!Test->TestEqual(TEXT("Exactly one placed boss"),Count,1)) { return true; }
            Test->TestTrue(TEXT("Boss is at stage center"),Boss->GetArenaHome().SizeSquared2D()<100);
            Test->TestEqual(TEXT("Boss starts at Lv10"),Boss->GetRPGLevel(),10);
            Test->TestEqual(TEXT("Lv10 boss has strengthened HP"),Boss->GetMaxHealth(),1800.0f);
            Test->TestEqual(TEXT("Lv10 boss has strengthened attack power"),Boss->GetAttack(0).Power,38.0f);
            auto Level10Weapon=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal,10,ERPGPrototypeRarity::Rare);
            Level10Weapon.Enhancement=5;
            const float StarterHits=Boss->GetMaxHealth()/P->GetAttack(0).EffectivePower();
            const float GearedHits=Boss->GetMaxHealth()/Level10Weapon.EffectivePower();
            Test->TestTrue(TEXT("Level1 starter weapon needs at least 90 ordinary hits"),StarterHits>=90);
            Test->TestTrue(TEXT("Level10 +5 rare weapon cuts required hits to under 45"),GearedHits<45 && GearedHits<StarterHits*.5f);
            const auto* Puppet=Boss->FindComponentByClass<UChaosImpactPuppetComponent>();
            Test->TestTrue(TEXT("Boss really uses authored Maguro model"),Puppet && Puppet->GetCharacterIndex()==5 && Puppet->IsReady() && Puppet->HasAuthoredAnimations());
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt() && *It!=Boss.Get()) { It->SetTrainingMenuFrozen(true); } }
            Boss->SetTrainingMenuFrozen(false); Boss->ResetEncounter();
            P->SetPrototypeAutomatedInput(true); P->SetActorLocation(FVector(-2650,0,120));
            RPGPrototypeTest::SupplySpecials(P);
            P->SetBagOpen(true);
            auto* HUD=P->GetPrototypeHUD();
            Test->TestFalse(TEXT("Empty badge slot safely reports no equipment"),P->InspectEquipmentSlot(2));
            for(int32 K=1;K<=3;++K)
            { P->AddLoot(FRPGPrototypeItem::MakeBadge(static_cast<ERPGItemKind>(K),2,ERPGPrototypeRarity::Unique,0)); P->EquipBagItem(P->GetBag().Num()-1,1); }
            for(int32 S=0;S<5;++S)
            {
                P->BagFilter=2;
                const int32 Slot=S<2?S+1:S+5; const int32 Group=Slot<3?0:2,Col=Slot-(Group==0?0:7);
                const int32 Action=HUD->BagActionAt(FVector2D(32+Col*(1280*.56f+12)/3+70,153+Group*155+50),1280,720);
                Test->TestEqual(TEXT("Each equipped panel has its own click target"),Action,700+Slot);
                HUD->ActivateBagAction(Action);
                const auto& E=S<2?P->GetAttack(S+1):P->GetBadge(S-2);
                Test->TestEqual(TEXT("Click selects actual equipped item for details"),P->GetBag()[P->BagSelection].Id,E.Id);
                Test->TestEqual(TEXT("Inspection selects correct equipment slot"),P->EquipmentSelection,Slot);
            }
            P->NoticeUntil=0; Shot(TEXT("boss-equipment-click.png")); Stage=1; Next=Now+.5; return false;
        }
        if(!Boss.IsValid()) { Test->AddError(TEXT("Boss unexpectedly removed")); return true; }
        if(Stage==1)
        {
            if(Now<Next) { return false; }
            P->SetBagOpen(false); Next=Now+2; Stage=2; return false;
        }
        if(Stage==2)
        {
            if(Now<Next) { return false; }
            Test->TestEqual(TEXT("Far-away player does not start boss AI"),Boss->GetBossState(),ERPGBossState::Idle);
            int32 Shots=0; for(TActorIterator<ARPGPrototypeBall> It(W);It;++It) { if(It->GetInstigator()==Boss.Get() && It->IsFlyingOnServer()) { ++Shots; } }
            Test->TestEqual(TEXT("No attacks from boss at spawn"),Shots,0);
            P->SetActorLocation(FVector(-800,0,160)); Stage=3; return false;
        }
        if(Stage==3)
        {
            P->RestoreHP(100);
            const auto State=Boss->GetBossState();
            if(State==ERPGBossState::Volley)
            {
                for(TActorIterator<ARPGPrototypeBall> It(W);It;++It)
                { if(It->GetInstigator()==Boss.Get() && It->IsFlyingOnServer()) { SeenProjectiles.Add(It->GetUniqueID()); } }
            }
            if(State==ERPGBossState::ChargeTell && !SawTell)
            { SawTell=true; Shot(TEXT("boss-charge-tell.png")); }
            if(State==ERPGBossState::Recovery && !SawRecovery)
            {
                SawRecovery=true;
                Test->TestTrue(TEXT("Boss launches all three volley projectiles"),SeenProjectiles.Num()>=3);
                const float HP=Boss->GetHealth();
                Test->TestEqual(TEXT("Post-charge opening takes 1.5x damage"),Boss->TakeDamage(10,FDamageEvent(),PC,P),15.0f);
                Test->TestEqual(TEXT("No wall-crash self damage"),HP,Boss->GetMaxHealth());
                Boss->TakeDamage((Boss->GetHealth()-Boss->GetMaxHealth()*.4f)/1.5f,FDamageEvent(),PC,P);
                Test->TestTrue(TEXT("Half HP enables enraged phase"),Boss->IsEnraged());
                Shot(TEXT("boss-opening.png"));
            }
            if(State==ERPGBossState::Wave && Boss->GetWaveRadius()>300 && !SawWave)
            { SawWave=true; Shot(TEXT("boss-shockwave.png")); }
            if(SawRecovery && State==ERPGBossState::Charge && Previous!=State) { ++EnragedCharges; }
            if(Previous==ERPGBossState::ReturnHome && State==ERPGBossState::Idle)
            {
                SawReturn=true;
                Test->TestTrue(TEXT("Post-charge return ends at original center"),FVector::Dist2D(Boss->GetActorLocation(),Boss->GetArenaHome())<80);
                Test->TestTrue(TEXT("Returning home does not reset boss health or end the encounter"),Boss->GetHealth()<Boss->GetMaxHealth()*.5f && Boss->IsBossActive());
                Test->TestEqual(TEXT("Returning home does not raise boss level"),Boss->GetRPGLevel(),10);
            }
            Previous=State;
            if(SawWave && SawReturn && EnragedCharges>=2 && State==ERPGBossState::Recovery)
            {
                Test->TestTrue(TEXT("Enraged boss makes two successive charges"),EnragedCharges>=2);
                // A real player knockout must reset the encounter, not advance its level.
                P->TakeDamage(10000,FDamageEvent(),Boss->GetController(),Boss.Get());
                // A randomly rolled life badge may survive one lethal hit. Its cooldown prevents the second.
                if(!P->IsEliminated()) { P->TakeDamage(10000,FDamageEvent(),Boss->GetController(),Boss.Get()); }
                Test->TestTrue(TEXT("Player can be defeated during boss encounter"),P->IsEliminated());
                Stage=4; Next=Now+.3; return false;
            }
            return false;
        }
        if(Stage==4)
        {
            if(Now<Next) { return false; }
            Test->TestEqual(TEXT("Player KO resets boss HP to full"),Boss->GetHealth(),Boss->GetMaxHealth());
            Test->TestEqual(TEXT("Loss does not raise boss level"),Boss->GetRPGLevel(),10);
            Test->TestEqual(TEXT("Loss returns boss to waiting"),Boss->GetBossState(),ERPGBossState::Idle);
            Stage=5; return false;
        }
        if(Stage==5)
        {
            if(P->IsEliminated()) { return false; }
            P->SetActorLocation(Boss->GetArenaHome()+FVector(-650,0,0));
            const int32 Rubies=P->GetRubies();
            Boss->TakeDamage(10000,FDamageEvent(),PC,P);
            Test->TestEqual(TEXT("Victory raises boss level by five"),Boss->GetRPGLevel(),15);
            Test->TestEqual(TEXT("Defeated boss waits for explicit retry"),Boss->GetBossState(),ERPGBossState::Defeated);
            Test->TestTrue(TEXT("Boss victory pays scaled rubies"),P->GetRubies()>=Rubies+80);
            MoneyAfter=P->GetRubies();
            Boss->TakeDamage(10000,FDamageEvent(),PC,P);
            Test->TestEqual(TEXT("Repeated hits on corpse give no rewards"),P->GetRubies(),MoneyAfter);
            Next=Now+2; Stage=6; return false;
        }
        if(Stage==6)
        {
            if(Now<Next) { return false; }
            Test->TestTrue(TEXT("Defeated encounter is retained rather than auto respawned/removed"),Boss.IsValid() && Boss->IsEliminated());
            Test->TestTrue(TEXT("Nearby player can explicitly retry"),Boss->Retry(P));
            Test->TestEqual(TEXT("Retry is Lv15"),Boss->GetRPGLevel(),15);
            Test->TestEqual(TEXT("Next level has higher full HP"),Boss->GetMaxHealth(),ARPGPrototypeBoss::HealthForLevel(15));
            Test->TestEqual(TEXT("Retry resets boss HP"),Boss->GetHealth(),Boss->GetMaxHealth());
            Boss->TakeDamage(10000,FDamageEvent(),PC,P);
            Test->TestEqual(TEXT("Second victory continues to Lv20"),Boss->GetRPGLevel(),20);
            P->SetPrototypeAutomatedInput(false);
            W->GetAuthGameMode<ARPGPrototypeGameMode>()->ResetPrototype();
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0,EnragedCharges=0,MoneyAfter=0;
    bool Travel=false,SawTell=false,SawRecovery=false,SawWave=false,SawReturn=false;
    ERPGBossState Previous=ERPGBossState::Idle;
    TSet<uint32> SeenProjectiles;
    TWeakObjectPtr<ARPGPrototypeBoss> Boss;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossPlayTest,"ChaosImpact.RPGPrototype.BossPlay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGBossPlayTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FRPGBossPlayCommand(this)); return true; }
#endif
