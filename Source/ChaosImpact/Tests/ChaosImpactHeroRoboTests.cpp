#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactPuppetComponent.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroRoboAssets,"ChaosImpact.Characters.HeroRobo.Assets",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FHeroRoboAssets::RunTest(const FString&)
{
	TestEqual(TEXT("Existing Fish index is unchanged"),FString(ChaosImpactRoster::Get(5).AssetPrefix),FString(TEXT("Fish")));
	for (int32 Index:{ChaosImpactRoster::HeroIndex,ChaosImpactRoster::RoboIndex})
	{
		const auto& Info=ChaosImpactRoster::Get(Index); const FString Base=FString(Info.AssetFolder)+TEXT("/");
		TestTrue(TEXT("New roster tile is selectable"),UChaosImpactCharacterSelect::IsTileUnlocked(Index));
		TestFalse(TEXT("Preserve supplied fixed texture"),Info.bColourVariants);
		auto* Mesh=LoadObject<USkeletalMesh>(nullptr,*(Base+TEXT("SK_")+Info.AssetPrefix));
		auto* Material=LoadObject<UMaterialInterface>(nullptr,*(Base+TEXT("M_CI_")+Info.AssetPrefix));
		TestNotNull(TEXT("Texture imported"),LoadObject<UTexture2D>(nullptr,*(Base+TEXT("T_")+Info.AssetPrefix)));
		if (!TestNotNull(TEXT("Skinned game mesh"),Mesh) || !TestNotNull(TEXT("Fixed material"),Material)) { continue; }
		TestTrue(TEXT("Character height matches existing roster"),Mesh->GetBounds().BoxExtent.Z*2>135 && Mesh->GetBounds().BoxExtent.Z*2<155);
		for (const auto& Slot:Mesh->GetMaterials()) { TestTrue(TEXT("Texture assigned to every slot"),Slot.MaterialInterface==Material); }
		for (const TCHAR* Bone:{TEXT("Character1_Hips"),TEXT("Character1_Head"),TEXT("Character1_LeftArm"),TEXT("Character1_LeftForeArm"),
			TEXT("Character1_LeftHand"),TEXT("Character1_RightArm"),TEXT("Character1_RightForeArm"),TEXT("Character1_RightHand"),
			TEXT("Character1_LeftUpLeg"),TEXT("Character1_LeftLeg"),TEXT("Character1_LeftFoot"),TEXT("Character1_RightUpLeg"),TEXT("Character1_RightLeg"),TEXT("Character1_RightFoot")})
		{ TestTrue(FString::Printf(TEXT("Required joint %s"),Bone),Mesh->GetRefSkeleton().FindBoneIndex(Bone)!=INDEX_NONE); }
	}
	return true;
}
namespace
{
class FHeroRoboGameplay : public IAutomationLatentCommand
{
public:
	explicit FHeroRoboGameplay(FAutomationTestBase* T):Test(T) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds()-Started>150) { Test->AddError(FString::Printf(TEXT("Hero/Robo timeout %d"),Step)); return true; }
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
			const FVector Focus=P->GetActorLocation()+FVector(0,0,0), Eye=Focus+FVector(280,230,100);
			Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation()); PC->SetViewTarget(Camera.Get());
		}
		const double Now=W->GetTimeSeconds(); if(Now<Next) { return false; }
		const auto Advance=[&](double Delay) { ++Step; Next=Now+Delay; return false; };
		const auto Shot=[&](const FString& Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("HeroRoboQA"),Name),true,false); };
		auto* Puppet=P->FindComponentByClass<UChaosImpactPuppetComponent>();
		switch(Step)
		{
		case 1:
			for(TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { It->Destroy(); }
			for(TActorIterator<AChaosImpactBall> It(W);It;++It) { It->Destroy(); }
			Original=State->CharacterIndex; State->CharacterIndex=ChaosImpactRoster::HeroIndex;
			Camera=W->SpawnActor<ACameraActor>(); Camera->GetCameraComponent()->SetFieldOfView(52); Camera->GetCameraComponent()->bConstrainAspectRatio=false;
			return Advance(.8);
		case 2:
			Test->TestNotNull(TEXT("Hero puppet built"),Puppet); Test->TestEqual(TEXT("Training uses selected Hero"),P->GetShownCharacter(),ChaosImpactRoster::HeroIndex);
			Shot(TEXT("Hero-Idle.png")); Before=P->GetActorLocation(); bMove=true; return Advance(.45);
		case 3:
			Test->TestTrue(TEXT("Hero can walk"),FVector::Dist2D(Before,P->GetActorLocation())>50);
			Shot(TEXT("Hero-Move.png")); bMove=false;
			return Advance(.2);
		case 4:
		{
			auto* B=W->SpawnActor<AChaosImpactBall>(P->GetActorLocation()+FVector(0,800,0),FRotator::ZeroRotator); B->MakePickup();
			Test->TestTrue(TEXT("Hero holds a ball"),B->ConsumePickup(P)); P->BeginThrowInput(); return Advance(.4);
		}
		case 5:
			P->EndThrowInput(); Test->TestTrue(TEXT("Hero throw uses animation cue"),P->IsThrowReleasePending()); Shot(TEXT("Hero-Throw.png")); return Advance(.6);
		case 6:
			State->CharacterIndex=ChaosImpactRoster::RoboIndex; return Advance(.7);
		case 7:
			Test->TestEqual(TEXT("Training uses selected Robo"),P->GetShownCharacter(),ChaosImpactRoster::RoboIndex);
			Test->TestNotNull(TEXT("Rigged Robo puppet built"),Puppet); Shot(TEXT("Robo-Idle.png")); Before=P->GetActorLocation(); bMove=true; return Advance(.4);
		case 8:
			Test->TestTrue(TEXT("Robo can walk"),FVector::Dist2D(Before,P->GetActorLocation())>50); Shot(TEXT("Robo-Move.png")); bMove=false;
			return Advance(.3);
		case 9:
		{
			auto* B=W->SpawnActor<AChaosImpactBall>(P->GetActorLocation()+FVector(0,800,0),FRotator::ZeroRotator); B->MakePickup();
			Test->TestTrue(TEXT("Robo holds a ball"),B->ConsumePickup(P)); P->BeginThrowInput(); return Advance(.4);
		}
		case 10:
			P->EndThrowInput(); Test->TestTrue(TEXT("Robo throw uses animation cue"),P->IsThrowReleasePending()); Shot(TEXT("Robo-Throw.png")); return Advance(.7);
		case 11:
			State->CharacterIndex=Original; PC->SetViewTarget(P); if(Camera.IsValid()) { Camera->Destroy(); }
			UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/solo/Lvl_SoloRoomTest"),true,TEXT("CISolo=1?CITargets=0?CICPUCount=0?CILocalPlayers=1"));
			++Step; Next=0; return false;
		case 12:
			if(!PC->IsSoloMode()) { return false; }
			Test->TestEqual(TEXT("Solo automatically uses Hero regardless of selected character"),P->GetShownCharacter(),ChaosImpactRoster::HeroIndex);
			State->CharacterIndex=5;
			for(TActorIterator<AChaosImpactSoloRoom> It(W);It;++It) { Room=*It; break; }
			if (!Test->TestNotNull(TEXT("Solo test room exists"),Room.Get())) { return true; }
			Room->StartRoom(P); return Advance(.7);
		case 13:
		{
			Test->TestEqual(TEXT("Solo Hero is not overwritten by loadout changes"),P->GetShownCharacter(),ChaosImpactRoster::HeroIndex);
			int32 Count=0;
			for(TActorIterator<AChaosImpactCharacter> It(W);It;++It)
			{
				if(*It==P || It->ActorHasTag(TEXT("CI.SoloBoss"))) { continue; }
				if(It->GetController()) { It->GetController()->SetActorTickEnabled(false); }
				Test->TestEqual(TEXT("Room minion automatically uses Robo"),It->GetShownCharacter(),ChaosImpactRoster::RoboIndex);
				Test->TestNotNull(TEXT("Room minion puppet initialized"),It->FindComponentByClass<UChaosImpactPuppetComponent>()); ++Count;
			}
			Test->TestTrue(TEXT("Multiple Robo enemies appeared"),Count>=5); Shot(TEXT("Solo-Hero-Robo.png"));
			TArray<TWeakObjectPtr<AChaosImpactCharacter>> Minions;
			for(TActorIterator<AChaosImpactCharacter> It(W);It;++It) { if(*It!=P && !It->ActorHasTag(TEXT("CI.SoloBoss"))) { Minions.Add(*It); } }
			for(const auto& Minion:Minions) { if(Minion.IsValid()) { Minion->Destroy(); } }
			return Advance(1);
		}
		case 14:
		{
			bool Found=false;
			for(TActorIterator<AChaosImpactCharacter> It(W);It;++It)
			{
				if(It->ActorHasTag(TEXT("CI.SoloBoss"))) { Found=true; Test->TestTrue(TEXT("Boss retains original model"),It->GetShownCharacter()!=ChaosImpactRoster::RoboIndex); }
			}
			Test->TestTrue(TEXT("Boss appeared with explicit appearance preservation"),Found); return true;
		}
		}
		return false;
	}
private:
	FAutomationTestBase* Test; int32 Step=0,Original=0; double Started=FPlatformTime::Seconds(),Next=0;
	bool bMove=false; FVector Before;
	TWeakObjectPtr<ACameraActor> Camera; TWeakObjectPtr<AChaosImpactSoloRoom> Room;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroRoboGameplayTest,"ChaosImpact.Characters.HeroRobo.Gameplay",
 EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FHeroRoboGameplayTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FHeroRoboGameplay(this)); return true; }
#endif
