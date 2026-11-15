#include "ChaosImpactGadget.h"

#include "ChaosImpact.h"
#include "ChaosImpactBallFormComponent.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactFxKit.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSfx.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FLinearColor GadgetWhite(0.95f, 0.98f, 1.0f, 1.0f);
}

AChaosImpactGadget::AChaosImpactGadget()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	// It never moves and its state changes two or three times in its life (each sent at once: ForceNetUpdate).
	SetNetUpdateFrequency(2.0f);
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// A barrier's wall: balls, hooks and anchors stop at it (they look for the stage: WorldStatic); people and the
	// camera go through. Off until it is known to be a barrier.
	Block = CreateDefaultSubobject<UBoxComponent>(TEXT("Block"));
	Block->SetupAttachment(SceneRoot);
	Block->SetBoxExtent(FVector(BarrierThickness * 0.5f, BarrierWidth * 0.5f, BarrierHeight * 0.5f));
	Block->SetRelativeLocation(FVector(0.0f, 0.0f, BarrierHeight * 0.5f));
	Block->SetCollisionObjectType(ECC_WorldStatic);
	Block->SetCollisionResponseToAllChannels(ECR_Block);
	Block->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Block->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Block->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Block->SetCanEverAffectNavigation(false);
	Block->SetGenerateOverlapEvents(false);
}

void AChaosImpactGadget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChaosImpactGadget, State);
	DOREPLIFETIME(AChaosImpactGadget, Source);
}

double AChaosImpactGadget::GetNow() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.0;
}

bool AChaosImpactGadget::FindFloor(const UWorld* World, const FVector& Near, FVector& OutFloor)
{
	if (!World)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ChaosImpactGadgetFloor), false);
	for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
	{
		Query.AddIgnoredActor(*It);
	}
	FHitResult Floor;
	if (World->LineTraceSingleByObjectType(Floor, Near + FVector(0.0f, 0.0f, 70.0f), Near - FVector(0.0f, 0.0f, 700.0f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query) && !Floor.bStartPenetrating && Floor.ImpactNormal.Z > 0.6f)
	{
		OutFloor = Floor.ImpactPoint;
		return true;
	}
	return false;
}

AChaosImpactGadget* AChaosImpactGadget::Place(UWorld* World, const EChaosImpactBallType Type, const FVector& Floor, const FVector& Heading,
	APawn* Source)
{
	if (!World || World->GetNetMode() == NM_Client || (Type != EChaosImpactBallType::Barrier && Type != EChaosImpactBallType::Mine))
	{
		return nullptr;
	}
	if (Type == EChaosImpactBallType::Mine && Source)
	{
		// Two a player: a third takes the oldest away.
		TArray<AChaosImpactGadget*, TInlineAllocator<4>> Own;
		for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
		{
			if (It->IsMine() && !It->HasEnded() && It->Source == Source)
			{
				Own.Add(*It);
			}
		}
		Own.Sort([](const AChaosImpactGadget& A, const AChaosImpactGadget& B) { return A.State.PlacedAt < B.State.PlacedAt; });
		for (int32 Index = 0; Index <= Own.Num() - MinesPerPlayer; ++Index)
		{
			Own[Index]->End(false);
		}
	}
	FVector Flat = Heading.GetSafeNormal2D();
	if (Flat.IsNearlyZero())
	{
		Flat = Source ? Source->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
	AChaosImpactGadget* Gadget = World->SpawnActorDeferred<AChaosImpactGadget>(StaticClass(), FTransform(Flat.Rotation(), Floor), nullptr, Source,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Gadget)
	{
		return nullptr;
	}
	Gadget->State.Kind = static_cast<uint8>(Type);
	Gadget->Source = Source;
	Gadget->State.PlacedAt = Gadget->GetNow();
	Gadget->FinishSpawning(FTransform(Flat.Rotation(), Floor));
	Gadget->OnRep_State();
	Gadget->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("%s placed at %s by %s"), ChaosImpactBallTypes::GetInternalName(Type), *Floor.ToCompactString(),
		*GetNameSafe(Source));
	return Gadget;
}

