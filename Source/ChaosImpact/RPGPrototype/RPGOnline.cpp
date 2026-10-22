#include "RPGOnline.h"
#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGPrototypeVFX.h"
#include "ChaosImpactSimaeBossFx.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpact.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/UObjectGlobals.h"
#include "RPGTitle.h"
#include "RPGSnowField.h"
#include "Engine/DamageEvents.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
const FName RPGPassKey(TEXT("CIRPGPASS"));
const FName RPGHostKey(TEXT("CIRPGHOST"));
const FName RPGCountKey(TEXT("CIRPGCOUNT"));
const TCHAR* RPGOnlineSection = TEXT("ChaosImpact.RPGOnline");
}

// ---------------------------------------------------------------- relay

ARPGNetRelay::ARPGNetRelay()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RelayRoot")));
}

ARPGNetRelay* ARPGNetRelay::Get(const UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<ARPGNetRelay> It(const_cast<UWorld*>(World)); It; ++It) { return *It; }
	return nullptr;
}

ARPGNetRelay* ARPGNetRelay::Ensure(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client) { return nullptr; }
	if (auto* Existing = Get(World)) { return Existing; }
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ARPGNetRelay>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
}

void ARPGNetRelay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	DamageTexts.RemoveAll([&](const FRPGPrototypeDamageText& Text) { return Now - Text.StartedAt > 1.15; });
}

void ARPGNetRelay::MulticastDamageText_Implementation(FVector_NetQuantize At, float Amount, bool bPlayer)
{
	if (HasAuthority()) { return; }
	FRPGPrototypeDamageText Text;
	Text.Location = At; Text.Amount = Amount; Text.bPlayer = bPlayer; Text.StartedAt = GetWorld()->GetTimeSeconds();
	if (DamageTexts.Num() >= 100) { DamageTexts.RemoveAt(0); }
	DamageTexts.Add(Text);
}

void ARPGNetRelay::MulticastImpact_Implementation(EChaosImpactBallType Type, FVector_NetQuantize At, float Scale, bool bTerminal)
{
	if (!HasAuthority()) { ARPGPrototypeVFX::PlayImpact(GetWorld(), Type, At, Scale, bTerminal); }
}

void ARPGNetRelay::MulticastLightning_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End)
{
	if (!HasAuthority()) { ARPGPrototypeVFX::PlayLightning(GetWorld(), Start, End); }
}

void ARPGNetRelay::BindSimaeFx(bool bBind)
{
	if (!bBind) { ChaosImpactSimaeFx::NetHook = nullptr; return; }
	ChaosImpactSimaeFx::NetHook = [](UWorld* World, const ChaosImpactSimaeFx::FNetFx& In)
	{
		auto* Relay = Get(World);
		if (!Relay) { return; }
		FRPGSimaeFxNet Fx;
		Fx.Kind = In.Kind; Fx.Id = In.Id; Fx.A = In.A; Fx.B = In.B.GetSafeNormal(); Fx.F0 = In.F0; Fx.F1 = In.F1; Fx.F2 = In.F2;
		Fx.Count = In.Count; Fx.bFlag = In.bFlag; Fx.Color = In.Color;
		Relay->MulticastSimaeFx(Fx);
	};
}

void ARPGNetRelay::MulticastSimaeFx_Implementation(const FRPGSimaeFxNet& Fx)
{
	if (HasAuthority()) { return; }
	UWorld* World = GetWorld();
	switch (Fx.Kind)
	{
	case 0: GuestWarnings.Add(Fx.Id, AChaosImpactSimaeWarning::SpawnCircle(World, Fx.A, Fx.F0, Fx.F1, Fx.Color)); break;
	case 1: GuestWarnings.Add(Fx.Id, AChaosImpactSimaeWarning::SpawnLane(World, Fx.A, Fx.B, Fx.F0, Fx.F1, Fx.F2, Fx.Color)); break;
	case 2: AChaosImpactSimaeBlast::Play(World, Fx.A, Fx.F0, Fx.F1, Fx.bFlag, Fx.Count); break;
	case 3: AChaosImpactSimaeFlameBurst::Play(World, Fx.A, Fx.F0, Fx.Count, Fx.bFlag); break;
	case 4: if (auto* Warning = GuestWarnings.FindRef(Fx.Id).Get()) { Warning->SetLane(Fx.A, Fx.B, Fx.F0); } break;
	case 5: if (auto* Warning = GuestWarnings.FindRef(Fx.Id).Get()) { Warning->Finish(); } GuestWarnings.Remove(Fx.Id); break;
	default: break;
	}
	// Forget the ones already gone.
	for (auto It = GuestWarnings.CreateIterator(); It; ++It) { if (!It.Value().IsValid()) { It.RemoveCurrent(); } }
}

// ---------------------------------------------------------------- sessions (password, LAN)

URPGOnlineSubsystem* URPGOnlineSubsystem::Get(const UObject* Context)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	return Game ? Game->GetSubsystem<URPGOnlineSubsystem>() : nullptr;
}

void URPGOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &URPGOnlineSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &URPGOnlineSubsystem::HandleTravelFailure);
	}
	MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URPGOnlineSubsystem::HandleMapLoaded);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &URPGOnlineSubsystem::TickRefresh), 0.25f);
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("CIRPGNet="), SmokeRole);
	if (!FParse::Value(FCommandLine::Get(), TEXT("CIRPGNetPass="), SmokePassword)) { SmokePassword = TEXT("netqa"); }
	if (SmokeRole == TEXT("guest")) { RPGHeroes::SetIndexSlotForTests(TEXT("CI_RPG_Heroes_NetQA")); }
#endif
}

void URPGOnlineSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkFailureHandle); GEngine->OnTravelFailure().Remove(TravelFailureHandle); }
	if (IOnlineSessionPtr Sessions = GetSessions()) { Sessions->ClearOnCreateSessionCompleteDelegates(this); Sessions->ClearOnFindSessionsCompleteDelegates(this); Sessions->ClearOnJoinSessionCompleteDelegates(this); }
	Super::Deinitialize();
}

FString URPGOnlineSubsystem::GetSavedPassword() const
{
	FString Saved;
	if (GConfig) { GConfig->GetString(RPGOnlineSection, TEXT("Password"), Saved, GGameUserSettingsIni); }
	return Saved.Left(MaxPasswordLength);
}

void URPGOnlineSubsystem::SavePassword(const FString& InPassword) const
{
	if (!GConfig) { return; }
	GConfig->SetString(RPGOnlineSection, TEXT("Password"), *InPassword.Left(MaxPasswordLength), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

FString URPGOnlineSubsystem::RandomPassword()
{
	static const TCHAR* Words[] = {TEXT("こもれび"), TEXT("ゆきみち"), TEXT("すいしょう"), TEXT("かざぐるま"), TEXT("ほしぞら"), TEXT("あおぞら"), TEXT("いりえ"), TEXT("もりのおく")};
	return FString::Printf(TEXT("%s%d"), Words[FMath::RandRange(0, UE_ARRAY_COUNT(Words) - 1)], FMath::RandRange(10, 99));
}

FString URPGOnlineSubsystem::GetHostName() const
{
	const auto* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UChaosImpactSessionSubsystem>() : nullptr;
	return Sessions ? Sessions->GetPlayerName() : FString(TEXT("ホスト"));
}

IOnlineSessionPtr URPGOnlineSubsystem::GetSessions() const
{
	const UGameInstance* Game = GetGameInstance();
	IOnlineSubsystem* Online = Online::GetSubsystem(Game ? Game->GetWorld() : nullptr);
	return Online ? Online->GetSessionInterface() : nullptr;
}

void URPGOnlineSubsystem::BindDelegates()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (bDelegatesBound || !Sessions) { return; }
	Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &URPGOnlineSubsystem::HandleCreateComplete));
	Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &URPGOnlineSubsystem::HandleFindComplete));
	Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &URPGOnlineSubsystem::HandleJoinComplete));
	bDelegatesBound = true;
}

void URPGOnlineSubsystem::PostNotice(const FString& Text)
{
	Notice = Text; NoticeAt = FPlatformTime::Seconds();
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: %s"), *Text);
}

bool URPGOnlineSubsystem::Host(UWorld* World, const FString& InPassword)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!World || !Sessions || World->GetNetMode() == NM_Client) { PostNotice(TEXT("通信の準備ができていません")); return false; }
	BindDelegates();
	Password = InPassword.TrimStartAndEnd().Left(MaxPasswordLength);
	if (Password.IsEmpty()) { PostNotice(TEXT("合言葉を入れてください")); return false; }
	// Listen first, in this very world (the session then advertises the port actually used).
	if (World->GetNetMode() == NM_Standalone)
	{
		FURL Url = World->URL;
		Url.AddOption(TEXT("listen"));
		if (!World->Listen(Url)) { PostNotice(TEXT("世界を開けませんでした（通信を始められません）")); return false; }
	}
	if (Sessions->GetNamedSession(NAME_GameSession)) { Sessions->DestroySession(NAME_GameSession); }
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true; Settings.bUsesPresence = false; Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true; Settings.bAllowJoinViaPresence = false; Settings.bUseLobbiesIfAvailable = false;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.Set(RPGPassKey, Password, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(RPGHostKey, GetHostName(), EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(RPGCountKey, FMath::Max(1, PlayerCount), EOnlineDataAdvertisementType::ViaOnlineService);
	State = ERPGOnlineState::Hosting;
	SavePassword(Password);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings)) { HandleCreateComplete(NAME_GameSession, false); return false; }
	return true;
}

void URPGOnlineSubsystem::HandleCreateComplete(FName SessionName, bool bSuccess)
{
	if (State != ERPGOnlineState::Hosting) { return; }
	if (!bSuccess) { PostNotice(TEXT("合言葉の世界を作れませんでした")); return; }
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: hosting with password \"%s\""), *Password);
}

void URPGOnlineSubsystem::SetPlayerCount(int32 Count)
{
	PlayerCount = FMath::Clamp(Count, 1, MaxPlayers);
	IOnlineSessionPtr Sessions = GetSessions();
	FOnlineSessionSettings* Current = Sessions ? Sessions->GetSessionSettings(NAME_GameSession) : nullptr;
	if (State != ERPGOnlineState::Hosting || !Current) { return; }
	FOnlineSessionSettings Updated = *Current;
	Updated.Set(RPGCountKey, PlayerCount, EOnlineDataAdvertisementType::ViaOnlineService);
	Sessions->UpdateSession(NAME_GameSession, Updated, true);
}

