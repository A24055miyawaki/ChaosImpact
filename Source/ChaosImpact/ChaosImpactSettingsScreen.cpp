#include "ChaosImpactSettingsScreen.h"

#include "ChaosImpact.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "Input/Events.h"

namespace
{
	using ERowKind = UChaosImpactSettingsScreen::ERowKind;
	using FRow = UChaosImpactSettingsScreen::FRow;

	const TCHAR* const TabNames[] = {TEXT("画面"), TEXT("画質"), TEXT("操作"), TEXT("その他")};
	const TCHAR* const QualityNames[] = {TEXT("低"), TEXT("中"), TEXT("高"), TEXT("最高")};
	const int32 FrameRates[] = {30, 60, 120, 144, 165, 240, 0};

	// Layout in the 1600 x 900 design space.
	constexpr float RowsLeft = 200.0f;
	constexpr float RowsWidth = 1200.0f;
	constexpr float RowsTop = 198.0f;
	constexpr float RowHeight = 56.0f;
	constexpr float RowStep = 62.0f;
	constexpr int32 VisibleRows = 9;
	constexpr float CellLeft = 780.0f;
	constexpr float CellWidth = 290.0f;
	constexpr float CellGap = 20.0f;
	const FBox2D BackButton(FVector2D(120.0f, 806.0f), FVector2D(360.0f, 860.0f));


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
	UE_LOG(LogChaosImpact, Log, TEXT("Settings opened"));
}

void UChaosImpactSettingsScreen::Close()
{
	bOpen = false;
	bWaitingForKey = false;
	NameEntry.Close();
}

void UChaosImpactSettingsScreen::SetTab(const ETab NewTab)
{
	Tab = static_cast<ETab>(Wrap(static_cast<int32>(NewTab), static_cast<int32>(ETab::Count)));
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
	EditedProfile = FMath::Clamp(Profile, 0, ChaosImpactSettings::GetProfileCount() - 1);
	EditedDevice = Device;
	BuildRows();
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
	static const TCHAR* const Modes[] = {TEXT("フルスクリーン"), TEXT("ボーダーレス"), TEXT("ウィンドウ")};
	const int32 Mode = GetWindowMode();
	Rows.Add(MakeRow(ERowKind::Value, RowWindowMode, TEXT("画面モード（F11でも切り替え）"), Modes[Mode]));
	const FIntPoint Resolution = GetResolution();
	const FIntPoint Monitor = GetMonitorResolution();
	FString ResolutionText = FString::Printf(TEXT("%d × %d"), Resolution.X, Resolution.Y);
	if (Mode != 2 && Resolution == Monitor)
	{
		ResolutionText += TEXT("（画面と同じ）");
	}
	else if (Mode != 2 && Resolution.Y > Monitor.Y)
	{
		ResolutionText += TEXT("（高画質・重い）");
	}
	Rows.Add(MakeRow(ERowKind::Value, RowResolution, Mode == 2 ? TEXT("ウィンドウの大きさ") : TEXT("解像度"), ResolutionText));
	Rows.Add(MakeRow(ERowKind::Value, RowVSync, TEXT("垂直同期"), OnOff(IsVSyncOn())));
	const int32 Limit = GetFrameRateLimit();
	Rows.Add(MakeRow(ERowKind::Value, RowFrameRate, TEXT("フレームレート上限"),
		Limit <= 0 ? FString(TEXT("無制限")) : FString::Printf(TEXT("%d fps"), Limit)));
	Rows.Add(MakeRow(ERowKind::Value, RowBrightness, TEXT("明るさ"), FString::Printf(TEXT("%d"), GetBrightness())));
}

