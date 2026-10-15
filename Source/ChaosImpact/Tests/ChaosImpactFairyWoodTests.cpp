#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactTrainingArena.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFairyWoodAssetsTest,"ChaosImpact.Characters.FairyWood.Assets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFairyWoodAssetsTest::RunTest(const FString&)
{
    TestEqual(TEXT("Old character indices stay unchanged"),FString(ChaosImpactRoster::Get(0).AssetPrefix),FString(TEXT("Player")));
    TestEqual(TEXT("Old bird index stays unchanged"),FString(ChaosImpactRoster::Get(1).AssetPrefix),FString(TEXT("Simae")));
    for (int32 Index : {2,3})
    {
        const auto& Info=ChaosImpactRoster::Get(Index);
        TestFalse(TEXT("New character has no colour variants"),Info.bColourVariants);
        TestTrue(TEXT("Character tile is selectable"),UChaosImpactCharacterSelect::IsTileUnlocked(Index));
        const FString Base=FString(Info.AssetFolder)+TEXT("/");
        auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*(Base+TEXT("SK_")+Info.AssetPrefix));
        auto* Texture=LoadObject<UTexture2D>(nullptr,*(Base+TEXT("T_")+Info.AssetPrefix));
        auto* Material=LoadObject<UMaterialInterface>(nullptr,*(Base+TEXT("M_CI_")+Info.AssetPrefix));
        if (!TestNotNull(TEXT("Game-ready skeletal mesh"),Mesh) || !TestNotNull(TEXT("Supplied texture"),Texture)
            || !TestNotNull(TEXT("Fixed texture material"),Material)) { continue; }
        const double Height=Mesh->GetBounds().BoxExtent.Z*2;
        TestTrue(TEXT("Both models fit the existing playable-character size"),Height>120 && Height<160);
        for (const TCHAR* Name : {TEXT("Character1_Hips"),TEXT("Character1_Spine"),TEXT("Character1_Spine1"),
            TEXT("Character1_Spine2"),TEXT("Character1_Neck"),TEXT("Character1_Head"),TEXT("Character1_LeftShoulder"),
            TEXT("Character1_RightShoulder"),TEXT("Character1_LeftArm"),TEXT("Character1_LeftForeArm"),
            TEXT("Character1_LeftHand"),TEXT("Character1_RightArm"),TEXT("Character1_RightForeArm"),
            TEXT("Character1_RightHand"),TEXT("Character1_LeftUpLeg"),TEXT("Character1_LeftLeg"),TEXT("Character1_LeftFoot"),
            TEXT("Character1_RightUpLeg"),TEXT("Character1_RightLeg"),TEXT("Character1_RightFoot")})
        { TestTrue(FString::Printf(TEXT("%s has joint %s"),Info.AssetPrefix,Name),Mesh->GetRefSkeleton().FindBoneIndex(Name)!=INDEX_NONE); }
        FLinearColor Unused;
        TestFalse(TEXT("No team-colour tint can overwrite the provided texture"),Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BodyColor")),Unused));
        for (int32 Slot=0;Slot<Mesh->GetMaterials().Num();++Slot)
        {
            auto* Expected=Index==3 && Slot==0 ? LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ChaosImpact/Wood/M_CI_Wood_Leaf")) : Material;
            TestNotNull(TEXT("Each source material is present"),Expected);
            TestTrue(TEXT("Texture and original green leaf material are assigned to their own slots"),Mesh->GetMaterials()[Slot].MaterialInterface==Expected);
        }
    }
    // A first-ever imported material may still be compiling; do not capture default grey fallback meshes.
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
    return true;
}