void URPGOnlineSubsystem::StartSearch(const FString& InPassword)
{
	if (!GetSessions()) { PostNotice(TEXT("通信の準備ができていません")); return; }
	BindDelegates();
	Password = InPassword.TrimStartAndEnd().Left(MaxPasswordLength);
	SavePassword(Password);
	State = ERPGOnlineState::Searching;
	Listings.Reset(); bSearchedOnce = false;
	BeginFind();
}

void URPGOnlineSubsystem::StopSearch()
{
	if (State != ERPGOnlineState::Searching) { return; }
	State = ERPGOnlineState::Offline;
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress) { Sessions->CancelFindSessions(); }
}

void URPGOnlineSubsystem::BeginFind()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || State != ERPGOnlineState::Searching) { return; }
	if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress) { return; }
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true; Search->MaxSearchResults = 64;
	NextFindAt = FPlatformTime::Seconds() + 2.0;
	if (!Sessions->FindSessions(0, Search.ToSharedRef())) { HandleFindComplete(false); }
}

void URPGOnlineSubsystem::HandleFindComplete(bool bSuccess)
{
	if (State != ERPGOnlineState::Searching || !Search.IsValid()) { return; }
	TArray<FRPGWorldListing> Found;
	for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
	{
		FString ResultPassword;
		// VS rooms (and other passwords) are not RPG worlds.
		if (!Result.Session.SessionSettings.Get(RPGPassKey, ResultPassword) || ResultPassword != Password) { continue; }
		FRPGWorldListing& Listing = Found.AddDefaulted_GetRef();
		Result.Session.SessionSettings.Get(RPGHostKey, Listing.Host);
		Result.Session.SessionSettings.Get(RPGCountKey, Listing.Players);
		Listing.PingMs = FMath::Max(0, Result.PingInMs);
		Listing.Result = Result;
	}
	Found.StableSort([](const FRPGWorldListing& A, const FRPGWorldListing& B) { return A.Players < MaxPlayers && B.Players >= MaxPlayers ? true : A.PingMs < B.PingMs; });
	Listings = MoveTemp(Found);
	bSearchedOnce = true;
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: search found %d world(s) with the password"), Listings.Num());
}

bool URPGOnlineSubsystem::TickRefresh(float DeltaSeconds)
{
	// The list refreshes while it is shown (on the core ticker: the title world may be paused).
	if (State == ERPGOnlineState::Searching && FPlatformTime::Seconds() >= NextFindAt) { BeginFind(); }
#if !UE_BUILD_SHIPPING
	if (!SmokeRole.IsEmpty()) { TickSmoke(); }
#endif
	return true;
}

