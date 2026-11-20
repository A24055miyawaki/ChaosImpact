// ラスボス ドッチ・ボール大統領の演出 (2026-11-19): AChaosImpactBossScene's parts for the President, in the White Ball House's
// great hall (AChaosImpactWhiteHall). The arrival (about 25 s): the great doors open on the hero; down the carpet between the
// lined soldiers, whose heads all turn as one; the President on its throne far off; the chandeliers going out one by one;
// its shadow huge on the wall; six balls lighting in its six hands under its face; the soldiers' stamp; looking up the dais at
// it, the rose window behind its head, its name. The words before the fight (its soldiers rushing in, the hero's special ball
// blasting them away into stars), its leap down into the arena; after the fight the words, then the ending: the dawn, a
// film's end roll over the hall at peace, ～Fin～, and a last little scene.
#include "ChaosImpactBossScene.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactEase.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactHints.h"
#include "ChaosImpactPaint.h"
#include "ChaosImpactPresidentBoss.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBoss.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactWhiteHall.h"
#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

namespace
{
	using EPart = AChaosImpactBossScene::EPart;
	using Hall30 = AChaosImpactWhiteHall;

	float Ease(const float T) { return ChaosImpactEase::InOut(FMath::Clamp(T, 0.0f, 1.0f)); }
	float Span(const float Time, const float From, const float To) { return FMath::Clamp((Time - From) / FMath::Max(To - From, 0.001f), 0.0f, 1.0f); }

	/** Where the hero stands for the words before the fight (on the carpet, facing the throne). */
	const FVector HeroMark(3300.0f, 0.0f, 0.0f);
	/** Where the President comes down to fight. */
	const FVector FightMark(Hall30::DaisX - 1300.0f, 0.0f, 0.0f);

	const EChaosImpactBallType SixKinds[6] = {EChaosImpactBallType::Fire, EChaosImpactBallType::Ice, EChaosImpactBallType::Thunder,
		EChaosImpactBallType::Black, EChaosImpactBallType::Wind, EChaosImpactBallType::Beam};

	/** The end roll (see Docs/Credits.md): 0 the title, 1 a heading, 2 a name, 3 a gap. */
	struct FCreditLine
	{
		uint8 Kind;
		const TCHAR* Text;
	};
	const FCreditLine Credits[] = {
		{0, TEXT("カオスインパクト")},
		{3, TEXT("")},
		{1, TEXT("企画・制作")},
		{2, TEXT("カオスインパクト制作チーム")},
		{3, TEXT("")},
		{1, TEXT("音楽")},
		{2, TEXT("Funky Magic　FLASH BEAT")},
		{2, TEXT("Pappa Parappa　Phalene")},
		{2, TEXT("Trick style　まんぼう二等兵")},
		{2, TEXT("戦いの旅路を征く　MATSU")},
		{2, TEXT("Fireworks　FLASH BEAT")},
		{2, TEXT("CLAP!　T.Waraya")},
		{2, TEXT("Tanukichi Adventure　MAKOOTO")},
		{2, TEXT("秋声に消ゆ")},
		{2, TEXT("提供　OpenTracks")},
		{3, TEXT("")},
		{1, TEXT("効果音")},
		{2, TEXT("Sonniss「GDC Game Audio Bundle」より")},
		{2, TEXT("Alexander Kopeikin")},
		{2, TEXT("BluezoneCorp")},
		{2, TEXT("Bolt")},
		{2, TEXT("CB Sounddesign")},
		{2, TEXT("Chupapsound")},
		{2, TEXT("Cinematic Sound Design")},
		{2, TEXT("David Dumais Audio")},
		{2, TEXT("Doex Studio")},
		{2, TEXT("Epic Stock Media")},
		{2, TEXT("Federico Soler")},
		{2, TEXT("InMotionAudio")},
		{2, TEXT("Ivo Vicic")},
		{2, TEXT("Jake Fielding")},
		{2, TEXT("Sonik Sound Library")},
		{2, TEXT("SoundBits")},
		{2, TEXT("The Noisery")},
		{3, TEXT("")},
		{1, TEXT("3Dモデル・テクスチャ")},
		{2, TEXT("Cartoon City Free - Low Poly 3D Models Pack")},
		{2, TEXT("ithappy")},
		{2, TEXT("Stylized Metallic Floor")},
		{2, TEXT("Studio94Roots")},
		{2, TEXT("Water Materials")},
		{2, TEXT("tharlevfx（CC BY 4.0）")},
		{2, TEXT("Unreal Engine サンプル素材")},
		{2, TEXT("（Level Prototyping・Mannequin・Niagara Examples）")},
		{2, TEXT("Epic Games")},
		{3, TEXT("")},
		{1, TEXT("エンジン")},
		{2, TEXT("Unreal Engine 5")},
		{2, TEXT("© Epic Games, Inc.")},
		{3, TEXT("")},
		{1, TEXT("Special Thanks")},
		{2, TEXT("遊んでくれたあなた")},
		{3, TEXT("")},
		{3, TEXT("")},
		{0, TEXT("Thank you for playing!")},
	};
	constexpr float CreditsFrom = 6.5f;
	constexpr float CreditsSeconds = 40.0f;
	constexpr float FinFrom = CreditsFrom + CreditsSeconds + 0.5f;
	constexpr float GagFrom = FinFrom + 5.0f;
	constexpr float EndingEnd = GagFrom + 6.5f;

	FVector Ground(UWorld* World, const FVector& Near, const AActor* Ignore)
	{
		return ChaosImpactSimaeFx::FindGround(World, Near + FVector(0.0f, 0.0f, 400.0f), Ignore);
	}

	void StandAt(AChaosImpactCharacter* Who, const FVector& Mark, const float Yaw)
	{
		if (!Who)
		{
			return;
		}
		// Put down standing on the floor (walking, not frozen in the air: else it is held in its falling pose).
		const bool bWasHeld = Who->IsTrainingMenuFrozen();
		Who->SetTrainingMenuFrozen(false);
		const FVector Floor = Ground(Who->GetWorld(), Mark, Who);
		Who->TeleportTo(FVector(Mark.X, Mark.Y, Floor.Z + Who->GetDefaultHalfHeight() + 1.0f), FRotator(0.0f, Yaw, 0.0f));
		Who->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
		if (UCharacterMovementComponent* Movement = Who->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->SetMovementMode(MOVE_Walking);
		}
		if (bWasHeld)
		{
			Who->SetTrainingMenuFrozen(true);
		}
	}
}

// ---------------------------------------------------------------------------------------------------- The arrival

