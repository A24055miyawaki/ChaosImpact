// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactChargeWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactWarpPad.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerState.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

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
	const float BallSwapSeconds = 0.3f;

	/** Characters farther than this from the viewer get no off-screen arrow. */
	const float PlayerMarkerRange = 4200.0f;
	/** At or inside this distance an off-screen arrow is drawn at its largest. */
	const float PlayerMarkerNearDistance = 700.0f;

	/** Ease-out with a small overshoot, so a swapped ball settles into its slot. */
	float SwapEaseOutBack(const float T)
	{
		const float U = FMath::Clamp(T, 0.0f, 1.0f) - 1.0f;
		return 1.0f + 2.70158f * U * U * U + 1.70158f * U * U;
	}

	/** A filled arrowhead pointing along Direction with its tip at Tip; K scales it. */
	void PaintMarkerArrow(const ChaosImpactPaint::FPainter& Painter, const FVector2D& Tip,
		const FVector2D& Direction, const float K, const FLinearColor& Color)
	{
		// Long and notched at the back: a short wide triangle read as pointing sideways.
		const FVector2D Side(-Direction.Y, Direction.X);
		const FVector2D Base = Tip - Direction * 28.0f * K;
		const FVector2D LeftCorner = Base + Side * 11.0f * K;
		const FVector2D RightCorner = Base - Side * 11.0f * K;
		const FVector2D Notch = Tip - Direction * 20.0f * K;
		Painter.Line(Tip, LeftCorner, ChaosImpactPaint::Ink, 5.0f * K);
		Painter.Line(LeftCorner, Notch, ChaosImpactPaint::Ink, 5.0f * K);
		Painter.Line(Notch, RightCorner, ChaosImpactPaint::Ink, 5.0f * K);
		Painter.Line(RightCorner, Tip, ChaosImpactPaint::Ink, 5.0f * K);
		// Slate has no filled polygon; a fan of thick strokes from the tip covers it.
		for (int32 Stroke = 0; Stroke <= 5; ++Stroke)
		{
			Painter.Line(Tip, FMath::Lerp(LeftCorner, Notch, Stroke / 5.0f), Color, 3.0f * K);
			Painter.Line(Tip, FMath::Lerp(Notch, RightCorner, Stroke / 5.0f), Color, 3.0f * K);
		}
	}
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
	StaminaPanel = StaminaFrame;

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
	InventoryPanel = InventoryCanvas;

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
		BallSlotBoxes.Add(SlotSize);

		UTextBlock* BallIcon = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		BallIcon->SetText(FText::FromString(TEXT("●")));
		BallIcon->SetJustification(ETextJustify::Center);
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
		BallSeams.Add(Seam);

		UTextBlock* TypeLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TypeLabel->SetJustification(ETextJustify::Center);
		FSlateFontInfo LabelFont = TypeLabel->GetFont();
		LabelFont.Size = 13;
		LabelFont.TypefaceFontName = TEXT("Bold");
		LabelFont.OutlineSettings.OutlineSize = 2;
		LabelFont.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.02f, 0.05f, 1.0f);
		TypeLabel->SetFont(LabelFont);
		if (UOverlaySlot* LabelSlot = SlotOverlay->AddChildToOverlay(TypeLabel))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Fill);
			LabelSlot->SetVerticalAlignment(VAlign_Bottom);
			LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}
		BallTypeLabels.Add(TypeLabel);

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

	// VS: stamina and ball stock only matter while the match is running.
	const AChaosImpactGameState* MatchState = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const bool bMatchRunning = MatchState && MatchState->bVersusMatch && MatchState->Phase == EChaosImpactOnlinePhase::Match;
	const ESlateVisibility HudVisibility = MatchState && MatchState->bVersusMatch && !bMatchRunning
		? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
	// The old segmented bar is replaced by the painted HP / stamina dial (PaintVitals).
	if (StaminaPanel && StaminaPanel->GetVisibility() != ESlateVisibility::Collapsed)
	{
		StaminaPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	bVitalsHidden = HudVisibility == ESlateVisibility::Collapsed;
	DisplayedStamina = FMath::FInterpTo(DisplayedStamina, StaminaValue, InDeltaTime, 12.0f);
	// The widget-built ball stock is replaced by the painted one (PaintBallInventory).
	if (InventoryPanel && InventoryPanel->GetVisibility() != ESlateVisibility::Collapsed)
	{
		InventoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (const AChaosImpactPlayerState* Own = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
	{
		if (bMatchRunning && Own->Points > LastOwnPoints)
		{
			PointsGained = Own->Points - LastOwnPoints;
			PointsGainedAt = FPlatformTime::Seconds();
		}
		LastOwnPoints = Own->Points;
	}

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
	const double Now = FPlatformTime::Seconds();
	const float Age = static_cast<float>(Now - BallGainedAt);
	const float Kick = Age < BallPickupSeconds
		? 1.0f - ChaosImpactPaint::EaseOut(Age / BallPickupSeconds) : 0.0f;
	const float SwapT = FMath::Clamp(static_cast<float>(Now - BallSwappedAt) / BallSwapSeconds, 0.0f, 1.0f);
	const bool bSwapping = SwapT < 1.0f && CarriedBalls >= 2;
	const float SwapLift = bSwapping ? FMath::Sin(SwapT * UE_PI) : 0.0f;
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
		if (USizeBox* SlotBox = BallSlotBoxes.IsValidIndex(SlotIndex) ? BallSlotBoxes[SlotIndex] : nullptr)
		{
			FVector2D Offset = FVector2D::ZeroVector;
			float Angle = 0.0f;
			float SwapScale = 1.0f;
			if (bSwapping && SlotIndex < 2)
			{
				// The slots already show their new balls; each starts at the other slot and flies home on
				// its own side of a short arc, so the pair visibly trades places. The ball arriving in
				// front (the next throw) swells while the other ducks behind it.
				const FVector2D From = BallSlotPositions[1 - SlotIndex] - BallSlotPositions[SlotIndex];
				const float Travel = SwapEaseOutBack(SwapT);
				const float ArcSide = SlotIndex == 0 ? 24.0f : -24.0f;
				Offset = From * (1.0f - Travel) + FVector2D(-From.Y, From.X).GetSafeNormal() * ArcSide * SwapLift;
				Angle = (SlotIndex == 0 ? -28.0f : 28.0f) * (1.0f - Travel);
				SwapScale = SlotIndex == 0 ? 1.0f + 0.22f * SwapLift : 1.0f - 0.14f * SwapLift;
			}
			SlotBox->SetRenderTranslation(Offset);
			SlotBox->SetRenderTransformAngle(Angle);
			SlotBox->SetRenderScale(FVector2D(SwapScale));
		}
	}
	if (InventoryCountLabel)
	{
		InventoryCountLabel->SetRenderScale(FVector2D(1.0f + 0.3f * Kick + 0.18f * SwapLift));
	}
}

void UChaosImpactChargeWidget::PlayBallSwap()
{
	BallSwappedAt = FPlatformTime::Seconds();
	RefreshBallPickupAnimation();
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
		// The HP arc that just emptied bursts outward in PaintVitals.
		LostHealthSegment = FMath::CeilToInt(Clamped - KINDA_SMALL_NUMBER);
	}
	Health = Clamped;
	MaxHealthValue = FMath::Max(MaxHealth, 1.0f);
	bHealthKnown = true;
}

