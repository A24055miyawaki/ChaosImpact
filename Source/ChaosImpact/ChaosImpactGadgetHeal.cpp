// 回復スポット (2026-11-19): what the heal ball (solo mode only) leaves where it lands. A small pink circle on the floor
// with a white heart in it and sparkles rising. Its thrower (or their team) stepping in, hurt, gets 1 back at once; standing
// on, a white arc fills round its edge, and when it closes, 1 more and the spot is gone (two heals each). Someone at full
// health is not healed and does not use it up; nobody coming, it fades after HealSeconds.
#include "ChaosImpactGadget.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSfx.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FLinearColor HealPink(1.0f, 0.5f, 0.72f, 1.0f);
	const FLinearColor HealDeep(1.0f, 0.22f, 0.52f, 1.0f);
	const FLinearColor HealWhite(1.0f, 0.97f, 0.99f, 1.0f);
	constexpr int32 HealRingSegments = 28;
	// The floor heart (on the pivot, +X up the screen): the square's side, where it and the two lobes sit.
	constexpr float HeartSide = 70.0f;
	constexpr float HeartSquareX = -4.0f;
	constexpr float HeartLobeX = HeartSquareX + HeartSide * 0.35355f;
	constexpr float HeartLobeY = HeartSide * 0.35355f;
}

bool AChaosImpactGadget::CanHeal(const AChaosImpactCharacter* Who) const
{
	if (!IsValid(Who) || Who->IsEliminated() || Who->IsHidden() || (Who->GetBallForm() && Who->GetBallForm()->IsCaptured()))
	{
		return false;
	}
	return Who == Source || AChaosImpactGameState::AreTeammates(GetWorld(), Source, Who);
}

void AChaosImpactGadget::ServerTickHeal(const double Now, const float DeltaSeconds)
{
	if (Now >= State.PlacedAt + HealSeconds)
	{
		End(false);
		return;
	}
	if (Now < State.PlacedAt + HealArmSeconds)
	{
		return;
	}
	// Someone of its side, hurt, standing on it (the most hurt first).
	AChaosImpactCharacter* Hurting = nullptr;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Who = *It;
		if (CanHeal(Who) && Who->GetHealth() < Who->GetMaxHealth() - KINDA_SMALL_NUMBER && IsInReach(Who, HealRadius)
			&& (!Hurting || Who->GetHealth() < Hurting->GetHealth()))
		{
			Hurting = Who;
		}
	}
	if (!Hurting)
	{
		return;
	}
	if (State.Heals == 0)
	{
		// Stepping in: 1 at once.
		GiveHeal(Hurting, Now);
		return;
	}
	// Staying on: the arc fills; closed, 1 more and it is used up.
	HealStay += DeltaSeconds;
	const uint8 Percent = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(HealStay / HealStaySeconds * 100.0f), 0, 100));
	if (Percent / 5 != State.StayPercent / 5)
	{
		State.StayPercent = Percent;
		ForceNetUpdate();
	}
	State.StayPercent = Percent;
	if (HealStay >= HealStaySeconds)
	{
		GiveHeal(Hurting, Now);
	}
}

void AChaosImpactGadget::GiveHeal(AChaosImpactCharacter* Who, const double Now)
{
	const float Won = Who->RestoreHealth(1.0f);
	State.Heals = static_cast<uint8>(FMath::Min<int32>(State.Heals + 1, HealsPerSpot));
	State.Healed = Who;
	State.TriggeredAt = Now;
	HealStay = 0.0f;
	State.StayPercent = State.Heals >= HealsPerSpot ? 100 : 0;
	UE_LOG(LogChaosImpact, Log, TEXT("Heal spot healed %s by %.0f (heal %d of %d)"), *Who->GetName(), Won, State.Heals, HealsPerSpot);
	OnRep_State();
	ForceNetUpdate();
	if (State.Heals >= HealsPerSpot)
	{
		// Used up: it blooms away (bBurst: used, not run out).
		End(true);
	}
}

