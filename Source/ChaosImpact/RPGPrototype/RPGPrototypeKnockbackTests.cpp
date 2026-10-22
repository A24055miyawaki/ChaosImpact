#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
class FRPGKnockbackCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGKnockbackCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>90) { Test->AddError(TEXT("Knockback test timeout")); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        auto* PC=W?Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController()):nullptr;
        if(!PC || !PC->GetPawn()) { return false; }
        if(Stage==0)
        {
            UGameplayStatics::OpenLevel(PC,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1"));
            Stage=1; return false;
        }
        auto* P=Cast<ARPGPrototypeCharacter>(PC->GetPawn());
        if(!P || !PC->IsGameplayActive() || W->GetTimeSeconds()<Next) { return false; }
        if(Stage==1)
        {
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { It->SetTrainingMenuFrozen(true); }
            P->SetPrototypeAutomatedInput(true);
            Base=P->GetActorLocation()+FVector(0,0,2000);
            Floor=Block(W,Base-FVector(0,0,10),FVector(40,40,.2f));
            for(int32 I=0;I<4;++I) { Targets.Add(Target(P,FVector(0,(I-1.5f)*300,100))); }
            Wall=Block(W,Targets[3]->GetActorLocation()+FVector(100,0,0),FVector(.2f,1.4f,3));
            auto CommonShot=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal);
            CommonShot.Tunes[2]={TEXT("Knockback"),100,1};
            Shot(P,CommonShot,0)->DealRPGDamage(Targets[0].Get());
            auto Charged=CommonShot; Charged.Tunes[0]={TEXT("ChargeKnockback"),160,1};
            Shot(P,Charged,1)->DealRPGDamage(Targets[1].Get());
            auto Weak=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal); Weak.Tunes[0]={TEXT("ChargeKnockback"),160,1};
            Shot(P,Weak,.2f)->DealRPGDamage(Targets[2].Get());
            Shot(P,CommonShot,0)->DealRPGDamage(Targets[3].Get());
            Test->TestTrue(TEXT("Direct tune starts smooth knockback"),Targets[0]->IsRPGKnockedBack());
            Test->TestFalse(TEXT("Knockback cannot immediately stack or refresh"),Targets[0]->ApplyRPGKnockback(FVector::ForwardVector,300));
            Test->TestFalse(TEXT("Knockback does not freeze"),Targets[0]->IsIceFrozen());
            Test->TestFalse(TEXT("Knockback is separate from electrical stun"),Targets[0]->IsRPGStunned());
            Stage=2; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==2)
        {
            UE_LOG(LogTemp,Display,TEXT("KNOCKBACK QA: direct %.1f, charge %.1f, weak %.1f, wall %.1f cm"),
                Targets[0]->GetActorLocation().X-Base.X,Targets[1]->GetActorLocation().X-Base.X,
                Targets[2]->GetActorLocation().X-Base.X,Targets[3]->GetActorLocation().X-Base.X);
            Test->TestTrue(TEXT("100cm direct knockback reaches configured distance"),FMath::IsNearlyEqual(Targets[0]->GetActorLocation().X-Base.X,100,3));
            Test->TestTrue(TEXT("Full charge combines to 260cm"),FMath::IsNearlyEqual(Targets[1]->GetActorLocation().X-Base.X,260,3));
            Test->TestTrue(TEXT("Low charge does not trigger full-charge tune"),FMath::IsNearlyZero(Targets[2]->GetActorLocation().X-Base.X,3));
            Test->TestTrue(TEXT("Swept knockback stops before wall"),Targets[3]->GetActorLocation().X-Base.X<70 && Targets[3]->GetActorLocation().X>Base.X);
            Test->TestFalse(TEXT("Controls unlock after knockback finishes"),Targets[0]->IsMatchInputLocked() || Targets[0]->IsMoveInputIgnored());
            for(const auto& T:Targets) { T->Destroy(); } Targets.Empty(); Wall->Destroy();
            Targets.Add(Target(P,FVector(-200,0,100))); Targets.Add(Target(P,FVector(200,0,100)));
            auto Blast=FRPGPrototypeItem::Make(EChaosImpactBallType::Fire); Blast.Tunes[0]={TEXT("BlastKnockback"),110,1};
            Fields.Add(ARPGPrototypeEffect::Create(P,Blast,TEXT("Blast"),Base+FVector(0,0,100),450,.15f,.25f));
            Stage=3; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==3)
        {
            Test->TestTrue(TEXT("Blast pushes left target outward"),FMath::IsNearlyEqual(Targets[0]->GetActorLocation().X-Base.X,-310,4));
            Test->TestTrue(TEXT("Blast pushes right target outward"),FMath::IsNearlyEqual(Targets[1]->GetActorLocation().X-Base.X,310,4));
            for(const auto& T:Targets) { T->Destroy(); } Targets.Empty();
            Targets.Add(Target(P,FVector(100,-800,100))); Targets.Add(Target(P,FVector(100,800,100)));
            auto Wind=FRPGPrototypeItem::Make(EChaosImpactBallType::Wind);
            auto* Ordinary=ARPGPrototypeEffect::Create(P,Wind,TEXT("Wind"),Base+FVector(0,-800,100),900,1.3f,.01f);
            Ordinary->Direction=FVector::ZeroVector; Fields.Add(Ordinary);
            Wind.Tunes[0]={TEXT("WindKick"),150,1};
            auto* Tuned=ARPGPrototypeEffect::Create(P,Wind,TEXT("Wind"),Base+FVector(0,800,100),900,1.3f,.01f);
            Tuned->Direction=FVector::ZeroVector; Fields.Add(Tuned);
            Targets.Add(Target(P,FVector(-1200,0,100)));
            auto Burn=FRPGPrototypeItem::Make(EChaosImpactBallType::Fire); Burn.Tunes[2]={TEXT("Knockback"),100,1};
            Fields.Add(ARPGPrototypeEffect::Create(P,Burn,TEXT("Burn"),Targets[2]->GetActorLocation(),50,2,.08f,Targets[2].Get()));
            Stage=4; Next=W->GetTimeSeconds()+1.18; return false;
        }
        if(Stage==4)
        {
            const float Difference=Targets[1]->GetActorLocation().X-Targets[0]->GetActorLocation().X;
            Test->TestTrue(TEXT("Wind kick adds exactly once, not on every damage pulse"),FMath::IsNearlyEqual(Difference,150,6));
            Test->TestTrue(TEXT("Burn does damage without repeatedly knocking back"),Targets[2]->GetHealth()<10000 && FMath::IsNearlyEqual(Targets[2]->GetActorLocation().X-Base.X,-1200,3));
            for(const auto& F:Fields) { if(F.IsValid()) { F->Destroy(); } } Fields.Empty();
            for(const auto& T:Targets) { T->Destroy(); } Targets.Empty();
            auto* B=W->SpawnActor<ARPGPrototypeBoss>(Base+FVector(1500,0,100),FRotator::ZeroRotator); Boss=B;
            Test->TestTrue(TEXT("Boss accepts reduced knockback while not armored"),B->ApplyRPGKnockback(FVector::ForwardVector,260));
            Test->TestFalse(TEXT("Boss has a knockback resistance window"),B->ApplyRPGKnockback(FVector::ForwardVector,260));
            Targets.Add(Target(P,FVector(0,0,100)));
            Test->TestTrue(TEXT("Very large tune requests are safely capped"),Targets[0]->ApplyRPGKnockback(FVector::ForwardVector,100000));
            Stage=5; Next=W->GetTimeSeconds()+.35; return false;
        }
        Test->TestTrue(TEXT("Boss moves only 20 percent of requested distance"),Boss.IsValid() && FMath::IsNearlyEqual(Boss->GetActorLocation().X-Base.X,1552,4));
        Test->TestTrue(TEXT("Knockback caps at 300cm"),FMath::IsNearlyEqual(Targets[0]->GetActorLocation().X-Base.X,300,4));
        for(const auto& T:Targets) { T->Destroy(); } Boss->Destroy(); Floor->Destroy();
        P->SetTrainingMenuFrozen(false); P->SetPrototypeAutomatedInput(false);
        W->GetAuthGameMode<ARPGPrototypeGameMode>()->ResetPrototype(); return true;
    }