void AChaosImpactGadget::ClearAll(UWorld* World)
{
	if (!World)
	{
		return;
	}
	for (TActorIterator<AChaosImpactGadget> It(World); It; ++It)
	{
		It->Destroy();
	}
}

void AChaosImpactGadget::BeginPlay()
{
	Super::BeginPlay();
	OnRep_State();
}

void AChaosImpactGadget::OnRep_State()
{
	if (State.Kind == 0)
	{
		return;
	}
	// The wall stands on every machine (a guest's own throws and hooks are shown stopping at it too).
	Block->SetCollisionEnabled(IsBarrier() && !HasEnded() ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!bLookBuilt && !HasEnded())
	{
		BuildLook();
	}
	if (HasEnded() && !bEndShown)
	{
		bEndShown = true;
		PlayEndEffect();
		JudgeOwnPlayers();
	}
}

void AChaosImpactGadget::End(const bool bBurst)
{
	if (!HasAuthority() || HasEnded())
	{
		return;
	}
	State.EndedAt = GetNow();
	State.bBurst = bBurst;
	// (Kept a moment: its last effect plays out, and a guest's report of the blast can still be checked.)
	SetLifeSpan(2.5f);
	OnRep_State();
	ForceNetUpdate();
}

void AChaosImpactGadget::AbsorbBall(APawn* Thrower)
{
	if (HasAuthority() && IsBarrier() && !HasEnded())
	{
		BrokenBy = Thrower;
		UE_LOG(LogChaosImpact, Log, TEXT("Barrier broken by a ball at %s"), *GetActorLocation().ToCompactString());
		End(true);
	}
}

bool AChaosImpactGadget::CanHurt(const AChaosImpactCharacter* Victim) const
{
	return IsValid(Victim) && !Victim->IsEliminated() && Victim->CanBeDamaged() && Victim != Source
		&& !(Victim->GetBallForm() && Victim->GetBallForm()->IsCaptured())
		&& !AChaosImpactGameState::AreTeammates(GetWorld(), Source, Victim);
}

bool AChaosImpactGadget::IsInReach(const AChaosImpactCharacter* Victim, const float Radius) const
{
	const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
	const FVector Body = Victim->GetActorLocation();
	const FVector Here = GetActorLocation();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	// (Its feet within a little of the floor it lies on: not someone on a ledge above, or jumping well over it.)
	const float Feet = static_cast<float>(Body.Z - Here.Z) - HalfHeight;
	return FVector::DistSquared2D(Body, Here) <= FMath::Square(Radius) && Feet > -120.0f && Feet < 130.0f;
}

void AChaosImpactGadget::ServerTickMine(const double Now)
{
	if (State.TriggeredAt > 0.0)
	{
		if (Now >= State.TriggeredAt + MineFuseSeconds)
		{
			Burst(Now);
		}
		return;
	}
	if (Now >= State.PlacedAt + MineSeconds)
	{
		End(false);
		return;
	}
	if (Now < State.PlacedAt + MineArmSeconds || Now < NextLookAt)
	{
		return;
	}
	NextLookAt = Now + 0.1;
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		if (CanHurt(*It) && IsInReach(*It, MineTriggerRadius))
		{
			State.TriggeredAt = Now;
			ForceNetUpdate();
			UE_LOG(LogChaosImpact, Log, TEXT("Mine set off by %s"), *It->GetName());
			return;
		}
	}
}

void AChaosImpactGadget::Burst(const double Now)
{
	End(true);
	// The host's own players and the CPUs here; a guest's player on their own screen (JudgeOwnPlayers there).
	for (TActorIterator<AChaosImpactCharacter> It(GetWorld()); It; ++It)
	{
		AChaosImpactCharacter* Victim = *It;
		if (!Victim->IsRemotePlayerOnServer() && CanHurt(Victim) && IsInReach(Victim, MineBlastRadius))
		{
			ApplyBlast(Victim);
		}
	}
}

