#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactControlPreview.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "ChaosImpactHints.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "Input/Events.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	using ERowKind = UChaosImpactSettingsScreen::ERowKind;
	using FRow = UChaosImpactSettingsScreen::FRow;

	const TCHAR* const TabNames[] = {TEXT("画面"), TEXT("画質"), TEXT("操作"), TEXT("サウンド・その他")};
	const TCHAR* const QualityNames[] = {TEXT("低"), TEXT("中"), TEXT("高"), TEXT("最高")};
	const int32 FrameRates[] = {30, 60, 120, 144, 165, 240, 0};

	/**
	 * Where everything is, in the design space. The PC: 1600 x 900, the tabs down the left, the tab's panel on the right
	 * (its rows, then what the chosen row does). スマホ版 (2026-11-17): sideways the same plan with fewer, taller rows and
	 * bigger words; upright (900 x 1600) the tabs as four chips across the top, the panel under them, 保存 and もどる side by
	 * side at the foot. No keys named by touch; every word fitted to its room.
	 */
	struct FSettingsLayout
	{
		bool bPhone = false;
		bool bUpright = false;
		FVector2D Canvas = FVector2D(1600.0, 900.0);
		float TitleX = 60.0f, TitleY = 54.0f, TitleSize = 54.0f, TitleRule = 128.0f;
		float RowsLeft = 440.0f, RowsWidth = 1060.0f, RowsTop = 198.0f, RowHeight = 52.0f, RowStep = 56.0f;
		int32 VisibleRows = 9;
		float CellLeft = 930.0f, CellWidth = 262.0f, CellGap = 16.0f;
		float PanelLeft = 410.0f, PanelTop = 120.0f, PanelWidth = 1130.0f, PanelHeight = 740.0f;
		/** The strip saying what the chosen row does (from the panel's foot); its lines. */
		float HelpHeight = 50.0f, HelpBottom = 54.0f;
		int32 HelpLines = 1;
		FBox2D Back = FBox2D(FVector2D(60.0f, 796.0f), FVector2D(380.0f, 856.0f));
		/** 保存, just above もどる (upright: beside it). */
		FBox2D Save = FBox2D(FVector2D(60.0f, 724.0f), FVector2D(380.0f, 784.0f));
		/** The question whether to save: its box (its three answers side by side; upright, one above another). */
		FBox2D Ask = FBox2D(FVector2D(400.0f, 290.0f), FVector2D(1200.0f, 620.0f));
		/** The device cards (操作's second step). */
		float CardWidth = 240.0f, CardHeight = 310.0f, CardTop = 214.0f;
		/** Words: the rows' names, an info row's, values, headings, small tags; the tabs and their notes; the buttons; help. */
		float Label = 21.0f, Info = 17.0f, Value = 21.0f, Heading = 18.0f, Small = 15.0f;
		float TabName = 24.0f, TabNote = 13.0f, Button = 24.0f, Help = 18.0f, PanelTitle = 32.0f, PanelNote = 21.0f;
		/** ◀ ▶ round a value: drawn as buttons this wide on a phone (0: plain arrows). */
		float ArrowButton = 0.0f;
	};

	FSettingsLayout MakePhoneLayout(const bool bUpright)
	{
		FSettingsLayout L;
		L.bPhone = true;
		L.bUpright = bUpright;
		L.Label = 30.0f;
		L.Info = 24.0f;
		L.Value = 30.0f;
		L.Heading = 24.0f;
		L.Small = 20.0f;
		L.TabName = bUpright ? 30.0f : 32.0f;
		L.TabNote = 18.0f;
		L.Button = 32.0f;
		L.Help = bUpright ? 24.0f : 22.0f;
		L.PanelTitle = 36.0f;
		L.PanelNote = 24.0f;
		L.HelpLines = bUpright ? 3 : 2;
		L.HelpBottom = 20.0f;
		if (!bUpright)
		{
			L.TitleX = 40.0f; L.TitleY = 24.0f; L.TitleSize = 50.0f; L.TitleRule = 96.0f;
			L.PanelLeft = 430.0f; L.PanelTop = 30.0f; L.PanelWidth = 1140.0f; L.PanelHeight = 840.0f;
			L.RowsLeft = 452.0f; L.RowsWidth = 1080.0f; L.RowsTop = 122.0f; L.RowHeight = 92.0f; L.RowStep = 102.0f; L.VisibleRows = 6;
			L.CellWidth = 250.0f; L.CellGap = 16.0f;
			L.HelpHeight = 80.0f;
			L.Save = FBox2D(FVector2D(40.0f, 624.0f), FVector2D(400.0f, 724.0f));
			L.Back = FBox2D(FVector2D(40.0f, 748.0f), FVector2D(400.0f, 848.0f));
			L.Ask = FBox2D(FVector2D(250.0f, 220.0f), FVector2D(1350.0f, 680.0f));
			L.CardWidth = 240.0f; L.CardHeight = 300.0f; L.CardTop = 134.0f;
			L.ArrowButton = 66.0f;
		}
		else
		{
			L.Canvas = FVector2D(900.0, 1600.0);
			L.TitleX = 40.0f; L.TitleY = 56.0f; L.TitleSize = 60.0f; L.TitleRule = 136.0f;
			L.PanelLeft = 30.0f; L.PanelTop = 300.0f; L.PanelWidth = 840.0f; L.PanelHeight = 1110.0f;
			L.RowsLeft = 48.0f; L.RowsWidth = 796.0f; L.RowsTop = 392.0f; L.RowHeight = 96.0f; L.RowStep = 106.0f; L.VisibleRows = 8;
			L.CellWidth = 182.0f; L.CellGap = 14.0f;
			L.HelpHeight = 116.0f;
			L.Save = FBox2D(FVector2D(30.0f, 1440.0f), FVector2D(440.0f, 1550.0f));
			L.Back = FBox2D(FVector2D(460.0f, 1440.0f), FVector2D(870.0f, 1550.0f));
			L.Ask = FBox2D(FVector2D(40.0f, 480.0f), FVector2D(860.0f, 1120.0f));
			L.CardWidth = (L.RowsWidth - 22.0f) * 0.5f; L.CardHeight = 260.0f; L.CardTop = L.RowsTop + 8.0f;
			L.ArrowButton = 62.0f;
		}
		L.CellLeft = L.RowsLeft + L.RowsWidth - (L.CellWidth * 2.0f + L.CellGap);
		return L;
	}

	const FSettingsLayout& Layout()
	{
		static const FSettingsLayout Pc;
		static const FSettingsLayout Sideways = MakePhoneLayout(false);
		static const FSettingsLayout Upright = MakePhoneLayout(true);
		return !UChaosImpactMenuWidget::IsPhoneBuild() ? Pc : UChaosImpactMenuWidget::IsUprightCanvas() ? Upright : Sideways;
	}

	/**
	 * A line too long for its room, in as many lines as it needs (at most MaxLines, the last then fitted): each line as full
	 * as it can be, broken after a 、。・）： or before a （ when one is near its end.
	 */
	TArray<FString> WrapLines(const FString& Text, const float Size, const float Width, const int32 MaxLines)
	{
		TArray<FString> Lines;
		FString Rest = Text;
		while (!Rest.IsEmpty())
		{
			if (Lines.Num() == MaxLines - 1 || ChaosImpactPaint::HudMeasure(Rest, Size, TEXT("Bold")) <= Width)
			{
				Lines.Add(Rest);
				break;
			}
			// How much fits, then a nicer break a little before that.
			int32 Fit = 1;
			while (Fit < Rest.Len() && ChaosImpactPaint::HudMeasure(Rest.Left(Fit + 1), Size, TEXT("Bold")) <= Width)
			{
				++Fit;
			}
			int32 Break = Fit;
			for (int32 Index = Fit; Index > FMath::Max(1, Fit - 8); --Index)
			{
				const TCHAR Before = Rest[Index - 1];
				const TCHAR After = Index < Rest.Len() ? Rest[Index] : TEXT(' ');
				if (Before == TEXT('、') || Before == TEXT('。') || Before == TEXT('・') || Before == TEXT('）') || Before == TEXT('：')
					|| Before == TEXT(' ') || After == TEXT('（'))
				{
					Break = Index;
					break;
				}
			}
			Lines.Add(Rest.Left(Break));
			Rest = Rest.Mid(Break);
		}
		return Lines;
	}
	const TCHAR* const AskAnswers[] = {TEXT("保存してもどる"), TEXT("保存しないでもどる"), TEXT("キャンセル")};
	/** The control style picker keeps its own back button where it draws it. */
	const FBox2D PickerBackButton(FVector2D(60.0f, 812.0f), FVector2D(300.0f, 866.0f));
	const FBox2D ApplyControlButton(FVector2D(1220.0f, 812.0f), FVector2D(1540.0f, 866.0f));
	/** The style cards down the left of the picker. */
	FBox2D ControlCard(const int32 Index)
	{
		return FBox2D(FVector2D(60.0f, 140.0f + Index * 156.0f), FVector2D(600.0f, 280.0f + Index * 156.0f));
	}


	FString OnOff(const bool bOn)
	{
		return bOn ? TEXT("ON") : TEXT("OFF");
	}

	FRow MakeRow(const ERowKind Kind, const int32 Id, const FString& Label, const FString& Value = FString(),
		const bool bDisabled = false)
	{
		FRow Row;
		Row.Kind = Kind;
		Row.Id = Id;
		Row.Label = Label;
		Row.Value = Value;
		Row.bDisabled = bDisabled;
		return Row;
	}

	int32 Wrap(const int32 Value, const int32 Count)
	{
		return Count <= 0 ? 0 : ((Value % Count) + Count) % Count;
	}
}

void UChaosImpactSettingsScreen::Open(AChaosImpactPlayerController* InController)
{
	Controller = InController;
	bOpen = true;
	bControlStylePickerOpen = false;
	bWaitingForKey = false;
	bDeleteArmed = false;
	bAskSave = false;
	bUnsaved = false;
	bPressing = false;
	bDragging = false;
	NameEntry.Close();
	Message.Reset();
	// What everything is now: kept unless saved over (see Save / Leave).
	ChaosImpactSettings::BeginSettingsEdit();
	OpenedAt = FPlatformTime::Seconds();
	TabChangedAt = OpenedAt;
	// The controls tab starts on what P1 plays with: their nickname, and their device.
	EditedProfile = ChaosImpactSettings::GetPlayerProfile(InController);
	EditedDevice = ChaosImpactSettings::GetPlayerDevice(InController);
	// The name entry's help names the keys the hints here do (see HintDevice).
	NameEntry.SetHintPlayer(InController && InController->FollowsLastDevice() ? InController : nullptr);
	SetTab(ETab::Display);
	bSideFocus = true;
	SideCursor = 0;
#if !UE_BUILD_SHIPPING
	// Development (-CIDevSettingsTab=<n>): opens on that tab (screenshots of it).
	int32 DevTab = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIDevSettingsTab="), DevTab))
	{
		SetTab(static_cast<ETab>(DevTab));
	}
	// -CIDevStylePicker: straight to the control styles (screenshots of it).
	if (FParse::Param(FCommandLine::Get(), TEXT("CIDevStylePicker")))
	{
		SetTab(ETab::Controls);
		if (FParse::Param(FCommandLine::Get(), TEXT("CIDevStylePad")))
		{
			EditedDevice = EChaosImpactDevice::Xbox;
			ChaosImpactHints::SetLastDeviceForTests(EChaosImpactDevice::Xbox);
			BuildRows();
		}
		OpenControlStylePicker();
	}
#endif
	UE_LOG(LogChaosImpact, Log, TEXT("Settings opened"));
}

void UChaosImpactSettingsScreen::Close()
{
	if (bOpen)
	{
		// Closed from outside without an answer (the match moved on, the level changed): what was not saved is not kept.
		// (Not while the game itself is closing: the window is going.)
		if (!IsEngineExitRequested())
		{
			ChaosImpactSettings::RevertSettingsEdit();
		}
		ChaosImpactSettings::EndSettingsEdit();
	}
	bAskSave = false;
	bUnsaved = false;
	if (ControlVideoPlayer) { ControlVideoPlayer->Close(); }
	OpenVideoId = INDEX_NONE;
	bOpen = false;
	bControlStylePickerOpen = false;
	bWaitingForKey = false;
	NameEntry.Close();
}

void UChaosImpactSettingsScreen::SetTab(const ETab NewTab)
{
	if (bOpen && static_cast<ETab>(Wrap(static_cast<int32>(NewTab), static_cast<int32>(ETab::Count))) != Tab)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiTab);
	}
	Tab = static_cast<ETab>(Wrap(static_cast<int32>(NewTab), static_cast<int32>(ETab::Count)));
	SideCursor = static_cast<int32>(Tab);
	ControlsStep = EControlsStep::Nicknames;
	bControlStylePickerOpen = false;
	TabChangedAt = FPlatformTime::Seconds();
	bDeleteArmed = false;
	BuildRows();
	SelectedRow = 0;
	SelectedCell = 0;
	FirstVisibleRow = 0;
	ScrollFrac = 0.0f;
	ScrollVelocity = 0.0f;
	// (Its first row picked, the cursor staying where it is: in the tab column, or in the settings.)
	const bool bWasSide = bSideFocus;
	if (!IsSelectable(SelectedRow))
	{
		MoveSelection(1);
	}
	bSideFocus = bWasSide;
}

void UChaosImpactSettingsScreen::EditControls(const int32 Profile, const EChaosImpactDevice Device)
{
	bControlStylePickerOpen = false;
	EditedProfile = FMath::Clamp(Profile, 0, ChaosImpactSettings::GetProfileCount() - 1);
	EditedDevice = Device;
	ControlsStep = EControlsStep::Buttons;
	BuildRows();
}

void UChaosImpactSettingsScreen::OpenControlStylePicker()
{
	if (!bOpen || Tab != ETab::Controls) { return; }
	bControlStylePickerOpen = true;
	PreviewControlScheme = ChaosImpactSettings::GetControlScheme(EditedProfile,EditedDevice);
	PreviewStartedAt = FPlatformTime::Seconds();
	UpdateControlVideo();
	ChaosImpactSfx::Play2D(Controller.Get(),EChaosImpactSfx::UiOpen);
}

void UChaosImpactSettingsScreen::MoveControlPreview(int32 Direction)
{
	if (!Direction) { return; }
	PreviewControlScheme = static_cast<EChaosImpactControlScheme>(Wrap(static_cast<int32>(PreviewControlScheme)
		+ (Direction < 0 ? -1 : 1),EditedDevice == EChaosImpactDevice::KeyboardMouse ? 4 : 2));
	PreviewStartedAt = FPlatformTime::Seconds();
	UpdateControlVideo();
	ChaosImpactSfx::Play2D(Controller.Get(),EChaosImpactSfx::UiMove);
}

void UChaosImpactSettingsScreen::BeginDestroy()
{
	if (ControlVideoPlayer) { ControlVideoPlayer->Close(); }
	Super::BeginDestroy();
}

bool UChaosImpactSettingsScreen::HasUnsavedChanges() const
{
	return bOpen && ChaosImpactSettings::HasUnsavedSettings();
}

