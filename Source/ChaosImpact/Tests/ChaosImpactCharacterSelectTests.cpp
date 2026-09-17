#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindSelectTestController()
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

	void SelectKey(const FKey Key)
	{
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	}

	/** A button on a real pad: its own input device, arriving as its own Slate user. */
	void SelectPadKey(const FKey Key, const int32 InputDeviceId, const int32 UserIndex)
	{
		const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(InputDeviceId);
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), Device, false, 0, 0, TOptional<int32>(UserIndex)));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), Device, false, 0, 0, TOptional<int32>(UserIndex)));
	}

	void SelectShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SelectQA"), Name), true, false);
	}

	/**
	 * Three players on one machine (keyboard 1P, pads 2P and 3P) go through character select into a local VS:
	 * each device drives only its own card, colours never collide, ready/cancel/back work, and the picks reach
	 * the players in the match.
	 */
	class FCharacterSelectCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCharacterSelectCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSelectTestController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactCharacterSelect* Select = Menu ? Menu->GetCharacterSelect() : nullptr;
			if (Stage < 10 && (!PC || !Menu || !Select))
			{
				return Waited(Now, TEXT("No menu."));
			}
			const auto Next = [this, Now](const double Seconds)
			{
				++Stage;
				NextAt = Now + Seconds;
				return false;
			};
			const auto Screen = [PC](const EChaosImpactScreen Expected)
			{
				return PC && PC->GetCurrentScreen() == Expected;
			};

			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 3.0)
				{
					return false;
				}
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(3);
				PC->RegisterKeyboardMouseJoin();
				SelectPadKey(EKeys::Gamepad_FaceButton_Top, 201, 1);
				SelectPadKey(EKeys::Gamepad_FaceButton_Top, 202, 2);
				Test->TestTrue(TEXT("Three players are assigned"), PC->AreControllerAssignmentsComplete());
				PC->ConfirmControllerAssignments();
				Test->TestTrue(TEXT("Assignment leads to character select"), Screen(EChaosImpactScreen::CharacterSelect));
				Test->TestEqual(TEXT("One card per player"), Select->GetPlayerCount(), 3);
				Test->TestTrue(TEXT("Everyone starts in a different colour"), Distinct(Select, true));
				return Next(1.2);
			case 1:
				SelectShot(TEXT("CS-01-Open.png"));
				return Next(0.3);
			case 2:
			{
				using EStep = UChaosImpactCharacterSelect::EStep;
				// 3P's cursor goes onto a COMING SOON icon: nothing can be picked there.
				SelectPadKey(EKeys::Gamepad_DPad_Right, 202, 2);
				Test->TestEqual(TEXT("The D-pad moves 3P's cursor"), Select->GetCursorTile(2), 1);
				SelectPadKey(EKeys::Gamepad_DPad_Right, 202, 2);
				Test->TestEqual(TEXT("3P's cursor reaches the first locked icon"), Select->GetCursorTile(2), ChaosImpactRoster::Num());
				Test->TestEqual(TEXT("Other cursors stay"), Select->GetCursorTile(0), 0);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				Test->TestTrue(TEXT("A locked icon cannot be picked"), Select->GetStep(2) == EStep::Character);
				SelectPadKey(EKeys::Gamepad_DPad_Down, 202, 2);
				Test->TestEqual(TEXT("The cursor moves between rows"), Select->GetCursorTile(2), ChaosImpactRoster::Num() + 4);
				SelectPadKey(EKeys::Gamepad_DPad_Up, 202, 2);
				for (int32 Step = 0; Step <= ChaosImpactRoster::Num(); ++Step)
				{
					SelectPadKey(EKeys::Gamepad_DPad_Left, 202, 2);
				}
				Test->TestEqual(TEXT("The cursor stops at the grid's edge"), Select->GetCursorTile(2), 0);

				// 1P picks the character; the colour row opens and Q/E change the colour.
				SelectKey(EKeys::Enter);
				Test->TestTrue(TEXT("Enter picks 1P's character"), Select->GetStep(0) == EStep::Colour);
				const int32 P1Colour = Select->GetColour(0);
				SelectKey(EKeys::E);
				Test->TestNotEqual(TEXT("E changes 1P's colour"), Select->GetColour(0), P1Colour);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				const int32 P3Colour = Select->GetColour(2);
				SelectPadKey(EKeys::Gamepad_DPad_Right, 202, 2);
				Test->TestNotEqual(TEXT("On the colour row the D-pad changes 3P's colour"), Select->GetColour(2), P3Colour);
				Test->TestTrue(TEXT("Colour choices never collide"), Distinct(Select));
				const int32 Before[] = {Select->GetColour(0), Select->GetColour(1), Select->GetColour(2)};
				SelectPadKey(EKeys::Gamepad_RightShoulder, 999, 3);
				Test->TestTrue(TEXT("A device nobody holds changes nothing"), Select->GetColour(0) == Before[0]
					&& Select->GetColour(1) == Before[1] && Select->GetColour(2) == Before[2]);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				for (int32 Turn = 0; Turn < 6; ++Turn)
				{
					SelectPadKey(EKeys::Gamepad_LeftShoulder, 201, 1);
					Test->TestTrue(TEXT("Cycling colours skips the ones in use"), Distinct(Select));
				}
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				Test->TestTrue(TEXT("Pad 201 finishes 2P"), Select->IsReady(1));
				Test->TestFalse(TEXT("2P's button does not finish 1P"), Select->IsReady(0));
				SelectShot(TEXT("CS-02-Choosing.png"));
				return Next(1.0);
			}
			case 3:
			{
				using EStep = UChaosImpactCharacterSelect::EStep;
				const int32 Locked = Select->GetColour(1);
				SelectPadKey(EKeys::Gamepad_RightShoulder, 201, 1);
				Test->TestEqual(TEXT("A finished player's colour is locked"), Select->GetColour(1), Locked);
				SelectPadKey(EKeys::Gamepad_FaceButton_Right, 201, 1);
				Test->TestTrue(TEXT("B takes 2P back to the colour row"), Select->GetStep(1) == EStep::Colour);
				SelectPadKey(EKeys::Gamepad_FaceButton_Right, 201, 1);
				Test->TestTrue(TEXT("B again takes 2P back to the icons"), Select->GetStep(1) == EStep::Character);
				SelectPadKey(EKeys::Gamepad_FaceButton_Right, 201, 1);
				Test->TestTrue(TEXT("Only 1P can back out of the screen"), Screen(EChaosImpactScreen::CharacterSelect));
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				SelectKey(EKeys::Enter);
				Test->TestTrue(TEXT("Enter finishes 1P"), Select->IsReady(0));
				SelectKey(EKeys::Enter);
				Test->TestFalse(TEXT("It cannot start while 3P is choosing"), Select->IsStarting());
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				Test->TestTrue(TEXT("Everyone is ready"), Select->AreAllReady());
				Test->TestTrue(TEXT("Final colours are all different"), Distinct(Select));
				SelectShot(TEXT("CS-03-AllReady.png"));
				return Next(0.9);
			}
			case 4:
				Picks[0] = Select->GetColour(0);
				Picks[1] = Select->GetColour(1);
				Picks[2] = Select->GetColour(2);
				// Backing out one step at a time: done, colour row, icons, then the assignment screen.
				SelectKey(EKeys::Escape);
				Test->TestFalse(TEXT("Esc undoes 1P's OK first"), Select->IsReady(0));
				SelectKey(EKeys::Escape);
				SelectKey(EKeys::Escape);
				Test->TestTrue(TEXT("1P backs out to assignment"), Screen(EChaosImpactScreen::ControllerAssignment));
				return Next(0.6);
			case 5:
				PC->ConfirmControllerAssignments();
				Test->TestTrue(TEXT("Character select opens again"), Screen(EChaosImpactScreen::CharacterSelect));
				Test->TestTrue(TEXT("The picks are remembered"), Select->GetColour(0) == Picks[0]
					&& Select->GetColour(1) == Picks[1] && Select->GetColour(2) == Picks[2]);
				Test->TestFalse(TEXT("Nobody is ready on a fresh visit"), Select->IsReady(0) || Select->IsReady(1) || Select->IsReady(2));
				SelectKey(EKeys::SpaceBar);
				SelectKey(EKeys::SpaceBar);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 201, 1);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				Test->TestTrue(TEXT("Everyone is ready again"), Select->AreAllReady());
				Test->TestTrue(TEXT("Keeping the same picks keeps the same colours"), Select->GetColour(0) == Picks[0]
					&& Select->GetColour(1) == Picks[1] && Select->GetColour(2) == Picks[2]);
				return Next(0.5);
			case 6:
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 202, 2);
				Test->TestTrue(TEXT("Any player can start once everyone is ready"), Select->IsStarting());
				// Presses during the start must not start twice or go back.
				SelectKey(EKeys::Escape);
				SelectKey(EKeys::Enter);
				return Next(0.15);
			case 7:
				SelectShot(TEXT("CS-04-Go.png"));
				return Next(0.8);
			case 8:
				if (!Screen(EChaosImpactScreen::MatchRules))
				{
					return Waited(Now, TEXT("Character select never moved on to the rules."));
				}
				Test->TestEqual(TEXT("The rules count the three players"), PC->GetMatchHumanCount(), 3);
				PC->ConfirmMatchRules();
				return Next(2.0);
			case 9:
			{
				UWorld* World = PC ? PC->GetWorld() : nullptr;
				const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
				if (!World || !World->URL.HasOption(TEXT("CIMatch=1")) || !GameInstance || GameInstance->GetLocalPlayers().Num() < 3)
				{
					return Waited(Now, TEXT("The VS level with three players never opened."));
				}
				int32 Applied = 0;
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const APlayerController* Local = GameInstance->GetLocalPlayers()[Index]->GetPlayerController(World);
					const AChaosImpactPlayerState* State = Local ? Local->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
					Applied += State && State->ColourChoice == Picks[Index] ? 1 : 0;
				}
				if (Applied < 3)
				{
					return Waited(Now, TEXT("The picked colours never reached the players."));
				}
				Test->TestEqual(TEXT("Every player wears their pick in the match"), Applied, 3);
				return Next(4.0);
			}
			case 10:
				SelectShot(TEXT("CS-05-InMatch.png"));
				return Next(0.8);
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now, const TCHAR* Error)
		{
			if (Now - StartedAt < 180.0)
			{
				return false;
			}
			Test->AddError(Error);
			return true;
		}

		/**
		 * No two players who have picked a character share a colour on it. A player still moving over the icons
		 * only previews a colour; theirs is settled when they pick. At opening, everyone's saved colour counts.
		 */
		static bool Distinct(const UChaosImpactCharacterSelect* Select, const bool bEveryone = false)
		{
			using EStep = UChaosImpactCharacterSelect::EStep;
			for (int32 A = 0; A < Select->GetPlayerCount(); ++A)
			{
				for (int32 B = A + 1; B < Select->GetPlayerCount(); ++B)
				{
					const bool bBothChose = bEveryone
						|| (Select->GetStep(A) != EStep::Character && Select->GetStep(B) != EStep::Character);
					if (bBothChose && Select->GetCharacter(A) == Select->GetCharacter(B) && Select->GetColour(A) == Select->GetColour(B))
					{
						return false;
					}
				}
			}
			return true;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Picks[3] = {0, 1, 2};
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCharacterSelectTest, "ChaosImpact.UI.CharacterSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactCharacterSelectTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCharacterSelectCommand(this));
	return true;
}

