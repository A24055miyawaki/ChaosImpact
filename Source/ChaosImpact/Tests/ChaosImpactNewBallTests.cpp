#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCPUController.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTornado.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	AChaosImpactPlayerController* FindNewBallController()
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

	void CaptureNewBall(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NewBallQA"), Name), true, false);
	}

	AChaosImpactBall* SpawnNewBall(UWorld* World, const EChaosImpactBallType Type, const FVector& Location, APawn* Thrower)
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

	/**
	 * The smoke, beam and snow balls, throw cancel and dropping balls on going down, in the training arena with one
	 * CPU (?CITraining=1?CICPUCount=1?CITargets=0). Screenshots go to Saved/NewBallQA.
	 */
	class FNewBallCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNewBallCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now < NextAt)
			{
				return false;
			}
			AChaosImpactPlayerController* PC = FindNewBallController();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			AChaosImpactCharacter* Player = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !Player)
			{
				return Waited(Now, TEXT("No training player was created."));
			}
			AChaosImpactCharacter* CPU = CPUCharacter.Get();
			if (Stage > 0 && !CPU)
			{
				Test->AddError(TEXT("The CPU character disappeared."));
				return true;
			}
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
			const auto PlaceAt = [](AChaosImpactCharacter* Character, const FVector& Location)
			{
				Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			};
			const auto Restore = [&PlaceAt](AChaosImpactCharacter* Character, const FVector& Location)
			{
				Character->ResetForOnlineMatch(Character->GetActorLocation(), Character->GetActorRotation());
				PlaceAt(Character, Location);
			};
			const auto Give = [World](AChaosImpactCharacter* Character, const EChaosImpactBallType Type)
			{
				AChaosImpactBall* Pickup = SpawnNewBall(World, Type, Character->GetActorLocation() + FVector(0, 0, 400), nullptr);
				Pickup->MakePickup();
				const bool bTaken = Character->TryPickupBall(Pickup);
				Pickup->Destroy();
				return bTaken;
			};
			const auto Launch = [&](const EChaosImpactBallType Type, const FVector& Direction, const float SnowScale = 1.0f)
			{
				// A big snowball starts high enough to clear the floor.
				AChaosImpactBall* Ball = SpawnNewBall(World, Type, Player->GetActorLocation() + Direction * 90.0f
					+ FVector(0, 0, FMath::Max(30.0f, 24.0f * SnowScale - 50.0f)), Player);
				if (Ball)
				{
					if (Type == EChaosImpactBallType::Snow)
					{
						Ball->SetSnowScale(SnowScale);
					}
					Ball->Launch(Direction, 2100.0f, EChaosImpactBallFlightMode::Straight, 0.0f);
				}
				return Ball;
			};
			const auto CountZones = [World](const EChaosImpactBallType Type)
			{
				int32 Count = 0;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					Count += It->GetZoneType() == Type ? 1 : 0;
				}
				return Count;
			};

			switch (Stage)
			{
			case 0:
			{
				for (TActorIterator<AChaosImpactCPUController> It(World); It; ++It)
				{
					CPUCharacter = Cast<AChaosImpactCharacter>(It->GetPawn());
					It->SetActorTickEnabled(false);
				}
				CPU = CPUCharacter.Get();
				bool bWindAbout = Player->IsCarriedByWind() || (CPU && CPU->IsCarriedByWind());
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It)
				{
					bWindAbout = true;
				}
				if (!CPU || !PC->IsGameplayActive() || Player->IsEliminated() || CPU->IsEliminated()
					|| !Player->GetCharacterMovement()->IsMovingOnGround() || bWindAbout)
				{
					return Waited(Now, TEXT("Run with ?CITraining=1?CICPUCount=1 so a CPU exists."));
				}
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (!It->IsPickup() && !It->GetAttachParentActor())
					{
						It->Destroy();
					}
				}
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				Origin = Player->GetActorLocation();
				ChooseOpenDirection(World, Player, CPU);
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Restore(CPU, Origin - Toward * 700.0f);

				// Nine kinds of ball in four bits a slot: the newest two in hand at once.
				Test->TestTrue(TEXT("A beam ball can be picked up"), Give(Player, EChaosImpactBallType::Beam));
				Test->TestTrue(TEXT("A snowball can be picked up"), Give(Player, EChaosImpactBallType::Snow));
				Test->TestEqual(TEXT("The right hand holds the beam ball"), Player->GetCarriedBallType(0), EChaosImpactBallType::Beam);
				Test->TestEqual(TEXT("The left hand holds the snowball"), Player->GetCarriedBallType(1), EChaosImpactBallType::Snow);

				// Throw cancel: the charge stops and the ball stays in hand.
				Player->BeginThrowInput();
				Test->TestTrue(TEXT("Charging a throw"), Player->IsChargingThrow());
				Player->RequestCancelThrow();
				Test->TestFalse(TEXT("Cancel stops the charge"), Player->IsChargingThrow());
				Player->EndThrowInput();
				Test->TestEqual(TEXT("A cancelled throw keeps both balls"), Player->GetCarriedBallCount(), 2);
				Stage = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
			{
				// A smoke ball bursting right beside the CPU: it takes a hit and its eyes sting.
				CPUHealth = CPU->GetHealth();
				Restore(CPU, Origin + Toward * 600.0f);
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke, CPU->GetActorLocation() + Side * 200.0f
					- FVector(0, 0, 60), Player, nullptr);
				Test->TestTrue(TEXT("Smoke from the burst blinds an opponent in it"), CPU->IsBlinded());
				Test->TestTrue(TEXT("The smoke burst hurts like the others"), CPU->GetHealth() < CPUHealth);
				Test->TestFalse(TEXT("The thrower's own smoke leaves them be"), Player->IsBlinded());
				Stage = 2;
				NextAt = Now + 0.9;
				return false;
			}
			case 2:
				// Its thrower sees through it.
				CaptureNewBall(TEXT("NB-01-SmokeThrowerView.png"));
				Stage = 20;
				NextAt = Now + 0.2;
				return false;
			case 20:
				// The CPU's smoke bursting on the player: the view it makes on their own screen (no names or arrows).
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke, Player->GetActorLocation() - FVector(0, 0, 60), CPU, nullptr);
				Test->TestTrue(TEXT("Caught in someone else's smoke"), Player->IsBlinded());
				Stage = 21;
				NextAt = Now + 0.8;
				return false;
			case 21:
				CaptureNewBall(TEXT("NB-02-Blinded.png"));
				Stage = 22;
				NextAt = Now + ChaosImpactBallTypes::SmokeBlindSeconds - 0.8 - 0.45;
				return false;
			case 22:
				// Clearing: names and arrows drifting back in.
				CaptureNewBall(TEXT("NB-02b-Clearing.png"));
				Stage = 3;
				NextAt = Now + 0.8;
				return false;
			case 3:
			{
				// The CPU walks into a cloud long after it burst: blinded while in it.
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				Restore(CPU, Origin - Toward * 700.0f);
				Test->TestFalse(TEXT("Outside any smoke, the CPU is not blinded"), CPU->IsBlinded() && CPU->GetBlindAmount() > 0.99f);
				AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Smoke, Origin + Toward * 900.0f - FVector(0, 0, 60), Player, nullptr);
				Stage = 4;
				NextAt = Now + 0.6;
				return false;
			}
			case 4:
				CPU->ResetForOnlineMatch(CPU->GetActorLocation(), CPU->GetActorRotation());
				PlaceAt(CPU, Origin + Toward * 900.0f);
				Stage = 5;
				NextAt = Now + 0.4;
				return false;
			case 5:
			{
				Test->TestTrue(TEXT("Walking into a hanging cloud blinds"), CPU->IsBlinded());
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					It->Destroy();
				}
				// A beam at a CPU standing behind a wall: through the wall and through the CPU, flying on.
				Restore(CPU, Origin - Toward * 700.0f);
				Stage = 6;
				NextAt = Now + 3.0;
				return false;
			}
			case 6:
			{
				// A wall of our own between the player and the CPU: 40 thick, 4 m wide, 4 m tall.
				const FVector WallAt = Origin + Toward * 400.0f;
				AStaticMeshActor* WallActor = World->SpawnActor<AStaticMeshActor>(WallAt, Toward.Rotation());
				if (WallActor)
				{
					WallActor->SetMobility(EComponentMobility::Movable);
					WallActor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					WallActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					WallActor->SetActorScale3D(FVector(0.4f, 4.0f, 4.0f));
					TestWall = WallActor;
				}
				FHitResult Blocked;
				Test->TestTrue(TEXT("The wall stands between them"), World->LineTraceSingleByObjectType(Blocked,
					Origin + FVector(0, 0, 30), Origin + Toward * 800.0f + FVector(0, 0, 30), FCollisionObjectQueryParams(ECC_WorldStatic)));
				BeamDirection = Toward;
				Restore(CPU, Origin + Toward * 800.0f);
				CPUHealth = CPU->GetHealth();
				Player->ResetForOnlineMatch(Origin, BeamDirection.Rotation());
				BeamBall = Launch(EChaosImpactBallType::Beam, BeamDirection);
				PhaseStartedAt = Now;
				Stage = 7;
				NextAt = Now + 0.12;
				return false;
			}
			case 7:
				CaptureNewBall(TEXT("NB-03-Beam.png"));
				if (CPU->GetHealth() >= CPUHealth && Now - PhaseStartedAt < 1.0)
				{
					return false;
				}
				Test->TestTrue(TEXT("The beam strikes someone behind a wall"), CPU->GetHealth() < CPUHealth);
				Test->TestTrue(TEXT("The beam flies on after striking"), BeamBall.IsValid() && !BeamBall->HasDetonated());
				if (TestWall.IsValid())
				{
					TestWall->Destroy();
				}
				Stage = 8;
				NextAt = Now + 2.5;
				return false;
			case 8:
			{
				Test->TestTrue(TEXT("The beam fades out at the end of its range"), CountZones(EChaosImpactBallType::Beam) > 0
					|| !BeamBall.IsValid() || BeamBall->HasDetonated());
				// Snow: walk with it and it grows (and weighs).
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Test->TestTrue(TEXT("A snowball can be picked up"), Give(Player, EChaosImpactBallType::Snow));
				WalkSpeedSmall = Player->GetCharacterMovement()->MaxWalkSpeed;
				SnowStart = Player->GetActorLocation();
				WalkSteps = 0;
				Stage = 9;
				return false;
			}
			case 9:
				// Back and forth along open floor, a metre a frame.
				if (WalkSteps < 36)
				{
					const float Along = 100.0f * (WalkSteps % 12 < 6 ? WalkSteps % 6 : 6 - WalkSteps % 6);
					PlaceAt(Player, SnowStart + Toward * Along);
					++WalkSteps;
					return false;
				}
				Stage = 10;
				NextAt = Now + 0.6;
				return false;
			case 10:
			{
				const float Growth = Player->GetSnowGrowth(0);
				UE_LOG(LogTemp, Display, TEXT("NEWBALL snow growth %.2f after walking, speed %.0f -> %.0f"), Growth, WalkSpeedSmall,
					Player->GetCharacterMovement()->MaxWalkSpeed);
				Test->TestTrue(TEXT("Walking grows the snowball"), Growth > 0.85f);
				Test->TestTrue(TEXT("A big snowball slows its carrier"), Player->GetCharacterMovement()->MaxWalkSpeed < WalkSpeedSmall * 0.9f);
				Test->TestTrue(TEXT("Not too much"), Player->GetCharacterMovement()->MaxWalkSpeed > WalkSpeedSmall * 0.7f);
				CaptureNewBall(TEXT("NB-04-SnowOverhead.png"));
				SnowGrowthAtThrow = Growth;
				Stage = 100;
				NextAt = Now + 0.3;
				return false;
			}
			case 100:
				// Thrown for real: from over the head.
				Player->BeginThrowInput();
				Player->EndThrowInput();
				Stage = 101;
				NextAt = Now + 0.1;
				return false;
			case 101:
			{
				const AChaosImpactBall* Thrown = nullptr;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Snow && It->WasThrownBy(Player) && !It->IsPickup())
					{
						Thrown = *It;
					}
				}
				Test->TestNotNull(TEXT("The snowball is thrown"), Thrown);
				if (Thrown)
				{
					UE_LOG(LogTemp, Display, TEXT("NEWBALL thrown snowball scale %.2f, %.0f above the thrower"), Thrown->GetSnowScale(),
						Thrown->GetActorLocation().Z - Player->GetActorLocation().Z);
					Test->TestTrue(TEXT("It keeps the size it grew to"),
						FMath::IsNearlyEqual(Thrown->GetSnowScale(), ChaosImpactBallTypes::GetSnowScale(SnowGrowthAtThrow), 0.05f));
					Test->TestTrue(TEXT("It leaves from over the head"), Thrown->GetActorLocation().Z > Player->GetActorLocation().Z + 150.0f);
				}
				CaptureNewBall(TEXT("NB-05-SnowHurled.png"));
				Stage = 102;
				NextAt = Now + 1.5;
				return false;
			}
			case 102:
			{
				const float Growth = SnowGrowthAtThrow;
				// A big snowball 90 across from its line still hits: a normal ball would miss by far.
				Restore(CPU, Origin + Toward * 700.0f + Side * 95.0f);
				CPUHealth = CPU->GetHealth();
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Launch(EChaosImpactBallType::Snow, Toward, ChaosImpactBallTypes::GetSnowScale(Growth));
				PhaseStartedAt = Now;
				Stage = 11;
				NextAt = Now + 0.15;
				return false;
			}
			case 11:
				CaptureNewBall(TEXT("NB-06-SnowHit.png"));
				if (CPU->GetHealth() >= CPUHealth && Now - PhaseStartedAt < 1.2)
				{
					return false;
				}
				Test->TestTrue(TEXT("A big snowball hits with its big size"), CPU->GetHealth() < CPUHealth);
				Stage = 110;
				NextAt = Now + 0.3;
				return false;
			case 110:
				// A fresh, small snowball is thrown by hand like any ball.
				Player->ResetForOnlineMatch(Origin, Toward.Rotation());
				Test->TestTrue(TEXT("A snowball can be picked up again"), Give(Player, EChaosImpactBallType::Snow));
				Player->BeginThrowInput();
				Player->EndThrowInput();
				Stage = 111;
				NextAt = Now + 0.1;
				return false;
			case 111:
			{
				const AChaosImpactBall* Thrown = nullptr;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Snow && It->WasThrownBy(Player) && !It->IsPickup() && !It->HasDetonated())
					{
						Thrown = *It;
					}
				}
				Test->TestNotNull(TEXT("The small snowball is thrown"), Thrown);
				if (Thrown)
				{
					UE_LOG(LogTemp, Display, TEXT("NEWBALL small snowball scale %.2f, %.0f above the thrower"), Thrown->GetSnowScale(),
						Thrown->GetActorLocation().Z - Player->GetActorLocation().Z);
					Test->TestTrue(TEXT("A small snowball is thrown from the hand, not over the head"),
						Thrown->GetActorLocation().Z < Player->GetActorLocation().Z + 120.0f);
				}
				Stage = 12;
				NextAt = Now + 1.0;
				return false;
			}
			case 12:
			{
				CaptureNewBall(TEXT("NB-07-SnowBurst.png"));
				// Going down with two balls in hand drops them.
				Restore(CPU, Origin + Toward * 500.0f);
				Give(CPU, EChaosImpactBallType::Smoke);
				Give(CPU, EChaosImpactBallType::Snow);
				const FVector Fell = CPU->GetActorLocation();
				UGameplayStatics::ApplyDamage(CPU, 10.0f, nullptr, nullptr, nullptr);
				Test->TestTrue(TEXT("The CPU went down"), CPU->IsEliminated());
				int32 Smoke = 0;
				int32 Snow = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->IsPickup() && FVector::Dist2D(It->GetActorLocation(), Fell) < 200.0f)
					{
						Smoke += It->GetBallType() == EChaosImpactBallType::Smoke ? 1 : 0;
						Snow += It->GetBallType() == EChaosImpactBallType::Snow ? 1 : 0;
					}
				}
				Test->TestTrue(TEXT("Its balls fall where it went down"), Smoke == 1 && Snow == 1);
				Stage = 13;
				NextAt = Now + 0.5;
				return false;
			}
			case 13:
				CaptureNewBall(TEXT("NB-08-Dropped.png"));
				Stage = 14;
				NextAt = Now + 0.3;
				return false;
			default:
				return true;
			}
		}

	private:
		bool Waited(const double Now, const TCHAR* Error)
		{
			if (Now - StartedAt < 60.0)
			{
				return false;
			}
			Test->AddError(Error);
			return true;
		}

		/** Level flights stay at the thrower's height: a way that is open and on the player's floor for a good distance. */
		void ChooseOpenDirection(UWorld* World, AActor* Player, AActor* CPU)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NewBallTestOpen), false, Player);
			Params.AddIgnoredActor(CPU);
			const auto FloorAt = [&](const FVector& Where)
			{
				FHitResult Floor;
				return World->LineTraceSingleByObjectType(Floor, Where + FVector(0, 0, 50), Where - FVector(0, 0, 600),
					FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? static_cast<float>(Floor.ImpactPoint.Z) : -1.0e6f;
			};
			const float Ground = FloorAt(Origin);
			float BestOpen = -1.0f;
			for (int32 Turn = 0; Turn < 24; ++Turn)
			{
				const FVector Candidate = FRotator(0.0f, Turn * 15.0f, 0.0f).Vector();
				FHitResult Blocked;
				const float Open = World->SweepSingleByObjectType(Blocked, Origin, Origin + Candidate * 1400.0f, FQuat::Identity,
					FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(90.0f), Params)
					? static_cast<float>(Blocked.Distance) : 1400.0f;
				bool bLevel = true;
				for (const float Along : {300.0f, 700.0f, 1000.0f})
				{
					bLevel &= FMath::Abs(FloorAt(Origin + Candidate * Along) - Ground) < 30.0f;
				}
				if (bLevel && Open > BestOpen)
				{
					BestOpen = Open;
					Toward = Candidate;
				}
			}
		}

		FAutomationTestBase* Test;
		FVector Toward = FVector(1.0f, 0.0f, 0.0f);
		FVector BeamDirection = FVector(1.0f, 0.0f, 0.0f);
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double PhaseStartedAt = 0.0;
		int32 Stage = 0;
		int32 WalkSteps = 0;
		FVector Origin = FVector::ZeroVector;
		FVector SnowStart = FVector::ZeroVector;
		float CPUHealth = 0.0f;
		float WalkSpeedSmall = 0.0f;
		float SnowGrowthAtThrow = 0.0f;
		TWeakObjectPtr<AChaosImpactCharacter> CPUCharacter;
		TWeakObjectPtr<AChaosImpactBall> BeamBall;
		TWeakObjectPtr<AStaticMeshActor> TestWall;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNewBallTest, "ChaosImpact.Training.NewBalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactNewBallTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNewBallCommand(this));
	return true;
}

#endif