void UChaosImpactSettingsScreen::Save()
{
	if (!bOpen)
	{
		return;
	}
	if (!HasUnsavedChanges())
	{
		ShowMessage(TEXT("変更はありません"));
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	ChaosImpactSettings::SaveSettingsEdit();
	bUnsaved = false;
	ShowMessage(TEXT("設定を保存しました"));
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
}

void UChaosImpactSettingsScreen::AnswerSave(const ESaveAnswer Answer)
{
	if (!bAskSave)
	{
		return;
	}
	bAskSave = false;
	switch (Answer)
	{
	case ESaveAnswer::SaveAndLeave:
		ChaosImpactSettings::SaveSettingsEdit();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
		LeaveNow();
		break;
	case ESaveAnswer::LeaveWithoutSaving:
		ChaosImpactSettings::RevertSettingsEdit();
		ChaosImpactSettings::BeginSettingsEdit();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		LeaveNow();
		break;
	default:
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		break;
	}
}

FBox2D UChaosImpactSettingsScreen::AskButtonRect(const int32 Index) const
{
	const FSettingsLayout& L = Layout();
	if (L.bUpright)
	{
		// Upright: one above another, the width of the box.
		const float Top = static_cast<float>(L.Ask.Min.Y) + 240.0f + Index * 120.0f;
		return FBox2D(FVector2D(L.Ask.Min.X + 40.0f, Top), FVector2D(L.Ask.Max.X - 40.0f, Top + 100.0f));
	}
	const float Width = L.bPhone ? 330.0f : 236.0f;
	constexpr float Gap = 20.0f;
	const float Left = static_cast<float>(L.Ask.GetCenter().X) - (Width * 3.0f + Gap * 2.0f) * 0.5f + Index * (Width + Gap);
	const float Top = L.bPhone ? static_cast<float>(L.Ask.Min.Y) + 280.0f : 476.0f;
	return FBox2D(FVector2D(Left, Top), FVector2D(Left + Width, Top + (L.bPhone ? 110.0f : 64.0f)));
}

bool UChaosImpactSettingsScreen::IsControlVideoPlaying() const
{
	const int32 Desired = EditedDevice == EChaosImpactDevice::KeyboardMouse
		? static_cast<int32>(PreviewControlScheme) : 4 + static_cast<int32>(PreviewControlScheme);
	return bControlStylePickerOpen && OpenVideoId == Desired && ControlVideoPlayer && ControlVideoPlayer->IsPlaying();
}

double UChaosImpactSettingsScreen::GetControlVideoTime() const
{
	return ControlVideoPlayer ? ControlVideoPlayer->GetTime().GetTotalSeconds() : 0;
}

void UChaosImpactSettingsScreen::UpdateControlVideo()
{
	if (!bOpen || !bControlStylePickerOpen)
	{
		if (OpenVideoId != INDEX_NONE && ControlVideoPlayer) { ControlVideoPlayer->Close(); }
		OpenVideoId = INDEX_NONE;
		return;
	}
	const bool bPad = EditedDevice != EChaosImpactDevice::KeyboardMouse;
	const int32 Id = bPad ? 4 + static_cast<int32>(PreviewControlScheme) : static_cast<int32>(PreviewControlScheme);
	if (OpenVideoId == Id) { return; }
	// A stick tap or crossing several cards must not open/close multiple asynchronous WMF topologies at once.
	if (FPlatformTime::Seconds() - PreviewStartedAt < 0.18) { return; }
	if (!ControlVideoPlayer)
	{
		ControlVideoPlayer = NewObject<UMediaPlayer>(this);
		ControlVideoPlayer->PlayOnOpen = true;
		ControlVideoTexture = NewObject<UMediaTexture>(this);
		ControlVideoTexture->SetMediaPlayer(ControlVideoPlayer);
		ControlVideoTexture->UpdateResource();
		ControlVideoBrush.SetResourceObject(ControlVideoTexture);
		ControlVideoBrush.DrawAs = ESlateBrushDrawType::Image;
		ControlVideoBrush.ImageSize = FVector2D(1280,720);
	}
	ControlVideoPlayer->Close();
	OpenVideoId = Id;
	ControlVideoPlayer->SetLooping(true);
	const TCHAR* const Names[] = {TEXT("Normal"),TEXT("Simple"),TEXT("MouseClick"),TEXT("MouseHold"),TEXT("PadNormal"),TEXT("PadSimple")};
	const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Movies/Controls"),FString(Names[Id]) + TEXT(".mp4")));
	bVideoOpenFailed = !ControlVideoPlayer->OpenFile(File);
}

void UChaosImpactSettingsScreen::ApplyControlPreview()
{
	ChaosImpactSettings::SetControlScheme(EditedProfile,EditedDevice,PreviewControlScheme);
	bControlStylePickerOpen = false;
	BuildRows();
	ShowMessage(TEXT("操作スタイルを変更しました"));
	ChaosImpactSfx::Play2D(Controller.Get(),EChaosImpactSfx::UiConfirm);
}

int32 UChaosImpactSettingsScreen::PaintControlStylePicker(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer) const
{
	using namespace ChaosImpactPaint;
	using namespace ChaosImpactSettings;
	const double Now = FPlatformTime::Seconds();
	const FPainter P{Design, Elements, Layer}, Top{Design, Elements, Layer + 1}, Words{Design, Elements, Layer + 2};
	HudPoly(P, {FVector2D(-1400.0, 0.0), FVector2D(3000.0, 0.0), FVector2D(3000.0, 900.0), FVector2D(-1400.0, 900.0)},
		MakeFill(FLinearColor(0.02f, 0.03f, 0.06f, 1.0f), FLinearColor(0.004f, 0.006f, 0.014f, 1.0f)));

	// ---- The title, and whose / which device's style this is.
	HudText3D(Words, TEXT("操作スタイル"), 60.0f, 40.0f, 46.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
	P.Box(64.0f, 110.0f, 120.0f, 6.0f, Ice);
	P.Box(188.0f, 110.0f, 60.0f, 6.0f, Fire);
	{
		const FString Who = GetProfile(EditedProfile).Name + TEXT("  ／  ") + GetDeviceName(EditedDevice);
		const float WhoX = 64.0f + HudMeasure(TEXT("操作スタイル"), 46.0f, TEXT("BlackItalic")) + 30.0f;
		const float WhoSize = HudFit(Who, 18.0f, 1540.0f - WhoX - 28.0f, TEXT("Bold"));
		const float WhoW = HudMeasure(Who, WhoSize, TEXT("Bold")) + 28.0f;
		RoundedBox(P, WhoX, 58.0f, WhoW, 34.0f, 17.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.06f));
		Words.Text(Who, WhoX + 14.0f, 75.0f - WhoSize * 0.72f, WhoSize, WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
	}

	// ---- The styles down the left: the one being shown lit, the one in use tagged.
	const bool bPad = EditedDevice != EChaosImpactDevice::KeyboardMouse;
	const int32 Count = bPad ? 2 : 4;
	const TCHAR* const Descriptions[] = {bPad ? TEXT("左右のスティックで移動とねらいを分ける") : TEXT("移動・投球などを自分のキー設定で操作"),
		bPad ? TEXT("左スティックと3つのボタンだけで遊べる") : TEXT("WASD＋左右クリック＋Eだけで遊べる"),
		TEXT("右クリックした場所まで自動で歩く"), TEXT("右を押している間、カーソルの方へ歩く")};
	const TCHAR* const Tags[] = {TEXT("細かくねらえる"), TEXT("はじめての人に"), TEXT("片手で遊べる"), TEXT("片手・直感的")};
	const FLinearColor Colors[] = {Ice, Gold, Fire, Violet};
	const int32 Active = static_cast<int32>(GetControlScheme(EditedProfile, EditedDevice));
	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 4.0f);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FBox2D R = ControlCard(Index);
		const float X = static_cast<float>(R.Min.X), Y = static_cast<float>(R.Min.Y);
		const float W = static_cast<float>(R.GetSize().X), H = static_cast<float>(R.GetSize().Y);
		const bool bPreview = Index == static_cast<int32>(PreviewControlScheme);
		const bool bActive = Index == Active;
		const FLinearColor C = Colors[Index];
		if (bPreview)
		{
			P.Box(X + 10.0f, Y + 12.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			FLinearColor Deep = C * 0.3f;
			Deep.A = 0.97f;
			HudPoly(P, {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
				MakeFill(FMath::Lerp(C, Paper, 0.1f) * FLinearColor(0.5f, 0.5f, 0.5f, 1.0f), Deep));
			Top.Box(X, Y, 7.0f, H, C);
			Top.Outline(X, Y, W, H, WithAlpha(FMath::Lerp(C, Paper, 0.4f), 0.55f + 0.35f * Pulse), 2.0f);
			// A pointer towards the preview on the right.
			HudPoly(Top, {FVector2D(X + W, Y + H * 0.5f - 14.0f), FVector2D(X + W + 16.0f, Y + H * 0.5f), FVector2D(X + W, Y + H * 0.5f + 14.0f)},
				SolidFill(FMath::Lerp(C, Paper, 0.3f)), 1);
		}
		else
		{
			RoundedBox(P, X, Y, W, H, 12.0f, FLinearColor(0.03f, 0.045f, 0.08f, 0.92f));
			Top.Box(X, Y + 18.0f, 4.0f, H - 36.0f, WithAlpha(C, 0.6f));
		}
		// Its number, name and what it is like.
		RoundedBox(Top, X + 24.0f, Y + 22.0f, 40.0f, 40.0f, 10.0f, bPreview ? C : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		Words.Text(FString::FromInt(Index + 1), X + 44.0f, Y + 27.0f, 22.0f, bPreview ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
		const FString Name = GetControlSchemeName(static_cast<EChaosImpactControlScheme>(Index));
		const float TagW = HudMeasure(Tags[Index], 14.0f, TEXT("Bold")) + 22.0f;
		const float NameRoom = W - 84.0f - (bActive ? 124.0f : 20.0f);
		Words.Text(Name, X + 80.0f, Y + 22.0f, HudFit(Name, 28.0f, NameRoom, TEXT("Black")), bPreview ? Paper : WithAlpha(Paper, 0.85f), ETextAlign::Left,
			TEXT("Black"));
		Words.Text(Descriptions[Index], X + 80.0f, Y + 66.0f, HudFit(Descriptions[Index], 18.0f, W - 104.0f, TEXT("Bold")),
			bPreview ? WithAlpha(Paper, 0.9f) : Muted, ETextAlign::Left, TEXT("Bold"));
		RoundedBox(Top, X + 80.0f, Y + H - 36.0f, TagW, 24.0f, 12.0f, WithAlpha(C, bPreview ? 0.35f : 0.18f));
		Words.Text(Tags[Index], X + 80.0f + TagW * 0.5f, Y + H - 33.0f, 14.0f, bPreview ? Paper : WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Bold"));
		if (bActive)
		{
			RoundedBox(Top, X + W - 104.0f, Y + 24.0f, 84.0f, 30.0f, 15.0f, Paper);
			Words.Text(TEXT("使用中"), X + W - 62.0f, Y + 28.0f, 16.0f, Ink, ETextAlign::Center, TEXT("Black"));
		}
	}

	// ---- What it looks like: the device, its keys, and the real play.
	ChaosImpactControlPreview::Paint(Design, Elements, Layer + 3, EditedProfile, EditedDevice, PreviewControlScheme,
		GetControlVideoTime(), IsControlVideoPlaying() ? &ControlVideoBrush : nullptr,
		bVideoOpenFailed || (ControlVideoPlayer && ControlVideoPlayer->HasError()));

	// ---- The foot: back, how to choose, and choosing it.
	const FPainter Foot{Design, Elements, Layer + 12}, FootTop{Design, Elements, Layer + 13}, FootText{Design, Elements, Layer + 14};
	{
		const float X = static_cast<float>(PickerBackButton.Min.X), Y = static_cast<float>(PickerBackButton.Min.Y);
		const float W = static_cast<float>(PickerBackButton.GetSize().X), H = static_cast<float>(PickerBackButton.GetSize().Y);
		RoundedBox(Foot, X, Y, W, H, 12.0f, FLinearColor(0.03f, 0.045f, 0.08f, 0.95f));
		FootTop.Box(X, Y + 12.0f, 4.0f, H - 24.0f, Muted);
		FootText.Text(TEXT("もどる"), X + W * 0.5f, Y + H * 0.5f - 17.0f, 23.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}
	{
		const bool bAlready = static_cast<int32>(PreviewControlScheme) == Active;
		const float X = static_cast<float>(ApplyControlButton.Min.X), Y = static_cast<float>(ApplyControlButton.Min.Y);
		const float W = static_cast<float>(ApplyControlButton.GetSize().X), H = static_cast<float>(ApplyControlButton.GetSize().Y);
		if (bAlready)
		{
			RoundedBox(Foot, X, Y, W, H, 12.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f));
			FootText.Text(TEXT("使用中の操作です"), X + W * 0.5f, Y + H * 0.5f - 14.0f, HudFit(TEXT("使用中の操作です"), 19.0f, W - 30.0f, TEXT("Black")), Muted,
				ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			Foot.Box(X + 6.0f, Y + 8.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			HudPoly(FootTop, {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
				MakeFill(FMath::Lerp(Gold, Paper, 0.35f), Gold * FLinearColor(0.85f, 0.75f, 0.6f, 1.0f)));
			FootText.Text(TEXT("この操作にする"), X + W * 0.5f, Y + H * 0.5f - 17.0f, HudFit(TEXT("この操作にする"), 23.0f, W - 30.0f, TEXT("Black")), Ink,
				ETextAlign::Center, TEXT("Black"));
		}
	}
	{
		const FString Hint = ChaosImpactHints::Text(HintDevice(), TEXT("{pad:{DPad}：比べる　　{Confirm}：この操作にする　　{Back}：もどる}")
			TEXT("{kb:カーソルを重ねて映像を見る　　クリック：この操作にする　　右クリック：もどる}"));
		const float L = static_cast<float>(PickerBackButton.Max.X) + 24.0f, R = static_cast<float>(ApplyControlButton.Min.X) - 24.0f;
		FootText.Text(Hint, (L + R) * 0.5f, static_cast<float>(PickerBackButton.Min.Y) + 17.0f, HudFit(Hint, 17.0f, R - L, TEXT("Bold")),
			WithAlpha(Paper, 0.55f), ETextAlign::Center, TEXT("Bold"));
	}
	// A note about the last change.
	if (!Message.IsEmpty() && Now - MessageAt < 2.4)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.4 - (Now - MessageAt)) / 0.4f, 0.0f, 1.0f);
		const float NoteW = FMath::Min(HudMeasure(Message, 20.0f, TEXT("Bold")) + 60.0f, 700.0f);
		RoundedBox(FPainter{Design, Elements, Layer + 15, Fade}, 1090.0f - NoteW * 0.5f, 734.0f, NoteW, 40.0f, 14.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		FPainter{Design, Elements, Layer + 16, Fade}.Text(Message, 1090.0f, 741.0f, HudFit(Message, 20.0f, NoteW - 40.0f, TEXT("Bold")), Gold,
			ETextAlign::Center, TEXT("Bold"));
	}
	return Layer + 17;
}

int32 UChaosImpactSettingsScreen::FindRow(const int32 Id) const
{
	return Rows.IndexOfByPredicate([Id](const FRow& Row) { return Row.Id == Id; });
}

void UChaosImpactSettingsScreen::BuildRows()
{
	Rows.Reset();
	switch (Tab)
	{
	case ETab::Display: BuildDisplayRows(); break;
	case ETab::Picture: BuildPictureRows(); break;
	case ETab::Controls: BuildControlRows(); break;
	default: BuildOtherRows(); break;
	}
	SelectedRow = FMath::Clamp(SelectedRow, 0, FMath::Max(Rows.Num() - 1, 0));
}

void UChaosImpactSettingsScreen::BuildDisplayRows()
{
	using namespace ChaosImpactSettings;
	// 表示: how the game is shown; なめらかさ: how often it is drawn.
	static const TCHAR* const Modes[] = {TEXT("フルスクリーン"), TEXT("フルスクリーン"), TEXT("ウィンドウ")};
	const int32 Mode = GetWindowMode();
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("表示")));
	if (UChaosImpactMenuWidget::IsPhoneBuild())
	{
		// スマホ版: the whole screen always (no window, its size or V-sync): how bright, and how smooth.
		Rows.Add(MakeRow(ERowKind::Value, RowScreenOrientation, TEXT("画面の向き"), GetScreenOrientationName(GetScreenOrientation())));
		Rows.Add(MakeRow(ERowKind::Value, RowBrightness, TEXT("明るさ"), FString::Printf(TEXT("%d"), GetBrightness())));
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("なめらかさ")));
		const int32 PhoneLimit = GetFrameRateLimit();
		Rows.Add(MakeRow(ERowKind::Value, RowFrameRate, TEXT("フレームレート上限"),
			PhoneLimit <= 0 ? FString(TEXT("画面に合わせる")) : FString::Printf(TEXT("%d fps"), PhoneLimit)));
		Rows.Add(MakeRow(ERowKind::Value, RowShowFps, TEXT("FPS表示"), OnOff(IsFpsShown())));
		return;
	}
	Rows.Add(MakeRow(ERowKind::Value, RowWindowMode, TEXT("画面モード"), Modes[Mode]));
	const FIntPoint Resolution = GetResolution();
	const FIntPoint Monitor = GetMonitorResolution();
	FString ResolutionText = FString::Printf(TEXT("%d × %d"), Resolution.X, Resolution.Y);
	if (!GetResolutionShape(Resolution).IsEmpty())
	{
		ResolutionText += FString::Printf(TEXT("（%s）"), *GetResolutionShape(Resolution));
	}
	if (Mode != 2 && Resolution == Monitor)
	{
		ResolutionText += TEXT("（画面と同じ）");
	}
	else if (Mode != 2 && Resolution.Y > Monitor.Y)
	{
		ResolutionText += TEXT("（高画質・重い）");
	}
	Rows.Add(MakeRow(ERowKind::Value, RowResolution, Mode == 2 ? TEXT("ウィンドウの大きさ") : TEXT("解像度"), ResolutionText));
	Rows.Add(MakeRow(ERowKind::Value, RowBrightness, TEXT("明るさ"), FString::Printf(TEXT("%d"), GetBrightness())));
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("なめらかさ")));
	const int32 Limit = GetFrameRateLimit();
	Rows.Add(MakeRow(ERowKind::Value, RowFrameRate, TEXT("フレームレート上限"),
		Limit <= 0 ? FString(TEXT("無制限")) : FString::Printf(TEXT("%d fps"), Limit)));
	Rows.Add(MakeRow(ERowKind::Value, RowVSync, TEXT("垂直同期"), OnOff(IsVSyncOn())));
	Rows.Add(MakeRow(ERowKind::Value, RowShowFps, TEXT("FPS表示"), OnOff(IsFpsShown())));
}

void UChaosImpactSettingsScreen::BuildPictureRows()
{
	using namespace ChaosImpactSettings;
	const int32 Overall = GetOverallQuality();
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("まとめて")));
	Rows.Add(MakeRow(ERowKind::Value, RowQuality, TEXT("画質"),
		Overall < 0 ? FString(TEXT("カスタム")) : FString(QualityNames[Overall])));
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("くわしく（ひとつずつ）")));
	for (int32 Group = 0; Group < QualityGroupCount; ++Group)
	{
		// 描画距離 and 草木 change nothing here (small stages seen from above, no foliage): they just follow まとめて.
		if (Group == 0 || Group == 8)
		{
			continue;
		}
		Rows.Add(MakeRow(ERowKind::Value, RowQualityGroup + Group, GetQualityGroupName(Group),
			QualityNames[GetQualityGroup(Group)]));
	}
}

bool UChaosImpactSettingsScreen::IsTouchScreenOnly() const
{
	const AChaosImpactPlayerController* Player = Controller.Get();
	return Player && Player->ShouldUseTouchControls() && !Player->IsUsingGamepad();
}

bool UChaosImpactSettingsScreen::IsTouchSettings() const
{
	return IsTouchScreenOnly() && EditedDevice == EChaosImpactDevice::KeyboardMouse;
}