#if !UE_BUILD_SHIPPING
void URPGOnlineSubsystem::TickSmoke()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	auto* P = PC ? Cast<ARPGPrototypeCharacter>(PC->GetPawn()) : nullptr;
	const double Now = FPlatformTime::Seconds();
	if (!World || !P || Now < SmokeAt) { return; }
	const auto Log = [&](const FString& Text) { UE_LOG(LogChaosImpact, Display, TEXT("RPGNET %s: %s"), *SmokeRole, *Text); };
	const auto Shot = [&](const FString& Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RPGPrototypeQA"), Name), true, false); };
	const auto Quit = [&]() { Log(TEXT("done")); FPlatformMisc::RequestExit(false); };
	int32 Heroes = 0, Grunts = 0; ARPGPrototypeCharacter* Other = nullptr; ARPGPrototypeCharacter* Grunt = nullptr; float Best = MAX_flt;
	for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
	{
		if (It->IsGrunt()) { ++Grunts; if (!It->IsEliminated() && !Cast<ARPGPrototypeBoss>(*It) && It->GetName().Contains(TEXT("Simae")) == false) { const float D = FVector::DistSquared2D(It->GetActorLocation(), P->GetActorLocation()); if (D < Best) { Best = D; Grunt = *It; } } }
		else { ++Heroes; if (*It != P) { Other = *It; } }
	}
	if (SmokeRole == TEXT("host"))
	{
		switch (SmokeStep)
		{
		case 0:
			if (!World->GetAuthGameMode()) { return; }
			Log(FString::Printf(TEXT("hosting: %d"), Host(World, SmokePassword))); SmokeStep = 1; SmokeAt = Now + 1; return;
		case 1:
			if (Heroes < 2) { if (SmokeT0 <= 0) { SmokeT0 = Now; } if (Now - SmokeT0 > 240) { Log(TEXT("no guest came")); Quit(); } return; }
			Log(FString::Printf(TEXT("guest joined: netmode %d players %d"), static_cast<int32>(World->GetNetMode()), Heroes));
			SmokeT0 = Now; SmokeStep = 2; SmokeAt = Now + 6; return;
		case 2:
			if (Other) { Log(FString::Printf(TEXT("guest '%s' at %s hp %.0f/%.0f level %d"), *Other->HeroName, *Other->GetActorLocation().ToString(), Other->GetHealth(), Other->GetMaxHealth(), Other->GetRPGLevel())); }
			Shot(TEXT("net-host-guest.png"));
			SmokeStep = 21; SmokeAt = Now + 2; return;
		case 21:
			if (Other)
			{
				int32 Flying = 0; for (TActorIterator<ARPGPrototypeBall> It(World); It; ++It) { if (It->GetInstigator() == Other) { ++Flying; } }
				ARPGPrototypeCharacter* Near = nullptr; float NearBest = MAX_flt;
				for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It) { if (It->IsGrunt() && !It->IsEliminated()) { const float D = FVector::DistSquared2D(It->GetActorLocation(), Other->GetActorLocation()); if (D < NearBest) { NearBest = D; Near = *It; } } }
				Log(FString::Printf(TEXT("watch: guest at %s hp %.0f balls %d flying %d charging %d frozen %d locked %d; nearest enemy %.0fcm hp %.0f"), *Other->GetActorLocation().ToString(), Other->GetHealth(), Other->GetCarriedBallCount(), Flying,
					Other->IsChargingThrow(), Other->IsTrainingMenuFrozen(), Other->IsMatchInputLocked(), FMath::Sqrt(NearBest), Near ? Near->GetHealth() : -1.0f));
			}
			SmokeAt = Now + 2; if (Now - SmokeT0 > 30) { SmokeStep = 3; } return;
		case 3:
			if (Other) { Log(FString::Printf(TEXT("after the fight: guest at %s hp %.0f, grunts %d"), *Other->GetActorLocation().ToString(), Other->GetHealth(), Grunts)); }
			// Down the guest and help it up (the host is the helper).
			if (Other)
			{
				P->SetPrototypeAutomatedInput(true);
				P->SetActorLocation(Other->GetActorLocation() + FVector(120, 0, 30), false, nullptr, ETeleportType::TeleportPhysics);
				Other->TakeDamage(100000, FDamageEvent(), Grunt ? Grunt->GetController() : nullptr, Grunt);
				Log(FString::Printf(TEXT("guest downed: %d"), Other->IsDowned()));
				P->bReviveHeld = true;
			}
			Shot(TEXT("net-host-downed.png"));
			SmokeStep = 4; SmokeAt = Now + 4.5; return;
		case 4:
			P->bReviveHeld = false;
			if (Other) { Log(FString::Printf(TEXT("guest revived: %d hp %.0f"), !Other->IsDowned(), Other->GetHealth())); }
			SmokeStep = 5; SmokeAt = Now + 10; return;
		case 5:
			Log(FString::Printf(TEXT("players now %d"), Heroes)); Quit(); SmokeStep = 6; return;
		default: return;
		}
	}
	if (SmokeRole == TEXT("guest"))
	{
		switch (SmokeStep)
		{
		case 0:
		{
			if (World->GetNetMode() == NM_Client) { SmokeStep = 2; return; }
			// First a look round the camp on our own (the development build makes its shaders now, not mid-fight).
			if (const auto* Adventure = ARPGAdventureWorld::Find(World))
			{
				for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It)
				{
					if (!It->bEncounterGroup || Adventure->IsInColony(It->GetActorLocation()) || !ARPGSnowField::RegionAt(World, It->GetActorLocation()).IsEmpty()) { continue; }
					P->SetActorLocation(It->GetActorLocation() + FVector(0, 0, 150), false, nullptr, ETeleportType::TeleportPhysics); break;
				}
			}
			Log(TEXT("warming up")); SmokeStep = 10; SmokeAt = Now + 45; return;
		}
		case 10:
			StartSearch(SmokePassword); Log(TEXT("searching")); SmokeStep = 1; SmokeAt = Now + 1; SmokeT0 = Now; return;
		case 1:
			if (Listings.IsEmpty()) { if (Now - SmokeT0 > 90) { Log(TEXT("nothing found")); Quit(); } return; }
			Log(FString::Printf(TEXT("found '%s' (%d players), joining"), *Listings[0].Host, Listings[0].Players)); Join(0); SmokeStep = 2; SmokeAt = Now + 2; return;
		case 2:
			if (World->GetNetMode() != NM_Client || !P->GetTitleWidget()) { return; }
			Log(FString::Printf(TEXT("in the host's world: heroes %d grunts %d, picking a hero"), Heroes, Grunts));
			RPGHeroes::Store({});
			P->GetTitleWidget()->CreateHeroForTest(TEXT("ゲスト"), 2);
			SmokeStep = 3; SmokeAt = Now + 3; return;
		case 3:
			Log(FString::Printf(TEXT("playing as '%s' level %d hp %.0f/%.0f, host seen %d (%s)"), *P->HeroName, P->GetRPGLevel(), P->GetHealth(), P->GetMaxHealth(), Other != nullptr, Other ? *Other->GetActorLocation().ToString() : TEXT("-")));
			P->NoticeUntil = 0; Shot(TEXT("net-guest-arrive.png"));
			// Off to an enemy camp out in the field (the host makes its enemies when someone comes near).
			if (const auto* Adventure = ARPGAdventureWorld::Find(World))
			{
				float Nearest = MAX_flt; FVector Camp = FVector::ZeroVector;
				for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It)
				{
					if (!It->bEncounterGroup || Adventure->IsInColony(It->GetActorLocation()) || !ARPGSnowField::RegionAt(World, It->GetActorLocation()).IsEmpty()) { continue; }
					const float D = FVector::DistSquared2D(It->GetActorLocation(), P->GetActorLocation());
					if (D < Nearest) { Nearest = D; Camp = It->GetActorLocation(); }
				}
				if (Nearest < MAX_flt) { P->TeleportHero(Camp + FVector(0, 0, 150), P->GetActorRotation()); Log(FString::Printf(TEXT("to the camp at %s"), *Camp.ToString())); }
			}
			SmokeT0 = Now; SmokeStep = 4; SmokeAt = Now + 2.5; Money = P->GetRubies(); return;
		case 4:
		{
			// Fight the nearest enemy for a while.
			if (Grunt)
			{
				P->SetPrototypeAutomatedInput(true);
				// Stand where we are and throw at the nearest one (they come to us).
				const FVector To = Grunt->GetActorLocation() - P->GetActorLocation();
				P->SetAIAimDirection(To.GetSafeNormal2D());
				// Like the enemies: charge a moment, then let go.
				if (P->IsChargingThrow()) { P->EndThrowInput(); } else { P->BeginThrowInput(); }
			}
			if (Now - SmokeT0 > 6 && Now - SmokeT0 < 6.5) { Shot(TEXT("net-guest-fight.png")); }
			if (++SmokeTicks % 5 == 0) { Log(FString::Printf(TEXT("watch: me at %s hp %.0f balls %d charging %d target %s"), *P->GetActorLocation().ToString(), P->GetHealth(), P->GetCarriedBallCount(), P->IsChargingThrow(), Grunt ? *Grunt->GetActorLocation().ToString() : TEXT("-"))); }
			SmokeAt = Now + 0.4;
			if (Now - SmokeT0 > 24 || (P->GetXP() > 0 && Now - SmokeT0 > 8))
			{
				Log(FString::Printf(TEXT("after the fight: level %d xp %d rubies %d (was %d), hp %.0f, quest kills %d"), P->GetRPGLevel(), P->GetXP(), P->GetRubies(), Money, P->GetHealth(), P->Quests.IsValidIndex(0) ? P->Quests[0].Count : -1));
				int32 Loot = 0; for (TActorIterator<ARPGPrototypeLoot> It(World); It; ++It) { ++Loot; }
				Log(FString::Printf(TEXT("own drops on the ground: %d"), Loot));
				P->NoticeUntil = 0; Log(FString::Printf(TEXT("multiplayer menu: %d"), P->OpenMembers()));
				SmokeStep = 45; SmokeAt = Now + 1.0;
			}
			return;
		}
		case 45:
			// The menu opens once the throw in hand is done (and not while down).
			if (!P->IsMembersOpen())
			{
				P->CancelChargingThrow();
				if (P->OpenMembers()) { SmokeAt = Now + 0.8; return; }
				if (Now - SmokeT0 < 34) { SmokeAt = Now + 0.5; return; }
				SmokeStep = 5; return;
			}
			Shot(TEXT("net-guest-members.png")); SmokeStep = 46; SmokeAt = Now + 0.5; return;
		case 46: P->SetQuestMenuOpen(false); SmokeStep = 5; return;
		case 5:
			if (P->IsDowned() && !bSmokeShotDowned) { bSmokeShotDowned = true; Log(FString::Printf(TEXT("I am down: %.0f s left"), P->GetDownedLeft())); P->NoticeUntil = 0; Shot(TEXT("net-guest-downed.png")); }
			if (bSmokeShotDowned && !P->IsDowned()) { Log(FString::Printf(TEXT("helped up: hp %.0f"), P->GetHealth())); SmokeStep = 6; SmokeAt = Now + 1; return; }
			if (Now - SmokeT0 > 60) { Log(TEXT("never downed")); SmokeStep = 6; }
			return;
		case 6:
			Shot(TEXT("net-guest-up.png")); SmokeStep = 7; SmokeAt = Now + 3; return;
		case 7:
			Quit(); SmokeStep = 8; return;
		default: return;
		}
	}
}
#endif