void AChaosImpactGadget::BuildHealLook()
{
	using namespace ChaosImpactBallTypes;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	// (The heart and the stay's arc on a pivot turned to the camera: the heart reads upright, the arc fills from the top.)
	HealPivot = NewObject<USceneComponent>(this);
	HealPivot->SetupAttachment(GetRootComponent());
	HealPivot->SetUsingAbsoluteRotation(true);
	HealPivot->RegisterComponent();
	const auto Part = [this](UStaticMesh* Mesh, UMaterialInterface* Look, const FVector& Center, const FVector& Size, const float Yaw = 0.0f,
		USceneComponent* Parent = nullptr)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetStaticMesh(Mesh);
		Piece->SetMaterial(0, Look);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		Piece->SetCanEverAffectNavigation(false);
		Piece->SetupAttachment(Parent ? Parent : GetRootComponent());
		Piece->RegisterComponent();
		Piece->SetRelativeLocationAndRotation(Center, FRotator(0.0f, Yaw, 0.0f));
		Piece->SetRelativeScale3D(Size / 100.0f);
		Parts.Add(Piece);
		return Parts.Num() - 1;
	};
	// A soft pink fill, the edge as a ring of short solid pink bars (it reads on white snow as on a dark floor), a white
	// arc inside it for the stay (UpdateHealLook shows as much of it as has filled), and a flat white heart in the middle.
	GlowMaterial = MakeAdditive(this, HealPink, 0.1f, 0.0f);
	BodyMaterial = MakeEmissive(this, HealDeep, 1.6f);
	UMaterialInstanceDynamic* White = MakeEmissive(this, HealWhite, 2.2f);
	HealFillPart = Part(Cylinder, GlowMaterial, FVector(0.0f, 0.0f, 1.5f), FVector(HealRadius * 2.0f, HealRadius * 2.0f, 1.0f));
	const float Bar = UE_TWO_PI * HealRadius / HealRingSegments * 0.78f;
	HealRingFrom = Parts.Num();
	for (int32 Index = 0; Index < HealRingSegments; ++Index)
	{
		const float Angle = UE_TWO_PI * (Index + 0.5f) / HealRingSegments;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		Part(Cube, BodyMaterial, Out * HealRadius + FVector(0.0f, 0.0f, 2.5f), FVector(Bar, 9.0f, 3.0f),
			FMath::RadiansToDegrees(Angle) + 90.0f);
	}
	HealStayPart = Parts.Num();
	const float Inner = HealRadius - 16.0f;
	const float InnerBar = UE_TWO_PI * Inner / HealRingSegments * 0.8f;
	for (int32 Index = 0; Index < HealRingSegments; ++Index)
	{
		// (On the pivot: +X up the screen, +Y right; from the top, clockwise.)
		const float Angle = UE_TWO_PI * (Index + 0.5f) / HealRingSegments;
		const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		const int32 Made = Part(Cube, White, Out * Inner + FVector(0.0f, 0.0f, 3.0f), FVector(InnerBar, 7.0f, 3.0f),
			FMath::RadiansToDegrees(Angle) + 90.0f, HealPivot);
		Parts[Made]->SetVisibility(false);
	}
	// The heart: a square turned on its corner (its point down the screen) and a round lobe on each upper side.
	HealHeartFrom = Parts.Num();
	Part(Cube, White, FVector(HeartSquareX, 0.0f, 4.0f), FVector(HeartSide, HeartSide, 3.0f), 45.0f, HealPivot);
	Part(Cylinder, White, FVector(HeartLobeX, -HeartLobeY, 4.0f), FVector(HeartSide, HeartSide, 3.0f), 0.0f, HealPivot);
	Part(Cylinder, White, FVector(HeartLobeX, HeartLobeY, 4.0f), FVector(HeartSide, HeartSide, 3.0f), 0.0f, HealPivot);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::HealSpot, GetActorLocation() + FVector(0.0f, 0.0f, 30.0f));
	if (AChaosImpactFxKit* Kit = GetFx())
	{
		// Opening: a ring of pink out to its edge, and a glimmer.
		FChaosImpactFxPiece Open;
		Open.Look = EChaosImpactFxLook::SolidRing;
		Open.Location = FVector(0.0f, 0.0f, 4.0f);
		Open.Life = 0.45f;
		Open.SizeFrom = 60.0f;
		Open.SizeTo = HealRadius * 2.0f / 0.92f;
		Open.RingFrom = Open.RingTo = 0.92f;
		Open.RingWidth = 0.06f;
		Open.Color = HealPink;
		Open.CoreColor = HealWhite;
		Open.Intensity = 1.4f;
		Open.Opacity = 0.9f;
		Open.ErodeTo = 0.3f;
		Open.NoiseAmount = 0.0f;
		Kit->Add(Open);
		FChaosImpactFxPiece Glint;
		Glint.Location = FVector(0.0f, 0.0f, 30.0f);
		Glint.Life = 0.3f;
		Glint.SizeFrom = 160.0f;
		Glint.SizeTo = 60.0f;
		Glint.Color = HealPink;
		Glint.CoreColor = HealWhite;
		Glint.Intensity = 2.0f;
		Kit->Add(Glint);
	}
	UpdateHealLook(GetNow(), 0.0f);
}

