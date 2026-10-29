#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaosImpactFxKit.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** How a piece of an effect is drawn. */
enum class EChaosImpactFxLook : uint8
{
	/** Additive soft glow, turned to the camera (a flash, a fireball, a spark's halo). */
	Glow,
	/** Additive soft glow lying on the ground (heat, a scorch's glow, an aura underfoot). */
	GroundGlow,
	/** Additive shock ring lying on the ground. */
	Ring,
	/** Additive shock ring turned to the camera (a burst in the air). */
	AirRing,
	/** Additive streak along its velocity (sparks, embers, speed lines). */
	Streak,
	/** Translucent smoke puff turned to the camera. */
	Smoke,
	/** Translucent glow turned to the camera: holds its colour on any floor (fireballs, flames, solid-coloured bursts). */
	Flame,
	/** Translucent glow lying on the ground. */
	GroundFlame,
	/** Additive whirlpool lying flat: arms twisting into the middle, streaming inward (a black hole's pull). */
	Spiral,
	/** Translucent shock ring lying on the ground: shows on white snow, where added light does not. */
	SolidRing,
};

/**
 * One piece: where it starts and how it moves, how big it is over its life, and how bright. Sizes are the plane's
 * width in centimetres (a streak's is its length; its thickness is Width). Times are in seconds from the burst.
 */
struct FChaosImpactFxPiece
{
	EChaosImpactFxLook Look = EChaosImpactFxLook::Glow;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** Pulls the velocity down (cm/s^2); negative floats it up. */
	float Gravity = 0.0f;
	/** Slows it down (per second). */
	float Drag = 0.0f;
	float Delay = 0.0f;
	float Life = 0.5f;
	float SizeFrom = 100.0f;
	float SizeTo = 200.0f;
	/** A streak's thickness, or a glow's height relative to its width (1 is round; 2.5 a tall flame). */
	float Width = 1.0f;
	FLinearColor Color = FLinearColor(1.0f, 0.4f, 0.1f);
	FLinearColor CoreColor = FLinearColor(1.0f, 0.95f, 0.8f);
	float Intensity = 4.0f;
	/** The share of its life spent brightening (the rest fades with イージーイーズ). */
	float Attack = 0.08f;
	/** Up to this share of its life it stays at full brightness before fading (0: fades right after the attack). */
	float Hold = 0.0f;
	/** How far it is eaten away at the start and at the end of its life. */
	float ErodeFrom = 0.0f;
	float ErodeTo = 0.85f;
	float NoiseScale = 1.3f;
	float NoiseSpeed = 0.35f;
	float NoiseAmount = 0.55f;
	float Softness = 1.0f;
	/** Ring only: the band's radius and width within the plane (1 is its edge). */
	float RingFrom = 0.25f;
	float RingTo = 0.92f;
	float RingWidth = 0.1f;
	/** Smoke and flames: how opaque at the thickest (Intensity is then their brightness, 1 as coloured). */
	float Opacity = 0.75f;
	/** Turns about the view as it lives (degrees per second): breaks up the same shape repeating. */
	float Spin = 0.0f;
	/** A flicker on the brightness (0 none, 1 strong): flames, sparks. */
	float Flicker = 0.0f;
	/** Glow only: 1 makes it a flame (full at the bottom, thinning to nothing at the top, the noise rising). */
	float Taper = 0.0f;
	/** Flame looks only: 1 is cel-shaded (crisp bands of edge, middle and core colour), 0 soft. */
	float Toon = 0.0f;
	/** Spiral only: how many arms, how far they twist from rim to eye, how fast they wheel (turns a second; the sign
	 * picks the way round), and how thin the arms are. */
	float Arms = 4.0f;
	float Twist = 1.6f;
	float Swirl = 0.6f;
	float Sharpness = 2.5f;
};

/**
 * An effect drawn on this machine only, out of soft pieces (M_CIFX_* materials on flat planes): a burst once, and/or
 * emitters that keep making pieces for a while. Pieces reuse a small pool of planes, and the actor goes when the last
 * one has faded. Nothing here touches gameplay.
 */
UCLASS(NotPlaceable, Transient)
class AChaosImpactFxKit : public AActor
{
	GENERATED_BODY()

public:
	AChaosImpactFxKit();

	/** A new effect at Location (nothing is drawn until pieces are added). Null on a dedicated server. */
	static AChaosImpactFxKit* Spawn(UObject* WorldContext, const FVector& Location);

	/** Adds one piece (Location is relative to the effect). */
	void Add(const FChaosImpactFxPiece& Piece);

	/**
	 * Keeps making pieces like Template for Seconds: Rate a second, each from a random spot within Radius of the
	 * effect (on the ground plane), its velocity varied by up to VelocityJitter and its size by SizeJitter.
	 */
	void AddEmitter(const FChaosImpactFxPiece& Template, float Rate, float Seconds, float Radius,
		const FVector& VelocityJitter = FVector::ZeroVector, float SizeJitter = 0.25f);

	/** Ends every emitter now (their pieces still fade out as they would). */
	void StopEmitters();

	/** Scales every piece made from now on (a bigger or smaller version of the same effect). */
	float Scale = 1.0f;
	/** A random stream for the jitter (seeded per effect so each looks a little different). */
	FRandomStream Stream;

	virtual void Tick(float DeltaSeconds) override;
	/** How many pieces are alive (waiting or drawn). */
	int32 GetPieceCount() const { return Pieces.Num(); }

private:
	struct FLive
	{
		FChaosImpactFxPiece Piece;
		float Age = 0.0f;
		int32 Slot = INDEX_NONE;
		float Seed = 0.0f;
	};
	struct FEmitter
	{
		FChaosImpactFxPiece Template;
		float Rate = 0.0f;
		float Left = 0.0f;
		float Radius = 0.0f;
		FVector VelocityJitter = FVector::ZeroVector;
		float SizeJitter = 0.0f;
		float Owed = 0.0f;
	};
	struct FSlot
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		EChaosImpactFxLook Look = EChaosImpactFxLook::Glow;
		bool bBusy = false;
	};

	int32 TakeSlot(EChaosImpactFxLook Look);
	void Draw(FLive& Live, const FVector& Camera);

	TArray<FLive> Pieces;
	TArray<FEmitter> Emitters;
	TArray<FSlot> Slots;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> KeepAlive;
	float Age = 0.0f;
};