void AChaosImpactBossScene::SkipArrivalPresident()
{
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	if (Room30)
	{
		Room30->SetCinematic(true);
		Room30->SetDoors(1.0f);
		Room30->SetChandelier(-1, 0.0f);
		Room30->SetMasterLight(0.45f);
		Room30->SetThroneLight(1.0f);
		Room30->SetRoseGlow(1.3f);
		Room30->TurnSoldiers(FVector::ZeroVector, 0.0f);
		Room30->ShowSoldiers(true);
	}
	if (Pres)
	{
		Pres->HideHandBalls();
		Pres->PlaySceneClip(0, true, 1.0f);
		if (Room30)
		{
			Pres->SetHover(FMath::Max(Room30->GetThroneSeat().Z - (Pres->GetActorLocation().Z - Pres->GetSimpleCollisionHalfHeight()) - 30.0f, 0.0f), 0.01f);
		}
	}
	if (ShadowLight)
	{
		ShadowLight->SetIntensity(0.0f);
	}
	if (FaceLight)
	{
		FaceLight->SetIntensity(0.0f);
	}
	StandAt(Hero.Get(), HeroMark, 0.0f);
	TitleTime = -1.0f;
	Black = 0.0f;
	White = 0.0f;
	Letterbox = 1.0f;
	Flash = 0.4f;
}

void AChaosImpactBossScene::TickArrivalPresident(const float T)
{
	UWorld* World = GetWorld();
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	AChaosImpactCharacter* Player = Hero.Get();
	if (!World || !Pres || !Player)
	{
		return;
	}
	HudShown = 1.0f - Ease(T / 0.3f);
	Letterbox = 1.0f;
	const FVector Seat = Room30 ? Room30->GetThroneSeat() : Pres->GetActorLocation();
	if (Cue == 0)
	{
		++Cue;
		// Set up in the dark: the doors shut, the hero outside them, the President on its throne, every light lit.
		Black = 1.0f;
		if (Room30)
		{
			Room30->SetCinematic(true);
			Room30->SetDoors(0.0f);
			Room30->SetChandelier(-1, 1.0f);
			Room30->SetMasterLight(1.0f);
			Room30->SetThroneLight(1.0f);
			Room30->SetRoseGlow(1.0f);
		}
		StandAt(Player, FVector(Hall30::DoorX - 260.0f, 0.0f, 0.0f), 0.0f);
		if (AChaosImpactSoloRoom* Arena = Room.Get())
		{
			// (The room's own door field is not shown with the great doors: it is back for the fight.)
			Arena->SetDoorHiddenForScene(true);
		}
		Pres->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		Pres->PlaySceneClip(0, true, 1.0f);
		Pres->HideHandBalls();
		Pres->SetHover(FMath::Max(Seat.Z - (Pres->GetActorLocation().Z - Pres->GetSimpleCollisionHalfHeight()) - 30.0f, 0.0f), 0.01f);
		ShadowLight = NewObject<USpotLightComponent>(this);
		ShadowLight->SetupAttachment(GetRootComponent());
		ShadowLight->RegisterComponent();
		ShadowLight->SetMobility(EComponentMobility::Movable);
		ShadowLight->SetIntensityUnits(ELightUnits::Candelas);
		ShadowLight->SetIntensity(0.0f);
		ShadowLight->SetLightColor(FLinearColor(1.0f, 0.82f, 0.62f));
		ShadowLight->SetAttenuationRadius(5000.0f);
		ShadowLight->SetOuterConeAngle(40.0f);
		ShadowLight->SetInnerConeAngle(25.0f);
		ShadowLight->SetCastShadows(true);
		FaceLight = NewObject<UPointLightComponent>(this);
		FaceLight->SetupAttachment(GetRootComponent());
		FaceLight->RegisterComponent();
		FaceLight->SetMobility(EComponentMobility::Movable);
		FaceLight->SetIntensityUnits(ELightUnits::Candelas);
		FaceLight->SetIntensity(0.0f);
		FaceLight->SetAttenuationRadius(900.0f);
		FaceLight->SetCastShadows(false);
	}
	// The heavy footsteps of the hero walking in, until it stops on its mark.
	if (T > 2.4f && T < 7.4f && FMath::FloorToInt((T - 2.4f) / 0.62f) != FMath::FloorToInt((T - 2.4f - FApp::GetDeltaTime()) / 0.62f))
	{
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallStep);
	}

	// ---- A (0-3.4): inside the hall, low, the great doors; they swing open on the hero against the corridor's light.
	if (T < 3.4f)
	{
		Black = 1.0f - Ease(Span(T, 0.1f, 0.8f));
		if (Cue == 1 && T >= 0.7f)
		{
			++Cue;
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallDoor);
		}
		if (Room30)
		{
			Room30->SetDoors(Ease(Span(T, 0.8f, 2.8f)));
		}
		const float Push = Ease(Span(T, 0.0f, 3.4f));
		SetCamera(FVector(Hall30::DoorX + 900.0f - 200.0f * Push, 240.0f, 130.0f), FVector(Hall30::DoorX - 200.0f, 0.0f, 420.0f), 46.0f);
		return;
	}
	// ---- B (3.4-7.4): down the carpet between the soldiers, low; their heads turn as one to follow.
	if (T < 7.4f)
	{
		const float U = Ease(Span(T, 3.4f, 7.4f));
		const FVector At(FMath::Lerp(1500.0f, 3000.0f, U), 0.0f, 150.0f);
		SetCamera(At, At + FVector(900.0f, 0.0f, 120.0f), 64.0f);
		if (Room30)
		{
			Room30->TurnSoldiers(At, Ease(Span(T, 4.4f, 4.75f)));
		}
		if (Cue == 2 && T >= 4.4f)
		{
			++Cue;
			// (Every head at once: a sharp shuffle of boots.)
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallStomp, 0.35f);
		}
		return;
	}
	if (Cue == 3)
	{
		++Cue;
		StandAt(Player, HeroMark, 0.0f);
	}
	// ---- C (7.4-10.2): over the hero's shoulder, down the long carpet to the throne far off (a long lens: it looms).
	if (T < 10.2f)
	{
		const float U = Ease(Span(T, 7.4f, 10.2f));
		const FVector Behind = HeroMark + FVector(-420.0f + 120.0f * U, -150.0f, 210.0f);
		SetCamera(Behind, Seat + FVector(0.0f, 0.0f, 120.0f), FMath::Lerp(30.0f, 26.0f, U), 0.6f);
		if (Room30)
		{
			Room30->TurnSoldiers(Behind, 1.0f - Ease(Span(T, 8.6f, 9.4f)));
		}
		return;
	}
	// ---- D (10.2-13.2): the chandeliers go out one by one, from the doors to the throne; only the rose window is left.
	if (T < 13.2f)
	{
		const float U = Ease(Span(T, 10.2f, 13.2f));
		SetCamera(FVector(2100.0f, -1700.0f, 240.0f + 80.0f * U), FVector(4300.0f, 0.0f, 1700.0f - 300.0f * U), 62.0f);
		const float Outs[3] = {10.5f, 11.1f, 11.7f};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (Cue == 4 + Index && T >= Outs[Index])
			{
				++Cue;
				if (Room30)
				{
					Room30->SetChandelier(Index, 0.0f);
					Room30->SetMasterLight(1.0f - 0.25f * (Index + 1));
				}
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallLightOff);
				Shake(0.15f, 0.25f);
			}
		}
		if (Cue == 7 && T >= 12.3f)
		{
			++Cue;
			if (Room30)
			{
				Room30->SetThroneLight(0.0f);
				Room30->SetMasterLight(0.12f);
			}
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallLightOff, 0.7f);
		}
		return;
	}
	// ---- E (13.2-16.0): a light low before the throne throws its shadow up the far wall, huge, six arms moving in it.
	if (T < 16.0f)
	{
		if (Cue == 8)
		{
			++Cue;
			Pres->PlaySceneClip(1, false, 0.45f);
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneRumble, 0.6f);
		}
		if (ShadowLight)
		{
			const FVector Lamp(Hall30::ThroneX - 1100.0f, 0.0f, Hall30::DaisTop + 40.0f);
			ShadowLight->SetWorldLocationAndRotation(Lamp, (Seat + FVector(600.0f, 0.0f, 700.0f) - Lamp).Rotation());
			ShadowLight->SetIntensity(9000.0f * Ease(Span(T, 13.2f, 13.7f)));
		}
		const float U = Ease(Span(T, 13.2f, 16.0f));
		SetCamera(FVector(Hall30::ThroneX - 2300.0f + 300.0f * U, 1100.0f - 200.0f * U, 260.0f), Seat + FVector(500.0f, 0.0f, 800.0f), 58.0f);
		return;
	}
	// ---- F (16.0-19.0): its face, close: six balls lighting one after another in its six hands, lighting it from below.
	if (T < 19.0f)
	{
		if (Cue == 9)
		{
			++Cue;
			if (ShadowLight)
			{
				ShadowLight->SetIntensity(0.0f);
			}
			Pres->PlaySceneClip(0, true, 0.7f);
		}
		for (int32 Arm = 0; Arm < 6; ++Arm)
		{
			if (Cue == 10 + Arm && T >= 16.35f + Arm * 0.38f)
			{
				++Cue;
				Pres->ShowHandBall(Arm, SixKinds[Arm]);
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallBallLight, 0.8f, 0.9f + Arm * 0.04f);
				if (FaceLight)
				{
					FaceLight->SetLightColor(ChaosImpactBallTypes::GetColor(SixKinds[Arm]));
				}
			}
		}
		const FVector Face = Pres->GetFace();
		const FVector Forward = Pres->GetActorForwardVector();
		if (FaceLight)
		{
			FaceLight->SetWorldLocation(Face + Forward * 120.0f - FVector(0.0f, 0.0f, 160.0f));
			FaceLight->SetIntensity(380.0f * Ease(Span(T, 16.3f, 18.6f)));
		}
		const float U = Ease(Span(T, 16.0f, 19.0f));
		SetCamera(Face + Forward * FMath::Lerp(520.0f, 380.0f, U) - FVector(0.0f, 0.0f, 90.0f), Face, 40.0f, 0.8f);
		return;
	}
	// ---- G (19.0-21.0): it raises its hands; every soldier stamps at once; the hall shakes, dust falls from the ceiling.
	if (T < 21.0f)
	{
		if (Cue == 16 && T >= 19.4f)
		{
			++Cue;
			if (Room30)
			{
				Room30->StampSoldiers(40.0f);
			}
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallStomp);
			Shake(0.8f, 0.7f);
			if (AChaosImpactFxKit* Dust = AChaosImpactFxKit::Spawn(this, FVector(3600.0f, 0.0f, 2900.0f)))
			{
				FRandomStream Stream(3030);
				for (int32 Index = 0; Index < 40; ++Index)
				{
					FChaosImpactFxPiece Mote;
					Mote.Look = EChaosImpactFxLook::Smoke;
					Mote.Location = FVector(Stream.FRandRange(-2000.0f, 2000.0f), Stream.FRandRange(-2200.0f, 2200.0f), Stream.FRandRange(-200.0f, 200.0f));
					Mote.Velocity = FVector(0.0f, 0.0f, -Stream.FRandRange(260.0f, 420.0f));
					Mote.Delay = Stream.FRandRange(0.0f, 0.4f);
					Mote.Life = 2.6f;
					Mote.SizeFrom = 40.0f;
					Mote.SizeTo = 160.0f;
					Mote.Color = FLinearColor(0.75f, 0.72f, 0.68f);
					Mote.Opacity = 0.3f;
					Dust->Add(Mote);
				}
				Dust->SetLifeSpan(4.0f);
			}
		}
		const float U = Ease(Span(T, 19.0f, 21.0f));
		SetCamera(FVector(3900.0f + 200.0f * U, -500.0f, 90.0f), FVector(Hall30::ThroneX, 0.0f, 700.0f), 70.0f);
		return;
	}
	// ---- H (21.0-25.0): looking up the dais at it, the rose window blazing behind its head; its name.
	if (Cue == 17)
	{
		++Cue;
		if (Room30)
		{
			Room30->SetThroneLight(1.0f);
			Room30->SetMasterLight(0.45f);
		}
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallLightOff, 0.9f, 0.8f);
	}
	if (Room30)
	{
		Room30->SetRoseGlow(FMath::Lerp(1.0f, 1.45f, Ease(Span(T, 21.0f, 22.0f))));
	}
	if (FaceLight)
	{
		FaceLight->SetIntensity(380.0f * (1.0f - Ease(Span(T, 21.0f, 21.6f))));
	}
	if (Cue == 18 && T >= 21.6f)
	{
		++Cue;
		TitleTime = 0.0f;
		Flash = FMath::Max(Flash, 0.5f);
		Shake(1.0f, 1.2f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneTitle);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::PresPowerUp, 0.6f);
	}
	const float U = Ease(Span(T, 21.0f, 25.0f));
	SetCamera(FVector(Hall30::ThroneX - FMath::Lerp(1300.0f, 1050.0f, U), 260.0f, Hall30::DaisTop + 30.0f), Seat + FVector(0.0f, 0.0f, 320.0f), 50.0f);
	if (T >= 25.2f)
	{
		TitleTime = -1.0f;
		Pres->HideHandBalls();
		SetPart(EPart::TalkBefore);
	}
}

