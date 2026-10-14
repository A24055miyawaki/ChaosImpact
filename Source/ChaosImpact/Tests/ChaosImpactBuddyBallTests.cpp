#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactBallSpawner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactSettings.h"
#include "InputKeyEventArgs.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	void BuddyTestKey(AChaosImpactPlayerController* PC,const FKey& Key,const EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Key,Event,
			Event==IE_Released ? 0.0f : 1.0f,false,FPlatformTime::Cycles64()));
	}
	class FBuddyBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FBuddyBallCommand(FAutomationTestBase* InTest):Test(InTest) {}
		bool Update() override
		{
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& C : GEngine->GetWorldContexts())
			{ if (C.World() && C.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(C.World()->GetFirstPlayerController()); if(PC) break; } }
			if (FPlatformTime::Seconds()-Started > 100) { Test->AddError(TEXT("Buddy ball timed out")); Cleanup(PC); return true; }
			if (!PC || !PC->IsTrainingMode()) { return false; }
			UWorld* W = PC->GetWorld();
			const double Now = W->GetTimeSeconds();
			if (Now < NextAt) { return false; }
			const auto Next = [&](double Delay) { ++Stage; NextAt=Now+Delay; return false; };
			const auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BuddyBallQA"),Name),true,false); };
			auto* P = Cast<AChaosImpactCharacter>(PC->GetPawn());
			if (!P) { return false; }
			const auto Give = [&](AChaosImpactCharacter* To) { auto* B=W->SpawnActor<AChaosImpactBall>(To->GetActorLocation()+FVector(0,0,500),FRotator::ZeroRotator); B->MakePickup(); const bool Got=To->TryPickupBall(B); B->Destroy(); return Got; };
			switch(Stage)
			{
			case 0:
				if (Now < 3) { return false; }
				for (TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) It->Destroy();
				for (TActorIterator<AChaosImpactBall> It(W);It;++It) It->Destroy();
				Origin=P->GetActorLocation();
				PC->ToggleTrainingOverlay();
				PC->CycleTrainingPlayerCount();
				Mate=Cast<AChaosImpactCharacter>(PC->GetGameInstance()->GetLocalPlayers()[1]->GetPlayerController(W)->GetPawn());
				if (!Test->TestNotNull(TEXT("Second local trainee"),Mate.Get())) { Cleanup(PC); return true; }
				OriginalMateCharacter=Mate->GetPlayerState<AChaosImpactPlayerState>()->CharacterIndex;
				PC->ToggleTrainingTeams();
				Test->TestEqual(TEXT("2v0 has no CPU minimum"),PC->GetTrainingCPUCount(),0);
				Test->TestTrue(TEXT("Both humans can be on the same side with no enemy"),AChaosImpactGameState::AreTeammates(W,P,Mate.Get()));
				Test->TestEqual(TEXT("Friendly damage is rejected in training"),Mate->TakeDamage(1,FDamageEvent(),PC,P),0.0f);
				for(int I=0;I<7;++I) PC->GetMenuWidget()->Navigate(EKeys::Down);
				PC->GetMenuWidget()->ConfirmSelection();
				Test->TestEqual(TEXT("Team submenu has toggle, eight slots and back"),PC->GetMenuWidget()->GetEntryCount(),10);
				return Next(.5);
			case 1:
				Shot(TEXT("01-Teams-2v0.png")); return Next(.3);
			case 2:
				PC->GetMenuWidget()->GoBack();
				PC->ToggleTrainingCPU();
				PC->ApplyTrainingTeams();
				for(TActorIterator<AChaosImpactCPUController> It(W);It;++It) { It->SetActorTickEnabled(false); Enemy=Cast<AChaosImpactCharacter>(It->GetPawn()); }
				PC->CloseTrainingOverlay();
				P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator);
				if(Enemy.IsValid()) Enemy->ResetForOnlineMatch(Origin+FVector(900,0,0),FRotator::ZeroRotator);
				return Next(1);
			case 3:
				Test->TestTrue(TEXT("First ordinary ball"),Give(P)); Test->TestTrue(TEXT("Second ordinary ball"),Give(P));
				Test->TestTrue(TEXT("Passenger has ordinary inventory"),Give(Mate.Get()));
				Test->TestTrue(TEXT("Passenger curls"),Mate->GetBallForm()->Toggle());
				Test->TestEqual(TEXT("Curl drops all carried balls"),Mate->GetCarriedBallCount(),0);
				Buddy=Mate->GetBallForm()->GetFormBall();
				if(!Test->TestNotNull(TEXT("Passenger proxy"),Buddy.Get())) { Cleanup(PC); return true; }
				Test->TestFalse(TEXT("A full inventory cannot grab a passenger"),Buddy->ConsumePickup(P));
				Test->TestFalse(TEXT("An enemy cannot grab a passenger"),Buddy->ConsumePickup(Enemy.Get()));
				P->RequestDropBall();
				Test->TestTrue(TEXT("A passenger fits in the second free slot"),Buddy->ConsumePickup(P));
				Test->TestEqual(TEXT("Two total slots, not two plus passenger"),P->GetCarriedBallCount(),2);
				P->RequestBallSwap();
				Test->TestTrue(TEXT("Swap moves the actual passenger to the throwing hand"),P->GetBallForm()->GetHeldBall(0)==Buddy.Get());
				Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(55); Camera->GetCameraComponent()->bConstrainAspectRatio=false;
				Camera->SetActorLocationAndRotation(Origin+FVector(480,180,80),(Origin-(Origin+FVector(480,180,80))).Rotation()); PC->SetViewTarget(Camera.Get());
				return Next(.6);
			case 4:
				Test->TestTrue(TEXT("Held passenger is at the hand"),FVector::Dist(Buddy->GetActorLocation(),P->GetBuddyHoldLocation(0))<10);
				Shot(TEXT("02-Held-Player.png")); P->SetAIAimDirection(FVector::ForwardVector); P->BeginThrowInput(); return Next(.4);
			case 5:
				P->EndThrowInput(); Test->TestTrue(TEXT("Uses the normal animation release cue"),P->IsThrowReleasePending()); return Next(.5);
			case 6:
				Test->TestTrue(TEXT("Actual passenger was thrown"),Buddy.IsValid() && Buddy->IsLaunched());
				Test->TestEqual(TEXT("Only the passenger slot was consumed"),P->GetCarriedBallCount(),1);
				if(Buddy.IsValid() && Enemy.IsValid()) {
					Buddy->SetActorLocation(Enemy->GetActorLocation()-FVector(200,0,0));
					Buddy->Launch(FVector::ForwardVector,1400,EChaosImpactBallFlightMode::Straight,0);
				}
				return Next(.6);
			case 7:
				Test->TestFalse(TEXT("Hit returns passenger to normal"),Mate->IsInBallForm());
				if(Enemy.IsValid()) Test->TestEqual(TEXT("Player ball does ordinary one-point damage"),Enemy->GetHealth(),Enemy->GetMaxHealth()-1);
				PC->SetViewTarget(P);
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator);
				return Next(.8);
			case 8:
				Test->TestTrue(TEXT("Can curl again"),Mate->GetBallForm()->Toggle());
				Buddy=Mate->GetBallForm()->GetFormBall();
				if(Buddy.IsValid()) { Buddy->SetActorLocation(Origin+FVector(0,0,100)); Buddy->Launch(FVector::ForwardVector,300,EChaosImpactBallFlightMode::Arc,0); }
				return Next(1.2);
			case 9:
				Test->TestFalse(TEXT("Floor contact automatically uncurls"),Mate->IsInBallForm());
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator); P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator); return Next(.8);
			case 10:
				Mate->GetBallForm()->Toggle(); Buddy=Mate->GetBallForm()->GetFormBall();
				if(Buddy.IsValid()) Test->TestTrue(TEXT("Pickup again"),Buddy->ConsumePickup(P));
				P->BeginThrowInput(); P->EndThrowInput();
				Mate->GetBallForm()->Toggle();
				Test->TestFalse(TEXT("Passenger can cancel during release animation"),Mate->IsInBallForm());
				Test->TestFalse(TEXT("Cancel removes the pending throw"),P->IsThrowReleasePending());
				Test->TestEqual(TEXT("No phantom passenger inventory"),P->GetCarriedBallCount(),0);
				return Next(.8);
			case 11:
				Mate->GetBallForm()->Toggle(); Buddy=Mate->GetBallForm()->GetFormBall();
				if(Buddy.IsValid()) Buddy->ConsumePickup(P);
				P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
				Test->TestFalse(TEXT("Carrier reset frees its passenger"),Mate->IsInBallForm());
				Test->TestTrue(TEXT("Passenger movement restored"),Mate->GetCharacterMovement()->MovementMode!=MOVE_None);
				PC->ToggleTrainingOverlay();
				PC->CycleTrainingTeam(false,1); Test->TestFalse(TEXT("Changing a team is live"),AChaosImpactGameState::AreTeammates(W,P,Mate.Get()));
				PC->CycleTrainingTeam(false,1,-1);
				PC->CloseTrainingOverlay();
				return Next(.7);
			case 12:
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator);
				Mate->GetPlayerState<AChaosImpactPlayerState>()->CharacterIndex=PoseIndex;
				P->SetActorLocation(Origin-FVector(500,0,0));
				W->GetGameViewport()->SetForceDisableSplitscreen(true);
				return Next(.8);
			case 13:
				Test->TestTrue(TEXT("Every roster character can curl"),Mate->GetBallForm()->Toggle());
				if(auto* Puppet=Mate->FindComponentByClass<UChaosImpactPuppetComponent>())
				{
					Puppet->UpdatePose(true);
					for(auto* Skin : TInlineComponentArray<UPoseableMeshComponent*>(Mate.Get()))
					{
						if(Skin->GetBoneIndex(TEXT("Character1_Head"))==INDEX_NONE) continue;
						const FVector Before=Skin->GetBoneLocationByName(TEXT("Character1_Head"),EBoneSpaces::ComponentSpace);
						Puppet->UpdatePose(true);
						Test->TestTrue(TEXT("Repeated curled poses do not accumulate bone offsets"),FVector::Dist(Before,Skin->GetBoneLocationByName(TEXT("Character1_Head"),EBoneSpaces::ComponentSpace))<.1);
					}
				}
				Buddy=Mate->GetBallForm()->GetFormBall();
				if(Buddy.IsValid()) {
					const FVector Focus=Buddy->GetActorLocation(); const FVector Eye=Focus+FVector(340,130,85);
					Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
				}
				return Next(.4);
			case 14:
				Shot(*FString::Printf(TEXT("Pose-%s.png"),ChaosImpactRoster::Get(PoseIndex).AssetPrefix)); return Next(.2);
			case 15:
				Mate->GetBallForm()->Restore();
				Test->TestTrue(TEXT("Collision is restored after uncurl"),Mate->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
				if(++PoseIndex<ChaosImpactRoster::Num()) { Stage=12; NextAt=Now+.6; return false; }
				Mate->GetPlayerState<AChaosImpactPlayerState>()->CharacterIndex=OriginalMateCharacter;
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator);
				return Next(.8);
			case 16:
				Mate->GetBallForm()->Toggle(); Buddy=Mate->GetBallForm()->GetFormBall();
				Wall=W->SpawnActor<AStaticMeshActor>(Origin+FVector(350,0,80),FRotator::ZeroRotator);
				Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
				Wall->SetActorScale3D(FVector(.2,6,4)); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				if(Buddy.IsValid()) { Buddy->SetActorLocation(Origin+FVector(0,0,70)); Buddy->Launch(FVector::ForwardVector,1200,EChaosImpactBallFlightMode::Straight,0); }
				return Next(.38);
			case 17:
				Test->TestTrue(TEXT("Player ball rebounds from walls"),Buddy.IsValid() && Buddy->GetReflectionCount()>0);
				if(Buddy.IsValid()) { BeforeSteer=Buddy->GetBallVelocity(); Buddy->SetSteering(FVector::RightVector); }
				return Next(.08);
			case 18:
				if(Buddy.IsValid()) {
					Test->TestTrue(TEXT("Passenger input bends flight"),FMath::Abs(Buddy->GetBallVelocity().Y)>FMath::Abs(BeforeSteer.Y)+1);
					Test->TestTrue(TEXT("Steering does not accelerate the ball"),Buddy->GetBallVelocity().Size2D()<=BeforeSteer.Size2D()+2);
				}
				Mate->GetBallForm()->Restore();
				if(Wall.IsValid()) Wall->Destroy();
				Mate->ResetForOnlineMatch(Origin+FVector(0,250,0),FRotator::ZeroRotator);
				return Next(.8);
			case 19:
				Mate->GetBallForm()->Toggle(); Buddy=Mate->GetBallForm()->GetFormBall();
				Test->TestTrue(TEXT("Can test damage on a curled player"),Buddy.IsValid());
				if(Buddy.IsValid() && Enemy.IsValid()) {
					const float Before=Mate->GetHealth();
					Buddy->TakeDamage(1,FDamageEvent(),Enemy->GetController(),Enemy.Get());
					Test->TestEqual(TEXT("Curling does not grant invulnerability"),Mate->GetHealth(),Before-1);
					Test->TestFalse(TEXT("Damage uncurls the player"),Mate->IsInBallForm());
				}
				return Next(.8);
			case 20:
				P->ResetForOnlineMatch(Origin,FRotator::ZeroRotator); PC->SetViewTarget(P); return Next(.8);
			case 21:
			{
				using namespace ChaosImpactSettings;
				const auto Scheme=GetPlayerControlScheme(PC);
				const auto Action=Scheme==EChaosImpactControlScheme::Simple ? EChaosImpactAction::SwapBall
					: IsMouseOnlyScheme(Scheme) ? EChaosImpactAction::Dash : EChaosImpactAction::DropBall;
				ManageKey=GetActiveKey(GetPlayerProfile(PC),GetPlayerDevice(PC),Action,0);
				Test->TestTrue(TEXT("Active input layout has a transform key"),ManageKey.IsValid());
				BuddyTestKey(PC,ManageKey,IE_Pressed);
				return Next(1.6);
			}
			case 22:
				Test->TestTrue(TEXT("Holding the actual bound key curls the player"),P->IsInBallForm());
				BuddyTestKey(PC,ManageKey,IE_Released); return Next(.2);
			case 23:
				BuddyTestKey(PC,ManageKey,IE_Pressed); return Next(.15);
			case 24:
				Test->TestFalse(TEXT("The same key uncurls without waiting for another hold"),P->IsInBallForm());
				BuddyTestKey(PC,ManageKey,IE_Released); Cleanup(PC); return true;
			}
			return false;
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if(Mate.IsValid()) Mate->GetBallForm()->Restore();
			if(Mate.IsValid() && OriginalMateCharacter>=0) Mate->GetPlayerState<AChaosImpactPlayerState>()->CharacterIndex=OriginalMateCharacter;
			if(Camera.IsValid()) Camera->Destroy();
			if(Wall.IsValid()) Wall->Destroy();
			if(PC) { if(ManageKey.IsValid()) BuddyTestKey(PC,ManageKey,IE_Released); if(auto* P=Cast<AChaosImpactCharacter>(PC->GetPawn())) P->GetBallForm()->Restore(); PC->SetViewTarget(PC->GetPawn()); PC->GetWorld()->GetGameViewport()->SetForceDisableSplitscreen(false); if(PC->IsTrainingOverlayOpen()) PC->CloseTrainingOverlay(); if(PC->AreTrainingTeamsEnabled()) PC->ToggleTrainingTeams(); }
		}
		FAutomationTestBase* Test;
		int32 Stage=0;
		int32 PoseIndex=0,OriginalMateCharacter=-1;
		double Started=FPlatformTime::Seconds(),NextAt=0;
		FVector Origin;
		FVector BeforeSteer;
		FKey ManageKey;
		TWeakObjectPtr<AChaosImpactCharacter> Mate,Enemy;
		TWeakObjectPtr<AChaosImpactBuddyBall> Buddy;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<AStaticMeshActor> Wall;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBuddyBallTest,"ChaosImpact.Training.BuddyBall",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactBuddyBallTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FBuddyBallCommand(this)); return true; }
#endif
