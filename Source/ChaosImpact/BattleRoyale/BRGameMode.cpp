#include "BRGame.h"
#include "BRIsland.h"
#include "BRIslandLayout.h"
#include "BROnline.h"
#include "ChaosImpactSessionSubsystem.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactLoadoutSubsystem.h"
#include "GameMapsSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "ChaosImpact.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactSettings.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* const BRBotNames[] = {TEXT("ボール山田"), TEXT("避け太郎"), TEXT("投手ケンジ"), TEXT("外野のサトウ"), TEXT("内野のタナカ"), TEXT("よけ子"), TEXT("ナイスキャッチ"), TEXT("顔面セーフ"),
		TEXT("ドッチ党員"), TEXT("合衆国兵A"), TEXT("合衆国兵B"), TEXT("合衆国兵C"), TEXT("元審判"), TEXT("ボール拾い"), TEXT("体育委員"), TEXT("最後の一人"),
		TEXT("投げ損ね"), TEXT("当たり屋"), TEXT("空気入れ職人"), TEXT("流れ球"), TEXT("硬いボール"), TEXT("逃げ足"), TEXT("二度投げ"), TEXT("安全地帯")};
}

void ABRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABRGameState, Phase);
	DOREPLIFETIME(ABRGameState, TeamSize);
	DOREPLIFETIME(ABRGameState, PhaseEndsAt);
	DOREPLIFETIME(ABRGameState, StageIndex);
	DOREPLIFETIME(ABRGameState, bShrinking);
	DOREPLIFETIME(ABRGameState, StageEndsAt);
	DOREPLIFETIME(ABRGameState, ShrinkStartedAt);
	DOREPLIFETIME(ABRGameState, ZoneFrom);
	DOREPLIFETIME(ABRGameState, ZoneFromRadius);
	DOREPLIFETIME(ABRGameState, ZoneTo);
	DOREPLIFETIME(ABRGameState, ZoneToRadius);
	DOREPLIFETIME(ABRGameState, AliveFighters);
	DOREPLIFETIME(ABRGameState, AliveTeams);
	DOREPLIFETIME(ABRGameState, WinnerTeam);
	DOREPLIFETIME(ABRGameState, Roster);
	DOREPLIFETIME(ABRGameState, ShipFrom);
	DOREPLIFETIME(ABRGameState, ShipTo);
	DOREPLIFETIME(ABRGameState, ShipStart);
	DOREPLIFETIME(ABRGameState, ShipSeconds);
}

void ABRGameState::MulticastKill_Implementation(const FString& Killer, const FString& Victim, const int32 KillerTeam, const int32 VictimTeam)
{
	KillLines.Add({Killer, Victim, KillerTeam, VictimTeam, GetServerWorldTimeSeconds()});
	if (KillLines.Num() > 12) { KillLines.RemoveAt(0); }
}

void ABRGameState::GetZoneNow(FVector2D& OutCentre, float& OutRadius) const
{
	if (!bShrinking)
	{
		OutCentre = ZoneFrom;
		OutRadius = ZoneFromRadius;
		return;
	}
	const double Length = FMath::Max(StageEndsAt - ShrinkStartedAt, 0.01);
	const float T = FMath::Clamp(static_cast<float>((GetServerWorldTimeSeconds() - ShrinkStartedAt) / Length), 0.0f, 1.0f);
	OutCentre = FMath::Lerp(ZoneFrom, ZoneTo, static_cast<double>(T));
	OutRadius = FMath::Lerp(ZoneFromRadius, ZoneToRadius, T);
}

float ABRGameState::GetShipProgress() const
{
	return ShipSeconds > 0.0f ? FMath::Clamp(static_cast<float>((GetServerWorldTimeSeconds() - ShipStart) / ShipSeconds), 0.0f, 1.0f) : 1.0f;
}

FVector ABRGameState::GetShipLocation() const
{
	const FVector2D At = FMath::Lerp(ShipFrom, ShipTo, static_cast<double>(GetShipProgress()));
	return FVector(At.X, At.Y, BRRules::ShipHeight);
}

bool ABRGameState::CanDrop() const
{
	return Phase == EBRPhase::Ship && FVector2D(GetShipLocation()).Size() <= BRRules::ShipDropRadius;
}

float ABRGameState::GetStageSecondsLeft() const
{
	return FMath::Max(0.0f, static_cast<float>(StageEndsAt - GetServerWorldTimeSeconds()));
}

ABRGameMode::ABRGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = ABRPlayerController::StaticClass();
	GameStateClass = ABRGameState::StaticClass();
	PlayerStateClass = AChaosImpactPlayerState::StaticClass();
	SimRandom.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
}

void ABRGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	GetWorld()->URL.AddOption(TEXT("CIOpenWorld=1"));
	GetWorld()->URL.AddOption(TEXT("CIBattleIsland=1"));
	CharacterClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/ChaosImpact/BattleRoyale/BP_BRCharacter.BP_BRCharacter_C"));
	if (CharacterClass) { DefaultPawnClass = CharacterClass; }
}

void ABRGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	const ABRGameState* State = GetBRState();
	if (ErrorMessage.IsEmpty() && ((State && State->Phase != EBRPhase::Title) || GetNumPlayers() >= BRRules::MaxHumans))
	{
		ErrorMessage = TEXT("この部屋には入れません");
	}
}

FString ABRGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	// The name the machine sent (Name= holds the engine's own nickname for it).
	const FString Sent = UGameplayStatics::ParseOption(Options, TEXT("CIName")).TrimStartAndEnd().Left(UChaosImpactSessionSubsystem::MaxNameLength);
	if (!Sent.IsEmpty() && NewPlayerController && NewPlayerController->PlayerState) { NewPlayerController->PlayerState->SetPlayerName(Sent); }
	return Error;
}

void ABRGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ReportPlayersToRelay();
}

void ABRGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	ReportPlayersToRelay();
	// Someone in the fight who left is out of it.
	if (const APlayerController* Person = Cast<APlayerController>(Exiting))
	{
		for (FBRFighter& Fighter : Fighters)
		{
			if (Fighter.Person.Get() == Person && Fighter.bAlive) { Fighter.Real = nullptr; KillNumber(Fighter, nullptr); }
		}
	}
}