bool URPGOnlineSubsystem::Join(int32 Index)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions || State != ERPGOnlineState::Searching || !Listings.IsValidIndex(Index)) { return false; }
	if (Listings[Index].Players >= MaxPlayers) { PostNotice(TEXT("この世界は4人そろっています")); return false; }
	if (Search.IsValid() && Search->SearchState == EOnlineAsyncTaskState::InProgress) { Sessions->CancelFindSessions(); }
	if (Sessions->GetNamedSession(NAME_GameSession)) { Sessions->DestroySession(NAME_GameSession); }
	State = ERPGOnlineState::Joining;
	if (!Sessions->JoinSession(0, NAME_GameSession, Listings[Index].Result))
	{ HandleJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError); return false; }
	return true;
}

void URPGOnlineSubsystem::HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (State != ERPGOnlineState::Joining) { return; }
	IOnlineSessionPtr Sessions = GetSessions();
	FString Connect;
	APlayerController* Controller = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions || !Controller || !Sessions->GetResolvedConnectString(NAME_GameSession, Connect))
	{
		PostNotice(TEXT("世界に入れませんでした"));
		State = ERPGOnlineState::Searching; BeginFind();
		return;
	}
	State = ERPGOnlineState::Guest;
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: joining %s"), *Connect);
	// The guest picks its hero on arrival (CIRPGHeroes with no hero given).
	Controller->ClientTravel(Connect + TEXT("?CIRPGPrototype=1?CIOpenWorld=1?CIRPGHeroes=1?CIRPGGuest=1?CILocalPlayers=1?CIKeyboardPlayer=0"), TRAVEL_Absolute);
}

