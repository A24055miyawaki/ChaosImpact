#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactShoulderCameraComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactTrainingArena.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShoulderMathTest,"ChaosImpact.Camera.Shoulder.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FShoulderMathTest::RunTest(const FString&)
{
	using Camera = UChaosImpactShoulderCameraComponent;
	TestTrue(TEXT("Mouse right turns right"),Camera::MouseLookDegrees(FVector2D(10,0),.14f).X > 0);
	TestTrue(TEXT("Mouse up looks up (engine mouse Y is positive up)"),Camera::MouseLookDegrees(FVector2D(0,10),.14f).Y > 0);
	TestTrue(TEXT("Pad right turns right"),Camera::PadLookDegrees(FVector2D(1,0),1,.2f,160,100).X > 0);
	TestTrue(TEXT("Pad down looks down (engine pad Y is positive down)"),Camera::PadLookDegrees(FVector2D(0,1),1,.2f,160,100).Y < 0);
	TestTrue(TEXT("Resting pad noise never rotates view"),Camera::PadLookDegrees(FVector2D(.1f,-.1f),1,.2f,160,100).IsNearlyZero());
	TestTrue(TEXT("Stick speed is frame-rate independent"),Camera::PadLookDegrees(FVector2D(.6f,.4f),.5f,.2f,160,100)
		.Equals(Camera::PadLookDegrees(FVector2D(.6f,.4f),1,.2f,160,100) * .5,0.001));
	for (float Yaw : {-179.f,-90.f,0.f,45.f,179.f})
	{
		const FVector View(400,-250,300);
		for (float Pitch : {-25.f,-12.f,15.f})
		{
			const FVector Anchor = Camera::MakeReticleAnchor(View,FRotator(Pitch,Yaw,0),120);
			const FVector Right = FRotator(0,Yaw,0).RotateVector(FVector::RightVector);
			TestTrue(TEXT("Direction marker never moves laterally with yaw/pitch"),FMath::Abs(FVector::DotProduct(Anchor-View,Right)) < .01);
			TestEqual(TEXT("Reticle follows the horizontal throw plane, not camera pitch"),Anchor.Z,120.);
		}
	}
	return true;
}

