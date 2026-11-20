#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactWhiteHall.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class USkeletalMeshComponent;
class USpotLightComponent;
class UMaterialInterface;
class UTexture;
class UAnimSequence;

/**
 * ホワイトボールハウスの大広間 (solo stage 30, 2026-11-19): the whole stage, built when the level starts. The grand corridor
 * the player walks in by (portraits of balls, sconces, a red runner), the great doors, and the oval audience hall: a black
 * and white marble floor, a colonnade of fluted columns down each side with the President's soldiers lined up behind them,
 * tall night windows, banners of the United States of Dett Ball, three great chandeliers, and at the far end, up the
 * steps of a dais, the throne of balls under a round stained-glass window, between red velvet curtains.
 *
 * Made of generated meshes (columns turned on a lathe, mouldings swept along the walls) in a few materials (one draw
 * each), lit by a handful of lights. The parts only the scenes' cameras should see (the ceiling, the near wall over the
 * doors, the doors themselves) are shown for the scenes alone: the game's camera looks down from behind (+X) and sees
 * the arena (X 1000-5000, Y +-2000) with the colonnade at its sides and the throne at its top.
 */
UCLASS()
class CHAOSIMPACT_API AChaosImpactWhiteHall : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactWhiteHall();
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static AChaosImpactWhiteHall* Find(const UWorld* World);

	/** The scenes: the parts only a scene's camera should see, shown or put away. */
	void SetCinematic(bool bOn);
	/** A chandelier lit (1) or out (0); -1 every one. */
	void SetChandelier(int32 Index, float Lit);
	int32 GetChandelierCount() const { return ChandelierLights.Num(); }
	FVector GetChandelier(int32 Index) const;
	/** The great doors: 0 shut, 1 wide open. */
	void SetDoors(float Open);
	/** The light on the throne, and the stained glass's glow (0-1 each). */
	void SetThroneLight(float Lit);
	void SetRoseGlow(float Glow);
	/** Half the President's health gone: the hall goes red (0-1), the chandeliers flickering. */
	void SetAlarm(float Amount);
	float GetAlarm() const { return Alarm; }
	/** Every light in the hall dimmed together (a scene's blackout): 1 as built. */
	void SetMasterLight(float Amount);

	/** Where the President sits on the throne (its seat), and which way it faces. */
	FVector GetThroneSeat() const;
	FVector GetThroneFacing() const { return FVector(-1.0f, 0.0f, 0.0f); }
	/** The great doors' middle, on the floor (the hall side). */
	FVector GetDoorway() const { return FVector(DoorX, 0.0f, 0.0f); }
	FVector GetRoseWindow() const { return FVector(FarWallX - 20.0f, 0.0f, RoseZ); }
	/** The soldiers standing in line: where each is, and its body (a scene turns and stamps them). */
	int32 GetSoldierCount() const { return Soldiers.Num(); }
	USkeletalMeshComponent* GetSoldier(int32 Index) const;
	/** All the soldiers turned toward a point (0: their rest, facing the carpet; 1: right at it). */
	void TurnSoldiers(const FVector& Toward, float Amount);
	/** All the soldiers stamping at once: a hop of Height, now. */
	void StampSoldiers(float Height);
	/** The lined soldiers put away or back. */
	void ShowSoldiers(bool bShown);
	/** A scene takes a soldier off its place (to run at the hero, to be blown away): where it is, which way, running or not. */
	void DriveSoldier(int32 Index, const FVector& At, float Yaw, bool bRunning);
	void HideSoldier(int32 Index);
	/** Every soldier back on its place, standing, shown (Except: the ones blown away stay gone). */
	void ResetSoldiers(const TArray<int32>& Gone = TArray<int32>());
	FVector GetSoldierHome(int32 Index) const { return SoldierHome.IsValidIndex(Index) ? SoldierHome[Index] : FVector::ZeroVector; }

	// The hall's measures (cm).
	static constexpr float CorridorStartX = -3400.0f;
	static constexpr float DoorX = 1000.0f;
	static constexpr float FarWallX = 7400.0f;
	static constexpr float HallHalfWidth = 3000.0f;
	static constexpr float HallHeight = 3300.0f;
	static constexpr float CorridorHalfWidth = 640.0f;
	static constexpr float CorridorHeight = 1100.0f;
	static constexpr float ColumnY = 2380.0f;
	static constexpr float ColumnHeight = 1650.0f;
	static constexpr float GalleryY = 2720.0f;
	static constexpr float DaisX = 5500.0f;
	static constexpr float ThroneX = 6650.0f;
	static constexpr float DaisTop = 240.0f;
	static constexpr float RoseZ = 2150.0f;
	static constexpr float RoseRadius = 720.0f;

private:
	void Build();
	UProceduralMeshComponent* MakePart(const FName Name, int32 Batch, bool bCollide);
	UMaterialInstanceDynamic* Surface(UTexture* Texture, const FLinearColor& Tint, float Rough, float Metal, float Glow, float Spec = 0.5f);
	UPointLightComponent* AddPoint(const FVector& At, const FLinearColor& Color, float Candela, float Radius);
	void UpdateLights();

	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> CinematicParts;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> ChandelierBulbs;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ChandelierLooks;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> ChandelierLights;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> OtherLights;
	UPROPERTY() TObjectPtr<USpotLightComponent> ThroneSpot;
	UPROPERTY() TObjectPtr<USpotLightComponent> RoseSpot;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RoseLook;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> WindowLook;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SconceLook;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShaftLook;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> DoorLeft;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> DoorRight;
	UPROPERTY() TObjectPtr<USceneComponent> DoorLeftHinge;
	UPROPERTY() TObjectPtr<USceneComponent> DoorRightHinge;
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Soldiers;
	UPROPERTY() TArray<TObjectPtr<class UChaosImpactPuppetComponent>> SoldierRobos;
	void SetSoldierShown(int32 Index, bool bShown);
	TArray<FRotator> SoldierRest;
	TArray<FVector> SoldierHome;
	TArray<float> SoldierHop;
	TArray<bool> SoldierDriven;
	UPROPERTY() TObjectPtr<UAnimSequence> SoldierIdle;
	UPROPERTY() TObjectPtr<UAnimSequence> SoldierRun;
	TArray<FVector> ChandelierSpots;
	TArray<float> ChandelierLit;
	TArray<FLinearColor> OtherLightColors;
	TArray<float> OtherLightCandela;
	float ThroneLit = 1.0f;
	float RoseGlow = 1.0f;
	float Alarm = 0.0f;
	float Master = 1.0f;
	float Clock = 0.0f;
	bool bCinematic = false;
	bool bBuilt = false;
};
