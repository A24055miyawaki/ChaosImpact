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
	/** カスタムカラー on the model shown (one value per part, ChaosImpactCustomColour; empty: none). */
	void ShowCustomColours(const TArray<uint8>& Parts);
	/** A short burst of motion when the player locks in. */
	void PlayReady();
	UTextureRenderTarget2D* GetPicture() const { return Picture; }
	/**
	 * An icon face: films into a picture half as wide and tall (an icon shows a small part of it) and only when
	 * asked (FilmNextFrame), not every frame. Call before taking GetPicture().
	 */
	void UseAsPortrait();
	/** Films one frame (a portrait is filmed in turns, not every frame). */
	void FilmNextFrame();
	/**
	 * Films now, on its own: for the select screen, which stops drawing the level behind it (the deferred filming
	 * rides on the level's drawing). Also turns off filming every frame, so the caller films each frame it wants.
	 */
	void FilmNow();
	/**
	 * Stops filming and hands the picture over, moved out of this actor's level: it keeps its last frame and outlives
	 * the level (the VS card shows it while the next level loads). The caller keeps it referenced.
	 */
	UTextureRenderTarget2D* ReleasePicture();

	/**
	 * Advances the idle (or ready) motion and the stage's sway. The select screen calls this every frame: menus pause
	 * the world, and paused worlds do not animate skeletal meshes on their own.
	 */
	void Animate(float DeltaSeconds);

	/**
	 * Results podium: the model only, posed in someone else's scene (their camera and lights). Its own camera, stage
	 * and lights are switched off.
	 */
	void UseAsFigure();
	/** Plays an animation on the model; HoldAt >= 0 stops it on that moment (a pose held). */
	void PlayFigureAnimation(UAnimSequenceBase* Animation, bool bLoop, float HoldAt = -1.0f);
	/** Which way the model faces (0: toward +Y) and how far it sways about that. */
	void SetFacing(float Yaw, float Sway);
	/** The winner's celebration on the podium (ChaosImpactVictoryMotion), from now on (or stopped). */
	void SetVictoryMotion(bool bOn);
	/** The model as it is now (to hide or tint). */
	class UChaosImpactPuppetComponent* GetModel() const { return Model; }

	/** ペイント over the model's colours (one picture per layer, UChaosImpactPuppetComponent::SetPaintLayers; empty: none). */
	void ShowPaint(const TArray<UTexture*>& Pictures);
	/** This machine's preset's ペイント (UChaosImpactPaintCache), or the pictures of a key (none while they are not here). */
	void ShowPresetPaint(int32 Character, int32 Preset);
	void ShowPaintKey(int32 Key);
	/**
	 * ペイント's view: the model held still in its pose (facing +Y), filmed into a picture Width x Height by a camera that
	 * goes round it (SetPaintView). The stage is put away and the lights go round with the camera. EndPaintView puts it all back.
	 */
	void BeginPaintView(int32 Width, int32 Height);
	void EndPaintView();
	bool IsPaintView() const { return bPaintView; }
	/** Yaw and pitch in degrees (yaw 90: in front), Zoom 1 the whole model, Pan in world units across and up the picture. */
	void SetPaintView(float Yaw, float Pitch, float Zoom, const FVector2D& Pan);
	/** Where the camera turns about (the middle of what it films). */
	void SetPaintFocus(const FVector& Centre, float Radius);
	/** The ray from the camera through a point of the picture (0..1 across and down). */
	void GetPictureRay(const FVector2D& PictureUV, FVector& OutOrigin, FVector& OutDirection) const;
	/** Where a world point is on the picture (0..1 across and down); false behind the camera. */
	bool ProjectToPicture(const FVector& World, FVector2D& OutPictureUV) const;

protected:
	virtual void BeginPlay() override;

private:
	void RebuildModel(int32 CharacterIndex);
	void ReturnToIdle();
	/**
	 * The model's textures at full resolution. The streamer sizes textures by distance from the player's camera,
	 * and this stage is filmed far off the level: left to it, the model is drawn with its lowest, blurred mips.
	 */
	void KeepTexturesSharp();

	UPROPERTY()
	TObjectPtr<USceneComponent> Stage;

	/** Hidden mannequin whose idle and throw animations the model copies. */
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Pose;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Floor;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FloorRing;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Backdrop;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FillLight;

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
	TObjectPtr<UMaterialInstanceDynamic> FloorRingMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackdropMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> IdleAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> ReadyAnimation;

	int32 ModelCharacter = INDEX_NONE;
	double ReadyEndsAt = 0.0;
	float FacingYaw = -22.0f;
	float SwayAmount = 14.0f;
	/** A held pose: stop the animation when this much of it has played (negative: none). */
	float HoldAnimationAt = -1.0f;
	float AnimationStartedAge = 0.0f;
	bool bHolding = false;
	float Age = 0.0f;
	bool bVictoryMotion = false;
	float VictoryStartedAge = 0.0f;
	float NextVictoryAppeal = 0.0f;
	float Spin = 0.0f;
	float SpinKick = 0.0f;

	// ペイント's view.
	bool bPaintView = false;
	FVector PaintFocus = FVector::ZeroVector;
	float PaintRadius = 100.0f;
	FVector SavedCameraLocation = FVector::ZeroVector;
	FRotator SavedCameraRotation = FRotator::ZeroRotator;
	float SavedFOV = 30.0f;
	float SavedFacing = -22.0f;
	FVector SavedBackdropLocation = FVector::ZeroVector;
	FRotator SavedBackdropRotation = FRotator::ZeroRotator;
	FVector SavedBackdropScale = FVector::OneVector;
	FVector SavedLights[3];
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture>> PaintLayers;
};
