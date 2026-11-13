#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactBallSwitch.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

/** The duel / tournament slot draw under ボールスイッチ. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactBallSwitchDrawTest, "ChaosImpact.BallSwitch.Draw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactBallSwitchDrawTest::RunTest(const FString& Parameters)
{
	using namespace ChaosImpactBallSwitch;
	FRandomStream Random(1234);
	const auto Rand = [&Random](const int32 Min, const int32 Max) { return Random.RandRange(Min, Max); };
	const uint8 Hook = static_cast<uint8>(EChaosImpactBallType::Hook);
	const uint8 Anchor = static_cast<uint8>(EChaosImpactBallType::Anchor);

	// Every kind on: three different, never the nova / the prison, never the hook with the anchor.
	FChaosImpactMatchRules All;
	bool bDistinct = true;
	bool bNoNova = true;
	bool bNoTwins = true;
	bool bSawHook = false;
	bool bSawAnchor = false;
	for (int32 Draw = 0; Draw < 2000; ++Draw)
	{
		const TArray<uint8> Picks = DrawDuelBalls(All, 3, Rand);
		bDistinct &= Picks.Num() == 3 && TSet<uint8>(Picks).Num() == 3;
		bNoNova &= !Picks.Contains(static_cast<uint8>(EChaosImpactBallType::Nova)) && !Picks.Contains(static_cast<uint8>(EChaosImpactBallType::Prison));
		bNoTwins &= !(Picks.Contains(Hook) && Picks.Contains(Anchor));
		bSawHook |= Picks.Contains(Hook);
		bSawAnchor |= Picks.Contains(Anchor);
	}
	TestTrue(TEXT("All on: three different kinds"), bDistinct);
	TestTrue(TEXT("All on: no nova, no prison"), bNoNova);
	TestTrue(TEXT("Never the hook and the anchor together"), bNoTwins);
	TestTrue(TEXT("The hook still comes up"), bSawHook);
	TestTrue(TEXT("The anchor still comes up"), bSawAnchor);

	// The normal ball is on the switch too (listed first); the last kind on can never be switched off.
	TestEqual(TEXT("Every kind listed, normal first"), GetSwitchable().Num(), ChaosImpactBallTypes::Count);
	TestTrue(TEXT("Normal listed first"), GetSwitchable()[0] == EChaosImpactBallType::Normal);
	FChaosImpactMatchRules Off;
	for (const EChaosImpactBallType Kind : GetSwitchable())
	{
		SetOn(Off, Kind, false);
	}
	TestTrue(TEXT("Every kind can be switched off (the screen keeps it from being left so)"), IsNoneOn(Off));
	TestEqual(TEXT("All off: the bits"), Off.OffBalls, AllBits());

	// Every special kind off: no slot balls at all (normal balls only).
	FChaosImpactMatchRules None;
	for (const EChaosImpactBallType Kind : GetSwitchable())
	{
		SetOn(None, Kind, Kind == EChaosImpactBallType::Normal);
	}
	TestEqual(TEXT("Specials off: the normal ball on alone"), CountOn(None), 1);
	TestTrue(TEXT("Specials off: normal on"), IsOn(None, EChaosImpactBallType::Normal));
	TestEqual(TEXT("Specials off: no slot balls"), DrawDuelBalls(None, 3, Rand).Num(), 0);

	// The normal ball off: never in a slot either way, the specials drawn as before.
	FChaosImpactMatchRules NoNormal;
	SetOn(NoNormal, EChaosImpactBallType::Normal, false);
	TestFalse(TEXT("Normal off"), IsOn(NoNormal, EChaosImpactBallType::Normal));
	TestEqual(TEXT("Normal off: three slots"), DrawDuelBalls(NoNormal, 3, Rand).Num(), 3);
	TestFalse(TEXT("Normal never in a slot"), DrawDuelBalls(All, 3, Rand).Contains(static_cast<uint8>(EChaosImpactBallType::Normal)));

	// Two kinds on: both drawn, one of them twice.
	FChaosImpactMatchRules Two = None;
	SetOn(Two, EChaosImpactBallType::Fire, true);
	SetOn(Two, EChaosImpactBallType::Ice, true);
	const TArray<uint8> TwoPicks = DrawDuelBalls(Two, 3, Rand);
	TestEqual(TEXT("Two on: three slots"), TwoPicks.Num(), 3);
	TestTrue(TEXT("Two on: both kinds"), TwoPicks.Contains(static_cast<uint8>(EChaosImpactBallType::Fire)) && TwoPicks.Contains(static_cast<uint8>(EChaosImpactBallType::Ice)));
	TestEqual(TEXT("Two on: only those"), TSet<uint8>(TwoPicks).Num(), 2);

	// Only the hook and the anchor on: one of them in every slot.
	FChaosImpactMatchRules Twins = None;
	SetOn(Twins, EChaosImpactBallType::Hook, true);
	SetOn(Twins, EChaosImpactBallType::Anchor, true);
	const TArray<uint8> TwinPicks = DrawDuelBalls(Twins, 3, Rand);
	TestEqual(TEXT("Hook + anchor on: three slots"), TwinPicks.Num(), 3);
	TestEqual(TEXT("Hook + anchor on: one kind of the two"), TSet<uint8>(TwinPicks).Num(), 1);

	// The nova alone on: nothing for a duel (it never comes up there).
	FChaosImpactMatchRules NovaOnly = None;
	SetOn(NovaOnly, EChaosImpactBallType::Nova, true);
	TestEqual(TEXT("Nova only: no slot balls"), DrawDuelBalls(NovaOnly, 3, Rand).Num(), 0);

	// Carried in the options a local match is opened with.
	FChaosImpactMatchRules Carried = Two;
	FURL URL(nullptr, *(FString(TEXT("/Game/Map?")) + ChaosImpactMatch::ToOptions(Carried)), TRAVEL_Absolute);
	FChaosImpactMatchRules Read;
	TestTrue(TEXT("Options read back"), ChaosImpactMatch::ReadOptions(URL, Read));
	TestEqual(TEXT("Off balls carried in the options"), Read.OffBalls, Carried.OffBalls);
	return true;
}

namespace
{
	/**
	 * In training with one CPU (?CITraining=1?CICPUCount=1?CITargets=0):
	 *  1. a pad never puts out a kind switched off on the match rules;
	 *  2. a hook thrown at a nova part brings it to its thrower;
	 *  3. a hooked CPU is pulled in over a small step (it used to stop dead against it).
	 */
	class FHookExtrasCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHookExtrasCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
			if (RealNow - StartedAt > 150.0)
			{
				Test->AddError(TEXT("Timed out."));
				return true;
			}
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !PC->IsGameplayActive())
			{
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
				}
				if (!CPUCharacter.IsValid())
				{
					return false;
				}
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It);
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				// 1. The switch on the pads: fire off, the rest on.
				AChaosImpactGameState* Match = World->GetGameState<AChaosImpactGameState>();
				AChaosImpactBallSpawner* Pad = nullptr;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It && !Pad; ++It)
				{
					Pad = *It;
				}
				if (Match && Pad)
				{
					const float Before = Pad->GetChance(EChaosImpactBallType::Fire);
					ChaosImpactBallSwitch::SetOn(Match->Rules, EChaosImpactBallType::Fire, false);
					Test->TestTrue(TEXT("Fire comes out with the switch on"), Before > 0.0f);
					Test->TestEqual(TEXT("Fire never comes out once switched off"), Pad->GetChance(EChaosImpactBallType::Fire), 0.0f);
					Test->TestTrue(TEXT("Ice still comes out"), Pad->GetChance(EChaosImpactBallType::Ice) > 0.0f);
					// The normal ball off: never from a pad, the specials on share it all.
					Test->TestTrue(TEXT("Normal comes out with the switch on"), Pad->GetChance(EChaosImpactBallType::Normal) > 0.0f);
					ChaosImpactBallSwitch::SetOn(Match->Rules, EChaosImpactBallType::Normal, false);
					Test->TestEqual(TEXT("Normal never comes out once switched off"), Pad->GetChance(EChaosImpactBallType::Normal), 0.0f);
					float Sum = 0.0f;
					for (int32 Type = 0; Type < ChaosImpactBallTypes::Count; ++Type)
					{
						Sum += Pad->GetChance(static_cast<EChaosImpactBallType>(Type));
					}
					Test->TestTrue(TEXT("Normal off: the pad still puts out a ball every time"), FMath::IsNearlyEqual(Sum, 1.0f, 0.001f));
					// Rules with nothing on still play: with normal balls.
					Match->Rules.OffBalls = ChaosImpactBallSwitch::AllBits();
					Test->TestEqual(TEXT("Nothing on: normal balls after all"), Pad->GetChance(EChaosImpactBallType::Normal), 1.0f);
					Match->Rules.OffBalls = 0;
				}
				else
				{
					Test->AddError(TEXT("No game state or no pad."));
				}
				// 2. A nova part ahead on the floor, the hook thrown at it.
				Origin = Player->GetActorLocation();
				// Along whichever way has the most open floor (the walls of the stage are not in the way).
				{
					float BestOpen = -1.0f;
					for (int32 Turn = 0; Turn < 16; ++Turn)
					{
						const FVector Candidate = FRotator(0.0f, Turn * 22.5f, 0.0f).Vector();
						FHitResult Blocked;
						const FVector Lift(0.0f, 0.0f, 40.0f);
						const float Open = World->SweepSingleByObjectType(Blocked, Origin + Lift, Origin + Lift + Candidate * 1600.0f, FQuat::Identity,
							FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(120.0f),
							FCollisionQueryParams(SCENE_QUERY_STAT(HookExtrasOpen), false, Player)) ? static_cast<float>(Blocked.Distance) : 1600.0f;
						if (Open > BestOpen)
						{
							BestOpen = Open;
							Toward = Candidate;
						}
					}
				}
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				CPUCharacter->ResetForOnlineMatch(Origin - Toward * 300.0f + FVector(0.0f, 0.0f, 0.0f), Toward.Rotation());
				const float Feet = static_cast<float>(Origin.Z) - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				AChaosImpactNovaPart::Spawn(World, EChaosImpactNovaPart::Gear,
					FVector(Origin.X, Origin.Y, Feet) + Toward * 650.0f + FVector(0.0f, 0.0f, ChaosImpactNovaParts::PartHoverHeight));
				AChaosImpactHook::Release(World, Player->GetActorLocation() + Toward * 60.0f + FVector(0.0f, 0.0f, 30.0f), Toward, Player);
				Stage = 1;
				NextAt = Now + 2.0;
				return false;
			}
			case 1:
			{
				Test->TestTrue(TEXT("The hook brought the nova part to its thrower"),
					(Player->GetNovaParts() & ChaosImpactNovaParts::Bit(EChaosImpactNovaPart::Gear)) != 0);
				// 3. A low step between the player and the CPU, the CPU beyond it (the same open way), hooked.
				const FVector Back = Toward;
				const float Feet = static_cast<float>(Player->GetActorLocation().Z) - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				const FVector StepAt = FVector(Player->GetActorLocation().X, Player->GetActorLocation().Y, Feet) + Back * 420.0f;
				if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
				{
					FActorSpawnParameters Parameters;
					Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					if (AStaticMeshActor* Step = World->SpawnActor<AStaticMeshActor>(StepAt + FVector(0.0f, 0.0f, 12.0f), Back.Rotation(), Parameters))
					{
						Step->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
						Step->GetStaticMeshComponent()->SetStaticMesh(Cube);
						Step->SetActorScale3D(FVector(0.6f, 4.0f, 0.24f));
						Step->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					}
				}
				CPU->ResetForOnlineMatch(StepAt + Back * 380.0f + FVector(0.0f, 0.0f, 100.0f), Toward.Rotation());
				Stage = 2;
				NextAt = Now + 0.8;
				return false;
			}
			case 2:
				StartDistance = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
				AChaosImpactHook::Release(World, Player->GetActorLocation() + Toward * 60.0f + FVector(0.0f, 0.0f, 30.0f), Toward, Player);
				Stage = 3;
				NextAt = Now + 2.2;
				return false;
			case 3:
			{
				const float EndDistance = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
				UE_LOG(LogTemp, Log, TEXT("Hook over a step: %.0f -> %.0f"), StartDistance, EndDistance);
				Test->TestTrue(TEXT("The hooked CPU came over the step"), EndDistance < 330.0f && EndDistance < StartDistance - 300.0f);
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		float StartDistance = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHookExtrasTest, "ChaosImpact.Training.HookExtras",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactHookExtrasTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHookExtrasCommand(this));
	return true;
}

#endif
