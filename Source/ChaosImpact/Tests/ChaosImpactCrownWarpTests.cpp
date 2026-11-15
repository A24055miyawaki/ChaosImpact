#if WITH_DEV_AUTOMATION_TESTS
// クラウンキープ: whoever wears the crown cannot warp (pad after pad, nobody could catch them); without it, the pad works.
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactWarpPad.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	class FCrownWarpCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCrownWarpCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			AChaosImpactGameMode* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0) { Test->AddError(FString::Printf(TEXT("Crown warp test stalled at %d"), Step)); return true; }
			if (!GM || !Match || Now < WaitUntil) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
			const auto PlaceOnPad = [this, Own]()
			{
				const float HalfHeight = Own->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				Own->SetActorLocation(Pad->GetPlatformCentre() + FVector(0, 0, Pad->GetStandHeight() + HalfHeight + 5.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Own->GetCharacterMovement()->Velocity = FVector::ZeroVector;
				PadAt = Pad->GetPlatformCentre();
			};
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = ChaosImpactMatch::PartyCrown;
				Rules.CPUCount = 1;
				Rules.CPULevel = 0;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			case 1:
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Own || !Crown || Own->IsMatchInputLocked())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactWarpPad> It(World); It; ++It)
				{
					if (!It->IsSetAside() && (!Pad.IsValid() || FVector::DistSquared2D(It->GetActorLocation(), Own->GetActorLocation())
						< FVector::DistSquared2D(Pad->GetActorLocation(), Own->GetActorLocation())))
					{
						Pad = *It;
					}
				}
				if (!Pad.IsValid())
				{
					Test->AddWarning(TEXT("No warp pad on this stage"));
					GM->EndMatchForTests();
					return true;
				}
				// Wearing the crown, on a pad, for well over its charge.
				Crown->TakeByHook(Own, Crown->GetShownLocation());
				PlaceOnPad();
				WaitUntil = Now + Pad->WarpChargeSeconds + 1.5;
				Step = 2;
				return false;
			case 2:
				Test->TestTrue(TEXT("The crown is worn"), Crown->GetHolder() == Own);
				Test->TestTrue(FString::Printf(TEXT("Wearing the crown, the pad sends nobody (%.0f cm off it)"),
					FVector::Dist2D(Own->GetActorLocation(), PadAt)), FVector::Dist2D(Own->GetActorLocation(), PadAt) < 400.0f);
				// Without it, the same pad works.
				Crown->DropFromHolder(ChaosImpactParty::NearestPoint(GM->GetPartyFieldPoints(), PadAt + FVector(2500, 0, 0)));
				PlaceOnPad();
				WaitUntil = Now + Pad->WarpChargeSeconds + 1.5;
				Step = 3;
				return false;
			case 3:
				Test->TestTrue(FString::Printf(TEXT("Without the crown, the pad sends them on (%.0f cm away)"),
					FVector::Dist2D(Own->GetActorLocation(), PadAt)), FVector::Dist2D(Own->GetActorLocation(), PadAt) > 400.0f);
				GM->EndMatchForTests();
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
		TWeakObjectPtr<AChaosImpactWarpPad> Pad;
		FVector PadAt = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrownWarpTest, "ChaosImpact.Party.CrownNoWarp", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCrownWarpTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrownWarpCommand(this));
	return true;
}
#endif
