#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactSfx.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
	using namespace ChaosImpactPaint;
	using EStep = UChaosImpactCharacterSelect::EStep;

	/** Player N's own controller on this machine (null when there is none). */
	const APlayerController* LocalPlayerController(const AChaosImpactPlayerController* Owner, const int32 Player)
	{
		const UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
		const TArray<ULocalPlayer*>* Locals = GameInstance ? &GameInstance->GetLocalPlayers() : nullptr;
		return Locals && Locals->IsValidIndex(Player) && (*Locals)[Player] ? (*Locals)[Player]->GetPlayerController(Owner->GetWorld()) : nullptr;
	}

	/** The same accents the controller assignment screen gives P1-P4. */
	FLinearColor SlotAccent(const int32 Player)
	{
		return PlayerAccents[FMath::Clamp(Player, 0, 3)];
	}

	/** Seconds between "start" and the next screen, so the GO! can land. */
	constexpr double StartDelay = 0.55;

	// Layout in the 1600 x 900 design space: icons on the left, player windows on the right, guide bar below.
	constexpr float IconSize = 142.0f;
	constexpr float IconGap = 10.0f;
	constexpr float GridLeft = 96.0f;
	constexpr float GridTop = 180.0f;
	constexpr float SelectPanelTop = 136.0f;
	constexpr float SelectPanelBottom = 792.0f;
	constexpr float WindowsLeft = 770.0f;
	constexpr float WindowsWidth = 774.0f;
	constexpr float WindowGap = 20.0f;
	constexpr float BarTop = 806.0f;

	bool IsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Candidate : Keys)
		{
			if (Key == Candidate)
			{
				return true;
			}
		}
		return false;
	}

	// Rounded shapes. The brushes live for the whole run: draw elements refer to them until the frame is drawn.
	const FSlateBrush& RoundBrush(const float Radius)
	{
		static const FSlateRoundedBoxBrush Small(FLinearColor::White, 12.0f);
		static const FSlateRoundedBoxBrush Medium(FLinearColor::White, 22.0f);
		static const FSlateRoundedBoxBrush Large(FLinearColor::White, 30.0f);
		return Radius <= 14.0f ? Small : Radius <= 24.0f ? Medium : Large;
	}

	void RoundBox(const FPainter& P, const float X, const float Y, const float W, const float H, const float Radius,
		const FLinearColor& Color)
	{
		if (W <= 0.0f || H <= 0.0f || P.Alpha * Color.A <= 0.001f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			&RoundBrush(Radius), ESlateDrawEffect::None, WithAlpha(Color, P.Alpha));
	}

	/** A hollow rounded frame: straight sides and quarter arcs (a rounded brush's outline draws filled here). */
	void RoundFrame(const FPainter& P, const float X, const float Y, const float W, const float H, const float Radius,
		const FLinearColor& Color, const float Width)
	{
		const float R = FMath::Min(Radius, FMath::Min(W, H) * 0.5f);
		const float Half = Width * 0.5f;
		P.Box(X + R, Y, W - R * 2.0f, Width, Color);
		P.Box(X + R, Y + H - Width, W - R * 2.0f, Width, Color);
		P.Box(X, Y + R, Width, H - R * 2.0f, Color);
		P.Box(X + W - Width, Y + R, Width, H - R * 2.0f, Color);
		const float ArcRadius = R - Half;
		P.Arc(FVector2D(X + R, Y + R), ArcRadius, 180.0f, 270.0f, Color, Width);
		P.Arc(FVector2D(X + W - R, Y + R), ArcRadius, 270.0f, 360.0f, Color, Width);
		P.Arc(FVector2D(X + W - R, Y + H - R), ArcRadius, 0.0f, 90.0f, Color, Width);
		P.Arc(FVector2D(X + R, Y + H - R), ArcRadius, 90.0f, 180.0f, Color, Width);
	}

	/** Draws a preview picture with Brush (its UV region already set) into X, Y, W, H. */
	void PaintPicture(const FPainter& P, const FSlateBrush& Brush, const float X, const float Y, const float W, const float H,
		const FLinearColor& Tint = FLinearColor::White)
	{
		if (!Brush.GetResourceObject() || W <= 0.0f || H <= 0.0f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(P.Elements, P.Layer,
			P.Geometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			&Brush, ESlateDrawEffect::None, WithAlpha(Tint, P.Alpha));
	}

	FSlateBrush MakePictureBrush(AChaosImpactCharacterPreview* Stage)
	{
		FSlateBrush Brush;
		if (Stage && Stage->GetPicture())
		{
			Brush.SetResourceObject(Stage->GetPicture());
		}
		Brush.ImageSize = FVector2D(AChaosImpactCharacterPreview::PictureWidth, AChaosImpactCharacterPreview::PictureHeight);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		return Brush;
	}
}

bool UChaosImpactCharacterSelect::IsTileUnlocked(const int32 Tile)
{
	return Tile >= 0 && Tile < ChaosImpactRoster::Num();
}

FVector2D UChaosImpactCharacterSelect::TileOrigin(const int32 Tile) const
{
	return FVector2D(GridLeft + (Tile % GridColumns) * (IconSize + IconGap), GridTop + (Tile / GridColumns) * (IconSize + IconGap));
}

FBox2D UChaosImpactCharacterSelect::WindowRect(const int32 Player) const
{
	const float Height = SelectPanelBottom - SelectPanelTop;
	if (Slots.Num() <= 1)
	{
		return FBox2D(FVector2D(WindowsLeft, SelectPanelTop), FVector2D(WindowsLeft + WindowsWidth, SelectPanelBottom));
	}
	if (Slots.Num() == 2)
	{
		const float Each = (Height - WindowGap) * 0.5f;
		const float Top = SelectPanelTop + Player * (Each + WindowGap);
		return FBox2D(FVector2D(WindowsLeft, Top), FVector2D(WindowsLeft + WindowsWidth, Top + Each));
	}
	const float EachWidth = (WindowsWidth - WindowGap) * 0.5f;
	const float EachHeight = (Height - WindowGap) * 0.5f;
	const float Left = WindowsLeft + (Player % 2) * (EachWidth + WindowGap);
	const float Top = SelectPanelTop + (Player / 2) * (EachHeight + WindowGap);
	return FBox2D(FVector2D(Left, Top), FVector2D(Left + EachWidth, Top + EachHeight));
}

void UChaosImpactCharacterSelect::Open(AChaosImpactPlayerController* InController)
{
	Close();
	Controller = InController;
	UWorld* World = InController ? InController->GetWorld() : nullptr;
	const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(InController);
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.ObjectFlags |= RF_Transient;
	const auto SpawnStage = [World, &Parameters](const FVector& Location)
	{
		return World->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(), Location,
			FRotator::ZeroRotator, Parameters);
	};

	// Icon faces: every roster character in its first colour, filmed far away from the level. They are made a
	// couple per frame in Tick (all at once held the screen up for most of a second as it opened).
	Portraits.Init(nullptr, ChaosImpactRoster::Num());
	PortraitBrushes.Init(FSlateBrush(), ChaosImpactRoster::Num());
	PortraitPendingSeconds.Init(0.0f, ChaosImpactRoster::Num());
	PortraitsMade = 0;

	const int32 PlayerCount = FMath::Clamp(InController->GetRequestedLocalPlayerCount(), 1, 4);
	Slots.SetNum(PlayerCount);
	for (int32 Player = 0; Player < PlayerCount; ++Player)
	{
		FSlot& Slot = Slots[Player];
		const FChaosImpactLoadout Saved = Loadouts ? Loadouts->GetLoadout(Player) : FChaosImpactLoadout();
		Slot.Character = ChaosImpactRoster::ClampIndex(Saved.Character);
		Slot.Cursor = Slot.Character;
		// Keep the last colour unless an earlier player already wears it on the same character.
		Slot.Colour = FindFreeColour(Player, Slot.Character,
			Saved.Colour >= 0 ? Saved.Colour : Player % ChaosImpactRoster::ColourCount, 1);
		Slot.bKeyboard = InController->IsKeyboardMouseAssignedToPlayer(Player);
		Slot.Profile = FMath::Max(ChaosImpactSettings::FindProfile(Saved.Nickname), 0);
		AChaosImpactCharacterPreview* Stage = SpawnStage(FVector(Player * 1600.0f, -90000.0f, 40000.0f));
		Previews.Add(Stage);
		PictureBrushes.Add(MakePictureBrush(Stage));
		RefreshPreview(Player);
	}
	OpenedAt = FPlatformTime::Seconds();
	AllReadyAt = -100.0;
	bStarting = false;
	NamePickerOwner = INDEX_NONE;
	NameEntry.Close();
	bOpen = true;
	UE_LOG(LogChaosImpact, Log, TEXT("Character select opened for %d player(s)"), PlayerCount);
}