void UChaosImpactSettingsScreen::BuildPictureRows()
{
	using namespace ChaosImpactSettings;
	const int32 Overall = GetOverallQuality();
	Rows.Add(MakeRow(ERowKind::Value, RowQuality, TEXT("画質（まとめて）"),
		Overall < 0 ? FString(TEXT("カスタム")) : FString(QualityNames[Overall])));
	Rows.Add(MakeRow(ERowKind::Heading, 0, TEXT("ひとつずつ設定")));
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
	Rows.Add(MakeRow(ERowKind::Value, RowProfile, TEXT("ニックネーム"), Profile.Name));
	Rows.Add(MakeRow(ERowKind::Button, RowNewProfile, TEXT("＋ ニックネームを作る"),
		FString::Printf(TEXT("%d / %d"), GetProfileCount() - 1, MaxProfiles - 1), GetProfileCount() >= MaxProfiles));
	Rows.Add(MakeRow(ERowKind::Button, RowRenameProfile, TEXT("なまえを変える"), FString(), bGuest));
	Rows.Add(MakeRow(ERowKind::Button, RowDeleteProfile,
		bDeleteArmed ? TEXT("もう一度おすと消します") : TEXT("このニックネームを消す"), FString(), bGuest));
	Rows.Add(MakeRow(ERowKind::Heading, 0, FString::Printf(TEXT("%s の操作"), *Profile.Name)));
	Rows.Add(MakeRow(ERowKind::Value, RowDevice, TEXT("設定するデバイス"), GetDeviceName(EditedDevice)));
	Rows.Add(MakeRow(ERowKind::Value, RowPadChoice, TEXT("使うコントローラーの種類"), GetPadChoiceName(Profile.Pad)));
	if (bPad)
	{
		Rows.Add(MakeRow(ERowKind::Value, RowSwapSticks, TEXT("スティックの左右を入れ替え"), OnOff(Profile.bSwapSticks)));
	}
	Rows.Add(MakeRow(ERowKind::Value, RowAimAssist, TEXT("エイムアシスト"), OnOff(Profile.bAimAssist)));
	Rows.Add(MakeRow(ERowKind::Heading, 1, bPad ? TEXT("ボタン（ひとつの操作に2つまで）") : TEXT("キー（ひとつの操作に2つまで）")));
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
	Rows.Add(MakeRow(ERowKind::Button, RowResetDevice, FString::Printf(TEXT("%s を初期設定にもどす"), *GetDeviceName(EditedDevice))));
}

void UChaosImpactSettingsScreen::BuildOtherRows()
{
	using namespace ChaosImpactSettings;
	static const TCHAR* const Shakes[] = {TEXT("なし"), TEXT("弱い"), TEXT("ふつう")};
	Rows.Add(MakeRow(ERowKind::Value, RowVolume, TEXT("音量"), FString::Printf(TEXT("%d%%"), GetMasterVolume())));
	Rows.Add(MakeRow(ERowKind::Value, RowRumble, TEXT("コントローラーの振動"), OnOff(AChaosImpactPlayerController::IsRumbleEnabled())));
	Rows.Add(MakeRow(ERowKind::Value, RowShake, TEXT("画面の揺れ"), Shakes[GetCameraShakeLevel()]));
	Rows.Add(MakeRow(ERowKind::Value, RowSwitchConfirm, TEXT("Switchコントローラーの決定ボタン"),
		IsSwitchConfirmRight() ? TEXT("A（右のボタン）") : TEXT("B（下のボタン）")));
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
	}
	SelectedRow = Row;
	SelectedCell = Rows[Row].Kind == ERowKind::Binding ? FMath::Clamp(Cell, 0, 1) : 0;
	KeepSelectionVisible();
	BuildRows();
}

void UChaosImpactSettingsScreen::MoveSelection(const int32 Direction)
{
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
	if (NameEntry.IsOpen() || bWaitingForKey)
	{
		return;
	}
	FirstVisibleRow = FMath::Clamp(FirstVisibleRow + (Delta > 0.0f ? -1 : 1), 0, FMath::Max(Rows.Num() - VisibleRows, 0));
}

