#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	class FOnlineConnectionGateCommand : public IAutomationLatentCommand
	{
	public:
		explicit FOnlineConnectionGateCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 45.) { Test->AddError(TEXT("Online entry check timed out")); return true; }
			AChaosImpactPlayerController* PC = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()); break;
				}
			}
			if (!PC || Now < NextAt) { return false; }
			if (Step == 0)
			{
				PC->ShowMenuScreen(EChaosImpactScreen::VSSelect);
				PC->BeginVersusOnline();
				Test->TestEqual(TEXT("Online opens the connection gate first"), PC->GetCurrentScreen(), EChaosImpactScreen::OnlineConnecting);
				Test->TestTrue(TEXT("Connection is checked asynchronously"), PC->IsCheckingOnlineConnection());
				PC->CancelOnlineConnection();
				NextAt = Now + 4.; ++Step; return false;
			}
			if (Step == 1)
			{
				Test->TestEqual(TEXT("A cancelled reply never navigates forward"), PC->GetCurrentScreen(), EChaosImpactScreen::VSSelect);
				PC->BeginVersusOnline(); ++Step; return false;
			}
			if (Step == 2)
			{
				if (PC->IsCheckingOnlineConnection()) { return false; }
				const bool bOffline = FParse::Param(FCommandLine::Get(), TEXT("CIGateExpectOffline"));
				Test->TestEqual(TEXT("Only a relay response opens online setup"), PC->GetCurrentScreen(),
					bOffline ? EChaosImpactScreen::OnlineConnecting : EChaosImpactScreen::OnlinePlayers);
				Test->TestEqual(TEXT("Unreachable relay shows a clear error"), !PC->GetOnlineConnectionError().IsEmpty(), bOffline);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WSSQA"),
					bOffline ? TEXT("Connection-Offline.png") : TEXT("Connection-Online.png")), true, false);
				NextAt = Now + .5; ++Step; return false;
			}
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds(), NextAt = 0.;
		int32 Step = 0;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineConnectionGateTest, "ChaosImpact.Online.ConnectionGate",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOnlineConnectionGateTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOnlineConnectionGateCommand(this)); return true;
}
#endif
