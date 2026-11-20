// 合体ボール: the fused look (2026-11-17). One ball of the two: its body the HUD icon's two-colour swirl
// (M_CI_FusionSwirl), dressed with what marks each of the two kinds (flames, ice crystals, crackling arcs, the black
// ball's rings, a whirl of wind, smoke, a halo of light, snow, little birds, a claw, an anchor, chasing arrows, a mine's
// spikes), so every pair looks like no other; crossed rings and a double helix of motes in the two colours round it, a
// light of both. Something flying with a black hole riding along draws its pull: a vortex on the floor, rings of bent
// light closing in on it, streaks of light drawn into it.
#include "ChaosImpactFusion.h"

#include "ChaosImpactFxKit.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactLightning.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "ProceduralMeshComponent.h"

namespace
{
	using EType = EChaosImpactBallType;
	using FSign = ChaosImpactFusion::FFusionLook::FSign;

	const TCHAR* const SwirlPath = TEXT("/Game/ChaosImpact/FX/Fusion/M_CI_FusionSwirl.M_CI_FusionSwirl");
	const TCHAR* const SpiralPath = TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Spiral.M_CIFX_Spiral");
	const TCHAR* const RingPath = TEXT("/Game/ChaosImpact/FX/Kit/M_CIFX_Ring.M_CIFX_Ring");

	UStaticMesh* LookMesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	FLinearColor LookColor(const EType Type)
	{
		FLinearColor Color = ChaosImpactBallTypes::GetColor(Type);
		Color.A = 1.0f;
		return Color;
	}