void UChaosImpactCharacterSelect::Close()
{
	for (AChaosImpactCharacterPreview* Stage : Previews)
	{
		if (IsValid(Stage))
		{
			Stage->Destroy();
		}
	}
	for (AChaosImpactCharacterPreview* Stage : Portraits)
	{
		if (IsValid(Stage))
		{
			Stage->Destroy();
		}
	}
	Previews.Reset();
	Portraits.Reset();
	PortraitPendingSeconds.Reset();
	PortraitBornAt.Reset();
	PortraitFrame = 0;
	PortraitsMade = 0;
	PictureBrushes.Reset();
	PortraitBrushes.Reset();
	Slots.Reset();
	bOpen = false;
	bStarting = false;
}

bool UChaosImpactCharacterSelect::AreAllReady() const
{
	if (Slots.IsEmpty())
	{
		return false;
	}
	for (const FSlot& Slot : Slots)
	{
		if (Slot.Step != EStep::Done)
		{
			return false;
		}
	}
	return true;
}

bool UChaosImpactCharacterSelect::IsColourTaken(const int32 Player, const int32 Character, const int32 Colour) const
{
	for (int32 Other = 0; Other < Slots.Num(); ++Other)
	{
		// While opening, earlier players' saved colours count; afterwards only players who have chosen a character.
		const bool bCounts = bOpen ? Slots[Other].Step != EStep::Character : Other < Player;
		if (Other != Player && bCounts && Slots[Other].Character == Character && Slots[Other].Colour == Colour)
		{
			return true;
		}
	}
	return false;
}

int32 UChaosImpactCharacterSelect::FindFreeColour(const int32 Player, const int32 Character, const int32 Start,
	const int32 Direction) const
{
	const int32 Count = ChaosImpactRoster::ColourCount;
	const int32 Step = Direction < 0 ? -1 : 1;
	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		const int32 Colour = ((Start + Step * Offset) % Count + Count) % Count;
		if (!IsColourTaken(Player, Character, Colour))
		{
			return Colour;
		}
	}
	return (Start % Count + Count) % Count;
}

void UChaosImpactCharacterSelect::RefreshPreview(const int32 Player)
{
	if (Slots.IsValidIndex(Player) && Previews.IsValidIndex(Player) && IsValid(Previews[Player]))
	{
		Previews[Player]->ShowLoadout(Slots[Player].Character, Slots[Player].Colour, SlotAccent(Player));
	}
}

void UChaosImpactCharacterSelect::Commit(const int32 Player)
{
	if (!Slots.IsValidIndex(Player))
	{
		return;
	}
	const FSlot& Slot = Slots[Player];
	if (UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(Controller.Get()))
	{
		Loadouts->SetLoadout(Player, {Slot.Character, Slot.Colour});
	}
}

void UChaosImpactCharacterSelect::SetStep(const int32 Player, const EStep Step)
{
	FSlot& Slot = Slots[Player];
	Slot.Step = Step;
	Slot.StepAt = FPlatformTime::Seconds();
	AllReadyAt = AreAllReady() ? Slot.StepAt : -100.0;
}

void UChaosImpactCharacterSelect::MoveCursor(const int32 Player, const int32 Columns, const int32 Rows)
{
	if (!bOpen || bStarting || !Slots.IsValidIndex(Player))
	{
		return;
	}
	FSlot& Slot = Slots[Player];
	if (Slot.Step == EStep::Colour)
	{
		// The colour row is worked with left and right.
		if (Columns != 0)
		{
			ChangeColour(Player, Columns);
		}
		return;
	}
	if (Slot.Step != EStep::Character)
	{
		return;
	}
	const int32 Column = FMath::Clamp(Slot.Cursor % GridColumns + FMath::Sign(Columns), 0, GridColumns - 1);
	const int32 Row = FMath::Clamp(Slot.Cursor / GridColumns + FMath::Sign(Rows), 0, GridRows - 1);
	const int32 Tile = Row * GridColumns + Column;
	if (Tile == Slot.Cursor)
	{
		return;
	}
	Slot.Cursor = Tile;
	Slot.MovedAt = FPlatformTime::Seconds();
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiMove);
	// The window shows the character under the cursor straight away.
	if (IsTileUnlocked(Tile) && Tile != Slot.Character)
	{
		Slot.Character = Tile;
		RefreshPreview(Player);
	}
}

void UChaosImpactCharacterSelect::ChangeColour(const int32 Player, const int32 Direction)
{
	if (!bOpen || bStarting || !Slots.IsValidIndex(Player) || Slots[Player].Step != EStep::Colour)
	{
		return;
	}
	FSlot& Slot = Slots[Player];
	const int32 Step = Direction < 0 ? -1 : 1;
	const int32 Next = FindFreeColour(Player, Slot.Character, Slot.Colour + Step, Step);
	if (Next == Slot.Colour)
	{
		return;
	}
	Slot.Colour = Next;
	Slot.LastDirection = Step;
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiValue);
	Slot.ChangedAt = FPlatformTime::Seconds();
	RefreshPreview(Player);
	Commit(Player);
}

void UChaosImpactCharacterSelect::PressConfirm(const int32 Player)
{
	if (!bOpen || bStarting || !Slots.IsValidIndex(Player))
	{
		return;
	}
	if (AreAllReady())
	{
		// Everyone is done: whoever presses again starts.
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiStart);
		bStarting = true;
		StartAt = FPlatformTime::Seconds() + StartDelay;
		for (AChaosImpactCharacterPreview* Stage : Previews)
		{
			if (IsValid(Stage))
			{
				Stage->PlayReady();
			}
		}
		UE_LOG(LogChaosImpact, Log, TEXT("Character select: starting (pressed by P%d)"), Player + 1);
		return;
	}
	FSlot& Slot = Slots[Player];
	if (Slot.Step == EStep::Character)
	{
		if (!IsTileUnlocked(Slot.Cursor))
		{
			ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiDeny);
			return;
		}
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiSelect);
		Slot.Character = Slot.Cursor;
		const bool bVariants = ChaosImpactRoster::Get(Slot.Character).bColourVariants;
		SetStep(Player, bVariants ? EStep::Colour : EStep::Done);
		// The row opens on a colour nobody else has on this character.
		Slot.Colour = FindFreeColour(Player, Slot.Character, Slot.Colour, 1);
		Slot.ChangedAt = Slot.StepAt;
		RefreshPreview(Player);
		Commit(Player);
		if (!bVariants && Previews.IsValidIndex(Player) && IsValid(Previews[Player]))
		{
			Previews[Player]->PlayReady();
		}
	}
	else if (Slot.Step == EStep::Colour)
	{
		ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiReady);
		SetStep(Player, EStep::Done);
		Commit(Player);
		if (Previews.IsValidIndex(Player) && IsValid(Previews[Player]))
		{
			Previews[Player]->PlayReady();
		}
	}
}

void UChaosImpactCharacterSelect::PressBack(const int32 Player)
{
	if (!bOpen || bStarting || !Slots.IsValidIndex(Player))
	{
		return;
	}
	const FSlot& Slot = Slots[Player];
	ChaosImpactSfx::Play2D(Controller.Get(), EChaosImpactSfx::UiBack);
	if (Slot.Step == EStep::Done)
	{
		SetStep(Player, ChaosImpactRoster::Get(Slot.Character).bColourVariants ? EStep::Colour : EStep::Character);
		return;
	}
	if (Slot.Step == EStep::Colour)
	{
		SetStep(Player, EStep::Character);
		return;
	}
	// Only 1P leaves the screen, so another player stepping back never throws everyone out.
	if (Player == 0 && Controller.IsValid())
	{
		Controller->CancelCharacterSelection();
	}
}

