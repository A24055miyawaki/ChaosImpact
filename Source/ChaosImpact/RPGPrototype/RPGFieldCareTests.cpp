#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGHealing.h"
#include "RPGSnowField.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Down and revive, falling off, hearts / herbs / resting, and fast travel (RPGRevive.cpp, RPGHealing.cpp, RPGFastTravel.cpp).
namespace
{
class FRPGFieldCareCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGFieldCareCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>240) { Test->AddError(FString::Printf(TEXT("Field care test timeout %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } } if(!W) { return false; }
        if(Stage==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=2?CIKeyboardPlayer=0")); Stage=1; return false; }
        auto* A=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0)); auto* B=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
        auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>(); auto* Place=ARPGAdventureWorld::Find(W);
        if(!A || !Mode || !Place || !A->GetPrototypeHUD() || !Cast<AChaosImpactPlayerController>(A->GetController())->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        const auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        const auto Move=[](ARPGPrototypeCharacter* P,const FVector& At) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics); };
        const auto Hit=[&](ARPGPrototypeCharacter* P,float Amount) { return P->TakeDamage(Amount,FDamageEvent(),Grunt.IsValid()?Grunt->GetController():nullptr,Grunt.Get()); };
        switch(Stage)
        {
        case 1:
        {
            if(!B || !B->GetPrototypeHUD()) { return false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); if(!Grunt.IsValid() && !Cast<ARPGSimaeBody>(*It) && !Cast<ARPGPrototypeBoss>(*It)) { Grunt=*It; } } }
            A->SetPrototypeAutomatedInput(true); B->SetPrototypeAutomatedInput(true);
            // No camp wakes up during the test (the hits it checks are its own).
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            // A rest beacon out in the field is the stage for the test.
            // Away from the bosses (they keep their own rules).
            float Farthest=-1;
            for(TActorIterator<ARPGAdventureCheckpoint> It(W);It;++It)
            {
                if(Place->IsInColony(It->GetActorLocation()) || ARPGSnowField::RegionAt(W,It->GetActorLocation()).Len()) { continue; }
                float Away=MAX_flt; for(TActorIterator<ARPGPrototypeBoss> Boss(W);Boss;++Boss) { Away=FMath::Min(Away,FVector::Dist2D(Boss->GetActorLocation(),It->GetActorLocation())); }
                if(Away>Farthest) { Farthest=Away; Beacon=*It; }
            }
            if(!Test->TestTrue(TEXT("A beacon to work by"),Beacon.IsValid())) { return true; }
            Field=Beacon->RespawnFor(0)+FVector(0,200,0);
            Test->TestTrue(TEXT("Healing herbs grow in the field"),TActorIterator<ARPGHealHerb>(W) ? true : false);
            Test->TestEqual(TEXT("Two players count as a team"),ARPGPrototypeCharacter::CountHeroes(W),2);
            Move(A,Field); Move(B,Field+FVector(0,150,0)); Stage=2; Next=Now+1.0; return false;
        }
        case 2:
            // Knocked out with a teammate around: down, not out.
            Hit(A,10000);
            Test->TestTrue(TEXT("A knockout is a down with a teammate"),A->IsDowned() && !A->IsEliminated());
            Test->TestEqual(TEXT("Down players take nothing more"),Hit(A,50),0.0f);
            Test->TestTrue(TEXT("Thirty seconds to be helped"),A->GetDownedLeft()>29 && A->GetDownedLeft()<=30.01f);
            Test->TestTrue(TEXT("Teammate close by can help"),B->FindReviveTarget()==A);
            Test->TestTrue(TEXT("Enemies leave a downed player alone"),ARPGPrototypeGameMode::NearestPlayer(W,A->GetActorLocation())==B);
            B->bReviveHeld=true; A->NoticeUntil=B->NoticeUntil=0; Stage=3; Next=Now+1.5; return false;
        case 3:
            Shot(TEXT("care-downed.png"));
            Test->TestTrue(TEXT("Holding fills the help"),A->GetReviveProgress()>.35f && A->IsDowned());
            // The helper is hit: the help starts over.
            Hit(B,5);
            Test->TestTrue(TEXT("A hit on the helper starts it over"),A->GetReviveProgress()<.1f);
            Stage=4; Next=Now+3.3; return false;
        case 4:
            Test->TestTrue(FString::Printf(TEXT("Three seconds of holding gets them up (down %d out %d help %.2f apart %.0f held %d)"),A->IsDowned(),A->IsEliminated(),A->GetReviveProgress(),FVector::Dist(A->GetActorLocation(),B->GetActorLocation()),B->bReviveHeld),!A->IsDowned() && !A->IsEliminated());
            Test->TestTrue(TEXT("Up with a third of their HP"),FMath::IsNearlyEqual(A->GetHealth(),A->GetMaxHealth()/3,1.0f));
            Test->TestEqual(TEXT("A moment of grace after getting up"),Hit(A,5),0.0f);
            B->bReviveHeld=false; Stage=5; Next=Now+1.8; return false;
        case 5:
            // Everybody down at once: nobody can help, so the usual knockout.
            Hit(A,10000); Hit(B,10000);
            Stage=6; Next=Now+.3; return false;
        case 6:
            Test->TestTrue(TEXT("Everyone down: knocked out"),A->IsEliminated() && B->IsEliminated() && !A->IsDowned() && !B->IsDowned());
            Mode->SetLocalPlayerCount(1); Stage=7; Next=Now+6.5; return false;
        case 7:
            if(A->IsEliminated()) { return false; }
            Move(A,Field); A->SetPrototypeAutomatedInput(true); Stage=8; Next=Now+2.2; return false;
        case 8:
        {
            // Alone: straight to the knockout.
            const float HP=A->GetHealth();
            Test->TestEqual(TEXT("Alone there is no down"),ARPGPrototypeCharacter::CountHeroes(W),1);
            // Off the edge: back on the last firm ground, with a little damage.
            Before=A->GetActorLocation();
            A->SetActorLocation(Before-FVector(0,0,4000),false,nullptr,ETeleportType::TeleportPhysics);
            Stage=9; Next=Now+.3; Money=HP; return false;
        }
        case 9:
            Test->TestTrue(FString::Printf(TEXT("A fall goes back to firm ground (%s -> %s)"),*Before.ToString(),*A->GetActorLocation().ToString()),FVector::Dist2D(A->GetActorLocation(),Before)<400 && FMath::Abs(A->GetActorLocation().Z-Before.Z)<300);
            Test->TestTrue(FString::Printf(TEXT("A fall costs some HP (%.1f -> %.1f)"),Money,A->GetHealth()),FMath::IsNearlyEqual(Money-A->GetHealth(),A->GetMaxHealth()*RPGTuning::FallDamagePercent*.01f,1.0f));
            Hit(A,10000);
            Test->TestTrue(FString::Printf(TEXT("Alone a knockout is a knockout (out %d down %d hp %.1f)"),A->IsEliminated(),A->IsDowned(),A->GetHealth()),A->IsEliminated() && !A->IsDowned());
            Stage=10; Next=Now+6.5; return false;
        case 10:
            if(A->IsEliminated()) { return false; }
            Move(A,Field); Stage=11; Next=Now+2; return false;
        case 11:
            // A heart drifts to a hurt player and heals them.
            Hit(A,A->GetMaxHealth()*.5f); Money=A->GetHealth();
            ARPGHealPickup::Drop(W,A->GetActorLocation()+FVector(160,0,0));
            Stage=12; Next=Now+.25; return false;
        case 12: A->NoticeUntil=0; Shot(TEXT("care-heart.png")); Stage=13; Next=Now+1.8; return false;
        case 13:
        {
            Test->TestTrue(TEXT("A heart heals a quarter"),FMath::IsNearlyEqual(A->GetHealth()-Money,A->GetMaxHealth()*RPGTuning::HeartHealPercent*.01f,1.5f));
            int32 Hearts=0; for(TActorIterator<ARPGHealPickup> It(W);It;++It) { ++Hearts; }
            Test->TestEqual(TEXT("The heart is used up"),Hearts,0);
            // A herb: walk into the bush.
            ARPGHealHerb* Herb=nullptr; float Best=MAX_flt;
            for(TActorIterator<ARPGHealHerb> It(W);It;++It) { const float D=FVector::DistSquared2D(It->GetActorLocation(),Field); if(D<Best) { Best=D; Herb=*It; } }
            if(!Test->TestNotNull(TEXT("A herb near the beacon"),Herb)) { return true; }
            HerbActor=Herb; Herb->Regrow();
            Move(A,Herb->GetActorLocation()+FVector(-200,0,110)); Stage=14; Next=Now+.8; return false;
        }
        case 14: A->NoticeUntil=0; Shot(TEXT("care-herb.png")); Money=A->GetHealth(); Move(A,HerbActor->GetActorLocation()+FVector(0,0,100)); Stage=15; Next=Now+.6; return false;
        case 15:
            Test->TestTrue(TEXT("Berries heal"),A->GetHealth()>Money+A->GetMaxHealth()*.2f);
            Test->TestFalse(FString::Printf(TEXT("And are eaten (herb %s player %s)"),*HerbActor->GetActorLocation().ToString(),*A->GetActorLocation().ToString()),HerbActor->IsRipe());
            // Resting by the beacon (not fighting). Lighting it heals fully, so light it first.
            Move(A,Beacon->GetActorLocation()+FVector(-200,0,120)); Stage=151; Next=Now+.6; return false;
        case 151:
            Test->TestTrue(TEXT("Standing at the beacon lights it for travel"),A->TravelPoints.Contains(ARPGPrototypeCharacter::TravelKey(Beacon.Get())));
            Hit(A,A->GetMaxHealth()*.6f); Money=A->GetHealth(); Stage=16; Next=Now+RPGTuning::OutOfCombatSeconds+1.5; return false;
        case 16:
        {
            Test->TestTrue(TEXT("Resting by a beacon heals"),A->GetHealth()>Money+A->GetMaxHealth()*.03f);
            // Fast travel: the lit beacon and the colony.
            Move(A,Place->PlayerSpawn(0)+FVector(300,0,0)); Stage=17; Next=Now+.5; return false;
        }
        case 17:
            Test->TestTrue(TEXT("Fast travel opens"),A->OpenFastTravel() && A->IsTravelOpen());
            Travel=A->TravelList().IndexOfByPredicate([&](const ARPGPrototypeCharacter::FTravelPoint& P) { return P.Key==ARPGPrototypeCharacter::TravelKey(Beacon.Get()); });
            A->TravelSelection=Travel; A->NoticeUntil=0; Stage=18; Next=Now+.8; return false;
        case 18: Shot(TEXT("care-travel.png")); Stage=19; Next=Now+.3; return false;
        case 19:
            Test->TestTrue(TEXT("Travel to the lit beacon"),A->FastTravelTo(Travel) && FVector::Dist2D(A->GetActorLocation(),Beacon->GetActorLocation())<400 && !A->IsTravelOpen());
            Test->TestFalse(TEXT("Places not lit cannot be travelled to"),A->TravelList().ContainsByPredicate([](const ARPGPrototypeCharacter::FTravelPoint& P) { return !P.bVisited && !P.bColony; })
                && A->FastTravelTo(A->TravelList().IndexOfByPredicate([](const ARPGPrototypeCharacter::FTravelPoint& P) { return !P.bVisited; })));
            Hit(A,5);
            Test->TestFalse(TEXT("Not while fighting"),A->CanFastTravel());
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test; double Started=FPlatformTime::Seconds(),Next=0; int32 Stage=0,Travel=0; float Money=0;
    FVector Field=FVector::ZeroVector,Before=FVector::ZeroVector;
    TWeakObjectPtr<ARPGPrototypeCharacter> Grunt; TWeakObjectPtr<ARPGAdventureCheckpoint> Beacon; TWeakObjectPtr<ARPGHealHerb> HerbActor;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFieldCareTest,"ChaosImpact.RPGPrototype.FieldCare",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGFieldCareTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGFieldCareCommand(this)); return true; }
#endif