namespace
{
    AChaosImpactPlayerController* FindPC()
    {
        for (const auto& C:GEngine->GetWorldContexts())
            if (C.World() && C.World()->IsGameWorld()) return Cast<AChaosImpactPlayerController>(C.World()->GetFirstPlayerController());
        return nullptr;
    }
    void Shot(const FString& Name,const FString& Folder=TEXT("FairyWoodQA"))
    { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),Folder,Name),true,false); }

    class FFairyWoodMenuCommand : public IAutomationLatentCommand
    {
    public:
        explicit FFairyWoodMenuCommand(FAutomationTestBase* T,int32 First=2,int32 Second=3,const TCHAR* Folder=TEXT("FairyWoodQA"))
            :Test(T),FirstCharacter(First),SecondCharacter(Second),Directory(Folder) {}
        bool Update() override
        {
            auto* PC=FindPC();
            auto* Select=PC && PC->GetMenuWidget()?PC->GetMenuWidget()->GetCharacterSelect():nullptr;
            const double Now=FPlatformTime::Seconds();
            if (Now-Started>100) { Test->AddError(TEXT("New character select timed out")); Cleanup(PC); return true; }
            if (!Select || Now<NextAt) return false;
            const auto Next=[this,Now](double Delay) { ++Stage; NextAt=Now+Delay; return false; };
            const auto Key=[Select](FKey K) { Select->HandleKeyDown(FKeyEvent(K,FModifierKeysState(),0,false,0,0)); };
            const auto Pad=[](FKey K) {
                const auto D=FInputDeviceId::CreateFromInternalId(801);
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),D,false,0,0,TOptional<int32>(1)));
                FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),D,false,0,0,TOptional<int32>(1)));
            };
            const auto GoTo=[Select,&Key,&Pad](int32 Player,int32 Target) {
                for (int32 Turn=0;Turn<10 && Select->GetCursorTile(Player)!=Target;++Turn) {
                    const int32 Current=Select->GetCursorTile(Player);
                    const bool Horizontal=Current%4!=Target%4;
                    const bool Positive=Horizontal ? Current%4<Target%4 : Current/4<Target/4;
                    if (Player==0) Key(Horizontal ? (Positive?EKeys::D:EKeys::A) : (Positive?EKeys::S:EKeys::W));
                    else Pad(Horizontal ? (Positive?EKeys::Gamepad_DPad_Right:EKeys::Gamepad_DPad_Left)
                                        : (Positive?EKeys::Gamepad_DPad_Down:EKeys::Gamepad_DPad_Up));
                }
            };
            switch(Stage)
            {
            case 0:
                if (!PC->IsGameplayActive()) return false;
                for (int I=0;I<4;++I) {
                    Originals.Add(UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(I));
                    UChaosImpactLoadoutSubsystem::Get(PC)->SetLoadout(I,{0,I});
                }
                PC->BeginVersusLocal(); PC->PrepareTrainingControllerAssignment(2);
                PC->RegisterKeyboardMouseJoin(); PC->RegisterControllerJoin(801,1); PC->ConfirmControllerAssignments();
                Test->TestEqual(TEXT("Real assignment flow opens two character cards"),Select->GetPlayerCount(),2);
                GoTo(0,FirstCharacter); Key(EKeys::Enter);
                Test->TestEqual(TEXT("Keyboard selects the new character"),Select->GetCharacter(0),FirstCharacter);
                Test->TestTrue(TEXT("Fixed-colour Fairy skips colour selection"),Select->IsReady(0));
                GoTo(1,SecondCharacter);
                Pad(EKeys::Gamepad_FaceButton_Bottom);
                Test->TestEqual(TEXT("Second controller selects the other character"),Select->GetCharacter(1),SecondCharacter);
                Test->TestTrue(TEXT("Fixed-colour Wood skips colour selection"),Select->IsReady(1));
                return Next(.8);
            case 1:
                Test->TestTrue(TEXT("Both new characters can be ready together"),Select->AreAllReady());
                Test->TestEqual(TEXT("First choice is saved"),UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(0).Character,FirstCharacter);
                Test->TestEqual(TEXT("Second choice is saved"),UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(1).Character,SecondCharacter);
                Shot(TEXT("Menu-TwoCharacters.png"),Directory);
                return Next(.3);
            case 2:
                Select->PressBack(0);
                Test->TestTrue(TEXT("Cancel returns directly to character icons"),Select->GetStep(0)==UChaosImpactCharacterSelect::EStep::Character);
                Test->TestTrue(TEXT("Mouse can pick the new character too"),Select->HandleClick(FVector2D(76+(FirstCharacter%4)*204+40,220+(FirstCharacter/4)*204)));
                Test->TestTrue(TEXT("Mouse also skips the non-existent colour variants"),Select->IsReady(0));
                Select->PressBack(1); GoTo(1,FirstCharacter); Pad(EKeys::Gamepad_FaceButton_Bottom);
                Test->TestTrue(TEXT("Two players may choose the same fixed-colour character"),Select->GetCharacter(0)==FirstCharacter && Select->GetCharacter(1)==FirstCharacter && Select->AreAllReady());
                Test->TestNotEqual(TEXT("Same fixed appearance still has distinct HUD identity"),Select->GetColour(0),Select->GetColour(1));
                return Next(.5);
            case 3:
                Shot(TEXT("Menu-SameCharacter.png"),Directory);
                return Next(.3);
            case 4:
                Select->PressBack(1); GoTo(1,SecondCharacter); Pad(EKeys::Gamepad_FaceButton_Bottom);
                Select->PressConfirm(0); return Next(1);
            case 5:
                Test->TestTrue(TEXT("New characters advance through the normal VS flow"),PC->GetCurrentScreen()==EChaosImpactScreen::MatchRules);
                PC->ConfirmMatchRules(); return Next(2);
            case 6:
            {
                if (!PC->GetWorld()->URL.HasOption(TEXT("CIMatch=1")) || PC->GetGameInstance()->GetLocalPlayers().Num()<2) return false;
                const auto* Match=PC->GetWorld()->GetGameState<AChaosImpactGameState>();
                if (!Match || Match->Phase!=EChaosImpactOnlinePhase::Match) return false;
                for (int32 I=0;I<2;++I) {
                    auto* Local=PC->GetGameInstance()->GetLocalPlayers()[I]->GetPlayerController(PC->GetWorld());
                    auto* Pawn=Local?Cast<AChaosImpactCharacter>(Local->GetPawn()):nullptr;
                    auto* Model=Pawn?Pawn->FindComponentByClass<UChaosImpactPuppetComponent>():nullptr;
                    Test->TestTrue(TEXT("A real VS pawn displays each selected new character"),Pawn && Pawn->GetShownCharacter()==(I==0?FirstCharacter:SecondCharacter) && Model && Model->IsReady());
                }
                return Next(.5);
            }
            case 7:
                Shot(TEXT("VS-TwoCharacters.png"),Directory); return Next(.3);
            case 8:
                Cleanup(PC); return true;
            }
            return false;
        }
    private:
        void Cleanup(AChaosImpactPlayerController* PC) {
            if (!PC) return;
            for (int I=0;I<Originals.Num();++I) UChaosImpactLoadoutSubsystem::Get(PC)->SetLoadout(I,Originals[I]);
            Originals.Reset(); PC->ShowMenuScreen(EChaosImpactScreen::Pause); PC->ResumeGameplay();
        }
        FAutomationTestBase* Test;
        int32 FirstCharacter,SecondCharacter;
        FString Directory;
        TArray<FChaosImpactLoadout> Originals;
        int32 Stage=0;
        double Started=FPlatformTime::Seconds(),NextAt=0;
    };

    class FFairyWoodGameplayCommand : public IAutomationLatentCommand
    {
    public:
        explicit FFairyWoodGameplayCommand(FAutomationTestBase* T,int32 First=2,int32 Count=2,const TCHAR* Folder=TEXT("FairyWoodQA"))
            :Test(T),FirstCharacter(First),CharacterCount(Count),Directory(Folder) {}
        bool Update() override
        {
            auto* PC=FindPC(); auto* Player=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
            auto* State=PC?PC->GetPlayerState<AChaosImpactPlayerState>():nullptr;
            const double WallNow=FPlatformTime::Seconds();
            if (WallNow-Started>100) { Test->AddError(TEXT("Character gameplay timed out")); Cleanup(PC); return true; }
            if (!Player || !State || !PC->IsGameplayActive()) return false;
            UWorld* World=PC->GetWorld();
            // Asset/texture hitches must not consume the entire walking interval without any simulation frames.
            const double Now=World->GetTimeSeconds();
            if (bMove) Player->AddMovementInput(FVector::ForwardVector,1);
            if (Camera.IsValid()) {
                const FVector Focus=Player->GetActorLocation()+FVector(0,0,10);
                const FVector Eye=Focus+Player->GetActorRotation().RotateVector(FVector(460,130,70));
                Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
            }
            if (Now<NextAt) return false;
            const auto Next=[this,Now](double Delay) { ++Stage; NextAt=Now+Delay; return false; };
            const auto Pic=[this](const TCHAR* Name) { Shot(FString(ChaosImpactRoster::Get(FirstCharacter+Iteration).AssetPrefix)+TEXT("-")+Name,Directory); };
            auto* Puppet=Player->FindComponentByClass<UChaosImpactPuppetComponent>();
            UPoseableMeshComponent* Skin=Player->FindComponentByClass<UPoseableMeshComponent>();
            const auto Give=[World,Player]() {
                auto* Ball=World->SpawnActor<AChaosImpactBall>(Player->GetActorLocation()+FVector(0,0,500),FRotator::ZeroRotator);
                Ball->MakePickup(); const bool Taken=Player->TryPickupBall(Ball); Ball->Destroy(); return Taken;
            };
            switch(Stage)
            {
            case 0:
                OriginalCharacter=State->CharacterIndex; OriginalColour=State->ColourChoice;
                for (TActorIterator<AChaosImpactCPUController> It(World);It;++It) { CPUs.Add(*It); It->SetActorTickEnabled(false); }
                for (TActorIterator<AChaosImpactBallSpawner> It(World);It;++It) It->Destroy();
                for (TActorIterator<AChaosImpactBall> It(World);It;++It) It->Destroy();
                Origin=Player->GetActorLocation();
                for (TActorIterator<AChaosImpactTrainingArena> It(World);It;++It) { Origin=It->GetCenter()+FVector(-350,-350,100); break; }
                Camera=World->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(50);
                State->CharacterIndex=FirstCharacter; State->ColourChoice=0; State->ForceNetUpdate();
                Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator); return Next(1);
            case 1:
                if (!Puppet || !Puppet->IsReady() || !Skin) { Test->AddError(TEXT("New puppet did not initialize")); Cleanup(PC); return true; }
                Test->TestEqual(TEXT("Selected character is actually shown in game"),Player->GetShownCharacter(),FirstCharacter+Iteration);
                Test->TestTrue(TEXT("Skin bounds remain sane after retargeting"),Skin->Bounds.BoxExtent.Size()<400);
                Test->TestTrue(TEXT("First ball is picked up"),Give()); Test->TestTrue(TEXT("Second ball is picked up"),Give());
                Puppet->UpdatePose(true);
                for (bool Right:{false,true}) {
                    FVector Palm; Test->TestTrue(TEXT("Both hands provide a held-ball position"),Puppet->GetPalmLocation(Right,Palm));
                    Test->TestTrue(TEXT("Palm is near the player's body"),FVector::Dist(Palm,Player->GetActorLocation())<220);
                }
                Pic(TEXT("Hold.png")); StartLocation=Player->GetActorLocation(); bMove=true;
                IdleFoot=Skin->GetBoneTransformByName(TEXT("Character1_LeftFoot"),EBoneSpaces::ComponentSpace); return Next(.45);
            case 2:
                bMove=false;
                Test->TestTrue(TEXT("New character walks with the normal movement system"),FVector::Dist(Player->GetActorLocation(),StartLocation)>20);
                Test->TestFalse(TEXT("Legs animate instead of staying in bind pose"),Skin->GetBoneTransformByName(TEXT("Character1_LeftFoot"),EBoneSpaces::ComponentSpace).Equals(IdleFoot,.1));
                if (!bRunShotTaken) { Pic(TEXT("Run.png")); bRunShotTaken=true; NextAt=Now+.1; bMove=true; return false; }
                bMove=false; bRunShotTaken=false; Player->Jump(); return Next(.18);
            case 3:
                Test->TestTrue(TEXT("Jump uses the usual playable-character movement"),Player->GetCharacterMovement()->IsFalling());
                Pic(TEXT("Jump.png")); Player->StopJumping(); return Next(1.1);
            case 4:
                Player->BeginThrowInput(); return Next(.4);
            case 5:
                Test->TestTrue(TEXT("New character charges a throw"),Player->IsChargingThrow());
                Pic(TEXT("Charge.png")); Player->EndThrowInput(); return Next(.1);
            case 6:
                Pic(TEXT("Throw.png")); return Next(.5);
            case 7:
                Test->TestEqual(TEXT("Animated release throws exactly one held ball"),Player->GetCarriedBallCount(),1);
                StartLocation=Player->GetActorLocation();
                Player->RequestAIDash(Player->GetActorForwardVector()); return Next(.25);
            case 8:
                Test->TestTrue(TEXT("New character can short-dash too"),FVector::Dist(Player->GetActorLocation(),StartLocation)>10);
                for (TActorIterator<AChaosImpactBall> It(World);It;++It) It->Destroy();
                if (++Iteration<CharacterCount) {
                    State->CharacterIndex=FirstCharacter+Iteration; State->ColourChoice=3; State->ForceNetUpdate();
                    Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
                    Stage=1; NextAt=Now+1; return false;
                }
                Cleanup(PC); return true;
            }
            return false;
        }
    private:
        void Cleanup(AChaosImpactPlayerController* PC) {
            bMove=false;
            if (PC) {
                if (auto* State=PC->GetPlayerState<AChaosImpactPlayerState>(); State && OriginalCharacter>=0) {
                    State->CharacterIndex=OriginalCharacter; State->ColourChoice=OriginalColour; State->ForceNetUpdate();
                }
                if (auto* P=Cast<AChaosImpactCharacter>(PC->GetPawn())) { P->RequestCancelThrow(); PC->SetViewTarget(P); }
            }
            if (Camera.IsValid()) Camera->Destroy();
            for (auto CPU:CPUs) if (CPU.IsValid()) CPU->SetActorTickEnabled(true);
        }
        FAutomationTestBase* Test;
        int32 FirstCharacter,CharacterCount;
        FString Directory;
        int32 Stage=0,Iteration=0,OriginalCharacter=-1,OriginalColour=-1;
        double Started=FPlatformTime::Seconds(),NextAt=0;
        bool bMove=false;
        bool bRunShotTaken=false;
        FVector Origin,StartLocation;
        FTransform IdleFoot;
        TWeakObjectPtr<ACameraActor> Camera;
        TArray<TWeakObjectPtr<AChaosImpactCPUController>> CPUs;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFairyWoodMenuTest,"ChaosImpact.Characters.FairyWood.Menu",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFairyWoodMenuTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFairyWoodMenuCommand(this)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFairyWoodGameplayTest,"ChaosImpact.Characters.FairyWood.Gameplay",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFairyWoodGameplayTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFairyWoodGameplayCommand(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAntAssetsTest,"ChaosImpact.Characters.Ant.Assets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAntAssetsTest::RunTest(const FString&)
{
    TestEqual(TEXT("Ant Kids is appended without changing existing saved character indices"),FString(ChaosImpactRoster::Get(4).AssetPrefix),FString(TEXT("Ant")));
    TestFalse(TEXT("Ant Kids keeps its supplied fixed texture"),ChaosImpactRoster::Get(4).bColourVariants);
    TestTrue(TEXT("The second-row Ant tile is selectable"),UChaosImpactCharacterSelect::IsTileUnlocked(4));
    auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/ChaosImpact/Ant/SK_Ant"));
    auto* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/ChaosImpact/Ant/T_Ant"));
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ChaosImpact/Ant/M_CI_Ant"));
    if (!TestNotNull(TEXT("Ant skeletal mesh"),Mesh) || !TestNotNull(TEXT("Ant supplied texture"),Texture) || !TestNotNull(TEXT("Ant material"),Material)) return false;
    const double Height=Mesh->GetBounds().BoxExtent.Z*2;
    TestTrue(TEXT("Ant fits the same playable-character height"),Height>120 && Height<160);
    for (const TCHAR* Joint:{TEXT("Character1_Hips"),TEXT("Character1_Head"),TEXT("Character1_LeftHand"),TEXT("Character1_RightHand"),TEXT("Character1_LeftFoot"),TEXT("Character1_RightFoot")})
        TestTrue(FString::Printf(TEXT("Ant has %s"),Joint),Mesh->GetRefSkeleton().FindBoneIndex(Joint)!=INDEX_NONE);
    for (const auto& Slot:Mesh->GetMaterials()) TestTrue(TEXT("All Ant parts use its texture"),Slot.MaterialInterface==Material);
    FLinearColor Unused; TestFalse(TEXT("Team colour cannot tint Ant"),Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BodyColor")),Unused));
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAntGameplayTest,"ChaosImpact.Characters.Ant.Gameplay",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAntGameplayTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFairyWoodGameplayCommand(this,4,1,TEXT("AntQA"))); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAntMenuTest,"ChaosImpact.Characters.Ant.Menu",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAntMenuTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFairyWoodMenuCommand(this,4,3,TEXT("AntQA"))); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishMenuTest,"ChaosImpact.Characters.Maguro.Menu",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FFishMenuTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FFairyWoodMenuCommand(this,5,4,TEXT("FishQA"))); return true; }
#endif