namespace
{
	/** Films the screen with one, two and four players, some of them already on the colour row or done. */
	class FCharacterSelectLayoutsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCharacterSelectLayoutsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindSelectTestController();
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactCharacterSelect* Select = Menu ? Menu->GetCharacterSelect() : nullptr;
			if (!PC || !Menu || !Select || Now - StartedAt < 3.0)
			{
				if (Now - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No menu."));
					return true;
				}
				return false;
			}
			const auto Open = [PC](const int32 Players)
			{
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				PC->BeginVersusLocal();
				PC->PrepareTrainingControllerAssignment(Players);
				PC->RegisterKeyboardMouseJoin();
				for (int32 Pad = 1; Pad < Players; ++Pad)
				{
					SelectPadKey(EKeys::Gamepad_FaceButton_Top, 300 + Pad, Pad);
				}
				PC->ConfirmControllerAssignments();
			};
			NextAt = Now + 1.4;
			switch (Stage++)
			{
			case 0:
				Open(1);
				Test->TestEqual(TEXT("One window"), Select->GetPlayerCount(), 1);
				return false;
			case 1:
				SelectShot(TEXT("CS-L1-Single.png"));
				return false;
			case 2:
				SelectKey(EKeys::Enter);
				NextAt = Now + 0.8;
				return false;
			case 3:
				SelectShot(TEXT("CS-L1-SingleColour.png"));
				return false;
			case 4:
				Open(2);
				Test->TestEqual(TEXT("Two windows"), Select->GetPlayerCount(), 2);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 301, 1);
				return false;
			case 5:
				SelectShot(TEXT("CS-L2-Pair.png"));
				return false;
			case 6:
				Open(4);
				Test->TestEqual(TEXT("Four windows"), Select->GetPlayerCount(), 4);
				SelectPadKey(EKeys::Gamepad_DPad_Right, 302, 2);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 303, 3);
				SelectPadKey(EKeys::Gamepad_FaceButton_Bottom, 303, 3);
				return false;
			case 7:
				SelectShot(TEXT("CS-L4-Four.png"));
				return false;
			case 8:
				PC->ShowMenuScreen(EChaosImpactScreen::ModeSelect);
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactCharacterSelectLayoutsTest, "ChaosImpact.UI.CharacterSelectLayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactCharacterSelectLayoutsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCharacterSelectLayoutsCommand(this));
	return true;
}

#endif
