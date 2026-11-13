// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactRuleCard.h"
#include "ChaosImpactSuddenDeathZone.h"
#include "ChaosImpactTutorial.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactTournamentWidget.h"
#include "ChaosImpactPlayerMarkerLayout.h"
#include "ChaosImpactGameViewportClient.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactBuddyBall.h"
#include "ChaosImpact.h"
#include "ChaosImpactSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactShoulderCameraComponent.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactCrashBall.h"
#include "ChaosImpactStageBase.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactSoloStages.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactSimaeBird.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactSpectatorPawn.h"
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
	/** A tournament is being played here (its arenas and practice areas are no training: no training settings). */
	bool CwIsTournamentWorld(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
		return Match && (Match->IsTournament() || Match->bPublicTournament);
	}

	/** A HUD card as the lobby draws them: a soft shadow, a night-blue body shading down, a faint rim, a strip of Accent. */
	void CwCard(const ChaosImpactPaint::FPainter& C, const float W, const float H, const FLinearColor& Accent, const float AccentW)
	{
		using namespace ChaosImpactPaint;
		const TArray<FVector2D> Shape{FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)};
		C.Box(9.0f, 10.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.34f));
		C.Box(4.0f, 5.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
		HudPoly(C, Shape, MakeFill(FLinearColor(0.04f, 0.06f, 0.12f, 0.95f), FLinearColor(0.006f, 0.008f, 0.02f, 0.96f)));
		const FPainter Up = HudAbove(C, 1);
		if (AccentW > 0.0f)
		{
			HudPoly(Up, {FVector2D(0.0, 0.0), FVector2D(AccentW, 0.0), FVector2D(AccentW, H), FVector2D(0.0, H)},
				MakeFill(FMath::Lerp(Accent, Paper, 0.25f), Accent * FLinearColor(0.6f, 0.6f, 0.6f, 1.0f)));
		}
		Up.Box(0.0f, 0.0f, W, 1.5f, WithAlpha(Paper, 0.16f));
		Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.1f), 1.0f);
	}

	/** Searching: a radar, its sweep turning and rings going out from it (Found: it stops, lit). */
	void CwRadar(const ChaosImpactPaint::FPainter& C, const FVector2D& At, const float Radius, const float Time, const FLinearColor& Tone,
		const bool bFound)
	{
		using namespace ChaosImpactPaint;
		C.Disc(At, Radius, FLinearColor(0.01f, 0.02f, 0.04f, 0.95f));
		const FPainter Up = HudAbove(C, 1);
		Up.Ring(At, Radius, WithAlpha(Tone, 0.55f), 2.0f, 48);
		Up.Ring(At, Radius * 0.62f, WithAlpha(Tone, 0.22f), 1.5f, 40);
		Up.Line(At - FVector2D(Radius, 0.0), At + FVector2D(Radius, 0.0), WithAlpha(Tone, 0.15f), 1.0f);
		Up.Line(At - FVector2D(0.0, Radius), At + FVector2D(0.0, Radius), WithAlpha(Tone, 0.15f), 1.0f);
		if (bFound)
		{
			Up.Disc(At, Radius * 0.42f, Tone);
			Up.Ring(At, Radius * (0.5f + 0.5f * FMath::Frac(Time * 1.5f)), WithAlpha(Tone, 1.0f - FMath::Frac(Time * 1.5f)), 3.0f, 48);
			return;
		}
		// The sweep: a fading fan of arcs behind its edge.
		const float Edge = FMath::Fmod(Time * 220.0f, 360.0f);
		for (int32 Trail = 0; Trail < 8; ++Trail)
		{
			Up.Arc(At, Radius * 0.55f, Edge - (Trail + 1) * 9.0f, Edge - Trail * 9.0f, WithAlpha(Tone, 0.5f * (1.0f - Trail / 8.0f)), Radius * 0.9f);
		}
		const float Rad = FMath::DegreesToRadians(Edge);
		Up.Line(At, At + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * Radius, Paper, 2.0f);
		// Blips that light as the sweep passes them.
		static const float BlipAngles[] = {35.0f, 140.0f, 250.0f};
		static const float BlipFar[] = {0.55f, 0.75f, 0.4f};
		for (int32 Blip = 0; Blip < 3; ++Blip)
		{
			const float Since = FMath::Fmod(Edge - BlipAngles[Blip] + 360.0f, 360.0f) / 360.0f;
			const float BlipRad = FMath::DegreesToRadians(BlipAngles[Blip]);
			HudAbove(Up, 1).Disc(At + FVector2D(FMath::Cos(BlipRad), FMath::Sin(BlipRad)) * Radius * BlipFar[Blip], 3.5f,
				WithAlpha(Paper, FMath::Clamp(1.0f - Since * 2.5f, 0.0f, 1.0f)));
		}
		// A ring going out.
		const float Wave = FMath::Frac(Time * 0.8f);
		Up.Ring(At, Radius * (1.0f + 0.5f * Wave), WithAlpha(Tone, 0.5f * (1.0f - Wave)), 2.0f, 48);
	}

	/** This player's tournament match is opening: like a duel's opening, no battle HUD over it. */
	bool CwIsTournamentOpening(const UUserWidget* Widget)
	{
		const UWorld* World = Widget ? Widget->GetWorld() : nullptr;
		const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
		const APlayerController* Owner = Widget ? Widget->GetOwningPlayer() : nullptr;
		const FChaosImpactDuelState* Duel = Match && Owner && Match->IsTournament() ? Match->FindDuelFor(Owner->PlayerState) : nullptr;
		return Duel && Duel->Step == EChaosImpactDuelStep::Opening;
	}
}

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
	SetClipping(EWidgetClipping::ClipToBoundsAlways);

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
	// The whole-stage map: filmed all the while it is open, thirty times a second (what moves on the stage moves on it;
	// the people and the balls are drawn over it every frame). Its middle glides after an area that moves.
	if (const auto* MapOwner = Cast<AChaosImpactPlayerController>(GetOwningPlayer()); MapOwner && MapOwner->IsOverviewMapOpen())
	{
		FVector Center;
		float Half = 0.0f;
		const double Now = FPlatformTime::Seconds();
		if (!bMapWasOpen)
		{
			MapOpenedAt = Now;
		}
		if (FindMapArea(Center, Half))
		{
			const float Yaw = MapOwner->PlayerCameraManager ? MapOwner->PlayerCameraManager->GetCameraRotation().Yaw : 0.0f;
			// (A new area, or the map just opened: straight there; else a quick glide after it.)
			MapGlideCenter = !bMapWasOpen || !bMapTaken || !MapGlideCenter.Equals(Center, Half * 0.5f) ? Center
				: FMath::VInterpTo(MapGlideCenter, Center, FMath::Min(InDeltaTime, 0.1f), 12.0f);
			if (!bMapWasOpen || Now - MapShotAt >= 1.0 / 30.0)
			{
				MapShotAt = Now;
				TakeMapPicture(MapGlideCenter, Half, Yaw);
			}
		}
		bMapWasOpen = true;
	}
	else
	{
		if (bMapWasOpen && IsValid(MapCamera))
		{
			MapCamera->Destroy();
			MapCamera = nullptr;
		}
		bMapWasOpen = false;
	}
	const ULocalPlayer* SingleDuelPlayer = UChaosImpactGameViewportClient::GetSingleDuelPlayer(GetWorld());
	// Slate keeps a player's HUD layer even when their scene view has zero size. Keep its tick alive so it
	// restores on return to the lobby, but hide the observer's personal HUD during the shared duel view.
	SetRenderOpacity(SingleDuelPlayer && GetOwningLocalPlayer() != SingleDuelPlayer ? 0.f : 1.f);
	RefreshPersonalAimGuide();
	RefreshSplitScreenDividers();
	if (!bSpectatorView)
	{
		Tutorial.Tick(Cast<AChaosImpactPlayerController>(GetOwningPlayer()), BallSwappedAt);
	}
	RefreshBallPickupAnimation();
	UpdateCombatHealth();

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
	bVitalsHidden = HudVisibility == ESlateVisibility::Collapsed || CwIsTournamentOpening(this);
	DisplayedStamina = FMath::FInterpTo(DisplayedStamina, StaminaValue, InDeltaTime, 12.0f);
	// The widget-built ball stock is replaced by the painted one (PaintBallInventory).
	if (InventoryPanel && InventoryPanel->GetVisibility() != ESlateVisibility::Collapsed)
	{
		InventoryPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (const AChaosImpactPlayerState* Own = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
	{
		// (ルビーラッシュ: this machine's own pickups count at once.)
		const AChaosImpactGameState* OwnMatch = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
		const int32 OwnPoints = OwnMatch && OwnMatch->IsRubyRush() ? ChaosImpactParty::GetShownCount(GetWorld(), Own) : Own->Points;
		if (bMatchRunning && OwnPoints > LastOwnPoints)
		{
			PointsGained = OwnPoints - LastOwnPoints;
			PointsGainedAt = FPlatformTime::Seconds();
		}
		LastOwnPoints = OwnPoints;
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
		UpdateRoomToasts(Room);
	}
}

void UChaosImpactChargeWidget::AddRoomToast(APlayerState* Member, const FString& Name, const TCHAR* Message,
	const FLinearColor& Accent)
{
	RoomToasts.Add({Member, Name, Message, Accent, FPlatformTime::Seconds()});
	UE_LOG(LogChaosImpact, Log, TEXT("Room note: %s%s"), Member ? *Member->GetPlayerName() : *Name, Message);
	// Only the newest few; a burst of arrivals should not fill the screen.
	constexpr int32 MaxToasts = 4;
	if (RoomToasts.Num() > MaxToasts)
	{
		RoomToasts.RemoveAt(0, RoomToasts.Num() - MaxToasts);
	}
}

void UChaosImpactChargeWidget::UpdateRoomToasts(const AChaosImpactGameState* Room)
{
	using namespace ChaosImpactPaint;

	const double Now = FPlatformTime::Seconds();
	if (RoomWatchStartedAt <= 0.0)
	{
		RoomWatchStartedAt = Now;
	}
	// Everyone already here replicates in over the first moments; they did not just arrive.
	const bool bAnnounce = Now - RoomWatchStartedAt > 2.0;
	const APlayerState* OwnState = GetOwningPlayer() ? GetOwningPlayer()->PlayerState.Get() : nullptr;
	TSet<const APlayerState*> Present;
	for (APlayerState* State : Room->PlayerArray)
	{
		const AChaosImpactPlayerState* Member = Cast<AChaosImpactPlayerState>(State);
		if (!Member || Member->IsABot() || Member->IsInactive())
		{
			continue;
		}
		Present.Add(Member);
		FRoomMemberSeen* Seen = RoomMembersSeen.Find(State);
		if (!Seen)
		{
			RoomMembersSeen.Add(State, {Member->GetPlayerName(), Member->bSpectating, Now, bAnnounce && State != OwnState});
			continue;
		}
		if (Seen->bJoinPending)
		{
			// A newcomer's machine name shows until their own name arrives, so the note waits a moment.
			if (Now - Seen->FirstSeenAt >= 1.0)
			{
				Seen->bJoinPending = false;
				AddRoomToast(State, Member->GetPlayerName(), Member->bSpectating ? TEXT(" が観戦に来ました") : TEXT(" が参加しました"),
					Member->bSpectating ? Gold : Ice);
			}
		}
		else if (Seen->bSpectating != Member->bSpectating && bAnnounce)
		{
			AddRoomToast(State, Member->GetPlayerName(),
				Member->bSpectating ? TEXT(" が観戦に切り替えました") : TEXT(" がプレイヤーに戻りました"),
				Member->bSpectating ? Gold : Ice);
		}
		Seen->Name = Member->GetPlayerName();
		Seen->bSpectating = Member->bSpectating;
	}
	for (auto It = RoomMembersSeen.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid() || !Present.Contains(It->Key.Get()))
		{
			if (bAnnounce && !It->Value.bJoinPending)
			{
				AddRoomToast(nullptr, It->Value.Name, TEXT(" が退出しました"), Muted);
			}
			It.RemoveCurrent();
		}
	}
	RoomToasts.RemoveAll([Now](const FRoomToast& Toast) { return Now - Toast.At > 4.5; });
}

