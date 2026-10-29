#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiCatAssets, "ChaosImpact.Characters.KawaiiCat.Assets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiCatAssets::RunTest(const FString&)
{
    for (const int32 Index : {ChaosImpactRoster::CatIndex, ChaosImpactRoster::BirdIndex})
    {
        const auto& Info = ChaosImpactRoster::Get(Index);
        TestTrue(TEXT("New character is selectable"), UChaosImpactCharacterSelect::IsTileUnlocked(Index));
        TestFalse(TEXT("Supplied texture has no palette variants"), Info.bColourVariants);
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s/SK_%s"), Info.AssetFolder, Info.AssetPrefix));
        TestNotNull(TEXT("Supplied atlas is imported"), LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/T_%s"), Info.AssetFolder, Info.AssetPrefix)));
        if (!TestNotNull(TEXT("Model is imported"), Mesh)) { return false; }
        TestTrue(TEXT("Character has bounded, useful size"), Mesh->GetBounds().BoxExtent.Z > 40 && Mesh->GetBounds().BoxExtent.Z < 100);
        TestNotNull(TEXT("Model has its textured material"), Mesh->GetMaterials()[0].MaterialInterface.Get());
    }
    for (const TCHAR* Action : {TEXT("Idle"), TEXT("Appeal")})
    {
        auto* Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/Kawaii/A_Kawaii_%s"), Action));
        if (TestNotNull(TEXT("Bird authored motion exists"), Clip))
        { TestTrue(TEXT("Loop contains the exported movement"), Clip->GetPlayLength() > .8f && Clip->GetPlayLength() < .9f); }
    }
    return true;
}

namespace
{
class FKawaiiCatCommand : public IAutomationLatentCommand
{
public:
    explicit FKawaiiCatCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        UWorld* W = nullptr;
        for (const auto& Context : GEngine->GetWorldContexts())
        { if (Context.World() && Context.World()->IsGameWorld()) { W = Context.World(); break; } }
        auto* PC = W ? Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController()) : nullptr;
        auto* P = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
        auto* State = P ? P->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
        if (FPlatformTime::Seconds() - Started > 70.) { Test->AddError(TEXT("Kawaii/Cat gameplay timed out")); return true; }
        if (!P || !State || (!PC->IsGameplayActive() && Step < 10)) { return false; }
        // The close-up camera faces the model; do not let its cursor ray turn the fighter away from it.
        if (Step < 10)
        {
            P->SetAIAimDirection(FVector(300,220,0));
            P->SetActorRotation(FVector(300,220,0).Rotation());
        }
        if (Camera.IsValid())
        {
            const FVector Focus = P->GetActorLocation() + FVector(0,0,8);
            const FVector Eye = Focus + FVector(300,220,125);
            Camera->SetActorLocationAndRotation(Eye, (Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
        }
        FVector2D Screen;
        if (Step < 10 && PC->ProjectWorldLocationToScreen(P->GetActorLocation() + FVector(600,0,0), Screen))
        { PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y)); }
        if (bMove) { P->AddMovementInput(FVector::ForwardVector); }
        const double Now = Step < 10 ? W->GetTimeSeconds() : FPlatformTime::Seconds(); if (Now < NextAt) { return false; }
        auto* Puppet = P->FindComponentByClass<UChaosImpactPuppetComponent>();
        const auto Next = [&](float Delay) { ++Step; NextAt = (Step >= 10 ? FPlatformTime::Seconds() : Now) + Delay; return false; };
        const auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KawaiiCatQA"), Name), true, false); };
        const auto Give = [&]()
        {
            auto* Ball = W->SpawnActor<AChaosImpactBall>(P->GetActorLocation() + FVector(0,0,400), FRotator::ZeroRotator);
            Ball->MakePickup(); const bool Taken = P->TryPickupBall(Ball); Ball->Destroy(); return Taken;
        };
        switch (Step)
        {
        case 0:
            Original = State->CharacterIndex; State->CharacterIndex = ChaosImpactRoster::CatIndex;
            P->ResetForOnlineMatch(P->GetActorLocation(), FRotator::ZeroRotator);
            Camera = W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(48);
            return Next(.6f);
        case 1:
            Test->TestTrue(TEXT("Cat uses the skinned humanoid puppet"), Puppet && Puppet->IsReady() && !Puppet->HasAuthoredAnimations());
            Test->TestTrue(TEXT("Cat takes first ball"), Give()); Test->TestTrue(TEXT("Cat takes second ball"), Give());
            Shot(TEXT("Cat-Hold.png")); Before = P->GetActorLocation(); bMove = true; return Next(.3f);
        case 2:
            Test->TestTrue(TEXT("Cat moves normally"), FVector::Dist2D(Before,P->GetActorLocation()) > 15.);
            Shot(TEXT("Cat-Move.png")); bMove = false; P->GetCharacterMovement()->StopMovementImmediately();
            State->CharacterIndex = ChaosImpactRoster::BirdIndex;
            P->ResetForOnlineMatch(P->GetActorLocation(), FRotator::ZeroRotator); return Next(.6f);
        case 3:
            if (!Test->TestTrue(TEXT("Bird authored puppet is ready"), Puppet && Puppet->IsReady() && Puppet->GetAuthoredMesh())) { return true; }
            Test->TestTrue(TEXT("Bird takes first ball"), Give()); Test->TestTrue(TEXT("Bird takes second ball"), Give());
            {
                FVector R, L, Mouth;
                Test->TestTrue(TEXT("Right wing carry anchor exists"), Puppet->GetPalmLocation(true,R));
                Test->TestTrue(TEXT("Left wing carry anchor exists"), Puppet->GetPalmLocation(false,L));
                Test->TestTrue(TEXT("Mouth release anchor exists"), Puppet->GetAuthoredMouthLocation(Mouth));
                Test->TestTrue(TEXT("Balls have separate carry positions"), FVector::Dist(R,L) > 35.);
                Test->TestTrue(TEXT("Mouth is ahead of the character"), FVector::DotProduct((Mouth-P->GetActorLocation()).GetSafeNormal2D(), P->GetActorForwardVector()) > .8);
                UE_LOG(LogTemp,Display,TEXT("BIRD ANCHORS right=%s left=%s mouth=%s actor=%s"),*R.ToString(),*L.ToString(),*Mouth.ToString(),*P->GetActorLocation().ToString());
            }
            WingRotation = Puppet->GetAuthoredMesh()->GetSocketQuaternion(TEXT("WingR"));
            Shot(TEXT("Bird-Hold.png")); return Next(.12f);
        case 4:
            Test->TestFalse(TEXT("Source animation flaps the wing at idle"), Puppet->GetAuthoredMesh()->GetSocketQuaternion(TEXT("WingR")).Equals(WingRotation,.01));
            Puppet->PlaySelectionAppeal(); return Next(.13f);
        case 5:
            Test->TestEqual(TEXT("Selection plays the supplied bird motion"), Puppet->GetAuthoredState(), FName(TEXT("Appeal")));
            Shot(TEXT("Bird-Appeal.png")); Before = P->GetActorLocation(); bMove = true; return Next(.3f);
        case 6:
            Test->TestTrue(TEXT("Bird moves using ordinary movement"), FVector::Dist2D(Before,P->GetActorLocation()) > 15.);
            bMove = false; P->GetCharacterMovement()->StopMovementImmediately(); P->BeginThrowInput(); return Next(.2f);
        case 7:
            P->EndThrowInput(); return Next(.09f);
        case 8:
            Test->TestTrue(TEXT("Bird adds no swing pose"), Puppet->GetAuthoredState() != TEXT("Swing"));
            Shot(TEXT("Bird-Release.png")); return Next(.28f);
        case 9:
            Test->TestEqual(TEXT("Bird releases only one carried ball"), P->GetCarriedBallCount(), 1);
            {
                bool Found = false;
                for (TActorIterator<AChaosImpactBall> It(W); It; ++It) { Found |= It->GetThrowingPawn()==P && !It->IsPickup() && !It->GetAttachParentActor(); }
                Test->TestTrue(TEXT("Bird's ball is a real projectile"), Found);
            }
            State->CharacterIndex = Original; P->RequestCancelThrow(); PC->SetViewTarget(P); Camera->Destroy();
            PC->OpenCharacterSelect(); return Next(.8f);
        case 10:
        {
            auto* Select = PC->GetMenuWidget() ? PC->GetMenuWidget()->GetCharacterSelect() : nullptr;
            if (!Test->TestTrue(TEXT("Real character selection opens"), Select && Select->IsOpen())) { return true; }
            for (int32 Count = 0; Count < 8 && Select->GetCursorTile(0) != ChaosImpactRoster::CatIndex; ++Count)
            {
                const int32 Cursor = Select->GetCursorTile(0);
                Select->MoveCursor(0, FMath::Sign(ChaosImpactRoster::CatIndex % 4 - Cursor % 4),
                    FMath::Sign(ChaosImpactRoster::CatIndex / 4 - Cursor / 4));
            }
            Test->TestEqual(TEXT("Menu can highlight Cat"), Select->GetCharacter(0), ChaosImpactRoster::CatIndex);
            return Next(.8f);
        }
        case 11:
            Shot(TEXT("Cat-Select.png")); return Next(.2f);
        case 12:
            PC->GetMenuWidget()->GetCharacterSelect()->MoveCursor(0,1,0); return Next(.8f);
        case 13:
        {
            auto* Select = PC->GetMenuWidget()->GetCharacterSelect();
            Test->TestEqual(TEXT("Menu can highlight Bird"), Select->GetCharacter(0), ChaosImpactRoster::BirdIndex);
            Shot(TEXT("Bird-Select.png")); return Next(.2f);
        }
        case 14:
        {
            auto* Select = PC->GetMenuWidget()->GetCharacterSelect(); Select->PressConfirm(0);
            Test->TestTrue(TEXT("Bird confirms directly without colour choice"), Select->IsReady(0));
            PC->CancelCharacterSelection(); return true;
        }
        }
        return false;
    }
private:
    FAutomationTestBase* Test; double Started = FPlatformTime::Seconds(), NextAt = 0.;
    int32 Step = 0, Original = 0; bool bMove = false; FVector Before; FQuat WingRotation;
    TWeakObjectPtr<ACameraActor> Camera;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiCatGameplay, "ChaosImpact.Characters.KawaiiCat.Gameplay",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiCatGameplay::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FKawaiiCatCommand(this)); return true; }
#endif
