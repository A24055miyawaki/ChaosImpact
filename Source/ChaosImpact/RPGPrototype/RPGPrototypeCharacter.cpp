#include "RPGPrototype.h"
#include "RPGTuning.h"
#include "RPGPrototypeHUD.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "RPGAdventure.h"
#include "RPGTitle.h"
#include "ChaosImpactSimaeBoss.h"
#include "GameFramework/SpringArmComponent.h"

ARPGPrototypeCharacter::ARPGPrototypeCharacter()
{
	BallClass = ARPGPrototypeBall::StaticClass();
	MaximumCarriedBalls = 1;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = ARPGPrototypeGruntController::StaticClass();
}

void ARPGPrototypeCharacter::SetEnemy(bool bEnemy, int32 Level)
{
	bGrunt = bEnemy;
	RPGLevel = FMath::Max(1, Level);
	SoloTeam = bEnemy ? 1 : 0;
	bRespawnAfterElimination = !bEnemy;
}

void ARPGPrototypeCharacter::BeginPlay()
{
	MaxHealth = bGrunt ? EnemyHP * (1 + (RPGLevel - 1) * RPGTuning::EnemyHPPerLevel) : PlayerHP;
	SoloTeam = bGrunt ? 1 : 0;
	BallClass = ARPGPrototypeBall::StaticClass();
	MaximumCarriedBalls = 1;
	if(auto* Adventure=ARPGAdventureWorld::Find(GetWorld()))
	{ if(GetCameraBoom()) { GetCameraBoom()->TargetArmLength*=Adventure->CameraDistanceMultiplier; } }
	Super::BeginPlay();
	Quests.SetNum(RPGQuests::Num());
	VSBaseSpeed = WalkSpeed>0 ? WalkSpeed : BaseMaxWalkSpeed;
	VSStaminaRegen=StaminaRegenPerSecond; VSStaminaHit=StaminaRecoveredPerBallHit;
	VSDashCD=DashCooldownSeconds; VSChargeTime=MaxChargeSeconds;
	BaseMaxWalkSpeed = bGrunt ? (SpawnPoint.IsValid() && SpawnPoint->bEncounterGroup ?
		(EncounterRole==ERPGGruntRole::Chaser?330.f:270.f) : 245.f) : VSBaseSpeed;
	GetCharacterMovement()->MaxWalkSpeed = BaseMaxWalkSpeed;
	Attacks.SetNum(3);
	Attacks[0]=FRPGPrototypeItem::Make(EChaosImpactBallType::Normal, RPGLevel);
	Badges.SetNum(3);
	Gears.SetNum(4);
	if (!bGrunt) { Bag.Add(Attacks[0]); RebuildEquipment(); Health=MaxHealth; }
	// The field has no VS stage edge: falling is handled by RecoverFromFall (RPGRevive.cpp), never a knockout.
	if (!bGrunt) { SetExplorationMode(true, 1.0f); }
	if (bGrunt) { Attacks[0].Power = RPGTuning::EnemyPowerBase + (RPGLevel - 1) * RPGTuning::EnemyPowerPerLevel; Attacks[0].Cooldown = RPGTuning::EnemyThrowCooldown; }
	EnsureReadyBall();
	LastTravelPosition=GetActorLocation();
	if (ChargeWidget) { ChargeWidget->SetVisibility(ESlateVisibility::Collapsed); }
	if (ChargeWidget) { ChargeWidget->RemoveFromParent(); }
}

