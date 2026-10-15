#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactDemoPlay.generated.h"

class AChaosImpactDemoPlay;
class FChaosImpactDemoInputWatcher;
class UTexture2D;

/** The demo's only picture over the match: the logo floating at the top right, PRESS START, fades in and out. */
UCLASS()
class CHAOSIMPACT_API UChaosImpactDemoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetDemo(AChaosImpactDemoPlay* InDemo) { Demo = InDemo; }

protected:
	virtual void NativeConstruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<AChaosImpactDemoPlay> Demo;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LogoTexture;
	FSlateBrush LogoBrush;
};

/**
 * The title's demo play (a level opened with ?CIDemo=1: a local VS match of CPUs only, on a stage picked at random,
 * watched): the camera of one CPU, no other UI at all, the logo and PRESS START over it. Any button, or a minute,
 * goes back to the title. The player controller starts it after the title has been left alone a while.
 */
UCLASS(NotPlaceable)
class CHAOSIMPACT_API AChaosImpactDemoPlay : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactDemoPlay();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	static AChaosImpactDemoPlay* Find(const UObject* WorldContext);
	/** How long a demo runs before going back to the title. */
	static constexpr float Length = 60.0f;

	/** Seconds the demo has been shown (a mid-battle one first runs ahead behind the dark). */
	float GetAge() const { return Age; }
	bool IsShowing() const { return bShowing; }
	/** Shown from the battle's opening (else dropped into its middle). */
	bool IsFromStart() const { return bFromStart; }
	/** 0 while showing; up to 1 as it fades out to leave. */
	float GetLeaving() const { return Leaving; }
	/** The CPU whose camera is shown. */
	AActor* GetFollowed() const;
	/** Back to the title (a button, or the minute is up). */
	void Leave();

private:
	/** Everything but the demo's own picture hidden. */
	void HideOtherUI();
	/** Keeps the camera on the CPU chosen (another one when it has been knocked out a while); bSnap: no blend. */
	void FollowSomeone(bool bSnap);

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactDemoWidget> Widget;
	TSharedPtr<FChaosImpactDemoInputWatcher> Watcher;
	float Age = 0.0f;
	float Leaving = 0.0f;
	bool bLeaving = false;
	bool bTravelled = false;
	float NextFollowCheck = 0.0f;
	bool bFromStart = true;
	bool bShowing = true;
	float MidBattleAt = 40.0f;
	float WaitSeconds = 0.0f;
	/** How long the CPU followed has been knocked out. */
	float DownSeconds = 0.0f;
	TWeakObjectPtr<class AChaosImpactCharacter> Chosen;
	/** Held still behind the dark while the textures in view load. */
	bool bLoading = false;
	float LoadingSeconds = 0.0f;
	float SettledSeconds = 0.0f;
	double LastClock = 0.0;
};
