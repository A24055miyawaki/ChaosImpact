// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactSessionSubsystem.h"
#include "GameFramework/PlayerState.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// Bottom-right ball inventory canvas, in widget units. Shared by the UMG layout and
	// the painted pickup ripple so both stay aligned.
	const FVector2D InventoryOffset(-24.0f, -22.0f);
	const FVector2D InventorySize(240.0f, 128.0f);
	const FVector2D BallSlotPositions[] = {FVector2D(17.0f, 30.0f), FVector2D(94.0f, 7.0f)};
	const float BallSlotSize = 86.0f;
	const float BallPickupSeconds = 0.32f;
}

void UChaosImpactChargeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	ForceVolatile(true);

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChargeRoot"));
	WidgetTree->RootWidget = RootCanvas;

	PersonalAimGuideBars.Reserve(3);
	for (int32 BarIndex = 0; BarIndex < 3; ++BarIndex)
	{
		UBorder* Bar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			*FString::Printf(TEXT("PersonalAimGuide_%d"), BarIndex));
		Bar->SetBrushColor(FLinearColor(0.12f, 0.93f, 1.0f, 0.94f));
		Bar->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
		Bar->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* BarSlot = RootCanvas->AddChildToCanvas(Bar);
		BarSlot->SetPosition(FVector2D::ZeroVector);
		BarSlot->SetSize(FVector2D(1.0f, BarIndex == 0 ? 6.0f : 7.0f));
		BarSlot->SetZOrder(40);
		PersonalAimGuideBars.Add(Bar);
	}

	// A strong dark seam keeps adjacent cameras readable, while the narrow blue
	// highlight gives the divider a deliberate in-game finish.
	auto AddDivider = [this](const FName Name, const bool bVertical,
		const float Thickness, const FLinearColor& Color) -> UBorder*
	{
		UBorder* Divider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Divider->SetBrushColor(Color);
		Divider->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* DividerSlot = RootCanvas->AddChildToCanvas(Divider);
		DividerSlot->SetZOrder(1000);
		if (bVertical)
		{
			DividerSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
			DividerSlot->SetAlignment(FVector2D(1.0f, 0.0f));
			DividerSlot->SetPosition(FVector2D::ZeroVector);
			DividerSlot->SetSize(FVector2D(Thickness, 0.0f));
		}
		else
		{
			DividerSlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
			DividerSlot->SetAlignment(FVector2D(0.0f, 1.0f));
			DividerSlot->SetPosition(FVector2D::ZeroVector);
			DividerSlot->SetSize(FVector2D(0.0f, Thickness));
		}
		return Divider;
	};
	VerticalDivider = AddDivider(TEXT("VerticalSplitDivider"), true, 10.0f,
		FLinearColor(0.003f, 0.006f, 0.012f, 0.98f));
	VerticalDividerAccent = AddDivider(TEXT("VerticalSplitAccent"), true, 2.0f,
		FLinearColor(0.0f, 0.64f, 1.0f, 0.9f));
	HorizontalDivider = AddDivider(TEXT("HorizontalSplitDivider"), false, 10.0f,
		FLinearColor(0.003f, 0.006f, 0.012f, 0.98f));
	HorizontalDividerAccent = AddDivider(TEXT("HorizontalSplitAccent"), false, 2.0f,
		FLinearColor(0.0f, 0.64f, 1.0f, 0.9f));

	ChargeFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("GaugeFrame"));
	ChargeFrame->SetBrushColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.86f));
	ChargeFrame->SetPadding(FMargin(7.0f));

	UCanvasPanelSlot* FrameSlot = RootCanvas->AddChildToCanvas(ChargeFrame);
	FrameSlot->SetAnchors(FAnchors(0.5f, 0.84f));
	FrameSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	FrameSlot->SetPosition(FVector2D::ZeroVector);
	FrameSlot->SetSize(FVector2D(420.0f, 48.0f));

	UOverlay* GaugeOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("GaugeOverlay"));
	ChargeFrame->SetContent(GaugeOverlay);

	ChargeBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ChargeBar"));
	ChargeBar->SetPercent(0.0f);
	ChargeBar->SetFillColorAndOpacity(FLinearColor(0.0f, 0.75f, 1.0f, 1.0f));
	if (UOverlaySlot* BarSlot = GaugeOverlay->AddChildToOverlay(ChargeBar))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ChargeLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChargeLabel"));
	ChargeLabel->SetText(FText::FromString(TEXT("CHARGE")));
	ChargeLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ChargeLabel->SetJustification(ETextJustify::Center);
	if (UOverlaySlot* LabelSlot = GaugeOverlay->AddChildToOverlay(ChargeLabel))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Fill);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	ChargeFrame->SetVisibility(ESlateVisibility::Collapsed);

	UBorder* StaminaFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StaminaFrame"));
	StaminaFrame->SetBrushColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.82f));
	StaminaFrame->SetPadding(FMargin(8.0f));

	UCanvasPanelSlot* StaminaFrameSlot = RootCanvas->AddChildToCanvas(StaminaFrame);
	StaminaFrameSlot->SetAnchors(FAnchors(0.0f, 1.0f));
	StaminaFrameSlot->SetAlignment(FVector2D(0.0f, 1.0f));
	StaminaFrameSlot->SetPosition(FVector2D(40.0f, -40.0f));
	StaminaFrameSlot->SetSize(FVector2D(340.0f, 40.0f));

	UHorizontalBox* SegmentRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("StaminaSegments"));
	StaminaFrame->SetContent(SegmentRow);

	StaminaSegments.Reserve(5);
	for (int32 SegmentIndex = 0; SegmentIndex < 5; ++SegmentIndex)
	{
		USizeBox* SegmentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SegmentSize->SetWidthOverride(60.0f);
		SegmentSize->SetHeightOverride(22.0f);

		UProgressBar* Segment = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		Segment->SetPercent(1.0f);
		Segment->SetFillColorAndOpacity(FLinearColor(0.02f, 0.22f, 0.92f, 1.0f));
		SegmentSize->SetContent(Segment);
		StaminaSegments.Add(Segment);

		if (UHorizontalBoxSlot* SegmentSlot = SegmentRow->AddChildToHorizontalBox(SegmentSize))
		{
			SegmentSlot->SetPadding(FMargin(2.0f, 0.0f));
			SegmentSlot->SetHorizontalAlignment(HAlign_Fill);
			SegmentSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	UCanvasPanel* InventoryCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("BallInventoryCanvas"));
	UCanvasPanelSlot* InventorySlot = RootCanvas->AddChildToCanvas(InventoryCanvas);
	InventorySlot->SetAnchors(FAnchors(1.0f, 1.0f));
	InventorySlot->SetAlignment(FVector2D(1.0f, 1.0f));
	InventorySlot->SetPosition(InventoryOffset);
	InventorySlot->SetSize(InventorySize);

	// Two sharp strokes establish direction without putting the HUD in another box.
	for (int32 StrokeIndex = 0; StrokeIndex < 2; ++StrokeIndex)
	{
		UBorder* Stroke = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Stroke->SetBrushColor(StrokeIndex == 0
			? FLinearColor(0.0f, 0.72f, 1.0f, 0.78f)
			: FLinearColor(0.92f, 0.02f, 0.22f, 0.72f));
		Stroke->SetRenderTransformAngle(-13.0f);
		UCanvasPanelSlot* StrokeSlot = InventoryCanvas->AddChildToCanvas(Stroke);
		StrokeSlot->SetPosition(StrokeIndex == 0 ? FVector2D(18.0f, 88.0f) : FVector2D(62.0f, 101.0f));
		StrokeSlot->SetSize(StrokeIndex == 0 ? FVector2D(176.0f, 7.0f) : FVector2D(130.0f, 3.0f));
	}

	BallSlots.Reserve(2);
	BallSlotAccents.Reserve(2);
	BallIcons.Reserve(2);
	for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
	{
		USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SlotSize->SetWidthOverride(BallSlotSize);
		SlotSize->SetHeightOverride(BallSlotSize);
		UCanvasPanelSlot* BallCanvasSlot = InventoryCanvas->AddChildToCanvas(SlotSize);
		BallCanvasSlot->SetPosition(BallSlotPositions[SlotIndex]);
		BallCanvasSlot->SetSize(FVector2D(BallSlotSize));

		UOverlay* SlotOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		SlotSize->SetContent(SlotOverlay);

		UBorder* BallSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		BallSlot->SetPadding(FMargin(0.0f));
		BallSlot->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		if (UOverlaySlot* RingSlot = SlotOverlay->AddChildToOverlay(BallSlot))
		{
			RingSlot->SetHorizontalAlignment(HAlign_Fill);
			RingSlot->SetVerticalAlignment(VAlign_Fill);
		}
		BallSlots.Add(BallSlot);

		UTextBlock* BallIcon = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		BallIcon->SetText(FText::FromString(TEXT("●")));
		BallIcon->SetJustification(ETextJustify::Center);
		BallIcon->SetShadowOffset(FVector2D(4.0f, 5.0f));
		BallIcon->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.88f));
		BallIcon->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		FSlateFontInfo IconFont = BallIcon->GetFont();
		IconFont.Size = 58;
		IconFont.OutlineSettings.OutlineSize = 2;
		IconFont.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.03f, 0.08f, 1.0f);
		BallIcon->SetFont(IconFont);
		if (UOverlaySlot* IconSlot = SlotOverlay->AddChildToOverlay(BallIcon))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}
		BallIcons.Add(BallIcon);

		UTextBlock* Seam = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Seam->SetText(FText::FromString(TEXT("╱")));
		Seam->SetJustification(ETextJustify::Center);
		Seam->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.12f, 0.24f, 0.9f)));
		FSlateFontInfo SeamFont = Seam->GetFont();
		SeamFont.Size = 35;
		Seam->SetFont(SeamFont);
		if (UOverlaySlot* SeamSlot = SlotOverlay->AddChildToOverlay(Seam))
		{
			SeamSlot->SetHorizontalAlignment(HAlign_Fill);
			SeamSlot->SetVerticalAlignment(VAlign_Center);
		}

		UBorder* AccentDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		USizeBox* AccentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		AccentSize->SetWidthOverride(14.0f);
		AccentSize->SetHeightOverride(14.0f);
		AccentSize->SetContent(AccentDot);
		if (UOverlaySlot* AccentSlot = SlotOverlay->AddChildToOverlay(AccentSize))
		{
			AccentSlot->SetHorizontalAlignment(HAlign_Right);
			AccentSlot->SetVerticalAlignment(VAlign_Top);
			AccentSlot->SetPadding(FMargin(0.0f, 4.0f, 4.0f, 0.0f));
		}
		BallSlotAccents.Add(AccentDot);
	}

	InventoryCountLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("BallInventoryCount"));
	InventoryCountLabel->SetText(FText::FromString(TEXT("× 0")));
	InventoryCountLabel->SetJustification(ETextJustify::Center);
	InventoryCountLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.56f, 0.68f, 1.0f)));
	InventoryCountLabel->SetShadowOffset(FVector2D(3.0f, 3.0f));
	InventoryCountLabel->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	InventoryCountLabel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	FSlateFontInfo CountFont = InventoryCountLabel->GetFont();
	CountFont.Size = 27;
	CountFont.OutlineSettings.OutlineSize = 2;
	CountFont.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.03f, 0.08f, 1.0f);
	InventoryCountLabel->SetFont(CountFont);
	UCanvasPanelSlot* CountSlot = InventoryCanvas->AddChildToCanvas(InventoryCountLabel);
	CountSlot->SetPosition(FVector2D(176.0f, 46.0f));
	CountSlot->SetSize(FVector2D(62.0f, 42.0f));

	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshSplitScreenDividers();
}

void UChaosImpactChargeWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPersonalAimGuide();
	RefreshSplitScreenDividers();
	RefreshBallPickupAnimation();

	if (const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		Room && Room->bOnlineRoom)
	{
		const double Now = FPlatformTime::Seconds();
		for (APlayerState* Member : Room->PlayerArray)
		{
			if (Member && !MemberSeenAt.Contains(Member))
			{
				MemberSeenAt.Add(Member, Now);
			}
		}
	}
}

void UChaosImpactChargeWidget::RefreshBallPickupAnimation()
{
	const float Age = static_cast<float>(FPlatformTime::Seconds() - BallGainedAt);
	const float Kick = Age < BallPickupSeconds
		? 1.0f - ChaosImpactPaint::EaseOut(Age / BallPickupSeconds) : 0.0f;
	for (int32 SlotIndex = 0; SlotIndex < BallSlots.Num(); ++SlotIndex)
	{
		const bool bFilled = SlotIndex < CarriedBalls;
		const float Pop = SlotIndex == BallGainedSlot ? 0.22f * Kick : 0.0f;
		if (UBorder* InventorySlot = BallSlots[SlotIndex])
		{
			InventorySlot->SetRenderScale(FVector2D((bFilled ? 1.0f : 0.92f) + Pop));
		}
		if (UTextBlock* Icon = BallIcons.IsValidIndex(SlotIndex) ? BallIcons[SlotIndex] : nullptr)
		{
			Icon->SetRenderScale(FVector2D(1.0f + Pop * 1.4f));
		}
	}
	if (InventoryCountLabel)
	{
		InventoryCountLabel->SetRenderScale(FVector2D(1.0f + 0.3f * Kick));
	}
}

