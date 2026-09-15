#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactWarpPad.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindWarpTestController()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				if (auto* PC = Cast<AChaosImpactPlayerController>(Context.World()->GetFirstPlayerController()))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	class FWarpPadCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWarpPadCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindWarpTestController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			const auto Capture = [World](const TCHAR* Name)
			{
				const bool bVersus = World && World->URL.HasOption(TEXT("CIMatch=1"));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WarpQA"),
					FString(bVersus ? TEXT("VS-") : TEXT("Training-")) + Name), true, false);
			};
			if (!World || !Player)
			{
				if (Now - StartedAt < 90.0)
				{
					return false;
				}
				Test->AddError(TEXT("No player was created."));
				return true;
			}
			const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const auto PlaceBeside = [Player, HalfHeight](const AChaosImpactWarpPad* Pad, const FVector& Offset)
			{
				Player->SetActorLocation(Pad->GetActorLocation() + Offset + FVector(0.0f, 0.0f, HalfHeight + 4.0f),
					false, nullptr, ETeleportType::TeleportPhysics);
			};
			const auto PlaceOnTop = [Player, HalfHeight](const AChaosImpactWarpPad* Pad)
			{
				Player->SetActorLocation(Pad->GetActorLocation() + FVector(30.0f, 20.0f, Pad->GetStandHeight() + HalfHeight + 5.0f),
					false, nullptr, ETeleportType::TeleportPhysics);
			};

			if (Stage == 0)
			{
				if (!PC->IsGameplayActive() || Player->IsMatchInputLocked())
				{
					return TimedOut(Now, TEXT("The player never became free to move."));
				}
				// The pads linked with the one nearest the player.
				AChaosImpactWarpPad* Nearest = nullptr;
				for (TActorIterator<AChaosImpactWarpPad> It(World); It; ++It)
				{
					if (!Nearest || FVector::Dist2D(It->GetActorLocation(), Player->GetActorLocation())
						< FVector::Dist2D(Nearest->GetActorLocation(), Player->GetActorLocation()))
					{
						Nearest = *It;
					}
				}
				TArray<AChaosImpactWarpPad*> Linked;
				for (TActorIterator<AChaosImpactWarpPad> It(World); It && Nearest; ++It)
				{
					if (It->WarpGroup == Nearest->WarpGroup && It->GetAttachParentActor() == Nearest->GetAttachParentActor())
					{
						Linked.Add(*It);
					}
				}
				if (Linked.Num() < 3)
				{
					if (!bLoggedPads)
					{
						bLoggedPads = true;
						UE_LOG(LogTemp, Display, TEXT("WARPTEST player at %s, nearest %s"),
							*Player->GetActorLocation().ToCompactString(), *GetNameSafe(Nearest));
						for (TActorIterator<AChaosImpactWarpPad> It(World); It; ++It)
						{
							UE_LOG(LogTemp, Display, TEXT("WARPTEST found %s group %d parent %s at %s"), *It->GetName(),
								It->WarpGroup, *GetNameSafe(It->GetAttachParentActor()), *It->GetActorLocation().ToCompactString());
						}
					}
					return TimedOut(Now, *FString::Printf(TEXT("Expected at least 3 linked warp pads, found %d."), Linked.Num()));
				}
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
				}
				for (AChaosImpactWarpPad* Pad : Linked)
				{
					Pads.Add(Pad);
					UE_LOG(LogTemp, Display, TEXT("WARPTEST pad %s (%s) parent %s at %s stand %.1f"), *Pad->GetName(),
						*Pad->GetClass()->GetName(), *GetNameSafe(Pad->GetAttachParentActor()),
						*Pad->GetActorLocation().ToCompactString(), Pad->GetStandHeight());
				}
				PlaceBeside(Pads[0].Get(), FVector(-560.0f, -420.0f, 0.0f));
				Stage = 1;
				NextAt = Now + 2.5;
				return false;
			}
			const AChaosImpactWarpPad* First = Pads[0].Get();
			if (!First)
			{
				Test->AddError(TEXT("The first pad disappeared."));
				return true;
			}
			const float Charge = First->WarpChargeSeconds;
			if (Stage == 1)
			{
				Capture(TEXT("01-pad"));
				Stage = 2;
				NextAt = Now + 0.6;
				return false;
			}
			if (Stage == 2)
			{
				// At the foot of the steps (the model's +Y side), facing up them.
				const FVector StepsOut = First->GetActorRotation().RotateVector(FVector(0.0f, 1.0f, 0.0f));
				PlaceBeside(First, StepsOut * 520.0f);
				WalkStartedAt = Now;
				Stage = 20;
				NextAt = Now + 0.3;
				return false;
			}
			if (Stage == 20)
			{
				// Walk up the steps until standing on top.
				if (!First->IsStandingOnPad(Player))
				{
					Player->AddMovementInput((First->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D(), 1.0f);
					if (Now - WalkStartedAt < 6.0)
					{
						return false;
					}
					UE_LOG(LogTemp, Display, TEXT("WARPTEST walk stuck at %s"), *Player->GetActorLocation().ToCompactString());
					Test->AddError(TEXT("The player could not walk up the pad's steps."));
					return true;
				}
				UE_LOG(LogTemp, Display, TEXT("WARPTEST walked up the steps in %.2f s"), Now - WalkStartedAt);
				Capture(TEXT("02-walked-up"));
				ChargeStartedAt = Now;
				Stage = 3;
				NextAt = Now + Charge * 0.5;
				return false;
			}
			if (Stage == 3)
			{
				Capture(TEXT("02-charging"));
				Test->TestTrue(TEXT("Halfway through the charge the player is still on the pad"),
					FVector::Dist2D(Player->GetActorLocation(), First->GetActorLocation()) < 400.0f);
				Test->TestTrue(TEXT("The charge gauge is filling"), AChaosImpactWarpPad::FindChargeProgress(Player) > 0.2f);
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				// Wait for the warp, frame by frame.
				if (FVector::Dist2D(Player->GetActorLocation(), First->GetActorLocation()) < 800.0f)
				{
					if (Now - ChargeStartedAt < Charge + 3.0)
					{
						return false;
					}
					Test->AddError(TEXT("Staying on the pad never warped the player."));
					return true;
				}
				Capture(TEXT("03-warp"));
				UE_LOG(LogTemp, Display, TEXT("WARPTEST warped after %.2f s (charge %.2f)"), Now - ChargeStartedAt, Charge);
				Test->TestTrue(TEXT("The warp waits for the charge"), Now - ChargeStartedAt >= Charge - 0.3);
				ArrivalPad = FindNearestPad(Player->GetActorLocation());
				Test->TestTrue(TEXT("The player arrives on top of another pad"), ArrivalPad.IsValid() && ArrivalPad != Pads[0]
					&& ArrivalPad->IsStandingOnPad(Player));
				Stage = 5;
				NextAt = Now + 0.3;
				return false;
			}
			if (Stage == 5)
			{
				Capture(TEXT("04-arrived"));
				Stage = 6;
				NextAt = Now + Charge + 1.0;
				return false;
			}
			if (Stage == 6)
			{
				Test->TestTrue(TEXT("Arriving does not start another warp"), ArrivalPad.IsValid()
					&& FVector::Dist2D(Player->GetActorLocation(), ArrivalPad->GetActorLocation()) < 400.0f);
				if (!ArrivalPad.IsValid())
				{
					return true;
				}
				PlaceBeside(ArrivalPad.Get(), FVector(-560.0f, -420.0f, 0.0f));
				Stage = 7;
				NextAt = Now + 1.0;
				return false;
			}
			if (Stage == 7)
			{
				if (ArrivalPad.IsValid())
				{
					PlaceOnTop(ArrivalPad.Get());
				}
				Stage = 8;
				NextAt = Now + Charge + 1.0;
				return false;
			}
			Test->TestTrue(TEXT("After stepping off, staying on the arrival pad warps again"), ArrivalPad.IsValid()
				&& FVector::Dist2D(Player->GetActorLocation(), ArrivalPad->GetActorLocation()) > 800.0f);
			return true;
		}

	private:
		bool bLoggedPads = false;

		bool TimedOut(const double Now, const TCHAR* Message) const
		{
			if (Now - StartedAt < 90.0)
			{
				return false;
			}
			Test->AddError(Message);
			return true;
		}

		TWeakObjectPtr<AChaosImpactWarpPad> FindNearestPad(const FVector& Location) const
		{
			TWeakObjectPtr<AChaosImpactWarpPad> Nearest;
			double NearestDistance = TNumericLimits<double>::Max();
			for (const TWeakObjectPtr<AChaosImpactWarpPad>& Pad : Pads)
			{
				if (Pad.IsValid() && FVector::Dist2D(Pad->GetActorLocation(), Location) < NearestDistance)
				{
					NearestDistance = FVector::Dist2D(Pad->GetActorLocation(), Location);
					Nearest = Pad;
				}
			}
			return Nearest;
		}

		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ChargeStartedAt = 0.0;
		double WalkStartedAt = 0.0;
		int32 Stage = 0;
		TArray<TWeakObjectPtr<AChaosImpactWarpPad>> Pads;
		TWeakObjectPtr<AChaosImpactWarpPad> ArrivalPad;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWarpPadTest, "ChaosImpact.Training.WarpPads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactWarpPadTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWarpPadCommand(this));
	return true;
}

#endif