void ABRGameMode::ReportPlayersToRelay()
{
	if (UBROnlineSubsystem* Online = UBROnlineSubsystem::Get(this))
	{
		const ABRGameState* State = GetBRState();
		Online->SetPlayerCount(GetNumPlayers(), !State || State->Phase == EBRPhase::Title);
	}
}

void ABRGameMode::SetTeamSize(const int32 TeamSize)
{
	if (ABRGameState* State = GetBRState(); State && State->Phase == EBRPhase::Title)
	{
		State->TeamSize = FMath::Clamp(TeamSize, 1, 4);
		State->ForceNetUpdate();
	}
}

void ABRGameMode::SetLocalPlayers(const int32 Count)
{
	UGameInstance* Game = GetGameInstance();
	UWorld* World = GetWorld();
	if (!Game || !World) { return; }
	const int32 Wanted = FMath::Clamp(Count, 1, 4);
	while (Game->GetLocalPlayers().Num() > Wanted)
	{
		ULocalPlayer* Last = Game->GetLocalPlayers().Last();
		if (APlayerController* Controller = Last->GetPlayerController(World)) { UGameplayStatics::RemovePlayer(Controller, true); }
		else { Game->RemoveLocalPlayer(Last); }
	}
	while (Game->GetLocalPlayers().Num() < Wanted)
	{
		if (!UGameplayStatics::CreatePlayer(this, -1, true)) { break; }
	}
	World->URL.RemoveOption(TEXT("CILocalPlayers="));
	World->URL.AddOption(*FString::Printf(TEXT("CILocalPlayers=%d"), Wanted));
	World->URL.RemoveOption(TEXT("CIKeyboardPlayer="));
	World->URL.AddOption(TEXT("CIKeyboardPlayer=0"));
	if (UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>())
	{
		Settings->bUseSplitscreen = true;
		Settings->TwoPlayerSplitscreenLayout = ETwoPlayerSplitScreenType::Vertical;
		Settings->ThreePlayerSplitscreenLayout = EThreePlayerSplitScreenType::FavorTop;
	}
	if (UGameViewportClient* Viewport = World->GetGameViewport()) { Viewport->LayoutPlayers(); }
	// The keyboard is the first person's; each pad plugged in goes to the next.
	AChaosImpactGameMode::ApplyLocalControllerAssignments(World, Wanted, 0);
	TArray<int32> Pads;
	TArray<FInputDeviceId> Devices;
	IPlatformInputDeviceMapper::Get().GetAllConnectedInputDevices(Devices);
	for (const FInputDeviceId& Device : Devices)
	{
		if (Device != IPlatformInputDeviceMapper::Get().GetDefaultInputDevice()) { Pads.Add(Device.GetId()); }
	}
	if (AChaosImpactPlayerController* First = Cast<AChaosImpactPlayerController>(Game->GetFirstLocalPlayerController(World)))
	{
		First->SetLocalInputRouting(Wanted, 0, Pads);
	}
	ReportPlayersToRelay();
	UE_LOG(LogChaosImpact, Log, TEXT("BR: %d people on this screen, %d pads"), Wanted, Pads.Num());
}

AActor* ABRGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) { return *It; }
	return Super::ChoosePlayerStart_Implementation(Player);
}

ABRGameState* ABRGameMode::GetBRState() const { return GetGameState<ABRGameState>(); }

void ABRGameMode::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<ABRIsland> It(GetWorld()); It; ++It) { Island = *It; break; }
	if (Island) { Island->EnsureLoadedAround(FVector2D(-3000.0, 0.0)); }
	BuildLootSpots();
	BuildVehicles();
	// -CIBRAuto=<team size>: straight into a match, for checks and pictures.
	FParse::Value(FCommandLine::Get(), TEXT("CIBRAuto="), AutoTeamSize);
	if (AutoTeamSize > 0) { NextAutoStart = GetWorld()->GetTimeSeconds() + 1.5; }
	FParse::Value(FCommandLine::Get(), TEXT("CIBRWait="), AutoWaitPeople);
	int32 DevLocals = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("CIBRLocals="), DevLocals) && DevLocals > 1) { SetLocalPlayers(DevLocals); }
	if (FParse::Param(FCommandLine::Get(), TEXT("CIBRSelfTest"))) { SelfTestStep = 0; SelfTestNext = GetWorld()->GetTimeSeconds() + 12.0; }
}

void ABRGameMode::BuildLootSpots()
{
	using namespace BRLayout;
	FRandomStream Random(777);
	const auto AddAt = [&](const float X, const float Y, const float Rarity)
	{
		if (BRMap::ShoreDistance(X, Y) < 1500.0f) { return; }
		FBRLootSpot Spot;
		const float Z = BRMap::HeightAt(X, Y);
		if (Z < 10.0f) { return; }
		Spot.At = FVector(X, Y, Z + 8.0f);
		const float Roll = Random.FRand();
		Spot.Kind = Roll < 0.58f ? EBRLootKind::Ball : Roll < 0.76f ? EBRLootKind::Heal : Roll < 0.93f ? EBRLootKind::Plate : EBRLootKind::Vest;
		Spot.BallType = PickBallType(Random, Rarity);
		LootSpots.Add(Spot);
	};
	// Plenty in the built-up parts, some everywhere else.
	for (const FBRDistrict* D : Flat())
	{
		const int32 Count = FMath::RoundToInt(D->Radius * D->Radius * 3.1f / 1400000.0f);
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector2D At = D->At + FVector2D(Random.FRandRange(-D->Radius, D->Radius), Random.FRandRange(-D->Radius, D->Radius));
			if (FVector2D::Distance(At, D->At) < D->Radius) { AddAt(At.X, At.Y, D == &Capital || D == &Airfield || D == &Lab ? 0.65f : 0.4f); }
		}
	}
	for (int32 I = 0; I < 3200; ++I)
	{
		const FVector2D At(Random.FRandRange(-76000.0f, 76000.0f), Random.FRandRange(-76000.0f, 76000.0f));
		if (BRMap::IsOpenGround(At.X, At.Y)) { AddAt(At.X, At.Y, 0.22f); }
	}
	UE_LOG(LogChaosImpact, Log, TEXT("BR loot spots: %d"), LootSpots.Num());
}

