#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactBall.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	/**
	 * Every special ball's effect as a player sees it (the game camera over the player): each one thrown across the
	 * screen (its flight), then burst in front of the player and filmed as it opens, at its height, as it lingers
	 * and as it goes. Saved/VfxQA/<Type>_<moment>.png. -CIVfxTypes=Fire,Ice limits it to some.
	 */
	class FVfxGalleryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVfxGalleryCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			const EChaosImpactBallType All[] = {EChaosImpactBallType::Fire, EChaosImpactBallType::Ice, EChaosImpactBallType::Thunder,
				EChaosImpactBallType::Black, EChaosImpactBallType::Wind, EChaosImpactBallType::Smoke, EChaosImpactBallType::Beam,
				EChaosImpactBallType::Snow, EChaosImpactBallType::Nova, EChaosImpactBallType::Simae, EChaosImpactBallType::Drive,
				EChaosImpactBallType::Prison};
			FString Only;
			FParse::Value(FCommandLine::Get(), TEXT("CIVfxTypes="), Only, false);
			for (const EChaosImpactBallType Type : All)
			{
				if (Only.IsEmpty() || Only.Contains(ChaosImpactBallTypes::GetInternalName(Type)))
				{
					Types.Add(Type);
				}
			}
		}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
			}
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player) { Test->AddError(TEXT("Requires a training world with a player")); return true; }
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt) { return false; }
			if (Cursor >= Types.Num()) { return true; }
			const EChaosImpactBallType Type = Types[Cursor];
			const FString Name = ChaosImpactBallTypes::GetInternalName(Type);
			const auto Shot = [&](const TCHAR* Moment)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VfxQA"),
					FString::Printf(TEXT("%02d_%s_%s.png"), Cursor + 1, *Name, Moment)), true, false);
			};
			// The camera looks along +X from behind the player: across the screen is the Y axis.
			const FVector Feet = Player->GetActorLocation() - FVector(0, 0, 90);
			// (Off to the side of the training pad straight ahead, which glows.)
			const FVector Ahead = Feet + FVector(560.0f, -380.0f, 0.0f);
			switch (Step)
			{
			case 0:
			{
				// Its flight: thrown from the left of the screen to the right, a little in front of the player.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { It->Destroy(); }
				const FVector From = Player->GetActorLocation() + FVector(380.0f, -900.0f, 40.0f);
				const FTransform At(FRotator::ZeroRotator, From);
				if (Type != EChaosImpactBallType::Wind)
				{
					if (AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, nullptr, nullptr,
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
					{
						Ball->SetBallType(Type);
						Ball->FinishSpawning(At);
						Ball->Launch(FVector(0.0f, 1.0f, 0.0f), 1500.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					}
				}
				NextAt = Now + 0.55;
				++Step;
				return false;
			}
			case 1:
				Shot(TEXT("a_flight"));
				NextAt = Now + 1.4;
				++Step;
				return false;
			case 2:
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It) { if (!It->IsPickup() && !It->GetAttachParentActor()) { It->Destroy(); } }
				if (Type == EChaosImpactBallType::Wind)
				{
					AChaosImpactTornado::Release(World, Ahead - FVector(300.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f), nullptr);
				}
				else
				{
					AChaosImpactHazardZone::Detonate(World, Type, Ahead, nullptr, nullptr, Type == EChaosImpactBallType::Snow ? 2.0f : 1.0f);
				}
				Burst = Now;
				NextAt = Now + 0.08;
				++Step;
				return false;
			case 3:
				Shot(TEXT("b_burst"));
				NextAt = Burst + 0.35;
				++Step;
				return false;
			case 4:
				Shot(TEXT("c_peak"));
				NextAt = Burst + 1.2;
				++Step;
				return false;
			case 5:
				Shot(TEXT("d_linger"));
				NextAt = Burst + 3.2;
				++Step;
				return false;
			default:
				Shot(TEXT("e_late"));
				NextAt = Now + 0.6;
				Step = 0;
				++Cursor;
				return false;
			}
		}

	private:
		FAutomationTestBase* Test;
		TArray<EChaosImpactBallType> Types;
		int32 Cursor = 0;
		int32 Step = 0;
		double NextAt = 0.0;
		double Burst = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactVfxGalleryTest, "ChaosImpact.VFX.Gallery",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactVfxGalleryTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FVfxGalleryCommand(this));
	return true;
}
#endif