void ARPGPrototypeCharacter::Tick(float DeltaSeconds)
{
	EnsureReadyBall();
	TickEquipment(DeltaSeconds);
	if(auto* Drive=Cast<ARPGPrototypeBall>(DrivenBall.Get()))
	{ DriveEndsAt=Drive->GetRPGDriveEnd(); DriveEndsAtServerTime=DriveEndsAt; }
	// The shared cancel action can hide/disable a drive actor. Resolve RPG-only ending traits here as well.
	for(TActorIterator<ARPGPrototypeBall> It(GetWorld());It;++It)
	{ if(It->GetInstigator()==this && It->GetBallType()==EChaosImpactBallType::Drive && It->HasDetonated()) { It->FinishRPGDrive(); } }
	Super::Tick(DeltaSeconds);
	TickRPGKnockback(DeltaSeconds);
	TickAdventure(DeltaSeconds);
	TickDownAndCare(DeltaSeconds);
	// Online: the host's MaxHealth for everyone else's screens; a guest's own hero reports what it fights with.
	if (HasAuthority()) { RepMaxHealth = MaxHealth; }
	else if (!IsLocallyControlled() && RepMaxHealth > 0) { MaxHealth = RepMaxHealth; }
	if (IsNetClientHero()) { TickNetSync(); }
	if(CarriedBallCount>0 && GetCarriedBallType(0)==EChaosImpactBallType::Snow)
	{
		const auto& Item=GetAttack(SelectedAttack);
		const float Added=FMath::Max(0.0f,GetSnowGrowth(0)-PresentedSnowGrowth);
		RPGSnowGrowth=FMath::Clamp(RPGSnowGrowth+Added*(1+(Item.Mod(TEXT("SnowGrow"))+Status(TEXT("SnowGrowth")))*.01f),0.0f,1.0f);
		PresentedSnowGrowth=(GetRPGSnowScale()-ChaosImpactBallTypes::SnowMinScale)/(ChaosImpactBallTypes::SnowMaxScale-ChaosImpactBallTypes::SnowMinScale);
		SetSlotSnowGrowth(0,PresentedSnowGrowth); UpdateSnowRollPresentation(); UpdateToonCharacter();
	}
	if (ChargeWidget) { ChargeWidget->SetVisibility(ESlateVisibility::Collapsed); }
	if (IsPlayerControlled() && IsLocallyControlled())
	{
		if (!PrototypeHUD)
		{
			InitializeRPGPreferences();
			PrototypeHUD = CreateWidget<URPGPrototypeHUD>(Cast<APlayerController>(GetController()));
			if (PrototypeHUD)
			{ const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); if(Mode && Mode->UsesSharedScreen()) { PrototypeHUD->AddToViewport(20+FMath::Max(0,GetLocalRPGIndex())); } else { PrototypeHUD->AddToPlayerScreen(20); } }
		}
		UpdateHeroFlow();
		const AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetController());
		if (PrototypeHUD)
		{
			PrototypeHUD->SetVisibility(!PC || !PC->IsGameplayActive() || IsInTitle() ? ESlateVisibility::Collapsed
				: bBagOpen && !UsesMiniMenu() ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
		}
		if (bBagOpen && (IsEliminated() || (PC && !PC->IsGameplayActive()))) { SetBagOpen(false); }
		if (ActiveMerchant.IsValid() && !IsAtMerchant()) { SetBagOpen(false); }
		UpdatePrototypeInput();
	}
}

void ARPGPrototypeCharacter::EnsureReadyBall()
{
	// Other players' characters on a guest's screen show the ball the host says they hold.
	if (!HasAuthority() && !IsLocallyControlled()) { return; }
	if (IsEliminated() || IsChargingThrow() || IsThrowReleasePending() || Attacks.Num() != 3) { return; }
	const bool bReady = GetCooldownLeft(SelectedAttack) <= 0 && GetAttack(SelectedAttack).IsValid();
	if (CarriedBallCount == (bReady ? 1 : 0) && (!bReady || GetCarriedBallType(0) == GetAttack(SelectedAttack).Type)) { return; }
	ClearCarriedBalls();
	if (bReady)
	{
		PushCarriedBall(GetAttack(SelectedAttack).Type);
		if(GetAttack(SelectedAttack).Type==EChaosImpactBallType::Snow)
		{ RPGSnowGrowth=FMath::Clamp(Status(TEXT("SnowCarry"))*.01f,0.0f,1.0f); PresentedSnowGrowth=0; }
	}
	UpdateBallPresentation();
}

