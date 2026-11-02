#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "RPGFieldEvents.h"
#include "RPGShop.h"
#include "RPGTuning.h"
#include "RPGWorldLayout.h"
#include "ChaosImpactPlayerController.h"
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
 * The ruby economy (2026-11): the shop's line-up and prices, buying, the merchant's level, the line-up changing, and a
 * share of the rubies lost when knocked out (pictures in Saved/RPGPrototypeQA).
 */
class FRPGShopCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGShopCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 120) { Test->AddError(FString::Printf(TEXT("Shop test timeout at stage %d"), Stage)); return true; }
		UWorld* W = nullptr;
		for (const auto& C : GEngine->GetWorldContexts()) { if (C.World() && C.World()->IsGameWorld()) { W = C.World(); break; } }
		if (!W) { return false; }
		if (Stage == 0)
		{
			CheckNumbers();
			UGameplayStatics::OpenLevel(W, TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier"), true, TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
			Stage = 1; return false;
		}
		auto* P = Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(W, 0));
		auto* PC = P ? Cast<AChaosImpactPlayerController>(P->GetController()) : nullptr;
		if (!P || !PC || !PC->IsGameplayActive() || !P->GetPrototypeHUD()) { return false; }
		const double Now = W->GetTimeSeconds();
		const auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); };
		const auto Move = [&](const FVector& To) { P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics); };
		switch (Stage)
		{
		case 1:
		{
			ARPGPrototypeMerchant* Shop = nullptr;
			for (TActorIterator<ARPGPrototypeMerchant> It(W); It; ++It) { if (!ARPGFieldEvents::IsCaravan(*It)) { Shop = *It; } }
			if (!Test->TestNotNull(TEXT("The colony has its merchant"), Shop)) { return true; }
			Move(Shop->GetActorLocation() + FVector(-170, 60, 95));
			From = Now; Stage = 2; return false;
		}
		case 2:
		{
			if (Now - From < 1.0) { return false; }
			Test->TestTrue(TEXT("The merchant's workshop opens"), P->OpenMerchant(P->FindMerchant()));
			P->SwitchInventoryPage(ERPGBagPage::Shop);
			Test->TestEqual(TEXT("The shop is a page of the workshop"), P->BagPage, static_cast<int32>(ERPGBagPage::Shop));
			Test->TestEqual(TEXT("A line-up of balls"), P->GetShopStock().Num(), RPGTuning::ShopStockCount);
			Test->TestEqual(TEXT("A new hero's merchant is level 1"), P->GetMerchantLevel(), 1);
			Test->TestTrue(TEXT("The line-up changes within its time"), P->GetShopSecondsLeft() > 0 && P->GetShopSecondsLeft() <= RPGTuning::ShopRotateSeconds);
			P->AddRubies(50000); P->NoticeUntil = 0;
			From = Now; Stage = 3; return false;
		}
		case 3:
			if (Now - From < 0.6) { return false; }
			Shot(TEXT("shop-page.png"));
			From = Now; Stage = 4; return false;
		case 4:
		{
			if (Now - From < 0.4) { return false; }
			const auto Stock = P->GetShopStock();
			const int32 Had = P->GetRubies(), Bag = P->GetBag().Num(), Price = Stock[0].ShopPrice();
			Test->TestTrue(TEXT("A ball is bought"), P->BuyShopItem(0));
			Test->TestEqual(TEXT("It costs its price"), P->GetRubies(), Had - Price);
			Test->TestEqual(TEXT("It goes into the bag"), P->GetBag().Num(), Bag + 1);
			Test->TestTrue(TEXT("The bag has that ball"), P->GetBag().Last().Type == Stock[0].Type && P->GetBag().Last().Rarity == Stock[0].Rarity && P->GetBag().Last().Id != Stock[0].Id);
			Test->TestEqual(TEXT("It is gone from the line-up"), P->GetShopStock().Num(), Stock.Num() - 1);
			Test->TestEqual(TEXT("What is paid counts for the merchant"), P->GetMerchantXP(), Price);
			// Just short of the next level: one more purchase raises it and brings a better line-up at once.
			P->MerchantXP = RPGTuning::MerchantLevelXP[1] - 1;
			Test->TestTrue(TEXT("Another ball is bought"), P->BuyShopItem(0));
			Test->TestEqual(TEXT("The merchant reaches level 2"), P->GetMerchantLevel(), 2);
			Test->TestTrue(TEXT("It is announced"), P->Notice.Contains(TEXT("商人のレベル")));
			Test->TestEqual(TEXT("A full new line-up"), P->GetShopStock().Num(), RPGTuning::ShopStockCount);
			// The next rotation: a different line-up.
			TSet<FGuid> Before; for (const auto& I : P->GetShopStock()) { Before.Add(I.Id); }
			P->ShopRotation = -1; P->RefreshShopStock();
			bool bNew = true; for (const auto& I : P->GetShopStock()) { bNew &= !Before.Contains(I.Id); }
			Test->TestTrue(TEXT("The line-up changes"), bNew && P->GetShopStock().Num() == RPGTuning::ShopStockCount);
			// Upgrades are paid to the merchant too.
			const int32 Spent = P->GetMerchantXP();
			P->SwitchInventoryPage(ERPGBagPage::Upgrade);
			const int32 Index = P->GetBag().Num() - 1, Cost = P->GetBag()[Index].UpgradeCost();
			Test->TestTrue(TEXT("The bought ball can be upgraded"), P->UpgradeItem(Index));
			Test->TestEqual(TEXT("An upgrade counts for the merchant"), P->GetMerchantXP(), Spent + Cost);
			P->SetBagOpen(false);
			From = Now; Stage = 41; return false;
		}
		case 41:
			// The same shop in the one-column menu (split screen).
			if (Now - From < 0.4) { return false; }
			OriginalMini = P->UsesMiniMenu();
			if (!OriginalMini) { P->ToggleMenuStyle(); }
			Test->TestTrue(TEXT("The workshop opens in the mini menu"), P->OpenMerchant(P->FindMerchant()));
			P->SwitchInventoryPage(ERPGBagPage::Shop);
			From = Now; Stage = 42; return false;
		case 42:
			if (Now - From < 1.0) { return false; }
			Test->TestEqual(TEXT("A row of the mini shop can be clicked"), P->GetPrototypeHUD()->MiniActionAt(FVector2D(220, 160)), static_cast<int32>(RPGMenuAction::ShopRowBase));
			P->NoticeUntil = 0; Shot(TEXT("shop-mini.png"));
			From = Now; Stage = 43; return false;
		case 43:
			if (Now - From < 0.4) { return false; }
			P->SetBagOpen(false);
			if (P->UsesMiniMenu() != OriginalMini) { P->ToggleMenuStyle(); }
			From = Now; Stage = 5; return false;
		case 5:
		{
			if (Now - From < 0.4) { return false; }
			// Knocked out (alone, so at once): a share of the rubies is lost.
			Move(FVector(6400, 24800, RPGWorld::Height(6400, 24800) + 140));
			Grunt = nullptr;
			for (TActorIterator<ARPGPrototypeCharacter> It(W); It; ++It) { if (It->IsGrunt() && !It->IsEliminated()) { Grunt = *It; break; } }
			if (!Test->TestNotNull(TEXT("An enemy to be knocked out by"), Grunt.Get())) { return true; }
			Money = P->GetRubies();
			From = Now; Stage = 6; return false;
		}
		case 6:
			if (Now - From < 0.6) { return false; }
			if (Grunt.IsValid()) { P->TakeDamage(1000000, FDamageEvent(), Grunt->GetController(), Grunt.Get()); }
			From = Now; Stage = 7; return false;
		case 7:
		{
			if (Now - From < 0.5) { return false; }
			Test->TestTrue(TEXT("Knocked out"), P->IsEliminated());
			const int32 Lost = FMath::FloorToInt(Money * RPGTuning::DeathRubyLoss);
			Test->TestEqual(TEXT("A share of the rubies is lost"), P->GetRubies(), Money - Lost);
			Test->TestEqual(TEXT("The respawn card says how many"), P->GetDeathRubyLoss(), Lost);
			Shot(TEXT("death-rubies.png"));
			From = Now; Stage = 8; return false;
		}
		case 8:
			if (Now - From < 0.5) { return false; }
			return true;
		default: return true;
		}
	}