void AChaosImpactGadget::ApplyBlast(AChaosImpactCharacter* Victim)
{
	if (Hurt.Contains(Victim))
	{
		return;
	}
	Hurt.Add(Victim);
	const float Applied = UGameplayStatics::ApplyDamage(Victim, 1.0f, Source ? Source->GetController() : nullptr, this, nullptr);
	if (Applied > 0.0f && !Victim->IsEliminated())
	{
		FVector Away = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = Victim->GetActorForwardVector().GetSafeNormal2D() * -1.0f;
		}
		Victim->ApplyBlastKnockback(Away * MineKnockback + FVector(0.0f, 0.0f, 420.0f));
	}
	UE_LOG(LogChaosImpact, Log, TEXT("Mine blast on %s: %.0f damage"), *Victim->GetName(), Applied);
}

bool AChaosImpactGadget::TryApplyReportedBlast(AChaosImpactCharacter* Victim)
{
	if (!HasAuthority() || !IsMine() || !State.bBurst || !HasEnded() || !CanHurt(Victim) || Hurt.Contains(Victim))
	{
		return false;
	}
	// Reaches here a round trip after their screen saw it; the host's copy of them trails them too.
	const float RoundTrip = Victim->GetNetworkRoundTripSeconds();
	const float Slack = FMath::Min(150.0f + 650.0f * RoundTrip, 750.0f);
	if (GetNow() - State.EndedAt > 1.0 + RoundTrip
		|| FVector::Dist2D(Victim->GetActorLocation(), GetActorLocation()) > MineBlastRadius + Slack)
	{
		UE_LOG(LogChaosImpact, Log, TEXT("Reported mine blast rejected: %s"), *Victim->GetName());
		return false;
	}
	ApplyBlast(Victim);
	return true;
}

void AChaosImpactGadget::JudgeOwnPlayers()
{
	// A guest: was this screen's own player in the blast, as this screen shows it? (A dash gets clear.)
	UWorld* World = GetWorld();
	if (bOwnJudged || HasAuthority() || !World || !IsMine() || !State.bBurst)
	{
		return;
	}
	bOwnJudged = true;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		AChaosImpactCharacter* Own = PlayerController && PlayerController->IsLocalController()
			? Cast<AChaosImpactCharacter>(PlayerController->GetPawn()) : nullptr;
		if (Own && !Own->IsDashing() && CanHurt(Own) && IsInReach(Own, MineBlastRadius))
		{
			Own->ReportGadgetBlastFromClient(this);
		}
	}
}

void AChaosImpactGadget::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetNow();
	if (HasAuthority() && !HasEnded())
	{
		if (IsMine())
		{
			ServerTickMine(Now);
		}
		else if (IsBarrier() && Now >= State.PlacedAt + BarrierSeconds)
		{
			End(false);
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateLook(Now, DeltaSeconds);
	}
}

AChaosImpactFxKit* AChaosImpactGadget::GetFx()
{
	AChaosImpactFxKit* Kit = Fx.Get();
	if (!IsValid(Kit))
	{
		Kit = AChaosImpactFxKit::Spawn(this, GetActorLocation());
		Fx = Kit;
	}
	return Kit;
}

