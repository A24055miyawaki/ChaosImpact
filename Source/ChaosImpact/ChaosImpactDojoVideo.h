#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "ChaosImpactDojoVideo.generated.h"

class UMediaPlayer;
class UMediaTexture;

/**
 * 訓練場's slide clips, played from Content/Movies/Dojo/<clip>.mp4 (filmed from the game: ChaosImpactDojoRecord.cpp).
 * One player for the dojo: the slide being read, or the lesson picked in the list. Asking for another clip switches
 * after a moment's settling (scrolling down the list does not open every clip on the way).
 */
UCLASS()
class UChaosImpactDojoVideo : public UObject
{
	GENERATED_BODY()

public:
	/** The clip wanted now (empty: none). */
	void Want(const FString& Clip);
	void Tick();
	/** The picture, once the clip has frames to show (null before: draw something else meanwhile). */
	const FSlateBrush* GetBrush() const;
	/** How far into the clip, 0-1. */
	float GetProgress() const;
	static bool HasClip(const FString& Clip);

private:
	static FString ClipPath(const FString& Clip);

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> Player;
	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> Texture;
	FSlateBrush Brush;
	FString Wanted;
	FString Open;
	double WantedAt = 0.0;
};