void UChaosImpactSettingsScreen::BuildControlRows()
{
	using namespace ChaosImpactSettings;
	EditedProfile = FMath::Clamp(EditedProfile, 0, GetProfileCount() - 1);
	const FChaosImpactProfile& Profile = GetProfile(EditedProfile);
	if (ControlsStep == EControlsStep::Nicknames)
	{
		// The first step: ＋ to make one, then every nickname (its devices shown beside it).
		Rows.Add(MakeRow(ERowKind::Button, RowNewProfile, TEXT("＋ ニックネームを作る"),
			FString::Printf(TEXT("%d / %d"), GetProfileCount() - 1, MaxProfiles - 1), GetProfileCount() >= MaxProfiles));
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("ニックネーム")));
		for (int32 Index = 0; Index < GetProfileCount(); ++Index)
		{
			Rows.Add(MakeRow(ERowKind::Button, RowProfileItem + Index, GetProfile(Index).Name));
		}
		return;
	}
	if (ControlsStep == EControlsStep::Devices)
	{
		// The second: the nickname's devices as cards, then its own name (changed, or taken off).
		const bool bGuest = EditedProfile == 0;
		for (int32 Device = 0; Device < static_cast<int32>(EChaosImpactDevice::Count); ++Device)
		{
			Rows.Add(MakeRow(ERowKind::Button, RowDeviceItem + Device, GetDeviceName(static_cast<EChaosImpactDevice>(Device))));
		}
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("このニックネーム")));
		Rows.Add(MakeRow(ERowKind::Button, RowRenameProfile, TEXT("名前を変える"), FString(), bGuest));
		Rows.Add(MakeRow(ERowKind::Button, RowDeleteProfile,
			bDeleteArmed ? TEXT("もう一度おすと消します") : TEXT("このニックネームを消す"), FString(), bGuest));
		return;
	}
	// The third: this nickname's settings on this device. スマホ版 (no controller): the touch settings instead.
	if (IsTouchSettings())
	{
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("タッチ操作")));
		Rows.Add(MakeRow(ERowKind::Value, RowTouchStyle, TEXT("操作方式"), GetTouchStyleName(Profile.TouchStyle)));
		Rows.Add(MakeRow(ERowKind::Value, RowTouchLeftHanded, TEXT("左右を入れ替え（左利き）"), OnOff(Profile.bTouchLeftHanded)));
		Rows.Add(MakeRow(ERowKind::Value, RowTouchFlick, TEXT("はじきやすさ"), GetTouchFlickName(Profile.TouchFlick)));
		if (Profile.TouchStyle != 2)
		{
			// (片手モード always aims by itself.)
			Rows.Add(MakeRow(ERowKind::Value, RowTouchAutoAim, TEXT("タップで自動ねらい"), OnOff(Profile.bTouchAutoAim)));
		}

		Rows.Add(MakeRow(ERowKind::Button, RowTouchLayout, TEXT("ボタン・画面の配置を変える")));
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("カメラ")));
		Rows.Add(MakeRow(ERowKind::Value, RowCameraDistance, TEXT("カメラの距離"), GetCameraDistanceName(Profile.CameraDistance)));
		return;
	}
	const bool bPad = EditedDevice != EChaosImpactDevice::KeyboardMouse;
	const EChaosImpactControlScheme Scheme = GetControlScheme(EditedProfile, EditedDevice);
	// How it is played (for now only the normal style: no style rows).
	const bool bStyles = !IsNormalControlsOnly();
	// (The keyboard and mouse: the wheel's ball swap.)
	const bool bWheelRow = !bPad && Scheme == EChaosImpactControlScheme::Normal;
	if (bStyles || bPad || bWheelRow)
	{
		Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("操作のしかた")));
	}
	if (bWheelRow)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowWheelSwap, TEXT("ホイールでボール切り替え"), OnOff(Profile.bWheelSwap)));
	}
	if (bStyles)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowControlScheme, TEXT("操作スタイル"), GetControlSchemeName(Scheme)));
		Rows.Add(MakeRow(ERowKind::Button, RowControlPreview, TEXT("操作スタイルを図と動画で見る")));
	}
	if (Scheme == EChaosImpactControlScheme::MouseOnly)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowMouseDash, TEXT("ダッシュ操作"),
			Profile.bMouseDoubleClickDash ? TEXT("右ダブルクリック / ホイール押し") : TEXT("ホイール押し")));
	}
	if (bPad)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowPadChoice, TEXT("コントローラーの種類"), GetPadChoiceName(Profile.Pad)));
		if (Scheme == EChaosImpactControlScheme::Normal)
		{
			Rows.Add(MakeRow(ERowKind::Value, RowSwapSticks, TEXT("スティックの左右を入れ替え"), OnOff(Profile.bSwapSticks)));
		}
	}
	// Aiming and the camera.
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("ねらい・カメラ")));
	Rows.Add(MakeRow(ERowKind::Value, RowCameraDistance, TEXT("カメラの距離"), GetCameraDistanceName(Profile.CameraDistance)));
	if (Scheme == EChaosImpactControlScheme::Normal)
	{
		// (The pads' simple style already throws the way it moves; the mouse-only styles move toward the cursor.)
		Rows.Add(MakeRow(ERowKind::Value, RowThrowToMove, TEXT("移動方向に投げる"), OnOff(GetThrowToMove(EditedProfile, EditedDevice))));
	}
	Rows.Add(MakeRow(ERowKind::Value, RowAimAssist, TEXT("エイムアシスト"), OnOff(Profile.bAimAssist)));
	Rows.Add(MakeRow(ERowKind::Value, RowShoulderSensitivityX,
		bPad ? TEXT("肩越しの感度（横）") : TEXT("肩越しのマウス感度"),
		FString::Printf(TEXT("%d%%"), GetShoulderSensitivity(EditedProfile, EditedDevice))));
	if (bPad)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowShoulderSensitivityY, TEXT("肩越しの感度（縦）"),
			FString::Printf(TEXT("%d%%"), GetShoulderSensitivity(EditedProfile, EditedDevice, true))));
	}
	Rows.Add(MakeRow(ERowKind::Button, RowResetShoulderSensitivity, TEXT("感度を100%にもどす")));
	if (Scheme != EChaosImpactControlScheme::Normal)
	{
		// A preset style: what it does, to read (its keys are its own; the normal style's are kept).
		Rows.Add(MakeRow(ERowKind::Heading, 99, FString::Printf(TEXT("%s の操作"), *GetControlSchemeName(Scheme))));
		Rows.Add(MakeRow(ERowKind::Info, 5000, bPad ? TEXT("左スティック：移動・方向 / ドライブ操縦")
			: Scheme == EChaosImpactControlScheme::MouseHold ? TEXT("右長押し：カーソル方向へ移動 / 段差は自動ジャンプ")
			: Scheme == EChaosImpactControlScheme::MouseOnly ? TEXT("右クリック：移動 / マウス：照準・ドライブ操縦")
			: TEXT("WASD：移動 / マウス：照準 / ドライブはWASD")));
		Rows.Add(MakeRow(ERowKind::Info, 5001, FString::Printf(TEXT("%s：投球（長押しでチャージ）"),
			*GetKeyName(GetActiveKey(EditedProfile, EditedDevice, EChaosImpactAction::Throw, 0), EditedDevice))));
		Rows.Add(MakeRow(ERowKind::Info, 5002, FString::Printf(TEXT("%s：ダッシュ"),
			*GetKeyName(GetActiveKey(EditedProfile, EditedDevice, EChaosImpactAction::Dash, 0), EditedDevice))));
		Rows.Add(MakeRow(ERowKind::Info, 5003, IsMouseOnlyScheme(Scheme)
			? TEXT("ホイール：持ち替え / ボール枠を右長押し：ドロップ")
			: FString::Printf(TEXT("%s：持ち替え / 0.7秒長押し：ドロップ"),
				*GetKeyName(GetActiveKey(EditedProfile, EditedDevice, EChaosImpactAction::SwapBall, 0), EditedDevice))));
		if (Scheme == EChaosImpactControlScheme::MouseHold)
		{
			Rows.Add(MakeRow(ERowKind::Info, 5004, TEXT("離すと停止 / ドライブはマウスで自由に操縦")));
		}
		Rows.Add(MakeRow(ERowKind::Info, 5005, IsMouseOnlyScheme(Scheme)
			? TEXT("左右クリック同時押し：タックル（投球・移動と排他）")
			: TEXT("回避＋投球ボタン同時押し：タックル")));
	}
	else
	{
		// The normal style: its keys or buttons, two to an action.
		Rows.Add(MakeRow(ERowKind::Heading, 1, bPad ? TEXT("ボタンの割り当て") : TEXT("キーの割り当て")));
		for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
		{
			const EChaosImpactAction Which = static_cast<EChaosImpactAction>(Action);
			FRow Row = MakeRow(ERowKind::Binding, RowBinding + Action, GetActionName(Which));
			for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
			{
				Row.Cells[Slot] = GetKeyName(GetKey(EditedProfile, EditedDevice, Which, Slot), EditedDevice);
			}
			if (bPad && Action <= static_cast<int32>(EChaosImpactAction::MoveRight))
			{
				// The stick always moves; buttons set here move too.
				Row.Label += TEXT("（スティック以外）");
			}
			Rows.Add(Row);
		}
		Rows.Add(MakeRow(ERowKind::Button, RowResetDevice, bPad ? TEXT("ボタンの割り当てを初期設定にもどす") : TEXT("キーの割り当てを初期設定にもどす")));
	}
}

bool UChaosImpactSettingsScreen::IsDeviceChanged(const int32 Profile, const EChaosImpactDevice Device)
{
	using namespace ChaosImpactSettings;
	for (int32 Action = 0; Action < FChaosImpactProfile::ActionCount; ++Action)
	{
		for (int32 Slot = 0; Slot < FChaosImpactProfile::SlotCount; ++Slot)
		{
			const EChaosImpactAction Which = static_cast<EChaosImpactAction>(Action);
			if (GetKey(Profile, Device, Which, Slot) != GetDefaultKey(Device, Which, Slot))
			{
				return true;
			}
		}
	}
	return false;
}

void UChaosImpactSettingsScreen::SetControlsStep(const EControlsStep Step)
{
	ControlsStep = Step;
	bDeleteArmed = false;
	FirstVisibleRow = 0;
	ScrollFrac = 0.0f;
	ScrollVelocity = 0.0f;
	BuildRows();
	// The cursor on where it came from: the nickname, or the device, being set.
	const int32 Row = Step == EControlsStep::Nicknames ? FindRow(RowProfileItem + EditedProfile)
		: Step == EControlsStep::Devices ? FindRow(RowDeviceItem + static_cast<int32>(EditedDevice)) : INDEX_NONE;
	SelectedRow = 0;
	SelectedCell = 0;
	if (IsSelectable(Row))
	{
		SelectedRow = Row;
	}
	else if (!IsSelectable(SelectedRow))
	{
		const bool bWasSide = bSideFocus;
		MoveSelection(1);
		bSideFocus = bWasSide;
	}
	KeepSelectionVisible();
	BuildRows();
}

void UChaosImpactSettingsScreen::StepBack()
{
	if (Tab == ETab::Controls && ControlsStep != EControlsStep::Nicknames && !bSideFocus)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		SetControlsStep(ControlsStep == EControlsStep::Buttons && !IsTouchSettings() ? EControlsStep::Devices : EControlsStep::Nicknames);
		return;
	}
	BackToTabs();
}

bool UChaosImpactSettingsScreen::IsDeviceCardRow(const int32 Row) const
{
	return Tab == ETab::Controls && ControlsStep == EControlsStep::Devices && Rows.IsValidIndex(Row)
		&& Rows[Row].Id >= RowDeviceItem && Rows[Row].Id < RowDeviceItem + static_cast<int32>(EChaosImpactDevice::Count);
}

void UChaosImpactSettingsScreen::BuildOtherRows()
{
	using namespace ChaosImpactSettings;
	static const TCHAR* const Shakes[] = {TEXT("なし"), TEXT("弱い"), TEXT("ふつう"), TEXT("強い")};
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("サウンド")));
	Rows.Add(MakeRow(ERowKind::Value, RowVolume, TEXT("全体の音量"), FString::Printf(TEXT("%d%%"), GetMasterVolume())));
	Rows.Add(MakeRow(ERowKind::Value, RowMusicVolume, TEXT("BGMの音量"), FString::Printf(TEXT("%d%%"), GetMusicVolume())));
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("プレイ中")));
	Rows.Add(MakeRow(ERowKind::Value, RowShake, TEXT("画面の揺れ"), Shakes[GetCameraShakeLevel()]));
	// (スマホ版 by touch alone: no controller, so none of its rows.)
	if (!IsTouchScreenOnly())
	{
		Rows.Add(MakeRow(ERowKind::Value, RowRumble, TEXT("コントローラーの振動"), OnOff(AChaosImpactPlayerController::IsRumbleEnabled())));
		Rows.Add(MakeRow(ERowKind::Value, RowMenuRumble, TEXT("メニュー中の振動"), OnOff(IsMenuRumbleOn()),
			!AChaosImpactPlayerController::IsRumbleEnabled()));
		Rows.Add(MakeRow(ERowKind::Value, RowSwitchConfirm, TEXT("Switchの決定ボタン"),
			IsSwitchConfirmRight() ? TEXT("A（右のボタン）") : TEXT("B（下のボタン）")));
	}
	static const TCHAR* const HudSizes[] = {TEXT("小さい"), TEXT("ふつう"), TEXT("大きい")};
	Rows.Add(MakeRow(ERowKind::Value, RowHudSize, TEXT("UIの大きさ"), HudSizes[FMath::Clamp(GetHudSize(), 0, 2)]));
	Rows.Add(MakeRow(ERowKind::Value, RowButtonHints, TEXT("ボタンヒント"), OnOff(IsButtonHintsOn())));
	Rows.Add(MakeRow(ERowKind::Value, RowOthersPaint, TEXT("ほかの人のペイント"), IsOthersPaintShown() ? TEXT("表示する") : TEXT("表示しない")));
	// (トレーニングのチュートリアル: retired with 訓練場; its row is gone.)
	// Tucked away at the very end: the loading screens' little games, to play for as long as you like.
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("おまけ")));
	Rows.Add(MakeRow(ERowKind::Button, RowMiniGames, TEXT("ミニゲーム")));
}

bool UChaosImpactSettingsScreen::IsSelectable(const int32 Row) const
{
	return Rows.IsValidIndex(Row) && Rows[Row].Kind != ERowKind::Heading;
}

void UChaosImpactSettingsScreen::MoveSide(const int32 Direction)
{
	SideCursor = Wrap(SideCursor + Direction, SideBack + 1);
	if (SideCursor < SideSave)
	{
		// A tab opens as the cursor reaches it (its settings show beside it).
		SetTab(static_cast<ETab>(SideCursor));
	}
	else
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
}

void UChaosImpactSettingsScreen::ActivateSide()
{
	if (SideCursor == SideSave)
	{
		Save();
	}
	else if (SideCursor == SideBack)
	{
		Leave();
	}
	else
	{
		EnterRows();
	}
}

