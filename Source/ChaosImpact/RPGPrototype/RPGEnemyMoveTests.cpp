#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGEnemyMoves.h"
#include "RPGWorldLayout.h"
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
/**
 * A high-level group in the snowfield: its bomber, charger and sniper show their warned attacks, and an elite stands
 * out (pictures in Saved/RPGPrototypeQA).
 */
class FRPGEnemyMoveCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGEnemyMoveCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 120) { Test->AddError(TEXT("Enemy move test timeout")); return true; }
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
		if (!P || !PC || !PC->IsGameplayActive() || !P->GetPrototypeHUD()) { return false; }
		const double Now = W->GetTimeSeconds();
		if (Stage == 1)
		{
			// Beside the snowfield wood's group (level 9: charger, bomber, sniper among them).
			P->GetCharacterMovement()->StopMovementImmediately();
			P->SetActorLocation(FVector(25300, 27400, RPGWorld::Height(25300, 27400)) + FVector(-700, -300, 140), false, nullptr, ETeleportType::TeleportPhysics);
			Stage = 2; From = Now; return false;
		}
		P->RestoreHP(100000);
		Lowest = FMath::Min(Lowest, P->GetHealth());
		for (TActorIterator<ARPGGroundStrike> It(W); It; ++It)
		{
			const uint8 Kind = It->GetKind();
			if (Seen.Contains(Kind)) { continue; }
			Seen.Add(Kind);
			P->NoticeUntil = 0;
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), FString::Printf(TEXT("enemy-strike-%d.png"), Kind)), true, false);
		}
		for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It)
		{
			if (!It->IsGrunt() || It->IsEliminated()) { continue; }
			Roles.Add(static_cast<uint8>(It->EncounterRole));
			if (It->IsElite()) { bElite = true; }
		}
		if (Now - From < 30 && Seen.Num() < 3) { return false; }
		UE_LOG(LogTemp, Display, TEXT("RPGENEMY strikes seen %d, roles %d, elite %d"), Seen.Num(), Roles.Num(), bElite ? 1 : 0);
		Test->TestTrue(TEXT("The group has bombers, chargers and snipers"), Roles.Contains(2) && Roles.Contains(3) && Roles.Contains(4));
		Test->TestTrue(TEXT("Bombs are thrown"), Seen.Contains(0));
		Test->TestTrue(TEXT("Chargers charge"), Seen.Contains(1));
		Test->TestTrue(TEXT("Snipers shoot"), Seen.Contains(2));
		return true;
	}
private:
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double From = 0, Started = FPlatformTime::Seconds();
	float Lowest = MAX_flt;
	TSet<uint8> Seen, Roles;
	bool bElite = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEnemyMoveTest, "ChaosImpact.RPGPrototype.EnemyMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGEnemyMoveTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGEnemyMoveCommand(this)); return true; }
#endif
