#include "ChaosImpactDemoPlay.h"

#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSpectatorPawn.h"
#include "Components/WidgetComponent.h"
#include "ContentStreaming.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

/** Notices any button (a key, a mouse button, a stick pushed well over) while the demo shows. */
class FChaosImpactDemoInputWatcher : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		bPressed = true;
		return false;
	}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		bPressed = true;
		return false;
	}
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		bPressed |= FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.6f;
		return false;
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("ChaosImpactDemoInput"); }

	bool bPressed = false;
};

namespace
{
	/** Mid-battle demos: the match runs this much faster behind a dark screen until it is well under way. */
	constexpr float FastForward = 6.0f;
}

// =====================================================================================================================
// The demo

AChaosImpactDemoPlay::AChaosImpactDemoPlay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AChaosImpactDemoPlay* AChaosImpactDemoPlay::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AChaosImpactDemoPlay> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AChaosImpactDemoPlay::BeginPlay()
{
	Super::BeginPlay();
	// From the battle's opening, or dropped into the middle of it.
	bFromStart = FMath::RandBool();
	if (const TCHAR* Forced = FCommandLine::Get(); FParse::Param(Forced, TEXT("CIDemoFromStart")))
	{
		bFromStart = true;
	}
	else if (FParse::Param(Forced, TEXT("CIDemoMidBattle")))
	{
		bFromStart = false;
	}
	MidBattleAt = FMath::FRandRange(25.0f, 80.0f);
	// Shown once it has got where it starts from and what is in view has loaded.
	bShowing = false;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		Widget = CreateWidget<UChaosImpactDemoWidget>(PC, UChaosImpactDemoWidget::StaticClass());
		if (Widget)
		{
			Widget->SetDemo(this);
			Widget->AddToViewport(10050);
		}
		PC->SetShowMouseCursor(false);
	}
	if (FSlateApplication::IsInitialized())
	{
		Watcher = MakeShared<FChaosImpactDemoInputWatcher>();
		FSlateApplication::Get().RegisterInputPreProcessor(Watcher);
	}
	UE_LOG(LogTemp, Display, TEXT("DEMO play begins %s"), bFromStart ? TEXT("from the opening") : *FString::Printf(TEXT("mid-battle (%.0f s in)"), MidBattleAt));
}

void AChaosImpactDemoPlay::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Watcher.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Watcher);
	}
	Watcher.Reset();
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	Super::EndPlay(EndPlayReason);
}

AActor* AChaosImpactDemoPlay::GetFollowed() const
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AChaosImpactSpectatorPawn* Viewer = PC ? Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()) : nullptr;
	return Viewer ? Viewer->GetFollowedCharacter() : nullptr;
}

void AChaosImpactDemoPlay::Leave()
{
	if (!bLeaving)
	{
		bLeaving = true;
		UE_LOG(LogTemp, Display, TEXT("DEMO play ends after %.1f s"), Age);
	}
}

void AChaosImpactDemoPlay::HideOtherUI()
{
	UWorld* World = GetWorld();
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Other = *It;
		if (Other == Widget || Other->GetWorld() != World || !Other->IsInViewport())
		{
			continue;
		}
		// Every other UI, the opening's words (Ready?, GO) too.
		if (Other->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Other->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	for (TObjectIterator<UWidgetComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsVisible())
		{
			It->SetVisibility(false);
		}
	}
}