void UChaosImpactCharacterSelect::Tick(const float DeltaSeconds)
{
	if (bOpen)
	{
		const float Step = FMath::Min(DeltaSeconds, 0.1f);
		for (AChaosImpactCharacterPreview* Stage : Previews)
		{
			if (IsValid(Stage))
			{
				Stage->Animate(Step);
				Stage->FilmNow();
			}
		}
		// The icon faces, two more each frame until all are made.
		for (int32 Made = 0; Made < 2 && PortraitsMade < Portraits.Num(); ++Made, ++PortraitsMade)
		{
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Parameters.ObjectFlags |= RF_Transient;
			AChaosImpactCharacterPreview* Portrait = Controller.IsValid() && Controller->GetWorld()
				? Controller->GetWorld()->SpawnActor<AChaosImpactCharacterPreview>(AChaosImpactCharacterPreview::StaticClass(),
					FVector(PortraitsMade * 1600.0f, -96000.0f, 40000.0f), FRotator::ZeroRotator, Parameters)
				: nullptr;
			if (Portrait)
			{
				Portrait->ShowLoadout(PortraitsMade, 0, Paper);
				Portrait->UseAsPortrait();
			}
			Portraits[PortraitsMade] = Portrait;
			PortraitBrushes[PortraitsMade] = MakePictureBrush(Portrait);
			PortraitBornAt.Add(PortraitsMade, FPlatformTime::Seconds());
		}
		// Each icon face is a whole scene filmed: only the faces the players' cursors are on keep moving (filmed every
		// other frame, with the time they missed). The rest hold still after their first second, when their textures
		// have arrived; a tile left keeps its last frame.
		++PortraitFrame;
		const double NowSeconds = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < Portraits.Num(); ++Index)
		{
			PortraitPendingSeconds[Index] += Step;
			AChaosImpactCharacterPreview* Stage = Portraits[Index];
			if (!IsValid(Stage))
			{
				continue;
			}
			bool bHovered = false;
			for (const FSlot& Slot : Slots)
			{
				bHovered |= Slot.Cursor == Index;
			}
			const double* Born = PortraitBornAt.Find(Index);
			const bool bSettling = Born && NowSeconds - *Born < 1.2;
			if ((bHovered || bSettling) && (PortraitFrame + Index) % (bHovered ? 2 : 4) == 0)
			{
				Stage->Animate(FMath::Min(PortraitPendingSeconds[Index], 0.1f));
				Stage->FilmNow();
				PortraitPendingSeconds[Index] = 0.0f;
			}
		}
	}
	if (!bOpen || !bStarting || FPlatformTime::Seconds() < StartAt)
	{
		return;
	}
	for (int32 Player = 0; Player < Slots.Num(); ++Player)
	{
		Commit(Player);
	}
	bStarting = false;
	// Going on closes this screen through the menu, so nothing may follow it.
	if (AChaosImpactPlayerController* Owner = Controller.Get())
	{
		Owner->ConfirmCharacterSelection();
	}
}

bool UChaosImpactCharacterSelect::HandleKeyDown(const FKeyEvent& Event)
{
	AChaosImpactPlayerController* Owner = Controller.Get();
	const FKey Key = Event.GetKey();
	if (!bOpen || !Owner || Key.IsMouseButton())
	{
		return false;
	}
	const int32 Player = Owner->GetLocalPlayerIndexForDevice(!Key.IsGamepadKey(), Event.GetInputDeviceId().GetId());
	if (!Slots.IsValidIndex(Player))
	{
		// A device nobody holds does nothing here, and must not reach the rest of the menu either.
		return true;
	}
	const bool bRepeat = Event.IsRepeat();
	// A Switch controller decides with A (its right button) when that option is on.
	const FKey MenuKey = ChaosImpactSettings::ToMenuKey(Key, Event.GetInputDeviceId().GetId());
	if (NameEntry.IsOpen())
	{
		if (Player == NameEntry.GetOwner())
		{
			NameEntry.HandleKey(MenuKey, bRepeat);
			FinishNameEntry();
		}
		return true;
	}
	if (NamePickerOwner != INDEX_NONE)
	{
		if (Player != NamePickerOwner)
		{
			return true;
		}
		if (IsAny(MenuKey, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W}))
		{
			NamePickerChoice = (NamePickerChoice + NamePickerLines() - 1) % NamePickerLines();
		}
		else if (IsAny(MenuKey, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S}))
		{
			NamePickerChoice = (NamePickerChoice + 1) % NamePickerLines();
		}
		else if (!bRepeat && IsAny(MenuKey, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar}))
		{
			PickName(NamePickerChoice);
		}
		else if (!bRepeat && IsAny(MenuKey, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace,
			EKeys::Gamepad_FaceButton_Top, EKeys::N, EKeys::Tab}))
		{
			CloseNamePicker();
		}
		return true;
	}
	if (!bRepeat && !bStarting && IsAny(MenuKey, {EKeys::Gamepad_FaceButton_Top, EKeys::N, EKeys::Tab}))
	{
		OpenNamePicker(Player);
		return true;
	}
	if (IsAny(MenuKey, {EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A}))
	{
		MoveCursor(Player, -1, 0);
	}
	else if (IsAny(Key, {EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D}))
	{
		MoveCursor(Player, 1, 0);
	}
	else if (IsAny(Key, {EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W}))
	{
		MoveCursor(Player, 0, -1);
	}
	else if (IsAny(Key, {EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S}))
	{
		MoveCursor(Player, 0, 1);
	}
	else if (IsAny(Key, {EKeys::Gamepad_LeftShoulder, EKeys::Q}))
	{
		ChangeColour(Player, -1);
	}
	else if (IsAny(Key, {EKeys::Gamepad_RightShoulder, EKeys::E}))
	{
		ChangeColour(Player, 1);
	}
	else if (!bRepeat && IsAny(MenuKey, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_Special_Right, EKeys::Enter,
		EKeys::SpaceBar}))
	{
		PressConfirm(Player);
	}
	else if (!bRepeat && IsAny(MenuKey, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace}))
	{
		PressBack(Player);
	}
	return true;
}

bool UChaosImpactCharacterSelect::HandleAnalog(const FAnalogInputEvent& Event)
{
	AChaosImpactPlayerController* Owner = Controller.Get();
	const FKey Key = Event.GetKey();
	if (!bOpen || !Owner || (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY))
	{
		return false;
	}
	const int32 Player = Owner->GetLocalPlayerIndexForDevice(false, Event.GetInputDeviceId().GetId());
	if (!Slots.IsValidIndex(Player))
	{
		return true;
	}
	if (NameEntry.IsOpen())
	{
		if (Player == NameEntry.GetOwner())
		{
			NameEntry.HandleAnalog(Key, Event.GetAnalogValue());
		}
		return true;
	}
	if (NamePickerOwner != INDEX_NONE)
	{
		const double When = FPlatformTime::Seconds();
		if (Player == NamePickerOwner && Key == EKeys::Gamepad_LeftY && FMath::Abs(Event.GetAnalogValue()) >= 0.65f
			&& When >= NextPickerStickAt)
		{
			NextPickerStickAt = When + 0.2;
			NamePickerChoice = (NamePickerChoice + NamePickerLines() + (Event.GetAnalogValue() > 0.0f ? -1 : 1)) % NamePickerLines();
		}
		return true;
	}
	FSlot& Slot = Slots[Player];
	const float Value = Event.GetAnalogValue();
	const double Now = FPlatformTime::Seconds();
	const int32 Axis = Key == EKeys::Gamepad_LeftX ? 0 : 1;
	if (FMath::Abs(Value) < 0.3f)
	{
		Slot.bStickHeld[Axis] = false;
		return true;
	}
	// A flick moves once; holding repeats.
	if (FMath::Abs(Value) >= 0.65f && (!Slot.bStickHeld[Axis] || Now >= Slot.NextStickAt[Axis]))
	{
		Slot.NextStickAt[Axis] = Now + (Slot.bStickHeld[Axis] ? 0.16 : 0.38);
		Slot.bStickHeld[Axis] = true;
		if (Axis == 0)
		{
			MoveCursor(Player, Value > 0.0f ? 1 : -1, 0);
		}
		else
		{
			// Stick up is positive Y, the row above.
			MoveCursor(Player, 0, Value > 0.0f ? -1 : 1);
		}
	}
	return true;
}

bool UChaosImpactCharacterSelect::HandleClick(const FVector2D& DesignPoint)
{
	AChaosImpactPlayerController* Owner = Controller.Get();
	if (!bOpen || !Owner)
	{
		return false;
	}
	const int32 Player = Owner->GetLocalPlayerIndexForDevice(true, 0);
	if (!Slots.IsValidIndex(Player))
	{
		return false;
	}
	if (NameEntry.IsOpen())
	{
		if (Player == NameEntry.GetOwner())
		{
			NameEntry.HandleClick(DesignPoint);
			FinishNameEntry();
		}
		return true;
	}
	if (NamePickerOwner != INDEX_NONE)
	{
		if (Player == NamePickerOwner)
		{
			for (int32 Line = 0; Line < NamePickerLines(); ++Line)
			{
				if (PickerLineRect(Line).IsInside(DesignPoint))
				{
					PickName(Line);
					return true;
				}
			}
			CloseNamePicker();
		}
		return true;
	}
	if (!bStarting && NameTagRect(Player).IsInside(DesignPoint))
	{
		OpenNamePicker(Player);
		return true;
	}
	if (AreAllReady() && DesignPoint.Y >= BarTop)
	{
		PressConfirm(Player);
		return true;
	}
	FSlot& Slot = Slots[Player];
	for (int32 Tile = 0; Tile < GetTileCount() && Slot.Step == EStep::Character; ++Tile)
	{
		const FVector2D Origin = TileOrigin(Tile);
		if (DesignPoint.X >= Origin.X && DesignPoint.X <= Origin.X + IconSize
			&& DesignPoint.Y >= Origin.Y && DesignPoint.Y <= Origin.Y + IconSize)
		{
			Slot.Cursor = Tile;
			Slot.MovedAt = FPlatformTime::Seconds();
			if (IsTileUnlocked(Tile))
			{
				Slot.Character = Tile;
				RefreshPreview(Player);
			}
			PressConfirm(Player);
			return true;
		}
	}
	return false;
}