void UChaosImpactSettingsScreen::EnterRows()
{
	if (!bSideFocus)
	{
		return;
	}
	if (SideCursor >= SideSave)
	{
		SideCursor = static_cast<int32>(Tab);
	}
	bSideFocus = false;
	if (Tab == ETab::Controls && ControlsStep == EControlsStep::Nicknames && IsSelectable(FindRow(RowProfileItem + EditedProfile)))
	{
		SelectedRow = FindRow(RowProfileItem + EditedProfile);
		KeepSelectionVisible();
		BuildRows();
	}
	if (!IsSelectable(SelectedRow))
	{
		MoveSelection(1);
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
}

void UChaosImpactSettingsScreen::BackToTabs()
{
	bSideFocus = true;
	SideCursor = static_cast<int32>(Tab);
	bDeleteArmed = false;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
}

void UChaosImpactSettingsScreen::SelectRow(const int32 Row, const int32 Cell)
{
	if (!IsSelectable(Row))
	{
		return;
	}
	bSideFocus = false;
	if (Row != SelectedRow)
	{
		bDeleteArmed = false;
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	}
	SelectedRow = Row;
	SelectedCell = Rows[Row].Kind == ERowKind::Binding ? FMath::Clamp(Cell, 0, 1) : 0;
	KeepSelectionVisible();
	BuildRows();
}

void UChaosImpactSettingsScreen::MoveSelection(const int32 Direction)
{
	if (bControlStylePickerOpen) { MoveControlPreview(Direction); return; }
	if (Rows.IsEmpty())
	{
		return;
	}
	if (IsDeviceCardRow(SelectedRow) || (Tab == ETab::Controls && ControlsStep == EControlsStep::Devices))
	{
		// The cards side by side are one stop; up and down go between them and the buttons under them.
		const int32 Count = static_cast<int32>(EChaosImpactDevice::Count);
		TArray<int32> Stops;
		Stops.Add(FindRow(RowDeviceItem + static_cast<int32>(EditedDevice)));
		for (int32 Row = Count; Row < Rows.Num(); ++Row)
		{
			if (IsSelectable(Row))
			{
				Stops.Add(Row);
			}
		}
		const int32 At = IsDeviceCardRow(SelectedRow) ? 0 : FMath::Max(Stops.IndexOfByKey(SelectedRow), 0);
		SelectRow(Stops[Wrap(At + (Direction < 0 ? -1 : 1), Stops.Num())], 0);
		return;
	}
	int32 Row = SelectedRow;
	for (int32 Step = 0; Step < Rows.Num(); ++Step)
	{
		Row = Wrap(Row + (Direction < 0 ? -1 : 1), Rows.Num());
		if (IsSelectable(Row))
		{
			break;
		}
	}
	SelectRow(Row, SelectedCell);
}

int32 UChaosImpactSettingsScreen::ShownRows() const
{
	return Tab == ETab::Controls && ControlsStep == EControlsStep::Devices ? Rows.Num() : Layout().VisibleRows;
}

void UChaosImpactSettingsScreen::KeepSelectionVisible()
{
	const int32 VisibleRows = ShownRows();
	// A heading just above the selection stays in view with it.
	const int32 Top = SelectedRow > 0 && Rows.IsValidIndex(SelectedRow - 1) && Rows[SelectedRow - 1].Kind == ERowKind::Heading
		? SelectedRow - 1 : SelectedRow;
	const int32 Was = FirstVisibleRow;
	if (Top < FirstVisibleRow)
	{
		FirstVisibleRow = Top;
	}
	else if (SelectedRow >= FirstVisibleRow + VisibleRows)
	{
		FirstVisibleRow = SelectedRow - VisibleRows + 1;
	}
	FirstVisibleRow = FMath::Clamp(FirstVisibleRow, 0, FMath::Max(Rows.Num() - VisibleRows, 0));
	if (FirstVisibleRow != Was)
	{
		ScrollFrac = 0.0f;
		ScrollVelocity = 0.0f;
	}
}

float UChaosImpactSettingsScreen::GetScrollPixels() const
{
	return FirstVisibleRow * Layout().RowStep + ScrollFrac;
}

float UChaosImpactSettingsScreen::MaxScrollPixels() const
{
	return FMath::Max(Rows.Num() - ShownRows(), 0) * Layout().RowStep;
}

void UChaosImpactSettingsScreen::SetScrollPixels(const float Pixels)
{
	const float Step = FMath::Max(Layout().RowStep, 1.0f);
	const float Clamped = FMath::Clamp(Pixels, 0.0f, MaxScrollPixels());
	FirstVisibleRow = FMath::Clamp(FMath::FloorToInt(Clamped / Step), 0, FMath::Max(Rows.Num() - ShownRows(), 0));
	ScrollFrac = FMath::Clamp(Clamped - FirstVisibleRow * Step, 0.0f, Step);
}

FBox2D UChaosImpactSettingsScreen::RowsArea() const
{
	const FSettingsLayout& L = Layout();
	const int32 VisibleRows = ShownRows();
	return FBox2D(FVector2D(L.RowsLeft - 40.0f, L.RowsTop - 4.0f),
		FVector2D(L.RowsLeft + L.RowsWidth + 40.0f, L.RowsTop + VisibleRows * L.RowStep - (L.RowStep - L.RowHeight) + 4.0f));
}

void UChaosImpactSettingsScreen::HandleWheel(const float Delta)
{
	if (bAskSave) { return; }
	if (bControlStylePickerOpen) { MoveControlPreview(Delta > 0 ? -1 : 1); return; }
	if (NameEntry.IsOpen() || bWaitingForKey)
	{
		return;
	}
	FirstVisibleRow = FMath::Clamp(FirstVisibleRow + (Delta > 0.0f ? -1 : 1), 0, FMath::Max(Rows.Num() - ShownRows(), 0));
	ScrollFrac = 0.0f;
	ScrollVelocity = 0.0f;
}

void UChaosImpactSettingsScreen::Adjust(const int32 Direction)
{
	if (bControlStylePickerOpen) { MoveControlPreview(Direction); return; }
	if (!IsSelectable(SelectedRow) || Direction == 0)
	{
		return;
	}
	const FRow& Row = Rows[SelectedRow];
	if (IsDeviceCardRow(SelectedRow))
	{
		// Left and right between the cards (the device to set follows the cursor).
		const int32 Count = static_cast<int32>(EChaosImpactDevice::Count);
		EditedDevice = static_cast<EChaosImpactDevice>(Wrap(Row.Id - RowDeviceItem + (Direction < 0 ? -1 : 1), Count));
		SelectRow(FindRow(RowDeviceItem + static_cast<int32>(EditedDevice)));
		return;
	}
	if (Row.Kind == ERowKind::Binding)
	{
		SelectedCell = Direction < 0 ? 0 : 1;
		return;
	}
	if (Row.Kind == ERowKind::Value && !Row.bDisabled)
	{
		ChangeValue(Row, Direction);
	}
}

void UChaosImpactSettingsScreen::ChangeValue(const FRow& Row, const int32 Direction)
{
	using namespace ChaosImpactSettings;
	const int32 Step = Direction < 0 ? -1 : 1;
	const int32 Id = Row.Id;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	if (Id == RowWindowMode)
	{
		SetWindowMode(GetWindowMode() == 2 ? 1 : 2);
	}
	else if (Id == RowResolution)
	{
		const TArray<FIntPoint> Choices = GetResolutionChoices();
		if (!Choices.IsEmpty())
		{
			const int32 Current = FMath::Max(Choices.IndexOfByKey(GetResolution()), 0);
			SetResolution(Choices[FMath::Clamp(Current + Step, 0, Choices.Num() - 1)]);
		}
	}
	else if (Id == RowVSync)
	{
		SetVSync(!IsVSyncOn());
	}
	else if (Id == RowFrameRate && UChaosImpactMenuWidget::IsPhoneBuild())
	{
		static const int32 PhoneRates[] = {30, 60, 90, 120, 0};
		const int32 Limit = GetFrameRateLimit();
		int32 Current = UE_ARRAY_COUNT(PhoneRates) - 1;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(PhoneRates) - 1; ++Index)
		{
			if (Limit > 0 && Limit <= PhoneRates[Index])
			{
				Current = Index;
				break;
			}
		}
		SetFrameRateLimit(PhoneRates[FMath::Clamp(Current + Step, 0, static_cast<int32>(UE_ARRAY_COUNT(PhoneRates)) - 1)]);
	}
	else if (Id == RowFrameRate)
	{
		const int32 Limit = GetFrameRateLimit();
		int32 Current = UE_ARRAY_COUNT(FrameRates) - 1;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FrameRates) - 1; ++Index)
		{
			if (Limit > 0 && Limit <= FrameRates[Index])
			{
				Current = Index;
				break;
			}
		}
		SetFrameRateLimit(FrameRates[FMath::Clamp(Current + Step, 0, static_cast<int32>(UE_ARRAY_COUNT(FrameRates)) - 1)]);
	}
	else if (Id == RowShowFps)
	{
		SetFpsShown(!IsFpsShown());
	}
	else if (Id == RowScreenOrientation)
	{
		SetScreenOrientation((GetScreenOrientation() + Step + 3) % 3);
	}
	else if (Id == RowBrightness)
	{
		SetBrightness(GetBrightness() + Step);
	}
	else if (Id == RowQuality)
	{
		const int32 Overall = GetOverallQuality();
		SetOverallQuality(FMath::Clamp((Overall < 0 ? 2 : Overall) + Step, 0, 3));
	}
	else if (Id == RowResolutionScale)
	{
		SetResolutionScale(FMath::Clamp(GetResolutionScale() / 5 * 5 + Step * 5, 50, 100));
	}
	else if (Id >= RowQualityGroup && Id < RowQualityGroup + QualityGroupCount)
	{
		SetQualityGroup(Id - RowQualityGroup, GetQualityGroup(Id - RowQualityGroup) + Step);
	}
	else if (Id == RowProfile)
	{
		EditedProfile = Wrap(EditedProfile + Step, GetProfileCount());
		bDeleteArmed = false;
	}
	else if (Id == RowDevice)
	{
		EditedDevice = static_cast<EChaosImpactDevice>(Wrap(static_cast<int32>(EditedDevice) + Step,
			static_cast<int32>(EChaosImpactDevice::Count)));
	}
	else if (Id == RowControlScheme)
	{
		SetControlScheme(EditedProfile, EditedDevice, static_cast<EChaosImpactControlScheme>(
			Wrap(static_cast<int32>(GetControlScheme(EditedProfile, EditedDevice)) + Step,
				EditedDevice == EChaosImpactDevice::KeyboardMouse ? 4 : 2)));
	}
	else if (Id == RowShoulderSensitivityX || Id == RowShoulderSensitivityY)
	{
		const bool bVertical = Id == RowShoulderSensitivityY;
		SetShoulderSensitivity(EditedProfile, EditedDevice, bVertical,
			GetShoulderSensitivity(EditedProfile, EditedDevice, bVertical) + Step * 10);
	}
	else if (Id == RowMouseDash)
	{
		SetMouseDoubleClickDash(EditedProfile, !GetProfile(EditedProfile).bMouseDoubleClickDash);
	}
	else if (Id == RowPadChoice)
	{
		SetPadChoice(EditedProfile, static_cast<EChaosImpactPadChoice>(Wrap(static_cast<int32>(GetProfile(EditedProfile).Pad) + Step,
			static_cast<int32>(EChaosImpactPadChoice::Count))));
	}
	else if (Id == RowSwapSticks)
	{
		SetSwapSticks(EditedProfile, !GetProfile(EditedProfile).bSwapSticks);
	}
	else if (Id == RowAimAssist)
	{
		SetAimAssist(EditedProfile, !GetProfile(EditedProfile).bAimAssist);
	}
	else if (Id == RowTouchStyle)
	{
		SetTouchStyle(EditedProfile, (GetProfile(EditedProfile).TouchStyle + Step + 3) % 3);
	}
	else if (Id == RowTouchLeftHanded)
	{
		SetTouchLeftHanded(EditedProfile, !GetProfile(EditedProfile).bTouchLeftHanded);
	}
	else if (Id == RowTouchFlick)
	{
		SetTouchFlick(EditedProfile, Wrap(GetProfile(EditedProfile).TouchFlick + Step, 3));
	}
	else if (Id == RowTouchAutoAim)
	{
		SetTouchAutoAim(EditedProfile, !GetProfile(EditedProfile).bTouchAutoAim);
	}

	else if (Id == RowWheelSwap)
	{
		SetWheelSwap(EditedProfile, !GetProfile(EditedProfile).bWheelSwap);
	}
	else if (Id == RowThrowToMove)
	{
		SetThrowToMove(EditedProfile, EditedDevice, !GetThrowToMove(EditedProfile, EditedDevice));
	}
	else if (Id == RowVolume)
	{
		SetMasterVolume(FMath::Clamp(GetMasterVolume() / 10 * 10 + Step * 10, 0, 100));
	}
	else if (Id == RowMusicVolume)
	{
		SetMusicVolume(FMath::Clamp(GetMusicVolume() / 10 * 10 + Step * 10, 0, 100));
	}
	else if (Id == RowMenuRumble)
	{
		SetMenuRumbleOn(!IsMenuRumbleOn());
		// (Felt at once when it is turned on.)
		AChaosImpactPlayerController::PlayMenuRumble(Controller.Get(), EChaosImpactSfx::UiConfirm);
	}
	else if (Id == RowRumble)
	{
		if (AChaosImpactPlayerController* Owner = Controller.Get())
		{
			Owner->ToggleRumbleEnabled();
		}
	}
	else if (Id == RowCameraDistance)
	{
		SetCameraDistance(EditedProfile, FMath::Clamp(GetProfile(EditedProfile).CameraDistance + Step, 0, 2));
	}
	else if (Id == RowShake)
	{
		SetCameraShakeLevel(FMath::Clamp(GetCameraShakeLevel() + Step, 0, 3));
	}
	else if (Id == RowSwitchConfirm)
	{
		SetSwitchConfirmRight(!IsSwitchConfirmRight());
	}
	else if (Id == RowHudSize)
	{
		SetHudSize(FMath::Clamp(GetHudSize() + Step, 0, 2));
	}
	else if (Id == RowButtonHints)
	{
		SetButtonHintsOn(!IsButtonHintsOn());
	}
	else if (Id == RowOthersPaint)
	{
		SetOthersPaintShown(!IsOthersPaintShown());
	}
	else if (Id == RowTutorial)
	{
		SetTrainingTutorialOn(!IsTrainingTutorialOn());
	}
	BuildRows();
}

void UChaosImpactSettingsScreen::Activate()
{
	using namespace ChaosImpactSettings;
	if (bControlStylePickerOpen) { ApplyControlPreview(); return; }
	if (!IsSelectable(SelectedRow) || Rows[SelectedRow].bDisabled)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
		return;
	}
	const FRow& Row = Rows[SelectedRow];
	if (Row.Id == RowControlScheme || Row.Id == RowControlPreview) { OpenControlStylePicker(); return; }
	if (Row.Kind == ERowKind::Value)
	{
		ChangeValue(Row, 1);
		return;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiConfirm);
	if (Row.Kind == ERowKind::Binding)
	{
		bWaitingForKey = true;
		WaitStartedAt = FPlatformTime::Seconds();
		return;
	}
	if (Row.Id >= RowProfileItem && Row.Id < RowDeviceItem)
	{
		EditedProfile = Row.Id - RowProfileItem;
		// The device this player is using first (スマホ版: straight to the touch settings).
		EditedDevice = GetPlayerDevice(Controller.Get());
		if (IsTouchScreenOnly())
		{
			EditedDevice = EChaosImpactDevice::KeyboardMouse;
			SetControlsStep(EControlsStep::Buttons);
			return;
		}
		SetControlsStep(EControlsStep::Devices);
		return;
	}
	if (IsDeviceCardRow(SelectedRow))
	{
		EditedDevice = static_cast<EChaosImpactDevice>(Row.Id - RowDeviceItem);
		SetControlsStep(EControlsStep::Buttons);
		return;
	}
	switch (Row.Id)
	{
	case RowTouchLayout:
	{
		// Over the whole screen, from any menu, every kind of play (starting on the one being played, if any).
		AChaosImpactPlayerController* Owner = Controller.Get();
		if (UChaosImpactMenuWidget* Menu = Owner ? Owner->GetMenuWidget() : nullptr)
		{
			Menu->OpenTouchLayoutEditor(EditedProfile, ChaosImpactSettings::GetTouchModeKey(Owner));
		}
		break;
	}
	case RowNewProfile:
		RenamingProfile = INDEX_NONE;
		NameEntry.Open(FString(), TEXT("あたらしいニックネーム"));
		break;
	case RowRenameProfile:
		RenamingProfile = EditedProfile;
		NameEntry.Open(GetProfile(EditedProfile).Name, TEXT("ニックネームを変える"));
		break;
	case RowDeleteProfile:
		if (!bDeleteArmed)
		{
			bDeleteArmed = true;
		}
		else
		{
			const FString Name = GetProfile(EditedProfile).Name;
			RemoveProfile(EditedProfile);
			EditedProfile = FMath::Clamp(EditedProfile - 1, 0, GetProfileCount() - 1);
			bDeleteArmed = false;
			ShowMessage(FString::Printf(TEXT("%s を消しました"), *Name));
			SetControlsStep(EControlsStep::Nicknames);
			return;
		}
		break;
	case RowMiniGames:
		if (UChaosImpactLoadingSubsystem* Loading = UChaosImpactLoadingSubsystem::Get(Controller.Get()))
		{
			Loading->ShowArcade();
		}
		break;
	case RowResetShoulderSensitivity:
		ResetShoulderSensitivity(EditedProfile, EditedDevice);
		ShowMessage(TEXT("肩越しの感度を100%に戻しました"));
		break;
	case RowResetDevice:
		ResetDevice(EditedProfile, EditedDevice);
		ShowMessage(FString::Printf(TEXT("%s を初期設定にもどしました"), *GetDeviceName(EditedDevice)));
		break;
	default:
		break;
	}
	BuildRows();
}

void UChaosImpactSettingsScreen::ClearSelectedKey()
{
	if (!IsSelectable(SelectedRow) || Rows[SelectedRow].Kind != ERowKind::Binding)
	{
		return;
	}
	ChaosImpactSettings::SetKey(EditedProfile, EditedDevice,
		static_cast<EChaosImpactAction>(Rows[SelectedRow].Id - RowBinding), SelectedCell, FKey());
	BuildRows();
}

void UChaosImpactSettingsScreen::ReceiveKey(const FKey& Key)
{
	using namespace ChaosImpactSettings;
	if (!bWaitingForKey || !IsSelectable(SelectedRow) || Rows[SelectedRow].Kind != ERowKind::Binding)
	{
		bWaitingForKey = false;
		return;
	}
	if (Key == EKeys::Escape)
	{
		bWaitingForKey = false;
		return;
	}
	if (!CanBind(Key, EditedDevice))
	{
		// A controller button while the keyboard is set (or the other way round) just stops waiting.
		if (Key.IsGamepadKey() != (EditedDevice != EChaosImpactDevice::KeyboardMouse))
		{
			bWaitingForKey = false;
		}
		else
		{
			ShowMessage(TEXT("そのキーは使えません"));
		}
		return;
	}
	const EChaosImpactAction Action = static_cast<EChaosImpactAction>(Rows[SelectedRow].Id - RowBinding);
	SetKey(EditedProfile, EditedDevice, Action, SelectedCell, Key);
	bWaitingForKey = false;
	ShowMessage(FString::Printf(TEXT("%s：%s"), *GetActionName(Action), *GetKeyName(Key, EditedDevice)));
	UE_LOG(LogChaosImpact, Log, TEXT("Settings: %s %s slot %d = %s"), *GetProfile(EditedProfile).Name,
		*GetActionName(Action), SelectedCell, *Key.ToString());
	BuildRows();
}

void UChaosImpactSettingsScreen::ShowMessage(const FString& Text)
{
	Message = Text;
	MessageAt = FPlatformTime::Seconds();
}

void UChaosImpactSettingsScreen::Leave()
{
	if (bControlStylePickerOpen)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
		bControlStylePickerOpen = false;
		return;
	}
	bWaitingForKey = false;
	if (HasUnsavedChanges())
	{
		// Changes not saved: asked first whether to keep them.
		bAskSave = true;
		AskChoice = ESaveAnswer::SaveAndLeave;
		AskOpenedAt = FPlatformTime::Seconds();
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiOpen);
		return;
	}
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	LeaveNow();
}

void UChaosImpactSettingsScreen::LeaveNow()
{
	bWaitingForKey = false;
	bAskSave = false;
	if (AChaosImpactPlayerController* Owner = Controller.Get())
	{
		Owner->CloseSettings();
	}
}

void UChaosImpactSettingsScreen::FinishNameEntry()
{
	using namespace ChaosImpactSettings;
	FString Name;
	if (NameEntry.ConsumeResult(Name) != FChaosImpactNameEntry::EResult::Done)
	{
		return;
	}
	if (RenamingProfile != INDEX_NONE)
	{
		if (Name == GetProfile(RenamingProfile).Name)
		{
			return;
		}
		if (RenameProfile(RenamingProfile, Name))
		{
			ShowMessage(FString::Printf(TEXT("なまえを %s にしました"), *Name));
		}
		else
		{
			ShowMessage(TEXT("その名前はもう使われています"));
		}
	}
	else if (const int32 Existing = FindProfile(Name); Existing != INDEX_NONE)
	{
		EditedProfile = Existing;
		ShowMessage(TEXT("その名前はもう使われています"));
	}
	else if (const int32 Added = AddProfile(Name); Added != INDEX_NONE)
	{
		EditedProfile = Added;
		ShowMessage(FString::Printf(TEXT("%s を作りました"), *Name));
		// On to its devices (the one this player is using first).
		EditedDevice = GetPlayerDevice(Controller.Get());
		SetControlsStep(EControlsStep::Devices);
	}
	else
	{
		ShowMessage(TEXT("これ以上作れません"));
	}
	BuildRows();
}

void UChaosImpactSettingsScreen::Tick(const float DeltaSeconds)
{
	UpdateControlVideo();
	if (!bOpen)
	{
		return;
	}
	bUnsaved = ChaosImpactSettings::HasUnsavedSettings();
	if (!bPressing && !FMath::IsNearlyZero(ScrollVelocity, 1.0f))
	{
		// Let go moving: the list glides on and slows, stopping at its ends.
		const float Before = GetScrollPixels();
		SetScrollPixels(Before + ScrollVelocity * FMath::Min(DeltaSeconds, 0.05f));
		ScrollVelocity *= FMath::Exp(-4.5f * DeltaSeconds);
		if (FMath::IsNearlyEqual(GetScrollPixels(), Before) || FMath::Abs(ScrollVelocity) < 20.0f)
		{
			ScrollVelocity = 0.0f;
		}
	}
	if (bWaitingForKey && FPlatformTime::Seconds() - WaitStartedAt > 8.0)
	{
		bWaitingForKey = false;
	}
	// F11 changes the window mode behind this screen: show what it is now.
	if (Tab == ETab::Display && IsSelectable(FindRow(RowWindowMode)))
	{
		static const TCHAR* const Modes[] = {TEXT("フルスクリーン"), TEXT("フルスクリーン"), TEXT("ウィンドウ")};
		if (Rows[FindRow(RowWindowMode)].Value != Modes[ChaosImpactSettings::GetWindowMode()])
		{
			BuildRows();
		}
	}
}