void URPGOnlineSubsystem::Leave()
{
	StopSearch();
	State = ERPGOnlineState::Offline;
	if (IOnlineSessionPtr Sessions = GetSessions(); Sessions && Sessions->GetNamedSession(NAME_GameSession)) { Sessions->DestroySession(NAME_GameSession); }
}

void URPGOnlineSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Type, const FString& Error) { LostConnection(Error); }
void URPGOnlineSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error) { LostConnection(Error); }

void URPGOnlineSubsystem::LostConnection(const FString& Error)
{
	if (State != ERPGOnlineState::Guest && State != ERPGOnlineState::Joining) { return; }
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: connection lost (%s)"), *Error);
	PostNotice(Error.Contains(TEXT("CIRPGFULL")) ? TEXT("この世界は4人そろっています") : TEXT("ホストとの接続が切れました。タイトルに戻りました"));
	Leave();
	bReturnToTitle = true;
}

void URPGOnlineSubsystem::HandleMapLoaded(UWorld* World)
{
	if (!bReturnToTitle || !World || World->GetNetMode() == NM_Client) { return; }
	// The engine has opened its default map after the lost connection: on to the RPG title.
	if (World->URL.HasOption(TEXT("CIRPGPrototype"))) { bReturnToTitle = false; return; }
	bReturnToTitle = false;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		if (auto* PC = Cast<AChaosImpactPlayerController>(GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)) { PC->OpenExplorationMap(TEXT("?CIRPGTitle=1")); }
		return false;
	}), 0.5f);
}

// ---------------------------------------------------------------- heroes over the network

void ARPGPrototypeCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ARPGPrototypeCharacter, bGrunt, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGPrototypeCharacter, EncounterRole, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ARPGPrototypeCharacter, RPGLevel, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ARPGPrototypeCharacter, RepMaxHealth, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ARPGPrototypeCharacter, HeroName, COND_SkipOwner);
	DOREPLIFETIME(ARPGPrototypeCharacter, bDowned);
	DOREPLIFETIME(ARPGPrototypeCharacter, DownedEndsAt);
	DOREPLIFETIME(ARPGPrototypeCharacter, ReviveProgress);
}

bool ARPGPrototypeCharacter::IsRemoteHero() const { return !bGrunt && HasAuthority() && IsPlayerControlled() && !IsLocallyControlled(); }
bool ARPGPrototypeCharacter::IsNetClientHero() const { return !bGrunt && !HasAuthority() && IsLocallyControlled(); }

void ARPGPrototypeCharacter::ShowNotice(const FString& Text, float Seconds)
{
	if (IsRemoteHero()) { ClientShowNotice(Text, Seconds); return; }
	Notice = Text; NoticeUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : 0;
}

void ARPGPrototypeCharacter::ClientShowNotice_Implementation(const FString& Text, float Seconds)
{ Notice = Text; NoticeUntil = GetWorld()->GetTimeSeconds() + Seconds; }

