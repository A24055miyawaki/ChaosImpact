#if WITH_DEV_AUTOMATION_TESTS
// スマホ版のタッチ操作 (2026-11-17): what the fingers mean. Left half the body (drag: move, flick: dash, tap: jump), right
// half the ball (tap: quick throw, hold: charge, drag: aim, let go: throw, flick: tackle); touches starting on the HUD
// are the HUD's (tap, long press). Then on a training stage, the character doing it: a held right thumb charges and its
// release throws; a tap on the left jumps. Run the second on Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=1.
#include "Misc/AutomationTest.h"
#include "Camera/PlayerCameraManager.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTouchLayoutEditor.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTouchControls.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "ChaosImpactSettings.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"

namespace
{
	/** One finger's frames: down at From, moving to To over Seconds, then let go; the frames' outcomes merged. */
	struct FTouchRun
	{
		FChaosImpactTouchGestures Gestures;
		FVector2D View = FVector2D(1280.0, 720.0);
		int32 Hud = 0;
		double Now = 10.0;

		FChaosImpactTouchFrame Stroke(const FVector2D& From, const FVector2D& To, const double Seconds, const int32 Steps = 6,
			const bool bLetGo = true)
		{
			FChaosImpactTouchFrame Merged;
			bool bDown[FChaosImpactTouchGestures::MaxFingers] = {};
			FVector2D At[FChaosImpactTouchGestures::MaxFingers];
			for (FVector2D& Point : At) { Point = FVector2D::ZeroVector; }
			const auto HudHit = [this](const FVector2D&) { return Hud; };
			for (int32 Step = 0; Step <= Steps + (bLetGo ? 1 : 0); ++Step)
			{
				const bool bHeld = Step <= Steps;
				bDown[0] = bHeld;
				At[0] = FMath::Lerp(From, To, FMath::Clamp(static_cast<double>(Step) / FMath::Max(Steps, 1), 0.0, 1.0));
				FChaosImpactTouchFrame Frame;
				Gestures.Update(bDown, At, Now, View, HudHit, Frame);
				Merged.bJump |= Frame.bJump;
				Merged.bDash |= Frame.bDash;
				Merged.bTackle |= Frame.bTackle;
				Merged.bChargeStart |= Frame.bChargeStart;
				Merged.bRelease |= Frame.bRelease;
				Merged.bQuickThrow |= Frame.bQuickThrow;
				Merged.HudTap = Merged.HudTap ? Merged.HudTap : Frame.HudTap;
				Merged.HudHold = Merged.HudHold ? Merged.HudHold : Frame.HudHold;
				if (!Frame.Move.IsNearlyZero()) { Merged.Move = Frame.Move; }
				if (Frame.bDash) { Merged.DashWay = Frame.DashWay; }
				if (Frame.bTackle) { Merged.TackleWay = Frame.TackleWay; }
				if (Frame.bAimByDrag) { Merged.bAimByDrag = true; Merged.AimWay = Frame.AimWay; }
				Now += Seconds / FMath::Max(Steps, 1);
			}
			Now += 1.0;
			return Merged;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTouchGestureTest, "ChaosImpact.Touch.Gestures",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactTouchGestureTest::RunTest(const FString&)
{
	FTouchRun Run;
	// ジェスチャー方式.
	Run.Gestures.bButtons = false;
	FChaosImpactTouchFrame F = Run.Stroke(FVector2D(300, 500), FVector2D(302, 501), 0.1);
	TestFalse(TEXT("Gestures: a left tap does nothing (no jump by mistake)"), F.bJump || F.bDash);
	F = Run.Stroke(FVector2D(300, 500), FVector2D(360, 500), 0.6, 12, false);
	TestTrue(TEXT("Gestures: left drag right moves right"), F.Move.X > 0.5 && FMath::Abs(F.Move.Y) < 0.2);
	TestFalse(TEXT("Gestures: a slow drag is no dash"), F.bDash);
	Run.Stroke(FVector2D(360, 500), FVector2D(360, 500), 0.05, 1);
	F = Run.Stroke(FVector2D(300, 500), FVector2D(300, 360), 0.06, 3);
	TestTrue(TEXT("Gestures: a flick as the thumb lifts dashes"), F.bDash && F.DashWay.Y > 0.9);
	F = Run.Stroke(FVector2D(1000, 500), FVector2D(1001, 500), 0.08);
	TestTrue(TEXT("Gestures: right tap, a quick throw"), F.bQuickThrow && !F.bChargeStart);
	F = Run.Stroke(FVector2D(1000, 500), FVector2D(1060, 500), 0.6, 12);
	TestTrue(TEXT("Gestures: a short drag while held charges and aims (no tackle)"), F.bChargeStart && F.bAimByDrag && F.AimWay.X > 0.9
		&& F.bRelease && !F.bTackle);
	F = Run.Stroke(FVector2D(1000, 500), FVector2D(1000, 350), 0.08, 2);
	TestTrue(TEXT("Gestures: a long fast swipe, a tackle"), F.bTackle && F.TackleWay.Y > 0.9 && !F.bQuickThrow);
	F = Run.Stroke(FChaosImpactTouchGestures::MakeLayout(Run.View, false, false).Jump, FChaosImpactTouchGestures::MakeLayout(Run.View, false, false).Jump, 0.1);
	TestTrue(TEXT("Gestures: the jump button"), F.bJump);
	// ボタン方式.
	Run.Gestures.bButtons = true;
	const FChaosImpactTouchLayout L = FChaosImpactTouchGestures::MakeLayout(Run.View, false, true);
	F = Run.Stroke(L.Throw, L.Throw + FVector2D(1, 0), 0.08);
	TestTrue(TEXT("Buttons: a tap on 投げる, a quick throw"), F.bQuickThrow);
	F = Run.Stroke(L.Throw, L.Throw + FVector2D(-80, -40), 0.6, 12);
	TestTrue(TEXT("Buttons: dragged from 投げる, charged and aimed that way"), F.bChargeStart && F.bRelease && F.bAimByDrag && F.AimWay.X < -0.8);
	F = Run.Stroke(L.Dash, L.Dash, 0.1);
	TestTrue(TEXT("Buttons: ダッシュ"), F.bDash && !F.bJump);
	F = Run.Stroke(L.Jump, L.Jump, 0.1);
	TestTrue(TEXT("Buttons: ジャンプ"), F.bJump);
	F = Run.Stroke(L.Tackle, L.Tackle, 0.1);
	TestTrue(TEXT("Buttons: タックル"), F.bTackle);
	F = Run.Stroke(FVector2D(300, 500), FVector2D(300, 360), 0.06, 3);
	TestFalse(TEXT("Buttons: a flick of the stick is no dash"), F.bDash || F.bJump);
	F = Run.Stroke(FVector2D(900, 300), FVector2D(901, 300), 0.08);
	TestFalse(TEXT("Buttons: a tap on the field (not on a button) throws nothing"), F.bQuickThrow);
	// 片手モード (upright): the thumb anywhere moves, a tap throws, a flick dashes; the row of small buttons.
	{
		FTouchRun One;
		One.View = FVector2D(720.0, 1560.0);
		One.Gestures.bButtons = false;
		One.Gestures.bOneHand = true;
		const FChaosImpactTouchLayout OL = FChaosImpactTouchGestures::MakeLayout(One.View, false, false, nullptr, true);
		TestTrue(TEXT("One hand: its layout"), OL.bOneHand && OL.Radius[FChaosImpactTouchCustom::Throw] <= 0.0f
			&& OL.Radius[FChaosImpactTouchCustom::Tackle] > 0.0f && FMath::Abs(OL.Jump.Y - OL.Fuse.Y) < 1.0);
		F = One.Stroke(FVector2D(560, 1100), FVector2D(561, 1100), 0.08);
		TestTrue(TEXT("One hand: a tap on the right, a quick throw"), F.bQuickThrow && !F.bJump);
		F = One.Stroke(FVector2D(200, 1000), FVector2D(202, 1001), 0.08);
		TestTrue(TEXT("One hand: a tap on the left, a quick throw too"), F.bQuickThrow);
		// (Moving off at once: held still a moment first, the thumb would charge instead.)
		F = One.Stroke(FVector2D(560, 1100), FVector2D(700, 1100), 0.4, 12, false);
		TestTrue(TEXT("One hand: the right half moves the body"), F.Move.X > 0.5 && !F.bQuickThrow && !F.bCharging);
		One.Stroke(FVector2D(700, 1100), FVector2D(700, 1100), 0.05, 1);
		F = One.Stroke(FVector2D(400, 1100), FVector2D(400, 960), 0.06, 3);
		TestTrue(TEXT("One hand: a flick dashes"), F.bDash && !F.bQuickThrow);
		F = One.Stroke(OL.Tackle, OL.Tackle, 0.1);
		TestTrue(TEXT("One hand: タックル"), F.bTackle && !F.bQuickThrow);
		F = One.Stroke(OL.Jump, OL.Jump, 0.1);
		TestTrue(TEXT("One hand: ジャンプ"), F.bJump && !F.bQuickThrow);
	}
	// The HUD.
	Run.Hud = 11;
	F = Run.Stroke(FVector2D(1150, 650), FVector2D(1151, 650), 0.1);
	TestEqual(TEXT("A tap on the holder"), F.HudTap, 11);
	TestFalse(TEXT("Not a throw"), F.bQuickThrow);
	F = Run.Stroke(FVector2D(1150, 650), FVector2D(1151, 650), 0.7, 10);
	TestEqual(TEXT("A long press on it"), F.HudHold, 11);
	TestEqual(TEXT("Not a tap too"), F.HudTap, 0);
	return true;
}

namespace
{
	class FTouchPlayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTouchPlayCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 60.0) { Test->AddError(FString::Printf(TEXT("Touch play stalled at step %d"), Step)); return true; }
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Own || Now < WaitUntil) { return false; }
			Own->RefillHealth();
			int32 X = 0;
			int32 Y = 0;
			PC->GetViewportSize(X, Y);
			// (ボタン方式, the standard: the 投げる button and the ジャンプ button; the stick anywhere on the left.)
			const FChaosImpactTouchLayout Layout = FChaosImpactTouchGestures::MakeLayout(FVector2D(X, Y), false, true);
			const FVector2D Right = Layout.Throw;
			const FVector2D Left(X * 0.25, Y * 0.6);
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			case 1:
				// (Standing: not knocked out at the start.)
				if (Own->IsEliminated()) { return false; }
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Normal);
				Own->SetTouchForTests({Right});
				WaitUntil = Now + 0.3;
				StepAt = Now;
				Step = 2;
				return false;
			case 2:
				// (A frame's hitch may delay it a little.)
				if (!Own->IsChargingThrow() && Now - StepAt < 2.0) { return false; }
				Test->TestTrue(TEXT("Held right thumb: charging"), Own->IsChargingThrow());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), TEXT("Buttons_Charging.png")), true, false);
				Own->SetTouchForTests({Right + FVector2D(60.0, -20.0)});
				WaitUntil = Now + 0.2;
				Step = 3;
				return false;
			case 3:
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.3;
				Step = 4;
				return false;
			case 4:
				Test->TestFalse(TEXT("Let go: thrown"), Own->IsChargingThrow());
				Test->TestEqual(TEXT("One ball left"), Own->GetCarriedBallCount(), 1);
				// A tap on ジャンプ.
				Own->SetTouchForTests({Layout.Jump});
				WaitUntil = Now + 0.05;
				Step = 5;
				return false;
			case 5:
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.15;
				Step = 6;
				return false;
			case 6:
				Test->TestTrue(TEXT("ジャンプ: in the air"), Own->GetCharacterMovement()->IsFalling());
				// A drag on the left: moving.
				StartAt = Own->GetActorLocation();
				Own->SetTouchForTests({Left});
				WaitUntil = Now + 0.6;
				Step = 7;
				return false;
			case 7:
				Own->SetTouchForTests({Left + FVector2D(70.0, 0.0)});
				WaitUntil = Now + 0.8;
				Step = 8;
				return false;
			case 8:
				Test->TestTrue(TEXT("Left drag: it walks"), FVector::Dist2D(Own->GetActorLocation(), StartAt) > 150.0);
				// (The thumb lifted first: a new touch.)
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.1;
				Step = 80;
				return false;
			case 80:
				// The 向き stick touched, then pushed to the right: it turns to face the screen's right.
				Own->SetTouchForTests({Layout.Aim});
				WaitUntil = Now + 0.1;
				Step = 81;
				return false;
			case 81:
				Own->SetTouchForTests({Layout.Aim + FVector2D(90.0, 0.0)});
				WaitUntil = Now + 0.6;
				Step = 9;
				return false;
			case 9:
			{
				const FVector ScreenRight = FRotationMatrix(FRotator(0.0f, PC->PlayerCameraManager->GetCameraRotation().Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
				const double Facing = FVector::DotProduct(Own->GetActorForwardVector().GetSafeNormal2D(), ScreenRight.GetSafeNormal2D());
				UE_LOG(LogTemp, Display, TEXT("TOUCHTEST aim stick facing %.2f"), Facing);
				Test->TestTrue(TEXT("向き stick: faces the way it is pushed"), Facing > 0.8);
				// A charge, its thumb slid onto やめる and let go there: put away, nothing thrown.
				Own->SetTouchForTests({});
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Normal);
				WaitUntil = Now + 0.2;
				Step = 10;
				return false;
			}
			case 10:
				Own->SetTouchForTests({Right});
				WaitUntil = Now + 0.4;
				StepAt = Now;
				Step = 11;
				return false;
			case 11:
				if (!Own->IsChargingThrow() && Now - StepAt < 2.0) { return false; }
				Test->TestTrue(TEXT("やめる: charging first"), Own->IsChargingThrow());
				Own->SetTouchForTests({Layout.Cancel});
				WaitUntil = Now + 0.2;
				Step = 12;
				return false;
			case 12:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), TEXT("Cancel.png")), true, false);
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.3;
				Step = 13;
				return false;
			case 13:
			{
				Test->TestFalse(TEXT("やめる: no longer charging"), Own->IsChargingThrow());
				Test->TestEqual(TEXT("やめる: nothing thrown"), Own->GetCarriedBallCount(), 2);
				// ボタン・画面の配置 (the menus' own editor, over a still picture): 投げる dragged 100 to the left, then 完了.
				AChaosImpactPlayerController* Menus = Cast<AChaosImpactPlayerController>(PC);
				UChaosImpactMenuWidget* Menu = Menus ? Menus->GetMenuWidget() : nullptr;
				if (!Menu) { Test->AddError(TEXT("No menu widget")); return true; }
				// (From the pause menu's 設定, as a player opens it: the menus are on the screen.)
				Menus->TogglePauseMenu();
				Menu->OpenTouchLayoutEditor(ChaosImpactSettings::GetPlayerProfile(PC), ChaosImpactSettings::GetTouchModeKey(PC));
				WaitUntil = Now + 0.4;
				Step = 14;
				return false;
			}
			case 14:
			{
				UChaosImpactMenuWidget* Menu = Cast<AChaosImpactPlayerController>(PC)->GetMenuWidget();
				UChaosImpactTouchLayoutEditor* Editor = Menu->GetTouchLayoutEditor();
				const FVector2D Size = Menu->GetCachedGeometry().GetLocalSize();
				Test->TestTrue(TEXT("The editor is open"), Editor && Editor->IsOpen());
				if (!Editor) { return true; }
				const FVector2D From = Editor->GetCenterOf(FChaosImpactTouchCustom::Throw, Size);
				Editor->HandleDown(From, Size);
				Editor->HandleMove(From + FVector2D(-50.0, 0.0), Size);
				Editor->HandleMove(From + FVector2D(-100.0, 0.0), Size);
				Editor->HandleUp(From + FVector2D(-100.0, 0.0), Size);
				Test->TestEqual(TEXT("投げる picked up"), Editor->GetSelected(), static_cast<int32>(FChaosImpactTouchCustom::Throw));
				// And the KO / HIT feed moved down.
				const FVector2D Clock = Editor->GetCenterOf(FChaosImpactTouchCustom::Feed, Size);
				Editor->HandleDown(Clock, Size);
				Editor->HandleMove(Clock + FVector2D(0.0, 80.0), Size);
				Editor->HandleUp(Clock + FVector2D(0.0, 80.0), Size);
				Test->TestEqual(TEXT("KO・HIT表示 picked up"), Editor->GetSelected(), static_cast<int32>(FChaosImpactTouchCustom::Feed));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), TEXT("LayoutEdit.png")), true, false);
				WaitUntil = Now + 0.4;
				Step = 150;
				return false;
			}
			case 150:
			{
				// The kinds of play from the bar: 1対1, its own HUD (the duel's scoreboard, no standings).
				UChaosImpactMenuWidget* Menu = Cast<AChaosImpactPlayerController>(PC)->GetMenuWidget();
				UChaosImpactTouchLayoutEditor* Editor = Menu->GetTouchLayoutEditor();
				const FVector2D Size = Menu->GetCachedGeometry().GetLocalSize();
				const FVector2D Open = Editor->GetButtonBox(UChaosImpactTouchLayoutEditor::ModeMenu, Size).GetCenter();
				Editor->HandleDown(Open, Size);
				Editor->HandleUp(Open, Size);
				Test->TestTrue(TEXT("The kinds of play open"), Editor->IsPanelOpen());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), TEXT("LayoutEdit_Modes.png")), true, false);
				WaitUntil = Now + 0.4;
				Step = 151;
				return false;
			}
			case 151:
			{
				UChaosImpactMenuWidget* Menu = Cast<AChaosImpactPlayerController>(PC)->GetMenuWidget();
				UChaosImpactTouchLayoutEditor* Editor = Menu->GetTouchLayoutEditor();
				const FVector2D Size = Menu->GetCachedGeometry().GetLocalSize();
				const FVector2D Duel = Editor->GetTabBox(1, Size).GetCenter();
				Editor->HandleDown(Duel, Size);
				Editor->HandleUp(Duel, Size);
				Test->TestEqual(TEXT("1対1 shown"), Editor->GetMode(), FString(TEXT("duel")));
				Test->TestFalse(TEXT("The panel out of the way"), Editor->IsPanelOpen());
				WaitUntil = Now + 0.4;
				Step = 152;
				return false;
			}
			case 152:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), TEXT("LayoutEdit_Duel.png")), true, false);
				WaitUntil = Now + 0.4;
				Step = 15;
				return false;
			case 15:
			{
				UChaosImpactMenuWidget* Menu = Cast<AChaosImpactPlayerController>(PC)->GetMenuWidget();
				UChaosImpactTouchLayoutEditor* Editor = Menu->GetTouchLayoutEditor();
				const FVector2D Size = Menu->GetCachedGeometry().GetLocalSize();
				const FVector2D Done = Editor->GetButtonBox(UChaosImpactTouchLayoutEditor::Done, Size).GetCenter();
				Editor->HandleDown(Done, Size);
				Editor->HandleUp(Done, Size);
				Test->TestFalse(TEXT("完了: the editor closed"), Editor->IsOpen());
				const FChaosImpactTouchCustom Kept = ChaosImpactSettings::GetTouchLayout(ChaosImpactSettings::GetPlayerProfile(PC),
					ChaosImpactSettings::GetTouchModeKey(PC));
				const float Moved = static_cast<float>(Kept.Offset[FChaosImpactTouchCustom::Throw].X);
				const float Lowered = static_cast<float>(Kept.Offset[FChaosImpactTouchCustom::Feed].Y);
				UE_LOG(LogTemp, Display, TEXT("TOUCHTEST throw moved %.1f, clock lowered %.1f"), Moved, Lowered);
				Test->TestTrue(TEXT("投げる kept where it was dragged (left)"), Moved < -40.0f);
				Test->TestTrue(TEXT("KO・HIT表示 kept lower"), Lowered > 30.0f);
				// (Put back as it was.)
				ChaosImpactSettings::SetTouchLayout(ChaosImpactSettings::GetPlayerProfile(PC), ChaosImpactSettings::GetTouchModeKey(PC),
					FChaosImpactTouchCustom());
				Own->ClearTouchForTests();
				Cast<AChaosImpactPlayerController>(PC)->TogglePauseMenu();
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Step = 0;
		double StepAt = 0.0;
		FVector StartAt = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTouchPlayTest, "ChaosImpact.Touch.Play",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactTouchPlayTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTouchPlayCommand(this));
	return true;
}
namespace
{
	/**
	 * 片手モード on an upright screen (run at 720 x 1560 with -CITouch): the camera keeps its width there; standing still with
	 * a ball, it charges by itself at the CPU and throws when full; a tap throws at once. Pictures in Saved/TouchQA.
	 */
	class FTouchOneHandCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTouchOneHandCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const int32 Profile = PC ? ChaosImpactSettings::GetPlayerProfile(PC) : 0;
			if (Now - Started > 70.0)
			{
				Test->AddError(FString::Printf(TEXT("One hand stalled at step %d"), Step));
				ChaosImpactSettings::SetTouchStyle(Profile, StyleBefore);
				return true;
			}
			if (!Own || Now < WaitUntil) { return false; }
			Own->RefillHealth();
			AChaosImpactCharacter* Other = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Own && !It->IsEliminated()) { Other = *It; }
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TouchQA"), Name), true, false);
			};
			switch (Step)
			{
			case 0:
				StyleBefore = ChaosImpactSettings::GetProfile(Profile).TouchStyle;
				ChaosImpactSettings::SetTouchStyle(Profile, 0);
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			case 1:
				if (Own->IsEliminated()) { return false; }
				Own->SetTouchForTests({});
				Test->TestTrue(TEXT("Upright: the camera keeps its width"), Own->GetUprightViewBlend() > 0.9f
					&& Own->GetFollowCamera()->bOverrideAspectRatioAxisConstraint);
				Shot(TEXT("Upright_Buttons.png"));
				WaitUntil = Now + 1.0;
				Step = 2;
				return false;
			case 2:
				ChaosImpactSettings::SetTouchStyle(Profile, 2);
				if (Other)
				{
					Other->SetActorLocation(Own->GetActorLocation() + Own->GetActorForwardVector() * 900.0f + FVector(0.0f, 0.0f, 20.0f));
				}
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Normal);
				// The thumb held still: after a moment, it charges.
				Own->SetTouchForTests({FVector2D(360.0, 900.0)});
				StepAt = Now;
				WaitUntil = Now + 0.5;
				Step = 3;
				return false;
			case 3:
				Test->TestTrue(TEXT("One hand: a held thumb charges"), Own->IsTouchOneHand() && Own->IsChargingThrow());
				// Dragged to aim (to the right of the screen).
				Own->SetTouchForTests({FVector2D(520.0, 900.0)});
				Shot(TEXT("OneHand_Charging.png"));
				WaitUntil = Now + 0.3;
				Step = 4;
				return false;
			case 4:
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.5;
				Step = 5;
				return false;
			case 5:
				Test->TestEqual(TEXT("One hand: let go, thrown"), Own->GetCarriedBallCount(), 1);
				// A tap: the other at once.
				Own->SetTouchForTests({FVector2D(360.0, 520.0)});
				WaitUntil = Now + 0.06;
				Step = 6;
				return false;
			case 6:
				Own->SetTouchForTests({});
				WaitUntil = Now + 0.5;
				Step = 7;
				return false;
			case 7:
				Test->TestEqual(TEXT("One hand: a tap throws the other"), Own->GetCarriedBallCount(), 0);
				Shot(TEXT("OneHand_Thrown.png"));
				WaitUntil = Now + 0.5;
				Step = 8;
				return false;
			default:
				ChaosImpactSettings::SetTouchStyle(Profile, StyleBefore);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		double StepAt = 0.0;
		int32 Step = 0;
		int32 StyleBefore = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTouchOneHandTest, "ChaosImpact.Touch.OneHand",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactTouchOneHandTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTouchOneHandCommand(this));
	return true;
}
#endif
