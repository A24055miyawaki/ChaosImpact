#include "RPGAbilityGates.h"
#include "RPGPrototype.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactBallTypes.h"
#include "ChaosImpactTackleComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"

// Boulders, ice walls and leaps (2026-11). Placed by the editor's ExtendLevel; the secret places they guard are in RPGWorldLayout.

namespace
{
UStaticMesh* GateMesh(const TCHAR* Name)
{
	const FString Path = FString(Name).StartsWith(TEXT("Kit"))
		? FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Meshes/SM_%s.SM_%s"), Name, Name)
		: FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
	return LoadObject<UStaticMesh>(nullptr, *Path);
}
UMaterialInterface* GateMaterial(const TCHAR* Name)
{
	if (FString(Name).StartsWith(TEXT("/"))) { return LoadObject<UMaterialInterface>(nullptr, Name); }
	return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"), Name, Name));
}
UStaticMeshComponent* GatePart(AActor* Owner, TArray<TObjectPtr<UStaticMeshComponent>>& Pieces, const TCHAR* Mesh, const TCHAR* Material,
	const FVector& At, const FVector& Size, const FRotator& Rotation, bool bSolid)
{
	auto* Piece = NewObject<UStaticMeshComponent>(Owner);
	Piece->SetupAttachment(Owner->GetRootComponent());
	Piece->SetStaticMesh(GateMesh(Mesh));
	if (UMaterialInterface* M = GateMaterial(Material)) { Piece->SetMaterial(0, M); }
	Piece->SetRelativeLocation(At); Piece->SetRelativeRotation(Rotation); Piece->SetRelativeScale3D(Size / 100);
	Piece->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Piece->SetCollisionResponseToAllChannels(ECR_Block);
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->RegisterComponent();
	Pieces.Add(Piece);
	return Piece;
}
}

// ---------------------------------------------------------------- boulders and ice

ARPGGateObstacle::ARPGGateObstacle()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GateRoot")));
}

void ARPGGateObstacle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGGateObstacle, Kind);
	DOREPLIFETIME(ARPGGateObstacle, bOpen);
}

void ARPGGateObstacle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (GetWorld() && GetWorld()->IsGameWorld() && Pieces.IsEmpty()) { Build(); }
}

void ARPGGateObstacle::Build()
{
	const float Half = Width * 0.5f;
	if (Kind == Ice)
	{
		// A wall of clear ice across the way, spires on top.
		const TCHAR* IceSurface = TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface");
		GatePart(this, Pieces, TEXT("Cube"), IceSurface, FVector(0, 0, 220), FVector(160, Width, 440), FRotator::ZeroRotator, true);
		for (int32 I = 0; I < 5; ++I)
		{
			const float Y = -Half + Width * (I + 0.5f) / 5;
			GatePart(this, Pieces, TEXT("KitGem"), TEXT("Crystal"), FVector(FMath::FRandRange(-40.0f, 40.0f), Y, 480), FVector(90, 90, FMath::FRandRange(180.0f, 300.0f)), FRotator(FMath::FRandRange(-12.0f, 12.0f), 0, FMath::FRandRange(-12.0f, 12.0f)), false);
		}
		Glow = NewObject<UPointLightComponent>(this);
		Glow->SetupAttachment(GetRootComponent()); Glow->SetRelativeLocation(FVector(-150, 0, 260));
		Glow->SetLightColor(FLinearColor(0.4f, 0.8f, 1.0f)); Glow->SetIntensity(2500); Glow->SetAttenuationRadius(700); Glow->SetCastShadows(false);
		Glow->RegisterComponent();
	}
	else
	{
		// Boulders heaped across the way, glowing cracks running over the front one.
		GatePart(this, Pieces, TEXT("KitRock"), TEXT("Rock"), FVector(0, 0, 150), FVector(320, Width * 0.8f, 320), FRotator(0, 0, 5), true);
		GatePart(this, Pieces, TEXT("KitRockB"), TEXT("Rock"), FVector(60, -Half * 0.7f, 110), FVector(240, 240, 230), FRotator(0, 40, 0), true);
		GatePart(this, Pieces, TEXT("KitRockB"), TEXT("Rock"), FVector(40, Half * 0.7f, 100), FVector(230, 230, 210), FRotator(0, -30, 0), true);
		for (int32 I = 0; I < 4; ++I)
		{
			GatePart(this, Pieces, TEXT("Cube"), TEXT("GlowWarm"), FVector(-158, FMath::FRandRange(-Half * 0.5f, Half * 0.5f), FMath::FRandRange(90.0f, 230.0f)),
				FVector(6, FMath::FRandRange(70.0f, 140.0f), 8), FRotator(0, 0, FMath::FRandRange(-60.0f, 60.0f)), false);
		}
	}
}

