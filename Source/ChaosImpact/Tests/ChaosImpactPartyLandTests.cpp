#if WITH_DEV_AUTOMATION_TESTS
// Rubies and the crown, knocked loose anywhere on any stage (even from up on a ledge), only ever land on floor a player
// can get to: never up on a wall's top or a ledge higher than a jump.
#include "Misc/AutomationTest.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactMatchTypes.h"
#include "ChaosImpactParty.h"
#include "ChaosImpactStageBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	UWorld* PLWorld()
	{
		UWorld* Found = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World() && Context.World()->IsGameWorld()) { Found = Context.World(); }
		}
		return Found;
	}

	/** Up on something: every way round it, the floor a few steps off is lower than a jump (or there is none). */
	bool PLIsLedgeTop(const UWorld* World, const FVector& Land)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const FVector Way = FVector::ForwardVector.RotateAngleAxis(Index * 45.0f, FVector::UpVector);
			for (const float Reach : {160.0f, 300.0f})
			{
				FVector Floor;
				if (ChaosImpactParty::FindFloor(World, Land + Way * Reach, 40.0f, 900.0f, Floor)
					&& Floor.Z > Land.Z - ChaosImpactParty::ClimbableRise - 5.0f)
				{
					return false;
				}
			}
		}
		return true;
	}

	class FPartyLandCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPartyLandCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = PLWorld();
			AChaosImpactGameMode* GM = World ? World->GetAuthGameMode<AChaosImpactGameMode>() : nullptr;
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 240.0) { Test->AddError(TEXT("Party landing test stalled")); return true; }
			if (!GM || Now < WaitUntil) { return false; }
			if (bStarted)
			{
				Check(World, GM);
				GM->EndMatchForTests();
				bStarted = false;
				++StageIndex;
				WaitUntil = Now + 1.0;
				return StageIndex >= ChaosImpactMatch::StageCount;
			}
			FChaosImpactMatchRules Rules;
			Rules.PartyRule = StageIndex % 2 == 0 ? ChaosImpactMatch::PartyRuby : ChaosImpactMatch::PartyCrown;
			Rules.CPUCount = 1;
			Rules.CPULevel = 1;
			Rules.StageIndex = StageIndex;
			GM->ConfigureVersusMatch(Rules);
			GM->ConfirmTeamsAndStart();
			bStarted = true;
			WaitUntil = Now + 1.5;
			return false;
		}

	private:
		void Check(UWorld* World, AChaosImpactGameMode* GM)
		{
			const TArray<FVector>& Points = GM->GetPartyFieldPoints();
			const AChaosImpactStageBase* Stage = GM->GetVersusStage();
			Test->TestTrue(FString::Printf(TEXT("Stage %d: a field to land on (%d spots)"), StageIndex, Points.Num()), Points.Num() >= 30);
			if (Points.IsEmpty() || !Stage)
			{
				return;
			}
			int32 LedgePoints = 0;
			for (const FVector& Point : Points)
			{
				if (PLIsLedgeTop(World, Point))
				{
					++LedgePoints;
					FString Around;
					for (int32 Way = 0; Way < 8; ++Way)
					{
						for (const float Reach : {80.0f, 160.0f, 300.0f})
						{
							FHitResult Hit;
							const FVector At = Point + FVector::ForwardVector.RotateAngleAxis(Way * 45.0f, FVector::UpVector) * Reach;
							const bool bHit = World->LineTraceSingleByObjectType(Hit, At + FVector(0, 0, 40), At - FVector(0, 0, 900), FCollisionObjectQueryParams(ECC_WorldStatic));
							Around += bHit ? FString::Printf(TEXT(" %.0f/%.2f/%s"), Hit.ImpactPoint.Z - Point.Z, Hit.ImpactNormal.Z, *GetNameSafe(Hit.GetComponent())) : FString(TEXT(" none"));
						}
						Around += TEXT(" |");
					}
					FHitResult Under;
					World->LineTraceSingleByObjectType(Under, Point + FVector(0, 0, 40), Point - FVector(0, 0, 100), FCollisionObjectQueryParams(ECC_WorldStatic));
					UE_LOG(LogTemp, Display, TEXT("PARTYLAND stage %d ledge spot %s on %s:%s"), StageIndex, *Point.ToCompactString(), *GetNameSafe(Under.GetComponent()), *Around);
				}
			}
			// From anywhere over the stage, high and low (up on its walls and boxes too): where things land.
			FRandomStream Stream(1234 + StageIndex);
			const FVector Centre = Stage->GetCenter();
			const float Half = Stage->GetHalfExtent();
			int32 Ledges = 0, OffField = 0, Tries = 0;
			for (int32 Index = 0; Index < 300; ++Index)
			{
				const FVector Origin = Index % 2 == 0
					? Points[Stream.RandRange(0, Points.Num() - 1)] + FVector(0, 0, Stream.FRandRange(0.0f, 300.0f))
					: Centre + FVector(Stream.FRandRange(-Half, Half), Stream.FRandRange(-Half, Half), Stream.FRandRange(100.0f, 700.0f));
				const FVector Land = ChaosImpactParty::FindLandSpot(World, Stage, Points, Origin, 170.0f, 560.0f, Stream);
				++Tries;
				Ledges += PLIsLedgeTop(World, Land) ? 1 : 0;
				OffField += ChaosImpactParty::IsOnField(Points, Land) ? 0 : 1;
			}
			UE_LOG(LogTemp, Display, TEXT("PARTYLAND stage %d: %d spots (%d up on ledges); %d landings, %d on ledges, %d off the field"),
				StageIndex, Points.Num(), LedgePoints, Tries, Ledges, OffField);
			Test->TestEqual(FString::Printf(TEXT("Stage %d: no field spot up on a ledge"), StageIndex), LedgePoints, 0);
			Test->TestEqual(FString::Printf(TEXT("Stage %d: nothing lands up on a ledge"), StageIndex), Ledges, 0);
			Test->TestEqual(FString::Printf(TEXT("Stage %d: everything lands on the walkable field"), StageIndex), OffField, 0);
		}

		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		int32 StageIndex = 0;
		bool bStarted = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyLandTest, "ChaosImpact.Party.LandSpots", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPartyLandTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPartyLandCommand(this));
	return true;
}
#endif
