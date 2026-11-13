#if WITH_DEV_AUTOMATION_TESTS
// パーティルール (ルビーラッシュ / クラウンキープ): the rules, a whole match of each on this machine, the screens, and a guest's
// view of a host's match (ChaosImpact.Party.Online, run in a second process joined to a listen host).
#include "Misc/AutomationTest.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHook.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactResults.h"
#include "ChaosImpactScreen.h"
#include "ChaosImpactMenuWidget.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UWorld* PTWorld()
	{
		UWorld* Found = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld())
			{
				Found = Context.World();
			}
		}
		return Found;
	}

	void PTShot(const FString& Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PartyQA"), Name), true, false);
	}

	/** Puts a character's feet on Floor (its own machine moves it: the movement is the owner's). */
	void PTPlace(AChaosImpactCharacter* Character, const FVector& Floor)
	{
		const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Character->SetActorLocation(Floor + FVector(0, 0, HalfHeight + 2.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}

	AChaosImpactCharacter* PTOwnCharacter(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Cast<AChaosImpactCharacter>(Controller->GetPawn()) : nullptr;
	}

	TArray<AChaosImpactCharacter*> PTCPUs(UWorld* World)
	{
		TArray<AChaosImpactCharacter*> Out;
		for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
		{
			if (!It->IsPlayerControlled() && It->GetController())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	const FChaosImpactRubyGem* PTNearestGem(const AChaosImpactRubyField* Field, const FVector& From, const bool bDropped, const APawn* Dropper = nullptr)
	{
		const FChaosImpactRubyGem* Best = nullptr;
		for (const FChaosImpactRubyGem& Gem : Field->GetGems())
		{
			if (!Gem.IsCollected() && Gem.bDropped == bDropped && (!Dropper || Gem.Dropper == Dropper)
				&& (!Best || FVector::DistSquared(Gem.To, From) < FVector::DistSquared(Best->To, From)))
			{
				Best = &Gem;
			}
		}
		return Best;
	}

	void PTUnpause(UWorld* World)
	{
		if (APlayerController* Pausing = World ? World->GetFirstPlayerController() : nullptr; Pausing && Pausing->IsPaused())
		{
			Pausing->SetPause(false);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyRulesTest, "ChaosImpact.Party.Rules", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyRulesTest::RunTest(const FString&)
{
	using namespace ChaosImpactMatch;
	FChaosImpactMatchRules Rules;
	AChaosImpactPlayerController::ApplyMatchMode(Rules, 11);
	Rules = Sanitize(Rules, 1);
	TestTrue(TEXT("Mode 11 is ルビーラッシュ"), Rules.IsRubyRush());
	TestFalse(TEXT("Everyone for themselves"), Rules.IsTeamBattle());
	TestEqual(TEXT("ルビーラッシュ is named"), DescribeRule(Rules), FString(TEXT("ルビーラッシュ  個人戦")));
	Rules.Minutes = 5;
	TestEqual(TEXT("ルビーラッシュ offers the usual lengths"), Sanitize(Rules, 1).Minutes, 5);
	Rules.Minutes = 4;
	TestEqual(TEXT("ルビーラッシュ: 4 minutes"), Sanitize(Rules, 1).Minutes, 4);

	AChaosImpactPlayerController::ApplyMatchMode(Rules, 12);
	TestTrue(TEXT("Mode 12 is クラウンキープ"), Sanitize(Rules, 1).IsCrownKeep());
	for (const TPair<int32, int32>& Case : TArray<TPair<int32, int32>>{{2, 3}, {3, 3}, {4, 5}, {5, 5}, {9, 5}})
	{
		Rules.Minutes = Case.Key;
		TestEqual(FString::Printf(TEXT("クラウンキープ: %d minutes become %d"), Case.Key, Case.Value), Sanitize(Rules, 1).Minutes, Case.Value);
	}
	Rules.TeamCount = 3;
	Rules.bCrashBall = true;
	Rules = Sanitize(Rules, 2);
	TestEqual(TEXT("No teams in a party rule"), Rules.TeamCount, 0);
	TestFalse(TEXT("Never クラッシュボール"), Rules.bCrashBall);
	TestTrue(TEXT("Still クラウンキープ"), Rules.IsCrownKeep());

	// Other kinds clear it.
	for (const int32 Mode : {0, 1, 4, 5, 6, 10})
	{
		FChaosImpactMatchRules Other = Rules;
		AChaosImpactPlayerController::ApplyMatchMode(Other, Mode);
		Other = Sanitize(Other, 1);
		TestFalse(FString::Printf(TEXT("Mode %d is no party rule"), Mode), Other.IsParty());
	}

	// The URL options carry it (a local match level opens with them).
	FChaosImpactMatchRules Ruby;
	AChaosImpactPlayerController::ApplyMatchMode(Ruby, 11);
	Ruby.Minutes = 4;
	Ruby.CPUCount = 5;
	Ruby = Sanitize(Ruby, 1);
	FChaosImpactMatchRules Read;
	TestTrue(TEXT("Options read back"), ReadOptions(FURL(nullptr, *(FString(TEXT("/Game/Map?")) + ToOptions(Ruby)), TRAVEL_Absolute), Read));
	TestEqual(TEXT("Party rule read back"), static_cast<int32>(Read.PartyRule), static_cast<int32>(PartyRuby));
	TestEqual(TEXT("Minutes read back"), Read.Minutes, 4);

	// Drops.
	TestEqual(TEXT("Nothing to drop"), GetRubyDrop(0, false), 0);
	TestEqual(TEXT("A hit always drops one"), GetRubyDrop(1, false), 1);
	TestEqual(TEXT("A hit drops 20% (10)"), GetRubyDrop(10, false), 2);
	TestEqual(TEXT("A hit drops 20% (13)"), GetRubyDrop(13, false), 3);
	TestEqual(TEXT("A hit drops 20% (50)"), GetRubyDrop(50, false), 10);
	TestEqual(TEXT("A knockout drops half (1)"), GetRubyDrop(1, true), 1);
	TestEqual(TEXT("A knockout drops half (9)"), GetRubyDrop(9, true), 5);
	TestEqual(TEXT("A knockout drops half (40)"), GetRubyDrop(40, true), 20);
	TestTrue(TEXT("A count is a little longer than a second"), CrownCountSeconds > 1.0f && CrownCountSeconds < 1.5f);
	// Drops of hundreds stay a handful of gems, every ruby accounted for.
	for (const int32 Amount : {1, 7, 24, 25, 100, 160, 400, 1000})
	{
		int32 Ones = 0, Big = 0, Huge = 0;
		ChaosImpactParty::SplitDrop(Amount, Ones, Big, Huge);
		TestEqual(FString::Printf(TEXT("Drop of %d adds up"), Amount), Ones + Big * ChaosImpactParty::BigRubyValue + Huge * ChaosImpactParty::HugeRubyValue, Amount);
		TestTrue(FString::Printf(TEXT("Drop of %d is a handful of gems (%d+%d+%d)"), Amount, Ones, Big, Huge),
			Ones + Big + Huge <= ChaosImpactParty::MaxGemsPerDrop || Amount > ChaosImpactParty::MaxGemsPerDrop * ChaosImpactParty::HugeRubyValue);
	}
	TestEqual(TEXT("13 kinds on the rules screen"), AChaosImpactPlayerController::MatchModeCount, 13);
	TestEqual(TEXT("同時進行 is a tournament"), AChaosImpactPlayerController::GetChoiceMode(AChaosImpactPlayerController::TournamentAllAtOnceChoice), 10);
	TestTrue(TEXT("The gem mesh builds"), ChaosImpactParty::GetGemMesh() != nullptr && ChaosImpactParty::GetGemMesh()->GetRenderData() != nullptr);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------

namespace
{
	/**
	 * ルビーラッシュ on this machine against CPUs: the field, a pickup, a hit's drop (the dropper kept off it for a moment),
	 * a knockout's drop, the CPUs collecting, and the results.
	 */
	class FPartyRubyMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyRubyMatchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIPartyStage="), StageIndex);
		}

		bool Update() override
		{
			UWorld* World = PTWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match)
			{
				Test->AddError(TEXT("Requires a game world"));
				return true;
			}
			PTUnpause(World);
			// (Waits in game time: a screenshot's hitch moves both clocks, but the test's own clock would lag behind.)
			const double Now = World->GetTimeSeconds();
			if (FPlatformTime::Seconds() - Started > 200.0)
			{
				Test->AddError(FString::Printf(TEXT("Ruby match timed out at step %d"), Step));
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}
			AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
			AChaosImpactCharacter* Own = PTOwnCharacter(World);
			AChaosImpactPlayerState* Member = Own ? Own->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = ChaosImpactMatch::PartyRuby;
				Rules.CPUCount = 3;
				Rules.CPULevel = 1;
				Rules.StageIndex = StageIndex;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Field = AChaosImpactRubyField::Get(World);
				Test->TestNotNull(TEXT("The rubies' field was made"), Field);
				if (!Field)
				{
					return true;
				}
				Test->TestTrue(FString::Printf(TEXT("Plenty of floor for rubies (%d spots)"), Field->GetFieldPoints().Num()), Field->GetFieldPoints().Num() >= 60);
				Test->TestTrue(FString::Printf(TEXT("Rubies laid out (%d)"), Field->GetGems().Num()), Field->GetGems().Num() >= 40);
				Step = 1;
				return false;
			}
			case 1:
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Field || !Own || !Member)
				{
					return false;
				}
				PTShot(FString::Printf(TEXT("Ruby_Stage%d_Start.png"), StageIndex + 1));
				// Every ruby on open floor someone can stand on.
				for (const FChaosImpactRubyGem& Gem : Field->GetGems())
				{
					FVector Floor;
					if (!ChaosImpactParty::FindFloor(World, Gem.To, 30.0f, 60.0f, Floor))
					{
						Test->AddError(FString::Printf(TEXT("Ruby %d is not on a floor (%s)"), Gem.Id, *FVector(Gem.To).ToCompactString()));
						break;
					}
				}
				Before = Member->Points;
				if (const FChaosImpactRubyGem* Gem = PTNearestGem(Field, Own->GetActorLocation(), false))
				{
					TakenId = Gem->Id;
					PTPlace(Own, Gem->To);
				}
				WaitUntil = Now + 0.6;
				Step = 2;
				return false;
			case 2:
			{
				Test->TestTrue(FString::Printf(TEXT("A ruby picked up (%d -> %d)"), Before, Member->Points), Member->Points > Before);
				const FChaosImpactRubyGem* Taken = Field->GetGems().FindByPredicate([this](const FChaosImpactRubyGem& Gem) { return Gem.Id == TakenId; });
				Test->TestTrue(TEXT("It is gone from the floor"), !Taken || Taken->IsCollected());
				PTShot(TEXT("Ruby_Pickup.png"));
				// A hit with ten: two shaken loose.
				Member->Points = 10;
				const TArray<AChaosImpactCharacter*> CPUs = PTCPUs(World);
				Attacker = CPUs.IsEmpty() ? nullptr : CPUs[0];
				const int32 GemsBefore = Field->GetGems().Num();
				Own->TakeDamage(1.0f, FDamageEvent(), Attacker ? Attacker->GetController() : nullptr, Attacker);
				Test->TestEqual(TEXT("A hit drops 20%"), Member->Points, 8);
				int32 Dropped = 0;
				for (const FChaosImpactRubyGem& Gem : Field->GetGems())
				{
					Dropped += Gem.bDropped && Gem.Dropper == Own ? Gem.Value : 0;
				}
				Test->TestEqual(TEXT("Two rubies fly out"), Dropped, 2);
				Test->TestTrue(TEXT("New gems in the list"), Field->GetGems().Num() > GemsBefore);
				WaitUntil = Now + 0.25;
				Step = 3;
				return false;
			}
			case 3:
				// Its dropper cannot take them back at once.
				if (const FChaosImpactRubyGem* Gem = PTNearestGem(Field, Own->GetActorLocation(), true, Own))
				{
					WaitUntil = Now + 0.45;
					PTPlace(Own, Gem->To);
					DropId = Gem->Id;
				}
				Step = 4;
				return false;
			case 4:
			{
				const FChaosImpactRubyGem* Gem = Field->GetGems().FindByPredicate([this](const FChaosImpactRubyGem& Item) { return Item.Id == DropId; });
				Test->TestTrue(TEXT("The dropper's own rubies wait a moment for them"), Gem && !Gem->IsCollected());
				PTShot(TEXT("Ruby_Drop.png"));
				WaitUntil = Now + 1.2;
				Step = 5;
				return false;
			}
			case 5:
				{
					const FChaosImpactRubyGem* Gem = Field->GetGems().FindByPredicate([this](const FChaosImpactRubyGem& Item) { return Item.Id == DropId; });
					Test->TestTrue(TEXT("Then they can take it back"), !Gem || (Gem->IsCollected() && Gem->Collector == Own));
				}
				// A knockout: half of them.
				Member->Points = 9;
				Own->TakeDamage(Own->GetHealth() + 1.0f, FDamageEvent(), Attacker ? Attacker->GetController() : nullptr, Attacker);
				Test->TestTrue(TEXT("Knocked out"), Own->IsEliminated());
				Test->TestEqual(TEXT("A knockout drops half"), Member->Points, 4);
				WaitUntil = Now + 0.3;
				Step = 6;
				return false;
			case 6:
				PTShot(TEXT("Ruby_KO.png"));
				UGameplayStatics::SetGlobalTimeDilation(World, 2.0f);
				WaitUntil = Now + 12.0;
				Step = 7;
				return false;
			case 7:
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
				int32 CPURubies = 0;
				for (const AChaosImpactCharacter* CPU : PTCPUs(World))
				{
					const AChaosImpactPlayerState* State = CPU->GetPlayerState<AChaosImpactPlayerState>();
					CPURubies += State ? State->Points : 0;
				}
				UE_LOG(LogTemp, Display, TEXT("PARTYTEST CPU rubies after a while: %d, lying %d"), CPURubies, Field->CountLying());
				Test->TestTrue(FString::Printf(TEXT("The CPUs collect rubies (%d)"), CPURubies), CPURubies > 0);
				Test->TestTrue(TEXT("The field is kept stocked"), Field->CountLying() >= 30);
				PTShot(FString::Printf(TEXT("Ruby_Stage%d_Play.png"), StageIndex + 1));
				// Scoreboard order is by rubies.
				const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
				bool bSorted = true;
				for (int32 Index = 1; Index < Ranking.Num(); ++Index)
				{
					bSorted &= Ranking[Index - 1]->Points >= Ranking[Index]->Points;
				}
				Test->TestTrue(TEXT("Ranked by rubies"), bSorted);
				GM->EndMatchForTests();
				WaitUntil = Now + 2.0;
				Step = 8;
				return false;
			}
			case 8:
			{
				const FChaosImpactResultsData Results = FChaosImpactResultsData::Capture(Match, GM);
				Test->TestEqual(TEXT("The results know the rule"), static_cast<int32>(Results.PartyRule), static_cast<int32>(ChaosImpactMatch::PartyRuby));
				Test->TestTrue(TEXT("Scores read as rubies"), !Results.Entries.IsEmpty() && Results.ScoreText(Results.Entries[0]).Contains(TEXT("ルビー")));
				return true;
			}
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 StageIndex = 0;
		int32 Before = 0;
		int32 TakenId = 0;
		int32 DropId = 0;
		AChaosImpactCharacter* Attacker = nullptr;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
	};

	/**
	 * クラウンキープ on this machine: taking the crown, the holder's slower walk, its counts, a hit knocking it off (its
	 * holder kept off it a moment), a CPU's hook catching it off the floor, and the goal ending the match.
	 */
	class FPartyCrownMatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyCrownMatchCommand(FAutomationTestBase* InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIPartyStage="), StageIndex);
		}

		bool Update() override
		{
			UWorld* World = PTWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match)
			{
				Test->AddError(TEXT("Requires a game world"));
				return true;
			}
			PTUnpause(World);
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 200.0)
			{
				Test->AddError(FString::Printf(TEXT("Crown match timed out at step %d"), Step));
				return true;
			}
			if (Now < WaitUntil)
			{
				return false;
			}
			AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
			AChaosImpactCharacter* Own = PTOwnCharacter(World);
			AChaosImpactPlayerState* Member = Own ? Own->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			switch (Step)
			{
			case 0:
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = ChaosImpactMatch::PartyCrown;
				Rules.Minutes = 5;
				Rules.CPUCount = 3;
				Rules.CPULevel = 0;
				Rules.StageIndex = StageIndex;
				GM->ConfigureVersusMatch(Rules);
				Test->TestEqual(TEXT("Five minutes"), Match->Rules.Minutes, 5);
				GM->ConfirmTeamsAndStart();
				Test->TestNotNull(TEXT("The crown was made"), AChaosImpactCrown::Get(World));
				Step = 1;
				return false;
			}
			case 1:
				if (Match->Phase != EChaosImpactOnlinePhase::Match || !Crown || !Own || !Member)
				{
					return false;
				}
				{
					// Everyone started round the edge, about as far from the crown as each other.
					float Nearest = UE_BIG_NUMBER;
					float Furthest = 0.0f;
					for (const AChaosImpactPlayerState* Starter : Match->GetCompetitors(true))
					{
						if (const APawn* Body = Starter->GetPawn())
						{
							const float Distance = static_cast<float>(FVector::Dist2D(Body->GetActorLocation(), Crown->GetState().To));
							Nearest = FMath::Min(Nearest, Distance);
							Furthest = FMath::Max(Furthest, Distance);
						}
					}
					UE_LOG(LogTemp, Display, TEXT("PARTYTEST crown starts: %.0f - %.0f from the crown"), Nearest, Furthest);
					Test->TestTrue(FString::Printf(TEXT("Everyone starts out at the edge (nearest %.0f)"), Nearest), Nearest > 1300.0f);
				}
				// The CPUs held back for the checks (they would run for it).
				for (AChaosImpactCharacter* CPU : PTCPUs(World))
				{
					PTPlace(CPU, ChaosImpactParty::NearestPoint(GM->GetPartyFieldPoints(), Crown->GetState().To + FVector(2500, 0, 0)));
					CPU->GetCharacterMovement()->DisableMovement();
				}
				PTShot(TEXT("Crown_Start.png"));
				BaseSpeed = Own->GetCharacterMovement()->MaxWalkSpeed;
				PTPlace(Own, Crown->GetState().To);
				WaitUntil = Now + 0.4;
				Step = 2;
				return false;
			case 2:
				Test->TestTrue(TEXT("The player wears the crown"), Crown->GetHolder() == Own && Crown->GetState().HolderState == Member);
				UE_LOG(LogTemp, Display, TEXT("PARTYTEST speed scale %.2f max walk %.0f tick %d"), ChaosImpactParty::GetSpeedScale(Own),
					Own->GetCharacterMovement()->MaxWalkSpeed, Own->IsActorTickEnabled() ? 1 : 0);
				PTShot(TEXT("Crown_Taken.png"));
				WaitUntil = Now + 2.65;
				Step = 3;
				return false;
			case 3:
			{
				Test->TestEqual(TEXT("Two counts in 2.65 s (a count is 1.25 s)"), Member->Points, 2);
				Test->TestTrue(FString::Printf(TEXT("Slower with it (%.0f -> %.0f)"), BaseSpeed, Own->GetCharacterMovement()->MaxWalkSpeed),
					FMath::IsNearlyEqual(Own->GetCharacterMovement()->MaxWalkSpeed, BaseSpeed * ChaosImpactParty::CrownSpeedScale, 2.0f));
				PTShot(TEXT("Crown_Counting.png"));
				const TArray<AChaosImpactCharacter*> CPUs = PTCPUs(World);
				Hooker = CPUs.IsEmpty() ? nullptr : CPUs[0];
				Own->TakeDamage(1.0f, FDamageEvent(), Hooker ? Hooker->GetController() : nullptr, Hooker);
				Test->TestTrue(TEXT("A hit knocks it off"), Crown->GetState().HolderState == nullptr && Crown->IsOnFloor());
				Test->TestEqual(TEXT("Its counts stay with the player"), Member->Points, 2);
				Test->TestTrue(TEXT("It lands a little way off"), FVector::Dist2D(Crown->GetState().To, Own->GetActorLocation()) > 250.0f);
				WaitUntil = Now + 0.3;
				Step = 4;
				return false;
			}
			case 4:
				PTShot(TEXT("Crown_Dropped.png"));
				// Walking straight onto it: kept off it for a moment.
				PTPlace(Own, Crown->GetState().To);
				WaitUntil = Now + 0.35;
				Step = 5;
				return false;
			case 5:
				Test->TestTrue(TEXT("The one who dropped it cannot take it straight back"), Crown->GetHolder() != Own);
				// Moved off it, then a CPU's hook from across the floor.
				PTPlace(Own, ChaosImpactParty::NearestPoint(GM->GetPartyFieldPoints(), Crown->GetState().To + FVector(0, 1600, 0)));
				WaitUntil = Now + 1.0;
				Step = 6;
				return false;
			case 6:
				if (Hooker)
				{
					Hooker->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
					const FVector Lying = Crown->GetState().To;
					const FVector Spot = ChaosImpactParty::FindLandSpot(World, nullptr, GM->GetPartyFieldPoints(), Lying, 600.0f, 900.0f, Stream);
					PTPlace(Hooker, Spot);
					const FVector Hand = Hooker->GetActorLocation() + FVector(0, 0, 20);
					AChaosImpactHook::Release(World, Hand, (Lying - Hand).GetSafeNormal2D(), Hooker);
				}
				WaitUntil = Now + 1.6;
				Step = 7;
				return false;
			case 7:
			{
				Test->TestTrue(TEXT("A hook catches the crown off the floor for its thrower"), Hooker && Crown->GetHolder() == Hooker);
				PTShot(TEXT("Crown_Hooked.png"));
				// The goal: the holder's last count ends the match.
				if (AChaosImpactPlayerState* HookerState = Hooker ? Hooker->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
				{
					HookerState->Points = ChaosImpactMatch::CrownGoalCounts - 1;
				}
				WaitUntil = Now + 1.6;
				Step = 8;
				return false;
			}
			case 8:
			{
				Test->TestEqual(TEXT("Twenty counts end it"), static_cast<int32>(Match->Phase), static_cast<int32>(EChaosImpactOnlinePhase::Results));
				const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
				Test->TestTrue(TEXT("The holder wins"), !Ranking.IsEmpty() && Hooker && Ranking[0] == Hooker->GetPlayerState());
				const FChaosImpactResultsData Results = FChaosImpactResultsData::Capture(Match, GM);
				Test->TestEqual(TEXT("The results know the rule"), static_cast<int32>(Results.PartyRule), static_cast<int32>(ChaosImpactMatch::PartyCrown));
				for (AChaosImpactCharacter* CPU : PTCPUs(World))
				{
					CPU->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				}
				WaitUntil = Now + 2.0;
				Step = 9;
				return false;
			}
			case 9:
				PTShot(TEXT("Crown_Finish.png"));
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 StageIndex = 0;
		float BaseSpeed = 0.0f;
		AChaosImpactCharacter* Hooker = nullptr;
		FRandomStream Stream{7};
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
	};

	/** CPUs alone (watching) play a sped-up match of each rule on a stage: it goes the distance without a hitch. */
	class FPartyCPUMatchCommand : public IAutomationLatentCommand
	{
	public:
		FPartyCPUMatchCommand(FAutomationTestBase* InTest, const uint8 InRule) : Test(InTest), Rule(InRule)
		{
			FParse::Value(FCommandLine::Get(), TEXT("CIPartyStage="), StageIndex);
		}

		bool Update() override
		{
			UWorld* World = PTWorld();
			auto* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			auto* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!GM || !Match)
			{
				Test->AddError(TEXT("Requires a game world"));
				return true;
			}
			PTUnpause(World);
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 300.0)
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
				Test->AddError(TEXT("CPU match timed out"));
				return true;
			}
			if (Step == 0)
			{
				FChaosImpactMatchRules Rules;
				Rules.PartyRule = Rule;
				Rules.bSpectate = true;
				Rules.CPUCount = 8;
				Rules.CPULevel = 2;
				Rules.Minutes = 3;
				Rules.StageIndex = StageIndex;
				GM->ConfigureVersusMatch(Rules);
				GM->ConfirmTeamsAndStart();
				Step = 1;
				return false;
			}
			if (Step == 1)
			{
				if (Match->Phase != EChaosImpactOnlinePhase::Match)
				{
					return false;
				}
				UGameplayStatics::SetGlobalTimeDilation(World, 4.0f);
				Step = 2;
				return false;
			}
			if (Step == 2)
			{
				const float Elapsed = Match->GetPhaseElapsedSeconds();
				if (Match->Phase == EChaosImpactOnlinePhase::Match && Elapsed >= NextLog)
				{
					NextLog += 20.0f;
					FString Line;
					for (const AChaosImpactPlayerState* Member : Match->GetRanking())
					{
						Line += FString::Printf(TEXT("%s=%d "), *Member->GetPlayerName(), Member->Points);
					}
					const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
					const AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
					UE_LOG(LogTemp, Display, TEXT("PARTYSIM rule%d stage%d t=%3.0f %s holder=%s lying=%d"), Rule, StageIndex + 1, Elapsed, *Line,
						Crown ? *GetNameSafe(Crown->GetHolderState()) : TEXT("-"), Field ? Field->CountLying() : -1);
				}
				if (!bShot && Elapsed > 30.0f)
				{
					bShot = true;
					PTShot(FString::Printf(TEXT("Sim_Rule%d_Stage%d.png"), Rule, StageIndex + 1));
				}
				if (Match->Phase == EChaosImpactOnlinePhase::Results)
				{
					UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
					const TArray<AChaosImpactPlayerState*> Ranking = Match->GetRanking();
					UE_LOG(LogTemp, Display, TEXT("PARTYSIM rule%d stage%d END winner=%s points=%d time=%.0f"), Rule, StageIndex + 1,
						Ranking.IsEmpty() ? TEXT("-") : *Ranking[0]->GetPlayerName(), Ranking.IsEmpty() ? 0 : Ranking[0]->Points, Match->GetPhaseElapsedSeconds());
					Test->TestTrue(TEXT("Somebody scored"), !Ranking.IsEmpty() && Ranking[0]->Points > 0);
					return true;
				}
			}
			return false;
		}

	private:
		FAutomationTestBase* Test;
		uint8 Rule = 1;
		int32 StageIndex = 0;
		int32 Step = 0;
		float NextLog = 0.0f;
		bool bShot = false;
		double Started = FPlatformTime::Seconds();
	};

	/**
	 * A guest's own view of a host's match (the host started with -CIAutoVersus=0:<cpus>:<delay>:ruby or :crown): the rule
	 * reached it, its own pickups show at once and the host counts them, the crown it takes slows it down.
	 */
	class FPartyOnlineCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyOnlineCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 480.0)
			{
				Test->AddError(FString::Printf(TEXT("Timed out at step %d"), Step));
				return true;
			}
			UWorld* World = PTWorld();
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (!Match || World->GetNetMode() != NM_Client || Now < WaitUntil)
			{
				return false;
			}
			AChaosImpactCharacter* Own = PTOwnCharacter(World);
			const AChaosImpactPlayerState* Member = Own ? Own->GetPlayerState<AChaosImpactPlayerState>() : nullptr;
			if (Step == 0)
			{
				if (!Match->bVersusMatch || Match->Phase != EChaosImpactOnlinePhase::Match || !Own || !Member || Match->GetPhaseElapsedSeconds() < 2.0f)
				{
					return false;
				}
				Test->TestTrue(TEXT("A party rule reached the guest"), Match->Rules.IsParty());
				bRuby = Match->Rules.IsRubyRush();
				UE_LOG(LogTemp, Display, TEXT("PARTYONLINE rule=%s"), *ChaosImpactMatch::DescribeRule(Match->Rules));
				PTShot(bRuby ? TEXT("Online_Ruby_Start.png") : TEXT("Online_Crown_Start.png"));
				if (bRuby)
				{
					const AChaosImpactRubyField* Field = AChaosImpactRubyField::Get(World);
					Test->TestNotNull(TEXT("The rubies reached the guest"), Field);
					if (!Field)
					{
						return true;
					}
					Test->TestTrue(FString::Printf(TEXT("Many rubies here (%d)"), Field->GetGems().Num()), Field->GetGems().Num() >= 30);
					Before = Member->Points;
					if (const FChaosImpactRubyGem* Gem = PTNearestGem(Field, Own->GetActorLocation(), false))
					{
						PTPlace(Own, Gem->To);
					}
					WaitUntil = Now + 0.15;
					Step = 1;
					return false;
				}
				const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
				Test->TestNotNull(TEXT("The crown reached the guest"), Crown);
				if (!Crown)
				{
					return true;
				}
				BaseSpeed = Own->GetCharacterMovement()->MaxWalkSpeed;
				PTPlace(Own, Crown->GetState().To);
				WaitUntil = Now + 0.12;
				Step = 10;
				return false;
			}
			if (Step == 1)
			{
				// On this screen at once (before the host has answered).
				Test->TestTrue(FString::Printf(TEXT("The pickup shows at once (%d -> %d)"), Before, ChaosImpactParty::GetShownCount(World, Member)),
					ChaosImpactParty::GetShownCount(World, Member) > Before);
				PTShot(TEXT("Online_Ruby_Pickup.png"));
				WaitUntil = Now + 1.5;
				Step = 2;
				return false;
			}
			if (Step == 2)
			{
				Test->TestTrue(FString::Printf(TEXT("The host counted it (%d)"), Member->Points), Member->Points > Before);
				Test->TestEqual(TEXT("Nothing left waiting"), ChaosImpactParty::GetShownCount(World, Member), Member->Points);
				UE_LOG(LogTemp, Display, TEXT("PARTYONLINE ruby ok: %d -> %d"), Before, Member->Points);
				return true;
			}
			const AChaosImpactCrown* Crown = AChaosImpactCrown::Get(World);
			if (Step == 10)
			{
				Test->TestTrue(TEXT("Worn on this screen at once"), Crown && Crown->GetHolder() == Own);
				WaitUntil = Now + 1.2;
				Step = 11;
				return false;
			}
			if (Step == 11)
			{
				Test->TestTrue(TEXT("The host gave it to the guest"), Crown && Crown->GetState().HolderState == Member);
				Test->TestTrue(FString::Printf(TEXT("Slower on the guest's own machine (%.0f -> %.0f)"), BaseSpeed, Own->GetCharacterMovement()->MaxWalkSpeed),
					Own->GetCharacterMovement()->MaxWalkSpeed < BaseSpeed * 0.9f);
				PTShot(TEXT("Online_Crown_Taken.png"));
				WaitUntil = Now + 2.6;
				Step = 12;
				return false;
			}
			if (Step == 12)
			{
				Test->TestTrue(FString::Printf(TEXT("Counts reach the guest (%d)"), Member->Points), Member->Points >= 2 || Crown->GetState().HolderState != Member);
				UE_LOG(LogTemp, Display, TEXT("PARTYONLINE crown ok: counts %d"), Member->Points);
				PTShot(TEXT("Online_Crown_Counting.png"));
				return true;
			}
			return true;
		}

	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		int32 Before = 0;
		bool bRuby = true;
		float BaseSpeed = 0.0f;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
	};
}

