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
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmojiAssets,"ChaosImpact.Characters.Emoji.Assets",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FEmojiAssets::RunTest(const FString&)
{
	TestTrue(TEXT("Emoji tile is selectable"),UChaosImpactCharacterSelect::IsTileUnlocked(ChaosImpactRoster::EmojiIndex));
	TestFalse(TEXT("Emoji retains original fixed colours"),ChaosImpactRoster::Get(ChaosImpactRoster::EmojiIndex).bColourVariants);
	TestTrue(TEXT("Emoji is unanimated floating geometry"),ChaosImpactRoster::Get(ChaosImpactRoster::EmojiIndex).Kind==EChaosImpactModelKind::Floating);
	auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/ChaosImpact/Emoji/SM_Emoji_Body"));
	auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ChaosImpact/Emoji/M_CI_Emoji"));
	TestNotNull(TEXT("Emoji supplied texture"),LoadObject<UTexture2D>(nullptr,TEXT("/Game/ChaosImpact/Emoji/T_Emoji")));
	if (!TestNotNull(TEXT("Combined head and fingers"),Mesh) || !TestNotNull(TEXT("Emoji material"),Mat)) { return false; }
	TestTrue(TEXT("Fingertips have separate editable ball sockets"),Mesh->FindSocket(TEXT("BallRight")) && Mesh->FindSocket(TEXT("BallLeft")));
	for (int32 I=0;I<Mesh->GetStaticMaterials().Num();++I) { TestTrue(TEXT("All geometry uses provided texture"),Mesh->GetMaterial(I)==Mat); }
	TestTrue(TEXT("Floating body has playable size"),Mesh->GetBounds().BoxExtent.Z*2>90 && Mesh->GetBounds().BoxExtent.Z*2<125);
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
#endif
	return true;
}