void AChaosImpactDemoPlay::FollowSomeone(const bool bSnap)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AChaosImpactSpectatorPawn* Viewer = PC ? Cast<AChaosImpactSpectatorPawn>(PC->GetPawn()) : nullptr;
	if (!Viewer)
	{
		return;
	}
	Viewer->SetHudHidden(true);
	// Knocked out a moment ago: on to another CPU still playing.
	AChaosImpactCharacter* Wanted = Chosen.Get();
	if (Wanted && Wanted->IsEliminated())
	{
		DownSeconds += 0.5f;
	}
	else
	{
		DownSeconds = 0.0f;
	}
	if (!Wanted || DownSeconds >= 1.5f)
	{
		// One of the CPUs still playing, at random.
		TArray<AChaosImpactCharacter*> Playing;
		for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsEliminated() && *It != Wanted)
			{
				Playing.Add(*It);
			}
		}
		if (Playing.IsEmpty())
		{
			return;
		}
		Wanted = Playing[FMath::RandRange(0, Playing.Num() - 1)];
		Chosen = Wanted;
		DownSeconds = 0.0f;
		UE_LOG(LogTemp, Display, TEXT("DEMO follows %s"), *Wanted->GetName());
	}
	// Through the spectator's own following (so it keeps to that camera), stepping round to the one chosen.
	for (int32 I = 0; I < 9 && Viewer->GetFollowedCharacter() != Wanted; ++I)
	{
		Viewer->CycleFollow(1);
	}
	if (AChaosImpactPlayerController* Menu = Cast<AChaosImpactPlayerController>(PC))
	{
		// The opening's camera (as a player sees it) dives into this CPU's view.
		Menu->SetDemoIntroCharacter(Wanted);
	}
	if (bSnap && PC->GetViewTarget() != Wanted && Viewer->GetFollowedCharacter() == Wanted)
	{
		PC->SetViewTarget(Wanted);
	}
}

void AChaosImpactDemoPlay::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Real seconds (the match may be running fast, or held, behind the dark).
	const double ClockNow = FPlatformTime::Seconds();
	const float Real = LastClock > 0.0 ? static_cast<float>(FMath::Min(ClockNow - LastClock, 0.25)) : 0.0f;
	LastClock = ClockNow;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AChaosImpactGameState* Match = GetWorld()->GetGameState<AChaosImpactGameState>();
	const bool bVersus = Match && Match->bVersusMatch;
	const bool bOn = bVersus && Match->Phase == EChaosImpactOnlinePhase::Match;
	const bool bOpening = bVersus && Match->Phase == EChaosImpactOnlinePhase::Intro;
	const float MatchSeconds = bOn ? Match->GetPhaseElapsedSeconds() : 0.0f;
	HideOtherUI();
	if (PC)
	{
		PC->SetShowMouseCursor(false);
	}

	if (!bShowing)
	{
		if (Watcher.IsValid())
		{
			Watcher->bPressed = false;
		}
		WaitSeconds += Real;
		if (!bLoading)
		{
			// Up to where it is shown from: the opening's first moment, or well into the match (run fast behind the dark).
			const bool bThere = bFromStart ? (bOpening || bOn) : (bOn && MatchSeconds >= MidBattleAt);
			UGameplayStatics::SetGlobalTimeDilation(this, bFromStart ? 1.0f : FastForward);
			if (bThere || WaitSeconds > 40.0f)
			{
				// Held there while what is in view loads.
				bLoading = true;
				LoadingSeconds = 0.0f;
				SettledSeconds = 0.0f;
				UGameplayStatics::SetGlobalTimeDilation(this, 0.0001f);
				FollowSomeone(true);
			}
			return;
		}
		LoadingSeconds += Real;
		FollowSomeone(!bOpening);
		const int32 Wanting = IStreamingManager::Get().GetNumWantingResources();
		SettledSeconds = Wanting == 0 ? SettledSeconds + Real : 0.0f;
		if ((LoadingSeconds > 1.5f && SettledSeconds > 0.5f) || LoadingSeconds > 10.0f)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
			bShowing = true;
			UE_LOG(LogTemp, Display, TEXT("DEMO shows the match (%s, %.0f s in) after %.1f s loading, %d still wanting"),
				bOpening ? TEXT("its opening") : TEXT("under way"), MatchSeconds, LoadingSeconds, Wanting);
		}
		return;
	}

	const float Before = Age;
	Age += Real;
	// Tests (or -CIDemoShots): pictures of the first seconds (the opening, when the demo starts there).
	if (GIsAutomationTesting || FParse::Param(FCommandLine::Get(), TEXT("CIDemoShots")))
	{
		for (const float At : {1.0f, 3.0f, 5.5f})
		{
			if (Before < At && Age >= At)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DemoPlayQA"),
					FString::Printf(TEXT("0_%s_%.1fs.png"), bFromStart ? TEXT("Opening") : TEXT("MidBattle"), At)), true, false);
			}
		}
	}
	NextFollowCheck -= Real;
	if (NextFollowCheck <= 0.0f)
	{
		NextFollowCheck = 0.5f;
		FollowSomeone(false);
	}

	if (!bLeaving && Watcher.IsValid() && Watcher->bPressed)
	{
		Watcher->bPressed = false;
		if (Age > 1.0f)
		{
			Leave();
		}
	}
	if (!bLeaving && Age >= Length)
	{
		Leave();
	}
	if (bLeaving)
	{
		Leaving = FMath::Min(1.0f, Leaving + Real / 0.5f);
		if (Leaving >= 1.0f && !bTravelled)
		{
			bTravelled = true;
			UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
			if (AChaosImpactPlayerController* Menu = Cast<AChaosImpactPlayerController>(PC))
			{
				Menu->EndDemoPlay();
			}
		}
	}
}