void UChaosImpactChargeWidget::PaintRoomToasts(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer, const float Top) const
{
	using namespace ChaosImpactPaint;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FGeometry TopRight = MakeAnchor(AllottedGeometry, Size.X, 0.0f, GetHudScale(Size.X, Size.Y));
	const double Clock = FPlatformTime::Seconds();
	constexpr float Width = 340.0f;
	constexpr float Height = 40.0f;
	// Newest on top; each slides in from the right and fades before it goes.
	for (int32 Index = RoomToasts.Num() - 1, Row = 0; Index >= 0; --Index, ++Row)
	{
		const FRoomToast& Toast = RoomToasts[Index];
		const float Age = static_cast<float>(Clock - Toast.At);
		const float In = EaseOut(Age / 0.25f);
		const float Alpha = In * FMath::Clamp((4.5f - Age) / 0.5f, 0.0f, 1.0f);
		const FString Name = Toast.Member.IsValid() ? Toast.Member->GetPlayerName() : Toast.Name;
		const FPainter P{MakeSkewed(TopRight, -Width - 40.0f + (1.0f - In) * 120.0f, Top + Row * (Height + 8.0f), Width, Height, -0.25f),
			OutDrawElements, BaseLayer, Alpha};
		P.Box(6.0f, 6.0f, Width, Height, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		P.Box(0.0f, 0.0f, Width, Height, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
		P.Box(0.0f, 0.0f, 8.0f, Height, Toast.Accent);
		const FString ToastLine = Name + Toast.Message;
		P.Text(ToastLine, 22.0f, 7.0f, HudFit(ToastLine, 20.0f, Width - 34.0f, TEXT("Bold")), Paper, ETextAlign::Left, TEXT("Bold"));
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
		&& !Character->IsShoulderViewActive()
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
			// The pivot is the bar's left middle, so its middle line starts exactly at Start.
			Slot->SetPosition(Start - FVector2D(0.0f, Thickness * 0.5f));
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
	if (PlayerCount == 2 && !UChaosImpactGameViewportClient::GetSingleDuelPlayer(GetWorld()))
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
	PointerChargeAlpha = FMath::Clamp(ChargeAlpha, 0.0f, 1.0f);
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
	bPointerCharging = bCharging;
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
		case EChaosImpactBallType::Wind:
			return {FLinearColor(0.8f, 1.0f, 0.78f), FLinearColor(0.0f, 0.16f, 0.04f, 0.97f),
				FLinearColor(0.35f, 1.0f, 0.45f), FLinearColor(0.06f, 0.42f, 0.14f, 0.95f),
				FLinearColor(0.84f, 1.0f, 0.8f), FLinearColor(0.78f, 1.0f, 0.74f)};
		case EChaosImpactBallType::Smoke:
			return {FLinearColor(0.82f, 0.8f, 0.9f), FLinearColor(0.07f, 0.065f, 0.1f, 0.97f),
				FLinearColor(0.7f, 0.66f, 0.86f), FLinearColor(0.25f, 0.23f, 0.32f, 0.95f),
				FLinearColor(0.88f, 0.86f, 0.96f), FLinearColor(0.84f, 0.82f, 0.94f)};
		case EChaosImpactBallType::Beam:
			return {FLinearColor(1.0f, 0.78f, 0.96f), FLinearColor(0.22f, 0.0f, 0.14f, 0.97f),
				FLinearColor(1.0f, 0.3f, 0.8f), FLinearColor(0.5f, 0.05f, 0.34f, 0.95f),
				FLinearColor(1.0f, 0.85f, 0.97f), FLinearColor(1.0f, 0.78f, 0.95f)};
		case EChaosImpactBallType::Nova:
			return {FLinearColor(0.85f, 0.97f, 1.0f), FLinearColor(0.0f, 0.07f, 0.2f, 0.97f),
				FLinearColor(0.45f, 0.86f, 1.0f), FLinearColor(0.08f, 0.32f, 0.6f, 0.95f),
				FLinearColor(0.9f, 0.98f, 1.0f), FLinearColor(0.82f, 0.95f, 1.0f)};
		case EChaosImpactBallType::Snow:
			return {FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(0.12f, 0.18f, 0.26f, 0.97f),
				FLinearColor(0.88f, 0.95f, 1.0f), FLinearColor(0.45f, 0.58f, 0.72f, 0.95f),
				FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(0.92f, 0.97f, 1.0f)};
		case EChaosImpactBallType::Drive:
			return {FLinearColor(1.0f, 0.9f, 0.55f), FLinearColor(0.24f, 0.11f, 0.0f, 0.97f),
				FLinearColor(1.0f, 0.62f, 0.12f), FLinearColor(0.55f, 0.27f, 0.0f, 0.95f),
				FLinearColor(1.0f, 0.95f, 0.7f), FLinearColor(1.0f, 0.88f, 0.6f)};
		case EChaosImpactBallType::Hook:
			return {FLinearColor(0.8f, 1.0f, 0.96f), FLinearColor(0.0f, 0.14f, 0.12f, 0.97f),
				FLinearColor(0.08f, 0.95f, 0.78f), FLinearColor(0.02f, 0.36f, 0.3f, 0.95f),
				FLinearColor(0.85f, 1.0f, 0.96f), FLinearColor(0.78f, 0.98f, 0.92f)};
		case EChaosImpactBallType::Anchor:
			return {FLinearColor(0.93f, 1.0f, 0.78f), FLinearColor(0.1f, 0.14f, 0.0f, 0.97f),
				FLinearColor(0.66f, 1.0f, 0.06f), FLinearColor(0.26f, 0.4f, 0.0f, 0.95f),
				FLinearColor(0.95f, 1.0f, 0.82f), FLinearColor(0.9f, 1.0f, 0.75f)};
		case EChaosImpactBallType::Simae:
			return {FLinearColor(0.98f, 0.99f, 1.0f), FLinearColor(0.12f, 0.14f, 0.18f, 0.97f),
				FLinearColor(0.92f, 0.96f, 1.0f), FLinearColor(0.03f, 0.035f, 0.045f, 1.0f),
				FLinearColor(1.0f, 0.72f, 0.18f), FLinearColor(0.98f, 0.99f, 1.0f)};
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
			Seam->SetText(FText::FromString(bFilled && Type == EChaosImpactBallType::Simae
				? TEXT("••\n▲") : TEXT("╱")));
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

namespace
{
	/** Per world: the window-wide HUD areas (absolute), and the frame they were painted in. */
	struct FCwSharedHud
	{
		uint64 Frame = 0;
		TArray<FBox2D> Boxes;
	};
	TMap<uint32, FCwSharedHud>& CwSharedHud()
	{
		static TMap<uint32, FCwSharedHud> Shared;
		return Shared;
	}
}

void UChaosImpactChargeWidget::OccupyShared(const FGeometry& Space, const float X, const float Y, const float W, const float H) const
{
	if (!GetWorld())
	{
		return;
	}
	FCwSharedHud& Shared = CwSharedHud().FindOrAdd(GetWorld()->GetUniqueID());
	if (Shared.Frame != GFrameCounter)
	{
		Shared.Frame = GFrameCounter;
		Shared.Boxes.Reset();
	}
	FBox2D Box(ForceInit);
	for (const FVector2f Corner : {FVector2f(X, Y), FVector2f(X + W, Y), FVector2f(X, Y + H), FVector2f(X + W, Y + H)})
	{
		Box += FVector2D(Space.LocalToAbsolute(Corner));
	}
	Shared.Boxes.Add(Box);
}

void UChaosImpactChargeWidget::Occupy(const FGeometry& Space, const float X, const float Y, const float W, const float H) const
{
	FBox2D Box(ForceInit);
	for (const FVector2f Corner : {FVector2f(X, Y), FVector2f(X + W, Y), FVector2f(X, Y + H), FVector2f(X + W, Y + H)})
	{
		Box += FVector2D(HudPaintGeometry.AbsoluteToLocal(Space.LocalToAbsolute(Corner)));
	}
	HudBoxes.Add(Box);
}

void UChaosImpactChargeWidget::PaintCrashMarkers(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	const int32 BaseLayer, const AChaosImpactGameState& Match, const float S) const
{
	using namespace ChaosImpactPaint;
	APlayerController* Owner = GetOwningPlayer();
	const AChaosImpactCrashBall* Ball = Match.GetCrashBall();
	if (!Owner || !Ball)
	{
		return;
	}
	if (!CrashStage.IsValid() || !CrashGoals[0].IsValid() || !CrashGoals[1].IsValid())
	{
		for (TActorIterator<AChaosImpactStageBase> It(GetWorld()); It; ++It)
		{
			if (It->IsCrashBallModeActive()) { CrashStage = *It; }
		}
		for (TActorIterator<AChaosImpactCrashGoal> It(GetWorld()); It; ++It)
		{
			CrashGoals[FMath::Clamp(It->GetTeamIndex(), 0, 1)] = *It;
		}
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FVector2D ViewCenter(Size.X * .5f, Size.Y * .5f);
	const auto* Member = Owner->GetPlayerState<AChaosImpactPlayerState>();
	const int32 Team = Member && !Member->bSpectating && (Member->TeamIndex == 0 || Member->TeamIndex == 1) ? Member->TeamIndex : INDEX_NONE;
	const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
	const auto Project = [Owner](const FVector& World, FVector2D& Out)
	{
		return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Owner, World, Out, true);
	};
	// Its shared scoreboard and gauge across the top.
	HudBoxes.Add(FBox2D(FVector2D(Size.X * 0.5f - 470.0f * S, 0.0), FVector2D(Size.X * 0.5f + 470.0f * S, 135.0f * S)));
	const auto OnScreen = [&](const FVector2D& At, const float Margin)
	{
		// Clear of the shared scoreboard and its gauge at the top.
		return At.X > Margin && At.X < Size.X - Margin && At.Y > 135 * S && At.Y < Size.Y - 70;
	};
	const auto EdgeArrow = [&](const FVector& World, const bool bProjected, const FVector2D& Position, const FLinearColor& Color,
		const TCHAR* Label, const float Scale, const int32 Layer)
	{
		FVector2D Direction = Position - ViewCenter;
		if (!bProjected)
		{
			FVector Camera; FRotator Rotation;
			Owner->GetPlayerViewPoint(Camera, Rotation);
			const FVector Relative = Rotation.UnrotateVector(World - Camera);
			Direction = FVector2D(Relative.Y, -Relative.Z);
		}
		Direction = Direction.GetSafeNormal();
		if (Direction.IsNearlyZero()) { Direction = FVector2D(0, -1); }
		const FVector2D Reach(FMath::Max(30.f, Size.X * .5f - 55 * S), FMath::Max(30.f, Size.Y * .5f - 145 * S));
		double Factor = FMath::Min(Reach.X / FMath::Max(.001, FMath::Abs(Direction.X)), Reach.Y / FMath::Max(.001, FMath::Abs(Direction.Y)));
		// In along the ray until clear of the HUD's own areas (health, balls, hints...).
		const FVector2D Half(40.0 * S * Scale, 34.0 * S * Scale);
		for (const double Least = Factor * 0.35; Factor > Least; Factor -= 6.0)
		{
			const FVector2D Body = ViewCenter + Direction * (Factor - 22.0 * S * Scale);
			const FBox2D Mark(Body - Half, Body + Half);
			bool bClear = true;
			for (const FBox2D& Box : HudBoxesLast) { if (Box.bIsValid && Box.Intersect(Mark)) { bClear = false; break; } }
			if (bClear) { break; }
		}
		const FVector2D Tip = ViewCenter + Direction * Factor;
		const FPainter Marker{AllottedGeometry, OutDrawElements, Layer};
		PaintMarkerArrow(Marker, Tip, Direction, S * Scale, Color);
		const FVector2D LabelAt = Tip - Direction * 34 * S * Scale;
		Marker.Text(Label, LabelAt.X, LabelAt.Y - 9 * S, 15 * S * Scale, Paper, ETextAlign::Center, TEXT("Black"), 2 * S, Ink);
	};

	// The goals: which one to attack and which to defend, over each goal while it is in view. Off screen, only the
	// goal to attack is pointed at, and only once the giant ball is in the opponents' half.
	const float Progress = Match.Crash.Progress;
	for (int32 GoalTeam = 0; GoalTeam < 2; ++GoalTeam)
	{
		const AChaosImpactCrashGoal* Goal = CrashGoals[GoalTeam].Get();
		if (!Goal)
		{
			continue;
		}
		const FVector Top = Goal->GetActorLocation() + FVector(0, 0, Goal->OpeningHeight + 190.0f);
		FVector2D At;
		const bool bProjected = Project(Top, At);
		const bool bAttack = Team != INDEX_NONE && GoalTeam != Team;
		const FLinearColor Color = ChaosImpactMatch::GetTeamColor(GoalTeam);
		if (bProjected && OnScreen(At, 60.0f))
		{
			const FPainter Tag{AllottedGeometry, OutDrawElements, BaseLayer + 3};
			const float Bob = FMath::Sin(Time * 3.0f + GoalTeam) * 4.0f * S;
			const FString Word = Team == INDEX_NONE ? FString::Printf(TEXT("%sゴール"), ChaosImpactMatch::GetTeamName(GoalTeam))
				: bAttack ? FString(TEXT("せめる")) : FString(TEXT("まもる"));
			const float Width = HudMeasure(Word, 18 * S, TEXT("Black")) + 26 * S;
			HudShard(Tag, {At + FVector2D(-Width * .5f, -34 * S + Bob), At + FVector2D(Width * .5f, -34 * S + Bob),
				At + FVector2D(Width * .5f - 6 * S, -6 * S + Bob), At + FVector2D(-Width * .5f - 6 * S, -6 * S + Bob)},
				bAttack ? MakeFill(FMath::Lerp(Color, Paper, 0.3f), Color * FLinearColor(0.55f, 0.55f, 0.55f, 1.0f)) : NightFill, 2.0f * S,
				FVector2D(2, 3) * S, Navy);
			HudAbove(Tag).Text(Word, At.X - 3 * S, At.Y - 33 * S + Bob, 18 * S, Paper, ETextAlign::Center, TEXT("Black"), 2 * S, Ink);
			HudAbove(Tag).Box(At.X - 7 * S, At.Y - 6 * S + Bob, 14 * S, 4 * S, Color);
		}
		else if (bAttack && (GoalTeam == 1 ? Progress > 0.2f : Progress < -0.2f))
		{
			EdgeArrow(Goal->GetActorLocation(), bProjected, At, Color, TEXT("ゴール"), 0.85f, BaseLayer + 3);
		}
	}

	// The giant ball: its name over it, and chevrons on the floor beside it running the way this player's team pushes it.
	const float Radius = Ball->GetBallRadius();
	FVector2D Position;
	const bool bProjected = Project(Ball->GetActorLocation() + FVector(0, 0, Radius + 60), Position);
	const int32 Pusher = Match.Crash.LastPushTeam;
	const FLinearColor BallColor = Pusher == 0 || Pusher == 1 ? FMath::Lerp(ChaosImpactMatch::GetTeamColor(Pusher), Gold, 0.25f) : Gold;
	if (bProjected && OnScreen(Position, 42.0f))
	{
		const FPainter Marker{AllottedGeometry, OutDrawElements, BaseLayer + 4};
		Marker.Text(TEXT("巨大球"), Position.X, Position.Y - 24 * S, 17 * S, Paper, ETextAlign::Center, TEXT("Black"), 2 * S, Ink);
		if (Team != INDEX_NONE && CrashStage.IsValid() && !Ball->IsRecovering())
		{
			const FVector Push = CrashStage->GetCrashBallPushDirection(Team, Ball->GetActorLocation());
			const FVector Floor = Ball->GetActorLocation() - FVector(0, 0, Radius * 0.85f);
			FVector2D From, To;
			if (Project(Floor + Push * (Radius + 40.0f), From) && Project(Floor + Push * (Radius + 190.0f), To))
			{
				const FVector2D Direction = (To - From).GetSafeNormal();
				if (!Direction.IsNearlyZero())
				{
					const FLinearColor Color = ChaosImpactMatch::GetTeamColor(Team);
					for (int32 Chevron = 0; Chevron < 3; ++Chevron)
					{
						const float Phase = FMath::Frac(Time * 1.4f + Chevron / 3.0f);
						PaintMarkerArrow(FPainter{AllottedGeometry, OutDrawElements, BaseLayer + 3, FMath::Sin(Phase * UE_PI)},
							FMath::Lerp(From, To, Phase), Direction, 0.75f * S, Color);
					}
				}
			}
		}
	}
	else
	{
		EdgeArrow(Ball->GetActorLocation(), bProjected, Position, BallColor, TEXT("巨大球"), 1.0f, BaseLayer + 4);
	}
}

int32 UChaosImpactChargeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;
	if (const ULocalPlayer* Single = UChaosImpactGameViewportClient::GetSingleDuelPlayer(GetWorld());
		Single && GetOwningLocalPlayer() != Single)
	{
		// The spectator still owns a Slate layer, but none of its hand-painted personal HUD belongs in the fighter's view.
		return LayerId;
	}

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	// Last frame's HUD areas for the arrows painted first; this frame's are gathered as the pieces paint.
	HudBoxesLast = MoveTemp(HudBoxes);
	HudBoxes.Reset();
	HudPaintGeometry = AllottedGeometry;
	// What is drawn once over the whole window (painted just before or after this view).
	if (const FCwSharedHud* Shared = GetWorld() ? CwSharedHud().Find(GetWorld()->GetUniqueID()) : nullptr; Shared && GFrameCounter - Shared->Frame <= 2)
	{
		for (const FBox2D& Box : Shared->Boxes)
		{
			HudBoxesLast.Add(FBox2D(FVector2D(AllottedGeometry.AbsoluteToLocal(Box.Min)), FVector2D(AllottedGeometry.AbsoluteToLocal(Box.Max))));
		}
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 1.0f || Size.Y < 1.0f)
	{
		return BaseLayer;
	}
	PaintPlayerMarkers(AllottedGeometry, OutDrawElements, BaseLayer + 1);
	PaintCombatHealth(AllottedGeometry, OutDrawElements, BaseLayer + 1);
	// Smoke hides the others' markers too; this player's own HP and balls stay readable on top.
	const bool bSmoked = BlindAmount > 0.0f && !bSpectatorView;
	if (bSmoked)
	{
		PaintSmokeVeil(AllottedGeometry, OutDrawElements, BaseLayer + 4);
	}
	const int32 HudLayer = bSmoked ? BaseLayer + 6 : BaseLayer + 1;
	if (bSpectatorView)
	{
		PaintSpectatorBar(AllottedGeometry, OutDrawElements, HudLayer);
	}
	else
	{
		PaintVitals(AllottedGeometry, OutDrawElements, HudLayer);
		PaintBallInventory(AllottedGeometry, OutDrawElements, HudLayer);
		if (const AChaosImpactPlayerController* Own = Cast<AChaosImpactPlayerController>(GetOwningPlayer()); Own && !bVitalsHidden)
		{
			const float HintScale = GetHudScale(Size.X, Size.Y);
			// The tutorial at the top right, under the corner buttons (lower when the lobby's ready / watch buttons are there).
			const bool bCornerColumn = !Own->IsUsingGamepad() && ChaosImpactSettings::GetPlayerControlScheme(Own) != EChaosImpactControlScheme::Normal
				&& (Own->CanToggleReady() || Own->CanToggleSpectating());
			const float KnockoutFade = 1.0f - 0.8f * FMath::Clamp(1.0f - static_cast<float>(FPlatformTime::Seconds() - KnockoutAt) / 2.2f, 0.0f, 1.0f);
			Tutorial.Paint(AllottedGeometry, OutDrawElements, HudLayer + 20, Own, HintScale, bCornerColumn ? 236.0f : 74.0f, KnockoutFade, &HudBoxes);
			// 訓練場: the lesson's slides, task and count (the first player's view only).
			if (const AChaosImpactDojo* Dojo = Own->IsPrimaryLocalPlayerController() ? AChaosImpactDojo::Find(GetWorld()) : nullptr)
			{
				Dojo->PaintHud(AllottedGeometry, OutDrawElements, HudLayer + 40, Own, HintScale);
			}
			// The button hints down the left side (not while the tutorial is teaching them).
			if (ChaosImpactSettings::IsButtonHintsOn() && !Tutorial.IsShowing() && Own->IsGameplayActive() && !AChaosImpactDojo::IsRecordingRun()
				&& !(Own->GetTournamentWidget() && Own->GetTournamentWidget()->CoversPlay()))
			{
				FChaosImpactTutorial::PaintHints(AllottedGeometry, OutDrawElements, HudLayer + 14, Own, HintScale, &HudBoxes);
			}
		}
		PaintLastHealthWarning(AllottedGeometry, OutDrawElements, HudLayer + 1);
	}
	if (!CwIsTournamentOpening(this))
	{
		PaintGameplayPointer(AllottedGeometry, OutDrawElements, HudLayer + 18);
	}
	if (!bSpectatorView && GetWorld())
	{
		// A boss fight: the boss's own bar across the top.
		AChaosImpactSimaeBoss::PaintHud(GetWorld(), AllottedGeometry, OutDrawElements, HudLayer + 2, GetHudScale(Size.X, Size.Y));
		if (TActorIterator<AChaosImpactSimaeBoss> Boss(GetWorld()); Boss && !Boss->IsDormant())
		{
			Occupy(MakeAnchor(AllottedGeometry, Size.X * 0.5f, 0.0f, GetHudScale(Size.X, Size.Y)), -400.0f, 0.0f, 800.0f, 96.0f);
		}
	}
	const double Clock = FPlatformTime::Seconds();
	const UGameInstance* GameInstance = GetGameInstance();
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerIndex = GameInstance && LocalPlayer
		? FMath::Max(0, GameInstance->GetLocalPlayers().IndexOfByKey(LocalPlayer)) : 0;
	const FLinearColor Accent = PlayerAccents[PlayerIndex % 4];

	// Hit flash: red screen edges that fade quickly.
	if (!bSpectatorView)
	{
		const AChaosImpactCharacter* Player = GetOwningPlayer()
			? Cast<AChaosImpactCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
		float PeckRemaining = 0.0f;
		float CountdownSeconds = ChaosImpactBallTypes::SimaePerchSeconds;
		int32 PecksRemaining = 0;
		if (AChaosImpactSimaeBird::HasPerchedBird(Player, &PeckRemaining, &CountdownSeconds, &PecksRemaining))
		{
			const float S = GetHudScale(Size.X, Size.Y);
			// Under whatever already stands at the top middle (the clock, a duel's or the battle royale's scoreboard, a
			// boss's bar), never on it.
			float TopY = 18.0f * S;
			const float MidLeft = Size.X * 0.5f - 250.0f * S, MidRight = Size.X * 0.5f + 250.0f * S;
			for (const TArray<FBox2D>* Boxes : {&HudBoxesLast, &HudBoxes})
			{
				for (const FBox2D& Box : *Boxes)
				{
					// (Not this card's own area from the frame before.)
					if (Box.bIsValid && !(SimaeCardBox.bIsValid && Box.Min.Equals(SimaeCardBox.Min, 0.5) && Box.Max.Equals(SimaeCardBox.Max, 0.5))
						&& Box.Min.Y < Size.Y * 0.3f && Box.Max.X > MidLeft && Box.Min.X < MidRight)
					{
						TopY = FMath::Max(TopY, static_cast<float>(Box.Max.Y) + 12.0f * S);
					}
				}
			}
			constexpr float W = 470.0f;
			constexpr float H = 86.0f;
			const float Ratio = FMath::Clamp(PeckRemaining / FMath::Max(CountdownSeconds, UE_SMALL_NUMBER), 0.0f, 1.0f);
			const bool bAgain = PecksRemaining < ChaosImpactBallTypes::SimaeMaxPecksPerTarget;
			// Hurrying: it shivers as the peck nears.
			const float Shiver = Ratio < 0.35f ? FMath::Sin(static_cast<float>(Clock) * 48.0f) * (0.35f - Ratio) * 10.0f : 0.0f;
			const FGeometry Top = MakeAnchor(AllottedGeometry, Size.X * 0.5f, TopY, S);
			const FGeometry WarningSpace = MakeSkewed(Top, -W * 0.5f + Shiver, 0.0f, W, H, -0.12f);
			Occupy(Top, -W * 0.5f - 12.0f, -6.0f, W + 24.0f, H + 14.0f);
			SimaeCardBox = HudBoxes.Last();
			const FPainter Warning{WarningSpace, OutDrawElements, HudLayer + 8};
			const FLinearColor CardAccent = bAgain ? Fire : Gold;
			// The plate: night blue, a lit top edge, the accent down its left side.
			HudShard(Warning, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)}, NightFill, 2.5f, FVector2D(6.0, 7.0));
			const FPainter Face = HudAbove(Warning, 2);
			Face.Box(0.0f, 0.0f, W, 2.0f, WithAlpha(Paper, 0.35f));
			HudPoly(Face, {FVector2D(0.0, 0.0), FVector2D(9.0, 0.0), FVector2D(9.0, H), FVector2D(0.0, H)}, bAgain ? FireFill : MakeFill(Gold, FLinearColor(0.85f, 0.45f, 0.05f, 1.0f)));
			// The bird itself: a round white simaenaga, two black eyes and a little beak.
			const FVector2D Bird(52.0, H * 0.5f - 2.0f);
			const float Hop = FMath::Abs(FMath::Sin(static_cast<float>(Clock) * 9.0f)) * 3.0f;
			Face.Disc(Bird + FVector2D(2.0, 3.0), 25.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
			Face.Disc(Bird - FVector2D(0.0, Hop), 24.0f, WithAlpha(CardAccent, 0.9f));
			const FPainter BirdFace = HudAbove(Face, 1);
			BirdFace.Disc(Bird - FVector2D(0.0, Hop), 21.0f, Paper);
			BirdFace.Disc(Bird + FVector2D(-7.0, -3.0 - Hop), 2.6f, Ink);
			BirdFace.Disc(Bird + FVector2D(7.0, -3.0 - Hop), 2.6f, Ink);
			BirdFace.Disc(Bird + FVector2D(0.0, 3.0 - Hop), 2.2f, FLinearColor(0.25f, 0.18f, 0.12f, 1.0f));
			// What is happening, then what to do (the dash key in a cap), or that it is about to peck again.
			const FPainter Words = HudAbove(Face, 2);
			constexpr float TextLeft = 92.0f;
			const float Room = W - TextLeft - 18.0f;
			Words.Text(TEXT("シマエナガにつかまった！"), TextLeft, 7.0f, HudFit(TEXT("シマエナガにつかまった！"), 15.0f, Room, TEXT("Bold")),
				WithAlpha(CardAccent, 0.95f), ETextAlign::Left, TEXT("Bold"));
			if (bAgain)
			{
				HudText3D(Words, TEXT("もう一度つつかれる！"), TextLeft, 27.0f, HudFit(TEXT("もう一度つつかれる！"), 25.0f, Room), Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
			}
			else
			{
				// (The first key set for it: one cap, as the button hints show it.)
				FString DashKey = ChaosImpactSettings::DescribeAction(GetOwningPlayer(), EChaosImpactAction::Dash);
				DashKey.Split(TEXT(" / "), &DashKey, nullptr);
				const float KeySize = HudFit(DashKey, 17.0f, 140.0f);
				const float KeyW = FMath::Max(HudMeasure(DashKey, KeySize, TEXT("Black")) + 16.0f, 34.0f);
				RoundedBox(Words, TextLeft, 29.0f, KeyW, 28.0f, 6.0f, WithAlpha(Paper, 0.92f));
				HudAbove(Words, 1).Text(DashKey, TextLeft + KeyW * 0.5f, 31.0f + (17.0f - KeySize) * 0.6f, KeySize, Ink, ETextAlign::Center, TEXT("Black"));
				const FString Line = TEXT("ダッシュで振り落とせ！");
				HudText3D(Words, Line, TextLeft + KeyW + 8.0f, 27.0f, HudFit(Line, 24.0f, Room - KeyW - 8.0f), Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
			}
			// Time until the peck: a bar in five cells that empties, gold to red, flashing at the end.
			const float BarLeft = TextLeft, BarTop = H - 16.0f, BarW = W - TextLeft - 18.0f;
			const float Flash = Ratio < 0.3f ? 0.6f + 0.4f * FMath::Sin(static_cast<float>(Clock) * 20.0f) : 1.0f;
			for (int32 Cell = 0; Cell < 5; ++Cell)
			{
				const float CellW = (BarW - 4.0f * 4.0f) / 5.0f;
				const float X = BarLeft + Cell * (CellW + 4.0f);
				const float Fill = FMath::Clamp(Ratio * 5.0f - Cell, 0.0f, 1.0f);
				RoundedBox(Words, X, BarTop, CellW, 6.0f, 3.0f, WithAlpha(Paper, 0.12f));
				if (Fill > 0.0f)
				{
					RoundedBox(HudAbove(Words, 1), X, BarTop, CellW * Fill, 6.0f, 3.0f, WithAlpha(FMath::Lerp(Fire, Gold, Ratio), Flash));
				}
			}
		}
	}

	// Sudden death: outside the closing circle, a card like the simaenaga's with a gauge that fills in the moment before
	// it counts (the host judges it; this is how long this screen has seen the player out).
	if (!bSpectatorView)
	{
		const AChaosImpactCharacter* Player = GetOwningPlayer() ? Cast<AChaosImpactCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
		const AChaosImpactSuddenDeathZone* Zone = nullptr;
		if (Player && !Player->IsEliminated())
		{
			float Nearest = TNumericLimits<float>::Max();
			for (TActorIterator<AChaosImpactSuddenDeathZone> It(GetWorld()); It; ++It)
			{
				const float Distance = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Player->GetActorLocation()));
				if (Distance < Nearest && Distance < 6000.0f)
				{
					Nearest = Distance;
					Zone = *It;
				}
			}
		}
		const bool bOut = Zone && !Zone->IsInside(Player->GetActorLocation());
		ZoneOutSince = bOut ? (ZoneOutSince < 0.0 ? Clock : ZoneOutSince) : -1.0;
		if (bOut)
		{
			const float S = GetHudScale(Size.X, Size.Y);
			float TopY = 18.0f * S;
			const float MidLeft = Size.X * 0.5f - 250.0f * S, MidRight = Size.X * 0.5f + 250.0f * S;
			for (const TArray<FBox2D>* Boxes : {&HudBoxesLast, &HudBoxes})
			{
				for (const FBox2D& Box : *Boxes)
				{
					if (Box.bIsValid && !(ZoneCardBox.bIsValid && Box.Min.Equals(ZoneCardBox.Min, 0.5) && Box.Max.Equals(ZoneCardBox.Max, 0.5))
						&& Box.Min.Y < Size.Y * 0.3f && Box.Max.X > MidLeft && Box.Min.X < MidRight)
					{
						TopY = FMath::Max(TopY, static_cast<float>(Box.Max.Y) + 12.0f * S);
					}
				}
			}
			constexpr float W = 470.0f;
			constexpr float H = 86.0f;
			const float Filled = FMath::Clamp(static_cast<float>(Clock - ZoneOutSince) / AChaosImpactSuddenDeathZone::OutSeconds, 0.0f, 1.0f);
			const float Shiver = FMath::Sin(static_cast<float>(Clock) * 48.0f) * Filled * 5.0f;
			const FGeometry Top = MakeAnchor(AllottedGeometry, Size.X * 0.5f, TopY, S);
			const FGeometry CardSpace = MakeSkewed(Top, -W * 0.5f + Shiver, 0.0f, W, H, -0.12f);
			Occupy(Top, -W * 0.5f - 12.0f, -6.0f, W + 24.0f, H + 14.0f);
			ZoneCardBox = HudBoxes.Last();
			const FPainter Card{CardSpace, OutDrawElements, HudLayer + 8};
			HudShard(Card, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)}, NightFill, 2.5f, FVector2D(6.0, 7.0));
			const FPainter Face = HudAbove(Card, 2);
			Face.Box(0.0f, 0.0f, W, 2.0f, WithAlpha(Paper, 0.35f));
			HudPoly(Face, {FVector2D(0.0, 0.0), FVector2D(9.0, 0.0), FVector2D(9.0, H), FVector2D(0.0, H)}, FireFill);
			// A red warning disc with "!" (the circle's colour).
			const FVector2D Mark(52.0, H * 0.5f - 2.0f);
			const float Beat = 1.0f + 0.08f * FMath::Sin(static_cast<float>(Clock) * 14.0f);
			Face.Disc(Mark + FVector2D(2.0, 3.0), 25.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
			Face.Disc(Mark, 24.0f * Beat, Fire);
			const FPainter MarkFace = HudAbove(Face, 1);
			MarkFace.Ring(Mark, 18.0f * Beat, WithAlpha(Paper, 0.9f), 2.5f);
			MarkFace.Text(TEXT("!"), static_cast<float>(Mark.X), static_cast<float>(Mark.Y) - 20.0f, 28.0f, Paper, ETextAlign::Center, TEXT("Black"));
			const FPainter Words = HudAbove(Face, 2);
			constexpr float TextLeft = 92.0f;
			const float Room = W - TextLeft - 18.0f;
			Words.Text(TEXT("エリアの外！"), TextLeft, 7.0f, HudFit(TEXT("エリアの外！"), 15.0f, Room, TEXT("Bold")), WithAlpha(Fire, 0.95f),
				ETextAlign::Left, TEXT("Bold"));
			HudText3D(Words, TEXT("円の中にもどれ！"), TextLeft, 27.0f, HudFit(TEXT("円の中にもどれ！"), 25.0f, Room), Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
			// The moment before it counts: five cells that fill, gold to red, flashing as they fill up.
			const float BarLeft = TextLeft, BarTop = H - 16.0f, BarW = W - TextLeft - 18.0f;
			const float Flash = Filled > 0.6f ? 0.6f + 0.4f * FMath::Sin(static_cast<float>(Clock) * 24.0f) : 1.0f;
			for (int32 Cell = 0; Cell < 5; ++Cell)
			{
				const float CellW = (BarW - 4.0f * 4.0f) / 5.0f;
				const float X = BarLeft + Cell * (CellW + 4.0f);
				const float Fill = FMath::Clamp(Filled * 5.0f - Cell, 0.0f, 1.0f);
				RoundedBox(Words, X, BarTop, CellW, 6.0f, 3.0f, WithAlpha(Paper, 0.12f));
				if (Fill > 0.0f)
				{
					RoundedBox(HudAbove(Words, 1), X, BarTop, CellW * Fill, 6.0f, 3.0f, WithAlpha(FMath::Lerp(Gold, Fire, Filled), Flash));
				}
			}
		}
	}

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

	// Knocked out: a card at the bottom middle, between the health and the balls (clear of both, and of the hints at the
	// sides): a ring that empties as the wait runs out with the count in it, then who did it.
	if (bRespawnVisible)
	{
		const float S = GetHudScale(Size.X, Size.Y);
		const float Age = static_cast<float>(Clock - RespawnShownAt);
		const float In = EaseOut(Age / 0.3f);
		constexpr float W = 430.0f;
		constexpr float H = 92.0f;
		const FGeometry Bottom = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y, S);
		const float Top = -262.0f + (1.0f - In) * 40.0f;
		const FGeometry Card = MakeSkewed(Bottom, -W * 0.5f, Top, W, H, -0.12f);
		Occupy(Bottom, -W * 0.5f - 16.0f, -270.0f, W + 32.0f, H + 16.0f);
		const FPainter Plate{Card, OutDrawElements, BaseLayer + 2, In};
		// The plate: deep night with a lit top edge, a flame of red down its left, a thin ice line under it.
		HudShard(Plate, {FVector2D(0.0, 0.0), FVector2D(W, 0.0), FVector2D(W, H), FVector2D(0.0, H)}, NightFill, 2.5f, FVector2D(6.0, 7.0));
		const FPainter Above = HudAbove(Plate, 2);
		Above.Box(0.0f, 0.0f, W, 2.0f, WithAlpha(Paper, 0.35f));
		HudPoly(Above, {FVector2D(0.0, 0.0), FVector2D(10.0, 0.0), FVector2D(10.0, H), FVector2D(0.0, H)}, FireFill);
		Above.Box(14.0f, H - 3.0f, W - 14.0f, 3.0f, WithAlpha(Ice, 0.9f));

		// The ring: the time left goes round it (red to ice as it runs out), the count large in it, popping as it changes.
		const float Remaining = FMath::Clamp(RespawnRemaining / FMath::Max(RespawnTotal, 0.01f), 0.0f, 1.0f);
		const FVector2D Ring(62.0, H * 0.5f);
		const float Radius = 32.0f;
		const FPainter Dial = HudAbove(Plate, 3);
		Dial.Disc(Ring, Radius + 6.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		Dial.Arc(Ring, Radius, -90.0f, 270.0f, WithAlpha(Paper, 0.12f), 6.0f);
		HudAbove(Dial, 1).Arc(Ring, Radius, -90.0f, -90.0f + 360.0f * Remaining, FMath::Lerp(Ice, Fire, Remaining), 6.0f);
		const float NumberAge = static_cast<float>(Clock - CountdownChangedAt);
		const float Pop = 1.0f + 0.45f * FMath::Exp(-12.0f * NumberAge);
		const FGeometry NumberSpace = MakeSkewed(Card, static_cast<float>(Ring.X) - 40.0f, static_cast<float>(Ring.Y) - 32.0f, 80.0f, 64.0f, 0.0f, Pop);
		HudText3D(FPainter{NumberSpace, OutDrawElements, BaseLayer + 7, In}, FString::FromInt(FMath::Max(1, CountdownNumber)), 40.0f, -2.0f, 48.0f,
			Paper, ETextAlign::Center, TEXT("BlackItalic"), 3.0f);

		// The words: what is happening, and who did it (or that the stage was left).
		const FPainter Words = HudAbove(Plate, 4);
		constexpr float TextLeft = 112.0f;
		const float TextRoom = W - TextLeft - 20.0f;
		HudText3D(Words, TEXT("復活まで"), TextLeft, 10.0f, 26.0f, Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
		const FString Cause = DefeatedByName.IsEmpty() ? FString(TEXT("ステージの外に出た！"))
			: FString::Printf(TEXT("%s にやられた！"), *DefeatedByName);
		Words.Text(Cause, TextLeft + 2.0f, 50.0f, HudFit(Cause, 19.0f, TextRoom, TEXT("Black")), WithAlpha(Fire, 0.95f),
			ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
	}

	// KO banner: two slashes cut across the top-right corner, then the KO stamp lands on them. (A duel calls its
	// knockouts in the middle of the screen instead, under its scoreboard.)
	const float KnockoutAge = static_cast<float>(Clock - KnockoutAt);
	const AChaosImpactGameState* KnockoutMatch = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	if (KnockoutAge < 2.2f && !(KnockoutMatch && (KnockoutMatch->IsDuel() || KnockoutMatch->FindDuelFor(GetOwningPlayerState()))))
	{
		const float S = GetHudScale(Size.X, Size.Y);
		const float Leave = FMath::Clamp((KnockoutAge - 1.8f) / 0.4f, 0.0f, 1.0f);
		const FGeometry TopRight = MakeAnchor(AllottedGeometry, Size.X, -ChaosImpactEase::InOut(Leave) * 60.0f * S, S);
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
		ChipPainter.Text(KnockoutVictim, 26.0f, 5.0f, HudFit(KnockoutVictim, 24.0f, KnockoutStreak >= 2 ? 160.0f : 262.0f), Paper);
		if (KnockoutStreak >= 2)
		{
			ChipPainter.Text(KnockoutStreak == 2 ? FString(TEXT("DOUBLE")) : KnockoutStreak == 3
				? FString(TEXT("TRIPLE")) : FString::Printf(TEXT("×%d"), KnockoutStreak),
				290.0f, 5.0f, 24.0f, Gold, ETextAlign::Right, TEXT("BlackItalic"));
		}
	}
	PaintVersusMatch(AllottedGeometry, OutDrawElements, BaseLayer + 6);
	if (const auto* P = GetOwningPlayer() ? Cast<AChaosImpactCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
		P && P->IsInBallForm())
	{
		const float Scale = GetHudScale(Size.X,Size.Y);
		const bool bCaptured=P->GetBallForm()->IsCaptured();
		const float Left=bCaptured ? P->GetBallForm()->GetFormBall()->GetCaptureSecondsLeft() : 0.f;
		const float Arrival=bCaptured ? EaseOut((ChaosImpactBallTypes::PrisonSeconds-Left)/.22f) : 1.f;
		const FGeometry HintSpace = MakeAnchor(AllottedGeometry, Size.X*.5f,
			Size.Y-(bCaptured ? 252 : 160)*Scale+(1-Arrival)*24*Scale, Scale);
		const FPainter Hint{HintSpace,OutDrawElements,BaseLayer+7,Arrival};
		Occupy(HintSpace,-215,-16,430,110);
		if (bCaptured)
		{
			// The same bevelled ink outlines, warm gradient and extruded numerals as the HP/stock HUD.
			const float PrisonClock=GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const float Urgency=Left<1.f ? .5f+.5f*FMath::Sin(PrisonClock*23.f) : 0.f;
			const FLinearColor Color=FMath::Lerp(FireFill.Middle,Paper,Urgency*.7f);
			const FHudFill Hot=MakeFill(Left<1.f ? FLinearColor(1,.42f,.16f) : FireFill.Top,FireFill.Bottom);
			HudShard(Hint,{FVector2D(-112,3),FVector2D(180,3),FVector2D(165,78),FVector2D(-112,78)},NightFill,2.5f,FVector2D(4,5));
			HudShard(HudAbove(Hint),{FVector2D(-86,3),FVector2D(180,3),FVector2D(178,10),FVector2D(-88,10)},Hot,1.f,FVector2D::ZeroVector);
			const FPainter Front=HudAbove(Hint,5);
			const FVector2D Dial(-122,41);
			Front.Disc(Dial+FVector2D(3,4),46,Extrude);
			Front.Disc(Dial,45,Navy);
			Front.Disc(Dial,39,Ink);
			Front.Ring(Dial,43,WithAlpha(Paper,.6f),1.5f);
			for (int32 I=0;I<40;++I)
			{
				const float Angle=-90+I*9.f;
				const bool Lit=I<Left/ChaosImpactBallTypes::PrisonSeconds*40;
				Front.Arc(Dial,38,Angle,Angle+6.5f,Lit ? Color : WithAlpha(Paper,.12f),Lit ? 4.f+Urgency : 2.f);
			}
			HudText3D(Front,FString::Printf(TEXT("%.1f"),Left),-122,17,32,Paper,ETextAlign::Center,TEXT("BlackItalic"));
			Front.Text(TEXT("秒"),-122,53,12,Color,ETextAlign::Center,TEXT("Bold"));
			HudText3D(Front,TEXT("捕獲中"),-67,17,29,Paper,ETextAlign::Left,TEXT("Black"));
			const FPainter Words=HudAbove(Front,2);
			Words.Text(TEXT("時間切れ / 味方のボールで解除"),-64,56,12,Paper,ETextAlign::Left,TEXT("Bold"),1.f,Navy);
			// Five slanted cells echo the stamina blade; the current cell drains continuously.
			for (int32 I=0;I<5;++I)
			{
				const float X=-63+I*43.f, Fill=FMath::Clamp(Left-I,0.f,1.f);
				const TArray<FVector2D> Cell{FVector2D(X,86),FVector2D(X+38,86),FVector2D(X+33,92),FVector2D(X-5,92)};
				HudShard(Hint,Cell,SolidFill(Navy),1.f,FVector2D(1,2));
				if (Fill>0) { HudPoly(HudAbove(Hint,4),{FVector2D(X,86),FVector2D(X+38*Fill,86),FVector2D(X+38*Fill-5,92),FVector2D(X-5,92)},Hot); }
			}
		}
		const auto Scheme = ChaosImpactSettings::GetPlayerControlScheme(GetOwningPlayer());
		const FString Key = ChaosImpactSettings::DescribeAction(GetOwningPlayer(), Scheme == EChaosImpactControlScheme::Simple ? EChaosImpactAction::SwapBall
			: ChaosImpactSettings::IsMouseOnlyScheme(Scheme) ? EChaosImpactAction::Dash : EChaosImpactAction::DropBall);
		if (!bCaptured)
		{
			Hint.Box(-190,0,380,42,WithAlpha(Ink,.9f));
			Hint.Text(FString::Printf(TEXT("ボール化中　%s で解除"),*Key),0,7,20,Paper,ETextAlign::Center);
		}
	}
	// The online room overlay is rendered once by the full-window announcer, outside split-view clipping.
	PaintSoloClear(AllottedGeometry, OutDrawElements, HudLayer + 28);
	if (const auto* MapOwner = Cast<AChaosImpactPlayerController>(GetOwningPlayer()); MapOwner && MapOwner->IsOverviewMapOpen())
	{
		PaintOverviewMap(AllottedGeometry, OutDrawElements, HudLayer + 46);
	}
	return FMath::Max(BaseLayer + 22, HudLayer + 56);
}

void UChaosImpactChargeWidget::PaintRuleDetails(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, const int32 Layer,
	const AChaosImpactPlayerController* Owner) const
{
	using namespace ChaosImpactPaint;
	const AChaosImpactGameState* Room = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!Room || !Owner || Size.X < 1.0f || Size.Y < 1.0f)
	{
		return;
	}
	const FChaosImpactMatchRules& Rules = Room->Rules;
	const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
	const float In = EaseOut(static_cast<float>(FPlatformTime::Seconds() - Owner->GetRuleCardOpenedAt()) / 0.18f);
	// The view dimmed under it (like the whole-stage map), the card in the middle.
	FPainter{AllottedGeometry, OutDrawElements, Layer}.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.004f, 0.02f, 0.84f * In));
	const float S = FMath::Min(FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f) * ChaosImpactSettings::GetHudSizeFactor(),
		FMath::Min(Size.X / 1180.0f, Size.Y / 720.0f));
	constexpr float CW = 1080.0f;
	constexpr float CH = 640.0f;
	const FGeometry Middle = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.5f, S);
	const FGeometry CardSpace = MakeSkewed(Middle, -CW * 0.5f, -CH * 0.5f + (1.0f - In) * 30.0f, CW, CH, -0.05f);
	const FLinearColor Accent = ChaosImpactRuleCard::GetColor(Rules);
	const FPainter C{CardSpace, OutDrawElements, Layer + 1, In};
	C.Box(14.0f, 16.0f, CW, CH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	FLinearColor Deep = Accent * 0.2f;
	Deep.A = 0.97f;
	HudPoly(C, {FVector2D(0.0, 0.0), FVector2D(CW, 0.0), FVector2D(CW, CH), FVector2D(0.0, CH)}, MakeFill(Deep, FLinearColor(0.008f, 0.01f, 0.025f, 0.98f)));
	const FPainter CU = HudAbove(C, 1);
	HudPoly(CU, {FVector2D(0.0, 0.0), FVector2D(CW, 0.0), FVector2D(CW, 8.0), FVector2D(0.0, 8.0)}, MakeFill(FMath::Lerp(Accent, Paper, 0.35f), Accent));
	CU.Outline(0.0f, 0.0f, CW, CH, WithAlpha(Paper, 0.14f), 1.5f);
	const FPainter CT = HudAbove(C, 3);
	// The kind: in English small, in Japanese large; its emblem on the right.
	CT.Text(ChaosImpactRuleCard::GetEnglish(Rules), 44.0f, 28.0f, 20.0f, Accent, ETextAlign::Left, TEXT("BlackItalic"));
	const FString Kind = ChaosImpactMatch::DescribeRule(Rules).Replace(TEXT("  "), TEXT(" "));
	HudText3D(HudAbove(CT, 1), Kind, 44.0f, 56.0f, HudFit(Kind, 50.0f, CW - 88.0f - 260.0f), Paper, ETextAlign::Left, TEXT("Black"), 3.0f);
	ChaosImpactRuleCard::PaintEmblem(HudAbove(C, 2), Rules, FVector2D(CW - 150.0f, 118.0f), 78.0f, WithAlpha(FMath::Lerp(Accent, Paper, 0.3f), 0.9f), Time);
	// How it goes.
	const int32 Total = Room->CountHumanMembers() + (Rules.bTournament ? Rules.Tournament.CPUs.Num() : Rules.CPUCount);
	const TArray<FString> Points = ChaosImpactRuleCard::GetPoints(Rules, Total);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const float Y = 150.0f + Index * 46.0f;
		CT.Disc(FVector2D(52.0, Y + 16.0), 6.0f, Accent);
		CT.Text(Points[Index], 72.0f, Y, HudFit(Points[Index], 25.0f, CW - 72.0f - 290.0f, TEXT("Bold")), Index == 0 ? Paper : WithAlpha(Paper, 0.85f),
			ETextAlign::Left, TEXT("Bold"));
	}
	CT.Box(44.0f, 300.0f, CW - 88.0f, 1.0f, WithAlpha(Paper, 0.14f));
	// The settings, two columns.
	const bool bDuelRules = Rules.bTournament || Rules.bDuel;
	const int32 CPULevel = Rules.bTournament ? (Rules.Tournament.CPUs.IsEmpty() ? INDEX_NONE : Rules.Tournament.CPUs[0].Level) : Rules.CPULevel;
	const int32 CPUs = Rules.bTournament ? Rules.Tournament.CPUs.Num() : Rules.CPUCount;
	const FString Length = Rules.bTournament
		? (Rules.Tournament.Mode == EChaosImpactTournamentMode::Simultaneous ? FString(TEXT("同時進行")) : FString(TEXT("1試合ずつ")))
		: Rules.bDuel ? FString::Printf(TEXT("1ラウンド%d秒・%d本先取"), FMath::RoundToInt(ChaosImpactMatch::DuelRoundSeconds), ChaosImpactMatch::DuelRoundsToWin)
		: FString::Printf(TEXT("%d分"), Rules.Minutes);
	const TPair<FString, FString> Items[] = {
		{TEXT("ステージ"), bDuelRules ? FString(ChaosImpactMatch::GetDuelStageName()) : ChaosImpactMatch::GetStageLabel(Rules.StageIndex)},
		{Rules.bTournament ? TEXT("進め方") : TEXT("試合時間"), Length},
		{TEXT("CPU"), CPUs > 0 ? FString::Printf(TEXT("%d人（%s）"), CPUs, CPULevel < 0 ? TEXT("おまかせ") : ChaosImpactMatch::GetCPULevelName(CPULevel))
			: FString(TEXT("なし"))},
		{TEXT("チーム"), Rules.IsTeamBattle() ? FString::Printf(TEXT("%dチーム"), Rules.TeamCount) : FString(TEXT("個人戦"))}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Items); ++Index)
	{
		const float X = Index % 2 == 0 ? 44.0f : CW * 0.5f + 10.0f;
		const float Y = 314.0f + (Index / 2) * 72.0f;
		CT.Text(Items[Index].Key, X, Y, 17.0f, Muted, ETextAlign::Left, TEXT("Bold"));
		HudText3D(HudAbove(CT, 1), Items[Index].Value, X, Y + 24.0f, HudFit(Items[Index].Value, 28.0f, CW * 0.5f - 64.0f), Paper, ETextAlign::Left,
			TEXT("Black"), 2.0f);
	}
	// ボールスイッチ: every ball, the ones switched off struck through.
	const TArray<EChaosImpactBallType> Kinds = ChaosImpactBallSwitch::GetSwitchable();
	const int32 On = ChaosImpactBallSwitch::CountOn(Rules);
	CT.Box(44.0f, 462.0f, CW - 88.0f, 1.0f, WithAlpha(Paper, 0.14f));
	CT.Text(TEXT("ボールスイッチ"), 44.0f, 474.0f, 17.0f, Muted, ETextAlign::Left, TEXT("Bold"));
	CT.Text(FString::Printf(TEXT("オン %d / %d"), On, Kinds.Num()), CW - 44.0f, 474.0f, 17.0f, On == Kinds.Num() ? Paper : Gold, ETextAlign::Right, TEXT("Black"));
	const float Step = FMath::Min(60.0f, (CW - 88.0f) / FMath::Max(Kinds.Num(), 1));
	for (int32 Index = 0; Index < Kinds.Num(); ++Index)
	{
		const bool bOn = ChaosImpactBallSwitch::IsOn(Rules, Kinds[Index]);
		const FVector2D At(44.0f + Step * (Index + 0.5f), 532.0f);
		const FPainter Icon{CardSpace, OutDrawElements, Layer + 6, In * (bOn ? 1.0f : 0.35f)};
		PaintBallIcon(Icon, At, FMath::Min(20.0f, Step * 0.36f), true, Kinds[Index], 0.0f, Time);
		if (!bOn)
		{
			HudAbove(CT, 4).Line(At + FVector2D(-18.0, 18.0), At + FVector2D(18.0, -18.0), FLinearColor(0.95f, 0.2f, 0.25f, 0.9f), 4.0f);
		}
	}
	// How to close it.
	const FString CloseKey = ChaosImpactSettings::DescribeAction(Owner, EChaosImpactAction::RuleCard);
	const float KeyW = FMath::Max(HudMeasure(CloseKey, 17.0f, TEXT("Black")) + 18.0f, 32.0f);
	const float CloseW = HudMeasure(TEXT("とじる"), 18.0f, TEXT("Bold"));
	RoundedBox(CT, CW - 44.0f - CloseW - 10.0f - KeyW, CH - 52.0f, KeyW, 28.0f, 7.0f, WithAlpha(Paper, 0.9f));
	HudAbove(CT, 1).Text(CloseKey, CW - 44.0f - CloseW - 10.0f - KeyW * 0.5f, CH - 49.0f, 17.0f, Ink, ETextAlign::Center, TEXT("Black"));
	CT.Text(TEXT("とじる"), CW - 44.0f, CH - 49.0f, 18.0f, Paper, ETextAlign::Right, TEXT("Bold"));
	const FString Note = TEXT("このルールで次の試合が始まります");
	CT.Text(Note, 44.0f, CH - 49.0f, HudFit(Note, 17.0f, CW - 88.0f - CloseW - KeyW - 40.0f, TEXT("Bold")), Muted, ETextAlign::Left, TEXT("Bold"));
}

