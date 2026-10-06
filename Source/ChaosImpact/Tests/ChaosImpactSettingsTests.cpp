#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	using FScreen = UChaosImpactSettingsScreen;

	AChaosImpactPlayerController* FindSettingsTestController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	void PressKey(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	void TypeLetter(const TCHAR Letter)
	{
		FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Letter, FModifierKeysState(), 0, false));
	}

	void SettingsShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SettingsQA"), Name), true, false);
	}

	/** Takes off any nickname a test made earlier (a test that failed half way). */
	void RemoveTestProfiles()
	{
		for (const TCHAR* Name : {TEXT("QAが"), TEXT("QAx"), TEXT("QAz")})
		{
			if (const int32 Index = ChaosImpactSettings::FindProfile(Name); Index > 0)
			{
				ChaosImpactSettings::RemoveProfile(Index);
			}
		}
	}

	/**
	 * The settings screen from the mode select menu (title world): the 設定 entry opens it; Q/E change tabs; a picture
	 * group changes and goes back; F11 changes the window mode and the screen shows it; a nickname is written with the
	 * letter grid and the keyboard; a control takes a new key (which comes off the control that had it), a mouse button,
	 * and Escape leaves a key as it was; a Switch controller's layout names its buttons; Escape goes back. Then
	 * character select: N opens 1P's nickname list, a nickname is picked and a new one written. Screenshots go to
	 * Saved/SettingsQA. Every setting the test touches is put back.
	 */
	class FSettingsMenuCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSettingsMenuCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSettingsTestController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			FScreen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			if (!PC || !Menu || !Settings)
			{
				return false;
			}
			const auto Next = [this, Now](const double Seconds)
			{
				++Stage;
				NextAt = Now + Seconds;
				return false;
			};
			using namespace ChaosImpactSettings;
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 3.0)
				{
					return false;
				}
				RemoveTestProfiles();
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				Test->TestEqual(TEXT("The mode select menu has 設定"), Menu->GetEntryCount(), 5);
				// Solo, down to タイトルへ, right to 設定.
				PressKey(EKeys::Down);
				PressKey(EKeys::Right);
				Test->TestEqual(TEXT("設定 sits between タイトルへ and トレーニング"), Menu->GetSelectedIndex(), 4);
				PressKey(EKeys::Enter);
				Test->TestTrue(TEXT("設定 opens the settings"), PC->GetCurrentScreen() == EChaosImpactScreen::Settings && Settings->IsOpen());
				Test->TestTrue(TEXT("It opens on 画面"), Settings->GetTab() == FScreen::ETab::Display);
				OriginalMode = GetWindowMode();
				OriginalResolution = GetResolution();
				OriginalShadows = GetQualityGroup(2);
				return Next(0.8);
			case 1:
				SettingsShot(TEXT("01-Display.png"));
				return Next(0.2);
			case 2:
				// F11: the window mode changes, and the screen shows it.
				PressKey(EKeys::F11);
				Test->TestNotEqual(TEXT("F11 changes the window mode"), GetWindowMode(), OriginalMode);
				return Next(0.2);
			case 3:
			{
				static const TCHAR* const Modes[] = {TEXT("フルスクリーン"), TEXT("ボーダーレス"), TEXT("ウィンドウ")};
				const int32 Row = Settings->FindRow(FScreen::RowWindowMode);
				Test->TestTrue(TEXT("The settings show F11's window mode"),
					Settings->GetRows().IsValidIndex(Row) && Settings->GetRows()[Row].Value == Modes[GetWindowMode()]);
				PressKey(EKeys::F11);
				Test->TestEqual(TEXT("F11 again goes back"), GetWindowMode(), OriginalMode);
				SetResolution(OriginalResolution);
				{
					// A window's sizes fit on the monitor; fullscreen offers up to 4K, drawn finer than the monitor when
					// it is bigger, and the screen itself stays at the monitor's pixels (nothing stretched).
					const FIntPoint Monitor = GetMonitorResolution();
					SetWindowMode(2);
					bool bFits = true;
					for (const FIntPoint& Choice : GetResolutionChoices())
					{
						bFits &= Choice == GetResolution() || (Choice.X < Monitor.X && Choice.Y < Monitor.Y);
					}
					Test->TestTrue(TEXT("Window sizes fit on the monitor"), bFits);
					SetWindowMode(1);
					Test->TestTrue(TEXT("Fullscreen offers 4K"), GetResolutionChoices().Contains(FIntPoint(3840, 2160)));
					Test->TestTrue(TEXT("Fullscreen offers the monitor's own"), GetResolutionChoices().Contains(Monitor));
					SetResolution(FIntPoint(3840, 2160));
					const float Expected = FMath::Clamp(100.0f * 2160.0f / Monitor.Y, 25.0f, 200.0f);
					const float Percent = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat();
					UE_LOG(LogTemp, Display, TEXT("SETTINGS monitor %dx%d, 4K draws at %.0f%%"), Monitor.X, Monitor.Y, Percent);
					Test->TestTrue(TEXT("4K draws at 2160 lines"), FMath::IsNearlyEqual(Percent, Expected, 0.5f));
					SetResolution(Monitor);
					Test->TestTrue(TEXT("The monitor's own draws at 100%"),
						FMath::IsNearlyEqual(IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat(), 100.0f, 0.5f));
					SetWindowMode(OriginalMode);
					SetResolution(OriginalResolution);
				}
				// 画質: one group down (or up) and back.
				PressKey(EKeys::E);
				Test->TestTrue(TEXT("E goes to 画質"), Settings->GetTab() == FScreen::ETab::Picture);
				Settings->SelectRow(Settings->FindRow(FScreen::RowQualityGroup + 2));
				PressKey(OriginalShadows > 0 ? EKeys::Left : EKeys::Right);
				Test->TestEqual(TEXT("Left/right change the shadows"), GetQualityGroup(2), OriginalShadows + (OriginalShadows > 0 ? -1 : 1));
				return Next(0.6);
			}
			case 4:
				SettingsShot(TEXT("02-Picture.png"));
				SetQualityGroup(2, OriginalShadows);
				PressKey(EKeys::E);
				Test->TestTrue(TEXT("E goes to 操作"), Settings->GetTab() == FScreen::ETab::Controls);
				// A new nickname: two letters typed, か from the grid made が with ゛.
				Settings->SelectRow(Settings->FindRow(FScreen::RowNewProfile));
				PressKey(EKeys::Enter);
				Test->TestTrue(TEXT("＋ ニックネームを作る opens the letters"), Settings->IsNameEntryOpen());
				TypeLetter(TEXT('Q'));
				TypeLetter(TEXT('A'));
				Settings->GetNameEntry().MoveCursor(1, 0);
				PressKey(EKeys::SpaceBar);
				Settings->GetNameEntry().MoveCursor(-2, 0);
				PressKey(EKeys::SpaceBar);
				Test->TestEqual(TEXT("Typed and picked letters, ゛ voicing か"), Settings->GetNameEntry().GetText(), FString(TEXT("QAが")));
				return Next(0.5);
			case 5:
				SettingsShot(TEXT("03-NameEntry.png"));
				PressKey(EKeys::Enter);
				Profile = FindProfile(TEXT("QAが"));
				Test->TestTrue(TEXT("Enter makes the nickname"), Profile > 0 && !Settings->IsNameEntryOpen());
				Test->TestEqual(TEXT("The controls tab shows the new nickname"), Settings->GetEditedProfile(), Profile);
				Test->TestTrue(TEXT("Keyboard and mouse first (P1's device)"), Settings->GetEditedDevice() == EChaosImpactDevice::KeyboardMouse);
				// 投げる, first key: H.
				Settings->SelectRow(Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::Throw)), 0);
				PressKey(EKeys::Enter);
				Test->TestTrue(TEXT("Enter waits for a key"), Settings->IsWaitingForKey());
				return Next(0.4);
			case 6:
				SettingsShot(TEXT("04-Waiting.png"));
				PressKey(EKeys::H);
				Test->TestFalse(TEXT("A key ends the wait"), Settings->IsWaitingForKey());
				Test->TestEqual(TEXT("投げる is H now"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0), EKeys::H);
				Test->TestEqual(TEXT("Its second key is kept"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 1), EKeys::G);
				// ボールを捨てる takes H: 投げる loses it.
				Settings->SelectRow(Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::DropBall)), 1);
				Settings->Activate();
				PressKey(EKeys::H);
				Test->TestEqual(TEXT("H drops now"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::DropBall, 1), EKeys::H);
				Test->TestFalse(TEXT("…and no longer throws (one key, one thing)"),
					GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0).IsValid());
				// A mouse button, and Escape leaving a key as it is.
				Settings->SelectRow(Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::Throw)), 0);
				Settings->Activate();
				Settings->HandleMouseDown(FVector2D(800.0f, 450.0f), EKeys::MiddleMouseButton);
				Test->TestEqual(TEXT("A mouse button can be set"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0),
					EKeys::MiddleMouseButton);
				Settings->Activate();
				PressKey(EKeys::Escape);
				Test->TestFalse(TEXT("Escape stops waiting"), Settings->IsWaitingForKey());
				Test->TestEqual(TEXT("…and leaves the key"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0),
					EKeys::MiddleMouseButton);
				Test->TestTrue(TEXT("The screen is still open after Escape while waiting"), Settings->IsOpen());
				// Y (Delete) takes a key off.
				Settings->SelectRow(Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::DropBall)), 1);
				PressKey(EKeys::Delete);
				Test->TestFalse(TEXT("Delete takes a key off"), GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::DropBall, 1).IsValid());
				// The Switch layout: its own names, A (the right button) jumps.
				Settings->SelectRow(Settings->FindRow(FScreen::RowDevice));
				PressKey(EKeys::Left);
				Test->TestTrue(TEXT("Left goes round to the Switch controller"), Settings->GetEditedDevice() == EChaosImpactDevice::Switch);
				{
					const int32 JumpRow = Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::Jump));
					Test->TestTrue(TEXT("A Switch controller jumps with A"), Settings->GetRows().IsValidIndex(JumpRow)
						&& Settings->GetRows()[JumpRow].Cells[0] == TEXT("A"));
					Test->TestTrue(TEXT("…which is its right button"),
						GetKey(Profile, EChaosImpactDevice::Switch, EChaosImpactAction::Jump, 0) == EKeys::Gamepad_FaceButton_Right);
				}
				Settings->SelectRow(Settings->FindRow(FScreen::RowBinding + static_cast<int32>(EChaosImpactAction::Throw)), 0);
				return Next(0.5);
			case 7:
				SettingsShot(TEXT("05-Controls.png"));
				// その他 (E wraps 操作 → その他).
				PressKey(EKeys::E);
				Test->TestTrue(TEXT("E goes to その他"), Settings->GetTab() == FScreen::ETab::Other);
				return Next(0.5);
			case 8:
				SettingsShot(TEXT("06-Other.png"));
				PressKey(EKeys::Escape);
				Test->TestTrue(TEXT("Escape goes back to the mode select menu"), PC->GetCurrentScreen() == EChaosImpactScreen::ModeSelect);
				Test->TestFalse(TEXT("…and closes the settings"), Settings->IsOpen());
				// Character select with one keyboard player.
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(1);
				PC->RegisterKeyboardMouseJoin();
				PC->ConfirmControllerAssignments();
				Test->TestTrue(TEXT("Character select is open"), PC->GetCurrentScreen() == EChaosImpactScreen::CharacterSelect);
				return Next(1.2);
			case 9:
			{
				UChaosImpactCharacterSelect* Select = Menu->GetCharacterSelect();
				if (!Select)
				{
					Test->AddError(TEXT("No character select."));
					return true;
				}
				PressKey(EKeys::N);
				Test->TestTrue(TEXT("N opens 1P's nickname list"), Select->IsNamePickerOpen());
				// Down until QAが, then Enter.
				for (int32 Step = 0; Step < Profile; ++Step)
				{
					PressKey(EKeys::Down);
				}
				return Next(0.4);
			}
			case 10:
			{
				UChaosImpactCharacterSelect* Select = Menu->GetCharacterSelect();
				SettingsShot(TEXT("07-NamePicker.png"));
				PressKey(EKeys::Enter);
				Test->TestFalse(TEXT("Picking closes the list"), Select->IsNamePickerOpen());
				Test->TestEqual(TEXT("1P plays as QAが"), Select->GetProfile(0), Profile);
				const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(PC);
				Test->TestTrue(TEXT("…kept for the match"), Loadouts && Loadouts->GetLoadout(0).Nickname == TEXT("QAが"));
				Test->TestEqual(TEXT("…and P1's controls are QAが's"), GetPlayerProfile(PC), Profile);
				// A new one from the list's last line.
				// The list opens on QAが, the last nickname; the line below it is 「＋ 新しく作る」.
				PressKey(EKeys::N);
				PressKey(EKeys::Down);
				PressKey(EKeys::Enter);
				Test->TestTrue(TEXT("「＋ 新しく作る」 opens the letters"), Select->IsNameEntryOpen());
				TypeLetter(TEXT('Q'));
				TypeLetter(TEXT('A'));
				TypeLetter(TEXT('z'));
				return Next(0.4);
			}
			case 11:
			{
				UChaosImpactCharacterSelect* Select = Menu->GetCharacterSelect();
				SettingsShot(TEXT("08-NewNickname.png"));
				PressKey(EKeys::Enter);
				const int32 Made = FindProfile(TEXT("QAz"));
				Test->TestTrue(TEXT("The written nickname is made"), Made > 0);
				Test->TestEqual(TEXT("…and 1P plays as it"), Select->GetProfile(0), Made);
				return Next(0.6);
			}
			case 12:
				SettingsShot(TEXT("09-SelectWithNickname.png"));
				return Next(0.3);
			default:
				// Put back what the test changed.
				if (UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(PC))
				{
					Loadouts->SetNickname(0, FString());
				}
				RemoveTestProfiles();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Profile = 0;
		int32 OriginalMode = 2;
		int32 OriginalShadows = 3;
		FIntPoint OriginalResolution = FIntPoint::ZeroValue;
	};

	/**
	 * Controls in play: 1P plays as a nickname whose throw is H, dash K and move-up I (training arena, one CPU held
	 * still). H charges and throws; the left mouse button (no longer a throw key) does nothing; K dashes; holding I
	 * walks; the name over 1P's head is the nickname.
	 */
	class FRebindCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRebindCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				Cleanup(FindSettingsTestController());
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSettingsTestController();
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			if (!PC || !Player || !World)
			{
				return false;
			}
			const auto Key = [PC](const FKey& Which, const EInputEvent Event)
			{
				// As a real keyboard's press arrives (with the keyboard's device), so the player's key state sees it too.
				PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Which, Event,
					FPlatformTime::Cycles64()));
			};
			const auto Give = [World, Player]()
			{
				const FTransform Where(FRotator::ZeroRotator, Player->GetActorLocation() + FVector(0, 0, 400));
				AChaosImpactBall* Pickup = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), Where,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Pickup->SetBallType(EChaosImpactBallType::Normal);
				Pickup->FinishSpawning(Where);
				Pickup->MakePickup();
				const bool bTaken = Player->TryPickupBall(Pickup);
				Pickup->Destroy();
				return bTaken;
			};
			const auto Next = [this, Now](const double Seconds)
			{
				++Stage;
				NextAt = Now + Seconds;
				return false;
			};
			using namespace ChaosImpactSettings;
			switch (Stage)
			{
			case 0:
			{
				if (!PC->IsGameplayActive() || !Player->GetCharacterMovement()->IsMovingOnGround())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
				}
				// Nothing the CPU let loose before it was stopped may hit 1P during the test.
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactSimaeBird> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				RemoveTestProfiles();
				Profile = AddProfile(TEXT("QAx"));
				Test->TestTrue(TEXT("A nickname is made"), Profile > 0);
				SetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0, EKeys::H);
				SetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Dash, 0, EKeys::K);
				SetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::MoveUp, 0, EKeys::I);
				if (UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(PC))
				{
					Loadouts->SetNickname(0, TEXT("QAx"));
				}
				Test->TestEqual(TEXT("1P plays as it"), GetPlayerProfile(PC), Profile);
				Test->TestEqual(TEXT("1P's name is the nickname"), Player->GetOverheadDisplayName(), FString(TEXT("QAx")));
				Test->TestTrue(TEXT("1P has a ball"), Give());
				Key(EKeys::LeftMouseButton, IE_Pressed);
				return Next(0.3);
			}
			case 1:
				Test->TestFalse(TEXT("The left button no longer throws"), Player->GetThrowChargeAlpha() > 0.0f);
				Key(EKeys::LeftMouseButton, IE_Released);
				Test->TestEqual(TEXT("…and the ball stays"), Player->GetCarriedBallCount(), 1);
				Key(EKeys::H, IE_Pressed);
				return Next(0.3);
			case 2:
				UE_LOG(LogTemp, Display, TEXT("REBIND throw: H down %d, throw bound down %d, alpha %.2f, balls %d, key0 %s"),
					PC->IsInputKeyDown(EKeys::H) ? 1 : 0, IsActionDown(PC, EChaosImpactAction::Throw) ? 1 : 0,
					Player->GetThrowChargeAlpha(), Player->GetCarriedBallCount(),
					*GetKey(Profile, EChaosImpactDevice::KeyboardMouse, EChaosImpactAction::Throw, 0).ToString());
				Test->TestTrue(TEXT("H charges a throw"), Player->GetThrowChargeAlpha() > 0.0f);
				Key(EKeys::H, IE_Released);
				return Next(0.4);
			case 3:
				Test->TestEqual(TEXT("Letting go of H throws"), Player->GetCarriedBallCount(), 0);
				Stamina = Player->GetStamina();
				Key(EKeys::K, IE_Pressed);
				return Next(0.1);
			case 4:
				UE_LOG(LogTemp, Display, TEXT("REBIND dash: pad %d, K down %d, dash bound down %d, device %d, profile %d"),
					PC->IsUsingGamepad() ? 1 : 0, PC->IsInputKeyDown(EKeys::K) ? 1 : 0,
					IsActionDown(PC, EChaosImpactAction::Dash) ? 1 : 0, static_cast<int32>(GetPlayerDevice(PC)), GetPlayerProfile(PC));
				Test->TestTrue(TEXT("K dashes"), Player->IsDashing() || Player->GetStamina() < Stamina);
				Key(EKeys::K, IE_Released);
				return Next(0.8);
			case 5:
				Start = Player->GetActorLocation();
				Key(EKeys::I, IE_Pressed);
				return Next(0.5);
			case 6:
				UE_LOG(LogTemp, Display, TEXT("REBIND walked %.0f with I"), FVector::Dist2D(Start, Player->GetActorLocation()));
				Test->TestTrue(TEXT("Holding I walks"), FVector::Dist2D(Start, Player->GetActorLocation()) > 80.0f);
				Key(EKeys::I, IE_Released);
				return Next(0.2);
			default:
				Cleanup(PC);
				return true;
			}
		}

	private:
		void Cleanup(AChaosImpactPlayerController* PC)
		{
			if (UChaosImpactLoadoutSubsystem* Loadouts = PC ? UChaosImpactLoadoutSubsystem::Get(PC) : nullptr)
			{
				Loadouts->SetNickname(0, FString());
			}
			RemoveTestProfiles();
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Profile = 0;
		float Stamina = 0.0f;
		FVector Start = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSettingsMenuTest, "ChaosImpact.Menu.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactSettingsMenuTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSettingsMenuCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactRebindTest, "ChaosImpact.Training.Rebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactRebindTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRebindCommand(this));
	return true;
}

#endif