// ---------------------------------------------------------------------------------------------------- The words

void AChaosImpactBossScene::SpawnRunners()
{
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	if (!Room30)
	{
		return;
	}
	// Every other soldier steps down off the gallery and charges at the hero (the rest stay lined up, watching).
	Runners.Reset();
	RunnerAt.Reset();
	RunnerFlight.Reset();
	for (int32 Index = 0; Index < Room30->GetSoldierCount(); ++Index)
	{
		if (Index % 2 == 1)
		{
			continue;
		}
		Runners.Add(Index);
		RunnerAt.Add(Room30->GetSoldierHome(Index));
		RunnerFlight.Add(FVector::ZeroVector);
	}
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallRush);
	UE_LOG(LogChaosImpact, Log, TEXT("President scene: %d soldiers charge"), Runners.Num());
}

void AChaosImpactBossScene::TickRunners(const float DeltaSeconds)
{
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	const AChaosImpactCharacter* Player = Hero.Get();
	if (!Room30 || !Player)
	{
		return;
	}
	const FVector Target = Player->GetActorLocation();
	const float Floor = Target.Z - Player->GetDefaultHalfHeight();
	for (int32 Slot = 0; Slot < Runners.Num(); ++Slot)
	{
		const int32 Index = Runners[Slot];
		if (BlownAway.Contains(Index))
		{
			continue;
		}
		FVector& At = RunnerAt[Slot];
		if (RunnerFlight[Slot].IsNearlyZero())
		{
			// Charging in (down off the gallery's step once past the columns), stopping in a ring round the hero.
			const float Spot = 360.0f + 70.0f * (Slot % 3);
			FVector To = Target - At;
			To.Z = 0.0f;
			if (To.Size() > Spot)
			{
				At += To.GetSafeNormal() * FMath::Min(720.0f * DeltaSeconds, To.Size() - Spot);
			}
			const bool bOnGallery = FMath::Abs(At.Y) > AChaosImpactWhiteHall::ColumnY - 140.0f;
			At.Z = FMath::FInterpTo(At.Z, bOnGallery ? Floor + 70.0f : Floor, DeltaSeconds, 10.0f);
			Room30->DriveSoldier(Index, At, To.Rotation().Yaw, To.Size() > Spot + 5.0f);
			continue;
		}
		// Blown away: up and off, spinning, and a twinkle where it goes (a star).
		RunnerFlight[Slot].Z -= 900.0f * DeltaSeconds;
		At += RunnerFlight[Slot] * DeltaSeconds;
		if (At.Z > Floor + 1900.0f || SpecialTime > 1.3f + Slot * 0.06f)
		{
			if (AChaosImpactFxKit* Kit = AChaosImpactFxKit::Spawn(this, At))
			{
				FChaosImpactFxPiece Star;
				Star.Life = 0.6f;
				Star.SizeFrom = 30.0f;
				Star.SizeTo = 180.0f;
				Star.Color = FLinearColor(1.0f, 0.92f, 0.5f);
				Star.CoreColor = FLinearColor::White;
				Star.Intensity = 4.0f;
				Kit->Add(Star);
				for (int32 Ray = 0; Ray < 4; ++Ray)
				{
					FChaosImpactFxPiece Glint;
					Glint.Look = EChaosImpactFxLook::Streak;
					Glint.Velocity = FVector(0.0f, 0.0f, 1.0f).RotateAngleAxis(Ray * 90.0f, FVector(1.0f, 0.0f, 0.0f)) * 10.0f;
					Glint.Life = 0.5f;
					Glint.SizeFrom = 260.0f;
					Glint.SizeTo = 20.0f;
					Glint.Width = 6.0f;
					Glint.Color = FLinearColor(1.0f, 0.95f, 0.7f);
					Glint.CoreColor = FLinearColor::White;
					Glint.Intensity = 4.0f;
					Kit->Add(Glint);
				}
				Kit->SetLifeSpan(1.2f);
			}
			ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallStarFly, 0.5f, 0.9f + 0.05f * (Slot % 5));
			Room30->HideSoldier(Index);
			BlownAway.Add(Index);
			continue;
		}
		const float Spin = (SpecialTime + Slot) * 720.0f;
		Room30->DriveSoldier(Index, At, Spin, false);
		if (USkeletalMeshComponent* Body = Room30->GetSoldier(Index))
		{
			Body->SetWorldRotation(FRotator(Spin * 0.5f, Spin, 0.0f));
		}
	}
}