void ARPGPrototypeCharacter::TickNetSync()
{
	if (IsInTitle() || !GetWorld()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	// What this hero fights with: sent whenever it changes (and now and then, to be safe).
	uint32 Signature = HashCombine(GetTypeHash(SelectedAttack), GetTypeHash(RPGLevel));
	Signature = HashCombine(Signature, GetTypeHash(HeroName));
	for (const TArray<FRPGPrototypeItem>* List : {&Attacks, &Badges, &Gears})
	{
		for (const auto& Item : *List)
		{
			Signature = HashCombine(Signature, GetTypeHash(Item.Id));
			Signature = HashCombine(Signature, GetTypeHash(Item.Enhancement + Item.Level * 100 + Item.Tunes.Num() * 10000));
			for (const auto& Tune : Item.Tunes) { Signature = HashCombine(Signature, GetTypeHash(Tune.Rank)); }
		}
	}
	if (Signature != SentHeroSignature || Now >= NextHeroSyncAt)
	{
		FRPGNetHero Hero;
		Hero.Attacks = Attacks; Hero.Badges = Badges; Hero.Gears = Gears;
		Hero.SelectedAttack = SelectedAttack; Hero.Level = RPGLevel; Hero.Name = HeroName;
		if (const auto* State = GetPlayerState<AChaosImpactPlayerState>()) { Hero.Character = State->CharacterIndex; Hero.Colour = State->ColourChoice; }
		if (HeroCharacter >= 0) { Hero.Character = HeroCharacter; Hero.Colour = HeroColour; }
		Hero.StartHP = bHeroSynced ? -1.0f : Health;
		ServerSyncHero(Hero);
		bHeroSynced = true; SentHeroSignature = Signature; NextHeroSyncAt = Now + 10;
	}
	if (bReviveHeld != bSentReviveHeld) { ServerSetReviveHeld(bReviveHeld); bSentReviveHeld = bReviveHeld; }
}

void ARPGPrototypeCharacter::ServerSyncHero_Implementation(const FRPGNetHero& Hero)
{
	if (bGrunt) { return; }
	// Only what fits the slots: three balls, three badges, four gears.
	const auto Take = [](TArray<FRPGPrototypeItem>& To, const TArray<FRPGPrototypeItem>& From, int32 Count)
	{ To.SetNum(Count); for (int32 Index = 0; Index < Count; ++Index) { To[Index] = From.IsValidIndex(Index) ? From[Index] : FRPGPrototypeItem(); } };
	Take(Attacks, Hero.Attacks, 3); Take(Badges, Hero.Badges, 3); Take(Gears, Hero.Gears, 4);
	if (!Attacks[0].IsValid()) { Attacks[0] = FRPGPrototypeItem::Make(EChaosImpactBallType::Normal, RPGLevel); }
	SelectedAttack = Attacks.IsValidIndex(Hero.SelectedAttack) && Attacks[Hero.SelectedAttack].IsValid() ? Hero.SelectedAttack : 0;
	const int32 OldLevel = RPGLevel;
	RPGLevel = FMath::Clamp(Hero.Level, 1, RPGTuning::MaxLevel);
	HeroName = Hero.Name.Left(10);
	RebuildEquipment();
	if (Hero.StartHP > 0) { Health = FMath::Clamp(Hero.StartHP, 1.0f, MaxHealth); }
	else if (RPGLevel > OldLevel) { Health = MaxHealth; }
	if (auto* State = GetPlayerState<AChaosImpactPlayerState>())
	{
		State->CharacterIndex = ChaosImpactRoster::ClampIndex(Hero.Character);
		State->ColourChoice = FMath::Clamp(Hero.Colour, 0, ChaosImpactRoster::ColourCount - 1);
		if (!HeroName.IsEmpty()) { State->SetPlayerName(HeroName); }
	}
	EnsureReadyBall();
}

void ARPGPrototypeCharacter::ServerSelectAttack_Implementation(int32 Slot) { SelectAttack(Slot); }
void ARPGPrototypeCharacter::ServerSetReviveHeld_Implementation(bool bHeld) { bReviveHeld = bHeld; }

void ARPGPrototypeCharacter::ServerRestAt_Implementation(FVector_NetQuantize Location, FRotator Rotation, bool bHeal)
{
	SetRespawnPoint(Location, Rotation);
	if (bHeal) { RestoreHP(MaxHealth); Stamina = MaxStamina; }
}

void ARPGPrototypeCharacter::ServerTeleportTo_Implementation(FVector_NetQuantize Location, FRotator Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	SafeGround.Reset(); GroundedSince = -1;
	// Moves still on their way carry the old place: the host keeps this one for a moment (and tells the guest).
	NotifyServerTeleport();
}

void ARPGPrototypeCharacter::TeleportHero(const FVector& Location, const FRotator& Rotation)
{
	GetCharacterMovement()->StopMovementImmediately();
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	SafeGround.Reset(); GroundedSince = -1;
	if (IsNetClientHero()) { ServerTeleportTo(Location, Rotation); }
}

void ARPGPrototypeCharacter::ClientKillCredit_Implementation(const FRPGKillCredit& Credit) { ReceiveKillCredit(Credit); }
void ARPGPrototypeCharacter::ClientApplyKnockback_Implementation(FVector_NetQuantizeNormal Direction, float Distance) { ApplyRPGKnockback(Direction, Distance); }
void ARPGPrototypeCharacter::ClientApplyStatus_Implementation(FName Key, float Value, float Seconds)
{
	if (Key == TEXT("Stun")) { ApplyRPGStun(Seconds); return; }
	ApplyRPGStatus(Key, Value, Seconds);
}
void ARPGPrototypeCharacter::ClientDropAllBalls_Implementation() { LoseCarriedBalls(); }

void ARPGPrototypeCharacter::ApplyKillEffects(const FRPGKillCredit& Credit)
{
	if (bGrunt || IsEliminated()) { return; }
	LastKillAt = GetWorld()->GetTimeSeconds(); bKillDashReady = true;
	RestoreHP(MaxHealth * BadgeMod(TEXT("KillHeal")) * .01f);
	Shield = FMath::Min(MaxHealth * .2f, Shield + MaxHealth * BadgeMod(TEXT("LifeShield")) * .01f);
	if (Credit.KillRefund > 0) { ReduceSpecialCooldowns(Credit.KillRefund); }
}

void ARPGPrototypeCharacter::ReceiveKillCredit(const FRPGKillCredit& Credit)
{
	if (bGrunt || !GetWorld()) { return; }
	if (!HasAuthority())
	{
		// The guest's own copy: the same kill-time bonuses its screen and its throws use.
		LastKillAt = GetWorld()->GetTimeSeconds(); bKillDashReady = true;
		if (Credit.KillRefund > 0) { ReduceSpecialCooldowns(Credit.KillRefund); }
	}
	int32 Gain = FMath::RandRange(RPGTuning::KillRubyMin, RPGTuning::KillRubyMax) + RPGLevel;
	Gain = FMath::RoundToInt(Gain * (1 + BadgeMod(TEXT("Ruby")) * .01f));
	if (FMath::FRand() * 100 < BadgeMod(TEXT("TechLucky"))) { Gain *= 2; }
	AddRubies(Gain);
	GainRPGExperience(RPGTuning::KillXP);
	QuestEvent(ERPGQuestEvent::Kill, Credit.At, Credit.Detail);
	if (Credit.bBoss) { AddRubies(Credit.Level * RPGTuning::BossRubyPerLevel); }
	if (!Credit.bBoss && FMath::FRand() >= FMath::Clamp(Credit.DropChance, 0.0f, 1.0f)) { return; }
	// A drop of its own (rarity and kind odds: RPGTuning, cumulative from the rarest).
	using namespace RPGTuning;
	const bool bBoss = Credit.bBoss;
	const float Roll = FMath::FRand();
	const auto Rarity = bBoss ? (Roll < BossLegendChance ? ERPGPrototypeRarity::Legend : Roll < BossLegendChance + BossUniqueChance ? ERPGPrototypeRarity::Unique : ERPGPrototypeRarity::Rare)
		: Roll < EnemyLegendChance ? ERPGPrototypeRarity::Legend : Roll < EnemyLegendChance + EnemyUniqueChance ? ERPGPrototypeRarity::Unique
		: Roll < EnemyLegendChance + EnemyUniqueChance + EnemyRareChance ? ERPGPrototypeRarity::Rare : ERPGPrototypeRarity::Normal;
	const int32 Level = bBoss ? Credit.Level : FMath::Max(1, RPGLevel + FMath::RandRange(-1, 1));
	const float Category = FMath::FRand();
	const bool bBadge = !bBoss && Category < DropBadgeChance;
	const bool bGear = !bBoss && Category >= DropBadgeChance && Category < DropBadgeChance + DropGearChance;
	const auto& Types = RPGEquipment::BallRoster();
	const auto Item = bGear ? FRPGPrototypeItem::MakeGear(static_cast<ERPGItemKind>(FMath::RandRange(5, 8)), Level, Rarity)
		: bBadge ? FRPGPrototypeItem::MakeBadge(static_cast<ERPGItemKind>(FMath::RandRange(1, 3)), Level, Rarity)
		: FRPGPrototypeItem::Make(Types[FMath::RandRange(bBoss ? 1 : 0, Types.Num() - 1)], Level, Rarity);
	const auto* Mode = GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();
	ARPGPrototypeGameMode::SpawnLootIn(GetWorld(), Credit.At, Item, this, Mode ? Mode->LootLifetimeSeconds : 60.0f);
}

// ---------------------------------------------------------------- the multiplayer menu

bool ARPGPrototypeCharacter::OpenMembers()
{
	if (bGrunt || IsEliminated() || bDowned) { return false; }
	return OpenQuestPanel(MembersPanel);
}

bool ARPGPrototypeCharacter::HandleMembersKey(FKey Key)
{
	if (!IsMembersOpen()) { return false; }
	if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::O || Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Right
		|| Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_DPad_Left) { SetQuestMenuOpen(false); }
	return true;
}