void AChaosImpactGadget::BuildLook()
{
	bLookBuilt = true;
	using namespace ChaosImpactBallTypes;
	const EChaosImpactBallType Type = static_cast<EChaosImpactBallType>(State.Kind);
	const FLinearColor Color = GetColor(Type);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	// A part at Center (relative to the gadget), Size in centimetres along its own axes.
	const auto Part = [this](UStaticMesh* Mesh, UMaterialInterface* Look, const FVector& Center, const FVector& Size,
		const FRotator& Turn = FRotator::ZeroRotator) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetStaticMesh(Mesh);
		Piece->SetMaterial(0, Look);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		Piece->SetCanEverAffectNavigation(false);
		Piece->SetupAttachment(GetRootComponent());
		Piece->RegisterComponent();
		Piece->SetRelativeLocationAndRotation(Center, Turn);
		Piece->SetRelativeScale3D(Size / 100.0f);
		Parts.Add(Piece);
		return Piece;
	};
	UMaterialInstanceDynamic* Steel = MakeEmissive(this, FLinearColor(0.1f, 0.11f, 0.14f), 1.0f);
	BodyMaterial = MakeEmissive(this, Color, 2.6f);
	if (Type == EChaosImpactBallType::Barrier)
	{
		// A pane of light between two posts, its edge bright so its outline reads; the field shows through it.
		GlowMaterial = MakeAdditive(this, Color, 0.1f, 0.0f);
		BodyMaterial = MakeEmissive(this, Color, 1.5f);
		UMaterialInstanceDynamic* Post = MakeEmissive(this, FLinearColor(0.05f, 0.16f, 0.4f), 1.0f);
		const float Half = BarrierWidth * 0.5f;
		// Parts 0-2 rise with it (UpdateLook): the pane, its top edge, its middle line.
		Part(Cube, GlowMaterial, FVector(0.0f, 0.0f, BarrierHeight * 0.5f), FVector(6.0f, BarrierWidth - 14.0f, BarrierHeight));
		Part(Cube, BodyMaterial, FVector(0.0f, 0.0f, BarrierHeight), FVector(8.0f, BarrierWidth, 5.0f));
		Part(Cube, BodyMaterial, FVector(0.0f, 0.0f, BarrierHeight * 0.5f), FVector(7.0f, BarrierWidth - 14.0f, 2.0f));
		for (const float Side : {-1.0f, 1.0f})
		{
			Part(Cylinder, Post, FVector(0.0f, Side * Half, BarrierHeight * 0.5f), FVector(14.0f, 14.0f, BarrierHeight));
			Part(Sphere, BodyMaterial, FVector(0.0f, Side * Half, BarrierHeight + 4.0f), FVector(20.0f, 20.0f, 20.0f));
			Part(Cylinder, Post, FVector(0.0f, Side * Half, 4.0f), FVector(34.0f, 34.0f, 8.0f));
		}
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BarrierUp, GetActorLocation() + FVector(0.0f, 0.0f, 60.0f));
		if (AChaosImpactFxKit* Kit = GetFx())
		{
			FChaosImpactFxPiece Ring;
			Ring.Look = EChaosImpactFxLook::Ring;
			Ring.Location = FVector(0.0f, 0.0f, 4.0f);
			Ring.Life = 0.32f;
			Ring.SizeFrom = 120.0f;
			Ring.SizeTo = BarrierWidth * 1.25f;
			Ring.Color = Color;
			Ring.CoreColor = GadgetWhite;
			Ring.Intensity = 2.4f;
			Kit->Add(Ring);
		}
	}
	else
	{
		// A low dark disc with a red lamp in the middle and four studs round it: small, but never hidden.
		GlowMaterial = MakeAdditive(this, Color, 0.22f, 0.0f);
		// (A deep red: the kind's own colour, bright, washes out to pink on a light floor.)
		BodyMaterial = MakeEmissive(this, FLinearColor(0.8f, 0.01f, 0.03f), 0.85f);
		Part(Cylinder, Steel, FVector(0.0f, 0.0f, 6.0f), FVector(78.0f, 78.0f, 12.0f));
		Part(Cylinder, Steel, FVector(0.0f, 0.0f, 13.0f), FVector(50.0f, 50.0f, 4.0f));
		// Parts 2-3 are the lamp and its halo (UpdateLook blinks them).
		Part(Sphere, BodyMaterial, FVector(0.0f, 0.0f, 15.0f), FVector(30.0f, 30.0f, 24.0f));
		// (A solid red ring on the floor round it, not a glow: it reads on a bright floor as well as a dark one.)
		Part(Cylinder, BodyMaterial, FVector(0.0f, 0.0f, 1.5f), FVector(104.0f, 104.0f, 1.5f));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(45.0f + 90.0f * Index);
			Part(Cylinder, BodyMaterial, FVector(FMath::Cos(Angle) * 31.0f, FMath::Sin(Angle) * 31.0f, 13.0f), FVector(8.0f, 8.0f, 6.0f));
		}
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MineSet, GetActorLocation());
	}
	UpdateLook(GetNow(), 0.0f);
}

