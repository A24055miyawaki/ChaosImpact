#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGAbilityGates.h"
#include "RPGAdventure.h"
#include "RPGOnline.h"
#include "RPGTutorial.h"
#include "RPGTuning.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactTackleComponent.h"
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
UWorld* GameWorld()
{
	for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { return C.World(); } }
	return nullptr;
}
void QAShot(const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); }
void Teleport(ARPGPrototypeCharacter* P, const FVector& To) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics); }
FVector Ground(float X, float Y, float Up = 140) { return FVector(X, Y, RPGWorld::Height(X, Y) + Up); }
int32 MenuIndex(RPGTutorial::EMenu Entry)
{
	const auto& Entries = RPGTutorial::MenuEntries();
	for (int32 I = 0; I < Entries.Num(); ++I) { if (Entries[I].Key == Entry) { return I; } }
	return INDEX_NONE;
}
int32 LessonIndex(FName Id)
{
	const auto& List = RPGTutorial::Lessons();
	for (int32 I = 0; I < List.Num(); ++I) { if (List[I].Id == Id) { return I; } }
	return INDEX_NONE;
}

/**
 * The controls tutorial done step by step by doing each thing; a lesson card done by trying it; the adventure menu,
 * its log and the tutorial list; a lesson read again from the list; skipping (run with -CITutorials).
 */
class FRPGTutorialCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGTutorialCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 180) { Test->AddError(FString::Printf(TEXT("Tutorial test timeout at stage %d (step %d)"), Stage, Doing)); return true; }
		UWorld* W = GameWorld();
		if (!W) { return false; }
		if (Stage == 0)
		{
			UGameplayStatics::OpenLevel(W, TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"), true, TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
			Stage = 1; return false;
		}
		auto* P = Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W, 0));
		auto* PC = P ? Cast<AChaosImpactPlayerController>(P->GetController()) : nullptr;
		if (!P || !PC || !PC->IsGameplayActive() || !P->GetPrototypeHUD()) { return false; }
		const double Now = W->GetTimeSeconds();
		P->RestoreHP(100000);
		switch (Stage)
		{
		case 1:
			if (Now < 2) { return false; }
			P->TutorialsDone.Reset(); P->LessonQueue.Reset(); P->DismissLesson();
			P->StartControlsTutorial();
			Test->TestEqual(TEXT("The controls start at their first step"), P->GetControlsStep(), 0);
			Rubies = P->GetRubies();
			From = Now; Stage = 2; return false;
		case 2:
			if (Now - From < 1.0) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("tutorial-controls.png"));
			From = StepFrom = Now; Stage = 3; Doing = 0; Acted = 0; return false;
		case 3:
		{
			// Each step: do the thing it asks until it moves on.
			const int32 Step = P->GetControlsStep();
			if (Step != Doing)
			{
				UE_LOG(LogTemp, Display, TEXT("RPGTUTORIAL step %d done in %.1fs"), Doing, Now - StepFrom);
				P->SetMapMode(0); if (P->IsBagOpen()) { P->SetBagOpen(false); } if (P->IsQuestMenuOpen()) { P->SetQuestMenuOpen(false); }
				if (Doing == 3) { P->EndThrowInput(); }
				if (Step < 0) { Stage = 4; From = Now; return false; }
				Doing = Step; StepFrom = Now; Acted = 0;
				return false;
			}
			if (Now - StepFrom > 10) { Test->AddError(FString::Printf(TEXT("Controls step %d (%s) did not finish by doing it"), Doing, RPGTutorial::ControlsSteps()[Doing].Title)); return true; }
			const double Since = Now - StepFrom;
			if (Now >= NextLogAt)
			{
				NextLogAt = Now + 1;
				UE_LOG(LogTemp, Display, TEXT("RPGTUTORIAL step %d walked %.0f done %.1f at %s"), Doing, P->StepWalked, P->LessonDoneAt, *P->GetActorLocation().ToString());
			}
			switch (Doing)
			{
			case 0: P->SetActorLocation(P->GetActorLocation() + FVector(-25, -8, 0)); break;
			case 1: if (Since > Acted * 0.8) { P->Jump(); ++Acted; } break;
			case 2: if (Since > Acted * 1.0) { P->RequestAIDash(P->GetActorForwardVector()); ++Acted; } break;
			case 3:
				if (Acted % 2 == 0 && Since > Acted * 0.5) { P->BeginThrowInput(); ++Acted; }
				else if (Acted % 2 == 1 && Since > Acted * 0.5) { P->EndThrowInput(); ++Acted; }
				break;
			case 4: if (Since > Acted * 1.0 && P->GetTackle()) { P->GetTackle()->Request(); ++Acted; } break;
			case 5: if (!Acted) { P->OpenQuestJournal(); ++Acted; } break;
			case 6: if (!Acted) { P->SetMapMode(1); ++Acted; } break;
			case 7: if (!Acted) { P->SetGuideOn(!P->IsGuideOn()); ++Acted; } break;
			case 8: if (!Acted) { P->SetBagOpen(true); ++Acted; } break;
			case 9:
				// At the colony's board (a conversation ends when the hero walks away from whoever it is with).
				if (Acted == 0)
				{
					for (TActorIterator<ARPGAdventureNode> It(W); It; ++It) { if (It->Kind == ERPGAdventureNodeKind::Board) { Teleport(P, It->GetActorLocation() + It->GetActorForwardVector() * 220 + FVector(0, 0, 100)); break; } }
					++Acted;
				}
				else if (Acted == 1 && Since > 0.5) { P->TalkToGiver(0); ++Acted; }
				break;
			default: break;
			}
			return false;
		}
		case 4:
			Test->TestTrue(TEXT("The controls are marked done"), P->TutorialsDone.Contains(RPGTutorial::ControlsId));
			Test->TestTrue(TEXT("Finishing them gives rubies"), P->GetRubies() >= Rubies + 100);
			// A lesson, done by trying it.
			P->LessonQueue.Reset(); while (P->GetActiveLesson() != NAME_None) { P->DismissLesson(); }
			P->ShowLesson(TEXT("Workshop"), true);
			Test->TestEqual(TEXT("A lesson card shows"), P->GetActiveLesson(), FName(TEXT("Workshop")));
			From = Now; Stage = 5; return false;
		case 5:
			if (Now - From < 0.8) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("tutorial-lesson.png"));
			From = Now; Stage = 6; return false;
		case 6:
			if (Now - From < 0.4) { return false; }
			P->TutorialEvent(TEXT("Upgrade"));
			From = Now; Stage = 7; return false;
		case 7:
			if (P->GetActiveLesson() == TEXT("Workshop")) { if (Now - From > 4) { Test->AddError(TEXT("A lesson done does not close")); return true; } return false; }
			Test->TestTrue(TEXT("Trying it closes the lesson"), Now - From > 1.0);
			Test->TestTrue(TEXT("The lesson is marked seen"), P->TutorialsDone.Contains(TEXT("Workshop")));
			// The adventure menu.
			P->LessonQueue.Reset(); while (P->GetActiveLesson() != NAME_None) { P->DismissLesson(); }
			Test->TestTrue(TEXT("The adventure menu opens"), P->OpenAdventureMenu() && P->IsAdventureMenuOpen());
			From = Now; Stage = 8; return false;
		case 8:
			if (Now - From < 0.6) { return false; }
			QAShot(TEXT("adventure-menu.png"));
			From = Now; Stage = 9; return false;
		case 9:
			if (Now - From < 0.4) { return false; }
			P->ActivateMenuEntry(MenuIndex(RPGTutorial::EMenu::Log));
			Test->TestEqual(TEXT("The log opens from the menu"), P->QuestGiver, ARPGPrototypeCharacter::LogPanel);
			From = Now; Stage = 10; return false;
		case 10:
			if (Now - From < 0.6) { return false; }
			QAShot(TEXT("adventure-log.png"));
			From = Now; Stage = 11; return false;
		case 11:
			if (Now - From < 0.4) { return false; }
			P->HandleMenuKey(EKeys::Escape);
			Test->TestEqual(TEXT("Back goes to the menu"), P->QuestGiver, ARPGPrototypeCharacter::MenuPanel);
			P->ActivateMenuEntry(MenuIndex(RPGTutorial::EMenu::Tutorials));
			Test->TestEqual(TEXT("The tutorial list opens from the menu"), P->QuestGiver, ARPGPrototypeCharacter::TutorialPanel);
			P->TutorialSelection = LessonIndex(TEXT("Workshop"));
			From = Now; Stage = 12; return false;
		case 12:
			if (Now - From < 0.6) { return false; }
			QAShot(TEXT("tutorial-list.png"));
			From = Now; Stage = 13; return false;
		case 13:
			if (Now - From < 0.4) { return false; }
			// One not met yet stays hidden; one seen is read again.
			P->TutorialSelection = LessonIndex(TEXT("Events"));
			P->HandleMenuKey(EKeys::Enter);
			Test->TestTrue(TEXT("One not met yet cannot be opened"), P->IsQuestMenuOpen() && P->GetActiveLesson() == NAME_None);
			P->TutorialSelection = LessonIndex(TEXT("Workshop"));
			P->HandleMenuKey(EKeys::Enter);
			Test->TestFalse(TEXT("Choosing one closes the menu"), P->IsQuestMenuOpen());
			Test->TestEqual(TEXT("It shows again"), P->GetActiveLesson(), FName(TEXT("Workshop")));
			From = Now; Stage = 14; return false;
		case 14:
			if (Now - From < 0.7) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("tutorial-review.png"));
			From = Now; Stage = 15; return false;
		case 15:
			if (Now - From < 0.4) { return false; }
			// The controls again from the list, then skipped.
			P->DismissLesson();
			P->OpenAdventureMenu(); P->ActivateMenuEntry(MenuIndex(RPGTutorial::EMenu::Tutorials));
			P->TutorialSelection = LessonIndex(RPGTutorial::ControlsId);
			P->HandleMenuKey(EKeys::Enter);
			Test->TestEqual(TEXT("The controls start again from the list"), P->GetControlsStep(), 0);
			P->SkipControlsTutorial();
			Test->TestEqual(TEXT("They can be skipped"), P->GetControlsStep(), -1);
			return true;
		default: return true;
		}
	}
