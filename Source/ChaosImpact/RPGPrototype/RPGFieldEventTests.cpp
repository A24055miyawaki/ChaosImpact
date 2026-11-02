#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGFieldEvents.h"
#include "RPGTreasureChest.h"
#include "RPGTutorial.h"
#include "RPGTuning.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactPlayerController.h"
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
 * Field events and named enemies: a falling star leaves its chest, the caravan's workshop is cheaper, a sandstorm
 * blows over the desert, a named one turns up with a strong group (-CINamed) and beating one pays as a boss does
 * (pictures in Saved/RPGPrototypeQA).
 */
class FRPGFieldEventCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGFieldEventCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150) { Test->AddError(FString::Printf(TEXT("Field event test timeout at stage %d"), Stage)); return true; }
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
		ARPGFieldEvents* Events = ARPGFieldEvents::Find(W);
		const auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); };
		const auto Move = [&](const FVector& To) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics); };
		const auto Wait = [&](double Seconds) { if (Now - From < Seconds) { return true; } From = Now; ++Stage; return false; };
		P->RestoreHP(100000);
		switch (Stage)
		{
		case 1:
			if (!Test->TestNotNull(TEXT("The adventure world has its field events"), Events)) { return true; }
			Test->TestEqual(TEXT("Nothing is happening at the start"), static_cast<int32>(Events->GetKind()), static_cast<int32>(ARPGFieldEvents::None));
			// Into the desert, then a falling star near by.
			Move(FVector(6400, 24800, RPGWorld::Height(6400, 24800) + 140));
			From = Now; Stage = 2; return false;
		case 2:
			if (Wait(2)) { return false; }
			Events->StartEvent(ARPGFieldEvents::Meteor);
			Test->TestEqual(TEXT("A falling star"), static_cast<int32>(Events->GetKind()), static_cast<int32>(ARPGFieldEvents::Meteor));
			return false;
		case 3:
		{
			ARPGTreasureChest* Star = nullptr;
			for (TActorIterator<ARPGTreasureChest> It(W); It; ++It) { if (It->DisplayName == TEXT("流れ星の宝箱")) { Star = *It; } }
			if (!Test->TestNotNull(TEXT("The falling star left a chest"), Star)) { return true; }
			Test->TestTrue(TEXT("Its chest is a gold one"), Star->Style == ERPGChestStyle::Gold);
			Move(Star->GetActorLocation() + FVector(-180, -70, 100));
			P->SetActorRotation(FRotator(0, 25, 0));
			From = Now; Stage = 4; return false;
		}
		case 4:
			if (Now - From < 1.5) { return false; }
			P->NoticeUntil = 0; Shot(TEXT("event-meteor.png"));
			From = Now; Stage = 40; return false;
		case 40:
			// (Each picture its own frame: nothing changes until it is taken.)
			if (Now - From < 0.4) { return false; }
			if (ARPGTreasureChest* Star = ARPGTreasureChest::Nearest(P))
			{
				const int32 Shards = P->GetTuneShards();
				Test->TestTrue(TEXT("Its chest opens"), Star->Open(P));
				Test->TestTrue(TEXT("Its chest held a tune shard"), P->GetTuneShards() > Shards);
			}
			else
			{
				for (TActorIterator<ARPGTreasureChest> It(W); It; ++It)
				{
					if (It->DisplayName != TEXT("流れ星の宝箱")) { continue; }
					FHitResult H; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGTestChest), false, P); Q.AddIgnoredActor(*It);
					const bool bBlocked = W->LineTraceSingleByObjectType(H, P->GetActorLocation(), It->GetActorLocation() + FVector(0, 0, 90), FCollisionObjectQueryParams(ECC_WorldStatic), Q);
					UE_LOG(LogTemp, Display, TEXT("RPGEVENT star chest at %s, hero %s, d %.0f dz %.0f, opened %d bag %d menu %d blocked %d by %s"), *It->GetActorLocation().ToString(), *P->GetActorLocation().ToString(),
						FVector::Dist2D(It->GetActorLocation(), P->GetActorLocation()), P->GetActorLocation().Z - It->GetActorLocation().Z, It->IsOpenedBy(P) ? 1 : 0, P->IsBagOpen() ? 1 : 0,
						P->IsQuestMenuOpen() ? 1 : 0, bBlocked ? 1 : 0, bBlocked && H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"));
				}
				Test->AddError(TEXT("The star's chest is not in reach"));
			}
			P->SetMapMode(2);
			From = Now; Stage = 5; return false;
		case 5:
			if (Now - From < 0.8) { return false; }
			Shot(TEXT("event-map.png"));
			From = Now; Stage = 50; return false;
		case 50:
			if (Now - From < 0.4) { return false; }
			P->SetMapMode(0);
			// The caravan.
			Events->StartEvent(ARPGFieldEvents::Caravan);
			Test->TestEqual(TEXT("A caravan"), static_cast<int32>(Events->GetKind()), static_cast<int32>(ARPGFieldEvents::Caravan));
			From = Now; Stage = 6; return false;
		case 6:
		{
			if (Now - From < 0.5) { return false; }
			int32 Stars = 0;
			for (TActorIterator<ARPGTreasureChest> It(W); It; ++It) { if (It->DisplayName == TEXT("流れ星の宝箱")) { ++Stars; } }
			Test->TestEqual(TEXT("The star's chest goes with it"), Stars, 0);
			ARPGPrototypeMerchant* Shop = nullptr;
			for (TActorIterator<ARPGPrototypeMerchant> It(W); It; ++It) { if (ARPGFieldEvents::IsCaravan(*It)) { Shop = *It; } }
			if (!Test->TestNotNull(TEXT("The caravan set up its workshop"), Shop)) { return true; }
			Move(Shop->GetActorLocation() + FVector(-170, 60, 95));
			From = Now; Stage = 7; return false;
		}
		case 7:
		{
			if (Now - From < 1.0) { return false; }
			ARPGPrototypeMerchant* Shop = P->FindMerchant();
			Test->TestTrue(TEXT("The caravan's workshop is in reach"), Shop && ARPGFieldEvents::IsCaravan(Shop));
			P->NoticeUntil = 0; Shot(TEXT("event-caravan-wagon.png"));
			From = Now; Stage = 70; return false;
		}
		case 70:
		{
			if (Now - From < 0.4) { return false; }
			ARPGPrototypeMerchant* Shop = P->FindMerchant();
			const float Before = RPGColony::Discount();
			Test->TestTrue(TEXT("Its workshop opens"), Shop && P->OpenMerchant(Shop));
			From = Now; Stage = 8; DiscountBefore = Before; return false;
		}
		case 8:
			if (Now - From < 0.6) { return false; }
			Test->TestTrue(TEXT("The caravan's workshop is cheaper"), RPGColony::Discount() >= DiscountBefore + RPGTuning::CaravanDiscount - 0.01f);
			Shot(TEXT("event-caravan.png"));
			From = Now; Stage = 80; return false;
		case 80:
			if (Now - From < 0.4) { return false; }
			P->SetBagOpen(false);
			// A sandstorm over the desert.
			Move(FVector(6400, 24800, RPGWorld::Height(6400, 24800) + 140));
			From = Now; Stage = 9; return false;
		case 9:
			if (Now - From < 1.0) { return false; }
			Events->StartEvent(ARPGFieldEvents::Storm);
			Test->TestEqual(TEXT("A storm"), static_cast<int32>(Events->GetKind()), static_cast<int32>(ARPGFieldEvents::Storm));
			Test->TestEqual(TEXT("A sandstorm in the desert"), Events->GetTitle(), FString(TEXT("砂嵐")));
			Test->TestTrue(TEXT("It blows where the player is"), ARPGFieldEvents::IsStormAt(W, P->GetActorLocation()));
			Test->TestFalse(TEXT("Not over the colony"), ARPGFieldEvents::IsStormAt(W, FVector(0, 0, 100)));
			Test->TestTrue(TEXT("It has a while to go"), Events->GetSecondsLeft() > 100);
			From = Now; Stage = 10; return false;
		case 10:
			if (Now - From < 1.2) { return false; }
			P->NoticeUntil = 0; Shot(TEXT("event-storm.png"));
			From = Now; Stage = 100; return false;
		case 100:
			if (Now - From < 0.4) { return false; }
			// A strong group, with a named one (-CINamed).
			Move(FVector(25300, 27400, RPGWorld::Height(25300, 27400)) + FVector(-1500, -300, 140));
			From = Now; Stage = 11; return false;
		case 11:
		{
			ARPGPrototypeCharacter* Named = nullptr;
			for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It) { if (It->IsNamed() && !It->IsEliminated()) { Named = *It; } }
			if (!Named) { if (Now - From > 20) { Test->AddError(TEXT("No named enemy turned up")); return true; } return false; }
			Test->TestTrue(TEXT("A named one is an elite too"), Named->IsElite());
			Test->TestTrue(TEXT("A named one is bigger than an elite"), Named->GetActorScale3D().Z > RPGTuning::EliteScale + 0.05f);
			int32 Count = 0;
			for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It) { if (It->IsNamed() && !It->IsEliminated()) { ++Count; } }
			Test->TestEqual(TEXT("One named one at a time"), Count, 1);
			UE_LOG(LogTemp, Display, TEXT("RPGEVENT named %s hp %.0f"), *Named->NamedTitle, Named->GetMaxHealth());
			// West of it, so it stands beside the player on screen (its name over its head in view).
			Move(Named->GetActorLocation() + FVector(-60, -480, 60));
			P->SetActorRotation(FRotator::ZeroRotator);
			From = Now; Stage = 12; return false;
		}
		case 12:
			if (Now - From < 0.7) { return false; }
			P->NoticeUntil = 0; Shot(TEXT("event-named.png"));
			From = Now; Stage = 120; return false;
		case 120:
		{
			if (Now - From < 0.4) { return false; }
			// Beating one: a boss's drop and its shards.
			const int32 Shards = P->GetTuneShards();
			int32 Loot = 0;
			for (TActorIterator<ARPGPrototypeLoot> It(W); It; ++It) { ++Loot; }
			FRPGKillCredit Credit; Credit.Level = 6; Credit.bNamed = true; Credit.Detail = 16; Credit.At = P->GetActorLocation() + FVector(200, 0, 0); Credit.DropChance = 0;
			P->ReceiveKillCredit(Credit);
			int32 After = 0;
			for (TActorIterator<ARPGPrototypeLoot> It(W); It; ++It) { ++After; }
			Test->TestEqual(TEXT("A named one gives its shards"), P->GetTuneShards(), Shards + RPGTuning::ShardsPerNamed);
			Test->TestTrue(TEXT("A named one always drops something"), After > Loot);
			return true;
		}
		default: return true;
		}
	}
private:
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double From = 0, Started = FPlatformTime::Seconds();
	float DiscountBefore = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFieldEventTest, "ChaosImpact.RPGPrototype.FieldEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGFieldEventTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGFieldEventCommand(this)); return true; }
#endif
