#include "BRGame.h"
#include "BRIsland.h"
#include "ChaosImpact.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"

// -CIBRSelfTest (with -CIBRAuto=2): plays through the mode's parts by itself and writes what happened to the log.

void ABRGameMode::TickSelfTest()
{
	ABRGameState* State = GetBRState();
	if (!State || State->Phase != EBRPhase::Play || SelfTestStep < 0) { return; }
	APlayerController* Person = GetWorld()->GetFirstPlayerController();
	ABRCharacter* Me = Person ? Cast<ABRCharacter>(Person->GetPawn()) : nullptr;
	if (!Me || Me->IsFlying()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < SelfTestNext) { return; }
	const auto Vehicle = [&](const EBRVehicleKind Kind)
	{
		const FVector At = Me->GetActorLocation() + Me->GetActorForwardVector() * 200.0f;
		ABRVehicle* Made = GetWorld()->SpawnActor<ABRVehicle>(ABRVehicle::StaticClass(), At, Me->GetActorRotation());
		if (Made) { Made->Setup(Kind, FLinearColor(0.1f, 0.6f, 0.9f)); }
		return Made;
	};
	if (FParse::Param(FCommandLine::Get(), TEXT("CIBRGuestLag")))
	{
		// With a guest in the room: first a car for them to drive, then CPUs throwing at them.
		ABRCharacter* Guest = nullptr;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (It->Get() && !It->Get()->IsLocalController()) { Guest = Cast<ABRCharacter>(It->Get()->GetPawn()); }
		}
		if (!Guest || Guest->IsFlying() || Guest->IsEliminated()) { SelfTestNext = Now + 0.5; return; }
		if (SelfTestStep == 0)
		{
			SelfVehicle = GetWorld()->SpawnActor<ABRVehicle>(ABRVehicle::StaticClass(), Guest->GetActorLocation() + Guest->GetActorForwardVector() * 260.0f, Guest->GetActorRotation());
			if (SelfVehicle) { SelfVehicle->Setup(EBRVehicleKind::Car, FLinearColor(0.1f, 0.6f, 0.9f)); SelfVehicle->Mount(Guest); }
			UE_LOG(LogChaosImpact, Log, TEXT("BRTEST host put the guest in a car: mounted=%d"), Guest->Vehicle != nullptr);
			SelfTestFrom = Guest->GetActorLocation();
			SelfTestStep = 1;
			SelfTestNext = Now + 10.0;
			return;
		}
		if (SelfTestStep == 1)
		{
			UE_LOG(LogChaosImpact, Log, TEXT("BRTEST host saw the car go %.0f cm in 10 s"), SelfVehicle ? FVector::Dist2D(SelfTestFrom, SelfVehicle->GetActorLocation()) : -1.0);
			if (SelfVehicle) { SelfVehicle->Dismount(Guest); }
			SelfTestStep = 2;
			SelfTestNext = Now + 1.5;
			return;
		}
		ABRCharacter* Foe = nullptr;
		for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsPlayerControlled() && It->TeamId != Guest->TeamId && !It->IsEliminated() && !It->IsDowned() && !It->IsFlying()) { Foe = *It; break; }
		}
		if (!Foe)
		{
			for (int32 Index = 0; Index < Fighters.Num() && !Foe; ++Index)
			{
				if (Fighters[Index].bAlive && !Fighters[Index].bHuman && !Fighters[Index].Real.IsValid() && Fighters[Index].Team != Guest->TeamId) { Foe = SpawnReal(Index, Guest->GetActorLocation() + FVector(700.0f, 0.0f, 120.0f), nullptr); }
			}
		}
		if (Foe)
		{
			const FVector Side = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
			Foe->SetActorLocation(Guest->GetActorLocation() + Side * 800.0f + FVector(0.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
			Foe->GiveBall(EChaosImpactBallType::Normal);
		}
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST guest under fire: foe=%d armour=%.0f health=%.0f downed=%d"), Foe != nullptr, Guest->Armor, Guest->GetHealthNow(), Guest->IsDowned());
		if (++SelfTestStep > 14) { SelfTestStep = -1; }
		SelfTestNext = Now + 2.5;
		return;
	}
	ABRCharacter* Mate = FindNearestAlly(Me);
	switch (SelfTestStep)
	{
	case 0:
		for (TActorIterator<ABRIsland> Map(GetWorld()); Map; ++Map) { Map->DevCheckSolid(FVector2D(Me->GetActorLocation()), 20000.0f); }
		if (FParse::Param(FCommandLine::Get(), TEXT("CIBRChargeShot")))
		{
			// For a picture of the throw: two balls in hand and a charge held.
			Me->GiveBall(EChaosImpactBallType::Fire);
			Me->GiveBall(EChaosImpactBallType::Normal);
			Me->BeginThrowInput();
			UE_LOG(LogChaosImpact, Log, TEXT("BRTEST charging"));
			SelfTestStep = -1;
			return;
		}
		SelfVehicle = Vehicle(EBRVehicleKind::Skateboard);
		Me->InteractNow();
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST board mounted=%d seats=%d"), Me->Vehicle != nullptr, SelfVehicle ? SelfVehicle->GetSeats() : 0);
		SelfFrom = Me->GetActorLocation();
		if (SelfVehicle) { SelfVehicle->bScriptedDriver = true; SelfVehicle->SetDriverInput(FVector2D(1.0, 0.0)); }
		SelfTestNext = Now + 2.5;
		break;
	case 1:
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST board drove %.0f cm in 2.5 s"), FVector::Dist2D(SelfFrom, Me->GetActorLocation()));
		if (Mate && SelfVehicle && Mate->GetActorLocation() != FVector::ZeroVector) { SelfVehicle->Mount(Mate); }
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST board riders=%d"), SelfVehicle ? SelfVehicle->Riders.Num() : 0);
		Me->InteractNow();
		if (Mate && Mate->Vehicle) { Mate->InteractNow(); }
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST board dismounted=%d"), Me->Vehicle == nullptr);
		SelfTestNext = Now + 1.0;
		break;
	case 2:
		SelfVehicle = Vehicle(EBRVehicleKind::Car);
		Me->InteractNow();
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST car mounted=%d seats=%d"), Me->Vehicle != nullptr, SelfVehicle ? SelfVehicle->GetSeats() : 0);
		SelfFrom = Me->GetActorLocation();
		if (SelfVehicle) { SelfVehicle->bScriptedDriver = true; SelfVehicle->SetDriverInput(FVector2D(0.0, 1.0)); }
		SelfTestNext = Now + 2.5;
		break;
	case 3:
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST car drove %.0f cm in 2.5 s"), FVector::Dist2D(SelfFrom, Me->GetActorLocation()));
		Me->InteractNow();
		SelfTestNext = Now + 1.0;
		break;
	case 4:
	{
		Me->TakeLoot(EBRLootKind::Vest);
		Me->TakeLoot(EBRLootKind::Vest);
		const float Armor = Me->Armor, Health = Me->GetHealthNow();
		Me->TakeDamage(1.0f, FDamageEvent(), nullptr, nullptr);
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST armour %.0f -> %.0f, health %.0f -> %.0f"), Armor, Me->Armor, Health, Me->GetHealthNow());
		Me->GiveBall(EChaosImpactBallType::Normal);
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST ball in hand=%d"), Me->GetCarriedBallCount());
		Me->SetAIAimDirection(Me->GetActorForwardVector());
		Me->BeginThrowInput();
		SelfTestNext = Now + 0.3;
		break;
	}
	case 5:
		Me->EndThrowInput();
		SelfTestNext = Now + 14.0;
		break;
	case 6:
	{
		int32 Lying = 0;
		for (TActorIterator<ABRBall> It(GetWorld()); It; ++It)
		{
			if (It->IsPickup() && FVector::Dist2D(It->GetActorLocation(), Me->GetActorLocation()) < 4000.0f) { ++Lying; }
		}
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST balls on the floor nearby after 14 s=%d, hand=%d"), Lying, Me->GetCarriedBallCount());
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST going down, teammate=%s"), *GetNameSafe(Mate));
		Me->TakeDamage(500.0f, FDamageEvent(), nullptr, this);
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST downed=%d eliminated=%d"), Me->IsDowned(), Me->IsEliminated());
		if (Mate) { Mate->SetActorLocation(Me->GetActorLocation() + FVector(120.0f, 0.0f, 20.0f)); }
		SelfTestNext = Now + 9.0;
		break;
	}
	case 7:
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST after 9 s: downed=%d health=%.0f"), Me->IsDowned(), Me->GetHealthNow());
		UE_LOG(LogChaosImpact, Log, TEXT("BRTEST alive=%d teams=%d stage=%d left=%.0f real=%d"), State->AliveFighters, State->AliveTeams, State->StageIndex, State->GetStageSecondsLeft(), [&]() { int32 N = 0; for (TActorIterator<ABRCharacter> It(GetWorld()); It; ++It) { ++N; } return N; }());
		SelfTestStep = -2;
		break;
	default:
		break;
	}
	++SelfTestStep;
}
