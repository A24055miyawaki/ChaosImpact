#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ChaosImpactBodyPaint.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChaosImpactCustomColour.generated.h"

/**
 * カスタムカラー: each character's parts (as its materials' part masks divide it) painted in any colour. A preset is
 * one colour per part (or that part as it is), saved per character on this machine and chosen on the character
 * select screen, then seen by everyone online (four bytes a part).
 * Parts are stored as bytes, four per part: on (0: as it is), then the colour's sRGB red, green and blue.
 */
namespace ChaosImpactCustomColour
{
	inline constexpr int32 MaxParts = 6;
	inline constexpr int32 MaxPresets = 8;
	inline constexpr int32 MaxNameLength = 8;
	inline constexpr int32 BytesPerPart = 4;
	/** The editor's palette: hues across, light to dark down (the last column greys). */
	inline constexpr int32 PaletteColumns = 13;
	inline constexpr int32 PaletteRows = 6;
	inline constexpr int32 MaxRecent = 12;
	FColor GetPaletteSwatch(int32 Column, int32 Row);
	/** The colourable parts of a character, by name (one per part mask channel / slot). */
	const TArray<FString>& GetPartNames(int32 Character);
	int32 GetPartCount(int32 Character);
	/** A part's colour in Parts (false: that part as it is). */
	bool GetPart(const TArray<uint8>& Parts, int32 Part, FColor& OutColour);
	/** Sets a part (bOn false: back to as it is); Parts grows to hold it. */
	void SetPart(TArray<uint8>& Parts, int32 Part, bool bOn, const FColor& Colour = FColor::White);
	/** Material values for the model (ChaosImpactPuppetComponent::SetCustomColours): alpha 0 for a part left as it is. */
	TArray<FLinearColor> ToPartColours(int32 Character, const TArray<uint8>& Parts);
	/** Exactly the character's parts' worth of bytes (missing ones as they are). */
	TArray<uint8> Clean(int32 Character, const TArray<uint8>& Parts);
	/** sRGB <-> hue (0-360), saturation and value (0-1), as the editor's sliders work. */
	void ToHSV(const FColor& Colour, float& OutHue, float& OutSaturation, float& OutValue);
	FColor FromHSV(float Hue, float Saturation, float Value);
}

USTRUCT()
struct FChaosImpactColourPreset
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	/** BytesPerPart per part (see above). */
	UPROPERTY()
	TArray<uint8> Parts;

	/** ペイント: the pictures drawn on the body, one per layer drawn on (none: nothing drawn). */
	UPROPERTY()
	TArray<FChaosImpactPaintLayerData> Paint;
};

USTRUCT()
struct FChaosImpactColourPresetList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FChaosImpactColourPreset> Presets;
};

UCLASS()
class UChaosImpactColourSave : public USaveGame
{
	GENERATED_BODY()
public:
	/** Character index -> its presets. */
	UPROPERTY()
	TMap<int32, FChaosImpactColourPresetList> Characters;

	/** The colours last used in the editor, the newest first. */
	UPROPERTY()
	TArray<FColor> Recent;
};

/** This machine's custom colour presets (loaded once, saved on every change). */
UCLASS()
class UChaosImpactColourSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static UChaosImpactColourSubsystem* Get(const UObject* WorldContext);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** A character's presets (at most MaxPresets). */
	const TArray<FChaosImpactColourPreset>& GetPresets(int32 Character) const;
	/** Slot past the end: added. Returns the slot it went to (INDEX_NONE: full). */
	int32 SetPreset(int32 Character, int32 Slot, const FChaosImpactColourPreset& Preset);
	void RemovePreset(int32 Character, int32 Slot);
	/** A preset's parts, or empty (no such preset: the ordinary colours). */
	TArray<uint8> GetPresetParts(int32 Character, int32 Slot) const;
	/** A preset's pictures (ペイント), or none. */
	TArray<FChaosImpactPaintLayerData> GetPresetPaint(int32 Character, int32 Slot) const;
	/** The editor's 最近使った色, the newest first (MaxRecent at most). */
	const TArray<FColor>& GetRecent() const;
	void AddRecent(const FColor& Colour);
#if WITH_DEV_AUTOMATION_TESTS
	/** Tests: the recent colours put back as they were before the test. */
	void SetRecentForTests(const TArray<FColor>& Colours);
#endif

private:
	void Save() const;
	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactColourSave> Data;
};
