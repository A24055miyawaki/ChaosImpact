#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

namespace
{
class FVanillaSpecialsCommand : public IAutomationLatentCommand
{
public:
    explicit FVanillaSpecialsCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Vanilla specials timeout: %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W || !W->GetFirstPlayerController()) { return false; }
        auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController());
        if(!PC || !PC->GetPawn()) { return false; }
        if(!Travel) { Travel=PC->OpenExplorationMap(); return false; }
        auto* P=Cast<ARPGPrototypeCharacter>(PC->GetPawn());
        if(!P || !W->GetAuthGameMode<ARPGPrototypeGameMode>() || !PC->IsGameplayActive()) { return false; }
        if(Stage==6)
        {
            if(Birds.IsValid()) { ObservedPecks=FMath::Max(ObservedPecks,Birds->GetPeckCount()); }
            if(A.IsValid()) { MaxWindMoved=FMath::Max(MaxWindMoved,static_cast<float>(FVector::Dist2D(OldWind,A->GetActorLocation()))); }
        }
        if(W->GetTimeSeconds()<Next) { return false; }
        if(Stage==0)
        {
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
            P->SetPrototypeAutomatedInput(true); P->SetActorLocation(RPGWorld::AreaPoint(TEXT("Ruins"),{-500,0})+FVector(0,0,120));
            P->GetCharacterMovement()->StopMovementImmediately();
            for(auto Type:RPGEquipment::BallRoster())
            {
                const auto Item=FRPGPrototypeItem::Make(Type,1,ERPGPrototypeRarity::Normal);
                Test->TestEqual(TEXT("Weakest ball has no intrinsic affixes"),Item.Intrinsics.Num(),0);
                Test->TestFalse(TEXT("Base effects do not secretly install a tune"),Item.Tunes.ContainsByPredicate([](const FRPGAffix& A){return !A.Key.IsNone();}));
                Test->TestFalse(TEXT("Every ball explains its baseline in the bag"),RPGEquipment::BaseEffectDescription(Type).IsEmpty());
            }
            auto SpawnTarget=[&](FVector Offset)
            {
                const FTransform At(FRotator::ZeroRotator,P->GetActorLocation()+Offset);
                auto* T=W->SpawnActorDeferred<ARPGPrototypeCharacter>(P->GetClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                T->SetEnemy(true); T->EnemyHP=1000; T->FinishSpawning(At); T->SetTrainingMenuFrozen(true); return T;
            };
            A=SpawnTarget(FVector(300,0,0)); B=SpawnTarget(FVector(300,180,0));
            auto* FireShot=Shot(P,EChaosImpactBallType::Fire); const float Applied=FireShot->DealRPGDamage(A.Get());
            DirectHP=A->GetHealth(); Test->TestTrue(TEXT("Untuned fire still deals its direct hit"),Applied>0);
            Stage=1; Next=W->GetTimeSeconds()+1.2; return false;
        }
        if(!A.IsValid() || !B.IsValid()) { Test->AddError(TEXT("Baseline target lost")); return true; }
        if(Stage==1)
        {
            Test->TestTrue(TEXT("Untuned fire adds real burn damage after the direct hit"),A->GetHealth()<DirectHP);
            Clear(W);
            auto* IceShot=Shot(P,EChaosImpactBallType::Ice); IceShot->DealRPGDamage(A.Get());
            Test->TestTrue(TEXT("Untuned ice slows on direct hit"),A->Status(TEXT("Slow"))>=20);
            ARPGPrototypeEffect::Create(P,IceShot->ShotItem,TEXT("Cold"),A->GetActorLocation(),110,1,0);
            Stage=2; Next=W->GetTimeSeconds()+.6; return false;
        }
        if(Stage==2)
        {
            Test->TestTrue(TEXT("Untuned ice can build up a short freeze"),A->Status(TEXT("FreezeLock"))>0);
            Clear(W);
            const float Before=A->GetHealth(); auto* Thunder=Shot(P,EChaosImpactBallType::Thunder); Thunder->DealRPGDamage(B.Get());
            Test->TestTrue(TEXT("Untuned thunder briefly stuns a previously unfrozen enemy"),B->IsRPGStunned());
            Test->TestFalse(TEXT("Thunder must never create ice freeze"),B->IsIceFrozen());
            Test->TestTrue(TEXT("Electrical stun locks gameplay actions"),B->IsMatchInputLocked() && B->IsMoveInputIgnored());
            Test->TestTrue(TEXT("Untuned thunder chains to a nearby second enemy"),A->GetHealth()<Before);
            A->ClearRPGStatus(TEXT("Slow"));
            auto* Snow=Shot(P,EChaosImpactBallType::Snow); const FVector Old=A->GetActorLocation(); Snow->DealRPGDamage(A.Get());
            Test->TestTrue(TEXT("Untuned snow pushes and slows"),FVector::Dist2D(Old,A->GetActorLocation())>30 && A->Status(TEXT("Slow"))>=10);
            Clear(W);
            A->ClearRPGStatus(TEXT("Slow"));
            ARPGPrototypeEffect::Create(P,FRPGPrototypeItem::Make(EChaosImpactBallType::Black),TEXT("Gravity"),A->GetActorLocation()+FVector(100,0,0),180,2,.14f);
            ARPGPrototypeEffect::Create(P,FRPGPrototypeItem::Make(EChaosImpactBallType::Smoke),TEXT("Smoke"),B->GetActorLocation(),170,2,0);
            Stage=3; Next=W->GetTimeSeconds()+.6; return false;
        }
        if(Stage==3)
        {
            Test->TestFalse(TEXT("Electrical stun expires without leaving controls locked"),B->IsRPGStunned());
            Test->TestTrue(TEXT("Untuned gravity slows"),A->Status(TEXT("Slow"))>=10);
            Test->TestTrue(TEXT("Untuned smoke disrupts aim"),B->GetAimError()>=15);
            Clear(W);
            // Physical two-target piercing, not just checking an assigned property.
            A->SetActorLocation(P->GetActorLocation()+FVector(400,0,0)); B->SetActorLocation(P->GetActorLocation()+FVector(650,0,0));
            auto* Beam=Shot(P,EChaosImpactBallType::Beam,false); Beam->SetActorLocation(P->GetActorLocation()+FVector(70,0,0));
            BeforeA=A->GetHealth(); BeforeB=B->GetHealth(); Beam->Launch(FVector::ForwardVector,3200,EChaosImpactBallFlightMode::Straight);
            Beam->bSecondary=false;
            Stage=4; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==4)
        {
            Test->TestTrue(TEXT("Untuned beam really hits both enemies"),A->GetHealth()<BeforeA && B->GetHealth()<BeforeB);
            Clear(W);
            auto* Drive=Shot(P,EChaosImpactBallType::Drive); Drive->SetActorLocation(A->GetActorLocation()+FVector(60,0,0));
            BeforeA=A->GetHealth(); Drive->Burst(Drive->GetActorLocation());
            Stage=5; Next=W->GetTimeSeconds()+.2; return false;
        }
        if(Stage==5)
        {
            Test->TestTrue(TEXT("Untuned drive ends with a damaging small blast"),A->GetHealth()<BeforeA);
            Clear(W); OldWind=A->GetActorLocation(); BeforeA=A->GetHealth();
            ARPGPrototypeEffect::Create(P,FRPGPrototypeItem::Make(EChaosImpactBallType::Wind),TEXT("Wind"),A->GetActorLocation()-FVector(30,0,0),140,1,.16f);
            Birds=ARPGPrototypeEffect::Create(P,FRPGPrototypeItem::Make(EChaosImpactBallType::Simae),TEXT("Birds"),B->GetActorLocation(),500,3,1);
            Stage=6; Next=W->GetTimeSeconds()+1.6; return false;
        }
        Test->TestTrue(TEXT("Untuned wind pushes enemies and damages them"),MaxWindMoved>30 && A->GetHealth()<BeforeA);
        Test->TestTrue(TEXT("Untuned Simae birds really peck before departing"),ObservedPecks>0);
        // Fresh targets prevent a prior ice field from masking an incorrect status on another attribute.
        Clear(W);
        for(const auto Type:RPGEquipment::BallRoster())
        {
            const FTransform Where(FRotator::ZeroRotator,P->GetActorLocation()+FVector(0,0,4000));
            auto* Fresh=W->SpawnActorDeferred<ARPGPrototypeCharacter>(P->GetClass(),Where,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Fresh->SetEnemy(true); Fresh->EnemyHP=10000; Fresh->FinishSpawning(Where); Fresh->SetTrainingMenuFrozen(true);
            auto* Ball=Shot(P,Type);
            Test->TestTrue(FString::Printf(TEXT("%s direct damage applies"),ChaosImpactBallTypes::GetInternalName(Type)),Ball->DealRPGDamage(Fresh)>0);
            Test->TestFalse(FString::Printf(TEXT("%s first hit never falsely freezes"),ChaosImpactBallTypes::GetInternalName(Type)),Fresh->IsIceFrozen());
            Test->TestEqual(FString::Printf(TEXT("%s uses electrical/root stun only when appropriate"),ChaosImpactBallTypes::GetInternalName(Type)),Fresh->IsRPGStunned(),Type==EChaosImpactBallType::Thunder);
            if(Type==EChaosImpactBallType::Ice)
            {
                Ball->DealRPGDamage(Fresh); Ball->DealRPGDamage(Fresh);
                Test->TestTrue(TEXT("Only accumulated ice creates actual ice freeze"),Fresh->IsIceFrozen());
                Test->TestFalse(TEXT("Ice freeze is not electrical stun"),Fresh->IsRPGStunned());
            }
            Fresh->Destroy(); Clear(W);
        }
        Clear(W); A->Destroy(); B->Destroy(); P->SetPrototypeAutomatedInput(false);
        W->GetAuthGameMode<ARPGPrototypeGameMode>()->ResetPrototype(); return true;
    }
private:
    ARPGPrototypeBall* Shot(ARPGPrototypeCharacter* P,EChaosImpactBallType Type,bool LaunchNow=true)
    {
        const FTransform At(FRotator::ZeroRotator,P->GetActorLocation()+FVector(0,0,800));
        auto* Ball=P->GetWorld()->SpawnActorDeferred<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),At,P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Ball->bSecondary=true; Ball->ShotItem=FRPGPrototypeItem::Make(Type,1,ERPGPrototypeRarity::Normal); Ball->SetBallType(Type); Ball->FinishSpawning(At);
        if(LaunchNow) { Ball->Launch(FVector::ForwardVector,1000,EChaosImpactBallFlightMode::Straight); }
        // Exercise primary-only contact traits without consuming or installing any equipment.
        Ball->bSecondary=!LaunchNow; return Ball;
    }
    void Clear(UWorld* W)
    { for(TActorIterator<ARPGPrototypeBall> It(W);It;++It) { It->Destroy(); } for(TActorIterator<ARPGPrototypeEffect> It(W);It;++It) { It->Destroy(); } }
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    bool Travel=false; int32 Stage=0;
    float DirectHP=0,BeforeA=0,BeforeB=0;
    TWeakObjectPtr<ARPGPrototypeCharacter> A,B;
    TWeakObjectPtr<ARPGPrototypeEffect> Birds;
    FVector OldWind=FVector::ZeroVector;
    int32 ObservedPecks=0;
    float MaxWindMoved=0;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVanillaSpecialsTest,"ChaosImpact.RPGPrototype.VanillaSpecials",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FVanillaSpecialsTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FVanillaSpecialsCommand(this)); return true; }
#endif