void AChaosImpactGadget::UpdateHealLook(const double Now, const float DeltaSeconds)
{
	const float Age = static_cast<float>(FMath::Max(0.0, Now - State.PlacedAt));
	const float Left = HealSeconds - Age;
	// Opening out of the floor in a moment; blinking in its last three seconds.
	const float Open = FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(Age / 0.25f, 0.0f, 1.0f), 2.0f);
	const bool bShown = Left > 3.0f || FMath::Fmod(Left, 0.3f) > 0.1f;
	const float Breath = 1.0f + 0.04f * FMath::Sin(Age * 3.0f);
	if (Parts.IsValidIndex(HealFillPart) && Parts[HealFillPart])
	{
		const float Fill = HealRadius * 2.0f * Open / 100.0f;
		Parts[HealFillPart]->SetRelativeScale3D(FVector(Fill, Fill, 0.01f));
		Parts[HealFillPart]->SetVisibility(bShown);
	}
	for (int32 Index = 0; Index < HealRingSegments; ++Index)
	{
		const int32 Ring = HealRingFrom + Index;
		if (Parts.IsValidIndex(Ring) && Parts[Ring])
		{
			const float Angle = UE_TWO_PI * (Index + 0.5f) / HealRingSegments + Age * 0.4f;
			Parts[Ring]->SetRelativeLocation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * HealRadius * Open * Breath + FVector(0.0f, 0.0f, 2.5f));
			Parts[Ring]->SetRelativeRotation(FRotator(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f));
			Parts[Ring]->SetVisibility(bShown && Open > 0.05f);
		}
		// The stay's white arc: as much of it as has filled (all of it lit for a moment as the second heal comes).
		const int32 Stay = HealStayPart + Index;
		if (Parts.IsValidIndex(Stay) && Parts[Stay])
		{
			Parts[Stay]->SetVisibility(bShown && State.Heals > 0 && Index < FMath::RoundToInt(State.StayPercent / 100.0f * HealRingSegments));
		}
	}
	if (HealPivot)
	{
		// Turned to this screen's camera; the heart beats gently (quicker once someone has been healed: there is more).
		float Yaw = 0.0f;
		if (const APlayerController* Viewer = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr; Viewer && Viewer->PlayerCameraManager)
		{
			Yaw = Viewer->PlayerCameraManager->GetCameraRotation().Yaw;
		}
		HealPivot->SetWorldRotation(FRotator(0.0f, Yaw, 0.0f));
		const float Beat = 1.0f + (State.Heals > 0 ? 0.12f : 0.07f) * FMath::Max(0.0f, FMath::Sin(Age * (State.Heals > 0 ? 7.0f : 4.0f)));
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const int32 Heart = HealHeartFrom + Index;
			if (Parts.IsValidIndex(Heart) && Parts[Heart])
			{
				const float Scale = Beat * FMath::Max(Open, 0.01f);
				const FVector At = Index == 0 ? FVector(HeartSquareX, 0.0f, 4.0f) : FVector(HeartLobeX, Index == 1 ? -HeartLobeY : HeartLobeY, 4.0f);
				Parts[Heart]->SetRelativeLocation(FVector(At.X * Scale, At.Y * Scale, At.Z));
				Parts[Heart]->SetRelativeScale3D(FVector(HeartSide, HeartSide, 3.0f) * Scale / 100.0f);
				Parts[Heart]->SetVisibility(bShown);
			}
		}
	}
	// Sparkles rising off it, and a soft pink haze on the floor that holds its colour on snow.
	if (Now >= NextSparkleAt && Age > 0.15f)
	{
		NextSparkleAt = Now + 0.16;
		if (AChaosImpactFxKit* Kit = GetFx())
		{
			const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
			const float Out = HealRadius * FMath::Sqrt(FMath::FRand()) * 0.9f;
			FChaosImpactFxPiece Sparkle;
			Sparkle.Location = FVector(FMath::Cos(Angle) * Out, FMath::Sin(Angle) * Out, 10.0f);
			Sparkle.Velocity = FVector(0.0f, 0.0f, FMath::FRandRange(70.0f, 130.0f));
			Sparkle.Life = FMath::FRandRange(0.8f, 1.2f);
			Sparkle.SizeFrom = FMath::FRandRange(16.0f, 24.0f);
			Sparkle.SizeTo = 4.0f;
			Sparkle.Color = HealPink;
			Sparkle.CoreColor = HealWhite;
			Sparkle.Intensity = 2.6f;
			Sparkle.Attack = 0.15f;
			Kit->Add(Sparkle);
			if (FMath::Fmod(static_cast<float>(Age), 0.8f) < 0.17f)
			{
				FChaosImpactFxPiece Haze;
				Haze.Look = EChaosImpactFxLook::GroundFlame;
				Haze.Location = FVector(0.0f, 0.0f, 1.0f);
				Haze.Life = 1.1f;
				Haze.SizeFrom = Haze.SizeTo = HealRadius * 1.9f;
				Haze.Color = HealPink;
				Haze.CoreColor = HealPink;
				Haze.Intensity = 1.0f;
				Haze.Opacity = 0.16f;
				Haze.Attack = 0.35f;
				Haze.Hold = 0.3f;
				Haze.NoiseAmount = 0.3f;
				Haze.Softness = 0.9f;
				Kit->Add(Haze);
			}
		}
	}
}

