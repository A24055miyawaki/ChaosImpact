#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "RPGPrototype/RPGPrototype.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
struct FJumpSample
{
    TWeakObjectPtr<AChaosImpactCharacter> Pawn;
    FString Name;
    float FloorZ=0,Peak=0,AirSeconds=0,ExpectedHeight=0;
    bool SawAir=false,Landed=false,Released=false;
};

class FSnappyJumpCommand : public IAutomationLatentCommand
{
public:
    explicit FSnappyJumpCommand(FAutomationTestBase* InTest):Test(InTest) {}
    bool Update() override
    {
        UWorld* World=nullptr;
        for(const auto& Context:GEngine->GetWorldContexts())
        { if(Context.World() && Context.World()->IsGameWorld()) { World=Context.World(); break; } }
        if(FPlatformTime::Seconds()-Started>90)
        { Test->AddError(FString::Printf(TEXT("Jump test timed out at stage %d"),Stage)); Cleanup(); return true; }
        if(!World) { return false; }
        if(Stage==0)
        {
            UGameplayStatics::OpenLevel(World,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,
                TEXT("CITraining=1?CITargets=0?CICPUCount=0?CILocalPlayers=1?CIKeyboardPlayer=0"));
            Stage=1; return false;
        }
        auto* PC=Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
        auto* Player=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
        if(!PC || !Player || !PC->IsGameplayActive()) { return false; }
        const double Now=World->GetTimeSeconds();
        if(Stage==1)
        {
            Test->TestEqual(TEXT("Existing BP player uses the new tempo"),Player->GetVerticalImpulseTempo(),1.7f);
            Floor=World->SpawnActor<AActor>();
            auto* Box=NewObject<UBoxComponent>(Floor.Get()); Floor->SetRootComponent(Box);
            Box->SetBoxExtent({1500,1000,25}); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent(); Floor->SetActorLocation({0,0,2400});
            for(int32 I=0;I<5;++I)
            {
                UClass* Class=I<3?Player->GetClass():ARPGPrototypeCharacter::StaticClass();
                const FVector At(-800+I*400,0,2523);
                auto* Pawn=World->SpawnActorDeferred<AChaosImpactCharacter>(Class,FTransform(At),nullptr,nullptr,
                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                Pawn->AutoPossessAI=EAutoPossessAI::Disabled; Pawn->JumpTempo=I==0?1.f:1.7f;
                if(I==4) { Cast<ARPGPrototypeCharacter>(Pawn)->SetEnemy(true,1); }
                Pawn->FinishSpawning(FTransform(At));
                auto* Move=Pawn->GetCharacterMovement(); Move->bRunPhysicsWithNoController=true;
                Move->SetMovementMode(MOVE_Falling);
                if(I==2 || I==4)
                {
                    AController* CPU=I==2?static_cast<AController*>(World->SpawnActor<AChaosImpactCPUController>()):
                        static_cast<AController*>(World->SpawnActor<ARPGPrototypeGruntController>());
                    CPU->Possess(Pawn); CPU->SetActorTickEnabled(false); Controllers.Add(CPU);
                }
                const TCHAR* Names[]={TEXT("Reference old arc"),TEXT("BP player tap"),TEXT("BP CPU held"),TEXT("RPG player"),TEXT("RPG grunt")};
                FJumpSample Sample; Sample.Pawn=Pawn; Sample.Name=Names[I];
                Sample.ExpectedHeight=FMath::Square(Move->JumpZVelocity)/(2*FMath::Abs(Move->GetGravityZ()));
                Samples.Add(Sample);
                Test->TestEqual(Sample.Name+TEXT(" has no hold-to-float"),Pawn->JumpMaxHoldTime,0.f);
                if(I>0) { Test->TestEqual(Sample.Name+TEXT(" tempo"),Pawn->GetVerticalImpulseTempo(),1.7f); }
            }
            Stage=2; Next=Now+.4; return false;
        }
        if(Stage==2)
        {
            if(Now<Next) { return false; }
            for(const auto& Sample:Samples)
            {
                if(!Sample.Pawn->GetCharacterMovement()->IsMovingOnGround())
                {
                    if(Now<Next+3) { return false; }
                    Test->AddError(FString::Printf(TEXT("%s did not settle: %s, movement %d"),*Sample.Name,
                        *Sample.Pawn->GetActorLocation().ToString(),static_cast<int32>(Sample.Pawn->GetCharacterMovement()->MovementMode)));
                    Cleanup(); return true;
                }
            }
            for(auto& Sample:Samples)
            {
                Sample.FloorZ=Sample.Pawn->GetActorLocation().Z; Sample.Pawn->Jump();
            }
            JumpAt=Now; Stage=3; return false;
        }
        if(Stage==3)
        {
            bool AllLanded=true;
            for(int32 I=0;I<Samples.Num();++I)
            {
                auto& Sample=Samples[I]; auto* Pawn=Sample.Pawn.Get();
                if(!Pawn) { Test->AddError(TEXT("Jump sample destroyed")); Cleanup(); return true; }
                if(Sample.Landed) { continue; }
                const bool Falling=Pawn->GetCharacterMovement()->IsFalling();
                Sample.SawAir|=Falling; Sample.Peak=FMath::Max(Sample.Peak,static_cast<float>(Pawn->GetActorLocation().Z-Sample.FloorZ));
                if(I==1 && Falling && !Sample.Released) { Pawn->StopJumping(); Sample.Released=true; }
                if(Sample.SawAir && !Falling)
                { Sample.Landed=true; Sample.AirSeconds=Now-JumpAt; Pawn->StopJumping(); }
                else { AllLanded=false; }
            }
            if(!AllLanded && Now-JumpAt<4) { return false; }
            for(int32 I=0;I<Samples.Num();++I)
            {
                const auto& S=Samples[I];
                Test->AddInfo(FString::Printf(TEXT("%s: height %.1f cm, airtime %.3f seconds"),*S.Name,S.Peak,S.AirSeconds));
                Test->TestTrue(S.Name+TEXT(" jumps and lands"),S.SawAir && S.Landed);
                Test->TestTrue(S.Name+TEXT(" retains intended jump height"),FMath::IsNearlyEqual(S.Peak,S.ExpectedHeight,FMath::Max(8.f,S.ExpectedHeight*.07f)));
            }
            for(int32 I=1;I<3;++I)
            {
                Test->TestTrue(TEXT("BP player and CPU land at least 30 percent sooner"),Samples[I].AirSeconds<Samples[0].AirSeconds*.7f);
                Test->TestTrue(TEXT("Existing BP height is preserved"),FMath::Abs(Samples[I].Peak-Samples[0].Peak)<8);
            }
            Test->TestTrue(TEXT("Short tap and CPU hold have the same arc"),FMath::Abs(Samples[1].AirSeconds-Samples[2].AirSeconds)<.04f);
            Cleanup(); return true;
        }
        return false;
    }
private:
    void Cleanup()
    {
        for(auto& CPU:Controllers) { if(CPU.IsValid()) { CPU->Destroy(); } }
        for(auto& S:Samples) { if(S.Pawn.IsValid()) { S.Pawn->Destroy(); } }
        if(Floor.IsValid()) { Floor->Destroy(); }
    }
    FAutomationTestBase* Test;
    int32 Stage=0;
    double Started=FPlatformTime::Seconds(),Next=0,JumpAt=0;
    TWeakObjectPtr<AActor> Floor;
    TArray<FJumpSample> Samples;
    TArray<TWeakObjectPtr<AController>> Controllers;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSnappyJumpTest,"ChaosImpact.Movement.SnappyJump",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FChaosImpactSnappyJumpTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FSnappyJumpCommand(this)); return true; }
#endif