bool UChaosImpactSettingsScreen::HandleKeyDown(const FKeyEvent& Event)
{
	if (!bOpen)
	{
		return false;
	}
	const FKey RawKey = Event.GetKey();
	const int32 DeviceId = Event.GetInputDeviceId().GetId();
	const FKey Key = ChaosImpactSettings::ToMenuKey(RawKey, DeviceId);
	if (bAskSave)
	{
		// The question whether to save: left / right (or up / down) between its answers, decide, or back to the settings.
		const int32 Count = static_cast<int32>(ESaveAnswer::Count);
		const int32 Step = Key == EKeys::Left || Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Up ? -1
			: Key == EKeys::Right || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_DPad_Down ? 1 : 0;
		if (Step != 0)
		{
			AskChoice = static_cast<ESaveAnswer>(Wrap(static_cast<int32>(AskChoice) + Step, Count));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
		}
		else if (!Event.IsRepeat() && (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom))
		{
			AnswerSave(AskChoice);
		}
		else if (!Event.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right))
		{
			AnswerSave(ESaveAnswer::Stay);
		}
		return true;
	}
	if (bControlStylePickerOpen)
	{
		if (Key == EKeys::Up || Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Left) { MoveControlPreview(-1); }
		else if (Key == EKeys::Down || Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Right) { MoveControlPreview(1); }
		else if (!Event.IsRepeat() && (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)) { ApplyControlPreview(); }
		else if (!Event.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)) { Leave(); }
		return true;
	}
	if (NameEntry.IsOpen())
	{
		NameEntry.HandleKey(Key, Event.IsRepeat());
		FinishNameEntry();
		return true;
	}
	if (bWaitingForKey)
	{
		if (!Event.IsRepeat())
		{
			ReceiveKey(RawKey);
		}
		return true;
	}
	const bool bRepeat = Event.IsRepeat();
	if (bSideFocus)
	{
		// The column on the left: up and down through the tabs, 保存 and もどる; A or right goes in.
		if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
		{
			MoveSide(-1);
		}
		else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
		{
			MoveSide(1);
		}
		else if (bRepeat)
		{
			return true;
		}
		else if ((Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) && SideCursor < SideSave)
		{
			EnterRows();
		}
		else if (Key == EKeys::Q)
		{
			SetTab(static_cast<ETab>(static_cast<int32>(Tab) - 1));
		}
		else if (Key == EKeys::E || Key == EKeys::Tab)
		{
			SetTab(static_cast<ETab>(static_cast<int32>(Tab) + 1));
		}
		else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			ActivateSide();
		}
		else if (Key == EKeys::S || Key == EKeys::Gamepad_FaceButton_Left)
		{
			Save();
		}
		else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right
			|| Key == EKeys::Gamepad_Special_Right)
		{
			Leave();
		}
		return true;
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		MoveSelection(-1);
	}
	else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		MoveSelection(1);
	}
	else if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)
	{
		Adjust(-1);
	}
	else if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)
	{
		Adjust(1);
	}
	else if (bRepeat)
	{
		return true;
	}
	else if (Key == EKeys::Q)
	{
		SetTab(static_cast<ETab>(static_cast<int32>(Tab) - 1));
	}
	else if (Key == EKeys::E || Key == EKeys::Tab)
	{
		SetTab(static_cast<ETab>(static_cast<int32>(Tab) + 1));
	}
	else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		Activate();
	}
	else if (Key == EKeys::Delete || Key == EKeys::Gamepad_FaceButton_Top)
	{
		ClearSelectedKey();
	}
	else if (Key == EKeys::S || Key == EKeys::Gamepad_FaceButton_Left)
	{
		Save();
	}
	else if (Key == EKeys::Gamepad_Special_Right)
	{
		Leave();
	}
	else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		// A step back (操作's steps), then out to the tabs (a further press there leaves).
		StepBack();
	}
	return true;
}

bool UChaosImpactSettingsScreen::HandleAnalog(const FAnalogInputEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (!bOpen || (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY))
	{
		return false;
	}
	if (NameEntry.IsOpen())
	{
		return NameEntry.HandleAnalog(Key, Event.GetAnalogValue());
	}
	if (bWaitingForKey)
	{
		return true;
	}
	const float Value = Event.GetAnalogValue();
	const int32 Axis = Key == EKeys::Gamepad_LeftX ? 0 : 1;
	const double Now = FPlatformTime::Seconds();
	if (FMath::Abs(Value) < 0.3f)
	{
		bStickHeld[Axis] = false;
		return true;
	}
	if (FMath::Abs(Value) >= 0.65f && (!bStickHeld[Axis] || Now >= NextStickAt[Axis]))
	{
		NextStickAt[Axis] = Now + (bStickHeld[Axis] ? 0.13 : 0.38);
		bStickHeld[Axis] = true;
		if (bAskSave)
		{
			// (The question's answers side by side: either axis steps through them, up or left to the previous.)
			const int32 Step = Axis == 0 ? (Value > 0.0f ? 1 : -1) : (Value > 0.0f ? -1 : 1);
			AskChoice = static_cast<ESaveAnswer>(Wrap(static_cast<int32>(AskChoice) + Step, static_cast<int32>(ESaveAnswer::Count)));
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
		}
		else if (bSideFocus)
		{
			// The column: up and down through it; pushed right, into the tab's settings.
			if (Axis == 1)
			{
				MoveSide(Value > 0.0f ? -1 : 1);
			}
			else if (Value > 0.0f && SideCursor < SideSave)
			{
				EnterRows();
			}
		}
		else if (Axis == 0)
		{
			Adjust(Value > 0.0f ? 1 : -1);
		}
		else
		{
			MoveSelection(Value > 0.0f ? -1 : 1);
		}
	}
	return true;
}

bool UChaosImpactSettingsScreen::HandleCharacter(const TCHAR Character)
{
	if (!bOpen || !NameEntry.IsOpen())
	{
		return false;
	}
	NameEntry.HandleCharacter(Character);
	return true;
}

FBox2D UChaosImpactSettingsScreen::TabRect(const int32 Index) const
{
	const FSettingsLayout& L = Layout();
	if (L.bUpright)
	{
		// Upright: four chips across the top.
		const float Width = (L.PanelWidth - 36.0f) / 4.0f;
		const float X = L.PanelLeft + Index * (Width + 12.0f);
		return FBox2D(FVector2D(X, 172.0f), FVector2D(X + Width, 272.0f));
	}
	if (L.bPhone)
	{
		const float Y = 128.0f + Index * 108.0f;
		return FBox2D(FVector2D(40.0f, Y), FVector2D(400.0f, Y + 96.0f));
	}
	const float Y = 168.0f + Index * 86.0f;
	return FBox2D(FVector2D(60.0f, Y), FVector2D(380.0f, Y + 74.0f));
}

bool UChaosImpactSettingsScreen::HasStepBackButton() const
{
	// スマホ版: 操作's later steps get a button back a step in the panel's head (no B button to press).
	return Layout().bPhone && Tab == ETab::Controls && ControlsStep != EControlsStep::Nicknames && !bControlStylePickerOpen;
}

FBox2D UChaosImpactSettingsScreen::StepBackRect() const
{
	const FSettingsLayout& L = Layout();
	const float Right = L.PanelLeft + L.PanelWidth - 24.0f;
	return FBox2D(FVector2D(Right - 230.0f, L.PanelTop + 16.0f), FVector2D(Right, L.PanelTop + 80.0f));
}

FBox2D UChaosImpactSettingsScreen::RowRect(const int32 Row) const
{
	const FSettingsLayout& L = Layout();
	if (Tab == ETab::Controls && ControlsStep == EControlsStep::Devices && Rows.IsValidIndex(Row))
	{
		// The cards side by side (upright: two by two), then the nickname's own rows under them.
		const int32 Count = static_cast<int32>(EChaosImpactDevice::Count);
		const int32 PerLine = L.bUpright ? 2 : Count;
		const float CardW = L.CardWidth;
		constexpr float CardGap = 22.0f;
		const float Left = L.RowsLeft + (L.RowsWidth - (PerLine * CardW + (PerLine - 1) * CardGap)) * 0.5f;
		const int32 Lines = (Count + PerLine - 1) / PerLine;
		const float CardsBottom = L.CardTop + Lines * L.CardHeight + (Lines - 1) * 20.0f;
		if (IsDeviceCardRow(Row))
		{
			const int32 Index = Rows[Row].Id - RowDeviceItem;
			const float X = Left + (Index % PerLine) * (CardW + CardGap);
			const float Y = L.CardTop + (Index / PerLine) * (L.CardHeight + 20.0f);
			return FBox2D(FVector2D(X, Y), FVector2D(X + CardW, Y + L.CardHeight));
		}
		const float Y = CardsBottom + 24.0f + (Row - Count) * L.RowStep;
		return FBox2D(FVector2D(L.RowsLeft, Y), FVector2D(L.RowsLeft + L.RowsWidth, Y + L.RowHeight));
	}
	const float Y = L.RowsTop + (Row - FirstVisibleRow) * L.RowStep - ScrollFrac;
	return FBox2D(FVector2D(L.RowsLeft, Y), FVector2D(L.RowsLeft + L.RowsWidth, Y + L.RowHeight));
}

FBox2D UChaosImpactSettingsScreen::CellRect(const int32 Row, const int32 Cell) const
{
	const FSettingsLayout& L = Layout();
	const FBox2D Rect = RowRect(Row);
	const float X = L.CellLeft + Cell * (L.CellWidth + L.CellGap);
	const float Inset = L.bPhone ? 10.0f : 6.0f;
	return FBox2D(FVector2D(X, Rect.Min.Y + Inset), FVector2D(X + L.CellWidth, Rect.Max.Y - Inset));
}

bool UChaosImpactSettingsScreen::HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button)
{
	if (!bOpen)
	{
		return false;
	}
	bPressing = false;
	bDragging = false;
	if (Button == EKeys::LeftMouseButton && Layout().bPhone && !bAskSave && !bControlStylePickerOpen && !NameEntry.IsOpen() && !bWaitingForKey)
	{
		// スマホ版: what was touched is pressed when the finger lifts there; moved up or down first, it scrolls the rows.
		bPressing = true;
		PressPoint = DesignPoint;
		PressFirstRow = FirstVisibleRow;
		PressScroll = GetScrollPixels();
		LastDragY = static_cast<float>(DesignPoint.Y);
		LastDragAt = FPlatformTime::Seconds();
		// (A finger on a gliding list stops it, as on a phone's own lists.)
		ScrollVelocity = 0.0f;
		return true;
	}
	return Click(DesignPoint, Button);
}

void UChaosImpactSettingsScreen::HandleMouseUp(const FVector2D& DesignPoint, const FKey& Button)
{
	if (!bOpen || Button != EKeys::LeftMouseButton || !bPressing)
	{
		return;
	}
	bPressing = false;
	const bool bWasDrag = bDragging;
	bDragging = false;
	if (bWasDrag && FPlatformTime::Seconds() - LastDragAt > 0.08)
	{
		// Held still at the end: it stays.
		ScrollVelocity = 0.0f;
	}
	// (Lifted somewhere else than touched, without a drag: still the press, where it began.)
	if (!bWasDrag)
	{
		Click(PressPoint, Button);
	}
}

bool UChaosImpactSettingsScreen::Click(const FVector2D& DesignPoint, const FKey& Button)
{
	const FSettingsLayout& L = Layout();
	if (bAskSave)
	{
		if (Button == EKeys::RightMouseButton)
		{
			AnswerSave(ESaveAnswer::Stay);
		}
		else if (Button == EKeys::LeftMouseButton)
		{
			for (int32 Index = 0; Index < static_cast<int32>(ESaveAnswer::Count); ++Index)
			{
				if (AskButtonRect(Index).IsInside(DesignPoint))
				{
					AskChoice = static_cast<ESaveAnswer>(Index);
					AnswerSave(AskChoice);
					break;
				}
			}
		}
		return true;
	}
	if (bControlStylePickerOpen)
	{
		if (Button == EKeys::RightMouseButton || (Button == EKeys::LeftMouseButton && PickerBackButton.IsInside(DesignPoint))) { Leave(); }
		else if (Button == EKeys::LeftMouseButton)
		{
			const int32 Count = EditedDevice == EChaosImpactDevice::KeyboardMouse ? 4 : 2;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (ControlCard(Index).IsInside(DesignPoint))
				{
					PreviewControlScheme = static_cast<EChaosImpactControlScheme>(Index); ApplyControlPreview(); return true;
				}
			}
			if (ApplyControlButton.IsInside(DesignPoint)) { ApplyControlPreview(); }
		}
		return true;
	}
	if (NameEntry.IsOpen())
	{
		if (Button == EKeys::LeftMouseButton)
		{
			NameEntry.HandleClick(DesignPoint);
			FinishNameEntry();
		}
		return true;
	}
	if (bWaitingForKey)
	{
		ReceiveKey(Button);
		return true;
	}
	if (Button == EKeys::RightMouseButton)
	{
		if (bSideFocus)
		{
			Leave();
		}
		else
		{
			StepBack();
		}
		return true;
	}
	if (Button != EKeys::LeftMouseButton)
	{
		return true;
	}
	if (L.Back.IsInside(DesignPoint))
	{
		Leave();
		return true;
	}
	if (L.Save.IsInside(DesignPoint))
	{
		Save();
		return true;
	}
	if (HasStepBackButton() && StepBackRect().ExpandBy(8.0).IsInside(DesignPoint))
	{
		StepBack();
		return true;
	}
	for (int32 Index = 0; Index < static_cast<int32>(ETab::Count); ++Index)
	{
		if (TabRect(Index).IsInside(DesignPoint))
		{
			SetTab(static_cast<ETab>(Index));
			bSideFocus = true;
			return true;
		}
	}
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + ShownRows() + 1); ++Row)
	{
		if (!RowRect(Row).IsInside(DesignPoint) || !IsSelectable(Row) || (!IsDeviceCardRow(Row) && !RowsArea().IsInside(DesignPoint)))
		{
			continue;
		}
		const FRow& Clicked = Rows[Row];
		if (Clicked.Id == RowControlScheme || Clicked.Id == RowControlPreview) { SelectRow(Row); OpenControlStylePicker(); return true; }
		if (Clicked.Kind == ERowKind::Binding)
		{
			const int32 Cell = CellRect(Row, 1).IsInside(DesignPoint) ? 1 : CellRect(Row, 0).IsInside(DesignPoint) ? 0 : -1;
			SelectRow(Row, FMath::Max(Cell, 0));
			if (Cell >= 0)
			{
				Activate();
			}
		}
		else if (Clicked.Kind == ERowKind::Value)
		{
			SelectRow(Row);
			// The left half of the value steps back, the right half forward.
			if (DesignPoint.X >= L.CellLeft)
			{
				Adjust(DesignPoint.X < L.CellLeft + L.CellWidth + L.CellGap * 0.5f ? -1 : 1);
			}
		}
		else
		{
			SelectRow(Row);
			Activate();
		}
		return true;
	}
	return true;
}

void UChaosImpactSettingsScreen::HandleMouseMove(const FVector2D& DesignPoint)
{
	if (bAskSave)
	{
		for (int32 Index = 0; Index < static_cast<int32>(ESaveAnswer::Count); ++Index)
		{
			if (AskButtonRect(Index).IsInside(DesignPoint) && Index != static_cast<int32>(AskChoice))
			{
				AskChoice = static_cast<ESaveAnswer>(Index);
				ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
			}
		}
		return;
	}
	if (bControlStylePickerOpen)
	{
		const int32 Count = EditedDevice == EChaosImpactDevice::KeyboardMouse ? 4 : 2;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (ControlCard(Index).IsInside(DesignPoint) && Index != static_cast<int32>(PreviewControlScheme))
			{ PreviewControlScheme = static_cast<EChaosImpactControlScheme>(Index); PreviewStartedAt = FPlatformTime::Seconds(); UpdateControlVideo(); }
		}
		return;
	}
	if (!bOpen || bWaitingForKey)
	{
		return;
	}
	if (NameEntry.IsOpen())
	{
		NameEntry.HandleMouseMove(DesignPoint);
		return;
	}
	const FSettingsLayout& L = Layout();
	if (bPressing)
	{
		// スマホ版: a finger moved up or down over the panel scrolls its rows (and is no longer a press).
		const float Moved = static_cast<float>(DesignPoint.Y - PressPoint.Y);
		const bool bInPanel = PressPoint.X >= L.PanelLeft && PressPoint.X <= L.PanelLeft + L.PanelWidth && PressPoint.Y >= L.PanelTop
			&& PressPoint.Y <= L.PanelTop + L.PanelHeight;
		if (!bDragging && (FMath::Abs(Moved) > 14.0f || FMath::Abs(DesignPoint.X - PressPoint.X) > 40.0))
		{
			bDragging = true;
			DragOriginY = static_cast<float>(DesignPoint.Y);
			PressScroll = GetScrollPixels();
		}
		if (bDragging && bInPanel && Rows.Num() > ShownRows())
		{
			// Following the finger by pixels (not a row at a time); the speed it moves at is kept to glide on with.
			SetScrollPixels(PressScroll - (static_cast<float>(DesignPoint.Y) - DragOriginY));
			const double NowAt = FPlatformTime::Seconds();
			const float Step = static_cast<float>(FMath::Max(NowAt - LastDragAt, 0.004));
			ScrollVelocity = FMath::Lerp(ScrollVelocity, -(static_cast<float>(DesignPoint.Y) - LastDragY) / Step, 0.5f);
			LastDragY = static_cast<float>(DesignPoint.Y);
			LastDragAt = NowAt;
		}
		return;
	}
	if (L.bPhone && IsTouchScreenOnly())
	{
		// (No pointer to follow by touch: a row is picked when it is pressed.)
		return;
	}
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + ShownRows() + 1); ++Row)
	{
		if (RowRect(Row).IsInside(DesignPoint) && IsSelectable(Row) && (IsDeviceCardRow(Row) || RowsArea().IsInside(DesignPoint)))
		{
			const int32 Cell = Rows[Row].Kind == ERowKind::Binding && CellRect(Row, 1).IsInside(DesignPoint) ? 1 : 0;
			if (Row != SelectedRow || Cell != SelectedCell || bSideFocus)
			{
				SelectedRow = Row;
				SelectedCell = Cell;
				bDeleteArmed = false;
				bSideFocus = false;
				BuildRows();
			}
			return;
		}
	}
}