void ARPGGateObstacle::Open()
{
	if (bOpen) { return; }
	bOpen = true;
	OnRep_Open();
	ARPGPrototypeVFX::PlayImpact(GetWorld(), Kind == Ice ? EChaosImpactBallType::Fire : EChaosImpactBallType::Normal, GetActorLocation() + FVector(0, 0, 150), 2.4f, true);
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsGrunt() && FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) < FMath::Square(2500.0f))
		{ It->ShowNotice(Kind == Ice ? TEXT("氷の壁が溶けた！ 奥へ進めそうだ") : TEXT("大岩が砕けた！ 奥へ進めそうだ"), 3); }
	}
}

void ARPGGateObstacle::OnRep_Open()
{
	// Gone (shrinking away over a moment; nothing blocks from now on).
	OpenAge = 0;
	for (UStaticMeshComponent* Piece : Pieces) { if (Piece) { Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision); } }
}

void ARPGGateObstacle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (OpenAge >= 0)
	{
		OpenAge += DeltaSeconds;
		const float Left = FMath::Clamp(1 - OpenAge / 0.6f, 0.0f, 1.0f);
		SetActorScale3D(FVector(FMath::Max(0.01f, Left)));
		if (Glow) { Glow->SetIntensity(2500 * Left); }
		if (Left <= 0) { SetActorHiddenInGame(true); SetActorTickEnabled(false); }
		return;
	}
	if (!HasAuthority() || bOpen) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Here = GetActorLocation();
	if (Kind == Ice)
	{
		// Fire melts it: fire balls flying by, fire left burning against it.
		for (TActorIterator<ARPGPrototypeBall> It(GetWorld()); It; ++It)
		{ if (It->ShotItem.Type == EChaosImpactBallType::Fire && FVector::DistSquared(It->GetActorLocation(), Here + FVector(0, 0, 150)) < FMath::Square(Width * 0.5f + 140)) { Melt += DeltaSeconds * 3; } }
		for (TActorIterator<ARPGPrototypeEffect> It(GetWorld()); It; ++It)
		{ if (It->Item.Type == EChaosImpactBallType::Fire && FVector::DistSquared2D(It->GetActorLocation(), Here) < FMath::Square(Width * 0.5f + 220)) { Melt += DeltaSeconds * 0.9f; } }
		if (Melt >= 1) { Open(); return; }
	}
	for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It)
	{
		ARPGPrototypeCharacter* Hero = *It;
		if (Hero->IsGrunt() || Hero->IsEliminated() || FVector::DistSquared2D(Hero->GetActorLocation(), Here) > FMath::Square(Width * 0.5f + 260)) { continue; }
		if (Kind == Boulder && Hero->GetTackle() && Hero->GetTackle()->IsActive())
		{
			if (Hero->GetTackleLevel() >= 3) { Open(); return; }
			if (Now >= NextHintAt) { NextHintAt = Now + 3; Hero->ShowNotice(TEXT("びくともしない。タックルを鍛えれば砕けそうだ（タックル Lv3：コロニーの道場）"), 3); }
		}
		else if (Now >= NextHintAt)
		{
			NextHintAt = Now + 9;
			Hero->ShowNotice(Kind == Ice ? TEXT("厚い氷の壁が道をふさいでいる。炎の球をぶつければ溶けそうだ")
				: TEXT("ひびの入った大岩が道をふさいでいる。強いタックルなら砕けるかもしれない"), 3);
		}
	}
}

// ---------------------------------------------------------------- leaps

