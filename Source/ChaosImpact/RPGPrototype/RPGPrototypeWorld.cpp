#include "RPGPrototype.h"
#include "RPGTuning.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "RPGAdventure.h"
#include "RPGHealing.h"
#include "RPGOnline.h"
#include "ChaosImpactGameViewportClient.h"

ARPGPrototypeLoot::ARPGPrototypeLoot()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LootBall"));
	SetRootComponent(Mesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	Mesh->SetStaticMesh(Sphere.Object);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetRelativeScale3D(FVector(0.48f));
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("LootLight"));
	Light->SetupAttachment(Mesh);
	Light->SetIntensity(90);
	Light->SetAttenuationRadius(180);
	Light->SetCastShadows(false);
}

void ARPGPrototypeLoot::BeginPlay()
{
	Super::BeginPlay();
	if (!Item.Id.IsValid()) { Item.Id = FGuid::NewGuid(); }
	RestLocation = GetActorLocation();
	const FLinearColor Color = Item.Kind==ERPGItemKind::Ball?ChaosImpactBallTypes::GetColor(Item.Type):
		Item.Kind==ERPGItemKind::LifeBadge?FLinearColor(.2f,1,.65f):Item.Kind==ERPGItemKind::MobilityBadge?FLinearColor(.1f,.6f,1):FLinearColor(.8f,.3f,1);
	if(Item.Kind!=ERPGItemKind::Ball) { Mesh->SetRelativeScale3D(FVector(.45f,.45f,.15f)); Mesh->SetRelativeRotation(FRotator(45,0,45)); }
	Mesh->SetMaterial(0, ChaosImpactBallTypes::MakeEmissive(this, Color, 1.7f));
	if (auto* Light = FindComponentByClass<UPointLightComponent>()) { Light->SetLightColor(Color); }
	BaseScale=Mesh->GetRelativeScale3D();
	SetOwner(Recipient);
	Mesh->SetOnlyOwnerSee(true);
	// A world light would reveal a private drop to everybody's split-screen view.
	if(auto* Light=FindComponentByClass<UPointLightComponent>()) { Light->SetIntensity(0); }
	if(!bChestLoot) { SetLifeSpan(FMath::Max(1.0f,LifetimeSeconds)); }
}

void ARPGPrototypeLoot::Pop(const FVector& Velocity)
{
	PopVelocity=Velocity; bFlying=true; PopBounces=0; PopOrigin=GetActorLocation();
}

void ARPGPrototypeLoot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
	Mesh->SetOnlyOwnerSee(!Mode || !Mode->UsesSharedScreen());
	if(bFlying)
	{
		// Out of the chest in an arc; it bounces lower each time, then rests floating a little above the ground.
		const FVector From=GetActorLocation();
		PopVelocity.Z-=1800*DeltaSeconds;
		FVector To=From+PopVelocity*DeltaSeconds;
		FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGLootPop),false,this);
		if(Recipient) { Q.AddIgnoredActor(Recipient); }
		if(GetWorld()->LineTraceSingleByObjectType(Hit,From,To-FVector(0,0,45),FCollisionObjectQueryParams(ECC_WorldStatic),Q))
		{
			if(Hit.ImpactNormal.Z<.6f) { PopVelocity=FVector::VectorPlaneProject(PopVelocity,Hit.ImpactNormal)*.5f+Hit.ImpactNormal*120; To=Hit.Location+Hit.ImpactNormal*8; }
			else if(++PopBounces>=3 || PopVelocity.Z>-260) { bFlying=false; To=Hit.ImpactPoint+FVector(0,0,45); RestLocation=To; }
			else { To=Hit.ImpactPoint+FVector(0,0,45); PopVelocity.Z*=-.38f; PopVelocity.X*=.6f; PopVelocity.Y*=.6f; }
		}
		// Off a ledge: back beside where it came from.
		if(To.Z<PopOrigin.Z-200 || GetGameTimeSinceCreation()>3) { bFlying=false; To=PopOrigin+FVector(0,0,-60); RestLocation=To; }
		SetActorLocation(To); AddActorLocalRotation(FRotator(0,420*DeltaSeconds,0));
		return;
	}
	// Follow editor/field movement without snapping a wind-moved drop back to its spawn.
	const float Bob=9*FMath::Sin(GetWorld()->GetTimeSeconds()*2.5f);
	FVector At=GetActorLocation(); At.Z=RestLocation.Z+Bob; SetActorLocation(At);
	AddActorLocalRotation(FRotator(0, 55 * DeltaSeconds, 0));
	if(bChestLoot) { return; }
	const float Left=GetLifeSpan(); const float Fade=FMath::Clamp(Left/5.0f,.25f,1.0f);
	Mesh->SetRelativeScale3D(BaseScale*Fade);
	Mesh->SetVisibility(Left>5 || FMath::Fmod(Left,.35f)>.1f);
}