int32 UChaosImpactCharacterSelect::Paint(const FGeometry& Design, FSlateWindowElementList& Elements, const int32 Layer) const
{
	if (!bOpen)
	{
		return Layer;
	}
	const double Now = FPlatformTime::Seconds();
	const float T = static_cast<float>(Now - OpenedAt);
	const AChaosImpactPlayerController* Owner = Controller.Get();
	const float Shimmer = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 3.2f);

	// ---- Heading ------------------------------------------------------------------------------
	{
		const float In = EaseOut(T / 0.35f);
		const float HeadX = 56.0f - (1.0f - In) * 70.0f;
		const FGeometry SkewHead = MakeSkewed(Design, HeadX, 20.0f, 850.0f, 102.0f, -0.22f);
		const FPainter HBack{SkewHead, Elements, Layer + 1, In};
		RoundBox(HBack, 0.0f, 8.0f, 640.0f, 82.0f, 18.0f, FLinearColor(0.012f, 0.02f, 0.048f, 0.92f));
		HBack.Box(12.0f, 16.0f, 10.0f, 66.0f, Ice);
		HBack.Box(28.0f, 16.0f, 4.0f, 66.0f, Fire);
		HBack.Box(12.0f, 84.0f, 420.0f * In, 6.0f, Ice);
		HBack.Box(440.0f * In, 84.0f, 150.0f * In, 6.0f, Fire);
		for (int32 Chev = 0; Chev < 4; ++Chev)
		{
			const float Pulse = 0.25f + 0.55f * FMath::Max(0.0f, FMath::Sin(static_cast<float>(Now) * 5.0f - Chev * 0.7f));
			HBack.Box(535.0f + Chev * 20.0f, 26.0f, 10.0f, 46.0f, WithAlpha(Ice, Pulse));
		}

		const FPainter Title{Design, Elements, Layer + 2, In};
		Title.Text(TEXT("CHARACTER SELECT"), HeadX + 42.0f, 22.0f, 44.0f, Paper, ETextAlign::Left,
			TEXT("BlackItalic"), 3.0f, Ink);
		Title.Text(TEXT("ファイターセレクト  //  CHOOSE YOUR FIGHTER"), HeadX + 46.0f, 75.0f, 15.0f,
			FMath::Lerp(Ice, Paper, 0.55f), ETextAlign::Left, TEXT("Bold"), 1.0f, Ink);

		// Active player count status pill on the right side of the header
		const FGeometry StatusSkew = MakeSkewed(Design, 1250.0f + (1.0f - In) * 50.0f, 36.0f, 294.0f, 54.0f, -0.2f);
		const FPainter Status{StatusSkew, Elements, Layer + 1, In};
		RoundBox(Status, 0.0f, 0.0f, 294.0f, 54.0f, 16.0f, FLinearColor(0.015f, 0.025f, 0.055f, 0.9f));
		RoundFrame(Status, 0.0f, 0.0f, 294.0f, 54.0f, 16.0f, WithAlpha(Ice, 0.45f), 2.5f);
		int32 ReadyCount = 0;
		for (const FSlot& S : Slots) { if (S.Step == EStep::Done) { ++ReadyCount; } }
		const FPainter StatusText{Design, Elements, Layer + 2, In};
		StatusText.Text(FString::Printf(TEXT("%dP BATTLE  •  %d/%d READY"), Slots.Num(), ReadyCount, Slots.Num()),
			1397.0f + (1.0f - In) * 50.0f, 49.0f, 18.0f, ReadyCount == Slots.Num() ? Gold : Paper,
			ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	}

	// ---- Icon grid ----------------------------------------------------------------------------
	{
		const float In = EaseOut((T - 0.05f) / 0.3f);
		const FPainter Panel{Design, Elements, Layer + 1, In};
		const float PanelW = 688.0f;
		const float PanelH = SelectPanelBottom - SelectPanelTop;
		RoundBox(Panel, 56.0f, SelectPanelTop, PanelW, PanelH, 30.0f, FLinearColor(0.012f, 0.02f, 0.048f, 0.92f));
		RoundBox(Panel, 64.0f, SelectPanelTop + 8.0f, PanelW - 16.0f, 36.0f, 18.0f, FLinearColor(0.04f, 0.09f, 0.22f, 0.45f));
		RoundFrame(Panel, 56.0f, SelectPanelTop, PanelW, PanelH, 30.0f, FLinearColor(0.14f, 0.32f, 0.68f, 0.55f), 3.0f);
		// Corner tech accents on the roster container
		Panel.Box(86.0f, SelectPanelTop + 2.0f, 140.0f, 4.0f, WithAlpha(Ice, 0.85f));
		Panel.Box(56.0f + PanelW - 226.0f, SelectPanelBottom - 6.0f, 140.0f, 4.0f, WithAlpha(Fire, 0.85f));
		Panel.Text(FString::Printf(TEXT("ROSTER // %d FIGHTERS"), ChaosImpactRoster::Num()), 86.0f, SelectPanelTop + 12.0f, 14.0f,
			WithAlpha(Ice, 0.8f), ETextAlign::Left, TEXT("BlackItalic"));
	}
	for (int32 Tile = 0; Tile < GetTileCount(); ++Tile)
	{
		const float In = EaseOut((T - 0.1f - Tile * 0.025f) / 0.28f);
		const FVector2D Origin = TileOrigin(Tile);
		const bool bUnlocked = IsTileUnlocked(Tile);
		TArray<int32, TInlineAllocator<4>> Here;
		bool bAnyLockedInHere = false;
		FLinearColor ActiveAccent = Ice;
		for (int32 Player = 0; Player < Slots.Num(); ++Player)
		{
			if (Slots[Player].Cursor == Tile)
			{
				Here.Add(Player);
				if (Slots[Player].Step != EStep::Character)
				{
					bAnyLockedInHere = true;
				}
			}
		}
		if (!Here.IsEmpty())
		{
			ActiveAccent = SlotAccent(Here[0]);
		}
		const float Pop = Here.IsEmpty() ? 1.0f : 1.055f;
		const FGeometry IconSpace = MakeSkewed(Design, Origin.X, Origin.Y + (1.0f - In) * 40.0f, IconSize, IconSize, 0.0f, Pop * In);
		const int32 IconLayer = Layer + 2 + (Here.IsEmpty() ? 0 : 3);
		const FPainter I{IconSpace, Elements, IconLayer, In};
		const FPainter IFace{IconSpace, Elements, IconLayer + 1, In};
		const FPainter ILabel{IconSpace, Elements, IconLayer + 2, In};
		if (bUnlocked)
		{
			const auto ModelKind = ChaosImpactRoster::Get(Tile).Kind;
			const FLinearColor TopCard = Here.IsEmpty()
				? FLinearColor(0.06f, 0.16f, 0.42f, 1.0f)
				: FMath::Lerp(FLinearColor(0.09f, 0.24f, 0.58f, 1.0f), ActiveAccent, bAnyLockedInHere ? 0.45f : 0.22f);
			const FLinearColor BotCard = Here.IsEmpty()
				? FLinearColor(0.02f, 0.065f, 0.2f, 1.0f)
				: FMath::Lerp(FLinearColor(0.03f, 0.1f, 0.28f, 1.0f), ActiveAccent, 0.15f);
			RoundBox(I, 0.0f, 0.0f, IconSize, IconSize, 22.0f, TopCard);
			RoundBox(I, 0.0f, IconSize * 0.45f, IconSize, IconSize * 0.55f, 22.0f, BotCard);
			// Subtle inner highlight studio rim
			RoundBox(I, 6.0f, 6.0f, IconSize - 12.0f, IconSize * 0.38f, 16.0f, FLinearColor(0.25f, 0.55f, 1.0f, Here.IsEmpty() ? 0.12f : 0.24f));
			RoundFrame(I, 1.5f, 1.5f, IconSize - 3.0f, IconSize - 3.0f, 21.0f,
				Here.IsEmpty() ? FLinearColor(0.2f, 0.45f, 0.88f, 0.42f) : WithAlpha(Paper, 0.75f), 2.0f);
			// Fighter number tag in top-left corner
			I.Text(FString::Printf(TEXT("%02d"), Tile + 1), 13.0f, 8.0f, 13.0f,
				WithAlpha(Paper, Here.IsEmpty() ? 0.38f : 0.75f), ETextAlign::Left, TEXT("BlackItalic"));

			if (PortraitBrushes.IsValidIndex(Tile) && PortraitBrushes[Tile].GetResourceObject())
			{
				FSlateBrush& Face = PortraitBrushes[Tile];
				// Nicola's face is below its waist: the normal upper-body crop would show only its raised feet.
				const bool bWholeBody = ModelKind == EChaosImpactModelKind::Authored || ModelKind == EChaosImpactModelKind::Handstand;
				const bool bMugCarp = Tile == ChaosImpactRoster::CapIndex;
				Face.SetUVRegion(bWholeBody
					? FBox2f(FVector2f(0.03f, 0.2f), FVector2f(0.97f, 0.8f))
					: bMugCarp
						? FBox2f(FVector2f(0.14f, 0.06f), FVector2f(0.86f, 0.62f))
						: FBox2f(FVector2f(0.2f, 0.04f), FVector2f(0.8f, 0.5f)));
				PaintPicture(IFace, Face, 10.0f, 4.0f, IconSize - 20.0f, IconSize - 38.0f);
			}
			if (*ChaosImpactRoster::Get(Tile).Name)
			{
				const float PlateY = IconSize - 34.0f;
				RoundBox(ILabel, 6.0f, PlateY, IconSize - 12.0f, 28.0f, 10.0f, FLinearColor(0.005f, 0.01f, 0.025f, 0.92f));
				ILabel.Box(14.0f, PlateY + 1.0f, IconSize - 28.0f, 2.5f, Here.IsEmpty() ? WithAlpha(Ice, 0.65f) : ActiveAccent);
				const int32 NameLen = FCString::Strlen(ChaosImpactRoster::Get(Tile).Name);
				const float FontSize = NameLen >= 9 ? 11.5f : (ModelKind == EChaosImpactModelKind::Handstand || NameLen >= 6 ? 13.5f : 16.0f);
				ILabel.Text(ChaosImpactRoster::Get(Tile).Name, IconSize * 0.5f, PlateY + (NameLen >= 9 ? 5.0f : NameLen >= 6 ? 3.5f : 1.5f),
					FontSize, Here.IsEmpty() ? Paper : FMath::Lerp(Paper, ActiveAccent, 0.2f),
					ETextAlign::Center, TEXT("Black"), 1.5f, Ink);
			}
		}
		else
		{
			RoundBox(I, 0.0f, 0.0f, IconSize, IconSize, 20.0f, FLinearColor(0.025f, 0.032f, 0.058f, 0.95f));
			RoundFrame(I, 2.0f, 2.0f, IconSize - 4.0f, IconSize - 4.0f, 18.0f, FLinearColor(0.12f, 0.16f, 0.26f, 0.5f), 2.0f);
			I.Ring(FVector2D(IconSize * 0.5f, IconSize * 0.44f), 32.0f, WithAlpha(Muted, 0.18f), 2.0f);
			I.Text(TEXT("?"), IconSize * 0.5f, 24.0f, 56.0f, WithAlpha(Muted, 0.35f + 0.1f * Shimmer), ETextAlign::Center, TEXT("BlackItalic"));
			I.Text(TEXT("LOCKED"), IconSize * 0.5f, IconSize - 28.0f, 12.0f, WithAlpha(Muted, 0.3f), ETextAlign::Center, TEXT("BlackItalic"));
		}

		// Cursors: a thick rounded frame per player, the later ones outside the earlier, and a name tag.
		const FPainter F{IconSpace, Elements, IconLayer + 3, In};
		const FPainter Tags{IconSpace, Elements, IconLayer + 4, In};
		const FPainter TagText{IconSpace, Elements, IconLayer + 5, In};
		// Lower-row tags sit inside the icon rather than covering the previous row's character names.
		const float TagY = Tile >= GridColumns ? 8.0f : -(6.0f + (Here.Num() - 1) * 9.0f) - 38.0f;
		for (int32 Order = 0; Order < Here.Num(); ++Order)
		{
			const int32 Player = Here[Order];
			const float Moved = static_cast<float>(Now - Slots[Player].MovedAt);
			const float Grow = Moved < 0.15f ? 8.0f * (1.0f - Moved / 0.15f) : 0.0f;
			const float Out = 5.0f + Order * 9.0f + Grow;
			const FLinearColor PCol = SlotAccent(Player);
			RoundFrame(F, -Out - 2.0f, -Out - 2.0f, IconSize + (Out + 2.0f) * 2.0f, IconSize + (Out + 2.0f) * 2.0f,
				28.0f + Out, WithAlpha(PCol, 0.32f), 9.0f);
			RoundFrame(F, -Out, -Out, IconSize + Out * 2.0f, IconSize + Out * 2.0f, 26.0f + Out, PCol, 5.5f);
		}
		for (int32 Order = 0; Order < Here.Num(); ++Order)
		{
			const int32 Player = Here[Order];
			const FSlot& Slot = Slots[Player];
			const bool bChosen = Slot.Step != EStep::Character;
			const float Bob = bChosen ? 0.0f : 3.0f * FMath::Sin(static_cast<float>(Now) * 6.0f + Player);
			const float TagX = -8.0f + Order * 60.0f;
			// A chosen player's tag turns white with their colour on it.
			RoundBox(Tags, TagX - 2.0f, TagY + Bob - 2.0f, 58.0f, 36.0f, 14.0f, WithAlpha(Ink, 0.85f));
			RoundBox(Tags, TagX, TagY + Bob, 54.0f, 32.0f, 12.0f, bChosen ? Paper : SlotAccent(Player));
			TagText.Text(FString::Printf(TEXT("%dP"), Player + 1), TagX + 27.0f, TagY - 1.0f + Bob, 20.0f,
				bChosen ? SlotAccent(Player) : Ink, ETextAlign::Center, TEXT("BlackItalic"));
		}
	}

	// ---- Player windows -----------------------------------------------------------------------
	for (int32 Player = 0; Player < Slots.Num(); ++Player)
	{
		PaintWindow(Player, Design, Elements, Layer + 10, Now);
	}
	if (NamePickerOwner != INDEX_NONE || NameEntry.IsOpen())
	{
		PaintNamePicker(Design, Elements, Layer + 22, Now);
		return NameEntry.Paint(Design, Elements, Layer + 26);
	}

	// ---- Guide bar / start --------------------------------------------------------------------
	const float BarIn = EaseOut((T - 0.25f) / 0.3f);
	if (AreAllReady() || bStarting)
	{
		const float Since = static_cast<float>(Now - AllReadyAt);
		const float In = EaseOut(Since / 0.25f);
		const FGeometry Bar = MakeSkewed(Design, 56.0f, BarTop + (1.0f - In) * 60.0f, 1488.0f, 78.0f, -0.14f, 0.92f + 0.08f * In);
		const FPainter B{Bar, Elements, Layer + 16, In};
		RoundBox(B, -4.0f, -4.0f, 1496.0f, 86.0f, 32.0f, WithAlpha(Fire, 0.55f));
		RoundBox(B, 0.0f, 0.0f, 1488.0f, 78.0f, 28.0f, Gold);
		RoundBox(B, 10.0f, 6.0f, 1468.0f, 30.0f, 18.0f, WithAlpha(Paper, 0.28f));
		const float Pulse = 0.7f + 0.3f * FMath::Sin(static_cast<float>(Now) * 6.0f);
		for (int32 Stripe = 0; Stripe < 6; ++Stripe)
		{
			B.Box(340.0f + Stripe * 32.0f, 14.0f, 14.0f, 50.0f, WithAlpha(Ink, 0.12f + 0.08f * Pulse));
		}
		if (bStarting)
		{
			const float Go = static_cast<float>(Now - (StartAt - StartDelay));
			RoundBox(B, 0.0f, 0.0f, 1488.0f, 78.0f, 28.0f, WithAlpha(Paper, 0.75f * FMath::Clamp(1.0f - Go / 0.25f, 0.0f, 1.0f)));
			B.Text(TEXT("GO!"), 744.0f, -6.0f, 62.0f * (1.0f + 0.3f * FMath::Exp(-10.0f * Go)), Ink, ETextAlign::Center,
				TEXT("BlackItalic"));
		}
		else
		{
			B.Text(TEXT("READY TO BATTLE!"), 44.0f, -1.0f, 52.0f, Ink, ETextAlign::Left, TEXT("BlackItalic"));
			RoundBox(B, 1040.0f, 14.0f, 424.0f, 50.0f, 18.0f, WithAlpha(Ink, 0.88f));
			B.Text(Owner && Owner->IsKeyboardMouseJoined() ? TEXT("A / Enter でスタート!") : TEXT("Aボタンでスタート!"),
				1252.0f, 18.0f, 30.0f, FMath::Lerp(Gold, Paper, Pulse * 0.5f), ETextAlign::Center, TEXT("Black"));
		}
	}
	else
	{
		const FPainter B{Design, Elements, Layer + 16, BarIn};
		const FPainter BKeys{Design, Elements, Layer + 17, BarIn};
		const FPainter KeyText{Design, Elements, Layer + 18, BarIn};
		RoundBox(B, 56.0f, BarTop, 1488.0f, 64.0f, 26.0f, FLinearColor(0.012f, 0.02f, 0.048f, 0.92f));
		RoundFrame(B, 56.0f, BarTop, 1488.0f, 64.0f, 26.0f, FLinearColor(0.14f, 0.3f, 0.62f, 0.45f), 2.0f);
		struct FGuide { const TCHAR* Key; const TCHAR* Label; };
		static const FGuide Guides[] = {{TEXT("十字 / WASD"), TEXT("いどう")}, {TEXT("A / Enter"), TEXT("けってい")},
			{TEXT("B / Esc"), TEXT("もどる")}, {TEXT("Y / N"), TEXT("ニックネーム")}, {TEXT("← → / LB RB"), TEXT("カラー")}};
		float X = 86.0f;
		for (const FGuide& Guide : Guides)
		{
			if (FCString::Strcmp(Guide.Label,TEXT("カラー")) == 0
				&& !Slots.ContainsByPredicate([](const FSlot& S) { return S.Step == EStep::Colour; })) { continue; }
			float KeyWidth = 22.0f;
			for (const TCHAR* Char = Guide.Key; *Char; ++Char)
			{
				KeyWidth += *Char > 0x2000 ? 22.0f : 12.5f;
			}
			RoundBox(BKeys, X, BarTop + 13.0f, KeyWidth, 38.0f, 11.0f, FLinearColor(0.1f, 0.24f, 0.55f, 0.95f));
			RoundBox(BKeys, X + 2.0f, BarTop + 15.0f, KeyWidth - 4.0f, 34.0f, 9.0f, Paper);
			KeyText.Text(Guide.Key, X + KeyWidth * 0.5f, BarTop + 16.0f, 19.0f, Ink, ETextAlign::Center, TEXT("Black"));
			BKeys.Text(Guide.Label, X + KeyWidth + 10.0f, BarTop + 16.0f, 21.0f, Paper, ETextAlign::Left, TEXT("Black"));
			X += KeyWidth + 132.0f;
		}
	}
	return Layer + 20;
}