	/** A part: no collision, no shadow, its own world scale and turn (set each frame). */
	UStaticMeshComponent* LookPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, UMaterialInterface* Look,
		const bool bAbsolute = true)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Look);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCastShadow(false);
		Part->SetupAttachment(Parent);
		Part->SetUsingAbsoluteScale(bAbsolute);
		Part->SetUsingAbsoluteRotation(bAbsolute);
		Part->RegisterComponent();
		return Part;
	}

	USceneComponent* LookHolder(AActor* Owner, USceneComponent* Parent)
	{
		USceneComponent* Holder = NewObject<USceneComponent>(Owner);
		Holder->SetupAttachment(Parent);
		Holder->SetUsingAbsoluteRotation(true);
		Holder->SetUsingAbsoluteScale(true);
		Holder->RegisterComponent();
		return Holder;
	}

	/** A part of a shape in its holder's space (X toward the camera when the holder faces it): along Way, Length long. */
	UStaticMeshComponent* ShapePart(AActor* Owner, USceneComponent* Holder, UStaticMesh* Mesh, UMaterialInterface* Look,
		const FVector& Center, const FVector& Way, const float Length, const float Thick)
	{
		UStaticMeshComponent* Piece = LookPart(Owner, Holder, Mesh, Look, false);
		Piece->SetRelativeLocationAndRotation(Center, FRotationMatrix::MakeFromZ(Way.GetSafeNormal()).Rotator());
		Piece->SetRelativeScale3D(FVector(Thick / 100.0f, Thick / 100.0f, Length / 100.0f));
		return Piece;
	}

	UNiagaraComponent* LookAura(USceneComponent* Parent, const TCHAR* Path, const float Scale)
	{
		UNiagaraSystem* System = Path ? ChaosImpactBallTypes::LoadEffect(Path) : nullptr;
		UNiagaraComponent* Aura = System ? UNiagaraFunctionLibrary::SpawnSystemAttached(System, Parent, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false) : nullptr;
		if (Aura)
		{
			Aura->SetUsingAbsoluteScale(true);
			Aura->SetWorldScale3D(FVector(Scale));
		}
		return Aura;
	}

	/** What marks one kind, round a ball of radius R (centimetres). */
	void BuildSign(AActor* Owner, USceneComponent* Root, USceneComponent* Mesh, const EType Type, const float R, FSign& Sign)
	{
		using namespace ChaosImpactBallTypes;
		Sign.Type = Type;
		const FLinearColor Color = LookColor(Type);
		USceneComponent* Holder = LookHolder(Owner, Root);
		Sign.Holder = Holder;
		UStaticMesh* Sphere = LookMesh(TEXT("Sphere"));
		UStaticMesh* Cylinder = LookMesh(TEXT("Cylinder"));
		UStaticMesh* Cone = LookMesh(TEXT("Cone"));
		const auto Add = [&Sign](UStaticMeshComponent* Part) { Sign.Parts.Add(Part); return Part; };
		switch (Type)
		{
		case EType::Fire:
		{
			// Real flames licking off it, and embers spiralling up from it.
			if (UNiagaraComponent* Aura = LookAura(Root, Effects::Fire, 0.62f * R / 24.0f))
			{
				SetEffectFloat(Aura, TEXT("Flame Scale"), 0.75f);
				SetEffectFloat(Aura, TEXT("Smoke Spawn Scale"), 0.0f);
				SetEffectFloat(Aura, TEXT("Base Light Intentsity"), 0.0f);
				Sign.Aura = Aura;
			}
			UMaterialInstanceDynamic* Ember = MakeAdditive(Owner, FLinearColor(1.0f, 0.45f, 0.08f), 3.5f);
			for (int32 Index = 0; Index < 5; ++Index)
			{
				Add(LookPart(Owner, Holder, Sphere, Ember));
			}
			break;
		}
		case EType::Ice:
		{
			// A crown of ice crystals breaking out of it, turning slowly.
			ChaosImpactIceMeshes::FMeshBuffers Gem;
			FRandomStream Stream(static_cast<int32>(Owner->GetUniqueID()) + 17);
			for (int32 Index = 0; Index < 7; ++Index)
			{
				const float Z = 0.75f - 1.3f * Index / 6.0f;
				const float Ring = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Z * Z));
				const float Angle = Index * 2.39996323f;
				const FVector Direction(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, Z);
				ChaosImpactIceMeshes::AppendCrystal(Gem, FTransform(FRotationMatrix::MakeFromZ(Direction).Rotator(), Direction * R * 0.72f),
					R * Stream.FRandRange(0.16f, 0.22f), R * Stream.FRandRange(0.75f, 1.05f), Stream);
			}
			if (UProceduralMeshComponent* Crystals = ChaosImpactIceMeshes::CreateComponent(Owner, Holder, Gem,
				MakeIceCrystal(Owner, 0.55f, 0.35f, FLinearColor(0.55f, 0.85f, 1.0f))))
			{
				Crystals->SetCastShadow(false);
			}
			UMaterialInstanceDynamic* Frost = MakeAdditive(Owner, FLinearColor(0.55f, 0.85f, 1.0f), 2.4f);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Add(LookPart(Owner, Holder, Sphere, Frost));
			}
			break;
		}
		case EType::Thunder:
		{
			// Arcs of lightning jumping off it (rebuilt every few frames) in a crackle of electricity, a shell of it flickering
			// round the ball, sparks thrown off.
			Sign.Aura = LookAura(Root, Effects::Electricity, 0.5f * R / 24.0f);
			Sign.Arcs = ChaosImpactLightning::CreateComponent(Owner, Root, MakeAdditive(Owner, FLinearColor(1.0f, 0.92f, 0.5f), 4.2f));
			{
				UMaterialInstanceDynamic* Shell = MakeAdditive(Owner, FLinearColor(1.0f, 0.85f, 0.2f), 1.2f, 1.0f);
				Sign.Glow = Shell;
				Add(LookPart(Owner, Holder, Sphere, Shell));
			}
			if (UProceduralMeshComponent* Arcs = Sign.Arcs.Get())
			{
				Arcs->SetUsingAbsoluteScale(true);
				Arcs->SetUsingAbsoluteRotation(true);
			}
			break;
		}
		case EType::Black:
		{
			// The black ball's two tilted rings spinning round it, an event horizon of violet, a dark haze.
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(0.9f, 0.38f, 1.0f), 1.7f, 1.0f)));
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(0.45f, 0.28f, 1.0f), 1.4f, 1.0f)));
			UMaterialInstanceDynamic* Horizon = MakeAdditive(Owner, FLinearColor(0.55f, 0.15f, 1.0f), 1.0f, 1.0f);
			Sign.Glow = Horizon;
			Add(LookPart(Owner, Holder, Sphere, Horizon));
			Sign.Aura = LookAura(Root, Effects::DarkAura, 0.4f * R / 24.0f);
			break;
		}
		case EType::Wind:
		{
			// Two flat rings whirling round it fast, stacked a little apart like a small whirlwind; green sparks.
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(0.45f, 1.0f, 0.5f), 1.5f, 1.0f)));
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(0.25f, 0.95f, 0.4f), 1.3f, 1.0f)));
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(0.55f, 1.0f, 0.6f), 1.0f, 1.0f)));
			if (UNiagaraComponent* Aura = LookAura(Root, Effects::WindSparks, 0.36f * R / 24.0f))
			{
				SetEffectFloat(Aura, TEXT("Spark Spawn Rate"), 26.0f);
				Sign.Aura = Aura;
			}
			break;
		}
		case EType::Smoke:
		{
			// Smoke leaking off it in a soft haze.
			if (UNiagaraComponent* Aura = LookAura(Root, Effects::LastHitSmoke, 0.5f * R / 24.0f))
			{
				Aura->SetAllowScalability(false);
				SetEffectColor(Aura, TEXT("Smoke Color"), FLinearColor(0.45f, 0.42f, 0.55f));
				SetEffectSize(Aura, TEXT("Sprite Size"), 45.0f * R / 24.0f);
				Sign.Aura = Aura;
			}
			UMaterialInstanceDynamic* Haze = MakeAdditive(Owner, FLinearColor(0.6f, 0.56f, 0.75f), 0.45f, 1.0f);
			Sign.Glow = Haze;
			Add(LookPart(Owner, Holder, Sphere, Haze));
			break;
		}
		case EType::Beam:
		{
			// A halo of pink light shimmering fast round it, sparks thrown off.
			UMaterialInstanceDynamic* Halo = MakeAdditive(Owner, Color, 1.1f, 1.0f);
			Sign.Glow = Halo;
			Add(LookPart(Owner, Holder, Sphere, Halo));
			UMaterialInstanceDynamic* Core = MakeAdditive(Owner, FLinearColor(1.0f, 0.55f, 0.9f), 2.6f);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Add(LookPart(Owner, Holder, Cylinder, Core));
			}
			if (UNiagaraComponent* Aura = LookAura(Root, Effects::WindSparks, 0.4f * R / 24.0f))
			{
				SetEffectFloat(Aura, TEXT("Spark Spawn Rate"), 45.0f);
				Sign.Aura = Aura;
			}
			break;
		}
		case EType::Snow:
		{
			// Clumps of snow stuck on it, flakes drifting round.
			if (Mesh)
			{
				// (Stuck on the ball itself, rolling with it; noted so they come off again when the two part.)
				TArray<USceneComponent*> Before;
				Mesh->GetChildrenComponents(false, Before);
				AttachSnowLumps(Owner, Mesh, MakeSnow(Owner), static_cast<int32>(Owner->GetUniqueID()) + 5);
				TArray<USceneComponent*> After;
				Mesh->GetChildrenComponents(false, After);
				for (USceneComponent* Child : After)
				{
					if (!Before.Contains(Child))
					{
						Sign.Extras.Add(Child);
					}
				}
			}
			UMaterialInstanceDynamic* Flake = MakeEmissive(Owner, FLinearColor(0.95f, 0.98f, 1.0f), 1.2f);
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Add(LookPart(Owner, Holder, Sphere, Flake));
			}
			break;
		}
		case EType::Simae:
		{
			// The shima-enaga's three little birds escorting it.
			if (Mesh)
			{
				BuildSimaeBallLook(Owner, Mesh, Sign.Simae);
			}
			break;
		}
		case EType::Drive:
		{
			// Golden rings of energy round it, as the drive ball's.
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(1.0f, 0.7f, 0.15f), 2.6f, 1.0f)));
			Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FLinearColor(1.0f, 0.85f, 0.4f), 2.0f, 1.0f)));
			break;
		}
		case EType::Hook:
		case EType::Anchor:
		case EType::Swap:
		case EType::Mine:
		{
			// Its emblem round the ball: dark steel, points in its colour (the shapes of the kinds' own balls, small).
			UMaterialInstanceDynamic* Steel = MakeEmissive(Owner, FLinearColor(0.16f, 0.18f, 0.21f), 1.0f);
			UMaterialInstanceDynamic* Tip = MakeEmissive(Owner, Color, 2.6f);
			const float K = R / 40.0f;
			const auto Rod = [&](UMaterialInterface* Look, const FVector& From, const FVector& To, const float Thick)
			{
				return ShapePart(Owner, Holder, Cylinder, Look, (From + To) * 0.5f, To - From,
					static_cast<float>(FVector::Dist(From, To)) + Thick * 0.5f, Thick);
			};
			if (Type == EType::Hook)
			{
				// Three claws round it, as a grappling hook's.
				for (int32 Claw = 0; Claw < 3; ++Claw)
				{
					const float A = FMath::DegreesToRadians(Claw * 120.0f);
					const FVector Out(0.0f, FMath::Cos(A), FMath::Sin(A));
					const FVector Side(0.0f, -FMath::Sin(A), FMath::Cos(A));
					Add(Rod(Steel, Out * 30.0f * K, Out * 46.0f * K, 7.0f * K));
					Add(ShapePart(Owner, Holder, Cone, Tip, Out * 50.0f * K + Side * 5.0f * K, Out * 0.4f + Side, 18.0f * K, 12.0f * K));
				}
			}
			else if (Type == EType::Anchor)
			{
				Add(Rod(Steel, FVector(0.0f, 0.0f, -44.0f) * K, FVector(0.0f, 0.0f, 48.0f) * K, 7.0f * K));
				Add(Rod(Steel, FVector(0.0f, -26.0f, 38.0f) * K, FVector(0.0f, 26.0f, 38.0f) * K, 6.0f * K));
				Add(ShapePart(Owner, Holder, Sphere, Tip, FVector(0.0f, 0.0f, 54.0f) * K, FVector::UpVector, 12.0f * K, 12.0f * K));
				for (const float S : {-1.0f, 1.0f})
				{
					const FVector Foot(0.0f, 0.0f, -44.0f);
					const FVector End(0.0f, S * 34.0f, -18.0f);
					Add(Rod(Steel, Foot * K, End * K, 6.0f * K));
					Add(ShapePart(Owner, Holder, Cone, Tip, (End + FVector(0.0f, S * 3.0f, 8.0f)) * K, FVector(0.0f, S * 0.45f, 1.0f),
						18.0f * K, 14.0f * K));
				}
			}
			else if (Type == EType::Swap)
			{
				// Two arrows chasing each other round it: one in its colour, one white.
				UMaterialInstanceDynamic* White = MakeEmissive(Owner, FLinearColor(0.92f, 0.95f, 1.0f), 2.4f);
				constexpr float Round = 46.0f;
				for (int32 Arrow = 0; Arrow < 2; ++Arrow)
				{
					UMaterialInterface* Look = Arrow == 0 ? Tip : White;
					const float Start = Arrow * 180.0f + 20.0f;
					FVector Last(0.0f, FMath::Cos(FMath::DegreesToRadians(Start)) * Round, FMath::Sin(FMath::DegreesToRadians(Start)) * Round);
					FVector Way = FVector::UpVector;
					for (int32 Step = 1; Step <= 5; ++Step)
					{
						const float A = FMath::DegreesToRadians(Start + Step * 25.0f);
						const FVector Next(0.0f, FMath::Cos(A) * Round, FMath::Sin(A) * Round);
						Add(Rod(Look, Last * K, Next * K, 7.0f * K));
						Way = Next - Last;
						Last = Next;
					}
					Add(ShapePart(Owner, Holder, Cone, Look, (Last + Way.GetSafeNormal() * 7.0f) * K, Way, 20.0f * K, 18.0f * K));
				}
			}
			else
			{
				// A sea mine's red-tipped spikes all round, a red lamp blinking on top.
				for (int32 Spike = 0; Spike < 8; ++Spike)
				{
					const float A = FMath::DegreesToRadians(22.5f + Spike * 45.0f);
					const FVector Out(0.0f, FMath::Cos(A), FMath::Sin(A));
					Add(Rod(Steel, Out * 30.0f * K, Out * 42.0f * K, 8.0f * K));
					Add(ShapePart(Owner, Holder, Sphere, Tip, Out * 46.0f * K, Out, 10.0f * K, 10.0f * K));
				}
				UMaterialInstanceDynamic* Lamp = MakeEmissive(Owner, FLinearColor(1.0f, 0.1f, 0.12f), 6.0f);
				Sign.Glow = Lamp;
				Add(ShapePart(Owner, Holder, Sphere, Lamp, FVector(8.0f, 0.0f, 0.0f) * K, FVector::UpVector, 14.0f * K, 14.0f * K));
			}
			break;
		}
		case EType::Nova:
		{
			// (2026-11-19) The nova's: three gear-gold rings of its colour turning round it, like its rig's.
			for (int32 Ring = 0; Ring < 3; ++Ring)
			{
				Add(LookPart(Owner, Holder, Cylinder, MakeAdditive(Owner, FMath::Lerp(Color, FLinearColor(1.0f, 0.85f, 0.45f), 0.35f * Ring), 2.4f, 1.0f)));
			}
			break;
		}
		case EType::Prison:
		{
			// The prison's: a cage of bars round it.
			UMaterialInstanceDynamic* Bar = MakeEmissive(Owner, Color, 2.4f);
			const float K = R / 40.0f;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const float A = FMath::DegreesToRadians(Index * 60.0f);
				const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0f);
				Add(ShapePart(Owner, Holder, Cylinder, Bar, Out * 44.0f * K, FVector::UpVector, 92.0f * K, 4.0f * K));
			}
			break;
		}
		case EType::Barrier:
		{
			// The barrier's: a little fence of light standing across it (posts and a top rail).
			UStaticMesh* Cube = LookMesh(TEXT("Cube"));
			Sign.Glow = MakeAdditive(Owner, Color, 2.2f, 1.0f);
			const float K = R / 40.0f;
			for (const float Y : {-40.0f, -13.0f, 13.0f, 40.0f})
			{
				Add(ShapePart(Owner, Holder, Cube, Sign.Glow.Get(), FVector(0.0f, Y, 0.0f) * K, FVector::UpVector, 70.0f * K, 4.0f * K));
			}
			Add(ShapePart(Owner, Holder, Cube, MakeEmissive(Owner, Color, 3.0f), FVector(0.0f, 0.0f, 36.0f) * K, FVector::RightVector, 96.0f * K, 4.0f * K));
			break;
		}
		default:
			break;
		}
	}

	/** Each frame: the sign's own motion. */
	void UpdateSign(AActor* Owner, FSign& Sign, const FVector& Center, const float R, const float Time, const float DeltaSeconds,
		const bool bShown, const bool bFlying, const FVector& ToCamera)
	{
		USceneComponent* Holder = Sign.Holder.Get();
		if (Holder && Holder->IsVisible() != bShown)
		{
			Holder->SetVisibility(bShown, true);
		}
		if (UNiagaraComponent* Aura = Sign.Aura.Get())
		{
			if (Aura->IsVisible() != bShown)
			{
				Aura->SetVisibility(bShown);
			}
		}
		if (Sign.Simae.IsBuilt())
		{
			ChaosImpactBallTypes::UpdateSimaeBallLook(Owner, Sign.Simae, Time, DeltaSeconds, false, bFlying, bShown);
		}
		if (!bShown || !Holder)
		{
			if (UProceduralMeshComponent* Arcs = Sign.Arcs.Get())
			{
				Arcs->SetVisibility(false);
			}
			return;
		}
		const auto Part = [&Sign](const int32 Index) { return Sign.Parts.IsValidIndex(Index) ? Sign.Parts[Index].Get() : nullptr; };
		switch (Sign.Type)
		{
		case EType::Fire:
			// Flames licking up off it (the FX kit's, as the fire ball's own bursts are drawn): left behind as it flies.
			if (Time >= Sign.NextArcAt)
			{
				Sign.NextArcAt = Time + 0.05f;
				if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(Owner, Center))
				{
					for (int32 Lick = 0; Lick < 2; ++Lick)
					{
						const float A = FMath::FRandRange(0.0f, UE_TWO_PI);
						FChaosImpactFxPiece Flame;
						Flame.Look = EChaosImpactFxLook::Flame;
						Flame.Location = FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * R * 0.45f - FVector(0.0f, 0.0f, R * 0.3f);
						Flame.Velocity = FVector(0.0f, 0.0f, 110.0f * R / 24.0f);
						Flame.Life = 0.36f;
						Flame.SizeFrom = R * 2.1f;
						Flame.SizeTo = R * 0.7f;
						Flame.Width = 1.7f;
						Flame.Taper = 1.0f;
						Flame.Toon = 0.6f;
						Flame.Color = FLinearColor(1.0f, 0.32f, 0.03f);
						Flame.CoreColor = FLinearColor(1.0f, 0.86f, 0.35f);
						Flame.Intensity = 1.6f;
						Flame.Opacity = 1.0f;
						Flame.Attack = 0.15f;
						Flame.Flicker = 0.4f;
						Fx->Add(Flame);
					}
				}
			}
			// Embers rising and turning round it, each in its own time.
			for (int32 Index = 0; Index < Sign.Parts.Num(); ++Index)
			{
				if (UStaticMeshComponent* Ember = Part(Index))
				{
					const float Life = FMath::Fmod(Time * 0.9f + Index * 0.21f, 1.0f);
					const float A = Time * 2.2f + Index * 1.3f;
					Ember->SetWorldLocation(Center + FVector(FMath::Cos(A) * R * (0.9f + 0.4f * Life), FMath::Sin(A) * R * (0.9f + 0.4f * Life),
						R * (-0.3f + 1.8f * Life)));
					Ember->SetWorldScale3D(FVector(R / 24.0f * 0.06f * (1.0f - Life)));
				}
			}
			break;
		case EType::Ice:
			Holder->SetWorldRotation(FRotator(0.0f, Time * 35.0f, 0.0f));
			for (int32 Index = 0; Index < Sign.Parts.Num(); ++Index)
			{
				if (UStaticMeshComponent* Frost = Part(Index))
				{
					const float A = -Time * 1.4f + Index * 1.57f;
					Frost->SetWorldLocation(Center + FVector(FMath::Cos(A) * R * 1.45f, FMath::Sin(A) * R * 1.45f, FMath::Sin(Time * 2.0f + Index) * R * 0.4f));
					Frost->SetWorldScale3D(FVector(R / 24.0f * 0.05f * (1.0f + 0.4f * FMath::Sin(Time * 6.0f + Index))));
				}
			}
			break;
		case EType::Thunder:
			// The crackling shell: flickering in size and light.
			if (UStaticMeshComponent* Shell = Part(0))
			{
				Shell->SetWorldScale3D(FVector(R / 24.0f * 0.72f * FMath::FRandRange(0.9f, 1.3f)));
			}
			if (UMaterialInstanceDynamic* Glow = Sign.Glow.Get())
			{
				Glow->SetScalarParameterValue(TEXT("Intensity"), FMath::FRandRange(0.5f, 1.9f));
			}
			// Sparks thrown off now and then.
			if (FMath::FRand() < DeltaSeconds * 7.0f)
			{
				if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(Owner, Center))
				{
					for (int32 Spark = 0; Spark < 4; ++Spark)
					{
						const FVector Out = FMath::VRand();
						FChaosImpactFxPiece Piece;
						Piece.Look = EChaosImpactFxLook::Streak;
						Piece.Location = Out * R * 0.9f;
						Piece.Velocity = Out * 900.0f * R / 24.0f;
						Piece.Drag = 4.0f;
						Piece.Life = 0.2f;
						Piece.SizeFrom = 30.0f * R / 24.0f;
						Piece.SizeTo = 10.0f;
						Piece.Width = 3.0f;
						Piece.Color = FLinearColor(1.0f, 0.85f, 0.25f);
						Piece.CoreColor = FLinearColor::White;
						Piece.Intensity = 6.0f;
						Fx->Add(Piece);
					}
				}
			}
			if (UProceduralMeshComponent* Arcs = Sign.Arcs.Get(); Arcs && Time >= Sign.NextArcAt)
			{
				Sign.NextArcAt = Time + 0.04f;
				FRandomStream Stream(FMath::Rand());
				ChaosImpactIceMeshes::FMeshBuffers Bolts;
				const FVector Facing = -ToCamera;
				for (int32 Index = Stream.RandRange(3, 5); Index > 0; --Index)
				{
					const FVector Out = Stream.GetUnitVector();
					ChaosImpactLightning::AppendBolt(Bolts, Out * R * 0.8f, Out * R * Stream.FRandRange(2.4f, 4.2f) + Stream.GetUnitVector() * R * 0.6f,
						R * 0.28f, Facing, Stream, 2);
				}
				ChaosImpactLightning::SetMesh(Arcs, Bolts);
				Arcs->SetWorldScale3D(FVector::OneVector);
				Arcs->SetWorldRotation(FRotator::ZeroRotator);
				Arcs->SetVisibility(true);
			}
			break;
		case EType::Black:
			if (UStaticMeshComponent* Ring = Part(0))
			{
				Ring->SetWorldRotation(FRotator(62.0f, Time * 230.0f, 10.0f));
				Ring->SetWorldScale3D(FVector(R / 24.0f * 0.85f, R / 24.0f * 0.85f, 0.02f));
			}
			if (UStaticMeshComponent* Ring = Part(1))
			{
				Ring->SetWorldRotation(FRotator(-48.0f, -Time * 170.0f, -25.0f));
				Ring->SetWorldScale3D(FVector(R / 24.0f * 1.05f, R / 24.0f * 1.05f, 0.02f));
			}
			if (UStaticMeshComponent* Horizon = Part(2))
			{
				Horizon->SetWorldScale3D(FVector(R / 24.0f * 0.7f * (1.0f + 0.06f * FMath::Sin(Time * 5.0f))));
			}
			if (UMaterialInstanceDynamic* Glow = Sign.Glow.Get())
			{
				Glow->SetScalarParameterValue(TEXT("Intensity"), 0.9f + 0.3f * FMath::Sin(Time * 7.0f));
			}
			break;
		case EType::Wind:
			for (int32 Index = 0; Index < Sign.Parts.Num(); ++Index)
			{
				if (UStaticMeshComponent* Ring = Part(Index))
				{
					Ring->SetWorldRotation(FRotator(Index == 1 ? 8.0f : -6.0f, Time * (620.0f - Index * 110.0f), 0.0f));
					const float Size = R / 24.0f * (0.62f + 0.18f * Index);
					Ring->SetWorldScale3D(FVector(Size, Size, 0.025f));
					Ring->SetWorldLocation(Center + FVector(0.0f, 0.0f, (Index - 1) * R * 0.35f));
				}
			}
			break;
		case EType::Smoke:
			if (UStaticMeshComponent* Haze = Part(0))
			{
				Haze->SetWorldScale3D(FVector(R / 24.0f * 0.8f * (1.0f + 0.12f * FMath::Sin(Time * 2.5f))));
			}
			break;
		case EType::Beam:
			if (UStaticMeshComponent* Halo = Part(0))
			{
				Halo->SetWorldScale3D(FVector(R / 24.0f * 0.85f * (1.0f + 0.12f * FMath::Sin(Time * 31.0f))));
			}
			if (UMaterialInstanceDynamic* Glow = Sign.Glow.Get())
			{
				Glow->SetScalarParameterValue(TEXT("Intensity"), 1.0f + 0.4f * FMath::Sin(Time * 23.0f));
			}
			// Four short rays turning round it, facing the camera.
			for (int32 Index = 1; Index < Sign.Parts.Num(); ++Index)
			{
				if (UStaticMeshComponent* Ray = Part(Index))
				{
					const FVector Side = FVector::CrossProduct(ToCamera, FVector::UpVector).GetSafeNormal();
					const FVector Up = FVector::CrossProduct(Side, ToCamera).GetSafeNormal();
					const float A = Time * 1.6f + Index * UE_HALF_PI;
					const FVector Way = Side * FMath::Cos(A) + Up * FMath::Sin(A);
					Ray->SetWorldLocation(Center + Way * R * 1.6f);
					Ray->SetWorldRotation(FRotationMatrix::MakeFromZ(Way).Rotator());
					Ray->SetWorldScale3D(FVector(R / 24.0f * 0.025f, R / 24.0f * 0.025f, R / 24.0f * (0.3f + 0.1f * FMath::Sin(Time * 17.0f + Index))));
				}
			}
			break;
		case EType::Snow:
			for (int32 Index = 0; Index < Sign.Parts.Num(); ++Index)
			{
				if (UStaticMeshComponent* Flake = Part(Index))
				{
					const float Fall = FMath::Fmod(Time * 0.35f + Index / 6.0f, 1.0f);
					const float A = Time * 0.9f + Index * 1.05f;
					Flake->SetWorldLocation(Center + FVector(FMath::Cos(A) * R * 1.4f, FMath::Sin(A) * R * 1.4f, R * (1.3f - 2.4f * Fall)));
					Flake->SetWorldScale3D(FVector(R / 24.0f * 0.045f));
				}
			}
			break;
		case EType::Drive:
			if (UStaticMeshComponent* Ring = Part(0))
			{
				Ring->SetWorldRotation(FRotator(70.0f, Time * 300.0f, 0.0f));
				Ring->SetWorldScale3D(FVector(R / 24.0f * 1.15f, R / 24.0f * 1.15f, 0.02f));
			}
			if (UStaticMeshComponent* Ring = Part(1))
			{
				Ring->SetWorldRotation(FRotator(-70.0f, -Time * 260.0f, 30.0f));
				Ring->SetWorldScale3D(FVector(R / 24.0f * 1.3f, R / 24.0f * 1.3f, 0.02f));
			}
			break;
		case EType::Nova:
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (UStaticMeshComponent* Ring = Part(Index))
				{
					Ring->SetWorldRotation(FRotator(60.0f - 40.0f * Index, Time * (180.0f + 70.0f * Index) * (Index % 2 == 0 ? 1.0f : -1.0f), 25.0f * Index));
					const float Size = R / 24.0f * (1.2f + 0.14f * Index);
					Ring->SetWorldScale3D(FVector(Size, Size, 0.02f));
				}
			}
			break;
		case EType::Prison:
			Holder->SetWorldRotation(FRotator(0.0f, Time * 40.0f, 0.0f));
			Holder->SetWorldScale3D(FVector::OneVector);
			break;
		case EType::Barrier:
		{
			const FRotator Facing = FRotationMatrix::MakeFromXZ(ToCamera, FVector::UpVector).Rotator();
			Holder->SetWorldRotation(Facing);
			Holder->SetWorldScale3D(FVector::OneVector);
			if (UMaterialInstanceDynamic* Lamp = Sign.Glow.Get())
			{
				Lamp->SetScalarParameterValue(TEXT("Intensity"), 1.6f + 0.8f * FMath::Sin(Time * 5.0f));
			}
			break;
		}
		case EType::Hook:
		case EType::Anchor:
		case EType::Swap:
		case EType::Mine:
		{
			// Faces the camera (so the shape reads), turning: the claws round, the arrows chasing, the anchor swaying.
			const float Turn = Sign.Type == EType::Swap ? -Time * 200.0f : Sign.Type == EType::Hook ? Time * 120.0f
				: Sign.Type == EType::Mine ? Time * 40.0f : 18.0f * FMath::Sin(Time * 1.8f);
			const FRotator Facing = FRotationMatrix::MakeFromXZ(ToCamera, FVector::UpVector).Rotator();
			Holder->SetWorldRotation((FQuat(ToCamera, FMath::DegreesToRadians(Turn)) * Facing.Quaternion()).Rotator());
			Holder->SetWorldScale3D(FVector::OneVector);
			if (UMaterialInstanceDynamic* Lamp = Sign.Glow.Get())
			{
				Lamp->SetScalarParameterValue(TEXT("Intensity"), FMath::Fmod(Time, 0.8f) < 0.4f ? 8.0f : 1.5f);
			}
			break;
		}
		default:
			break;
		}
	}

	FVector LookToCamera(const UWorld* World, const FVector& At)
	{
		if (const APlayerController* Viewer = World ? World->GetFirstPlayerController() : nullptr; Viewer && Viewer->PlayerCameraManager)
		{
			return (Viewer->PlayerCameraManager->GetCameraLocation() - At).GetSafeNormal();
		}
		return FVector(-0.5f, 0.0f, 0.85f).GetSafeNormal();
	}
}

