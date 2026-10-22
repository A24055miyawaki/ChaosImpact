#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactTornado.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"

namespace
{
class FRPGVFXCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGVFXCommand(FAutomationTestBase* InTest):Test(InTest) {}
	bool Update() override
	{
		if(FPlatformTime::Seconds()-Started>100) { Test->AddError(TEXT("RPG visual test timeout")); return true; }
		UWorld* W=nullptr;
		for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if(!W) { return false; }
		auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController());
		if(!PC || !PC->GetPawn()) { return false; }
		if(Stage==0)
		{
			UGameplayStatics::OpenLevel(PC,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1"));
			Stage=1; return false;
		}
		auto* P=Cast<ARPGPrototypeCharacter>(PC->GetPawn());
		auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>();
		if(!P || !Mode || !PC->IsGameplayActive() || W->GetTimeSeconds()<Next) { return false; }
		if(Stage==1)
		{
			for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
			for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { It->SetTrainingMenuFrozen(true); }
			P->SetPrototypeAutomatedInput(true); OriginalHP=P->GetHealth();
			Base=P->GetActorLocation()+FVector(0,0,2000);
			auto* Floor=W->SpawnActor<AStaticMeshActor>(Base-FVector(0,0,15),FRotator::ZeroRotator);
			Fixture=Floor; Floor->SetMobility(EComponentMobility::Movable);
			Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->SetActorScale3D(FVector(20,20,.2f));
			Floor->GetStaticMeshComponent()->SetMaterial(0,ChaosImpactBallTypes::MakeEmissive(Floor,FLinearColor(.055f,.07f,.1f),.12f));
			auto* Cam=W->SpawnActor<ACameraActor>(Base+FVector(-900,0,1450),FRotator(-58,0,0));
			Camera=Cam; Cam->GetCameraComponent()->SetFieldOfView(58);
			PC->SetViewTarget(Cam);
			Kinds={TEXT("Fire"),TEXT("Cold"),TEXT("Gravity"),TEXT("Wind"),TEXT("Smoke"),TEXT("Electric"),TEXT("Snow"),TEXT("Light")};
			Types={EChaosImpactBallType::Fire,EChaosImpactBallType::Ice,EChaosImpactBallType::Black,EChaosImpactBallType::Wind,
				EChaosImpactBallType::Smoke,EChaosImpactBallType::Thunder,EChaosImpactBallType::Snow,EChaosImpactBallType::Beam};
			for(int32 I=0;I<Types.Num();++I)
			{
				const FVector At=Base+FVector(I/4==0?250:-250,(I%4-1.5f)*340,0);
				auto* F=ARPGPrototypeEffect::Create(P,FRPGPrototypeItem::Make(Types[I]),Kinds[I],At,110,8,0);
				Fields.Add(F); F->Direction=FVector::ZeroVector;
			}
			Stage=2; Next=W->GetTimeSeconds()+1.4; return false;
		}
		if(Stage==2)
		{
			int32 FieldsBuilt=0;
			for(TActorIterator<ARPGPrototypeVFX> It(W);It;++It)
			{
				if(It->IsFieldVisual()) { ++FieldsBuilt; Test->TestTrue(TEXT("Each persistent field is drawn (own geometry or the VS effect)"),It->GetGeometryVertexCount()>0 || It->UsesVersusVisual()); }
				Test->TestFalse(TEXT("Visual actors never collide"),It->GetActorEnableCollision());
				TArray<UPrimitiveComponent*> Parts; It->GetComponents(Parts);
				for(auto* Part:Parts)
				{
					Test->TestTrue(TEXT("Field parts cannot affect gameplay collision"),Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
					Test->TestFalse(TEXT("Visible in P2-P4, not owner-only"),Part->bOnlyOwnerSee);
				}
			}
			Test->TestEqual(TEXT("Eight attributes have independent field visuals"),FieldsBuilt,8);
			Test->TestEqual(TEXT("Presentation does not damage the player"),P->GetHealth(),OriginalHP);
			int32 Hazards=0;
			for(TActorIterator<AChaosImpactHazardZone> It(W);It;++It) { ++Hazards; Test->TestTrue(TEXT("VS zones drawing RPG fields are look-only"),It->IsVisualOnly()); }
			Test->TestEqual(TEXT("Cold, fire, gravity and smoke use the VS zone look"),Hazards,4);
			int32 Tornados=0; for(TActorIterator<AChaosImpactTornado> It(W);It;++It) { ++Tornados; Test->TestTrue(TEXT("VS tornado drawing the wind field is look-only"),It->IsVisualOnly()); }
			Test->TestEqual(TEXT("Wind uses the VS tornado look"),Tornados,1);
			Shot(TEXT("rpg-fx-fields.png")); Stage=3; Next=W->GetTimeSeconds()+.25; return false;
		}
		if(Stage==3)
		{
			Mode->SetLocalPlayerCount(2);
			for(auto* Local:W->GetGameInstance()->GetLocalPlayers())
			{
				if(auto* Controller=Local->GetPlayerController(W))
				{
					Controller->SetViewTarget(Camera.Get());
					if(auto* Player=Cast<ARPGPrototypeCharacter>(Controller->GetPawn())) { Player->SetTrainingMenuFrozen(true); }
				}
			}
			Stage=4; Next=W->GetTimeSeconds()+.7; return false;
		}
		if(Stage==4)
		{
			Shot(TEXT("rpg-fx-split.png")); Stage=5; Next=W->GetTimeSeconds()+.2; return false;
		}
		if(Stage==5)
		{
			Mode->SetLocalPlayerCount(1); PC->SetViewTarget(Camera.Get());
			for(const auto& F:Fields) { if(F.IsValid()) { F->Destroy(); } }
			Stage=6; Next=W->GetTimeSeconds()+1; return false;
		}
		if(Stage==6)
		{
			int32 Remaining=0; for(TActorIterator<ARPGPrototypeVFX> It(W);It;++It) { ++Remaining; }
			Test->TestEqual(TEXT("Destroyed fields stop and fade their visuals"),Remaining,0);
			for(int32 I=0;I<Types.Num();++I)
			{
				ARPGPrototypeVFX::PlayImpact(W,Types[I],Base+FVector(I/4==0?250:-250,(I%4-1.5f)*340,45),1);
			}
			ARPGPrototypeVFX::PlayLightning(W,Base+FVector(-250,150,50),Base+FVector(-250,450,50));
			Stage=7; Next=W->GetTimeSeconds()+.16; return false;
		}
		if(Stage==7)
		{
			for(TActorIterator<ARPGPrototypeVFX> It(W);It;++It) { Test->TestTrue(TEXT("Impact and chain meshes are built"),It->GetGeometryVertexCount()>0); }
			Shot(TEXT("rpg-fx-impacts.png")); Stage=8; Next=W->GetTimeSeconds()+1.2; return false;
		}
		int32 Remaining=0; for(TActorIterator<ARPGPrototypeVFX> It(W);It;++It) { ++Remaining; }
		Test->TestEqual(TEXT("All impact actors clean up automatically"),Remaining,0);
		PC->SetViewTarget(P); if(Camera.IsValid()) { Camera->Destroy(); } if(Fixture.IsValid()) { Fixture->Destroy(); }
		P->SetTrainingMenuFrozen(false); P->SetPrototypeAutomatedInput(false);
		Mode->ResetPrototype(); return true;
	}
private:
	void Shot(const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); }
	FAutomationTestBase* Test;
	double Started=FPlatformTime::Seconds(),Next=0;
	int32 Stage=0;
	float OriginalHP=0;
	FVector Base=FVector::ZeroVector;
	TArray<FName> Kinds;
	TArray<EChaosImpactBallType> Types;
	TArray<TWeakObjectPtr<ARPGPrototypeEffect>> Fields;
	TWeakObjectPtr<ACameraActor> Camera;
	TWeakObjectPtr<AActor> Fixture;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGVFXTest,"ChaosImpact.RPGPrototype.BallVFX",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGVFXTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGVFXCommand(this)); return true; }
#endif