void UChaosImpactChargeWidget::RefreshPersonalAimGuide()
{
	APlayerController* PlayerController = GetOwningPlayer();
	AChaosImpactCharacter* Character = PlayerController
		? Cast<AChaosImpactCharacter>(PlayerController->GetPawn()) : nullptr;
	const bool bShouldShow = Character && Character->IsChargingThrow()
		&& !Character->IsEliminated() && PersonalAimGuideBars.Num() >= 3;
	for (UBorder* Bar : PersonalAimGuideBars)
	{
		if (Bar)
		{
			Bar->SetVisibility(bShouldShow
				? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	if (!bShouldShow)
	{
		return;
	}

	const FVector Direction = Character->GetAimDirection().GetSafeNormal2D();
	const FVector StartWorld = Character->GetAimGuideStartWorldLocation();
	const FVector EndWorld = StartWorld + Direction * Character->GetAimGuideLength();
	FVector2D StartScreen;
	FVector2D EndScreen;
	// This projection removes DPI and quality scaling and returns coordinates
	// relative to this player's sub-viewport. It remains aligned after resizing
	// and for every split-screen layout.
	if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		PlayerController, StartWorld, StartScreen, true)
		|| !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, EndWorld, EndScreen, true))
	{
		for (UBorder* Bar : PersonalAimGuideBars)
		{
			Bar->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	auto PlaceBar = [](UBorder* Bar, const FVector2D& Start, const FVector2D& End,
		const float Thickness)
	{
		if (!Bar)
		{
			return;
		}
		const FVector2D Delta = End - Start;
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Bar->Slot))
		{
			Slot->SetPosition(Start);
			Slot->SetSize(FVector2D(FMath::Max(Delta.Length(), 1.0f), Thickness));
		}
		Bar->SetRenderTransformAngle(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
	};

	PlaceBar(PersonalAimGuideBars[0], StartScreen, EndScreen, 6.0f);
	const FVector2D ScreenDirection = (EndScreen - StartScreen).GetSafeNormal();
	const float HeadLength = 42.0f;
	for (int32 WingIndex = 0; WingIndex < 2; ++WingIndex)
	{
		const float WingRadians = FMath::DegreesToRadians(WingIndex == 0 ? 145.0f : -145.0f);
		const FVector2D WingDirection(
			ScreenDirection.X * FMath::Cos(WingRadians) - ScreenDirection.Y * FMath::Sin(WingRadians),
			ScreenDirection.X * FMath::Sin(WingRadians) + ScreenDirection.Y * FMath::Cos(WingRadians));
		PlaceBar(PersonalAimGuideBars[WingIndex + 1], EndScreen,
			EndScreen + WingDirection * HeadLength, 7.0f);
	}
}

bool UChaosImpactChargeWidget::IsAimGuideVisible() const
{
	return PersonalAimGuideBars.Num() > 0 && PersonalAimGuideBars[0]
		&& PersonalAimGuideBars[0]->GetVisibility() != ESlateVisibility::Collapsed;
}

void UChaosImpactChargeWidget::RefreshSplitScreenDividers()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* OwningLocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerCount = GameInstance ? GameInstance->GetLocalPlayers().Num() : 1;
	const int32 PlayerIndex = GameInstance && OwningLocalPlayer
		? GameInstance->GetLocalPlayers().IndexOfByKey(OwningLocalPlayer) : INDEX_NONE;

	bool bShowVertical = false;
	bool bShowHorizontal = false;
	if (PlayerCount == 2)
	{
		bShowVertical = PlayerIndex == 0;
	}
	else if (PlayerCount == 3)
	{
		// FavorTop: player 1 spans the top; players 2 and 3 share the bottom.
		bShowHorizontal = PlayerIndex == 0;
		bShowVertical = PlayerIndex == 1;
	}
	else if (PlayerCount >= 4)
	{
		bShowVertical = PlayerIndex == 0 || PlayerIndex == 2;
		bShowHorizontal = PlayerIndex == 0 || PlayerIndex == 1;
	}

	const ESlateVisibility VerticalVisibility = bShowVertical
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	const ESlateVisibility HorizontalVisibility = bShowHorizontal
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (VerticalDivider)
	{
		VerticalDivider->SetVisibility(VerticalVisibility);
	}
	if (VerticalDividerAccent)
	{
		VerticalDividerAccent->SetVisibility(VerticalVisibility);
	}
	if (HorizontalDivider)
	{
		HorizontalDivider->SetVisibility(HorizontalVisibility);
	}
	if (HorizontalDividerAccent)
	{
		HorizontalDividerAccent->SetVisibility(HorizontalVisibility);
	}
}

