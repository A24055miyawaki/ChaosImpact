#include "ChaosImpactSettingsScreen.h"
#include "ChaosImpactControlPreview.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "ChaosImpactLoadingScreen.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
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

	// Layout in the 1600 x 900 design space.
	// The tabs down the left, the tab's panel on the right (its rows, then what the chosen row does).
	constexpr float RowsLeft = 440.0f;
	constexpr float RowsWidth = 1060.0f;
	constexpr float RowsTop = 198.0f;
	constexpr float RowHeight = 52.0f;
	constexpr float RowStep = 56.0f;
	constexpr int32 VisibleRows = 9;
	constexpr float CellLeft = 930.0f;
	constexpr float CellWidth = 262.0f;
	constexpr float CellGap = 16.0f;
	const FBox2D BackButton(FVector2D(60.0f, 796.0f), FVector2D(380.0f, 856.0f));
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
	NameEntry.Close();
	Message.Reset();
	OpenedAt = FPlatformTime::Seconds();
	TabChangedAt = OpenedAt;
	// The controls tab starts on what P1 plays with: their nickname, and their device.
	EditedProfile = ChaosImpactSettings::GetPlayerProfile(InController);
	EditedDevice = ChaosImpactSettings::GetPlayerDevice(InController);
	bLastInputPad = EditedDevice != EChaosImpactDevice::KeyboardMouse;
	LastPadDevice = bLastInputPad ? EditedDevice : EChaosImpactDevice::Xbox;
	SetTab(ETab::Display);
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
			bLastInputPad = true;
			BuildRows();
		}
		OpenControlStylePicker();
	}
#endif
	UE_LOG(LogChaosImpact, Log, TEXT("Settings opened"));
}

void UChaosImpactSettingsScreen::Close()
{
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
	bControlStylePickerOpen = false;
	TabChangedAt = FPlatformTime::Seconds();
	bDeleteArmed = false;
	BuildRows();
	SelectedRow = 0;
	SelectedCell = 0;
	FirstVisibleRow = 0;
	if (!IsSelectable(SelectedRow))
	{
		MoveSelection(1);
	}
}

void UChaosImpactSettingsScreen::EditControls(const int32 Profile, const EChaosImpactDevice Device)
{
	bControlStylePickerOpen = false;
	EditedProfile = FMath::Clamp(Profile, 0, ChaosImpactSettings::GetProfileCount() - 1);
	EditedDevice = Device;
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
		const FString Hint = bLastInputPad ? TEXT("十字キー：比べる　　決定：この操作にする　　戻る：もどる")
			: TEXT("カーソルを重ねて映像を見る　　クリック：この操作にする　　右クリック：もどる");
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

void UChaosImpactSettingsScreen::BuildControlRows()
{
	using namespace ChaosImpactSettings;
	EditedProfile = FMath::Clamp(EditedProfile, 0, GetProfileCount() - 1);
	const FChaosImpactProfile& Profile = GetProfile(EditedProfile);
	const bool bGuest = EditedProfile == 0;
	const bool bPad = EditedDevice != EChaosImpactDevice::KeyboardMouse;
	const EChaosImpactControlScheme Scheme = GetControlScheme(EditedProfile, EditedDevice);
	// Whose controls (a nickname keeps one person's), and with what.
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("だれの・どのデバイスの設定か")));
	Rows.Add(MakeRow(ERowKind::Value, RowProfile, TEXT("ニックネーム"), Profile.Name));
	Rows.Add(MakeRow(ERowKind::Value, RowDevice, TEXT("デバイス"), GetDeviceName(EditedDevice)));
	// How it is played.
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("操作のしかた")));
	Rows.Add(MakeRow(ERowKind::Value, RowControlScheme, TEXT("操作スタイル"), GetControlSchemeName(Scheme)));
	Rows.Add(MakeRow(ERowKind::Button, RowControlPreview, TEXT("操作スタイルを図と動画で見る")));
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
	// The nicknames themselves, last (seldom needed).
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("ニックネームの管理")));
	Rows.Add(MakeRow(ERowKind::Button, RowNewProfile, TEXT("＋ 新しいニックネームを作る"),
		FString::Printf(TEXT("%d / %d"), GetProfileCount() - 1, MaxProfiles - 1), GetProfileCount() >= MaxProfiles));
	Rows.Add(MakeRow(ERowKind::Button, RowRenameProfile, TEXT("このニックネームの名前を変える"), FString(), bGuest));
	Rows.Add(MakeRow(ERowKind::Button, RowDeleteProfile,
		bDeleteArmed ? TEXT("もう一度おすと消します") : TEXT("このニックネームを消す"), FString(), bGuest));
}

