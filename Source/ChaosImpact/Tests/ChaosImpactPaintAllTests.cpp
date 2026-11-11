#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactColourEdit.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPaintStudio.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	int32 DrawnPixels(const UChaosImpactColourEdit* Edit)
	{
		int32 Count = 0;
		for (const UChaosImpactPaintLayer* Layer : Edit->GetPaintLayers())
		{
			for (const FColor& Pixel : Layer ? Layer->GetPixels() : TArray<FColor>())
			{
				Count += Pixel.A > 0 ? 1 : 0;
			}
		}
		return Count;
	}

	/**
	 * ペイント on every character: the studio opens on it, a stroke draws (and its mirror on the other side), the screen is
	 * filmed, and やめる leaves the pictures as they were. Nothing is saved. Saved/PaintQA/all_<n>.png.
	 */
	class FPaintAllCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintAllCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			UChaosImpactColourEdit* Edit = PC && PC->GetMenuWidget() ? PC->GetMenuWidget()->GetColourEdit() : nullptr;
			if (Now - StartedAt > 400.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at character %d"), Character));
				return true;
			}
			if (!PC || !PC->GetPawn() || Now < NextAt)
			{
				return false;
			}
			if (Stage == 0)
			{
				if (Now - StartedAt < 4.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::Pause);
				PC->OpenColourEdit(EChaosImpactScreen::Pause);
				Stage = 1;
				NextAt = Now + 2.0;
				return false;
			}
			if (!Edit || !Edit->IsOpen())
			{
				Test->AddError(TEXT("カラーエディット did not open"));
				return true;
			}
			if (Character >= ChaosImpactRoster::Num())
			{
				PC->CloseColourEdit();
				return true;
			}
			UChaosImpactPaintStudio* Studio = Edit->GetStudio();
			switch (Stage)
			{
			case 1:
				Edit->SelectCharacter(Character);
				Stage = 2;
				NextAt = Now + 1.5;
				return false;
			case 2:
				Before = DrawnPixels(Edit);
				Edit->OpenPaint();
				Stage = 3;
				NextAt = Now + 1.2;
				return false;
			case 3:
			{
				Studio = Edit->GetStudio();
				const FString Name = ChaosImpactRoster::Get(Character).Name;
				if (!Studio || !Studio->IsOpen() || !Studio->CanPaint())
				{
					Test->AddError(FString::Printf(TEXT("%s cannot be painted"), *Name));
					Stage = 5;
					return false;
				}
				// A short stroke a little off the middle, mirrored.
				const FVector2D C = Studio->GetModelCentreOnScreen();
				Studio->GetBrush().Colour = FColor(255, 40, 160);
				Studio->GetBrush().Radius = UChaosImpactPaintStudio::SizeToRadius(0.5f);
				Studio->GetBrush().bMirror = false;
				Studio->DrawLine(C + FVector2D(18.0, -60.0), C + FVector2D(40.0, 20.0));
				const int32 One = DrawnPixels(Edit) - Before;
				Studio->GetBrush().bMirror = true;
				Studio->Undo();
				Studio->DrawLine(C + FVector2D(18.0, -60.0), C + FVector2D(40.0, 20.0));
				const int32 Both = DrawnPixels(Edit) - Before;
				UE_LOG(LogTemp, Display, TEXT("PAINTALL %-12s one side %6d, mirrored %6d"), *Name, One, Both);
				Test->TestTrue(FString::Printf(TEXT("%s: the pen draws"), *Name), One > 50);
				Test->TestTrue(FString::Printf(TEXT("%s: the mirror draws on the other side too"), *Name), Both > One + One / 3);
				Studio->SetView(112.0f, 8.0f, 1.0f);
				Stage = 4;
				NextAt = Now + 0.8;
				return false;
			}
			case 4:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PaintQA"), FString::Printf(TEXT("all_%02d.png"), Character)),
					true, false);
				Stage = 5;
				NextAt = Now + 0.6;
				return false;
			default:
				if (Studio && Studio->IsOpen())
				{
					Studio->Finish(false);
				}
				Test->TestEqual(FString::Printf(TEXT("%s: やめる puts it back"), ChaosImpactRoster::Get(Character).Name), DrawnPixels(Edit), Before);
				++Character;
				Stage = 1;
				NextAt = Now + 0.5;
				return false;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Character = 0;
		int32 Before = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintAllTest, "ChaosImpact.Paint.AllCharacters",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintAllTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPaintAllCommand(this));
	return true;
}

#endif
