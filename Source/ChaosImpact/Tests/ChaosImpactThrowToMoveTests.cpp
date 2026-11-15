#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetLayoutLibrary.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"

namespace
{
	/**
	 * 移動方向に投げる (keyboard and mouse, the normal style; training arena with -CITraining): off, the aim follows the mouse;
	 * on, with the mouse still pointing ahead, D (right) turns the aim to the right, it stays that way when the key is let go,
	 * and a throw goes that way. The settings are put back afterwards.
	 */
	class FThrowToMoveCommand : public IAutomationLatentCommand
	{
	public:
		explicit FThrowToMoveCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual ~FThrowToMoveCommand() override
		{
			if (Profile != INDEX_NONE)
			{
				ChaosImpactSettings::SetThrowToMove(Profile, EChaosImpactDevice::KeyboardMouse, bOriginalOn);
				ChaosImpactSettings::SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, OriginalScheme);
			}
		}

		virtual bool Update() override
		{
			using namespace ChaosImpactSettings;
			const double RealNow = FPlatformTime::Seconds();
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = PC ? PC : Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (RealNow - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Stalled at stage %d."), Stage));
				return true;
			}
			if (!Player || !PC->PlayerCameraManager)
			{
				return false;
			}
			UWorld* World = Player->GetWorld();
			const double Now = World->GetTimeSeconds();
			const FRotator Yaw(0.0f, PC->PlayerCameraManager->GetCameraRotation().Yaw, 0.0f);
			const FVector Ahead = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
			const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
			// The mouse, every frame, ahead of the player.
			if (Stage > 0)
			{
				FVector2D Pixels;
				if (PC->ProjectWorldLocationToScreen(Player->GetActorLocation() + Ahead * 450.0f, Pixels))
				{
					PC->SetMouseLocation(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y));
					FSlateApplication::Get().SetCursorPos(UWidgetLayoutLibrary::GetViewportWidgetGeometry(PC).LocalToAbsolute(
						Pixels / UWidgetLayoutLibrary::GetViewportScale(PC)));
				}
			}
			if (Now < NextAt)
			{
				return false;
			}
			const auto Key = [PC](const FKey& Which, const EInputEvent Event)
			{
				PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Which, Event, FPlatformTime::Cycles64()));
			};
			const auto Next = [this, Now](const double Delay) { ++Stage; NextAt = Now + Delay; return false; };
			const auto Dot = [Player](const FVector& Way) { return FVector::DotProduct(Player->GetAimDirection().GetSafeNormal2D(), Way); };
			switch (Stage)
			{
			case 0:
				if (!PC->IsGameplayActive() || PC->IsUsingGamepad() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It) { It->SetActorTickEnabled(false); }
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { It->Destroy(); }
				Profile = GetPlayerProfile(PC);
				OriginalScheme = GetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse);
				bOriginalOn = GetThrowToMove(Profile, EChaosImpactDevice::KeyboardMouse);
				SetControlScheme(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactControlScheme::Normal);
				SetThrowToMove(Profile, EChaosImpactDevice::KeyboardMouse, false);
				return Next(0.4);
			case 1:
				UE_LOG(LogTemp, Display, TEXT("THROWTOMOVE off: aim . ahead %.3f"), Dot(Ahead));
				Test->TestTrue(TEXT("Off: the aim follows the mouse"), Dot(Ahead) > 0.95f);
				SetThrowToMove(Profile, EChaosImpactDevice::KeyboardMouse, true);
				Test->TestTrue(TEXT("On for this player"), IsThrowToMoveOn(PC));
				Key(EKeys::D, IE_Pressed);
				return Next(0.5);
			case 2:
				UE_LOG(LogTemp, Display, TEXT("THROWTOMOVE on, moving right: aim . right %.3f"), Dot(Right));
				Test->TestTrue(TEXT("On: moving right aims right (the mouse points ahead)"), Dot(Right) > 0.95f);
				Key(EKeys::D, IE_Released);
				return Next(0.4);
			case 3:
			{
				UE_LOG(LogTemp, Display, TEXT("THROWTOMOVE on, stopped: aim . right %.3f"), Dot(Right));
				Test->TestTrue(TEXT("Stopped: the aim stays the way last moved"), Dot(Right) > 0.95f);
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
					FTransform(Player->GetActorLocation() + FVector::UpVector * 500), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ball->FinishSpawning(FTransform(Player->GetActorLocation() + FVector::UpVector * 500));
				Ball->MakePickup();
				Test->TestTrue(TEXT("A ball in hand"), Player->TryPickupBall(Ball));
				Ball->Destroy();
				Key(EKeys::LeftMouseButton, IE_Pressed);
				return Next(0.25);
			}
			case 4:
				Key(EKeys::LeftMouseButton, IE_Released);
				return Next(0.3);
			default:
			{
				const AChaosImpactBall* Thrown = nullptr;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetVelocity().Size2D() > 300.0f)
					{
						Thrown = *It;
					}
				}
				const float Along = Thrown ? FVector::DotProduct(Thrown->GetVelocity().GetSafeNormal2D(), Right) : -2.0f;
				UE_LOG(LogTemp, Display, TEXT("THROWTOMOVE throw: velocity . right %.3f"), Along);
				Test->TestTrue(TEXT("The throw goes the way last moved, not to the mouse"), Along > 0.9f);
				return true;
			}
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Profile = INDEX_NONE;
		EChaosImpactControlScheme OriginalScheme = EChaosImpactControlScheme::Normal;
		bool bOriginalOn = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactThrowToMoveTest, "ChaosImpact.Training.ThrowToMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactThrowToMoveTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FThrowToMoveCommand(this));
	return true;
}

#endif
