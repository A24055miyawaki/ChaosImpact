#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"

namespace
{
class FPrisonCommand : public IAutomationLatentCommand
{
public:
	explicit FPrisonCommand(FAutomationTestBase* T): Test(T) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds()-Started>160) { Test->AddError(FString::Printf(TEXT("Prison timeout at %d"),Step)); return true; }
		UWorld* W=nullptr;
		for (const auto& C:GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if (!W) { return false; }
		if (Step==0)
		{
			UGameplayStatics::OpenLevel(W,TEXT("/Game/ThirdPerson/Lvl_ThirdPerson"),true,TEXT("CITraining=1?CITargets=0?CICPUCount=0?CILocalPlayers=4"));
			++Step; return false;
		}
		auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController());
		auto* A=Cast<AChaosImpactCharacter>(UGameplayStatics::GetPlayerPawn(W,0));
		auto* B=Cast<AChaosImpactCharacter>(UGameplayStatics::GetPlayerPawn(W,1));
		auto* C=Cast<AChaosImpactCharacter>(UGameplayStatics::GetPlayerPawn(W,2));
		auto* D=Cast<AChaosImpactCharacter>(UGameplayStatics::GetPlayerPawn(W,3));
		if (!PC || !A || !B || !C || !D || !PC->IsGameplayActive()) { return false; }
		const double Now=W->GetTimeSeconds();
		if (Now<Next) { return false; }
		const auto Advance=[&](double Delay) { UE_LOG(LogTemp,Display,TEXT("PRISONQA step=%d time=%.2f remaining=%.2f hp=%.0f/%.0f stock=%d"),Step,Now,Shell.IsValid()?Shell->GetCaptureSecondsLeft():-1,B->GetHealth(),C->GetHealth(),A->GetCarriedBallCount()); ++Step; Next=Now+Delay; return false; };
		const auto Shot=[&](const TCHAR* File) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("PrisonBallQA"),File),true,false); };
		const auto Throw=[&](AChaosImpactCharacter* Who,EChaosImpactBallType Type,const FVector& At,const FVector& Direction)
		{
			FActorSpawnParameters Params; Params.Owner=Who; Params.Instigator=Who; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Ball=W->SpawnActor<AChaosImpactBall>(At,Direction.Rotation(),Params);
			Ball->SetBallType(Type); Ball->Launch(Direction,1400,EChaosImpactBallFlightMode::Straight,0); return Ball;
		};
		const auto LaunchShell=[&](const FVector& At, EChaosImpactBallFlightMode Mode,float Speed)
		{
			if (!Shell.IsValid()) { Test->AddError(TEXT("Missing captive shell")); return; }
			if (auto* Holder=Shell->GetCarrier()) { Holder->GetBallForm()->Remove(Shell.Get()); }
			Shell->SetOwner(A); Shell->SetInstigator(A); Shell->SetActorLocation(At);
			Shell->Launch(FVector::ForwardVector,Speed,Mode,0);
		};
		switch (Step)
		{
		case 1:
			if (Now<3) { return false; }
			// A flat temporary platform isolates projectile semantics from the editable training geometry.
			Floor=W->SpawnActor<AStaticMeshActor>(A->GetActorLocation()+FVector(0,0,1000),FRotator::ZeroRotator);
			Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->SetActorScale3D(FVector(80,80,.2)); Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Origin=Floor->GetActorLocation()+FVector(0,0,A->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+12);
			for (TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
			for (TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
			A->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
			B->ResetForOnlineMatch(Origin+FVector(500,0,0),FRotator::ZeroRotator);
			C->ResetForOnlineMatch(Origin+FVector(900,0,0),FRotator::ZeroRotator);
			D->ResetForOnlineMatch(Origin+FVector(0,650,0),FRotator::ZeroRotator);
			A->SoloTeam=0; B->SoloTeam=1; C->SoloTeam=2; D->SoloTeam=1;
			Test->TestTrue(TEXT("Training permits prison"),ChaosImpactBallTypes::IsPrisonMode(W));
			Test->TestEqual(TEXT("Prison round trips through both inventory nibbles"),ChaosImpactBallTypes::GetPackedSlot(ChaosImpactBallTypes::Pack({EChaosImpactBallType::Prison,EChaosImpactBallType::Prison}),1),EChaosImpactBallType::Prison);
			W->URL.AddOption(TEXT("CIRPGPrototype=1"));
			Test->TestFalse(TEXT("RPG cannot capture"),B->GetBallForm()->Capture(A));
			W->URL.RemoveOption(TEXT("CIRPGPrototype"));
			W->URL.AddOption(TEXT("CISolo=1"));
			Test->TestFalse(TEXT("Solo cannot capture"),B->GetBallForm()->Capture(A));
			W->URL.RemoveOption(TEXT("CISolo"));
			return Advance(.4);
		case 2:
		{
			auto* Gift=W->SpawnActor<AChaosImpactBall>(Origin+FVector(0,-700,100),FRotator::ZeroRotator);
			Gift->MakePickup(); Gift->ConsumePickup(B); B->BeginThrowInput();
			Test->TestTrue(TEXT("Victim was charging"),B->IsChargingThrow());
			Throw(A,EChaosImpactBallType::Prison,B->GetActorLocation()-FVector(180,0,0),FVector::ForwardVector);
			return Advance(.45);
		}
		case 3:
			if (!Test->TestTrue(TEXT("Actual prison projectile captures"),B->GetBallForm()->IsCaptured())) { return true; }
			Shell=B->GetBallForm()->GetFormBall();
			Test->TestEqual(TEXT("Capture deals no initial damage"),B->GetHealth(),B->GetMaxHealth());
			Test->TestFalse(TEXT("Capture interrupts charge"),B->IsChargingThrow());
			Test->TestEqual(TEXT("Victim drops its stock"),B->GetCarriedBallCount(),0);
			Test->TestFalse(TEXT("Captive cannot voluntarily escape"),B->GetBallForm()->Toggle());
			Test->TestFalse(TEXT("Captive cannot tackle"),B->GetTackle()->CanStart());
			Test->TestTrue(TEXT("Captive body cannot walk"),B->GetCharacterMovement()->MovementMode==MOVE_None);
			Test->TestFalse(TEXT("An ally cannot pick up its captured teammate"),Shell->CanBeCollectedBy(D));
			Shot(TEXT("01-FourPlayer-Captured.png"));
			return Advance(.25);
		case 4:
		{
			auto* Gift=W->SpawnActor<AChaosImpactBall>(Origin+FVector(0,-700,100),FRotator::ZeroRotator);
			Gift->MakePickup(); Gift->ConsumePickup(A);
			Test->TestTrue(TEXT("Captured opponent uses second free stock slot"),Shell->ConsumePickup(A));
			Test->TestEqual(TEXT("Only two total balls"),A->GetCarriedBallCount(),2);
			A->RequestBallSwap();
			Test->TestTrue(TEXT("Swap preserves actual captive"),A->GetBallForm()->GetHeldBall(0)==Shell.Get());
			Remaining=Shell->GetCaptureSecondsLeft();
			Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(52);
			Camera->GetCameraComponent()->bConstrainAspectRatio=false;
			W->GetGameViewport()->SetForceDisableSplitscreen(true);
			const FVector Focus=A->GetBuddyHoldLocation(0);
			Camera->SetActorLocationAndRotation(Focus+FVector(300,160,140),(-FVector(300,160,140)).Rotation()); PC->SetViewTarget(Camera.Get());
			return Advance(.3);
		}
		case 5:
			Test->TestTrue(TEXT("Pickup never resets deadline"),Shell->GetCaptureSecondsLeft()<Remaining-.2);
			Test->TestTrue(TEXT("Captured actor follows throwing hand"),FVector::Dist(Shell->GetActorLocation(),A->GetBuddyHoldLocation(0))<12);
			Test->TestTrue(TEXT("Cage has real world geometry"),Shell->FindComponentByClass<UProceduralMeshComponent>()!=nullptr);
			Shot(TEXT("02-Held-Captive.png"));
			A->SetAIAimDirection(FVector::ForwardVector); A->BeginThrowInput();
			return Advance(.2);
		case 6:
			A->EndThrowInput(); Test->TestTrue(TEXT("Throw uses animation cue"),A->IsThrowReleasePending()); return Advance(.45);
		case 7:
			Test->TestTrue(TEXT("Same captive actor launched by animation"),Shell.IsValid() && Shell->IsLaunched());
			Test->TestEqual(TEXT("One throw consumes one stock slot"),A->GetCarriedBallCount(),1);
			LaunchShell(C->GetActorLocation()-FVector(180,0,0),EChaosImpactBallFlightMode::Straight,1400);
			return Advance(.45);
		case 8:
			Test->TestFalse(TEXT("Impact breaks prison"),B->IsInBallForm());
			Test->TestEqual(TEXT("Captive loses exactly one HP"),B->GetHealth(),B->GetMaxHealth()-1);
			Test->TestEqual(TEXT("Struck enemy loses exactly one HP"),C->GetHealth(),C->GetMaxHealth()-1);
			Test->TestFalse(TEXT("Immediate recapture is rejected"),B->GetBallForm()->Capture(A));
			Test->TestEqual(TEXT("Release grants short hit protection"),B->TakeDamage(1,FDamageEvent(),A->GetController(),A),0.f);
			PC->SetViewTarget(A); W->GetGameViewport()->SetForceDisableSplitscreen(false);
			return Advance(3.3);
		case 9:
			B->SetActorLocation(Origin+FVector(0,600,0));
			Test->TestTrue(TEXT("Recapture possible after immunity"),B->GetBallForm()->Capture(A)); Shell=B->GetBallForm()->GetFormBall(); return Advance(.8);
		case 10:
			Test->TestTrue(TEXT("Can carry again"),Shell.IsValid() && Shell->ConsumePickup(A));
			A->LoseCarriedBalls();
			Test->TestTrue(TEXT("Tackle/drop keeps victim captured"),B->GetBallForm()->IsCaptured());
			Test->TestTrue(TEXT("Drop removes carrier and detaches"),Shell.IsValid() && !Shell->GetCarrier() && !Shell->GetAttachParentActor());
			LaunchShell(Origin+FVector(0,300,120),EChaosImpactBallFlightMode::Arc,250);
			return Advance(1.3);
		case 11:
			Test->TestTrue(TEXT("Floor landing rolls, without breaking prison"),Shell.IsValid() && Shell->IsPickup() && B->GetBallForm()->IsCaptured());
			Test->TestEqual(TEXT("Floor deals no captive damage"),B->GetHealth(),B->GetMaxHealth()-1);
			if (Shell.IsValid()) { Test->TestTrue(TEXT("Landed captive can be picked up again"),Shell->ConsumePickup(A)); Remaining=Shell->GetCaptureSecondsLeft(); }
			return Advance(Remaining+.15);
		case 12:
			Test->TestFalse(TEXT("Five seconds frees even a held captive"),B->IsInBallForm());
			Test->TestEqual(TEXT("Timeout removes held stock"),A->GetCarriedBallCount(),0);
			Test->TestEqual(TEXT("Timeout is harmless"),B->GetHealth(),B->GetMaxHealth()-1);
			Test->TestTrue(TEXT("Collision restored"),B->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
			return Advance(3.3);
		case 13:
			Test->TestFalse(TEXT("Friendly capture is rejected"),B->GetBallForm()->Capture(D));
			Test->TestTrue(TEXT("Capture before rescue"),B->GetBallForm()->Capture(A)); Shell=B->GetBallForm()->GetFormBall(); return Advance(.8);
		case 14:
			if (Shell.IsValid())
			{
				if (!Shell->GetCarrier()) { Shell->ConsumePickup(A); }
				Test->TestTrue(TEXT("Test rescue of a held captive"),Shell->GetCarrier()==A);
				const FVector Out=(Shell->GetActorLocation()-A->GetActorLocation()).GetSafeNormal2D();
				Throw(D,EChaosImpactBallType::Normal,Shell->GetActorLocation()+Out*180,-Out);
			}
			return Advance(.4);
		case 15:
			Test->TestFalse(TEXT("Allied projectile rescues the captive"),B->IsInBallForm());
			Test->TestEqual(TEXT("Rescue is harmless"),B->GetHealth(),B->GetMaxHealth()-1);
			return Advance(3.3);
		case 16:
			B->ResetForOnlineMatch(Origin+FVector(0,550,0),FRotator::ZeroRotator);
			Test->TestTrue(TEXT("Capture before wall test"),B->GetBallForm()->Capture(A)); Shell=B->GetBallForm()->GetFormBall();
			Wall=W->SpawnActor<AStaticMeshActor>(Origin+FVector(250,300,100),FRotator::ZeroRotator);
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->SetActorScale3D(FVector(.2,4,4)); Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			LaunchShell(Origin+FVector(0,300,100),EChaosImpactBallFlightMode::Straight,1400);
			return Advance(.5);
		case 17:
			Test->TestFalse(TEXT("Fast wall impact breaks prison"),B->IsInBallForm());
			Test->TestEqual(TEXT("Wall costs captive one HP"),B->GetHealth(),B->GetMaxHealth()-1);
			Test->TestTrue(TEXT("Release did not leave player inside wall"),FMath::Abs(B->GetActorLocation().X-Wall->GetActorLocation().X)>45);
			if (Wall.IsValid()) { Wall->Destroy(); }
			Test->TestTrue(TEXT("Primary player can also be captured"),A->GetBallForm()->Capture(C));
			W->GetGameViewport()->SetForceDisableSplitscreen(true); PC->SetViewTarget(A);
			return Advance(.4);
		case 18:
			Shot(TEXT("03-Solo-CaptureHUD.png")); return Advance(3.8);
		case 19:
			Shot(TEXT("04-Solo-FinalSecond.png")); return Advance(.15);
		case 20:
			A->GetBallForm()->Restore(); W->GetGameViewport()->SetForceDisableSplitscreen(false);
			if (Camera.IsValid()) { Camera->Destroy(); } if (Floor.IsValid()) { Floor->Destroy(); }
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	int32 Step=0;
	double Started=FPlatformTime::Seconds(),Next=0;
	float Remaining=0;
	FVector Origin=FVector::ZeroVector;
	TWeakObjectPtr<AChaosImpactBuddyBall> Shell;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AStaticMeshActor> Wall;
	TWeakObjectPtr<AStaticMeshActor> Floor;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrisonBallTest,"ChaosImpact.PrisonBall.Training",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPrisonBallTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FPrisonCommand(this)); return true; }
#endif