const FRPGPrototypeItem& ARPGPrototypeCharacter::GetAttack(int32 Slot) const
{
	static const FRPGPrototypeItem Empty;
	return Attacks.IsValidIndex(Slot) ? Attacks[Slot] : Empty;
}

float ARPGPrototypeCharacter::GetCooldownLeft(int32 Slot) const
{
	return Slot >= 0 && Slot < 3 && GetWorld()
		? FMath::Max(0.0, FMath::Max(ReadyAt[Slot], ItemReadyAt.FindRef(GetAttack(Slot).Id)) - GetWorld()->GetTimeSeconds()) : 0.0f;
}

bool ARPGPrototypeCharacter::SelectAttack(int32 Slot)
{
	if (Slot < 0 || Slot > 2 || !GetAttack(Slot).IsValid() || IsEliminated() || IsChargingThrow()
		|| IsThrowReleasePending() || IsDriving() || bBagOpen) { return false; }
	SelectedAttack = Slot;
	EnsureReadyBall();
	// Online guest: the host throws this ball too (sent before the throw itself).
	if (IsNetClientHero()) { ServerSelectAttack(Slot); }
	return true;
}

float ARPGPrototypeCharacter::ConsumeAttack()
{
	const FRPGPrototypeItem& Item = GetAttack(SelectedAttack);
	ReadyAt[SelectedAttack] = GetWorld()->GetTimeSeconds() + Item.EffectiveCooldown()
		* (SelectedAttack>0?FMath::Max(RPGTuning::SpecialCooldownBadgeFloor,1-BadgeMod(TEXT("SpecialCD"))*.01f):1);
	ItemReadyAt.Add(Item.Id, ReadyAt[SelectedAttack]);
	float Power=Item.EffectivePower()*(1+BadgeMod(TEXT("AllPower"))*.01f);
	if (bCounterReady) { Power*=1+BadgeMod(TEXT("Counter"))*.01f; bCounterReady=false; }
	if (TravelDistance>=300) { Power*=1+BadgeMod(TEXT("TravelPower"))*.01f; TravelDistance=0; }
	if (SelectedAttack==0 && LastAttackSlot>0) { Power*=1+(BadgeMod(TEXT("Combo"))+Item.Mod(TEXT("NFinish")))*.01f; }
	if (SelectedAttack>0 && LastAttackSlot>0 && SelectedAttack!=LastAttackSlot) { Power*=1+(BadgeMod(TEXT("Switch"))+BadgeMod(TEXT("TechPair")))*.01f; }
	if (SelectedAttack==0 && GetWorld()->GetTimeSeconds()-LastDashAt<1) { Power*=1+BadgeMod(TEXT("MoveCounter"))*.01f; }
	if (Status(TEXT("Smoke"))>0 && GetWorld()->GetTimeSeconds()-LastAttackAt>3) { Power*=1+Status(TEXT("Shadow"))*.01f; }
	LastAttackSlot=SelectedAttack; LastAttackAt=GetWorld()->GetTimeSeconds(); VolleyTargets.Empty();
	return Power;
}

void ARPGPrototypeCharacter::RequestBallSwap()
{
	if (const auto* PC = Cast<AChaosImpactPlayerController>(GetController()); PC && !PC->IsGameplayActive()) { return; }
	if(!bBagOpen && !bQuestMenuOpen && InteractAdventure()) { return; }
	if(!bBagOpen && !IsChargingThrow()) { if(auto* Merchant=FindMerchant()) { OpenMerchant(Merchant); return; } }
	for(int32 Step=1;Step<=3;++Step) { if(SelectAttack((SelectedAttack+Step)%3)) { break; } }
}

void ARPGPrototypeCharacter::RequestDropBall()
{
	PickupNearbyLoot();
}