bool ARPGPrototypeLoot::Collect(ARPGPrototypeCharacter* Player)
{
	if (bCollected || bFlying || !IsValid(Player) || !IsVisibleTo(Player) || Player->IsEliminated() || Player->IsGrunt()
		|| FVector::DistSquared2D(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(230.0f)
		|| Player->FindNearbyLoot() != this || !Player->AddLoot(Item)) { return false; }
	bCollected = true;
	Destroy();
	return true;
}

ARPGPrototypeSpawnPoint::ARPGPrototypeSpawnPoint()
{
	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("EnemySpawn"));
	SetRootComponent(Arrow);
	Arrow->ArrowColor = FColor(255, 70, 90);
	Arrow->ArrowSize = 1.8f;
	Arrow->SetHiddenInGame(true);
}

ARPGPrototypeGruntController::ARPGPrototypeGruntController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
}

void ARPGPrototypeGruntController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	auto* Enemy = Cast<ARPGPrototypeCharacter>(GetPawn());
	if (Enemy && Enemy->SpawnPoint.IsValid() && Enemy->SpawnPoint->bEncounterGroup)
	{ TickEncounter(DeltaSeconds); return; }
	auto* Target = Enemy?ARPGPrototypeGameMode::NearestPlayer(GetWorld(),Enemy->GetActorLocation()):nullptr;
	if (!Enemy || !Target || Enemy->IsEliminated() || Target->IsEliminated() || Enemy->IsTrainingMenuFrozen()
		|| Enemy->IsIceFrozen() || Enemy->IsRPGStunned() || Enemy->IsRPGKnockedBack()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	FVector Toward = (Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
	const float Distance = FVector::Dist2D(Target->GetActorLocation(), Enemy->GetActorLocation());
	if (Enemy->IsChargingThrow())
	{
		// Aim is deliberately committed at the start: no perfect tracking of the player's dodge.
		if (Now >= ReleaseAt) { Enemy->EndThrowInput(); NextThrowAt = Now + 3.2f + FMath::FRandRange(0, 0.8f); }
		return;
	}
	const bool Adventure=ARPGAdventureWorld::Find(GetWorld())!=nullptr;
	const FVector Home=Enemy->SpawnPoint.IsValid()?Enemy->SpawnPoint->GetActorLocation():Enemy->GetActorLocation();
	const bool Leashed=Adventure && FVector::DistSquared2D(Enemy->GetActorLocation(),Home)>FMath::Square(1300.0f);
	const bool Aggro=Distance<1850 && !Leashed;
	const bool Decoy=Now<Target->DecoyUntil && FVector::DistSquared2D(Enemy->GetActorLocation(),Target->DecoyLocation)<FMath::Square(900.0f);
	const FVector AimAt=Decoy?Target->DecoyLocation:Target->GetActorLocation();
	Toward=(AimAt-Enemy->GetActorLocation()).GetSafeNormal2D();
	Enemy->SetAIAimDirection(Toward);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGGruntSight), false, Enemy);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Enemy->GetActorLocation(), Target->GetActorLocation(), ECC_WorldStatic, Params);
	if(Now>=NextWanderAt)
	{ StrafeSign=FMath::FRand()<.5f?-1:1; NextWanderAt=Now+FMath::FRandRange(1.4f,2.8f); }
	FVector Desired;
	if(!Aggro)
	{
		const FVector Patrol=Home+FVector(FMath::Cos(Now*.25+Enemy->GetCPUNumber())*260,FMath::Sin(Now*.25+Enemy->GetCPUNumber())*260,0);
		Desired=((Leashed?Home:Patrol)-Enemy->GetActorLocation()).GetSafeNormal2D();
	}
	else if(Distance>650 || bBlocked) { Desired=Toward; }
	else if(Distance<280) { Desired=(-Toward+FVector::CrossProduct(FVector::UpVector,Toward)*StrafeSign*.4f).GetSafeNormal2D(); }
	else { Desired=FVector::CrossProduct(FVector::UpVector,Toward)*StrafeSign*.65f; }
	if (!Desired.IsNearlyZero())
	{
		FVector Move = Now < AvoidUntil ? AvoidDirection : Desired;
		bool Found=false;
		for(float Angle:{0.0f,45.0f,-45.0f,85.0f,-85.0f,140.0f,-140.0f})
		{
			const FVector Candidate=Move.RotateAngleAxis(Angle*StrafeSign,FVector::UpVector).GetSafeNormal2D();
			FHitResult Wall,Floor; const FVector End=Enemy->GetActorLocation()+Candidate*160;
			if(const auto* Place=ARPGAdventureWorld::Find(GetWorld());Place && Place->IsInColony(End)) { continue; }
			const bool Block=GetWorld()->SweepSingleByObjectType(Wall,Enemy->GetActorLocation(),End,FQuat::Identity,
				FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(35),Params);
			const bool Ground=GetWorld()->LineTraceSingleByObjectType(Floor,End,End-FVector(0,0,260),FCollisionObjectQueryParams(ECC_WorldStatic),Params);
			if(!Block && Ground)
			{ Found=true; Move=Candidate; if(Angle!=0) { AvoidDirection=Candidate; AvoidUntil=Now+.55; } break; }
		}
		if(Found) { Enemy->AddMovementInput(Move,Aggro?1:.6f); }
		if(FVector::DistSquared2D(LastLocation,Enemy->GetActorLocation())<1 && Aggro && Now>=AvoidUntil)
		{ AvoidDirection=-Toward; AvoidUntil=Now+.7; StrafeSign=-StrafeSign; }
		LastLocation=Enemy->GetActorLocation();
	}
	if (Aggro && Distance < 900 && !bBlocked && Now >= NextThrowAt && Enemy->GetCarriedBallCount() > 0 && !Enemy->IsThrowReleasePending())
	{
		const float Error=10+Enemy->GetAimError();
		Enemy->SetAIAimDirection(Toward.RotateAngleAxis(FMath::FRandRange(-Error,Error), FVector::UpVector));
		Enemy->BeginThrowInput();
		ReleaseAt = Now + 0.3;
	}
}