void AChaosImpactBossScene::ClearRunners()
{
	// The soldiers back on their places, but those blown away gone (they come back for the ending).
	if (AChaosImpactWhiteHall* Room30 = Hall.Get())
	{
		Room30->ResetSoldiers(Part == EPart::Ending || Part == EPart::Done ? TArray<int32>() : BlownAway);
	}
	Runners.Reset();
	RunnerAt.Reset();
	RunnerFlight.Reset();
}

void AChaosImpactBossScene::StartLinePresident(const FLine& Line)
{
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	AChaosImpactCharacter* Player = Hero.Get();
	switch (Line.Emote)
	{
	case 1:
		Shake(0.4f, 0.45f);
		break;
	case 10:
		// 捕らえろ！: every light back on at its word, and its soldiers come running.
		Shake(0.5f, 0.5f);
		if (Room30)
		{
			Room30->SetChandelier(-1, 1.0f);
			Room30->SetMasterLight(1.0f);
		}
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallLightOff, 1.0f, 0.75f);
		SpawnRunners();
		break;
	case 12:
		// The special ball: gathering in the hero's hands.
		SpecialTime = -1.0f;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HeroSpecialCharge);
		break;
	case 13:
	{
		// It goes: a blast among the soldiers; they fly off into stars.
		FVector Middle = FVector::ZeroVector;
		for (const FVector& At : RunnerAt)
		{
			Middle += At;
		}
		SpecialAt = RunnerAt.Num() > 0 ? Middle / RunnerAt.Num() : (Player ? Player->GetActorLocation() + Player->GetActorForwardVector() * 500.0f : FVector::ZeroVector);
		SpecialTime = 0.0f;
		Flash = 0.85f;
		Shake(1.3f, 1.2f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HeroSpecialBlast);
		for (int32 Slot = 0; Slot < RunnerAt.Num(); ++Slot)
		{
			const FVector Away = (RunnerAt[Slot] - (Player ? Player->GetActorLocation() : SpecialAt)).GetSafeNormal2D();
			RunnerFlight[Slot] = Away * FMath::FRandRange(900.0f, 1400.0f) + FVector(0.0f, 0.0f, FMath::FRandRange(1500.0f, 2100.0f));
		}
		if (AChaosImpactFxKit* Kit = AChaosImpactFxKit::Spawn(this, SpecialAt))
		{
			const FLinearColor Rainbow[6] = {FLinearColor(1.0f, 0.3f, 0.2f), FLinearColor(1.0f, 0.8f, 0.2f), FLinearColor(0.3f, 1.0f, 0.4f),
				FLinearColor(0.3f, 0.7f, 1.0f), FLinearColor(0.7f, 0.35f, 1.0f), FLinearColor(1.0f, 0.4f, 0.8f)};
			FChaosImpactFxPiece Ball;
			Ball.Location = FVector(0.0f, 0.0f, 120.0f);
			Ball.Life = 0.5f;
			Ball.SizeFrom = 300.0f;
			Ball.SizeTo = 1700.0f;
			Ball.Color = FLinearColor(1.0f, 0.95f, 0.8f);
			Ball.CoreColor = FLinearColor::White;
			Ball.Intensity = 3.0f;
			Kit->Add(Ball);
			for (int32 Index = 0; Index < 6; ++Index)
			{
				FChaosImpactFxPiece Ring;
				Ring.Look = EChaosImpactFxLook::SolidRing;
				Ring.Location = FVector(0.0f, 0.0f, 8.0f);
				Ring.Delay = Index * 0.05f;
				Ring.Life = 0.6f;
				Ring.SizeFrom = 200.0f;
				Ring.SizeTo = 2600.0f;
				Ring.RingFrom = Ring.RingTo = 0.9f;
				Ring.RingWidth = 0.04f;
				Ring.Color = Rainbow[Index];
				Ring.CoreColor = FLinearColor::White;
				Ring.Intensity = 2.0f;
				Ring.Opacity = 0.85f;
				Ring.NoiseAmount = 0.0f;
				Kit->Add(Ring);
			}
			for (int32 Index = 0; Index < 30; ++Index)
			{
				FChaosImpactFxPiece Spark;
				Spark.Look = EChaosImpactFxLook::Streak;
				Spark.Location = FVector(0.0f, 0.0f, 100.0f);
				Spark.Velocity = (FMath::VRand() + FVector(0.0f, 0.0f, 0.8f)).GetSafeNormal() * FMath::FRandRange(900.0f, 1800.0f);
				Spark.Gravity = 900.0f;
				Spark.Drag = 1.4f;
				Spark.Life = 0.7f;
				Spark.SizeFrom = 140.0f;
				Spark.SizeTo = 30.0f;
				Spark.Width = 6.0f;
				Spark.Color = Rainbow[Index % 6];
				Spark.CoreColor = FLinearColor::White;
				Spark.Intensity = 3.4f;
				Kit->Add(Spark);
			}
			Kit->SetLifeSpan(3.0f);
		}
		break;
	}
	case 14:
		// 処刑球を渡してやる！: its six balls light in its hands, quickly, one after another.
		Shake(0.4f, 0.4f);
		if (Pres)
		{
			Pres->HideHandBalls();
		}
		break;
	case 16:
		Shake(0.25f, 0.3f);
		break;
	case 18:
		// 「避球を！」: together, the screen ringing white.
		Flash = FMath::Max(Flash, 0.6f);
		Shake(0.6f, 0.6f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneTitle, 0.7f);
		break;
	default:
		break;
	}
}