ARPGPrototypeLoot* ARPGPrototypeCharacter::FindNearbyLoot() const
{
	ARPGPrototypeLoot* Closest = nullptr;
	float Best = FMath::Square(230.0f);
	for (TActorIterator<ARPGPrototypeLoot> It(GetWorld()); It; ++It)
	{
		if(!It->IsVisibleTo(this)) { continue; }
		const FVector Delta = It->GetActorLocation() - GetActorLocation();
		if (FMath::Abs(Delta.Z) > 160 || Delta.SizeSquared2D() > Best) { continue; }
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RPGPickup), false, this);
		Params.AddIgnoredActor(*It);
		if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(), It->GetActorLocation(), ECC_Visibility, Params)) { continue; }
		Best = Delta.SizeSquared2D(); Closest = *It;
	}
	return Closest;
}

bool ARPGPrototypeCharacter::PickupNearbyLoot()
{
	const auto* PC = Cast<AChaosImpactPlayerController>(GetController());
	if (bGrunt || bBagOpen || IsEliminated() || (PC && !PC->IsGameplayActive()) || GetWorld()->GetTimeSeconds() < NextPickupAt) { return false; }
	ARPGPrototypeLoot* Loot = FindNearbyLoot();
	const bool bCollected = Loot && Loot->Collect(this);
	if (bCollected) { NextPickupAt = GetWorld()->GetTimeSeconds() + 0.15; }
	return bCollected;
}

bool ARPGPrototypeCharacter::AddLoot(const FRPGPrototypeItem& Item)
{
	if (!Item.IsValid() || Item.Type==EChaosImpactBallType::Nova) { return false; }
	for (const auto& Existing : Bag) { if (Existing.Id == Item.Id) { return false; } }
	if (Bag.Num()>=RPGTuning::BagCapacity)
	{
		Notice = FString::Printf(TEXT("バッグがいっぱいです（%d/%d）。分解して空きを作ってください"), Bag.Num(), RPGTuning::BagCapacity);
		NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
		return false;
	}
	Bag.Add(Item); UnseenItems.Add(Item.Id);
	if (Item.Kind == ERPGItemKind::Ball && Item.Type != EChaosImpactBallType::Normal) { QuestEvent(ERPGQuestEvent::ItemObtained, GetActorLocation(), 1); }
	Notice = Item.Name() + TEXT("を拾った　バッグで装備できます");
	NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
	return true;
}

