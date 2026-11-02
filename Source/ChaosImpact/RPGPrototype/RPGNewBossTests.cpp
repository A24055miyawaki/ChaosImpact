#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGSnowField.h"
#include "ChaosImpactPuppetComponent.h"
#include "Engine/DamageEvents.h"
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
 * The bosses of the new lands (pictures in Saved/RPGPrototypeQA): each wakes when a player comes, shows its warned
 * moves, and once beaten comes back as its strong version; the legend sleeps until every other boss is beaten.
 */
class FRPGNewBossCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGNewBossCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 260) { Test->AddError(FString::Printf(TEXT("New boss test timeout (boss %d, stage %d)"), Current, Stage)); return true; }
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
		P->RestoreHP(100000);
		if (Now < Next) { return false; }
		const auto Shot = [](const FString& Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); };
		const auto Move = [&](const FVector& At) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics); };
		if (Stage == 1)
		{
			// Only the bosses move (the groups stand still), and the player cannot fall.
			for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It) { if (It->IsGrunt() && !Cast<ARPGPrototypeBoss>(*It) && !Cast<ARPGSimaeBody>(*It)) { It->SetTrainingMenuFrozen(true); } }
			for (TActorIterator<ARPGPrototypeBoss> It(W); It; ++It) { if (It->Kind != ERPGBossKind::Maguro) { Bosses.Add(*It); } }
			Bosses.Sort([](const TWeakObjectPtr<ARPGPrototypeBoss>& A, const TWeakObjectPtr<ARPGPrototypeBoss>& B) { return A->Kind < B->Kind; });
			Test->TestEqual(TEXT("Five bosses in the new lands"), Bosses.Num(), 5);
			Stage = 2; Current = 0; return false;
		}
		if (!Bosses.IsValidIndex(Current)) { return true; }
		ARPGPrototypeBoss* Boss = Bosses[Current].Get();
		if (!Boss) { Test->AddError(TEXT("A boss went missing")); return true; }
		const FString Name = UEnum::GetValueAsString(Boss->Kind).RightChop(14);
		switch (Stage)
		{
		case 2:
			if (Boss->Kind == ERPGBossKind::Legend)
			{
				Test->TestTrue(TEXT("The legend sleeps at first"), Boss->IsAsleep());
				Move(Boss->GetArenaHome() + FVector(-800, 0, 140));
				Stage = 3; Next = Now + 1.0; return false;
			}
			Move(Boss->GetArenaHome() + FVector(-750, 0, 140));
			Seen.Reset(); bShot = false; FightFrom = Now;
			Stage = 4; Next = Now + 0.2; return false;
		case 3:
			Test->TestTrue(TEXT("Still asleep while bosses remain"), Boss->IsAsleep() && Boss->GetBossesLeftToWake() > 0);
			P->NoticeUntil = 0; Shot(TEXT("boss-legend-asleep.png"));
			// Every other boss beaten: it wakes.
			P->BossesBeaten = 0x3F;
			Seen.Reset(); bShot = false; FightFrom = Now;
			Stage = 4; Next = Now + 0.5; return false;
		case 4:
		{
			// Fighting: note what it does; a picture of its first warned move.
			const ERPGBossState S = Boss->GetBossState();
			Seen.Add(S);
			const bool bTell = S == ERPGBossState::GeyserTell || S == ERPGBossState::VortexTell || S == ERPGBossState::ChargeTell || S == ERPGBossState::LeapTell;
			if (bTell && !bShot && Now - FightFrom > 1.0) { bShot = true; P->NoticeUntil = 0; Shot(FString::Printf(TEXT("boss-%s-%d.png"), *Name, static_cast<int32>(S))); }
			if (Now - FightFrom < 16 && (Seen.Num() < 5 || !bShot)) { return false; }
			Test->TestFalse(FString::Printf(TEXT("%s woke and fought"), *Name), Boss->IsAsleep());
			Test->TestTrue(FString::Printf(TEXT("%s used several moves"), *Name), Seen.Num() >= 4);
			FString Moves; for (const ERPGBossState Each : Seen) { Moves += UEnum::GetValueAsString(Each).RightChop(15) + TEXT(" "); }
			UE_LOG(LogTemp, Display, TEXT("RPGBOSS %s Lv%d HP%.0f moves: %s"), *Boss->GetBossName(), Boss->GetRPGLevel(), Boss->GetMaxHealth(), *Moves);
			// Its own model, kept while it fights (not the default character).
			const auto* Puppet = Boss->FindComponentByClass<UChaosImpactPuppetComponent>();
			Test->TestTrue(FString::Printf(TEXT("%s keeps its own model"), *Name), Puppet && Puppet->GetCharacterIndex() == Boss->LookOverride && Boss->LookOverride > 0);
			// Beaten: the next fight is its strong version.
			Boss->TakeDamage(Boss->GetHealth() * 4 + 100000, FDamageEvent(), PC, P);
			Stage = 5; Next = Now + 0.5; return false;
		}
		case 5:
			Test->TestEqual(FString::Printf(TEXT("%s beaten"), *Name), Boss->GetBossState(), ERPGBossState::Defeated);
			Move(Boss->GetArenaHome() + FVector(-500, 0, 140));
			Stage = 6; Next = Now + 0.5; return false;
		case 6:
			Test->TestTrue(FString::Printf(TEXT("%s can be fought again"), *Name), Boss->Retry(P));
			Test->TestTrue(FString::Printf(TEXT("%s comes back strong"), *Name), Boss->IsStrongVersion() && Boss->GetBossName().StartsWith(TEXT("真・")));
			UE_LOG(LogTemp, Display, TEXT("RPGBOSS strong %s Lv%d HP%.0f"), *Boss->GetBossName(), Boss->GetRPGLevel(), Boss->GetMaxHealth());
			Stage = 7; Next = Now + 2.5; return false;
		case 7:
			P->NoticeUntil = 0; Shot(FString::Printf(TEXT("boss-%s-strong.png"), *Name));
			Move(FVector(0, 0, 300));
			++Current; Stage = 2; Next = Now + 1.0; return false;
		default: return true;
		}
	}
private:
	FAutomationTestBase* Test;
	TArray<TWeakObjectPtr<ARPGPrototypeBoss>> Bosses;
	TSet<ERPGBossState> Seen;
	int32 Stage = 0, Current = 0;
	bool bShot = false;
	double Next = 0, FightFrom = 0, Started = FPlatformTime::Seconds();
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNewBossTest, "ChaosImpact.RPGPrototype.NewBosses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGNewBossTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGNewBossCommand(this)); return true; }
#endif