ARPGPrototypeGameMode::ARPGPrototypeGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = ARPGPrototypePlayerController::StaticClass();
	PlayerStateClass = AChaosImpactPlayerState::StaticClass();
	GameStateClass = AChaosImpactGameState::StaticClass();
	DefaultPawnClass = ARPGPrototypeCharacter::StaticClass();
	GruntClass = ARPGPrototypeCharacter::StaticClass();
}

void ARPGPrototypeGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Direct editor Play and command-line launches enter playing, never the main title/demo flow.
	GetWorld()->URL.AddOption(TEXT("CIOpenWorld=1"));
	GetWorld()->URL.AddOption(TEXT("CIRPGPrototype=1"));
	DesiredLocalPlayers=FMath::Clamp(FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CILocalPlayers="),TEXT("1"))),1,4);
	bSharedScreen=FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CISharedScreen="),TEXT("0")))!=0;
	const int32 Keyboard = FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="), TEXT("0")));
	if (Keyboard>=DesiredLocalPlayers || Keyboard<INDEX_NONE) { GetWorld()->URL.RemoveOption(TEXT("CIKeyboardPlayer=")); GetWorld()->URL.AddOption(TEXT("CIKeyboardPlayer=0")); }
}

void ARPGPrototypeGameMode::BeginPlay()
{
	Super::BeginPlay();
    if(auto* Viewport=GetWorld()->GetGameViewport())
    {
        SharedBeginDrawHandle=Viewport->OnBeginDraw().AddUObject(this,&ARPGPrototypeGameMode::BeginSharedViewDraw);
        SharedEndDrawHandle=Viewport->OnEndDraw().AddUObject(this,&ARPGPrototypeGameMode::EndSharedViewDraw);
    }
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this]() { SetLocalPlayerCount(DesiredLocalPlayers); }));
	ARPGNetRelay::Ensure(GetWorld());
	// Online: the snowfield boss's warnings and blasts on guests' screens too.
	ARPGNetRelay::BindSimaeFx(true);
	// A pad nobody holds can join mid-game (RPGPrototypeSession.cpp, HandleUnownedPad).
	UChaosImpactGameViewportClient::UnownedGamepadHandler=[Self=TWeakObjectPtr<ARPGPrototypeGameMode>(this)](const FInputKeyEventArgs& Args) { return Self.IsValid() && Self->HandleUnownedPad(Args); };
}

void ARPGPrototypeGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UChaosImpactGameViewportClient::UnownedGamepadHandler=nullptr;
    ARPGNetRelay::BindSimaeFx(false);
    if(auto* Viewport=GetWorld()->GetGameViewport())
    {
        Viewport->OnBeginDraw().Remove(SharedBeginDrawHandle);
        Viewport->OnEndDraw().Remove(SharedEndDrawHandle);
        if(bSharedScreen) { Viewport->SetForceDisableSplitscreen(false); }
    }
    Super::EndPlay(EndPlayReason);
}

void ARPGPrototypeGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickSharedScreen(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	DamageTexts.RemoveAll([&](const FRPGPrototypeDamageText& Text) { return Now - Text.StartedAt > 1.15; });
	for(auto It=Contributions.CreateIterator();It;++It) { if(!It.Key().IsValid() || It.Key()->IsEliminated()) { It.RemoveCurrent(); } }
	if (bResetPending) { return; }
	ARPGPrototypeCharacter* Player = Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player) { return; }
	int32 Count = 0;
	for (TActorIterator<ARPGPrototypeSpawnPoint> It(GetWorld()); It; ++It)
	{
		++Count;
		if (It->bEncounterGroup) { It->TickEncounter(this, Now); continue; }
		if (!It->bEnabled || It->Enemy.IsValid() || Now < It->NextSpawnAt) { continue; }
		const FTransform Where = It->GetActorTransform();
		auto* Enemy = GetWorld()->SpawnActorDeferred<ARPGPrototypeCharacter>(GruntClass, Where, *It, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Enemy) { continue; }
		Enemy->SetEnemy(true, It->EnemyLevel);
		Enemy->SpawnPoint = *It;
		Enemy->SetCPUNumber(Count);
		Enemy->OnEliminated.AddDynamic(this, &ARPGPrototypeGameMode::HandleEliminated);
		Enemy->FinishSpawning(Where);
		auto* CPU = GetWorld()->SpawnActor<ARPGPrototypeGruntController>();
		if (CPU) { CPU->Possess(Enemy); }
		It->Enemy = Enemy;
	}
}