bool ARPGPrototypeCharacter::EquipBagItem(int32 Index, int32 Slot)
{
	if (!bBagOpen || !Bag.IsValidIndex(Index) || Slot < 1 || Slot > 2 || IsEliminated()) { return false; }
	if (Bag[Index].Kind!=ERPGItemKind::Ball)
	{
		if(RPGEquipment::IsGear(Bag[Index].Kind))
		{ const int32 GearSlot=static_cast<int32>(Bag[Index].Kind)-5; Gears[GearSlot]=Bag[Index]; RebuildEquipment(); Notice=Bag[Index].Name()+TEXT("を装備しました"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true; }
		const int32 BadgeSlot=static_cast<int32>(Bag[Index].Kind)-1;
		if (!Badges.IsValidIndex(BadgeSlot)) { return false; }
		Badges[BadgeSlot]=Bag[Index]; RebuildEquipment();
		Notice=RPGEquipment::KindName(Bag[Index].Kind)+TEXT("に装備しました"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return true;
	}
	if (Bag[Index].Type==EChaosImpactBallType::Normal)
	{ Attacks[0]=Bag[Index]; RebuildEquipment(); EnsureReadyBall(); return true; }
	const int32 Other = Slot == 1 ? 2 : 1;
	if (Attacks[Other].Id == Bag[Index].Id) { Notice = TEXT("同じボールを2枠には装備できません"); NoticeUntil = GetWorld()->GetTimeSeconds() + 2; return false; }
	// An empty slot is not an inventory item. Switching never clears an item's cooldown.
	if (Attacks[Slot].IsValid() && !Bag.ContainsByPredicate([&](const FRPGPrototypeItem& Item) { return Item.Id == Attacks[Slot].Id; }))
	{ Bag.Add(Attacks[Slot]); }
	Attacks[Slot] = Bag[Index];
	Notice = FString::Printf(TEXT("特殊球%dに装備しました"), Slot);
	NoticeUntil = GetWorld()->GetTimeSeconds() + 2;
	EnsureReadyBall();
	return true;
}

bool ARPGPrototypeCharacter::InspectEquipmentSlot(int32 Slot)
{
	if(!bBagOpen || Slot<0 || Slot>=5 || HasConfirmation()) { return false; }
	const auto& Item=Slot<2?GetAttack(Slot+1):GetBadge(Slot-2);
	if(Slot<2) { EquipTargetSlot=Slot+1; }
	if(!Item.IsValid())
	{ Notice=TEXT("この枠は未装備です"); NoticeUntil=GetWorld()->GetTimeSeconds()+2; return false; }
	const int32 Index=Bag.IndexOfByPredicate([&](const FRPGPrototypeItem& I) { return I.Id==Item.Id; });
	if(Index==INDEX_NONE) { return false; }
	BagFilter=0; BagSelection=Index; BagPage=0; bChoosingEquipment=false; SelectEquipment(Slot<2?Slot+1:Slot+5); DetailScroll=TuneSelection=0;
	return true;
}

void ARPGPrototypeCharacter::SetBagOpen(bool bOpen)
{
	if (bBagOpen == bOpen || (bOpen && (IsEliminated() || IsThrowReleasePending() || IsDriving()))) { return; }
	if(bOpen && bQuestMenuOpen) { SetQuestMenuOpen(false); }
	bBagOpen = bOpen;
	if (!bOpen) { ActiveMerchant.Reset(); BagPage=0; PendingSalvage=PendingTune=INDEX_NONE; PendingSessionAction=0; bChoosingEquipment=bChoosingDestination=false; }
	else { SwitchInventoryPage(0); }
	SetTrainingMenuFrozen(bOpen);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (bOpen && PrototypeHUD && !UsesMiniMenu())
		{
			PrototypeHUD->SetVisibility(ESlateVisibility::Visible);
			PC->SetInputMode(FInputModeGameAndUI().SetWidgetToFocus(PrototypeHUD->TakeWidget()).SetHideCursorDuringCapture(false));
			const auto* MenuPC=Cast<AChaosImpactPlayerController>(PC);
			PC->bShowMouseCursor = !MenuPC || !MenuPC->IsUsingGamepad();
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly().SetConsumeCaptureMouseDown(false));
			if (const auto* MenuPC = Cast<AChaosImpactPlayerController>(PC)) { PC->bShowMouseCursor = !MenuPC->IsUsingGamepad(); }
		}
	}
}

void ARPGPrototypeCharacter::AwardKillXP()
{
	if (bGrunt || IsEliminated()) { return; }
	LastKillAt=GetWorld()->GetTimeSeconds(); bKillDashReady=true;
	RestoreHP(MaxHealth*BadgeMod(TEXT("KillHeal"))*.01f);
	Shield=FMath::Min(MaxHealth*.2f,Shield+MaxHealth*BadgeMod(TEXT("LifeShield"))*.01f);
	int32 Gain=FMath::RandRange(RPGTuning::KillRubyMin,RPGTuning::KillRubyMax)+RPGLevel;
	Gain=FMath::RoundToInt(Gain*(1+BadgeMod(TEXT("Ruby"))*.01f));
	if (FMath::FRand()*100<BadgeMod(TEXT("TechLucky"))) { Gain*=2; }
	AddRubies(Gain);
	GainRPGExperience(RPGTuning::KillXP);
}

void ARPGPrototypeCharacter::GainRPGExperience(int32 Amount)
{
	if(bGrunt || IsEliminated() || Amount<=0) { return; }
	XP+=FMath::Min(Amount,10000);
	while (XP >= GetRequiredXP() && RPGLevel<RPGTuning::MaxLevel)
	{
		XP -= GetRequiredXP(); ++RPGLevel;
		RebuildEquipment(); Health=MaxHealth;
		// Constant normal attack grows with the player, without rerolling its equipped core or losing tunes.
		if (Attacks[0].Level<RPGLevel)
		{
			const float OldScale=1+(Attacks[0].Level-1)*RPGTuning::PowerPerItemLevel;
			Attacks[0].Power*=(1+(RPGLevel-1)*RPGTuning::PowerPerItemLevel)/OldScale; Attacks[0].Level=RPGLevel;
			for(auto& I:Bag) { if(I.Id==Attacks[0].Id) { I=Attacks[0]; } }
		}
		Notice = FString::Printf(TEXT("LEVEL UP!　Lv.%d"), RPGLevel);
		NoticeUntil = GetWorld()->GetTimeSeconds() + 3;
	}
}

float ARPGPrototypeCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
	// This disposable single-player workshop is safe while interacting; ordinary bag use is not.
	if(!bGrunt && IsAtMerchant()) { return 0; }
	// Down (waiting for a teammate) or just helped up: nothing more lands.
	if(!bGrunt && !bBleedingOut && (bDowned || GetWorld()->GetTimeSeconds()-RevivedAt<RPGTuning::ReviveGraceSeconds)) { return 0; }
	if(!bGrunt) { if(const auto* Adventure=ARPGAdventureWorld::Find(GetWorld());Adventure && Adventure->IsInColony(GetActorLocation())) { return 0; } }
	float RPGDamage = Amount;
	if (const AChaosImpactHazardZone* Zone = Cast<AChaosImpactHazardZone>(Causer))
	{
		if (Zone->GetSourcePawn() == this) { return 0; }
		// Shared special-ball effects originally deal 1 HP or instant KO. Translate only here.
		const ARPGPrototypeCharacter* Source = Cast<ARPGPrototypeCharacter>(Zone->GetSourcePawn());
		const float LevelScale = Source ? 1 + (Source->GetRPGLevel() - 1) * 0.09f : 1;
		RPGDamage = Zone->GetZoneType() == EChaosImpactBallType::Nova ? 60 : 6 * LevelScale;
	}
	if (EventInstigator && EventInstigator->GetPawn()==this) { return 0; }
	// The snowfield's Shima-enaga hits in solo units (1 per attack): read it as an RPG hit.
	if (const auto* Simae = EventInstigator ? Cast<AChaosImpactSimaeBoss>(EventInstigator->GetPawn()) : Cast<AChaosImpactSimaeBoss>(Causer))
	{ RPGDamage = Amount * RPGTuning::SimaeHitDamage * (Simae->IsFullPower() ? 1.2f : 1.0f); }
	if (!bGrunt)
	{
		const double Now=GetWorld()->GetTimeSeconds();
		float Reduction=BadgeMod(TEXT("Armor"))+Status(TEXT("Hide"));
		if(Health<MaxHealth*.3f) { Reduction+=BadgeMod(TEXT("LowArmor")); }
		if(Now-LastDashAt<.6) { Reduction+=BadgeMod(TEXT("DashArmor")); }
		if(IsChargingThrow()) { Reduction+=BadgeMod(TEXT("ChargeArmor")); }
		if(Now-LastHealAt<2) { Reduction+=BadgeMod(TEXT("LifeAid")); }
		if(Now-RevivedAt<3) { Reduction+=BadgeMod(TEXT("LifeRevive")); }
		RPGDamage*=1-FMath::Clamp(Reduction*.01f,0.0f,RPGTuning::DamageReductionCap);
		if(IsDashing()) { return 0; }
		const float Absorbed=FMath::Min(Shield,RPGDamage); Shield-=Absorbed; RPGDamage-=Absorbed;
		if(RPGDamage>=Health && BadgeMod(TEXT("LifeSurvive"))>0 && Now-LastSurviveAt>=BadgeMod(TEXT("LifeSurvive")))
		{ RPGDamage=FMath::Max(0.0f,Health-1); LastSurviveAt=Now; }
	}
	// With teammates around, a knockout is a down first (RPGRevive.cpp).
	if (!bGrunt && !bBleedingOut && RPGDamage >= Health && CountHeroes(GetWorld()) >= 2)
	{
		const float Taken = Super::TakeDamage(FMath::Max(0.0f, Health - 1), Event, EventInstigator, Causer);
		EnterDowned();
		if (auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); Mode && Taken > 0) { Mode->AddDamageText(this, Taken); }
		return Taken;
	}
	const float Applied = Super::TakeDamage(RPGDamage, Event, EventInstigator, Causer);
	if (Applied > 0)
	{
		// A hit on someone who is helping a teammate up: that count starts over.
		if (!bGrunt) { for (TActorIterator<ARPGPrototypeCharacter> It(GetWorld()); It; ++It) { if (It->Reviver.Get() == this) { UE_LOG(LogTemp, Log, TEXT("RPG revive of %s restarts: helper %s hit for %.1f by %s"), *It->GetName(), *GetName(), Applied, *GetNameSafe(Causer)); It->ReviveProgress = 0; } } }
		bCounterReady=true;
		if(!bGrunt && !IsEliminated() && Health<MaxHealth*.3f && BadgeMod(TEXT("LifeRecover"))>0
			&& GetWorld()->GetTimeSeconds()-LastLowHealAt>=45)
		{ LastLowHealAt=GetWorld()->GetTimeSeconds(); RestoreHP(MaxHealth*BadgeMod(TEXT("LifeRecover"))*.01f); }
		if (auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->AddDamageText(this, Applied); }
	}
	return Applied;
}