void UChaosImpactChargeWidget::SetChargeAlpha(const float ChargeAlpha)
{
	if (!ChargeBar)
	{
		return;
	}

	const float ClampedAlpha = FMath::Clamp(ChargeAlpha, 0.0f, 1.0f);
	ChargeBar->SetPercent(ClampedAlpha);
	if (ChargeLabel)
	{
		ChargeLabel->SetText(FText::FromString(
			FString::Printf(TEXT("CHARGE  %d%%"), FMath::RoundToInt(ClampedAlpha * 100.0f))));
	}

	const FLinearColor LowColor(0.02f, 0.22f, 0.92f, 1.0f);
	const FLinearColor MidColor(1.0f, 0.58f, 0.0f, 1.0f);
	const FLinearColor FullColor(0.96f, 0.035f, 0.08f, 1.0f);
	const FLinearColor GaugeColor = ClampedAlpha < 0.65f
		? FLinearColor::LerpUsingHSV(LowColor, MidColor, ClampedAlpha / 0.65f)
		: FLinearColor::LerpUsingHSV(MidColor, FullColor, (ClampedAlpha - 0.65f) / 0.35f);
	ChargeBar->SetFillColorAndOpacity(GaugeColor);
}

void UChaosImpactChargeWidget::SetCharging(const bool bCharging)
{
	if (ChargeFrame)
	{
		ChargeFrame->SetVisibility(bCharging ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UChaosImpactChargeWidget::SetHealth(const float CurrentHealth, const float MaxHealth)
{
	const float Clamped = FMath::Clamp(CurrentHealth, 0.0f, FMath::Max(MaxHealth, 1.0f));
	if (bHealthKnown && Clamped < Health - KINDA_SMALL_NUMBER)
	{
		HitAt = FPlatformTime::Seconds();
	}
	Health = Clamped;
	bHealthKnown = true;
}

void UChaosImpactChargeWidget::SetStamina(const float CurrentStamina, const float MaxStamina)
{
	const float SafeMaximum = FMath::Max(MaxStamina, 1.0f);
	const float ClampedStamina = FMath::Clamp(CurrentStamina, 0.0f, SafeMaximum);
	const float StaminaPerSegment = SafeMaximum / FMath::Max(1, StaminaSegments.Num());
	for (int32 SegmentIndex = 0; SegmentIndex < StaminaSegments.Num(); ++SegmentIndex)
	{
		if (UProgressBar* Segment = StaminaSegments[SegmentIndex])
		{
			const float SegmentPercent = FMath::Clamp(
				ClampedStamina / StaminaPerSegment - SegmentIndex, 0.0f, 1.0f);
			Segment->SetPercent(SegmentPercent);
			const FLinearColor ReadyColor(0.02f, 0.22f, 0.92f, 1.0f);
			const FLinearColor RecoveringColor(0.55f, 0.67f, 0.88f, 1.0f);
			Segment->SetFillColorAndOpacity(
				FLinearColor::LerpUsingHSV(RecoveringColor, ReadyColor, SegmentPercent));
		}
	}
}

void UChaosImpactChargeWidget::SetBallInventory(const int32 CurrentBalls, const int32 MaximumBalls)
{
	const int32 ClampedMaximum = FMath::Clamp(MaximumBalls, 0, BallIcons.Num());
	const int32 ClampedCurrent = FMath::Clamp(CurrentBalls, 0, ClampedMaximum);
	if (ClampedCurrent > CarriedBalls)
	{
		BallGainedAt = FPlatformTime::Seconds();
		BallGainedSlot = ClampedCurrent - 1;
	}
	CarriedBalls = ClampedCurrent;
	if (InventoryCountLabel)
	{
		InventoryCountLabel->SetText(FText::FromString(FString::Printf(TEXT("× %d"), ClampedCurrent)));
		InventoryCountLabel->SetColorAndOpacity(FSlateColor(ClampedCurrent > 0
			? FLinearColor(0.0f, 0.82f, 1.0f, 1.0f)
			: FLinearColor(0.44f, 0.5f, 0.62f, 1.0f)));
	}
	for (int32 SlotIndex = 0; SlotIndex < BallIcons.Num(); ++SlotIndex)
	{
		const bool bEnabledSlot = SlotIndex < ClampedMaximum;
		const bool bFilled = SlotIndex < ClampedCurrent;
		if (UTextBlock* Icon = BallIcons[SlotIndex])
		{
			Icon->SetText(FText::FromString(TEXT("●")));
			Icon->SetColorAndOpacity(FSlateColor(bFilled
				? FLinearColor(0.78f, 0.96f, 1.0f, 1.0f)
				: FLinearColor(0.11f, 0.14f, 0.21f, 1.0f)));
		}
		if (UBorder* InventorySlot = BallSlots.IsValidIndex(SlotIndex) ? BallSlots[SlotIndex] : nullptr)
		{
			const FLinearColor FillColor = bFilled
				? FLinearColor(0.0f, 0.1f, 0.22f, 0.96f)
				: bEnabledSlot ? FLinearColor(0.015f, 0.022f, 0.045f, 0.82f)
				: FLinearColor(0.01f, 0.01f, 0.015f, 0.6f);
			const FLinearColor RingColor = bFilled
				? FLinearColor(0.0f, 0.82f, 1.0f, 1.0f)
				: FLinearColor(0.15f, 0.19f, 0.28f, 0.82f);
			InventorySlot->SetBrush(FSlateRoundedBoxBrush(
				FillColor, RingColor, bFilled ? 4.0f : 2.0f));
		}
		if (UBorder* Accent = BallSlotAccents.IsValidIndex(SlotIndex)
			? BallSlotAccents[SlotIndex] : nullptr)
		{
			Accent->SetBrush(FSlateRoundedBoxBrush(
				bFilled ? FLinearColor(1.0f, 0.18f, 0.055f, 1.0f)
					: FLinearColor(0.08f, 0.1f, 0.16f, 1.0f),
				bFilled ? FLinearColor::White : FLinearColor(0.18f, 0.22f, 0.3f, 1.0f), 1.0f));
		}
	}
	RefreshBallPickupAnimation();
}

void UChaosImpactChargeWidget::ShowRespawn(const FString& DefeatedBy, const float TotalSeconds)
{
	DefeatedByName = DefeatedBy;
	bRespawnVisible = true;
	RespawnShownAt = FPlatformTime::Seconds();
	CountdownNumber = INDEX_NONE;
	UpdateRespawn(TotalSeconds, TotalSeconds);
}

void UChaosImpactChargeWidget::UpdateRespawn(const float RemainingSeconds, const float TotalSeconds)
{
	RespawnTotal = FMath::Max(TotalSeconds, UE_SMALL_NUMBER);
	RespawnRemaining = FMath::Clamp(RemainingSeconds, 0.0f, RespawnTotal);
	const int32 Number = FMath::Max(1, FMath::CeilToInt(RespawnRemaining));
	if (Number != CountdownNumber)
	{
		CountdownNumber = Number;
		CountdownChangedAt = FPlatformTime::Seconds();
	}
}

void UChaosImpactChargeWidget::HideRespawn()
{
	bRespawnVisible = false;
}

void UChaosImpactChargeWidget::ShowKnockout(const FString& VictimName)
{
	const double Now = FPlatformTime::Seconds();
	KnockoutStreak = Now - KnockoutAt < 4.0 ? KnockoutStreak + 1 : 1;
	KnockoutAt = Now;
	KnockoutVictim = VictimName;
}

int32 UChaosImpactChargeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 1.0f || Size.Y < 1.0f)
	{
		return BaseLayer;
	}
	const double Clock = FPlatformTime::Seconds();
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerIndex = GameInstance && LocalPlayer
		? FMath::Max(0, GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer)) : 0;
	const FLinearColor Accent = PlayerAccents[PlayerIndex % 4];

	// Hit flash: red screen edges that fade quickly.
	const float HitAge = static_cast<float>(Clock - HitAt);
	if (HitAge < 0.45f)
	{
		const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f);
		const FPainter Edge{AllottedGeometry, OutDrawElements, BaseLayer + 1, 1.0f - HitAge / 0.45f};
		const FLinearColor Red(1.0f, 0.02f, 0.1f, 0.5f);
		const float Band = 70.0f * S;
		Edge.Box(0.0f, 0.0f, Band, Size.Y, Red);
		Edge.Box(Size.X - Band, 0.0f, Band, Size.Y, Red);
		Edge.Box(0.0f, 0.0f, Size.X, Band * 0.6f, Red);
		Edge.Box(0.0f, Size.Y - Band * 0.6f, Size.X, Band * 0.6f, Red);
	}

	// Ball pickup ripple around the slot that just filled.
	const float PickupAge = static_cast<float>(Clock - BallGainedAt);
	if (PickupAge < BallPickupSeconds && BallGainedSlot >= 0 && BallGainedSlot < 2)
	{
		const float T = PickupAge / BallPickupSeconds;
		const FVector2D SlotCenter = FVector2D(Size) + InventoryOffset - InventorySize
			+ BallSlotPositions[BallGainedSlot] + FVector2D(BallSlotSize * 0.5f);
		const FPainter Ripple{AllottedGeometry, OutDrawElements, BaseLayer + 1, 1.0f - T};
		Ripple.Ring(SlotCenter, 44.0f + 30.0f * EaseOut(T), FLinearColor(0.6f, 0.93f, 1.0f, 1.0f),
			2.0f + 4.0f * (1.0f - T));
	}

	// Respawn notice in the bottom-right corner, above the ball stock.
	if (bRespawnVisible)
	{
		const float In = EaseOut(static_cast<float>(Clock - RespawnShownAt) / 0.3f);
		const float Left = Size.X - 488.0f + (1.0f - In) * 140.0f;
		const float Top = Size.Y - 298.0f;
		const FGeometry Corner = MakeAnchor(AllottedGeometry, Left, Top, 1.0f);

		const FGeometry Band = MakeSkewed(Corner, 0.0f, 18.0f, 470.0f, 104.0f, -0.3f);
		const FPainter BandPainter{Band, OutDrawElements, BaseLayer + 2, In};
		BandPainter.Box(8.0f, 9.0f, 470.0f, 104.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		BandPainter.Box(0.0f, 0.0f, 470.0f, 104.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
		BandPainter.Box(0.0f, 0.0f, 470.0f, 5.0f, Fire);
		BandPainter.Box(0.0f, 101.0f, 470.0f, 3.0f, Ice);
		BandPainter.Box(0.0f, 0.0f, 118.0f, 104.0f, Fire);

		const float NumberAge = static_cast<float>(Clock - CountdownChangedAt);
		const FGeometry NumberSpace = MakeSkewed(Corner, 0.0f, 10.0f, 118.0f, 120.0f, -0.2f,
			1.0f + 0.6f * FMath::Exp(-12.0f * NumberAge));
		const FPainter NumberPainter{NumberSpace, OutDrawElements, BaseLayer + 3, In};
		NumberPainter.Text(FString::FromInt(FMath::Max(1, CountdownNumber)), 59.0f, 4.0f, 76.0f, Paper,
			ETextAlign::Center, TEXT("Black"), 3.0f, Ink);

		const FGeometry Labels = MakeSkewed(Corner, 140.0f, 26.0f, 320.0f, 90.0f, -0.25f);
		const FPainter LabelPainter{Labels, OutDrawElements, BaseLayer + 3, In};
		LabelPainter.Text(TEXT("復活まで"), 0.0f, -2.0f, 30.0f, Paper);
		LabelPainter.Text(FString::Printf(TEXT("%s にやられた！"), *DefeatedByName), 2.0f, 46.0f, 21.0f, Fire,
			ETextAlign::Left, TEXT("Black"), 2.0f, Ink);

		const float Remaining = FMath::Clamp(RespawnRemaining / RespawnTotal, 0.0f, 1.0f);
		const FGeometry Progress = MakeSkewed(Corner, 44.0f, 134.0f, 416.0f, 9.0f, -0.6f);
		const FPainter ProgressPainter{Progress, OutDrawElements, BaseLayer + 3, In};
		ProgressPainter.Box(0.0f, 0.0f, 416.0f, 9.0f, FLinearColor(0.05f, 0.06f, 0.1f, 0.9f));
		ProgressPainter.Box(0.0f, 0.0f, 416.0f * Remaining, 9.0f, FMath::Lerp(Ice, Fire, Remaining));
	}

	// KO banner: two slashes cut across the top-right corner, then the KO stamp lands on them.
	const float KnockoutAge = static_cast<float>(Clock - KnockoutAt);
	if (KnockoutAge < 2.2f)
	{
		const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f);
		const float Leave = FMath::Clamp((KnockoutAge - 1.8f) / 0.4f, 0.0f, 1.0f);
		const FGeometry TopRight = MakeAnchor(AllottedGeometry, Size.X, -Leave * 60.0f * S, S);
		const float Cut = EaseOut(KnockoutAge / 0.14f);

		const FGeometry Slashes = MakeSkewed(TopRight, -720.0f, 96.0f, 720.0f, 110.0f, -0.55f);
		const FPainter SlashPainter{Slashes, OutDrawElements, BaseLayer + 4, 1.0f - Leave};
		SlashPainter.Box(720.0f * (1.0f - Cut), 22.0f, 720.0f * Cut, 58.0f, WithAlpha(Fire, 0.92f));
		SlashPainter.Box(720.0f * (1.0f - Cut) + 90.0f, 86.0f, 630.0f * Cut, 10.0f, Ice);

		const float StampAge = FMath::Max(0.0f, KnockoutAge - 0.08f);
		const float Stamp = 1.0f + 1.3f * FMath::Exp(-16.0f * StampAge);
		const float Shake = StampAge > 0.05f && StampAge < 0.3f
			? FMath::Sin(StampAge * 95.0f) * (0.3f - StampAge) * 36.0f : 0.0f;
		const FGeometry StampSpace = MakeSkewed(TopRight, -520.0f + Shake, 40.0f, 330.0f, 170.0f, -0.22f, Stamp);
		const FPainter StampPainter{StampSpace, OutDrawElements, BaseLayer + 5,
			(1.0f - Leave) * FMath::Clamp(StampAge / 0.06f, 0.0f, 1.0f)};
		StampPainter.Text(TEXT("KO!"), 165.0f, 0.0f, 128.0f, Gold, ETextAlign::Center, TEXT("Black"), 7.0f, Ink);

		// Speed lines radiating from the stamp for the first moments.
		if (StampAge < 0.35f)
		{
			const float T = StampAge / 0.35f;
			const FPainter Lines{TopRight, OutDrawElements, BaseLayer + 4, (1.0f - T) * (1.0f - Leave)};
			const FVector2D Center(-355.0f, 125.0f);
			for (int32 LineIndex = 0; LineIndex < 10; ++LineIndex)
			{
				const float Angle = FMath::DegreesToRadians(LineIndex * 36.0f + 12.0f);
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				const float Inner = 150.0f + 160.0f * EaseOut(T);
				Lines.Line(Center + Direction * Inner, Center + Direction * (Inner + 70.0f),
					LineIndex % 2 == 0 ? Paper : Gold, 5.0f);
			}
		}

		const float ChipIn = EaseOut((KnockoutAge - 0.18f) / 0.2f);
		const FGeometry Chip = MakeSkewed(TopRight, -470.0f + (1.0f - ChipIn) * 80.0f, 214.0f, 300.0f, 44.0f, -0.3f);
		const FPainter ChipPainter{Chip, OutDrawElements, BaseLayer + 5, ChipIn * (1.0f - Leave)};
		ChipPainter.Box(0.0f, 0.0f, 300.0f, 44.0f, WithAlpha(Ink, 0.9f));
		ChipPainter.Box(0.0f, 0.0f, 10.0f, 44.0f, Accent);
		ChipPainter.Text(KnockoutVictim, 26.0f, 5.0f, 24.0f, Paper);
		if (KnockoutStreak >= 2)
		{
			ChipPainter.Text(KnockoutStreak == 2 ? FString(TEXT("DOUBLE")) : KnockoutStreak == 3
				? FString(TEXT("TRIPLE")) : FString::Printf(TEXT("×%d"), KnockoutStreak),
				290.0f, 5.0f, 24.0f, Gold, ETextAlign::Right, TEXT("BlackItalic"));
		}
	}
	PaintOnlineOverlay(AllottedGeometry, OutDrawElements, BaseLayer + 6);
	return BaseLayer + 10;
}