// =====================================================================================================================
// Its picture

void UChaosImpactDemoWidget::NativeConstruct()
{
	Super::NativeConstruct();
	LogoTexture = FImageUtils::ImportFileAsTexture2D(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("UI/TitleLogoTransparent.png")));
	if (LogoTexture)
	{
		LogoBrush.SetResourceObject(LogoTexture);
		LogoBrush.ImageSize = FVector2D(LogoTexture->GetSizeX(), LogoTexture->GetSizeY());
		LogoBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

int32 UChaosImpactDemoWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	using namespace ChaosImpactPaint;
	const int32 Base = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AChaosImpactDemoPlay* D = Demo.Get();
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (!D || Size.X < 1.0f)
	{
		return Base;
	}
	const float S = FMath::Min(Size.X / 1600.0f, Size.Y / 900.0f);
	const FGeometry Plan = MakeAnchor(AllottedGeometry, (Size.X - 1600.0f * S) * 0.5f, (Size.Y - 900.0f * S) * 0.5f, S);
	const FGeometry Whole = MakeAnchor(AllottedGeometry, 0.0f, 0.0f, 1.0f);
	const float Time = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));
	const float Age = D->GetAge();

	if (D->IsShowing())
	{
		// The logo floating at the top right.
		const float In = EaseOut((Age - 0.3f) / 0.8f);
		if (LogoTexture && In > 0.0f)
		{
			const float Width = 380.0f;
			const float Height = Width * LogoBrush.ImageSize.Y / FMath::Max(LogoBrush.ImageSize.X, 1.0f);
			const float Bob = FMath::Sin(Time * 1.6f) * 6.0f;
			const FVector2f At(1600.0f - 40.0f - Width, 36.0f + Bob);
			FSlateDrawElement::MakeBox(OutDrawElements, Base + 2,
				Plan.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(At)), &LogoBrush, ESlateDrawEffect::None,
				FLinearColor(1.0f, 1.0f, 1.0f, In));
		}
		// PRESS START, as on the title.
		const float PromptIn = EaseOut((Age - 0.8f) / 0.4f);
		if (PromptIn > 0.0f)
		{
			const float Blink = 0.4f + 0.6f * (0.5f + 0.5f * FMath::Cos(Time * 4.2f));
			const FGeometry Prompt = MakeSkewed(Plan, 520, 790, 560, 70, -0.25f);
			const FPainter Row{Prompt, OutDrawElements, Base + 4, PromptIn};
			Row.Box(280.0f - 260.0f * PromptIn, 28, 80, 12, Ice);
			Row.Box(200.0f + 260.0f * PromptIn, 28, 80, 12, Fire);
			Row.Text(TEXT("PRESS START"), 280, 4, 44, WithAlpha(Paper, Blink), ETextAlign::Center, TEXT("Black"), 3.0f, Navy);
		}
	}
	// Coming in from the dark (and dark while a mid-battle demo runs ahead), going out to it.
	const float Dark = D->IsShowing() ? FMath::Max(1.0f - FMath::Clamp(Age / 0.7f, 0.0f, 1.0f), D->GetLeaving()) : 1.0f;
	FPainter{Whole, OutDrawElements, Base + 20}.Box(0.0f, 0.0f, Size.X, Size.Y, FLinearColor(0.0f, 0.0f, 0.0f, Dark));
	return Base + 30;
}
