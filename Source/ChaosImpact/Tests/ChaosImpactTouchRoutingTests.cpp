#if WITH_DEV_AUTOMATION_TESTS
// スマホ版: real touches put through Slate (as a phone's screen sends them) reach the game in a match: a held right thumb
// charges a throw, a left drag walks. Logs what is under the finger (a widget taking the touch would stop the game from
// getting it). Run on a local match: Lvl_VS_Stage1?CIMatch=1?CIMatchCPU=1?CIMinutes=3 with -CITouch.
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactTouchControls.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

namespace
{
	class FTouchRoutingCommand : public IAutomationLatentCommand
	{
	public:
		explicit FTouchRoutingCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		static FString DescribeUnder(const FVector2D& Screen)
		{
			FString Out;
			if (!FSlateApplication::IsInitialized())
			{
				return Out;
			}
			TArray<TSharedRef<SWindow>> Windows;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
			const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Screen, Windows);
			for (int32 Index = Path.Widgets.Num() - 1; Index >= 0 && Index >= Path.Widgets.Num() - 6; --Index)
			{
				Out += Path.Widgets[Index].Widget->GetTypeAsString() + TEXT(" < ");
			}
			return Out;
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0) { Test->AddError(FString::Printf(TEXT("Touch routing stalled at step %d"), Step)); return true; }
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			UGameViewportClient* Viewport = GEngine->GameViewport;
			TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr;
			if (!Own || !Window.IsValid() || Now < WaitUntil) { return false; }
			Own->RefillHealth();
			const FVector2D Origin = Window->GetPositionInScreen();
			const FVector2D Size = Window->GetClientSizeInScreen();
			// (ボタン方式: the 投げる button.)
			const FVector2D RightPoint = Origin + FChaosImpactTouchGestures::MakeLayout(Size, false, true).Throw;
			const FVector2D LeftPoint = Origin + Size * FVector2D(0.22, 0.55);
			FSlateApplication& Slate = FSlateApplication::Get();
			const TSharedPtr<FGenericWindow> Native = Window->GetNativeWindow();
			const FPlatformUserId User = FPlatformMisc::GetPlatformUserForUserIndex(0);
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 20.0;
				Step = 1;
				return false;
			case 1:
				if (const AChaosImpactPlayerController* Player = Cast<AChaosImpactPlayerController>(PC))
				{
					const AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
					UE_LOG(LogTemp, Display, TEXT("TOUCHROUTE screen %d gameplay %d phase %d paused %d"), static_cast<int32>(Player->GetCurrentScreen()),
						Player->IsGameplayActive() ? 1 : 0, Match ? static_cast<int32>(Match->Phase) : -1, World->IsPaused() ? 1 : 0);
				}
				UE_LOG(LogTemp, Display, TEXT("TOUCHROUTE under right thumb: %s"), *DescribeUnder(RightPoint));
				UE_LOG(LogTemp, Display, TEXT("TOUCHROUTE under left thumb: %s"), *DescribeUnder(LeftPoint));
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Normal);
				Slate.OnTouchStarted(Native, RightPoint, 1.0f, 0, User, FInputDeviceId::CreateFromInternalId(0));
				WaitUntil = Now + 0.4;
				Step = 2;
				return false;
			case 2:
				Slate.OnTouchMoved(RightPoint + FVector2D(40.0, 0.0), 1.0f, 0, User, FInputDeviceId::CreateFromInternalId(0));
				WaitUntil = Now + 0.15;
				Step = 3;
				return false;
			case 3:
				Test->TestTrue(TEXT("A held right thumb charges in the match"), Own->IsChargingThrow());
				Slate.OnTouchEnded(RightPoint + FVector2D(40.0, 0.0), 0, User, FInputDeviceId::CreateFromInternalId(0));
				WaitUntil = Now + 0.4;
				Step = 4;
				return false;
			case 4:
				Test->TestEqual(TEXT("Let go: thrown"), Own->GetCarriedBallCount(), 1);
				WalkFrom = Own->GetActorLocation();
				Slate.OnTouchStarted(Native, LeftPoint, 1.0f, 1, User, FInputDeviceId::CreateFromInternalId(0));
				WaitUntil = Now + 0.2;
				Step = 5;
				return false;
			case 5:
				Slate.OnTouchMoved(LeftPoint + FVector2D(80.0, 0.0), 1.0f, 1, User, FInputDeviceId::CreateFromInternalId(0));
				WaitUntil = Now + 0.8;
				Step = 6;
				return false;
			case 6:
				Test->TestTrue(TEXT("A left drag walks in the match"), FVector::Dist2D(Own->GetActorLocation(), WalkFrom) > 150.0);
				Slate.OnTouchEnded(LeftPoint + FVector2D(80.0, 0.0), 1, User, FInputDeviceId::CreateFromInternalId(0));
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Step = 0;
		FVector WalkFrom = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactTouchRoutingTest, "ChaosImpact.Touch.Routing",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactTouchRoutingTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTouchRoutingCommand(this));
	return true;
}
#endif