ARPGLeapPoint::ARPGLeapPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("LeapRoot")));
}

void ARPGLeapPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (GetWorld() && GetWorld()->IsGameWorld() && Pieces.IsEmpty()) { Build(); }
}

void ARPGLeapPoint::Build()
{
	// A round stone pad with a glowing ring and an arrow toward the far side.
	const FVector Toward = (Partner - GetActorLocation()).GetSafeNormal2D();
	const FRotator Facing(0, Toward.Rotation().Yaw - GetActorRotation().Yaw, 0);
	GatePart(this, Pieces, TEXT("KitCylinder"), TEXT("Stone"), FVector(0, 0, 6), FVector(300, 300, 14), FRotator::ZeroRotator, false);
	Ring = GatePart(this, Pieces, TEXT("KitCylinder"), TEXT("GlowTeal"), FVector(0, 0, 14), FVector(250, 250, 3), FRotator::ZeroRotator, false);
	GatePart(this, Pieces, TEXT("Cone"), TEXT("GlowTeal"), Facing.RotateVector(FVector(90, 0, 22)), FVector(70, 70, 16), Facing + FRotator(-90, 0, 0), false);
}

void ARPGLeapPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Ring) { const float S = 2.5f + 0.25f * FMath::Sin(Clock * 3); Ring->SetRelativeScale3D(FVector(S, S, 0.03f)); }
}

ARPGLeapPoint* ARPGLeapPoint::Under(const ARPGPrototypeCharacter* Player)
{
	if (!Player || !Player->GetWorld()) { return nullptr; }
	for (TActorIterator<ARPGLeapPoint> It(Player->GetWorld()); It; ++It)
	{
		if (FVector::DistSquared2D(It->GetActorLocation(), Player->GetActorLocation()) < FMath::Square(230.0f)
			&& FMath::Abs(It->GetActorLocation().Z - Player->GetActorLocation().Z) < 260) { return *It; }
	}
	return nullptr;
}

void ARPGLeapPoint::Launch(ARPGPrototypeCharacter* Player) const
{
	if (!Player) { return; }
	// A high arc landing on the far pad in a little under a second (the hero's own frames carry it: air braking
	// would stop a thrown body short of the far side).
	const FVector From = Player->GetActorLocation();
	Player->StartLeap(Partner + FVector(0, 0, Player->GetCapsuleComponent() ? Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20 : 110));
	ARPGPrototypeVFX::PlayImpact(Player->GetWorld(), EChaosImpactBallType::Wind, From, 1.0f, false);
}

void ARPGPrototypeCharacter::StartLeap(const FVector& To)
{
	LeapFrom = GetActorLocation(); LeapTo = To; LeapStartedAt = GetWorld()->GetTimeSeconds();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
}

void ARPGPrototypeCharacter::UpdateLeap()
{
	if (LeapStartedAt < 0) { return; }
	constexpr float Flight = 0.95f, Height = 520.0f;
	const float Alpha = IsEliminated() ? 1.0f : FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - LeapStartedAt) / Flight), 0.0f, 1.0f);
	if (!IsEliminated()) { SetActorLocation(FMath::Lerp(LeapFrom, LeapTo, Alpha) + FVector(0, 0, Height * 4 * Alpha * (1 - Alpha)), false, nullptr, ETeleportType::None); }
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	if (Alpha >= 1) { LeapStartedAt = -1; GetCharacterMovement()->SetMovementMode(MOVE_Falling); }
}

// ---------------------------------------------------------------- the dojo: tackle and dash levels

bool ARPGPrototypeCharacter::OpenTraining()
{
	if (bGrunt || IsEliminated() || bDowned) { return false; }
	ShowLesson(TEXT("Dojo"));
	return OpenQuestPanel(TrainingPanel);
}