void UChaosImpactCharacterSelect::PaintWindow(const int32 Player, const FGeometry& Design, FSlateWindowElementList& Elements,
	const int32 Layer, const double Now) const
{
	const FSlot& Slot = Slots[Player];
	const FBox2D Rect = WindowRect(Player);
	const float W = static_cast<float>(Rect.GetSize().X);
	const float H = static_cast<float>(Rect.GetSize().Y);
	const float Since = static_cast<float>(Now - OpenedAt) - 0.15f - Player * 0.06f;
	const float In = EaseOut(Since / 0.35f);
	const FLinearColor Accent = SlotAccent(Player);
	const FLinearColor Swatch = ChaosImpactRoster::GetColourSwatch(Slot.Colour);
	const bool bDone = Slot.Step == EStep::Done;
	const float StepAge = static_cast<float>(Now - Slot.StepAt);
	const FGeometry Space = MakeSkewed(Design, static_cast<float>(Rect.Min.X) + (1.0f - In) * 80.0f,
		static_cast<float>(Rect.Min.Y), W, H, 0.0f);
	const FPainter C{Space, Elements, Layer + 2, In};
	const FPainter Back{Space, Elements, Layer, In};
	const FPainter Photo{Space, Elements, Layer + 1, In};
	const bool bOnLockedTile = Slot.Step == EStep::Character && !IsTileUnlocked(Slot.Cursor);

	// Window: dark, rounded, lit in the player's colour once they are done.
	RoundBox(Back, 0.0f, 0.0f, W, H, 30.0f, FLinearColor(0.012f, 0.02f, 0.048f, 0.94f));
	RoundBox(Back, 0.0f, 0.0f, W, H, 30.0f, WithAlpha(Swatch, bDone ? 0.22f : 0.08f));
	RoundBox(Back, 8.0f, 8.0f, W - 16.0f, 52.0f, 20.0f, WithAlpha(Accent, 0.16f));
	RoundFrame(Back, 0.0f, 0.0f, W, H, 30.0f, WithAlpha(Accent, bDone ? 0.95f : 0.38f), bDone ? 5.5f : 2.5f);

	const bool bWide = W > H * 1.3f;
	RoundBox(C, 14.0f, 12.0f, 70.0f, 38.0f, 12.0f, Accent);
	C.Text(FString::Printf(TEXT("%dP"), Player + 1), 49.0f, 10.0f, 26.0f, Ink, ETextAlign::Center, TEXT("BlackItalic"));
	{
		const FBox2D Tag = NameTagRect(Player);
		const float TagW = static_cast<float>(Tag.GetSize().X);
		const bool bGuest = Slot.Profile <= 0;
		const FPainter TagText{Space, Elements, Layer + 3, In};
		RoundBox(C, 92.0f, 12.0f, TagW, 38.0f, 12.0f, bGuest ? FLinearColor(0.01f, 0.015f, 0.035f, 0.72f) : WithAlpha(Accent, 0.32f));
		RoundFrame(C, 92.0f, 12.0f, TagW, 38.0f, 12.0f, WithAlpha(Accent, bGuest ? 0.25f : 0.6f), 1.5f);
		TagText.Text(ChaosImpactSettings::GetProfile(Slot.Profile).Name, 104.0f, 16.0f, 20.0f, bGuest ? Muted : Paper,
			ETextAlign::Left, TEXT("Black"));
		// Which button opens the list: N on a keyboard, the top face button (Y / △ / X) on a controller.
		const FString Hint = Slot.bKeyboard ? FString(TEXT("N")) : ChaosImpactSettings::GetKeyName(EKeys::Gamepad_FaceButton_Top,
			ChaosImpactSettings::DetectPadDevice(LocalPlayerController(Controller.Get(), Player)));
		RoundBox(TagText, 92.0f + TagW - 34.0f, 18.0f, 26.0f, 26.0f, 8.0f, WithAlpha(Paper, 0.9f));
		const FPainter HintText{Space, Elements, Layer + 4, In};
		HintText.Text(Hint, 92.0f + TagW - 21.0f, 19.0f, 16.0f, Ink, ETextAlign::Center, TEXT("Black"));
	}
	if (bWide || W > 400.0f)
	{
		RoundBox(C, W - 148.0f, 15.0f, 132.0f, 32.0f, 10.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		C.Text(Slot.bKeyboard ? TEXT("キーボード") : TEXT("コントローラー"), W - 82.0f, 19.0f, 15.0f, Muted,
			ETextAlign::Center, TEXT("Bold"));
	}

	// Picture: the character under the cursor idling in this player's colour. Wide windows put it on the left.
	const float AreaTop = 58.0f;
	const bool bTall = !bWide && W > 400.0f;
	// Tall windows keep the bottom for the name, character description, and the hint or colour row.
	const float AreaHeight = H - AreaTop - (bWide ? 14.0f : bTall ? 210.0f : 114.0f);
	float PictureW = AreaHeight * 0.75f;
	float PictureH = AreaHeight;
	float VisibleTop = 0.0f;
	float VisibleHeight = 1.0f;
	if (bWide)
	{
		// Crop to the upper body so the character is not tiny in a short window.
		VisibleHeight = 0.72f;
		const auto ModelKind = ChaosImpactRoster::Get(Slot.Character).Kind;
		VisibleTop = ModelKind == EChaosImpactModelKind::Authored ? 0.14f : 0.02f;
		if (ModelKind == EChaosImpactModelKind::Handstand) { VisibleHeight = 0.94f; VisibleTop = 0.03f; }
		if (Slot.Character == ChaosImpactRoster::BirdIndex) { VisibleHeight = 0.94f; VisibleTop = 0.02f; }
		PictureW = AreaHeight * 0.75f / VisibleHeight;
	}
	else if (PictureW > W - 28.0f)
	{
		PictureW = W - 28.0f;
		PictureH = PictureW / 0.75f;
	}
	const float PictureX = bWide ? 18.0f : (W - PictureW) * 0.5f;
	const float PictureY = AreaTop;
	// Framed 3D viewport card behind the character render
	RoundBox(Back, PictureX - 4.0f, PictureY - 2.0f, PictureW + 8.0f, PictureH + 4.0f, 20.0f, FLinearColor(0.008f, 0.012f, 0.03f, 0.85f));
	RoundFrame(C, PictureX - 4.0f, PictureY - 2.0f, PictureW + 8.0f, PictureH + 4.0f, 20.0f, WithAlpha(Accent, 0.32f), 2.0f);
	if (PictureBrushes.IsValidIndex(Player))
	{
		FSlateBrush& Picture = PictureBrushes[Player];
		Picture.SetUVRegion(FBox2f(FVector2f(0.0f, VisibleTop), FVector2f(1.0f, VisibleTop + VisibleHeight)));
		PaintPicture(Photo, Picture, PictureX, PictureY, PictureW, PictureH,
			bOnLockedTile ? FLinearColor(0.2f, 0.2f, 0.25f) : FLinearColor::White);
	}

	// Name, fighter number banner & character description lines.
	const FChaosImpactCharacterInfo& Info = ChaosImpactRoster::Get(Slot.Character);
	const bool bBigWindow = W > 400.0f;
	const float NameX = bWide ? PictureX + PictureW + 26.0f : W * 0.5f;
	const float NameY = bWide ? 68.0f : bBigWindow ? H - 202.0f : H - 110.0f;
	const ETextAlign NameAlign = bWide ? ETextAlign::Left : ETextAlign::Center;
	const bool bHasDesc1 = !bOnLockedTile && Info.DescriptionLine1 && *Info.DescriptionLine1;
	const bool bHasDesc2 = !bOnLockedTile && Info.DescriptionLine2 && *Info.DescriptionLine2;
	if (bBigWindow || bWide)
	{
		const float BannerX = bWide ? NameX - 10.0f : 28.0f;
		const float BannerW = bWide ? W - BannerX - 18.0f : W - 56.0f;
		const float BannerH = bHasDesc1 ? (bHasDesc2 ? 98.0f : 76.0f) : 50.0f;
		RoundBox(Back, BannerX, NameY - 4.0f, BannerW, BannerH, 14.0f, FLinearColor(0.006f, 0.01f, 0.025f, 0.82f));
		C.Box(BannerX + 10.0f, NameY + 42.0f, BannerW - 20.0f, 2.5f, WithAlpha(Accent, 0.75f));
		C.Text(bOnLockedTile ? FString(TEXT("NO.??")) : FString::Printf(TEXT("FIGHTER NO.%02d"), Slot.Character + 1),
			bWide ? NameX : W * 0.5f, NameY - 19.0f, 12.5f, WithAlpha(Accent, 0.9f), NameAlign, TEXT("BlackItalic"));
	}
	else
	{
		RoundBox(Back, 10.0f, NameY - 2.0f, W - 20.0f, bHasDesc1 ? 70.0f : 32.0f, 10.0f, FLinearColor(0.006f, 0.01f, 0.025f, 0.82f));
	}
	C.Text(bOnLockedTile ? FString(TEXT("？？？")) : FString(Info.Name), NameX, NameY, bWide || bBigWindow ? 32.0f : 20.0f,
		bOnLockedTile ? Muted : Paper, NameAlign, TEXT("Black"), 2.0f, Ink);
	if (bHasDesc1)
	{
		const float DescFontSize = bWide ? 14.5f : bBigWindow ? 15.5f : 11.5f;
		const float DescY1 = NameY + (bWide || bBigWindow ? 48.0f : 26.0f);
		const float DescY2 = DescY1 + (bWide || bBigWindow ? 21.0f : 16.0f);
		const FLinearColor DescCol = FMath::Lerp(Paper, Muted, 0.2f);
		C.Text(Info.DescriptionLine1, NameX, DescY1, DescFontSize, DescCol, NameAlign, TEXT("Bold"), 1.0f, Ink);
		if (bHasDesc2)
		{
			C.Text(Info.DescriptionLine2, NameX, DescY2, DescFontSize, DescCol, NameAlign, TEXT("Bold"), 1.0f, Ink);
		}
	}
	if (!bWide && Slot.Step == EStep::Character)
	{
		// Before a pick the bottom band says how to pick.
		const float HintY = H - (bBigWindow ? 46.0f : 34.0f);
		if (!bOnLockedTile)
		{
			RoundBox(Back, W * 0.5f - 110.0f, HintY - 4.0f, 220.0f, 32.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		}
		C.Text(bOnLockedTile ? FString() : Slot.bKeyboard ? FString(TEXT("Enter でけってい"))
			: FString(TEXT("A でけってい")), W * 0.5f, HintY, 17.0f,
			WithAlpha(Paper, 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 4.0f)), ETextAlign::Center, TEXT("Bold"));
	}
	else if (bWide && Slot.Step == EStep::Character && !bOnLockedTile)
	{
		RoundBox(Back, NameX - 6.0f, H - 50.0f, 210.0f, 34.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		C.Text(Slot.bKeyboard ? TEXT("Enter でけってい") : TEXT("A でけってい"), NameX + 99.0f, H - 46.0f, 17.0f,
			WithAlpha(Paper, 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 4.0f)), ETextAlign::Center, TEXT("Bold"));
	}

	// Colour row: opens when the character is chosen; the chosen colour is ringed, colours in use are crossed out.
	if (Slot.Step != EStep::Character && Info.bColourVariants)
	{
		const float Open = EaseOut(StepAge / 0.2f);
		const float Changed = static_cast<float>(Now - Slot.ChangedAt);
		const int32 Count = ChaosImpactRoster::ColourCount;
		const float Spacing = FMath::Min(62.0f, (W - 120.0f) / Count);
		const float RowWidth = Spacing * Count + 70.0f;
		const float RowX = bWide ? NameX - 8.0f : (W - RowWidth) * 0.5f;
		const float RowY = bWide ? H - 102.0f : bBigWindow ? H - 102.0f : H - 96.0f;
		const FPainter Row{Space, Elements, Layer + 3, In * (bDone ? 1.0f : Open)};
		// The colour's name sits inside the row, above the colours.
		RoundBox(Row, RowX, RowY, RowWidth, 94.0f, 22.0f, FLinearColor(0.005f, 0.01f, 0.025f, 0.85f));
		RoundFrame(Row, RowX, RowY, RowWidth, 94.0f, 22.0f, WithAlpha(Swatch, 0.55f), 2.0f);
		Row.Text(ChaosImpactRoster::ColourNames[Slot.Colour], RowX + RowWidth * 0.5f, RowY + 4.0f, 18.0f, Swatch,
			ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
		if (!bDone)
		{
			const float Nudge = Changed < 0.15f ? 5.0f * (1.0f - Changed / 0.15f) : 0.0f;
			Row.Text(TEXT("<"), RowX + 16.0f - (Slot.LastDirection < 0 ? Nudge : 0.0f), RowY + 38.0f, 30.0f, Paper,
				ETextAlign::Center, TEXT("Black"));
			Row.Text(TEXT(">"), RowX + RowWidth - 16.0f + (Slot.LastDirection > 0 ? Nudge : 0.0f), RowY + 38.0f, 30.0f, Paper,
				ETextAlign::Center, TEXT("Black"));
		}
		for (int32 Colour = 0; Colour < Count; ++Colour)
		{
			const FVector2D Centre(RowX + 35.0f + Spacing * (Colour + 0.5f), RowY + 62.0f);
			const bool bChosen = Colour == Slot.Colour;
			const bool bTaken = !bChosen && IsColourTaken(Player, Slot.Character, Colour);
			const float Pop = bChosen && Changed < 0.22f ? 1.0f + 0.35f * (1.0f - Changed / 0.22f) : 1.0f;
			Row.Disc(Centre, (bChosen ? 19.0f : 14.0f) * Pop, WithAlpha(ChaosImpactRoster::GetColourSwatch(Colour), bTaken ? 0.25f : 1.0f));
			if (bChosen)
			{
				Row.Ring(Centre, 25.0f * Pop, Paper, 3.5f);
			}
			if (bTaken)
			{
				Row.Line(Centre + FVector2D(-9.0f, -9.0f), Centre + FVector2D(9.0f, 9.0f), WithAlpha(Paper, 0.7f), 3.0f);
			}
		}
	}

	// Done: an OK! lands on the window and its frame lights up.
	if (bDone)
	{
		RoundFrame(C, 0.0f, 0.0f, W, H, 30.0f, Accent, 6.0f);
		const float Stamp = 1.0f + 0.45f * FMath::Exp(-16.0f * StepAge);
		const float StampX = bWide ? W - 230.0f : W * 0.5f - 110.0f;
		const float StampY = bWide ? 36.0f : PictureY + PictureH * 0.30f;
		const FGeometry StampSpace = MakeSkewed(Space, StampX, StampY, 220.0f, 100.0f, -0.18f, Stamp);
		const FPainter S{StampSpace, Elements, Layer + 4, In * FMath::Clamp(StepAge / 0.06f, 0.0f, 1.0f)};
		RoundBox(S, -4.0f, 10.0f, 228.0f, 80.0f, 24.0f, WithAlpha(Ink, 0.85f));
		RoundBox(S, 0.0f, 14.0f, 220.0f, 72.0f, 22.0f, Accent);
		RoundFrame(S, 0.0f, 14.0f, 220.0f, 72.0f, 22.0f, Paper, 3.0f);
		S.Text(TEXT("OK!"), 110.0f, 2.0f, 74.0f, Paper, ETextAlign::Center, TEXT("BlackItalic"), 5.0f, Ink);
	}
}

FBox2D UChaosImpactCharacterSelect::NameTagRect(const int32 Player) const
{
	if (!Slots.IsValidIndex(Player))
	{
		return FBox2D(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}
	const FBox2D Window = WindowRect(Player);
	const FString& Name = ChaosImpactSettings::GetProfile(Slots[Player].Profile).Name;
	// About a letter's width each, room for the button, and never over the window's right edge.
	const float Width = FMath::Min(30.0f + Name.Len() * 21.0f + 42.0f, static_cast<float>(Window.GetSize().X) - 110.0f);
	const FVector2D Origin = Window.Min + FVector2D(92.0f, 12.0f);
	return FBox2D(Origin, Origin + FVector2D(Width, 38.0f));
}

int32 UChaosImpactCharacterSelect::NamePickerLines() const
{
	const int32 Count = ChaosImpactSettings::GetProfileCount();
	return Count + (Count < ChaosImpactSettings::MaxProfiles ? 1 : 0);
}

FBox2D UChaosImpactCharacterSelect::PickerLineRect(const int32 Line) const
{
	const float Top = 450.0f - NamePickerLines() * 23.0f + 60.0f;
	const FVector2D Origin(560.0f, Top + Line * 46.0f);
	return FBox2D(Origin, Origin + FVector2D(480.0f, 40.0f));
}

void UChaosImpactCharacterSelect::OpenNamePicker(const int32 Player)
{
	if (!bOpen || !Slots.IsValidIndex(Player) || NameEntry.IsOpen())
	{
		return;
	}
	NamePickerOwner = Player;
	NamePickerChoice = FMath::Clamp(Slots[Player].Profile, 0, NamePickerLines() - 1);
	NamePickerAt = FPlatformTime::Seconds();
}

void UChaosImpactCharacterSelect::PickName(const int32 Choice)
{
	const int32 Player = NamePickerOwner;
	if (!Slots.IsValidIndex(Player))
	{
		NamePickerOwner = INDEX_NONE;
		return;
	}
	if (Choice >= ChaosImpactSettings::GetProfileCount())
	{
		NameEntry.Open(FString(), FString::Printf(TEXT("%dP のニックネーム"), Player + 1), Player);
		NamePickerOwner = INDEX_NONE;
		return;
	}
	SetProfile(Player, FMath::Max(Choice, 0));
	NamePickerOwner = INDEX_NONE;
}

void UChaosImpactCharacterSelect::SetProfile(const int32 Player, const int32 Profile)
{
	if (!Slots.IsValidIndex(Player))
	{
		return;
	}
	Slots[Player].Profile = Profile;
	if (UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(Controller.Get()))
	{
		Loadouts->SetNickname(Player, Profile > 0 ? ChaosImpactSettings::GetProfile(Profile).Name : FString());
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Character select: P%d plays as %s"), Player + 1, *ChaosImpactSettings::GetProfile(Profile).Name);
}

void UChaosImpactCharacterSelect::FinishNameEntry()
{
	FString Name;
	const FChaosImpactNameEntry::EResult Result = NameEntry.ConsumeResult(Name);
	if (Result == FChaosImpactNameEntry::EResult::Done)
	{
		// A name already made is simply picked.
		const int32 Profile = ChaosImpactSettings::AddProfile(Name);
		if (Profile != INDEX_NONE)
		{
			SetProfile(NameEntry.GetOwner(), Profile);
		}
	}
	else if (Result == FChaosImpactNameEntry::EResult::Cancelled)
	{
		OpenNamePicker(NameEntry.GetOwner());
	}
}

bool UChaosImpactCharacterSelect::HandleCharacter(const TCHAR Character)
{
	// Only the keyboard player's own name takes typed letters.
	const AChaosImpactPlayerController* Owner = Controller.Get();
	return bOpen && NameEntry.IsOpen() && Owner && Owner->GetLocalPlayerIndexForDevice(true, 0) == NameEntry.GetOwner()
		&& NameEntry.HandleCharacter(Character);
}

void UChaosImpactCharacterSelect::HandleMouseMove(const FVector2D& DesignPoint)
{
	if (!bOpen)
	{
		return;
	}
	if (NameEntry.IsOpen())
	{
		NameEntry.HandleMouseMove(DesignPoint);
		return;
	}
	for (int32 Line = 0; Line < NamePickerLines() && NamePickerOwner != INDEX_NONE; ++Line)
	{
		if (PickerLineRect(Line).IsInside(DesignPoint))
		{
			NamePickerChoice = Line;
		}
	}
}

void UChaosImpactCharacterSelect::PaintNamePicker(const FGeometry& Design, FSlateWindowElementList& Elements,
	const int32 Layer, const double Now) const
{
	if (NamePickerOwner == INDEX_NONE)
	{
		return;
	}
	const float In = EaseOut(static_cast<float>(Now - NamePickerAt) / 0.2f);
	const FPainter Shade{Design, Elements, Layer, In};
	Shade.Box(-400.0f, -200.0f, 2400.0f, 1300.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	const int32 Lines = NamePickerLines();
	const FBox2D First = PickerLineRect(0);
	const float Top = static_cast<float>(First.Min.Y) - 80.0f;
	const float Height = Lines * 46.0f + 130.0f;
	const FPainter Panel{Design, Elements, Layer + 1, In};
	RoundBox(Panel, 520.0f, Top, 560.0f, Height, 30.0f, FLinearColor(0.02f, 0.03f, 0.07f, 0.98f));
	RoundBox(Panel, 520.0f, Top, 560.0f, 8.0f, 4.0f, SlotAccent(NamePickerOwner));
	const FPainter Words{Design, Elements, Layer + 3, In};
	Words.Text(FString::Printf(TEXT("%dP のニックネーム"), NamePickerOwner + 1), 800.0f, Top + 20.0f, 28.0f, Paper,
		ETextAlign::Center, TEXT("Black"));
	Words.Text(TEXT("そのニックネームの操作設定で遊びます"), 800.0f, Top + Height - 40.0f, 17.0f, Muted, ETextAlign::Center, TEXT("Bold"));
	const FPainter Lights{Design, Elements, Layer + 2, In};
	for (int32 Line = 0; Line < Lines; ++Line)
	{
		const FBox2D Rect = PickerLineRect(Line);
		const bool bChosen = Line == NamePickerChoice;
		const bool bNew = Line >= ChaosImpactSettings::GetProfileCount();
		const bool bCurrent = !bNew && Slots.IsValidIndex(NamePickerOwner) && Slots[NamePickerOwner].Profile == Line;
		RoundBox(Lights, static_cast<float>(Rect.Min.X), static_cast<float>(Rect.Min.Y), 480.0f, 40.0f, 12.0f,
			bChosen ? FLinearColor(0.08f, 0.2f, 0.42f, 1.0f) : FLinearColor(0.035f, 0.05f, 0.09f, 1.0f));
		Words.Text(bNew ? FString(TEXT("＋ 新しく作る")) : ChaosImpactSettings::GetProfile(Line).Name,
			static_cast<float>(Rect.Min.X) + 24.0f, static_cast<float>(Rect.Min.Y) + 6.0f, 22.0f,
			bNew ? Gold : Paper, ETextAlign::Left, TEXT("Black"));
		if (bCurrent)
		{
			Words.Text(TEXT("いまの"), static_cast<float>(Rect.Max.X) - 20.0f, static_cast<float>(Rect.Min.Y) + 9.0f, 17.0f,
				SlotAccent(NamePickerOwner), ETextAlign::Right, TEXT("Bold"));
		}
	}
}
