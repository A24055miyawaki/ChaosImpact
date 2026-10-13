#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactTrainingArena.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/** Opt-in capture tool, NOT part of the normal control regression suite.
	 * Launch training at 1280x720; add ?CIP1Gamepad=1 for the two pad clips.
	 * Real character input, real viewport and HUD, no imitation or editor-level writes.
	 */
	class FRecordControlsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRecordControlsCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			using namespace ChaosImpactSettings;
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& C : GEngine->GetWorldContexts())
			{ if (C.World() && C.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(C.World()->GetFirstPlayerController()); } }
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (FPlatformTime::Seconds() - Started > 600)
			{ Test->AddError(TEXT("Control recording timed out")); Cleanup(PC); return true; }
			if (!PC || !Player || !PC->IsGameplayActive()) { return false; }
			UWorld* World = PC->GetWorld();
			if (Profile < 0)
			{
				if (!Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				Profile = AddProfile(TEXT("QV") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
				if (Profile <= 0) { Test->AddError(TEXT("No spare recording profile")); return true; }
				auto* Loadouts = UChaosImpactLoadoutSubsystem::Get(PC);
				OriginalName = Loadouts->GetLoadout(0).Nickname;
				Loadouts->SetNickname(0,GetProfile(Profile).Name);
				bPad = PC->IsUsingGamepad(); Device = GetPlayerDevice(PC);
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				Origin = Player->GetActorLocation();
				for (TActorIterator<AChaosImpactTrainingArena> It(World); It; ++It)
				{ Origin = It->GetCenter() + FVector(-350,-350,100); break; }
				bOriginalFixed = FApp::UseFixedTimeStep(); OriginalDelta = FApp::GetFixedDeltaTime();
				FApp::SetFixedDeltaTime(1.0 / 24.0); FApp::SetUseFixedTimeStep(true);
			}
			const auto Key = [PC](FKey K,EInputEvent E)
			{ PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),K,E,FPlatformTime::Cycles64())); };
			const auto Axis = [PC](FKey K,float V)
			{ PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),K,V,1.f / 24,1,FPlatformTime::Cycles64())); };
			const auto Point = [PC](FVector At)
			{
				FVector2D Pixels; if (!PC->ProjectWorldLocationToScreen(At,Pixels)) { return; }
				PC->SetMouseLocation(FMath::RoundToInt(Pixels.X),FMath::RoundToInt(Pixels.Y));
				FSlateApplication::Get().SetCursorPos(UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC).LocalToAbsolute(Pixels / UWidgetLayoutLibrary::GetViewportScale(PC)));
			};
			const auto Give = [World,Player](EChaosImpactBallType Type)
			{
				AChaosImpactBall* B = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),FTransform(Player->GetActorLocation() + FVector(0,0,500)),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				B->SetBallType(Type); B->FinishSpawning(FTransform(Player->GetActorLocation() + FVector(0,0,500)));
				B->MakePickup(); Player->TryPickupBall(B); B->Destroy();
			};
			if (Frame == -24)
			{
				Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
				SetControlScheme(Profile,Device,static_cast<EChaosImpactControlScheme>(Clip));
				Give(EChaosImpactBallType::Normal); Give(EChaosImpactBallType::Fire);
				const TCHAR* const Names[] = {TEXT("Normal"),TEXT("Simple"),TEXT("MouseClick"),TEXT("MouseHold")};
				Folder = FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ControlRecordings"),(bPad ? TEXT("Pad") : TEXT("")) + FString(Names[Clip]));
				IFileManager::Get().MakeDirectory(*Folder,true);
			}
			if (Frame < 0) { ++Frame; Point(Origin + FVector(200,400,-100)); return false; }
			const auto Scheme = static_cast<EChaosImpactControlScheme>(Clip);
			const auto ActionKey = [this](EChaosImpactAction A) { FKey K = GetActiveKey(Profile,Device,A,0); return K.IsValid() ? K : GetActiveKey(Profile,Device,A,1); };
			if (!bPad)
			{
				const FVector Here = Player->GetActorLocation();
				if (Scheme == EChaosImpactControlScheme::MouseOnly && Frame < 48) { Point(Origin + FVector(0,550,-100)); }
				else { Point(FVector(Here.X + (Frame < 24 ? 0 : 300),Here.Y + (Frame < 24 ? 350 : 100),Origin.Z - 100)); }
			}
			if (bPad)
			{
				Axis(EKeys::Gamepad_LeftX,Frame < 24 ? 0.7f : Frame < 48 ? 0.0f : 0.0f);
				Axis(EKeys::Gamepad_LeftY,Frame >= 24 && Frame < 48 ? 0.7f : 0);
				Axis(EKeys::Gamepad_RightX,Clip == 0 ? 0.65f : 0);
				Axis(EKeys::Gamepad_RightY,Clip == 0 ? 0.35f : 0);
			}
			else if (Clip < 2)
			{
				if (Frame == 0) { Key(ActionKey(EChaosImpactAction::MoveRight),IE_Pressed); }
				if (Frame == 24) { Key(ActionKey(EChaosImpactAction::MoveRight),IE_Released); Key(ActionKey(EChaosImpactAction::MoveUp),IE_Pressed); }
				if (Frame == 48) { Key(ActionKey(EChaosImpactAction::MoveUp),IE_Released); }
			}
			else
			{
				if (Frame == 0) { Key(EKeys::RightMouseButton,IE_Pressed); }
				if ((Clip == 2 && Frame == 1) || Frame == 48) { Key(EKeys::RightMouseButton,IE_Released); }
			}
			if (Frame == 48) { Key(ActionKey(EChaosImpactAction::Throw),IE_Pressed); }
			if (Frame == 78) { Key(ActionKey(EChaosImpactAction::Throw),IE_Released); }
			if (Frame == 96) { Key(ActionKey(EChaosImpactAction::Dash),IE_Pressed); }
			if (Frame == 97) { Key(ActionKey(EChaosImpactAction::Dash),IE_Released); }
			if (Frame == 106) { Give(EChaosImpactBallType::Ice); }
			if (Frame == 112) { Key(Clip >= 2 ? EKeys::MouseScrollUp : ActionKey(EChaosImpactAction::SwapBall),IE_Pressed); }
			if (Frame == 114) { Key(Clip >= 2 ? EKeys::MouseScrollUp : ActionKey(EChaosImpactAction::SwapBall),IE_Released); }
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder,FString::Printf(TEXT("Frame%04d.png"),Frame)),true,false);
			if (++Frame >= 168)
			{
				Test->TestTrue(TEXT("Recorded real movement"),FVector::Dist2D(Origin,Player->GetActorLocation()) > 100);
				UE_LOG(LogTemp,Display,TEXT("CONTROLVIDEO recorded %s"),*Folder);
				if (++Clip >= (bPad ? 2 : 4)) { Cleanup(PC); return true; }
				Frame = -24;
			}
			return false;
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (Profile > 0)
			{
				if (auto* L = UChaosImpactLoadoutSubsystem::Get(PC)) { L->SetNickname(0,OriginalName); }
				ChaosImpactSettings::RemoveProfile(Profile); Profile = -1;
				FApp::SetUseFixedTimeStep(bOriginalFixed); FApp::SetFixedDeltaTime(OriginalDelta);
			}
		}
		FAutomationTestBase* Test;
		int32 Profile = -1, Clip = 0, Frame = -24;
		bool bPad = false, bOriginalFixed = false;
		double Started = FPlatformTime::Seconds(), OriginalDelta = 0;
		FVector Origin; FString Folder, OriginalName;
		EChaosImpactDevice Device = EChaosImpactDevice::KeyboardMouse;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactRecordControlsTest,"ChaosImpact.Tools.RecordControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactRecordControlsTest::RunTest(const FString& Parameters)
{ ADD_LATENT_AUTOMATION_COMMAND(FRecordControlsCommand(this)); return true; }
#endif