bool UChaosImpactChargeWidget::FindMapArea(FVector& OutCenter, float& OutHalf) const
{
	const UWorld* World = GetWorld();
	const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
	if (!Match)
	{
		return false;
	}
	// A tournament: the arena this player is playing in.
	const APlayerState* Mine = GetOwningPlayerState();
	for (const FChaosImpactTournamentArena& Arena : Match->TournamentArenas)
	{
		if (Mine && Arena.Duel.SideOf(Mine) != INDEX_NONE)
		{
			OutCenter = Arena.Center;
			OutHalf = Arena.HalfExtent;
			return true;
		}
	}
	if (Match->bVersusMatch)
	{
		OutCenter = Match->StageCenter;
		OutHalf = Match->StageHalfExtent;
		return true;
	}
	// Training or a room's lobby: the stage standing in the level, else the ground round this player.
	for (TActorIterator<AChaosImpactStageBase> It(World); It; ++It)
	{
		OutCenter = It->GetCenter();
		OutHalf = It->GetHalfExtent();
		return true;
	}
	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		OutCenter = Pawn->GetActorLocation();
		OutHalf = 3000.0f;
		return true;
	}
	return false;
}

void UChaosImpactChargeWidget::TakeMapPicture(const FVector& Center, const float Half, const float Yaw)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!MapTarget)
	{
		MapTarget = NewObject<UTextureRenderTarget2D>(this);
		MapTarget->RenderTargetFormat = RTF_RGBA8;
		MapTarget->ClearColor = FLinearColor(0.02f, 0.03f, 0.06f, 1.0f);
		MapTarget->InitAutoFormat(1024, 1024);
		MapTarget->UpdateResourceImmediate(true);
	}
	if (!IsValid(MapCamera))
	{
		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags |= RF_Transient;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MapCamera = World->SpawnActor<ASceneCapture2D>(Center, FRotator(-90.0f, Yaw, 0.0f), Parameters);
		if (!MapCamera)
		{
			return;
		}
		USceneCaptureComponent2D* Setup = MapCamera->GetCaptureComponent2D();
		Setup->ProjectionType = ECameraProjectionMode::Orthographic;
		Setup->TextureTarget = MapTarget;
		// The stage's own colours, flat and clear (no light or fog to darken it): a map, not a photo.
		Setup->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
		Setup->bCaptureEveryFrame = false;
		Setup->bCaptureOnMovement = false;
		MapHiddenAt = -100.0;
	}
	USceneCaptureComponent2D* Capture = MapCamera->GetCaptureComponent2D();
	// Straight down from high above, turned with the player's view so the map's up is the screen's up.
	MapCamera->SetActorLocationAndRotation(Center + FVector(0.0f, 0.0f, Half * 2.0f + 4000.0f), FRotator(-90.0f, Yaw, 0.0f));
	Capture->OrthoWidth = Half * 2.0f * 1.08f;
	// Nobody and no ball in the picture: they are drawn over it as they move (the list kept up as balls come and go).
	if (const double Now = FPlatformTime::Seconds(); Now - MapHiddenAt > 0.5)
	{
		MapHiddenAt = Now;
		Capture->HiddenActors.Reset();
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			Capture->HiddenActors.Add(*It);
		}
		for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
		{
			Capture->HiddenActors.Add(*It);
		}
	}
	Capture->CaptureScene();
	MapBrush.SetResourceObject(MapTarget);
	MapBrush.ImageSize = FVector2D(1024.0, 1024.0);
	MapBrush.DrawAs = ESlateBrushDrawType::Image;
	MapCenter = Center;
	MapHalf = Half;
	MapYaw = Yaw;
	bMapTaken = true;
}

void UChaosImpactChargeWidget::PaintOverviewMap(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	UWorld* World = GetWorld();
	if (!bMapTaken || !MapTarget || !World)
	{
		return;
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float S = GetHudScale(Size.X, Size.Y);
	const double Clock = FPlatformTime::Seconds();
	const float In = EaseOut(static_cast<float>(Clock - MapOpenedAt) / 0.18f);
	// A dark veil over the play (it shows through), the map square in the middle.
	FPainter{AllottedGeometry, OutDrawElements, Layer, In}.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.004f, 0.008f, 0.02f, 0.62f));
	// Under whatever stands at the top middle (the clock, a duel's scoreboard), with room for its title over it.
	float TopY = 0.0f;
	for (const FBox2D& Box : HudBoxesLast)
	{
		// (Only what hangs from the top edge: the clock, a duel's scoreboard; not cards that come and go.)
		if (Box.bIsValid && Box.Min.Y <= 4.0 && Box.Max.Y < Size.Y * 0.3f && Box.Max.X > Size.X * 0.3f && Box.Min.X < Size.X * 0.7f)
		{
			TopY = FMath::Max(TopY, static_cast<float>(Box.Max.Y));
		}
	}
	const float Top = TopY + 50.0f * S;
	const float Side = FMath::Max(120.0f, FMath::Min(Size.X * 0.9f, Size.Y - Top - 40.0f * S));
	const FVector2D Origin((Size.X - Side) * 0.5f, Top + (1.0f - In) * 18.0f * S);
	// (The picture's own alpha is not kept by the capture: drawn as solid colour, faded by the tint.)
	FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(Side, Side),
		FSlateLayoutTransform(FVector2f(Origin))), &MapBrush, ESlateDrawEffect::IgnoreTextureAlpha, FLinearColor(1.0f, 1.0f, 1.0f, 0.86f * In));
	const FPainter Frame{AllottedGeometry, OutDrawElements, Layer + 2, In};
	Frame.Outline(static_cast<float>(Origin.X) - 3.0f * S, static_cast<float>(Origin.Y) - 3.0f * S, Side + 6.0f * S, Side + 6.0f * S,
		WithAlpha(Ice, 0.85f), 2.5f * S);
	// Its title over its top left, and how to close it under it.
	const UWorld* MapWorld = World;
	const AChaosImpactGameState* Match = MapWorld->GetGameState<AChaosImpactGameState>();
	const FString Title = Match && (Match->IsDuel() || Match->IsTournament()) ? FString(ChaosImpactMatch::GetDuelStageName())
		: Match && Match->bVersusMatch ? ChaosImpactMatch::GetStageLabel(Match->Rules.StageIndex) : FString(TEXT("マップ"));
	HudText3D(Frame, TEXT("MAP"), static_cast<float>(Origin.X), static_cast<float>(Origin.Y) - 44.0f * S, 30.0f * S, Ice, ETextAlign::Left,
		TEXT("BlackItalic"), 2.0f * S);
	Frame.Text(Title, static_cast<float>(Origin.X) + 92.0f * S, static_cast<float>(Origin.Y) - 36.0f * S,
		HudFit(Title, 20.0f * S, Side - 100.0f * S, TEXT("Black")), Paper, ETextAlign::Left, TEXT("Black"), 2.0f * S, Ink);
	FString CloseKey = ChaosImpactSettings::DescribeAction(GetOwningPlayer(), EChaosImpactAction::OverviewMap);
	CloseKey.Split(TEXT(" / "), &CloseKey, nullptr);
	const FString Close = FString::Printf(TEXT("%s で閉じる"), *CloseKey);
	Frame.Text(Close, static_cast<float>(Origin.X + Side), static_cast<float>(Origin.Y + Side) + 8.0f * S, 16.0f * S,
		WithAlpha(Paper, 0.85f), ETextAlign::Right, TEXT("Bold"), 1.5f * S, Ink);

	// Where a place in the world falls on the square (its up is the player's view's forward, as the picture was taken).
	const float Width = MapHalf * 2.0f * 1.08f;
	const float YawRad = FMath::DegreesToRadians(MapYaw);
	const float Cos = FMath::Cos(YawRad), Sin = FMath::Sin(YawRad);
	const auto ToMap = [&](const FVector& Where, bool& bInside)
	{
		const FVector Rel = Where - MapCenter;
		const float Forward = Rel.X * Cos + Rel.Y * Sin;
		const float Right = -Rel.X * Sin + Rel.Y * Cos;
		const FVector2D UV(0.5f + Right / Width, 0.5f - Forward / Width);
		bInside = UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0;
		return Origin + UV * Side;
	};
	const auto Heading = [&](const FVector& Direction)
	{
		const float Forward = Direction.X * Cos + Direction.Y * Sin;
		const float Right = -Direction.X * Sin + Direction.Y * Cos;
		return FVector2D(Right, -Forward).GetSafeNormal();
	};
	const FPainter Marks{AllottedGeometry, OutDrawElements, Layer + 4, In};
	// The balls lying about (their kind's colour) and those in the air (small and white).
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		const AChaosImpactBall* Ball = *It;
		bool bInside = false;
		const FVector2D At = ToMap(Ball->GetActorLocation(), bInside);
		if (!bInside || Ball->IsHidden() || Ball->GetAttachParentActor())
		{
			continue;
		}
		if (Ball->IsPickup())
		{
			Marks.Disc(At, 6.5f * S, WithAlpha(Ink, 0.9f));
			HudAbove(Marks, 1).Disc(At, 4.8f * S, ChaosImpactBallTypes::GetColor(Ball->GetBallType()));
		}
		else
		{
			HudAbove(Marks, 1).Disc(At, 3.5f * S, WithAlpha(Paper, 0.9f));
		}
	}
	// クラッシュボール: the goals (their team's colour) and the giant ball.
	for (TActorIterator<AChaosImpactCrashGoal> It(World); It; ++It)
	{
		bool bInside = false;
		const FVector2D At = ToMap(It->GetActorLocation(), bInside);
		if (!bInside) { continue; }
		const FLinearColor GoalColor = ChaosImpactMatch::GetTeamColor(It->GetTeamIndex());
		RoundedBox(Marks, static_cast<float>(At.X) - 16.0f * S, static_cast<float>(At.Y) - 16.0f * S, 32.0f * S, 32.0f * S, 6.0f * S, WithAlpha(Ink, 0.9f));
		RoundedBox(HudAbove(Marks, 1), static_cast<float>(At.X) - 13.0f * S, static_cast<float>(At.Y) - 13.0f * S, 26.0f * S, 26.0f * S, 5.0f * S, GoalColor);
		HudAbove(Marks, 2).Text(TEXT("ゴール"), static_cast<float>(At.X), static_cast<float>(At.Y) - 36.0f * S, 13.0f * S, GoalColor,
			ETextAlign::Center, TEXT("Black"), 2.0f * S, Ink);
	}
	if (const AChaosImpactCrashBall* Giant = Match ? Match->GetCrashBall() : nullptr)
	{
		bool bInside = false;
		const FVector2D At = ToMap(Giant->GetActorLocation(), bInside);
		if (bInside)
		{
			Marks.Disc(At, 17.0f * S, WithAlpha(Ink, 0.95f));
			HudAbove(Marks, 1).Disc(At, 14.0f * S, Gold);
			HudAbove(Marks, 2).Text(TEXT("巨大球"), static_cast<float>(At.X), static_cast<float>(At.Y) - 40.0f * S, 13.0f * S, Gold,
				ETextAlign::Center, TEXT("Black"), 2.0f * S, Ink);
		}
	}
	// Everyone: this player an arrow the way they face, the others a disc of their side's colour with their name.
	const UGameInstance* GameInstance = GetGameInstance();
	const APawn* Me = GetOwningPlayerPawn();
	for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
	{
		const AChaosImpactCharacter* Character = *It;
		if (!IsValid(Character) || Character->IsEliminated() || Character->IsHidden())
		{
			continue;
		}
		bool bInside = false;
		const FVector2D At = ToMap(Character->GetActorLocation(), bInside);
		if (!bInside)
		{
			continue;
		}
		FLinearColor Color = Character->GetCPUNumber() > 0 ? FLinearColor(0.74f, 0.78f, 0.86f, 1.0f) : Gold;
		if (const APlayerController* Controller = Cast<APlayerController>(Character->GetController());
			Controller && Controller->IsLocalController() && Controller->GetLocalPlayer() && GameInstance)
		{
			Color = PlayerAccents[FMath::Max(GameInstance->GetLocalPlayers().IndexOfByKey(Controller->GetLocalPlayer()), 0) % 4];
		}
		if (const AChaosImpactPlayerState* State = Character->GetPlayerState<AChaosImpactPlayerState>();
			Match && Match->IsTeamBattle() && State && State->TeamIndex >= 0)
		{
			Color = ChaosImpactMatch::GetTeamColor(State->TeamIndex);
		}
		if (Character == Me)
		{
			// A ring that breathes, and the arrow.
			const float Breath = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Clock) * 5.0f);
			Marks.Ring(At, (16.0f + 4.0f * Breath) * S, WithAlpha(Color, 0.5f + 0.3f * Breath), 2.5f * S);
			PaintMarkerArrow(HudAbove(Marks, 2), At + Heading(Character->GetActorForwardVector()) * 14.0f * S,
				Heading(Character->GetActorForwardVector()), 0.75f * S, Color);
			HudAbove(Marks, 3).Text(TEXT("YOU"), static_cast<float>(At.X), static_cast<float>(At.Y) - 40.0f * S, 13.0f * S, Color,
				ETextAlign::Center, TEXT("BlackItalic"), 2.0f * S, Ink);
			continue;
		}
		Marks.Disc(At, 10.0f * S, WithAlpha(Ink, 0.95f));
		HudAbove(Marks, 1).Disc(At, 7.5f * S, Color);
		const FString Name = Character->GetOverheadDisplayName();
		HudAbove(Marks, 2).Text(Name, static_cast<float>(At.X), static_cast<float>(At.Y) - 30.0f * S, HudFit(Name, 13.0f * S, 120.0f * S, TEXT("Bold")),
			Color, ETextAlign::Center, TEXT("Bold"), 2.0f * S, Ink);
	}
	// In smoke the map is clouded over too (as thick as the view is): its picture and everyone on it hidden.
	if (const float Thick = GetSmokeCover(); Thick > 0.0f)
	{
		OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(FVector2f(Origin)))));
		const FPainter Fog{AllottedGeometry, OutDrawElements, Layer + 10, Thick * In};
		Fog.Box(static_cast<float>(Origin.X), static_cast<float>(Origin.Y), Side, Side, FLinearColor(0.3f, 0.29f, 0.34f, 0.9f));
		const FPainter Clouds = HudAbove(Fog, 1);
		const float Time = static_cast<float>(FMath::Fmod(Clock, 1000.0));
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const float Seed = Index * 12.9898f;
			const float BaseX = FMath::Frac(FMath::Sin(Seed) * 43758.55f);
			const float BaseY = FMath::Frac(FMath::Sin(Seed + 3.1f) * 24634.63f);
			const FVector2D Center = Origin + FVector2D(BaseX + 0.07f * FMath::Sin(Time * 0.3f + Index), BaseY + 0.06f * FMath::Cos(Time * 0.25f + Index * 1.7f)) * Side;
			const float Radius = Side * (0.2f + 0.12f * FMath::Frac(FMath::Sin(Seed + 7.7f) * 9123.1f));
			const FLinearColor Tone = Index % 3 == 0 ? FLinearColor(0.2f, 0.19f, 0.23f) : FLinearColor(0.55f, 0.53f, 0.6f);
			for (int32 Ring = 0; Ring < 3; ++Ring)
			{
				Clouds.Disc(Center, Radius * (1.0f - Ring * 0.25f), FLinearColor(Tone.R, Tone.G, Tone.B, 0.2f));
			}
		}
		OutDrawElements.PopClip();
	}
}

void UChaosImpactChargeWidget::PaintSoloClear(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (!PC || !PC->IsSoloMode() || PC->GetSoloClearedAt() < 0.0)
	{
		return;
	}
	const float Age = static_cast<float>(FPlatformTime::Seconds() - PC->GetSoloClearedAt());
	const float Total = AChaosImpactPlayerController::SoloClearSeconds;
	if (Age < 0.0f || Age > Total + 0.5f)
	{
		return;
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float S = FMath::Clamp(FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f), 0.42f, 1.4f);
	const FGeometry Center = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.4f, S);
	// Everything leaves together in the last half second (the loading screen takes over).
	const float Leave = 1.0f - ChaosImpactEase::InOut(FMath::Clamp((Age - (Total - 0.5f)) / 0.5f, 0.0f, 1.0f));
	// The flash of the moment.
	if (Age < 0.4f)
	{
		FPainter{AllottedGeometry, OutDrawElements, BaseLayer}.Box(0.0f, 0.0f, Size.X, Size.Y, WithAlpha(Paper, 0.5f * (1.0f - Age / 0.4f)));
	}
	// A slanted band sweeps in behind the words (gold above, fire below), thin enough to leave the field in view.
	const float BandIn = ChaosImpactEase::Out(FMath::Clamp((Age - 0.05f) / 0.35f, 0.0f, 1.0f));
	const FGeometry Band = MakeSkewed(Center, -1300.0f + (1.0f - BandIn) * -1100.0f, -86.0f, 2600.0f, 172.0f, -0.18f);
	const FPainter BandPainter{Band, OutDrawElements, BaseLayer + 1, Leave};
	BandPainter.Box(0.0f, 0.0f, 2600.0f, 172.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.72f));
	BandPainter.Box(0.0f, 0.0f, 2600.0f, 7.0f, Gold);
	BandPainter.Box(0.0f, 165.0f, 2600.0f, 7.0f, Fire);
	// STAGE CLEAR! slams in out of a golden burst, lands a little large and settles; a light sweeps across it.
	const float WordAge = Age - 0.12f;
	if (WordAge > 0.0f)
	{
		const float TextSize = 118.0f;
		const float Scale = 1.0f + 0.9f * FMath::Exp(-9.0f * WordAge);
		const float Alpha = ChaosImpactEase::Out(FMath::Clamp(WordAge / 0.1f, 0.0f, 1.0f)) * Leave;
		const FString Word = TEXT("STAGE CLEAR!");
		const FHudFill Blast = MakeFill(FLinearColor(1.0f, 0.93f, 0.55f, 1.0f), FLinearColor(0.85f, 0.38f, 0.02f, 1.0f));
		const float Open = FMath::Clamp(Alpha * 1.3f, 0.0f, 1.0f) * Scale;
		const float Outer = TextSize * 1.05f * Open;
		const float HalfWidth = (HudMeasure(Word, TextSize, TEXT("BlackItalic")) * 0.5f + TextSize * 0.6f) * Open;
		HudBurst(FPainter{Center, OutDrawElements, BaseLayer + 2, Alpha * FMath::Clamp(1.4f - WordAge, 0.35f, 1.0f)},
			FVector2D(0.0, -TextSize * 0.1f), TextSize * 0.62f * Open, Outer, 18, Blast, -6.0f,
			FVector2D(HalfWidth / FMath::Max(Outer, 1.0f), 0.85), 6.0f, FVector2D(9.0, 10.0), TextSize);
		const FGeometry Space = MakeSkewed(Center, -800.0f, -TextSize * 0.8f, 1600.0f, TextSize * 1.6f, -0.12f, Scale);
		const float Shine = FMath::Frac(Age * 0.45f);
		const FLinearColor Color = FMath::Lerp(Paper, Gold, 0.25f + 0.25f * FMath::Sin(Shine * UE_TWO_PI));
		HudText3D(FPainter{Space, OutDrawElements, BaseLayer + 5, Alpha}, Word, 800.0f, 0.0f, TextSize, Color, ETextAlign::Center,
			TEXT("BlackItalic"), TextSize * 0.07f);
	}
	// The stage's name, in under the words.
	const float NameIn = ChaosImpactEase::Out(FMath::Clamp((Age - 0.55f) / 0.3f, 0.0f, 1.0f));
	if (NameIn > 0.0f)
	{
		FString Name;
		if (const FChaosImpactSoloStageInfo* Info = ChaosImpactSolo::FindStage(PC->GetCurrentSoloStage()))
		{
			Name = FString::Printf(TEXT("ステージ %d　%s"), Info->Number, Info->Name);
		}
		const FGeometry Line = MakeSkewed(Center, -600.0f + (1.0f - NameIn) * 60.0f, 92.0f, 1200.0f, 40.0f, -0.12f);
		const FPainter Words{Line, OutDrawElements, BaseLayer + 8, NameIn * Leave};
		Words.Text(Name, 600.0f, 0.0f, 30.0f, Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
	}
	// The way back to the map: a short gauge filling.
	const float GaugeIn = ChaosImpactEase::Out(FMath::Clamp((Age - 1.2f) / 0.3f, 0.0f, 1.0f));
	if (GaugeIn > 0.0f)
	{
		const FGeometry Bottom = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.78f, S);
		const FPainter Gauge{Bottom, OutDrawElements, BaseLayer + 8, GaugeIn * Leave};
		Gauge.Text(TEXT("マップにもどります"), 0.0f, -34.0f, 20.0f, Paper, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
		const float Fill = FMath::Clamp((Age - 1.2f) / (Total - 1.4f), 0.0f, 1.0f);
		const TArray<FVector2D> Track{FVector2D(-160, 0), FVector2D(166, 0), FVector2D(160, 9), FVector2D(-166, 9)};
		HudShard(Gauge, Track, SolidFill(Navy), 1.5f, FVector2D(2, 3));
		HudPoly(HudAbove(Gauge, 3), {FVector2D(-160, 0), FVector2D(-160 + 326 * Fill, 0), FVector2D(-166 + 326 * Fill, 9), FVector2D(-166, 9)},
			MakeFill(Gold, Fire));
	}
}

namespace
{
	/** The ball holder: where each ball sits (from the bottom-right corner) and its pedestal's radius. */
	const FVector2f BallHolderHomes[] = {FVector2f(-122.0f, -104.0f), FVector2f(-218.0f, -72.0f)};
	constexpr float BallHolderRings[] = {50.0f, 32.0f};
	constexpr float BallHolderBalls[] = {33.0f, 21.0f};

