#include "ChaosImpactCharacterSelect.h"

#include "ChaosImpact.h"
#include "ChaosImpactCharacterPreview.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
	using namespace ChaosImpactPaint;
	using EStep = UChaosImpactCharacterSelect::EStep;

	/** The same accents the controller assignment screen gives P1-P4. */
	FLinearColor SlotAccent(const int32 Player)
	{
		return PlayerAccents[FMath::Clamp(Player, 0, 3)];
	}

	/** Seconds between "start" and the next screen, so the GO! can land. */
	constexpr double StartDelay = 0.55;

	// Layout in the 1600 x 900 design space: icons on the left, player windows on the right, guide bar below.
	constexpr float IconSize = 192.0f;
	constexpr float IconGap = 12.0f;
	constexpr float GridLeft = 76.0f;
	constexpr float GridTop = 180.0f;
	constexpr float PanelTop = 140.0f;
	constexpr float PanelBottom = 790.0f;
	constexpr float WindowsLeft = 930.0f;
	constexpr float WindowsWidth = 614.0f;
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
	const float Height = PanelBottom - PanelTop;
	if (Slots.Num() <= 1)
	{
		return FBox2D(FVector2D(WindowsLeft, PanelTop), FVector2D(WindowsLeft + WindowsWidth, PanelBottom));
	}
	if (Slots.Num() == 2)
	{
		const float Each = (Height - WindowGap) * 0.5f;
		const float Top = PanelTop + Player * (Each + WindowGap);
		return FBox2D(FVector2D(WindowsLeft, Top), FVector2D(WindowsLeft + WindowsWidth, Top + Each));
	}
	const float EachWidth = (WindowsWidth - WindowGap) * 0.5f;
	const float EachHeight = (Height - WindowGap) * 0.5f;
	const float Left = WindowsLeft + (Player % 2) * (EachWidth + WindowGap);
	const float Top = PanelTop + (Player / 2) * (EachHeight + WindowGap);
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

	// Icon faces: every roster character in its first colour, filmed far away from the level.
	for (int32 Character = 0; Character < ChaosImpactRoster::Num(); ++Character)
	{
		AChaosImpactCharacterPreview* Portrait = SpawnStage(FVector(Character * 1600.0f, -96000.0f, 40000.0f));
		if (Portrait)
		{
			Portrait->ShowLoadout(Character, 0, Paper);
		}
		Portraits.Add(Portrait);
		PortraitBrushes.Add(MakePictureBrush(Portrait));
	}

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
		AChaosImpactCharacterPreview* Stage = SpawnStage(FVector(Player * 1600.0f, -90000.0f, 40000.0f));
		Previews.Add(Stage);
		PictureBrushes.Add(MakePictureBrush(Stage));
		RefreshPreview(Player);
	}
	OpenedAt = FPlatformTime::Seconds();
	AllReadyAt = -100.0;
	bStarting = false;
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
			return;
		}
		Slot.Character = Slot.Cursor;
		SetStep(Player, EStep::Colour);
		// The row opens on a colour nobody else has on this character.
		Slot.Colour = FindFreeColour(Player, Slot.Character, Slot.Colour, 1);
		Slot.ChangedAt = Slot.StepAt;
		RefreshPreview(Player);
		Commit(Player);
	}
	else if (Slot.Step == EStep::Colour)
	{
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
	if (Slot.Step == EStep::Done)
	{
		SetStep(Player, EStep::Colour);
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
			}
		}
		for (AChaosImpactCharacterPreview* Stage : Portraits)
		{
			if (IsValid(Stage))
			{
				Stage->Animate(Step);
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
	if (IsAny(Key, {EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A}))
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
	else if (!bRepeat && IsAny(Key, {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_Special_Right, EKeys::Enter,
		EKeys::SpaceBar}))
	{
		PressConfirm(Player);
	}
	else if (!bRepeat && IsAny(Key, {EKeys::Gamepad_FaceButton_Right, EKeys::Escape, EKeys::BackSpace}))
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

	// ---- Heading ------------------------------------------------------------------------------
	{
		const float In = EaseOut(T / 0.35f);
		const FPainter Title{Design, Elements, Layer + 1, In};
		Title.Text(TEXT("CHARACTER SELECT"), 64.0f - (1.0f - In) * 60.0f, 24.0f, 44.0f, Paper, ETextAlign::Left,
			TEXT("BlackItalic"), 3.0f, Ink);
		Title.Text(TEXT("キャラクターをえらんでね"), 68.0f, 82.0f, 20.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		if (Owner && Owner->GetPlayFlow() != EChaosImpactPlayFlow::Training)
		{
			Title.Text(TEXT("チーム戦ではチームのカラーになります"), 1544.0f, 92.0f, 16.0f, WithAlpha(Muted, 0.9f),
				ETextAlign::Right, TEXT("Regular"));
		}
	}

	// ---- Icon grid ----------------------------------------------------------------------------
	{
		const float In = EaseOut((T - 0.05f) / 0.3f);
		const FPainter Panel{Design, Elements, Layer + 1, In};
		RoundBox(Panel, 56.0f, PanelTop, 850.0f, PanelBottom - PanelTop, 30.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.88f));
	}
	for (int32 Tile = 0; Tile < GetTileCount(); ++Tile)
	{
		const float In = EaseOut((T - 0.1f - Tile * 0.025f) / 0.28f);
		const FVector2D Origin = TileOrigin(Tile);
		const bool bUnlocked = IsTileUnlocked(Tile);
		TArray<int32, TInlineAllocator<4>> Here;
		for (int32 Player = 0; Player < Slots.Num(); ++Player)
		{
			if (Slots[Player].Cursor == Tile)
			{
				Here.Add(Player);
			}
		}
		const float Pop = Here.IsEmpty() ? 1.0f : 1.05f;
		const FGeometry IconSpace = MakeSkewed(Design, Origin.X, Origin.Y + (1.0f - In) * 40.0f, IconSize, IconSize, 0.0f, Pop * In);
		const int32 IconLayer = Layer + 2 + (Here.IsEmpty() ? 0 : 3);
		const FPainter I{IconSpace, Elements, IconLayer, In};
		const FPainter IFace{IconSpace, Elements, IconLayer + 1, In};
		const FPainter ILabel{IconSpace, Elements, IconLayer + 2, In};
		if (bUnlocked)
		{
			RoundBox(I, 0.0f, 0.0f, IconSize, IconSize, 22.0f, FLinearColor(0.08f, 0.2f, 0.52f, 1.0f));
			RoundBox(I, 0.0f, IconSize * 0.5f, IconSize, IconSize * 0.5f, 22.0f, FLinearColor(0.03f, 0.1f, 0.3f, 1.0f));
			if (PortraitBrushes.IsValidIndex(Tile))
			{
				FSlateBrush& Face = PortraitBrushes[Tile];
				Face.SetUVRegion(FBox2f(FVector2f(0.2f, 0.04f), FVector2f(0.8f, 0.5f)));
				PaintPicture(IFace, Face, 16.0f, 8.0f, IconSize - 32.0f, IconSize - 50.0f);
			}
			if (*ChaosImpactRoster::Get(Tile).Name)
			{
				RoundBox(ILabel, 10.0f, IconSize - 44.0f, IconSize - 20.0f, 36.0f, 12.0f, WithAlpha(Ink, 0.85f));
				ILabel.Text(ChaosImpactRoster::Get(Tile).Name, IconSize * 0.5f, IconSize - 44.0f, 21.0f, Paper,
					ETextAlign::Center, TEXT("Black"));
			}
		}
		else
		{
			RoundBox(I, 0.0f, 0.0f, IconSize, IconSize, 22.0f, FLinearColor(0.05f, 0.06f, 0.09f, 0.95f));
			I.Text(TEXT("?"), IconSize * 0.5f, 26.0f, 76.0f, WithAlpha(Muted, 0.4f), ETextAlign::Center, TEXT("Black"));
			I.Text(TEXT("COMING SOON"), IconSize * 0.5f, IconSize - 38.0f, 14.0f, WithAlpha(Muted, 0.6f),
				ETextAlign::Center, TEXT("Bold"));
		}

		// Cursors: a thick rounded frame per player, the later ones outside the earlier, and a name tag.
		const FPainter F{IconSpace, Elements, IconLayer + 3, In};
		const FPainter Tags{IconSpace, Elements, IconLayer + 4, In};
		const FPainter TagText{IconSpace, Elements, IconLayer + 5, In};
		const float TagY = -(6.0f + (Here.Num() - 1) * 9.0f) - 38.0f;
		for (int32 Order = 0; Order < Here.Num(); ++Order)
		{
			const int32 Player = Here[Order];
			const float Moved = static_cast<float>(Now - Slots[Player].MovedAt);
			const float Grow = Moved < 0.15f ? 8.0f * (1.0f - Moved / 0.15f) : 0.0f;
			const float Out = 6.0f + Order * 9.0f + Grow;
			RoundFrame(F, -Out, -Out, IconSize + Out * 2.0f, IconSize + Out * 2.0f, 26.0f + Out, SlotAccent(Player), 6.0f);
		}
		for (int32 Order = 0; Order < Here.Num(); ++Order)
		{
			const int32 Player = Here[Order];
			const FSlot& Slot = Slots[Player];
			const bool bChosen = Slot.Step != EStep::Character;
			const float Bob = bChosen ? 0.0f : 3.0f * FMath::Sin(static_cast<float>(Now) * 6.0f + Player);
			const float TagX = -8.0f + Order * 60.0f;
			// A chosen player's tag turns white with their colour on it.
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

	// ---- Guide bar / start --------------------------------------------------------------------
	const float BarIn = EaseOut((T - 0.25f) / 0.3f);
	if (AreAllReady() || bStarting)
	{
		const float Since = static_cast<float>(Now - AllReadyAt);
		const float In = EaseOut(Since / 0.25f);
		const FGeometry Bar = MakeSkewed(Design, 56.0f, BarTop + (1.0f - In) * 60.0f, 1488.0f, 78.0f, 0.0f, 0.9f + 0.1f * In);
		const FPainter B{Bar, Elements, Layer + 16, In};
		RoundBox(B, 0.0f, 0.0f, 1488.0f, 78.0f, 30.0f, Gold);
		const float Pulse = 0.7f + 0.3f * FMath::Sin(static_cast<float>(Now) * 6.0f);
		if (bStarting)
		{
			const float Go = static_cast<float>(Now - (StartAt - StartDelay));
			RoundBox(B, 0.0f, 0.0f, 1488.0f, 78.0f, 30.0f, WithAlpha(Paper, 0.7f * FMath::Clamp(1.0f - Go / 0.25f, 0.0f, 1.0f)));
			B.Text(TEXT("GO!"), 744.0f, -6.0f, 60.0f * (1.0f + 0.3f * FMath::Exp(-10.0f * Go)), Ink, ETextAlign::Center,
				TEXT("BlackItalic"));
		}
		else
		{
			B.Text(TEXT("READY!"), 40.0f, -2.0f, 56.0f, Ink, ETextAlign::Left, TEXT("BlackItalic"));
			B.Text(Owner && Owner->IsKeyboardMouseJoined() ? TEXT("A / Enter でスタート") : TEXT("Aボタンでスタート"),
				1450.0f, 18.0f, 32.0f, WithAlpha(Ink, Pulse), ETextAlign::Right, TEXT("Black"));
		}
	}
	else
	{
		const FPainter B{Design, Elements, Layer + 16, BarIn};
		const FPainter BKeys{Design, Elements, Layer + 17, BarIn};
		const FPainter KeyText{Design, Elements, Layer + 18, BarIn};
		RoundBox(B, 56.0f, BarTop, 1488.0f, 64.0f, 30.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.88f));
		struct FGuide { const TCHAR* Key; const TCHAR* Label; };
		static const FGuide Guides[] = {{TEXT("十字 / WASD"), TEXT("いどう")}, {TEXT("A / Enter"), TEXT("けってい")},
			{TEXT("B / Esc"), TEXT("もどる")}, {TEXT("← → / LB RB"), TEXT("カラー")}};
		float X = 90.0f;
		for (const FGuide& Guide : Guides)
		{
			float KeyWidth = 22.0f;
			for (const TCHAR* Char = Guide.Key; *Char; ++Char)
			{
				KeyWidth += *Char > 0x2000 ? 22.0f : 12.5f;
			}
			RoundBox(BKeys, X, BarTop + 14.0f, KeyWidth, 36.0f, 12.0f, Paper);
			KeyText.Text(Guide.Key, X + KeyWidth * 0.5f, BarTop + 15.0f, 20.0f, Ink, ETextAlign::Center, TEXT("Black"));
			BKeys.Text(Guide.Label, X + KeyWidth + 12.0f, BarTop + 15.0f, 22.0f, Paper, ETextAlign::Left, TEXT("Black"));
			X += KeyWidth + 150.0f;
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
	RoundBox(Back, 0.0f, 0.0f, W, H, 30.0f, FLinearColor(0.02f, 0.03f, 0.06f, 0.92f));
	RoundBox(Back, 0.0f, 0.0f, W, H, 30.0f, WithAlpha(Swatch, bDone ? 0.18f : 0.06f));
	const bool bWide = W > H * 1.3f;
	RoundBox(C, 14.0f, 12.0f, 70.0f, 38.0f, 12.0f, Accent);
	C.Text(FString::Printf(TEXT("%dP"), Player + 1), 49.0f, 10.0f, 26.0f, Ink, ETextAlign::Center, TEXT("BlackItalic"));
	if (bWide || W > 400.0f)
	{
		C.Text(Slot.bKeyboard ? TEXT("キーボード") : TEXT("コントローラー"), W - 22.0f, 20.0f, 16.0f, Muted,
			ETextAlign::Right, TEXT("Bold"));
	}

	// Picture: the character under the cursor idling in this player's colour. Wide windows put it on the left.
	const float AreaTop = 58.0f;
	const float AreaHeight = H - AreaTop - (bWide ? 14.0f : 92.0f);
	float PictureW = AreaHeight * 0.75f;
	float PictureH = AreaHeight;
	float VisibleTop = 0.0f;
	float VisibleHeight = 1.0f;
	if (bWide)
	{
		// Crop to the upper body so the character is not tiny in a short window.
		VisibleHeight = 0.72f;
		VisibleTop = 0.02f;
		PictureW = AreaHeight * 0.75f / VisibleHeight;
	}
	else if (PictureW > W - 28.0f)
	{
		PictureW = W - 28.0f;
		PictureH = PictureW / 0.75f;
	}
	const float PictureX = bWide ? 18.0f : (W - PictureW) * 0.5f;
	const float PictureY = AreaTop;
	if (PictureBrushes.IsValidIndex(Player))
	{
		FSlateBrush& Picture = PictureBrushes[Player];
		Picture.SetUVRegion(FBox2f(FVector2f(0.0f, VisibleTop), FVector2f(1.0f, VisibleTop + VisibleHeight)));
		PaintPicture(Photo, Picture, PictureX, PictureY, PictureW, PictureH,
			bOnLockedTile ? FLinearColor(0.2f, 0.2f, 0.25f) : FLinearColor::White);
	}

	// Name.
	const FChaosImpactCharacterInfo& Info = ChaosImpactRoster::Get(Slot.Character);
	const bool bBigWindow = W > 400.0f;
	const float NameX = bWide ? PictureX + PictureW + 26.0f : bBigWindow ? W * 0.5f : W - 18.0f;
	const float NameY = bWide ? 70.0f : bBigWindow ? H - 74.0f : 14.0f;
	const ETextAlign NameAlign = bWide ? ETextAlign::Left : bBigWindow ? ETextAlign::Center : ETextAlign::Right;
	C.Text(bOnLockedTile ? FString(TEXT("？？？")) : FString(Info.Name), NameX, NameY, bWide || bBigWindow ? 34.0f : 24.0f,
		bOnLockedTile ? Muted : Paper, NameAlign, TEXT("Black"), 2.0f, Ink);
	if (!bWide && Slot.Step == EStep::Character)
	{
		// Before a pick the bottom band says how to pick.
		C.Text(bOnLockedTile ? FString(TEXT("COMING SOON")) : Slot.bKeyboard ? FString(TEXT("Enter でけってい"))
			: FString(TEXT("A でけってい")), W * 0.5f, H - (bBigWindow ? 40.0f : 58.0f), 18.0f,
			WithAlpha(Paper, 0.75f + 0.25f * FMath::Sin(static_cast<float>(Now) * 4.0f)), ETextAlign::Center, TEXT("Bold"));
	}
	if (bWide && bOnLockedTile)
	{
		C.Text(TEXT("COMING SOON"), NameX + 2.0f, NameY + 46.0f, 15.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	}

	// Colour row: opens when the character is chosen; the chosen colour is ringed, colours in use are crossed out.
	if (Slot.Step != EStep::Character)
	{
		const float Open = EaseOut(StepAge / 0.2f);
		const float Changed = static_cast<float>(Now - Slot.ChangedAt);
		const int32 Count = ChaosImpactRoster::ColourCount;
		const float Spacing = FMath::Min(62.0f, (W - 120.0f) / Count);
		const float RowWidth = Spacing * Count + 70.0f;
		const float RowX = bWide ? NameX - 8.0f : (W - RowWidth) * 0.5f;
		const float RowY = bWide ? H - 104.0f : bBigWindow ? H - 178.0f : H - 88.0f;
		const FPainter Row{Space, Elements, Layer + 3, In * (bDone ? 1.0f : Open)};
		RoundBox(Row, RowX, RowY, RowWidth, 84.0f, 22.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
		Row.Text(ChaosImpactRoster::ColourNames[Slot.Colour], RowX + RowWidth * 0.5f, RowY - 2.0f, 18.0f, Swatch,
			ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
		if (!bDone)
		{
			const float Nudge = Changed < 0.15f ? 5.0f * (1.0f - Changed / 0.15f) : 0.0f;
			Row.Text(TEXT("<"), RowX + 16.0f - (Slot.LastDirection < 0 ? Nudge : 0.0f), RowY + 26.0f, 30.0f, Paper,
				ETextAlign::Center, TEXT("Black"));
			Row.Text(TEXT(">"), RowX + RowWidth - 16.0f + (Slot.LastDirection > 0 ? Nudge : 0.0f), RowY + 26.0f, 30.0f, Paper,
				ETextAlign::Center, TEXT("Black"));
		}
		for (int32 Colour = 0; Colour < Count; ++Colour)
		{
			const FVector2D Centre(RowX + 35.0f + Spacing * (Colour + 0.5f), RowY + 50.0f);
			const bool bChosen = Colour == Slot.Colour;
			const bool bTaken = !bChosen && IsColourTaken(Player, Slot.Character, Colour);
			const float Pop = bChosen && Changed < 0.22f ? 1.0f + 0.35f * (1.0f - Changed / 0.22f) : 1.0f;
			Row.Disc(Centre, (bChosen ? 19.0f : 14.0f) * Pop, WithAlpha(ChaosImpactRoster::GetColourSwatch(Colour), bTaken ? 0.25f : 1.0f));
			if (bChosen)
			{
				Row.Ring(Centre, 25.0f * Pop, Paper, 3.0f);
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
		const float StampY = bWide ? 30.0f : PictureY + PictureH * 0.32f;
		const FGeometry StampSpace = MakeSkewed(Space, StampX, StampY, 220.0f, 100.0f, -0.18f, Stamp);
		const FPainter S{StampSpace, Elements, Layer + 4, In * FMath::Clamp(StepAge / 0.06f, 0.0f, 1.0f)};
		RoundBox(S, 0.0f, 14.0f, 220.0f, 72.0f, 22.0f, Accent);
		S.Text(TEXT("OK!"), 110.0f, 2.0f, 74.0f, Paper, ETextAlign::Center, TEXT("BlackItalic"), 5.0f, Ink);
	}
}
