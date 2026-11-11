#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactPaintCache.generated.h"

class APlayerController;
class UTexture;
class UTexture2D;
struct FChaosImpactPaintLayerData;

/** One set of pictures as textures, one per layer (null: nothing drawn on that layer). */
USTRUCT()
struct FChaosImpactPaintTextures
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UTexture2D>> Layers;
};

/**
 * ペイント pictures by their key (ChaosImpactBodyPaint::KeyOf the packed PNGs): this machine's own presets, and
 * everyone else's as they come over the network. A key names the same pictures everywhere, so a player's
 * pictures cross the network once and are shown from here on every level after that.
 */
UCLASS()
class UChaosImpactPaintCache : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static UChaosImpactPaintCache* Get(const UObject* WorldContext);

	/** Pictures kept (this machine's own): their key, 0 for none. */
	int32 Register(const TArray<FChaosImpactPaintLayerData>& Layers);
	/** A preset's pictures kept: their key, 0 for none. */
	int32 RegisterPreset(int32 Character, int32 Slot);
	bool Has(int32 Key) const { return Key != 0 && Packed.Contains(Key); }
	const TArray<uint8>* GetPacked(int32 Key) const { return Packed.Find(Key); }
	/** The pictures as textures (made the first time asked); false while they are not here. */
	bool GetTextures(int32 Key, TArray<UTexture*>& Out);
	/** A preset's pictures as textures, for the model shown on a menu (empty: none). */
	TArray<UTexture*> GetPresetTextures(int32 Character, int32 Slot);

	// Sent over the network a piece at a time (each piece asked for after the last has come).
	static constexpr int32 PieceBytes = 12 * 1024;
	/** The most one set of pictures may be (a little more than ChaosImpactBodyPaint::MaxBytes for the packing). */
	static constexpr int32 MostBytes = 640 * 1024;
	/** A piece come in. True when it was the last and the pictures are kept now; OutNext is where to ask on from. */
	bool Receive(int32 Key, int32 Offset, int32 Total, const TArray<uint8>& Bytes, int32& OutNext);
	/** The piece of Key's bytes starting at Offset (empty past the end or when not here). */
	TArray<uint8> GetPiece(int32 Key, int32 Offset) const;

	/** Asked for by a character being shown: fetched by this machine's player controller (TakeWanted). */
	void Want(int32 Key);
	/** One key to start fetching now (0: none), and its fetch over (bFailed: try it again a little later). */
	int32 TakeWanted();
	void FinishFetch(int32 Key, bool bFailed);

private:
	TMap<int32, TArray<uint8>> Packed;
	struct FIncoming
	{
		int32 Total = 0;
		TArray<uint8> Bytes;
	};
	TMap<int32, FIncoming> Incoming;
	UPROPERTY(Transient)
	TMap<int32, FChaosImpactPaintTextures> Textures;
	TSet<int32> Wanted;
	TSet<int32> Fetching;
	TMap<int32, double> RetryAt;
};