	FBox2D EasyButtonRect(const FVector2D& Size, const float S, const int32 Hit)
	{
		const FVector2D Position = Hit == 1 ? FVector2D(Size.X - 140.0f * S, 18.0f * S)
			: Hit == 2 ? FVector2D(Size.X - 300.0f * S, 18.0f * S)
			: Hit >= 4 ? FVector2D(Size.X - 140.0f * S, (70.0f + (Hit - 4) * 52.0f) * S)
			: FVector2D(Size.X - 150.0f * S, Size.Y - 290.0f * S);
		return FBox2D(Position, Position + FVector2D(Hit == 2 ? 148.0f : 120.0f, 44.0f) * S);
	}
}

int32 UChaosImpactChargeWidget::GetGameplayPointerHit() const
{
	if (bReplacedByOwnHud) { return 0; }
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const auto* Character = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
	if (Character && Character->IsShoulderViewActive()) { return 0; }
	if (!PC || !PC->IsGameplayActive() || PC->IsUsingGamepad()
		|| ChaosImpactSettings::GetPlayerControlScheme(PC) == EChaosImpactControlScheme::Normal
		|| !FSlateApplication::IsInitialized()) { return 0; }
	const FGeometry& Geometry = GetCachedGeometry();
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Pointer = Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const float S = GetHudScale(Size.X, Size.Y);
	if (Size.X < 1 || Size.Y < 1 || !FBox2D(FVector2D::ZeroVector, Size).IsInside(Pointer)) { return 0; }
	for (int32 Hit = 1; Hit <= 6; ++Hit)
	{
		if (Hit == 2 && (!PC->IsTrainingMode() || !PC->IsPrimaryLocalPlayerController() || PC->IsOnlineRoom() || PC->IsVersusMatchWorld() || CwIsTournamentWorld(this) || PC->IsDojo())) { continue; }
		if (Hit == 4 && !PC->CanToggleReady()) { continue; }
		if (Hit == 5 && !PC->CanToggleSpectating()) { continue; }
		if (Hit == 6 && (!Character || Character->GetBallForm()->IsCaptured() || !Character->GetBallForm()->IsAvailable())) { continue; }
		if (EasyButtonRect(Size, S, Hit).IsInside(Pointer)) { return Hit; }
	}
	// The inventory remains UI while the ball icons animate; never let these clicks leak into throwing.
	if (!bVitalsHidden && !bSpectatorView)
	{
		const FVector2D Homes[] = {FVector2D(BallHolderHomes[0]), FVector2D(BallHolderHomes[1])};
		for (int32 BallIndex = 0; BallIndex < 2; ++BallIndex)
		{
			if (FVector2D::DistSquared(Pointer, Size + Homes[BallIndex] * S) <= FMath::Square(BallHolderRings[BallIndex] * S))
			{ return 10 + BallIndex; }
		}
		// Empty pedestals and the count are still HUD, not places to throw a ball through.
		if (FBox2D(Size + FVector2D(-262.0f, -176.0f) * S, Size + FVector2D(-40.0f, -36.0f) * S).IsInside(Pointer))
		{ return 12; }
	}
	return 0;
}

void UChaosImpactChargeWidget::PaintGameplayPointer(const FGeometry& Geometry,
	FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	if (!PC || !PC->IsGameplayActive()) { return; }
	const FVector2D Size = Geometry.GetLocalSize();
	const float S = GetHudScale(Size.X, Size.Y);
	const auto* ShoulderCharacter = Cast<AChaosImpactCharacter>(PC->GetPawn());
	if (ShoulderCharacter && ShoulderCharacter->IsShoulderViewActive())
	{
		const FPainter Paint{Geometry, Elements, Layer};
		// A near wall hit + shoulder offset produced huge lateral parallax. Character aim also
		// belongs to a different tick than the rendered camera. Use the actual rendered view,
		// fixed depth and flat throw plane for a stable direction marker, not a hit guarantee.
		FVector ViewLocation; FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation,ViewRotation);
		const FVector End = UChaosImpactShoulderCameraComponent::MakeReticleAnchor(
			ViewLocation,ViewRotation,ShoulderCharacter->GetActorLocation().Z + 54);
		FVector2D At;
		if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),End,At,true))
		{
			At.X = Size.X * .5;
			At.Y = FMath::Clamp(At.Y, 24.0, Size.Y - 24.0);
			Paint.Ring(At, 8 * S, Ink, 5 * S);
			Paint.Ring(At, 8 * S, Paper, 2 * S);
			Paint.Line(At - FVector2D(13 * S,0), At + FVector2D(13 * S,0), Ink, 5 * S);
			Paint.Line(At - FVector2D(13 * S,0), At + FVector2D(13 * S,0), Paper, 2 * S);
			if (bPointerCharging) { Paint.Arc(At, 13 * S,-90,-90 + 360 * PointerChargeAlpha, Gold, 3 * S); }
		}
		// A separate ground marker previews first stage contact, not a false pitch-directed crosshair.
		FVector Ground; float Radius = 0;
		if (ShoulderCharacter->IsChargingThrow() && ShoulderCharacter->PredictThrowLanding(ShoulderCharacter->GetThrowChargeAlpha(),Ground,Radius)
			&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Ground + FVector(0,0,4),At,true)
			&& FBox2D(FVector2D::ZeroVector,Size).IsInside(At))
		{
			Paint.Ring(At, 6 * S, Ink, 5 * S); Paint.Ring(At, 6 * S, Gold, 2 * S);
		}
		return;
	}
	if (PC->IsUsingGamepad() || !FSlateApplication::IsInitialized()) { return; }
	// The cursor is in desktop coordinates; a paint geometry is in the window's own, so converting with it would be off
	// by where the window sits. The cached (tick-space) geometry is the desktop one, and the local point is the same.
	const FVector2D Pointer = GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	const bool bEasy = ChaosImpactSettings::GetPlayerControlScheme(PC) != EChaosImpactControlScheme::Normal;
	const int32 Hover = GetGameplayPointerHit();
	// The pointer and its marks above the buttons (each a three-layer shard with its label on it).
	const FPainter Paint{Geometry, Elements, Layer + 5};
	// (Not in 訓練場's filmed clips: the picture keeps to the game itself.)
	if (bEasy && !AChaosImpactDojo::IsRecordingRun())
	{
		for (int32 Hit = 1; Hit <= 6; ++Hit)
		{
			if (Hit == 2 && (!PC->IsTrainingMode() || !PC->IsPrimaryLocalPlayerController() || PC->IsOnlineRoom() || PC->IsVersusMatchWorld() || CwIsTournamentWorld(this) || PC->IsDojo())) { continue; }
			if (Hit == 4 && !PC->CanToggleReady()) { continue; }
			if (Hit == 5 && !PC->CanToggleSpectating()) { continue; }
			if (Hit == 6 && (!ShoulderCharacter || ShoulderCharacter->GetBallForm()->IsCaptured() || !ShoulderCharacter->GetBallForm()->IsAvailable())) { continue; }
			const FBox2D R = EasyButtonRect(Size, S, Hit);
			HudBoxes.Add(FBox2D(R.Min - FVector2D(8.0, 8.0) * S, R.Max + FVector2D(8.0, 8.0) * S));
			const bool bHover = Hover == Hit;
			const FPainter Button{Geometry, Elements, Layer};
			HudShard(Button, {FVector2D(R.Min.X + 10.0f * S, R.Min.Y), FVector2D(R.Max.X, R.Min.Y), FVector2D(R.Max.X - 10.0f * S, R.Max.Y),
				FVector2D(R.Min.X, R.Max.Y)}, bHover ? FireFill : NightFill, 2.5f * S, FVector2D(3.0f * S, 4.0f * S));
			if (!bHover)
			{
				HudStroke(HudAbove(Button), {FVector2D(R.Min.X + 12.0f * S, R.Min.Y + 3.0f * S), FVector2D(R.Max.X - 3.0f * S, R.Min.Y + 3.0f * S)},
					IceFill.Middle, 2.0f * S);
			}
			HudAbove(Button).Text(Hit == 1 ? TEXT("ポーズ") : Hit == 2 ? TEXT("練習設定") : Hit == 4 ? TEXT("準備OK") : Hit == 5 ? TEXT("観戦")
				: Hit == 6 ? (ShoulderCharacter->IsInBallForm() ? TEXT("元に戻る") : TEXT("ボール化")) : TEXT("ダッシュ"),
				(R.Min.X + R.Max.X) * 0.5f, R.Min.Y + 10.0f * S, 18.0f * S,
				bHover ? Navy : Paper, ETextAlign::Center, TEXT("Black"));
		}
	}
	const AChaosImpactCharacter* Character = Cast<AChaosImpactCharacter>(PC->GetPawn());
	if (Character && Character->HasClickMoveGoal())
	{
		FVector2D ViewportPosition;
		if (PC->ProjectWorldLocationToScreen(Character->GetClickMoveGoal()
			- FVector::UpVector * (Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 8.0f), ViewportPosition))
		{
			const FVector2D At = GetCachedGeometry().AbsoluteToLocal(UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).LocalToAbsolute(
				ViewportPosition / UWidgetLayoutLibrary::GetViewportScale(this)));
			Paint.Ring(At, 14.0f * S, Ink, 6.0f * S);
			Paint.Ring(At, 14.0f * S, Ice, 2.5f * S);
		}
	}
	if (!FBox2D(FVector2D::ZeroVector, Size).IsInside(Pointer)) { return; }
	const float Radius = FMath::Clamp(15.0f * S, 11.0f, 22.0f);
	const FLinearColor Color = Hover != 0 ? Gold : Character && Character->WasClickMoveRejected() ? Fire : Paper;
	// White + black outline stays legible on both bright tiles and dark effects. The tiny system crosshair stays inside.
	Paint.Ring(Pointer, Radius, Ink, 6.0f);
	Paint.Ring(Pointer, Radius, Color, 2.5f);
	for (int32 Axis = 0; Axis < 4; ++Axis)
	{
		const FVector2D Direction(FMath::Cos(Axis * UE_HALF_PI), FMath::Sin(Axis * UE_HALF_PI));
		Paint.Line(Pointer + Direction * (Radius + 3), Pointer + Direction * (Radius + 9), Ink, 6.0f);
		Paint.Line(Pointer + Direction * (Radius + 3), Pointer + Direction * (Radius + 9), Color, 2.0f);
	}
	if (bPointerCharging) { Paint.Arc(Pointer, Radius + 5, -90, -90 + 360.0f * PointerChargeAlpha, Gold, 3.0f); }
}

float UChaosImpactChargeWidget::GetHudScale(const double Width, const double Height) const
{
	const UGameInstance* ScaleGameInstance = GetGameInstance();
	const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (ScaleGameInstance && ScaleGameInstance->GetLocalPlayers().Num() > 1 && Viewport && !Viewport->IsSplitscreenForceDisabled()
		&& !UChaosImpactGameViewportClient::GetSingleDuelPlayer(GetWorld()))
	{
		// A split view is only part of the screen but is read from the same distance, so it is sized by what
		// fits in the view rather than shrunk in proportion to it.
		return FMath::Clamp(static_cast<float>(FMath::Min(Width / 880.0, Height / 560.0)), 0.62f, 1.1f) * ChaosImpactSettings::GetHudSizeFactor();
	}
	return FMath::Clamp(static_cast<float>(FMath::Min(Width / 1600.0, Height / 900.0)), 0.5f, 1.4f) * ChaosImpactSettings::GetHudSizeFactor();
}

