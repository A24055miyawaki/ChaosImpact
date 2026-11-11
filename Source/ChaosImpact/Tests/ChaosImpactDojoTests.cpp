#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactDojo.h"
#include "ChaosImpactDojoLessons.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTrainingTarget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* DojoTestWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	void DojoShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DojoQA"), Name), true, false);
	}

	/**
	 * Run on the dojo's own world (/Game/ChaosImpact/Versus/Levels/Lvl_VS_Stage1?CITraining=1?CIDojo=1?CITargets=0?CICPUCount=0):
	 * the lesson list comes up by itself; every lesson of every group is opened (its first slide), started (its things
	 * set out on the stage) and counted through to CLEAR, with a screenshot of each, and comes back to the list after
	 * the last of a group. -CIDojoOnly=<group> limits it to one group. Saved/DojoQA.
	 */
	class FDojoSweepCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDojoSweepCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIDojoOnly="), Only);
		}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
			if (RealNow - StartedAt > 900.0)
			{
				Test->AddError(TEXT("Timed out."));
				return true;
			}
			UWorld* World = DojoTestWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactDojo* Dojo = World ? AChaosImpactDojo::Find(World) : nullptr;
			if (!PC || !Dojo || !PC->GetPawn())
			{
				return false;
			}
			// (Real time: the lesson list pauses the world.)
			const double Now = RealNow;
			if (Now < NextAt)
			{
				return false;
			}
			const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
			switch (Stage)
			{
			case 0:
				if (PC->GetCurrentScreen() != EChaosImpactScreen::DojoSelect)
				{
					if (RealNow - StartedAt > 90.0)
					{
						Test->AddError(TEXT("The lesson list never came up."));
						return true;
					}
					return false;
				}
				ChaosImpactDojo::ResetProgress();
				Stage = 10;
				NextAt = Now + 2.0;
				return false;
			case 10:
				// (The preview's clip has had time to start.)
				DojoShot(TEXT("00_List.png"));
				Group = Only >= 0 ? Only : 0;
				Lesson = 0;
				Stage = 1;
				NextAt = Now + 0.6;
				return false;
			case 1:
				if (!All.IsValidIndex(Group) || (Only >= 0 && Group != Only))
				{
					DojoShot(TEXT("99_ListAfter.png"));
					Stage = 9;
					NextAt = Now + 0.6;
					return false;
				}
				// (A lesson held on the other level is swept on that level's own run.)
				if (!Dojo->IsLessonHere(All[Group].Lessons[Lesson]))
				{
					Stage = 5;
					bSkipped = true;
					return false;
				}
				bSkipped = false;
				Dojo->StartLesson(Group, Lesson);
				Test->TestEqual(TEXT("A lesson opens on its slides"), static_cast<int32>(Dojo->GetPhase()), static_cast<int32>(EChaosImpactDojoPhase::Intro));
				Stage = 2;
				NextAt = Now + 1.4;
				return false;
			case 2:
				// (A screenshot is taken at the end of the frame: the state is changed a moment later.)
				DojoShot(FString::Printf(TEXT("%d_%02d_a_Slide.png"), Group, Lesson));
				Stage = 21;
				NextAt = Now + 0.3;
				return false;
			case 21:
				if (Lesson == 0)
				{
					Dojo->DebugReady();
					Stage = 6;
					NextAt = Now + 0.8;
					return false;
				}
				Dojo->DebugBegin();
				Test->TestEqual(TEXT("Started, the lesson is played"), static_cast<int32>(Dojo->GetPhase()), static_cast<int32>(EChaosImpactDojoPhase::Play));
				Stage = 3;
				NextAt = Now + 1.6;
				return false;
			case 3:
				DojoShot(FString::Printf(TEXT("%d_%02d_b_Play.png"), Group, Lesson));
				Stage = 31;
				NextAt = Now + 0.3;
				return false;
			case 31:
				Dojo->DebugCount(Dojo->GetLesson() ? Dojo->GetLesson()->Count : 1);
				Test->TestEqual(TEXT("Counted out, the lesson is cleared"), static_cast<int32>(Dojo->GetPhase()), static_cast<int32>(EChaosImpactDojoPhase::Clear));
				Test->TestTrue(TEXT("A cleared lesson is kept"), Dojo->GetLesson() && ChaosImpactDojo::IsCleared(Dojo->GetLesson()->Id));
				Stage = 4;
				NextAt = Now + 0.5;
				return false;
			case 4:
				DojoShot(FString::Printf(TEXT("%d_%02d_c_Clear.png"), Group, Lesson));
				Stage = 41;
				NextAt = Now + AChaosImpactDojo::ClearMenuDelay + 0.6;
				return false;
			case 41:
			{
				// CLEAR's choices: on to the next lesson when it is held here, else back to the list.
				Test->TestTrue(TEXT("CLEAR offers its choices"), Dojo->IsClearMenuUp());
				DojoShot(FString::Printf(TEXT("%d_%02d_c2_Choices.png"), Group, Lesson));
				Stage = 42;
				NextAt = Now + 0.3;
				return false;
			}
			case 42:
			{
				const bool bNextHere = Lesson + 1 < All[Group].Lessons.Num() && Dojo->IsLessonHere(All[Group].Lessons[Lesson + 1]);
				Dojo->ChooseAfterClear(bNextHere ? 0 : 2);
				Stage = 5;
				NextAt = Now + 0.5;
				return false;
			}
			case 5:
			{
				// After CLEAR: the next lesson's slides, or (after a group's last) the list.
				const bool bLast = Lesson + 1 >= All[Group].Lessons.Num();
				if (bLast)
				{
					if (!bSkipped)
					{
						Test->TestEqual(TEXT("After a group's last lesson, the list"), static_cast<int32>(PC->GetCurrentScreen()),
							static_cast<int32>(EChaosImpactScreen::DojoSelect));
					}
					DojoShot(FString::Printf(TEXT("%d_99_ListDone.png"), Group));
					++Group;
					Lesson = 0;
				}
				else
				{
					if (!bSkipped && Dojo->IsLessonHere(All[Group].Lessons[Lesson + 1]))
					{
						Test->TestEqual(TEXT("After CLEAR, the next lesson"), Dojo->GetLessonIndex(), Lesson + 1);
					}
					++Lesson;
				}
				Stage = 1;
				NextAt = Now + 0.4;
				return false;
			}
			case 6:
				DojoShot(FString::Printf(TEXT("%d_%02d_a2_Ready.png"), Group, Lesson));
				Stage = 61;
				NextAt = Now + 0.3;
				return false;
			case 61:
				Dojo->DebugBegin();
				Stage = 3;
				NextAt = Now + 1.6;
				return false;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test = nullptr;
		double StartedAt = FPlatformTime::Seconds();
		double NextAt = 0.0;
		int32 Stage = 0;
		int32 Group = 0;
		int32 Lesson = 0;
		int32 Only = -1;
		bool bSkipped = false;
	};

	/**
	 * The same world: a few lessons done for real, by the player's own actions (no counting by hand), to see each task
	 * is counted from what really happens: dash, jump, a full charge thrown, the balls picked up, the rings walked to.
	 */
	class FDojoPlayCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDojoPlayCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
			if (RealNow - StartedAt > 400.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at %d"), Stage));
				return true;
			}
			UWorld* World = DojoTestWorld();
			AChaosImpactPlayerController* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			AChaosImpactDojo* Dojo = World ? AChaosImpactDojo::Find(World) : nullptr;
			AChaosImpactCharacter* Me = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			if (!PC || !Dojo || !Me || (Stage == 0 && PC->GetCurrentScreen() != EChaosImpactScreen::DojoSelect))
			{
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (Now < NextAt)
			{
				return false;
			}
			const auto Find = [](const TCHAR* Id, int32& OutGroup, int32& OutLesson)
			{
				const TArray<FChaosImpactDojoCategory>& All = ChaosImpactDojo::GetCategories();
				for (int32 G = 0; G < All.Num(); ++G)
				{
					for (int32 L = 0; L < All[G].Lessons.Num(); ++L)
					{
						if (All[G].Lessons[L].Id == Id)
						{
							OutGroup = G;
							OutLesson = L;
							return true;
						}
					}
				}
				return false;
			};
			const auto Open = [&](const TCHAR* Id)
			{
				int32 G = 0;
				int32 L = 0;
				Test->TestTrue(FString::Printf(TEXT("Lesson %s exists"), Id), Find(Id, G, L));
				Dojo->StartLesson(G, L);
				Dojo->DebugBegin();
			};
			const auto Done = [&](const TCHAR* What)
			{
				const bool bClear = Dojo->GetPhase() == EChaosImpactDojoPhase::Clear;
				UE_LOG(LogTemp, Log, TEXT("Dojo play: %s -> phase %d, done %d"), What, static_cast<int32>(Dojo->GetPhase()), Dojo->GetDone());
				Test->TestTrue(FString::Printf(TEXT("%s is cleared by doing it"), What), bClear);
			};
			switch (Stage)
			{
			case 0:
				Open(TEXT("b_dash"));
				Step = 0;
				Stage = 1;
				NextAt = Now + 0.6;
				return false;
			case 1:
				// Three dashes, a moment apart.
				if (Dojo->GetPhase() == EChaosImpactDojoPhase::Clear || Step >= 6)
				{
					Done(TEXT("Dash"));
					Open(TEXT("b_jump"));
					Step = 0;
					Stage = 2;
					NextAt = Now + 0.6;
					return false;
				}
				Me->RequestAIDash(Step % 2 == 0 ? FVector::ForwardVector : -FVector::ForwardVector);
				++Step;
				NextAt = Now + 1.1;
				return false;
			case 2:
				if (Dojo->GetPhase() == EChaosImpactDojoPhase::Clear || Step >= 6)
				{
					Done(TEXT("Jump"));
					Open(TEXT("b_charge"));
					Step = 0;
					Stage = 3;
					NextAt = Now + 0.8;
					return false;
				}
				Me->Jump();
				++Step;
				NextAt = Now + 1.2;
				return false;
			case 3:
				// A full charge, thrown; three times (the ball is handed back each time).
				if (Dojo->GetPhase() == EChaosImpactDojoPhase::Clear || Step >= 8)
				{
					Done(TEXT("Charged throw"));
					Open(TEXT("b_pickup"));
					Stage = 5;
					NextAt = Now + 0.6;
					return false;
				}
				if (Me->GetCarriedBallCount() > 0 && !Me->IsChargingThrow())
				{
					Me->BeginThrowInput();
					Stage = 4;
					NextAt = Now + 2.2;
					return false;
				}
				NextAt = Now + 0.3;
				return false;
			case 4:
				Me->EndThrowInput();
				++Step;
				Stage = 3;
				NextAt = Now + 1.0;
				return false;
			case 5:
			{
				// Walk onto each ball lying out.
				TArray<AChaosImpactBall*> Lying;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->IsPickup())
					{
						Lying.Add(*It);
					}
				}
				if (Dojo->GetPhase() == EChaosImpactDojoPhase::Clear || Lying.IsEmpty() || Step >= 6)
				{
					Done(TEXT("Pick up"));
					Open(TEXT("b_move"));
					Step = 0;
					Stage = 6;
					NextAt = Now + 0.6;
					return false;
				}
				Me->SetActorLocation(Lying[0]->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f), false, nullptr, ETeleportType::TeleportPhysics);
				++Step;
				NextAt = Now + 0.6;
				return false;
			}
			case 6:
			{
				// Walk the rings: moved onto each in turn (the glowing one).
				if (Dojo->GetPhase() == EChaosImpactDojoPhase::Clear || Step >= 6)
				{
					Done(TEXT("Rings"));
					Stage = 7;
					NextAt = Now + 0.6;
					return false;
				}
				const FVector Ring = Dojo->DebugNextRing();
				Me->SetActorLocation(Ring + FVector(0.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
				++Step;
				NextAt = Now + 0.6;
				return false;
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
		int32 Step = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDojoSweepTest, "ChaosImpact.Dojo.Sweep",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDojoSweepTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDojoSweepCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactDojoPlayTest, "ChaosImpact.Dojo.Play",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactDojoPlayTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDojoPlayCommand(this));
	return true;
}

#endif