void AChaosImpactBossScene::TickTalkPresident(const float DeltaSeconds)
{
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactCharacter* Player = Hero.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	if (!Pres || !Player || !Lines().IsValidIndex(LineIndex))
	{
		return;
	}
	TickRunners(DeltaSeconds);
	const FLine& Line = Lines()[LineIndex];
	const FVector Seat = Room30 ? Room30->GetThroneSeat() : Pres->GetActorLocation();
	const FVector HeroAtNow = Player->GetActorLocation();
	const FVector HeroForward = Player->GetActorForwardVector().GetSafeNormal2D();
	const FVector HeroSide = FVector::CrossProduct(FVector::UpVector, HeroForward);
	if (bAfter)
	{
		// After the fight: the two in the arena (PlaceTalk's shot), leaning toward whoever speaks, pushed in on a turn.
		const FVector Speaker = Line.bBoss ? Pres->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f) : HeroAtNow;
		const float Lean = Ease(FMath::Min(LineTime / 0.6f, 1.0f));
		FVector Look = FMath::Lerp(TalkLook, FMath::Lerp(TalkLook, Speaker + FVector(0.0f, 0.0f, 60.0f), 0.22f), Lean);
		FVector At = TalkCamera;
		float Fov = 52.0f;
		if (Line.Emote == 4)
		{
			const float Push = Ease(FMath::Min(EmoteTime / 3.0f, 1.0f));
			At = FMath::Lerp(TalkCamera, HeroAtNow + TalkSide * 520.0f + FVector(0.0f, 0.0f, 80.0f), 0.4f * Push);
			Look = FMath::Lerp(Look, HeroAtNow + FVector(0.0f, 0.0f, 60.0f), 0.6f * Push);
		}
		else if (Line.Emote == 16)
		{
			const float Push = ChaosImpactEase::Out(FMath::Min(EmoteTime / 0.18f, 1.0f));
			const FVector Face = Pres->GetFace();
			At = FMath::Lerp(TalkCamera, Face + (TalkCamera - Face).GetSafeNormal2D() * 650.0f, 0.6f * Push);
			Look = FMath::Lerp(Look, Face, Push);
			Fov = FMath::Lerp(52.0f, 38.0f, Push);
		}
		else if (Line.Emote == 17)
		{
			const float Push = Ease(FMath::Min(EmoteTime / 3.5f, 1.0f));
			At = FMath::Lerp(TalkCamera, (TalkCamera + TalkLook) * 0.5f, 0.35f * Push);
		}
		else if (Line.Emote == 18)
		{
			// Pulled back, the two side by side, the rose window blazing behind them.
			const float Back = Ease(FMath::Min(EmoteTime / 0.8f, 1.0f));
			At = FMath::Lerp(TalkCamera, TalkCamera + (TalkCamera - TalkLook).GetSafeNormal() * 600.0f + FVector(0.0f, 0.0f, 150.0f), Back);
			Fov = FMath::Lerp(52.0f, 58.0f, Back);
		}
		SetCamera(At, Look, Fov, 1.0f);
		return;
	}
	// Before the fight: each line its own shot.
	switch (Line.Emote)
	{
	case 10: case 1:
	{
		// The President on its throne, from low on the dais steps (a shout shakes the frame).
		const float U = Ease(FMath::Min(LineTime / 4.0f, 1.0f));
		SetCamera(FVector(Hall30::ThroneX - FMath::Lerp(1250.0f, 1150.0f, U), -240.0f, Hall30::DaisTop + 40.0f), Seat + FVector(0.0f, 0.0f, 280.0f), 48.0f);
		break;
	}
	case 11:
	{
		// The soldiers pouring in on the hero, seen from behind it.
		SetCamera(HeroAtNow - HeroForward * 700.0f + HeroSide * 260.0f + FVector(0.0f, 0.0f, 420.0f), HeroAtNow + HeroForward * 250.0f, 66.0f);
		break;
	}
	case 12:
	{
		// The special ball gathering: close on the hero, a swirl of colours growing in its hands.
		const float U = Ease(FMath::Min(LineTime / 2.5f, 1.0f));
		const FVector Hands = HeroAtNow + HeroForward * 70.0f + FVector(0.0f, 0.0f, 50.0f);
		SetCamera(HeroAtNow + HeroForward * FMath::Lerp(460.0f, 340.0f, U) + HeroSide * 160.0f + FVector(0.0f, 0.0f, 10.0f), HeroAtNow + FVector(0.0f, 0.0f, 40.0f), 44.0f, 0.6f);
		if (AChaosImpactFxKit* Kit = AChaosImpactFxKit::Spawn(this, Hands); Kit && FMath::Fmod(LineTime, 0.06f) < DeltaSeconds)
		{
			const FLinearColor Colors[4] = {FLinearColor(1.0f, 0.35f, 0.2f), FLinearColor(0.35f, 0.75f, 1.0f), FLinearColor(1.0f, 0.9f, 0.3f), FLinearColor(0.8f, 0.4f, 1.0f)};
			FChaosImpactFxPiece Core;
			Core.Life = 0.12f;
			Core.SizeFrom = Core.SizeTo = 30.0f + 70.0f * U;
			Core.Color = Colors[FMath::FloorToInt(LineTime * 12.0f) % 4];
			Core.CoreColor = FLinearColor::White;
			Core.Intensity = 2.0f;
			Kit->Add(Core);
			FChaosImpactFxPiece Inward;
			Inward.Look = EChaosImpactFxLook::Streak;
			const FVector From = FMath::VRand() * 160.0f;
			Inward.Location = From;
			Inward.Velocity = -From * 4.0f;
			Inward.Life = 0.22f;
			Inward.SizeFrom = 90.0f;
			Inward.SizeTo = 20.0f;
			Inward.Width = 4.0f;
			Inward.Color = Core.Color;
			Inward.CoreColor = FLinearColor::White;
			Inward.Intensity = 3.0f;
			Kit->Add(Inward);
			Kit->SetLifeSpan(0.5f);
		}
		break;
	}
	case 13:
	{
		// Away they fly: a wide shot from low, looking up after them.
		SpecialTime += DeltaSeconds;
		SetCamera(HeroAtNow - HeroForward * 500.0f - HeroSide * 300.0f + FVector(0.0f, 0.0f, 60.0f), SpecialAt + FVector(0.0f, 0.0f, 300.0f + 500.0f * Ease(SpecialTime / 1.5f)), 72.0f);
		break;
	}
	case 14:
	{
		// Its face, its six balls lighting one after another.
		for (int32 Arm = 0; Arm < 6; ++Arm)
		{
			if (LineTime >= 0.25f + Arm * 0.16f && LineTime - DeltaSeconds < 0.25f + Arm * 0.16f)
			{
				Pres->ShowHandBall(Arm, SixKinds[Arm]);
				ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallBallLight, 0.6f, 0.95f + Arm * 0.05f);
			}
		}
		const FVector Face = Pres->GetFace();
		const FVector Forward = Pres->GetActorForwardVector();
		const float U = Ease(FMath::Min(LineTime / 3.0f, 1.0f));
		SetCamera(Face + Forward * FMath::Lerp(700.0f, 560.0f, U) - FVector(0.0f, 0.0f, 180.0f), Face - FVector(0.0f, 0.0f, 80.0f), 50.0f, 0.8f);
		break;
	}
	case 15:
	{
		// The hero, from low in front: こいッ！
		const float U = Ease(FMath::Min(LineTime / 1.2f, 1.0f));
		SetCamera(HeroAtNow + HeroForward * FMath::Lerp(380.0f, 300.0f, U) - HeroSide * 80.0f - FVector(0.0f, 0.0f, 40.0f), HeroAtNow + FVector(0.0f, 0.0f, 60.0f), 42.0f, 0.6f);
		break;
	}
	default:
	{
		// Over the hero's shoulder at the throne.
		const float U = Ease(FMath::Min(LineTime / 5.0f, 1.0f));
		SetCamera(HeroAtNow - HeroForward * (380.0f - 60.0f * U) - HeroSide * 150.0f + FVector(0.0f, 0.0f, 200.0f), Seat + FVector(0.0f, 0.0f, 150.0f), 30.0f, 0.6f);
		break;
	}
	}
}