void UChaosImpactSettingsScreen::BuildOtherRows()
{
	using namespace ChaosImpactSettings;
	static const TCHAR* const Shakes[] = {TEXT("なし"), TEXT("弱い"), TEXT("ふつう"), TEXT("強い")};
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("サウンド")));
	Rows.Add(MakeRow(ERowKind::Value, RowVolume, TEXT("音量"), FString::Printf(TEXT("%d%%"), GetMasterVolume())));
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("プレイ中")));
	Rows.Add(MakeRow(ERowKind::Value, RowShake, TEXT("画面の揺れ"), Shakes[GetCameraShakeLevel()]));
	Rows.Add(MakeRow(ERowKind::Value, RowRumble, TEXT("コントローラーの振動"), OnOff(AChaosImpactPlayerController::IsRumbleEnabled())));
	Rows.Add(MakeRow(ERowKind::Value, RowSwitchConfirm, TEXT("Switchの決定ボタン"),
		IsSwitchConfirmRight() ? TEXT("A（右のボタン）") : TEXT("B（下のボタン）")));
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

void UChaosImpactSettingsScreen::SelectRow(const int32 Row, const int32 Cell)
{
	if (!IsSelectable(Row))
	{
		return;
	}
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

void UChaosImpactSettingsScreen::KeepSelectionVisible()
{
	// A heading just above the selection stays in view with it.
	const int32 Top = SelectedRow > 0 && Rows.IsValidIndex(SelectedRow - 1) && Rows[SelectedRow - 1].Kind == ERowKind::Heading
		? SelectedRow - 1 : SelectedRow;
	if (Top < FirstVisibleRow)
	{
		FirstVisibleRow = Top;
	}
	else if (SelectedRow >= FirstVisibleRow + VisibleRows)
	{
		FirstVisibleRow = SelectedRow - VisibleRows + 1;
	}
	FirstVisibleRow = FMath::Clamp(FirstVisibleRow, 0, FMath::Max(Rows.Num() - VisibleRows, 0));
}

void UChaosImpactSettingsScreen::HandleWheel(const float Delta)
{
	if (bControlStylePickerOpen) { MoveControlPreview(Delta > 0 ? -1 : 1); return; }
	if (NameEntry.IsOpen() || bWaitingForKey)
	{
		return;
	}
	FirstVisibleRow = FMath::Clamp(FirstVisibleRow + (Delta > 0.0f ? -1 : 1), 0, FMath::Max(Rows.Num() - VisibleRows, 0));
}

void UChaosImpactSettingsScreen::Adjust(const int32 Direction)
{
	if (bControlStylePickerOpen) { MoveControlPreview(Direction); return; }
	if (!IsSelectable(SelectedRow) || Direction == 0)
	{
		return;
	}
	const FRow& Row = Rows[SelectedRow];
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
	else if (Id == RowVolume)
	{
		SetMasterVolume(FMath::Clamp(GetMasterVolume() / 10 * 10 + Step * 10, 0, 100));
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
	switch (Row.Id)
	{
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
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	if (bControlStylePickerOpen) { bControlStylePickerOpen = false; return; }
	bWaitingForKey = false;
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
	if (RawKey.IsGamepadKey())
	{
		bLastInputPad = true;
		LastPadDevice = ChaosImpactSettings::DetectPadDeviceForInput(DeviceId);
	}
	else if (!RawKey.IsMouseButton())
	{
		bLastInputPad = false;
	}
	const FKey Key = ChaosImpactSettings::ToMenuKey(RawKey, DeviceId);
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
	else if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_LeftTrigger)
	{
		SetTab(static_cast<ETab>(static_cast<int32>(Tab) - 1));
	}
	else if (Key == EKeys::E || Key == EKeys::Tab || Key == EKeys::Gamepad_RightShoulder || Key == EKeys::Gamepad_RightTrigger)
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
	else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right
		|| Key == EKeys::Gamepad_Special_Right)
	{
		Leave();
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
		if (Axis == 0)
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
	const float Y = 168.0f + Index * 86.0f;
	return FBox2D(FVector2D(60.0f, Y), FVector2D(380.0f, Y + 74.0f));
}

FBox2D UChaosImpactSettingsScreen::RowRect(const int32 Row) const
{
	const float Y = RowsTop + (Row - FirstVisibleRow) * RowStep;
	return FBox2D(FVector2D(RowsLeft, Y), FVector2D(RowsLeft + RowsWidth, Y + RowHeight));
}

FBox2D UChaosImpactSettingsScreen::CellRect(const int32 Row, const int32 Cell) const
{
	const FBox2D Rect = RowRect(Row);
	const float X = CellLeft + Cell * (CellWidth + CellGap);
	return FBox2D(FVector2D(X, Rect.Min.Y + 6.0f), FVector2D(X + CellWidth, Rect.Max.Y - 6.0f));
}

bool UChaosImpactSettingsScreen::HandleMouseDown(const FVector2D& DesignPoint, const FKey& Button)
{
	if (!bOpen)
	{
		return false;
	}
	bLastInputPad = false;
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
		Leave();
		return true;
	}
	if (Button != EKeys::LeftMouseButton)
	{
		return true;
	}
	if (BackButton.IsInside(DesignPoint))
	{
		Leave();
		return true;
	}
	for (int32 Index = 0; Index < static_cast<int32>(ETab::Count); ++Index)
	{
		if (TabRect(Index).IsInside(DesignPoint))
		{
			SetTab(static_cast<ETab>(Index));
			return true;
		}
	}
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + VisibleRows); ++Row)
	{
		if (!RowRect(Row).IsInside(DesignPoint) || !IsSelectable(Row))
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
			if (DesignPoint.X >= CellLeft)
			{
				Adjust(DesignPoint.X < CellLeft + CellWidth + CellGap * 0.5f ? -1 : 1);
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
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + VisibleRows); ++Row)
	{
		if (RowRect(Row).IsInside(DesignPoint) && IsSelectable(Row))
		{
			const int32 Cell = Rows[Row].Kind == ERowKind::Binding && CellRect(Row, 1).IsInside(DesignPoint) ? 1 : 0;
			if (Row != SelectedRow || Cell != SelectedCell)
			{
				SelectedRow = Row;
				SelectedCell = Cell;
				bDeleteArmed = false;
				BuildRows();
			}
			return;
		}
	}
}