EChaosImpactDevice UChaosImpactSettingsScreen::HintDevice() const
{
	// Alone on the machine: what this player last pressed (their nickname's controller kind too). Several: anyone can work
	// this screen, so whatever was pressed last.
	const AChaosImpactPlayerController* Owner = Controller.Get();
	return Owner && Owner->FollowsLastDevice() ? ChaosImpactHints::GetDevice(Owner) : ChaosImpactHints::GetLastDevice();
}

FString UChaosImpactSettingsScreen::HintText() const
{
	// (はずす only where there are keys to take off.)
	if (bSideFocus)
	{
		return ChaosImpactHints::Text(HintDevice(),
			TEXT("{pad:{DPad}・スティック↑↓：タブをえらぶ　　{Confirm}：決定　　{X}：保存　　{Back}：もどる}")
			TEXT("{kb:↑↓：タブをえらぶ　　Enter・→：決定　　S：保存　　Esc：もどる}"));
	}
	if (Tab == ETab::Controls && ControlsStep != EControlsStep::Buttons)
	{
		return ChaosImpactHints::Text(HintDevice(), ControlsStep == EControlsStep::Devices
			? TEXT("{pad:{DPad}・スティック：えらぶ　　{Confirm}：決定　　{X}：保存　　{Back}：ニックネームへ}")
				TEXT("{kb:↑↓←→：えらぶ　　Enter：決定　　S：保存　　Esc：ニックネームへ}")
			: TEXT("{pad:{DPad}・スティック：えらぶ　　{Confirm}：決定　　{X}：保存　　{Back}：タブへ}")
				TEXT("{kb:↑↓：えらぶ　　Enter：決定　　S：保存　　Esc：タブへ}"));
	}
	return ChaosImpactHints::Text(HintDevice(), Tab == ETab::Controls
		? TEXT("{pad:{DPad}・スティック：えらぶ・変える　　{Confirm}：決定　　{Y}：ボタンをはずす　　{X}：保存　　{Back}：デバイスへ}")
			TEXT("{kb:↑↓：えらぶ　　←→：変える　　Enter：決定　　Delete：キーをはずす　　S：保存　　Esc：デバイスへ}")
		: TEXT("{pad:{DPad}・スティック：えらぶ・変える　　{Confirm}：決定　　{X}：保存　　{Back}：タブへ}")
			TEXT("{kb:↑↓：えらぶ　　←→：変える　　Enter：決定　　S：保存　　Esc：タブへ}"));
}

FString UChaosImpactSettingsScreen::GetRowHelp(const int32 Id) const
{
	if (Id >= RowBinding)
	{
		return ChaosImpactHints::Text(HintDevice(), TEXT("{pad:{Confirm}でボタンを登録（ひとつの操作に2つまで）・{Y}ボタンではずします}")
			TEXT("{kb:Enterでキーを登録（ひとつの操作に2つまで）・Deleteではずします}"));
	}
	if (Id >= RowProfileItem && Id < RowDeviceItem)
	{
		return TEXT("このニックネームのボタン設定をえらびます（右のマークは、設定を変えたデバイス）");
	}
	if (Id >= RowDeviceItem && Id < RowBinding)
	{
		return TEXT("このデバイスで遊ぶときの設定です");
	}
	if (Id >= RowQualityGroup && Id < RowProfile)
	{
		return TEXT("この項目だけの画質です。重いときはここを下げても軽くなります");
	}
	switch (Id)
	{
	// (The keyboard's and the mouse's ways only for them.)
	case RowWindowMode: return ChaosImpactHints::Text(HintDevice(), TEXT("フルスクリーン・ボーダーレス・ウィンドウを切り替えます{kb:（F11でも切り替え）}"));
	case RowResolution: return ChaosImpactHints::Text(HintDevice(),
		TEXT("画面（ウィンドウ）の大きさです。21:9・32:9の横長は左右が広く見えます{kb:（ウィンドウは端のドラッグでも変えられます）}"));
	case RowVSync: return TEXT("ONにすると画面のちらつきを防ぎます。入力が少し遅れることがあります");
	case RowFrameRate: return TEXT("1秒あたりのコマ数の上限です");
	case RowShowFps: return TEXT("いまの1秒あたりのコマ数（fps）を画面の左上に小さく出します。どの画面でも出ます");
	case RowBrightness: return TEXT("画面全体の明るさです");
	case RowScreenOrientation: return TEXT("自動回転：持ち方に合わせて回ります。縦画面・横画面：その向きで止めます。操作ボタンの配置は縦と横で別々に保存されます");
	case RowQuality: return TEXT("画質をまとめて変えます。動きが重いときは下げてください");
	case RowResolutionScale: return TEXT("描く細かさです。下げると軽くなります");
	case RowProfile: return TEXT("操作の設定をしまっておく名前（ニックネーム）です。人ごとに分けられます");
	case RowNewProfile: return TEXT("新しいニックネームを作ります");
	case RowRenameProfile: return TEXT("このニックネームの名前を変えます");
	case RowDeleteProfile: return TEXT("このニックネームを消します（もう一度で決定）");
	case RowDevice: return TEXT("設定するのがキーボード・マウスか、コントローラーかです（下の項目はこのデバイスのもの）");
	case RowPadChoice: return TEXT("ボタンの表示（A/B・○/×など）をどのコントローラーに合わせるかです");
	case RowSwapSticks: return TEXT("左右のスティックの役割を入れ替えます");
	case RowAimAssist: return TEXT("投げるときに、ねらいを少しだけ補助します");
	case RowThrowToMove: return TEXT("オンにすると、マウスや右スティックではなく、移動している方向に投げます");
	case RowWheelSwap: return TEXT("オンにすると、マウスのホイールを回しても持っている2個のボールを入れ替えます");
	case RowTouchLeftHanded: return TEXT("オンにすると、左半分でボール（投げる・タックル）、右半分で体（移動・ダッシュ・ジャンプ）を操作します");
	case RowTouchLayout: return TEXT("ボタン・HP・ボールの表示を、ドラッグで動かしたり大きさを変えたりします（モードごとに別々）");
	case RowTouchStyle: return TEXT("ボタン方式：画面のボタンで操作。ジェスチャー方式：左右をなぞる・はじく。片手モード：親指1本でなぞって移動、タップで投げる、長押しでため・なぞってねらう");
	case RowTouchFlick: return TEXT("ダッシュ・タックルになる「はじく」の速さです。やわらかめほど軽くはじいただけで出ます");
	case RowTouchAutoAim: return TEXT("オンにすると、タップや、なぞらずにためた球は近くの相手へ自動でねらいます");

	case RowCameraDistance: return TEXT("見下ろしカメラの距離です。近いと迫力が、遠いと見える範囲が増えます（人ごと・分割画面でも別々）");
	case RowResetDevice: return TEXT("このデバイスのボタン設定を、はじめの状態にもどします");
	case RowControlScheme: return TEXT("操作のしかたです。下の「図と動画で見る」で見比べられます");
	case RowMouseDash: return TEXT("ダッシュをどの操作で出すかです");
	case RowControlPreview: return TEXT("それぞれの操作スタイルを図と動画で見比べて、そのまま選べます");
	case RowShoulderSensitivityX:
	case RowShoulderSensitivityY: return TEXT("肩越し視点で、カメラが動く速さです");
	case RowResetShoulderSensitivity: return TEXT("肩越しの感度を100%にもどします");
	case RowVolume: return TEXT("ゲーム全体の音の大きさです");
	case RowMusicVolume: return TEXT("BGM（音楽）だけの大きさです。効果音はそのままです");
	case RowRumble: return TEXT("コントローラーを振動させるかどうかです");
	case RowMenuRumble: return TEXT("メニューでカーソルを動かしたり決定したりしたときに、コントローラーを軽く振動させます");
	case RowShake: return TEXT("当たったときなどに、画面がどれだけ揺れるかです");
	case RowSwitchConfirm: return TEXT("Switchのコントローラーで「決定」に使うボタンです");
	case RowHudSize: return TEXT("プレイ中の画面に出る表示（体力・ボール・順位・ヒントなど）の大きさです。全モード共通です");
	case RowButtonHints: return TEXT("プレイ中、画面の左右にいま使える操作のボタンを出すかどうかです");
	case RowOthersPaint: return TEXT("ほかの人がカラーエディットでキャラに描いた絵を表示するかどうかです（自分の絵はいつも表示）");
	case RowTutorial: return TEXT("トレーニングに入るたびに、操作とはじめて持ったボールを教えます");
	case RowMiniGames: return TEXT("ロード画面のミニゲームを、それだけで遊べます");
	default: return FString();
	}
}

