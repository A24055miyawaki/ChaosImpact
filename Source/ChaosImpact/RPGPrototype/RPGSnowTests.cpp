#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "ChaosImpactSimaeBoss.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FRPGSnowCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGSnowCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>180) { Test->AddError(FString::Printf(TEXT("Snow test timeout %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        if(Stage==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0)); auto* Field=ARPGSnowField::Find(W); auto* Place=ARPGAdventureWorld::Find(W);
        if(!P || !Field || !Place || !P->GetPrototypeHUD() || !Cast<AChaosImpactPlayerController>(P->GetController())->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        const auto Move=[&](const FVector& At) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics); };
        auto* Boss=Field->GetBoss(); auto* Body=Field->GetBossBody();
        switch(Stage)
        {
        case 1:
        {
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt() && !Cast<ARPGSimaeBody>(*It)) { It->SetTrainingMenuFrozen(true); } }
            ARPGWarpGate* ToSnow=nullptr; for(TActorIterator<ARPGWarpGate> It(W);It;++It) { if(It->DisplayName.Contains(TEXT("雪原"))) { ToSnow=*It; } }
            if(!Test->TestNotNull(TEXT("A gate to the snowfield stands in the colony"),ToSnow)) { return true; }
            Test->TestTrue(TEXT("The boss sleeps in its nest"),Boss && Boss->IsDormant() && Body && !Body->IsAwake());
            Move(ToSnow->GetActorLocation()+FVector(-150,0,110)); Stage=2; Next=Now+.3; return false;
        }
        case 2:
            Test->TestTrue(TEXT("The gate takes the player to the snowfield"),P->InteractAdventure() && Place->RegionName(P->GetActorLocation()).StartsWith(TEXT("雪原")));
            Stage=3; Next=Now+1.5; return false;
        case 3:
        {
            P->NoticeUntil=0; Shot(TEXT("snow-camp.png"));
            ARPGQuestGiver* Watcher=nullptr; for(TActorIterator<ARPGQuestGiver> It(W);It;++It) { if(It->GiverId==5) { Watcher=*It; } }
            Test->TestNotNull(TEXT("The watcher stands at the camp"),Watcher);
            Test->TestEqual(TEXT("Snow hunt is offered"),RPGQuests::State(P,17),ERPGQuestState::Offered);
            Test->TestEqual(TEXT("Boss quest waits for the hunt"),RPGQuests::State(P,18),ERPGQuestState::Locked);
            P->Quests[17].Stage=3;
            if(Watcher) { Move(Watcher->GetActorLocation()+FVector(-160,0,100)); }
            Stage=4; Next=Now+.4; return false;
        }
        case 4:
            Test->TestTrue(TEXT("Talking to the watcher"),P->InteractAdventure() && P->QuestGiver==5);
            P->QuestSelection=18; P->RequestQuestPrimary(); Test->TestEqual(TEXT("Boss quest taken"),RPGQuests::State(P,18),ERPGQuestState::Active);
            P->SetQuestMenuOpen(false);
            Move(ARPGSnowField::Origin()+FVector(1200,-900,120)); Stage=5; Next=Now+1.2; return false;
        case 5:
            Test->TestEqual(TEXT("The lake has its own name"),Place->RegionName(P->GetActorLocation()),FString(TEXT("雪原・凍った湖")));
            P->NoticeUntil=0; Shot(TEXT("snow-lake.png"));
            Move(Field->NestCenter()+FVector(-1100,0,120)); Stage=6; Next=Now+.6; return false;
        case 6:
            Test->TestTrue(TEXT("Walking in wakes the boss"),Boss && !Boss->IsDormant() && Body->IsAwake());
            Boss->bHoldMoves=true; Boss->bHarmless=true; Stage=7; Next=Now+1.4; return false;
        case 7:
            P->NoticeUntil=0; Shot(TEXT("snow-boss-wake.png"));
            Boss->ForceMove(EChaosImpactSimaeMove::Slam); Stage=8; Next=Now+.6; return false;
        case 8: Shot(TEXT("snow-boss-slam.png")); Stage=9; Next=Now+2.0; return false;
        case 9: Boss->ForceMove(EChaosImpactSimaeMove::Throw); Stage=10; Next=Now+.6; return false;
        case 10: Shot(TEXT("snow-boss-goukyu.png")); Stage=11; Next=Now+2.2; return false;
        case 11: Boss->ForceMove(EChaosImpactSimaeMove::Avalanche); Stage=12; Next=Now+1.3; return false;
        case 12: Shot(TEXT("snow-boss-avalanche.png")); Stage=13; Next=Now+2.5; return false;
        case 13:
        {
            Boss->ForceMove(EChaosImpactSimaeMove::None);
            // One hit in solo units is one RPG hit; the slam's "drop every ball" makes every ball wait.
            Boss->bHarmless=false; const float Before=P->GetHealth(); P->SelectAttack(0);
            AChaosImpactSimaeBoss::Strike(Boss,P,Boss,1.0f,FVector::ZeroVector,true);
            Test->TestTrue(TEXT("A boss hit costs RPG health"),FMath::IsNearlyEqual(Before-P->GetHealth(),RPGTuning::SimaeHitDamage,1.0f));
            Test->TestTrue(TEXT("Dropped balls wait a moment"),P->GetCooldownLeft(0)>.5f);
            Boss->bHarmless=true; P->RestoreHP(1000);
            // RPG damage goes through the shell; half health always powers it up.
            Body->TakeDamage(Body->GetMaxHealth()*2,FDamageEvent(),P->GetController(),P);
            Test->TestTrue(TEXT("Half health starts the power-up"),Boss->GetMove()==EChaosImpactSimaeMove::PowerUp && Body->GetHealth()>Body->GetMaxHealth()*.4f);
            Test->TestEqual(TEXT("Powering up it cannot be hurt"),Body->TakeDamage(500,FDamageEvent(),P->GetController(),P),0.0f);
            Stage=14; Next=Now+1.0; return false;
        }
        case 14: Shot(TEXT("snow-boss-powerup.png")); Stage=15; Next=Now+3.0; return false;
        case 15:
            Test->TestTrue(TEXT("Full power after the change"),Boss->IsFullPower());
            Boss->ForceMove(EChaosImpactSimaeMove::Throw); Stage=16; Next=Now+.7; return false;
        case 16: Shot(TEXT("snow-boss-fullpower.png")); Stage=17; Next=Now+2.2; return false;
        case 17:
            Boss->ForceMove(EChaosImpactSimaeMove::None); Money=P->GetRubies();
            Body->TakeDamage(Body->GetMaxHealth()*2,FDamageEvent(),P->GetController(),P);
            Stage=18; Next=Now+.5; return false;
        case 18:
            Test->TestTrue(TEXT("Beaten"),!Field->GetBoss() || Field->GetBoss()->IsEliminated());
            Test->TestTrue(TEXT("Boss reward paid"),P->GetRubies()>Money);
            Test->TestEqual(TEXT("Boss quest done"),RPGQuests::State(P,18),ERPGQuestState::Ready);
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds(),Next=0; int32 Stage=0,Money=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSnowTest,"ChaosImpact.RPGPrototype.Snowfield",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGSnowTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGSnowCommand(this)); return true; }
#endif