void ChaosImpactFusion::BuildLook(AActor* Owner, USceneComponent* Parent, const EChaosImpactBallType Primary,
	const EChaosImpactBallType Secondary, FFusionLook& Look, const FFusionLookOptions& Options)
{
	DestroyLook(Look);
	if (!Owner || !Parent || !Owner->GetWorld() || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace ChaosImpactBallTypes;
	Look.Primary = Primary;
	Look.Secondary = Secondary;
	Look.Scale = Options.Scale;
	Look.Radius = 24.0f * Options.Scale;
	Look.bAtom = Options.bAtom;
	USceneComponent* Root = LookHolder(Owner, Parent);
	Root->SetRelativeLocation(Options.Offset);
	Look.Root = Root;
	const FLinearColor ColorA = LookColor(Primary);
	const FLinearColor ColorB = LookColor(Secondary);

	// The body: the swirl of the two colours (the icon's), on the ball's own mesh.
	if (UMaterialInterface* Swirl = Options.bBody ? LoadObject<UMaterialInterface>(nullptr, SwirlPath) : nullptr)
	{
		UMaterialInstanceDynamic* Body = UMaterialInstanceDynamic::Create(Swirl, Owner);
		Body->SetVectorParameterValue(TEXT("ColorA"), ColorA);
		Body->SetVectorParameterValue(TEXT("ColorB"), ColorB);
		Body->SetScalarParameterValue(TEXT("Snow"), Options.SnowCover);
		// Its own light kept low: the two colours read as colours, not washed out to white.
		Body->SetScalarParameterValue(TEXT("Glow"), 0.55f);
		Body->SetScalarParameterValue(TEXT("Seam"), 1.3f);
		Body->SetScalarParameterValue(TEXT("Rim"), 0.6f);
		// A soft sheen, not a mirror: a hard white highlight washed the colours out to pastel.
		Body->SetScalarParameterValue(TEXT("Roughness"), 0.45f);
		Body->SetScalarParameterValue(TEXT("Specular"), 0.3f);
		Look.Swirl = Body;
		if (UPrimitiveComponent* Mesh = Cast<UPrimitiveComponent>(Parent))
		{
			DressMesh(Look, Mesh);
		}
	}
	// What marks each of the two.
	if (Options.bPrimarySign)
	{
		BuildSign(Owner, Root, Parent, Primary, Look.Radius, Look.Signs.AddDefaulted_GetRef());
	}
	if (Options.bSecondarySign)
	{
		BuildSign(Owner, Root, Parent, Secondary, Look.Radius, Look.Signs.AddDefaulted_GetRef());
	}
	// Crossed rings of the two colours and a double helix of motes, the two winding round each other.
	UStaticMesh* Sphere = LookMesh(TEXT("Sphere"));
	if (Options.bAtom)
	{
		UStaticMesh* Cylinder = LookMesh(TEXT("Cylinder"));
		Look.Rings[0] = LookPart(Owner, Root, Cylinder, MakeAdditive(Owner, ColorA, 2.2f, 1.0f));
		Look.Rings[1] = LookPart(Owner, Root, Cylinder, MakeAdditive(Owner, ColorB, 2.2f, 1.0f));
	}
	UMaterialInstanceDynamic* MoteA = MakeAdditive(Owner, ColorA, 1.6f);
	UMaterialInstanceDynamic* MoteB = MakeAdditive(Owner, ColorB, 1.6f);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Look.Motes.Add(LookPart(Owner, Root, Sphere, Index % 2 == 0 ? MoteA : MoteB));
	}
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner);
	Light->SetupAttachment(Root);
	Light->SetLightColor(FMath::Lerp(ColorA, ColorB, 0.55f));
	Light->SetIntensity(1800.0f * Options.Scale);
	Light->SetAttenuationRadius(300.0f * Options.Scale);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
	Look.Light = Light;

	// A black hole riding along on something flying: its pull drawn round it.
	if (Options.SuctionRadius > 0.0f)
	{
		Look.SuctionRadius = Options.SuctionRadius;
		UStaticMesh* Plane = LookMesh(TEXT("Plane"));
		const FLinearColor BlackHue = LookColor(EType::Black);
		if (UMaterialInterface* Spiral = LoadObject<UMaterialInterface>(nullptr, SpiralPath))
		{
			UMaterialInstanceDynamic* Vortex = UMaterialInstanceDynamic::Create(Spiral, Owner);
			Vortex->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(BlackHue, ColorA, 0.25f));
			Vortex->SetVectorParameterValue(TEXT("CoreColor"), FLinearColor(0.35f, 0.1f, 0.7f));
			Vortex->SetScalarParameterValue(TEXT("Erode"), 0.0f);
			Vortex->SetScalarParameterValue(TEXT("NoiseScale"), 1.3f);
			Vortex->SetScalarParameterValue(TEXT("NoiseSpeed"), 0.35f);
			Vortex->SetScalarParameterValue(TEXT("NoiseAmount"), 0.5f);
			Vortex->SetScalarParameterValue(TEXT("Softness"), 1.0f);
			Vortex->SetScalarParameterValue(TEXT("Taper"), 0.0f);
			Vortex->SetScalarParameterValue(TEXT("Toon"), 0.0f);
			Vortex->SetScalarParameterValue(TEXT("Arms"), 4.0f);
			Vortex->SetScalarParameterValue(TEXT("Twist"), 1.7f);
			Vortex->SetScalarParameterValue(TEXT("Swirl"), 0.7f);
			Vortex->SetScalarParameterValue(TEXT("Sharpness"), 2.4f);
			Vortex->SetScalarParameterValue(TEXT("Intensity"), 2.2f);
			Look.VortexMaterial = Vortex;
			Look.Vortex = LookPart(Owner, Root, Plane, Vortex);
		}
		if (UMaterialInterface* Ring = LoadObject<UMaterialInterface>(nullptr, RingPath))
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				UMaterialInstanceDynamic* Lens = UMaterialInstanceDynamic::Create(Ring, Owner);
				Lens->SetVectorParameterValue(TEXT("Color"), Index == 0 ? BlackHue : FMath::Lerp(BlackHue, ColorA, 0.5f));
				Lens->SetVectorParameterValue(TEXT("CoreColor"), FLinearColor(0.7f, 0.45f, 1.0f));
				Lens->SetScalarParameterValue(TEXT("Erode"), 0.0f);
				Lens->SetScalarParameterValue(TEXT("Softness"), 1.0f);
				Lens->SetScalarParameterValue(TEXT("NoiseAmount"), 0.3f);
				Lens->SetScalarParameterValue(TEXT("Fade"), 40.0f);
				Look.LensMaterial[Index] = Lens;
				Look.Lens[Index] = LookPart(Owner, Root, Plane, Lens);
			}
		}
	}
	Look.bBuilt = true;
	UpdateLook(Look, 0.0f, 0.0f, true);
}

