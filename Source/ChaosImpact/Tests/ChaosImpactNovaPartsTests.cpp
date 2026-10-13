#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactNovaParts.h"
#include "ChaosImpactNovaRig.h"
#include "ChaosImpactPlayerController.h"
#include "Camera/CameraActor.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNovaRigTest, "ChaosImpact.Nova.Rig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** The landing's timeline read from the model's animation: the ball of fire grows, holds and shrinks away. */
bool FChaosImpactNovaRigTest::RunTest(const FString& Parameters)
{
	using namespace ChaosImpactNova;
	TestEqual(TEXT("No fire as it lands"), GetExplosionSize(FireStart), 0.0f, 0.001f);
	TestEqual(TEXT("No fire while it spins"), GetExplosionSize(FireExplodeAt - 0.1f), 0.0f, 0.001f);
	TestTrue(TEXT("Growing"), GetExplosionSize(25.2f) > 0.2f && GetExplosionSize(25.2f) < 0.9f);
	TestEqual(TEXT("Full"), GetExplosionSize(27.0f), 1.0f, 0.001f);
	TestTrue(TEXT("Shrinking"), GetExplosionSize(29.2f) > 0.05f && GetExplosionSize(29.2f) < 0.9f);
	TestEqual(TEXT("Gone"), GetExplosionSize(FireGoneAt + 0.1f), 0.0f, 0.001f);
	TestEqual(TEXT("28 parts in the landing"), GetNodeCount(EChaosImpactNovaRig::NovaFire), 28);
	for (int32 Rig = 0; Rig < 5; ++Rig)
	{
		for (int32 Node = 0; Node < GetNodeCount(static_cast<EChaosImpactNovaRig>(Rig)); ++Node)
		{
			const FString Path = GetMeshPath(static_cast<EChaosImpactNovaRig>(Rig), Node);
			TestNotNull(Path, LoadObject<UStaticMesh>(nullptr, *Path));
		}
	}
	TestNotNull(TEXT("Explosion material"), ChaosImpactNova::MakeExplosionMaterial(GetTransientPackage()));
	TestNotNull(TEXT("Metal material"), ChaosImpactNova::MakeMaterial(GetTransientPackage(), EChaosImpactNovaRig::Gear));
	return true;
}

