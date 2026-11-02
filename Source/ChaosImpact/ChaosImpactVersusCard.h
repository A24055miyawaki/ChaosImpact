#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SLeafWidget.h"

class UTexture;

/** One fighter on the VS card. */
struct FChaosImpactVersusEntrant
{
	/** Big label: P1, CPU2... */
	FString Label;
	/** The character (players) or the strength (CPUs). */
	FString Name;
	/** Smaller line under the name: the colour, or "CPU". */
	FString Detail;
	FLinearColor Color = FLinearColor::White;
	bool bCPU = false;
	/** The fighter's team when the card knows the teams (Teams on the card info); -1 otherwise. */
	int32 Team = -1;
	/** The fighter's model, filmed (a character preview's picture); none: a "?" (CPUs pick theirs in the level). */
	UTexture* Picture = nullptr;
};

/** Everything the VS card shows, gathered before the VS level is opened. */
struct FChaosImpactVersusCardInfo
{
	TArray<FChaosImpactVersusEntrant> Entrants;
	/** The rules in one line (ステージ1 ・ 個人戦 ・ 3分...). */
	FString Footer;
	/**
	 * How many teams the fighters are in, when the card knows (each entrant's Team): two face each other from the two
	 * sides. 0: each for themselves, or the teams are not drawn yet (a local match draws them in its level).
	 */
	int32 Teams = 0;
	/** FPlatformTime::Seconds() when the card first appeared: every copy of the card plays from it. */
	double StartedAt = 0.0;
};

/**
 * The VS card: the match's fighters slide in and "VS" slams down among them. A 1-on-1 or two teams face each other
 * from the two sides; anything else (each for themselves) stands in a ring round the VS, nobody on a side. Shown over the menu
 * as the VS level is about to load, then as that load's loading screen (painted on the loading thread). Both copies
 * share StartedAt, so the second simply carries on from the first. Fighters' models are pictures filmed beforehand.
 */
class SChaosImpactVersusCard : public SLeafWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SChaosImpactVersusCard) {}
		SLATE_ARGUMENT(FChaosImpactVersusCardInfo, Info)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1600.0, 900.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	/** How long the card plays before the level starts loading (the fighters are in and VS has landed). */
	static constexpr float IntroSeconds = 1.6f;
	/** When VS lands (the fighters strike their ready pose then). */
	static constexpr float VersusLandsAt = 0.62f;

	// The pictures stay loaded as long as the card shows them (it outlives the level they were filmed in).
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SChaosImpactVersusCard"); }

private:
	FChaosImpactVersusCardInfo Info;
	TArray<TObjectPtr<UTexture>> Pictures;
	TArray<FSlateBrush> PictureBrushes;
};

/**
 * The VS level's first moments: dark, with the VS emblem the card ended on, until the match's opening starts; then
 * the dark opens from the middle of the screen in a widening circle and the opening shows through.
 */
class SChaosImpactVersusReveal : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SChaosImpactVersusReveal) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1600.0, 900.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	/** Starts opening (once). */
	void Open();
	bool IsOpening() const { return OpenedAt > 0.0; }
	/** Fully open: nothing left to draw. */
	bool IsFinished() const;

	static constexpr float OpenSeconds = 0.9f;

private:
	double CreatedAt = 0.0;
	double OpenedAt = 0.0;
};
