#include "ChaosImpactCustomColour.h"

#include "ChaosImpactCharacterRoster.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const TCHAR* const SlotName = TEXT("CustomColours");

	/** In roster order; each list as its materials divide the model (Scripts/Characters/CustomColour/parts.py; the names here in the same order). */
	const TArray<TArray<FString>>& AllPartNames()
	{
		static const TArray<TArray<FString>> Names =
		{
			{TEXT("帽子"), TEXT("体"), TEXT("顔・腕・脚"), TEXT("Sマーク"), TEXT("手・くつ・目と口")},
			{TEXT("羽毛"), TEXT("パンツ"), TEXT("目・くちばし")},
			{TEXT("頭"), TEXT("体"), TEXT("目・口")},
			{TEXT("葉っぱ"), TEXT("体（木の皮）")},
			{TEXT("頭・脚"), TEXT("おなか・目"), TEXT("胸・しょっかく・足先")},
			{TEXT("せなか"), TEXT("おなか"), TEXT("ヒレ")},
			{TEXT("スーツ"), TEXT("手袋・ブーツ・ベルト"), TEXT("バイザー")},
			{TEXT("胴体・手のつめ"), TEXT("頭・肩・手首"), TEXT("腕・脚"), TEXT("顔の画面・アンテナ・足")},
			{TEXT("顔"), TEXT("手"), TEXT("目・まゆ・口")},
			{TEXT("はだ"), TEXT("シャツ・くつ下"), TEXT("ズボン"), TEXT("かみ"), TEXT("服の絵・ロゴ"), TEXT("目・口")},
			{TEXT("カップ"), TEXT("手・くつ"), TEXT("目"), TEXT("腕・脚"), TEXT("コーヒー")},
			{TEXT("髪"), TEXT("はだ"), TEXT("ズボン"), TEXT("シャツ"), TEXT("くつ"), TEXT("目")},
			{TEXT("体の毛"), TEXT("顔・手足の先・もよう"), TEXT("耳"), TEXT("目・鼻・口")},
			{TEXT("体"), TEXT("くちばし・足"), TEXT("目・頭の毛")},
		};
		return Names;
	}
}

void ChaosImpactCustomColour::ToHSV(const FColor& Colour, float& OutHue, float& OutSaturation, float& OutValue)
{
	const float R = Colour.R / 255.0f, G = Colour.G / 255.0f, B = Colour.B / 255.0f;
	const float Max = FMath::Max3(R, G, B), Min = FMath::Min3(R, G, B), Range = Max - Min;
	OutValue = Max;
	OutSaturation = Max > 0.0f ? Range / Max : 0.0f;
	float Hue = 0.0f;
	if (Range > 0.0f)
	{
		Hue = Max == R ? FMath::Fmod((G - B) / Range, 6.0f) : Max == G ? (B - R) / Range + 2.0f : (R - G) / Range + 4.0f;
	}
	OutHue = FMath::Fmod(Hue * 60.0f + 360.0f, 360.0f);
}

FColor ChaosImpactCustomColour::FromHSV(const float Hue, const float Saturation, const float Value)
{
	const float H = FMath::Fmod(FMath::Fmod(Hue, 360.0f) + 360.0f, 360.0f) / 60.0f;
	const float S = FMath::Clamp(Saturation, 0.0f, 1.0f), V = FMath::Clamp(Value, 0.0f, 1.0f);
	const float C = V * S, X = C * (1.0f - FMath::Abs(FMath::Fmod(H, 2.0f) - 1.0f)), M = V - C;
	float R = 0.0f, G = 0.0f, B = 0.0f;
	switch (static_cast<int32>(H))
	{
	case 0: R = C; G = X; break;
	case 1: R = X; G = C; break;
	case 2: G = C; B = X; break;
	case 3: G = X; B = C; break;
	case 4: R = X; B = C; break;
	default: R = C; B = X; break;
	}
	return FColor(FMath::RoundToInt((R + M) * 255.0f), FMath::RoundToInt((G + M) * 255.0f), FMath::RoundToInt((B + M) * 255.0f));
}

FColor ChaosImpactCustomColour::GetPaletteSwatch(const int32 Column, const int32 Row)
{
	const int32 Shade = FMath::Clamp(Row, 0, PaletteRows - 1);
	if (Column >= PaletteColumns - 1)
	{
		// White to black.
		const uint8 Grey = static_cast<uint8>(FMath::RoundToInt(255.0f * (1.0f - Shade / static_cast<float>(PaletteRows - 1))));
		return FColor(Grey, Grey, Grey);
	}
	// Pale and bright at the top, deep and dark at the bottom.
	static const float Saturations[PaletteRows] = {0.3f, 0.55f, 0.85f, 1.0f, 1.0f, 1.0f};
	static const float Values[PaletteRows] = {1.0f, 1.0f, 0.97f, 0.8f, 0.58f, 0.36f};
	return FromHSV(FMath::Clamp(Column, 0, PaletteColumns - 2) * 30.0f, Saturations[Shade], Values[Shade]);
}

const TArray<FString>& ChaosImpactCustomColour::GetPartNames(const int32 Character)
{
	static const TArray<FString> None;
	const TArray<TArray<FString>>& All = AllPartNames();
	return All.IsValidIndex(Character) ? All[Character] : None;
}

int32 ChaosImpactCustomColour::GetPartCount(const int32 Character)
{
	return FMath::Min(GetPartNames(Character).Num(), MaxParts);
}

