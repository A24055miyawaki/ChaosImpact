#pragma once

#include "CoreMinimal.h"

struct FRPGPrototypeItem;

/** The merchants' shop (RPGShop.cpp; the character's side is in RPGPrototype.h). */
namespace RPGShop
{
	/** Which line-up of the shop is on now (real time, RPGTuning::ShopRotateSeconds each). */
	int64 RotationNow();
	/** A new line-up: Count balls around the hero's level, rarer with the merchant's level (the caravan's a little higher). */
	TArray<FRPGPrototypeItem> MakeStock(int32 MerchantLevel, int32 HeroLevel, int32 Count, bool bCaravan);
}