private:
	/** The numbers alone: line-ups, prices, the merchant's level, the upgrade and kill rubies. */
	void CheckNumbers()
	{
		const auto Stock = RPGShop::MakeStock(1, 10, RPGTuning::ShopStockCount, false);
		Test->TestEqual(TEXT("Four balls on sale"), Stock.Num(), RPGTuning::ShopStockCount);
		TSet<EChaosImpactBallType> Kinds;
		for (const auto& I : Stock)
		{
			Test->TestTrue(TEXT("Each is a ball near the hero's level"), I.IsValid() && I.Kind == ERPGItemKind::Ball && I.Level >= 9 && I.Level <= 11 && I.Enhancement == 0);
			Kinds.Add(I.Type);
		}
		Test->TestEqual(TEXT("All of different kinds"), Kinds.Num(), Stock.Num());
		Test->TestTrue(TEXT("Rarest first"), Stock[0].Rarity >= Stock.Last().Rarity);
		int32 LowBest = 0; double Low = 0, High = 0;
		for (int32 Try = 0; Try < 150; ++Try)
		{
			for (const auto& I : RPGShop::MakeStock(1, 10, 4, false)) { Low += static_cast<int32>(I.Rarity); LowBest = FMath::Max(LowBest, static_cast<int32>(I.Rarity)); }
			for (const auto& I : RPGShop::MakeStock(5, 10, 4, false)) { High += static_cast<int32>(I.Rarity); }
		}
		Test->TestTrue(TEXT("A level 1 merchant sells normal and rare balls only"), LowBest <= static_cast<int32>(ERPGPrototypeRarity::Rare));
		Test->TestTrue(TEXT("A higher level merchant sells rarer balls"), High > Low * 2);
		const auto Normal = FRPGPrototypeItem::Make(EChaosImpactBallType::Fire, 10, ERPGPrototypeRarity::Normal);
		const auto Legend = FRPGPrototypeItem::Make(EChaosImpactBallType::Fire, 10, ERPGPrototypeRarity::Legend);
		const auto Higher = FRPGPrototypeItem::Make(EChaosImpactBallType::Fire, 20, ERPGPrototypeRarity::Normal);
		Test->TestTrue(TEXT("A legend costs much more"), Legend.ShopPrice() > Normal.ShopPrice() * 10);
		Test->TestTrue(TEXT("A higher level costs more"), Higher.ShopPrice() > Normal.ShopPrice());
		Test->TestEqual(TEXT("Merchant level 1 at first"), RPGTuning::MerchantLevelFor(0), 1);
		Test->TestEqual(TEXT("Merchant level 2"), RPGTuning::MerchantLevelFor(RPGTuning::MerchantLevelXP[1]), 2);
		Test->TestEqual(TEXT("Merchant level stops at 5"), RPGTuning::MerchantLevelFor(100000000), RPGTuning::MerchantMaxLevel);
		// Upgrades: cheap at first, dear near +10.
		auto Ball = Normal; const int32 First = Ball.UpgradeCost();
		Ball.Enhancement = 9;
		Test->TestTrue(TEXT("The last upgrades cost several times the first"), Ball.UpgradeCost() >= First * 5);
		int32 Least = MAX_int32, Most = 0;
		for (int32 Try = 0; Try < 200; ++Try) { const int32 Gain = RPGTuning::KillRubies(10); Least = FMath::Min(Least, Gain); Most = FMath::Max(Most, Gain); }
		Test->TestTrue(TEXT("An enemy gives a few rubies"), Least >= RPGTuning::KillRubyMin + 2 && Most <= RPGTuning::KillRubyMax + 2);
		Test->TestEqual(TEXT("A chest gives a share of its range"), RPGTuning::ChestRubies(500), 200);
	}
	FAutomationTestBase* Test;
	int32 Stage = 0, Money = 0;
	bool OriginalMini = false;
	double From = 0, Started = FPlatformTime::Seconds();
	TWeakObjectPtr<ARPGPrototypeCharacter> Grunt;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopTest, "ChaosImpact.RPGPrototype.Shop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGShopTest::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(FRPGShopCommand(this)); return true; }
#endif