void AChaosImpactGadget::UpdateLook(const double Now, const float DeltaSeconds)
{
	if (!bLookBuilt)
	{
		return;
	}
	if (HasEnded())
	{
		// Gone at once (its end is an effect of its own).
		for (UStaticMeshComponent* Piece : Parts)
		{
			if (Piece && Piece->IsVisible())
			{
				Piece->SetVisibility(false);
			}
		}
		return;
	}
	const float Age = static_cast<float>(FMath::Max(0.0, Now - State.PlacedAt));
	if (IsBarrier())
	{
		// Up out of the floor in a moment; flickering in its last second and a half.
		const float Rise = FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(Age / BarrierRiseSeconds, 0.0f, 1.0f), 2.0f);
		const float Left = BarrierSeconds - Age;
		const bool bShown = Left > 1.5f || FMath::Fmod(Left, 0.24f) > 0.09f;
		if (Parts.Num() >= 3 && Parts[0] && Parts[1] && Parts[2])
		{
			Parts[0]->SetRelativeScale3D(FVector(0.06f, (BarrierWidth - 14.0f) / 100.0f, FMath::Max(0.01f, BarrierHeight * Rise / 100.0f)));
			Parts[0]->SetRelativeLocation(FVector(0.0f, 0.0f, BarrierHeight * Rise * 0.5f));
			Parts[1]->SetRelativeLocation(FVector(0.0f, 0.0f, BarrierHeight * Rise));
			Parts[2]->SetRelativeLocation(FVector(0.0f, 0.0f, BarrierHeight * Rise * 0.5f));
			Parts[0]->SetVisibility(bShown);
			Parts[2]->SetVisibility(bShown);
		}
		return;
	}
	// A mine: dark until armed, then a slow blink; set off, a fast one, with its reach drawn on the floor.
	const bool bArmed = Age >= MineArmSeconds;
	const bool bSetOff = State.TriggeredAt > 0.0;
	if (bArmed && !bArmShown)
	{
		// (Two beeps as it arms: heard by anyone near, its owner included.)
		bArmShown = true;
		if (!bSetOff && Age < MineArmSeconds + 0.5f)
		{
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MineArm, GetActorLocation());
		}
	}
	const float Blink = !bArmed ? 0.0f : bSetOff ? (FMath::Fmod(Age, 0.12f) < 0.07f ? 1.0f : 0.25f)
		: 0.45f + 0.55f * FMath::Square(FMath::Sin(Age * 3.2f));
	if (Parts.Num() >= 4 && Parts[2] && Parts[3])
	{
		Parts[2]->SetVisibility(bArmed);
		Parts[3]->SetVisibility(bArmed);
		const float Halo = bSetOff ? 1.3f : 1.0f;
		const float Ring = 1.04f * (0.82f + 0.18f * Blink) * Halo;
		Parts[3]->SetRelativeScale3D(FVector(Ring, Ring, 0.015f));
		// (The lamp itself goes out between blinks once it is set off: a hurry anyone can read.)
		Parts[2]->SetVisibility(bArmed && (!bSetOff || Blink > 0.5f));
	}
	if (bSetOff && !bTriggerShown)
	{
		bTriggerShown = true;
		ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MineAlarm, GetActorLocation());
		if (AChaosImpactFxKit* Kit = GetFx())
		{
			const FLinearColor Color = ChaosImpactBallTypes::GetColor(EChaosImpactBallType::Mine);
			const float Left = FMath::Clamp(static_cast<float>(State.TriggeredAt + MineFuseSeconds - Now), 0.1f, MineFuseSeconds);
			// Where the blast will reach, for as long as the fuse burns: an edge that holds, and a ring closing in on it.
			FChaosImpactFxPiece Edge;
			Edge.Look = EChaosImpactFxLook::SolidRing;
			Edge.Location = FVector(0.0f, 0.0f, 5.0f);
			Edge.Life = Left;
			Edge.SizeFrom = Edge.SizeTo = MineBlastRadius * 2.0f / 0.92f;
			Edge.RingFrom = Edge.RingTo = 0.92f;
			Edge.RingWidth = 0.035f;
			Edge.Color = Color;
			Edge.CoreColor = GadgetWhite;
			Edge.Intensity = 1.6f;
			Edge.Opacity = 0.85f;
			Edge.Attack = 0.05f;
			Edge.Hold = 0.9f;
			Edge.ErodeTo = 0.0f;
			Edge.NoiseAmount = 0.0f;
			Kit->Add(Edge);
			FChaosImpactFxPiece Closing = Edge;
			Closing.Look = EChaosImpactFxLook::Ring;
			Closing.RingFrom = 0.92f;
			Closing.RingTo = 0.12f;
			Closing.RingWidth = 0.05f;
			Closing.Intensity = 2.2f;
			Kit->Add(Closing);
			FChaosImpactFxPiece Fill;
			Fill.Look = EChaosImpactFxLook::GroundGlow;
			Fill.Location = FVector(0.0f, 0.0f, 4.0f);
			Fill.Life = Left;
			Fill.SizeFrom = Fill.SizeTo = MineBlastRadius * 2.0f;
			Fill.Color = Color;
			Fill.CoreColor = Color;
			Fill.Intensity = 0.55f;
			Fill.Attack = 0.1f;
			Fill.Hold = 0.9f;
			Fill.ErodeTo = 0.0f;
			Fill.NoiseAmount = 0.15f;
			Kit->Add(Fill);
		}
	}
}