void UChaosImpactChargeWidget::PaintBallIcon(const ChaosImpactPaint::FPainter& Paint, const FVector2D& Center, const float Radius, const bool bFilled,
	const EChaosImpactBallType Type, const float Growth, const float Time)
{
	using namespace ChaosImpactPaint;
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
	case EChaosImpactBallType::Wind: Body = FLinearColor(0.2f, 0.8f, 0.32f, 1.0f); break;
	case EChaosImpactBallType::Smoke: Body = FLinearColor(0.3f, 0.28f, 0.36f, 1.0f); break;
	case EChaosImpactBallType::Beam: Body = FLinearColor(1.0f, 0.24f, 0.74f, 1.0f); break;
	case EChaosImpactBallType::Snow: Body = FLinearColor(0.9f, 0.95f, 1.0f, 1.0f); break;
	case EChaosImpactBallType::Nova: Body = FLinearColor(0.3f, 0.66f, 1.0f, 1.0f); break;
	case EChaosImpactBallType::Simae: Body = FLinearColor(0.95f, 0.97f, 1.0f, 1.0f); break;
	case EChaosImpactBallType::Drive: Body = FLinearColor(1.0f, 0.56f, 0.1f, 1.0f); break;
	case EChaosImpactBallType::Prison: Body = FLinearColor(.22f,.035f,.018f,1.f); break;
	case EChaosImpactBallType::Hook: Body = FLinearColor(0.1f, 0.42f, 0.4f, 1.0f); break;
	case EChaosImpactBallType::Anchor: Body = FLinearColor(0.34f, 0.5f, 0.04f, 1.0f); break;
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
	case EChaosImpactBallType::Wind:
	{
		// Three gusts curling round the middle.
		const float Swirl = FMath::Fmod(Time * 260.0f, 360.0f);
		for (int32 Gust = 0; Gust < 3; ++Gust)
		{
			const float Start = Swirl + Gust * 120.0f;
			Paint.Arc(Center, Radius * (0.28f + 0.16f * Gust), Start, Start + 150.0f,
				FLinearColor(0.88f, 1.0f, 0.84f, 0.95f - 0.15f * Gust), 2.5f);
		}
		break;
	}
	case EChaosImpactBallType::Hook:
	{
		// A hook hanging from two chain links, swinging a little.
		const float Swing = 0.18f * FMath::Sin(Time * 3.0f);
		const FVector2D Down(FMath::Sin(Swing), FMath::Cos(Swing));
		const FVector2D Across(Down.Y, -Down.X);
		const FLinearColor Steel(0.88f, 0.98f, 0.96f, 1.0f);
		const FVector2D Top = Center - Down * Radius * 0.62f;
		Paint.Ring(Top, Radius * 0.13f, Steel, 2.0f);
		Paint.Ring(Top + Down * Radius * 0.24f, Radius * 0.13f, Glow, 2.0f);
		const FVector2D Shank = Top + Down * Radius * 0.37f;
		const FVector2D Bend = Center + Down * Radius * 0.3f;
		Paint.Line(Shank, Bend, Steel, 3.0f);
		// The curve up to the point, and its barb.
		const FVector2D Bowl = Bend - Across * Radius * 0.24f;
		const float Base = FMath::RadiansToDegrees(FMath::Atan2(Down.Y, Down.X));
		Paint.Arc(Bowl, Radius * 0.24f, Base - 90.0f - 180.0f, Base - 90.0f + 10.0f, Steel, 3.0f);
		const FVector2D Point = Bowl - Across * Radius * 0.24f - Down * Radius * 0.22f;
		Paint.Line(Bowl - Across * Radius * 0.24f, Point, Steel, 3.0f);
		Paint.Line(Point, Point + Across * Radius * 0.14f + Down * Radius * 0.1f, Glow, 2.0f);
		break;
	}
	case EChaosImpactBallType::Anchor:
	{
		// An anchor: a ring on top, the shank with its stock, and the two curved flukes, bobbing a little.
		const FVector2D Bob(0.0f, Radius * 0.05f * FMath::Sin(Time * 4.0f));
		const FLinearColor Steel(0.95f, 1.0f, 0.86f, 1.0f);
		const FVector2D Top = Center + Bob - FVector2D(0.0f, Radius * 0.6f);
		const FVector2D Bottom = Center + Bob + FVector2D(0.0f, Radius * 0.55f);
		Paint.Ring(Top, Radius * 0.13f, Glow, 2.0f);
		Paint.Line(Top + FVector2D(0.0f, Radius * 0.13f), Bottom, Steel, 3.0f);
		Paint.Line(Top + FVector2D(-Radius * 0.3f, Radius * 0.32f), Top + FVector2D(Radius * 0.3f, Radius * 0.32f), Steel, 2.5f);
		Paint.Arc(Bottom - FVector2D(0.0f, Radius * 0.42f), Radius * 0.48f, 30.0f, 150.0f, Steel, 3.0f);
		for (const float Side : {-1.0f, 1.0f})
		{
			const FVector2D Tip = Bottom - FVector2D(0.0f, Radius * 0.42f)
				+ FVector2D(FMath::Cos(FMath::DegreesToRadians(Side > 0.0f ? 30.0f : 150.0f)), FMath::Sin(FMath::DegreesToRadians(30.0f))) * Radius * 0.48f;
			Paint.Line(Tip, Tip + FVector2D(Side * Radius * 0.06f, -Radius * 0.16f), Glow, 2.5f);
		}
		break;
	}
	case EChaosImpactBallType::Prison:
	{
		Paint.Ring(Center,Radius*.82f,Glow,2.5f);
		for (int32 I=-1;I<=1;++I)
		{
			const float X=I*Radius*.36f;
			Paint.Line(Center+FVector2D(X,-Radius*.66f),Center+FVector2D(X,Radius*.66f),Paper,2.f);
		}
		Paint.Line(Center-FVector2D(Radius*.7f,0),Center+FVector2D(Radius*.7f,0),Glow,2.f);
		break;
	}
	case EChaosImpactBallType::Smoke:
	{
		// Puffs of smoke rolling over each other.
		for (int32 Puff = 0; Puff < 3; ++Puff)
		{
			const float Angle = Time * 1.6f + Puff * 2.1f;
			const FVector2D Offset(FMath::Cos(Angle) * Radius * 0.28f, FMath::Sin(Angle) * Radius * 0.2f);
			Paint.Disc(Center + Offset, Radius * (0.34f + 0.05f * Puff), FLinearColor(0.7f, 0.68f, 0.78f, 0.55f));
		}
		break;
	}
	case EChaosImpactBallType::Beam:
	{
		// A shaft of light straight through it, with a four-point glint that pulses.
		const float Pulse = 0.75f + 0.25f * FMath::Sin(Time * 12.0f);
		Paint.Line(Center - FVector2D(Radius * 0.9f, 0.0f), Center + FVector2D(Radius * 0.9f, 0.0f),
			FLinearColor(1.0f, 0.5f, 0.88f, 0.7f), 7.0f);
		Paint.Line(Center - FVector2D(Radius * 0.9f, 0.0f), Center + FVector2D(Radius * 0.9f, 0.0f),
			FLinearColor(1.0f, 0.96f, 1.0f, Pulse), 2.5f);
		Paint.Line(Center - FVector2D(0.0f, Radius * 0.42f * Pulse), Center + FVector2D(0.0f, Radius * 0.42f * Pulse),
			FLinearColor(1.0f, 0.96f, 1.0f, Pulse), 2.0f);
		break;
	}
	case EChaosImpactBallType::Snow:
	{
		// Clumps of snow, and a gauge round it of how big it has grown.
		const FLinearColor Shadow(0.62f, 0.72f, 0.86f, 0.9f);
		Paint.Disc(Center + FVector2D(Radius * 0.3f, Radius * 0.28f), Radius * 0.26f, Shadow);
		Paint.Disc(Center + FVector2D(-Radius * 0.34f, Radius * 0.2f), Radius * 0.18f, Shadow);
		Paint.Disc(Center + FVector2D(Radius * 0.1f, -Radius * 0.36f), Radius * 0.14f, Shadow);
		if (Growth > 0.0f)
		{
			Paint.Arc(Center, Radius + 5.0f, -90.0f, -90.0f + 360.0f * FMath::Clamp(Growth, 0.0f, 1.0f),
				FLinearColor(1.0f, 1.0f, 1.0f, 0.95f), 3.5f);
		}
		break;
	}
	case EChaosImpactBallType::Nova:
	{
		// A white-hot heart, with light streaming into it from all round.
		const float Pulse = 0.85f + 0.15f * FMath::Sin(Time * 6.0f);
		Paint.Disc(Center, Radius * 0.62f * Pulse, FLinearColor(0.72f, 0.92f, 1.0f, 0.65f));
		Paint.Disc(Center, Radius * 0.36f * Pulse, FLinearColor(1.0f, 1.0f, 1.0f, 0.95f));
		for (int32 Ray = 0; Ray < 6; ++Ray)
		{
			const float Angle = Ray * UE_TWO_PI / 6.0f + Time * 0.8f;
			const float Phase = FMath::Frac(Time * 1.4f + Ray * 0.37f);
			const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle));
			Paint.Line(Center + Way * Radius * (1.3f - 0.6f * Phase), Center + Way * Radius * (1.08f - 0.6f * Phase),
				FLinearColor(0.8f, 0.95f, 1.0f, 1.0f - Phase), 2.0f);
		}
		break;
	}
	case EChaosImpactBallType::Drive:
	{
		// A white-hot core with a ring of energy swinging round it, the steering in it.
		Paint.Disc(Center, Radius * 0.3f, FLinearColor(1.0f, 0.97f, 0.85f, 1.0f));
		const float Swing = FMath::Fmod(Time * 220.0f, 360.0f);
		Paint.Arc(Center, Radius * 0.62f, Swing, Swing + 200.0f, FLinearColor(1.0f, 0.86f, 0.4f, 0.95f), 2.5f);
		const float Angle = FMath::DegreesToRadians(Swing + 200.0f);
		Paint.Disc(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius * 0.62f, Radius * 0.12f,
			FLinearColor(1.0f, 0.95f, 0.75f, 1.0f));
		break;
	}
	case EChaosImpactBallType::Simae:
	{
		// Two bead-black eyes and a tiny warm beak make the white ball readable even in split screen.
		const float Blink = FMath::Frac(Time * 0.37f) > 0.94f ? 0.18f : 1.0f;
		for (const float Side : {-1.0f, 1.0f})
		{
			Paint.Disc(Center + FVector2D(Side * Radius * 0.32f, -Radius * 0.12f),
				Radius * FVector2D(0.11f, 0.15f * Blink).Size(), FLinearColor(0.015f, 0.02f, 0.028f, 1.0f));
		}
		Paint.Line(Center + FVector2D(-Radius * 0.12f, Radius * 0.17f), Center + FVector2D(0.0f, Radius * 0.31f),
			FLinearColor(1.0f, 0.63f, 0.08f, 1.0f), 3.0f);
		Paint.Line(Center + FVector2D(0.0f, Radius * 0.31f), Center + FVector2D(Radius * 0.12f, Radius * 0.17f),
			FLinearColor(1.0f, 0.63f, 0.08f, 1.0f), 3.0f);
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
	if (bSpecial && Type != EChaosImpactBallType::Snow && Type != EChaosImpactBallType::Simae)
	{
		// Special balls: two arcs orbiting just outside (a snowball shows its growth there instead).
		const float Spin = FMath::Fmod(Time * 160.0f, 360.0f);
		Paint.Arc(Center, Radius + 5.0f, Spin, Spin + 70.0f, Glow, 2.5f);
		Paint.Arc(Center, Radius + 5.0f, Spin + 180.0f, Spin + 250.0f, Glow, 2.5f);
	}
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
	Occupy(Corner, -300.0f, NovaParts != 0 ? -250.0f : -200.0f, 300.0f, NovaParts != 0 ? 250.0f : 200.0f);

	// The holder: no plate, two round pedestals. The ball thrown next sits large on the front one, with a jagged blast
	// round it (fire while it holds a ball) and a ring of its ball's colour turning slowly; the other ball sits smaller
	// behind. The count is a fire diamond on the front pedestal's shoulder.
	const auto PaintPedestal = [&](const FVector2D& Center, const float Radius, const FLinearColor& RingColor, const bool bFront)
	{
		const FPainter Back{Corner, OutDrawElements, BaseLayer};
		if (bFront)
		{
			HudBurst(Back, Center, Radius + 2.0f, Radius + 17.0f, 10, CarriedBalls > 0 ? FireFill : NightFill, Time * 12.0f,
				FVector2D(1.0, 1.0), 3.5f, FVector2D(6.0, 7.0), 9.0f);
		}
		const FPainter Plinth{Corner, OutDrawElements, BaseLayer + 3};
		Plinth.Disc(Center + FVector2D(4.0, 5.0), Radius + 4.0f, Extrude);
		const FPainter Rim{Corner, OutDrawElements, BaseLayer + 4};
		Rim.Disc(Center, Radius + 4.0f, Navy);
		const FPainter Face{Corner, OutDrawElements, BaseLayer + 5};
		Face.Disc(Center, Radius, FLinearColor(0.08f, 0.12f, 0.27f, 1.0f));
		Face.Disc(Center + FVector2D(0.0, 3.0), Radius - 9.0f, FLinearColor(0.015f, 0.025f, 0.07f, 1.0f));
		const FPainter Ring{Corner, OutDrawElements, BaseLayer + 6};
		const float Turn = bFront ? Time * 40.0f : -Time * 25.0f;
		const int32 Pieces = bFront ? 8 : 6;
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const float From = Turn + 360.0f * Piece / Pieces;
			Ring.Arc(Center, Radius - 4.0f, From, From + 360.0f / Pieces - 9.0f, RingColor, bFront ? 5.0f : 4.0f);
		}
	};
	const auto SlotColor = [&](const int32 SlotIndex)
	{
		return SlotIndex < CarriedBalls ? ChaosImpactBallTypes::GetColor(ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, SlotIndex))
			: FLinearColor(0.35f, 0.42f, 0.55f, 0.7f);
	};
	PaintPedestal(FVector2D(BallHolderHomes[1]), BallHolderRings[1], SlotColor(1), false);
	PaintPedestal(FVector2D(BallHolderHomes[0]), BallHolderRings[0], SlotColor(0), true);

	// DrawSlot 0 (thrown next) sits large at the front, slot 1 smaller behind it; a swap flies each to the other's place.
	const FVector2D Homes[] = {FVector2D(BallHolderHomes[0]), FVector2D(BallHolderHomes[1])};
	const float Radii[] = {BallHolderBalls[0], BallHolderBalls[1]};
	const float PickupAge = static_cast<float>(Clock - BallGainedAt);
	const float Kick = PickupAge < BallPickupSeconds ? 1.0f - EaseOut(PickupAge / BallPickupSeconds) : 0.0f;
	const float SwapT = FMath::Clamp(static_cast<float>(Clock - BallSwappedAt) / BallSwapSeconds, 0.0f, 1.0f);
	const bool bSwapping = SwapT < 1.0f && CarriedBalls >= 2;
	const float SwapLift = bSwapping ? FMath::Sin(SwapT * UE_PI) : 0.0f;
	FVector2D Centers[2];
	float DrawnRadii[2];
	const FPainter Balls{Corner, OutDrawElements, BaseLayer + 7};
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
		if (!bFilled)
		{
			continue;
		}
		EChaosImpactBallType IconType=ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes,DrawSlot);
		if (const auto* OwnerPawn=GetOwningPlayer() ? Cast<AChaosImpactCharacter>(GetOwningPlayer()->GetPawn()) : nullptr)
		{
			if (const auto* Shell=OwnerPawn->GetBallForm()->GetHeldBall(DrawSlot); Shell && Shell->IsPrisonBall()) { IconType=EChaosImpactBallType::Prison; }
		}
		PaintBallIcon(Balls, Center, Radius, bFilled, IconType, SnowGrowth[DrawSlot], Time);
	}

	const FPainter Labels{Corner, OutDrawElements, BaseLayer + 8};
	const auto* BuddyOwner = GetOwningPlayer() ? Cast<AChaosImpactCharacter>(GetOwningPlayer()->GetPawn()) : nullptr;
	for (int32 DrawSlot = 0; DrawSlot < FMath::Min(CarriedBalls, 2); ++DrawSlot)
	{
		const EChaosImpactBallType Type = ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, DrawSlot);
		const AChaosImpactBuddyBall* Buddy = BuddyOwner ? BuddyOwner->GetBallForm()->GetHeldBall(DrawSlot) : nullptr;
		if (Type == EChaosImpactBallType::Normal && !Buddy)
		{
			continue;
		}
		// A small tag with the type's name under the ball.
		const FVector2D Under = Centers[DrawSlot] + FVector2D(0.0f, DrawnRadii[DrawSlot] + 3.0f);
		const FGeometry TagSpace = MakeSkewed(Corner, static_cast<float>(Under.X) - 30.0f, static_cast<float>(Under.Y),
			60.0f, 17.0f, -0.22f);
		const FPainter Tag{TagSpace, OutDrawElements, BaseLayer + 8};
		Tag.Box(0.0f, 0.0f, 60.0f, 17.0f, WithAlpha(Ink, 0.88f));
		Tag.Box(0.0f, 15.0f, 60.0f, 2.0f, Buddy ? Buddy->GetTeamColor() : ChaosImpactBallTypes::GetColor(Type));
		Labels.Text(Buddy ? (Buddy->IsPrisonBall() ? TEXT("捕獲") : TEXT("味方")) : ChaosImpactBallTypes::GetDisplayName(Type), static_cast<float>(Under.X), static_cast<float>(Under.Y) - 1.0f,
			11.0f, Paper, ETextAlign::Center, TEXT("Bold"));
		if (Buddy && Buddy->IsPrisonBall())
		{
			Labels.Arc(Centers[DrawSlot],DrawnRadii[DrawSlot]+5,-90,-90+360*Buddy->GetCaptureSecondsLeft()/ChaosImpactBallTypes::PrisonSeconds,
				ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Prison),3.f);
		}
	}
	if (CarriedBalls > 0)
	{
		Labels.Text(TEXT("NEXT"), static_cast<float>(Centers[0].X - DrawnRadii[0]) - 6.0f,
			static_cast<float>(Centers[0].Y - DrawnRadii[0]) - 10.0f, 10.0f, Accent, ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
	}

	// The count: a large number in the colour of the next ball.
	const FLinearColor CountColor = CarriedBalls > 0
		? ChaosImpactBallTypes::GetColor(ChaosImpactBallTypes::GetPackedSlot(CarriedBallTypes, 0)) : Muted;
	(void)CountColor;
	const FVector2D Badge = FVector2D(BallHolderHomes[0]) + FVector2D(48.0, -40.0);
	const float BadgeSize = 21.0f * (1.0f + 0.35f * Kick);
	HudShard(FPainter{Corner, OutDrawElements, BaseLayer + 10}, {Badge + FVector2D(4.0, -BadgeSize), Badge + FVector2D(BadgeSize, 0.0),
		Badge + FVector2D(-4.0, BadgeSize), Badge + FVector2D(-BadgeSize, 0.0)}, CarriedBalls > 0 ? FireFill
		: MakeFill(FLinearColor(0.5f, 0.55f, 0.65f), FLinearColor(0.2f, 0.23f, 0.3f)), 3.0f, FVector2D(3.0, 4.0));
	HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 13}, FString::FromInt(CarriedBalls), static_cast<float>(Badge.X),
		static_cast<float>(Badge.Y) - 16.0f, 24.0f, Paper, ETextAlign::Center);
	// The nova's parts carried so far: a row of their own over the holder, NOVA and a diamond per part (lit in the
	// nova's ice as each is picked up), its name under it, joined by a thin line that fills as they come.
	if (NovaParts != 0)
	{
		const TCHAR* PartNames[] = {TEXT("ギア"), TEXT("コア"), TEXT("リング")};
		constexpr float RowY = -214.0f;
		constexpr float FirstX = -222.0f;
		constexpr float Step = 62.0f;
		const FPainter Row{Corner, OutDrawElements, BaseLayer + 10};
		HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 13}, TEXT("NOVA"), FirstX - 34.0f, RowY - 15.0f, 20.0f, IceFill.Top,
			ETextAlign::Right);
		HudPoly(Row, HudInflate({FVector2D(FirstX, RowY - 2.0), FVector2D(FirstX + Step * 2.0f, RowY - 2.0),
			FVector2D(FirstX + Step * 2.0f, RowY + 2.0), FVector2D(FirstX, RowY + 2.0)}, 2.0f), SolidFill(Navy), 1);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const bool bHave = (NovaParts & (1 << Index)) != 0;
			const FVector2D Gem(FirstX + Index * Step, RowY);
			if (bHave && Index < 2 && (NovaParts & (1 << (Index + 1))) != 0)
			{
				HudPoly(FPainter{Corner, OutDrawElements, BaseLayer + 11}, {Gem + FVector2D(0.0, -2.0), Gem + FVector2D(Step, -2.0),
					Gem + FVector2D(Step, 2.0), Gem + FVector2D(0.0, 2.0)}, SolidFill(IceFill.Middle), 1);
			}
			HudShard(FPainter{Corner, OutDrawElements, BaseLayer + 12}, {Gem + FVector2D(3.0, -16.0), Gem + FVector2D(15.0, 0.0),
				Gem + FVector2D(-3.0, 16.0), Gem + FVector2D(-15.0, 0.0)}, bHave ? IceFill : SolidFill(FLinearColor(0.04f, 0.06f, 0.15f, 0.95f)),
				2.5f, FVector2D(3.0, 4.0));
			HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 15}, PartNames[Index], static_cast<float>(Gem.X), RowY + 16.0f, 12.0f,
				bHave ? Paper : FLinearColor(0.5f, 0.56f, 0.68f), ETextAlign::Center, TEXT("Black"));
		}
	}

	// Pickup ripple and swap flash.
	if (PickupAge < BallPickupSeconds && BallGainedSlot >= 0 && BallGainedSlot < 2)
	{
		const float T = PickupAge / BallPickupSeconds;
		const FPainter Ripple{Corner, OutDrawElements, BaseLayer + 8, 1.0f - T};
		Ripple.Ring(Centers[BallGainedSlot], DrawnRadii[BallGainedSlot] + 6.0f + 26.0f * EaseOut(T),
			FLinearColor(0.6f, 0.93f, 1.0f, 1.0f), 2.0f + 4.0f * (1.0f - T));
	}
	const float SwapFlashSeconds = BallSwapSeconds + 0.1f;
	const float SwapAge = static_cast<float>(Clock - BallSwappedAt);
	if (SwapAge < SwapFlashSeconds && CarriedBalls >= 2)
	{
		const float T = SwapAge / SwapFlashSeconds;
		const FPainter Flash{Corner, OutDrawElements, BaseLayer + 8, 1.0f - T};
		for (int32 DrawSlot = 0; DrawSlot < 2; ++DrawSlot)
		{
			Flash.Ring(Centers[DrawSlot], DrawnRadii[DrawSlot] + 6.0f + 16.0f * EaseOut(T), WithAlpha(Gold, 0.9f), 1.5f + 3.5f * (1.0f - T));
		}
	}

	// Both hands full: a keycap showing how to swap.
	if (CarriedBalls >= 2 && !bRespawnVisible)
	{
		const AChaosImpactPlayerController* InputController = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
		// The keys this player has set for swapping (about one letter's width each).
		const FString SwapKeys = ChaosImpactSettings::DescribeAction(InputController, EChaosImpactAction::SwapBall);
		float KeyWidth = 10.0f;
		for (const TCHAR Letter : SwapKeys)
		{
			KeyWidth += Letter > 0x2000 ? 13.0f : 8.0f;
		}
		KeyWidth = FMath::Max(KeyWidth, 24.0f);
		// Left of the ball behind: a keycap shard of ice with the key on it, the word beside it.
		const float KeyRight = BallHolderHomes[1].X - BallHolderRings[1] - 14.0f;
		const float KeyTop = BallHolderHomes[1].Y - 12.0f;
		const float KeyLeft = KeyRight - KeyWidth - 6.0f;
		HudShard(FPainter{Corner, OutDrawElements, BaseLayer + 10}, {FVector2D(KeyLeft + 5.0f, KeyTop), FVector2D(KeyRight, KeyTop),
			FVector2D(KeyRight - 5.0f, KeyTop + 24.0f), FVector2D(KeyLeft, KeyTop + 24.0f)}, IceFill, 2.5f, FVector2D(3.0, 4.0));
		FPainter{Corner, OutDrawElements, BaseLayer + 13}.Text(SwapKeys, (KeyLeft + KeyRight) * 0.5f, KeyTop + 3.0f, 13.0f, Navy,
			ETextAlign::Center, TEXT("Black"));
		HudText3D(FPainter{Corner, OutDrawElements, BaseLayer + 13}, TEXT("持ち替え"), KeyLeft - 8.0f, KeyTop + 1.0f, 14.0f, Paper,
			ETextAlign::Right, TEXT("Black"));
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
	const float Time = static_cast<float>(FMath::Fmod(Clock, 3600.0));
	const UGameInstance* OwningGameInstance = GetGameInstance();
	const ULocalPlayer* OwningLocalPlayer = GetOwningLocalPlayer();
	const int32 PlayerIndex = OwningGameInstance && OwningLocalPlayer
		? FMath::Max(0, OwningGameInstance->GetLocalPlayers().IndexOfByKey(OwningLocalPlayer)) : 0;
	const FGeometry Corner = MakeAnchor(AllottedGeometry, 0.0f, Size.Y, S);
	Occupy(Corner, 0.0f, -185.0f, 400.0f, 185.0f);
	const auto At = [](const float X, const float Y) { return FVector2D(X, Y); };
	// No panel: the pieces stand on the game as they are, each outlined like the logo's letters.
	const FPainter Low{Corner, OutDrawElements, BaseLayer};
	const FPainter High{Corner, OutDrawElements, BaseLayer + 3};
	const FPainter Top{Corner, OutDrawElements, BaseLayer + 6};

	// The player's number and name over it all.
	FString Name;
	if (const AChaosImpactCharacter* Me = Cast<AChaosImpactCharacter>(GetOwningPlayerPawn()))
	{
		Name = Me->GetOverheadDisplayName();
	}
	if (Name.IsEmpty() && GetOwningPlayer() && GetOwningPlayer()->PlayerState)
	{
		Name = GetOwningPlayer()->PlayerState->GetPlayerName();
	}
	const FString Number = FString::Printf(TEXT("P%d"), PlayerIndex + 1);
	HudText3D(Top, Number, 44.0f, -160.0f, 18.0f, FireFill.Top, ETextAlign::Left);
	HudText3D(Top, Name, 50.0f + HudMeasure(Number, 18.0f, TEXT("BlackItalic")), -158.0f, HudFit(Name, 16.0f, 230.0f), Paper, ETextAlign::Left, TEXT("Black"));

	// HP: a diamond of fire with the number in it (it jolts when hit; on the last point it flares and pulses), then a
	// chevron of ice per point, pointing on. A lost point breaks apart.
	const int32 HealthCells = FMath::Clamp(FMath::RoundToInt(MaxHealthValue), 1, 6);
	const int32 HealthLeft = FMath::Max(0, FMath::CeilToInt(Health - KINDA_SMALL_NUMBER));
	const bool bLastPoint = HealthLeft == 1 && Health > 0 && !bRespawnVisible;
	const float Pulse = bLastPoint ? 0.5f + 0.5f * FMath::Sin(Time * 8.0f) : 0.0f;
	const float HitAge = static_cast<float>(Clock - HitAt);
	const float Shake = HitAge < 0.3f ? FMath::Sin(HitAge * 90.0f) * (0.3f - HitAge) * 18.0f : 0.0f;
	const FHudFill Burning = bLastPoint ? MakeFill(FMath::Lerp(FireFill.Top, FLinearColor::White, Pulse * 0.6f), FireFill.Bottom) : FireFill;
	const FVector2D Gem(92.0f + Shake, -86.0f);
	HudShard(Low, {Gem + At(8.0f, -50.0f), Gem + At(50.0f, -2.0f), Gem + At(-8.0f, 48.0f), Gem + At(-50.0f, 2.0f)}, Burning, 3.5f,
		FVector2D(6.0, 7.0));
	HudStroke(High, {Gem + At(-38.0f, -2.0f), Gem + At(6.0f, -40.0f)}, FLinearColor(1.0f, 1.0f, 0.9f, 0.7f), 2.5f);
	const float Jolt = 1.0f + 0.4f * FMath::Exp(-10.0f * FMath::Max(HitAge, 0.0f));
	const FPainter GemNumber{MakeSkewed(Corner, static_cast<float>(Gem.X) - 50.0f, static_cast<float>(Gem.Y) - 42.0f, 100.0f, 84.0f, -0.12f, Jolt),
		OutDrawElements, BaseLayer + 6};
	HudText3D(GemNumber, FString::FromInt(HealthLeft), 50.0f, 0.0f, 50.0f, Paper, ETextAlign::Center);

	constexpr float RowLeft = 152.0f;
	constexpr float ChevronWidth = 56.0f;
	constexpr float ChevronGap = 5.0f;
	constexpr float Tip = 15.0f;
	constexpr float RowTop = -120.0f;
	constexpr float RowBottom = -84.0f;
	constexpr float RowMiddle = (RowTop + RowBottom) * 0.5f;
	const auto* CombatSample = SupportsCombatHealth() ? CombatHealth.Find(Cast<AChaosImpactCharacter>(GetOwningPlayerPawn())) : nullptr;
	for (int32 Cell = 0; Cell < HealthCells; ++Cell)
	{
		const float L = RowLeft + Cell * (ChevronWidth + ChevronGap);
		const float R = L + ChevronWidth;
		const bool bHave = Cell < HealthLeft;
		const FHudFill Lit = !bHave ? SolidFill(FLinearColor(0.03f, 0.05f, 0.13f, 0.9f))
			: bLastPoint ? MakeFill(FMath::Lerp(FireFill.Top, FLinearColor::White, Pulse * 0.5f), FireFill.Bottom) : IceFill;
		// A chevron in two halves (each convex), sharing one gradient.
		HudShard(High, {At(L, RowTop), At(R - Tip, RowTop), At(R, RowMiddle), At(L + Tip, RowMiddle)}, Lit, 2.5f,
			bHave ? FVector2D(3.0, 4.0) : FVector2D::ZeroVector, Navy, RowTop, RowBottom);
		HudShard(High, {At(L + Tip, RowMiddle), At(R, RowMiddle), At(R - Tip, RowBottom), At(L, RowBottom)}, Lit, 2.5f,
			bHave ? FVector2D(3.0, 4.0) : FVector2D::ZeroVector, Navy, RowTop, RowBottom);
		if (bHave)
		{
			HudStroke(Top, {At(L + 4.0f, RowTop + 4.0f), At(R - Tip - 2.0f, RowTop + 4.0f)}, FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 2.0f);
		}
		if (CombatSample)
		{
			for (const auto& Broken : CombatSample->Breaks)
			{
				if (Broken.Cell != Cell) { continue; }
				ChaosImpactCombatHealth::PaintBreak(FPainter{Corner, OutDrawElements, BaseLayer + 8}, At((L + R) * .5f, RowMiddle),
					static_cast<float>(GetWorld()->GetTimeSeconds() - Broken.At) / ChaosImpactCombatHealth::BreakSeconds,
					ChevronWidth, RowBottom - RowTop, IceFill, Cell);
			}
		}
		else if (Cell == LostHealthSegment && HitAge < 0.7f)
		{
			HudShatter(FPainter{Corner, OutDrawElements, BaseLayer + 8}, At((L + R) * 0.5f, RowMiddle), HitAge / 0.7f, 16.0f,
				bLastPoint ? FireFill : IceFill, 7, static_cast<float>(Cell));
		}
	}

	// Each row says what it is at its end: HP in ice after the chevrons, ST in fire before the stamina at the blade's tip.
	const bool bCriticalCombat = IsLastHealthWarningActive();
	HudText3D(Top, bCriticalCombat ? TEXT("残り1") : TEXT("HP"), RowLeft + HealthCells * (ChevronWidth + ChevronGap) + 4.0f,
		RowTop + 3.0f, 20.0f, bCriticalCombat ? FireFill.Top : IceFill.Top, ETextAlign::Left);

	// Stamina: a thin blade under the chevrons that fills with fire, a notch per dash. A dash's notch flashes, a full
	// blade glints now and then, and it all goes grey while a dash is not possible.
	const int32 StaminaCells = FMath::Clamp(FMath::RoundToInt(MaxStaminaValue), 1, 10);
	const bool bCanDash = StaminaValue >= 1.0f - KINDA_SMALL_NUMBER;
	const bool bFull = DisplayedStamina >= MaxStaminaValue - 0.01f;
	const float SpentAge = static_cast<float>(Clock - StaminaSpentAt);
	const float BladeRight = RowLeft + FMath::Max(HealthCells * (ChevronWidth + ChevronGap) - ChevronGap + 6.0f, 200.0f);
	constexpr float BladeTop = -72.0f;
	constexpr float BladeBottom = -61.0f;
	const TArray<FVector2D> Blade{At(RowLeft - 4.0f, BladeTop), At(BladeRight - 16.0f, BladeTop), At(BladeRight, (BladeTop + BladeBottom) * 0.5f),
		At(BladeRight - 16.0f, BladeBottom), At(RowLeft - 10.0f, BladeBottom)};
	HudShard(Low, Blade, SolidFill(FLinearColor(0.03f, 0.05f, 0.13f, 0.9f)), 2.5f, FVector2D(3.0, 4.0));
	const float Ratio = FMath::Clamp(DisplayedStamina / FMath::Max(MaxStaminaValue, 0.01f), 0.0f, 1.0f);
	if (Ratio > 0.0f)
	{
		const FHudFill Energy = bCanDash ? FireFill : MakeFill(FLinearColor(0.55f, 0.6f, 0.7f), FLinearColor(0.25f, 0.28f, 0.36f));
		const float FillX = RowLeft - 10.0f + (BladeRight - RowLeft + 10.0f) * Ratio;
		HudPoly(High, HudClip(Blade, FillX, false, true), Energy, 3);
		if (Ratio < 1.0f && bCanDash)
		{
			HudStroke(Top, {At(FillX + 1.0f, BladeTop - 2.0f), At(FillX - 2.0f, BladeBottom + 2.0f)}, Paper, 2.5f);
		}
	}
	for (int32 Cell = 1; Cell < StaminaCells; ++Cell)
	{
		const float X = RowLeft - 10.0f + (BladeRight - RowLeft + 10.0f) * Cell / StaminaCells;
		HudStroke(Top, {At(X + 2.0f, BladeTop), At(X - 1.0f, BladeBottom)}, Navy, 2.5f);
	}
	if (SpentStaminaSegment >= 0 && SpentAge < 0.35f)
	{
		const float L = RowLeft - 10.0f + (BladeRight - RowLeft + 10.0f) * SpentStaminaSegment / StaminaCells;
		const float R = RowLeft - 10.0f + (BladeRight - RowLeft + 10.0f) * (SpentStaminaSegment + 1) / StaminaCells;
		HudPoly(FPainter{Corner, OutDrawElements, BaseLayer + 7, 1.0f - SpentAge / 0.35f}, HudClip(HudClip(Blade, L, true, true), R, false, true),
			SolidFill(Paper), 1);
	}
	if (bFull)
	{
		const float Travel = FMath::Fmod(Time, 3.0f) / 0.7f;
		if (Travel < 1.0f)
		{
			const float X = RowLeft + (BladeRight - RowLeft) * Travel;
			HudStroke(Top, {At(X + 5.0f, BladeTop + 1.0f), At(X, BladeBottom - 1.0f)}, FLinearColor(1.0f, 1.0f, 0.9f, 0.9f), 6.0f);
		}
	}
	HudText3D(Top, TEXT("ST"), BladeRight + 8.0f, BladeTop - 7.0f, 16.0f, bCanDash ? FireFill.Top : Muted, ETextAlign::Left);
	HudText3D(Top, FString::FromInt(FMath::FloorToInt(StaminaValue + KINDA_SMALL_NUMBER)),
		BladeRight + 14.0f + HudMeasure(TEXT("ST"), 16.0f, TEXT("BlackItalic")), BladeTop - 10.0f, 19.0f, bCanDash ? Paper : Muted, ETextAlign::Left);
}

void UChaosImpactChargeWidget::PaintSpectatorBar(const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, const int32 BaseLayer) const
{
	using namespace ChaosImpactPaint;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const AChaosImpactSpectatorPawn* Camera = Cast<AChaosImpactSpectatorPawn>(GetOwningPlayerPawn());
	const AChaosImpactGameState* Match = GetWorld() ? GetWorld()->GetGameState<AChaosImpactGameState>() : nullptr;
	// The results take the screen to themselves.
	if (!Camera || Size.X < 1.0f || Size.Y < 1.0f
		|| (Match && Match->bVersusMatch && Match->Phase == EChaosImpactOnlinePhase::Results))
	{
		return;
	}
	const float S = GetHudScale(Size.X, Size.Y);
	const AChaosImpactCharacter* Watched = Camera->GetFollowedCharacter();
	FString Showing = TEXT("フリーカメラ");
	FLinearColor ShowingColor = Ice;
	if (Watched)
	{
		const AChaosImpactPlayerState* Member = Watched->GetPlayerState<AChaosImpactPlayerState>();
		Showing = FString::Printf(TEXT("%s の視点"), *Watched->GetOverheadDisplayName());
		ShowingColor = Member && Member->TeamIndex != INDEX_NONE ? ChaosImpactMatch::GetTeamColor(Member->TeamIndex) : Gold;
	}

	// Bottom middle: 観戦中 and who is shown, then the camera keys under it.
	const FGeometry Bottom = MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y, S);
	const FGeometry Plate = MakeSkewed(Bottom, -300.0f, -128.0f, 600.0f, 64.0f, -0.25f);
	Occupy(Bottom, -330.0f, -140.0f, 660.0f, 140.0f);
	const FPainter P{Plate, OutDrawElements, BaseLayer, 1.0f};
	const auto* RoyaleEntry = Match && Match->IsBattleRoyale() && GetOwningPlayer() ? Match->GetRoyaleEntry(GetOwningPlayer()->PlayerState) : nullptr;
	const bool bRoyaleOut = RoyaleEntry && RoyaleEntry->bOut;
	// バトルロイヤル: the place this player fell at, in its medal's colours (gold, silver, bronze; red after that).
	const FHudFill PlaceFill = !bRoyaleOut ? SolidFill(Ice) : RoyaleEntry->Place == 1 ? FireFill : RoyaleEntry->Place == 2 ? SilverFill
		: RoyaleEntry->Place == 3 ? BronzeFill : MakeFill(FLinearColor(1.0f, 0.3f, 0.32f), FLinearColor(0.55f, 0.04f, 0.1f));
	P.Box(8.0f, 8.0f, 600.0f, 64.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	P.Box(0.0f, 0.0f, 600.0f, 64.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.88f));
	P.Box(0.0f, 0.0f, 150.0f, 64.0f, PlaceFill.Middle);
	P.Box(0.0f, 60.0f, 600.0f, 4.0f, ShowingColor);
	if (bRoyaleOut && Match)
	{
		// The moment this player falls: their place, large over the field, before the camera takes over. Falling at
		// the start of sudden death, it waits until that call-out has gone.
		const double Now = FPlatformTime::Seconds();
		const bool bSuddenCall = Match->Royale.bSuddenDeath && Match->GetServerWorldTimeSeconds() - Match->Royale.SuddenDeathStartedAt < 2.2;
		if (RoyaleOwnOutMatch != Match->PhaseStartedAt && !bSuddenCall)
		{
			RoyaleOwnOutMatch = Match->PhaseStartedAt;
			RoyaleOwnOutSeenAt = Now;
		}
		const float Age = static_cast<float>(Now - RoyaleOwnOutSeenAt);
		if (RoyaleOwnOutMatch == Match->PhaseStartedAt && Age < 3.2f && RoyaleEntry->Place > 0)
		{
			const float Alpha = EaseOut(Age / 0.15f) * (1.0f - FMath::Clamp((Age - 2.7f) / 0.5f, 0.0f, 1.0f));
			const float Pop = 1.0f + 0.5f * FMath::Exp(-10.0f * Age);
			const FGeometry Card = MakeSkewed(MakeAnchor(AllottedGeometry, Size.X * 0.5f, Size.Y * 0.31f, S), -200.0f, -90.0f, 400.0f, 180.0f, -0.12f, Pop);
			const FPainter Back{Card, OutDrawElements, BaseLayer + 6, Alpha};
			HudShard(Back, {FVector2D(22.0, 16.0), FVector2D(400.0, 0.0), FVector2D(378.0, 168.0), FVector2D(0.0, 180.0)}, NightFill, 4.0f, FVector2D(8.0, 9.0));
			const FPainter Front = HudAbove(Back);
			HudShard(Front, {FVector2D(40.0, 30.0), FVector2D(250.0, 22.0), FVector2D(236.0, 156.0), FVector2D(26.0, 164.0)}, PlaceFill, 3.0f, FVector2D(4.0, 5.0));
			const FString PlaceText = FString::FromInt(RoyaleEntry->Place);
			const float PlaceWidth = HudMeasure(PlaceText, 108.0f, TEXT("BlackItalic"));
			HudText3D(HudAbove(Front, 2), PlaceText, 112.0f, 18.0f, 108.0f, Paper, ETextAlign::Center);
			HudText3D(HudAbove(Front, 2), TEXT("位"), 112.0f + PlaceWidth * 0.5f + 4.0f, 82.0f, 44.0f, Paper, ETextAlign::Left, TEXT("Black"));
			HudText3D(HudAbove(Front, 2), FString::Printf(TEXT("/%d"), Match->Royale.Entries.Num()), 274.0f, 98.0f, 30.0f, WithAlpha(Paper, 0.85f), ETextAlign::Left);
			FPainter{Card, OutDrawElements, BaseLayer + 12, Alpha}.Text(TEXT("脱落"), 272.0f, 38.0f, 40.0f, Fire, ETextAlign::Left, TEXT("Black"), 3.0f, Ink);
			if (Age < 0.6f)
			{
				HudShatter(FPainter{Card, OutDrawElements, BaseLayer + 13, Alpha}, FVector2D(200.0, 90.0), Age / 0.6f, 40.0f, PlaceFill, 9, 3.0f);
			}
		}
	}
	const FString Caption = RoyaleEntry && RoyaleEntry->bOut
		? (RoyaleEntry->Place > 0 ? FString::Printf(TEXT("%d位 脱落"), RoyaleEntry->Place) : FString(TEXT("脱落"))) : FString(TEXT("観戦中"));
	P.Text(Caption, 75.0f, 11.0f + (30.0f - HudFit(Caption, 30.0f, 136.0f)) * 0.5f, HudFit(Caption, 30.0f, 136.0f), Paper, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	P.Text(Showing, 375.0f, 12.0f + (30.0f - HudFit(Showing, 30.0f, 420.0f)) * 0.5f, HudFit(Showing, 30.0f, 420.0f), ShowingColor, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	const AChaosImpactPlayerController* Viewer = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
	const bool bOnline = Viewer && Viewer->IsOnlineRoom();
	const FPainter Keys{MakeAnchor(Bottom, 0.0f, -54.0f, 1.0f), OutDrawElements, BaseLayer, 1.0f};
	FString KeyHelp = Watched
		? TEXT("← →  /  LB RB  選手切替　　F / X  フリーカメラ　　H / Y  UI表示切替")
		: TEXT("WASD  移動　E Q / RT LT  上下　右クリック / 右スティック  視点　Shift / L3  高速　← →  選手視点　H / Y  UI");
	// Offline, time can be stopped for screenshots; online, the lobby is where to switch back to playing.
	KeyHelp += !bOnline ? TEXT("　T / A  時間停止")
		: Viewer->CanToggleSpectating() ? FString::Printf(TEXT("　%s  プレイヤーに戻る"),
			*ChaosImpactSettings::DescribeAction(Viewer, EChaosImpactAction::LobbySpectate)) : FString();
	// Within this player's part of the screen: smaller to fit, and on two lines when one would be too small to read.
	const float KeyRoom = Size.X / FMath::Max(S, 0.01f) - 40.0f;
	if (HudFit(KeyHelp, 18.0f, KeyRoom, TEXT("Regular")) >= 13.0f)
	{
		Keys.Text(KeyHelp, 0.0f, 0.0f, HudFit(KeyHelp, 18.0f, KeyRoom, TEXT("Regular")), WithAlpha(Paper, 0.8f), ETextAlign::Center, TEXT("Regular"), 1.5f, Ink);
	}
	else
	{
		TArray<FString> Parts;
		KeyHelp.ParseIntoArray(Parts, TEXT("　"), true);
		FString Lines[2];
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			FString& Line = Lines[Index < (Parts.Num() + 1) / 2 ? 0 : 1];
			Line += (Line.IsEmpty() ? TEXT("") : TEXT("　")) + Parts[Index];
		}
		for (int32 Row = 0; Row < 2; ++Row)
		{
			Keys.Text(Lines[Row], 0.0f, -22.0f + Row * 22.0f, HudFit(Lines[Row], 17.0f, KeyRoom, TEXT("Regular")), WithAlpha(Paper, 0.8f), ETextAlign::Center,
				TEXT("Regular"), 1.5f, Ink);
		}
	}

	if (Viewer && Viewer->IsSpectateTimeStopped())
	{
		// Top middle, under the clock: the match is frozen.
		const FGeometry Top = MakeAnchor(AllottedGeometry, Size.X * 0.5f, 0.0f, S);
		const FPainter Stop{MakeSkewed(Top, -150.0f, 92.0f, 300.0f, 52.0f, -0.25f), OutDrawElements, BaseLayer, 1.0f};
		Stop.Box(0.0f, 0.0f, 300.0f, 52.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.9f));
		Stop.Box(0.0f, 0.0f, 300.0f, 5.0f, Ice);
		Stop.Text(TEXT("時間停止中"), 150.0f, 8.0f, 28.0f, Ice, ETextAlign::Center, TEXT("Black"), 2.0f, Ink);
	}
}