void ARPGPrototypeGameMode::AddDamageText(ARPGPrototypeCharacter* Victim, float Amount)
{
	FRPGPrototypeDamageText Text;
	Text.Location = Victim->GetActorLocation() + FVector(0, FMath::FRandRange(-24.0f, 24.0f), 95);
	Text.Amount = Amount;
	Text.StartedAt = GetWorld()->GetTimeSeconds();
	Text.bPlayer = !Victim->IsGrunt();
	if (DamageTexts.Num() >= 100) { DamageTexts.RemoveAt(0); }
	DamageTexts.Add(Text);
	// Online: the same number on every guest's screen.
	if (GetWorld()->GetNetMode() == NM_ListenServer) { if (auto* Relay = ARPGNetRelay::Get(GetWorld())) { Relay->MulticastDamageText(Text.Location, Amount, Text.bPlayer); } }
	if(!Victim->IsGrunt() || Victim->IsEliminated()) { return; }
	AActor* Causer=Victim->GetLastDamageCauser();
	APawn* Pawn=Causer?Causer->GetInstigator():nullptr;
	if(auto* Character=Cast<ARPGPrototypeCharacter>(Causer)) { Pawn=Character; }
	if(const auto* Ball=Cast<AChaosImpactBall>(Causer)) { Pawn=Ball->GetThrowingPawn(); }
	if(auto* Source=Cast<ARPGPrototypeCharacter>(Pawn);Source && !Source->IsGrunt() && Source->IsPlayerControlled())
	{ Contributions.FindOrAdd(Victim).Add(Source,GetWorld()->GetTimeSeconds());
	  if(Victim->SpawnPoint.IsValid()) { Victim->SpawnPoint->AlertTo(Source); } }
}

ARPGPrototypeLoot* ARPGPrototypeGameMode::SpawnLoot(const FVector& Location, const FRPGPrototypeItem& Item, ARPGPrototypeCharacter* Recipient)
{
	if(!Recipient) { Recipient=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); }
	return SpawnLootIn(GetWorld(), Location, Item, Recipient, LootLifetimeSeconds);
}
ARPGPrototypeLoot* ARPGPrototypeGameMode::SpawnLootIn(UWorld* World, const FVector& Location, const FRPGPrototypeItem& Item, ARPGPrototypeCharacter* Recipient, float Lifetime)
{
	if(!World || !Recipient || Recipient->IsGrunt()) { return nullptr; }
	int32 Count = 0;
	for (TActorIterator<ARPGPrototypeLoot> It(World); It; ++It) { if(It->Recipient==Recipient) { ++Count; } }
	if (Count >= 40) { return nullptr; }
	FHitResult Ground;
	FVector At = Location;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGLootGround), false);
	// Ignore pawns so drops sit over the floor, not over the defeated character.
	if (World->LineTraceSingleByObjectType(Ground, Location + FVector(0,0,120), Location - FVector(0,0,350),
		FCollisionObjectQueryParams(ECC_WorldStatic), Params)) { At = Ground.ImpactPoint + FVector(0,0,35); }
	else { At.Z = FMath::Max(35.0, At.Z); }
	const FTransform Transform(FRotator::ZeroRotator, At);
	auto* Loot = World->SpawnActorDeferred<ARPGPrototypeLoot>(ARPGPrototypeLoot::StaticClass(), Transform);
	if (Loot) { Loot->Item = Item; Loot->Recipient=Recipient; Loot->LifetimeSeconds=Lifetime; Loot->FinishSpawning(Transform); }
	return Loot;
}

void ARPGPrototypeGameMode::HandleEliminated(AChaosImpactCharacter* Character)
{
	auto* Enemy = Cast<ARPGPrototypeCharacter>(Character);
	if (!Enemy || !Enemy->IsGrunt()) { return; }
	auto* Spawn = Enemy->SpawnPoint.Get();
	if (Spawn) { Spawn->NextSpawnAt = GetWorld()->GetTimeSeconds() + Spawn->RespawnSeconds; }
	if (Spawn && Spawn->bEncounterGroup) { if(auto* CPU=Enemy->GetController()) { CPU->Destroy(); } }
	RewardEnemy(Enemy);
}

