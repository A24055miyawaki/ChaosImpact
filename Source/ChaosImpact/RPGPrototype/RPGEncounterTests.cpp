#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
class FEncounterCommand : public IAutomationLatentCommand
{
public:
    explicit FEncounterCommand(FAutomationTestBase* T):Test(T) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>160) { Test->AddError(FString::Printf(TEXT("Encounter timeout stage %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        { UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1")); Stage=1; return false; }
        auto* P=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,0));
        auto* PC=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>();
        auto* Adventure=ARPGAdventureWorld::Find(W);
        if(!P || !PC || !Mode || !Adventure || !PC->IsGameplayActive()) { return false; }
        const double Now=W->GetTimeSeconds();
        if(Stage==7) { bOverAttackCap|=Mode->GetActiveGruntAttackers(P)>2; }
        if(Now<Next) { return false; }
        auto MovePlayer=[&](float Radius)
        {
            FVector Spot;
            if(!FindSpot(W,Camp->GetActorLocation(),Radius,Spot)) { Test->AddError(TEXT("No walkable player test position")); return false; }
            P->ResetForOnlineMatch(Spot,FRotator::ZeroRotator); P->SetPrototypeAutomatedInput(true); return true;
        };
        auto SetAI=[&](bool Enabled)
        { for(const auto& M:Camp->Members) { if(M.IsValid() && M->GetController()) { M->GetController()->SetActorTickEnabled(Enabled); if(!Enabled) { M->CancelChargingThrow(); } } } };
        switch(Stage)
        {
        case 1:
        {
            if(Now<2) { return false; }
            int32 Groups=0,SpawnedInColony=0;
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It)
            {
                if(It->bEncounterGroup && It->bEnabled)
                {
                    ++Groups; Camps.Add(*It);
                    SpawnedInColony+=It->Members.Num();
                    Test->TestEqual(TEXT("Each camp has three chasers"),It->ChaserCount,3);
                    Test->TestEqual(TEXT("Each camp has two throwers"),It->ThrowerCount,2);
                    Test->TestFalse(TEXT("No camp inside the colony"),Adventure->IsInColony(It->GetActorLocation()));
                }
                It->bEnabled=false;
            }
            Test->TestTrue(TEXT("Enemy camps are distributed across the field and dungeon"),Groups>=15);
            Test->TestEqual(TEXT("Distant camps are not spawned while the player is in the colony"),SpawnedInColony,0);
            P->SetPrototypeAutomatedInput(true);
            Stage=2; return false;
        }
        case 2:
        {
            // Audit every placed camp on real map collision, not on an artificial test plane.
            if(AuditIndex>=Camps.Num())
            {
                for(const auto& C:Camps) { if(C.IsValid() && C->GetActorLocation().X>5000 && C->GetActorLocation().X<8000) { Camp=C; break; } }
                if(!Test->TestTrue(TEXT("A field camp exists for the combat test"),Camp.IsValid())) { return true; }
                Camp->bEnabled=true; Camp->NextSpawnAt=0; Camp->RespawnSeconds=1;
                if(!MovePlayer(1300)) { return true; }
                Stage=3; Next=Now+.3; return false;
            }
            auto* C=Camps[AuditIndex++].Get();
            FVector At;
            if(!FindSpot(W,C->GetActorLocation(),1250,At)) { Test->AddError(TEXT("Camp has no reachable approach floor")); return true; }
            P->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
            C->bEnabled=true; C->TickEncounter(Mode,Now);
            Test->TestEqual(FString::Printf(TEXT("Map camp %d spawns all five enemies on safe floor"),AuditIndex),C->Members.Num(),5);
            int32 Chasers=0,Throwers=0;
            for(const auto& M:C->Members)
            {
                if(!M.IsValid()) { continue; }
                Chasers+=M->EncounterRole==ERPGGruntRole::Chaser; Throwers+=M->EncounterRole==ERPGGruntRole::Thrower;
                if(M->GetController()) { M->GetController()->Destroy(); } M->Destroy();
            }
            Test->TestEqual(TEXT("Runtime chaser count"),Chasers,3); Test->TestEqual(TEXT("Runtime thrower count"),Throwers,2);
            C->bEnabled=false;
            return false;
        }
        case 3:
            Test->TestEqual(TEXT("Nearby camp is preloaded"),Camp->Members.Num(),5);
            Test->TestFalse(TEXT("Preloaded camp does not attack a distant player"),Camp->IsEngaged());
            if(!MovePlayer(650)) { return true; }
            Stage=4; Next=Now+.3; return false;
        case 4:
            Test->TestTrue(TEXT("Only the approached group engages the player"),Camp->GetEncounterTarget()==P);
            SetAI(false);
            Stage=5; Next=Now+2.2; return false;
        case 5:
            Test->TestTrue(TEXT("First enemy reserves an attack"),Mode->ReserveGruntAttack(Camp->Members[0].Get(),P,1.8));
            Test->TestFalse(TEXT("Attack starts are staggered, not simultaneous"),Mode->ReserveGruntAttack(Camp->Members[1].Get(),P,1.8));
            Stage=51; Next=Now+.32; return false;
        case 51:
            Test->TestTrue(TEXT("Second enemy may attack after a short gap"),Mode->ReserveGruntAttack(Camp->Members[1].Get(),P,1.8));
            Stage=52; Next=Now+.32; return false;
        case 52:
            Test->TestFalse(TEXT("Third enemy cannot join an existing two-enemy volley"),Mode->ReserveGruntAttack(Camp->Members[2].Get(),P,1.8));
            Test->TestEqual(TEXT("Only two attack windows per player"),Mode->GetActiveGruntAttackers(P),2);
            Stage=6; Next=Now+2; return false;
        case 6:
            HPBefore=P->GetHealth(); SetAI(true);
            Stage=7; Next=Now+6.f; return false;
        case 7:
            Test->TestFalse(TEXT("Live combat stays within the two-attacker cap"),bOverAttackCap);
            Test->TestTrue(TEXT("A stationary player is actually hit by the group"),P->GetHealth()<HPBefore);
            Test->AddInfo(FString::Printf(TEXT("Audited %d camps. Player HP during 6s of stationary combat: %.0f -> %.0f"),Camps.Num(),HPBefore,P->GetHealth()));
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGEncounterQA/GroupCombat.png")),true,false);
            Stage=70; Next=Now+.15; return false;
        case 70:
            if(!MovePlayer(2400)) { return true; }
            Stage=8; Next=Now+.5; return false;
        case 8:
            Test->TestFalse(TEXT("Group stops chasing outside its home leash"),Camp->IsEngaged());
            Mode->SetLocalPlayerCount(2);
            Stage=9; Next=Now+.4; return false;
        case 9:
        {
            auto* P2=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            if(!Test->TestNotNull(TEXT("Second local RPG player"),P2)) { return true; }
            FVector At; if(!FindSpot(W,Camp->GetActorLocation(),650,At)) { return true; }
            P2->ResetForOnlineMatch(At,FRotator::ZeroRotator); P2->SetPrototypeAutomatedInput(true);
            Stage=10; Next=Now+.3; return false;
        }
        case 10:
        {
            auto* P2=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            Test->TestTrue(TEXT("A group also notices P2, independently of distant P1"),Camp->GetEncounterTarget()==P2);
            for(const auto& M:Camp->Members) { if(M.IsValid()) { M->TakeDamage(100000,FDamageEvent(),PC,P); } }
            Stage=11; Next=Now+1.5; return false;
        }
        case 11:
        {
            bool Alive=false; for(const auto& M:Camp->Members) { Alive|=M.IsValid()&&!M->IsEliminated(); }
            Test->TestFalse(TEXT("A cleared group does not respawn on a nearby player's head"),Alive);
            auto* P2=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
            P2->ResetForOnlineMatch(Adventure->PlayerSpawn(1),FRotator::ZeroRotator);
            if(!MovePlayer(1300)) { return true; }
            Stage=12; Next=Now+.5; return false;
        }
        case 12:
        {
            int32 Alive=0; for(const auto& M:Camp->Members) { Alive+=M.IsValid()&&!M->IsEliminated(); }
            Test->TestEqual(TEXT("Whole group respawns after cooldown once players leave"),Alive,5);
            return true;
        }
        }
        return false;
    }