namespace
{
	/**
	 * パーティルール: a competitor's rubies (or counts left) in a small pill, its bottom middle at Bottom: the gem or the
	 * crown, the number, a glow as it goes up; the crown's holder gets the count in progress as a ring round the crown.
	 */
	void CwPaintPartyCount(const ChaosImpactPaint::FPainter& P, const UWorld* World, const APlayerState* Member, const FVector2D& Bottom,
		const float S, const float Scale = 1.0f)
	{
		using namespace ChaosImpactPaint;
		const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
		if (!Match || !Member)
		{
			return;
		}
		const bool bCrown = Match->Rules.IsCrownKeep();
		const AChaosImpactCrown* Crown = bCrown ? AChaosImpactCrown::Get(World) : nullptr;
		const bool bHolder = Crown && Crown->GetHolderState() == Member;
		const float K = S * Scale;
		const float Time = static_cast<float>(World->GetTimeSeconds());
		const float Glow = FMath::Clamp(1.0f - static_cast<float>(Match->GetServerWorldTimeSeconds()
			- ChaosImpactParty::GetCountGainedAt(World, Member)) / 0.55f, 0.0f, 1.0f);
		const FString Text = FString::FromInt(ChaosImpactParty::GetShownCount(World, Member));
		const float TextSize = 17.0f * K;
		const float H = 26.0f * K;
		const float W = 31.0f * K + HudMeasure(Text, TextSize, TEXT("BlackItalic")) + 11.0f * K;
		const float X = static_cast<float>(Bottom.X) - W * 0.5f;
		const float Y = static_cast<float>(Bottom.Y) - H;
		const FLinearColor Accent = bCrown ? ChaosImpactParty::CrownColor : ChaosImpactParty::RubyColor;
		if (Glow > 0.0f)
		{
			RoundedBox(P, X - 5.0f * K * Glow, Y - 5.0f * K * Glow, W + 10.0f * K * Glow, H + 10.0f * K * Glow, H * 0.5f + 5.0f * K * Glow,
				WithAlpha(Accent, 0.4f * Glow));
		}
		RoundedBox(P, X - 1.5f * K, Y - 1.5f * K, W + 3.0f * K, H + 3.0f * K, H * 0.5f + 1.5f * K, WithAlpha(bHolder ? Accent : Paper, bHolder ? 0.95f : 0.22f));
		const FPainter Face = HudAbove(P, 1);
		RoundedBox(Face, X, Y, W, H, H * 0.5f, WithAlpha(Ink, 0.86f));
		const FPainter Icon = HudAbove(Face, 1);
		const FVector2D IconAt(X + 15.5f * K, Y + H * 0.5f + (bCrown ? 1.0f : 0.5f) * K);
		if (bCrown)
		{
			ChaosImpactParty::PaintCrown(Icon, IconAt, 9.0f * K, Time + Member->GetPlayerId() * 0.37f, bHolder ? 1.0f : 0.4f);
			if (bHolder)
			{
				Icon.Arc(IconAt - FVector2D(0.0, 0.5f * K), 12.5f * K, -90.0f, -90.0f + 360.0f * Crown->GetCountProgress(), Accent, 2.5f * K);
			}
		}
		else
		{
			ChaosImpactParty::PaintRuby(Icon, IconAt, 8.5f * K, Time + Member->GetPlayerId() * 0.37f);
		}
		HudText3D(HudAbove(Icon, 3), Text, X + 28.0f * K, Y + 1.0f * K, TextSize, FMath::Lerp(Paper, FMath::Lerp(Accent, Paper, 0.45f), Glow),
			ETextAlign::Left, TEXT("BlackItalic"), 1.5f * K);
	}
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
	// In smoke nobody else's name or arrow shows; they drift back in as it clears.
	const float OthersShown = 1.0f - GetSmokeCover();
	float MarkerTopInset = 0.f;
	if (const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
		Match && (Match->IsDuel() || Match->IsBattleRoyale() || (Match->IsTournament() && Match->FindDuelFor(GetOwningPlayerState()))))
	{
		// The scoreboard is shared across the whole window, while markers live inside each player's view.
		// Convert its lower edge to this view's widget coordinates, including split screens and DPI scaling.
		const FGeometry Viewport = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
		const FVector2D ViewportSize = Viewport.GetLocalSize();
		const float ScoreScale = FMath::Clamp(static_cast<float>(FMath::Min(ViewportSize.X / 1600., ViewportSize.Y / 900.)), .42f, 1.4f) * ChaosImpactSettings::GetHudSizeFactor();
		MarkerTopInset = FMath::Clamp(static_cast<float>(GetCachedGeometry().AbsoluteToLocal(
			Viewport.LocalToAbsolute(FVector2D(ViewportSize.X * .5, (Match->IsBattleRoyale() ? 190.f : 155.f) * ScoreScale))).Y), 0.f, static_cast<float>(Center.Y * .75));
		HudBoxes.Add(FBox2D(FVector2D(0.0, 0.0), FVector2D(Size.X, MarkerTopInset)));
	}

