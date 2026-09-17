#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactBall* SpawnTypedWindTestBall(UWorld* World, const FVector& Location, APawn* Thrower, const EChaosImpactBallType Type)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
			SpawnTransform, Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ball)
		{
			Ball->SetBallType(Type);
			Ball->FinishSpawning(SpawnTransform);
		}
		return Ball;
	}

	AChaosImpactBall* SpawnWindBall(UWorld* World, const FVector& Location, APawn* Thrower)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
		AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(),
			SpawnTransform, Thrower, Thrower, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Ball)
		{
			Ball->SetBallType(EChaosImpactBallType::Wind);
			Ball->FinishSpawning(SpawnTransform);
		}
		return Ball;
	}

	int32 CountTornadoes(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/**
	 * Run in training with one CPU (?CITraining=1?CICPUCount=1?CITargets=0): a wind ball released toward a standing
	 * CPU forms a tornado that travels, hits the CPU once and blows it away, spares the thrower and ends; a wind ball
	 * picked up and thrown with the throw input does the same. Screenshots in Saved/WindQA.
	 */
	class FWindBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWindBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !PC->IsGameplayActive())
			{
				if (Now - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WindQA"), Name), true, false);
			};
			// A camera behind and above the thrower, looking down the throw.
			if (Stage > 0)
			{
				if (!Camera.IsValid())
				{
					FActorSpawnParameters Parameters;
					Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Camera = World->SpawnActor<ACameraActor>(Parameters);
					Camera->GetCameraComponent()->SetFieldOfView(60.0f);
					PC->SetViewTarget(Camera.Get());
				}
				const FVector Focus = Origin + Toward * 550.0f;
				const FVector Eye = Origin - Toward * 520.0f + FVector(0.0f, 0.0f, 620.0f)
					+ FVector::CrossProduct(Toward, FVector::UpVector) * 260.0f;
				Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (CPU && CPU->IsCarriedByWind())
			{
				bSawCPUCarried = true;
				CarriedHeightSeen = FMath::Max(CarriedHeightSeen, static_cast<float>(CPU->GetMesh()->GetRelativeLocation().Z - CPUMeshRestZ));
			}
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			switch (Stage)
			{
			case 0:
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					It->SetActorTickEnabled(false);
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
				}
				if (!CPUCharacter.IsValid())
				{
					if (Now - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				// Zones left by earlier tests (burning ground) would hurt the CPU before the tornado arrives.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					It->Destroy();
				}
				Origin = Player->GetActorLocation();
				Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				// Throw along whichever way has the most open floor, so the CPU is not pinned to a wall when blown.
				{
					float BestOpen = -1.0f;
					for (int32 Turn = 0; Turn < 16; ++Turn)
					{
						const FVector Candidate = FRotator(0.0f, Turn * 22.5f, 0.0f).Vector();
						FHitResult Blocked;
						const FVector Lift(0.0f, 0.0f, 40.0f);
						const float Open = World->SweepSingleByObjectType(Blocked, Origin + Lift, Origin + Lift + Candidate * 2200.0f,
							FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(120.0f),
							FCollisionQueryParams(SCENE_QUERY_STAT(WindTestOpen), false, Player))
							? static_cast<float>(Blocked.Distance) : 2200.0f;
						if (Open > BestOpen)
						{
							BestOpen = Open;
							Toward = Candidate;
						}
					}
				}
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				CPUCharacter->ResetForOnlineMatch(Origin + Toward * 700.0f, (-Toward).Rotation());
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			case 1:
			{
				CPUStart = CPU->GetActorLocation();
				CPUMeshRestZ = CPU->GetMesh()->GetRelativeLocation().Z;
				CPUHealth = CPU->GetHealth();
				PlayerHealth = Player->GetHealth();
				// A ball lying on the tornado's way, to be swept up.
				LyingBall = SpawnTypedWindTestBall(World, Origin + Toward * 330.0f, nullptr, EChaosImpactBallType::Normal);
				if (LyingBall.IsValid())
				{
					LyingBall->MakePickup();
				}
				AChaosImpactBall* Ball = SpawnWindBall(World, Origin + Toward * 90.0f + FVector(0.0f, 0.0f, 30.0f), Player);
				Test->TestNotNull(TEXT("A wind ball spawns"), Ball);
				if (Ball)
				{
					Ball->Launch(Toward, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					Test->TestTrue(TEXT("A released wind ball does not fly"), Ball->IsHidden());
				}
				Test->TestEqual(TEXT("Releasing a wind ball forms one tornado"), CountTornadoes(World), 1);
				ReleasedAt = Now;
				Stage = 2;
				NextAt = Now + 0.15;
				return false;
			}
			case 2:
				Shot(TEXT("Wind-01-Forming.png"));
				Stage = 3;
				NextAt = ReleasedAt + 0.8;
				return false;
			case 3:
			{
				Shot(TEXT("Wind-02-Travelling.png"));
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					Test->TestTrue(TEXT("The tornado travels"), FVector::Dist2D(It->GetCenter(), Origin) > 150.0f);
				}
				Stage = 4;
				NextAt = ReleasedAt + 1.5;
				return false;
			}
			case 4:
				Shot(TEXT("Wind-03-Hit.png"));
				Test->TestTrue(TEXT("A lying ball on the way is swept up"), LyingBall.IsValid() && LyingBall->IsCarriedByWind());
				Stage = 5;
				NextAt = ReleasedAt + 2.6;
				return false;
			case 5:
				Shot(TEXT("Wind-04-After.png"));
				Test->AddInfo(FString::Printf(TEXT("CPU health %.0f -> %.0f, moved %.0f"), CPUHealth, CPU->GetHealth(),
					FVector::Dist2D(CPU->GetActorLocation(), CPUStart)));
				Test->TestEqual(TEXT("The tornado hits the CPU once"), CPU->GetHealth(), CPUHealth - 1.0f);
				Test->AddInfo(FString::Printf(TEXT("CPU carried=%d lifted %.0f"), bSawCPUCarried, CarriedHeightSeen));
				Test->TestTrue(TEXT("The CPU is caught up in the tornado"), bSawCPUCarried);
				Test->TestTrue(TEXT("The caught CPU is lifted off the ground"), CarriedHeightSeen > 60.0f);
				Test->TestFalse(TEXT("The tornado lets the CPU go"), CPU->IsCarriedByWind());
				Test->TestTrue(TEXT("The CPU is carried off and thrown out"), FVector::Dist2D(CPU->GetActorLocation(), CPUStart) > 150.0f);
				Test->TestEqual(TEXT("The thrower is never hurt by their own tornado"), Player->GetHealth(), PlayerHealth);
				// A ball thrown into the funnel from the side is flung round in a new direction.
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					const FVector Side = FVector::CrossProduct(Toward, FVector::UpVector);
					const FVector From = It->GetCenter() + Side * 260.0f + FVector(0.0f, 0.0f, 60.0f);
					FlyingBall = SpawnTypedWindTestBall(World, From, Player, EChaosImpactBallType::Normal);
					if (FlyingBall.IsValid())
					{
						FlyingBall->Launch((It->GetCenter() - From).GetSafeNormal2D(), 2200.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					}
				}
				Stage = 51;
				NextAt = Now + 0.5;
				return false;
			case 51:
				Shot(TEXT("Wind-04b-Deflect.png"));
				Test->TestTrue(TEXT("A ball flying into the tornado is turned"),
					FlyingBall.IsValid() && (FlyingBall->GetReflectionCount() > 0 || FlyingBall->IsPickup()));
				Stage = 6;
				NextAt = ReleasedAt + AChaosImpactTornado::ActiveSeconds + AChaosImpactTornado::CollapseSeconds + 2.2;
				return false;
			case 6:
			{
				Test->TestEqual(TEXT("The tornado is gone after its time"), CountTornadoes(World), 0);
				Test->TestTrue(TEXT("A swept-up ball is dropped again, ready to pick up"),
					LyingBall.IsValid() && LyingBall->IsPickup() && !LyingBall->IsCarriedByWind());
				if (LyingBall.IsValid())
				{
					Test->AddInfo(FString::Printf(TEXT("Swept ball dropped %.0f from where it lay"),
						FVector::Dist2D(LyingBall->GetActorLocation(), Origin + Toward * 330.0f)));
				}
				// Now the real throw: pick one up and throw it with the throw input.
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				AChaosImpactBall* Pickup = SpawnWindBall(World, Origin + FVector(0.0f, 0.0f, 300.0f), nullptr);
				if (Pickup)
				{
					Pickup->MakePickup();
					Test->TestTrue(TEXT("A wind ball can be picked up"), Player->TryPickupBall(Pickup));
					Pickup->Destroy();
				}
				Test->TestEqual(TEXT("The player carries the wind ball"), Player->GetCarriedBallType(0), EChaosImpactBallType::Wind);
				Stage = 7;
				NextAt = Now + 0.6;
				return false;
			}
			case 7:
				Shot(TEXT("Wind-05-Hold-HUD.png"));
				Player->BeginThrowInput();
				Stage = 8;
				NextAt = Now + 0.3;
				return false;
			case 8:
				Player->EndThrowInput();
				Stage = 9;
				NextAt = Now + 0.7;
				return false;
			case 9:
				Shot(TEXT("Wind-06-Thrown.png"));
				Test->TestEqual(TEXT("Throwing a carried wind ball forms a tornado"), CountTornadoes(World), 1);
				Test->TestEqual(TEXT("The wind ball left the hand"), Player->GetCarriedBallCount(), 0);
				Stage = 10;
				NextAt = Now + 0.5;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ReleasedAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		FVector CPUStart = FVector::ZeroVector;
		float CPUHealth = 0.0f;
		float PlayerHealth = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
		TWeakObjectPtr<AChaosImpactBall> LyingBall;
		bool bSawCPUCarried = false;
		float CarriedHeightSeen = 0.0f;
		float CPUMeshRestZ = 0.0f;
		TWeakObjectPtr<AChaosImpactBall> FlyingBall;
		TWeakObjectPtr<ACameraActor> Camera;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWindBallTest, "ChaosImpact.Training.WindBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactWindBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWindBallCommand(this));
	return true;
}

namespace
{
	/**
	 * Run in training with one CPU (?CITraining=1?CICPUCount=1?CITargets=0): the player releases tornadoes straight at a
	 * CPU that is playing normally; the CPU should step or dash out of their way.
	 */
	class FWindDodgeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FWindDodgeCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld())
				{
					World = Context.World();
					break;
				}
			}
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCPUController> It(World); World && It; ++It)
			{
				// An earlier test in the same run may have stopped it.
				It->SetActorTickEnabled(true);
				CPU = Cast<AChaosImpactCharacter>(It->GetPawn());
			}
			if (!Player || !CPU || !PC->IsGameplayActive())
			{
				if (Now - StartedAt > 120.0)
				{
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				return false;
			}
			if (CPU->IsCarriedByWind())
			{
				bCaughtThisRound = true;
			}
			if (Now < NextAt)
			{
				return false;
			}
			if (Round > 0)
			{
				Test->AddInfo(FString::Printf(TEXT("Tornado %d: CPU %s"), Round, bCaughtThisRound ? TEXT("caught") : TEXT("dodged")));
				Dodged += bCaughtThisRound ? 0 : 1;
			}
			if (Round >= Rounds)
			{
				Test->AddInfo(FString::Printf(TEXT("CPU dodged %d of %d tornadoes"), Dodged, Rounds));
				Test->TestTrue(TEXT("The CPU gets out of the way of most tornadoes"), Dodged * 3 >= Rounds * 2);
				return true;
			}
			for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
			{
				It->Destroy();
			}
			// Keep both alive and close enough; release it from beside the CPU, straight at it.
			Player->ResetForOnlineMatch(Player->GetActorLocation(), Player->GetActorRotation());
			CPU->ResetForOnlineMatch(CPU->GetActorLocation(), CPU->GetActorRotation());
			FVector Toward = (CPU->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
			if (Toward.IsNearlyZero())
			{
				Toward = FVector::ForwardVector;
			}
			AChaosImpactTornado::Release(World, CPU->GetActorLocation() - Toward * 650.0f + FVector(0.0f, 0.0f, 60.0f), Toward, Player);
			bCaughtThisRound = false;
			++Round;
			NextAt = Now + AChaosImpactTornado::ActiveSeconds + 0.6;
			return false;
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		static constexpr int32 Rounds = 3;
		int32 Round = 0;
		int32 Dodged = 0;
		bool bCaughtThisRound = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactWindDodgeTest, "ChaosImpact.Training.WindBallCPUDodge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactWindDodgeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWindDodgeCommand(this));
	return true;
}

#endif