EChaosImpactBallType ABRGameMode::PickBallType(FRandomStream& Random, const float Rarity) const
{
	if (Random.FRand() > Rarity + 0.25f) { return EChaosImpactBallType::Normal; }
	const EChaosImpactBallType Options[] = {EChaosImpactBallType::Fire, EChaosImpactBallType::Ice, EChaosImpactBallType::Thunder, EChaosImpactBallType::Wind,
		EChaosImpactBallType::Smoke, EChaosImpactBallType::Black, EChaosImpactBallType::Beam, EChaosImpactBallType::Snow};
	return Options[Random.RandHelper(UE_ARRAY_COUNT(Options))];
}

void ABRGameMode::BuildVehicles()
{
	using namespace BRLayout;
	FRandomStream Random(778);
	const auto Place = [&](const FVector2D& At, const EBRVehicleKind Kind)
	{
		if (BRMap::ShoreDistance(At.X, At.Y) < 1500.0f) { return; }
		const FRotator Facing(0.0f, Random.FRandRange(0.0f, 360.0f), 0.0f);
		const FVector Where(At.X, At.Y, BRMap::HeightAt(At.X, At.Y) + 30.0f);
		ABRVehicle* Vehicle = GetWorld()->SpawnActor<ABRVehicle>(ABRVehicle::StaticClass(), Where, Facing);
		if (Vehicle)
		{
			const FLinearColor Paints[] = {FLinearColor(0.75f, 0.08f, 0.1f), FLinearColor(0.06f, 0.25f, 0.7f), FLinearColor(0.95f, 0.62f, 0.05f), FLinearColor(0.1f, 0.55f, 0.3f), FLinearColor(0.82f, 0.82f, 0.86f)};
			Vehicle->Setup(Kind, Paints[Random.RandHelper(UE_ARRAY_COUNT(Paints))]);
			Vehicles.Add(Vehicle);
		}
	};
	// Along the roads; in pairs of boards and now and then a car.
	for (const TArray<FVector2D>& Line : BRMap::Roads())
	{
		for (int32 I = 2; I + 1 < Line.Num(); I += 11)
		{
			const FVector2D Way = (Line[I + 1] - Line[I]).GetSafeNormal();
			const FVector2D Side(-Way.Y, Way.X);
			Place(Line[I] + Side * 900.0, Random.FRand() < 0.4f ? EBRVehicleKind::Car : EBRVehicleKind::Skateboard);
			Place(Line[I] - Side * 900.0, EBRVehicleKind::Skateboard);
		}
	}
	for (const FBRDistrict* D : Flat())
	{
		for (int32 I = 0; I < 5; ++I)
		{
			const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
			Place(D->At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * D->Radius * 0.8, I % 2 ? EBRVehicleKind::Car : EBRVehicleKind::Skateboard);
		}
	}
	UE_LOG(LogChaosImpact, Log, TEXT("BR vehicles: %d"), Vehicles.Num());
}

void ABRGameMode::SetPhase(const EBRPhase Phase, const double Seconds)
{
	ABRGameState* State = GetBRState();
	if (!State) { return; }
	State->Phase = Phase;
	State->PhaseEndsAt = State->GetServerWorldTimeSeconds() + Seconds;
	State->ForceNetUpdate();
	UE_LOG(LogChaosImpact, Log, TEXT("BR phase: %d"), static_cast<int32>(Phase));
}

void ABRGameMode::StartMatch(const int32 TeamSize)
{
	ABRGameState* State = GetBRState();
	if (!State || State->Phase != EBRPhase::Title) { return; }
	State->TeamSize = FMath::Clamp(TeamSize, 1, 4);
	const int32 Size = State->TeamSize;
	Fighters.Reset();
	ChosenLandings.Reset();
	// The people, each team filled from the front.
	int32 NextSeat = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Person = It->Get();
		if (!Person) { continue; }
		FBRFighter Fighter;
		Fighter.bHuman = true;
		Fighter.Person = Person;
		const int32 Profile = ChaosImpactSettings::GetPlayerProfile(Person);
		Fighter.Name = Profile > 0 ? ChaosImpactSettings::GetProfile(Profile).Name : FString(TEXT("あなた"));
		if (Profile <= 0 && GetNetMode() != NM_Standalone)
		{
			// Online everyone goes by their own name.
			const UChaosImpactSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UChaosImpactSessionSubsystem>();
			const FString Own = Person->IsLocalController() ? (Sessions ? Sessions->GetPlayerName() : FString()) : (Person->PlayerState ? Person->PlayerState->GetPlayerName() : FString());
			if (!Own.IsEmpty()) { Fighter.Name = Own; }
		}
		Fighter.Team = NextSeat / Size;
		if (const AChaosImpactPlayerState* Loadout = Person->GetPlayerState<AChaosImpactPlayerState>())
		{
			Fighter.Character = Loadout->CharacterIndex;
		}
		// Others sharing this screen go as they last chose.
		if (const ULocalPlayer* Local = Person->GetLocalPlayer(); Local && Local != GetGameInstance()->GetFirstGamePlayer())
		{
			if (const UChaosImpactLoadoutSubsystem* Loadouts = UChaosImpactLoadoutSubsystem::Get(Person))
			{
				Fighter.Character = ChaosImpactRoster::ClampIndex(Loadouts->GetLoadout(GetGameInstance()->GetLocalPlayers().IndexOfByKey(Local)).Character);
				if (AChaosImpactPlayerState* State2 = Person->GetPlayerState<AChaosImpactPlayerState>()) { State2->CharacterIndex = Fighter.Character; State2->ForceNetUpdate(); }
			}
		}
		Fighter.Skill = 1.0f;
		Fighters.Add(Fighter);
		++NextSeat;
	}
	// CPUs for the rest: mostly ordinary, a few strong.
	int32 Named = SimRandom.RandHelper(UE_ARRAY_COUNT(BRBotNames));
	while (Fighters.Num() < BRRules::TotalFighters)
	{
		FBRFighter Bot;
		Bot.Team = NextSeat / Size;
		Bot.Name = FString::Printf(TEXT("%s%d"), BRBotNames[Named++ % UE_ARRAY_COUNT(BRBotNames)], Fighters.Num() + 1);
		Bot.Character = SimRandom.RandHelper(ChaosImpactRoster::Num());
		Bot.Colour = SimRandom.RandHelper(ChaosImpactRoster::ColourCount);
		const float Roll = SimRandom.FRand();
		Bot.Skill = Roll < 0.7f ? SimRandom.FRandRange(0.2f, 0.55f) : Roll < 0.94f ? SimRandom.FRandRange(0.55f, 0.8f) : SimRandom.FRandRange(0.85f, 1.0f);
		Fighters.Add(Bot);
		++NextSeat;
	}
	State->Roster.SetNum(Fighters.Num());
	UpdateRoster();
	BoardEveryone();
	ReportPlayersToRelay();
}