namespace
{
	/** A match opened the way the game opens it (its level with the rules in the URL): pictures along the way. */
	class FPartyWatchCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyWatchCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = PTWorld();
			const AChaosImpactGameState* Match = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			if (FPlatformTime::Seconds() - Started > 260.0)
			{
				Test->AddError(TEXT("No match"));
				return true;
			}
			if (!Match || !Match->bVersusMatch)
			{
				return false;
			}
			PTUnpause(World);
			if (Match->Phase != EChaosImpactOnlinePhase::Match)
			{
				return Match->Phase == EChaosImpactOnlinePhase::Results;
			}
			const float Elapsed = Match->GetPhaseElapsedSeconds();
			static const float Times[] = {3.0f, 20.0f, 45.0f, 90.0f, 150.0f};
			if (Shot < UE_ARRAY_COUNT(Times) && Elapsed >= Times[Shot])
			{
				PTShot(FString::Printf(TEXT("Watch_%s_%d.png"), Match->Rules.IsCrownKeep() ? TEXT("Crown") : TEXT("Ruby"), FMath::RoundToInt(Times[Shot])));
				++Shot;
			}
			return Shot >= UE_ARRAY_COUNT(Times);
		}
	private:
		FAutomationTestBase* Test;
		int32 Shot = 0;
		double Started = FPlatformTime::Seconds();
	};
}