void ARPGPrototypeCharacter::UpdatePrototypeInput()
{
	AChaosImpactPlayerController* PC = Cast<AChaosImpactPlayerController>(GetController());
	if (!PC || !PC->IsGameplayActive() || bAutomatedInput) { return; }
	if(bQuestMenuOpen || bDowned) { bReviveHeld = false; return; }
	const bool bPad = PC->IsUsingGamepad();
	// Holding the interact button next to a downed teammate helps them up (and does not open anything).
	const bool bCanHelp = !bBagOpen && FindReviveTarget() != nullptr;
	bReviveHeld = bCanHelp && PC->IsInputKeyDown(bPad ? EKeys::Gamepad_FaceButton_Top : EKeys::E);
	const auto Pressed = [&](FKey Keyboard, FKey Pad) { return PC->WasInputKeyJustPressed(bPad ? Pad : Keyboard); };
	if (Pressed(EKeys::Tab, EKeys::Gamepad_Special_Left)) { SetBagOpen(!bBagOpen); }
	if (IsEliminated()) { return; }
	if (bBagOpen)
	{
		if (Pressed(EKeys::Escape, EKeys::Gamepad_FaceButton_Right)) { HandleBagKey(bPad?EKeys::Gamepad_FaceButton_Right:EKeys::Escape); return; }
		float X = 0, Y = 0;
		if (bPad) { PC->GetInputAnalogStickState(EControllerAnalogStick::CAS_LeftStick, X, Y); }
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextBagNavigationAt)
		{
			int32 Direction = PC->WasInputKeyJustPressed(bPad ? EKeys::Gamepad_DPad_Down : EKeys::Down) || Y < -0.65f ? 1 : 0;
			Direction -= PC->WasInputKeyJustPressed(bPad ? EKeys::Gamepad_DPad_Up : EKeys::Up) || Y > 0.65f ? 1 : 0;
			if (Direction) { NavigateBag(Direction); NextBagNavigationAt = Now + 0.2; }
			if (PC->WasInputKeyJustPressed(bPad ? EKeys::Gamepad_DPad_Left : EKeys::Left) || X < -0.65f) { HandleBagKey(EKeys::Left); NextBagNavigationAt = Now + 0.2; }
			if (PC->WasInputKeyJustPressed(bPad ? EKeys::Gamepad_DPad_Right : EKeys::Right) || X > 0.65f) { HandleBagKey(EKeys::Right); NextBagNavigationAt = Now + 0.2; }
		}
		if (Pressed(EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom)) { HandleBagKey(bPad?EKeys::Gamepad_FaceButton_Bottom:EKeys::Enter); }
		// The right stick scrolls the description, so the triggers are free for sorting and bulk salvage.
		if (bPad && Now >= NextDetailScrollAt)
		{
			float RX = 0, RY = 0; PC->GetInputAnalogStickState(EControllerAnalogStick::CAS_RightStick, RX, RY);
			if (FMath::Abs(RY) > 0.65f) { HandleBagKey(RY < 0 ? EKeys::PageDown : EKeys::PageUp); NextDetailScrollAt = Now + 0.2; }
		}
		return;
	}
	if (!bPad)
	{
		if (PC->WasInputKeyJustPressed(EKeys::One)) { SelectAttack(0); }
		if (PC->WasInputKeyJustPressed(EKeys::Two)) { SelectAttack(1); }
		if (PC->WasInputKeyJustPressed(EKeys::Three)) { SelectAttack(2); }
		if (PC->WasInputKeyJustPressed(EKeys::F)) { PickupNearbyLoot(); }
		if (PC->WasInputKeyJustPressed(EKeys::J)) { OpenQuestJournal(); }
		if (PC->WasInputKeyJustPressed(EKeys::M)) { OpenFastTravel(); }
		if (PC->WasInputKeyJustPressed(EKeys::O)) { OpenMembers(); }
		if (PC->WasInputKeyJustPressed(EKeys::E) && !bCanHelp)
		{ if(!InteractAdventure() && !OpenMerchant(FindMerchant())) { for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It) { if(It->Retry(this)) { break; } } } }
		if (PC->WasInputKeyJustPressed(EKeys::R) && GetLocalRPGIndex()==0) { if (auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>()) { Mode->ResetPrototype(); } }
	}
	// Standard/simple/mouse-only schemes keep their existing throw, move and dash bindings.
	if (bPad && PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up)) { PickupNearbyLoot(); }
	if (bPad && PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) { OpenQuestJournal(); }
	if (bPad && PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right)) { OpenFastTravel(); }
	if (bPad && PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left)) { OpenMembers(); }
	if (bPad && !bCanHelp && PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top))
	{ if(!InteractAdventure() && !OpenMerchant(FindMerchant())) { for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It) { if(It->Retry(this)) { break; } } } }
}