private:
	FAutomationTestBase* Test;
	int32 Stage = 0, Doing = 0, Acted = 0, Rubies = 0;
	double From = 0, StepFrom = 0, NextLogAt = 0, Started = FPlatformTime::Seconds();
};

/**
 * Growing through the world: the fog lifts where a hero goes and a new place is announced; the colony grows with
 * gifts and quests (its prices drop); the dojo teaches tackle and dash; a strong tackle breaks a boulder and a strong
 * dash leaps a gorge.
 */
class FRPGProgressionCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGProgressionCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 180) { Test->AddError(FString::Printf(TEXT("Progression test timeout at stage %d"), Stage)); return true; }
		UWorld* W = GameWorld();
		if (!W) { return false; }
		if (Stage == 0)
		{
			UGameplayStatics::OpenLevel(W, TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"), true, TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
			Stage = 1; return false;
		}
		auto* P = Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W, 0));
		auto* PC = P ? Cast<AChaosImpactPlayerController>(P->GetController()) : nullptr;
		if (!P || !PC || !PC->IsGameplayActive() || !P->GetPrototypeHUD()) { return false; }
		const double Now = W->GetTimeSeconds();
		P->RestoreHP(100000);
		const FVector2D Oasis(5600, 30200);
		const int32 CellX = FMath::FloorToInt((Oasis.X - RPGWorld::MinX) / 1000.0f), CellY = FMath::FloorToInt((Oasis.Y - RPGWorld::MinY) / 1000.0f);
		switch (Stage)
		{
		case 1:
			if (Now < 2) { return false; }
			// The fog: the oasis unseen, then seen.
			Test->TestFalse(TEXT("Far places start in the fog"), P->IsExplored(CellX, CellY));
			Teleport(P, Ground(Oasis.X - 600, Oasis.Y));
			From = Now; Stage = 2; return false;
		case 2:
			if (Now - From < 1.2) { return false; }
			Test->TestTrue(TEXT("The fog lifts where the hero goes"), P->IsExplored(CellX, CellY));
			Test->TestTrue(TEXT("The place is noted"), P->VisitedPlaces.Contains(TEXT("砂漠のオアシス")));
			Test->TestEqual(TEXT("A new place is announced"), P->PlaceCardName, FString(TEXT("砂漠のオアシス")));
			P->NoticeUntil = 0; QAShot(TEXT("discovery-card.png"));
			From = Now; Stage = 3; return false;
		case 3:
			if (Now - From < 0.4) { return false; }
			P->SetMapMode(2);
			From = Now; Stage = 4; return false;
		case 4:
			if (Now - From < 0.8) { return false; }
			QAShot(TEXT("discovery-map.png"));
			From = Now; Stage = 5; return false;
		case 5:
		{
			if (Now - From < 0.4) { return false; }
			P->SetMapMode(0);
			// The colony: a gift, then enough to grow it all the way.
			Teleport(P, Ground(150, -900));
			P->AddRubies(20000);
			const int32 Points = P->ColonyPoints;
			Test->TestTrue(TEXT("A gift to the colony"), P->DonateToColony());
			Test->TestTrue(TEXT("A gift counts for the colony"), P->ColonyPoints > Points);
			P->AddColonyPoints(RPGColony::StagePoints[RPGColony::MaxStage]);
			From = Now; Stage = 6; return false;
		}
		case 6:
			if (Now - From < 1.5) { return false; }
			Test->TestEqual(TEXT("The colony grows (the world shows it)"), RPGColony::WorldStage(W), RPGColony::MaxStage);
			Test->TestTrue(TEXT("A grown colony's workshop is cheaper"), RPGColony::Discount() >= 0.19f);
			P->NoticeUntil = 0;
			Teleport(P, Ground(150, -650));
			P->SetActorRotation(FRotator(0, 0, 0));
			From = Now; Stage = 7; return false;
		case 7:
			if (Now - From < 1.0) { return false; }
			QAShot(TEXT("colony-grown.png"));
			From = Now; Stage = 8; return false;
		case 8:
			if (Now - From < 0.4) { return false; }
			// The dojo: strong enough, rich enough, the bosses beaten.
			while (P->GetRPGLevel() < 15) { P->GainRPGExperience(P->GetRequiredXP()); }
			P->BossesBeaten |= 3;
			P->AddRubies(20000); P->AddTuneShards(60, TEXT("test"));
			Teleport(P, Ground(-250 + 220, -1050));
			Test->TestTrue(TEXT("The dojo opens"), P->OpenTraining() && P->IsTrainingOpen());
			From = Now; Stage = 9; return false;
		case 9:
			if (Now - From < 0.7) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("dojo.png"));
			From = Now; Stage = 10; return false;
		case 10:
		{
			if (Now - From < 0.4) { return false; }
			for (int32 I = 0; I < 2; ++I) { Test->TestTrue(TEXT("Tackle lesson learned"), P->LearnSkill(0)); Test->TestTrue(TEXT("Dash lesson learned"), P->LearnSkill(1)); }
			Test->TestEqual(TEXT("Tackle Lv3"), P->GetTackleLevel(), 3);
			Test->TestEqual(TEXT("Dash Lv3"), P->GetDashLevel(), 3);
			P->SetQuestMenuOpen(false);
			// A boulder: a strong tackle breaks it.
			for (TActorIterator<ARPGGateObstacle> It(W); It; ++It) { if (It->Kind == ARPGGateObstacle::Boulder && !It->IsOpen()) { Gate = *It; break; } }
			if (!Test->TestNotNull(TEXT("A boulder blocks a way"), Gate.Get())) { return true; }
			const FVector Front = Gate->GetActorLocation() - Gate->GetActorForwardVector() * 260;
			Teleport(P, FVector(Front.X, Front.Y, RPGWorld::Height(Front.X, Front.Y) + 140));
			P->SetActorRotation(Gate->GetActorForwardVector().Rotation());
			From = Now; Stage = 11; return false;
		}
		case 11:
			if (Now - From < 0.8) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("gate-boulder.png"));
			From = Now; Stage = 12; return false;
		case 12:
			if (Now - From < 0.4) { return false; }
			if (P->GetTackle()) { P->GetTackle()->Request(); }
			From = Now; Stage = 13; return false;
		case 13:
		{
			if (Gate.IsValid() && !Gate->IsOpen()) { if (Now - From > 3) { Test->AddError(TEXT("A strong tackle does not break the boulder")); return true; } return false; }
			// A leap across a gorge.
			for (TActorIterator<ARPGLeapPoint> It(W); It; ++It) { Leap = *It; break; }
			if (!Test->TestNotNull(TEXT("A leap point by a gorge"), Leap.Get())) { return true; }
			Teleport(P, Leap->GetActorLocation() + FVector(0, 0, 110));
			P->SetActorRotation((Leap->Partner - Leap->GetActorLocation()).GetSafeNormal2D().Rotation());
			From = Now; Stage = 14; return false;
		}
		case 14:
			if (Now - From < 0.8) { return false; }
			UE_LOG(LogTemp, Display, TEXT("RPGPROGRESS leap at %s to %s, hero %s, under %d, dash %d"), *Leap->GetActorLocation().ToString(), *Leap->Partner.ToString(),
				*P->GetActorLocation().ToString(), ARPGLeapPoint::Under(P) ? 1 : 0, P->GetDashLevel());
			P->RequestAIDash((Leap->Partner - Leap->GetActorLocation()).GetSafeNormal2D());
			UE_LOG(LogTemp, Display, TEXT("RPGPROGRESS dashing %d"), P->IsDashing() ? 1 : 0);
			From = Now; Stage = 15; return false;
		case 15:
			if (Now - From < 0.45) { return false; }
			P->NoticeUntil = 0; QAShot(TEXT("gate-leap.png"));
			From = Now; Stage = 16; return false;
		case 16:
			if (Now >= NextLogAt) { NextLogAt = Now + 0.25; UE_LOG(LogTemp, Display, TEXT("RPGPROGRESS flying %s"), *P->GetActorLocation().ToString()); }
			if (Now - From < 2.0) { return false; }
			UE_LOG(LogTemp, Display, TEXT("RPGPROGRESS leap landed %.0f from its far pad"), FVector::Dist2D(P->GetActorLocation(), Leap->Partner));
			Test->TestTrue(TEXT("A strong dash leaps the gorge"), FVector::Dist2D(P->GetActorLocation(), Leap->Partner) < 500);
			return true;
		default: return true;
		}
	}
private:
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double From = 0, Started = FPlatformTime::Seconds();
	double NextLogAt = 0;
	TWeakObjectPtr<ARPGGateObstacle> Gate;
	TWeakObjectPtr<ARPGLeapPoint> Leap;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTutorialTest, "ChaosImpact.RPGPrototype.Tutorials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGTutorialTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGTutorialCommand(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGProgressionTest, "ChaosImpact.RPGPrototype.Progression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGProgressionTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGProgressionCommand(this)); return true; }
#endif