void ABRGameMode::SetLanding(APlayerController* Who, const FVector2D& Where)
{
	const ABRGameState* State = GetBRState();
	if (State && State->Phase == EBRPhase::Ship && BRMap::ShoreDistance(Where.X, Where.Y) > 1800.0f)
	{
		ChosenLandings.Add(Who, Where);
	}
}

FVector2D ABRGameMode::PickLanding(FRandomStream& Random) const
{
	using namespace BRLayout;
	// Mostly a built-up place (crowded at first), now and then the open.
	for (int32 Try = 0; Try < 40; ++Try)
	{
		FVector2D At;
		if (Random.FRand() < 0.72f)
		{
			const FBRDistrict* D = Flat()[Random.RandHelper(Flat().Num())];
			const float Angle = Random.FRandRange(0.0f, UE_TWO_PI);
			At = D->At + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Random.FRandRange(0.0f, D->Radius);
		}
		else
		{
			At = FVector2D(Random.FRandRange(-70000.0f, 70000.0f), Random.FRandRange(-70000.0f, 70000.0f));
		}
		if (BRMap::ShoreDistance(At.X, At.Y) > 2500.0f) { return At; }
	}
	return FVector2D(-3000.0, 0.0);
}

void ABRGameMode::BoardEveryone()
{
	ABRGameState* State = GetBRState();
	const double Now = State->GetServerWorldTimeSeconds();
	// The airship's line: straight across the island, off to one side of the middle.
	const float Angle = SimRandom.FRandRange(0.0f, UE_TWO_PI);
	const FVector2D Way(FMath::Cos(Angle), FMath::Sin(Angle)), Across(-Way.Y, Way.X);
	const FVector2D Mid = Across * SimRandom.FRandRange(-20000.0f, 20000.0f);
	State->ShipFrom = Mid - Way * BRRules::ShipReach;
	State->ShipTo = Mid + Way * BRRules::ShipReach;
	State->ShipStart = Now;
	State->ShipSeconds = AutoTeamSize > 0 && SelfTestStep >= 0 ? 14.0f : BRRules::ShipSeconds;

	TSet<int32> PeopleTeams;
	for (const FBRFighter& Fighter : Fighters) { if (Fighter.bHuman) { PeopleTeams.Add(Fighter.Team); } }
	// Teams of CPUs each drop together, somewhere near their stretch of the line.
	TMap<int32, FVector2D> Drops;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FBRFighter& Fighter = Fighters[Index];
		if (!PeopleTeams.Contains(Fighter.Team))
		{
			if (!Drops.Contains(Fighter.Team))
			{
				const FVector2D Under = FMath::Lerp(State->ShipFrom, State->ShipTo, static_cast<double>(SimRandom.FRandRange(0.14f, 0.86f)));
				FVector2D Best = Under;
				double BestD = TNumericLimits<double>::Max();
				for (int32 Try = 0; Try < 10; ++Try)
				{
					const FVector2D Candidate = PickLanding(SimRandom);
					const double D = FVector2D::DistSquared(Candidate, Under);
					if (D < BestD && D < FMath::Square(40000.0)) { BestD = D; Best = Candidate; }
				}
				Drops.Add(Fighter.Team, Best);
			}
			Fighter.Pos = Drops[Fighter.Team] + FVector2D(SimRandom.FRandRange(-500.0f, 500.0f), SimRandom.FRandRange(-500.0f, 500.0f));
			Fighter.Goal = Fighter.Pos;
			Fighter.NextDecide = GetWorld()->GetTimeSeconds() + SimRandom.FRandRange(15.0f, 40.0f);
			continue;
		}
		// People, and the CPUs on their teams, are aboard for real.
		const FVector Aboard = State->GetShipLocation();
		ABRCharacter* Body = nullptr;
		if (Fighter.bHuman && Fighter.Person.IsValid())
		{
			Body = Cast<ABRCharacter>(Fighter.Person->GetPawn());
			if (Body)
			{
				Body->InitFighter(Index, Fighter.Team, Fighter.Name);
				Body->OnEliminated.AddUniqueDynamic(this, &ABRGameMode::HandleEliminated);
				Fighter.Real = Body;
			}
		}
		else
		{
			Body = SpawnReal(Index, Aboard, nullptr);
		}
		if (Body) { Body->BoardShip(); }
	}
	SetPhase(EBRPhase::Ship, State->ShipSeconds);
}

void ABRGameMode::TickShip()
{
	ABRGameState* State = GetBRState();
	const double Now = State->GetServerWorldTimeSeconds();
	// CPUs go when their person goes, a moment apart. (Checks: people jump by themselves early on.)
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FBRFighter& Fighter = Fighters[Index];
		ABRCharacter* Body = Fighter.Real.Get();
		if (!Body || !Body->IsAboard()) { continue; }
		if (Fighter.bHuman)
		{
			if (AutoTeamSize > 0 && Now - State->ShipStart > 3.0 && State->CanDrop() && Body->IsLocallyControlled()) { Body->JumpOff(); }
			continue;
		}
		const ABRCharacter* Ally = FindNearestAlly(Body);
		if (Ally && !Ally->IsAboard() && SimRandom.FRand() < 0.05f) { Body->JumpOff(); }
	}
	// Past the far shore everyone still aboard goes, while the island is in reach.
	if (Now >= State->PhaseEndsAt || (State->GetShipProgress() > 0.5f && !State->CanDrop()))
	{
		for (const FBRFighter& Fighter : Fighters)
		{
			if (ABRCharacter* Body = Fighter.Real.Get(); Body && Body->IsAboard()) { Body->JumpOff(); }
		}
	}
	if (Now >= State->PhaseEndsAt)
	{
		SetPhase(EBRPhase::Play, 0.0);
		StartZone();
	}
}