namespace
{
	/**
	 * The screens: the rules screen's list with the パーティ row and each party rule's card, then a 合言葉 room's lobby with
	 * the rules decided (its card with the ルール確認 key) and the full rules opened over it, for several kinds of match.
	 */
	class FPartyScreensCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyScreensCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = PTWorld();
			auto* PC = World ? Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Room = World ? World->GetGameState<AChaosImpactGameState>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (!PC || !Room || Now - Started > 200.0)
			{
				if (Now - Started > 200.0) { Test->AddError(TEXT("Timed out")); return true; }
				return false;
			}
			if (Now < NextAt)
			{
				return false;
			}
			UChaosImpactMenuWidget* Menu = PC->GetMenuWidget();
			if (!Pending.IsEmpty())
			{
				// The full rules faded in: shot now, the next kind on the next pass.
				PTShot(Pending);
				Pending.Reset();
				NextAt = Now + 0.4;
				return false;
			}
			switch (Step)
			{
			case 0:
				PC->OpenMatchRules(EChaosImpactScreen::Playing);
				if (Menu) { Menu->OpenModePickerForTests(true); }
				NextAt = Now + 1.2;
				break;
			case 1:
				PTShot(TEXT("Screen_ModePicker.png"));
				NextAt = Now + 0.5;
				break;
			case 2:
				if (Menu) { Menu->OpenModePickerForTests(false); }
				PC->SetMatchModeChoice(11);
				Test->TestTrue(TEXT("ルビーラッシュ chosen from the list"), PC->GetPendingMatchRules().IsRubyRush());
				PC->ShowMenuScreen(EChaosImpactScreen::MatchRules);
				NextAt = Now + 1.2;
				break;
			case 3:
				PTShot(TEXT("Screen_RulesRuby.png"));
				NextAt = Now + 0.5;
				break;
			case 4:
				PC->SetMatchModeChoice(12);
				PC->AdjustMatchRule(0, 1);
				Test->TestTrue(TEXT("クラウンキープ: 3 or 5 minutes"), PC->GetPendingMatchRules().Minutes == 3 || PC->GetPendingMatchRules().Minutes == 5);
				PC->AdjustMatchRule(0, 1);
				PC->ShowMenuScreen(EChaosImpactScreen::MatchRules);
				NextAt = Now + 1.2;
				break;
			case 5:
				PTShot(TEXT("Screen_RulesCrown.png"));
				NextAt = Now + 0.5;
				break;
			case 6:
				PC->CancelMatchRules();
				NextAt = Now + 0.8;
				break;
			default:
			{
				// The lobby, with each kind of rules decided in turn: the card, then the full rules (a step each, the shot
				// on the frame after).
				const int32 Kind = (Step - 7) / 3;
				const int32 Part = (Step - 7) % 3;
				static const int32 Modes[] = {11, 12, 4, 5, 0};
				if (Kind >= UE_ARRAY_COUNT(Modes))
				{
					PC->SetRuleCardOpen(false);
					return true;
				}
				if (Part == 0)
				{
					FChaosImpactMatchRules Rules;
					AChaosImpactPlayerController::ApplyMatchMode(Rules, Modes[Kind]);
					Rules.CPUCount = 3;
					Rules.Minutes = Modes[Kind] == 12 ? 5 : 3;
					Rules.StageIndex = Kind % 6;
					Rules.OffBalls = Kind == 3 ? 0x1C : 0;
					Room->bOnlineRoom = true;
					Room->bRulesDecided = true;
					Room->Phase = EChaosImpactOnlinePhase::Lobby;
					Room->ReadyDeadline = Room->GetServerWorldTimeSeconds() + 40.0;
					Room->Rules = ChaosImpactMatch::Sanitize(Rules, 1);
					PC->SetRuleCardOpen(false);
					Test->TestTrue(TEXT("ルール確認 can be opened while the room waits"), PC->CanOpenRuleCard());
				}
				else if (Part == 1)
				{
					if (Kind == 0) { PTShot(TEXT("Screen_LobbyCard.png")); }
					NextAt = Now + 0.3;
					break;
				}
				else
				{
					PC->SetRuleCardOpen(true);
					Test->TestTrue(TEXT("Open"), PC->IsRuleCardOpen());
				}
				if (Part == 2)
				{
					Pending = FString::Printf(TEXT("Screen_RuleCard%d.png"), Kind);
				}
				NextAt = Now + 0.8;
				break;
			}
			}
			if (!Pending.IsEmpty() && Step % 3 == 0 && Step > 4)
			{
				// (Unused: the shot is taken at the start of the next pass.)
			}
			++Step;
			return false;
		}
	private:
		FAutomationTestBase* Test;
		int32 Step = 0;
		FString Pending;
		double NextAt = 0.0;
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyScreensTest, "ChaosImpact.Party.Screens", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyScreensTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyScreensCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyWatchTest, "ChaosImpact.Party.Watch", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyWatchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyWatchCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyRubyMatchTest, "ChaosImpact.Party.RubyMatch", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyRubyMatchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyRubyMatchCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyCrownMatchTest, "ChaosImpact.Party.CrownMatch", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyCrownMatchTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyCrownMatchCommand(this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyRubyCPUTest, "ChaosImpact.Party.RubyCPU", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyRubyCPUTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyCPUMatchCommand(this, ChaosImpactMatch::PartyRuby));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyCrownCPUTest, "ChaosImpact.Party.CrownCPU", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyCrownCPUTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyCPUMatchCommand(this, ChaosImpactMatch::PartyCrown));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyOnlineTest, "ChaosImpact.Party.Online", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyOnlineTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyOnlineCommand(this));
	return true;
}
#endif
