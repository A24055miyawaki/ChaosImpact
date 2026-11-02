#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RPGNavigation.generated.h"

class ARPGPrototypeCharacter;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FGeometry;
class FSlateWindowElementList;

/**
 * Finding the way on the adventure map: where a quest wants a player to go next, and the shortest way there along the
 * roads (RPGWorldLayout's roads and areas, any warp gates). The followed quest's way is
 * laid on the ground in front of its player (ARPGQuestGuide, N / R3 turns it on and off) and drawn on the map: a
 * minimap in the corner and the whole map over the screen (M / D-pad right), both with every player in the world on it.
 */
namespace RPGNav
{
	/** Every place that would do for the quest's next step (the way picks the nearest by road), with what it is. */
	struct FGoal
	{
		TArray<FVector> Candidates;
		TArray<FString> Names;
		FString Label;
	};
	/** The quest the guide follows: the tracked one, else the first one being done (INDEX_NONE: none). */
	int32 GuideQuest(const ARPGPrototypeCharacter* Player);
	/** Where the quest leads this player now: the next thing to do, or the person to report to. False: nowhere. */
	bool QuestGoals(const ARPGPrototypeCharacter* Player, int32 Quest, FGoal& Out);
	/** No quest under way: the people and the board with something to offer. */
	bool OfferGoals(const ARPGPrototypeCharacter* Player, FGoal& Out);
	/**
	 * The shortest way by road from From to the nearest of Candidates (points from From to that candidate). Prefer: the
	 * goal chosen last time, kept unless another is clearly nearer (so the line does not flick between two). Empty: none.
	 */
	TArray<FVector> ShortestWay(const UWorld* World, const FVector& From, const TArray<FVector>& Candidates, int32* OutIndex = nullptr,
		const FVector* Prefer = nullptr);
	/** The length of a way along the ground (2D). */
	float WayLength(const TArray<FVector>& Way);
}

namespace RPGMap
{
	/** The minimap in the top right corner (design units: the HUD's W x H). */
	void PaintMini(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad);
	/** The surroundings over the middle of the screen, see-through, moving with the player (who plays on). */
	void PaintNearby(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad);
	/** The whole map over the screen, half see-through. */
	void PaintFull(const FGeometry& Design, FSlateWindowElementList& Elements, int32 Layer, float W, float H, const ARPGPrototypeCharacter* Player, float Time, bool bPad);
}

/**
 * The followed quest's way as a line of small lights on the ground running ahead of its player (the next stretch of
 * the way). Seen only by that player (on a shared screen by everyone, in that player's colour). Made by the character.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class ARPGQuestGuide : public AActor
{
	GENERATED_BODY()
public:
	ARPGQuestGuide();
	/** The way to lay (empty: none). Its points are put down on the ground here. */
	void ShowRoute(const TArray<FVector>& Route);
	void SetColor(const FLinearColor& InColor);
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Lights;
	UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Look;
	/** The way's first stretch on the ground, a light every few steps, and which way each one points. */
	TArray<FVector> Path;
	TArray<float> Yaws;
	float Clock = 0;
	FLinearColor Color = FLinearColor(0.25f, 0.9f, 1.0f, 1.0f);
};
