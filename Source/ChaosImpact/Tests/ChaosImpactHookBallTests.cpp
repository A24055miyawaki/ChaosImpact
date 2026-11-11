#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactBall* SpawnHookTestBall(UWorld* World, const FVector& Location, APawn* Thrower, const EChaosImpactBallType Type)
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

	int32 CountHooks(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AChaosImpactHook> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/**
	 * Run in training with one CPU (?CITraining=1?CICPUCount=1?CITargets=0):
	 *  1. a hook ball released toward a standing CPU sends out a hook (the ball does not fly), which catches the CPU:
	 *     one damage, reeled in to just in front of the thrower, the thrower untouched; the hook then goes away;
	 *  2. a hook released at a ball lying on the floor brings it into the thrower's hands;
	 *  3. a CPU that dashes while being reeled in breaks free.
	 * Screenshots in Saved/HookQA.
	 */
	class FHookBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHookBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
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
				if (RealNow - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			// Game time (a screenshot's stall must not count as time passing).
			const double Now = World->GetTimeSeconds();
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HookQA"), Name), true, false);
			};
			if (Stage > 0 && !bOwnView)
			{
				if (!Camera.IsValid())
				{
					FActorSpawnParameters Parameters;
					Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Camera = World->SpawnActor<ACameraActor>(Parameters);
					Camera->GetCameraComponent()->SetFieldOfView(55.0f);
					PC->SetViewTarget(Camera.Get());
				}
				const FVector Side = FVector::CrossProduct(Toward, FVector::UpVector);
				// (Close up: in front of the player and to the side, wherever they now face.)
				const FVector Facing = Player->GetActorForwardVector().GetSafeNormal2D();
				const FVector Beside = FVector::CrossProduct(Facing, FVector::UpVector);
				const FVector Focus = bClose ? Player->GetActorLocation() + Facing * 90.0f + FVector(0.0f, 0.0f, 50.0f) : Origin + Toward * 420.0f;
				const FVector Eye = bClose ? Player->GetActorLocation() + Facing * 330.0f + Beside * 420.0f + FVector(0.0f, 0.0f, 150.0f)
					: Origin + Toward * 380.0f + Side * 900.0f + FVector(0.0f, 0.0f, 620.0f);
				Camera->SetActorLocationAndRotation(Eye, (Focus - Eye).Rotation());
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			if (CPU && CPU->IsPulledByHook())
			{
				bSawPull = true;
				NearestSeen = FMath::Min(NearestSeen, static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation())));
			}
			if (Stage >= 13 && CountHooks(World) > 0)
			{
				bSawRealHook = true;
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
					if (RealNow - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				// The pads stop putting balls out (a ball on the line would be hooked first); the pad check puts one out.
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					World->GetTimerManager().ClearAllTimersForObject(*It);
				}
				Origin = Player->GetActorLocation();
				Toward = Player->GetActorForwardVector().GetSafeNormal2D();
				// Along whichever way has the most open floor.
				{
					float BestOpen = -1.0f;
					for (int32 Turn = 0; Turn < 16; ++Turn)
					{
						const FVector Candidate = FRotator(0.0f, Turn * 22.5f, 0.0f).Vector();
						FHitResult Blocked;
						const FVector Lift(0.0f, 0.0f, 40.0f);
						const float Open = World->SweepSingleByObjectType(Blocked, Origin + Lift, Origin + Lift + Candidate * 2400.0f,
							FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(120.0f),
							FCollisionQueryParams(SCENE_QUERY_STAT(HookTestOpen), false, Player))
							? static_cast<float>(Blocked.Distance) : 2400.0f;
						if (Open > BestOpen)
						{
							BestOpen = Open;
							Toward = Candidate;
						}
					}
					// As far off as the floor allows, up to most of the hook's reach.
					FarDistance = FMath::Clamp(BestOpen - 250.0f, 900.0f, 2100.0f);
				}
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				CPUCharacter->ResetForOnlineMatch(Origin + Toward * FarDistance, (-Toward).Rotation());
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			case 1:
			{
				// Nothing lying on the way (a pad's ball would be hooked first).
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				CPUHealth = CPU->GetHealth();
				PlayerHealth = Player->GetHealth();
				CPUStartDistance = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
				AChaosImpactBall* Ball = SpawnHookTestBall(World, Player->GetActorLocation() + Toward * 70.0f + FVector(0.0f, 0.0f, 30.0f),
					Player, EChaosImpactBallType::Hook);
				Test->TestNotNull(TEXT("A hook ball spawns"), Ball);
				if (Ball)
				{
					Ball->Launch(Toward, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
					Test->TestTrue(TEXT("A released hook ball does not fly"), Ball->IsHidden());
				}
				Test->TestEqual(TEXT("Releasing a hook ball sends out one hook"), CountHooks(World), 1);
				bSawPull = false;
				NearestSeen = TNumericLimits<float>::Max();
				ReleasedAt = Now;
				Stage = 2;
				NextAt = Now + 0.18;
				return false;
			}
			case 2:
				Shot(TEXT("Hook_01_Out.png"));
				Stage = 3;
				NextAt = ReleasedAt + 0.5;
				return false;
			case 3:
				Shot(TEXT("Hook_02_Pull.png"));
				Stage = 31;
				NextAt = ReleasedAt + 0.85;
				return false;
			case 31:
				Shot(TEXT("Hook_02b_Reel.png"));
				Stage = 4;
				NextAt = ReleasedAt + 2.8;
				return false;
			case 4:
			{
				const float Distance = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
				UE_LOG(LogTemp, Log, TEXT("Hook test: CPU %.0f -> %.0f (nearest %.0f), health %.1f -> %.1f, player %.1f -> %.1f, hooks %d"),
					CPUStartDistance, Distance, NearestSeen, CPUHealth, CPU->GetHealth(), PlayerHealth, Player->GetHealth(), CountHooks(World));
				Test->TestTrue(TEXT("The hook catches the CPU and reels it in"), bSawPull);
				Test->TestTrue(TEXT("The CPU ends up near the thrower"), Distance < 360.0f);
				Test->TestTrue(TEXT("The CPU stops short of the thrower"), Distance > 90.0f);
				Test->TestTrue(TEXT("The caught CPU takes a hit"), CPU->GetHealth() < CPUHealth);
				Test->TestEqual(TEXT("The thrower is untouched"), Player->GetHealth(), PlayerHealth);
				Test->TestEqual(TEXT("The hook is gone afterwards"), CountHooks(World), 0);
				Test->TestFalse(TEXT("The pull is over"), CPU->IsPulledByHook());
				// 2: a ball lying on the floor, the CPU out of the way.
				CPU->ResetForOnlineMatch(Origin + Side() * 700.0f, (-Toward).Rotation());
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Stage = 5;
				NextAt = Now + 0.8;
				return false;
			}
			case 5:
			{
				HeldBefore = Player->GetCarriedBallCount();
				// On the floor, where balls really lie (well below the hook's flight at hand height).
				FVector Lie = Origin + Toward * 800.0f;
				{
					FHitResult Floor;
					if (World->LineTraceSingleByObjectType(Floor, Lie + FVector(0.0f, 0.0f, 300.0f), Lie - FVector(0.0f, 0.0f, 600.0f),
						FCollisionObjectQueryParams(ECC_WorldStatic)))
					{
						Lie = Floor.ImpactPoint + FVector(0.0f, 0.0f, 22.0f);
					}
				}
				LyingBall = SpawnHookTestBall(World, Lie, nullptr, EChaosImpactBallType::Fire);
				if (LyingBall.IsValid())
				{
					LyingBall->MakePickup();
				}
				AChaosImpactBall* Ball = SpawnHookTestBall(World, Player->GetActorLocation() + Toward * 70.0f + FVector(0.0f, 0.0f, 30.0f),
					Player, EChaosImpactBallType::Hook);
				if (Ball)
				{
					Ball->Launch(Toward, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				ReleasedAt = Now;
				Stage = 6;
				NextAt = Now + 0.55;
				return false;
			}
			case 6:
				Shot(TEXT("Hook_03_BallPull.png"));
				Stage = 7;
				NextAt = ReleasedAt + 2.2;
				return false;
			case 7:
				UE_LOG(LogTemp, Log, TEXT("Hook test: held %d -> %d, lying ball %s"), HeldBefore, Player->GetCarriedBallCount(),
					LyingBall.IsValid() ? *LyingBall->GetActorLocation().ToCompactString() : TEXT("taken"));
				Test->TestEqual(TEXT("A hooked ball comes into the thrower's hands"), Player->GetCarriedBallCount(), HeldBefore + 1);
				Test->TestFalse(TEXT("The hooked ball no longer lies on the floor"), LyingBall.IsValid() && LyingBall->IsPickup());
				// 3: a dash breaks free.
				CPU->ResetForOnlineMatch(Origin + Toward * 900.0f, (-Toward).Rotation());
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Stage = 8;
				NextAt = Now + 0.8;
				return false;
			case 8:
			{
				// Nothing lying on the way (a pad's ball would be hooked first).
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					It->Destroy();
				}
				AChaosImpactBall* Ball = SpawnHookTestBall(World, Player->GetActorLocation() + Toward * 70.0f + FVector(0.0f, 0.0f, 30.0f),
					Player, EChaosImpactBallType::Hook);
				if (Ball)
				{
					Ball->Launch(Toward, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				bSawPull = false;
				ReleasedAt = Now;
				Stage = 9;
				return false;
			}
			case 9:
				if (CPU->IsPulledByHook())
				{
					PulledFrom = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
					CPU->RequestAIDash(-Toward);
					Stage = 10;
					NextAt = Now + 1.6;
					return false;
				}
				if (Now - ReleasedAt > 1.5)
				{
					Test->AddError(TEXT("The third hook never caught the CPU."));
					return true;
				}
				return false;
			case 10:
			{
				const float Distance = static_cast<float>(FVector::Dist2D(CPU->GetActorLocation(), Player->GetActorLocation()));
				UE_LOG(LogTemp, Log, TEXT("Hook test: dash from %.0f, now %.0f"), PulledFrom, Distance);
				Test->TestFalse(TEXT("A dash breaks free of the pull"), CPU->IsPulledByHook());
				Test->TestTrue(TEXT("Having broken free, the CPU was not reeled all the way in"), Distance > 400.0f);
				// 4: the arms, close up: charging (swung round overhead), then thrown for real (along the chain).
				CPU->ResetForOnlineMatch(Origin + Side() * 900.0f, (-Toward).Rotation());
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Stage = 11;
				NextAt = Now + 0.8;
				return false;
			}
			case 11:
				bClose = true;
				Test->TestTrue(TEXT("The player can take a hook ball"), Player->GiveContestedPickup(EChaosImpactBallType::Hook));
				Player->BeginThrowInput();
				Stage = 12;
				NextAt = Now + 0.45;
				return false;
			case 12:
				Shot(TEXT("Hook_04_Spin.png"));
				Stage = 13;
				NextAt = Now + 0.12;
				return false;
			case 13:
				Shot(TEXT("Hook_05_Spin2.png"));
				Player->EndThrowInput();
				ReleasedAt = Now;
				Stage = 14;
				NextAt = Now + 0.3;
				return false;
			case 14:
				Shot(TEXT("Hook_06_Chain.png"));
				Stage = 16;
				NextAt = ReleasedAt + 0.65;
				return false;
			case 16:
				Shot(TEXT("Hook_07_Chain2.png"));
				Stage = 15;
				NextAt = ReleasedAt + 2.0;
				return false;
			case 15:
			{
				Test->TestEqual(TEXT("A real throw of a hook ball sent out a hook (and it is back)"), CountHooks(World), 0);
				Test->TestTrue(TEXT("A real throw of a hook ball sent out a hook"), bSawRealHook);
				// 5: a ball on a spawner's pad, seen through the game's own camera.
				AChaosImpactBallSpawner* Pad = nullptr;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
				{
					if (It->IsInPlay() && FVector::Dist2D(It->GetActorLocation(), Origin) < 1600.0f)
					{
						It->SpawnNow();
						if (It->GetActiveBall())
						{
							Pad = *It;
							break;
						}
					}
				}
				if (!Pad)
				{
					Test->AddWarning(TEXT("No spawner with a ball near by: the pad check is skipped."));
					return true;
				}
				PadBall = Pad->GetActiveBall();
				const FVector ToPad = (Pad->GetActorLocation() - Origin).GetSafeNormal2D();
				const FVector Stand = Pad->GetActorLocation() - ToPad * 700.0f;
				Player->ResetForOnlineMatch(FVector(Stand.X, Stand.Y, Origin.Z), ToPad.Rotation());
				CPU->ResetForOnlineMatch(Origin + Side() * 900.0f, (-Toward).Rotation());
				PadToward = ToPad;
				bClose = false;
				bOwnView = true;
				PC->SetViewTarget(Player);
				Stage = 20;
				NextAt = Now + 1.0;
				return false;
			}
			case 20:
			{
				HeldBefore = Player->GetCarriedBallCount();
				const FVector ToPad = PadBall.IsValid() ? (PadBall->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D() : PadToward;
				AChaosImpactBall* Ball = SpawnHookTestBall(World, Player->GetActorLocation() + ToPad * 70.0f + FVector(0.0f, 0.0f, 30.0f),
					Player, EChaosImpactBallType::Hook);
				if (Ball)
				{
					Ball->Launch(ToPad, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				ReleasedAt = Now;
				Stage = 21;
				NextAt = Now + 0.12;
				return false;
			}
			case 21:
				Shot(TEXT("Hook_08_GameView.png"));
				Stage = 22;
				NextAt = ReleasedAt + 0.32;
				return false;
			case 22:
				Shot(TEXT("Hook_09_GameView2.png"));
				Stage = 23;
				NextAt = ReleasedAt + 2.0;
				return false;
			case 23:
				UE_LOG(LogTemp, Log, TEXT("Hook test: pad ball, held %d -> %d"), HeldBefore, Player->GetCarriedBallCount());
				Test->TestEqual(TEXT("A ball on a spawner's pad comes into the thrower's hands"), Player->GetCarriedBallCount(), HeldBefore + 1);
				return true;
			default:
				return true;
			}
		}

	private:
		FVector Side() const { return FVector::CrossProduct(Toward, FVector::UpVector); }

		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ReleasedAt = 0.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<AChaosImpactBall> LyingBall;
		float CPUHealth = 0.0f;
		float PlayerHealth = 0.0f;
		float CPUStartDistance = 0.0f;
		float NearestSeen = 0.0f;
		float PulledFrom = 0.0f;
		float FarDistance = 900.0f;
		bool bClose = false;
		bool bOwnView = false;
		FVector PadToward = FVector::ForwardVector;
		TWeakObjectPtr<AChaosImpactBall> PadBall;
		bool bSawRealHook = false;
		bool bSawPull = false;
		int32 HeldBefore = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactHookBallTest, "ChaosImpact.Training.HookBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactHookBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHookBallCommand(this));
	return true;
}


namespace
{
	/**
	 * Run in training with one CPU (?CITraining=1?CICPUCount=1?CITargets=0):
	 *  1. an anchor thrown over open floor sticks in the floor at its reach and whisks the thrower straight across to it
	 *     (level), going straight through the CPU in between without hurting it;
	 *  2. one thrown at a wall puts them down just in front of it;
	 *  3. a dash on the way stops the flight short.
	 * Screenshots (the game's own camera) in Saved/HookQA.
	 */
	class FAnchorBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FAnchorBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
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
				if (RealNow - StartedAt > 120.0)
				{
					Test->AddError(TEXT("No playable character."));
					return true;
				}
				return false;
			}
			const double Now = World->GetTimeSeconds();
			const auto Shot = [](const TCHAR* Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HookQA"), Name), true, false);
			};
			const auto Throw = [World, Player](const FVector& Way)
			{
				const FTransform At(FRotator::ZeroRotator, Player->GetActorLocation() + Way * 70.0f + FVector(0.0f, 0.0f, 30.0f));
				AChaosImpactBall* Ball = World->SpawnActorDeferred<AChaosImpactBall>(AChaosImpactBall::StaticClass(), At, Player, Player,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Ball)
				{
					Ball->SetBallType(EChaosImpactBallType::Anchor);
					Ball->FinishSpawning(At);
					Ball->Launch(Way, 2000.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
			};
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			for (TActorIterator<AChaosImpactHook> It(World); It; ++It)
			{
				if (It->IsAnchor())
				{
					SeenZipTarget = It->GetZipTarget();
					bSeenWallAnchor = It->IsWallAnchor();
				}
			}
			if (Player->IsAnchorZipping())
			{
				bSawZip = true;
				HighestSeen = FMath::Max(HighestSeen, static_cast<float>(Player->GetActorLocation().Z));
				if (ZipSeenAt > 1000.0)
				{
					ZipSeenAt = Now;
				}
			}
			if (Now < NextAt)
			{
				return false;
			}
			const FCollisionQueryParams Query(SCENE_QUERY_STAT(AnchorTestOpen), false, Player);
			const auto Open = [World, &Query](const FVector& From, const FVector& Way, const float Far)
			{
				FHitResult Blocked;
				const FVector Lift(0.0f, 0.0f, 30.0f);
				return World->SweepSingleByObjectType(Blocked, From + Lift, From + Lift + Way * Far, FQuat::Identity,
					FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(60.0f), Query)
					? static_cast<float>(Blocked.Distance) : Far;
			};
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
					if (RealNow - StartedAt < 40.0)
					{
						return false;
					}
					Test->AddError(TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
					return true;
				}
				Origin = Player->GetActorLocation();
				// The most open way (for the floor throw) and the way with a wall nearest within reach (for the wall).
				float Best = -1.0f;
				float Nearest = TNumericLimits<float>::Max();
				for (int32 Turn = 0; Turn < 24; ++Turn)
				{
					const FVector Way = FRotator(0.0f, Turn * 15.0f, 0.0f).Vector();
					const float Free = Open(Origin, Way, 3000.0f);
					if (Free > Best)
					{
						Best = Free;
						Toward = Way;
					}
					if (Free > 700.0f && Free < 2100.0f && Free < Nearest)
					{
						Nearest = Free;
						WallWay = Way;
					}
				}
				bHaveWall = Nearest < 2100.0f;
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				CPUCharacter->ResetForOnlineMatch(Origin + Toward * 900.0f, (-Toward).Rotation());
				CPUHealth = CPUCharacter->GetHealth();
				Stage = 1;
				NextAt = Now + 1.0;
				return false;
			}
			case 1:
				Start = Player->GetActorLocation();
				HighestSeen = Start.Z;
				bSawZip = false;
				Throw(Toward);
				ReleasedAt = Now;
				Stage = 2;
				NextAt = Now + 0.62;
				return false;
			case 2:
				Shot(TEXT("Anchor_01_Fly.png"));
				Stage = 3;
				NextAt = ReleasedAt + 0.85;
				return false;
			case 3:
				Shot(TEXT("Anchor_02_Fly2.png"));
				Stage = 4;
				NextAt = ReleasedAt + 2.6;
				return false;
			case 4:
			{
				const float Moved = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Start));
				UE_LOG(LogTemp, Log, TEXT("Anchor test: floor throw moved %.0f, rose %.0f, cpu health %.1f -> %.1f, walking %d (mode %d, at %s, target %s)"), Moved,
					HighestSeen - Start.Z, CPUHealth, CPU ? CPU->GetHealth() : -1.0f, Player->GetCharacterMovement()->IsMovingOnGround(),
					static_cast<int32>(Player->GetCharacterMovement()->MovementMode.GetValue()), *Player->GetActorLocation().ToCompactString(),
					*SeenZipTarget.ToCompactString());
				Test->TestTrue(TEXT("The thrower flew to the anchor"), bSawZip);
				Test->TestTrue(TEXT("Thrown over open floor, the thrower goes most of the anchor's reach"),
					Moved > AChaosImpactHook::Length * 0.75f);
				Test->TestTrue(TEXT("The flight goes straight across (level)"), HighestSeen - Start.Z < 40.0f);
				Test->TestTrue(TEXT("The anchor does not hurt the CPU it passes"), CPU && FMath::IsNearlyEqual(CPU->GetHealth(), CPUHealth));
				Test->TestTrue(TEXT("Back on the ground afterwards"), Player->GetCharacterMovement()->IsMovingOnGround());
				Test->TestFalse(TEXT("The flight is over"), Player->IsAnchorZipping());
				Player->ResetForOnlineMatch(Origin, (bHaveWall ? WallWay : Toward).Rotation());
				Stage = 5;
				NextAt = Now + 0.8;
				return false;
			}
			case 5:
				if (!bHaveWall)
				{
					Test->AddWarning(TEXT("No wall within reach: the wall throw is skipped."));
					Stage = 7;
					return false;
				}
				Start = Player->GetActorLocation();
				bSeenWallAnchor = false;
				WallAt = Open(Start, WallWay, 3000.0f);
				Throw(WallWay);
				ReleasedAt = Now;
				Stage = 6;
				NextAt = Now + 2.4;
				return false;
			case 6:
			{
				const float Moved = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Start));
				const float Off = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), SeenZipTarget));
				UE_LOG(LogTemp, Log, TEXT("Anchor test: wall %.0f away, moved %.0f, %.0f from its landing spot, in a wall %d"), WallAt, Moved, Off,
					bSeenWallAnchor);
				Test->TestTrue(TEXT("Thrown at a wall, the anchor sticks in it"), bSeenWallAnchor);
				Test->TestTrue(TEXT("Thrown at a wall, the thrower lands just in front of it"), Off < 90.0f);
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Stage = 7;
				NextAt = Now + 0.8;
				return false;
			}
			case 7:
				Start = Player->GetActorLocation();
				bSawZip = false;
				ZipSeenAt = 100000.0;
				Throw(Toward);
				ReleasedAt = Now;
				Stage = 8;
				return false;
			case 8:
				if (ZipSeenAt < 1000.0 && Now - ZipSeenAt > 0.12)
				{
					Player->RequestAIDash(Toward);
					Stage = 9;
					NextAt = Now + 1.5;
					return false;
				}
				if (Now - ReleasedAt > 2.0)
				{
					Test->AddError(TEXT("The third anchor never carried the player."));
					return true;
				}
				return false;
			case 9:
			{
				const float Moved = static_cast<float>(FVector::Dist2D(Player->GetActorLocation(), Start));
				UE_LOG(LogTemp, Log, TEXT("Anchor test: dashed off after moving %.0f"), Moved);
				Test->TestFalse(TEXT("A dash stops the flight"), Player->IsAnchorZipping());
				Test->TestTrue(TEXT("Stopped short of the anchor"), Moved < AChaosImpactHook::Length * 0.85f);
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
		double ReleasedAt = 0.0;
		double ZipSeenAt = 100000.0;
		int32 Stage = 0;
		FVector Origin = FVector::ZeroVector;
		FVector Start = FVector::ZeroVector;
		FVector Toward = FVector::ForwardVector;
		FVector WallWay = FVector::ForwardVector;
		bool bHaveWall = false;
		bool bSawZip = false;
		bool bSeenWallAnchor = false;
		FVector SeenZipTarget = FVector::ZeroVector;
		float WallAt = 0.0f;
		float HighestSeen = 0.0f;
		float CPUHealth = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactAnchorBallTest, "ChaosImpact.Training.AnchorBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::EngineFilter)

bool FChaosImpactAnchorBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FAnchorBallCommand(this));
	return true;
}

#endif
