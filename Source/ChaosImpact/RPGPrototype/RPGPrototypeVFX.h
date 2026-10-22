#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactBallTypes.h"
#include "RPGPrototypeVFX.generated.h"

class ARPGPrototypeEffect;
class UNiagaraComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UAudioComponent;

/** Render/sound only. Never a VS hazard, never deals damage, freezes, pulls or changes collision. */
UCLASS(NotBlueprintable)
class ARPGPrototypeVFX : public AActor
{
	GENERATED_BODY()
public:
	ARPGPrototypeVFX();
	static ARPGPrototypeVFX* PlayField(ARPGPrototypeEffect* Field);
	static ARPGPrototypeVFX* PlayImpact(UWorld* World, EChaosImpactBallType Type, const FVector& At,
		float Scale=1, bool bTerminal=true);
	static ARPGPrototypeVFX* PlayLightning(UWorld* World, const FVector& Start, const FVector& End);
	virtual void Tick(float DeltaSeconds) override;
	int32 GetGeometryVertexCount() const;
	bool IsFieldVisual() const { return bField; }
	/** Drawn by the VS mode's own effect (its look-only hazard zone or tornado) rather than this actor's geometry. */
	bool UsesVersusVisual() const { return Borrowed.IsValid(); }
	/** The VS effect that draws an RPG ball's burst or field, look only (nullptr: this type keeps the RPG look). */
	static AActor* PlayVersus(UWorld* World, EChaosImpactBallType Type, FName Kind, const FVector& At, float Radius,
		float Seconds, APawn* Source=nullptr, float Scale=1);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	struct FMote { FVector Velocity; float Size, Phase; };
	TArray<FMote> Motes;
	TWeakObjectPtr<ARPGPrototypeEffect> Field;
	/** The VS zone or tornado drawing this field (it is moved along with the field and ended with it). */
	TWeakObjectPtr<AActor> Borrowed;
	EChaosImpactBallType Type=EChaosImpactBallType::Normal;
	FName Kind;
	float Radius=110, Seconds=.65f, Age=0, FadeAge=0, MeshClock=0;
	bool bField=false, bStopping=false, bTerminal=true;
	bool bChain=false;
	FVector BoltEnd=FVector::ZeroVector;
	int32 Seed=0;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Ribbons;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Debris;
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Crystals;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Surface;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Core;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RibbonLook;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DebrisLook;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Light;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Loops;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> LoopSound;
	UNiagaraComponent* AddLoop(const TCHAR* Path, FVector Offset, float Scale);
	void BuildField();
	void BuildImpact();
	void UpdateGeometry(float Fade);
	void StopLoops();
};