FString UChaosImpactSettingsScreen::HintText() const
{
	if (!bLastInputPad)
	{
		// (はずす only where there are keys to take off.)
		return Tab == ETab::Controls ? TEXT("Q / E：タブ　　↑↓：えらぶ　　←→：変える　　Enter：決定　　Delete：キーをはずす　　Esc：もどる")
			: TEXT("Q / E：タブ　　↑↓：えらぶ　　←→：変える　　Enter：決定　　Esc：もどる");
	}
	using namespace ChaosImpactSettings;
	const EChaosImpactDevice Pad = LastPadDevice;
	const FKey Confirm = Pad == EChaosImpactDevice::Switch && IsSwitchConfirmRight()
		? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom;
	const FKey Back = Confirm == EKeys::Gamepad_FaceButton_Right ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right;
	if (Tab != ETab::Controls)
	{
		return FString::Printf(TEXT("%s / %s：タブ　　十字キー・スティック：えらぶ・変える　　%s：決定　　%s：もどる"),
			*GetKeyName(EKeys::Gamepad_LeftShoulder, Pad), *GetKeyName(EKeys::Gamepad_RightShoulder, Pad), *GetKeyName(Confirm, Pad), *GetKeyName(Back, Pad));
	}
	return FString::Printf(TEXT("%s / %s：タブ　　十字キー・スティック：えらぶ・変える　　%s：決定　　%s：ボタンをはずす　　%s：もどる"),
		*GetKeyName(EKeys::Gamepad_LeftShoulder, Pad), *GetKeyName(EKeys::Gamepad_RightShoulder, Pad),
		*GetKeyName(Confirm, Pad), *GetKeyName(EKeys::Gamepad_FaceButton_Top, Pad), *GetKeyName(Back, Pad));
}

