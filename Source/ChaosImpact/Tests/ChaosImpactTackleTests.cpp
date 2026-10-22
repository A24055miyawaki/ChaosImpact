#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactTackleComponent.h"
#include "ChaosImpactTackleVFX.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "InputKeyEventArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTackleBindings,"ChaosImpact.Tackle.Bindings",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FTackleBindings::RunTest(const FString&)
{
    using namespace ChaosImpactSettings;
    TestEqual(TEXT("Normal KBM tackle defaults to RMB"),GetDefaultKey(EChaosImpactDevice::KeyboardMouse,EChaosImpactAction::Tackle,0),EKeys::RightMouseButton);
    TestEqual(TEXT("Cancel has a separate E default"),GetDefaultKey(EChaosImpactDevice::KeyboardMouse,EChaosImpactAction::CancelThrow,0),EKeys::E);
    for(auto Device:{EChaosImpactDevice::Xbox,EChaosImpactDevice::PlayStation,EChaosImpactDevice::Switch})
    { TestEqual(TEXT("Normal pads use the free top face button"),GetDefaultKey(Device,EChaosImpactAction::Tackle,0),EKeys::Gamepad_FaceButton_Top); }
    TestEqual(TEXT("Tackle is named in the binding menu"),GetActionName(EChaosImpactAction::Tackle),FString(TEXT("タックル")));
    return true;
}
namespace
{
class FTackleCommand : public IAutomationLatentCommand
{
public:
    explicit FTackleCommand(FAutomationTestBase* T):Test(T) {}
    ~FTackleCommand() { RestoreKeys(); }
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Start>100) { Test->AddError(FString::Printf(TEXT("Tackle timeout %d"),Stage)); return true; }
        UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World()&&C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        if(Stage==0)
        {
            UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CITargets=0?CICPUCount=0?CILocalPlayers=2"));
            Stage=1; return false;
        }
        auto* PC=Cast<AChaosImpactPlayerController>(UGameplayStatics::GetPlayerController(W,0));
        auto* A=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
        auto* B=Cast<AChaosImpactCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
        if(!A || !B || !PC->IsGameplayActive()) { return false; }
        // Observe transient impact geometry every frame, not only after the screenshot delay:
        // shader/screenshot stalls can legitimately outlast the effect's short lifespan.
        if(Stage>=3 && Stage<=4)
        {
            for(TActorIterator<AChaosImpactTackleVFX> It(W);It;++It)
            { if(It->IsImpact() && It->GetVertexCount()>300) { bSawImpact=true; } }
        }
        const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
        auto Give=[&](AChaosImpactCharacter* To)
        {
            auto* Ball=W->SpawnActor<AChaosImpactBall>(To->GetActorLocation()+FVector(0,1000,0),FRotator::ZeroRotator);
            Ball->MakePickup(); Test->TestTrue(TEXT("Give test pickup"),Ball->ConsumePickup(To));
        };
        auto Key=[&](const FKey& K,EInputEvent E)
        { PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),K,E,E==IE_Released?0.f:1.f,false,FPlatformTime::Cycles64())); };
        switch(Stage)
        {
        case 1:
            Origin=A->GetActorLocation();
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            B->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
            A->SoloTeam=B->SoloTeam=-1;
            Give(B); Give(B); B->BeginThrowInput();
            Test->TestTrue(TEXT("Victim is charging before tackle"),B->IsChargingThrow());
            Stage=2; Next=Now+.2; return false;
        case 2:
            A->SetAIAimDirection(FVector::ForwardVector);
            Test->TestTrue(TEXT("Grounded tackle can start"),A->GetTackle()->Request());
            Test->TestFalse(TEXT("Tackle is not a dash/invulnerability"),A->IsDashing());
            Test->TestFalse(TEXT("Cannot immediately spam tackle"),A->GetTackle()->Request());
            Stage=3; Next=Now+.09; return false;
        case 3:
        {
            bool bRush=false;
            for(TActorIterator<AChaosImpactTackleVFX> It(W);It;++It)
            {
                if(!It->IsImpact())
                {
                    bRush=true;
                    Test->TestTrue(TEXT("Rush has visible world-space geometry"),It->GetVertexCount()>200);
                    Test->TestFalse(TEXT("Rush cosmetics cannot block players or balls"),It->GetActorEnableCollision());
                }
            }
            Test->TestTrue(TEXT("Tackle emits its rush effect"),bRush);
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("TackleQA/Shoulder-Rush.png")),true,false);
            Stage=4; Next=Now+.3; return false;
        }
        case 4:
        {
            Test->TestTrue(TEXT("Successful tackle emits pressure arcs and sparks"),bSawImpact);
            Test->TestEqual(TEXT("Exactly one of two held balls is dropped"),B->GetCarriedBallCount(),1);
            Test->TestFalse(TEXT("Hit cancels the throw charge"),B->IsChargingThrow());
            Test->TestEqual(TEXT("Tackle deals no HP damage"),B->GetHealth(),3.f);
            Test->TestTrue(TEXT("Victim is knocked over three metres from the hit position"),B->GetActorLocation().X>Origin.X+480);
            Test->TestTrue(TEXT("Tackle pays one stamina"),A->GetStamina()<4.1f);
            Stage=5; Next=Now+1.2; return false;
        }
        case 5:
            Test->TestFalse(TEXT("Tackle cosmetics automatically expire"),static_cast<bool>(TActorIterator<AChaosImpactTackleVFX>(W)));
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            B->ResetForOnlineMatch(Origin+FVector(180,0,0),FRotator::ZeroRotator);
            A->SoloTeam=B->SoloTeam=0; Give(B);
            Stage=6; Next=Now+.2; return false;
        case 6:
            A->SetAIAimDirection(FVector::ForwardVector); Test->TestTrue(TEXT("Second tackle ready after cooldown"),A->GetTackle()->Request());
            Stage=7; Next=Now+.5; return false;
        case 7:
            Test->TestEqual(TEXT("Teammate keeps the ball"),B->GetCarriedBallCount(),1);
            Test->TestEqual(TEXT("Teammate remains unharmed"),B->GetHealth(),3.f);
            Stage=8; Next=Now+1.2; return false;
        case 8:
        {
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            B->ResetForOnlineMatch(Origin+FVector(330,0,0),FRotator::ZeroRotator);
            A->SoloTeam=B->SoloTeam=-1; Give(B);
            auto* Barrier=W->SpawnActor<AStaticMeshActor>(Origin+FVector(165,0,0),FRotator::ZeroRotator);
            auto* Mesh=Barrier->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetWorldScale3D(FVector(.5f,4,3)); Mesh->SetCollisionProfileName(TEXT("BlockAll")); Wall=Barrier;
            Stage=9; Next=Now+.2; return false;
        }
        case 9:
            A->SetAIAimDirection(FVector::ForwardVector); A->GetTackle()->Request();
            Stage=10; Next=Now+.5; return false;
        case 10:
            Test->TestEqual(TEXT("No disarm through a blocking wall"),B->GetCarriedBallCount(),1);
            Test->TestTrue(TEXT("Rush doesn't pass through the wall"),A->GetActorLocation().X<Origin.X+160);
            if(Wall.IsValid()) { Wall->Destroy(); }
            A->SoloTeam=B->SoloTeam=-1;
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            B->ResetForOnlineMatch(Origin+FVector(400,0,0),FRotator::ZeroRotator);
            Profile=ChaosImpactSettings::GetPlayerProfile(PC); SavedProfile=ChaosImpactSettings::GetProfile(Profile); bRestoreKeys=true;
            ChaosImpactSettings::SetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse,EChaosImpactControlScheme::Normal);
            ChaosImpactSettings::SetKey(Profile,EChaosImpactDevice::KeyboardMouse,EChaosImpactAction::Tackle,0,EKeys::RightMouseButton);
            Stage=11; Next=Now+1.2; return false;
        case 11:
            A->SetAIAimDirection(FVector::ForwardVector); Key(EKeys::RightMouseButton,IE_Pressed);
            Test->TestTrue(TEXT("Actual normal RMB press starts tackle"),A->GetTackle()->IsActive());
            Key(EKeys::RightMouseButton,IE_Released);
            Stage=12; Next=Now+1.3; return false;
        case 12:
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            ChaosImpactSettings::SetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse,EChaosImpactControlScheme::Simple);
            PC->SetMouseLocation(200,200);
            Give(A); Stage=13; Next=Now+.2; return false;
        case 13:
            A->SetAIAimDirection(FVector::ForwardVector);
            Key(EKeys::LeftMouseButton,IE_Pressed); Key(EKeys::RightMouseButton,IE_Pressed);
            Test->TestTrue(TEXT("Simple throw+dash chord tackles"),A->GetTackle()->IsActive());
            Test->TestFalse(TEXT("Simple chord doesn't also dash"),A->IsDashing());
            Key(EKeys::LeftMouseButton,IE_Released); Key(EKeys::RightMouseButton,IE_Released);
            Stage=14; Next=Now+.5; return false;
        case 14:
            Test->TestEqual(TEXT("Simple chord doesn't also throw its ball"),A->GetCarriedBallCount(),1);
            ChaosImpactSettings::SetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse,EChaosImpactControlScheme::MouseOnly);
            PC->SetMouseLocation(200,200);
            Stage=15; Next=Now+1.2; return false;
        case 15:
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Key(EKeys::RightMouseButton,IE_Pressed); Key(EKeys::LeftMouseButton,IE_Pressed);
            Test->TestTrue(TEXT("Mouse-only RMB+LMB chord tackles"),A->GetTackle()->IsActive());
            Key(EKeys::LeftMouseButton,IE_Released); Key(EKeys::RightMouseButton,IE_Released);
            ChaosImpactSettings::SetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse,EChaosImpactControlScheme::MouseHold);
            Stage=16; Next=Now+1.3; return false;
        case 16:
            A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
            Key(EKeys::RightMouseButton,IE_Pressed); Key(EKeys::LeftMouseButton,IE_Pressed);
            Test->TestTrue(TEXT("Mouse-hold RMB+LMB chord tackles"),A->GetTackle()->IsActive());
            Key(EKeys::LeftMouseButton,IE_Released); Key(EKeys::RightMouseButton,IE_Released);
            RestoreKeys(); return true;
        }
        return false;
    }
private:
    void RestoreKeys()
    {
        if(!bRestoreKeys) { return; } bRestoreKeys=false;
        using namespace ChaosImpactSettings;
        for(int32 I=0;I<FChaosImpactProfile::ActionCount;++I) { for(int32 K=0;K<2;++K) { SetKey(Profile,EChaosImpactDevice::KeyboardMouse,static_cast<EChaosImpactAction>(I),K,FKey()); } }
        for(int32 I=0;I<FChaosImpactProfile::ActionCount;++I) { for(int32 K=0;K<2;++K) { SetKey(Profile,EChaosImpactDevice::KeyboardMouse,static_cast<EChaosImpactAction>(I),K,SavedProfile.Keys[0][I][K]); } }
        SetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse,SavedProfile.Schemes[0]);
    }
    FAutomationTestBase* Test;
    double Start=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0;
    FVector Origin=FVector::ZeroVector;
    TWeakObjectPtr<AStaticMeshActor> Wall;
    bool bRestoreKeys=false;
    bool bSawImpact=false;
    int32 Profile=0;
    FChaosImpactProfile SavedProfile;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTackleGameplay,"ChaosImpact.Tackle.Gameplay",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FTackleGameplay::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FTackleCommand(this)); return true; }
#endif