void AChaosImpactGadget::PlayHealOn(AChaosImpactCharacter* Who)
{
	if (GetNetMode() == NM_DedicatedServer || !IsValid(Who))
	{
		return;
	}
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::HealGain, Who->GetActorLocation());
	AChaosImpactFxKit* Kit = GetFx();
	if (!Kit)
	{
		return;
	}
	// On the one healed: a pink ring at their feet, sparkles streaming up round them, a soft glow on their body.
	const UCapsuleComponent* Capsule = Who->GetCapsuleComponent();
	const float Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Feet = Who->GetActorLocation() - FVector(0.0f, 0.0f, Half - 4.0f) - GetActorLocation();
	FChaosImpactFxPiece Ring;
	Ring.Look = EChaosImpactFxLook::SolidRing;
	Ring.Location = Feet;
	Ring.Life = 0.5f;
	Ring.SizeFrom = 60.0f;
	Ring.SizeTo = 260.0f;
	Ring.RingFrom = 0.6f;
	Ring.RingTo = 0.92f;
	Ring.RingWidth = 0.07f;
	Ring.Color = HealPink;
	Ring.CoreColor = HealWhite;
	Ring.Intensity = 1.5f;
	Ring.Opacity = 0.85f;
	Ring.NoiseAmount = 0.0f;
	Kit->Add(Ring);
	FChaosImpactFxPiece Body;
	Body.Location = Feet + FVector(0.0f, 0.0f, Half);
	Body.Life = 0.55f;
	Body.SizeFrom = Half * 2.6f;
	Body.SizeTo = Half * 3.2f;
	Body.Color = HealPink;
	Body.CoreColor = HealWhite;
	Body.Intensity = 1.3f;
	Body.Attack = 0.06f;
	Kit->Add(Body);
	for (int32 Index = 0; Index < 14; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 14.0f;
		FChaosImpactFxPiece Rise;
		Rise.Look = EChaosImpactFxLook::Streak;
		Rise.Location = Feet + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * FMath::FRandRange(35.0f, 60.0f)
			+ FVector(0.0f, 0.0f, FMath::FRandRange(0.0f, Half));
		Rise.Velocity = FVector(0.0f, 0.0f, FMath::FRandRange(260.0f, 420.0f));
		Rise.Drag = 1.5f;
		Rise.Delay = FMath::FRandRange(0.0f, 0.18f);
		Rise.Life = FMath::FRandRange(0.45f, 0.7f);
		Rise.SizeFrom = 46.0f;
		Rise.SizeTo = 12.0f;
		Rise.Width = 4.0f;
		Rise.Color = HealPink;
		Rise.CoreColor = HealWhite;
		Rise.Intensity = 3.0f;
		Kit->Add(Rise);
	}
}

