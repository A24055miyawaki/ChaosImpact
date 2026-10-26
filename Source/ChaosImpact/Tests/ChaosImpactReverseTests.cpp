#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReverseAssets,"ChaosImpact.Characters.Reverse.Assets",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FReverseAssets::RunTest(const FString&)
{
	TestEqual(TEXT("Cancelled Emoji rename is respected"),FString(ChaosImpactRoster::Get(ChaosImpactRoster::EmojiIndex).Name),FString(TEXT("しんきんぐ")));
	TestEqual(TEXT("New character's name"),FString(ChaosImpactRoster::Get(ChaosImpactRoster::ReverseIndex).Name),FString(TEXT("サカサ・ニコラ")));
	TestTrue(TEXT("New tile is selectable"),UChaosImpactCharacterSelect::IsTileUnlocked(ChaosImpactRoster::ReverseIndex));
	TestFalse(TEXT("Preserve supplied Reverse texture"),ChaosImpactRoster::Get(ChaosImpactRoster::ReverseIndex).bColourVariants);
	auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/ChaosImpact/Reverse/SK_Reverse"));
	auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ChaosImpact/Reverse/M_CI_Reverse"));
	TestNotNull(TEXT("Reverse texture"),LoadObject<UTexture2D>(nullptr,TEXT("/Game/ChaosImpact/Reverse/T_Reverse")));
	if(!TestNotNull(TEXT("Reverse skinned body"),Mesh) || !TestNotNull(TEXT("Reverse fixed material"),Mat)) { return false; }
	TestTrue(TEXT("Floor normalized without changing geometry proportions"),FMath::Abs(Mesh->GetBounds().Origin.Z-Mesh->GetBounds().BoxExtent.Z)<1);
	for(const auto& Slot:Mesh->GetMaterials()) { TestTrue(TEXT("All slots use supplied texture"),Slot.MaterialInterface==Mat); }
	for(const TCHAR* Bone:{TEXT("Character1_Head"),TEXT("Character1_LeftHand"),TEXT("Character1_RightHand"),TEXT("Character1_LeftFoot"),TEXT("Character1_RightFoot")})
	{ TestTrue(FString::Printf(TEXT("Rig has %s"),Bone),Mesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE); }
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
	if(GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
#endif
	return true;
}
namespace
{
class FReverseFootCarry : public IAutomationLatentCommand
{
public:
	explicit FReverseFootCarry(FAutomationTestBase* T):Test(T) {}
	bool Update() override
	{
		if(FPlatformTime::Seconds()-Started>100) { Test->AddError(FString::Printf(TEXT("Reverse foot carry timeout %d"),Step)); return true; }
		UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if(!W) { return false; }
		if(Step==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CICPUCount=0?CITargets=0?CILocalPlayers=1")); ++Step; return false; }
		auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController()); auto* P=PC?Cast<AChaosImpactCharacter>(PC->GetPawn()):nullptr;
		auto* State=PC?PC->GetPlayerState<AChaosImpactPlayerState>():nullptr;
		if(!P || !State || !PC->IsGameplayActive()) { return false; }
		const double Now=W->GetTimeSeconds();
		if(Camera.IsValid())
		{
			const FVector Focus=P->GetActorLocation()+FVector(0,0,10),Eye=Focus+P->GetActorRotation().RotateVector(FVector(450,110,95));
			Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
		}
		if(Now<Next) { return false; }
		const auto Advance=[&](double Delay) { ++Step; Next=Now+Delay; return false; };
		const auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ReverseQA"),Name),true,false); };
		auto* Puppet=P->FindComponentByClass<UChaosImpactPuppetComponent>(); auto* Skin=P->FindComponentByClass<UPoseableMeshComponent>();
		switch(Step)
		{
		case 1:
			Original=State->CharacterIndex; State->CharacterIndex=ChaosImpactRoster::ReverseIndex;
			for(TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
			for(TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
			Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(50); Camera->GetCameraComponent()->bConstrainAspectRatio=false;
			return Advance(.8);
		case 2:
		{
			if(!Test->TestNotNull(TEXT("Reverse puppet"),Puppet) || !Test->TestNotNull(TEXT("Reverse skin"),Skin)) { return true; }
			Test->TestTrue(TEXT("Reverse uses foot carry"),Puppet->HasFootCarry());
			// Reverse's authored feet are weighted to *Hand; its walking hands to *Foot.
			const FVector LowerHand=Skin->GetBoneLocationByName(TEXT("Character1_LeftFoot"),EBoneSpaces::ComponentSpace);
			const FVector UpperFoot=Skin->GetBoneLocationByName(TEXT("Character1_LeftHand"),EBoneSpaces::ComponentSpace);
			Test->TestTrue(TEXT("Hands near floor, feet raised above torso"),LowerHand.Z<25 && UpperFoot.Z>110);
			for(int32 I=0;I<2;++I)
			{
				auto* Ball=W->SpawnActor<AChaosImpactBall>(P->GetActorLocation()+FVector(0,800,0),FRotator::ZeroRotator); Ball->MakePickup();
				Test->TestTrue(TEXT("Both feet can carry a ball"),Ball->ConsumePickup(P)); if(IsValid(Ball)) { Ball->Destroy(); }
			}
			return Advance(.3);
		}
		case 3:
			for(bool Right:{true,false}) { FVector Toe; Test->TestTrue(TEXT("Both feet have ball positions"),Puppet->GetPalmLocation(Right,Toe));
				Test->TestTrue(TEXT("Balls remain above body instead of on walking hands"),Puppet->GetComponentTransform().InverseTransformPosition(Toe).Z>110); }
			Shot(TEXT("Feet-Two-Balls.png")); P->BeginThrowInput(); return Advance(.35);
		case 4:
		{
			P->EndThrowInput();
			// Validate before returning to the frame loop; a slow frame can already execute the release timer.
			AChaosImpactBall* Pending=nullptr; FVector Direction; float Speed,Up,Seconds; bool Arc;
			Test->TestTrue(TEXT("Foot throw uses the common timing"),P->GetPendingThrow(Pending,Direction,Speed,Up,Arc,Seconds));
			if(Pending)
			{
				Test->TestTrue(TEXT("Projectile attached to visible foot model"),Pending->GetRootComponent()->GetAttachParent()==Skin);
				Test->TestTrue(TEXT("Right foot is release socket"),Pending->GetRootComponent()->GetAttachSocketName()==FName(TEXT("Character1_RightHand")));
				Thrown=Pending;
			}
			return Advance(.05);
		}
		case 5:
			Shot(TEXT("Foot-Throw-Cue.png")); return Advance(.5);
		case 6:
			Test->TestEqual(TEXT("One ball thrown, one remains"),P->GetCarriedBallCount(),1);
			Test->TestTrue(TEXT("Foot throw becomes an ordinary projectile"),Thrown.IsValid() && Thrown->GetThrowingPawn()==P && !Thrown->GetAttachParentActor());
			State->CharacterIndex=Original; PC->SetViewTarget(P); if(Camera.IsValid()) { Camera->Destroy(); } return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test; int32 Step=0,Original=0; double Started=FPlatformTime::Seconds(),Next=0;
	TWeakObjectPtr<ACameraActor> Camera; TWeakObjectPtr<AChaosImpactBall> Thrown;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReverseFootCarryTest,"ChaosImpact.Characters.Reverse.FootCarry",
 EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FReverseFootCarryTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FReverseFootCarry(this)); return true; }
#endif