void ChaosImpactFusion::DressMesh(FFusionLook& Look, UPrimitiveComponent* Mesh)
{
	UMaterialInstanceDynamic* Body = Look.Swirl.Get();
	if (!Mesh || !Body)
	{
		return;
	}
	FFusionLook::FDressed* Found = Look.Dressed.FindByPredicate([Mesh](const FFusionLook::FDressed& D) { return D.Mesh.Get() == Mesh; });
	if (!Found)
	{
		Found = &Look.Dressed.AddDefaulted_GetRef();
		Found->Mesh = Mesh;
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			Found->Originals.Add(Mesh->GetMaterial(Slot));
		}
	}
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		if (Mesh->GetMaterial(Slot) != Body)
		{
			Mesh->SetMaterial(Slot, Body);
		}
	}
}

void ChaosImpactFusion::Redress(FFusionLook& Look)
{
	for (FFusionLook::FDressed& Dressed : Look.Dressed)
	{
		if (UPrimitiveComponent* Mesh = Dressed.Mesh.Get())
		{
			// (Whatever put its own look back on it since, the swirl goes on again; the new look is the one to give back.)
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Now = Mesh->GetMaterial(Slot);
				if (Now != Look.Swirl.Get())
				{
					if (Dressed.Originals.IsValidIndex(Slot))
					{
						Dressed.Originals[Slot] = Now;
					}
					Mesh->SetMaterial(Slot, Look.Swirl.Get());
				}
			}
		}
	}
}