void AChaosImpactGadget::PlayHealEnd()
{
	AChaosImpactFxKit* Kit = GetFx();
	if (!Kit)
	{
		return;
	}
	if (State.bBurst)
	{
		// Used up: it blooms upward and is gone (its heal's own sound has just played).
		FChaosImpactFxPiece Bloom;
		Bloom.Look = EChaosImpactFxLook::SolidRing;
		Bloom.Location = FVector(0.0f, 0.0f, 4.0f);
		Bloom.Life = 0.4f;
		Bloom.SizeFrom = HealRadius * 2.0f;
		Bloom.SizeTo = HealRadius * 3.2f;
		Bloom.RingFrom = Bloom.RingTo = 0.9f;
		Bloom.RingWidth = 0.05f;
		Bloom.Color = HealPink;
		Bloom.CoreColor = HealWhite;
		Bloom.Intensity = 1.4f;
		Bloom.Opacity = 0.8f;
		Bloom.NoiseAmount = 0.0f;
		Kit->Add(Bloom);
		for (int32 Index = 0; Index < 12; ++Index)
		{
			const float Angle = UE_TWO_PI * Index / 12.0f;
			FChaosImpactFxPiece Petal;
			Petal.Location = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * HealRadius * 0.7f + FVector(0.0f, 0.0f, 10.0f);
			Petal.Velocity = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 60.0f + FVector(0.0f, 0.0f, 240.0f);
			Petal.Drag = 1.2f;
			Petal.Life = 0.6f;
			Petal.SizeFrom = 22.0f;
			Petal.SizeTo = 5.0f;
			Petal.Color = HealPink;
			Petal.CoreColor = HealWhite;
			Petal.Intensity = 2.6f;
			Kit->Add(Petal);
		}
		return;
	}
	// Run out unused: it shrinks away quietly.
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::HealFade, GetActorLocation() + FVector(0.0f, 0.0f, 20.0f));
	FChaosImpactFxPiece Fade;
	Fade.Look = EChaosImpactFxLook::SolidRing;
	Fade.Location = FVector(0.0f, 0.0f, 4.0f);
	Fade.Life = 0.4f;
	Fade.SizeFrom = HealRadius * 2.0f;
	Fade.SizeTo = HealRadius * 0.4f;
	Fade.RingFrom = Fade.RingTo = 0.9f;
	Fade.RingWidth = 0.05f;
	Fade.Color = HealPink;
	Fade.CoreColor = HealWhite;
	Fade.Intensity = 1.2f;
	Fade.Opacity = 0.7f;
	Fade.NoiseAmount = 0.0f;
	Kit->Add(Fade);
}