private:
    AStaticMeshActor* Block(UWorld* W,FVector At,FVector Scale)
    {
        auto* A=W->SpawnActor<AStaticMeshActor>(At,FRotator::ZeroRotator); A->SetMobility(EComponentMobility::Movable);
        A->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        A->SetActorScale3D(Scale); return A;
    }
    ARPGPrototypeCharacter* Target(ARPGPrototypeCharacter* P,FVector Offset)
    {
        const FTransform At(FRotator::ZeroRotator,Base+Offset);
        auto* T=P->GetWorld()->SpawnActorDeferred<ARPGPrototypeCharacter>(P->GetClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        T->AutoPossessAI=EAutoPossessAI::Disabled; T->SetEnemy(true); T->EnemyHP=10000; T->FinishSpawning(At); return T;
    }
    ARPGPrototypeBall* Shot(ARPGPrototypeCharacter* P,FRPGPrototypeItem Item,float Charge)
    {
        const FTransform At(FRotator::ZeroRotator,Base+FVector(0,0,2500));
        auto* B=P->GetWorld()->SpawnActorDeferred<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),At,P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        B->bSecondary=true; B->ShotItem=Item; B->SetBallType(Item.Type); B->FinishSpawning(At);
        B->Launch(FVector::ForwardVector,1400,EChaosImpactBallFlightMode::Straight);
        // Freeze the test projectile in space without clearing its incoming velocity/direction.
        B->FindComponentByClass<UProjectileMovementComponent>()->SetComponentTickEnabled(false); B->SetActorTickEnabled(false);
        B->bSecondary=false; B->ShotCharge=Charge; B->SetLifeSpan(1); return B;
    }
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(), Next=0;
    int32 Stage=0;
    FVector Base=FVector::ZeroVector;
    TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> Targets;
    TArray<TWeakObjectPtr<ARPGPrototypeEffect>> Fields;
    TWeakObjectPtr<ARPGPrototypeBoss> Boss;
    TWeakObjectPtr<AStaticMeshActor> Floor,Wall;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackTest,"ChaosImpact.RPGPrototype.Knockback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGKnockbackCommand(this)); return true; }
#endif