ABRCharacter* ABRGameMode::SpawnReal(const int32 Index, const FVector& At, AController* Person)
{
	FBRFighter& Fighter = Fighters[Index];
	if (!CharacterClass) { return nullptr; }
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ABRBotController* Brain = GetWorld()->SpawnActor<ABRBotController>(ABRBotController::StaticClass(), At, FRotator::ZeroRotator, Parameters);
	ABRCharacter* Body = GetWorld()->SpawnActor<ABRCharacter>(CharacterClass, At, FRotator(0.0f, SimRandom.FRandRange(0.0f, 360.0f), 0.0f), Parameters);
	if (!Brain || !Body)
	{
		if (Brain) { Brain->Destroy(); }
		if (Body) { Body->Destroy(); }
		return nullptr;
	}
	Brain->Skill = Fighter.Skill;
	Brain->Possess(Body);
	if (AChaosImpactPlayerState* Loadout = Brain->GetPlayerState<AChaosImpactPlayerState>())
	{
		Loadout->SetPlayerName(Fighter.Name);
		Loadout->CharacterIndex = Fighter.Character;
		Loadout->ColourChoice = Fighter.Colour;
		Loadout->ForceNetUpdate();
	}
	Body->InitFighter(Index, Fighter.Team, Fighter.Name);
	Body->SetHealthDirect(Fighter.Health);
	Body->SetArmorMax(Fighter.Gear > 0.6f ? 75.0f : Fighter.Gear > 0.25f ? 50.0f : 25.0f);
	Body->Armor = FMath::Min(Fighter.Armor, Body->ArmorMax);
	if (Fighter.Gear > 0.1f || SimRandom.FRand() < 0.7f) { Body->GiveBall(SimRandom.FRand() < Fighter.Gear ? PickBallType(SimRandom, Fighter.Gear) : EChaosImpactBallType::Normal); }
	if (Fighter.Gear > 0.4f) { Body->GiveBall(EChaosImpactBallType::Normal); }
	Body->OnEliminated.AddUniqueDynamic(this, &ABRGameMode::HandleEliminated);
	Fighter.Real = Body;
	return Body;
}

void ABRGameMode::Realize(const int32 Index)
{
	FBRFighter& Fighter = Fighters[Index];
	FVector At(Fighter.Pos.X, Fighter.Pos.Y, BRMap::HeightAt(Fighter.Pos.X, Fighter.Pos.Y) + 110.0f);
	SpawnReal(Index, At, nullptr);
}

void ABRGameMode::Unrealize(const int32 Index)
{
	FBRFighter& Fighter = Fighters[Index];
	ABRCharacter* Body = Fighter.Real.Get();
	if (!Body) { return; }
	Fighter.Pos = FVector2D(Body->GetActorLocation());
	Fighter.Health = Body->GetHealthNow();
	Fighter.Armor = Body->Armor;
	Fighter.Goal = Fighter.Pos;
	Fighter.Real = nullptr;
	if (AController* Brain = Body->GetController())
	{
		Brain->UnPossess();
		Brain->Destroy();
	}
	Body->Destroy();
}

void ABRGameMode::CollectPeople(TArray<FVector2D>& Out) const
{
	for (const FBRFighter& Fighter : Fighters)
	{
		if (Fighter.bHuman && Fighter.bAlive && Fighter.Real.IsValid() && !Fighter.Real->IsFlying())
		{
			Out.Add(FVector2D(Fighter.Real->GetActorLocation()));
		}
	}
	// Those watching a CPU teammate keep the fight around it real.
	for (const FBRFighter& Fighter : Fighters)
	{
		if (!Fighter.bHuman && Fighter.bAlive && Fighter.Real.IsValid() && !Fighter.Real->IsFlying() && Fighter.Team == 0 && Out.IsEmpty())
		{
			Out.Add(FVector2D(Fighter.Real->GetActorLocation()));
		}
	}
}

void ABRGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABRGameState* State = GetBRState();
	if (!State) { return; }
	const double Now = State->GetServerWorldTimeSeconds();
	if (AutoTeamSize > 0 && NextAutoStart > 0.0 && GetWorld()->GetTimeSeconds() >= NextAutoStart && GetNumPlayers() >= FMath::Max(1, AutoWaitPeople))
	{
		NextAutoStart = 0.0;
		StartMatch(AutoTeamSize);
	}
	switch (State->Phase)
	{
	case EBRPhase::Ship:
		TickShip();
		TickFighters(DeltaSeconds);
		break;
	case EBRPhase::Play:
		if (DevEndAt < 0.0)
		{
			float Seconds = 0.0f;
			DevEndAt = FParse::Value(FCommandLine::Get(), TEXT("CIBREnd="), Seconds) ? Now + Seconds : 0.0;
		}
		if (DevEndAt > 0.0 && Now >= DevEndAt) { FinishMatch(Fighters.IsValidIndex(0) ? Fighters[0].Team : 0); }
		if (SelfTestStep >= 0) { TickSelfTest(); }
		TickZone();
		TickFighters(DeltaSeconds);
		break;
	default:
		break;
	}
	if (State->Phase != EBRPhase::Title && Now >= RosterClock)
	{
		RosterClock = Now + 0.33;
		UpdateRoster();
	}
	if (Now >= LootClock && State->Phase != EBRPhase::Title)
	{
		LootClock = Now + 0.5;
		TickLoot();
	}
}

void ABRGameMode::StartZone()
{
	ABRGameState* State = GetBRState();
	State->ZoneFrom = FVector2D::ZeroVector;
	State->ZoneFromRadius = BRRules::StartRadius;
	NextStage(0);
}