	const AChaosImpactGameState* PartyMatch = World->GetGameState<AChaosImpactGameState>();
	const bool bPartyTags = PartyMatch && PartyMatch->bVersusMatch && PartyMatch->Rules.IsParty();
	// (The count's pill over the name: the warp and nova gauges sit above it.)
	const float PartyLift = bPartyTags ? 30.0f : 0.0f;
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
		FString Name = Character->GetOverheadDisplayName();
		if (const auto* TrainingPC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			TrainingPC && TrainingPC->ShowsTrainingPauseEntries() && TrainingPC->AreTrainingTeamsEnabled() && Character->SoloTeam >= 0)
		{
			Color = ChaosImpactMatch::GetTeamColor(Character->SoloTeam);
			Name += FString::Printf(TEXT("  T%d"),Character->SoloTeam+1);
		}
		const float Shown = Character == ViewerPawn ? 1.0f : OthersShown;
		if (Shown <= 0.01f)
		{
			continue;
		}

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
				const FPainter Tag{AllottedGeometry, OutDrawElements, BaseLayer, Shown};
				const float FontSize = 15.0f * S;
				ChaosImpactCombatHealth::FPresentation HealthTag;
				const float HealthSpace = GetCombatHealthPresentation(Character,HealthTag) ? 24.0f * S : 0.0f;
				if (MarkerTopInset > 0.f)
				{
					HeadScreen.Y = FMath::Max(HeadScreen.Y, static_cast<double>(MarkerTopInset + FontSize * 1.75f + 7.f * S + HealthSpace));
				}
				const FVector2D Tip(HeadScreen.X, HeadScreen.Y);
				Tag.Text(Name, static_cast<float>(HeadScreen.X), static_cast<float>(HeadScreen.Y) - FontSize * 1.75f - 7.0f * S - HealthSpace,
					FontSize, Color, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
				PaintMarkerArrow(Tag, Tip, FVector2D(0.0, 1.0), 0.38f * S, Color);
				// (クラウンキープ: only those with counts, and the crown's holder; ルビーラッシュ: everyone.)
				const AChaosImpactPlayerState* TagState = Character->GetPlayerState<AChaosImpactPlayerState>();
				const AChaosImpactCrown* TagCrown = bPartyTags && PartyMatch->Rules.IsCrownKeep() ? AChaosImpactCrown::Get(World) : nullptr;
				if (bPartyTags && TagState && (!PartyMatch->Rules.IsCrownKeep() || TagState->Points > 0 || (TagCrown && TagCrown->GetHolder() == Character)))
				{
					CwPaintPartyCount(HudAbove(Tag, 2), World, TagState,
						FVector2D(HeadScreen.X, HeadScreen.Y - FontSize * 1.75f - 9.0f * S - HealthSpace), S);
				}
			}
			// Standing on a warp pad: a ring above the name fills up until the warp.
			if (const float WarpCharge = AChaosImpactWarpPad::FindChargeProgress(Character); WarpCharge > 0.0f
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					PlayerController, Body + FVector(0.0f, 0.0f, HalfHeight + 40.0f), HeadScreen, true))
			{
				const FPainter Gauge{AllottedGeometry, OutDrawElements, BaseLayer + 1, Shown};
				const FVector2D GaugeCenter(HeadScreen.X, HeadScreen.Y - (58.0f + PartyLift) * S);
				const float Radius = 12.0f * S;
				const float Pulse = WarpCharge > 0.8f
					? 0.5f + 0.5f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * 22.0f) : 0.0f;
				Gauge.Disc(GaugeCenter, Radius + 5.0f * S, WithAlpha(Ink, 0.6f));
				Gauge.Arc(GaugeCenter, Radius, -90.0f, 270.0f, WithAlpha(Color, 0.25f), 3.5f * S);
				Gauge.Arc(GaugeCenter, Radius, -90.0f, -90.0f + 360.0f * WarpCharge,
					FMath::Lerp(Color, FLinearColor::White, Pulse * 0.7f), 3.5f * S);
			}
			// Carrying nova parts: the three of them over the name, the ones held lit (everyone can see who is near a nova).
			if (const uint8 Parts = Character->GetNovaParts(); Parts != 0
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					PlayerController, Body + FVector(0.0f, 0.0f, HalfHeight + 40.0f), HeadScreen, true))
			{
				const float Now = static_cast<float>(World->GetTimeSeconds());
				const FLinearColor NovaColor = ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Nova);
				const FVector2D Mid(HeadScreen.X, HeadScreen.Y - (60.0f + PartyLift) * S + FMath::Sin(Now * 3.0f) * 2.0f * S);
				const float Step = 20.0f * S;
				const FPainter Back{AllottedGeometry, OutDrawElements, BaseLayer + 1, Shown};
				RoundedBox(Back, Mid.X - Step * 1.5f - 4.0f * S, Mid.Y - 11.0f * S, Step * 3.0f + 8.0f * S, 22.0f * S, 11.0f * S, WithAlpha(Ink, 0.7f));
				const FPainter Pip = HudAbove(Back, 1);
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FVector2D At(Mid.X + (Index - 1) * Step, Mid.Y);
					const bool bHeld = (Parts & ChaosImpactNovaParts::Bit(static_cast<EChaosImpactNovaPart>(Index))) != 0;
					if (bHeld)
					{
						const float Glow = 0.5f + 0.5f * FMath::Sin(Now * 5.0f + Index);
						Pip.Disc(At, 9.0f * S, WithAlpha(NovaColor, 0.3f + 0.25f * Glow));
						HudAbove(Pip, 1).Disc(At, 6.5f * S, NovaColor);
						HudAbove(Pip, 2).Disc(At + FVector2D(-2.0f * S, -2.0f * S), 2.0f * S, WithAlpha(FLinearColor::White, 0.85f));
					}
					else
					{
						Pip.Ring(At, 5.5f * S, WithAlpha(Paper, 0.35f), 1.5f * S);
					}
				}
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
		const float LabelSize = 11.0f * S * FMath::Lerp(0.95f, 1.2f, Nearness);
		// Along its ray, in from the edge until it is clear of this mode's HUD (scores, health, balls, hints...).
		const FVector2D Footprint(FMath::Max(30.0f * K, 22.0f * K + HudMeasure(Name, LabelSize, TEXT("Bold")) * 0.5f + 6.0f), 30.0f * K + LabelSize);
		const FVector2D Tip = ChaosImpactPlayerMarkerLayout::EdgeTipClear(Size, Direction, K, MarkerTopInset, HudBoxesLast, Footprint, 26.0f * K);

		const FPainter Marker{AllottedGeometry, OutDrawElements, BaseLayer, FMath::Lerp(0.6f, 1.0f, Nearness) * Shown};
		Marker.Disc(Tip - Direction * 16.0f * K, 18.0f * K, WithAlpha(Ink, 0.62f));
		PaintMarkerArrow(Marker, Tip, Direction, K, Color);
		const FVector2D Label = Tip - Direction * 48.0f * K;
		Marker.Text(Name, static_cast<float>(Label.X), static_cast<float>(Label.Y) - LabelSize * 0.85f, LabelSize,
			Color, ETextAlign::Center, TEXT("Bold"), 2.0f, Ink);
	}

	// クラウンキープ: where the crown is. Off screen, a gold mark at the edge towards it; lying on the floor in view, a small
	// crown over it (it can be behind a wall). Not for the one wearing it.
	if (const AChaosImpactCrown* Crown = bPartyTags && PartyMatch->Rules.IsCrownKeep() ? AChaosImpactCrown::Get(World) : nullptr;
		Crown && Crown->GetHolder() != ViewerPawn && PartyMatch->Phase == EChaosImpactOnlinePhase::Match)
	{
		const FVector At = Crown->GetShownLocation() + FVector(0, 0, 40);
		const float Time = static_cast<float>(World->GetTimeSeconds());
		FVector2D Screen;
		const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, At, Screen, true);
		const FVector FromCamera = At - CameraLocation;
		const bool bInFront = FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::X)) > 0.0;
		const bool bOnScreen = bProjected && bInFront && Screen.X >= 0.0 && Screen.Y >= 0.0 && Screen.X <= Size.X && Screen.Y <= Size.Y;
		if (bOnScreen)
		{
			if (Crown->IsOnFloor())
			{
				const FPainter Mark{AllottedGeometry, OutDrawElements, BaseLayer + 1, 0.85f};
				const FVector2D Over(Screen.X, Screen.Y - 34.0f * S + FMath::Sin(Time * 3.0f) * 3.0f * S);
				Mark.Disc(Over, 17.0f * S, WithAlpha(Ink, 0.55f));
				ChaosImpactParty::PaintCrown(HudAbove(Mark, 1), Over + FVector2D(0.0, 1.0f * S), 11.0f * S, Time);
				PaintMarkerArrow(HudAbove(Mark, 1), Over + FVector2D(0.0, 26.0f * S), FVector2D(0.0, 1.0), 0.32f * S, ChaosImpactParty::CrownColor);
			}
		}
		else
		{
			FVector2D Direction = bProjected && bInFront ? Screen - Center
				: FVector2D(FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Y)), -FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Z)));
			if (Direction.Normalize())
			{
				const float K = S * 1.25f;
				const FVector2D Tip = ChaosImpactPlayerMarkerLayout::EdgeTipClear(Size, Direction, K, MarkerTopInset, HudBoxesLast,
					FVector2D(34.0f * K, 34.0f * K), 22.0f * K);
				const FVector2D Badge = Tip - Direction * 22.0f * K;
				const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 4.0f);
				const FPainter Mark{AllottedGeometry, OutDrawElements, BaseLayer + 2};
				Mark.Disc(Badge, (22.0f + 3.0f * Pulse) * K, WithAlpha(ChaosImpactParty::CrownColor, 0.2f + 0.15f * Pulse));
				HudAbove(Mark, 1).Disc(Badge, 17.0f * K, WithAlpha(Ink, 0.88f));
				HudAbove(Mark, 1).Ring(Badge, 17.5f * K, WithAlpha(ChaosImpactParty::CrownColor, 0.9f), 2.0f * K);
				ChaosImpactParty::PaintCrown(HudAbove(Mark, 2), Badge + FVector2D(0.0, 1.0f * K), 10.5f * K, Time);
				PaintMarkerArrow(HudAbove(Mark, 2), Tip, Direction, 0.5f * K, ChaosImpactParty::CrownColor);
			}
		}
	}

	// A ball thrown at this player from off screen: a mark at the edge it will come in by, in the ball's colour, larger
	// and brighter as it nears (gone once it is on screen), and a small sound from that side when it first shows.
	const AChaosImpactCharacter* Target = Cast<AChaosImpactCharacter>(ViewerPawn);
	if (!Target || Target->IsEliminated())
	{
		return;
	}
	constexpr float WarnSeconds = 1.6f;
	constexpr float WarnMiss = 280.0f;
	TSet<TWeakObjectPtr<AChaosImpactBall>> StillComing;
	for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
	{
		AChaosImpactBall* Ball = *It;
		if (!IsValid(Ball) || Ball->IsHidden() || Ball->IsPickup() || Ball->GetAttachParentActor() || Ball->WasThrownBy(Target))
		{
			continue;
		}
		const APawn* Thrower = Ball->GetShownThrower();
		if (Thrower && AChaosImpactGameState::AreTeammates(World, Thrower, Target))
		{
			continue;
		}
		// Coming this way: along the ground it passes within reach of this player, soon.
		const FVector Velocity = Ball->GetBallVelocity();
		const FVector2D Flat(Velocity.X, Velocity.Y);
		const double Speed2 = Flat.SizeSquared();
		if (Speed2 < FMath::Square(300.0))
		{
			continue;
		}
		const FVector Offset = Target->GetActorLocation() - Ball->GetActorLocation();
		const FVector2D Toward(Offset.X, Offset.Y);
		const double When = FVector2D::DotProduct(Toward, Flat) / Speed2;
		if (When <= 0.0 || When > WarnSeconds || (Toward - Flat * When).Size() > WarnMiss)
		{
			continue;
		}
		// Only from off screen (on screen it can be seen and dodged as it is).
		FVector2D BallScreen;
		const bool bBallProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, Ball->GetActorLocation(), BallScreen, true);
		const FVector FromCamera = Ball->GetActorLocation() - CameraLocation;
		const bool bInFront = FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::X)) > 0.0;
		if (bBallProjected && bInFront && BallScreen.X >= 0.0 && BallScreen.Y >= 0.0 && BallScreen.X <= Size.X && BallScreen.Y <= Size.Y)
		{
			continue;
		}
		FVector2D Direction = bBallProjected && bInFront ? BallScreen - Center
			: FVector2D(FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Y)), -FVector::DotProduct(FromCamera, CameraAxes.GetScaledAxis(EAxis::Z)));
		if (!Direction.Normalize())
		{
			continue;
		}
		StillComing.Add(Ball);
		if (!WarnedBalls.Contains(Ball))
		{
			// (Placed a little way toward the ball from this player, so it is heard from that side and not too far.)
			const FVector Side = FVector(Toward.X, Toward.Y, 0.0).GetSafeNormal() * -420.0f;
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MatchTick, Target->GetActorLocation() + Side, 0.5f, 1.35f);
		}
		const float Urgency = 1.0f - FMath::Clamp(static_cast<float>(When) / WarnSeconds, 0.0f, 1.0f);
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(World->GetTimeSeconds()) * (10.0f + 14.0f * Urgency));
		const float K = S * FMath::Lerp(0.85f, 1.35f, Urgency);
		const FLinearColor BallColor = ChaosImpactBallTypes::GetColor(Ball->GetBallType());
		const FVector2D Tip = ChaosImpactPlayerMarkerLayout::EdgeTipClear(Size, Direction, K, MarkerTopInset, HudBoxesLast,
			FVector2D(30.0f * K, 30.0f * K), 18.0f * K);
		// The mark: a soft glow, the ball (its colour, a white rim), and a chevron inside it pointing in, the way it comes.
		const FVector2D Ball2D = Tip - Direction * 18.0f * K;
		const FPainter Mark{AllottedGeometry, OutDrawElements, BaseLayer + 2, FMath::Lerp(0.6f, 1.0f, Urgency)};
		Mark.Disc(Ball2D, (24.0f + 6.0f * Pulse) * K, WithAlpha(BallColor, 0.18f + 0.22f * Pulse));
		HudAbove(Mark, 1).Disc(Ball2D, 15.0f * K, WithAlpha(Ink, 0.85f));
		HudAbove(Mark, 2).Disc(Ball2D, 12.5f * K, BallColor);
		HudAbove(Mark, 2).Ring(Ball2D, 13.5f * K, WithAlpha(Paper, 0.9f), 2.0f * K);
		PaintMarkerArrow(HudAbove(Mark, 3), Ball2D - Direction * 30.0f * K, -Direction, 0.55f * K, WithAlpha(Paper, 0.75f + 0.25f * Pulse));
	}
	WarnedBalls = MoveTemp(StillComing);
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
	if (bSpectatorView)
	{
		// Seen from the player being watched; nobody's in free flight.
		const AChaosImpactSpectatorPawn* Camera = Cast<AChaosImpactSpectatorPawn>(GetOwningPlayerPawn());
		const AChaosImpactCharacter* Watched = Camera ? Camera->GetFollowedCharacter() : nullptr;
		Own = Watched ? Watched->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
	}
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
		if (In > 0.0f && Out < 1.0f && !Match->Rules.bBattleRoyale)
		{
			const float Alpha = In * (1.0f - Out);
			// Plain type over the flyover, rising slightly into place.
			const float Rise = (1.0f - In) * 16.0f;
			const FPainter Words{Center, OutDrawElements, BaseLayer + 2, Alpha};
			Words.Text(Match->Rules.IsRubyRush() ? TEXT("ルビーを一番多く集めろ！") : Match->Rules.IsCrownKeep() ? TEXT("王冠を奪って守りきれ！")
				: Match->Rules.bDuel ? TEXT("相手を3回当てて倒せ！") : Match->Rules.bCrashBall ? TEXT("巨大球を相手のゴールへ押し込め！")
				: bTeams ? TEXT("チームで多く敵を倒せ！") : TEXT("時間内に多く敵を倒せ！"), 0.0f, -74.0f + Rise, 60.0f,
				Paper, ETextAlign::Center, TEXT("Black"), 3.0f, Ink);
			Words.Box(-150.0f * In, 18.0f + Rise, 300.0f * In, 2.0f, WithAlpha(Paper, 0.6f));
			Words.Text(Match->Rules.bDuel
				? FString::Printf(TEXT("%s ・ 1ラウンド%d秒 ・ %s"), *ChaosImpactMatch::DescribeRule(Match->Rules),
					FMath::RoundToInt(ChaosImpactMatch::DuelRoundSeconds), ChaosImpactMatch::GetDuelStageName())
				: FString::Printf(TEXT("%s ・ %d分"), *ChaosImpactMatch::DescribeRule(Match->Rules), Match->Rules.Minutes),
				0.0f, 30.0f + Rise, 22.0f, WithAlpha(Paper, 0.85f), ETextAlign::Center, TEXT("Regular"), 2.0f, Ink);
		}
		// Ready? is drawn once for the whole screen by UChaosImpactMatchAnnouncerWidget.
		return;
	}

	if (Match->Phase == EChaosImpactOnlinePhase::Match)
	{
		if (Match->Rules.bCrashBall)
		{
			PaintCrashMarkers(AllottedGeometry, OutDrawElements, BaseLayer, *Match, S);
			return;
		}
		// The duel's scoreboard (both players, their rounds and health) is drawn once for the whole screen.
		if (Match->Rules.bDuel || Match->Rules.bBattleRoyale)
		{
			return;
		}
		// The time is shown once for the whole screen by UChaosImpactMatchAnnouncerWidget. Each view keeps
		// the standings: the top three players, or every team in a team battle, plus this player's own line.
		// In split screen the shared clock sits at the top middle of the whole screen, over a view's corner.
		// That clock is scaled for the whole screen while this view is scaled for itself, so its height is
		// converted into this view's units before the rows are placed under it.
		const UGameInstance* ViewGameInstance = GetGameInstance();
		{
			// The shared clock at the top middle of the whole window: taken in this view too (wherever it falls in it). (The
			// viewport's geometry is in the same space as this widget's cached one, not as the paint geometry.)
			const FGeometry Viewport = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
			const FVector2D Window = Viewport.GetLocalSize();
			const float ClockScale = FMath::Clamp(static_cast<float>(FMath::Min(Window.X / 1600.0, Window.Y / 900.0)), 0.42f, 1.4f) * ChaosImpactSettings::GetHudSizeFactor();
			FBox2D ClockBox(ForceInit);
			ClockBox += FVector2D(GetCachedGeometry().AbsoluteToLocal(Viewport.LocalToAbsolute(FVector2D(Window.X * 0.5 - 175.0 * ClockScale, 0.0))));
			ClockBox += FVector2D(GetCachedGeometry().AbsoluteToLocal(Viewport.LocalToAbsolute(FVector2D(Window.X * 0.5 + 175.0 * ClockScale,
				(Match->Rules.IsCrownKeep() ? 150.0 : 96.0) * ClockScale))));
			HudBoxes.Add(ClockBox);
		}
		float RowTop = 16.0f;
		if (ViewGameInstance && ViewGameInstance->GetLocalPlayers().Num() > 1)
		{
			const FVector2D Screen = UWidgetLayoutLibrary::GetViewportWidgetGeometry(this).GetLocalSize();
			const float ScreenScale = FMath::Clamp(static_cast<float>(FMath::Min(Screen.X / 1600.0, Screen.Y / 900.0)), 0.42f, 1.4f) * ChaosImpactSettings::GetHudSizeFactor();
			RowTop = FMath::Max(16.0f, 92.0f * ScreenScale / S + 4.0f);
		}
		const auto PaintRow = [&](const int32 Row, const int32 Rank, const FLinearColor& Accent, const FString& Label,
			const int32 Points, const bool bOwn, const AChaosImpactPlayerState* Player = nullptr)
		{
			constexpr float RowLeft = 18.0f;
			constexpr float RowWidth = 270.0f;
			constexpr float RowHeight = 28.0f;
			const float Y = RowTop + Row * 36.0f;
			Occupy(TopLeft, 0.0f, Y - 8.0f, RowLeft + RowWidth + 60.0f, RowHeight + 16.0f);
			const auto At = [](const float X, const float YY) { return FVector2D(X, YY); };
			const FPainter Strip{TopLeft, OutDrawElements, BaseLayer};
			// A strip with a torn right end; this player's own glows ice blue.
			HudShard(Strip, {At(RowLeft + 10.0f, Y), At(RowLeft + RowWidth, Y), At(RowLeft + RowWidth - 8.0f, Y + RowHeight * 0.5f),
				At(RowLeft + RowWidth - 2.0f, Y + RowHeight), At(RowLeft, Y + RowHeight)}, bOwn ? MakeFill(FLinearColor(0.16f, 0.3f, 0.6f, 0.96f),
				FLinearColor(0.04f, 0.08f, 0.22f, 0.96f)) : NightFill, 2.5f, FVector2D(4.0, 5.0));
			const FPainter Front = HudAbove(Strip);
			HudStroke(Front, {At(RowLeft + 44.0f, Y + RowHeight - 3.0f), At(RowLeft + RowWidth - 6.0f, Y + RowHeight - 3.0f)}, WithAlpha(Accent, 0.9f), 2.0f);
			// The place in a medal shard of its own.
			const FHudFill Medal = Rank == 1 ? FireFill : Rank == 2 ? SilverFill : Rank == 3 ? BronzeFill : MakeFill(FLinearColor(0.3f, 0.36f, 0.5f),
				FLinearColor(0.12f, 0.15f, 0.24f));
			HudShard(Front, {At(RowLeft - 2.0f, Y - 4.0f), At(RowLeft + 36.0f, Y - 4.0f), At(RowLeft + 30.0f, Y + RowHeight + 4.0f),
				At(RowLeft - 8.0f, Y + RowHeight + 4.0f)}, Medal, 2.5f, FVector2D(3.0, 4.0));
			const FPainter Words = HudAbove(Front);
			HudText3D(Words, Rank > 0 ? FString::FromInt(Rank) : FString(TEXT("-")), RowLeft + 14.0f, Y - 2.0f, 20.0f, Paper, ETextAlign::Center);
			// (Clear of the points at the strip's right end; パーティルール: the gem or the crown before the number.)
			const bool bPartyRow = Match->Rules.IsParty() && Player;
			const float NumberW = HudMeasure(FString::FromInt(Points), 22.0f, TEXT("BlackItalic"));
			const float PointsW = NumberW + (bPartyRow ? 26.0f : 0.0f);
			if (bPartyRow)
			{
				const FVector2D IconAt(RowLeft + RowWidth - 16.0f - NumberW - 14.0f, Y + RowHeight * 0.5f);
				const float IconTime = static_cast<float>(Clock) + Row * 0.8f;
				if (Match->Rules.IsCrownKeep())
				{
					const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(GetWorld());
					ChaosImpactParty::PaintCrown(HudAbove(Words, 1), IconAt, 10.0f, IconTime, Crown && Crown->GetHolderState() == Player ? 1.0f : 0.3f);
				}
				else
				{
					ChaosImpactParty::PaintRuby(HudAbove(Words, 1), IconAt, 9.5f, IconTime);
				}
			}
			Words.Text(Label, RowLeft + 46.0f, Y + 3.0f, HudFit(Label, 15.0f, RowWidth - 16.0f - PointsW - 10.0f - 46.0f), bOwn ? Paper : WithAlpha(Paper, 0.85f),
				ETextAlign::Left, TEXT("Black"), 2.0f, Navy);
			HudText3D(Words, FString::FromInt(Points), RowLeft + RowWidth - 16.0f, Y - 3.0f, 22.0f, bOwn ? IceFill.Top : Paper, ETextAlign::Right);
			// Online: their connection, small, just past the strip (the host's own machine has none).
			if (Player && Match->bOnlineRoom && !Player->IsABot())
			{
				const float PingMs = Player->GetPingInMilliseconds();
				const FString PingText = Player->bHostMachine ? FString(TEXT("HOST")) : PingMs > 0.0f ? FString::Printf(TEXT("%dms"), FMath::RoundToInt(PingMs))
					: FString(TEXT("--ms"));
				const FLinearColor PingColor = Player->bHostMachine || PingMs <= 60.0f ? FLinearColor(0.2f, 0.95f, 0.45f)
					: PingMs <= 120.0f ? FLinearColor(0.65f, 0.95f, 0.3f) : PingMs <= 200.0f ? Gold : Fire;
				Words.Text(PingText, RowLeft + RowWidth + 8.0f, Y + 7.0f, 12.0f, WithAlpha(PingColor, 0.9f), ETextAlign::Left, TEXT("Bold"), 1.5f, Navy);
			}
		};
		// パーティルール: rubies (with this machine's own pickups at once), or the crown's counts left.
		const auto ShownOf = [this, &Match](const AChaosImpactPlayerState* Member)
		{
			return Match->Rules.IsParty() ? ChaosImpactParty::GetShownCount(GetWorld(), Member) : Member->Points;
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
				PaintRow(OwnRow, 0, ColorOf(Own), NameOf(Own), Own->Points, true, Own);
			}
		}
		else
		{
			const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
			int32 Rank = 0;
			for (int32 Index = 0; Index < FMath::Min(3, Ranking.Num()); ++Index)
			{
				Rank = Index == 0 || Ranking[Index]->Points != Ranking[Index - 1]->Points ? Index + 1 : Rank;
				PaintRow(Index, Rank, ColorOf(Ranking[Index]), NameOf(Ranking[Index]), ShownOf(Ranking[Index]),
					Ranking[Index] == Own, Ranking[Index]);
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
				PaintRow(3, OwnRank, ColorOf(Own), NameOf(Own), ShownOf(Own), true, Own);
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
			// Over the name: a tag in the medal's colours (gold, silver, bronze; night blue after that) with the place read
			// as one word, the number large and 位 right after it, out of how many beside. It lands large when it changes.
			const FGeometry BottomLeft = MakeAnchor(AllottedGeometry, 0.0f, Size.Y, S);
			constexpr float BadgeLeft = 40.0f;
			constexpr float BadgeTop = -236.0f;
			constexpr float BadgeHeight = 58.0f;
			const FString RankText = FString::FromInt(OwnRank);
			const FString OfText = FString::Printf(TEXT("/%d"), Places);
			const float NumberWidth = HudMeasure(RankText, 46.0f, TEXT("BlackItalic"));
			const float PlaceWidth = HudMeasure(TEXT("位"), 26.0f, TEXT("Black"));
			const float BadgeWidth = 26.0f + NumberWidth + 4.0f + PlaceWidth + 8.0f + HudMeasure(OfText, 18.0f, TEXT("BlackItalic")) + 22.0f;
			const float Pop = 1.0f + 0.5f * FMath::Exp(-9.0f * ChangeAge);
			const FHudFill Medal = OwnRank == 1 ? FireFill : OwnRank == 2 ? SilverFill : OwnRank == 3 ? BronzeFill : NightFill;
			const FGeometry BadgeSpace = MakeSkewed(BottomLeft, BadgeLeft, BadgeTop, BadgeWidth, BadgeHeight, 0.0f, Pop);
			Occupy(BottomLeft, BadgeLeft - 8.0f, BadgeTop - 26.0f, BadgeWidth + 50.0f, BadgeHeight + 34.0f);
			HudShard(FPainter{BadgeSpace, OutDrawElements, BaseLayer}, {FVector2D(14.0, 0.0), FVector2D(BadgeWidth, 0.0),
				FVector2D(BadgeWidth - 14.0f, BadgeHeight), FVector2D(0.0, BadgeHeight)}, Medal, 3.5f, FVector2D(5.0, 6.0));
			if (OwnRank == 1)
			{
				// A glint running across first place now and then.
				const float Travel = static_cast<float>(FMath::Fmod(Clock, 2.6)) / 0.6f;
				if (Travel < 1.0f)
				{
					const float X = -10.0f + (BadgeWidth + 20.0f) * Travel;
					HudStroke(FPainter{BadgeSpace, OutDrawElements, BaseLayer + 3}, {FVector2D(X + 12.0f, 3.0), FVector2D(X, BadgeHeight - 3.0f)},
						FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 8.0f);
				}
			}
			if (ChangeAge < 0.6f)
			{
				HudShatter(FPainter{BottomLeft, OutDrawElements, BaseLayer + 6}, FVector2D(BadgeLeft + BadgeWidth * 0.5f, BadgeTop + BadgeHeight * 0.5f),
					ChangeAge / 0.6f, 20.0f, Medal, 8, 5.0f);
			}
			const FPainter Words{BadgeSpace, OutDrawElements, BaseLayer + 4};
			HudText3D(Words, RankText, 22.0f, -4.0f, 46.0f, Paper, ETextAlign::Left);
			HudText3D(Words, TEXT("位"), 26.0f + NumberWidth, 14.0f, 26.0f, Paper, ETextAlign::Left, TEXT("Black"));
			HudText3D(Words, OfText, 34.0f + NumberWidth + PlaceWidth, 24.0f, 17.0f, Paper, ETextAlign::Left);
			if (bTeams)
			{
				HudText3D(FPainter{BottomLeft, OutDrawElements, BaseLayer + 4}, TEXT("チーム"), BadgeLeft + 10.0f, BadgeTop - 22.0f, 14.0f,
					IceFill.Top, ETextAlign::Left, TEXT("Black"));
			}
			// パーティルール: this player's own rubies (or counts left), large, beside the place.
			if (Match->Rules.IsParty())
			{
				const float ChipLeft = BadgeLeft + BadgeWidth + 18.0f;
				CwPaintPartyCount(FPainter{BottomLeft, OutDrawElements, BaseLayer + 4}, GetWorld(), Own,
					FVector2D(ChipLeft + 52.0f, BadgeTop + BadgeHeight - 6.0f), 1.0f, 1.65f);
				Occupy(BottomLeft, ChipLeft - 10.0f, BadgeTop - 4.0f, 140.0f, BadgeHeight + 8.0f);
			}

			if (OwnRankDelta != 0 && ChangeAge < 1.6f)
			{
				const bool bUp = OwnRankDelta > 0;
				const FPainter Arrow{BottomLeft, OutDrawElements, BaseLayer + 2, 1.0f - FMath::Clamp((ChangeAge - 1.1f) / 0.5f, 0.0f, 1.0f)};
				Arrow.Text(bUp ? TEXT("▲") : TEXT("▼"), BadgeLeft + BadgeWidth + 10.0f,
					BadgeTop + 12.0f + (bUp ? -1.0f : 1.0f) * 8.0f * EaseOut(ChangeAge / 0.4f), 22.0f,
					bUp ? FLinearColor(0.2f, 1.0f, 0.45f) : Fire, ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
			}
		}

		// Points just scored float up beside this player's own line.
		const float GainAge = static_cast<float>(Clock - PointsGainedAt);
		if (OwnRow != INDEX_NONE && GainAge < 0.9f && PointsGained > 0)
		{
			HudText3D(FPainter{TopLeft, OutDrawElements, BaseLayer + 7, 1.0f - GainAge / 0.9f}, FString::Printf(TEXT("+%d"), PointsGained), 300.0f,
				RowTop + OwnRow * 36.0f - 12.0f * EaseOut(GainAge / 0.9f), 20.0f, FireFill.Top, ETextAlign::Left);
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
	// Split participants need room to see their arena: use one compact shared overlay, scaled for the window.
	const UGameInstance* OverlayGI = GetGameInstance();
	const float S = GetHudScale(Size.X, Size.Y) * (OverlayGI && OverlayGI->GetNumLocalPlayers() > 1 ? .72f : 1.f);
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
		constexpr float CW = 600.0f;
		constexpr float CH = 112.0f;
		const FPainter P{MakeSkewed(TopLeft, 40.0f, 36.0f, CW, CH, -0.18f), OutDrawElements, BaseLayer};
		const FLinearColor Tone = bJoining ? Gold : Ice;
		CwCard(P, CW, CH, Tone, 8.0f);
		CwRadar(HudAbove(P, 2), FVector2D(66.0, CH * 0.5), 38.0f, static_cast<float>(Clock), Tone, bJoining);
		const FPainter T = HudAbove(P, 3);
		T.Text(TEXT("合言葉マッチ"), 124.0f, 12.0f, 15.0f, Tone, ETextAlign::Left, TEXT("BlackItalic"));
		const FString Title = bJoining ? TEXT("へやに入ります") : TEXT("へやをさがしています");
		HudText3D(HudAbove(T, 1), Title, 124.0f, 32.0f, HudFit(Title, 30.0f, CW - 150.0f), Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
		const FString Password = FString::Printf(TEXT("あいことば %s"), *Sessions->GetPassword());
		const float TagW = HudMeasure(Password, 15.0f, TEXT("Black")) + 24.0f;
		RoundedBox(T, 124.0f, 76.0f, TagW, 24.0f, 12.0f, Gold);
		HudAbove(T, 1).Text(Password, 124.0f + TagW * 0.5f, 78.0f, 15.0f, Ink, ETextAlign::Center, TEXT("Black"));
		NoticeY = 160.0f;
	}

	// ランダムマッチ: searching (or just found) while waiting in local training.
	if (Sessions && (!Room || !Room->bOnlineRoom) && Sessions->IsQueueing())
	{
		const bool bFound = Sessions->IsQueueMatched() || Sessions->IsDuelOffered();
		const bool bDuel = Sessions->GetQueueMode() == EChaosImpactQueueMode::Duel;
		const int32 Elapsed = FMath::Max(0, FMath::FloorToInt(Clock - Sessions->GetQueueStartedAt()));
		constexpr float CW = 660.0f;
		constexpr float CH = 124.0f;
		const FPainter P{MakeSkewed(TopLeft, 40.0f, 36.0f, CW, CH, -0.18f), OutDrawElements, BaseLayer};
		const FLinearColor Tone = bFound ? Gold : bDuel ? Gold : Fire;
		CwCard(P, CW, CH, Tone, 8.0f);
		if (bFound)
		{
			// Found: the card lights up gold for a moment.
			HudAbove(P, 1).Box(0.0f, 0.0f, CW, CH, WithAlpha(Gold, 0.12f + 0.08f * FMath::Sin(static_cast<float>(Clock) * 8.0f)));
		}
		CwRadar(HudAbove(P, 2), FVector2D(70.0, CH * 0.5), 42.0f, static_cast<float>(Clock), Tone, bFound);
		const FPainter T = HudAbove(P, 3);
		T.Text(bDuel ? TEXT("RANDOM MATCH  1v1") : TEXT("RANDOM MATCH  8 PLAYERS"), 134.0f, 12.0f, 15.0f, Tone, ETextAlign::Left, TEXT("BlackItalic"));
		const FString Title = bFound ? FString(TEXT("見つかりました！")) : bDuel ? FString(TEXT("1v1の相手をさがしています"))
			: FString(TEXT("対戦相手をさがしています"));
		HudText3D(HudAbove(T, 1), Title, 134.0f, 32.0f, HudFit(Title, 30.0f, CW - 160.0f), bFound ? Gold : Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
		if (bFound)
		{
			T.Text(TEXT("まもなく対戦がはじまります"), 136.0f, 80.0f, 18.0f, Paper, ETextAlign::Left, TEXT("Bold"));
		}
		else
		{
			// The time searched, then tags: how many are searching, when an 8-player match starts, the line to the relay.
			HudText3D(HudAbove(T, 1), FString::Printf(TEXT("%d:%02d"), Elapsed / 60, Elapsed % 60), 136.0f, 76.0f, 24.0f, Paper,
				ETextAlign::Left, TEXT("BlackItalic"), 1.5f);
			float TagX = 136.0f + HudMeasure(TEXT("0:00"), 24.0f, TEXT("BlackItalic")) + 18.0f;
			const auto Tag = [&T, &TagX, CW](const FString& Word, const FLinearColor& Fill, const FLinearColor& Letters)
			{
				const float TagW = HudMeasure(Word, 14.0f, TEXT("Black")) + 22.0f;
				if (TagX + TagW > CW - 16.0f)
				{
					return;
				}
				RoundedBox(T, TagX, 80.0f, TagW, 24.0f, 12.0f, Fill);
				HudAbove(T, 1).Text(Word, TagX + TagW * 0.5f, 82.0f, 14.0f, Letters, ETextAlign::Center, TEXT("Black"));
				TagX += TagW + 8.0f;
			};
			if (Sessions->GetQueueWaitingCount() > 0)
			{
				Tag(FString::Printf(TEXT("さがしている人 %d"), Sessions->GetQueueWaitingCount()), WithAlpha(Paper, 0.16f), Paper);
			}
			if (!bDuel && Sessions->GetQueueStartsIn() >= 0.0f)
			{
				Tag(FString::Printf(TEXT("あと%d秒で開始"), FMath::CeilToInt(Sessions->GetQueueStartsIn())), Gold, Ink);
			}
			const int32 Rtt = Sessions->GetLastRelayRtt();
			if (Rtt > 0)
			{
				const FLinearColor Line = Rtt <= 60 ? FLinearColor(0.2f, 0.9f, 0.45f) : Rtt <= 120 ? FLinearColor(0.65f, 0.95f, 0.3f) : Rtt <= 200 ? Gold : Fire;
				Tag(FString::Printf(TEXT("回線 %dms"), Rtt), WithAlpha(Line, 0.85f), Ink);
			}
			Tag(TEXT("ポーズでやめる"), WithAlpha(Paper, 0.1f), WithAlpha(Paper, 0.8f));
		}
		NoticeY = 172.0f;
	}

	// Room notices such as "the room was dissolved".
	if (Sessions && !Sessions->GetNotice().IsEmpty() && Clock - Sessions->GetNoticeTime() < 4.0)
	{
		const float Age = static_cast<float>(Clock - Sessions->GetNoticeTime());
		const float Alpha = EaseOut(Age / 0.25f) * FMath::Clamp((4.0f - Age) / 0.4f, 0.0f, 1.0f);
		const FString Notice = Sessions->GetNotice();
		const float NoticeSize = HudFit(Notice, 26.0f, 600.0f);
		const float NoticeW = FMath::Max(320.0f, HudMeasure(Notice, NoticeSize, TEXT("Black")) + 70.0f);
		const FPainter P{MakeSkewed(TopLeft, 40.0f + (1.0f - EaseOut(Age / 0.25f)) * -60.0f, NoticeY + 6.0f, NoticeW, 58.0f, -0.18f), OutDrawElements,
			BaseLayer, Alpha};
		P.Box(7.0f, 8.0f, NoticeW, 58.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		HudPoly(P, {FVector2D(0.0, 0.0), FVector2D(NoticeW, 0.0), FVector2D(NoticeW, 58.0), FVector2D(0.0, 58.0)},
			MakeFill(FMath::Lerp(Fire, Paper, 0.18f), Fire * FLinearColor(0.62f, 0.62f, 0.62f, 1.0f)));
		HudAbove(P, 1).Box(0.0f, 0.0f, NoticeW, 2.0f, WithAlpha(Paper, 0.5f));
		HudText3D(HudAbove(P, 2), Notice, 30.0f, 29.0f - NoticeSize * 0.72f, NoticeSize, Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
	}

	if (!Room || !Room->bOnlineRoom)
	{
		return;
	}
	// Join / leave / play-or-watch notes in the top-right corner, under the lobby's rules panel when it is shown.
	{
		const bool bLobbyPanels = !Room->bVersusMatch && !Room->bRoomMatchLevel
			&& (Room->Phase == EChaosImpactOnlinePhase::Lobby || Room->Phase == EChaosImpactOnlinePhase::Starting);
		const float ToastTop = !bLobbyPanels ? 36.0f : Room->bRulesDecided ? 384.0f : Room->bRecruitmentClosed ? 112.0f : 36.0f;
		PaintRoomToasts(AllottedGeometry, OutDrawElements, BaseLayer + 1, ToastTop);
	}
	// A public tournament (and any tournament being played) has its own screens (UChaosImpactTournamentWidget), not the
	// 合言葉 room's lobby.
	if (Room->bPublicTournament || Room->IsTournament())
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
	if (!Room->bVersusMatch && !Room->bRoomMatchLevel
		&& (Room->Phase == EChaosImpactOnlinePhase::Lobby || Room->Phase == EChaosImpactOnlinePhase::Starting))
	{
		// Every card of the lobby is drawn alike (the rules panel too): a soft shadow, a deep night-blue body shading
		// down, a faint rim; Accent is the strip down its left edge.
		const auto CardRect = [](const float X, const float Y, const float W, const float H)
		{
			return TArray<FVector2D>{FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)};
		};
		const auto DrawCard = [&CardRect](const FPainter& C, const float W, const float H, const FLinearColor& Accent, const float AccentW,
			const bool bBright)
		{
			C.Box(9.0f, 10.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.34f));
			C.Box(4.0f, 5.0f, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
			HudPoly(C, CardRect(0.0f, 0.0f, W, H), bBright
				? MakeFill(FLinearColor(0.06f, 0.09f, 0.16f, 0.96f), FLinearColor(0.012f, 0.018f, 0.04f, 0.96f))
				: MakeFill(FLinearColor(0.035f, 0.05f, 0.1f, 0.94f), FLinearColor(0.006f, 0.008f, 0.02f, 0.95f)));
			const FPainter Up = HudAbove(C, 1);
			if (AccentW > 0.0f)
			{
				HudPoly(Up, CardRect(0.0f, 0.0f, AccentW, H), MakeFill(FMath::Lerp(Accent, Paper, 0.25f), Accent * FLinearColor(0.6f, 0.6f, 0.6f, 1.0f)));
			}
			Up.Box(0.0f, 0.0f, W, 1.5f, WithAlpha(Paper, 0.16f));
			Up.Outline(0.0f, 0.0f, W, H, WithAlpha(Paper, 0.1f), 1.0f);
		};
		// A rounded tag; returns its width.
		const auto Tag = [](const FPainter& C, const float X, const float Y, const FString& Word, const float TextSize, const FLinearColor& Fill,
			const FLinearColor& Letters)
		{
			const float W = HudMeasure(Word, TextSize, TEXT("Black")) + TextSize * 1.1f;
			const float H = TextSize * 1.6f;
			RoundedBox(C, X, Y, W, H, H * 0.5f, Fill);
			HudAbove(C, 1).Text(Word, X + W * 0.5f, Y + (H - TextSize * 1.32f) * 0.5f, TextSize, Letters, ETextAlign::Center, TEXT("Black"));
			return W;
		};

		// The room card: its name, the password and whether it recruits on tags, how full it is (a capsule per place).
		constexpr float ListW = 500.0f;
		{
			const FGeometry Header = MakeSkewed(TopLeft, 40.0f, 36.0f, ListW, 88.0f, -0.12f);
			const FPainter H{Header, OutDrawElements, BaseLayer};
			DrawCard(H, ListW, 88.0f, Ice, 0.0f, false);
			const FPainter H2 = HudAbove(H, 2);
			HudPoly(H2, CardRect(0.0f, 0.0f, ListW, 4.0f), MakeFill(FMath::Lerp(Ice, Paper, 0.3f), Ice));
			H2.Box(0.0f, 84.0f, ListW * 0.5f, 4.0f, Ice);
			H2.Box(ListW * 0.5f, 84.0f, ListW * 0.5f, 4.0f, Fire);
			const FString Title = Room->RoomName.IsEmpty() ? FString::Printf(TEXT("へや  %s"), *Room->RoomPassword) : Room->RoomName;
			const float TitleSize = FMath::Clamp(30.0f * 300.0f / FMath::Max(HudMeasure(Title, 30.0f, TEXT("Black")), 1.0f), 18.0f, 30.0f);
			HudText3D(H2, Title, 22.0f, 8.0f + (30.0f - TitleSize) * 0.5f, TitleSize, Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
			float TagX = 22.0f;
			TagX += Tag(HudAbove(H2, 2), TagX, 52.0f, FString::Printf(TEXT("あいことば %s"), *Room->RoomPassword), 14.0f, Gold, Ink) + 8.0f;
			if (Room->bRecruitmentClosed)
			{
				Tag(HudAbove(H2, 2), TagX, 52.0f, TEXT("募集終了"), 14.0f, Fire, Paper);
			}
			else if (!Room->IsMatchmade())
			{
				Tag(HudAbove(H2, 2), TagX, 52.0f, TEXT("募集中"), 14.0f, Ice, Paper);
			}
			const bool bFull = Members.Num() >= AChaosImpactGameState::MaxMembers;
			HudText3D(H2, FString::Printf(TEXT("%d/%d"), Members.Num(), AChaosImpactGameState::MaxMembers), ListW - 22.0f, 4.0f, 34.0f,
				bFull ? Fire : Gold, ETextAlign::Right, TEXT("BlackItalic"), 2.0f);
			for (int32 Place = 0; Place < AChaosImpactGameState::MaxMembers; ++Place)
			{
				const bool bTaken = Place < Members.Num();
				const float X = ListW - 22.0f - (AChaosImpactGameState::MaxMembers - Place) * 17.0f + 3.0f;
				RoundedBox(HudAbove(H2, 1), X, 58.0f, 12.0f, 14.0f, 4.0f, bTaken ? (bFull ? Fire : Gold) : WithAlpha(Paper, 0.12f));
			}
		}

		// One card per member: their colour, name, the character they play, tags for the host and this machine's
		// players, 準備OK, and the connection as bars and milliseconds.
		constexpr float RowH = 60.0f;
		constexpr float RowGap = 8.0f;
		const float RowsTop = 138.0f;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const AChaosImpactPlayerState* Member = Members[Index];
			const double* SeenAt = MemberSeenAt.Find(Member);
			const float Age = SeenAt ? static_cast<float>(Clock - *SeenAt) : 1.0f;
			const float In = EaseOut(Age / 0.3f);
			const FLinearColor Color = Member->ColourChoice >= 0 ? ChaosImpactRoster::GetColourSwatch(Member->ColourChoice)
				: MemberColors[FMath::Abs(Member->JoinOrder) % 8];
			const bool bMine = LocalStates.Contains(Member);
			const FGeometry Row = MakeSkewed(TopLeft, 40.0f - (1.0f - In) * 480.0f, RowsTop + Index * (RowH + RowGap), ListW, RowH, -0.12f);
			const FPainter R{Row, OutDrawElements, BaseLayer, In};
			DrawCard(R, ListW, RowH, Color, 12.0f, bMine);
			const FPainter R2 = HudAbove(R, 2);
			// The order they came in.
			R2.Text(FString::FromInt(Index + 1), 36.0f, 13.0f, 26.0f, WithAlpha(Paper, 0.3f), ETextAlign::Center, TEXT("BlackItalic"));
			const FString Name = Member->GetPlayerName();
			// A long name is set smaller, so the tags after it always fit.
			const float NameSize = 24.0f * FMath::Min(1.0f, 160.0f / FMath::Max(HudMeasure(Name, 24.0f, TEXT("Black")), 1.0f));
			HudText3D(R2, Name, 58.0f, 4.0f + (24.0f - NameSize) * 0.6f, NameSize, Paper, ETextAlign::Left, TEXT("Black"), 1.5f);
			float ChipX = 58.0f + FMath::Min(HudMeasure(Name, NameSize, TEXT("Black")), 160.0f) + 12.0f;
			if (Member->bRoomHost)
			{
				ChipX += Tag(HudAbove(R2, 2), ChipX, 9.0f, TEXT("ホスト"), 12.0f, Gold, Ink) + 5.0f;
			}
			if (bMine)
			{
				ChipX += Tag(HudAbove(R2, 2), ChipX, 9.0f, TEXT("あなた"), 12.0f, Ice, Paper) + 5.0f;
			}
			const FString CharacterName = ChaosImpactRoster::Get(Member->CharacterIndex).Name;
			R2.Text(CharacterName, 58.0f, 35.0f, 15.0f, WithAlpha(Paper, 0.62f), ETextAlign::Left, TEXT("Bold"));
			// Team rules decided: the team this member picked, after the character.
			if (Room->bRulesDecided && Room->Rules.IsTeamBattle() && Member->TeamIndex >= 0)
			{
				const FString TeamWord = FString::Printf(TEXT("%sチーム"), ChaosImpactMatch::GetTeamName(Member->TeamIndex));
				const float TeamX = 58.0f + FMath::Min(HudMeasure(CharacterName, 15.0f, TEXT("Bold")), 150.0f) + 10.0f;
				Tag(HudAbove(R2, 2), TeamX, 34.0f, TeamWord, HudFit(TeamWord, 12.0f, 340.0f - TeamX - 14.0f), ChaosImpactMatch::GetTeamColor(Member->TeamIndex),
					Member->TeamIndex == 2 ? Ink : Paper);
			}
			if (Room->bRulesDecided)
			{
				// 準備OK at a glance: a lit green check, or an empty box while still waiting.
				constexpr float BoxLeft = 352.0f;
				if (Member->bReadyForMatch)
				{
					RoundedBox(R2, BoxLeft, 13.0f, 34.0f, 34.0f, 8.0f, FLinearColor(0.16f, 0.9f, 0.42f, 1.0f));
					const FPainter Check = HudAbove(R2, 1);
					Check.Line(FVector2D(BoxLeft + 8.0f, 30.0f), FVector2D(BoxLeft + 15.0f, 38.0f), Paper, 5.0f);
					Check.Line(FVector2D(BoxLeft + 14.0f, 38.0f), FVector2D(BoxLeft + 27.0f, 20.0f), Paper, 5.0f);
				}
				else
				{
					RoundedBox(R2, BoxLeft, 13.0f, 34.0f, 34.0f, 8.0f, WithAlpha(Paper, 0.28f));
					RoundedBox(HudAbove(R2, 1), BoxLeft + 2.0f, 15.0f, 30.0f, 30.0f, 7.0f, FLinearColor(0.01f, 0.014f, 0.03f, 1.0f));
				}
			}
			// The connection: bars, and the round trip in milliseconds under them. A pair shares one connection;
			// the host's own machine has no delay at all.
			const float PingMs = Member->GetPingInMilliseconds();
			const bool bLocalToHost = Member->bHostMachine;
			const int32 Level = bLocalToHost || PingMs <= 60.0f ? 4 : PingMs <= 120.0f ? 3 : PingMs <= 200.0f ? 2 : 1;
			const FLinearColor SignalColor = Level == 4 ? FLinearColor(0.2f, 0.95f, 0.45f)
				: Level == 3 ? FLinearColor(0.65f, 0.95f, 0.3f) : Level == 2 ? Gold : Fire;
			const bool bMeasured = bLocalToHost || PingMs > 0.0f;
			for (int32 Bar = 0; Bar < 4; ++Bar)
			{
				const float BarHeight = 6.0f + Bar * 6.0f;
				RoundedBox(R2, 412.0f + Bar * 10.0f, 31.0f - BarHeight, 7.0f, BarHeight, 2.0f,
					bMeasured && Bar < Level ? SignalColor : WithAlpha(Paper, 0.14f));
			}
			const FString PingText = bLocalToHost ? FString(TEXT("ホスト")) : !bMeasured ? FString(TEXT("--ms"))
				: FString::Printf(TEXT("%dms"), FMath::RoundToInt(PingMs));
			R2.Text(PingText, 432.0f, 35.0f, 13.0f, bLocalToHost ? Muted : SignalColor, ETextAlign::Center, TEXT("Bold"));
			if (bMine)
			{
				HudAbove(R2, 3).Outline(-2.0f, -2.0f, ListW + 4.0f, RowH + 4.0f, WithAlpha(Ice, 0.9f), 2.0f);
			}
			if (Age < 0.45f)
			{
				HudAbove(R2, 4).Box(0.0f, 0.0f, ListW, RowH, WithAlpha(Paper, 0.8f * (1.0f - Age / 0.45f)));
			}
		}
		float ListBottom = RowsTop + Members.Num() * (RowH + RowGap);
		// While places are open: an empty card waiting for the next member, the balls hopping on it.
		if (!Room->bRecruitmentClosed && Members.Num() < AChaosImpactGameState::MaxMembers)
		{
			const FPainter E{MakeSkewed(TopLeft, 40.0f, ListBottom, ListW, 46.0f, -0.12f), OutDrawElements, BaseLayer,
				0.75f + 0.25f * FMath::Sin(static_cast<float>(Clock) * 2.5f)};
			RoundedBox(E, 0.0f, 0.0f, ListW, 46.0f, 6.0f, FLinearColor(0.012f, 0.016f, 0.03f, 0.5f));
			// A dashed rim: an empty place.
			const FPainter Dash = HudAbove(E, 1);
			for (float X = 6.0f; X < ListW - 6.0f; X += 18.0f)
			{
				Dash.Box(X, 0.0f, 10.0f, 2.0f, WithAlpha(Paper, 0.3f));
				Dash.Box(X, 44.0f, 10.0f, 2.0f, WithAlpha(Paper, 0.3f));
			}
			Dash.Text(TEXT("メンバーを待っています"), 26.0f, 9.0f, 20.0f, WithAlpha(Paper, 0.75f), ETextAlign::Left, TEXT("Bold"));
			HudWaitBalls(HudAbove(E, 2), FVector2D(ListW - 70.0, 37.0), static_cast<float>(Clock), 13.0f);
			ListBottom += 54.0f;
		}

		// Spectators under the players, and the key that switches between playing and watching.
		const TArray<AChaosImpactPlayerState*> Watchers = Room->GetSpectators();
		const float WatchTop = ListBottom + 4.0f;
		const FPainter W{MakeSkewed(TopLeft, 40.0f, WatchTop, ListW, 38.0f, -0.12f), OutDrawElements, BaseLayer};
		OccupyShared(TopLeft, 20.0f, 0.0f, ListW + 50.0f, WatchTop + 104.0f);
		TArray<FString> WatcherNames;
		for (const AChaosImpactPlayerState* Watcher : Watchers)
		{
			WatcherNames.Add(Watcher->GetPlayerName());
		}
		DrawCard(W, ListW, 38.0f, Gold, 8.0f, false);
		const FPainter W2 = HudAbove(W, 2);
		W2.Text(TEXT("観戦"), 24.0f, 6.0f, 19.0f, Watchers.IsEmpty() ? Muted : Gold, ETextAlign::Left, TEXT("Black"));
		W2.Text(FString::Printf(TEXT("%d/%d"), Watchers.Num(), AChaosImpactGameState::MaxSpectators), 76.0f, 6.0f, 19.0f,
			Watchers.IsEmpty() ? Muted : Paper, ETextAlign::Left, TEXT("BlackItalic"));
		const FString WatcherLine = FString::Join(WatcherNames, TEXT("・"));
		W2.Text(WatcherLine, 130.0f, 8.0f, HudFit(WatcherLine, 17.0f, ListW - 150.0f, TEXT("Bold")), Paper, ETextAlign::Left, TEXT("Bold"));
		if (const UGameInstance* LocalGI = GetGameInstance(); LocalGI && !Room->bRecruitmentClosed)
		{
			for (int32 Index = 0; Index < LocalGI->GetNumLocalPlayers(); ++Index)
			{
				const auto* SwitchController = Cast<AChaosImpactPlayerController>(LocalGI->GetLocalPlayers()[Index]->GetPlayerController(GetWorld()));
				if (!SwitchController || !SwitchController->CanToggleSpectating()) { continue; }
				const FString Keys = ChaosImpactSettings::DescribeAction(SwitchController, EChaosImpactAction::LobbySpectate);
				const float Y = 46.0f + Index * 28.0f;
				float X = 24.0f;
				if (LocalGI->GetNumLocalPlayers() > 1)
				{
					W2.Text(FString::Printf(TEXT("%dP"), Index + 1), X, Y + 2.0f, 16.0f, Muted, ETextAlign::Left, TEXT("BlackItalic"));
					X += 32.0f;
				}
				const float TagX = X;
				X += Tag(HudAbove(W2, 1), X, Y, Keys, 14.0f, WithAlpha(Paper, 0.9f), Ink) + 8.0f;
				const FString SwitchLine = SwitchController->IsSpectating() ? TEXT("長押しでプレイヤーに戻る") : TEXT("長押しで観戦にする");
				W2.Text(SwitchLine, X, Y + 2.0f, HudFit(SwitchLine, 17.0f, FMath::Max(60.0f, ListW - X - 16.0f), TEXT("Bold")),
					WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
				// How far the hold has gone, under the key.
				if (const float Held = SwitchController->GetSpectateHoldProgress(); Held > 0.0f)
				{
					HudAbove(W2, 2).Box(TagX, Y + 22.0f, (X - 8.0f - TagX) * Held, 3.0f, Gold);
				}
			}
		}

		if (!Room->bRecruitmentClosed && !Room->IsMatchmade())
		{
			const auto* Host = Cast<AChaosImpactPlayerController>(GetOwningPlayer());
			if (Host && Host->IsOnlineRoomHost())
			{
				const bool bCanClose = Host->CanCloseRecruitment();
				const float Progress = Host->GetRecruitmentHoldProgress();
				const bool bNarrow = Size.X / S < 1500.f;
				// (Where the rules panel comes later: the top-right corner for one player here.)
				const bool bCorner = LocalStates.Num() <= 1 && Size.X / S >= 40.0f + ListW + 40.0f + 440.0f + 170.0f;
				const FGeometry Hint = bCorner ? MakeSkewed(MakeAnchor(AllottedGeometry, Size.X, 0.0f, S), -440.0f - 168.0f, 78.0f, 420.f, 104.f, -.12f)
					: bNarrow ? MakeSkewed(TopLeft, 40.f, WatchTop + 78.f, 420.f, 104.f, -.12f)
					: MakeSkewed(TopCenter, -210.f, 36.f, 420.f, 104.f, -.12f);
				const FPainter P{Hint, OutDrawElements, BaseLayer + 2};
				OccupyShared(Hint, -10.0f, -10.0f, 440.0f, 124.0f);
				DrawCard(P, 420.0f, 104.0f, bCanClose ? Gold : Muted, 7.0f, false);
				const FPainter P2 = HudAbove(P, 2);
				HudText3D(P2, bCanClose ? TEXT("長押しで募集終了") : TEXT("メンバーの参加を待っています"), 24.0f, 10.0f,
					HudFit(bCanClose ? TEXT("長押しで募集終了") : TEXT("メンバーの参加を待っています"), bCanClose ? 28.f : 22.f, 372.0f),
					Paper, ETextAlign::Left, TEXT("Black"), 2.0f);
				if (bCanClose)
				{
					const float KeyW = Tag(HudAbove(P2, 1), 24.0f, 50.0f, ChaosImpactSettings::DescribeAction(Host, EChaosImpactAction::LobbyReady), 15.0f,
						WithAlpha(Paper, 0.9f), Ink);
					P2.Text(FString::Printf(TEXT("を %.1f秒長押し"), AChaosImpactPlayerController::RecruitmentHoldSeconds), 34.0f + KeyW, 51.0f,
						HudFit(FString::Printf(TEXT("を %.1f秒長押し"), AChaosImpactPlayerController::RecruitmentHoldSeconds), 18.0f, 386.0f - KeyW, TEXT("Bold")),
						Paper, ETextAlign::Left, TEXT("Bold"));
					RoundedBox(P2, 24.0f, 84.0f, 372.0f, 8.0f, 4.0f, WithAlpha(Paper, 0.12f));
					RoundedBox(HudAbove(P2, 1), 24.0f, 84.0f, 372.0f * Progress, 8.0f, 4.0f, Gold);
				}
				else
				{
					P2.Text(TEXT("参加者がそろったら、ここから締め切れます"), 24.0f, 50.0f, HudFit(TEXT("参加者がそろったら、ここから締め切れます"), 16.0f, 372.0f, TEXT("Bold")),
						Muted, ETextAlign::Left, TEXT("Bold"));
					HudWaitBalls(HudAbove(P2, 1), FVector2D(384.0, 92.0), static_cast<float>(Clock), 11.0f);
				}
			}
		}

		if (Room->bRulesDecided)
		{
			// The decided rules, who is ready, and how long until the match starts by itself.
			const FChaosImpactMatchRules& Rules = Room->Rules;
			const int32 Ready = Room->CountReadyMembers();
			const double ServerNow = Room->GetServerWorldTimeSeconds();
			const float WaitLeft = Room->ReadyDeadline > 0.0
				? FMath::Max(0.0f, static_cast<float>(Room->ReadyDeadline - ServerNow)) : 0.0f;
			const bool bStartingNow = Room->Phase == EChaosImpactOnlinePhase::Starting;
			// Whether this machine still presses 準備OK (its key is shown at the bottom of the panel).
			const AChaosImpactPlayerState* OwnState = LocalStates.IsEmpty() ? nullptr : Cast<AChaosImpactPlayerState>(LocalStates[0]);
			const bool bPrompt = !bStartingNow && Room->Phase == EChaosImpactOnlinePhase::Lobby && !(OwnState && OwnState->bSpectating);
			constexpr float PW = 440.0f;
			// Team rules: a line per player on this machine with the team they are on and the key that changes it.
			TArray<TPair<int32, const AChaosImpactPlayerController*>> TeamPickers;
			if (const UGameInstance* LocalGI = GetGameInstance(); LocalGI && Rules.IsTeamBattle() && !bStartingNow)
			{
				for (int32 Index = 0; Index < LocalGI->GetNumLocalPlayers(); ++Index)
				{
					const auto* Picker = Cast<AChaosImpactPlayerController>(LocalGI->GetLocalPlayers()[Index]->GetPlayerController(GetWorld()));
					if (Picker && Picker->CanChangeLobbyTeam())
					{
						TeamPickers.Add({Index, Picker});
					}
				}
			}
			const float TB = TeamPickers.IsEmpty() ? 0.0f : 12.0f + 34.0f * TeamPickers.Num();
			// Who on this machine still presses 準備OK: each of a split screen's players presses their own (a line each).
			TArray<TPair<int32, const AChaosImpactPlayerController*>> ReadyPressers;
			if (const UGameInstance* LocalGI = GetGameInstance(); LocalGI && bPrompt)
			{
				for (int32 Index = 0; Index < LocalGI->GetNumLocalPlayers(); ++Index)
				{
					const auto* Presser = Cast<AChaosImpactPlayerController>(LocalGI->GetLocalPlayers()[Index]->GetPlayerController(GetWorld()));
					if (Presser && Presser->CanToggleReady())
					{
						ReadyPressers.Add({Index, Presser});
					}
				}
			}
			const int32 ReadyRows = bPrompt ? FMath::Max(ReadyPressers.Num(), 1) : 0;
			const float PH = (bPrompt ? 248.0f + 40.0f * ReadyRows : 246.0f) + TB;
			// One player on this machine: the top-right corner, under the 練習設定 button and left of the column of buttons. A split screen: the top
			// centre, or under the member list when the view is too narrow for that.
			const bool bSinglePlayer = LocalStates.Num() <= 1;
			const bool bNarrow = Size.X / S < 2.0f * (40.0f + ListW + 30.0f + PW * 0.5f);
			const FGeometry TopRightCorner = MakeAnchor(AllottedGeometry, Size.X, 0.0f, S);
			const FGeometry Panel = bSinglePlayer && Size.X / S >= 40.0f + ListW + 40.0f + PW + 170.0f
				? MakeSkewed(TopRightCorner, -PW - 168.0f, 78.0f, PW, PH, -0.12f)
				: bNarrow ? MakeSkewed(TopLeft, 40.0f, WatchTop + 76.0f, PW, PH, -0.12f)
				: MakeSkewed(TopCenter, -PW * 0.5f, 36.0f, PW, PH, -0.12f);
			const FPainter P{Panel, OutDrawElements, BaseLayer};
			OccupyShared(Panel, -10.0f, -10.0f, PW + 20.0f, PH + 20.0f);
			const FPainter P2 = HudAbove(P, 1);
			const FPainter P3 = HudAbove(P, 2);
			const auto Rect = [](const float X, const float Y, const float W, const float H)
			{
				return TArray<FVector2D>{FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H)};
			};
			const FLinearColor ReadyGreen(0.16f, 0.9f, 0.42f, 1.0f);
			// The card: a soft shadow, a deep night-blue body, a gold title band with a thin ice edge under it.
			DrawCard(P, PW, PH, Gold, 0.0f, false);
			HudPoly(P2, Rect(0.0f, 0.0f, PW, 52.0f), MakeFill(FLinearColor(1.0f, 0.86f, 0.38f, 1.0f), FLinearColor(0.95f, 0.55f, 0.04f, 1.0f)));
			P2.Box(0.0f, 0.0f, PW, 2.0f, WithAlpha(Paper, 0.55f));
			P2.Box(0.0f, 52.0f, PW, 3.0f, Ice);
			P2.Outline(0.0f, 0.0f, PW, PH, WithAlpha(Paper, 0.14f), 1.5f);
			P3.Text(TEXT("MATCH RULES"), 22.0f, 2.0f, 12.0f, WithAlpha(Ink, 0.6f), ETextAlign::Left, TEXT("BlackItalic"));
			P3.Text(TEXT("ルール"), 22.0f, 16.0f, 24.0f, Ink, ETextAlign::Left, TEXT("Black"));
			// ルール確認: its key, beside the title (the rules in full over the screen).
			const float CardKeyLeft = 22.0f + HudMeasure(TEXT("ルール"), 24.0f, TEXT("Black")) + 18.0f;
			float CardKeyRight = CardKeyLeft;
			if (const auto* CardOwner = Cast<AChaosImpactPlayerController>(GetOwningPlayer()); CardOwner && CardOwner->CanOpenRuleCard())
			{
				const FString CardKey = ChaosImpactSettings::DescribeAction(CardOwner, EChaosImpactAction::RuleCard);
				const float CardKeyW = FMath::Max(HudMeasure(CardKey, 14.0f, TEXT("Black")) + 14.0f, 26.0f);
				RoundedBox(P3, CardKeyLeft, 17.0f, CardKeyW, 24.0f, 6.0f, WithAlpha(Ink, 0.85f));
				HudAbove(P3, 1).Text(CardKey, CardKeyLeft + CardKeyW * 0.5f, 20.0f, 14.0f, Paper, ETextAlign::Center, TEXT("Black"));
				HudAbove(P3, 1).Text(TEXT("くわしく"), CardKeyLeft + CardKeyW + 6.0f, 20.0f, 15.0f, WithAlpha(Ink, 0.85f), ETextAlign::Left, TEXT("Black"));
				CardKeyRight = CardKeyLeft + CardKeyW + 12.0f + HudMeasure(TEXT("くわしく"), 15.0f, TEXT("Black"));
			}
			// The match's kind as the rules screen names it; a duel or a tournament is played in rounds in the duel arena,
			// not for some minutes on a stage.
			const bool bDuelRules = Rules.bTournament || Rules.bDuel;
			const FString Where = bDuelRules ? FString(ChaosImpactMatch::GetDuelStageName()) : ChaosImpactMatch::GetStageLabel(Rules.StageIndex);
			P3.Text(Where, PW - 22.0f, 16.0f, HudFit(Where, 20.0f, FMath::Max(60.0f, PW - 22.0f - CardKeyRight - 14.0f)), Ink, ETextAlign::Right, TEXT("Black"));

			// Chips on the right of the kind: the length, and the CPUs and how strong.
			const int32 CPULevel = Rules.bTournament ? (Rules.Tournament.CPUs.IsEmpty() ? INDEX_NONE : Rules.Tournament.CPUs[0].Level) : Rules.CPULevel;
			TArray<TPair<FString, FLinearColor>> Chips;
			if (!bDuelRules)
			{
				Chips.Add({FString::Printf(TEXT("%d分"), Rules.Minutes), Ice});
			}
			Chips.Add({Rules.CPUCount > 0 ? FString::Printf(TEXT("CPU %d（%s）"), Rules.CPUCount,
				CPULevel < 0 ? TEXT("おまかせ") : ChaosImpactMatch::GetCPULevelName(CPULevel)) : FString(TEXT("CPUなし")),
				Rules.CPUCount > 0 ? Fire : Muted});
			// The kind, big, then the chips on their own line under it.
			const FString Kind = ChaosImpactMatch::DescribeRule(Rules).Replace(TEXT("  "), TEXT(" "));
			const float KindSize = FMath::Clamp(30.0f * (PW - 52.0f) / FMath::Max(HudMeasure(Kind, 30.0f, TEXT("Black")), 1.0f), 16.0f, 30.0f);
			HudText3D(P3, Kind, 22.0f, 62.0f + (30.0f - KindSize) * 0.55f, KindSize, Rules.bTournament || Rules.IsTeamBattle() ? Gold : Paper,
				ETextAlign::Left, TEXT("Black"), 2.0f);
			float ChipLeft = 22.0f;
			// (Smaller when they would not fit across the panel.)
			float ChipsWidth = 0.0f;
			for (const TPair<FString, FLinearColor>& Each : Chips)
			{
				ChipsWidth += HudMeasure(Each.Key, 15.0f, TEXT("Black")) + 32.0f;
			}
			const float ChipText = ChipsWidth > PW - 44.0f ? 15.0f * (PW - 44.0f - Chips.Num() * 32.0f) / FMath::Max(ChipsWidth - Chips.Num() * 32.0f, 1.0f) : 15.0f;
			for (const TPair<FString, FLinearColor>& Each : Chips)
			{
				const float ChipW = HudMeasure(Each.Key, ChipText, TEXT("Black")) + 24.0f;
				RoundedBox(P2, ChipLeft, 104.0f, ChipW, 26.0f, 13.0f, WithAlpha(Each.Value, 0.22f));
				RoundedBox(P3, ChipLeft, 104.0f, 5.0f, 26.0f, 2.5f, Each.Value);
				P3.Text(Each.Key, ChipLeft + ChipW * 0.5f + 2.0f, 107.0f + (15.0f - ChipText) * 0.6f, ChipText, Each.Value == Muted ? Muted : Paper, ETextAlign::Center, TEXT("Black"));
				ChipLeft += ChipW + 8.0f;
			}
			P2.Box(22.0f, 142.0f, PW - 44.0f, 1.0f, WithAlpha(Paper, 0.12f));

			// The team band: per player here, the team (its colour) and the key that moves to the next one.
			for (int32 Line = 0; Line < TeamPickers.Num(); ++Line)
			{
				const AChaosImpactPlayerController* Picker = TeamPickers[Line].Value;
				const AChaosImpactPlayerState* PickerState = Picker->GetPlayerState<AChaosImpactPlayerState>();
				const int32 Team = PickerState ? PickerState->TeamIndex : INDEX_NONE;
				const float Y = 152.0f + Line * 34.0f;
				float X = 22.0f;
				if (TeamPickers.Num() > 1)
				{
					P3.Text(FString::Printf(TEXT("%dP"), TeamPickers[Line].Key + 1), X, Y + 3.0f, 17.0f, Muted, ETextAlign::Left, TEXT("BlackItalic"));
					X += 34.0f;
				}
				// (Shorter with two lines: the team's name alone, and で変更.)
				const bool bShort = TeamPickers.Num() > 1;
				const FString TeamWord = Team >= 0 ? FString::Printf(TEXT("%s%s"), ChaosImpactMatch::GetTeamName(Team), bShort ? TEXT("") : TEXT("チーム"))
					: FString(TEXT("未定"));
				const FLinearColor TeamColor = Team >= 0 ? ChaosImpactMatch::GetTeamColor(Team) : Muted;
				const float TeamW = HudMeasure(TeamWord, 17.0f, TEXT("Black")) + 26.0f;
				RoundedBox(P2, X, Y, TeamW, 28.0f, 8.0f, TeamColor);
				P3.Text(TeamWord, X + TeamW * 0.5f, Y + 3.0f, 17.0f, Team == 2 ? Ink : Paper, ETextAlign::Center, TEXT("Black"));
				X += TeamW + 12.0f;
				const FString TeamKeys = ChaosImpactSettings::DescribeAction(Picker, EChaosImpactAction::LobbyTeam);
				const float KeyW = FMath::Max(HudMeasure(TeamKeys, 16.0f, TEXT("Black")) + 18.0f, 32.0f);
				RoundedBox(P3, X, Y + 2.0f, KeyW, 24.0f, 6.0f, WithAlpha(Paper, 0.9f));
				HudAbove(P3, 1).Text(TeamKeys, X + KeyW * 0.5f, Y + 3.0f, 16.0f, Ink, ETextAlign::Center, TEXT("Black"));
				X += KeyW + 8.0f;
				const TCHAR* const ChangeWord = bShort ? TEXT("で変更") : TEXT("でチーム変更");
				P3.Text(ChangeWord, X, Y + 3.0f, HudFit(ChangeWord, 17.0f, FMath::Max(40.0f, PW - 22.0f - X), TEXT("Bold")),
					WithAlpha(Paper, 0.85f), ETextAlign::Left, TEXT("Bold"));
			}
			if (TB > 0.0f)
			{
				P2.Box(22.0f, TB + 142.0f, PW - 44.0f, 1.0f, WithAlpha(Paper, 0.12f));
			}

			// 準備OK: how many, and a capsule per member, lit green when ready.
			P3.Text(TEXT("準備OK"), 22.0f, TB + 152.0f, 20.0f, WithAlpha(Paper, 0.9f), ETextAlign::Left, TEXT("Black"));
			const bool bAllReady = Ready >= Members.Num() && Members.Num() > 0;
			HudText3D(P3, FString::Printf(TEXT("%d / %d"), Ready, Members.Num()), PW - 22.0f, TB + 146.0f, 28.0f, bAllReady ? ReadyGreen : Paper,
				ETextAlign::Right, TEXT("BlackItalic"), 2.0f);
			const int32 Pips = FMath::Max(1, Members.Num());
			const float PipGap = 6.0f;
			const float PipW = FMath::Min(44.0f, (PW - 44.0f - PipGap * (Pips - 1)) / Pips);
			for (int32 Pip = 0; Pip < Pips; ++Pip)
			{
				const bool bLit = Pip < Ready;
				const float X = 22.0f + Pip * (PipW + PipGap);
				RoundedBox(P2, X, TB + 184.0f, PipW, 12.0f, 6.0f, bLit ? ReadyGreen : WithAlpha(Paper, 0.1f));
				if (bLit)
				{
					RoundedBox(P3, X + 4.0f, TB + 186.0f, PipW - 8.0f, 3.0f, 1.5f, WithAlpha(Paper, 0.55f));
				}
			}

			// The time left before the match starts without waiting for everyone (someone still on the last match's
			// results holds it back).
			const float WaitAlpha = FMath::Clamp(WaitLeft / AChaosImpactGameState::ReadyWaitSeconds, 0.0f, 1.0f);
			int32 Viewing = 0;
			for (const APlayerState* Member : Room->PlayerArray)
			{
				const AChaosImpactPlayerState* Viewer = Cast<AChaosImpactPlayerState>(Member);
				Viewing += Viewer && Viewer->bViewingResults ? 1 : 0;
			}
			const bool bHurry = bStartingNow || WaitLeft <= 10.0f;
			RoundedBox(P2, 22.0f, TB + 210.0f, PW - 44.0f, 8.0f, 4.0f, WithAlpha(Paper, 0.1f));
			RoundedBox(P3, 22.0f, TB + 210.0f, (PW - 44.0f) * (bStartingNow ? 1.0f : WaitAlpha), 8.0f, 4.0f, bHurry ? Fire : Ice);
			P3.Text(bStartingNow ? FString(TEXT("まもなく開始"))
				: Viewing > 0 ? FString::Printf(TEXT("成績を見ている人を待っています（%d人）"), Viewing)
				: FString(TEXT("自動スタートまで")),
				22.0f, TB + 222.0f, HudFit(Viewing > 0 ? FString::Printf(TEXT("成績を見ている人を待っています（%d人）"), Viewing) : FString(TEXT("まもなく開始")), 17.0f, PW - 44.0f, TEXT("Bold")), Viewing > 0 ? Gold : bStartingNow ? Fire : Muted, ETextAlign::Left, TEXT("Bold"));
			if (!bStartingNow && Viewing == 0)
			{
				P3.Text(FString::Printf(TEXT("%d秒"), FMath::CeilToInt(WaitLeft)), PW - 22.0f, TB + 220.0f, 20.0f, bHurry ? Fire : Paper,
					ETextAlign::Right, TEXT("Black"));
			}

			// How this machine presses 準備OK, and whether it already has (only the key for the device in use now).
			if (bPrompt)
			{
				if (ReadyPressers.IsEmpty())
				{
					ReadyPressers.Add({0, Cast<AChaosImpactPlayerController>(GetOwningPlayer())});
				}
				const bool bSplit = ReadyPressers.Num() > 1;
				for (int32 Row = 0; Row < ReadyPressers.Num(); ++Row)
				{
					const AChaosImpactPlayerController* InputController = ReadyPressers[Row].Value;
					const AChaosImpactPlayerState* RowState = InputController ? InputController->GetPlayerState<AChaosImpactPlayerState>() : OwnState;
					const bool bOwnReady = RowState && RowState->bReadyForMatch;
					const float Pulse = bOwnReady ? 1.0f : 0.72f + 0.28f * FMath::Sin(static_cast<float>(Clock) * 5.0f + Row);
					const FString ReadyKeys = ChaosImpactSettings::DescribeAction(InputController, EChaosImpactAction::LobbyReady);
					const float KeyWidth = FMath::Max(HudMeasure(ReadyKeys, 18.0f, TEXT("Black")) + 22.0f, 38.0f);
					const float Y = PH - 40.0f * (ReadyPressers.Num() - Row);
					// A strip across the bottom per player: green once ready, else their key pulsing (1P, 2P in their colours).
					P2.Box(0.0f, Y, PW, 40.0f, bOwnReady ? WithAlpha(ReadyGreen, 0.18f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
					P2.Box(0.0f, Y, PW, 1.0f, WithAlpha(Paper, 0.1f));
					float X = 22.0f;
					if (bSplit)
					{
						const int32 LocalIndex = ReadyPressers[Row].Key;
						RoundedBox(P3, X, Y + 7.0f, 38.0f, 26.0f, 6.0f, PlayerAccents[LocalIndex % 4]);
						P3.Text(FString::Printf(TEXT("%dP"), LocalIndex + 1), X + 19.0f, Y + 8.0f, 17.0f, Ink, ETextAlign::Center, TEXT("BlackItalic"));
						X += 46.0f;
					}
					RoundedBox(P3, X, Y + 7.0f, KeyWidth, 26.0f, 6.0f, WithAlpha(Paper, Pulse));
					P3.Text(ReadyKeys, X + KeyWidth * 0.5f, Y + 8.0f, 18.0f, Ink, ETextAlign::Center, TEXT("Black"));
					const TCHAR* Line = bOwnReady ? TEXT("準備OK！　もう一度でキャンセル") : TEXT("で準備OK");
					P3.Text(Line, X + KeyWidth + 12.0f, Y + 8.0f, HudFit(Line, 19.0f, PW - 34.0f - X - KeyWidth),
						bOwnReady ? ReadyGreen : Paper, ETextAlign::Left, TEXT("Black"));
				}
			}
		}
		else if (Room->bRecruitmentClosed && Room->Phase == EChaosImpactOnlinePhase::Lobby)
		{
			// (Where the rules panel will be: the top-right corner for one player here, else the top centre.)
			const bool bSinglePlayer = LocalStates.Num() <= 1 && Size.X / S >= 40.0f + ListW + 40.0f + 440.0f + 170.0f;
			const FGeometry Chip = bSinglePlayer ? MakeSkewed(MakeAnchor(AllottedGeometry, Size.X, 0.0f, S), -440.0f - 168.0f, 78.0f, 440.0f, 64.0f, -0.12f)
				: MakeSkewed(TopCenter, -220.0f, 36.0f, 440.0f, 64.0f, -0.12f);
			const FPainter P{Chip, OutDrawElements, BaseLayer};
			OccupyShared(Chip, -10.0f, -10.0f, 460.0f, 84.0f);
			DrawCard(P, 440.0f, 64.0f, Gold, 8.0f, false);
			HudText3D(HudAbove(P, 2), TEXT("ホストがルールを決めています"), 30.0f, 14.0f, HudFit(TEXT("ホストがルールを決めています"), 25.0f, 330.0f),
				WithAlpha(Paper, 0.8f + 0.2f * FMath::Sin(static_cast<float>(Clock) * 4.0f)), ETextAlign::Left, TEXT("Black"), 2.0f);
			HudWaitBalls(HudAbove(P, 3), FVector2D(396.0, 46.0), static_cast<float>(Clock), 12.0f);
		}
	}

}

void UChaosImpactChargeWidget::SetBlindAmount(const float Amount)
{
	if (Amount > 0.0f && BlindAmount <= 0.0f)
	{
		BlindShownAt = FPlatformTime::Seconds();
	}
	BlindAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
}

float UChaosImpactChargeWidget::GetSmokeCover() const
{
	if (BlindAmount <= 0.0f || bSpectatorView)
	{
		return 0.0f;
	}
	// Billows in over a quarter of a second, holds, and thins out over its last second.
	const float Age = static_cast<float>(FPlatformTime::Seconds() - BlindShownAt);
	const float In = 1.0f - FMath::Pow(1.0f - FMath::Clamp(Age / 0.25f, 0.0f, 1.0f), 3.0f);
	return FMath::Clamp(In * BlindAmount, 0.0f, 1.0f);
}

void UChaosImpactChargeWidget::PaintSmokeVeil(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const double Clock = FPlatformTime::Seconds();
	const float Age = static_cast<float>(Clock - BlindShownAt);
	const float In = 1.0f - FMath::Pow(1.0f - FMath::Clamp(Age / 0.25f, 0.0f, 1.0f), 3.0f);
	const float Thick = GetSmokeCover();
	const float Time = static_cast<float>(FMath::Fmod(Clock, 1000.0));
	const FPainter Veil{AllottedGeometry, OutDrawElements, Layer, Thick};
	// A murky base over everything, then big soft clouds rolling across it.
	Veil.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.3f, 0.29f, 0.34f, 0.8f));
	const FPainter Clouds{AllottedGeometry, OutDrawElements, Layer + 1, Thick};
	const float Span = FMath::Max(Size.X, Size.Y);
	constexpr int32 CloudCount = 14;
	for (int32 Index = 0; Index < CloudCount; ++Index)
	{
		// Scattered by a fixed pattern, each drifting on its own slow loop.
		const float Seed = Index * 12.9898f;
		const float BaseX = FMath::Frac(FMath::Sin(Seed) * 43758.55f);
		const float BaseY = FMath::Frac(FMath::Sin(Seed + 3.1f) * 24634.63f);
		const float DriftX = 0.06f * FMath::Sin(Time * (0.23f + 0.05f * (Index % 5)) + Index);
		const float DriftY = 0.05f * FMath::Cos(Time * (0.19f + 0.04f * (Index % 4)) + Index * 1.7f);
		const FVector2D Center((BaseX + DriftX) * Size.X, (BaseY + DriftY) * Size.Y);
		const float Radius = Span * (0.16f + 0.11f * FMath::Frac(FMath::Sin(Seed + 7.7f) * 9123.1f)) * (0.7f + 0.3f * In);
		const FLinearColor Tone = Index % 3 == 0 ? FLinearColor(0.2f, 0.19f, 0.23f) : FLinearColor(0.52f, 0.5f, 0.58f);
		// Soft edges: a few faint layers, each a little off-centre, so no rim shows as a line. (Every layer is drawn
		// over much of the screen, so they are kept few.)
		constexpr int32 Layers = 4;
		for (int32 Ring = 0; Ring < Layers; ++Ring)
		{
			const float Shrink = 1.0f - Ring * 0.2f;
			const FVector2D Wobble(FMath::Sin(Seed + Ring * 1.9f + Time * 0.3f), FMath::Cos(Seed + Ring * 2.3f + Time * 0.25f));
			Clouds.Disc(Center + Wobble * Radius * 0.06f, Radius * Shrink, FLinearColor(Tone.R, Tone.G, Tone.B, 0.15f));
		}
	}
	// Thickest in the middle of the view, where the player looks.
	const FVector2D Middle(Size.X * 0.5f, Size.Y * 0.5f);
	for (int32 Ring = 0; Ring < 4; ++Ring)
	{
		Clouds.Disc(Middle, Span * (0.34f - Ring * 0.07f), FLinearColor(0.4f, 0.39f, 0.45f, 0.17f));
	}
}