void UChaosImpactChargeWidget::SetStamina(const float CurrentStamina, const float MaxStamina)
{
	const float SafeMaximum = FMath::Max(MaxStamina, 1.0f);
	const float ClampedStamina = FMath::Clamp(CurrentStamina, 0.0f, SafeMaximum);
	if (ClampedStamina < StaminaValue - 0.5f)
	{
		// A dash: the arc it used flashes once.
		StaminaSpentAt = FPlatformTime::Seconds();
		SpentStaminaSegment = FMath::FloorToInt(ClampedStamina + KINDA_SMALL_NUMBER);
	}
	StaminaValue = ClampedStamina;
	MaxStaminaValue = SafeMaximum;
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

void UChaosImpactChargeWidget::SetBallInventory(const int32 CurrentBalls, const int32 MaximumBalls,
	const uint8 BallTypes)
{
	const int32 ClampedMaximum = FMath::Clamp(MaximumBalls, 0, BallIcons.Num());
	const int32 ClampedCurrent = FMath::Clamp(CurrentBalls, 0, ClampedMaximum);
	if (ClampedCurrent > CarriedBalls)
	{
		BallGainedAt = FPlatformTime::Seconds();
		BallGainedSlot = ClampedCurrent - 1;
	}
	CarriedBalls = ClampedCurrent;
	CarriedBallTypes = BallTypes;

	struct FSlotLook
	{
		FLinearColor Icon;
		FLinearColor Fill;
		FLinearColor Ring;
		FLinearColor Seam;
		FLinearColor Accent;
		FLinearColor Label;
	};
	const auto LookFor = [](const EChaosImpactBallType Type) -> FSlotLook
	{
		switch (Type)
		{
		case EChaosImpactBallType::Fire:
			return {FLinearColor(1.0f, 0.62f, 0.12f), FLinearColor(0.26f, 0.045f, 0.0f, 0.97f),
				FLinearColor(1.0f, 0.38f, 0.03f), FLinearColor(0.55f, 0.1f, 0.0f, 0.95f),
				FLinearColor(1.0f, 0.86f, 0.25f), FLinearColor(1.0f, 0.86f, 0.55f)};
		case EChaosImpactBallType::Ice:
			return {FLinearColor(0.84f, 0.97f, 1.0f), FLinearColor(0.02f, 0.16f, 0.3f, 0.97f),
				FLinearColor(0.62f, 0.93f, 1.0f), FLinearColor(0.3f, 0.6f, 0.85f, 0.95f),
				FLinearColor(0.9f, 0.98f, 1.0f), FLinearColor(0.86f, 0.97f, 1.0f)};
		case EChaosImpactBallType::Thunder:
			return {FLinearColor(1.0f, 0.96f, 0.6f), FLinearColor(0.22f, 0.16f, 0.0f, 0.97f),
				FLinearColor(1.0f, 0.86f, 0.12f), FLinearColor(0.55f, 0.42f, 0.0f, 0.95f),
				FLinearColor(1.0f, 1.0f, 0.72f), FLinearColor(1.0f, 0.94f, 0.55f)};
		case EChaosImpactBallType::Black:
			return {FLinearColor(0.5f, 0.26f, 0.82f), FLinearColor(0.045f, 0.0f, 0.09f, 0.97f),
				FLinearColor(0.68f, 0.3f, 1.0f), FLinearColor(0.24f, 0.07f, 0.4f, 0.95f),
				FLinearColor(0.86f, 0.62f, 1.0f), FLinearColor(0.86f, 0.72f, 1.0f)};
		default:
			return {FLinearColor(0.78f, 0.96f, 1.0f), FLinearColor(0.0f, 0.1f, 0.22f, 0.96f),
				FLinearColor(0.0f, 0.82f, 1.0f), FLinearColor(0.0f, 0.12f, 0.24f, 0.9f),
				FLinearColor(1.0f, 0.18f, 0.055f), FLinearColor::Transparent};
		}
	};

	if (InventoryCountLabel)
	{
		InventoryCountLabel->SetText(FText::FromString(FString::Printf(TEXT("× %d"), ClampedCurrent)));
		// Colored like the ball that will be thrown next.
		InventoryCountLabel->SetColorAndOpacity(FSlateColor(ClampedCurrent > 0
			? LookFor(ChaosImpactBallTypes::GetPackedSlot(BallTypes, 0)).Ring
			: FLinearColor(0.44f, 0.5f, 0.62f, 1.0f)));
	}
	for (int32 SlotIndex = 0; SlotIndex < BallIcons.Num(); ++SlotIndex)
	{
		const bool bEnabledSlot = SlotIndex < ClampedMaximum;
		const bool bFilled = SlotIndex < ClampedCurrent;
		const EChaosImpactBallType Type = bFilled
			? ChaosImpactBallTypes::GetPackedSlot(BallTypes, SlotIndex) : EChaosImpactBallType::Normal;
		const FSlotLook Look = LookFor(Type);
		if (UTextBlock* Icon = BallIcons[SlotIndex])
		{
			Icon->SetText(FText::FromString(TEXT("●")));
			Icon->SetColorAndOpacity(FSlateColor(bFilled ? Look.Icon : FLinearColor(0.11f, 0.14f, 0.21f, 1.0f)));
		}
		if (UTextBlock* Seam = BallSeams.IsValidIndex(SlotIndex) ? BallSeams[SlotIndex] : nullptr)
		{
			Seam->SetColorAndOpacity(FSlateColor(bFilled ? Look.Seam : FLinearColor(0.0f, 0.12f, 0.24f, 0.9f)));
		}
		if (UTextBlock* Label = BallTypeLabels.IsValidIndex(SlotIndex) ? BallTypeLabels[SlotIndex] : nullptr)
		{
			const bool bSpecial = bFilled && Type != EChaosImpactBallType::Normal;
			Label->SetText(bSpecial ? FText::FromString(ChaosImpactBallTypes::GetDisplayName(Type)) : FText::GetEmpty());
			Label->SetColorAndOpacity(FSlateColor(Look.Label));
		}
		if (UBorder* InventorySlot = BallSlots.IsValidIndex(SlotIndex) ? BallSlots[SlotIndex] : nullptr)
		{
			const FLinearColor FillColor = bFilled ? Look.Fill
				: bEnabledSlot ? FLinearColor(0.015f, 0.022f, 0.045f, 0.82f)
				: FLinearColor(0.01f, 0.01f, 0.015f, 0.6f);
			const FLinearColor RingColor = bFilled ? Look.Ring : FLinearColor(0.15f, 0.19f, 0.28f, 0.82f);
			InventorySlot->SetBrush(FSlateRoundedBoxBrush(FillColor, RingColor,
				bFilled ? (Type == EChaosImpactBallType::Normal ? 4.0f : 5.0f) : 2.0f));
		}
		if (UBorder* Accent = BallSlotAccents.IsValidIndex(SlotIndex)
			? BallSlotAccents[SlotIndex] : nullptr)
		{
			Accent->SetBrush(FSlateRoundedBoxBrush(
				bFilled ? Look.Accent : FLinearColor(0.08f, 0.1f, 0.16f, 1.0f),
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
	PaintPlayerMarkers(AllottedGeometry, OutDrawElements, BaseLayer + 1);
	PaintVitals(AllottedGeometry, OutDrawElements, BaseLayer + 1);
	PaintBallInventory(AllottedGeometry, OutDrawElements, BaseLayer + 1);
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
		const float S = GetHudScale(Size.X, Size.Y);
		const FPainter Edge{AllottedGeometry, OutDrawElements, BaseLayer + 1, 1.0f - HitAge / 0.45f};
		const FLinearColor Red(1.0f, 0.02f, 0.1f, 0.5f);
		const float Band = 70.0f * S;
		Edge.Box(0.0f, 0.0f, Band, Size.Y, Red);
		Edge.Box(Size.X - Band, 0.0f, Band, Size.Y, Red);
		Edge.Box(0.0f, 0.0f, Size.X, Band * 0.6f, Red);
		Edge.Box(0.0f, Size.Y - Band * 0.6f, Size.X, Band * 0.6f, Red);
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
		const float S = GetHudScale(Size.X, Size.Y);
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
	PaintVersusMatch(AllottedGeometry, OutDrawElements, BaseLayer + 6);
	PaintOnlineOverlay(AllottedGeometry, OutDrawElements, BaseLayer + 6);
	return BaseLayer + 10;
}

float UChaosImpactChargeWidget::GetHudScale(const double Width, const double Height) const
{
	const UGameInstance* ScaleGameInstance = GetGameInstance();
	const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (ScaleGameInstance && ScaleGameInstance->GetLocalPlayers().Num() > 1 && Viewport && !Viewport->IsSplitscreenForceDisabled())
	{
		// A split view is only part of the screen but is read from the same distance, so it is sized by what
		// fits in the view rather than shrunk in proportion to it.
		return FMath::Clamp(static_cast<float>(FMath::Min(Width / 880.0, Height / 560.0)), 0.62f, 1.1f);
	}
	return FMath::Clamp(static_cast<float>(FMath::Min(Width / 1600.0, Height / 900.0)), 0.5f, 1.4f);
}

void UChaosImpactChargeWidget::PaintBallInventory(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (bVitalsHidden || Size.X < 1.0f || Size.Y < 1.0f)
	{
		return;
	}
	const float S = GetHudScale(Size.X, Size.Y);
	const double Clock = FPlatformTime::Seconds();
	const float Time = static_cast<float>(FMath::Fmod(Clock, 3600.0));
	const UGameInstance* OwningGameInstance = GetGameInstance();
	const ULocalPlayer* OwningLocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerIndex = OwningGameInstance && OwningLocalPlayer
		? FMath::Max(0, OwningGameInstance->GetLocalPlayers().IndexOfByKey(OwningLocalPlayer)) : 0;
	const FLinearColor Accent = PlayerAccents[PlayerIndex % 4];
	const FGeometry Corner = MakeAnchor(AllottedGeometry, Size.X, Size.Y, S);

	// A slanted plate mirroring the vitals panel: the player's colour on the right edge, a hairline fading left.
	constexpr float PlateWidth = 276.0f;
	constexpr float PlateHeight = 80.0f;
	constexpr float PlateRight = -40.0f;
	constexpr float PlateTop = -124.0f;
	const float PlateLeft = PlateRight - PlateWidth;
	const FGeometry PlateSpace = MakeSkewed(Corner, PlateLeft, PlateTop, PlateWidth, PlateHeight, -0.22f);
	const FPainter Plate{PlateSpace, OutDrawElements, BaseLayer};
	Plate.Box(0.0f, 0.0f, PlateWidth, PlateHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.34f));
	Plate.Box(0.0f, PlateHeight * 0.5f, PlateWidth, PlateHeight * 0.5f, FLinearColor(0.0f, 0.0f, 0.0f, 0.18f));
	Plate.Box(PlateWidth - 4.0f, 0.0f, 4.0f, PlateHeight, Accent);
	Plate.Box(PlateWidth * 0.4f, 0.0f, PlateWidth * 0.6f - 4.0f, 1.0f, WithAlpha(Accent, 0.7f));
	Plate.Box(PlateWidth * 0.1f, 0.0f, PlateWidth * 0.3f, 1.0f, WithAlpha(Accent, 0.25f));
	for (int32 Tick = 0; Tick < 5; ++Tick)
	{
		Plate.Box(PlateWidth - 70.0f + Tick * 9.0f, PlateHeight - 7.0f, 5.0f, 2.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
	}

	// One ball, shaded like a sphere, marked by its type.
	const auto PaintBall = [Time](const FPainter& Paint, const FVector2D& Center, const float Radius, const bool bFilled,
		const EChaosImpactBallType Type)
	{
		if (!bFilled)
		{
			// An empty socket: a dark hollow with a dashed rim.
			Paint.Disc(Center, Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
			for (int32 Dash = 0; Dash < 12; ++Dash)
			{
				const float From = Dash * 30.0f + 4.0f;
				Paint.Arc(Center, Radius - 1.0f, From, From + 17.0f, WithAlpha(Muted, 0.55f), 2.0f);
			}
			return;
		}
		FLinearColor Body(0.0f, 0.62f, 1.0f, 1.0f);
		switch (Type)
		{
		case EChaosImpactBallType::Fire: Body = FLinearColor(1.0f, 0.34f, 0.04f, 1.0f); break;
		case EChaosImpactBallType::Ice: Body = FLinearColor(0.58f, 0.88f, 1.0f, 1.0f); break;
		case EChaosImpactBallType::Thunder: Body = FLinearColor(1.0f, 0.8f, 0.1f, 1.0f); break;
		case EChaosImpactBallType::Black: Body = FLinearColor(0.2f, 0.06f, 0.34f, 1.0f); break;
		default: break;
		}
		const FLinearColor Glow = ChaosImpactBallTypes::GetColor(Type);
		const bool bSpecial = Type != EChaosImpactBallType::Normal;
		FLinearColor Shade = Body * 0.5f;
		Shade.A = 1.0f;
		// Glow, drop shadow, a darker body with the lit side on top, then a glossy highlight.
		Paint.Disc(Center, Radius * (bSpecial ? 1.45f + 0.07f * FMath::Sin(Time * 5.0f) : 1.25f),
			WithAlpha(Glow, bSpecial ? 0.2f : 0.1f));
		Paint.Disc(Center + FVector2D(2.5f, 3.5f), Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		Paint.Disc(Center, Radius, Shade);
		Paint.Disc(Center - FVector2D(Radius * 0.1f, Radius * 0.12f), Radius * 0.86f, Body);
		switch (Type)
		{
		case EChaosImpactBallType::Fire:
			// Flame tongues licking upward.
			for (int32 Tongue = 0; Tongue < 3; ++Tongue)
			{
				const float Flicker = 0.5f + 0.5f * FMath::Sin(Time * 9.0f + Tongue * 2.1f);
				Paint.Arc(Center + FVector2D((Tongue - 1) * Radius * 0.3f, Radius * (0.28f - 0.08f * Flicker)),
					Radius * 0.34f, 215.0f, 325.0f, FLinearColor(1.0f, 0.88f, 0.3f, 0.75f + 0.25f * Flicker), 2.5f);
			}
			break;
		case EChaosImpactBallType::Ice:
			// A slowly turning frost star.
			for (int32 Arm = 0; Arm < 3; ++Arm)
			{
				const float Angle = FMath::DegreesToRadians(Arm * 60.0f + Time * 25.0f);
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				Paint.Line(Center - Direction * Radius * 0.55f, Center + Direction * Radius * 0.55f,
					FLinearColor(1.0f, 1.0f, 1.0f, 0.9f), 2.0f);
			}
			break;
		case EChaosImpactBallType::Thunder:
		{
			// A jagged bolt that flickers.
			const float Flicker = FMath::Frac(Time * 11.0f) < 0.8f ? 1.0f : 0.45f;
			const FVector2D Points[] = {FVector2D(-0.12f, -0.66f), FVector2D(0.2f, -0.12f), FVector2D(-0.1f, 0.0f),
				FVector2D(0.14f, 0.62f)};
			for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(Points); ++Index)
			{
				Paint.Line(Center + Points[Index] * Radius, Center + Points[Index + 1] * Radius,
					FLinearColor(1.0f, 1.0f, 0.86f, Flicker), 3.0f);
			}
			break;
		}
		case EChaosImpactBallType::Black:
		{
			// A lightless core ringed in violet, with a sweep circling it.
			const float Sweep = FMath::Fmod(Time * 150.0f, 360.0f);
			Paint.Disc(Center, Radius * 0.46f, Ink);
			Paint.Ring(Center, Radius * 0.52f, FLinearColor(0.82f, 0.48f, 1.0f, 0.95f), 2.0f);
			Paint.Arc(Center, Radius * 0.72f, Sweep, Sweep + 110.0f, FLinearColor(0.92f, 0.62f, 1.0f, 0.8f), 2.0f);
			break;
		}
		default:
			// Dodgeball seams.
			Paint.Arc(Center + FVector2D(Radius * 1.05f, 0.0f), Radius * 0.8f, 140.0f, 220.0f,
				FLinearColor(0.0f, 0.1f, 0.24f, 0.85f), 2.5f);
			Paint.Arc(Center - FVector2D(Radius * 1.05f, 0.0f), Radius * 0.8f, -40.0f, 40.0f,
				FLinearColor(0.0f, 0.1f, 0.24f, 0.85f), 2.5f);
			break;
		}
		Paint.Disc(Center - FVector2D(Radius * 0.36f, Radius * 0.4f), Radius * 0.24f, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		Paint.Disc(Center - FVector2D(Radius * 0.5f, Radius * 0.18f), Radius * 0.08f, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f));
		Paint.Ring(Center, Radius, WithAlpha(Glow, 0.95f), 2.0f);
		if (bSpecial)
		{
			// Special balls: two arcs orbiting just outside.
			const float Spin = FMath::Fmod(Time * 160.0f, 360.0f);
			Paint.Arc(Center, Radius + 5.0f, Spin, Spin + 70.0f, Glow, 2.5f);
			Paint.Arc(Center, Radius + 5.0f, Spin + 180.0f, Spin + 250.0f, Glow, 2.5f);
		}
	};

	// DrawSlot 0 (thrown next) sits large at the front, slot 1 smaller behind it; a swap flies each to the other's place.
	const FVector2D Homes[] = {FVector2D(PlateLeft + 66.0f, PlateTop + 44.0f), FVector2D(PlateLeft + 142.0f, PlateTop + 38.0f)};
	constexpr float Radii[] = {30.0f, 22.0f};
	const float PickupAge = static_cast<float>(Clock - BallGainedAt);
	const float Kick = PickupAge < BallPickupSeconds ? 1.0f - EaseOut(PickupAge / BallPickupSeconds) : 0.0f;
	const float SwapT = FMath::Clamp(static_cast<float>(Clock - BallSwappedAt) / BallSwapSeconds, 0.0f, 1.0f);
	const bool bSwapping = SwapT < 1.0f && CarriedBalls >= 2;
	const float SwapLift = bSwapping ? FMath::Sin(SwapT * UE_PI) : 0.0f;
	FVector2D Centers[2];
	float DrawnRadii[2];
	const FPainter Balls{Corner, OutDrawElements, BaseLayer + 1};
	for (int32 DrawSlot = 1; DrawSlot >= 0; --DrawSlot)
	{
		FVector2D Center = Homes[DrawSlot];
		float Radius = Radii[DrawSlot];
		if (bSwapping)
		{
			const float Travel = SwapEaseOutBack(SwapT);
			const FVector2D From = Homes[1 - DrawSlot];
			const FVector2D Span = Homes[DrawSlot] - From;
			Center = From + Span * Travel + FVector2D(-Span.Y, Span.X).GetSafeNormal() * (DrawSlot == 0 ? -18.0f : 18.0f) * SwapLift;
			Radius = FMath::Lerp(Radii[1 - DrawSlot], Radii[DrawSlot], FMath::Clamp(Travel, 0.0f, 1.0f));
		}
		if (DrawSlot == BallGainedSlot)
		{
			Radius *= 1.0f + 0.28f * Kick;
		}
		Centers[DrawSlot] = Center;
		DrawnRadii[DrawSlot] = Radius;
		const bool bFilled = DrawSlot < CarriedBalls;
		PaintBall(Balls, Center, Radius, bFilled, bFilled ? ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, DrawSlot)
			: EChaosImpactBallType::Normal);
	}

	const FPainter Labels{Corner, OutDrawElements, BaseLayer + 2};
	for (int32 DrawSlot = 0; DrawSlot < FMath::Min(CarriedBalls, 2); ++DrawSlot)
	{
		const EChaosImpactBallType Type = ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, DrawSlot);
		if (Type == EChaosImpactBallType::Normal)
		{
			continue;
		}
		// A small tag with the type's name under the ball.
		const FVector2D Under = Centers[DrawSlot] + FVector2D(0.0f, DrawnRadii[DrawSlot] + 3.0f);
		const FGeometry TagSpace = MakeSkewed(Corner, static_cast<float>(Under.X) - 30.0f, static_cast<float>(Under.Y),
			60.0f, 17.0f, -0.22f);
		const FPainter Tag{TagSpace, OutDrawElements, BaseLayer + 2};
		Tag.Box(0.0f, 0.0f, 60.0f, 17.0f, WithAlpha(Ink, 0.88f));
		Tag.Box(0.0f, 15.0f, 60.0f, 2.0f, ChaosImpactBallTypes::GetColor(Type));
		Labels.Text(ChaosImpactBallTypes::GetDisplayName(Type), static_cast<float>(Under.X), static_cast<float>(Under.Y) - 1.0f,
			11.0f, Paper, ETextAlign::Center, TEXT("Bold"));
	}
	if (CarriedBalls > 0)
	{
		Labels.Text(TEXT("NEXT"), static_cast<float>(Centers[0].X - DrawnRadii[0]) - 6.0f,
			static_cast<float>(Centers[0].Y - DrawnRadii[0]) - 10.0f, 10.0f, Accent, ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
	}

	// The count: a large number in the colour of the next ball.
	const FLinearColor CountColor = CarriedBalls > 0
		? ChaosImpactBallTypes::GetColor(ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, 0)) : Muted;
	Labels.Text(FString::FromInt(CarriedBalls), PlateRight - 18.0f, PlateTop + 10.0f, 38.0f, CountColor,
		ETextAlign::Right, TEXT("Black"), 2.0f, Ink);
	if (Kick > 0.0f)
	{
		const FPainter Flash{Corner, OutDrawElements, BaseLayer + 3, Kick};
		Flash.Text(FString::FromInt(CarriedBalls), PlateRight - 18.0f, PlateTop + 10.0f, 38.0f, Paper,
			ETextAlign::Right, TEXT("Black"));
	}
	Labels.Text(TEXT("×"), PlateRight - 58.0f, PlateTop + 30.0f, 18.0f, WithAlpha(Paper, 0.7f), ETextAlign::Right, TEXT("Bold"));
	Labels.Text(TEXT("BALL"), PlateRight - 18.0f, PlateTop + 60.0f, 10.0f, Muted, ETextAlign::Right, TEXT("Bold"));

	// Pickup ripple and swap flash.
	if (PickupAge < BallPickupSeconds && BallGainedSlot >= 0 && BallGainedSlot < 2)
	{
		const float T = PickupAge / BallPickupSeconds;
		const FPainter Ripple{Corner, OutDrawElements, BaseLayer + 2, 1.0f - T};
		Ripple.Ring(Centers[BallGainedSlot], DrawnRadii[BallGainedSlot] + 6.0f + 26.0f * EaseOut(T),
			FLinearColor(0.6f, 0.93f, 1.0f, 1.0f), 2.0f + 4.0f * (1.0f - T));
	}
	const float SwapFlashSeconds = BallSwapSeconds + 0.1f;
	const float SwapAge = static_cast<float>(Clock - BallSwappedAt);
	if (SwapAge < SwapFlashSeconds && CarriedBalls >= 2)
	{
		const float T = SwapAge / SwapFlashSeconds;
		const FPainter Flash{Corner, OutDrawElements, BaseLayer + 2, 1.0f - T};
		for (int32 DrawSlot = 0; DrawSlot < 2; ++DrawSlot)
		{
			Flash.Ring(Centers[DrawSlot], DrawnRadii[DrawSlot] + 6.0f + 16.0f * EaseOut(T), WithAlpha(Gold, 0.9f), 1.5f + 3.5f * (1.0f - T));
		}
	}

	// Both hands full: a keycap showing how to swap.
	if (CarriedBalls >= 2 && !bRespawnVisible)
	{
		const AChaosImpactPlayerController* InputController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		const bool bGamepad = InputController && InputController->IsUsingGamepad();
		const float KeyWidth = bGamepad ? 34.0f : 24.0f;
		const float KeyRight = PlateRight - 14.0f;
		const float KeyTop = PlateTop - 30.0f;
		const FPainter Hint{Corner, OutDrawElements, BaseLayer + 2};
		Hint.Box(KeyRight - KeyWidth, KeyTop + 2.0f, KeyWidth, 22.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		Hint.Box(KeyRight - KeyWidth, KeyTop, KeyWidth, 20.0f, WithAlpha(Paper, 0.92f));
		Hint.Text(bGamepad ? TEXT("LB") : TEXT("Q"), KeyRight - KeyWidth * 0.5f, KeyTop + 1.0f, 13.0f, Ink,
			ETextAlign::Center, TEXT("Black"));
		Hint.Text(TEXT("持ち替え"), KeyRight - KeyWidth - 8.0f, KeyTop + 1.0f, 13.0f, Paper, ETextAlign::Right,
			TEXT("Bold"), 2.0f, Ink);
	}
}

void UChaosImpactChargeWidget::PaintVitals(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (bVitalsHidden || !bHealthKnown || Size.X < 1.0f || Size.Y < 1.0f)
	{
		return;
	}
	const float S = GetHudScale(Size.X, Size.Y);
	const double Clock = FPlatformTime::Seconds();
	const UGameInstance* OwningGameInstance = GetGameInstance();
	const ULocalPlayer* OwningLocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerIndex = OwningGameInstance && OwningLocalPlayer
		? FMath::Max(0, OwningGameInstance->GetLocalPlayers().IndexOfByKey(OwningLocalPlayer)) : 0;
	const FLinearColor Accent = PlayerAccents[PlayerIndex % 4];
	const FGeometry Corner = MakeAnchor(AllottedGeometry, 0.0f, Size.Y, S);
	const FPainter Panel{Corner, OutDrawElements, BaseLayer};

	// Two segmented rows in the spot the old stamina bar used: HP cells above, stamina cells below.
	constexpr float Left = 40.0f;
	constexpr float LabelWidth = 36.0f;
	constexpr float BarWidth = 268.0f;
	constexpr float ValueWidth = 40.0f;
	constexpr float CellGap = 4.0f;
	constexpr float PanelTop = -124.0f;
	constexpr float PanelHeight = 80.0f;
	const float BarLeft = Left + LabelWidth;
	const float PanelWidth = LabelWidth + BarWidth + ValueWidth + 20.0f;

	// Panel: darker towards the bottom, a player-colour edge on the left and a fading hairline on top.
	Panel.Box(Left - 10.0f, PanelTop, PanelWidth, PanelHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.3f));
	Panel.Box(Left - 10.0f, PanelTop + PanelHeight * 0.5f, PanelWidth, PanelHeight * 0.5f, FLinearColor(0.0f, 0.0f, 0.0f, 0.18f));
	Panel.Box(Left - 10.0f, PanelTop, 4.0f, PanelHeight, Accent);
	Panel.Box(Left - 6.0f, PanelTop, PanelWidth * 0.6f, 1.0f, WithAlpha(Accent, 0.7f));
	Panel.Box(Left - 6.0f + PanelWidth * 0.6f, PanelTop, PanelWidth * 0.3f, 1.0f, WithAlpha(Accent, 0.25f));

	// A cell with a light top band and a dark lower edge, so it reads as a small raised block.
	const auto PaintCell = [&Panel](const float X, const float Y, const float W, const float H, const FLinearColor& Color)
	{
		if (W <= 0.0f)
		{
			return;
		}
		Panel.Box(X, Y, W, H, Color);
		Panel.Box(X, Y, W, H * 0.38f, FLinearColor(1.0f, 1.0f, 1.0f, 0.22f * Color.A));
		Panel.Box(X, Y + H - 2.0f, W, 2.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f * Color.A));
	};

	// HP: large cells. On the last point they glow and pulse red; a lost cell flashes and grows out.
	const int32 HealthCells = FMath::Clamp(FMath::RoundToInt(MaxHealthValue), 1, 6);
	const int32 HealthLeft = FMath::Max(0, FMath::CeilToInt(Health - KINDA_SMALL_NUMBER));
	const bool bLastPoint = HealthLeft <= 1;
	const float Pulse = bLastPoint ? 0.6f + 0.4f * FMath::Sin(static_cast<float>(Clock) * 7.0f) : 1.0f;
	const float HitAge = static_cast<float>(Clock - HitAt);
	constexpr float HealthY = -112.0f;
	constexpr float HealthHeight = 28.0f;
	const float HealthCell = (BarWidth - CellGap * (HealthCells - 1)) / HealthCells;
	const FLinearColor HealthColor = bLastPoint ? Fire : Paper;
	Panel.Text(TEXT("HP"), Left, HealthY + 4.0f, 16.0f, HealthColor, ETextAlign::Left, TEXT("Bold"));
	for (int32 Cell = 0; Cell < HealthCells; ++Cell)
	{
		const float X = BarLeft + Cell * (HealthCell + CellGap);
		if (Cell < HealthLeft)
		{
			if (bLastPoint)
			{
				Panel.Box(X - 3.0f, HealthY - 3.0f, HealthCell + 6.0f, HealthHeight + 6.0f, WithAlpha(Fire, 0.28f * Pulse));
			}
			PaintCell(X, HealthY, HealthCell, HealthHeight, WithAlpha(HealthColor, bLastPoint ? Pulse : 0.95f));
		}
		else
		{
			Panel.Box(X, HealthY, HealthCell, HealthHeight, FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
		}
		if (Cell == LostHealthSegment && HitAge < 0.5f)
		{
			const float T = HitAge / 0.5f;
			const float Grow = 8.0f * EaseOut(T);
			const FPainter Burst{Corner, OutDrawElements, BaseLayer + 1, 1.0f - T};
			Burst.Box(X, HealthY, HealthCell, HealthHeight, WithAlpha(Fire, 0.85f));
			Burst.Outline(X - Grow, HealthY - Grow, HealthCell + Grow * 2.0f, HealthHeight + Grow * 2.0f, Fire, 2.0f);
		}
	}
	Panel.Text(FString::FromInt(HealthLeft), BarLeft + BarWidth + ValueWidth, HealthY + 1.0f, 22.0f, HealthColor,
		ETextAlign::Right, TEXT("Black"), 2.0f, Ink);

	// Stamina: thinner cells in the player's colour. The one recovering shows a bright leading edge, a full
	// bar catches a sheen now and then, it all greys out while a dash is not possible, and a dash's cell flashes.
	const int32 StaminaCells = FMath::Clamp(FMath::RoundToInt(MaxStaminaValue), 1, 10);
	const bool bCanDash = StaminaValue >= 1.0f - KINDA_SMALL_NUMBER;
	const bool bFull = DisplayedStamina >= MaxStaminaValue - 0.01f;
	const float SpentAge = static_cast<float>(Clock - StaminaSpentAt);
	constexpr float StaminaY = -72.0f;
	constexpr float StaminaHeight = 18.0f;
	const float StaminaCell = (BarWidth - CellGap * (StaminaCells - 1)) / StaminaCells;
	Panel.Text(TEXT("ST"), Left, StaminaY - 1.0f, 16.0f, bCanDash ? Paper : Muted, ETextAlign::Left, TEXT("Bold"));
	for (int32 Cell = 0; Cell < StaminaCells; ++Cell)
	{
		const float X = BarLeft + Cell * (StaminaCell + CellGap);
		Panel.Box(X, StaminaY, StaminaCell, StaminaHeight, FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
		const float Fill = FMath::Clamp(DisplayedStamina - Cell, 0.0f, 1.0f);
		if (Fill > 0.0f)
		{
			const FLinearColor Color = !bCanDash ? Muted : Fill >= 1.0f ? Accent : WithAlpha(Accent, 0.5f);
			PaintCell(X, StaminaY, StaminaCell * Fill, StaminaHeight, Color);
			if (Fill < 1.0f && bCanDash)
			{
				Panel.Box(X + StaminaCell * Fill - 2.0f, StaminaY, 2.0f, StaminaHeight, WithAlpha(Paper, 0.85f));
			}
		}
		if (Cell == SpentStaminaSegment && SpentAge < 0.35f)
		{
			const FPainter Flash{Corner, OutDrawElements, BaseLayer + 1, 1.0f - SpentAge / 0.35f};
			Flash.Box(X, StaminaY, StaminaCell, StaminaHeight, Paper);
		}
	}
	if (bFull)
	{
		// A narrow light band crosses the full bar every few seconds, clipped to the bar.
		constexpr float SheenWidth = 26.0f;
		const float Travel = FMath::Fmod(static_cast<float>(Clock), 3.0f) / 0.8f;
		if (Travel < 1.0f)
		{
			const float SheenLeft = BarLeft - SheenWidth + (BarWidth + SheenWidth) * Travel;
			const float ClipLeft = FMath::Max(SheenLeft, BarLeft);
			const float ClipRight = FMath::Min(SheenLeft + SheenWidth, BarLeft + BarWidth);
			const FPainter Sheen{Corner, OutDrawElements, BaseLayer + 1};
			Sheen.Box(ClipLeft, StaminaY, ClipRight - ClipLeft, StaminaHeight, FLinearColor(1.0f, 1.0f, 1.0f, 0.28f));
		}
	}
	Panel.Text(FString::FromInt(FMath::FloorToInt(StaminaValue + KINDA_SMALL_NUMBER)), BarLeft + BarWidth + ValueWidth,
		StaminaY - 1.0f, 16.0f, bCanDash ? WithAlpha(Paper, 0.8f) : Muted, ETextAlign::Right, TEXT("Bold"));
}

void UChaosImpactChargeWidget::PaintPlayerMarkers(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	APlayerController* PlayerController = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PlayerController || !World || !PlayerController->PlayerCameraManager)
	{
		return;
	}
	const FVector2D Size(AllottedGeometry.GetLocalSize());
	if (Size.X < 1.0 || Size.Y < 1.0)
	{
		return;
	}
	const float S = GetHudScale(Size.X, Size.Y);
	const FVector2D Center = Size * 0.5;
	const APawn* ViewerPawn = PlayerController->GetPawn();
	const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	const FRotationMatrix CameraAxes(PlayerController->PlayerCameraManager->GetCameraRotation());
	// "Near" is measured from the player's own character, or from the camera while spectating.
	const FVector Reference = ViewerPawn ? ViewerPawn->GetActorLocation() : CameraLocation;
	const UGameInstance* GameInstance = GetGameInstance();

	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		const AChaosImpactCharacter* Character = *It;
		if (!IsValid(Character) || Character->IsEliminated() || Character->IsHidden())
		{
			continue;
		}

		// Local players wear their split-screen accent, other people gold, CPUs a quiet grey.
		FLinearColor Color = Gold;
		if (Character->GetCPUNumber() > 0)
		{
			Color = FLinearColor(0.74f, 0.78f, 0.86f, 1.0f);
		}
		else if (const APlayerController* CharacterController = Cast<APlayerController>(Character->GetController());
			CharacterController && CharacterController->IsLocalController() && CharacterController->GetLocalPlayer()
			&& GameInstance)
		{
			Color = PlayerAccents[FMath::Max(
				GameInstance->GetLocalPlayers().IndexOfByKey(CharacterController->GetLocalPlayer()), 0) % 4];
		}
		if (const AChaosImpactGameState* MatchState = World->GetGameState<AChaosImpactGameState>();
			MatchState && MatchState->IsTeamBattle())
		{
			if (const AChaosImpactPlayerState* CharacterState = Character->GetPlayerState<AChaosImpactPlayerState>();
				CharacterState && CharacterState->TeamIndex >= 0)
			{
				Color = ChaosImpactMatch::GetTeamColor(CharacterState->TeamIndex);
			}
		}
		const FString Name = Character->GetOverheadDisplayName();

		const FVector Body = Character->GetPresentationLocation();
		FVector2D BodyScreen;
		const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController, Body, BodyScreen, true);
		const bool bInView = bProjected && BodyScreen.X >= 0.0 && BodyScreen.Y >= 0.0
			&& BodyScreen.X <= Size.X && BodyScreen.Y <= Size.Y;
		if (bInView)
		{
			const float HalfHeight = Character->GetCapsuleComponent()
				? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f;
			FVector2D HeadScreen;
			if (!Name.IsEmpty() && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				PlayerController, Body + FVector(0.0f, 0.0f, HalfHeight + 40.0f), HeadScreen, true))
			{
				const FPainter Tag{AllottedGeometry, OutDrawElements, BaseLayer, 1.0f};
				const float FontSize = 15.0f * S;
				const FVector2D Tip(HeadScreen.X, HeadScreen.Y);
				Tag.Text(Name, static_cast<float>(HeadScreen.X), static_cast<float>(HeadScreen.Y) - FontSize * 1.75f - 7.0f * S,
					FontSize, Color, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
				PaintMarkerArrow(Tag, Tip, FVector2D(0.0, 1.0), 0.38f * S, Color);
			}
			// Standing on a warp pad: a ring above the name fills up until the warp.
			if (const float WarpCharge = AChaosImpactWarpPad::FindChargeProgress(Character); WarpCharge > 0.0f
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					PlayerController, Body + FVector(0.0f, 0.0f, HalfHeight + 40.0f), HeadScreen, true))
			{
				const FPainter Gauge{AllottedGeometry, OutDrawElements, BaseLayer + 1, 1.0f};
				const FVector2D GaugeCenter(HeadScreen.X, HeadScreen.Y - 58.0f * S);
				const float Radius = 12.0f * S;
				const float Pulse = WarpCharge > 0.8f
					? 0.5f + 0.5f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 22.0f) : 0.0f;
				Gauge.Disc(GaugeCenter, Radius + 5.0f * S, WithAlpha(Ink, 0.6f));
				Gauge.Arc(GaugeCenter, Radius, -90.0f, 270.0f, WithAlpha(Color, 0.25f), 3.5f * S);
				Gauge.Arc(GaugeCenter, Radius, -90.0f, -90.0f + 360.0f * WarpCharge,
					FMath::Lerp(Color, FLinearColor::White, Pulse * 0.7f), 3.5f * S);
			}
			continue;
		}
		if (Character == ViewerPawn)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Reference, Body));
		if (Distance > PlayerMarkerRange)
		{
			continue;
		}

		// Toward the character on screen. Points behind the camera project mirrored, so use the camera axes.
		const FVector FromCamera = Body - CameraLocation;
		FVector2D Direction = bProjected && FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::X)) > 0.0
			? BodyScreen - Center
			: FVector2D(FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Y)),
				-FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Z)));
		if (!Direction.Normalize())
		{
			continue;
		}

		// The closer they are, the bigger and more solid the arrow.
		const float Nearness = 1.0f - FMath::Clamp(
			(Distance - PlayerMarkerNearDistance) / (PlayerMarkerRange - PlayerMarkerNearDistance), 0.0f, 1.0f);
		const float K = S * FMath::Lerp(0.75f, 1.8f, Nearness * Nearness);
		const float Margin = 30.0f * K + 6.0f;
		const FVector2D Half(FMath::Max(Center.X - Margin, 1.0), FMath::Max(Center.Y - Margin, 1.0));
		const double Reach = FMath::Min(
			FMath::Abs(Direction.X) > UE_KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Direction.X) : UE_BIG_NUMBER,
			FMath::Abs(Direction.Y) > UE_KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Direction.Y) : UE_BIG_NUMBER);
		const FVector2D Tip = Center + Direction * Reach + Direction * 14.0f * K;

		const FPainter Marker{AllottedGeometry, OutDrawElements, BaseLayer, FMath::Lerp(0.6f, 1.0f, Nearness)};
		Marker.Disc(Tip - Direction * 16.0f * K, 18.0f * K, WithAlpha(Ink, 0.62f));
		PaintMarkerArrow(Marker, Tip, Direction, K, Color);
		const FVector2D Label = Tip - Direction * 48.0f * K;
		const float LabelSize = 11.0f * S * FMath::Lerp(0.95f, 1.2f, Nearness);
		Marker.Text(Name, static_cast<float>(Label.X), static_cast<float>(Label.Y) - LabelSize * 0.85f, LabelSize,
			Color, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
	}
}