void ARPGPrototypeGameMode::RewardEnemy(ARPGPrototypeCharacter* Enemy,bool bBoss)
{
	if(!Enemy || !Enemy->IsGrunt()) { return; }
	auto* Spawn=Enemy->SpawnPoint.Get();
	TSet<ARPGPrototypeCharacter*> Eligible;
	if(const auto* Hits=Contributions.Find(Enemy))
	{ for(const auto& Hit:*Hits) { if(Hit.Key.IsValid() && GetWorld()->GetTimeSeconds()-Hit.Value<=AssistSeconds) { Eligible.Add(Hit.Key.Get()); } } }
	// Off-stage accidents are not farming rewards. Give credit only for the player's attack.
	AActor* Causer = Enemy->GetLastDamageCauser();
	APawn* Source = Causer ? Causer->GetInstigator() : nullptr;
	if(auto* Character=Cast<ARPGPrototypeCharacter>(Causer)) { Source=Character; }
	if (auto* Ball = Cast<AChaosImpactBall>(Causer)) { Source = Ball->GetThrowingPawn(); }
	if (auto* Zone = Cast<AChaosImpactHazardZone>(Causer)) { Source = Zone->GetSourcePawn(); }
	if(auto* Killer=Cast<ARPGPrototypeCharacter>(Source);Killer && !Killer->IsGrunt() && Killer->IsPlayerControlled()) { Eligible.Add(Killer); }
	Contributions.Remove(Enemy);
	// Hearts to pick up (RPGHealing.h): now and then from anyone, a few from every boss.
	if(!Eligible.IsEmpty())
	{ const int32 Hearts=bBoss?RPGTuning::BossHearts:(FMath::FRand()<RPGTuning::HeartDropChance?1:0); for(int32 I=0;I<Hearts;++I) { ARPGHealPickup::Drop(GetWorld(),Enemy->GetActorLocation()); } }
	// Each player who took part gets the whole reward on their own machine (online: a guest's is sent there).
	for(auto* Player:Eligible)
	{
		FRPGKillCredit Credit;
		Credit.Level=Enemy->GetRPGLevel(); Credit.bBoss=bBoss; Credit.At=Enemy->GetActorLocation();
		Credit.DropChance=Spawn?FMath::Clamp(Spawn->DropChance,0.0f,1.0f):RPGTuning::DefaultDropChance;
		// For quests: 1 the boss, 2 a thrower-role enemy, 4 finished by this player's special ball.
		Credit.Detail=(bBoss?1:0)|(Enemy->EncounterRole==ERPGGruntRole::Thrower && !bBoss?2:0);
		if(Player==Source)
		{
			const auto* Shot=Cast<ARPGPrototypeBall>(Causer); const auto* Field=Cast<ARPGPrototypeEffect>(Causer);
			const EChaosImpactBallType Type=Shot?Shot->ShotItem.Type:Field?Field->Item.Type:EChaosImpactBallType::Normal;
			if(Type!=EChaosImpactBallType::Normal) { Credit.Detail|=4; }
			Credit.KillRefund=Shot?Shot->ShotItem.Mod(TEXT("KillRefund")):Field?Field->Item.Mod(TEXT("KillRefund")):0.0f;
		}
		Player->ApplyKillEffects(Credit);
		if(Player->IsRemoteHero()) { Player->ClientKillCredit(Credit); } else { Player->ReceiveKillCredit(Credit); }
	}
}

void ARPGPrototypeGameMode::ResetPrototype()
{
	if (bResetPending) { return; }
	bResetPending = true;
	FString Options=FString::Printf(TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=%d?CIKeyboardPlayer=%s"),GetLocalPlayerCount(),GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="),TEXT("0")));
	Options+=FString::Printf(TEXT("?CISharedScreen=%d"),bSharedScreen?1:0);
	for(int32 I=0;I<4;++I)
	{ const FString Key=FString::Printf(TEXT("CIPadDevice%d="),I); if(const TCHAR* Value=GetWorld()->URL.GetOption(*Key,nullptr)) { Options+=TEXT("?")+Key+Value; } }
	const FName Map=ARPGAdventureWorld::Find(GetWorld())?FName(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Lvl_RPGColonyFrontier")):FName(TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"));
	UGameplayStatics::OpenLevel(this,Map,true,Options);
}