// ---------------------------------------------------------------------------------------------------- To the fight

void AChaosImpactBossScene::TickToFightPresident(const float T)
{
	UWorld* World = GetWorld();
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	BoxShown = FMath::Max(1.0f - T / 0.25f, 0.0f);
	if (!Pres || !World)
	{
		return;
	}
	if (Cue == 0)
	{
		++Cue;
		// Down off its throne in one leap, into the arena.
		ClearRunners();
		Pres->HideHandBalls();
		LeapFrom = Pres->GetActorLocation();
		const FVector Floor = Ground(World, FightMark, Pres);
		LeapTo = FVector(FightMark.X, FightMark.Y, Floor.Z + Pres->GetSimpleCollisionHalfHeight() + 2.0f);
		Pres->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Pres->SetHover(0.0f, 0.3f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneWhoosh);
	}
	if (T < 1.0f)
	{
		const float U = Ease(T / 1.0f);
		const FVector Arc = FMath::Lerp(LeapFrom, LeapTo, U) + FVector(0.0f, 0.0f, FMath::Sin(U * UE_PI) * 900.0f);
		Pres->SetActorLocation(Arc, false, nullptr, ETeleportType::TeleportPhysics);
		SetCamera(FVector(2500.0f, -1900.0f, 520.0f), (LeapFrom + LeapTo) * 0.5f + FVector(0.0f, 0.0f, 300.0f), 64.0f);
		return;
	}
	if (Cue == 1)
	{
		++Cue;
		Pres->SetActorLocation(LeapTo, false, nullptr, ETeleportType::TeleportPhysics);
		Pres->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Pres->SetHover(AChaosImpactPresidentBoss::IdleHover, 0.6f);
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::SceneLand);
		Shake(1.0f, 0.8f);
		AChaosImpactSimaeBlast::Play(World, Ground(World, LeapTo, Pres), 1000.0f, 0.5f, false, 0);
		if (Room30)
		{
			Room30->StampSoldiers(30.0f);
		}
	}
	Letterbox = 1.0f - Ease(Span(T, 1.3f, 1.8f));
	HudShown = Ease(Span(T, 1.6f, 2.2f));
	if (Cue == 2 && T >= 1.3f)
	{
		++Cue;
		if (AChaosImpactCharacter* Player = Hero.Get())
		{
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->SetViewTargetWithBlend(Player, 0.8f, VTBlend_Cubic);
			}
		}
	}
	if (Cue == 3 && T >= 2.15f)
	{
		++Cue;
		// The fight.
		Hold(false);
		Pres->SetSceneHold(false);
		if (Room30)
		{
			Room30->SetCinematic(false);
			Room30->SetMasterLight(1.0f);
			Room30->SetChandelier(-1, 1.0f);
			Room30->TurnSoldiers(FVector::ZeroVector, 0.0f);
		}
		if (AChaosImpactSoloRoom* Arena = Room.Get())
		{
			// (Its door's field shows again: hidden while the great doors were in the scenes.)
			Arena->SetDoorHiddenForScene(false);
			Arena->WakeBossAfterScene();
		}
		if (AChaosImpactCharacter* Player = Hero.Get())
		{
			// (The game's camera drawn back for this fight: the President, the colonnade and the throne in view.)
			Player->SetStageCameraScale(1.35f);
		}
		if (Camera)
		{
			Camera->Destroy();
			Camera = nullptr;
		}
		LineIndex = -1;
		SetPart(EPart::Fight);
	}
}

// ---------------------------------------------------------------------------------------------------- The ending