int32 UChaosImpactSettingsScreen::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	if (!bOpen)
	{
		return Layer;
	}
	if (bControlStylePickerOpen) { return PaintControlStylePicker(Design,Elements,Layer); }
	const FSettingsLayout& L = Layout();
	// (By touch alone: no keys named, no cursor drawn; a press lights what it picks.)
	const bool bTouch = L.bPhone && IsTouchScreenOnly();
	const double Now = FPlatformTime::Seconds();
	const float In = EaseOut(static_cast<float>(Now - OpenedAt) / 0.3f);
	const FPainter P{Design, Elements, Layer, In};
	const FPainter Top{Design, Elements, Layer + 1, In};
	const FPainter Text{Design, Elements, Layer + 2, In};
	const bool bPhoneRows = UChaosImpactMenuWidget::IsPhoneBuild();
	const TCHAR* const TabNotes[] = {bPhoneRows ? TEXT("明るさ・なめらかさ") : TEXT("画面モード・大きさ・なめらかさ"), TEXT("見た目の細かさ・動きの軽さ"),
		bTouch ? TEXT("タッチ操作・カメラ") : ChaosImpactSettings::IsNormalControlsOnly() ? TEXT("感度・カメラ・ボタン") : TEXT("操作スタイル・感度・ボタン"),
		bTouch ? TEXT("音量・画面の揺れ・表示") : TEXT("音量・振動・画面の揺れ")};
	/** The chips upright: short names. */
	const TCHAR* const ChipNames[] = {TEXT("画面"), TEXT("画質"), TEXT("操作"), TEXT("音・その他")};
	const FLinearColor TabColors[] = {Fire, Gold, Ice, Violet};
	const FLinearColor Accent = TabColors[FMath::Clamp(static_cast<int32>(Tab), 0, 3)];
	const float Pulse5 = 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 5.0f);

	// ---- The title, and the tabs (down the side; upright, across the top).
	HudText3D(Text, TEXT("SETTINGS"), L.TitleX, L.TitleY, L.TitleSize, Paper, ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
	P.Box(L.TitleX + 4.0f, L.TitleRule, 120.0f, 6.0f, Ice);
	P.Box(L.TitleX + 128.0f, L.TitleRule, 60.0f, 6.0f, Fire);
	for (int32 Index = 0; Index < static_cast<int32>(ETab::Count); ++Index)
	{
		const FBox2D Rect = TabRect(Index);
		const bool bCurrent = Index == static_cast<int32>(Tab);
		const float X = static_cast<float>(Rect.Min.X);
		const float Y = static_cast<float>(Rect.Min.Y);
		const float W = static_cast<float>(Rect.GetSize().X);
		const float H = static_cast<float>(Rect.GetSize().Y);
		if (bCurrent)
		{
			const float Grow = EaseOut(static_cast<float>(Now - TabChangedAt) / 0.2f);
			FLinearColor Deep = TabColors[Index] * 0.35f;
			Deep.A = 0.97f;
			HudPoly(P, {FVector2D(X, Y), FVector2D(X + W * (0.6f + 0.4f * Grow), Y), FVector2D(X + W * (0.6f + 0.4f * Grow), Y + H), FVector2D(X, Y + H)},
				MakeFill(FMath::Lerp(TabColors[Index], Paper, 0.1f) * FLinearColor(0.55f, 0.55f, 0.55f, 1.0f), Deep));
			if (L.bUpright)
			{
				Top.Box(X, Y + H - 6.0f, W, 6.0f, TabColors[Index]);
			}
			else
			{
				Top.Box(X, Y, 6.0f, H, TabColors[Index]);
			}
			Top.Outline(X, Y, W, H, WithAlpha(Paper, 0.25f), 1.0f);
		}
		else
		{
			RoundedBox(P, X, Y, W, H, 10.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.85f));
			if (L.bUpright)
			{
				Top.Box(X + 14.0f, Y + H - 5.0f, W - 28.0f, 4.0f, WithAlpha(TabColors[Index], 0.6f));
			}
			else
			{
				Top.Box(X, Y + 14.0f, 4.0f, H - 28.0f, WithAlpha(TabColors[Index], 0.6f));
			}
		}
		if (bSideFocus && Index == SideCursor && !bTouch)
		{
			// The cursor: a gold frame round the tab it is on.
			Top.Outline(X - 4.0f, Y - 4.0f, W + 8.0f, H + 8.0f, WithAlpha(Gold, Pulse5), 3.0f);
		}
		const FLinearColor NameColor = bCurrent ? Paper : WithAlpha(Paper, 0.7f);
		if (L.bUpright)
		{
			// A chip: its short name in the middle.
			const float Size = HudFit(ChipNames[Index], L.TabName, W - 20.0f, TEXT("Black"));
			Text.Text(ChipNames[Index], X + W * 0.5f, Y + H * 0.5f - Size * 0.72f - 2.0f, Size, NameColor, ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			const float NameY = L.bPhone ? Y + 10.0f : Y + 6.0f;
			const float NoteY = L.bPhone ? Y + 58.0f : Y + 46.0f;
			Text.Text(TabNames[Index], X + 26.0f, NameY, HudFit(TabNames[Index], L.TabName, W - 40.0f, TEXT("Black")), NameColor, ETextAlign::Left, TEXT("Black"));
			Text.Text(TabNotes[Index], X + 27.0f, NoteY, HudFit(TabNotes[Index], L.TabNote, W - 40.0f, TEXT("Bold")), bCurrent ? WithAlpha(Paper, 0.85f) : Muted,
				ETextAlign::Left, TEXT("Bold"));
		}
	}
	if (!L.bPhone)
	{
		// How to change tabs, under them.
		const FString TabKeys = bSideFocus ? ChaosImpactHints::Text(HintDevice(), TEXT("{Confirm|Enter}：この設定へ"))
			: ChaosImpactHints::Text(HintDevice(), TEXT("{Back|Esc}：タブにもどる"));
		Text.Text(TabKeys, 64.0f, TabRect(static_cast<int32>(ETab::Count) - 1).Max.Y + 16.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	}

	// ---- The tab's panel.
	const float PanelLeft = L.PanelLeft;
	const float PanelTop = L.PanelTop;
	const float PanelWidth = L.PanelWidth;
	const float PanelHeight = L.PanelHeight;
	P.Box(PanelLeft + 12.0f, PanelTop + 14.0f, PanelWidth, PanelHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	HudPoly(P, {FVector2D(PanelLeft, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop + PanelHeight),
		FVector2D(PanelLeft, PanelTop + PanelHeight)}, MakeFill(FLinearColor(0.035f, 0.05f, 0.1f, 0.96f), FLinearColor(0.006f, 0.008f, 0.02f, 0.97f)));
	HudPoly(Top, {FVector2D(PanelLeft, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop + 6.0f),
		FVector2D(PanelLeft, PanelTop + 6.0f)}, MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
	Top.Outline(PanelLeft, PanelTop, PanelWidth, PanelHeight, WithAlpha(Paper, 0.1f), 1.0f);
	HudText3D(Text, TabNames[static_cast<int32>(Tab)], PanelLeft + 30.0f, PanelTop + 22.0f, L.PanelTitle, Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
	// スマホ版: back a step in 操作 (top right of the panel).
	float HeadRight = PanelLeft + PanelWidth - 40.0f;
	if (HasStepBackButton())
	{
		const FBox2D Rect = StepBackRect();
		const float X = static_cast<float>(Rect.Min.X), Y = static_cast<float>(Rect.Min.Y);
		const float W = static_cast<float>(Rect.GetSize().X), H = static_cast<float>(Rect.GetSize().Y);
		RoundedBox(Top, X, Y, W, H, H * 0.5f, FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
		Top.Outline(X, Y, W, H, WithAlpha(Accent, 0.6f), 1.5f);
		const FString Label = TEXT("◀ ニックネーム");
		const float Size = HudFit(Label, 24.0f, W - 30.0f, TEXT("Black"));
		Text.Text(Label, X + W * 0.5f, Y + H * 0.5f - Size * 0.72f, Size, Paper, ETextAlign::Center, TEXT("Black"));
		HeadRight = X - 16.0f;
	}
	{
		// (操作: which step, and whose / which device.)
		const FString Whose = Tab != ETab::Controls ? FString(TabNotes[static_cast<int32>(Tab)])
			: ControlsStep == EControlsStep::Nicknames ? FString(TEXT("ニックネームをえらぶ"))
			: ControlsStep == EControlsStep::Devices ? FString::Printf(TEXT("「%s」 ▶ デバイスをえらぶ"), *ChaosImpactSettings::GetProfile(EditedProfile).Name)
			: FString::Printf(TEXT("「%s」 ▶ %s"), *ChaosImpactSettings::GetProfile(EditedProfile).Name,
				IsTouchSettings() ? TEXT("タッチ操作") : *ChaosImpactSettings::GetDeviceName(EditedDevice));
		const float NoteLeft = PanelLeft + 32.0f + HudMeasure(TabNames[static_cast<int32>(Tab)], L.PanelTitle, TEXT("Black")) + 24.0f;
		const bool bStep = Tab == ETab::Controls;
		const float WantSize = bStep ? L.PanelNote : L.PanelNote * 0.8f;
		const float Size = HudFit(Whose, WantSize, HeadRight - NoteLeft, TEXT("Bold"));
		Text.Text(Whose, NoteLeft, PanelTop + 22.0f + L.PanelTitle * 0.72f - Size * 0.72f + 2.0f, Size,
			bStep ? FMath::Lerp(Accent, Paper, 0.45f) : Muted, ETextAlign::Left, TEXT("Bold"));
	}

	// ---- The rows: a clean list; the chosen one lit in the tab's colour.
	const float RowsLeft = L.RowsLeft;
	const float RowsWidth = L.RowsWidth;
	const float RowHeight = L.RowHeight;
	const float CellLeft = L.CellLeft;
	const float CellWidth = L.CellWidth;
	const float CellGap = L.CellGap;
	const int32 VisibleRows = ShownRows();
	const float RowsIn = EaseOut(static_cast<float>(Now - TabChangedAt) / 0.22f);
	// (Scrolled part way by a finger: one more row, the one partly in view, and both ends clipped to the rows' area.)
	const bool bClipRows = ScrollFrac > 0.01f && !(Tab == ETab::Controls && ControlsStep == EControlsStep::Devices);
	if (bClipRows)
	{
		const FBox2D Area = RowsArea();
		Elements.PushClip(FSlateClippingZone(Design.ToPaintGeometry(FVector2f(Area.GetSize()), FSlateLayoutTransform(FVector2f(Area.Min)))));
	}
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + VisibleRows + (bClipRows ? 1 : 0)); ++Row)
	{
		const FRow& Item = Rows[Row];
		const FBox2D Rect = RowRect(Row);
		const float Slide = (1.0f - RowsIn) * (30.0f + 8.0f * (Row - FirstVisibleRow));
		const float X = static_cast<float>(Rect.Min.X) + Slide;
		const float Y = static_cast<float>(Rect.Min.Y);
		const FPainter RowBack{Design, Elements, Layer, In * RowsIn};
		const FPainter RowMid{Design, Elements, Layer + 1, In * RowsIn};
		const FPainter RowText{Design, Elements, Layer + 2, In * RowsIn};
		if (IsDeviceCardRow(Row))
		{
			PaintDeviceCard(Design, Elements, Layer, Row, In * RowsIn, Now);
			continue;
		}
		if (Item.Kind == ERowKind::Heading)
		{
			// (A phone's heading sits at the foot of its tall row, by what it heads.)
			const float HeadY = L.bPhone ? Y + RowHeight - L.Heading * 1.3f - 14.0f : Y + 20.0f;
			RowText.Text(Item.Label, X + 8.0f, HeadY, HudFit(Item.Label, L.Heading, CellLeft - RowsLeft - 20.0f), Accent, ETextAlign::Left, TEXT("Black"));
			RowBack.Box(X + 8.0f, Y + RowHeight - 6.0f, RowsWidth - 16.0f, 1.0f, WithAlpha(Accent, 0.4f));
			if (Item.Id == 1)
			{
				RowText.Text(TEXT("1つめ"), CellLeft + Slide + CellWidth * 0.5f, HeadY + 2.0f, L.Small, Muted, ETextAlign::Center, TEXT("Bold"));
				RowText.Text(TEXT("2つめ"), CellLeft + Slide + CellWidth * 1.5f + CellGap, HeadY + 2.0f, L.Small, Muted, ETextAlign::Center, TEXT("Bold"));
			}
			continue;
		}
		// (While the cursor is in the tab column, no row is lit.)
		const bool bSelected = Row == SelectedRow && !bSideFocus;
		if (bSelected)
		{
			FLinearColor Lit = Accent * 0.28f;
			Lit.A = 0.95f;
			HudPoly(RowBack, {FVector2D(X, Y), FVector2D(X + RowsWidth, Y), FVector2D(X + RowsWidth, Y + RowHeight), FVector2D(X, Y + RowHeight)},
				MakeFill(Lit, FLinearColor(0.02f, 0.03f, 0.07f, 0.95f)));
			RoundedBox(RowMid, X, Y + 6.0f, 6.0f, RowHeight - 12.0f, 3.0f, Accent);
			RowMid.Outline(X, Y, RowsWidth, RowHeight, WithAlpha(Paper, 0.18f), 1.0f);
		}
		else if (L.bPhone)
		{
			// スマホ版: every row a plate of its own (what can be pressed is plain to see).
			RoundedBox(RowBack, X, Y, RowsWidth, RowHeight, 12.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.035f));
		}
		else
		{
			RowBack.Box(X + 8.0f, Y + RowHeight - 1.0f, RowsWidth - 16.0f, 1.0f, WithAlpha(Paper, 0.06f));
		}
		const bool bInfo = Item.Kind == ERowKind::Info;
		const bool bProfileItem = Item.Id >= RowProfileItem && Item.Id < RowDeviceItem;
		const FLinearColor LabelColor = Item.bDisabled ? Muted : bInfo ? WithAlpha(Paper, 0.7f) : Paper;
		// What the row's name has room for: up to its value, its key cells, or what it shows on its right.
		const float ProfileMarks = bProfileItem ? (bTouch ? 140.0f : 4.0f * 62.0f + 160.0f) : 0.0f;
		const float ButtonValue = Item.Kind == ERowKind::Button && !Item.Value.IsEmpty()
			? HudMeasure(Item.Value, L.Value * 0.85f, TEXT("Bold")) + 80.0f : 70.0f;
		const float LabelRight = Item.Kind == ERowKind::Value || Item.Kind == ERowKind::Binding ? CellLeft - 16.0f
			: bProfileItem ? RowsLeft + RowsWidth - ProfileMarks - 16.0f
			: RowsLeft + RowsWidth - FMath::Max(ButtonValue, bInfo && !Item.Value.IsEmpty() ? FMath::Min(HudMeasure(Item.Value, L.Value * 0.85f, TEXT("Bold")), 320.0f) + 50.0f : 70.0f);
		const float LabelRoom = LabelRight - RowsLeft - 30.0f;
		const float LabelSize = HudFit(Item.Label, bInfo ? L.Info : L.Label, LabelRoom, TEXT("Bold"));
		RowText.Text(Item.Label, X + 30.0f, Y + RowHeight * 0.5f - LabelSize * 0.72f, LabelSize, LabelColor, ETextAlign::Left, TEXT("Bold"));
		if (Item.Kind == ERowKind::Binding)
		{
			for (int32 Cell = 0; Cell < 2; ++Cell)
			{
				const FBox2D CellBox = CellRect(Row, Cell);
				const float CellX = static_cast<float>(CellBox.Min.X) + Slide;
				const float CellH = static_cast<float>(CellBox.GetSize().Y);
				const bool bCell = bSelected && Cell == SelectedCell;
				const bool bWaiting = bCell && bWaitingForKey;
				RoundedBox(RowMid, CellX, static_cast<float>(CellBox.Min.Y), CellWidth, CellH, 8.0f,
					bWaiting ? WithAlpha(Gold, 0.35f + 0.25f * FMath::Sin(static_cast<float>(Now) * 8.0f))
					: bCell ? WithAlpha(Accent, 0.45f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
				if (bCell)
				{
					RowText.Outline(CellX, static_cast<float>(CellBox.Min.Y), CellWidth, CellH, WithAlpha(Paper, 0.6f), 1.5f);
				}
				const FString Shown = bWaiting ? FString(TEXT("おしてください…")) : Item.Cells[Cell];
				const float ShownSize = HudFit(Shown, L.Value * 0.9f, CellWidth - 20.0f, TEXT("Bold"));
				const FPainter CellText{Design, Elements, Layer + 3, In * RowsIn};
				CellText.Text(Shown, CellX + CellWidth * 0.5f, static_cast<float>(CellBox.Min.Y) + CellH * 0.5f - ShownSize * 0.72f, ShownSize,
					Item.Cells[Cell] == TEXT("－") && !bWaiting ? Muted : Paper, ETextAlign::Center, TEXT("Bold"));
			}
		}
		else if (Item.Kind == ERowKind::Value)
		{
			// The value between ◀ ▶ (nudging while chosen; スマホ版: two buttons to press).
			const float AreaWidth = CellWidth * 2.0f + CellGap;
			const float AreaLeft = CellLeft + Slide;
			const float Centre = AreaLeft + AreaWidth * 0.5f;
			const float ArrowRoom = L.ArrowButton > 0.0f ? L.ArrowButton * 2.0f + 24.0f : 80.0f;
			const float ValueSize = HudFit(Item.Value, L.Value, AreaWidth - ArrowRoom, TEXT("Black"));
			RowText.Text(Item.Value, Centre, Y + RowHeight * 0.5f - ValueSize * 0.72f, ValueSize, Item.bDisabled ? Muted : bSelected ? Paper : WithAlpha(Paper, 0.85f),
				ETextAlign::Center, TEXT("Black"));
			if (!Item.bDisabled)
			{
				const FLinearColor Arrow = bSelected ? FMath::Lerp(Accent, Paper, 0.25f) : L.bPhone ? WithAlpha(Paper, 0.8f) : WithAlpha(Muted, 0.6f);
				const float MidY = Y + RowHeight * 0.5f;
				if (L.ArrowButton > 0.0f)
				{
					const float BH = RowHeight - 24.0f;
					const float BW = L.ArrowButton;
					RoundedBox(RowMid, AreaLeft, MidY - BH * 0.5f, BW, BH, 12.0f, bSelected ? WithAlpha(Accent, 0.35f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
					RoundedBox(RowMid, AreaLeft + AreaWidth - BW, MidY - BH * 0.5f, BW, BH, 12.0f, bSelected ? WithAlpha(Accent, 0.35f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
					const float LX = AreaLeft + BW * 0.5f - 4.0f;
					const float RX = AreaLeft + AreaWidth - BW * 0.5f + 4.0f;
					RowText.Line(FVector2D(LX + 9.0f, MidY - 13.0f), FVector2D(LX - 4.0f, MidY), Arrow, 5.0f);
					RowText.Line(FVector2D(LX - 4.0f, MidY), FVector2D(LX + 9.0f, MidY + 13.0f), Arrow, 5.0f);
					RowText.Line(FVector2D(RX - 9.0f, MidY - 13.0f), FVector2D(RX + 4.0f, MidY), Arrow, 5.0f);
					RowText.Line(FVector2D(RX + 4.0f, MidY), FVector2D(RX - 9.0f, MidY + 13.0f), Arrow, 5.0f);
				}
				else
				{
					const float Nudge = bSelected ? 3.0f * FMath::Sin(static_cast<float>(Now) * 6.0f) : 0.0f;
					const float LX = AreaLeft + 16.0f - Nudge;
					const float RX = AreaLeft + AreaWidth - 16.0f + Nudge;
					RowText.Line(FVector2D(LX + 7.0f, MidY - 9.0f), FVector2D(LX - 2.0f, MidY), Arrow, 3.5f);
					RowText.Line(FVector2D(LX - 2.0f, MidY), FVector2D(LX + 7.0f, MidY + 9.0f), Arrow, 3.5f);
					RowText.Line(FVector2D(RX - 7.0f, MidY - 9.0f), FVector2D(RX + 2.0f, MidY), Arrow, 3.5f);
					RowText.Line(FVector2D(RX + 2.0f, MidY), FVector2D(RX - 7.0f, MidY + 9.0f), Arrow, 3.5f);
				}
			}
		}
		else if (Item.Kind == ERowKind::Button)
		{
			// A button: what it does now on the right, and a chevron.
			const float RightX = RowsLeft + Slide + RowsWidth - 26.0f;
			const float MidY = Y + RowHeight * 0.5f;
			if (!Item.Value.IsEmpty())
			{
				const float Size = HudFit(Item.Value, L.Value * 0.85f, FMath::Max(RowsWidth * 0.3f, 120.0f), TEXT("Bold"));
				RowText.Text(Item.Value, RightX - 26.0f, MidY - Size * 0.72f, Size, Muted, ETextAlign::Right, TEXT("Bold"));
			}
			const FLinearColor Chevron = Item.bDisabled ? WithAlpha(Muted, 0.4f) : bSelected ? Paper : WithAlpha(Paper, 0.45f);
			const float Arm = L.bPhone ? 12.0f : 8.0f;
			RowText.Line(FVector2D(RightX - Arm + 2.0f, MidY - Arm), FVector2D(RightX + 2.0f, MidY), Chevron, L.bPhone ? 4.0f : 3.0f);
			RowText.Line(FVector2D(RightX + 2.0f, MidY), FVector2D(RightX - Arm + 2.0f, MidY + Arm), Chevron, L.bPhone ? 4.0f : 3.0f);
		}
		else if (!Item.Value.IsEmpty())
		{
			const float Size = HudFit(Item.Value, L.Value * 0.85f, 320.0f, TEXT("Bold"));
			RowText.Text(Item.Value, RowsLeft + Slide + RowsWidth - 30.0f, Y + RowHeight * 0.5f - Size * 0.72f, Size, Muted, ETextAlign::Right, TEXT("Bold"));
		}
		if (Item.Id == RowDeleteProfile && bDeleteArmed)
		{
			RoundedBox(RowText, X, Y + RowHeight - 4.0f, RowsWidth, 4.0f, 2.0f, Fire);
		}
		if (bProfileItem)
		{
			// A nickname: the devices it has its own buttons for (lit; not by touch alone), and 使用中 on the one this player plays as.
			const int32 Profile = Item.Id - RowProfileItem;
			const float MarksRight = RowsLeft + Slide + RowsWidth - 60.0f;
			float BadgeRight = MarksRight;
			if (!bTouch)
			{
				for (int32 Device = 0; Device < static_cast<int32>(EChaosImpactDevice::Count); ++Device)
				{
					const bool bChanged = IsDeviceChanged(Profile, static_cast<EChaosImpactDevice>(Device));
					const float MX = MarksRight - (static_cast<int32>(EChaosImpactDevice::Count) - Device) * 62.0f;
					RoundedBox(RowMid, MX, Y + 10.0f, 54.0f, RowHeight - 20.0f, 6.0f, bChanged ? WithAlpha(Accent, 0.55f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.05f));
					static const TCHAR* const Short[] = {TEXT("KB"), TEXT("Xbox"), TEXT("PS"), TEXT("SW")};
					RowText.Text(Short[Device], MX + 27.0f, Y + RowHeight * 0.5f - 10.0f, HudFit(Short[Device], 14.0f, 48.0f, TEXT("Black")),
						bChanged ? Paper : WithAlpha(Paper, 0.3f), ETextAlign::Center, TEXT("Black"));
				}
				BadgeRight = MarksRight - 4.0f * 62.0f - 18.0f;
			}
			if (Profile == ChaosImpactSettings::GetPlayerProfile(Controller.Get()))
			{
				const float BW = L.bPhone ? 112.0f : 82.0f;
				const float BH = L.bPhone ? 44.0f : RowHeight - 22.0f;
				const float BX = BadgeRight - BW;
				RoundedBox(RowMid, BX, Y + RowHeight * 0.5f - BH * 0.5f, BW, BH, BH * 0.5f, Fire);
				RowText.Text(TEXT("使用中"), BX + BW * 0.5f, Y + RowHeight * 0.5f - L.Small * 0.72f, L.Small, Paper, ETextAlign::Center, TEXT("Black"));
			}
		}
	}

	if (bClipRows)
	{
		Elements.PopClip();
	}
	// Scroll bar, when the tab has more rows than fit.
	if (Rows.Num() > VisibleRows)
	{
		const float Height = VisibleRows * L.RowStep - (L.RowStep - RowHeight);
		const float Thumb = Height * VisibleRows / Rows.Num();
		const float ThumbY = L.RowsTop + (Height - Thumb) * GetScrollPixels() / FMath::Max(MaxScrollPixels(), 1.0f);
		const float BarW = L.bPhone ? 8.0f : 6.0f;
		RoundedBox(P, RowsLeft + RowsWidth + 10.0f, L.RowsTop, BarW, Height, BarW * 0.5f, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		RoundedBox(Top, RowsLeft + RowsWidth + 10.0f, ThumbY, BarW, Thumb, BarW * 0.5f, WithAlpha(Accent, 0.8f));
	}

	// ---- What the chosen row does, in a strip at the panel's foot (a phone's in up to two lines); then the keys (not by touch).
	const float HelpY = L.bPhone ? PanelTop + PanelHeight - L.HelpBottom - L.HelpHeight : PanelTop + PanelHeight - 104.0f;
	{
		const FString Help = Rows.IsValidIndex(SelectedRow) && !(L.bPhone && bSideFocus) ? GetRowHelp(Rows[SelectedRow].Id) : FString();
		const float HelpH = L.HelpHeight;
		RoundedBox(P, PanelLeft + 24.0f, HelpY, PanelWidth - 48.0f, HelpH, 12.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
		const float Dot = L.bPhone ? 16.0f : 12.0f;
		Top.Disc(FVector2D(PanelLeft + 52.0f, HelpY + HelpH * 0.5f), Dot, WithAlpha(Accent, 0.9f));
		Text.Text(TEXT("i"), PanelLeft + 52.0f, HelpY + HelpH * 0.5f - Dot * 1.0f, Dot * 1.4f, Ink, ETextAlign::Center, TEXT("Black"));
		const float HelpRoom = PanelWidth - 130.0f;
		if (L.bPhone && Help.IsEmpty())
		{
			const FString Tip = TEXT("項目をタップすると、ここに説明が出ます");
			const float Size = HudFit(Tip, L.Help, HelpRoom, TEXT("Bold"));
			Text.Text(Tip, PanelLeft + 82.0f, HelpY + HelpH * 0.5f - Size * 0.72f, Size, Muted, ETextAlign::Left, TEXT("Bold"));
		}
		else if (!Help.IsEmpty())
		{
			const TArray<FString> Lines = L.HelpLines > 1 ? WrapLines(Help, L.Help, HelpRoom, L.HelpLines) : TArray<FString>{Help};
			float Size = L.Help;
			for (const FString& Line : Lines)
			{
				Size = FMath::Min(Size, HudFit(Line, L.Help, HelpRoom, TEXT("Bold")));
			}
			const float LineH = Size * 1.45f;
			const float FirstY = HelpY + HelpH * 0.5f - LineH * Lines.Num() * 0.5f + (LineH - Size * 1.44f) * 0.5f;
			for (int32 Index = 0; Index < Lines.Num(); ++Index)
			{
				Text.Text(Lines[Index], PanelLeft + (L.bPhone ? 82.0f : 76.0f), Lines.Num() == 1 && !L.bPhone ? HelpY + 13.0f : FirstY + Index * LineH, Size,
					WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
			}
		}
		if (!L.bPhone)
		{
			const FString Hint = HintText();
			Text.Text(Hint, PanelLeft + PanelWidth * 0.5f, PanelTop + PanelHeight - 42.0f, HudFit(Hint, 16.0f, PanelWidth - 60.0f, TEXT("Bold")),
				WithAlpha(Paper, 0.55f), ETextAlign::Center, TEXT("Bold"));
		}
	}

	// A note about the last change (just above the strip; on a dark plate of its own).
	if (!Message.IsEmpty() && Now - MessageAt < 2.4)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.4 - (Now - MessageAt)) / 0.4f, 0.0f, 1.0f);
		const FPainter Note{Design, Elements, Layer + 3, In * Fade};
		const float NoteSize = L.bPhone ? 26.0f : 20.0f;
		const float NoteH = L.bPhone ? 56.0f : 42.0f;
		const float NoteW = FMath::Min(HudMeasure(Message, NoteSize, TEXT("Bold")) + 60.0f, PanelWidth - 80.0f);
		const float NoteY = L.bPhone ? HelpY - NoteH - 10.0f : PanelTop + PanelHeight - 160.0f;
		RoundedBox(Note, PanelLeft + PanelWidth * 0.5f - NoteW * 0.5f, NoteY, NoteW, NoteH, 14.0f, FLinearColor(0.0f, 0.0f, 0.0f, L.bPhone ? 0.96f : 0.85f));
		const FPainter NoteText{Design, Elements, Layer + 4, In * Fade};
		const float Size = HudFit(Message, NoteSize, NoteW - 40.0f, TEXT("Bold"));
		NoteText.Text(Message, PanelLeft + PanelWidth * 0.5f, NoteY + NoteH * 0.5f - Size * 0.72f, Size, Gold, ETextAlign::Center, TEXT("Bold"));
	}
	// The back button, at the foot of the tabs.
	{
		const float X = static_cast<float>(L.Back.Min.X);
		const float Y = static_cast<float>(L.Back.Min.Y);
		const float W = static_cast<float>(L.Back.GetSize().X);
		const float H = static_cast<float>(L.Back.GetSize().Y);
		RoundedBox(P, X, Y, W, H, 12.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.95f));
		Top.Box(X, Y + 12.0f, 4.0f, H - 24.0f, Muted);
		Top.Outline(X, Y, W, H, WithAlpha(Paper, L.bPhone ? 0.16f : 0.0f), 1.0f);
		Text.Text(TEXT("もどる"), X + W * 0.5f, Y + H * 0.5f - L.Button * 0.75f, L.Button, Paper, ETextAlign::Center, TEXT("Black"));
		if (bSideFocus && SideCursor == SideBack && !bTouch)
		{
			Top.Outline(X - 4.0f, Y - 4.0f, W + 8.0f, H + 8.0f, WithAlpha(Gold, Pulse5), 3.0f);
		}
	}
	// 保存, above it (upright: beside it): lit while there are changes not saved, quiet once they are.
	{
		const float X = static_cast<float>(L.Save.Min.X);
		const float Y = static_cast<float>(L.Save.Min.Y);
		const float W = static_cast<float>(L.Save.GetSize().X);
		const float H = static_cast<float>(L.Save.GetSize().Y);
		if (bUnsaved)
		{
			FLinearColor Deep = Gold * 0.3f;
			Deep.A = 0.95f;
			HudPoly(P, {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
				MakeFill(FMath::Lerp(Gold, Ink, 0.55f), Deep));
			Top.Box(X, Y + 12.0f, 4.0f, H - 24.0f, Gold);
			Top.Outline(X, Y, W, H, WithAlpha(Gold, 0.5f + 0.3f * FMath::Sin(static_cast<float>(Now) * 4.0f)), 1.5f);
			Text.Text(TEXT("保存する"), X + W * 0.5f, Y + H * 0.5f - L.Button * 0.75f, L.Button, Paper, ETextAlign::Center, TEXT("Black"));
		}
		else
		{
			const float Size = L.Button * 0.84f;
			RoundedBox(P, X, Y, W, H, 12.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.95f));
			Top.Box(X, Y + 12.0f, 4.0f, H - 24.0f, WithAlpha(Muted, 0.5f));
			Text.Text(TEXT("保存ずみ"), X + W * 0.5f, Y + H * 0.5f - Size * 0.75f, Size, Muted, ETextAlign::Center, TEXT("Bold"));
		}
		if (bSideFocus && SideCursor == SideSave && !bTouch)
		{
			Top.Outline(X - 4.0f, Y - 4.0f, W + 8.0f, H + 8.0f, WithAlpha(Gold, Pulse5), 3.0f);
		}
	}

	int32 TopLayer = Layer + 5;
	if (bAskSave)
	{
		return PaintAskSave(Design, Elements, TopLayer);
	}
	// Waiting for a key: the screen dims and says what to press.
	if (bWaitingForKey && IsSelectable(SelectedRow))
	{
		const FPainter Shade{Design, Elements, TopLayer, 1.0f};
		Shade.Box(-1400.0f, -200.0f, 4400.0f, 2200.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		const FPainter Box{Design, Elements, TopLayer + 1, 1.0f};
		const float BoxW = FMath::Min(1040.0f, static_cast<float>(L.Canvas.X) - 60.0f);
		const float BoxX = static_cast<float>(L.Canvas.X) * 0.5f - BoxW * 0.5f;
		const float BoxY = static_cast<float>(L.Canvas.Y) * 0.5f - 120.0f;
		const float MidX = BoxX + BoxW * 0.5f;
		HudPoly(Box, {FVector2D(BoxX, BoxY), FVector2D(BoxX + BoxW, BoxY), FVector2D(BoxX + BoxW, BoxY + 220.0f), FVector2D(BoxX, BoxY + 220.0f)},
			MakeFill(FLinearColor(0.05f, 0.07f, 0.13f, 1.0f), FLinearColor(0.01f, 0.012f, 0.03f, 1.0f)));
		Box.Box(BoxX, BoxY, BoxW, 6.0f, Gold);
		const FPainter Words{Design, Elements, TopLayer + 2, 1.0f};
		Words.Text(Rows[SelectedRow].Label, MidX, BoxY + 26.0f, HudFit(Rows[SelectedRow].Label, 30.0f, BoxW - 80.0f), Paper, ETextAlign::Center, TEXT("Black"));
		const FString Ask = FString::Printf(TEXT("%s の、使いたい%sをおしてください"), *ChaosImpactSettings::GetDeviceName(EditedDevice),
			EditedDevice == EChaosImpactDevice::KeyboardMouse ? TEXT("キー・マウスボタン") : TEXT("ボタン"));
		Words.Text(Ask, MidX, BoxY + 92.0f, HudFit(Ask, 20.0f, BoxW - 80.0f, TEXT("Bold")), WithAlpha(Paper, 0.6f + 0.4f * FMath::Sin(static_cast<float>(Now) * 5.0f)),
			ETextAlign::Center, TEXT("Bold"));
		// How to stop: Esc on the keyboard; a controller's button stops waiting for a key, but is taken when buttons are being set.
		const FString Stop = ChaosImpactHints::Text(HintDevice(), EditedDevice == EChaosImpactDevice::KeyboardMouse
			? TEXT("{kb:Esc }{pad:コントローラーのボタン}でやめる（何もしなければ 8 秒でもどります）")
			: TEXT("{kb:Esc でやめる（}何もしなければ 8 秒でもどります{kb:）}"));
		Words.Text(Stop, MidX, BoxY + 150.0f, HudFit(Stop, 18.0f, BoxW - 80.0f, TEXT("Bold")), Muted, ETextAlign::Center, TEXT("Bold"));
		TopLayer += 3;
	}
	return NameEntry.Paint(Design, Elements, TopLayer);
}

int32 UChaosImpactSettingsScreen::PaintAskSave(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const FSettingsLayout& L = Layout();
	const double Now = FPlatformTime::Seconds();
	const float In = EaseOut(static_cast<float>(Now - AskOpenedAt) / 0.18f);
	// The settings dim behind the question.
	const FPainter Shade{Design, Elements, Layer, In};
	Shade.Box(-1400.0f, -200.0f, 4400.0f, 2200.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.62f));
	const float X = static_cast<float>(L.Ask.Min.X);
	const float Y = static_cast<float>(L.Ask.Min.Y) + (1.0f - In) * 18.0f;
	const float W = static_cast<float>(L.Ask.GetSize().X);
	const float H = static_cast<float>(L.Ask.GetSize().Y);
	const FPainter Box{Design, Elements, Layer + 1, In};
	HudPoly(Box, {FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)},
		MakeFill(FLinearColor(0.05f, 0.07f, 0.13f, 1.0f), FLinearColor(0.01f, 0.012f, 0.03f, 1.0f)));
	Box.Box(X, Y, W, 6.0f, Gold);
	Box.Outline(X, Y, W, H, WithAlpha(Paper, 0.12f), 1.0f);
	const FPainter Words{Design, Elements, Layer + 2, In};
	const FString Title = TEXT("変更が保存されていません");
	Words.Text(Title, X + W * 0.5f, Y + 44.0f, HudFit(Title, L.bPhone ? 40.0f : 32.0f, W - 80.0f), Paper, ETextAlign::Center, TEXT("Black"));
	if (L.bPhone)
	{
		// (Two lines, big enough to read on a phone.)
		const FString Lines[] = {TEXT("変更した設定を保存しますか？"), TEXT("（保存しないと、変更する前にもどります）")};
		float Size = 26.0f;
		for (const FString& Line : Lines) { Size = FMath::Min(Size, HudFit(Line, 26.0f, W - 80.0f, TEXT("Bold"))); }
		Words.Text(Lines[0], X + W * 0.5f, Y + 118.0f, Size, WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Bold"));
		Words.Text(Lines[1], X + W * 0.5f, Y + 118.0f + Size * 1.5f, Size, WithAlpha(Paper, 0.65f), ETextAlign::Center, TEXT("Bold"));
	}
	else
	{
		const FString Ask = TEXT("変更した設定を保存しますか？（保存しないと、変更する前にもどります）");
		Words.Text(Ask, X + W * 0.5f, Y + 110.0f, HudFit(Ask, 19.0f, W - 80.0f, TEXT("Bold")), WithAlpha(Paper, 0.75f), ETextAlign::Center, TEXT("Bold"));
	}
	const bool bTouch = L.bPhone && IsTouchScreenOnly();
	for (int32 Index = 0; Index < static_cast<int32>(ESaveAnswer::Count); ++Index)
	{
		const FBox2D Rect = AskButtonRect(Index);
		const float BX = static_cast<float>(Rect.Min.X);
		const float BY = static_cast<float>(Rect.Min.Y) + (1.0f - In) * 18.0f;
		const float BW = static_cast<float>(Rect.GetSize().X);
		const float BH = static_cast<float>(Rect.GetSize().Y);
		// (By touch: no cursor; every answer looks pressable, each in its colour.)
		const bool bChosen = Index == static_cast<int32>(AskChoice) && !bTouch;
		// 保存 in gold, 保存しない in red, キャンセル plain.
		const FLinearColor Tint = Index == 0 ? Gold : Index == 1 ? Fire : Ice;
		if (bChosen)
		{
			FLinearColor Deep = Tint * 0.3f;
			Deep.A = 1.0f;
			HudPoly(Box, {FVector2D(BX, BY), FVector2D(BX + BW, BY), FVector2D(BX + BW, BY + BH), FVector2D(BX, BY + BH)},
				MakeFill(FMath::Lerp(Tint, Ink, 0.45f), Deep));
			Words.Outline(BX, BY, BW, BH, WithAlpha(Paper, 0.7f), 2.0f);
		}
		else
		{
			RoundedBox(Box, BX, BY, BW, BH, 10.0f, bTouch ? WithAlpha(Tint * 0.25f, 0.9f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			Words.Box(BX, BY + 12.0f, 4.0f, BH - 24.0f, WithAlpha(Tint, 0.7f));
			if (bTouch)
			{
				Words.Outline(BX, BY, BW, BH, WithAlpha(Tint, 0.55f), 1.5f);
			}
		}
		const FString Label = AskAnswers[Index];
		const float Size = HudFit(Label, L.bPhone ? 30.0f : 21.0f, BW - 24.0f, TEXT("Black"));
		Words.Text(Label, BX + BW * 0.5f, BY + BH * 0.5f - Size * 0.72f, Size, bChosen || bTouch ? Paper : WithAlpha(Paper, 0.7f), ETextAlign::Center, TEXT("Black"));
	}
	if (!L.bPhone)
	{
		const FString Keys = ChaosImpactHints::Text(HintDevice(), TEXT("{pad:{DPad}・スティック：えらぶ　　{Confirm}：決定　　{Back}：キャンセル}")
			TEXT("{kb:←→：えらぶ　　Enter：決定　　Esc：キャンセル}"));
		Words.Text(Keys, X + W * 0.5f, Y + H - 50.0f, HudFit(Keys, 16.0f, W - 60.0f, TEXT("Bold")), Muted, ETextAlign::Center, TEXT("Bold"));
	}
	return Layer + 3;
}

void UChaosImpactSettingsScreen::PaintDeviceCard(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer, const int32 Row,
	const float Alpha, const double Now) const
{
	using namespace ChaosImpactPaint;
	const FRow& Item = Rows[Row];
	const EChaosImpactDevice Device = static_cast<EChaosImpactDevice>(Item.Id - RowDeviceItem);
	const FBox2D Rect = RowRect(Row);
	const bool bSelected = Row == SelectedRow && !bSideFocus;
	const float Lift = bSelected ? 8.0f : 0.0f;
	const float X = static_cast<float>(Rect.Min.X);
	const float Y = static_cast<float>(Rect.Min.Y) - Lift;
	const float W = static_cast<float>(Rect.GetSize().X);
	const float H = static_cast<float>(Rect.GetSize().Y);
	// Each device in a colour of its own (no makers' marks: a plain keyboard, a plain controller).
	const FLinearColor Marks[] = {Ice, FLinearColor(0.32f, 0.8f, 0.36f, 1.0f), FLinearColor(0.3f, 0.5f, 1.0f, 1.0f), FLinearColor(1.0f, 0.3f, 0.32f, 1.0f)};
	const FLinearColor Mark = Marks[FMath::Clamp(static_cast<int32>(Device), 0, 3)];
	const FPainter Back{Design, Elements, Layer, Alpha};
	const FPainter Mid{Design, Elements, Layer + 1, Alpha};
	const FPainter Front{Design, Elements, Layer + 2, Alpha};
	Back.Box(X + 8.0f, Y + 10.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
	RoundedBox(Back, X, Y, W, H, 14.0f, FLinearColor(0.035f, 0.05f, 0.09f, 0.97f));
	// Its name on a band across the top.
	FLinearColor Band = Mark * (bSelected ? 0.6f : 0.34f);
	Band.A = 1.0f;
	RoundedBox(Mid, X, Y, W, 92.0f, 14.0f, Band);
	Mid.Box(X, Y + 60.0f, W, 32.0f, Band);
	const FString Name = ChaosImpactSettings::GetDeviceName(Device);
	// (Centred in the band whatever size it fits at.)
	const float NameSize = HudFit(Name, 24.0f, W - 28.0f, TEXT("Black"));
	Front.Text(Name, X + W * 0.5f, Y + 46.0f - NameSize * 0.72f, NameSize, Paper, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	// Its picture: a white silhouette in the middle.
	const FLinearColor Shape(0.93f, 0.95f, 1.0f, 1.0f);
	const FLinearColor Hole(0.035f, 0.05f, 0.09f, 1.0f);
	const float CX = X + W * 0.5f;
	// (Between the band and the state under it, whatever the card's height.)
	const float CY = Y + 92.0f + (H - 150.0f) * 0.65f;
	if (Device == EChaosImpactDevice::KeyboardMouse)
	{
		RoundedBox(Mid, CX - 92.0f, CY - 34.0f, 132.0f, 68.0f, 10.0f, Shape);
		for (int32 KeyRow = 0; KeyRow < 3; ++KeyRow)
		{
			for (int32 Key = 0; Key < 7; ++Key)
			{
				RoundedBox(Front, CX - 84.0f + Key * 17.0f + (KeyRow == 1 ? 5.0f : 0.0f), CY - 26.0f + KeyRow * 16.0f, 13.0f, 11.0f, 2.0f, Hole);
			}
		}
		RoundedBox(Front, CX - 60.0f, CY + 21.0f, 66.0f, 8.0f, 2.0f, Hole);
		RoundedBox(Mid, CX + 52.0f, CY - 30.0f, 40.0f, 60.0f, 19.0f, Shape);
		Front.Box(CX + 71.0f, CY - 30.0f, 2.0f, 22.0f, Hole);
	}
	else
	{
		Mid.Disc(FVector2D(CX - 54.0f, CY + 20.0f), 27.0f, Shape);
		Mid.Disc(FVector2D(CX + 54.0f, CY + 20.0f), 27.0f, Shape);
		RoundedBox(Mid, CX - 80.0f, CY - 32.0f, 160.0f, 64.0f, 30.0f, Shape);
		Front.Disc(FVector2D(CX - 46.0f, CY - 8.0f), 13.0f, Hole);
		Front.Disc(FVector2D(CX + 22.0f, CY + 14.0f), 11.0f, Hole);
		for (const FVector2D& Offset : {FVector2D(0.0, -12.0), FVector2D(0.0, 12.0), FVector2D(-12.0, 0.0), FVector2D(12.0, 0.0)})
		{
			Front.Disc(FVector2D(CX + 48.0f, CY - 8.0f) + Offset, 5.5f, Hole);
		}
		Front.Box(CX - 30.0f, CY + 8.0f, 8.0f, 24.0f, Hole);
		Front.Box(CX - 38.0f, CY + 16.0f, 24.0f, 8.0f, Hole);
	}
	// Under it: whether this nickname changed its buttons here.
	const bool bChanged = IsDeviceChanged(EditedProfile, Device);
	const FString State = bChanged ? TEXT("ボタン変更あり") : TEXT("はじめの設定");
	RoundedBox(Mid, X + 24.0f, Y + H - 58.0f, W - 48.0f, 38.0f, 19.0f, bChanged ? WithAlpha(Gold, 0.9f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
	Front.Text(State, X + W * 0.5f, Y + H - 51.0f, HudFit(State, 17.0f, W - 70.0f, TEXT("Black")), bChanged ? Ink : Muted, ETextAlign::Center, TEXT("Black"));
	// 使用中: the device this player is playing with.
	if (Device == ChaosImpactSettings::GetPlayerDevice(Controller.Get()))
	{
		RoundedBox(Front, X + W - 104.0f, Y + 76.0f, 92.0f, 34.0f, 17.0f, Fire);
		Front.Text(TEXT("使用中"), X + W - 58.0f, Y + 81.0f, 17.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}
	if (bSelected)
	{
		Front.Outline(X - 4.0f, Y - 4.0f, W + 8.0f, H + 8.0f, WithAlpha(Gold, 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 5.0f)), 4.0f);
	}
}
