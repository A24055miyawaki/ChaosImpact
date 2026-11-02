#if WITH_DEV_AUTOMATION_TESTS
#include "RPGNavigation.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "RPGWorldLayout.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
/**
 * The way finder, the light line and the maps (pictures in Saved/RPGPrototypeQA): every place on the map can be reached
 * by road from the colony, a quest's line leads to its nearest goal, N turns the line off and on, M opens the whole map.
 */
class FRPGMapGuideCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGMapGuideCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150) { Test->AddError(FString::Printf(TEXT("Map guide test timeout %d"), Stage)); return true; }
		UWorld* W = nullptr;
		for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { W = C.World(); break; } }
		if (!W) { return false; }
		if (Stage == 0)
		{
			UGameplayStatics::OpenLevel(W, TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"), true, TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
			Stage = 1; return false;
		}
		auto* P = Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W, 0));
		auto* PC = P ? Cast<AChaosImpactPlayerController>(P->GetController()) : nullptr;
		if (!P || !PC || !ARPGAdventureWorld::Find(W) || !P->GetPrototypeHUD() || !PC->IsGameplayActive()) { return false; }
		const double Now = W->GetTimeSeconds();
		if (Now < Next) { return false; }
		const auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); };
		const FVector Feet = P->GetActorLocation() - FVector(0, 0, P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		switch (Stage)
		{
		case 1:
		{
			for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It) { if (It->IsGrunt()) { It->SetTrainingMenuFrozen(true); } }
			bWasGuideOn = P->IsGuideOn();
			// Every place on the map by road from the colony (the snowfield too).
			int32 Missing = 0;
			for (const RPGWorld::FArea& Area : RPGWorld::Areas())
			{
				const FVector Goal = RPGWorld::AreaPoint(Area.Id);
				const TArray<FVector> Way = RPGNav::ShortestWay(W, Feet, {Goal});
				const float Straight = FVector::Dist2D(Feet, Goal), Length = RPGNav::WayLength(Way);
				UE_LOG(LogTemp, Display, TEXT("RPGNAV %s points=%d length=%.0f straight=%.0f"), *Area.Id.ToString(), Way.Num(), Length, Straight);
				if (Way.Num() < 2 || Length > Straight * 3.5f + 3000) { ++Missing; Test->AddError(FString::Printf(TEXT("No sensible way to %s (%d points, %.0f)"), *Area.Id.ToString(), Way.Num(), Length)); }
			}
			Test->TestEqual(TEXT("Every area can be reached by road"), Missing, 0);
			if (const ARPGSnowField* Field = ARPGSnowField::Find(W))
			{
				const TArray<FVector> Way = RPGNav::ShortestWay(W, Feet, {Field->NestCenter()});
				UE_LOG(LogTemp, Display, TEXT("RPGNAV SnowNest points=%d length=%.0f"), Way.Num(), RPGNav::WayLength(Way));
				Test->TestTrue(TEXT("The Shima-enaga's nest can be reached"), Way.Num() >= 2);
			}
			// The supply-box quest: the line leads to the nearest box that is still to be checked.
			Test->TestTrue(TEXT("The board's supply quest can be taken"), P->AcceptQuest(1));
			P->TrackedQuest = 1;
			P->SetGuideOn(true);
			Stage = 2; Next = Now + 1.2; return false;
		}
		case 2:
		{
			FVector Goal; FString Label;
			Test->TestTrue(TEXT("The followed quest has a goal"), P->GetQuestTarget(Goal, &Label));
			Test->TestTrue(TEXT("The way starts here and ends at the goal"), P->GetQuestRoute().Num() >= 2 && FVector::Dist2D(P->GetQuestRoute().Last(), Goal) < 1);
			// The nearest box by road (not just in a straight line).
			RPGNav::FGoal Goals; RPGNav::QuestGoals(P, 1, Goals);
			float Best = MAX_flt;
			for (const FVector& Box : Goals.Candidates) { Best = FMath::Min(Best, RPGNav::WayLength(RPGNav::ShortestWay(W, Feet, {Box}))); }
			Test->TestTrue(TEXT("The line leads to the box nearest by road"), RPGNav::WayLength(P->GetQuestRoute()) <= Best * 1.16f + 50);
			UE_LOG(LogTemp, Display, TEXT("RPGNAV guide %s points=%d length=%.0f nearest=%.0f"), *Label, P->GetQuestRoute().Num(), RPGNav::WayLength(P->GetQuestRoute()), Best);
			Test->TestTrue(TEXT("The light line is laid"), P->QuestGuide.IsValid() && !P->QuestGuide->IsHidden());
			P->NoticeUntil = 0;
			Shot(TEXT("guide-colony.png"));
			Stage = 3; Next = Now + 0.4; return false;
		}
		case 3:
		{
			// Partway along: the line goes on from there.
			const TArray<FVector> Way = P->GetQuestRoute();
			float Walked = 0;
			FVector Ahead = Way.Last();
			for (int32 Index = 1; Index < Way.Num(); ++Index)
			{
				Walked += FVector::Dist2D(Way[Index - 1], Way[Index]);
				if (Walked > 2600) { Ahead = Way[Index]; break; }
			}
			P->SetActorLocation(Ahead + FVector(0, 0, 140), false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 4; Next = Now + 1.4; return false;
		}
		case 4:
			Test->TestTrue(TEXT("The way is laid again from the new place"), P->GetQuestRoute().Num() >= 2 && FVector::Dist2D(P->GetQuestRoute()[0], P->GetActorLocation()) < 200);
			P->NoticeUntil = 0;
			Shot(TEXT("guide-road.png"));
			Stage = 5; Next = Now + 0.4; return false;
		case 5:
			P->CycleMap();
			Test->TestEqual(TEXT("M first shows the surroundings"), P->GetMapMode(), static_cast<int32>(ARPGPrototypeCharacter::MapNearby));
			Stage = 51; Next = Now + 0.6; return false;
		case 51:
			Shot(TEXT("map-nearby.png"));
			Stage = 52; Next = Now + 0.3; return false;
		case 52:
			Test->TestFalse(TEXT("The surroundings leave A to the game"), P->HandleMapKey(EKeys::Gamepad_FaceButton_Bottom));
			Test->TestTrue(TEXT("M again opens the whole map"), P->HandleMapKey(EKeys::M) && P->IsMapOpen());
			Stage = 6; Next = Now + 0.6; return false;
		case 6:
			Shot(TEXT("map-full.png"));
			Stage = 7; Next = Now + 0.4; return false;
		case 7:
			Test->TestTrue(TEXT("N on the map turns the line off"), P->HandleMapKey(EKeys::N) && !P->IsGuideOn());
			Test->TestTrue(TEXT("No light line while it is off"), !P->QuestGuide.IsValid() || P->QuestGuide->IsHidden());
			Test->TestTrue(TEXT("M a third time closes the map"), P->HandleMapKey(EKeys::M) && P->GetMapMode() == ARPGPrototypeCharacter::MapClosed);
			P->SetGuideOn(true);
			Test->TestTrue(TEXT("Turning it on lays the line at once"), P->QuestGuide.IsValid() && !P->QuestGuide->IsHidden());
			P->NoticeUntil = 0;
			Stage = 8; Next = Now + 0.5; return false;
		case 8:
			Shot(TEXT("guide-minimap.png"));
			Stage = 9; Next = Now + 0.4; return false;
		default:
			P->SetGuideOn(bWasGuideOn);
			return true;
		}
	}
private:
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double Next = 0;
	double Started = FPlatformTime::Seconds();
	bool bWasGuideOn = true;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMapGuideTest, "ChaosImpact.RPGPrototype.MapGuide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGMapGuideTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGMapGuideCommand(this)); return true; }
#endif