void ABRGameMode::NextStage(const int32 Index)
{
	ABRGameState* State = GetBRState();
	const BRRules::FZoneStage& Stage = BRRules::Stages[Index];
	State->StageIndex = Index;
	State->bShrinking = false;
	State->StageEndsAt = State->GetServerWorldTimeSeconds() + Stage.Hold;
	// The next circle lies wholly within this one, somewhere with land in it.
	FVector2D Best = State->ZoneFrom;
	for (int32 Try = 0; Try < 40; ++Try)
	{
		const float Reach = FMath::Max(0.0f, State->ZoneFromRadius - Stage.Radius) * 0.92f;
		const float Angle = SimRandom.FRandRange(0.0f, UE_TWO_PI);
		const float Distance = Reach * FMath::Sqrt(SimRandom.FRand());
		const FVector2D At = State->ZoneFrom + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;
		if (BRMap::ShoreDistance(At.X, At.Y) > FMath::Min(Stage.Radius * 0.5f, 9000.0f) + 500.0f) { Best = At; break; }
	}
	State->ZoneTo = Best;
	State->ZoneToRadius = Stage.Radius;
	State->ForceNetUpdate();
}

void ABRGameMode::TickZone()
{
	ABRGameState* State = GetBRState();
	const double Now = State->GetServerWorldTimeSeconds();
	if (Now >= State->StageEndsAt)
	{
		if (!State->bShrinking)
		{
			State->bShrinking = true;
			State->ShrinkStartedAt = Now;
			State->StageEndsAt = Now + BRRules::Stages[State->StageIndex].Shrink;
		}
		else
		{
			State->ZoneFrom = State->ZoneTo;
			State->ZoneFromRadius = State->ZoneToRadius;
			State->bShrinking = false;
			if (State->StageIndex + 1 < BRRules::StageCount) { NextStage(State->StageIndex + 1); }
			else { State->StageEndsAt = Now + 3600.0; }
		}
		State->ForceNetUpdate();
	}
	// Outside it hurts, armour or not.
	if (Now < ZoneDamageClock) { return; }
	ZoneDamageClock = Now + 1.0;
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	const float Dps = BRRules::Stages[State->StageIndex].Dps;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FBRFighter& Fighter = Fighters[Index];
		if (!Fighter.bAlive) { continue; }
		if (ABRCharacter* Body = Fighter.Real.Get())
		{
			if (!Body->IsEliminated() && FVector2D::Distance(FVector2D(Body->GetActorLocation()), Centre) > Radius)
			{
				Body->TakeDamage(Dps, FDamageEvent(), nullptr, this);
			}
		}
		else if (FVector2D::Distance(Fighter.Pos, Centre) > Radius)
		{
			Fighter.Health -= Dps;
			if (Fighter.Health <= 0.0f) { KillNumber(Fighter, nullptr); }
		}
	}
}

void ABRGameMode::TickFighters(const float DeltaSeconds)
{
	ABRGameState* State = GetBRState();
	const double Now = State->GetServerWorldTimeSeconds();
	if (Now < SimClock) { return; }
	const float Step = 0.25f;
	SimClock = Now + Step;
	TArray<FVector2D> People;
	CollectPeople(People);
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	const auto NearestPerson = [&People](const FVector2D& At)
	{
		float Best = TNumericLimits<float>::Max();
		for (const FVector2D& P : People) { Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(At, P))); }
		return Best;
	};

	// ---- Who's real.
	int32 Real = 0;
	for (const FBRFighter& Fighter : Fighters) { if (Fighter.bAlive && !Fighter.bHuman && Fighter.Real.IsValid()) { ++Real; } }
	TSet<int32> PeopleTeams;
	for (const FBRFighter& Fighter : Fighters) { if (Fighter.bHuman) { PeopleTeams.Add(Fighter.Team); } }
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FBRFighter& Fighter = Fighters[Index];
		if (!Fighter.bAlive || Fighter.bHuman) { continue; }
		const bool bFriend = PeopleTeams.Contains(Fighter.Team);
		if (Fighter.Real.IsValid())
		{
			if (!bFriend && NearestPerson(FVector2D(Fighter.Real->GetActorLocation())) > BRRules::DataRange)
			{
				Unrealize(Index);
				--Real;
			}
		}
		else if (!bFriend && Real < BRRules::MaxRealBots && NearestPerson(Fighter.Pos) < BRRules::RealRange)
		{
			Realize(Index);
			++Real;
		}
	}

	// ---- The numbers: they head for safety, arm themselves as they go, and fight when they meet.
	for (FBRFighter& Fighter : Fighters)
	{
		if (!Fighter.bAlive || Fighter.Real.IsValid() || Fighter.bHuman) { continue; }
		Fighter.Gear = FMath::Min(1.0f, Fighter.Gear + Step * 0.0012f * (0.5f + Fighter.Skill));
		const float ToZone = FVector2D::Distance(Fighter.Pos, State->ZoneTo);
		if (Now >= Fighter.NextDecide || FVector2D::Distance(Fighter.Pos, Fighter.Goal) < 300.0)
		{
			Fighter.NextDecide = Now + SimRandom.FRandRange(10.0f, 30.0f);
			const float Keep = FMath::Max(State->ZoneToRadius * 0.85f, 400.0f);
			const float Angle = SimRandom.FRandRange(0.0f, UE_TWO_PI);
			const FVector2D Try = State->ZoneTo + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Keep * FMath::Sqrt(SimRandom.FRand());
			// Far from the circle: go straight in; inside it: drift to another place in it.
			Fighter.Goal = BRMap::ShoreDistance(Try.X, Try.Y) > 1500.0f ? Try : State->ZoneTo;
		}
		const FVector2D Way = Fighter.Goal - Fighter.Pos;
		const float Length = Way.Size();
		const bool bHurry = ToZone > State->ZoneToRadius || FVector2D::Distance(Fighter.Pos, Centre) > Radius * 0.92f;
		const float Speed = bHurry ? 620.0f : 340.0f;
		if (Length > 1.0f) { Fighter.Pos += Way / Length * FMath::Min(Length, Speed * Step); }
	}
	TickFights(Step);
}

