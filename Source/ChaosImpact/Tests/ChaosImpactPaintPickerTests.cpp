#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactColourEdit.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPaintStudio.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	FKeyEvent PickerKey(const FKey& Which)
	{
		return FKeyEvent(Which, FModifierKeysState(), 0, false, 0, 0);
	}

	float HueOf(const FColor& Colour)
	{
		float Hue = 0.0f, Saturation = 0.0f, Value = 0.0f;
		ChaosImpactCustomColour::ToHSV(Colour, Hue, Saturation, Value);
		return Hue;
	}

	/**
	 * ペイントの色えらび as on the カラーエディット: it opens the way the カラーエディット was (パレット / こまかく), T and the
	 * tabs switch, the hue ring, the square and the H S B / R G B sliders pick by click and drag, the controller's d-pad
	 * and the wheel nudge them, a colour picked on the eraser goes back to the pen, and the colour made is kept in 最近
	 * when let go. Screenshots Saved/PaintQA/picker_*.png. Nothing is saved; 最近使った色 is put back.
	 */
	class FPaintPickerCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintPickerCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 120.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d"), Stage));
				PutRecentBack();
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactColourEdit* Edit = Menu ? Menu->GetColourEdit() : nullptr;
			UChaosImpactPaintStudio* Studio = Edit ? Edit->GetStudio() : nullptr;
			AChaosImpactPlayerState* State = PC ? PC->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			if (!PC || !Menu || !PC->GetPawn() || !State || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"), Name), true, false);
			};
			const auto Next = [this, Now](const double Seconds) { ++Stage; NextAt = Now + Seconds; return false; };
			const auto Click = [Studio](const FVector2D& At)
			{
				Studio->HandleMouseMove(At);
				Studio->HandleMouseDown(At, EKeys::LeftMouseButton, false);
				Studio->HandleMouseUp(At, EKeys::LeftMouseButton);
			};
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				Colours = UChaosImpactColourSubsystem::Get(PC);
				if (Colours.IsValid())
				{
					RecentBefore = Colours->GetRecent();
				}
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				PC->OpenColourEdit(EChaosImpactScreen::Pause);
				return Next(2.0);
			case 1:
				if (!Edit || !Edit->IsOpen())
				{
					Test->AddError(TEXT("カラーエディット did not open"));
					return true;
				}
				Edit->SelectCharacter(State->CharacterIndex);
				if (Edit->IsFine())
				{
					Edit->ToggleFine();
				}
				Edit->OpenPaint();
				return Next(1.5);
			case 2:
				if (!Studio || !Studio->IsOpen())
				{
					Test->AddError(TEXT("ペイント did not open"));
					PutRecentBack();
					return true;
				}
				Test->TestFalse(TEXT("Opened on パレット, as the カラーエディット was"), Studio->IsFine());
				Studio->SetColour(FColor(255, 70, 90), false);
				Studio->HandleMouseMove(FVector2D(800.0, 470.0));
				Shot(TEXT("picker_01_palette.png"));
				return Next(0.8);
			case 3:
			{
				// T: こまかく.
				Studio->HandleKeyDown(PickerKey(EKeys::T));
				Test->TestTrue(TEXT("T switches to こまかく"), Studio->IsFine());
				// The hue ring: green at 120 degrees (on the eraser: picking a colour goes back to the pen).
				Studio->SetTool(UChaosImpactPaintStudio::ETool::Eraser);
				Click(UChaosImpactPaintStudio::RingPoint(120.0f));
				const FColor Green = Studio->GetBrush().Colour;
				Test->TestTrue(FString::Printf(TEXT("The ring picks the hue (%.0f)"), HueOf(Green)), FMath::Abs(HueOf(Green) - 120.0f) < 3.0f);
				Test->TestTrue(TEXT("Picking a colour goes back to the pen"), Studio->GetTool() == UChaosImpactPaintStudio::ETool::Pen);
				Test->TestTrue(TEXT("The colour let go of is in 最近"), Colours.IsValid() && Colours->GetRecent().Num() > 0 && Colours->GetRecent()[0] == Green);
				// The square: saturation across, brightness up.
				const FBox2D Square = UChaosImpactPaintStudio::SquareRect();
				Click(FVector2D(Square.Min.X + Square.GetSize().X * 0.5, Square.Min.Y + Square.GetSize().Y * 0.25));
				float Hue = 0.0f, Saturation = 0.0f, Value = 0.0f;
				ChaosImpactCustomColour::ToHSV(Studio->GetBrush().Colour, Hue, Saturation, Value);
				Test->TestTrue(FString::Printf(TEXT("The square picks saturation and brightness (%.2f, %.2f)"), Saturation, Value),
					FMath::Abs(Saturation - 0.5f) < 0.03f && FMath::Abs(Value - 0.75f) < 0.03f && FMath::Abs(Hue - 120.0f) < 3.0f);
				// R dragged to the far end: R 255, G and B as they were.
				const FColor Before = Studio->GetBrush().Colour;
				const FBox2D Red = UChaosImpactPaintStudio::ColourSliderRect(3);
				Studio->HandleMouseMove(Red.GetCenter());
				Studio->HandleMouseDown(Red.GetCenter(), EKeys::LeftMouseButton, false);
				Studio->HandleMouseMove(FVector2D(Red.Max.X + 40.0, Red.GetCenter().Y));
				Studio->HandleMouseUp(FVector2D(Red.Max.X + 40.0, Red.GetCenter().Y), EKeys::LeftMouseButton);
				const FColor Dragged = Studio->GetBrush().Colour;
				Test->TestTrue(FString::Printf(TEXT("The R slider drags (%d %d %d)"), Dragged.R, Dragged.G, Dragged.B),
					Dragged.R == 255 && Dragged.G == Before.G && Dragged.B == Before.B);
				// H at its middle: 180.
				Click(UChaosImpactPaintStudio::ColourSliderRect(0).GetCenter() + FVector2D(-18.0, 0.0));
				Test->TestTrue(FString::Printf(TEXT("The H slider sets the hue (%.0f)"), HueOf(Studio->GetBrush().Colour)),
					FMath::Abs(HueOf(Studio->GetBrush().Colour) - 180.0f) < 4.0f);
				// The controller: the cursor on the B slider, the d-pad nudges it (one per cent a press).
				const FBox2D Bright = UChaosImpactPaintStudio::ColourSliderRect(2);
				Studio->HandleMouseMove(Bright.GetCenter());
				ChaosImpactCustomColour::ToHSV(Studio->GetBrush().Colour, Hue, Saturation, Value);
				const float ValueBefore = Value;
				const float BrushSize = Studio->GetBrush().Radius;
				Studio->HandleKeyDown(PickerKey(EKeys::Gamepad_DPad_Left));
				ChaosImpactCustomColour::ToHSV(Studio->GetBrush().Colour, Hue, Saturation, Value);
				Test->TestTrue(FString::Printf(TEXT("The d-pad nudges the slider under the cursor (%.3f -> %.3f)"), ValueBefore, Value),
					Value < ValueBefore - 0.002f && Value > ValueBefore - 0.03f);
				Test->TestEqual(TEXT("…and not the brush's size"), Studio->GetBrush().Radius, BrushSize);
				// The wheel on the G slider.
				const FBox2D GreenSlider = UChaosImpactPaintStudio::ColourSliderRect(4);
				const uint8 GreenBefore = Studio->GetBrush().Colour.G;
				Studio->HandleWheel(GreenSlider.GetCenter(), -3.0f, false);
				Test->TestTrue(FString::Printf(TEXT("The wheel nudges a slider (G %d -> %d)"), GreenBefore, Studio->GetBrush().Colour.G),
					Studio->GetBrush().Colour.G < GreenBefore);
				// The look of it: a blue, the cursor on the S slider.
				Studio->SetColour(FColor(40, 110, 255), false);
				Studio->HandleMouseMove(UChaosImpactPaintStudio::ColourSliderRect(1).GetCenter());
				Shot(TEXT("picker_02_fine.png"));
				return Next(0.8);
			}
			case 4:
				// The tabs: パレット, then a swatch picked from it.
				Click(UChaosImpactPaintStudio::TabRect(false).GetCenter());
				Test->TestFalse(TEXT("The パレット tab goes back to the palette"), Studio->IsFine());
				Click(UChaosImpactPaintStudio::TabRect(true).GetCenter());
				Test->TestTrue(TEXT("The こまかく tab"), Studio->IsFine());
				Studio->HandleMouseMove(UChaosImpactPaintStudio::RingPoint(300.0f));
				Shot(TEXT("picker_03_ring_hover.png"));
				return Next(0.8);
			case 5:
				// Back, and opened again from a カラーエディット on こまかく.
				Studio->Finish(false);
				Edit->ToggleFine();
				Edit->OpenPaint();
				return Next(1.2);
			case 6:
				Test->TestTrue(TEXT("Opened on こまかく, as the カラーエディット was"), Studio->IsOpen() && Studio->IsFine());
				Studio->Finish(false);
				Edit->ToggleFine();
				PC->CloseColourEdit();
				PutRecentBack();
				return true;
			default:
				return true;
			}
		}

	private:
		void PutRecentBack()
		{
			if (Colours.IsValid())
			{
				Colours->SetRecentForTests(RecentBefore);
			}
		}

		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		TWeakObjectPtr<UChaosImpactColourSubsystem> Colours;
		TArray<FColor> RecentBefore;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintPickerTest, "ChaosImpact.Paint.ColourPicker",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintPickerTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPaintPickerCommand(this));
	return true;
}

#endif
