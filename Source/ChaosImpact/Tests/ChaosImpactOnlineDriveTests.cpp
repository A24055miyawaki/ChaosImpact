#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
// Two real processes: listen host with -CIAutoVersus=0:0:15:duel; client connects to its loopback port.
// Make the server release immediately while the client's hand animation still runs: the adoption race is deterministic.
class FOnlineDriveCommand : public IAutomationLatentCommand
{
public:
    explicit FOnlineDriveCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 100.)
        { Test->AddError(FString::Printf(TEXT("Online drive timeout at step %d"), Step)); return true; }
        UWorld* World = nullptr;
        for (const auto& Context : GEngine->GetWorldContexts())
        { if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; } }
        if (!World || World->GetNetMode() == NM_Standalone) { return false; }
        // The participant may finish/leave first; let the already-completed host finish its grace period.
        if (World->GetNetMode() == NM_ListenServer && Step >= 3) { return World->GetTimeSeconds() >= NextAt; }
        auto* Match = World->GetGameState<AChaosImpactGameState>();
        if (!Match || !Match->IsDuelLive()) { return false; }
        auto* Host = Match->Duel.Sides[0] ? Cast<AChaosImpactCharacter>(Match->Duel.Sides[0]->GetPawn()) : nullptr;
        auto* Remote = Match->Duel.Sides[1] ? Cast<AChaosImpactCharacter>(Match->Duel.Sides[1]->GetPawn()) : nullptr;
        if (!Host || !Remote) { return false; }
        const double Now = World->GetTimeSeconds();
        if (World->GetNetMode() == NM_ListenServer)
        {
            switch (Step)
            {
            case 0:
            {
                const FVector Centre = Match->StageCenter;
                const float Z = Remote->GetActorLocation().Z;
                Host->ResetForOnlineMatch(FVector(Centre.X - 1200, Centre.Y + 1200, Z), FRotator::ZeroRotator);
                Remote->ResetForOnlineMatch(FVector(Centre.X - 300, Centre.Y - 700, Z), FRotator(0, 90, 0));
                if (auto* Delay = FindFProperty<FFloatProperty>(Remote->GetClass(), TEXT("ThrowReleaseDelaySeconds")))
                { Delay->SetPropertyValue_InContainer(Remote, 0.f); }
                auto* Pickup = World->SpawnActor<AChaosImpactBall>(Remote->GetActorLocation() + FVector(0, 0, 500), FRotator::ZeroRotator);
                Pickup->SetBallType(EChaosImpactBallType::Drive); Pickup->MakePickup();
                Test->TestTrue(TEXT("Server grants drive ball"), Pickup->ConsumePickup(Remote));
                struct { int32 Count; uint8 Types; } Args{Remote->GetCarriedBallCount(), Remote->GetCarriedBallTypesPacked()};
                Remote->ProcessEvent(Remote->FindFunctionChecked(TEXT("ClientBallCountReset")), &Args);
                ++Step; break;
            }
            case 1:
                if (Remote->IsDriving()) { SawStart = true; }
                if (const auto* Ball = Remote->GetDrivenBall(); Ball && FVector::DotProduct(Ball->GetVelocity().GetSafeNormal2D(), FVector::ForwardVector) > .9f)
                {
                    Test->TestTrue(TEXT("Participant steering reaches the server and turns the real ball"), SawStart);
                    UE_LOG(LogTemp, Display, TEXT("DRIVENET server accepted participant steering"));
                    ++Step;
                }
                break;
            case 2:
                if (!Remote->IsDriving())
                {
                    Test->TestTrue(TEXT("The server ball stops on the participant's cancel"), !Remote->GetDrivenBall() || Remote->GetDrivenBall()->HasDetonated());
                    NextAt = Now + 2.; ++Step;
                }
                break;
            default: return Now >= NextAt;
            }
            return false;
        }

        auto* PC = Cast<AChaosImpactPlayerController>(Remote->GetController());
        if (!Remote->IsLocallyControlled() || !PC || !PC->IsGameplayActive()) { return false; }
        if (Step == 0)
        {
            if (Remote->GetCarriedBallCount() == 0 || Remote->GetCarriedBallType(0) != EChaosImpactBallType::Drive) { return false; }
            FVector2D Screen;
            if (PC->ProjectWorldLocationToScreen(Remote->GetActorLocation() + FVector(0, 500, 0), Screen))
            { PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y)); }
            Remote->SetAIAimDirection(FVector::RightVector);
            Remote->BeginThrowInput(); NextAt = Now + .15; ++Step;
        }
        else if (Step == 1 && Now >= NextAt)
        {
            Remote->SetAIAimDirection(FVector::RightVector);
            Remote->EndThrowInput(); NextAt = Now + .45; ++Step;
        }
        else if (Step == 2 && Now >= NextAt)
        {
            Test->TestTrue(TEXT("Owner remains in drive control after early server adoption"), Remote->IsDriving());
            Test->TestNotNull(TEXT("Owner has a live steerable ball"), Remote->GetDrivenBall());
            if (!Remote->IsDriving() || !Remote->GetDrivenBall()) { return true; }
            ThrowerAt = Remote->GetActorLocation(); NextAt = Now + .55; ++Step;
        }
        else if (Step == 3)
        {
            // World +X is screen-back for side 1. Exercise the real camera-relative move API.
            const FRotator Yaw(0, Remote->GetCameraBoom()->GetComponentRotation().Yaw, 0);
            Remote->DoMove(FVector::DotProduct(FVector::ForwardVector, FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)),
                FVector::DotProduct(FVector::ForwardVector, Yaw.Vector()));
            if (Now < NextAt) { return false; }
            Test->TestTrue(TEXT("Thrower stays rooted while steering"), FVector::Dist2D(ThrowerAt, Remote->GetActorLocation()) < 20.);
            Test->TestTrue(TEXT("Owner camera follows drive ball"), Remote->GetCameraBoom()->TargetOffset.Size2D() > 80.);
            Remote->StopDrivenBall(); NextAt = Now + .35; ++Step;
        }
        else if (Step == 4 && Now >= NextAt)
        {
            Test->TestFalse(TEXT("Participant can cancel steering"), Remote->IsDriving());
            return true;
        }
        return false;
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), NextAt = 0.;
    int32 Step = 0;
    bool SawStart = false;
    FVector ThrowerAt = FVector::ZeroVector;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactOnlineDriveTest, "ChaosImpact.Online.Drive",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactOnlineDriveTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FOnlineDriveCommand(this)); return true; }
#endif
