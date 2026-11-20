#if WITH_DEV_AUTOMATION_TESTS
// The HIT cards at the top right: hitting a CPU twice shows HIT! and its name (×2), and being hit by it shows 被弾 and
// who did it. Run on a training stage with a CPU (?CITraining=1?CICPUCount=1). Saved/HitFeedQA/HitFeed.png.
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

namespace
{
	class FHitFeedCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHitFeedCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 90.0) { Test->AddError(FString::Printf(TEXT("Hit feed test stalled at %d"), Step)); return true; }
			if (!World || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Own && It->GetController() && !It->IsPlayerControlled()) { CPU = *It; break; }
			}
			if (!Own || !CPU) { return false; }
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			case 1:
			{
				Own->RefillHealth();
				CPU->RefillHealth();
				const float Before = CPU->GetHealth();
				UGameplayStatics::ApplyDamage(CPU, 1.0f, PC, Own, nullptr);
				UGameplayStatics::ApplyDamage(CPU, 1.0f, PC, Own, nullptr);
				Test->TestEqual(TEXT("The CPU took both hits"), CPU->GetHealth(), Before - 2.0f);
				// (The CPU may have hit the player a moment ago itself: either way a 被弾 card is up.)
				UGameplayStatics::ApplyDamage(Own, 1.0f, CPU->GetController(), CPU, nullptr);
				WaitUntil = Now + 0.5;
				Step = 2;
				return false;
			}
			case 2:
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HitFeedQA"), TEXT("HitFeed.png")), true, false);
				WaitUntil = Now + 0.6;
				Step = 3;
				return false;
			default:
				Own->RefillHealth();
				CPU->RefillHealth();
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHitFeedTest, "ChaosImpact.UI.HitFeed", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHitFeedTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHitFeedCommand(this));
	return true;
}
#endif