namespace
{
class FEmojiGameplay : public IAutomationLatentCommand
{
public:
	explicit FEmojiGameplay(FAutomationTestBase* T):Test(T) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Emoji timeout at step %d"),Step)); return true; }
		UWorld* W=nullptr; for (const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if (!W) { return false; }
		if (Step==0) { UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CICPUCount=0?CITargets=0?CILocalPlayers=1")); ++Step; return false; }
		auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController());
		auto* P=PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
		if(!P || !PC->IsGameplayActive()) { return false; }
		auto* State=P->GetPlayerState<AChaosImpactPlayerState>(); if(!State) { return false; }
		if (bMove) { P->AddMovementInput(FVector::ForwardVector); }
		if(Camera.IsValid())
		{
			const FVector Focus=P->GetActorLocation()+FVector(0,0,12), Eye=Focus+P->GetActorRotation().RotateVector(FVector(470,100,90));
			Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
		}
		const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
		const auto Advance=[&](double Delay) { ++Step; Next=Now+Delay; return false; };
		const auto Shot=[&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("EmojiQA"),Name),true,false); };
		auto* Puppet=P->FindComponentByClass<UChaosImpactPuppetComponent>();
		const auto Give=[&](EChaosImpactBallType Type)
		{
			auto* Ball=W->SpawnActor<AChaosImpactBall>(P->GetActorLocation()+FVector(0,800,0),FRotator::ZeroRotator);
			Ball->SetBallType(Type); Ball->MakePickup(); const bool Picked=Ball->ConsumePickup(P); if(IsValid(Ball)) { Ball->Destroy(); } return Picked;
		};
		const auto Stable=[&]()
		{
			Test->TestTrue(TEXT("Body ignores underlying walk/throw/tackle animation"),Body.IsValid() && Body->GetRelativeTransform().Equals(BodyReference,.01));
		};
		const auto Held=[&]()
		{
			Puppet->UpdatePose(true);
			for(bool Right:{true,false})
			{
				FVector Tip; Test->TestTrue(TEXT("Both fingertip positions available"),Puppet->GetPalmLocation(Right,Tip));
				TArray<UStaticMeshComponent*> Parts; P->GetComponents(Parts);
				for(auto* Part:Parts) { if(Part->GetFName()==FName(Right ? TEXT("HeldBallMesh") : TEXT("LeftHeldBallMesh")))
				{ Test->TestTrue(TEXT("Held ball is visible on its fingertip"),Part->IsVisible()); Test->TestTrue(TEXT("Ball remains pinned to finger"),FVector::Dist(Part->GetComponentLocation(),Tip)<.2); } }
			}
		};
		switch(Step)
		{
		case 1:
			for(TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
			for(TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
			Original=State->CharacterIndex; State->CharacterIndex=ChaosImpactRoster::EmojiIndex;
			Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(48); Camera->GetCameraComponent()->bConstrainAspectRatio=false;
			return Advance(.8);
		case 2:
			if(!Test->TestNotNull(TEXT("Floating puppet initialized"),Puppet) || !Test->TestTrue(TEXT("Uses fixed fingertip carry"),Puppet->HasFixedCarrySockets())) { return true; }
			Body=Puppet->GetFixedCarryComponent(); BodyReference=Body->GetRelativeTransform();
			Test->TestTrue(TEXT("Visible model hovers above its local floor"),BodyReference.GetLocation().Z>15);
			Test->TestTrue(TEXT("First fingertip ball picked up"),Give(EChaosImpactBallType::Normal));
			Test->TestTrue(TEXT("Second fingertip ball picked up"),Give(EChaosImpactBallType::Fire));
			Test->TestFalse(TEXT("Cannot exceed the two-ball inventory"),Give(EChaosImpactBallType::Normal));
			Held(); return Advance(.25);
		case 3:
			Shot(TEXT("Two-Fingertip-Balls.png")); Before=P->GetActorLocation(); bMove=true; return Advance(.45);
		case 4:
			bMove=false; Stable(); Held(); Test->TestTrue(TEXT("Floating character still moves normally"),FVector::Dist2D(Before,P->GetActorLocation())>40);
			Shot(TEXT("Moving.png")); P->RequestBallSwap(); return Advance(.2);
		case 5:
			Test->TestTrue(TEXT("Can swap equipped fingers"),P->GetCarriedBallType(0)==EChaosImpactBallType::Fire); Held(); P->BeginThrowInput(); return Advance(.35);
		case 6:
		{
			Stable(); Held(); Shot(TEXT("Charging.png")); P->EndThrowInput();
			// Inspect immediately: shader/effect hitches can exceed the whole 0.18s cue before the next frame.
			AChaosImpactBall* Pending=nullptr; FVector Dir; float Speed,Up,Left; bool Arc;
			Test->TestTrue(TEXT("Throw uses the existing release cue"),P->GetPendingThrow(Pending,Dir,Speed,Up,Arc,Left));
			if(Pending)
			{
				Test->TestTrue(TEXT("Ball stays attached to the visible body before release"),Pending->GetRootComponent()->GetAttachParent()==Body.Get());
				Test->TestTrue(TEXT("Ball is attached to fingertip, not invisible animated hand"),Pending->GetRootComponent()->GetAttachSocketName()==FName(TEXT("BallRight")));
				Thrown=Pending;
			}
			return Advance(.05);
		}
		case 7:
			Stable(); Shot(TEXT("Throw-Cue.png")); return Advance(.45);
		case 8:
			Stable(); Test->TestEqual(TEXT("One ball released, one remains"),P->GetCarriedBallCount(),1);
			Test->TestTrue(TEXT("Released projectile uses the normal thrower and physics"),Thrown.IsValid() && Thrown->GetThrowingPawn()==P && !Thrown->GetAttachParentActor());
			P->RequestAIDash(P->GetActorForwardVector()); return Advance(.3);
		case 9:
			Stable(); P->Jump(); return Advance(.15);
		case 10:
			Stable(); Test->TestTrue(TEXT("Visual hovering does not grant actual flight"),P->GetCharacterMovement()->IsFalling()); P->StopJumping(); return Advance(.9);
		case 11:
			State->CharacterIndex=Original; PC->SetViewTarget(P); if(Camera.IsValid()) { Camera->Destroy(); } return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test; int32 Step=0,Original=0; double Started=FPlatformTime::Seconds(),Next=0;
	bool bMove=false; FVector Before; FTransform BodyReference;
	TWeakObjectPtr<ACameraActor> Camera; TWeakObjectPtr<USceneComponent> Body; TWeakObjectPtr<AChaosImpactBall> Thrown;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEmojiGameplayTest,"ChaosImpact.Characters.Emoji.Gameplay",
 EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FEmojiGameplayTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FEmojiGameplay(this)); return true; }
#endif