bool ARPGPrototypeCharacter::CanLearnSkill(int32 Which, FString* Why) const
{
	const auto Fail = [Why](const FString& Reason) { if (Why) { *Why = Reason; } return false; };
	const int32 Level = Which == 0 ? TackleSkill : DashSkill;
	if (Level >= 4) { return Fail(TEXT("もう極めている")); }
	const RPGTuning::FSkillStep& Step = (Which == 0 ? RPGTuning::TackleSteps : RPGTuning::DashSteps)[Level - 1];
	if (RPGLevel < Step.MinLevel) { return Fail(FString::Printf(TEXT("Lv.%d から修行できる"), Step.MinLevel)); }
	if (Step.Boss >= 0 && !(BossesBeaten & (1 << Step.Boss))) { return Fail(Step.Boss == 0 ? TEXT("暴走マグロを倒すと修行できる") : TEXT("シマエナガを倒すと修行できる")); }
	if (Rubies < Step.Rubies) { return Fail(TEXT("ルビーが足りない")); }
	if (TuneShards < Step.Shards) { return Fail(TEXT("チューンの欠片が足りない")); }
	return true;
}

bool ARPGPrototypeCharacter::LearnSkill(int32 Which)
{
	FString Why;
	if (!CanLearnSkill(Which, &Why)) { Notice = Why; NoticeUntil = GetWorld()->GetTimeSeconds() + 2.5; return false; }
	uint8& Level = Which == 0 ? TackleSkill : DashSkill;
	const RPGTuning::FSkillStep& Step = (Which == 0 ? RPGTuning::TackleSteps : RPGTuning::DashSteps)[Level - 1];
	Rubies -= Step.Rubies; TuneShards -= Step.Shards; ++Level;
	ApplySkills();
	TutorialEvent(TEXT("Learn"));
	Notice = FString::Printf(TEXT("「%s」を身につけた！ %s"), Step.Name, Step.Effect); NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
	AutosaveHero();
	return true;
}

bool ARPGPrototypeCharacter::HandleTrainingKey(FKey Key)
{
	if (!IsTrainingOpen()) { return false; }
	if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left) { SetQuestMenuOpen(false); return true; }
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down) { TrainingSelection = 1 - TrainingSelection; return true; }
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { LearnSkill(TrainingSelection); return true; }
	return true;
}

void ARPGPrototypeCharacter::ApplySkills()
{
	// Tackle: harder knockback from level 2, quick again at level 4. Dash: further from level 2 (its pace: the passives).
	if (UChaosImpactTackleComponent* Rush = GetTackle())
	{
		Rush->KnockbackSpeed = TackleSkill >= 2 ? 1900.0f : 1500.0f;
		Rush->CooldownSeconds = TackleSkill >= 4 ? 0.6f : 1.1f;
	}
	// Untrained a dash is short and tiring; each lesson takes it further for less.
	static constexpr float Distances[] = {160.0f, 260.0f, 285.0f, 330.0f};
	static constexpr float Costs[] = {1.6f, 1.3f, 1.2f, 0.9f};
	DashDistance = Distances[FMath::Clamp<int32>(DashSkill, 1, 4) - 1];
	DashCost = Costs[FMath::Clamp<int32>(DashSkill, 1, 4) - 1];
	if (!HasAuthority() && IsLocallyControlled()) { ServerSetSkills(TackleSkill, DashSkill); }
}

void ARPGPrototypeCharacter::ServerSetSkills_Implementation(uint8 InTackle, uint8 InDash)
{
	TackleSkill = FMath::Clamp<uint8>(InTackle, 1, 4); DashSkill = FMath::Clamp<uint8>(InDash, 1, 4);
	ApplySkills();
}

void ARPGPrototypeCharacter::OnTackledSomeone(AChaosImpactCharacter* Victim)
{
	// From level 2 a tackle hurts enemies; from level 3 it stuns them a moment (bosses shrug that off).
	auto* Enemy = Cast<ARPGPrototypeCharacter>(Victim);
	if (bGrunt || TackleSkill < 2 || !Enemy || !Enemy->IsGrunt() || Enemy->IsEliminated()) { return; }
	const float Damage = (RPGTuning::TackleDamageBase + RPGTuning::TackleDamagePerLevel * RPGLevel) * (TackleSkill >= 4 ? 1.5f : 1.0f);
	UGameplayStatics::ApplyDamage(Enemy, Damage, GetController(), this, nullptr);
	if (TackleSkill >= 3 && !Enemy->IsCrowdControlImmune()) { Enemy->ApplyRPGStun(1.0f); }
}