void UChaosImpactChargeWidget::PaintOnlineOverlay(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const double Clock = FPlatformTime::Seconds();
	const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f);
	const FGeometry TopLeft = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, S);
	const FGeometry Center = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.5f, S);
	const FGeometry TopCenter = MakeAnchor(AllottedGeometry, Size.X * 0.5f, 0.0f, S);
	static const FLinearColor MemberColors[] = {Ice, Fire, Gold, Violet,
		FLinearColor(0.1f, 0.85f, 0.35f), FLinearColor(1.0f, 0.45f, 0.05f),
		FLinearColor(1.0f, 0.3f, 0.7f), FLinearColor(0.2f, 0.95f, 0.9f)};

	const UChaosImpactSessionSubsystem* Sessions = UChaosImpactSessionSubsystem::Get(this);
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	float NoticeY = 40.0f;

	// Searching for a room while waiting in local training.
	if (Sessions && (!Room || !Room->bOnlineRoom)
		&& (Sessions->GetState() == EChaosImpactRoomState::Searching || Sessions->GetState() == EChaosImpactRoomState::Joining))
	{
		const bool bJoining = Sessions->GetState() == EChaosImpactRoomState::Joining;
		const FGeometry Chip = MakeSkewed(TopLeft, 40.0f, 40.0f, 560.0f, 84.0f, -0.25f);
		const FPainter P{Chip, OutDrawElements, BaseLayer};
		P.Box(8.0f, 9.0f, 560.0f, 84.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		P.Box(0.0f, 0.0f, 560.0f, 84.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
		P.Box(0.0f, 0.0f, 12.0f, 84.0f, bJoining ? Gold : Fire);
		P.Text(bJoining ? TEXT("へやに入ります") : TEXT("へやをさがしています"), 40.0f, 6.0f, 30.0f, Paper);
		P.Text(FString::Printf(TEXT("あいことば  %s"), *Sessions->GetPassword()), 42.0f, 48.0f, 20.0f, Muted);
		const FVector2D SpinCenter(510.0f, 42.0f);
		for (int32 Dot = 0; Dot < 6; ++Dot)
		{
			const float Angle = static_cast<float>(Clock) * 6.0f + Dot * UE_TWO_PI / 6.0f;
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			P.Line(SpinCenter + Direction * 12.0f, SpinCenter + Direction * 22.0f,
				WithAlpha(Paper, 0.2f + 0.8f * Dot / 5.0f), 5.0f);
		}
		NoticeY = 144.0f;
	}

	// Room notices such as "the room was dissolved".
	if (Sessions && !Sessions->GetNotice().IsEmpty() && Clock - Sessions->GetNoticeTime() < 4.0)
	{
		const float Age = static_cast<float>(Clock - Sessions->GetNoticeTime());
		const float Alpha = EaseOut(Age / 0.25f) * FMath::Clamp((4.0f - Age) / 0.4f, 0.0f, 1.0f);
		const FGeometry Chip = MakeSkewed(TopLeft, 40.0f, NoticeY, 520.0f, 60.0f, -0.25f);
		const FPainter P{Chip, OutDrawElements, BaseLayer, Alpha};
		P.Box(0.0f, 0.0f, 520.0f, 60.0f, WithAlpha(Fire, 0.92f));
		P.Text(Sessions->GetNotice(), 30.0f, 10.0f, 28.0f, Paper);
	}

	if (!Room || !Room->bOnlineRoom)
	{
		return;
	}
	const TArray<AChaosImpactPlayerState*> Members = Room->GetMembersInJoinOrder();
	const APlayerState* Self = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<APlayerState>() : nullptr;

	// Member list: host at the top, then everyone in the order they came in.
	if (Room->Phase == EChaosImpactOnlinePhase::Lobby || Room->Phase == EChaosImpactOnlinePhase::Countdown)
	{
		const FGeometry Header = MakeSkewed(TopLeft, 40.0f, 36.0f, 380.0f, 56.0f, -0.25f);
		const FPainter H{Header, OutDrawElements, BaseLayer};
		H.Box(0.0f, 0.0f, 380.0f, 56.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
		H.Box(0.0f, 52.0f, 380.0f, 4.0f, Ice);
		H.Text(FString::Printf(TEXT("へや  %s"), *Room->RoomPassword), 24.0f, 8.0f, 28.0f, Paper);
		if (Room->bRecruitmentClosed)
		{
			// Sits just outside the header so it never covers the password or member count.
			H.Box(396.0f, 8.0f, 112.0f, 40.0f, Fire);
			H.Text(TEXT("募集終了"), 452.0f, 10.0f, 24.0f, Paper, ETextAlign::Center);
		}
		H.Text(FString::Printf(TEXT("%d/%d"), Members.Num(), AChaosImpactGameState::MaxMembers), 360.0f, 6.0f, 32.0f,
			Members.Num() >= AChaosImpactGameState::MaxMembers ? Fire : Gold, ETextAlign::Right);

		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const AChaosImpactPlayerState* Member = Members[Index];
			const double* SeenAt = MemberSeenAt.Find(Member);
			const float Age = SeenAt ? static_cast<float>(Clock - *SeenAt) : 1.0f;
			const float In = EaseOut(Age / 0.3f);
			const FLinearColor Color = MemberColors[FMath::Abs(Member->JoinOrder) % 8];
			const FGeometry Row = MakeSkewed(TopLeft, 40.0f - (1.0f - In) * 420.0f, 104.0f + Index * 52.0f,
				380.0f, 44.0f, -0.25f);
			const FPainter R{Row, OutDrawElements, BaseLayer, In};
			R.Box(6.0f, 6.0f, 380.0f, 44.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			R.Box(0.0f, 0.0f, 380.0f, 44.0f, FLinearColor(0.02f, 0.026f, 0.045f, 0.9f));
			R.Box(0.0f, 0.0f, 16.0f, 44.0f, Color);
			R.Text(Member->GetPlayerName(), 34.0f, 5.0f, 26.0f, Paper);
			if (Member->bRoomHost)
			{
				R.Box(296.0f, 9.0f, 70.0f, 26.0f, Gold);
				R.Text(TEXT("HOST"), 331.0f, 8.0f, 18.0f, Ink, ETextAlign::Center);
			}
			if (Member == Self)
			{
				R.Outline(-3.0f, -3.0f, 386.0f, 50.0f, WithAlpha(Paper, 0.85f), 2.0f);
			}
			if (Age < 0.45f)
			{
				R.Box(0.0f, 0.0f, 380.0f, 44.0f, WithAlpha(Paper, 0.8f * (1.0f - Age / 0.45f)));
			}
		}
	}

	if (Room->Phase == EChaosImpactOnlinePhase::Countdown)
	{
		const float Remaining = Room->GetPhaseRemainingSeconds();
		const int32 Number = FMath::Max(1, FMath::CeilToInt(Remaining));
		const float Pop = 1.0f - (static_cast<float>(Number) - Remaining);
		const FGeometry NumberSpace = MakeSkewed(Center, -200.0f, -160.0f, 400.0f, 300.0f, -0.2f,
			1.0f + 0.8f * FMath::Clamp(Pop - 0.75f, 0.0f, 1.0f) * 4.0f);
		const FPainter N{NumberSpace, OutDrawElements, BaseLayer + 2};
		N.Text(FString::FromInt(Number), 200.0f, 0.0f, 220.0f, Paper, ETextAlign::Center, TEXT("Black"), 7.0f, Fire);
		const FPainter Title{Center, OutDrawElements, BaseLayer + 2};
		Title.Text(TEXT("メンバー募集終了"), 0.0f, 150.0f, 40.0f, Gold, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
	}

	if (Room->Phase == EChaosImpactOnlinePhase::Match)
	{
		const float Remaining = Room->GetPhaseRemainingSeconds();
		const int32 Seconds = FMath::CeilToInt(Remaining);
		const bool bHurry = Remaining <= 10.0f;
		const FGeometry Plate = MakeSkewed(TopCenter, -170.0f, 24.0f, 340.0f, 90.0f, -0.25f,
			bHurry ? 1.0f + 0.05f * FMath::Sin(static_cast<float>(Clock) * 12.0f) : 1.0f);
		const FPainter P{Plate, OutDrawElements, BaseLayer};
		P.Box(8.0f, 9.0f, 340.0f, 90.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		P.Box(0.0f, 0.0f, 340.0f, 90.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
		P.Box(0.0f, 84.0f, 340.0f, 6.0f, bHurry ? Fire : Ice);
		P.Text(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), 170.0f, 4.0f, 60.0f,
			bHurry ? Fire : Paper, ETextAlign::Center);
		if (const AChaosImpactPlayerState* Own = Cast<AChaosImpactPlayerState>(Self))
		{
			const FGeometry KO = MakeSkewed(TopCenter, 190.0f, 34.0f, 170.0f, 64.0f, -0.25f);
			const FPainter K{KO, OutDrawElements, BaseLayer};
			K.Box(0.0f, 0.0f, 170.0f, 64.0f, Fire);
			K.Text(FString::Printf(TEXT("KO %d"), Own->Knockouts), 85.0f, 6.0f, 40.0f, Paper, ETextAlign::Center);
		}
		const float Elapsed = AChaosImpactGameMode::MatchSeconds - Remaining;
		if (Elapsed < 1.0f)
		{
			const FGeometry Start = MakeSkewed(Center, -500.0f, -110.0f, 1000.0f, 220.0f, -0.2f,
				FMath::Lerp(1.6f, 1.0f, EaseOut(Elapsed / 0.2f)));
			const FPainter St{Start, OutDrawElements, BaseLayer + 2, 1.0f - FMath::Clamp((Elapsed - 0.7f) / 0.3f, 0.0f, 1.0f)};
			St.Text(TEXT("START!"), 500.0f, 10.0f, 170.0f, Gold, ETextAlign::Center, TEXT("Black"), 7.0f, Ink);
		}
	}

	if (Room->Phase == EChaosImpactOnlinePhase::Results)
	{
		TArray<AChaosImpactPlayerState*> Ranking = Members;
		Ranking.StableSort([](const AChaosImpactPlayerState& A, const AChaosImpactPlayerState& B)
		{
			return A.Knockouts > B.Knockouts;
		});
		const FPainter Dim{AllottedGeometry, OutDrawElements, BaseLayer};
		Dim.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		const FPainter T{Center, OutDrawElements, BaseLayer + 1};
		T.Text(TEXT("RESULT"), 0.0f, -380.0f, 90.0f, Paper, ETextAlign::Center, TEXT("Black"), 5.0f, Fire);
		int32 Rank = 0;
		for (int32 Index = 0; Index < Ranking.Num(); ++Index)
		{
			if (Index == 0 || Ranking[Index]->Knockouts != Ranking[Index - 1]->Knockouts)
			{
				Rank = Index + 1;
			}
			const FLinearColor Color = MemberColors[FMath::Abs(Ranking[Index]->JoinOrder) % 8];
			const FGeometry Row = MakeSkewed(Center, -380.0f, -250.0f + Index * 66.0f, 760.0f, 56.0f, -0.25f);
			const FPainter R{Row, OutDrawElements, BaseLayer + 1};
			R.Box(0.0f, 0.0f, 760.0f, 56.0f, Rank == 1 ? WithAlpha(Gold, 0.95f) : FLinearColor(0.02f, 0.026f, 0.045f, 0.92f));
			R.Box(0.0f, 0.0f, 16.0f, 56.0f, Color);
			const FLinearColor TextColor = Rank == 1 ? Ink : Paper;
			R.Text(FString::Printf(TEXT("%d位"), Rank), 40.0f, 8.0f, 32.0f, TextColor);
			R.Text(Ranking[Index]->GetPlayerName(), 150.0f, 8.0f, 32.0f, TextColor);
			R.Text(FString::Printf(TEXT("KO %d"), Ranking[Index]->Knockouts), 740.0f, 8.0f, 32.0f, TextColor,
				ETextAlign::Right);
			if (Ranking[Index] == Self)
			{
				R.Outline(-4.0f, -4.0f, 768.0f, 64.0f, Paper, 3.0f);
			}
		}
	}
}
