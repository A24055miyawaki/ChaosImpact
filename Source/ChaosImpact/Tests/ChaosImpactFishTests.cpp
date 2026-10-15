#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactGameState.h"
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
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishAssetsTest, "ChaosImpact.Characters.Maguro.Assets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFishAssetsTest::RunTest(const FString&)
{
    TestEqual(TEXT("Fish is appended without changing saved character choices"), FString(ChaosImpactRoster::Get(5).AssetPrefix), FString(TEXT("Fish")));
    TestFalse(TEXT("Fish keeps its supplied texture"), ChaosImpactRoster::Get(5).bColourVariants);
    TestTrue(TEXT("Fish is selectable"), UChaosImpactCharacterSelect::IsTileUnlocked(5));
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/ChaosImpact/Fish/SK_Fish"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/Fish/M_CI_Fish"));
    TestNotNull(TEXT("Texture"), LoadObject<UTexture2D>(nullptr, TEXT("/Game/ChaosImpact/Fish/T_Fish")));
    if (!TestNotNull(TEXT("Fish skeleton"), Mesh) || !TestNotNull(TEXT("Fish material"), Material)) { return false; }
    for (const auto& Slot : Mesh->GetMaterials()) { TestTrue(TEXT("Every mesh slot uses the supplied texture"), Slot.MaterialInterface == Material); }
    for (const TCHAR* Name : {TEXT("Swim"), TEXT("Appeal"), TEXT("Ball"), TEXT("Nova"), TEXT("Swing")})
    {
        auto* Clip = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/Fish/A_Fish_%s"), Name));
        if (TestNotNull(FString::Printf(TEXT("Authored %s clip"), Name), Clip))
        {
            TestTrue(TEXT("Clip belongs to Fish skeleton"), Clip->GetSkeleton() == Mesh->GetSkeleton());
            TestTrue(TEXT("Clip has a useful length"), Clip->GetPlayLength() > 0.25f);
        }
    }
    for (const TCHAR* Name : {TEXT("joint1"), TEXT("joint5"), TEXT("joint8"), TEXT("joint10")})
    { TestTrue(TEXT("Body and fin bones exist"), Mesh->GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE); }
    return true;
}