namespace
{
	/**
	 * In training (/Game/ThirdPerson/Lvl_ThirdPerson?CITraining=1?CICPUCount=1?CITargets=0): the three parts come out;
	 * picked up one by one they make a nova in a ball slot; knocked out, the nova is dropped and stays. Then a nova
	 * lands by the (held still) CPU: nothing while it spins, the CPU knocked out once the fire reaches it, and the zone
	 * goes after its whole animation. Screenshots of each stage go to Saved/NovaQA.
	 */
	class FNovaPartsCommand : public IAutomationLatentCommand
	{
	public:
		explicit FNovaPartsCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - StartedAt > 150.0)
			{
				Test->AddError(FString::Printf(TEXT("Stuck at step %d."), Step));
				return true;
			}
			if (Now < NextAt)
			{
				return false;
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
			if (!World || !Player || !PC->IsGameplayActive())
			{
				return false;
			}
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				CPU = *It != Player ? *It : CPU;
			}
			const auto Shot = [](const FString& Name)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NovaQA"), Name), true, false);
			};
			// A camera of its own for the pictures, looking at a point from a side.
			const auto LookAt = [World, PC, this](const FVector& Target, const FVector& From)
			{
				if (!Camera.IsValid())
				{
					Camera = World->SpawnActor<ACameraActor>(From, FRotator::ZeroRotator);
				}
				if (ACameraActor* View = Camera.Get())
				{
					View->SetActorLocation(From);
					View->SetActorRotation((Target - From).Rotation());
					PC->SetViewTarget(View);
				}
			};
			TArray<AChaosImpactNovaPart*> Parts;
			for (TActorIterator<AChaosImpactNovaPart> It(World); It; ++It)
			{
				Parts.Add(*It);
			}
			switch (Step)
			{
			case 0:
			{
				// Out of the way of any set the training would bring by itself: this one now.
				for (AChaosImpactNovaPart* Part : Parts)
				{
					Part->Destroy();
				}
				UChaosImpactNovaDirector* Director = UChaosImpactNovaDirector::Get(World);
				Test->TestNotNull(TEXT("The director"), Director);
				if (!Director)
				{
					return true;
				}
				// Nobody holding any from before (a CPU may have picked one up on its way).
				for (AChaosImpactCharacter* Someone : {Player, CPU})
				{
					if (Someone && Someone->GetNovaParts() != 0)
					{
						Someone->ResetForOnlineMatch(Someone->GetActorLocation(), Someone->GetActorRotation());
					}
				}
				if (CPU)
				{
					CPU->SetTrainingMenuFrozen(true);
				}
				Director->SpawnAllNow();
				Step = 1;
				NextAt = Now + 0.5;
				return false;
			}
			case 1:
				// A CPU that happened to stand where one came out took it: put it back out, beside the others.
				if (CPU && CPU->GetNovaParts() != 0)
				{
					const uint8 Taken = CPU->GetNovaParts();
					const FVector Near = Parts.Num() > 0 ? Parts[0]->GetActorLocation() : Player->GetActorLocation();
					CPU->ResetForOnlineMatch(CPU->GetActorLocation(), CPU->GetActorRotation());
					CPU->SetTrainingMenuFrozen(true);
					ChaosImpactNovaParts::Drop(World, Taken, &Near);
					NextAt = Now + 0.2;
					return false;
				}
				Test->TestEqual(TEXT("Three parts out"), Parts.Num(), 3);
				for (const AChaosImpactNovaPart* Part : Parts)
				{
					// Each out of a ball pad, on it.
					float Nearest = TNumericLimits<float>::Max();
					for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It)
					{
						Nearest = It->IsInPlay() ? FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Part->GetActorLocation()))) : Nearest;
					}
					Test->TestTrue(FString::Printf(TEXT("The %s came out of a pad (%.0f away)"), ChaosImpactNovaParts::GetName(Part->GetPart()), Nearest), Nearest < 20.0f);
				}
				if (Parts.Num() > 0)
				{
					// A close look at one.
					LookAt(Parts[0]->GetActorLocation(), Parts[0]->GetActorLocation() + FVector(-260.0f, 60.0f, 90.0f));
				}
				Step = 2;
				NextAt = Now + 1.0;
				return false;
			case 2:
				// The picture is taken at the end of this frame: change the view only after it.
				Shot(TEXT("1_Part.png"));
				Step = 20;
				NextAt = Now + 0.3;
				return false;
			case 20:
				PC->SetViewTarget(Player);
				// Empty hands, so the nova has a slot to go to (a full pair would keep it waiting).
				Player->RequestDropBall();
				Step = 21;
				NextAt = Now + 0.4;
				return false;
			case 21:
				Player->RequestDropBall();
				Step = 3;
				NextAt = Now + 0.6;
				return false;
			case 3:
				// Each part in turn: walk into it (empty-handed: a pad's ball picked up on the way would leave the nova no slot).
				if (!Parts.IsEmpty() && Player->GetCarriedBallCount() > 0)
				{
					Player->RequestDropBall();
					NextAt = Now + 0.4;
					return false;
				}
				if (!Parts.IsEmpty())
				{
					const uint8 Before = Player->GetNovaParts();
					Player->SetActorLocation(Parts[0]->GetActorLocation() - FVector(0.0f, 0.0f, 60.0f), false, nullptr,
						ETeleportType::TeleportPhysics);
					UE_LOG(LogTemp, Display, TEXT("NOVAPARTS walking into %s (parts %d)"), ChaosImpactNovaParts::GetName(Parts[0]->GetPart()), Before);
					NextAt = Now + 0.5;
					return false;
				}
				Test->TestEqual(TEXT("All parts taken: put together, none left over"), static_cast<int32>(Player->GetNovaParts()), 0);
				{
					bool bNova = false;
					for (int32 Slot = 0; Slot < Player->GetCarriedBallCount(); ++Slot)
					{
						bNova |= Player->GetCarriedBallType(Slot) == EChaosImpactBallType::Nova;
					}
					Test->TestTrue(TEXT("The nova is in a ball slot"), bNova);
				}
				LookAt(Player->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), Player->GetActorLocation() + FVector(-380.0f, 160.0f, 200.0f));
				Step = 4;
				NextAt = Now + 0.8;
				return false;
			case 4:
				Shot(TEXT("2_Finished.png"));
				Step = 40;
				NextAt = Now + 0.3;
				return false;
			case 40:
				PC->SetViewTarget(Player);
				// Knocked out with it: it drops, and stays.
				Player->TakeDamage(100.0f, FDamageEvent(), nullptr, nullptr);
				Step = 5;
				NextAt = Now + 2.5;
				return false;
			case 5:
			{
				int32 Novas = 0;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					Novas += It->GetBallType() == EChaosImpactBallType::Nova && It->IsPickup() ? 1 : 0;
				}
				if (!bCheckedDrop)
				{
					bCheckedDrop = true;
					Test->TestEqual(TEXT("The nova dropped where they went down"), Novas, 1);
				}
				if (Player->IsEliminated())
				{
					return false;
				}
				// The nova again, landing by the held CPU, with the player well back to watch.
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Nova)
					{
						It->Destroy();
					}
				}
				if (!CPU)
				{
					Test->AddError(TEXT("Run with a CPU (?CICPUCount=1)."));
					return true;
				}
				CPU->SetTrainingMenuFrozen(true);
				const FVector Ground = CPU->GetActorLocation() - FVector(0.0f, 0.0f, 90.0f);
				// Fully charged: its blast as wide as a nova's ever was (GetNovaBlastRadius).
				Zone = AChaosImpactHazardZone::Detonate(World, EChaosImpactBallType::Nova, Ground + FVector(220.0f, 0.0f, 40.0f), Player, nullptr,
					ChaosImpactBallTypes::NovaMaxScale);
				Player->SetActorLocation(Ground + FVector(-1600.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
				LookAt(Ground + FVector(220.0f, 0.0f, 300.0f), Ground + FVector(-3300.0f, -1900.0f, 2300.0f));
				Step = 6;
				ShotIndex = 0;
				ZoneStartedAt = Now;
				return false;
			}
			default:
			{
				// Its materials ready (not the stand-in shown while they compile), so the pictures show them.
				if (!bShadersReady)
				{
					bShadersReady = true;
					if (GShaderCompilingManager)
					{
						GShaderCompilingManager->FinishAllCompilation();
					}
				}
				// Its own (game) time: a hitch as it goes off must not put the pictures out of step.
				AChaosImpactHazardZone* Live = Zone.Get();
				if (IsValid(Live))
				{
					LastAge = Live->GetAge();
				}
				const float Age = IsValid(Live) ? LastAge : static_cast<float>(LastAge + (Now - LastAgeAt));
				if (IsValid(Live))
				{
					LastAgeAt = Now;
				}
				static const float ShotAges[] = {1.0f, 3.2f, 4.2f, 5.5f, 7.0f, 11.9f, 13.6f, 15.0f, 15.9f, 16.5f};
				if (ShotIndex < UE_ARRAY_COUNT(ShotAges) && Age >= ShotAges[ShotIndex])
				{
					Shot(FString::Printf(TEXT("3_Landing_%d_%.1fs.png"), ShotIndex, ShotAges[ShotIndex]));
					UE_LOG(LogTemp, Display, TEXT("NOVAPARTS landing %.1fs: fire radius %.0f, CPU %s"), Age,
						Live ? Live->GetNovaFireRadius() : -1.0f, CPU && CPU->IsEliminated() ? TEXT("out") : TEXT("standing"));
					++ShotIndex;
				}
				if (Age > 1.8f && Age < 2.2f)
				{
					Test->TestFalse(TEXT("Nothing happens while it spins"), CPU && CPU->IsEliminated());
				}
				if (Age > 5.0f && !bSawKnockout)
				{
					bSawKnockout = CPU && CPU->IsEliminated();
				}
				if (Age > 9.0f && !bCheckedKnockout)
				{
					bCheckedKnockout = true;
					Test->TestTrue(TEXT("The fire reached the CPU"), bSawKnockout);
				}
				if (IsValid(Live) && !bCheckedReach && Age > 5.0f)
				{
					bCheckedReach = true;
					Test->TestTrue(TEXT("At its fullest the fire reaches as far as the blast always did"),
						FMath::IsNearlyEqual(Live->GetNovaFireRadius(), ChaosImpactBallTypes::GetNovaBlastRadius(ChaosImpactBallTypes::NovaMaxScale), 5.0f));
				}
				if (!IsValid(Live))
				{
					Test->TestTrue(TEXT("Gone only after its whole animation"), LastAge > AChaosImpactHazardZone::NovaBlastSeconds);
					PC->SetViewTarget(Player);
					return true;
				}
				return false;
			}
			}
		}

	private:
		FAutomationTestBase* Test;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		double ZoneStartedAt = 0.0;
		int32 Step = 0;
		int32 ShotIndex = 0;
		bool bSawKnockout = false;
		bool bCheckedDrop = false;
		bool bCheckedKnockout = false;
		bool bShadersReady = false;
		bool bCheckedReach = false;
		TWeakObjectPtr<AChaosImpactHazardZone> Zone;
		TWeakObjectPtr<ACameraActor> Camera;
		float LastAge = 0.0f;
		double LastAgeAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactNovaPartsTest, "ChaosImpact.Nova.Parts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactNovaPartsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNovaPartsCommand(this));
	return true;
}

#endif