void UChaosImpactSettingsScreen::Adjust(const int32 Direction)
{
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
	if (Id == RowWindowMode)
	{
		SetWindowMode(Wrap(GetWindowMode() + Step, 3));
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
	else if (Id == RowShake)
	{
		SetCameraShakeLevel(FMath::Clamp(GetCameraShakeLevel() + Step, 0, 2));
	}
	else if (Id == RowSwitchConfirm)
	{
		SetSwitchConfirmRight(!IsSwitchConfirmRight());
	}
	BuildRows();
}

void UChaosImpactSettingsScreen::Activate()
{
	using namespace ChaosImpactSettings;
	if (!IsSelectable(SelectedRow) || Rows[SelectedRow].bDisabled)
	{
		return;
	}
	const FRow& Row = Rows[SelectedRow];
	if (Row.Kind == ERowKind::Value)
	{
		ChangeValue(Row, 1);
		return;
	}
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
		static const TCHAR* const Modes[] = {TEXT("フルスクリーン"), TEXT("ボーダーレス"), TEXT("ウィンドウ")};
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
	const float X = 310.0f + Index * 250.0f;
	return FBox2D(FVector2D(X, 116.0f), FVector2D(X + 230.0f, 170.0f));
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
		return TEXT("Q / E：タブ　　↑↓：えらぶ　　←→：変える　　Enter：決定　　Delete：キーをはずす　　Esc：もどる");
	}
	using namespace ChaosImpactSettings;
	const EChaosImpactDevice Pad = LastPadDevice;
	const FKey Confirm = Pad == EChaosImpactDevice::Switch && IsSwitchConfirmRight()
		? EKeys::Gamepad_FaceButton_Right : EKeys::Gamepad_FaceButton_Bottom;
	const FKey Back = Confirm == EKeys::Gamepad_FaceButton_Right ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_FaceButton_Right;
	return FString::Printf(TEXT("%s / %s：タブ　　十字キー・スティック：えらぶ・変える　　%s：決定　　%s：ボタンをはずす　　%s：もどる"),
		*GetKeyName(EKeys::Gamepad_LeftShoulder, Pad), *GetKeyName(EKeys::Gamepad_RightShoulder, Pad),
		*GetKeyName(Confirm, Pad), *GetKeyName(EKeys::Gamepad_FaceButton_Top, Pad), *GetKeyName(Back, Pad));
}