// ---------------------------------------------------------------- the host's door

void ARPGPrototypeGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty() && GetNumPlayers() >= URPGOnlineSubsystem::MaxPlayers) { ErrorMessage = TEXT("CIRPGFULL"); }
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: login check %d player(s): %s"), GetNumPlayers(), ErrorMessage.IsEmpty() ? TEXT("ok") : *ErrorMessage);
}

void ARPGPrototypeGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Who is the host's (for the multiplayer menu), and the order everyone came in.
	if (auto* Member = NewPlayer ? NewPlayer->GetPlayerState<AChaosImpactPlayerState>() : nullptr)
	{
		Member->bHostMachine = NewPlayer->IsLocalController();
		int32 Order = 0; for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) { ++Order; }
		Member->JoinOrder = Order - 1;
	}
	Super::PostLogin(NewPlayer);
	if (!NewPlayer || NewPlayer->IsLocalController()) { return; }
	// A guest starts in the colony, beside the others' start points.
	if (auto* Hero = Cast<ARPGPrototypeCharacter>(NewPlayer->GetPawn()))
	{
		int32 Index = 0; for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) { ++Index; }
		const auto* Adventure = ARPGAdventureWorld::Find(GetWorld());
		const FVector At = Adventure ? Adventure->PlayerSpawn(FMath::Clamp(Index - 1, 0, 3)) : Hero->GetActorLocation();
		Hero->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Hero->SetRespawnPoint(At, FRotator::ZeroRotator);
	}
	if (auto* Online = URPGOnlineSubsystem::Get(this)) { Online->SetPlayerCount(GetNumPlayers()); }
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{ if (auto* P = Cast<ARPGPrototypeCharacter>(It->Get() ? It->Get()->GetPawn() : nullptr); P && It->Get()->IsLocalController()) { P->ShowNotice(TEXT("仲間が世界に入ってきた"), 3); } }
	UE_LOG(LogChaosImpact, Log, TEXT("RPG online: a guest joined (%d player(s))"), GetNumPlayers());
}

void ARPGPrototypeGameMode::Logout(AController* Exiting)
{
	const bool bRemote = Exiting && Cast<APlayerController>(Exiting) && !Cast<APlayerController>(Exiting)->IsLocalController();
	Super::Logout(Exiting);
	if (!bRemote || !GetWorld()) { return; }
	if (auto* Online = URPGOnlineSubsystem::Get(this)) { Online->SetPlayerCount(FMath::Max(1, GetNumPlayers() - 1)); }
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{ if (auto* P = Cast<ARPGPrototypeCharacter>(It->Get() ? It->Get()->GetPawn() : nullptr); P && It->Get()->IsLocalController()) { P->ShowNotice(TEXT("仲間が世界から出ていった"), 3); } }
}