void ChaosImpactFusion::UpdateLook(FFusionLook& Look, const float Time, const float DeltaSeconds, const bool bShown, const float Pop,
	const bool bFlying)
{
	if (!Look.bBuilt || !Look.Root.IsValid())
	{
		return;
	}
	USceneComponent* Root = Look.Root.Get();
	AActor* Owner = Root->GetOwner();
	UWorld* World = Root->GetWorld();
	const float S = Look.Scale;
	const float R = Look.Radius;
	const float Swell = 1.0f + 0.6f * FMath::Clamp(Pop, 0.0f, 1.0f);
	const auto Show = [bShown](USceneComponent* Part)
	{
		if (Part && Part->IsVisible() != bShown)
		{
			Part->SetVisibility(bShown);
		}
	};
	Show(Look.Rings[0].Get());
	Show(Look.Rings[1].Get());
	for (const TWeakObjectPtr<UStaticMeshComponent>& Mote : Look.Motes)
	{
		Show(Mote.Get());
	}
	Show(Look.Light.Get());
	const bool bSuctionShown = bShown && bFlying && Look.SuctionRadius > 0.0f;
	if (UStaticMeshComponent* Vortex = Look.Vortex.Get(); Vortex && Vortex->IsVisible() != bSuctionShown)
	{
		Vortex->SetVisibility(bSuctionShown);
	}
	for (const TWeakObjectPtr<UStaticMeshComponent>& Lens : Look.Lens)
	{
		if (UStaticMeshComponent* Ring = Lens.Get(); Ring && Ring->IsVisible() != bSuctionShown)
		{
			Ring->SetVisibility(bSuctionShown);
		}
	}
	const FVector Center = Root->GetComponentLocation();
	const FVector ToCamera = LookToCamera(World, Center);
	for (FFusionLook::FSign& Sign : Look.Signs)
	{
		UpdateSign(Owner, Sign, Center, R * Swell, Time, DeltaSeconds, bShown, bFlying, ToCamera);
	}
	if (UMaterialInstanceDynamic* Body = Look.Swirl.Get())
	{
		Body->SetScalarParameterValue(TEXT("Glow"), 0.55f * (1.0f + 3.5f * Pop));
	}
	if (!bShown)
	{
		return;
	}
	// Crossed like an atom's, each turning its own way.
	if (UStaticMeshComponent* Ring = Look.Rings[0].Get())
	{
		Ring->SetWorldRotation(FRotator(64.0f, Time * 250.0f, 12.0f));
		Ring->SetWorldScale3D(FVector(1.0f * S * Swell, 1.0f * S * Swell, 0.016f * S));
	}
	if (UStaticMeshComponent* Ring = Look.Rings[1].Get())
	{
		Ring->SetWorldRotation(FRotator(-58.0f, 90.0f - Time * 200.0f, -18.0f));
		Ring->SetWorldScale3D(FVector(1.1f * S * Swell, 1.1f * S * Swell, 0.016f * S));
	}
	// Two strands of motes opposite each other, round and round and up and down.
	const float Round = 44.0f * S * Swell;
	for (int32 Index = 0; Index < Look.Motes.Num(); ++Index)
	{
		if (UStaticMeshComponent* Mote = Look.Motes[Index].Get())
		{
			const int32 Strand = Index % 2;
			const float Phase = Time * 4.6f + (Index / 2) * (UE_TWO_PI / 3.0f) + Strand * UE_PI;
			Mote->SetWorldLocation(Center + FVector(FMath::Cos(Phase) * Round, FMath::Sin(Phase) * Round,
				FMath::Sin(Phase * 1.5f + Strand * UE_PI) * Round * 0.45f));
			Mote->SetWorldScale3D(FVector(0.045f * S * (1.0f + 0.3f * FMath::Sin(Time * 11.0f + Index))));
		}
	}
	if (UPointLightComponent* Light = Look.Light.Get())
	{
		Light->SetIntensity(1800.0f * S * (1.0f + 0.25f * FMath::Sin(Time * 7.0f)) * (1.0f + 2.5f * Pop));
	}
	if (!bSuctionShown || !World)
	{
		return;
	}
	// The pull: a vortex turning on the floor under it, as wide as its reach; rings of bent light closing in on it over
	// and over; streaks of light drawn in to it from all round.
	const float Reach = Look.SuctionRadius;
	if (UStaticMeshComponent* Vortex = Look.Vortex.Get())
	{
		FVector Floor = Center - FVector(0.0f, 0.0f, 60.0f);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(FusionVortexFloor), false, Owner);
		if (World->LineTraceSingleByObjectType(Hit, Center, Center - FVector(0.0f, 0.0f, 900.0f), FCollisionObjectQueryParams(ECC_WorldStatic), Query))
		{
			Floor = Hit.ImpactPoint + FVector(0.0f, 0.0f, 3.0f);
		}
		Vortex->SetWorldLocation(Floor);
		Vortex->SetWorldRotation(FRotator(0.0f, -Time * 110.0f, 0.0f));
		const float Breathe = 1.0f + 0.05f * FMath::Sin(Time * 3.0f);
		Vortex->SetWorldScale3D(FVector(Reach * 2.0f / 100.0f * Breathe, Reach * 2.0f / 100.0f * Breathe, 1.0f));
		if (UMaterialInstanceDynamic* Look2 = Look.VortexMaterial.Get())
		{
			Look2->SetScalarParameterValue(TEXT("Intensity"), 2.8f + 0.6f * FMath::Sin(Time * 5.0f));
		}
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Lens = Look.Lens[Index].Get();
		UMaterialInstanceDynamic* LensLook = Look.LensMaterial[Index].Get();
		if (!Lens || !LensLook)
		{
			continue;
		}
		const float Phase = FMath::Fmod(Time * 1.7f + Index * 0.5f, 1.0f);
		const float Size = Reach * 0.75f;
		Lens->SetWorldLocation(Center + ToCamera * 20.0f);
		Lens->SetWorldRotation(FRotationMatrix::MakeFromZ(ToCamera).Rotator());
		Lens->SetWorldScale3D(FVector(Size * 2.0f / 100.0f, Size * 2.0f / 100.0f, 1.0f));
		LensLook->SetScalarParameterValue(TEXT("Radius"), FMath::Lerp(0.95f, 0.12f, Phase * Phase));
		LensLook->SetScalarParameterValue(TEXT("Width"), FMath::Lerp(0.03f, 0.08f, Phase));
		LensLook->SetScalarParameterValue(TEXT("Intensity"), 1.1f * FMath::Sin(Phase * UE_PI));
	}
	if (Time >= Look.SuctionPuffAt)
	{
		Look.SuctionPuffAt = Time + 0.05f;
		ChaosImpactFusion::PlaySuctionPuff(Owner, Center, Reach, Look.Primary);
	}
}

