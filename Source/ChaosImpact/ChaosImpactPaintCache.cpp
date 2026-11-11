#include "ChaosImpactPaintCache.h"

#include "ChaosImpactBodyPaint.h"
#include "ChaosImpactCustomColour.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ImageCore.h"
#include "ImageUtils.h"

UChaosImpactPaintCache* UChaosImpactPaintCache::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	return Game ? Game->GetSubsystem<UChaosImpactPaintCache>() : nullptr;
}

int32 UChaosImpactPaintCache::Register(const TArray<FChaosImpactPaintLayerData>& Layers)
{
	TArray<uint8> Bytes = ChaosImpactBodyPaint::Pack(Layers);
	const int32 Key = ChaosImpactBodyPaint::KeyOf(Bytes);
	if (Key != 0 && !Packed.Contains(Key) && Bytes.Num() <= MostBytes)
	{
		Packed.Add(Key, MoveTemp(Bytes));
	}
	return Packed.Contains(Key) ? Key : 0;
}

int32 UChaosImpactPaintCache::RegisterPreset(const int32 Character, const int32 Slot)
{
	const UChaosImpactColourSubsystem* Colours = UChaosImpactColourSubsystem::Get(this);
	return Colours && Slot >= 0 ? Register(Colours->GetPresetPaint(Character, Slot)) : 0;
}

bool UChaosImpactPaintCache::GetTextures(const int32 Key, TArray<UTexture*>& Out)
{
	Out.Reset();
	if (Key == 0)
	{
		return true;
	}
	if (const FChaosImpactPaintTextures* Made = Textures.Find(Key))
	{
		for (UTexture2D* Layer : Made->Layers)
		{
			Out.Add(Layer);
		}
		return true;
	}
	const TArray<uint8>* Bytes = Packed.Find(Key);
	TArray<FChaosImpactPaintLayerData> Layers;
	if (!Bytes || !ChaosImpactBodyPaint::Unpack(*Bytes, Layers))
	{
		return false;
	}
	FChaosImpactPaintTextures& Made = Textures.Add(Key);
	for (const FChaosImpactPaintLayerData& Layer : Layers)
	{
		FImage Image;
		if (!FImageUtils::DecompressImage(Layer.Png.GetData(), Layer.Png.Num(), Image) || Image.SizeX != ChaosImpactBodyPaint::Size
			|| Image.SizeY != ChaosImpactBodyPaint::Size)
		{
			continue;
		}
		FImage Bgra;
		Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const TArrayView64<FColor> View = Bgra.AsBGRA8();
		TArray<FColor> Pixels(View.GetData(), static_cast<int32>(View.Num()));
		if (Made.Layers.Num() <= Layer.Layer)
		{
			Made.Layers.SetNum(Layer.Layer + 1);
		}
		Made.Layers[Layer.Layer] = ChaosImpactBodyPaint::MakeTexture(Pixels, this);
	}
	for (UTexture2D* Layer : Made.Layers)
	{
		Out.Add(Layer);
	}
	return true;
}

TArray<UTexture*> UChaosImpactPaintCache::GetPresetTextures(const int32 Character, const int32 Slot)
{
	TArray<UTexture*> Out;
	GetTextures(RegisterPreset(Character, Slot), Out);
	return Out;
}

bool UChaosImpactPaintCache::Receive(const int32 Key, const int32 Offset, const int32 Total, const TArray<uint8>& Bytes, int32& OutNext)
{
	OutNext = INDEX_NONE;
	if (Key == 0 || Has(Key) || Total <= 0 || Total > MostBytes)
	{
		Incoming.Remove(Key);
		return Has(Key);
	}
	FIncoming& Coming = Incoming.FindOrAdd(Key);
	if (Coming.Total != Total || Offset != Coming.Bytes.Num())
	{
		// Begun again (or out of turn): from the start.
		Coming.Total = Total;
		Coming.Bytes.Reset();
		if (Offset != 0)
		{
			OutNext = 0;
			return false;
		}
	}
	Coming.Bytes.Append(Bytes.GetData(), FMath::Min(Bytes.Num(), Total - Coming.Bytes.Num()));
	if (Coming.Bytes.Num() < Total)
	{
		OutNext = Coming.Bytes.Num();
		return false;
	}
	TArray<uint8> Whole = MoveTemp(Coming.Bytes);
	Incoming.Remove(Key);
	// Kept only when it is what its key says (and reads as pictures).
	TArray<FChaosImpactPaintLayerData> Layers;
	if (ChaosImpactBodyPaint::KeyOf(Whole) != Key || !ChaosImpactBodyPaint::Unpack(Whole, Layers))
	{
		return false;
	}
	Packed.Add(Key, MoveTemp(Whole));
	return true;
}

TArray<uint8> UChaosImpactPaintCache::GetPiece(const int32 Key, const int32 Offset) const
{
	const TArray<uint8>* Bytes = Packed.Find(Key);
	if (!Bytes || Offset < 0 || Offset >= Bytes->Num())
	{
		return TArray<uint8>();
	}
	return TArray<uint8>(Bytes->GetData() + Offset, FMath::Min(PieceBytes, Bytes->Num() - Offset));
}

void UChaosImpactPaintCache::Want(const int32 Key)
{
	if (Key != 0 && !Has(Key))
	{
		Wanted.Add(Key);
	}
}

int32 UChaosImpactPaintCache::TakeWanted()
{
	const double Now = FPlatformTime::Seconds();
	for (auto It = Wanted.CreateIterator(); It; ++It)
	{
		const int32 Key = *It;
		if (Has(Key))
		{
			It.RemoveCurrent();
			continue;
		}
		const double* Retry = RetryAt.Find(Key);
		if (Fetching.Contains(Key) || (Retry && Now < *Retry))
		{
			continue;
		}
		Fetching.Add(Key);
		return Key;
	}
	return 0;
}

void UChaosImpactPaintCache::FinishFetch(const int32 Key, const bool bFailed)
{
	Fetching.Remove(Key);
	if (bFailed)
	{
		RetryAt.Add(Key, FPlatformTime::Seconds() + 2.0);
	}
	else
	{
		Wanted.Remove(Key);
		RetryAt.Remove(Key);
	}
}