void ABRGameMode::TickFights(const float Step)
{
	// Number CPUs that meet another team's: a bucket grid so 80 of them stay cheap.
	constexpr float Cell = 3400.0f;
	TMap<uint64, TArray<int32>> Grid;
	const auto Key = [](const FVector2D& P)
	{
		return (static_cast<uint64>(static_cast<uint32>(FMath::FloorToInt(P.X / Cell))) << 32) | static_cast<uint32>(FMath::FloorToInt(P.Y / Cell));
	};
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		const FBRFighter& Fighter = Fighters[Index];
		if (Fighter.bAlive && !Fighter.Real.IsValid() && !Fighter.bHuman) { Grid.FindOrAdd(Key(Fighter.Pos)).Add(Index); }
	}
	for (TPair<uint64, TArray<int32>>& Bucket : Grid)
	{
		for (int32 A = 0; A < Bucket.Value.Num(); ++A)
		{
			FBRFighter& Me = Fighters[Bucket.Value[A]];
			for (int32 B = A + 1; B < Bucket.Value.Num() && Me.bAlive; ++B)
			{
				FBRFighter& Other = Fighters[Bucket.Value[B]];
				if (!Other.bAlive || Other.Team == Me.Team || FVector2D::Distance(Me.Pos, Other.Pos) > Cell) { continue; }
				// Each trades blows now and then, the better armed and more skilful landing harder.
				const float PerSecond = 0.09f;
				if (SimRandom.FRand() < PerSecond * Step)
				{
					const float MeDamage = 12.0f + 26.0f * Me.Skill + 16.0f * Me.Gear;
					const float OtherDamage = 12.0f + 26.0f * Other.Skill + 16.0f * Other.Gear;
					const auto Hit = [&](FBRFighter& Victim, FBRFighter& Killer, float Damage)
					{
						const float Absorbed = FMath::Min(Victim.Armor, Damage);
						Victim.Armor -= Absorbed;
						Victim.Health -= Damage - Absorbed;
						if (Victim.Health <= 0.0f) { KillNumber(Victim, &Killer); }
					};
					Hit(Other, Me, MeDamage * SimRandom.FRandRange(0.5f, 1.1f));
					if (Other.bAlive) { Hit(Me, Other, OtherDamage * SimRandom.FRandRange(0.5f, 1.1f)); }
				}
			}
		}
	}
}

void ABRGameMode::KillNumber(FBRFighter& Victim, FBRFighter* Killer)
{
	ABRGameState* State = GetBRState();
	Victim.bAlive = false;
	Victim.Health = 0.0f;
	if (Killer) { ++Killer->Kills; }
	State->MulticastKill(Killer ? Killer->Name : FString(), Victim.Name, Killer ? Killer->Team : -1, Victim.Team);
	Victim.Place = State->AliveFighters;
	CheckEnd();
}

void ABRGameMode::HandleEliminated(AChaosImpactCharacter* Character)
{
	ABRCharacter* Body = Cast<ABRCharacter>(Character);
	ABRGameState* State = GetBRState();
	if (!Body || !State || !Fighters.IsValidIndex(Body->Fighter)) { return; }
	FBRFighter& Victim = Fighters[Body->Fighter];
	if (!Victim.bAlive) { return; }
	Victim.bAlive = false;
	Victim.Health = 0.0f;
	const ABRCharacter* Source = Cast<ABRCharacter>(Body->GetLastHitSourcePawnForBR());
	FBRFighter* Killer = Source && Fighters.IsValidIndex(Source->Fighter) && Source != Body ? &Fighters[Source->Fighter] : nullptr;
	if (Killer && Killer->Team == Victim.Team) { Killer = nullptr; }
	if (Killer)
	{
		++Killer->Kills;
		if (ABRCharacter* KillerBody = Killer->Real.Get()) { ++KillerBody->KillCount; KillerBody->ForceNetUpdate(); }
	}
	State->MulticastKill(Killer ? Killer->Name : FString(), Victim.Name, Killer ? Killer->Team : -1, Victim.Team);
	// Non-people leave no body; a person's stays until the end so their view has somewhere to be.
	if (!Victim.bHuman)
	{
		if (AController* Brain = Body->GetController()) { FTimerHandle Timer; TWeakObjectPtr<AController> Weak(Brain); GetWorldTimerManager().SetTimer(Timer, [Weak]() { if (Weak.IsValid()) { Weak->Destroy(); } }, 1.5f, false); }
		Body->SetLifeSpan(3.0f);
		Victim.Real = nullptr;
	}
	Victim.Place = State->AliveFighters;
	CheckEnd();
}

void ABRGameMode::FighterDown(const int32, const int32) {}

int32 ABRGameMode::CountAliveTeams() const
{
	TSet<int32> Alive;
	for (const FBRFighter& Fighter : Fighters)
	{
		if (Fighter.bAlive) { Alive.Add(Fighter.Team); }
	}
	return Alive.Num();
}

void ABRGameMode::CheckEnd()
{
	ABRGameState* State = GetBRState();
	if (!State || State->Phase != EBRPhase::Play) { return; }
	UpdateRoster();
	TSet<int32> Alive;
	for (const FBRFighter& Fighter : Fighters) { if (Fighter.bAlive) { Alive.Add(Fighter.Team); } }
	if (Alive.Num() <= 1)
	{
		FinishMatch(Alive.IsEmpty() ? -1 : *Alive.CreateConstIterator());
	}
}

void ABRGameMode::FinishMatch(const int32 WinnerTeam)
{
	ABRGameState* State = GetBRState();
	State->WinnerTeam = WinnerTeam;
	SetPhase(EBRPhase::Over, 0.0);
	UpdateRoster();
}

void ABRGameMode::UpdateRoster()
{
	ABRGameState* State = GetBRState();
	if (!State) { return; }
	int32 AliveCount = 0;
	TSet<int32> Teams;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FBRFighter& Fighter = Fighters[Index];
		FBRRosterEntry& Entry = State->Roster[Index];
		const ABRCharacter* Body = Fighter.Real.Get();
		FVector2D Pos = Fighter.Pos;
		if (Body)
		{
			Pos = FVector2D(Body->GetActorLocation());
			Fighter.Pos = Pos;
			Fighter.Health = Body->IsDowned() ? 1.0f : Body->GetHealthNow();
			Fighter.Armor = Body->Armor;
			Fighter.Kills = FMath::Max(Fighter.Kills, Body->KillCount);
		}
		Entry.Name = Fighter.Name;
		Entry.Team = static_cast<uint8>(Fighter.Team);
		Entry.Character = static_cast<uint8>(Fighter.Character);
		Entry.Colour = static_cast<uint8>(Fighter.Colour);
		Entry.bHuman = Fighter.bHuman;
		Entry.bAlive = Fighter.bAlive;
		Entry.bDowned = Body && Body->IsDowned();
		Entry.Health = static_cast<uint8>(FMath::Clamp(Fighter.Health, 0.0f, 100.0f));
		Entry.Armor = static_cast<uint8>(FMath::Clamp(Fighter.Armor, 0.0f, 200.0f));
		Entry.Kills = static_cast<uint8>(FMath::Clamp(Fighter.Kills, 0, 200));
		Entry.X = static_cast<int16>(FMath::Clamp(Pos.X / 8.0, -32000.0, 32000.0));
		Entry.Y = static_cast<int16>(FMath::Clamp(Pos.Y / 8.0, -32000.0, 32000.0));
		if (Fighter.bAlive) { ++AliveCount; Teams.Add(Fighter.Team); }
	}
	State->AliveFighters = AliveCount;
	State->AliveTeams = Teams.Num();
}