void ChaosImpactFusion::PlaySuctionPuff(const UObject* WorldContext, const FVector& Center, const float Reach, const EChaosImpactBallType OtherType)
{
	{
		if (AChaosImpactFxKit* Fx = AChaosImpactFxKit::Spawn(const_cast<UObject*>(WorldContext), Center))
		{
			const FLinearColor BlackHue = LookColor(EType::Black);
			const FLinearColor Other = LookColor(OtherType);
			for (int32 Streak = 0; Streak < 4; ++Streak)
			{
				const float A = FMath::FRandRange(0.0f, UE_TWO_PI);
				const float D = Reach * FMath::FRandRange(0.55f, 1.0f);
				const FVector From(FMath::Cos(A) * D, FMath::Sin(A) * D, FMath::FRandRange(-40.0f, 20.0f));
				FChaosImpactFxPiece Piece;
				Piece.Look = EChaosImpactFxLook::Streak;
				Piece.Location = From;
				Piece.Velocity = -From / 0.3f;
				Piece.Life = 0.3f;
				Piece.SizeFrom = 70.0f;
				Piece.SizeTo = 26.0f;
				Piece.Width = 5.0f;
				Piece.Color = Streak % 2 == 0 ? BlackHue : FMath::Lerp(BlackHue, Other, 0.6f);
				Piece.CoreColor = FLinearColor(0.95f, 0.88f, 1.0f);
				Piece.Intensity = 5.0f;
				Piece.Attack = 0.3f;
				Fx->Add(Piece);
			}
		}
	}
}