namespace
{
	class FShoulderGameplayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FShoulderGameplayCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			using namespace ChaosImpactSettings;
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& C : GEngine->GetWorldContexts())
			{ if (C.World() && C.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(C.World()->GetFirstPlayerController()); } }
			auto* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 100) { Test->AddError(FString::Printf(TEXT("Shoulder camera stalled at %d"),Stage)); Cleanup(PC); return true; }
			if (!Player) { return false; }
			UWorld* World = PC->GetWorld();
			auto* Camera = Player->GetShoulderCamera();
			const auto Axis = [this,PC](FKey K,float V)
			{ PC->InputKey(FInputKeyEventArgs(nullptr,InputDevice,K,V,1.f / 60,1,FPlatformTime::Cycles64())); };
			if (bPad && Profile > 0)
			{
				Axis(EKeys::Gamepad_LeftY,bMove ? .8f : 0);
				Axis(EKeys::Gamepad_LeftX,bDriveMove ? .8f : 0);
				Axis(EKeys::Gamepad_RightX,bLook ? .7f : 0);
				Axis(EKeys::Gamepad_RightY,0);
			}
			else if (bLook) { Axis(EKeys::MouseX,8); Axis(EKeys::MouseY,0); }
			if (Now < NextAt) { return false; }
			const auto Next = [this,Now](double Delay) { ++Stage; NextAt = Now + Delay; return false; };
			const auto Key = [this,PC](FKey K,EInputEvent E)
			{ PC->InputKey(FInputKeyEventArgs(nullptr,InputDevice,K,E,FPlatformTime::Cycles64())); };
			const auto ActionKey = [this](EChaosImpactAction A) { return GetActiveKey(Profile,Device,A,0); };
			const auto ClearBalls = [Player,World]()
			{
				Player->RequestCancelThrow();
				for (int I = 0; I < 2 && Player->GetCarriedBallCount(); ++I) { Player->RequestDropBall(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
			};
			const auto Give = [Player,World](EChaosImpactBallType Type)
			{
				const FTransform T(Player->GetActorLocation() + FVector(0,0,500));
				auto* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(Type); Ball->FinishSpawning(T); Ball->MakePickup();
				const bool bTaken = Player->TryPickupBall(Ball); Ball->Destroy(); return bTaken;
			};
			const auto Shot = [this](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ShoulderCameraQA"),
					(bPad ? TEXT("Pad-") : TEXT("Mouse-")) + FString(Name)),true,false);
			};
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				Test->TestFalse(TEXT("Top-down remains the default"),PC->IsTrainingShoulderCameraEnabled());
				Test->TestFalse(TEXT("No default shoulder camera"),Player->IsShoulderViewActive());
				OriginalRotation = Player->GetCameraBoom()->GetComponentRotation();
				OriginalArm = Player->GetCameraBoom()->TargetArmLength;
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				ClearBalls();
				Profile = AddProfile(TEXT("QC") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
				if (Profile <= 0) { Test->AddError(TEXT("No spare test profile")); return true; }
				for (int I = 0; I < PC->GetGameInstance()->GetLocalPlayers().Num(); ++I)
				{
					OriginalNames.Add(UChaosImpactLoadoutSubsystem::Get(PC)->GetLoadout(I).Nickname);
					UChaosImpactLoadoutSubsystem::Get(PC)->SetNickname(I,GetProfile(Profile).Name);
				}
				bPad = PC->IsUsingGamepad(); Device = GetPlayerDevice(PC);
				{
					auto& Mapper = IPlatformInputDeviceMapper::Get();
					InputDevice = Mapper.GetPrimaryInputDeviceForUser(PC->GetPlatformUserId());
					if (!InputDevice.IsValid() || Mapper.GetUserForInputDevice(InputDevice) != PC->GetPlatformUserId())
					{
						// No physical pad is required. Respect UE's platform-user input filter in split-screen.
						InputDevice = Mapper.AllocateNewInputDeviceId(); bAllocatedInputDevice = true;
						Mapper.Internal_MapInputDeviceToUser(InputDevice,PC->GetPlatformUserId(),EInputDeviceConnectionState::Connected);
					}
				}
				for (auto D : {EChaosImpactDevice::KeyboardMouse,EChaosImpactDevice::Xbox,EChaosImpactDevice::Switch,EChaosImpactDevice::PlayStation})
				{ SetControlScheme(Profile,D,EChaosImpactControlScheme::Normal); }
				SetAimAssist(Profile,false);
				Origin = Player->GetActorLocation();
				for (TActorIterator<AChaosImpactTrainingArena> It(World); It; ++It) { Origin = It->GetCenter() + FVector(-350,-350,100); break; }
				Player->ResetForOnlineMatch(Origin,FRotator::ZeroRotator);
				for (int I = 1; I < PC->GetGameInstance()->GetLocalPlayers().Num(); ++I)
				{
					const auto* OtherPC = PC->GetGameInstance()->GetLocalPlayers()[I]->GetPlayerController(World);
					if (auto* Other = OtherPC ? Cast<AChaosImpactCharacter>(OtherPC->GetPawn()) : nullptr)
					{ Other->ResetForOnlineMatch(Origin + FVector(-900,800 * I,0),FRotator::ZeroRotator); }
				}
				PC->ToggleTrainingOverlay();
				Test->TestEqual(TEXT("Training menu includes camera without losing ball list"),PC->GetMenuWidget()->GetEntryCount(),9);
				for (int I = 0; I < 6; ++I) { PC->GetMenuWidget()->Navigate(EKeys::Down); }
				Test->TestEqual(TEXT("Camera is menu row 6"),PC->GetMenuWidget()->GetSelectedIndex(),6);
				PC->GetMenuWidget()->ConfirmSelection();
				Test->TestTrue(TEXT("Menu actually switches camera"),PC->IsTrainingShoulderCameraEnabled());
				Shot(TEXT("01-TrainingMenu.png"));
				TimeBeforeMenu = World->GetTimeSeconds(); return Next(.6);
			case 1:
				Test->TestFalse(TEXT("Training settings never pause world time"),World->IsPaused());
				Test->TestTrue(TEXT("World advances while changing camera"),World->GetTimeSeconds() > TimeBeforeMenu);
				Test->TestTrue(TEXT("Training menu keeps actors frozen"),Player->IsTrainingMenuFrozen());
				Test->TestFalse(TEXT("Menu uses its own camera"),Player->IsShoulderViewActive());
				PC->CloseTrainingOverlay(); return Next(1);
			case 2:
				Test->TestTrue(TEXT("Shoulder enabled in gameplay"),Player->IsShoulderViewActive());
				Test->TestTrue(TEXT("Shoulder camera collides with walls"),Player->GetCameraBoom()->bDoCollisionTest);
				Test->TestTrue(TEXT("Shoulder camera moves closer"),Player->GetCameraBoom()->TargetArmLength < 510);
				Test->TestFalse(TEXT("No cursor obstructing shoulder gameplay"),PC->bShowMouseCursor);
				Camera->AddLookDegrees(90 - Camera->GetViewRotation().Yaw,5);
				Test->TestTrue(TEXT("Explicit yaw change points right in world space"),Camera->GetFlatAimDirection().Equals(FVector(0,1,0),.03));
				return Next(.2);
			case 3:
				// Real mouse events may arrive during the wait; check the current camera, not an old yaw.
				Test->TestTrue(TEXT("Aim follows horizontal camera yaw"),Player->GetAimDirection().Equals(Camera->GetFlatAimDirection(),.03));
				Test->TestTrue(TEXT("Camera pitch never tilts throws"),FMath::Abs(Player->GetAimDirection().Z) < .001);
				Start = Player->GetActorLocation(); MoveForward = Camera->GetFlatAimDirection();
				bMove = true; if (!bPad) { Key(ActionKey(EChaosImpactAction::MoveUp),IE_Pressed); }
				return Next(.4);
			case 4:
				bMove = false; if (!bPad) { Key(ActionKey(EChaosImpactAction::MoveUp),IE_Released); }
				Test->TestTrue(TEXT("W / left stick moves camera-relative"),FVector::DotProduct(Player->GetActorLocation() - Start,MoveForward) > 60);
				LookYaw = Camera->GetViewRotation().Yaw; bLook = true; return Next(.3);
			case 5:
				bLook = false;
				Test->TestTrue(TEXT("Real mouse / right-stick input rotates right, not left"),FRotator::NormalizeAxis(Camera->GetViewRotation().Yaw - LookYaw) > 1);
				Give(EChaosImpactBallType::Normal); Give(EChaosImpactBallType::Fire);
				Key(ActionKey(EChaosImpactAction::Throw),IE_Pressed); return Next(.3);
			case 6:
				Test->TestTrue(TEXT("Existing throw button charges in shoulder view"),Player->IsChargingThrow());
				if (!bChargeShotTaken) { Shot(TEXT("02-Charging.png")); bChargeShotTaken = true; NextAt = Now + .15; return false; }
				Key(ActionKey(EChaosImpactAction::Throw),IE_Released); return Next(.35);
			case 7:
				Test->TestEqual(TEXT("Release actually throws one held ball"),Player->GetCarriedBallCount(),1);
				ClearBalls();
				// Compare real ball launch parameters for every kind, both flight modes, three charges and two pitches.
				for (int ModeIndex = 0; ModeIndex < 2; ++ModeIndex)
				{
					for (int TypeIndex = 0; TypeIndex < ChaosImpactBallTypes::Count; ++TypeIndex)
					{
						const auto Type = ChaosImpactBallTypes::FromIndex(TypeIndex);
						for (float Charge : {.0f,.5f,1.f})
						{
							float SpeedA,UpA,SpeedB,UpB; bool OverA,OverB;
							EChaosImpactBallFlightMode ModeA,ModeB;
							PC->ToggleTrainingShoulderCamera(); Camera->UpdateCamera(0);
							Player->GetThrowFlight(Charge,Type,1,SpeedA,UpA,ModeA,OverA);
							PC->ToggleTrainingShoulderCamera(); Camera->UpdateCamera(0); Camera->AddLookDegrees(0,100);
							Player->GetThrowFlight(Charge,Type,1,SpeedB,UpB,ModeB,OverB);
							Test->TestTrue(TEXT("View/pitch preserve type, charge, horizontal speed, elevation and flight"),
								SpeedA == SpeedB && UpA == UpB && ModeA == ModeB && OverA == OverB);
						}
					}
					PC->ToggleBallFlightMode();
				}
				Camera->AddLookDegrees(-Camera->GetViewRotation().Yaw,-Camera->GetViewRotation().Pitch - 12);
				{
					const FURL OriginalURL = World->URL; World->URL.AddOption(TEXT("CIMatch=1"));
					Test->TestFalse(TEXT("VS cannot enable experimental camera"),PC->IsTrainingShoulderCameraEnabled()); World->URL = OriginalURL;
				}
				if (!bPad) { SetControlScheme(Profile,Device,EChaosImpactControlScheme::MouseOnly); }
				return Next(.3);
			case 8:
				if (!bPad)
				{
					Test->TestFalse(TEXT("Click-to-move keeps top-down even when shoulder was requested"),Player->IsShoulderViewActive());
					Test->TestTrue(TEXT("Mouse-only has its visible cursor"),PC->bShowMouseCursor);
					SetControlScheme(Profile,Device,EChaosImpactControlScheme::MouseHold);
				}
				return Next(.3);
			case 9:
				if (!bPad) { Test->TestFalse(TEXT("Hold-to-move also keeps top-down"),Player->IsShoulderViewActive()); }
				SetControlScheme(Profile,Device,EChaosImpactControlScheme::Simple); return Next(.4);
			case 10:
				Test->TestTrue(TEXT("Simple controls support shoulder camera"),Player->IsShoulderViewActive());
				ClearBalls(); Give(EChaosImpactBallType::Normal); Give(EChaosImpactBallType::Fire);
				Key(ActionKey(EChaosImpactAction::SwapBall),IE_Pressed); return Next(.1);
			case 11:
				Key(ActionKey(EChaosImpactAction::SwapBall),IE_Released); return Next(.2);
			case 12:
				Test->TestTrue(TEXT("Simple tap still swaps held balls"),Player->GetCarriedBallType(0) == EChaosImpactBallType::Fire);
				Key(ActionKey(EChaosImpactAction::SwapBall),IE_Pressed); return Next(.85);
			case 13:
				Test->TestEqual(TEXT("Simple hold still drops one ball"),Player->GetCarriedBallCount(),1);
				Key(ActionKey(EChaosImpactAction::SwapBall),IE_Released);
				StaminaBefore = Player->GetStamina(); Key(ActionKey(EChaosImpactAction::Dash),IE_Pressed); return Next(.05);
			case 14:
				Key(ActionKey(EChaosImpactAction::Dash),IE_Released);
				Test->TestTrue(TEXT("Existing dash button still consumes stamina"),Player->GetStamina() < StaminaBefore - .5f);
				ClearBalls(); Give(EChaosImpactBallType::Nova); return Next(.3); // Finish the dash before charging.
			case 15:
				Key(ActionKey(EChaosImpactAction::Throw),IE_Pressed); return Next(.75);
			case 16:
				Test->TestTrue(TEXT("Nova charge runs"),Player->IsChargingNova());
				Test->TestFalse(TEXT("Nova temporarily overrides shoulder"),Player->IsShoulderViewActive());
				Test->TestTrue(TEXT("Nova uses stage-wide overhead view"),Player->GetCameraBoom()->GetComponentRotation().Pitch < -75);
				Test->TestEqual(TEXT("Nova mouse cursor is available"),PC->bShowMouseCursor,!bPad);
				Shot(TEXT("03-Nova.png")); return Next(.15);
			case 17:
				Player->RequestCancelThrow(); Key(ActionKey(EChaosImpactAction::Throw),IE_Released); return Next(.7);
			case 18:
				Test->TestTrue(TEXT("Shoulder returns after nova"),Player->IsShoulderViewActive());
				ClearBalls(); Give(EChaosImpactBallType::Drive); Key(ActionKey(EChaosImpactAction::Throw),IE_Pressed); return Next(.1);
			case 19:
				Key(ActionKey(EChaosImpactAction::Throw),IE_Released); return Next(.25);
			case 20:
				Test->TestTrue(TEXT("Drive launched with unchanged throw button"),Player->IsDriving());
				Test->TestFalse(TEXT("Drive retains its ball-follow camera"),Player->IsShoulderViewActive());
				if (auto* Ball = Player->GetDrivenBall()) { DriveDirection = Ball->GetVelocity().GetSafeNormal2D(); }
				bDriveMove = true; if (!bPad) { Key(ActionKey(EChaosImpactAction::MoveRight),IE_Pressed); } return Next(.2);
			case 21:
				bDriveMove = false; if (!bPad) { Key(ActionKey(EChaosImpactAction::MoveRight),IE_Released); }
				Test->TestTrue(TEXT("Drive accepts steering"),Player->IsDriving());
				if (const auto* Ball = Player->GetDrivenBall())
				{ Test->TestTrue(TEXT("Movement input actually turns the drive ball"),FVector::DotProduct(DriveDirection,Ball->GetVelocity().GetSafeNormal2D()) < .999); }
				Player->StopDrivenBall(); return Next(.7);
			case 22:
				Test->TestTrue(TEXT("Shoulder returns after drive"),Player->IsShoulderViewActive());
				for (ULocalPlayer* Local : PC->GetGameInstance()->GetLocalPlayers())
				{
					auto* OtherPC = Cast<AChaosImpactPlayerController>(Local->GetPlayerController(World));
					auto* Other = OtherPC ? Cast<AChaosImpactCharacter>(OtherPC->GetPawn()) : nullptr;
					Test->TestTrue(TEXT("All local views share training toggle"),Other && Other->IsShoulderViewActive());
					if (Other)
					{
						FVector2D Position;
						FVector View; FRotator Rotation;
						OtherPC->GetPlayerViewPoint(View,Rotation);
						Test->TestTrue(TEXT("Each shoulder reticle projects into its own subviewport"),
							UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(OtherPC,
								UChaosImpactShoulderCameraComponent::MakeReticleAnchor(View,Rotation,Other->GetActorLocation().Z + 54),Position,true));
						const FVector2D Size = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(OtherPC).GetLocalSize();
						Test->TestTrue(TEXT("Direction marker remains horizontally centered in each view"),FMath::Abs(Position.X - Size.X * .5) < 1);
						Test->TestTrue(TEXT("Reticle coordinates stay inside the owning player view after resize/split"),
							Size.X > 1 && Size.Y > 1 && FBox2D(FVector2D::ZeroVector,Size).IsInside(Position));
					}
				}
				Shot(TEXT("04-Shoulder.png")); return Next(.2);
			case 23:
				PC->TogglePauseMenu(); return Next(.2);
			case 24:
				Test->TestTrue(TEXT("Pause still pauses world"),World->IsPaused());
				Test->TestEqual(TEXT("Menus restore mouse access"),PC->bShowMouseCursor,!bPad);
				PC->ResumeGameplay(); return Next(.3);
			case 25:
				Test->TestFalse(TEXT("Closing pause recaptures mouse"),PC->bShowMouseCursor);
				PC->ToggleTrainingShoulderCamera(); return Next(1);
			case 26:
				Test->TestFalse(TEXT("Can revert to top-down immediately"),Player->IsShoulderViewActive());
				Test->TestTrue(TEXT("Original top-down rotation restored"),Player->GetCameraBoom()->GetComponentRotation().Equals(OriginalRotation,1));
				Test->TestTrue(TEXT("Original top-down distance restored"),FMath::Abs(Player->GetCameraBoom()->TargetArmLength - OriginalArm) < 3);
				Cleanup(PC); return true;
			}
			return false;
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (PC)
			{
				if (PC->IsTrainingOverlayOpen()) { PC->CloseTrainingOverlay(); }
				if (!PC->IsGameplayActive()) { PC->ResumeGameplay(); }
				if (PC->IsTrainingShoulderCameraEnabled()) { PC->ToggleTrainingShoulderCamera(); }
				if (auto* Player = Cast<AChaosImpactCharacter>(PC->GetPawn())) { Player->RequestCancelThrow(); Player->StopDrivenBall(); Player->ResetEasyControls(); }
				if (PC->PlayerInput) { PC->PlayerInput->FlushPressedKeys(); }
				for (int I = 0; I < OriginalNames.Num(); ++I) { UChaosImpactLoadoutSubsystem::Get(PC)->SetNickname(I,OriginalNames[I]); }
			}
			if (Profile > 0) { ChaosImpactSettings::RemoveProfile(Profile); Profile = -1; }
			if (bAllocatedInputDevice)
			{
				IPlatformInputDeviceMapper::Get().Internal_SetInputDeviceConnectionState(InputDevice,EInputDeviceConnectionState::Disconnected);
				bAllocatedInputDevice = false;
			}
		}
		FAutomationTestBase* Test;
		int32 Stage = 0, Profile = -1;
		double Started = FPlatformTime::Seconds(), NextAt = 0;
		bool bPad = false, bMove = false, bLook = false, bDriveMove = false, bChargeShotTaken = false, bAllocatedInputDevice = false;
		FInputDeviceId InputDevice = INPUTDEVICEID_NONE;
		EChaosImpactDevice Device = EChaosImpactDevice::KeyboardMouse;
		TArray<FString> OriginalNames;
		FVector Origin, Start, MoveForward, DriveDirection;
		FRotator OriginalRotation;
		float OriginalArm = 0, LookYaw = 0, StaminaBefore = 0, TimeBeforeMenu = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShoulderGameplayTest,"ChaosImpact.Camera.Shoulder.Gameplay",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FShoulderGameplayTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FShoulderGameplayCommand(this));
	return true;
}
#endif
