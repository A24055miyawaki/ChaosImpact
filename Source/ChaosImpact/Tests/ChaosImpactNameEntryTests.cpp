#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactNameEntry.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactSettingsScreen.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	FString CursorLabel(const FChaosImpactNameEntry& Entry)
	{
		const FChaosImpactNameEntry::FCell* Cell = Entry.GetCursorCell();
		return Cell ? Cell->Label : FString();
	}

	void Clear(FChaosImpactNameEntry& Entry)
	{
		for (int32 Index = 0; Index < 20 && !Entry.GetText().IsEmpty(); ++Index)
		{
			Entry.HandleKey(EKeys::BackSpace, false);
		}
	}

	/**
	 * 名前入力 (the settings' ＋ ニックネームを作る): the keys laid out as a phone's (五十音; QWERTY staggered, the numbers on
	 * top, ⇧ once / locked, ⌫ beside m; 記号), the cursor going between the rows to the nearest key, Space typing a
	 * space, Enter pressing the key the arrows moved to, full-width letters typed straight in, and the system's IME:
	 * nothing else taken while it converts, its decided letters written (held to the name's length), the Enter that
	 * decided them not finishing the name. Left with Escape: no nickname is made. Saved/SettingsQA/name_*.png.
	 */
	class FNameEntryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNameEntryCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 90.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at stage %d."), Stage));
				return true;
			}
			AChaosImpactPlayerController* PC = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController());
				}
			}
			UChaosImpactMenuWidget* Menu = PC ? PC->GetMenuWidget() : nullptr;
			UChaosImpactSettingsScreen* Settings = Menu ? Menu->GetSettingsScreen() : nullptr;
			if (!PC || !Settings || Now < NextAt)
			{
				return false;
			}
			const auto Next = [this, Now](const double Seconds) { ++Stage; NextAt = Now + Seconds; return false; };
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SettingsQA"), Name), true, false);
			};
			FChaosImpactNameEntry& Entry = Settings->GetNameEntry();
			using FEntry = FChaosImpactNameEntry;
			switch (Stage)
			{
			case 0:
				if (Now - StartedAt < 3.0) { return false; }
				ProfilesBefore = ChaosImpactSettings::GetProfileCount();
				PC->ShowMenuScreen(EChaosImpactScreen::Settings);
				return Next(0.8);
			case 1:
			{
				// 操作 → ＋ ニックネームを作る.
				for (int32 Tab = 0; Tab < 4 && Settings->FindRow(UChaosImpactSettingsScreen::RowNewProfile) == INDEX_NONE; ++Tab)
				{
					FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::E, FModifierKeysState(), 0, false, 0, 0));
					FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::E, FModifierKeysState(), 0, false, 0, 0));
				}
				Settings->SelectRow(Settings->FindRow(UChaosImpactSettingsScreen::RowNewProfile));
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
				if (!Entry.IsOpen())
				{
					Test->AddError(TEXT("The name entry did not open"));
					return true;
				}
				Clear(Entry);
				Test->TestEqual(TEXT("It opens on ひらがな"), Entry.GetPage(), FEntry::KanaPage);
				Test->TestEqual(TEXT("…on あ"), CursorLabel(Entry), FString(TEXT("あ")));
				// 五十音: か, then ゛ (round the row to its end).
				Entry.MoveCursor(1, 0);
				Entry.PressCell();
				Entry.MoveCursor(-2, 0);
				Entry.PressCell();
				Test->TestEqual(TEXT("か and ゛ make が"), Entry.GetText(), FString(TEXT("が")));
				// Down from や past the empty places: ゆ.
				Entry.PointAt(TEXT("や"));
				Entry.MoveCursor(0, 1);
				Test->TestEqual(TEXT("Down from や skips the empty place to ゆ"), CursorLabel(Entry), FString(TEXT("ゆ")));
				Entry.SetPage(FEntry::KatakanaPage);
				Entry.PointAt(TEXT("か"));
				Entry.PressCell();
				Test->TestEqual(TEXT("カタカナ"), Entry.GetText(), FString(TEXT("がカ")));
				Entry.SetPage(FEntry::KanaPage);
				Entry.PointAt(TEXT("あ"));
				Shot(TEXT("name_01_kana.png"));
				return Next(0.6);
			}
			case 2:
			{
				Clear(Entry);
				Entry.SetPage(FEntry::LatinPage);
				// QWERTY: q, ⇧ once for W, then back to small.
				Test->TestTrue(TEXT("q is on the 英字 page"), Entry.PointAt(TEXT("q")));
				Entry.PressCell();
				Entry.PointAt(TEXT("⇧"));
				Entry.PressCell();
				Test->TestEqual(TEXT("⇧ once"), Entry.GetShift(), 1);
				Entry.PointAt(TEXT("w"));
				Entry.PressCell();
				Test->TestEqual(TEXT("⇧ makes one capital, then lets go"), Entry.GetText(), FString(TEXT("qW")));
				Test->TestEqual(TEXT("…and is off again"), Entry.GetShift(), 0);
				// Locked: two capitals.
				Entry.PointAt(TEXT("⇧"));
				Entry.PressCell();
				Entry.PressCell();
				Test->TestEqual(TEXT("⇧ twice locks"), Entry.GetShift(), 2);
				Entry.PointAt(TEXT("e"));
				Entry.PressCell();
				Entry.PressCell();
				Test->TestEqual(TEXT("Locked: capitals go on"), Entry.GetText(), FString(TEXT("qWEE")));
				// The rows are staggered: down from q is a, down from a is ⇧, right of ⇧ is z.
				Entry.PointAt(TEXT("q"));
				Entry.MoveCursor(0, 1);
				Test->TestEqual(TEXT("Down from q: a"), CursorLabel(Entry), FString(TEXT("a")));
				Entry.MoveCursor(0, 1);
				Test->TestEqual(TEXT("Down from a: ⇧"), CursorLabel(Entry), FString(TEXT("⇧")));
				Entry.MoveCursor(1, 0);
				Test->TestEqual(TEXT("Right of ⇧: z"), CursorLabel(Entry), FString(TEXT("z")));
				Entry.MoveCursor(0, -1);
				Entry.MoveCursor(0, -1);
				Entry.MoveCursor(0, -1);
				Test->TestEqual(TEXT("Up from z to the numbers"), CursorLabel(Entry).Len(), 1);
				Test->TestTrue(TEXT("…a number"), FChar::IsDigit(CursorLabel(Entry)[0]));
				// ⌫ beside m.
				Entry.PointAt(TEXT("⌫"));
				Entry.PressCell();
				Test->TestEqual(TEXT("⌫ rubs one out"), Entry.GetText(), FString(TEXT("qWE")));
				// Space types a space; Enter after the arrows presses the key under the cursor.
				Entry.HandleCharacter(TEXT(' '));
				Test->TestEqual(TEXT("Space types a space"), Entry.GetText(), FString(TEXT("qWE ")));
				Entry.PointAt(TEXT("m"));
				Entry.HandleKey(EKeys::Left, false);
				Entry.HandleKey(EKeys::Enter, false);
				Test->TestTrue(TEXT("Enter after the arrows writes that key (n, capital while locked)"),
					Entry.IsOpen() && Entry.GetText() == TEXT("qWE N"));
				// Full-width straight from the keyboard.
				Entry.HandleCharacter(TEXT('Ａ'));
				Test->TestEqual(TEXT("A full-width letter types straight in"), Entry.GetText(), FString(TEXT("qWE NＡ")));
				Entry.PointAt(TEXT("R"));
				Shot(TEXT("name_02_latin.png"));
				return Next(0.6);
			}
			case 3:
			{
				// The IME converting かんじ: nothing else is taken meanwhile; then 漢字 decided.
				Clear(Entry);
				Entry.HandleCharacter(TEXT('Q'));
				const bool bIme = Entry.ImeComposeForTests(TEXT("かんじ"));
				Test->TestTrue(TEXT("The game window has the system's IME"), bIme);
				if (!bIme)
				{
					return Next(0.1);
				}
				Test->TestTrue(TEXT("Converting"), Entry.IsComposing());
				Test->TestEqual(TEXT("The letters being converted are in the name"), Entry.GetText(), FString(TEXT("Qかんじ")));
				Entry.HandleKey(EKeys::Enter, false);
				Entry.HandleCharacter(TEXT('z'));
				Entry.HandleKey(EKeys::BackSpace, false);
				Test->TestTrue(TEXT("Keys while converting are the IME's"), Entry.IsOpen() && Entry.GetText() == TEXT("Qかんじ"));
				Shot(TEXT("name_03_ime.png"));
				return Next(0.6);
			}
			case 4:
			{
				if (Entry.IsComposing())
				{
					Entry.ImeDecideForTests(TEXT("漢字"));
					Test->TestEqual(TEXT("The decided letters are written"), Entry.GetText(), FString(TEXT("Q漢字")));
					Test->TestFalse(TEXT("…and converting is over"), Entry.IsComposing());
					Entry.HandleKey(EKeys::Enter, false);
					Test->TestTrue(TEXT("The Enter that decided them does not finish the name"), Entry.IsOpen());
					// Too long a word: held to the name's length.
					Entry.ImeComposeForTests(TEXT("ちょうちょうちょう"));
					Entry.ImeDecideForTests(TEXT("超超超超超超超超"));
					Test->TestEqual(TEXT("The IME's letters are held to the length"), Entry.GetText().Len(), ChaosImpactSettings::MaxNameLength);
				}
				Entry.SetPage(FEntry::SymbolPage);
				Entry.PointAt(TEXT("☆"));
				Shot(TEXT("name_04_symbols.png"));
				return Next(0.6);
			}
			case 5:
				// Escape (not just after the IME): left, nothing made.
				Entry.HandleKey(EKeys::Escape, false);
				Test->TestFalse(TEXT("Escape leaves"), Entry.IsOpen());
				return Next(0.4);
			default:
				Test->TestEqual(TEXT("No nickname was made"), ChaosImpactSettings::GetProfileCount(), ProfilesBefore);
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 ProfilesBefore = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNameEntryTest, "ChaosImpact.UI.NameEntry",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactNameEntryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNameEntryCommand(this));
	return true;
}

#endif
