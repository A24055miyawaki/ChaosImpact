#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCustomColour.h"
#include "ChaosImpactGallery.h"
#include "ChaosImpactMenuWidget.h"
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
	FKeyEvent GalleryKey(const FKey& Which)
	{
		return FKeyEvent(Which, FModifierKeysState(), 0, false, 0, 0);
	}

	/**
	 * みんなのカラー against a LOCAL test relay (started with -CIRelay=127.0.0.1:<port>, its gallery seeded with eleven
	 * posts by others): the online menu's card; the list (pages, 人気, a character), a work opened large with its paint and
	 * kept as a preset; one of this machine's presets (made here, with paint) posted from the post screen, seen under
	 * 自分の投稿 and deleted again. Every preset made here is removed at the end. Screenshots in Saved/GalleryQA.
	 */
	class FGalleryFlowCommand : public IAutomationLatentCommand
	{
	public:
		explicit FGalleryFlowCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactGallery* Gallery = Menu ? Menu->GetGallery() : nullptr;
			Colours = PC ? UChaosImpactColourSubsystem::Get(PC) : nullptr;
			if (Now - StartedAt > 240.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at stage %d"), Stage));
				CleanUp();
				return true;
			}
			if (!PC || !Menu || !Gallery || !Colours.IsValid() || !PC->GetPawn() || Now < NextAt)
			{
				return false;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GalleryQA"), Name), true, false);
			};
			const auto Next = [this, Now](const double Seconds) { ++Stage; NextAt = Now + Seconds; return false; };
			const auto Waiting = [&]() { return Gallery->IsLoading(); };
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				PC->ShowMenuScreen(EChaosImpactScreen::MultiReady);
				return Next(1.5);
			case 1:
				Test->TestEqual(TEXT("The online menu has its ten cards"), Menu->GetEntryCount(), 10);
				// 合言葉 -> right: みんなのカラー.
				Menu->Navigate(EKeys::Down);
				Menu->Navigate(EKeys::Down);
				Menu->Navigate(EKeys::Up);
				Menu->Navigate(EKeys::Right);
				Test->TestEqual(TEXT("Right of 合言葉 is みんなのカラー"), Menu->GetSelectedIndex(), 9);
				return Next(0.8);
			case 2:
				Shot(TEXT("gallery_00_online_menu.png"));
				return Next(0.5);
			case 3:
				Menu->ConfirmSelection();
				return Next(0.3);
			case 4:
				if (PC->GetCurrentScreen() != EChaosImpactScreen::Gallery)
				{
					PC->OpenGallery();
					Test->AddWarning(TEXT("Enter on the card did not open the gallery: opened directly"));
				}
				return Next(2.0);
			case 5:
				if (Waiting()) { return false; }
				Test->TestTrue(TEXT("The list came"), Gallery->GetError().IsEmpty());
				Test->TestEqual(TEXT("Eleven seeded works"), Gallery->GetTotal(), 11);
				Test->TestEqual(TEXT("Eight on the first page"), Gallery->GetPosts().Num(), 8);
				Test->TestEqual(TEXT("Two pages"), Gallery->GetPages(), 2);
				if (Gallery->GetPosts().Num() > 0)
				{
					Test->TestEqual(TEXT("Newest first"), Gallery->GetPosts()[0].Title, FString(TEXT("いちごミルク")));
				}
				// (Their paint and textures come in.)
				return Next(6.0);
			case 6:
				Shot(TEXT("gallery_01_list.png"));
				return Next(0.5);
			case 7:
				Gallery->HandleKeyDown(GalleryKey(EKeys::Right));
				Gallery->HandleKeyDown(GalleryKey(EKeys::Down));
				return Next(1.0);
			case 8:
				Shot(TEXT("gallery_02_list_cursor.png"));
				Gallery->TurnPage(1);
				return Next(1.0);
			case 9:
				if (Waiting()) { return false; }
				Test->TestEqual(TEXT("Page 2 has the other three"), Gallery->GetPosts().Num(), 3);
				return Next(3.0);
			case 10:
				Shot(TEXT("gallery_03_page2.png"));
				Gallery->SetTab(UChaosImpactGallery::ETab::Popular);
				return Next(1.0);
			case 11:
				if (Waiting()) { return false; }
				if (Gallery->GetPosts().Num() >= 3)
				{
					Test->TestEqual(TEXT("人気: most kept first"), Gallery->GetPosts()[0].Title, FString(TEXT("空色スマイル")));
					Test->TestEqual(TEXT("人気: its count"), Gallery->GetPosts()[0].Saves, 3);
				}
				return Next(3.0);
			case 12:
				Shot(TEXT("gallery_04_popular.png"));
				Gallery->SetCharacterFilter(2);
				return Next(1.0);
			case 13:
				if (Waiting()) { return false; }
				Test->TestEqual(TEXT("ドッジ妖精's works only"), Gallery->GetPosts().Num(), 2);
				for (const UChaosImpactGallery::FPost& Post : Gallery->GetPosts())
				{
					Test->TestEqual(TEXT("Each of that character"), Post.Character, 2);
				}
				return Next(3.0);
			case 14:
				Shot(TEXT("gallery_05_filtered.png"));
				// The painted one, opened.
				for (int32 Index = 0; Index < Gallery->GetPosts().Num(); ++Index)
				{
					if (Gallery->GetPosts()[Index].PaintBytes > 0)
					{
						Gallery->OpenDetail(Index);
					}
				}
				Test->TestTrue(TEXT("A work opened"), Gallery->GetView() == UChaosImpactGallery::EView::Detail);
				return Next(1.0);
			case 15:
				if (!Gallery->IsDetailPaintReady()) { return false; }
				return Next(2.5);
			case 16:
				Shot(TEXT("gallery_06_detail.png"));
				return Next(0.5);
			case 17:
			{
				BeforeSave = Colours->GetPresets(2).Num();
				Gallery->SaveToPresets();
				const TArray<FChaosImpactColourPreset>& Presets = Colours->GetPresets(2);
				if (BeforeSave < ChaosImpactCustomColour::MaxPresets)
				{
					Test->TestEqual(TEXT("Kept as a preset"), Presets.Num(), BeforeSave + 1);
					if (Presets.Num() == BeforeSave + 1)
					{
						bSavedOne = true;
						Test->TestEqual(TEXT("Named after the work"), Presets.Last().Name, FString(TEXT("空色スマイル")).Left(ChaosImpactCustomColour::MaxNameLength));
						Test->TestTrue(TEXT("With its paint"), !Presets.Last().Paint.IsEmpty());
						FColor Colour;
						Test->TestTrue(TEXT("With its colours"), ChaosImpactCustomColour::GetPart(Presets.Last().Parts, 0, Colour) && Colour == FColor(90, 200, 255));
					}
				}
				return Next(1.0);
			}
			case 18:
				Shot(TEXT("gallery_07_saved.png"));
				Gallery->HandleKeyDown(GalleryKey(EKeys::Escape));
				Test->TestTrue(TEXT("Back to the list"), Gallery->GetView() == UChaosImpactGallery::EView::List);
				Gallery->SetCharacterFilter(INDEX_NONE);
				Gallery->SetTab(UChaosImpactGallery::ETab::Popular);
				return Next(1.0);
			case 19:
				if (Waiting()) { return false; }
				if (Gallery->GetPosts().Num() > 0)
				{
					Test->TestEqual(TEXT("The keep counted on the relay"), Gallery->GetPosts()[0].Saves, 4);
					Test->TestTrue(TEXT("And marked as kept"), Gallery->GetPosts()[0].bSaved);
				}
				MakeOwnPreset();
				Gallery->OpenPost();
				return Next(1.5);
			case 20:
			{
				int32 Mine = INDEX_NONE;
				for (int32 Choice = 0; Choice < Gallery->GetPostChoiceCount(); ++Choice)
				{
					Gallery->ChoosePost(Choice);
					if (Gallery->GetPostTitle() == TEXT("テストQA"))
					{
						Mine = Choice;
					}
				}
				Test->TestTrue(TEXT("The preset made here is offered"), Mine != INDEX_NONE);
				Gallery->ChoosePost(FMath::Max(Mine, 0));
				Gallery->SetPostTitle(TEXT("ぼくのさくひん"));
				return Next(2.0);
			}
			case 21:
				Shot(TEXT("gallery_08_post.png"));
				Gallery->SubmitPost();
				return Next(0.2);
			case 22:
				Shot(TEXT("gallery_09_posting.png"));
				return Next(0.1);
			case 23:
				if (Gallery->IsPosting()) { return false; }
				Test->TestTrue(TEXT("Posted"), Gallery->GetNotice().Contains(TEXT("投稿しました")));
				Test->TestTrue(TEXT("Shown under 自分の投稿"), Gallery->GetTab() == UChaosImpactGallery::ETab::Mine);
				return Next(1.0);
			case 24:
				if (Waiting()) { return false; }
				Test->TestEqual(TEXT("One of mine"), Gallery->GetPosts().Num(), 1);
				if (Gallery->GetPosts().Num() == 1)
				{
					Test->TestEqual(TEXT("Its name"), Gallery->GetPosts()[0].Title, FString(TEXT("ぼくのさくひん")));
					Test->TestTrue(TEXT("Its paint went up"), Gallery->GetPosts()[0].PaintBytes > 0);
					Test->TestTrue(TEXT("Marked as mine"), Gallery->GetPosts()[0].bMine);
				}
				return Next(4.0);
			case 25:
				Shot(TEXT("gallery_10_mine.png"));
				Gallery->OpenDetail(0);
				return Next(0.5);
			case 26:
				if (!Gallery->IsDetailPaintReady()) { return false; }
				return Next(2.0);
			case 27:
				Shot(TEXT("gallery_11_mine_detail.png"));
				// 投稿を消す (twice: the first asks).
				Gallery->HandleKeyDown(GalleryKey(EKeys::Right));
				Gallery->HandleKeyDown(GalleryKey(EKeys::Enter));
				Gallery->HandleKeyDown(GalleryKey(EKeys::Enter));
				return Next(1.5);
			case 28:
				if (Waiting()) { return false; }
				Test->TestEqual(TEXT("Deleted"), Gallery->GetPosts().Num(), 0);
				return Next(1.0);
			case 29:
				Shot(TEXT("gallery_12_mine_empty.png"));
				Gallery->HandleKeyDown(GalleryKey(EKeys::Escape));
				return Next(1.0);
			case 30:
				Test->TestTrue(TEXT("Back to the online menu"), PC->GetCurrentScreen() == EChaosImpactScreen::MultiReady);
				return Next(0.5);
			default:
				CleanUp();
				return true;
			}
		}

	private:
		/** One of this machine's presets, with paint, for posting (removed again at the end). */
		void MakeOwnPreset()
		{
			for (int32 Character = 0; Character < ChaosImpactRoster::Num(); ++Character)
			{
				if (Colours->GetPresets(Character).Num() < ChaosImpactCustomColour::MaxPresets)
				{
					OwnCharacter = Character;
					break;
				}
			}
			FChaosImpactColourPreset Preset;
			Preset.Name = TEXT("テストQA");
			ChaosImpactCustomColour::SetPart(Preset.Parts, 0, true, FColor(250, 120, 0));
			ChaosImpactCustomColour::SetPart(Preset.Parts, 1, true, FColor(30, 30, 200));
			UChaosImpactPaintLayer* Layer = NewObject<UChaosImpactPaintLayer>();
			Layer->Init();
			TArray<FColor>& Pixels = Layer->GetPixels();
			for (int32 Y = 0; Y < ChaosImpactBodyPaint::Size; ++Y)
			{
				for (int32 X = 0; X < ChaosImpactBodyPaint::Size; ++X)
				{
					if (((X / 40) + (Y / 40)) % 2 == 0)
					{
						Pixels[Y * ChaosImpactBodyPaint::Size + X] = FColor(255, 255, 255, 255);
					}
				}
			}
			Preset.Paint.Add({0, Layer->ToPng()});
			OwnSlot = Colours->SetPreset(OwnCharacter, INDEX_NONE, Preset);
			Test->TestTrue(TEXT("A preset made for posting"), OwnSlot != INDEX_NONE);
		}

		void CleanUp()
		{
			if (!Colours.IsValid())
			{
				return;
			}
			if (OwnSlot != INDEX_NONE && Colours->GetPresets(OwnCharacter).IsValidIndex(OwnSlot)
				&& Colours->GetPresets(OwnCharacter)[OwnSlot].Name == TEXT("テストQA"))
			{
				Colours->RemovePreset(OwnCharacter, OwnSlot);
			}
			OwnSlot = INDEX_NONE;
			if (bSavedOne && Colours->GetPresets(2).Num() == BeforeSave + 1)
			{
				Colours->RemovePreset(2, BeforeSave);
			}
			bSavedOne = false;
		}

		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<UChaosImpactColourSubsystem> Colours;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 BeforeSave = 0;
		bool bSavedOne = false;
		int32 OwnCharacter = 0;
		int32 OwnSlot = INDEX_NONE;
	};

	/** みんなのカラー with no relay answering (or one too old to know it): the list says so and nothing breaks. */
	class FGalleryOfflineCommand : public IAutomationLatentCommand
	{
	public:
		explicit FGalleryOfflineCommand(FAutomationTestBase* InTest) : Test(InTest) {}

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
			UChaosImpactGallery* Gallery = PC && PC->GetMenuWidget() ? PC->GetMenuWidget()->GetGallery() : nullptr;
			if (Now - StartedAt > 60.0)
			{
				Test->AddError(TEXT("Timed out"));
				return true;
			}
			if (!PC || !Gallery || !PC->GetPawn() || Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 4.0) { return false; }
				PC->OpenGallery();
				++Stage;
				NextAt = Now + 1.0;
				return false;
			case 1:
				if (Gallery->IsLoading()) { return false; }
				Test->TestFalse(TEXT("It says what is wrong"), Gallery->GetError().IsEmpty());
				UE_LOG(LogTemp, Display, TEXT("GALLERYQA offline message: %s"), *Gallery->GetError());
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GalleryQA"),
					FString::Printf(TEXT("gallery_offline_%s.png"), Gallery->GetError().Contains(TEXT("古い")) ? TEXT("old") : TEXT("down"))), true, false);
				// The post screen still opens (and says there is nothing / lets the presets be chosen) without crashing.
				Gallery->OpenPost();
				++Stage;
				NextAt = Now + 1.0;
				return false;
			default:
				Gallery->HandleKeyDown(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
				Gallery->HandleKeyDown(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactGalleryFlowTest, "ChaosImpact.Gallery.Flow",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactGalleryFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGalleryFlowCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactGalleryOfflineTest, "ChaosImpact.Gallery.Offline",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactGalleryOfflineTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGalleryOfflineCommand(this));
	return true;
}

#endif