void AChaosImpactBossScene::TickEnding(const float Step)
{
	UWorld* World = GetWorld();
	AChaosImpactPresidentBoss* Pres = President.Get();
	AChaosImpactWhiteHall* Room30 = Hall.Get();
	AChaosImpactCharacter* Player = Hero.Get();
	const float T = PartTime;
	BoxShown = FMath::Max(BoxShown - Step / 0.25f, 0.0f);
	HudShown = 0.0f;
	Letterbox = 1.0f;
	// Held down a second (the pause button, the decide button or a finger): the rest skipped.
	if (const APlayerController* PC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr)
	{
		const EChaosImpactDevice Device = ChaosImpactHints::GetDevice(PC);
		float X = 0.0f;
		float Y = 0.0f;
		bool bTouch = false;
		PC->GetInputTouchState(ETouchIndex::Touch1, X, Y, bTouch);
		const bool bHeld = bTouch || PC->IsInputKeyDown(EKeys::Escape) || PC->IsInputKeyDown(EKeys::Enter) || PC->IsInputKeyDown(EKeys::SpaceBar)
			|| PC->IsInputKeyDown(EKeys::Gamepad_Special_Right) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom)
			|| PC->IsInputKeyDown(ChaosImpactHints::ConfirmKey(Device)) || PC->IsInputKeyDown(EKeys::LeftMouseButton);
		EndingSkipHold = bHeld ? EndingSkipHold + Step : FMath::Max(EndingSkipHold - Step * 2.0f, 0.0f);
		if (EndingSkipHold >= 1.0f && T < EndingEnd - 0.6f)
		{
			PartTime = EndingEnd - 0.6f;
			EndingSkipHold = 0.0f;
			Black = 1.0f;
		}
	}
	if (Cue == 0)
	{
		++Cue;
		// Dawn: the red goes out of the hall, every light warm again.
		if (Room30)
		{
			Room30->SetCinematic(true);
			Room30->SetChandelier(-1, 1.0f);
			Room30->SetMasterLight(1.0f);
			// (Everyone back, the soldiers blown away too: it is over.)
			Room30->ResetSoldiers();
			Room30->TurnSoldiers(FightMark, 0.6f);
		}
		CreditsAge = -1.0f;
		FinAge = -1.0f;
		GagAge = -1.0f;
	}
	if (Room30)
	{
		Room30->SetAlarm(1.0f - Ease(Span(T, 0.0f, 3.0f)));
		Room30->SetRoseGlow(FMath::Lerp(1.0f, 1.6f, Ease(Span(T, 0.0f, 5.0f))));
	}
	const FVector Pair = Player && Pres ? (Player->GetActorLocation() + Pres->GetActorLocation()) * 0.5f : FightMark;
	// ---- (0-6) The two in the hall at dawn; the camera rises away from them toward the rose window, into its light.
	if (T < 6.5f)
	{
		const float U = Ease(Span(T, 0.0f, 6.0f));
		const FVector From = TalkCamera.IsNearlyZero() ? Pair + FVector(-900.0f, -600.0f, 300.0f) : TalkCamera;
		const FVector To(Hall30::ThroneX - 900.0f, 0.0f, Hall30::RoseZ);
		SetCamera(FMath::Lerp(From, To, U * 0.85f), FMath::Lerp(Pair + FVector(0.0f, 0.0f, 150.0f), Hall30::FarWallX * FVector(1.0f, 0.0f, 0.0f) + FVector(0.0f, 0.0f, Hall30::RoseZ), U), FMath::Lerp(52.0f, 40.0f, U));
		White = Ease(Span(T, 4.8f, 6.2f));
		return;
	}
	// ---- (6.5-39) The end roll over the hall at peace: five slow shots while the names go up the right of the screen.
	if (T < FinFrom)
	{
		CreditsAge = T - CreditsFrom;
		White = 1.0f - Ease(Span(T, 6.5f, 7.6f));
		Black = Ease(Span(T, FinFrom - 1.0f, FinFrom));
		const float Shot = (T - CreditsFrom) / (CreditsSeconds / 5.0f);
		const int32 Which = FMath::Clamp(FMath::FloorToInt(Shot), 0, 4);
		const float U = Ease(FMath::Frac(FMath::Max(Shot, 0.0f)));
		if (Room30 && FMath::Fmod(T, 2.4f) < Step)
		{
			// The soldiers cheering now and then.
			Room30->StampSoldiers(26.0f);
		}
		if (Pres && Which == 1)
		{
			Pres->PlaySceneClip(0, true, 1.2f);
		}
		switch (Which)
		{
		case 0:
			// From the gallery, wide: the hall, the soldiers, the two in the middle of it.
			SetCamera(FVector(2000.0f + 400.0f * U, -2300.0f, 700.0f), Pair + FVector(0.0f, 0.0f, 100.0f), 58.0f);
			break;
		case 1:
			// The President, close, at ease.
			if (Pres)
			{
				const FVector Face = Pres->GetFace();
				SetCamera(Face + Pres->GetActorForwardVector().RotateAngleAxis(-30.0f + 40.0f * U, FVector::UpVector) * 620.0f - FVector(0.0f, 0.0f, 120.0f), Face, 44.0f, 0.7f);
			}
			break;
		case 2:
			// The empty throne under the rose window, from low.
			SetCamera(FVector(Hall30::ThroneX - 1500.0f, 300.0f - 600.0f * U, Hall30::DaisTop + 60.0f), FVector(Hall30::ThroneX, 0.0f, Hall30::DaisTop + 600.0f + 400.0f * U), 54.0f);
			break;
		case 3:
			// Over the hero's shoulder at the President.
			if (Player && Pres)
			{
				const FVector Way = (Pres->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Way);
				SetCamera(Player->GetActorLocation() - Way * (360.0f + 80.0f * U) + Side * 140.0f + FVector(0.0f, 0.0f, 120.0f), Pres->GetFace() - FVector(0.0f, 0.0f, 120.0f), 40.0f, 0.6f);
			}
			break;
		default:
			// High over the whole hall, slowly drawing back.
			SetCamera(FVector(1500.0f - 300.0f * U, 0.0f, 2700.0f + 200.0f * U), FVector(4400.0f, 0.0f, 300.0f), 70.0f);
			break;
		}
		return;
	}
	CreditsAge = -1.0f;
	// ---- ～Fin～ on black.
	if (T < GagFrom)
	{
		Black = 1.0f;
		FinAge = T - FinFrom;
		return;
	}
	FinAge = -1.0f;
	// ---- The last little scene: the great doors creak open a crack, and a shima-enaga peeks in.
	if (Cue == 1)
	{
		++Cue;
		if (Room30)
		{
			Room30->SetDoors(0.0f);
		}
		GagLine = TEXT("……もう終わったシマ？");
		if (World)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			const FVector Peek(Hall30::DoorX - 380.0f, 260.0f, 300.0f);
			if (AChaosImpactSimaeBoss* Simae = World->SpawnActor<AChaosImpactSimaeBoss>(AChaosImpactSimaeBoss::StaticClass(), FTransform(FRotator(0.0f, 0.0f, 0.0f), Peek), Params))
			{
				Simae->SetDormant(true);
				Simae->bHarmless = true;
				Simae->bHoldMoves = true;
				UGameplayStatics::FinishSpawningActor(Simae, FTransform(FRotator(0.0f, 0.0f, 0.0f), Peek));
				Simae->SetSceneHold(true);
				Simae->PlaySceneClip(0, true, 1.0f);
				Gag = Simae;
			}
		}
	}
	Black = 1.0f - Ease(Span(T, GagFrom, GagFrom + 0.6f));
	if (Room30)
	{
		Room30->SetDoors(0.28f * Ease(Span(T, GagFrom + 0.4f, GagFrom + 1.4f)));
	}
	if (Cue == 2 && T >= GagFrom + 0.5f)
	{
		++Cue;
		ChaosImpactSfx::Play2D(this, EChaosImpactSfx::HallDoor, 0.6f, 1.15f);
	}
	GagAge = T - (GagFrom + 1.6f);
	SetCamera(FVector(Hall30::DoorX + 650.0f, -140.0f, 220.0f), FVector(Hall30::DoorX, 220.0f, 380.0f), 44.0f);
	if (T >= EndingEnd - 0.6f)
	{
		Black = Ease(Span(T, EndingEnd - 0.6f, EndingEnd));
	}
	if (T >= EndingEnd)
	{
		// Over: the President goes (the room counts it down and the stage is cleared).
		if (AActor* Joke = Gag.Get())
		{
			Joke->Destroy();
		}
		if (Room30)
		{
			Room30->SetCinematic(false);
			Room30->SetDoors(1.0f);
		}
		if (Player)
		{
			if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
			{
				PC->SetViewTarget(Player);
			}
		}
		Black = 0.0f;
		Hold(false);
		LineIndex = -1;
		if (Player)
		{
			Player->SetStageCameraScale(1.0f);
		}
		if (Pres)
		{
			Pres->FinishBeaten();
		}
		Finish();
	}
}

