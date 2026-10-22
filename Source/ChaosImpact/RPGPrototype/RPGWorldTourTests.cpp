#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGWorldLayout.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
/**
 * Pictures of the RPG map (Saved/RPGWorldQA): from high above, then the player's own view in every area. With
 * -RPGTourOnly=<AreaId> just that area; -RPGTourHigh for only the overviews.
 */
class FRPGWorldTourCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGWorldTourCommand(FAutomationTestBase* InTest):Test(InTest)
	{
		FString Only; FParse::Value(FCommandLine::Get(),TEXT("RPGTourOnly="),Only);
		const bool bHighOnly=FParse::Param(FCommandLine::Get(),TEXT("RPGTourHigh"));
		Views.Add({TEXT("0_Overview_South"),FVector(-9000,0,26000),FRotator(-62,0,0),75});
		Views.Add({TEXT("0_Overview_North"),FVector(14000,0,30000),FRotator(-62,0,0),75});
		Views.Add({TEXT("0_Overview_Canyon"),FVector(17000,-500,16000),FRotator(-55,0,0),70});
		Views.Add({TEXT("0_Overview_Cave"),FVector(30000,200,14000),FRotator(-58,0,0),70});
		if(!bHighOnly)
		{
			for(const auto& A:RPGWorld::Areas())
			{
				if(A.Id==TEXT("ColonyPlaza") || (!Only.IsEmpty() && A.Id.ToString()!=Only)) { continue; }
				Views.Add({FString::Printf(TEXT("1_%s"),*A.Id.ToString()),RPGWorld::AreaPoint(A.Id)+FVector(0,0,120),FRotator::ZeroRotator,0});
			}
		}
	}
	bool Update() override
	{
		if(FPlatformTime::Seconds()-Started>900) { Test->AddError(TEXT("Tour timeout")); return true; }
		UWorld* W=nullptr; for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
		if(!W) { return false; }
		if(Stage==0)
		{ UGameplayStatics::OpenLevel(W,TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1")); Stage=1; Next=FPlatformTime::Seconds()+4; return false; }
		auto* PC=Cast<ARPGPrototypePlayerController>(UGameplayStatics::GetPlayerController(W,0));
		auto* P=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr;
		if(!P || !PC->IsGameplayActive() || FPlatformTime::Seconds()<Next) { return false; }
		if(Stage==1)
		{
			for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; }
			P->SetPrototypeAutomatedInput(true); Stage=2;
		}
		if(Index>0 && !bShot)
		{
			// The picture first; the next place only on a later frame (the picture is taken at the end of this one).
			const auto& Done=Views[Index-1];
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGWorldQA"),Done.Name+TEXT(".png")),true,false);
			UE_LOG(LogTemp,Display,TEXT("RPGTOUR %s region=%s ground=%.0f"),*Done.Name,*RPGWorld::RegionAt(Done.At),RPGWorld::Height(Done.At.X,Done.At.Y));
			bShot=true; Next=FPlatformTime::Seconds()+.3; return false;
		}
		bShot=false;
		if(Index>=Views.Num())
		{
			if(Camera.IsValid()) { PC->SetViewTarget(P); Camera->Destroy(); }
			return true;
		}
		const auto& V=Views[Index++];
		if(V.Fov>0)
		{
			if(!Camera.IsValid()) { Camera=W->SpawnActor<ACameraActor>(V.At,V.Rotation); }
			Camera->SetActorLocationAndRotation(V.At,V.Rotation); Camera->GetCameraComponent()->SetFieldOfView(V.Fov);
			PC->SetViewTarget(Camera.Get());
		}
		else
		{
			PC->SetViewTarget(P);
			P->SetActorLocation(V.At,false,nullptr,ETeleportType::TeleportPhysics);
		}
		Next=FPlatformTime::Seconds()+(V.Fov>0?5.0:3.0);
		return false;
	}
private:
	struct FView { FString Name; FVector At; FRotator Rotation; float Fov; };
	FAutomationTestBase* Test;
	TArray<FView> Views;
	int32 Stage=0, Index=0;
	bool bShot=false;
	double Started=FPlatformTime::Seconds(), Next=0;
	TWeakObjectPtr<ACameraActor> Camera;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWorldTourTest,"ChaosImpact.RPG.WorldTour",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGWorldTourTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGWorldTourCommand(this)); return true; }
#endif