bool ChaosImpactCustomColour::GetPart(const TArray<uint8>& Parts, const int32 Part, FColor& OutColour)
{
	const int32 At = Part * BytesPerPart;
	if (Part < 0 || At + BytesPerPart > Parts.Num() || Parts[At] == 0)
	{
		return false;
	}
	OutColour = FColor(Parts[At + 1], Parts[At + 2], Parts[At + 3]);
	return true;
}

void ChaosImpactCustomColour::SetPart(TArray<uint8>& Parts, const int32 Part, const bool bOn, const FColor& Colour)
{
	if (Part < 0 || Part >= MaxParts)
	{
		return;
	}
	if (Parts.Num() < (Part + 1) * BytesPerPart)
	{
		Parts.SetNumZeroed((Part + 1) * BytesPerPart);
	}
	const int32 At = Part * BytesPerPart;
	Parts[At] = bOn ? 1 : 0;
	Parts[At + 1] = bOn ? Colour.R : 0;
	Parts[At + 2] = bOn ? Colour.G : 0;
	Parts[At + 3] = bOn ? Colour.B : 0;
}

TArray<uint8> ChaosImpactCustomColour::Clean(const int32 Character, const TArray<uint8>& Parts)
{
	TArray<uint8> Out;
	Out.SetNumZeroed(GetPartCount(Character) * BytesPerPart);
	for (int32 Part = 0; Part < GetPartCount(Character); ++Part)
	{
		FColor Colour;
		if (GetPart(Parts, Part, Colour))
		{
			SetPart(Out, Part, true, Colour);
		}
	}
	return Out;
}

TArray<FLinearColor> ChaosImpactCustomColour::ToPartColours(const int32 Character, const TArray<uint8>& Parts)
{
	TArray<FLinearColor> Out;
	for (int32 Part = 0; Part < GetPartCount(Character); ++Part)
	{
		FColor Colour;
		const bool bOn = GetPart(Parts, Part, Colour);
		// The material works in linear colour; the editor's swatches are sRGB.
		FLinearColor Linear = bOn ? FLinearColor(Colour) : FLinearColor::White;
		Linear.A = bOn ? 1.0f : 0.0f;
		Out.Add(Linear);
	}
	return Out;
}

UChaosImpactColourSubsystem* UChaosImpactColourSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UChaosImpactColourSubsystem>() : nullptr;
}

void UChaosImpactColourSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Data = Cast<UChaosImpactColourSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Data)
	{
		Data = Cast<UChaosImpactColourSave>(UGameplayStatics::CreateSaveGameObject(UChaosImpactColourSave::StaticClass()));
	}
}

const TArray<FChaosImpactColourPreset>& UChaosImpactColourSubsystem::GetPresets(const int32 Character) const
{
	static const TArray<FChaosImpactColourPreset> None;
	const FChaosImpactColourPresetList* List = Data ? Data->Characters.Find(Character) : nullptr;
	return List ? List->Presets : None;
}

int32 UChaosImpactColourSubsystem::SetPreset(const int32 Character, const int32 Slot, const FChaosImpactColourPreset& Preset)
{
	if (!Data || Character < 0 || Character >= ChaosImpactRoster::Num())
	{
		return INDEX_NONE;
	}
	TArray<FChaosImpactColourPreset>& Presets = Data->Characters.FindOrAdd(Character).Presets;
	FChaosImpactColourPreset Cleaned;
	Cleaned.Name = Preset.Name.Left(ChaosImpactCustomColour::MaxNameLength);
	Cleaned.Parts = ChaosImpactCustomColour::Clean(Character, Preset.Parts);
	int32 Index = Slot;
	if (Presets.IsValidIndex(Slot))
	{
		Presets[Slot] = Cleaned;
	}
	else if (Presets.Num() < ChaosImpactCustomColour::MaxPresets)
	{
		Index = Presets.Add(Cleaned);
	}
	else
	{
		return INDEX_NONE;
	}
	Save();
	return Index;
}

void UChaosImpactColourSubsystem::RemovePreset(const int32 Character, const int32 Slot)
{
	if (FChaosImpactColourPresetList* List = Data ? Data->Characters.Find(Character) : nullptr; List && List->Presets.IsValidIndex(Slot))
	{
		List->Presets.RemoveAt(Slot);
		Save();
	}
}

TArray<uint8> UChaosImpactColourSubsystem::GetPresetParts(const int32 Character, const int32 Slot) const
{
	const TArray<FChaosImpactColourPreset>& Presets = GetPresets(Character);
	return Presets.IsValidIndex(Slot) ? ChaosImpactCustomColour::Clean(Character, Presets[Slot].Parts) : TArray<uint8>();
}

const TArray<FColor>& UChaosImpactColourSubsystem::GetRecent() const
{
	static const TArray<FColor> None;
	return Data ? Data->Recent : None;
}

void UChaosImpactColourSubsystem::AddRecent(const FColor& Colour)
{
	if (!Data || (!Data->Recent.IsEmpty() && Data->Recent[0] == Colour))
	{
		return;
	}
	Data->Recent.Remove(Colour);
	Data->Recent.Insert(Colour, 0);
	if (Data->Recent.Num() > ChaosImpactCustomColour::MaxRecent)
	{
		Data->Recent.SetNum(ChaosImpactCustomColour::MaxRecent);
	}
	Save();
}

void UChaosImpactColourSubsystem::Save() const
{
	if (Data)
	{
		UGameplayStatics::SaveGameToSlot(Data, SlotName, 0);
	}
}