void AChaosImpactBossScene::PaintEnding(const FGeometry& Geometry, FSlateWindowElementList& Elements, const int32 Layer) const
{
	using namespace ChaosImpactPaint;
	const FVector2f Size = Geometry.GetLocalSize();
	const float S = FMath::Max(Size.X / 1920.0f, Size.Y / 1920.0f);
	// ---- The end roll: up the right of the screen, fading in at the bottom and out at the top.
	if (CreditsAge >= 0.0f)
	{
		const FGeometry Space = MakeAnchor(Geometry, 0.0f, 0.0f, S);
		const float W = Size.X / S;
		const float H = Size.Y / S;
		const float Column = W * 0.78f;
		float Total = 0.0f;
		for (const FCreditLine& Line : Credits)
		{
			Total += Line.Kind == 0 ? 120.0f : Line.Kind == 1 ? 74.0f : Line.Kind == 2 ? 54.0f : 50.0f;
		}
		const float Travel = H + Total;
		float Y = H - CreditsAge / CreditsSeconds * Travel;
		const float Band = H * 0.105f + 40.0f;
		for (const FCreditLine& Line : Credits)
		{
			const float Step = Line.Kind == 0 ? 120.0f : Line.Kind == 1 ? 74.0f : Line.Kind == 2 ? 54.0f : 50.0f;
			if (Line.Kind != 3 && Y > Band - 80.0f && Y < H - Band + 20.0f)
			{
				const float Edge = FMath::Min(FMath::Clamp((Y - Band + 80.0f) / 120.0f, 0.0f, 1.0f), FMath::Clamp((H - Band + 20.0f - Y) / 120.0f, 0.0f, 1.0f));
				const FPainter Words{Space, Elements, Layer + 50, Edge};
				const float TextSize = Line.Kind == 0 ? 64.0f : Line.Kind == 1 ? 30.0f : 34.0f;
				const FLinearColor Color = Line.Kind == 1 ? FLinearColor(1.0f, 0.82f, 0.45f) : Paper;
				const float Fit = HudFit(Line.Text, TextSize, W * 0.4f, Line.Kind == 0 ? TEXT("BlackItalic") : TEXT("Bold"));
				Words.Text(Line.Text, Column, Y, Fit, Color, ETextAlign::Center, Line.Kind == 0 ? TEXT("BlackItalic") : TEXT("Bold"), 2.0f, Ink);
			}
			Y += Step;
		}
	}
	// ---- ～Fin～
	if (FinAge >= 0.0f)
	{
		const float Alpha = Ease(FinAge / 1.2f) * (1.0f - Ease(Span(FinAge, 4.0f, 4.8f)));
		const FGeometry Middle = MakeAnchor(Geometry, Size.X * 0.5f, Size.Y * 0.5f, S);
		FPainter{Middle, Elements, Layer + 60, Alpha}.Text(TEXT("～Fin～"), 0.0f, -60.0f, 96.0f, Paper, ETextAlign::Center, TEXT("BlackItalic"), 3.0f, Ink);
	}
	// ---- The last little scene's words, in the same box as the talk.
	if (GagAge >= 0.0f && !GagLine.IsEmpty())
	{
		const int32 Shown = FMath::Clamp(FMath::FloorToInt(GagAge * 18.0f), 0, GagLine.Len());
		const float BoxW = FMath::Min(1560.0f, Size.X / S - 60.0f);
		const float BoxH = 150.0f;
		const FGeometry Anchor = MakeAnchor(Geometry, Size.X * 0.5f, Size.Y - 38.0f * S, S);
		const FGeometry BoxSpace = MakeSkewed(Anchor, -BoxW * 0.5f, -BoxH, BoxW, BoxH, -0.07f);
		const FPainter Back{BoxSpace, Elements, Layer + 52, FMath::Clamp(GagAge / 0.2f, 0.0f, 1.0f)};
		HudShard(Back, {{14.0f, 0.0f}, {BoxW, 0.0f}, {BoxW, BoxH - 14.0f}, {BoxW - 14.0f, BoxH}, {0.0f, BoxH}, {0.0f, 14.0f}},
			MakeFill(FLinearColor(0.07f, 0.1f, 0.22f, 0.94f), FLinearColor(0.015f, 0.025f, 0.07f, 0.94f)), 3.0f, FVector2D(5.0, 6.0));
		HudAbove(Back, 4).Text(TEXT("シマエナガ"), 52.0f, 18.0f, 26.0f, FLinearColor(1.0f, 0.75f, 0.85f), ETextAlign::Left, TEXT("Black"), 2.0f, Ink);
		HudAbove(Back, 4).Text(GagLine.Left(Shown), 52.0f, 66.0f, 38.0f, Paper, ETextAlign::Left, TEXT("Bold"), 2.0f, Ink);
	}
	// ---- Hold to skip (top right).
	{
		const FString Label = TEXT("長押しでスキップ");
		const float LabelSize = 22.0f;
		const float W = HudMeasure(Label, LabelSize, TEXT("Bold")) + 70.0f;
		const float X = Size.X / S - W - 26.0f;
		const FGeometry Space = MakeAnchor(Geometry, 0.0f, 0.0f, S);
		const FPainter Chip{Space, Elements, Layer + 55, 0.75f};
		HudShard(Chip, {{X + 8.0f, 20.0f}, {X + W, 20.0f}, {X + W - 8.0f, 62.0f}, {X, 62.0f}}, MakeFill(FLinearColor(0.1f, 0.13f, 0.26f), FLinearColor(0.03f, 0.04f, 0.1f)),
			2.0f, FVector2D(2.0, 3.0));
		HudAbove(Chip, 3).Text(Label, X + 22.0f, 28.0f, LabelSize, Paper, ETextAlign::Left, TEXT("Bold"), 1.5f, Ink);
		if (EndingSkipHold > 0.0f)
		{
			HudAbove(Chip, 4).Box(X + 10.0f, 56.0f, (W - 20.0f) * FMath::Clamp(EndingSkipHold, 0.0f, 1.0f), 4.0f, Gold);
		}
	}
}