void UChaosImpactChargeWidget::PaintVersusMatch(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match || !Match->bVersusMatch || Match->Phase == EChaosImpactOnlinePhase::TeamSelect)
	{
		return;
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 1.0f || Size.Y < 1.0f)
	{
		return;
	}
	const float S = GetHudScale(Size.X, Size.Y);
	const double Clock = FPlatformTime::Seconds();
	// The opening runs on this machine's own clock, so a late arrival still sees all of it.
	const float Elapsed = Match->GetIntroElapsedSeconds();
	const FGeometry Center = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.5f, S);
	const FGeometry TopLeft = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, S);
	const AChaosImpactPlayerState* Own = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	const bool bTeams = Match->IsTeamBattle();
	const auto NameOf = [](const AChaosImpactPlayerState* Member) -> FString
	{
		const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(Member->GetPawn());
		return Character ? Character->GetOverheadDisplayName() : Member->GetPlayerName();
	};
	const auto ColorOf = [bTeams, Own](const AChaosImpactPlayerState* Member) -> FLinearColor
	{
		if (bTeams && Member->TeamIndex >= 0)
		{
			return ChaosImpactMatch::GetTeamColor(Member->TeamIndex);
		}
		return Member == Own ? Ice : Member->IsABot() ? Muted : Gold;
	};
	const auto RankColor = [](const int32 Rank) -> FLinearColor
	{
		return Rank == 1 ? Gold : Rank == 2 ? FLinearColor(0.78f, 0.84f, 0.94f)
			: Rank == 3 ? FLinearColor(0.86f, 0.52f, 0.26f) : Muted;
	};

	// Opening: mission banner over the flyover, then Ready? once the camera reaches the player.
	if (Match->Phase == EChaosImpactOnlinePhase::Intro)
	{
		const float CameraEnd = ChaosImpactMatch::FlyoverSeconds + ChaosImpactMatch::DiveSeconds;
		const float Bars = EaseOut(Elapsed / 0.4f)
			* (1.0f - FMath::Clamp((Elapsed - (CameraEnd - 0.5f)) / 0.5f, 0.0f, 1.0f));
		if (Bars > 0.0f)
		{
			const FPainter Frame{AllottedGeometry, OutDrawElements, BaseLayer};
			const float BarHeight = Size.Y * 0.09f * Bars;
			Frame.Box(0.0f, 0.0f, Size.X, BarHeight, Ink);
			Frame.Box(0.0f, Size.Y - BarHeight, Size.X, BarHeight, Ink);
		}
		const float In = EaseOut((Elapsed - 0.3f) / 0.35f);
		const float Out = FMath::Clamp((Elapsed - (ChaosImpactMatch::FlyoverSeconds - 0.35f)) / 0.3f, 0.0f, 1.0f);
		if (In > 0.0f && Out < 1.0f)
		{
			const float Alpha = In * (1.0f - Out);
			// Plain type over the flyover, rising slightly into place.
			const float Rise = (1.0f - In) * 16.0f;
			const FPainter Words{Center, OutDrawElements, BaseLayer + 2, Alpha};
			Words.Text(bTeams ? TEXT("チームで多く敵を倒せ！") : TEXT("時間内に多く敵を倒せ！"), 0.0f, -74.0f + Rise, 60.0f,
				Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
			Words.Box(-150.0f * In, 18.0f + Rise, 300.0f * In, 2.0f, WithAlpha(Paper, 0.6f));
			Words.Text(FString::Printf(TEXT("%s ・ %d分"), *ChaosImpactMatch::DescribeTeams(Match->Rules.TeamCount),
				Match->Rules.Minutes), 0.0f, 30.0f + Rise, 22.0f, WithAlpha(Paper, 0.85f), ETextAlign::Center,
				TEXT("Regular"), 2.0f, Ink);
		}
		// Ready? is drawn once for the whole screen by UChaosImpactMatchAnnouncerWidget.
		return;
	}

	if (Match->Phase == EChaosImpactOnlinePhase::Match)
	{
		// The time is shown once for the whole screen by UChaosImpactMatchAnnouncerWidget. Each view keeps
		// the standings: the top three players, or every team in a team battle, plus this player's own line.
		// In split screen the shared clock sits at the top middle of the whole screen, over a view's corner.
		// That clock is scaled for the whole screen while this view is scaled for itself, so its height is
		// converted into this view's units before the rows are placed under it.
		const UGameInstance* ViewGameInstance = GetGameInstance();
		float RowTop = 16.0f;
		if (ViewGameInstance && ViewGameInstance->GetLocalPlayers().Num() > 1)
		{
			const FVector2D Screen = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).GetLocalSize();
			const float ScreenScale = FMath::Clamp(static_cast<float>(FMath::Min(Screen.X / 1600.0, Screen.Y / 900.0)), 0.42f, 1.4f);
			RowTop = FMath::Max(16.0f, 56.0f * ScreenScale / S + 4.0f);
		}
		const auto PaintRow = [&](const int32 Row, const int32 Rank, const FLinearColor& Accent, const FString& Label,
			const int32 Points, const bool bOwn)
		{
			const float Y = RowTop + Row * 30.0f;
			const FPainter Line{TopLeft, OutDrawElements, BaseLayer};
			Line.Box(16.0f, Y, 236.0f, 26.0f, FLinearColor(0.0f, 0.0f, 0.0f, bOwn ? 0.5f : 0.3f));
			Line.Box(16.0f, Y, 3.0f, 26.0f, Accent);
			if (Rank > 0)
			{
				Line.Text(FString::FromInt(Rank), 34.0f, Y + 2.0f, 16.0f, RankColor(Rank), ETextAlign::Center, TEXT("Bold"));
			}
			Line.Text(Label, 50.0f, Y + 2.0f, 16.0f, bOwn ? Paper : WithAlpha(Paper, 0.82f), ETextAlign::Left, TEXT("Regular"));
			Line.Text(FString::FromInt(Points), 244.0f, Y + 1.0f, 17.0f, Paper, ETextAlign::Right, TEXT("Bold"));
		};
		int32 OwnRow = INDEX_NONE;
		if (bTeams)
		{
			TArray<TPair<int32, int32>> Teams;
			for (int32 Team = 0; Team < Match->Rules.TeamCount; ++Team)
			{
				Teams.Add({Team, Match->GetTeamPoints(Team)});
			}
			Teams.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Value > B.Value; });
			int32 Rank = 0;
			for (int32 Index = 0; Index < Teams.Num(); ++Index)
			{
				Rank = Index == 0 || Teams[Index].Value != Teams[Index - 1].Value ? Index + 1 : Rank;
				PaintRow(Index, Rank, ChaosImpactMatch::GetTeamColor(Teams[Index].Key),
					ChaosImpactMatch::GetTeamName(Teams[Index].Key),
					Teams[Index].Value, Own && Own->TeamIndex == Teams[Index].Key);
			}
			// A team total hides what this player scored, so they get a line of their own below.
			if (Own)
			{
				OwnRow = Teams.Num();
				PaintRow(OwnRow, 0, ColorOf(Own), NameOf(Own), Own->Points, true);
			}
		}
		else
		{
			const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
			int32 Rank = 0;
			for (int32 Index = 0; Index < FMath::Min(3, Ranking.Num()); ++Index)
			{
				Rank = Index == 0 || Ranking[Index]->Points != Ranking[Index - 1]->Points ? Index + 1 : Rank;
				PaintRow(Index, Rank, ColorOf(Ranking[Index]), NameOf(Ranking[Index]), Ranking[Index]->Points,
					Ranking[Index] == Own);
				OwnRow = Ranking[Index] == Own ? Index : OwnRow;
			}
			// This player's own place when outside the top three.
			if (Own && Ranking.IndexOfByKey(Own) >= 3)
			{
				int32 OwnRank = 1;
				for (const AChaosImpactPlayerState* Other : Ranking)
				{
					OwnRank += Other->Points > Own->Points ? 1 : 0;
				}
				PaintRow(3, OwnRank, ColorOf(Own), NameOf(Own), Own->Points, true);
				OwnRow = 3;
			}
		}

		// This player's place, large, just above the HP panel so it can be read at a glance.
		if (Own)
		{
			int32 OwnRank = 1;
			int32 Places = 1;
			if (bTeams)
			{
				Places = FMath::Max(1, Match->Rules.TeamCount);
				const int32 OwnTeamPoints = Match->GetTeamPoints(FMath::Clamp(Own->TeamIndex, 0, Places - 1));
				for (int32 Team = 0; Team < Places; ++Team)
				{
					OwnRank += Match->GetTeamPoints(Team) > OwnTeamPoints ? 1 : 0;
				}
			}
			else
			{
				const TArray<AChaosImpactPlayerState*> Everyone = Match->GetRanking();
				Places = FMath::Max(1, Everyone.Num());
				for (const AChaosImpactPlayerState* Other : Everyone)
				{
					OwnRank += Other->Points > Own->Points ? 1 : 0;
				}
			}
			if (OwnRank != ShownOwnRank)
			{
				OwnRankDelta = ShownOwnRank > 0 ? ShownOwnRank - OwnRank : 0;
				ShownOwnRank = OwnRank;
				OwnRankChangedAt = Clock;
			}
			const float ChangeAge = static_cast<float>(Clock - OwnRankChangedAt);
			const FLinearColor Place = RankColor(OwnRank);
			const FGeometry BottomLeft = MakeAnchor(AllottedGeometry, 0.0f, Size.Y, S);
			constexpr float BadgeLeft = 30.0f;
			constexpr float BadgeTop = -206.0f;
			constexpr float BadgeWidth = 188.0f;
			constexpr float BadgeHeight = 68.0f;
			const FPainter Plate{MakeSkewed(BottomLeft, BadgeLeft, BadgeTop, BadgeWidth, BadgeHeight, -0.22f), OutDrawElements, BaseLayer};
			if (OwnRank == 1)
			{
				const float Shine = 0.5f + 0.5f * FMath::Sin(static_cast<float>(FMath::Fmod(Clock, 3600.0)) * 3.0f);
				Plate.Box(-4.0f, -4.0f, BadgeWidth + 8.0f, BadgeHeight + 8.0f, WithAlpha(Gold, 0.12f + 0.1f * Shine));
			}
			Plate.Box(0.0f, 0.0f, BadgeWidth, BadgeHeight, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			Plate.Box(0.0f, 0.0f, 6.0f, BadgeHeight, Place);
			Plate.Box(6.0f, BadgeHeight - 2.0f, BadgeWidth - 6.0f, 2.0f, WithAlpha(Place, 0.6f));
			if (ChangeAge < 0.5f)
			{
				Plate.Box(0.0f, 0.0f, BadgeWidth, BadgeHeight, WithAlpha(Paper, 0.35f * (1.0f - ChangeAge / 0.5f)));
			}
			const FPainter Words{BottomLeft, OutDrawElements, BaseLayer + 1};
			Words.Text(bTeams ? TEXT("チーム順位") : TEXT("順位"), BadgeLeft + 18.0f, BadgeTop + 4.0f, 11.0f,
				WithAlpha(Paper, 0.7f), ETextAlign::Left, TEXT("Bold"));
			// The number lands a little large whenever the place changes.
			const float Pop = 1.0f + 0.6f * FMath::Exp(-9.0f * ChangeAge);
			const FPainter Number{MakeSkewed(BottomLeft, BadgeLeft + 16.0f, BadgeTop + 8.0f, 80.0f, 60.0f, 0.0f, Pop),
				OutDrawElements, BaseLayer + 2};
			Number.Text(FString::FromInt(OwnRank), 80.0f, -4.0f, 52.0f, Place, ETextAlign::Right, TEXT("Black"), 3.0f, Ink);
			Words.Text(TEXT("位"), BadgeLeft + 100.0f, BadgeTop + 28.0f, 24.0f, Place, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
			Words.Text(FString::Printf(TEXT("/ %d"), Places), BadgeLeft + 142.0f, BadgeTop + 36.0f, 17.0f, Muted,
				ETextAlign::Left, TEXT("Bold"));
			if (OwnRankDelta != 0 && ChangeAge < 1.6f)
			{
				const bool bUp = OwnRankDelta > 0;
				const FPainter Arrow{BottomLeft, OutDrawElements, BaseLayer + 2, 1.0f - FMath::Clamp((ChangeAge - 1.1f) / 0.5f, 0.0f, 1.0f)};
				Arrow.Text(bUp ? TEXT("▲") : TEXT("▼"), BadgeLeft + BadgeWidth + 6.0f,
					BadgeTop + 18.0f + (bUp ? -1.0f : 1.0f) * 8.0f * EaseOut(ChangeAge / 0.4f), 22.0f,
					bUp ? FLinearColor(0.2f, 1.0f, 0.45f) : Fire, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
			}
		}

		// Points just scored float up beside this player's own line.
		const float GainAge = static_cast<float>(Clock - PointsGainedAt);
		if (OwnRow != INDEX_NONE && GainAge < 0.9f && PointsGained > 0)
		{
			const FPainter Gain{TopLeft, OutDrawElements, BaseLayer + 1, 1.0f - GainAge / 0.9f};
			Gain.Text(FString::Printf(TEXT("+%d"), PointsGained), 262.0f,
				RowTop + 1.0f + OwnRow * 30.0f - 10.0f * EaseOut(GainAge / 0.9f), 17.0f, Gold, ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
		}
		// GO!, the final countdown, the time and the results are drawn once for the whole screen by
		// UChaosImpactMatchAnnouncerWidget.
	}
}

void UChaosImpactChargeWidget::PaintOnlineOverlay(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	// Split screen: the room overlay is about the whole machine, so only the first player's view shows it.
	if (const UGameInstance* OwningGameInstance = GetGameInstance();
		OwningGameInstance && OwningGameInstance->GetLocalPlayers().IndexOfByKey(GetOwningLocalPlayer()) > 0)
	{
		return;
	}

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const double Clock = FPlatformTime::Seconds();
	const float S = GetHudScale(Size.X, Size.Y);
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
	// The single list outlines every player on this machine.
	TArray<const APlayerState*, TInlineAllocator<4>> LocalStates;
	if (const UGameInstance* OwningGameInstance = GetGameInstance())
	{
		for (const ULocalPlayer* LocalPlayer : OwningGameInstance->GetLocalPlayers())
		{
			const APlayerController* LocalController = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
			if (const APlayerState* LocalState = LocalController ? LocalController->GetPlayerState<APlayerState>() : nullptr)
			{
				LocalStates.Add(LocalState);
			}
		}
	}

	// Member list: host at the top, then everyone in the order they came in.
	if (!Room->bVersusMatch && (Room->Phase == EChaosImpactOnlinePhase::Lobby || Room->Phase == EChaosImpactOnlinePhase::Starting))
	{
		const FGeometry Header = MakeSkewed(TopLeft, 40.0f, 36.0f, 380.0f, 56.0f, -0.25f);
		const FPainter H{Header, OutDrawElements, BaseLayer};
		H.Box(0.0f, 0.0f, 380.0f, 56.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
		H.Box(0.0f, 52.0f, 380.0f, 4.0f, Ice);
		H.Text(Room->RoomName.IsEmpty() ? FString::Printf(TEXT("へや  %s"), *Room->RoomPassword) : Room->RoomName,
			24.0f, 3.0f, 24.0f, Paper);
		H.Text(FString::Printf(TEXT("あいことば %s"), *Room->RoomPassword), 26.0f, 33.0f, 13.0f, Muted);
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
			if (Room->bRulesDecided)
			{
				// 準備OK at a glance: a lit green check, or an empty box while still waiting.
				const float BoxLeft = Member->bRoomHost ? 252.0f : 326.0f;
				if (Member->bReadyForMatch)
				{
					R.Box(BoxLeft, 5.0f, 34.0f, 34.0f, FLinearColor(0.12f, 0.85f, 0.35f, 1.0f));
					R.Line(FVector2D(BoxLeft + 7.0f, 22.0f), FVector2D(BoxLeft + 15.0f, 31.0f), Paper, 5.0f);
					R.Line(FVector2D(BoxLeft + 14.0f, 31.0f), FVector2D(BoxLeft + 28.0f, 11.0f), Paper, 5.0f);
				}
				else
				{
					R.Box(BoxLeft, 5.0f, 34.0f, 34.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
					R.Outline(BoxLeft, 5.0f, 34.0f, 34.0f, WithAlpha(Paper, 0.35f), 2.0f);
				}
			}
			if (!Member->bSecondOfMachine)
			{
				// Connection quality from the round-trip time. A pair shares one connection, so only its
				// first player shows it; players on the host's machine have no network delay at all.
				const float PingMs = Member->GetPingInMilliseconds();
				const bool bMeasured = Member->bHostMachine || PingMs > 0.0f;
				const int32 Level = Member->bHostMachine || PingMs <= 60.0f ? 4
					: PingMs <= 120.0f ? 3 : PingMs <= 200.0f ? 2 : 1;
				const FLinearColor SignalColor = Level == 4 ? FLinearColor(0.2f, 0.95f, 0.45f)
					: Level == 3 ? FLinearColor(0.65f, 0.95f, 0.3f) : Level == 2 ? Gold : Fire;
				R.Box(392.0f, 2.0f, 56.0f, 40.0f, FLinearColor(0.02f, 0.026f, 0.045f, 0.9f));
				for (int32 Bar = 0; Bar < 4; ++Bar)
				{
					const float BarHeight = 8.0f + Bar * 8.0f;
					const bool bLit = bMeasured && Bar < Level;
					R.Box(400.0f + Bar * 11.0f, 36.0f - BarHeight, 8.0f, BarHeight,
						bLit ? SignalColor : WithAlpha(Paper, 0.16f));
				}
			}
			if (LocalStates.Contains(Member))
			{
				R.Outline(-3.0f, -3.0f, 386.0f, 50.0f, WithAlpha(Paper, 0.85f), 2.0f);
			}
			if (Age < 0.45f)
			{
				R.Box(0.0f, 0.0f, 380.0f, 44.0f, WithAlpha(Paper, 0.8f * (1.0f - Age / 0.45f)));
			}
		}

		const FGeometry TopRight = MakeAnchor(AllottedGeometry, Size.X, 0.0f, S);
		if (Room->bRulesDecided)
		{
			// The decided rules, who is ready, and how long until the match starts by itself.
			const FChaosImpactMatchRules& Rules = Room->Rules;
			const int32 Ready = Room->CountReadyMembers();
			const double ServerNow = Room->GetServerWorldTimeSeconds();
			const float WaitLeft = Room->ReadyDeadline > 0.0
				? FMath::Max(0.0f, static_cast<float>(Room->ReadyDeadline - ServerNow)) : 0.0f;
			const bool bStartingNow = Room->Phase == EChaosImpactOnlinePhase::Starting;
			const FGeometry Panel = MakeSkewed(TopRight, -460.0f, 36.0f, 420.0f, 250.0f, -0.12f);
			const FPainter P{Panel, OutDrawElements, BaseLayer};
			P.Box(8.0f, 9.0f, 420.0f, 250.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
			P.Box(0.0f, 0.0f, 420.0f, 250.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
			P.Box(0.0f, 0.0f, 420.0f, 50.0f, Gold);
			P.Text(TEXT("ルール"), 24.0f, 6.0f, 30.0f, Ink, ETextAlign::Left, TEXT("Black"));
			P.Text(FString::Printf(TEXT("%d分"), Rules.Minutes), 396.0f, 6.0f, 30.0f, Ink, ETextAlign::Right, TEXT("Black"));
			P.Text(ChaosImpactMatch::DescribeTeams(Rules.TeamCount), 26.0f, 62.0f, 28.0f,
				Rules.IsTeamBattle() ? Gold : Paper, ETextAlign::Left, TEXT("Bold"));
			P.Text(FString::Printf(TEXT("CPU %d人"), Rules.CPUCount), 396.0f, 62.0f, 28.0f,
				Rules.CPUCount > 0 ? Fire : Muted, ETextAlign::Right, TEXT("Bold"));

			// One pip per member, lit when ready.
			P.Text(TEXT("準備OK"), 26.0f, 112.0f, 24.0f, Paper, ETextAlign::Left, TEXT("Bold"));
			const int32 Pips = FMath::Max(1, Members.Num());
			for (int32 Pip = 0; Pip < Pips; ++Pip)
			{
				const bool bLit = Pip < Ready;
				const float X = 136.0f + Pip * 30.0f;
				P.Box(X, 116.0f, 22.0f, 26.0f, bLit ? FLinearColor(0.12f, 0.85f, 0.35f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
			}
			P.Text(FString::Printf(TEXT("%d/%d"), Ready, Members.Num()), 396.0f, 110.0f, 28.0f,
				Ready >= Members.Num() ? FLinearColor(0.2f, 0.95f, 0.45f) : Paper, ETextAlign::Right, TEXT("Bold"));

			// The time left before the match starts without waiting for everyone.
			const float WaitAlpha = FMath::Clamp(WaitLeft / AChaosImpactGameState::ReadyWaitSeconds, 0.0f, 1.0f);
			P.Box(26.0f, 162.0f, 370.0f, 10.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
			P.Box(26.0f, 162.0f, 370.0f * (bStartingNow ? 0.0f : WaitAlpha), 10.0f, WaitLeft <= 10.0f ? Fire : Ice);
			P.Text(bStartingNow ? FString(TEXT("まもなく開始")) : FString::Printf(TEXT("自動スタートまで %d秒"),
				FMath::CeilToInt(WaitLeft)), 26.0f, 178.0f, 20.0f, bStartingNow || WaitLeft <= 10.0f ? Fire : Muted);

			// How this machine presses 準備OK, and whether it already has.
			const AChaosImpactPlayerState* OwnState = LocalStates.IsEmpty() ? nullptr : Cast<AChaosImpactPlayerState>(LocalStates[0]);
			if (!bStartingNow && Room->Phase == EChaosImpactOnlinePhase::Lobby)
			{
				// Only the key for the device this player is using right now.
				const AChaosImpactPlayerController* InputController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
				const bool bGamepad = InputController && InputController->IsUsingGamepad();
				const bool bOwnReady = OwnState && OwnState->bReadyForMatch;
				const float Pulse = bOwnReady ? 1.0f : 0.7f + 0.3f * FMath::Sin(static_cast<float>(Clock) * 5.0f);
				const float KeyWidth = bGamepad ? 70.0f : 36.0f;
				P.Box(26.0f, 208.0f, KeyWidth, 32.0f, WithAlpha(Paper, Pulse));
				P.Text(bGamepad ? TEXT("十字↑") : TEXT("R"), 26.0f + KeyWidth * 0.5f, 209.0f, 20.0f, Ink,
					ETextAlign::Center, TEXT("Black"));
				P.Text(bOwnReady ? TEXT("もう一度でキャンセル") : TEXT("で準備OK"), 38.0f + KeyWidth, 210.0f, 22.0f,
					bOwnReady ? Muted : Paper, ETextAlign::Left, TEXT("Bold"));
			}
		}
		else if (Room->bRecruitmentClosed && Room->Phase == EChaosImpactOnlinePhase::Lobby)
		{
			const FGeometry Chip = MakeSkewed(TopRight, -460.0f, 36.0f, 420.0f, 60.0f, -0.25f);
			const FPainter P{Chip, OutDrawElements, BaseLayer, 0.75f + 0.25f * FMath::Sin(static_cast<float>(Clock) * 4.0f)};
			P.Box(0.0f, 0.0f, 420.0f, 60.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.92f));
			P.Box(0.0f, 0.0f, 10.0f, 60.0f, Gold);
			P.Text(TEXT("ホストがルールを決めています"), 30.0f, 12.0f, 26.0f, Paper);
		}
	}

}
