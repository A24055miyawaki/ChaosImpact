#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactCharacterPreview.generated.h"

class UAnimSequenceBase;
class UChaosImpactPuppetComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * One player's card picture on the character select screen: the chosen character in its colour, idling on a
 * small lit stage far outside the level, filmed into a texture the menu draws. Menus pause the world, so every
 * part of it keeps running while paused.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactCharacterPreview : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactCharacterPreview();

	static constexpr int32 PictureWidth = 600;
	static constexpr int32 PictureHeight = 800;

	/** Shows this character and colour; Accent tints the stage lights (the player's slot colour). */
	void ShowLoadout(int32 CharacterIndex, int32 Colour, const FLinearColor& Accent);
	/** A short burst of motion when the player locks in. */
	void PlayReady();
	UTextureRenderTarget2D* GetPicture() const { return Picture; }

	/**
	 * Advances the idle (or ready) motion and the stage's sway. The select screen calls this every frame: menus pause
	 * the world, and paused worlds do not animate skeletal meshes on their own.
	 */
	void Animate(float DeltaSeconds);

protected:
	virtual void BeginPlay() override;

private:
	void RebuildModel(int32 CharacterIndex);
	void ReturnToIdle();

	UPROPERTY()
	TObjectPtr<USceneComponent> Stage;

	/** Hidden mannequin whose idle and throw animations the model copies. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Pose;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Floor;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UChaosImpactPuppetComponent> Model;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Picture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FloorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackdropMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> ReadyAnimation;

	int32 ModelCharacter = INDEX_NONE;
	double ReadyEndsAt = 0.0;
	float Age = 0.0f;
	float Spin = 0.0f;
	float SpinKick = 0.0f;
};