FString UChaosImpactSettingsScreen::GetRowHelp(const int32 Id) const
{
	if (Id >= RowBinding)
	{
		return bLastInputPad ? TEXT("決定でボタンを登録（ひとつの操作に2つまで）・Yボタンではずします")
			: TEXT("Enterでキーを登録（ひとつの操作に2つまで）・Deleteではずします");
	}
	if (Id >= RowQualityGroup && Id < RowProfile)
	{
		return TEXT("この項目だけの画質です。重いときはここを下げても軽くなります");
	}
	switch (Id)
	{
	case RowWindowMode: return TEXT("フルスクリーン・ボーダーレス・ウィンドウを切り替えます（F11でも切り替え）");
	case RowResolution: return TEXT("画面（ウィンドウ）の大きさです。21:9・32:9の横長は左右が広く見えます（ウィンドウは端のドラッグでも変えられます）");
	case RowVSync: return TEXT("ONにすると画面のちらつきを防ぎます。入力が少し遅れることがあります");
	case RowFrameRate: return TEXT("1秒あたりのコマ数の上限です");
	case RowBrightness: return TEXT("画面全体の明るさです");
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
	case RowCameraDistance: return TEXT("見下ろしカメラの距離です。近いと迫力が、遠いと見える範囲が増えます（人ごと・分割画面でも別々）");
	case RowResetDevice: return TEXT("このデバイスのボタン設定を、はじめの状態にもどします");
	case RowControlScheme: return TEXT("操作のしかたです。下の「図と動画で見る」で見比べられます");
	case RowMouseDash: return TEXT("ダッシュをどの操作で出すかです");
	case RowControlPreview: return TEXT("それぞれの操作スタイルを図と動画で見比べて、そのまま選べます");
	case RowShoulderSensitivityX:
	case RowShoulderSensitivityY: return TEXT("肩越し視点で、カメラが動く速さです");
	case RowResetShoulderSensitivity: return TEXT("肩越しの感度を100%にもどします");
	case RowVolume: return TEXT("ゲーム全体の音の大きさです");
	case RowRumble: return TEXT("コントローラーを振動させるかどうかです");
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
	const double Now = FPlatformTime::Seconds();
	const float In = EaseOut(static_cast<float>(Now - OpenedAt) / 0.3f);
	const FPainter P{Design, Elements, Layer, In};
	const FPainter Top{Design, Elements, Layer + 1, In};
	const FPainter Text{Design, Elements, Layer + 2, In};
	static const TCHAR* const TabNotes[] = {TEXT("画面モード・大きさ・なめらかさ"), TEXT("見た目の細かさ・動きの軽さ"), TEXT("操作スタイル・感度・ボタン"),
		TEXT("音量・振動・画面の揺れ")};
	const FLinearColor TabColors[] = {Fire, Gold, Ice, Violet};
	const FLinearColor Accent = TabColors[FMath::Clamp(static_cast<int32>(Tab), 0, 3)];

	// ---- Left: the title, and the tabs down the side.
	HudText3D(Text, TEXT("SETTINGS"), 60.0f, 54.0f, 54.0f, Paper, ETextAlign::Left, TEXT("BlackItalic"), 3.0f);
	P.Box(64.0f, 128.0f, 120.0f, 6.0f, Ice);
	P.Box(188.0f, 128.0f, 60.0f, 6.0f, Fire);
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
			Top.Box(X, Y, 6.0f, H, TabColors[Index]);
			Top.Outline(X, Y, W, H, WithAlpha(Paper, 0.25f), 1.0f);
		}
		else
		{
			RoundedBox(P, X, Y, W, H, 10.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.85f));
			Top.Box(X, Y + 14.0f, 4.0f, H - 28.0f, WithAlpha(TabColors[Index], 0.6f));
		}
		Text.Text(TabNames[Index], X + 26.0f, Y + 6.0f, 24.0f, bCurrent ? Paper : WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Black"));
		Text.Text(TabNotes[Index], X + 27.0f, Y + 46.0f, HudFit(TabNotes[Index], 13.0f, W - 40.0f, TEXT("Bold")), bCurrent ? WithAlpha(Paper, 0.85f) : Muted,
			ETextAlign::Left, TEXT("Bold"));
	}
	{
		// How to change tabs, under them.
		const FString TabKeys = bLastInputPad
			? FString::Printf(TEXT("%s / %s：タブ"), *ChaosImpactSettings::GetKeyName(EKeys::Gamepad_LeftShoulder, LastPadDevice),
				*ChaosImpactSettings::GetKeyName(EKeys::Gamepad_RightShoulder, LastPadDevice))
			: FString(TEXT("Q / E：タブ"));
		Text.Text(TabKeys, 64.0f, TabRect(static_cast<int32>(ETab::Count) - 1).Max.Y + 16.0f, 16.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	}

	// ---- Right: the tab's panel.
	constexpr float PanelLeft = 410.0f;
	constexpr float PanelTop = 120.0f;
	constexpr float PanelWidth = 1130.0f;
	constexpr float PanelHeight = 740.0f;
	P.Box(PanelLeft + 12.0f, PanelTop + 14.0f, PanelWidth, PanelHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	HudPoly(P, {FVector2D(PanelLeft, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop + PanelHeight),
		FVector2D(PanelLeft, PanelTop + PanelHeight)}, MakeFill(FLinearColor(0.035f, 0.05f, 0.1f, 0.96f), FLinearColor(0.006f, 0.008f, 0.02f, 0.97f)));
	HudPoly(Top, {FVector2D(PanelLeft, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop), FVector2D(PanelLeft + PanelWidth, PanelTop + 6.0f),
		FVector2D(PanelLeft, PanelTop + 6.0f)}, MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
	Top.Outline(PanelLeft, PanelTop, PanelWidth, PanelHeight, WithAlpha(Paper, 0.1f), 1.0f);
	HudText3D(Text, TabNames[static_cast<int32>(Tab)], PanelLeft + 30.0f, PanelTop + 22.0f, 32.0f, Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
	Text.Text(TabNotes[static_cast<int32>(Tab)], PanelLeft + 32.0f + HudMeasure(TabNames[static_cast<int32>(Tab)], 32.0f, TEXT("Black")) + 24.0f,
		PanelTop + 36.0f, 17.0f, Muted, ETextAlign::Left, TEXT("Bold"));

	// ---- The rows: a clean list; the chosen one lit in the tab's colour.
	const float RowsIn = EaseOut(static_cast<float>(Now - TabChangedAt) / 0.22f);
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + VisibleRows); ++Row)
	{
		const FRow& Item = Rows[Row];
		const FBox2D Rect = RowRect(Row);
		const float Slide = (1.0f - RowsIn) * (30.0f + 8.0f * (Row - FirstVisibleRow));
		const float X = static_cast<float>(Rect.Min.X) + Slide;
		const float Y = static_cast<float>(Rect.Min.Y);
		const FPainter RowBack{Design, Elements, Layer, In * RowsIn};
		const FPainter RowMid{Design, Elements, Layer + 1, In * RowsIn};
		const FPainter RowText{Design, Elements, Layer + 2, In * RowsIn};
		if (Item.Kind == ERowKind::Heading)
		{
			RowText.Text(Item.Label, X + 8.0f, Y + 20.0f, HudFit(Item.Label, 18.0f, CellLeft - RowsLeft - 20.0f), Accent, ETextAlign::Left, TEXT("Black"));
			RowBack.Box(X + 8.0f, Y + RowHeight - 6.0f, RowsWidth - 16.0f, 1.0f, WithAlpha(Accent, 0.4f));
			if (Item.Id == 1)
			{
				RowText.Text(TEXT("1つめ"), CellLeft + Slide + CellWidth * 0.5f, Y + 22.0f, 15.0f, Muted, ETextAlign::Center, TEXT("Bold"));
				RowText.Text(TEXT("2つめ"), CellLeft + Slide + CellWidth * 1.5f + CellGap, Y + 22.0f, 15.0f, Muted, ETextAlign::Center, TEXT("Bold"));
			}
			continue;
		}
		const bool bSelected = Row == SelectedRow;
		if (bSelected)
		{
			FLinearColor Lit = Accent * 0.28f;
			Lit.A = 0.95f;
			HudPoly(RowBack, {FVector2D(X, Y), FVector2D(X + RowsWidth, Y), FVector2D(X + RowsWidth, Y + RowHeight), FVector2D(X, Y + RowHeight)},
				MakeFill(Lit, FLinearColor(0.02f, 0.03f, 0.07f, 0.95f)));
			RoundedBox(RowMid, X, Y + 6.0f, 6.0f, RowHeight - 12.0f, 3.0f, Accent);
			RowMid.Outline(X, Y, RowsWidth, RowHeight, WithAlpha(Paper, 0.18f), 1.0f);
		}
		else
		{
			RowBack.Box(X + 8.0f, Y + RowHeight - 1.0f, RowsWidth - 16.0f, 1.0f, WithAlpha(Paper, 0.06f));
		}
		const bool bInfo = Item.Kind == ERowKind::Info;
		const FLinearColor LabelColor = Item.bDisabled ? Muted : bInfo ? WithAlpha(Paper, 0.7f) : Paper;
		const float LabelRoom = (Item.Kind == ERowKind::Value || Item.Kind == ERowKind::Binding ? CellLeft : RowsLeft + RowsWidth - 160.0f) - RowsLeft - 50.0f;
		const float LabelSize = HudFit(Item.Label, bInfo ? 17.0f : 21.0f, LabelRoom, TEXT("Bold"));
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
				const float ShownSize = HudFit(Shown, 19.0f, CellWidth - 20.0f, TEXT("Bold"));
				const FPainter CellText{Design, Elements, Layer + 3, In * RowsIn};
				CellText.Text(Shown, CellX + CellWidth * 0.5f, static_cast<float>(CellBox.Min.Y) + CellH * 0.5f - ShownSize * 0.72f, ShownSize,
					Item.Cells[Cell] == TEXT("－") && !bWaiting ? Muted : Paper, ETextAlign::Center, TEXT("Bold"));
			}
		}
		else if (Item.Kind == ERowKind::Value)
		{
			// The value between ◀ ▶ (nudging while chosen).
			const float AreaWidth = CellWidth * 2.0f + CellGap;
			const float Centre = CellLeft + Slide + AreaWidth * 0.5f;
			const float ValueSize = HudFit(Item.Value, 21.0f, AreaWidth - 80.0f, TEXT("Black"));
			RowText.Text(Item.Value, Centre, Y + RowHeight * 0.5f - ValueSize * 0.72f, ValueSize, Item.bDisabled ? Muted : bSelected ? Paper : WithAlpha(Paper, 0.85f),
				ETextAlign::Center, TEXT("Black"));
			if (!Item.bDisabled)
			{
				const FLinearColor Arrow = bSelected ? FMath::Lerp(Accent, Paper, 0.25f) : WithAlpha(Muted, 0.6f);
				const float Nudge = bSelected ? 3.0f * FMath::Sin(static_cast<float>(Now) * 6.0f) : 0.0f;
				const float MidY = Y + RowHeight * 0.5f;
				const float LX = CellLeft + Slide + 16.0f - Nudge;
				const float RX = CellLeft + Slide + AreaWidth - 16.0f + Nudge;
				RowText.Line(FVector2D(LX + 7.0f, MidY - 9.0f), FVector2D(LX - 2.0f, MidY), Arrow, 3.5f);
				RowText.Line(FVector2D(LX - 2.0f, MidY), FVector2D(LX + 7.0f, MidY + 9.0f), Arrow, 3.5f);
				RowText.Line(FVector2D(RX - 7.0f, MidY - 9.0f), FVector2D(RX + 2.0f, MidY), Arrow, 3.5f);
				RowText.Line(FVector2D(RX + 2.0f, MidY), FVector2D(RX - 7.0f, MidY + 9.0f), Arrow, 3.5f);
			}
		}
		else if (Item.Kind == ERowKind::Button)
		{
			// A button: what it does now on the right, and a chevron.
			const float RightX = RowsLeft + Slide + RowsWidth - 26.0f;
			if (!Item.Value.IsEmpty())
			{
				RowText.Text(Item.Value, RightX - 26.0f, Y + RowHeight * 0.5f - 13.0f, HudFit(Item.Value, 18.0f, 300.0f, TEXT("Bold")), Muted,
					ETextAlign::Right, TEXT("Bold"));
			}
			const FLinearColor Chevron = Item.bDisabled ? WithAlpha(Muted, 0.4f) : bSelected ? Paper : WithAlpha(Paper, 0.45f);
			const float MidY = Y + RowHeight * 0.5f;
			RowText.Line(FVector2D(RightX - 6.0f, MidY - 8.0f), FVector2D(RightX + 2.0f, MidY), Chevron, 3.0f);
			RowText.Line(FVector2D(RightX + 2.0f, MidY), FVector2D(RightX - 6.0f, MidY + 8.0f), Chevron, 3.0f);
		}
		else if (!Item.Value.IsEmpty())
		{
			RowText.Text(Item.Value, RowsLeft + Slide + RowsWidth - 30.0f, Y + 14.0f, HudFit(Item.Value, 18.0f, 300.0f, TEXT("Bold")), Muted, ETextAlign::Right, TEXT("Bold"));
		}
		if (Item.Id == RowDeleteProfile && bDeleteArmed)
		{
			RoundedBox(RowText, X, Y + RowHeight - 4.0f, RowsWidth, 4.0f, 2.0f, Fire);
		}
	}

	// Scroll bar, when the tab has more rows than fit.
	if (Rows.Num() > VisibleRows)
	{
		const float Height = VisibleRows * RowStep - (RowStep - RowHeight);
		const float Thumb = Height * VisibleRows / Rows.Num();
		const float ThumbY = RowsTop + (Height - Thumb) * FirstVisibleRow / FMath::Max(Rows.Num() - VisibleRows, 1);
		RoundedBox(P, RowsLeft + RowsWidth + 12.0f, RowsTop, 6.0f, Height, 3.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		RoundedBox(Top, RowsLeft + RowsWidth + 12.0f, ThumbY, 6.0f, Thumb, 3.0f, WithAlpha(Accent, 0.8f));
	}

	// ---- What the chosen row does, in a strip at the panel's foot; then the keys.
	{
		const FString Help = Rows.IsValidIndex(SelectedRow) ? GetRowHelp(Rows[SelectedRow].Id) : FString();
		const float HelpY = PanelTop + PanelHeight - 104.0f;
		RoundedBox(P, PanelLeft + 24.0f, HelpY, PanelWidth - 48.0f, 50.0f, 12.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
		Top.Disc(FVector2D(PanelLeft + 52.0f, HelpY + 25.0), 12.0f, WithAlpha(Accent, 0.9f));
		Text.Text(TEXT("i"), PanelLeft + 52.0f, HelpY + 13.0f, 17.0f, Ink, ETextAlign::Center, TEXT("Black"));
		if (!Help.IsEmpty())
		{
			Text.Text(Help, PanelLeft + 76.0f, HelpY + 13.0f, HudFit(Help, 18.0f, PanelWidth - 130.0f, TEXT("Bold")), WithAlpha(Paper, 0.85f),
				ETextAlign::Left, TEXT("Bold"));
		}
		const FString Hint = HintText();
		Text.Text(Hint, PanelLeft + PanelWidth * 0.5f, PanelTop + PanelHeight - 42.0f, HudFit(Hint, 16.0f, PanelWidth - 60.0f, TEXT("Bold")),
			WithAlpha(Paper, 0.55f), ETextAlign::Center, TEXT("Bold"));
	}

	// A note about the last change.
	if (!Message.IsEmpty() && Now - MessageAt < 2.4)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.4 - (Now - MessageAt)) / 0.4f, 0.0f, 1.0f);
		const FPainter Note{Design, Elements, Layer + 3, In * Fade};
		const float NoteW = FMath::Min(HudMeasure(Message, 20.0f, TEXT("Bold")) + 60.0f, 900.0f);
		RoundedBox(Note, PanelLeft + PanelWidth * 0.5f - NoteW * 0.5f, PanelTop + PanelHeight - 160.0f, NoteW, 42.0f, 14.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		const FPainter NoteText{Design, Elements, Layer + 4, In * Fade};
		NoteText.Text(Message, PanelLeft + PanelWidth * 0.5f, PanelTop + PanelHeight - 152.0f, HudFit(Message, 20.0f, NoteW - 40.0f, TEXT("Bold")), Gold,
			ETextAlign::Center, TEXT("Bold"));
	}
	// The back button, at the foot of the tabs.
	{
		const float X = static_cast<float>(BackButton.Min.X);
		const float Y = static_cast<float>(BackButton.Min.Y);
		const float W = static_cast<float>(BackButton.GetSize().X);
		const float H = static_cast<float>(BackButton.GetSize().Y);
		RoundedBox(P, X, Y, W, H, 12.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.95f));
		Top.Box(X, Y + 12.0f, 4.0f, H - 24.0f, Muted);
		Text.Text(TEXT("もどる"), X + W * 0.5f, Y + H * 0.5f - 18.0f, 24.0f, Paper, ETextAlign::Center, TEXT("Black"));
	}

	int32 TopLayer = Layer + 5;
	// Waiting for a key: the screen dims and says what to press.
	if (bWaitingForKey && IsSelectable(SelectedRow))
	{
		const FPainter Shade{Design, Elements, TopLayer, 1.0f};
		Shade.Box(-1400.0f, -200.0f, 4400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		const FPainter Box{Design, Elements, TopLayer + 1, 1.0f};
		HudPoly(Box, {FVector2D(280.0, 330.0), FVector2D(1320.0, 330.0), FVector2D(1320.0, 550.0), FVector2D(280.0, 550.0)},
			MakeFill(FLinearColor(0.05f, 0.07f, 0.13f, 1.0f), FLinearColor(0.01f, 0.012f, 0.03f, 1.0f)));
		Box.Box(280.0f, 330.0f, 1040.0f, 6.0f, Gold);
		const FPainter Words{Design, Elements, TopLayer + 2, 1.0f};
		Words.Text(Rows[SelectedRow].Label, 800.0f, 356.0f, HudFit(Rows[SelectedRow].Label, 30.0f, 960.0f), Paper, ETextAlign::Center, TEXT("Black"));
		const FString Ask = FString::Printf(TEXT("%s の、使いたい%sをおしてください"), *ChaosImpactSettings::GetDeviceName(EditedDevice),
			EditedDevice == EChaosImpactDevice::KeyboardMouse ? TEXT("キー・マウスボタン") : TEXT("ボタン"));
		Words.Text(Ask, 800.0f, 422.0f, HudFit(Ask, 20.0f, 960.0f, TEXT("Bold")), WithAlpha(Paper, 0.6f + 0.4f * FMath::Sin(static_cast<float>(Now) * 5.0f)),
			ETextAlign::Center, TEXT("Bold"));
		Words.Text(TEXT("Esc でやめる（何もしなければ 8 秒でもどります）"), 800.0f, 480.0f, 18.0f, Muted, ETextAlign::Center, TEXT("Bold"));
		TopLayer += 3;
	}
	return NameEntry.Paint(Design, Elements, TopLayer);
}