void ChaosImpactFusion::DestroyLook(FFusionLook& Look)
{
	const auto Drop = [](UActorComponent* Part)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	};
	// The meshes it dressed get their own look back.
	for (FFusionLook::FDressed& Dressed : Look.Dressed)
	{
		if (UPrimitiveComponent* Mesh = Dressed.Mesh.Get())
		{
			for (int32 Slot = 0; Slot < Dressed.Originals.Num() && Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				if (Mesh->GetMaterial(Slot) == Look.Swirl.Get())
				{
					Mesh->SetMaterial(Slot, Dressed.Originals[Slot].Get());
				}
			}
		}
	}
	for (FFusionLook::FSign& Sign : Look.Signs)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Part : Sign.Parts)
		{
			Drop(Part.Get());
		}
		Drop(Sign.Aura.Get());
		Drop(Sign.Arcs.Get());
		for (const TWeakObjectPtr<USceneComponent>& Extra : Sign.Extras)
		{
			Drop(Extra.Get());
		}
		if (USceneComponent* Holder = Sign.Holder.Get())
		{
			TArray<USceneComponent*> Children;
			Holder->GetChildrenComponents(true, Children);
			for (USceneComponent* Child : Children)
			{
				Drop(Child);
			}
			Drop(Holder);
		}
		if (USceneComponent* SimaeRoot = Sign.Simae.Root.Get())
		{
			TArray<USceneComponent*> Children;
			SimaeRoot->GetChildrenComponents(true, Children);
			for (USceneComponent* Child : Children)
			{
				Drop(Child);
			}
			Drop(SimaeRoot);
		}
	}
	Drop(Look.Rings[0].Get());
	Drop(Look.Rings[1].Get());
	for (const TWeakObjectPtr<UStaticMeshComponent>& Mote : Look.Motes)
	{
		Drop(Mote.Get());
	}
	Drop(Look.Light.Get());
	Drop(Look.Vortex.Get());
	Drop(Look.Lens[0].Get());
	Drop(Look.Lens[1].Get());
	if (USceneComponent* Root = Look.Root.Get())
	{
		TArray<USceneComponent*> Children;
		Root->GetChildrenComponents(true, Children);
		for (USceneComponent* Child : Children)
		{
			Drop(Child);
		}
		Drop(Root);
	}
	Look = FFusionLook();
}