bool ARPGPrototypeCharacter::IsPointerOverGameplayUI() const
{
	return bBagOpen || bQuestMenuOpen || (PrototypeHUD && PrototypeHUD->GetPointerAction() != 0);
}

float ARPGPrototypeCharacter::GetRPGRespawnLeft() const
{
	return GetWorld() && RespawnAtWorldSeconds > 0 ? FMath::Max(0.0f, RespawnAtWorldSeconds - GetWorld()->GetTimeSeconds()) : 0.0f;
}

bool ARPGPrototypeCharacter::HandleEasyPointerKey(const FKey& Key, EInputEvent Event)
{
	const int32 Hit = PrototypeHUD ? PrototypeHUD->GetPointerAction() : 0;
	if (!bBagOpen && Key == EKeys::LeftMouseButton && (Hit || bSuppressPointerThrow))
	{
		if (Event == IE_Pressed || Event == IE_DoubleClick)
		{
			bSuppressPointerThrow = true;
			if (Hit >= 1 && Hit <= 3) { SelectAttack(Hit - 1); }
			if (Hit == 10) { SetBagOpen(true); }
			if (Hit == 11) { PickupNearbyLoot(); }
			if (Hit == 12) { StartDash(); }
			if (Hit == 13) { if(UsesMiniMenu()) { SetBagOpen(true); BagPage=3; } else if (auto* PC = Cast<AChaosImpactPlayerController>(GetController())) { PC->TogglePauseMenu(); } }
			if (Hit == 14) { OpenMerchant(FindMerchant()); }
			if (Hit == 15) { for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It) { if(It->Retry(this)) { break; } } }
			if (Hit == 16) { InteractAdventure(); }
			if (Hit == 17) { OpenQuestJournal(); }
			if (Hit == 18) { OpenFastTravel(); }
			if (Hit == 19) { OpenMembers(); }
		}
		return true;
	}
	return Super::HandleEasyPointerKey(Key, Event);
}
