#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactCharacterPreview.h"
#include "Components/SkeletalMeshComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactColourEdit.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactPaintCache.h"
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
	int32 CountDrawn(const UChaosImpactColourEdit* Edit)
	{
		int32 Count = 0;
		for (const UChaosImpactPaintLayer* Layer : Edit->GetPaintLayers())
		{
			if (!Layer)
			{
				continue;
			}
			for (const FColor& Pixel : Layer->GetPixels())
			{
				Count += Pixel.A > 0 ? 1 : 0;
			}
		}
		return Count;
	}

	FKeyEvent Key(const FKey& Which)
	{
		return FKeyEvent(Which, FModifierKeysState(), 0, false, 0, 0);
	}

	/**
	 * ペイント in カラーエディット: opened from the button, drawn on with the pen (a mirrored stroke too), taken back and done
	 * again, rubbed out, filled, zoomed and turned, left through its menu (kept) and once thrown away; the pictures packed
	 * and read back, and shown on the player's character in play. Screenshots in Saved/PaintQA. No preset is saved.
	 */
	class FPaintStudioCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPaintStudioCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
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
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				// (The colours used here would go into the player's 最近使った色: put back at the end.)
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
				return Next(2.5);
			case 2:
				// The button under the others.
				for (int32 Right = 0; Right < 4 && Edit->GetArea() == UChaosImpactColourEdit::EArea::Characters; ++Right) { Edit->Move(1, 0); }
				while (Edit->GetArea() != UChaosImpactColourEdit::EArea::Buttons && Guard++ < 40)
				{
					Edit->Move(0, 1);
				}
				Edit->Move(0, 1);
				// (Confirmed at once: the real mouse over the window would move the cursor in the meantime.)
				Test->TestTrue(TEXT("Down from the buttons reaches ペイント"), Edit->GetArea() == UChaosImpactColourEdit::EArea::Buttons);
				// (The walk through the palette tried its colours on: the preset's own back first.)
				Edit->SelectCharacter(State->CharacterIndex);
				Edit->Confirm();
				return Next(1.5);
			case 3:
				return Next(0.1);
			case 4:
				Studio = Edit->GetStudio();
				if (!Studio || !Studio->IsOpen())
				{
					Test->AddError(TEXT("ペイント did not open from its button"));
					return true;
				}
				Test->TestTrue(TEXT("The model can be painted"), Studio->CanPaint());
				Shot(TEXT("paint_01_open.png"));
				UE_LOG(LogTemp, Display, TEXT("PAINTQA opened"));
				// Held still while it is painted: a hand and the head where they were a moment ago.
				if (const AChaosImpactCharacterPreview* Preview = Studio->GetPreviewForTests())
				{
					StillHand = Preview->GetPoseForTests()->GetBoneLocation(TEXT("hand_r"));
					StillHead = Preview->GetPoseForTests()->GetBoneLocation(TEXT("head"));
				}
				return Next(0.8);
			case 5:
			{
				if (const AChaosImpactCharacterPreview* Preview = Studio->GetPreviewForTests())
				{
					const FVector Hand = Preview->GetPoseForTests()->GetBoneLocation(TEXT("hand_r"));
					const FVector Head = Preview->GetPoseForTests()->GetBoneLocation(TEXT("head"));
					UE_LOG(LogTemp, Display, TEXT("PAINTQA still: hand moved %.2f, head moved %.2f"), FVector::Dist(Hand, StillHand), FVector::Dist(Head, StillHead));
					Test->TestTrue(TEXT("The character stands still while painted"), FVector::Dist(Hand, StillHand) < 0.5f && FVector::Dist(Head, StillHead) < 0.5f);
				}
				// Strokes across the body: red, then a mirrored blue one off the middle.
				const FVector2D C = Studio->GetModelCentreOnScreen();
				Studio->SetColour(FColor(250, 250, 250));
				Studio->GetBrush().Radius = UChaosImpactPaintStudio::SizeToRadius(0.45f);
				const double T0 = FPlatformTime::Seconds();
				Studio->DrawLine(C + FVector2D(-80.0, -40.0), C + FVector2D(80.0, -40.0));
				Studio->DrawLine(C + FVector2D(-60.0, 40.0), C + FVector2D(60.0, 60.0));
				const double T1 = FPlatformTime::Seconds();
				UE_LOG(LogTemp, Display, TEXT("PAINTQA two strokes took %.1f ms"), (T1 - T0) * 1000.0);
				Drawn = CountDrawn(Edit);
				Test->TestTrue(TEXT("The pen drew"), Drawn > 200);
				Studio->SetColour(FColor(30, 90, 240));
				Studio->GetBrush().bMirror = true;
				Studio->DrawLine(C + FVector2D(30.0, -100.0), C + FVector2D(50.0, 10.0));
				Studio->GetBrush().bMirror = false;
				Mirrored = CountDrawn(Edit);
				Test->TestTrue(TEXT("The mirrored stroke drew"), Mirrored > Drawn);
				UE_LOG(LogTemp, Display, TEXT("PAINTQA drawn %d, with the mirrored stroke %d"), Drawn, Mirrored);
				return Next(0.6);
			}
			case 6:
				Shot(TEXT("paint_02_strokes.png"));
				return Next(0.6);
			case 7:
				Studio->Undo();
				Test->TestEqual(TEXT("もどす takes the mirrored stroke back"), CountDrawn(Edit), Drawn);
				Studio->Redo();
				Test->TestEqual(TEXT("やりなおす puts it back"), CountDrawn(Edit), Mirrored);
				{
					const FVector2D C = Studio->GetModelCentreOnScreen();
					Studio->SetTool(UChaosImpactPaintStudio::ETool::Eraser);
					Studio->DrawLine(C + FVector2D(-80.0, -40.0), C + FVector2D(80.0, -40.0));
					Test->TestTrue(TEXT("The eraser rubbed out"), CountDrawn(Edit) < Mirrored);
					Studio->SetTool(UChaosImpactPaintStudio::ETool::Pen);
				}
				return Next(0.6);
			case 8:
				Shot(TEXT("paint_03_erased.png"));
				return Next(0.5);
			case 9:
				// A side view, close up.
				Studio->SetView(0.0f, 10.0f, 2.2f);
				return Next(0.8);
			case 10:
				Shot(TEXT("paint_04_side_zoom.png"));
				return Next(0.5);
			case 11:
				Studio->ResetView();
				{
					const FVector2D C = Studio->GetModelCentreOnScreen();
					const int32 Was = CountDrawn(Edit);
					Studio->SetColour(FColor(255, 210, 0));
					Studio->SetTool(UChaosImpactPaintStudio::ETool::Fill);
					Studio->ClickView(C + FVector2D(0.0, -150.0));
					Filled = CountDrawn(Edit);
					Test->TestTrue(TEXT("The bucket filled the blank surface"), Filled > Was + 20000);
					UE_LOG(LogTemp, Display, TEXT("PAINTQA fill: %d -> %d"), Was, Filled);
					Studio->SetTool(UChaosImpactPaintStudio::ETool::Pen);
				}
				return Next(0.8);
			case 12:
				Shot(TEXT("paint_05_fill.png"));
				return Next(0.5);
			case 13:
				Studio->Undo();
				Test->TestTrue(TEXT("もどす takes the fill back"), CountDrawn(Edit) < Filled);
				// From behind (seen in play), a stroke on the back, and the front's mirrored stroke seen from there.
				Studio->SetView(270.0f, 10.0f, 1.0f);
				return Next(0.6);
			case 14:
			{
				const FVector2D C = Studio->GetModelCentreOnScreen();
				Studio->SetColour(FColor(40, 220, 90));
				Studio->GetBrush().Radius = UChaosImpactPaintStudio::SizeToRadius(0.6f);
				Studio->DrawLine(C + FVector2D(-70.0, -60.0), C + FVector2D(70.0, 60.0));
				Studio->DrawLine(C + FVector2D(70.0, -60.0), C + FVector2D(-70.0, 60.0));
				return Next(0.6);
			}
			case 15:
				Shot(TEXT("paint_06_back.png"));
				return Next(0.5);
			case 16:
				Studio->HandleKeyDown(Key(EKeys::Escape));
				return Next(0.6);
			case 17:
				Shot(TEXT("paint_07_leave.png"));
				return Next(0.5);
			case 18:
				// 完了してもどる.
				Studio->HandleKeyDown(Key(EKeys::Enter));
				return Next(1.2);
			case 19:
				Test->TestFalse(TEXT("ペイント closed"), Edit->IsPainting());
				Test->TestTrue(TEXT("The pictures stay on the model"), Edit->HasPaint());
				Kept = CountDrawn(Edit);
				Shot(TEXT("paint_08_back_in_colouredit.png"));
				return Next(0.6);
			case 20:
				// Again, and thrown away this time.
				Edit->OpenPaint();
				return Next(1.2);
			case 21:
			{
				const FVector2D C = Studio->GetModelCentreOnScreen();
				Studio->SetColour(FColor(255, 255, 255));
				Studio->DrawLine(C + FVector2D(-90.0, 0.0), C + FVector2D(90.0, 0.0));
				Test->TestTrue(TEXT("Drawn again"), CountDrawn(Edit) != Kept);
				Studio->Finish(false);
				Test->TestEqual(TEXT("やめる puts the pictures back"), CountDrawn(Edit), Kept);
				return Next(0.8);
			}
			case 22:
			{
				// Packed (as a preset keeps them and the network sends them) and read back.
				TArray<FChaosImpactPaintLayerData> Data;
				for (int32 Index = 0; Index < Edit->GetPaintLayers().Num(); ++Index)
				{
					const UChaosImpactPaintLayer* Layer = Edit->GetPaintLayers()[Index];
					if (Layer && !Layer->IsBlank())
					{
						Data.Add({Index, Layer->ToPng()});
					}
				}
				const int32 Weight = ChaosImpactBodyPaint::WeightOf(Data);
				UE_LOG(LogTemp, Display, TEXT("PAINTQA %d layers drawn, %d bytes as PNG"), Data.Num(), Weight);
				Test->TestTrue(TEXT("The pictures fit what may be sent"), Weight > 0 && Weight <= ChaosImpactBodyPaint::MaxBytes);
				const TArray<uint8> Packed = ChaosImpactBodyPaint::Pack(Data);
				TArray<FChaosImpactPaintLayerData> Back;
				Test->TestTrue(TEXT("Packed pictures read back"), ChaosImpactBodyPaint::Unpack(Packed, Back) && Back.Num() == Data.Num());
				UChaosImpactPaintCache* Cache = UChaosImpactPaintCache::Get(PC);
				const int32 Key = Cache ? Cache->Register(Data) : 0;
				Test->TestTrue(TEXT("Kept by key"), Key != 0 && Key == ChaosImpactBodyPaint::KeyOf(Packed));
				// Pieces as the network sends them, into a fresh key's reception.
				if (Cache)
				{
					int32 Offset = 0, NextOffset = 0, Pieces = 0;
					bool bDone = false;
					TArray<uint8> Copy = Packed;
					Copy.Append(reinterpret_cast<const uint8*>("x"), 1);
					// (A changed copy: a different key, not yet here.)
					const int32 OtherKey = ChaosImpactBodyPaint::KeyOf(Copy);
					while (!bDone && Offset < Copy.Num() && Pieces < 200)
					{
						const TArray<uint8> Piece(Copy.GetData() + Offset, FMath::Min(UChaosImpactPaintCache::PieceBytes, Copy.Num() - Offset));
						bDone = Cache->Receive(OtherKey, Offset, Copy.Num(), Piece, NextOffset);
						Offset = NextOffset;
						++Pieces;
					}
					// The copy has a stray byte at the end: it unpacks still (the layers are all there), so it is kept.
					Test->TestTrue(TEXT("Pieces put back together"), bDone && Cache->Has(OtherKey));
					UE_LOG(LogTemp, Display, TEXT("PAINTQA sent in %d pieces"), Pieces);
				}
				PaintKey = Key;
				PC->CloseColourEdit();
				return Next(1.0);
			}
			case 23:
				PC->ShowMenuScreen(EChaosImpactScreen::Playing);
				State->PaintKey = PaintKey;
				return Next(2.5);
			case 24:
				Shot(TEXT("paint_09_in_play.png"));
				return Next(1.0);
			default:
				State->PaintKey = 0;
				PutRecentBack();
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

		TWeakObjectPtr<UChaosImpactColourSubsystem> Colours;
		TArray<FColor> RecentBefore;
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector StillHand = FVector::ZeroVector;
		FVector StillHead = FVector::ZeroVector;
		int32 Guard = 0;
		int32 Drawn = 0;
		int32 Mirrored = 0;
		int32 Filled = 0;
		int32 Kept = 0;
		int32 PaintKey = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintStudioTest, "ChaosImpact.Paint.Studio",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintStudioTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPaintStudioCommand(this));
	return true;
}

#endif