void AChaosImpactGadget::PlayEndEffect()
{
	const FLinearColor Color = ChaosImpactBallTypes::GetColor(static_cast<EChaosImpactBallType>(State.Kind));
	AChaosImpactFxKit* Kit = GetFx();
	if (!Kit)
	{
		return;
	}
	Kit->StopEmitters();
	if (IsBarrier())
	{
		const FVector Middle(0.0f, 0.0f, BarrierHeight * 0.5f);
		if (State.bBurst)
		{
			// Broken by a ball: shards of light flying off along the wall.
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BarrierBreak, GetActorLocation() + Middle);
			const FVector Across = GetActorRightVector();
			for (int32 Index = 0; Index < 14; ++Index)
			{
				FChaosImpactFxPiece Shard;
				Shard.Look = EChaosImpactFxLook::Streak;
				Shard.Location = Across * FMath::FRandRange(-BarrierWidth * 0.5f, BarrierWidth * 0.5f)
					+ FVector(0.0f, 0.0f, FMath::FRandRange(15.0f, BarrierHeight));
				Shard.Velocity = GetActorForwardVector() * FMath::FRandRange(-380.0f, 380.0f) + Across * FMath::FRandRange(-220.0f, 220.0f)
					+ FVector(0.0f, 0.0f, FMath::FRandRange(120.0f, 420.0f));
				Shard.Gravity = 900.0f;
				Shard.Drag = 1.2f;
				Shard.Life = FMath::FRandRange(0.28f, 0.5f);
				Shard.SizeFrom = 70.0f;
				Shard.SizeTo = 20.0f;
				Shard.Width = 5.0f;
				Shard.Color = Color;
				Shard.CoreColor = GadgetWhite;
				Shard.Intensity = 3.0f;
				Kit->Add(Shard);
			}
			FChaosImpactFxPiece Flash;
			Flash.Location = Middle;
			Flash.Life = 0.16f;
			Flash.SizeFrom = 160.0f;
			Flash.SizeTo = 300.0f;
			Flash.Color = Color;
			Flash.CoreColor = GadgetWhite;
			Flash.Intensity = 2.0f;
			Kit->Add(Flash);
		}
		else
		{
			// Timed out: it sinks away quietly.
			ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::BarrierFade, GetActorLocation() + Middle);
			FChaosImpactFxPiece Fade;
			Fade.Look = EChaosImpactFxLook::Ring;
			Fade.Location = FVector(0.0f, 0.0f, 4.0f);
			Fade.Life = 0.35f;
			Fade.SizeFrom = BarrierWidth;
			Fade.SizeTo = BarrierWidth * 0.4f;
			Fade.Color = Color;
			Fade.CoreColor = GadgetWhite;
			Fade.Intensity = 1.4f;
			Kit->Add(Fade);
		}
		return;
	}
	if (!State.bBurst)
	{
		// A mine nobody stepped near: a little puff as it goes out.
		FChaosImpactFxPiece Puff;
		Puff.Look = EChaosImpactFxLook::Smoke;
		Puff.Location = FVector(0.0f, 0.0f, 20.0f);
		Puff.Velocity = FVector(0.0f, 0.0f, 60.0f);
		Puff.Life = 0.5f;
		Puff.SizeFrom = 50.0f;
		Puff.SizeTo = 110.0f;
		Puff.Color = FLinearColor(0.3f, 0.3f, 0.34f);
		Puff.Opacity = 0.5f;
		Kit->Add(Puff);
		return;
	}
	// The blast: a quick fireball no wider than its reach (over in a third of a second: the field is never hidden),
	// a shock ring out to the edge, and sparks.
	const FVector At = GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	ChaosImpactSfx::PlayAt(this, EChaosImpactSfx::MineBlast, At);
	FChaosImpactFxPiece Ball;
	Ball.Look = EChaosImpactFxLook::Flame;
	Ball.Location = FVector(0.0f, 0.0f, 55.0f);
	Ball.Life = 0.3f;
	Ball.SizeFrom = 150.0f;
	Ball.SizeTo = MineBlastRadius * 1.5f;
	Ball.Color = Color;
	Ball.CoreColor = FLinearColor(1.0f, 0.9f, 0.7f);
	Ball.Intensity = 1.6f;
	Ball.Opacity = 0.8f;
	Ball.Toon = 1.0f;
	Kit->Add(Ball);
	FChaosImpactFxPiece Shock;
	Shock.Look = EChaosImpactFxLook::Ring;
	Shock.Location = FVector(0.0f, 0.0f, 6.0f);
	Shock.Life = 0.3f;
	Shock.SizeFrom = 120.0f;
	Shock.SizeTo = MineBlastRadius * 2.0f / 0.92f;
	Shock.Color = Color;
	Shock.CoreColor = GadgetWhite;
	Shock.Intensity = 3.2f;
	Kit->Add(Shock);
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Angle = FMath::FRandRange(0.0f, UE_TWO_PI);
		FChaosImpactFxPiece Spark;
		Spark.Look = EChaosImpactFxLook::Streak;
		Spark.Location = FVector(0.0f, 0.0f, 25.0f);
		Spark.Velocity = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * FMath::FRandRange(500.0f, 950.0f)
			+ FVector(0.0f, 0.0f, FMath::FRandRange(150.0f, 520.0f));
		Spark.Gravity = 1100.0f;
		Spark.Drag = 2.2f;
		Spark.Life = FMath::FRandRange(0.22f, 0.4f);
		Spark.SizeFrom = 80.0f;
		Spark.SizeTo = 24.0f;
		Spark.Width = 4.0f;
		Spark.Color = Color;
		Spark.CoreColor = FLinearColor(1.0f, 0.9f, 0.7f);
		Spark.Intensity = 3.4f;
		Kit->Add(Spark);
	}
	FChaosImpactFxPiece Scorch;
	Scorch.Look = EChaosImpactFxLook::Smoke;
	Scorch.Location = FVector(0.0f, 0.0f, 50.0f);
	Scorch.Velocity = FVector(0.0f, 0.0f, 90.0f);
	Scorch.Delay = 0.12f;
	Scorch.Life = 0.55f;
	Scorch.SizeFrom = 120.0f;
	Scorch.SizeTo = 230.0f;
	Scorch.Color = FLinearColor(0.16f, 0.14f, 0.15f);
	Scorch.Opacity = 0.4f;
	Kit->Add(Scorch);
}
