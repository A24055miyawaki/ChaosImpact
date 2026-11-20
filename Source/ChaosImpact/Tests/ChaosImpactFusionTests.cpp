#if WITH_DEV_AUTOMATION_TESTS
// 合体ボール (prototype, 2026-11-16): in training, two special balls join with the 合体 button (the one that flies goes in
// front), part again when it is pressed again, and are thrown as one: a drive ball with a black hole riding along (it
// opens where the drive bursts), and a tornado with a fire riding along (a line of fire on its way). Pairs that do not
// join (フック＋アンカー, ノヴァ) are refused. Frames of the HUD's joining / parting / throw are saved to Saved/FusionQA.
// Run on a training stage with one CPU: Lvl_VS_Stage1?CITraining=1?CITargets=0?CICPUCount=1
#include "Misc/AutomationTest.h"
#include "ChaosImpactBall.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFusion.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactTornado.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"

namespace
{
	class FFusionCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFusionCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); }
			}
			const double Now = FPlatformTime::Seconds();
			if (Now - Started > 120.0) { Test->AddError(FString::Printf(TEXT("Fusion test stalled at step %d"), Step)); return true; }
			if (!World) { return false; }
			APlayerController* PC = World->GetFirstPlayerController();
			AChaosImpactCharacter* Own = PC ? Cast<AChaosImpactCharacter>(PC->GetPawn()) : nullptr;
			AChaosImpactCharacter* CPU = nullptr;
			for (TActorIterator<AChaosImpactCharacter> It(World); It; ++It)
			{
				if (*It != Own && !It->IsPlayerControlled()) { CPU = *It; break; }
			}
			if (!Own) { return false; }
			// The CPU plays on (and tackles): the player is kept standing so what is in hand stays.
			if (Step > 0 && !Own->IsEliminated()) { Own->RefillHealth(); }
			// Frames asked for at set times after a mark (one screenshot a frame at most).
			if (!Shots.IsEmpty() && Now >= ShotMark + Shots[0].Key)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FusionQA"), Shots[0].Value + TEXT(".png")),
					true, false);
				Shots.RemoveAt(0);
				return false;
			}
			if (Now < WaitUntil || !Shots.IsEmpty()) { return false; }
			const auto Frames = [this, Now](const TCHAR* Name, std::initializer_list<float> Times)
			{
				ShotMark = Now;
				int32 Index = 0;
				for (const float Time : Times) { Shots.Add({Time, FString::Printf(TEXT("%s_%d"), Name, Index++)}); }
			};
			const auto Ahead = [Own]() { return Own->GetActorForwardVector().GetSafeNormal2D(); };
			switch (Step)
			{
			case 0:
				WaitUntil = Now + 3.0;
				Step = 1;
				return false;
			case 1:
			{
				Own->RefillHealth();
				if (CPU)
				{
					CPU->RefillHealth();
					CPU->SetActorLocation(Own->GetActorLocation() + Ahead() * 1100.0f + FVector(0.0f, 0.0f, 20.0f));
				}
				// (2026-11-19) Every two specials join; never a normal ball.
				Own->GiveBallsForTests(EChaosImpactBallType::Hook, EChaosImpactBallType::Anchor);
				Test->TestTrue(TEXT("フック＋アンカー joins (グラップル)"), Own->CanFuseBalls());
				Test->TestEqual(TEXT("グラップル: the anchor flies"), ChaosImpactFusion::PickPrimary(EChaosImpactBallType::Hook,
					EChaosImpactBallType::Anchor), EChaosImpactBallType::Anchor);
				Own->GiveBallsForTests(EChaosImpactBallType::Nova, EChaosImpactBallType::Fire);
				Test->TestTrue(TEXT("ノヴァ joins"), Own->CanFuseBalls());
				Test->TestEqual(TEXT("太陽ノヴァ: the nova flies"), ChaosImpactFusion::PickPrimary(EChaosImpactBallType::Fire,
					EChaosImpactBallType::Nova), EChaosImpactBallType::Nova);
				Test->TestEqual(TEXT("ノヴァ砲: the beam flies"), ChaosImpactFusion::PickPrimary(EChaosImpactBallType::Nova,
					EChaosImpactBallType::Beam), EChaosImpactBallType::Beam);
				Own->GiveBallsForTests(EChaosImpactBallType::Fire, EChaosImpactBallType::Fire);
				Test->TestTrue(TEXT("Two of the same join (強化ボール)"), Own->CanFuseBalls());
				Test->TestEqual(TEXT("強化ボール's label"), ChaosImpactFusion::GetFusionLabel(EChaosImpactBallType::Fire, EChaosImpactBallType::Fire),
					FString(TEXT("超ファイア")));
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Fire);
				Test->TestFalse(TEXT("A normal ball never joins"), Own->CanFuseBalls());
				Own->GiveBallsForTests(EChaosImpactBallType::Normal, EChaosImpactBallType::Normal);
				Test->TestFalse(TEXT("Two normal balls never join"), Own->CanFuseBalls());
				Test->TestEqual(TEXT("The plan's name"), ChaosImpactFusion::GetFusionName(EChaosImpactBallType::Black, EChaosImpactBallType::Drive),
					FString(TEXT("吸引ドライブ")));
				Test->TestEqual(TEXT("The screen's label: the one that flies × the one riding along"),
					ChaosImpactFusion::GetFusionLabel(EChaosImpactBallType::Drive, EChaosImpactBallType::Black), FString(TEXT("ドライブ×ブラック")));
				// ブラック in front, ドライブ behind: joined, the drive (which flies) goes in front.
				Own->GiveBallsForTests(EChaosImpactBallType::Black, EChaosImpactBallType::Drive);
				Test->TestTrue(TEXT("Two specials join in training"), Own->CanFuseBalls());
				Own->RequestBallFusion();
				Test->TestTrue(TEXT("Joined"), Own->AreBallsFused());
				Test->TestEqual(TEXT("The drive flies (in front)"), Own->GetCarriedBallType(0), EChaosImpactBallType::Drive);
				Test->TestEqual(TEXT("The black hole rides along"), Own->GetCarriedBallType(1), EChaosImpactBallType::Black);
				Frames(TEXT("Join"), {0.06f, 0.18f, 0.3f, 0.38f, 0.44f, 0.5f, 0.6f, 0.8f, 1.3f});
				WaitUntil = Now + 1.6;
				Step = 2;
				return false;
			}
			case 2:
				// Pressed again: parted, both still in hand.
				Own->RequestBallFusion();
				Test->TestFalse(TEXT("Parted"), Own->AreBallsFused());
				Test->TestEqual(TEXT("Both still in hand"), Own->GetCarriedBallCount(), 2);
				Frames(TEXT("Undo"), {0.04f, 0.12f, 0.24f, 0.6f});
				WaitUntil = Now + 0.9;
				Step = 3;
				return false;
			case 3:
				Own->RequestBallFusion();
				Test->TestTrue(TEXT("Joined again"), Own->AreBallsFused());
				WaitUntil = Now + 1.2;
				Step = 4;
				return false;
			case 4:
			{
				const FVector Toward = CPU ? (CPU->GetActorLocation() - Own->GetActorLocation()).GetSafeNormal2D() : Ahead();
				CPUHealthAtThrow = CPU ? CPU->GetHealth() : 0.0f;
				Test->TestTrue(TEXT("The fused throw goes"), Own->ThrowForTests(Toward, 0.3f));
				Frames(TEXT("Throw"), {0.02f, 0.08f, 0.16f, 0.26f, 0.4f, 0.7f, 1.0f});
				WaitUntil = Now + 0.5;
				Step = 5;
				return false;
			}
			case 5:
			{
				Test->TestEqual(TEXT("Both balls left the hands"), Own->GetCarriedBallCount(), 0);
				Test->TestFalse(TEXT("Parted by the throw"), Own->AreBallsFused());
				bool bFusedBall = false;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					bFusedBall |= It->GetBallType() == EChaosImpactBallType::Drive && It->GetFusedType() == EChaosImpactBallType::Black;
				}
				Test->TestTrue(TEXT("One ball: a drive carrying a black hole"), bFusedBall);
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Drive && It->GetFusedType() == EChaosImpactBallType::Black)
					{
						Test->TestTrue(TEXT("吸引ドライブ: steered, it draws in without bursting"), It->IsDragDrive() || It->HasDetonated());
					}
				}
				WaitUntil = Now + 0.1;
				Step = 6;
				return false;
			}
			case 6:
			{
				// Until the drive bursts: a black hole opens there (the one riding along).
				bool bBlack = false;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					bBlack |= It->GetZoneType() == EChaosImpactBallType::Black;
				}
				if (!bBlack && Now - StepStarted() < 6.0)
				{
					return false;
				}
				Test->TestTrue(TEXT("The black hole opened where the drive burst"), bBlack);
				if (CPU)
				{
					// Drawn in and carried along: outside 1v1 (2026-11-19) it hits again as it drags them; it did hit.
					UE_LOG(LogTemp, Display, TEXT("FUSION drag drive: CPU health %.1f -> %.1f, black hole after %.1f s"),
						CPUHealthAtThrow, CPU->GetHealth(), Now - StepStarted());
					// (The CPU may have stepped out of its way: how much it was hit is only logged.)
				}
				Frames(TEXT("Burst"), {0.05f, 0.4f, 1.0f});
				WaitUntil = Now + 1.5;
				Step = 7;
				return false;
			}
			case 7:
			{
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				// ファイア＋ウィンド: the tornado flies (炎の竜巻), burning a line on its way.
				Own->GiveBallsForTests(EChaosImpactBallType::Fire, EChaosImpactBallType::Wind);
				Own->RequestBallFusion();
				Test->TestEqual(TEXT("The wind goes in front"), Own->GetCarriedBallType(0), EChaosImpactBallType::Wind);
				WaitUntil = Now + 1.1;
				Step = 8;
				return false;
			}
			case 8:
				Test->TestTrue(TEXT("The fused tornado goes"), Own->ThrowForTests(Ahead(), 0.3f));
				Frames(TEXT("Tornado"), {0.6f, 1.2f, 2.0f});
				WaitUntil = Now + 3.2;
				Step = 9;
				return false;
			case 9:
			{
				int32 Fires = 0;
				bool bTornado = false;
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It)
				{
					Fires += It->GetZoneType() == EChaosImpactBallType::Fire ? 1 : 0;
				}
				for (TActorIterator<AChaosImpactTornado> It(World); It; ++It) { bTornado = true; }
				UE_LOG(LogTemp, Display, TEXT("FUSION tornado %d, fires along its way %d"), bTornado ? 1 : 0, Fires);
				Test->TestTrue(TEXT("A tornado went"), bTornado);
				Test->TestTrue(TEXT("It burned a line of fire on its way"), Fires >= 3);
				Own->RefillHealth();
				WaitUntil = Now + 2.0;
				Step = 10;
				return false;
			}
			case 10:
			{
				for (TActorIterator<AChaosImpactHazardZone> It(World); It; ++It) { It->Destroy(); }
				// (The CPU is done with: it stands still from here, so its throws and captures cannot take the balls away.)
				if (CPU && CPU->GetController())
				{
					CPU->GetController()->UnPossess();
				}
				// ドライブ×スノー: the snowball goes in front (rolled and growing); thrown, it flies as the drive at the snow's size.
				Own->GiveBallsForTests(EChaosImpactBallType::Drive, EChaosImpactBallType::Snow);
				Own->RequestBallFusion();
				Test->TestEqual(TEXT("The snowball goes in front"), Own->GetCarriedBallType(0), EChaosImpactBallType::Snow);
				Own->SetSnowGrowthForTests(0, 1.0f);
				Frames(TEXT("Snow"), {0.9f});
				WaitUntil = Now + 1.4;
				Step = 11;
				return false;
			}
			case 11:
			{
				Test->TestTrue(TEXT("The fused snowball goes"), Own->ThrowForTests(Ahead(), 0.3f));
				bool bFound = false;
				for (TActorIterator<AChaosImpactBall> It(World); It; ++It)
				{
					if (It->GetBallType() == EChaosImpactBallType::Drive && It->GetFusedType() == EChaosImpactBallType::Snow && !It->HasDetonated())
					{
						bFound = true;
						UE_LOG(LogTemp, Display, TEXT("FUSION drive x snow: size %.2f, reach %.2f"), It->GetSnowScale(), It->GetFusionReach());
						Test->TestEqual(TEXT("At the snow's size, at most 1.8"), It->GetSnowScale(), AChaosImpactBall::FusionSnowCap);
						Test->TestEqual(TEXT("Its effects reach as far"), It->GetFusionReach(), AChaosImpactBall::FusionSnowCap);
					}
				}
				Test->TestTrue(TEXT("It flies as the drive, carrying the snow"), bFound);
				WaitUntil = Now + 3.0;
				Step = 12;
				return false;
			}
			default:
				Own->RefillHealth();
				if (CPU) { CPU->RefillHealth(); }
				return true;
			}
		}

	private:
		double StepStarted()
		{
			if (LastStep != Step) { LastStep = Step; StepAt = FPlatformTime::Seconds(); }
			return StepAt;
		}
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
		double WaitUntil = 0.0;
		double ShotMark = 0.0;
		TArray<TPair<float, FString>> Shots;
		int32 Step = 0;
		int32 LastStep = -1;
		float CPUHealthAtThrow = 0.0f;
		double StepAt = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactFusionTest, "ChaosImpact.Training.FusionBall",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FChaosImpactFusionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFusionCommand(this));
	return true;
}
#endif
