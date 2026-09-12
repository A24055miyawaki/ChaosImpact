// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactChargeWidget.h"

#include "Blueprint/WidgetTree.h"
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
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

void UChaosImpactChargeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChargeRoot"));
	WidgetTree->RootWidget = RootCanvas;

	// A strong dark seam keeps adjacent cameras readable, while the narrow blue
	// highlight gives the divider a deliberate in-game finish.
	auto AddDivider = [this, RootCanvas](const FName Name, const bool bVertical,
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
	InventorySlot->SetPosition(FVector2D(-24.0f, -22.0f));
	InventorySlot->SetSize(FVector2D(240.0f, 128.0f));

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
		SlotSize->SetWidthOverride(86.0f);
		SlotSize->SetHeightOverride(86.0f);
		UCanvasPanelSlot* BallCanvasSlot = InventoryCanvas->AddChildToCanvas(SlotSize);
		BallCanvasSlot->SetPosition(SlotIndex == 0 ? FVector2D(17.0f, 30.0f) : FVector2D(94.0f, 7.0f));
		BallCanvasSlot->SetSize(FVector2D(86.0f));

		UOverlay* SlotOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		SlotSize->SetContent(SlotOverlay);

		UBorder* BallSlot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		BallSlot->SetPadding(FMargin(0.0f));
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
	RefreshSplitScreenDividers();
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
			InventorySlot->SetRenderScale(bFilled ? FVector2D(1.0f) : FVector2D(0.92f));
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
}
