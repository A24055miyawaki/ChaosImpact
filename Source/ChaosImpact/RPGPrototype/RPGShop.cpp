#include "RPGShop.h"
#include "RPGPrototype.h"
#include "RPGEquipment.h"
#include "RPGFieldEvents.h"
#include "RPGTuning.h"
#include "RPGTutorial.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"

// The merchants' shop (2026-11): balls for rubies, at the merchant's workshop and at the caravan's.
//
// - The line-up is RPGTuning::ShopStockCount balls around the hero's level. It changes every ShopRotateSeconds of real
//   time (so also while the game is closed) and is saved with the hero, so loading again does not make a new one.
// - Rubies paid at a merchant (upgrades, tunes, purchases) raise the merchant's level, 1 to 5: rarer balls are sold,
//   and the line-up is made again at once. The caravan sells a level better, and new each time it comes.
// - Everything is the player's own (rubies, bag, line-up), so online it all happens on that player's machine.

int64 RPGShop::RotationNow() { return FDateTime::UtcNow().ToUnixTimestamp() / RPGTuning::ShopRotateSeconds; }

TArray<FRPGPrototypeItem> RPGShop::MakeStock(int32 MerchantLevel, int32 HeroLevel, int32 Count, bool bCaravan)
{
	using namespace RPGTuning;
	const FShopOdds& Odds = ShopOdds[FMath::Clamp(MerchantLevel, 1, MerchantMaxLevel) - 1];
	// Different kinds of ball first; the same kind again only when there are more places than kinds.
	TArray<EChaosImpactBallType> Kinds = RPGEquipment::BallRoster();
	for (int32 Index = Kinds.Num() - 1; Index > 0; --Index) { Kinds.Swap(Index, FMath::RandRange(0, Index)); }
	TArray<FRPGPrototypeItem> Stock;
	for (int32 Index = 0; Index < Count && Kinds.Num(); ++Index)
	{
		const float Roll = FMath::FRand();
		const ERPGPrototypeRarity Rarity = Roll < Odds.Legend ? ERPGPrototypeRarity::Legend : Roll < Odds.Legend + Odds.Unique ? ERPGPrototypeRarity::Unique
			: Roll < Odds.Legend + Odds.Unique + Odds.Rare ? ERPGPrototypeRarity::Rare : ERPGPrototypeRarity::Normal;
		const int32 Level = FMath::Max(1, HeroLevel + (bCaravan ? FMath::RandRange(0, 2) : FMath::RandRange(-1, 1)));
		Stock.Add(FRPGPrototypeItem::Make(Kinds[Index % Kinds.Num()], Level, Rarity));
	}
	// Rarest first, so the best of the line-up is at the top.
	Stock.StableSort([](const FRPGPrototypeItem& A, const FRPGPrototypeItem& B) { return A.Rarity > B.Rarity; });
	return Stock;
}

int32 FRPGPrototypeItem::ShopPrice() const
{
	using namespace RPGTuning;
	const double Base = (ShopBase + static_cast<int64>(Level) * ShopPerLevel) * ShopRarityMultiplier[FMath::Clamp(static_cast<int32>(Rarity), 0, 3)];
	return FMath::Clamp<int64>(FMath::RoundToInt64(Base * (1 - RPGColony::Discount())), 1, CostCap);
}

bool ARPGPrototypeCharacter::IsAtCaravan() const { return IsAtMerchant() && ARPGFieldEvents::IsCaravan(ActiveMerchant.Get()); }

const TArray<FRPGPrototypeItem>& ARPGPrototypeCharacter::GetShopStock() const { return IsAtCaravan() ? CaravanStock : ShopStock; }

int32 ARPGPrototypeCharacter::GetShopSecondsLeft() const
{
	if (IsAtCaravan()) { const auto* Events = ARPGFieldEvents::Find(GetWorld()); return Events ? FMath::CeilToInt(Events->GetSecondsLeft()) : 0; }
	return RPGTuning::ShopRotateSeconds - static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp() % RPGTuning::ShopRotateSeconds);
}

void ARPGPrototypeCharacter::RefreshShopStock()
{
	if (bGrunt || !GetWorld()) { return; }
	const int32 Level = GetMerchantLevel();
	if (IsAtCaravan())
	{
		const auto* Events = ARPGFieldEvents::Find(GetWorld());
		const int32 Visit = Events ? Events->GetEventId() : 0;
		if (CaravanStockEvent != Visit) { CaravanStock = RPGShop::MakeStock(FMath::Min(Level + 1, RPGTuning::MerchantMaxLevel), RPGLevel, RPGTuning::CaravanStockCount, true); CaravanStockEvent = Visit; ShopSelection = 0; }
		return;
	}
	const int64 Rotation = RPGShop::RotationNow();
	if (ShopRotation != Rotation || ShopStockLevel != Level)
	{
		ShopStock = RPGShop::MakeStock(Level, RPGLevel, RPGTuning::ShopStockCount, false);
		ShopRotation = Rotation; ShopStockLevel = Level; ShopSelection = 0;
	}
}

bool ARPGPrototypeCharacter::SpendAtMerchant(int32 Cost)
{
	const int32 Before = GetMerchantLevel();
	Cost = FMath::Clamp(Cost, 0, Rubies);
	Rubies -= Cost;
	MerchantXP = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(MerchantXP) + Cost, 1000000000));
	if (GetMerchantLevel() == Before) { return false; }
	// A better line-up at once, at both counters.
	CaravanStockEvent = -1;
	RefreshShopStock();
	return true;
}

void ARPGPrototypeCharacter::AnnounceMerchantLevel()
{
	if (!GetWorld()) { return; }
	Notice = FString::Printf(TEXT("商人のレベルが %d に上がった！　レアなボールが並ぶようになった"), GetMerchantLevel());
	NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
}

bool ARPGPrototypeCharacter::BuyShopItem(int32 Index)
{
	if (!IsAtMerchant() || IsEliminated() || !GetWorld()) { return false; }
	RefreshShopStock();
	TArray<FRPGPrototypeItem>& Stock = IsAtCaravan() ? CaravanStock : ShopStock;
	if (!Stock.IsValidIndex(Index)) { return false; }
	const int32 Price = Stock[Index].ShopPrice();
	if (Rubies < Price) { Notice = TEXT("ルビーが足りません"); NoticeUntil = GetWorld()->GetTimeSeconds() + 2; return false; }
	FRPGPrototypeItem Item = Stock[Index];
	Item.Id = FGuid::NewGuid();
	// AddLoot says so itself when the bag is full.
	if (!AddLoot(Item)) { return false; }
	Stock.RemoveAt(Index);
	ShopSelection = FMath::Clamp(Index, 0, FMath::Max(0, Stock.Num() - 1));
	const bool bLevelUp = SpendAtMerchant(Price);
	TutorialEvent(TEXT("Buy"));
	Notice = FString::Printf(TEXT("%s を買った（◆%d）　バッグに入れました"), *Item.Name(), Price); NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
	if (bLevelUp) { AnnounceMerchantLevel(); }
	return true;
}