private:
    bool FindSpot(UWorld* W,const FVector& Home,float Radius,FVector& Spot)
    {
        FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGEncounterQA),false);
        for(int32 I=0;I<96;++I)
        {
            // Round the camp at the asked distance first, then nearer in (small plateaus have only a road out).
            const FVector Probe=Home+FRotator(0,15.f*(I%24),0).Vector()*Radius*(1-.15f*(I/24));
            FHitResult Floor;
            if(!W->LineTraceSingleByObjectType(Floor,Probe+FVector(0,0,350),Probe-FVector(0,0,650),FCollisionObjectQueryParams(ECC_WorldStatic),Q)
                || Floor.ImpactNormal.Z<.65f) { continue; }
            const FVector At=Floor.ImpactPoint+FVector(0,0,99);
            if(ARPGAdventureWorld::Find(W)->IsInColony(At) || FMath::Abs(At.Z-Home.Z)>200) { continue; }
            if(W->OverlapAnyTestByObjectType(At,FQuat::Identity,FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeCapsule(43,95),Q)) { continue; }
            Spot=At; return true;
        }
        return false;
    }
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0,AuditIndex=0;
    TArray<TWeakObjectPtr<ARPGPrototypeSpawnPoint>> Camps;
    TWeakObjectPtr<ARPGPrototypeSpawnPoint> Camp;
    float HPBefore=0;
    bool bOverAttackCap=false;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEncounterTest,"ChaosImpact.RPG.Encounters",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGEncounterTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FEncounterCommand(this)); return true; }
#endif