int32 UChaosImpactSettingsScreen::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float In = ChaosImpactPaint::EaseOut(static_cast<float>(Now - OpenedAt) / 0.3f);
	const ChaosImpactPaint::FPainter P{Design, Elements, Layer, In};
	const ChaosImpactPaint::FPainter Top{Design, Elements, Layer + 1, In};
	const ChaosImpactPaint::FPainter Text{Design, Elements, Layer + 2, In};

	// Title.
	Text.Text(TEXT("SETTINGS"), 800.0f, 26.0f, 54.0f, ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Center, TEXT("BlackItalic"));
	P.Box(650.0f, 98.0f, 150.0f, 6.0f, ChaosImpactPaint::Ice);
	P.Box(800.0f, 98.0f, 150.0f, 6.0f, ChaosImpactPaint::Fire);

	// Tabs.
	for (int32 Index = 0; Index < static_cast<int32>(ETab::Count); ++Index)
	{
		const FBox2D Rect = TabRect(Index);
		const bool bCurrent = Index == static_cast<int32>(Tab);
		const float X = static_cast<float>(Rect.Min.X);
		const float Y = static_cast<float>(Rect.Min.Y);
		ChaosImpactPaint::RoundedBox(P, X, Y, 230.0f, 54.0f, 16.0f, bCurrent ? FLinearColor(0.05f, 0.16f, 0.34f, 0.98f)
			: FLinearColor(0.02f, 0.03f, 0.06f, 0.9f));
		if (bCurrent)
		{
			const float Grow = ChaosImpactPaint::EaseOut(static_cast<float>(Now - TabChangedAt) / 0.2f);
			Top.Box(X + 115.0f - 100.0f * Grow, Y + 50.0f, 200.0f * Grow, 4.0f, ChaosImpactPaint::Ice);
		}
		Text.Text(TabNames[Index], X + 115.0f, Y + 10.0f, 26.0f, bCurrent ? ChaosImpactPaint::Paper : ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
	}
	Text.Text(bLastInputPad ? ChaosImpactSettings::GetKeyName(EKeys::Gamepad_LeftShoulder, LastPadDevice) : FString(TEXT("Q")),
		282.0f, 128.0f, 22.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Right, TEXT("Bold"));
	Text.Text(bLastInputPad ? ChaosImpactSettings::GetKeyName(EKeys::Gamepad_RightShoulder, LastPadDevice) : FString(TEXT("E")),
		1318.0f, 128.0f, 22.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Left, TEXT("Bold"));

	// Rows.
	const float RowsIn = ChaosImpactPaint::EaseOut(static_cast<float>(Now - TabChangedAt) / 0.22f);
	for (int32 Row = FirstVisibleRow; Row < FMath::Min(Rows.Num(), FirstVisibleRow + VisibleRows); ++Row)
	{
		const FRow& Item = Rows[Row];
		const FBox2D Rect = RowRect(Row);
		const float Slide = (1.0f - RowsIn) * (30.0f + 8.0f * (Row - FirstVisibleRow));
		const float X = static_cast<float>(Rect.Min.X) + Slide;
		const float Y = static_cast<float>(Rect.Min.Y);
		const ChaosImpactPaint::FPainter RowBack{Design, Elements, Layer, In * RowsIn};
		const ChaosImpactPaint::FPainter RowText{Design, Elements, Layer + 2, In * RowsIn};
		if (Item.Kind == ERowKind::Heading)
		{
			RowText.Text(Item.Label, X + 20.0f, Y + 18.0f, 22.0f, ChaosImpactPaint::Gold, ChaosImpactPaint::ETextAlign::Left, TEXT("Black"));
			RowBack.Box(X + 20.0f, Y + 50.0f, RowsWidth - 40.0f, 2.0f, ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Gold, 0.35f));
			if (Item.Id == 1)
			{
				RowText.Text(TEXT("1つめ"), CellLeft + Slide + CellWidth * 0.5f, Y + 20.0f, 18.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
				RowText.Text(TEXT("2つめ"), CellLeft + Slide + CellWidth * 1.5f + CellGap, Y + 20.0f, 18.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
			}
			continue;
		}
		const bool bSelected = Row == SelectedRow;
		const FLinearColor Accent = Tab == ETab::Controls ? ChaosImpactPaint::Ice : Tab == ETab::Picture ? ChaosImpactPaint::Gold : Tab == ETab::Display ? ChaosImpactPaint::Fire : ChaosImpactPaint::Violet;
		ChaosImpactPaint::RoundedBox(RowBack, X, Y, RowsWidth, RowHeight, 14.0f, bSelected ? FLinearColor(0.045f, 0.09f, 0.18f, 0.98f)
			: FLinearColor(0.018f, 0.026f, 0.05f, 0.92f));
		if (bSelected)
		{
			ChaosImpactPaint::RoundedBox(RowText, X, Y + 8.0f, 8.0f, RowHeight - 16.0f, 4.0f, Accent);
		}
		const FLinearColor LabelColor = Item.bDisabled ? ChaosImpactPaint::Muted : ChaosImpactPaint::Paper;
		RowText.Text(Item.Label, X + 36.0f, Y + 12.0f, 24.0f, LabelColor, ChaosImpactPaint::ETextAlign::Left, TEXT("Bold"));
		if (Item.Kind == ERowKind::Binding)
		{
			for (int32 Cell = 0; Cell < 2; ++Cell)
			{
				const FBox2D CellBox = CellRect(Row, Cell);
				const float CellX = static_cast<float>(CellBox.Min.X) + Slide;
				const bool bCell = bSelected && Cell == SelectedCell;
				const bool bWaiting = bCell && bWaitingForKey;
				ChaosImpactPaint::RoundedBox(RowText, CellX, static_cast<float>(CellBox.Min.Y), CellWidth, RowHeight - 12.0f, 10.0f,
					bWaiting ? ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Gold, 0.35f + 0.25f * FMath::Sin(static_cast<float>(Now) * 8.0f))
					: bCell ? FLinearColor(0.09f, 0.22f, 0.44f, 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
				const ChaosImpactPaint::FPainter CellText{Design, Elements, Layer + 3, In * RowsIn};
				CellText.Text(bWaiting ? FString(TEXT("おしてください…")) : Item.Cells[Cell], CellX + CellWidth * 0.5f,
					static_cast<float>(CellBox.Min.Y) + 8.0f, 22.0f, Item.Cells[Cell] == TEXT("－") && !bWaiting ? ChaosImpactPaint::Muted : ChaosImpactPaint::Paper,
					ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
			}
		}
		else if (Item.Kind == ERowKind::Value)
		{
			const float Centre = CellLeft + Slide + CellWidth + CellGap * 0.5f;
			RowText.Text(Item.Value, Centre, Y + 12.0f, 24.0f, Item.bDisabled ? ChaosImpactPaint::Muted : bSelected ? ChaosImpactPaint::Paper : ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Paper, 0.85f),
				ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
			if (!Item.bDisabled)
			{
				const FLinearColor Arrow = bSelected ? Accent : ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Muted, 0.7f);
				RowText.Text(TEXT("＜"), CellLeft + Slide + 10.0f, Y + 10.0f, 26.0f, Arrow, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
				RowText.Text(TEXT("＞"), CellLeft + Slide + CellWidth * 2.0f + CellGap - 10.0f, Y + 10.0f, 26.0f, Arrow,
					ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
			}
		}
		else if (!Item.Value.IsEmpty())
		{
			RowText.Text(Item.Value, RowsLeft + Slide + RowsWidth - 30.0f, Y + 14.0f, 22.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Right, TEXT("Bold"));
		}
		if (Item.Id == RowDeleteProfile && bDeleteArmed)
		{
			ChaosImpactPaint::RoundedBox(RowText, X, Y + RowHeight - 4.0f, RowsWidth, 4.0f, 2.0f, ChaosImpactPaint::Fire);
		}
	}

	// Scroll bar, when the tab has more rows than fit.
	if (Rows.Num() > VisibleRows)
	{
		const float Height = VisibleRows * RowStep - (RowStep - RowHeight);
		const float Thumb = Height * VisibleRows / Rows.Num();
		const float ThumbY = RowsTop + (Height - Thumb) * FirstVisibleRow / FMath::Max(Rows.Num() - VisibleRows, 1);
		ChaosImpactPaint::RoundedBox(P, RowsLeft + RowsWidth + 14.0f, RowsTop, 8.0f, Height, 4.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		ChaosImpactPaint::RoundedBox(Top, RowsLeft + RowsWidth + 14.0f, ThumbY, 8.0f, Thumb, 4.0f, ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Paper, 0.6f));
	}

	// A note about the last change, then the hint line and the back button.
	if (!Message.IsEmpty() && Now - MessageAt < 2.4)
	{
		const float Fade = FMath::Clamp(static_cast<float>(2.4 - (Now - MessageAt)) / 0.4f, 0.0f, 1.0f);
		const ChaosImpactPaint::FPainter Note{Design, Elements, Layer + 2, In * Fade};
		ChaosImpactPaint::RoundedBox(Note, 560.0f, 760.0f, 480.0f, 40.0f, 14.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
		const ChaosImpactPaint::FPainter NoteText{Design, Elements, Layer + 3, In * Fade};
		NoteText.Text(Message, 800.0f, 766.0f, 20.0f, ChaosImpactPaint::Gold, ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
	}
	ChaosImpactPaint::RoundedBox(P, static_cast<float>(BackButton.Min.X), static_cast<float>(BackButton.Min.Y), 240.0f, 54.0f, 16.0f,
		FLinearColor(0.02f, 0.03f, 0.06f, 0.95f));
	Text.Text(TEXT("もどる"), static_cast<float>(BackButton.Min.X) + 120.0f, static_cast<float>(BackButton.Min.Y) + 10.0f, 26.0f,
		ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
	Text.Text(HintText(), 900.0f, 820.0f, 18.0f, ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Paper, 0.75f), ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));

	int32 TopLayer = Layer + 4;
	// Waiting for a key: the screen dims and says what to press.
	if (bWaitingForKey && IsSelectable(SelectedRow))
	{
		const ChaosImpactPaint::FPainter Shade{Design, Elements, TopLayer, 1.0f};
		Shade.Box(-400.0f, -200.0f, 2400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		const ChaosImpactPaint::FPainter Box{Design, Elements, TopLayer + 1, 1.0f};
		ChaosImpactPaint::RoundedBox(Box, 280.0f, 330.0f, 1040.0f, 220.0f, 26.0f, FLinearColor(0.02f, 0.03f, 0.07f, 1.0f));
		ChaosImpactPaint::RoundedBox(Box, 280.0f, 330.0f, 1040.0f, 8.0f, 4.0f, ChaosImpactPaint::Gold);
		const ChaosImpactPaint::FPainter Words{Design, Elements, TopLayer + 2, 1.0f};
		Words.Text(Rows[SelectedRow].Label, 800.0f, 356.0f, 30.0f, ChaosImpactPaint::Paper, ChaosImpactPaint::ETextAlign::Center, TEXT("Black"));
		Words.Text(FString::Printf(TEXT("%s の、使いたい%sをおしてください"), *ChaosImpactSettings::GetDeviceName(EditedDevice),
			EditedDevice == EChaosImpactDevice::KeyboardMouse ? TEXT("キー・マウスボタン") : TEXT("ボタン")),
			800.0f, 422.0f, 20.0f, ChaosImpactPaint::WithAlpha(ChaosImpactPaint::Paper, 0.6f + 0.4f * FMath::Sin(static_cast<float>(Now) * 5.0f)), ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
		Words.Text(TEXT("Esc でやめる（何もしなければ 8 秒でもどります）"), 800.0f, 480.0f, 18.0f, ChaosImpactPaint::Muted, ChaosImpactPaint::ETextAlign::Center, TEXT("Bold"));
		TopLayer += 3;
	}
	return NameEntry.Paint(Design, Elements, TopLayer);
}