void ABRGameMode::TickLoot()
{
	// Real only near a person.
	TArray<FVector2D> People;
	CollectPeople(People);
	constexpr double Near = 11000.0, Far = 16000.0;
	for (int32 Index = 0; Index < LootSpots.Num(); ++Index)
	{
		FBRLootSpot& Spot = LootSpots[Index];
		if (Spot.bTaken) { continue; }
		double Best = TNumericLimits<double>::Max();
		for (const FVector2D& P : People) { Best = FMath::Min(Best, FVector2D::DistSquared(P, FVector2D(Spot.At))); }
		if (AActor* Spawned = Spot.Spawned.Get())
		{
			if (Best > Far * Far) { Spawned->Destroy(); Spot.Spawned = nullptr; }
			continue;
		}
		if (Best > Near * Near) { continue; }
		// The ground the spot was laid out on may have a building on it: rest on whatever is really there.
		FHitResult Hit;
		FVector At = Spot.At;
		if (GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.0f, 0.0f, 900.0f), At - FVector(0.0f, 0.0f, 300.0f), ECC_WorldStatic)) { At = Hit.ImpactPoint + FVector(0.0f, 0.0f, 8.0f); }
		if (Spot.Kind == EBRLootKind::Ball)
		{
			const FTransform Where(At + FVector(0.0f, 0.0f, 40.0f));
			ABRBall* Ball = GetWorld()->SpawnActorDeferred<ABRBall>(ABRBall::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Ball)
			{
				Ball->SetBallType(Spot.BallType);
				Ball->FinishSpawning(Where);
				Ball->MakePickup();
				Spot.Spawned = Ball;
			}
		}
		else if (ABRLoot* Loot = GetWorld()->SpawnActor<ABRLoot>(ABRLoot::StaticClass(), At + FVector(0.0f, 0.0f, 40.0f), FRotator::ZeroRotator))
		{
			Loot->Setup(Spot.Kind);
			Loot->Spot = Index;
			Spot.Spawned = Loot;
		}
	}
	// A ball spawned from a spot that has been picked up is gone for good.
	for (FBRLootSpot& Spot : LootSpots)
	{
		if (!Spot.bTaken && Spot.Kind == EBRLootKind::Ball && Spot.Spawned.IsStale()) { Spot.bTaken = true; }
	}
}

void ABRGameMode::NotifyLootTaken(const int32 Spot)
{
	if (LootSpots.IsValidIndex(Spot)) { LootSpots[Spot].bTaken = true; }
}

void ABRGameMode::NotifyBallTaken(AChaosImpactBall*) {}

ABRCharacter* ABRGameMode::FindNearestAlly(ABRCharacter* Self) const
{
	ABRCharacter* Best = nullptr;
	double BestD = TNumericLimits<double>::Max();
	for (const FBRFighter& Fighter : Fighters)
	{
		ABRCharacter* Other = Fighter.Real.Get();
		if (!Other || Other == Self || Fighter.Team != Self->TeamId || Other->IsEliminated()) { continue; }
		const double D = FVector::DistSquared2D(Other->GetActorLocation(), Self->GetActorLocation());
		// People first.
		if (Fighter.bHuman) { return Other; }
		if (D < BestD) { BestD = D; Best = Other; }
	}
	return Best;
}

FVector ABRGameMode::GetBotLanding(ABRCharacter* Bot) const
{
	if (const ABRCharacter* Ally = FindNearestAlly(Bot)) { return Ally->GetActorLocation(); }
	return Fighters.IsValidIndex(Bot->Fighter) ? FVector(Fighters[Bot->Fighter].Pos.X, Fighters[Bot->Fighter].Pos.Y, 0.0f) : Bot->GetActorLocation();
}

FVector ABRGameMode::GetBotGoal(ABRCharacter* Bot)
{
	const ABRGameState* State = GetBRState();
	if (!State || !Fighters.IsValidIndex(Bot->Fighter)) { return Bot->GetActorLocation(); }
	FBRFighter& Fighter = Fighters[Bot->Fighter];
	const double Now = State->GetServerWorldTimeSeconds();
	FVector2D Centre;
	float Radius;
	State->GetZoneNow(Centre, Radius);
	const FVector2D Here(Bot->GetActorLocation());
	if (Now >= Fighter.NextDecide || FVector2D::Distance(Here, Fighter.Goal) < 350.0)
	{
		Fighter.NextDecide = Now + SimRandom.FRandRange(14.0f, 34.0f);
		const float Keep = FMath::Max(State->ZoneToRadius * 0.75f, 300.0f);
		const float Angle = SimRandom.FRandRange(0.0f, UE_TWO_PI);
		const FVector2D Try = State->ZoneTo + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Keep * FMath::Sqrt(SimRandom.FRand());
		Fighter.Goal = BRMap::ShoreDistance(Try.X, Try.Y) > 1500.0f ? Try : State->ZoneTo;
	}
	return FVector(Fighter.Goal.X, Fighter.Goal.Y, Bot->GetActorLocation().Z);
}

void ABRGameMode::ResetToTitle()
{
	// With guests in the room everyone goes back to the title together, the room still open.
	if (GetNetMode() == NM_ListenServer)
	{
		GetWorld()->ServerTravel(FString::Printf(TEXT("%s?listen?CIOpenWorld=1?CIBattleIsland=1"), *GetWorld()->GetOutermost()->GetName().Replace(*GetWorld()->StreamingLevelsPrefix, TEXT(""))), true);
		return;
	}
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, TEXT(""));
}
