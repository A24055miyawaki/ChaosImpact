#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactEasyControls.h"
#include "ChaosImpactSettings.h"
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactClickNavigation.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactBallSpawner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/BoxComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FEasyGameplayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FEasyGameplayCommand(FAutomationTestBase* InTest, bool bOnlyHold = false) : Test(InTest)
		{ if (bOnlyHold) { Stage = 20; } }
		virtual bool Update() override
		{
			using namespace ChaosImpactSettings;
			const double RealNow = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); }
			}
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (RealNow - StartedAt > 120)
			{
				Test->AddError(FString::Printf(TEXT("Easy controls stalled at stage %d"), Stage)); Cleanup(PC); return true;
			}
			if (!Player || !PC) { return false; }
			UWorld* World = Player->GetWorld();
			const double Now = World->GetTimeSeconds();
			if (Stage == 23) { bSawHeldAutoJump |= Player->GetCharacterMovement()->IsFalling(); }
			if (Now < NextAt) { return false; }
			const auto Key = [PC](const FKey& Which, const EInputEvent Event)
			{
				PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Which, Event, FPlatformTime::Cycles64()));
			};
			const auto Give = [World, Player](EChaosImpactBallType Type)
			{
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					FTransform(Player->GetActorLocation() + FVector::UpVector * 500), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->SetBallType(Type); Ball->FinishSpawning(FTransform(Player->GetActorLocation() + FVector::UpVector * 500));
				Ball->MakePickup(); const bool bTaken = Player->TryPickupBall(Ball);
				UE_LOG(LogTemp,Display,TEXT("EASYGIVE type %d taken %d count %d"),static_cast<int32>(Type),bTaken,Player->GetCarriedBallCount());
				Ball->Destroy(); return bTaken;
			};
			const auto Next = [this, Now](double Delay) { ++Stage; NextAt = Now + Delay; return false; };
			const auto PointMouse = [PC](const FVector& Point)
			{
				FVector2D Pixels;
				if (!PC->ProjectWorldLocationToScreen(Point, Pixels)) { return false; }
				const FGeometry View = UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC);
				if (!FBox2D(FVector2D::ZeroVector,View.GetLocalSize() * UWidgetLayoutLibrary::GetViewportScale(PC)).IsInside(Pixels)) { return false; }
				PC->SetMouseLocation(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y));
				FSlateApplication::Get().SetCursorPos(UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC).LocalToAbsolute(
					Pixels / UWidgetLayoutLibrary::GetViewportScale(PC)));
				return true;
			};
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive() || PC->IsUsingGamepad() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				Player->RequestCancelThrow();
				for (int32 I = 0; I < 2 && Player->GetCarriedBallCount() > 0; ++I) { Player->RequestDropBall(); }
				Profile = GetPlayerProfile(PC); Original = GetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse);
				SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::Simple);
				Give(EChaosImpactBallType::Normal); Give(EChaosImpactBallType::Fire);
				return Next(0.25);
			case 1:
				Test->TestEqual(TEXT("Two balls ready"), Player->GetCarriedBallCount(), 2);
				Key(EKeys::E, IE_Pressed); return Next(0.12);
			case 2:
				Key(EKeys::E, IE_Released); return Next(0.15);
			case 3:
				Test->TestTrue(TEXT("Tap E actually swaps the two held balls"), Player->GetCarriedBallType(0) == EChaosImpactBallType::Fire);
				Key(EKeys::E, IE_Pressed); return Next(0.85);
			case 4:
				Test->TestEqual(TEXT("Hold E actually drops one ball"), Player->GetCarriedBallCount(), 1);
				Key(EKeys::E, IE_Released); PointMouse(Player->GetActorLocation() + FVector(400, 0, 0));
				Key(EKeys::LeftMouseButton, IE_Pressed); return Next(0.2);
			case 5:
				Test->TestTrue(TEXT("Simple LMB charges"), Player->GetThrowChargeAlpha() > 0);
				Key(EKeys::LeftMouseButton, IE_Released); return Next(0.4);
			case 6:
				Test->TestEqual(TEXT("Release throws, not a premature catch"), Player->GetCarriedBallCount(), 0);
				SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::MouseOnly);
				return Next(0.2);
			case 7:
				Start = Player->GetActorLocation(); Key(EKeys::W, IE_Pressed); return Next(0.3);
			case 8:
			{
				Key(EKeys::W, IE_Released);
				Test->TestTrue(TEXT("Mouse only ignores keyboard movement"), FVector::Dist2D(Start, Player->GetActorLocation()) < 20);
				TArray<FVector> Route;
				bool bFound = false;
				for (int32 Direction = 0; Direction < 8; ++Direction)
				{
					const float Angle = Direction * UE_PI / 4;
					Goal = Start + FVector(FMath::Cos(Angle) * 350, FMath::Sin(Angle) * 350, 0);
					if (ChaosImpactClickNavigation::FindPath(Player, Goal, Route) && PointMouse(Goal)) { bFound = true; break; }
				}
				Test->TestTrue(TEXT("A supported destination can be found"), bFound);
				return Next(0.15); // Let the viewport receive its new pointer position before clicking.
			}
			case 9:
				Player->HandleEasyPointerKey(EKeys::RightMouseButton, IE_Pressed);
				Player->HandleEasyPointerKey(EKeys::RightMouseButton, IE_Released);
				Test->TestTrue(TEXT("First click starts movement without a double-click wait"), Player->HasClickMoveGoal());
				UE_LOG(LogTemp, Display, TEXT("EASYMOVE start %s requested %s actual %s"), *Start.ToString(), *Goal.ToString(), *Player->GetClickMoveGoal().ToString());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EasyControlsQA/MouseOnly.png")), true, false);
				return Next(0.6);
			case 10:
				UE_LOG(LogTemp, Display, TEXT("EASYMOVE after %s active %d rejected %d velocity %s"), *Player->GetActorLocation().ToString(), Player->HasClickMoveGoal(), Player->WasClickMoveRejected(), *Player->GetVelocity().ToString());
				Test->TestTrue(TEXT("Click destination moves the pawn"), FVector::Dist2D(Start, Player->GetActorLocation()) > 50);
				Player->ResetEasyControls(); Give(EChaosImpactBallType::Drive); return Next(0.15);
			case 11:
				PointMouse(Player->GetActorLocation() + FVector(400, 0, 0));
				Key(EKeys::LeftMouseButton, IE_Pressed); return Next(0.1);
			case 12:
				Key(EKeys::LeftMouseButton, IE_Released); return Next(0.3);
			case 13:
				Test->TestTrue(TEXT("Mouse only can launch a drive ball"), Player->IsDriving());
				if (AChaosImpactBall* Ball = Player->GetDrivenBall()) { FirstDrive = Ball->GetVelocity().GetSafeNormal2D(); PointMouse(Ball->GetActorLocation() + FVector(0, 650, 0)); }
				return Next(0.25);
			case 14:
				if (AChaosImpactBall* Ball = Player->GetDrivenBall())
				{
					Test->TestTrue(TEXT("Drive remains freely steerable with the mouse"), FVector::DotProduct(FirstDrive, Ball->GetVelocity().GetSafeNormal2D()) < 0.99);
				}
				else { Test->AddError(TEXT("Drive vanished before steering could be checked")); }
				Player->StopDrivenBall(); Give(EChaosImpactBallType::Normal);
				{
					const FGeometry G = UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC);
					const FVector2D Size = G.GetLocalSize();
					const float S = FMath::Clamp(static_cast<float>(FMath::Min(Size.X / 1600, Size.Y / 900)), 0.5f, 1.4f);
					FSlateApplication::Get().SetCursorPos(G.LocalToAbsolute(Size + FVector2D(-174,-86) * S));
				}
				return Next(0.15);
			case 15:
				Test->TestTrue(TEXT("Empty ball slot still captures a click"), Player->HandleEasyPointerKey(EKeys::LeftMouseButton, IE_Pressed));
				Player->HandleEasyPointerKey(EKeys::LeftMouseButton, IE_Released);
				Test->TestTrue(TEXT("Clicking an empty slot does not throw the held ball"), Player->GetCarriedBallCount() > 0);
				{
					const FGeometry G = UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC);
					const FVector2D Size = G.GetLocalSize();
					const float S = FMath::Clamp(static_cast<float>(FMath::Min(Size.X / 1600, Size.Y / 900)), 0.5f, 1.4f);
					FSlateApplication::Get().SetCursorPos(G.LocalToAbsolute(FVector2D(Size.X - 80 * S, 40 * S)));
				}
				return Next(0.15);
			case 16:
				Test->TestTrue(TEXT("Mouse pause button consumes its click"), Player->HandleEasyPointerKey(EKeys::LeftMouseButton, IE_Pressed));
				Test->TestTrue(TEXT("Mouse only can actually open pause"), PC->GetCurrentScreen() == EChaosImpactScreen::Pause);
				Test->TestFalse(TEXT("Pause clears autonomous click movement"), Player->HasClickMoveGoal());
				if (PC->GetCurrentScreen() == EChaosImpactScreen::Pause) { PC->TogglePauseMenu(); }
				SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::Normal);
				NormalThrowKey = GetActiveKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0);
				return Next(0.2);
			case 17:
				if (!NormalThrowKey.IsValid()) { Cleanup(PC); return true; }
				Key(NormalThrowKey, IE_Pressed); return Next(0.15);
			case 18:
				Test->TestTrue(TEXT("Returning from mouse-only pause to normal controls can still charge"), Player->GetThrowChargeAlpha() > 0);
				Key(NormalThrowKey, IE_Released); return Next(0.3);
			case 19:
				Test->TestEqual(TEXT("Normal throw works after changing styles"), Player->GetCarriedBallCount(), 0);
				return Next(0.2);
			case 20:
			{
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return false; }
				if (Profile < 0) { Profile = GetPlayerProfile(PC); Original = GetControlScheme(Profile,EChaosImpactDevice::KeyboardMouse); }
				SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::MouseHold);
				ReturnLocation = Player->GetActorLocation(); bHasReturnLocation = true;
				const auto Box = [World](const FVector& At, const FVector& Extent)
				{
					AActor* Actor = World->SpawnActor<AActor>();
					UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
					Actor->SetRootComponent(Shape); Shape->InitBoxExtent(Extent);
					Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
					Shape->SetCollisionObjectType(ECC_WorldStatic); Shape->SetCollisionResponseToAllChannels(ECR_Block);
					Shape->RegisterComponent(); Actor->SetActorLocation(At); return Actor;
				};
				HoldFloor = Box(HoldOrigin - FVector::UpVector * 50, FVector(2500,2500,50));
				HoldLedge = Box(HoldOrigin + FVector(300,0,60), FVector(100,300,60));
				Player->GetCharacterMovement()->StopMovementImmediately();
				Player->SetActorLocation(HoldOrigin + FVector::UpVector * 100, false, nullptr, ETeleportType::TeleportPhysics);
				return Next(0.6);
			}
			case 21:
				Start = Player->GetActorLocation(); Key(EKeys::W, IE_Pressed); return Next(0.2);
			case 22:
				Key(EKeys::W, IE_Released);
				Test->TestTrue(TEXT("Mouse hold ignores WASD too"), FVector::Dist2D(Start, Player->GetActorLocation()) < 20);
				Test->TestTrue(TEXT("Can place the held movement pointer ahead"), PointMouse(HoldOrigin + FVector(400,0,0)));
				Key(EKeys::RightMouseButton, IE_Pressed); return Next(0.75);
			case 23:
				UE_LOG(LogTemp,Display,TEXT("EASYHOLD after %s start %s jump %d"),*Player->GetActorLocation().ToString(),*Start.ToString(),bSawHeldAutoJump);
				Test->TestTrue(TEXT("Holding RMB continuously moves toward the pointer"), Player->GetActorLocation().X > Start.X + 100);
				Test->TestTrue(TEXT("Held mouse movement automatically jumps the 120 cm ledge"), bSawHeldAutoJump);
				Test->TestFalse(TEXT("Hold mode never creates an autonomous click destination"), Player->HasClickMoveGoal());
				Key(EKeys::RightMouseButton, IE_Released); return Next(0.5);
			case 24:
				Test->TestTrue(TEXT("Releasing RMB stops held movement"), Player->GetVelocity().Size2D() < 10);
				Start = Player->GetActorLocation();
				Test->TestTrue(TEXT("Second direction is inside the viewport"),PointMouse(FVector(Start.X,Start.Y + 400,0)));
				Key(EKeys::RightMouseButton, IE_Pressed); return Next(0.35);
			case 25:
				Test->TestTrue(TEXT("Holding can move in a second direction"), Player->GetActorLocation().Y > Start.Y + 50);
				Start = Player->GetActorLocation();
				Test->TestTrue(TEXT("Turning direction is inside the viewport"),PointMouse(FVector(Start.X - 175,Start.Y,0)));
				return Next(0.4);
			case 26:
				Test->TestTrue(TEXT("Moving the cursor turns movement without releasing RMB"), Player->GetActorLocation().X < Start.X - 50);
				PC->TogglePauseMenu();
				Test->TestTrue(TEXT("Hold mode can pause while RMB is down"), PC->GetCurrentScreen() == EChaosImpactScreen::Pause);
				PC->TogglePauseMenu(); return Next(0.5);
			case 27:
				Test->TestTrue(TEXT("Closing pause does not resume the old mouse hold"), Player->GetVelocity().Size2D() < 10);
				Key(EKeys::RightMouseButton, IE_Released);
				Test->TestTrue(TEXT("Release test starts over gameplay rather than UI"),PointMouse(Player->GetActorLocation()));
				Key(EKeys::LeftMouseButton,IE_Pressed); return Next(0.12);
			case 28:
			{
				Test->TestTrue(TEXT("Gameplay LMB press is recorded before release over UI"),PC->IsInputKeyDown(EKeys::LeftMouseButton));
				const FGeometry G = UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC);
				const FVector2D Size = G.GetLocalSize();
				const float S = FMath::Clamp(static_cast<float>(FMath::Min(Size.X / 1600,Size.Y / 900)),0.5f,1.4f);
				FSlateApplication::Get().SetCursorPos(G.LocalToAbsolute(Size + FVector2D(-174,-86) * S));
				Key(EKeys::LeftMouseButton,IE_Released); return Next(0.15);
			}
			case 29:
				Test->TestFalse(TEXT("Releasing a gameplay button over inventory never leaves it stuck down"),PC->IsInputKeyDown(EKeys::LeftMouseButton));
				Cleanup(PC); return true;
			default: Cleanup(PC); return true;
			}
		}
	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (PC)
			{
				if (PC->GetCurrentScreen() == EChaosImpactScreen::Pause) { PC->TogglePauseMenu(); }
				PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
					EKeys::RightMouseButton, IE_Released, FPlatformTime::Cycles64()));
			}
			if (Profile >= 0) { ChaosImpactSettings::SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, Original); Profile = -1; }
			if (AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr)
			{
				Player->RequestCancelThrow(); Player->ResetEasyControls();
				// Other gameplay tests expect empty hands at entry; do not leak the UI-test pickup to them.
				for (int32 I = 0; I < 2 && Player->GetCarriedBallCount() > 0; ++I) { Player->RequestDropBall(); }
				if (bHasReturnLocation)
				{
					Player->GetCharacterMovement()->StopMovementImmediately();
					Player->SetActorLocation(ReturnLocation, false, nullptr, ETeleportType::TeleportPhysics);
				}
			}
			if (HoldFloor.IsValid()) { HoldFloor->Destroy(); }
			if (HoldLedge.IsValid()) { HoldLedge->Destroy(); }
		}
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds(), NextAt = 0;
		int32 Stage = 0, Profile = -1;
		EChaosImpactControlScheme Original = EChaosImpactControlScheme::Normal;
		FKey NormalThrowKey;
		FVector Start = FVector::ZeroVector, Goal = FVector::ZeroVector, FirstDrive = FVector::ZeroVector;
		const FVector HoldOrigin = FVector(200000,200000,0);
		FVector ReturnLocation = FVector::ZeroVector;
		TWeakObjectPtr<AActor> HoldFloor, HoldLedge;
		bool bHasReturnLocation = false, bSawHeldAutoJump = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactEasyGameplayTest, "ChaosImpact.Controls.EasyGameplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactEasyGameplayTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEasyGameplayCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactMouseHoldMovementTest, "ChaosImpact.Controls.HoldMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactMouseHoldMovementTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEasyGameplayCommand(this,true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactEasyGesturesTest, "ChaosImpact.Controls.EasyGestures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactEasyGesturesTest::RunTest(const FString& Parameters)
{
	using namespace ChaosImpactEasyControls;
	FManageGesture Manage;
	TestTrue(TEXT("Press does not prematurely swap"), Manage.Update(true, 1) == EManageResult::None);
	TestTrue(TEXT("Tap swaps on release"), Manage.Update(false, 1.1) == EManageResult::Swap);
	Manage.Update(true, 2);
	TestTrue(TEXT("Hold drops once"), Manage.Update(true, 2.71) == EManageResult::Drop);
	TestTrue(TEXT("Long hold does not repeatedly drop"), Manage.Update(true, 8) == EManageResult::None);
	TestTrue(TEXT("Release after drop never swaps"), Manage.Update(false, 8.1) == EManageResult::None);
	Manage.Update(true, 10);
	TestTrue(TEXT("Low frame rate release still drops instead of swap"), Manage.Update(false, 11) == EManageResult::Drop);
	Manage.Update(true, 12); Manage.Reset();
	TestTrue(TEXT("Menu cancellation does not swap"), Manage.Update(false, 13) == EManageResult::None);
	FMoveClicks Clicks;
	TestFalse(TEXT("First click moves immediately but never dashes"), Clicks.Press(FVector2D(100,100), 1));
	TestFalse(TEXT("Fast clicks elsewhere never dash"), Clicks.Press(FVector2D(200,100), 1.1));
	TestTrue(TEXT("Close second click dashes"), Clicks.Press(FVector2D(210,105), 1.2));
	TestFalse(TEXT("A third click is not another dash"), Clicks.Press(FVector2D(210,105), 1.21));
	Clicks.Reset();
	TestFalse(TEXT("Menu clears double-click history"), Clicks.Press(FVector2D(210,105), 1.22));
	TestFalse(TEXT("Slow repeat is ordinary movement"), Clicks.Press(FVector2D(210,105), 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactEasyPresetsTest, "ChaosImpact.Controls.EasyPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactEasyPresetsTest::RunTest(const FString& Parameters)
{
	using namespace ChaosImpactSettings;
	const int32 Profile = AddProfile(TEXT("QA") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
	if (Profile <= 0) { AddError(TEXT("No room for a temporary test profile")); return false; }
	const auto KBM = EChaosImpactDevice::KeyboardMouse;
	const auto Xbox = EChaosImpactDevice::Xbox;
	SetKey(Profile, KBM, EChaosImpactAction::Dash, 0, EKeys::H);
	TestTrue(TEXT("Unapplied simple preview uses its own keys"),GetSchemeKey(Profile,KBM,EChaosImpactControlScheme::Simple,EChaosImpactAction::Dash,0) == EKeys::RightMouseButton);
	TestTrue(TEXT("Unapplied preview leaves the active normal binding untouched"),GetActiveKey(Profile,KBM,EChaosImpactAction::Dash,0) == EKeys::H);
	SetControlScheme(Profile, KBM, EChaosImpactControlScheme::Simple);
	TestTrue(TEXT("Simple throws with LMB"), GetActiveKey(Profile, KBM, EChaosImpactAction::Throw, 0) == EKeys::LeftMouseButton);
	TestTrue(TEXT("Simple dashes with RMB"), GetActiveKey(Profile, KBM, EChaosImpactAction::Dash, 0) == EKeys::RightMouseButton);
	TestFalse(TEXT("RMB no longer cancels a throw"), GetActiveKey(Profile, KBM, EChaosImpactAction::CancelThrow, 0).IsValid());
	TestFalse(TEXT("Drop has no extra button"), GetActiveKey(Profile, KBM, EChaosImpactAction::DropBall, 0).IsValid());
	SetControlScheme(Profile, Xbox, EChaosImpactControlScheme::Simple);
	TestTrue(TEXT("Simple pad throws with bottom face"), GetActiveKey(Profile, Xbox, EChaosImpactAction::Throw, 0) == EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("Simple pad manages balls with left face"), GetActiveKey(Profile, Xbox, EChaosImpactAction::SwapBall, 0) == EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("No shoulders to dash"), GetActiveKey(Profile, Xbox, EChaosImpactAction::Dash, 0) == EKeys::Gamepad_FaceButton_Right);
	SetControlScheme(Profile, KBM, EChaosImpactControlScheme::MouseOnly);
	TestFalse(TEXT("Mouse only ignores WASD"), GetActiveKey(Profile, KBM, EChaosImpactAction::MoveUp, 0).IsValid());
	TestTrue(TEXT("Middle button is always available for dash"), GetActiveKey(Profile, KBM, EChaosImpactAction::Dash, 0) == EKeys::MiddleMouseButton);
	TestTrue(TEXT("Other devices keep their own preset"), GetControlScheme(Profile, Xbox) == EChaosImpactControlScheme::Simple);
	SetControlScheme(Profile, KBM, EChaosImpactControlScheme::MouseHold);
	TestTrue(TEXT("A separate hold mouse scheme can be selected"), GetControlScheme(Profile, KBM) == EChaosImpactControlScheme::MouseHold);
	TestTrue(TEXT("Hold mode belongs to mouse-only controls"), IsMouseOnlyScheme(GetControlScheme(Profile, KBM)));
	TestFalse(TEXT("Hold mode also ignores WASD"), GetActiveKey(Profile, KBM, EChaosImpactAction::MoveUp, 0).IsValid());
	TestFalse(TEXT("RMB movement never binds to dash in hold mode"), GetActiveKey(Profile, KBM, EChaosImpactAction::Dash, 0) == EKeys::RightMouseButton);
	TestTrue(TEXT("Hold mode throws with LMB"), GetActiveKey(Profile, KBM, EChaosImpactAction::Throw, 0) == EKeys::LeftMouseButton);
	TestTrue(TEXT("Hold mode still dashes with the middle button"), GetActiveKey(Profile, KBM, EChaosImpactAction::Dash, 0) == EKeys::MiddleMouseButton);
	SetControlScheme(Profile, Xbox, EChaosImpactControlScheme::MouseHold);
	TestTrue(TEXT("Controllers reject the hold mouse preset"), GetControlScheme(Profile, Xbox) == EChaosImpactControlScheme::Simple);
	SetControlScheme(Profile, Xbox, EChaosImpactControlScheme::MouseOnly);
	TestTrue(TEXT("Controllers cannot select an unusable mouse preset"), GetControlScheme(Profile, Xbox) == EChaosImpactControlScheme::Simple);
	SetControlScheme(Profile, KBM, EChaosImpactControlScheme::Normal);
	TestTrue(TEXT("Custom normal bindings are preserved"), GetActiveKey(Profile, KBM, EChaosImpactAction::Dash, 0) == EKeys::H);
	RemoveProfile(Profile);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactClickNavigationTest, "ChaosImpact.Controls.ClickNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactClickNavigationTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
	}
	if (!World) { AddError(TEXT("Run this navigation test in a game world")); return false; }
	const FVector Origin(200000,200000,0);
	const auto Box = [World](const FVector& At, const FVector& Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Shape); Shape->InitBoxExtent(Extent);
		Shape->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Shape->SetCollisionObjectType(ECC_WorldStatic); Shape->SetCollisionResponseToAllChannels(ECR_Block);
		Shape->RegisterComponent(); Actor->SetActorLocation(At); return Actor;
	};
	AActor* Floor = Box(Origin - FVector::UpVector * 50, FVector(1200,1200,50));
	AActor* Wall = Box(Origin + FVector::UpVector * 200, FVector(50,300,200));
	ACharacter* Probe = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), Origin + FVector(-500,0,100), FRotator::ZeroRotator);
	if (!Probe) { Wall->Destroy(); Floor->Destroy(); AddError(TEXT("Cannot create navigation probe")); return false; }
	TArray<FVector> Route;
	TestTrue(TEXT("Can route around a tall wall without NavMesh"), ChaosImpactClickNavigation::FindPath(Probe, Origin + FVector(500,0,0), Route));
	TestTrue(TEXT("Detour has multiple waypoints"), Route.Num() > 2);
	bool bDetour = false;
	for (const FVector& Point : Route) { bDetour |= FMath::Abs(Point.Y - Origin.Y) > 300; }
	TestTrue(TEXT("Route goes around the wall, not through it"), bDetour);
	TestFalse(TEXT("No route to unsupported space"), ChaosImpactClickNavigation::FindPath(Probe, Origin + FVector(5000,0,0), Route));
	Probe->Destroy(); Wall->Destroy(); Floor->Destroy();
	return true;
}
#endif