namespace
{
class FFishGameplayCommand : public IAutomationLatentCommand
{
public:
    explicit FFishGameplayCommand(FAutomationTestBase* InTest) : Test(InTest) {}
    bool Update() override
    {
        AChaosImpactPlayerController* PC = nullptr;
        for (const auto& Context : GEngine->GetWorldContexts())
        { if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); } }
        AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
        auto* State = Player ? Player->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
        if (FPlatformTime::Seconds() - Started > 100)
        { Test->AddError(FString::Printf(TEXT("Fish test timed out at step %d"), Step)); Cleanup(Player, State, PC); return true; }
        if (!Player || !State || !PC->IsGameplayActive()) { return false; }
        UWorld* World = Player->GetWorld();
        if (bMove) { Player->AddMovementInput(FVector::ForwardVector, 1.0f); }
        if (Camera.IsValid())
        {
            const FVector Focus = Player->GetActorLocation() + FVector(0, 0, 8);
            const FVector Eye = Focus + FVector(330, 230, 150);
            Camera->SetActorLocationAndRotation(Eye, (Focus-Eye).Rotation());
            PC->SetViewTarget(Camera.Get());
        }
        const double Now = World->GetTimeSeconds();
        if (Now < NextAt) { return false; }
        auto* Puppet = Player->FindComponentByClass<UChaosImpactPuppetComponent>();
        auto* Skin = Puppet ? Puppet->GetAuthoredMesh() : nullptr;
        const auto Next = [&](float Seconds) { ++Step; NextAt = Now + Seconds; return false; };
        const auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FishQA"), Name), true, false); };
        const auto Give = [&](EChaosImpactBallType Type)
        {
            auto* Ball = World->SpawnActor<AChaosImpactBall>(Player->GetActorLocation() + FVector(0,0,500), FRotator::ZeroRotator);
            Ball->SetBallType(Type); Ball->MakePickup(); const bool bTaken = Player->TryPickupBall(Ball); Ball->Destroy(); return bTaken;
        };
        switch (Step)
        {
        case 0:
            OriginalCharacter = State->CharacterIndex; OriginalColour = State->ColourChoice;
            State->CharacterIndex = 5; State->ColourChoice = 0; State->ForceNetUpdate();
            Player->ResetForOnlineMatch(Player->GetActorLocation(), FRotator::ZeroRotator);
            Camera = World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(48);
            return Next(0.6f);
        case 1:
            if (!Test->TestTrue(TEXT("Native Fish model is ready"), Puppet && Puppet->IsReady() && Skin)) { Cleanup(Player, State, PC); return true; }
            Test->TestEqual(TEXT("Fish is the active playable character"), Player->GetShownCharacter(), 5);
            Test->TestTrue(TEXT("Fish bounds remain close to its collision body"), FVector::Dist(Skin->Bounds.Origin,Player->GetActorLocation()) < 180);
            Test->TestTrue(TEXT("First ball pickup"), Give(EChaosImpactBallType::Normal));
            Test->TestTrue(TEXT("Second ball pickup"), Give(EChaosImpactBallType::Normal));
            for (bool Right : {false,true})
            {
                FVector Palm, Mouth;
                Test->TestTrue(TEXT("Fin supports a held ball"), Puppet->GetPalmLocation(Right,Palm));
                Test->TestTrue(TEXT("Mouth provides an animated release point"), Puppet->GetAuthoredMouthLocation(Mouth));
                Test->TestTrue(TEXT("Held ball stays close"),FVector::Dist(Palm,Player->GetActorLocation())<210);
                Test->TestTrue(TEXT("Ball waits at a side fin rather than in the mouth"),FVector::Dist(Palm,Mouth)>30);
            }
            StartLocation = Player->GetActorLocation(); IdleTail = Skin->GetBoneTransform(Skin->GetBoneIndex(TEXT("joint1")));
            Shot(TEXT("Hold.png")); bMove = true; return Next(0.35f);
        case 2:
            Test->TestTrue(TEXT("Moves with the ordinary movement system"), FVector::Dist(StartLocation,Player->GetActorLocation())>20);
            Test->TestEqual(TEXT("Movement plays Swim"), Puppet->GetAuthoredState(), FName(TEXT("Swim")));
            Test->TestFalse(TEXT("Tail moves rather than staying in bind pose"),Skin->GetBoneTransform(Skin->GetBoneIndex(TEXT("joint1"))).Equals(IdleTail,.01f));
            Shot(TEXT("Swim.png")); bMove=false; Player->GetCharacterMovement()->StopMovementImmediately(); Player->BeginThrowInput(); return Next(.25f);
        case 3:
            Test->TestTrue(TEXT("Can charge a normal throw"),Player->IsChargingThrow());
            Player->EndThrowInput(); return Next(.07f);
        case 4:
            Test->TestEqual(TEXT("Throw uses Swing"),Puppet->GetAuthoredState(),FName(TEXT("Swing")));
            Shot(TEXT("Swing.png")); return Next(.55f);
        case 5:
            Test->TestEqual(TEXT("Throw releases exactly one ball"),Player->GetCarriedBallCount(),1);
            Player->RequestDropBall(); Test->TestTrue(TEXT("Pick up Nova"),Give(EChaosImpactBallType::Nova));
            Player->BeginThrowInput(); return Next(1.1f);
        case 6:
            Test->TestTrue(TEXT("Can charge Nova"),Player->IsChargingNova());
            Test->TestEqual(TEXT("Nova holds the authored charging pose"),Puppet->GetAuthoredState(),FName(TEXT("NovaCharge")));
            Shot(TEXT("NovaCharge.png")); Player->EndThrowInput(); return Next(.07f);
        case 7:
            Test->TestEqual(TEXT("Release finishes Nova clip"),Puppet->GetAuthoredState(),FName(TEXT("NovaThrow")));
            Shot(TEXT("NovaThrow.png")); return Next(.8f);
        case 8:
            Test->TestTrue(TEXT("Real ball transformation is available"),Player->GetBallForm()->Toggle());
            return Next(.3f);
        case 9:
            Test->TestTrue(TEXT("Fish really becomes a teammate ball"),Player->IsInBallForm());
            Test->TestEqual(TEXT("Transformation uses Ball clip"),Puppet->GetAuthoredState(),FName(TEXT("Ball")));
            Shot(TEXT("Ball.png")); return Next(.4f);
        case 10:
            Test->TestTrue(TEXT("Ball form can be restored"),Player->GetBallForm()->Toggle());
            return Next(.45f);
        case 11:
            Test->TestFalse(TEXT("Restored fish is not in ball form"),Player->IsInBallForm());
            Test->TestTrue(TEXT("Restored mesh has sane bounds"),Skin->Bounds.BoxExtent.Size()<250);
            StartLocation=Player->GetActorLocation(); Player->RequestAIDash(FVector::ForwardVector); return Next(.25f);
        case 12:
            Test->TestTrue(TEXT("Fish supports short dash"),FVector::Dist(StartLocation,Player->GetActorLocation())>10);
            Cleanup(Player,State,PC); return true;
        }
        return false;
    }
private:
    void Cleanup(AChaosImpactCharacter* Player,AChaosImpactPlayerState* State,AChaosImpactPlayerController* PC)
    {
        bMove=false;
        if (Player) { Player->GetBallForm()->Restore(false); Player->RequestCancelThrow(); }
        if (State && OriginalCharacter>=0) { State->CharacterIndex=OriginalCharacter; State->ColourChoice=OriginalColour; State->ForceNetUpdate(); }
        if (PC && Player) { PC->SetViewTarget(Player); }
        if (Camera.IsValid()) { Camera->Destroy(); }
    }
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),NextAt=0;
    int32 Step=0,OriginalCharacter=-1,OriginalColour=-1;
    bool bMove=false;
    FVector StartLocation;
    FTransform IdleTail;
    TWeakObjectPtr<ACameraActor> Camera;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishGameplayTest,"ChaosImpact.Characters.Maguro.Gameplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFishGameplayTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFishGameplayCommand(this)); return true; }
#endif
