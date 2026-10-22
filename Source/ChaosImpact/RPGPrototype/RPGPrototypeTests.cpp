#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototypeTestLoadout.h"
#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactSoloRoom.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactMenuWidget.h"
#include "ChaosImpactLoadingScreen.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "RPGAdventure.h"

namespace
{
	class FRPGPrototypeCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRPGPrototypeCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			const double RealNow = FPlatformTime::Seconds();
			if (RealNow-Started > 120) { Test->AddError(FString::Printf(TEXT("RPG prototype timed out at stage %d"),Stage)); return true; }
			UWorld* World = nullptr;
			for (const auto& Context : GEngine->GetWorldContexts())
			{ if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; } }
			if (!World) { return false; }
			auto* PC = Cast<AChaosImpactPlayerController>(World->GetFirstPlayerController());
			if (!PC || !PC->GetPawn()) { return false; }
			if (Stage==17)
			{
				if(PC->GetCurrentScreen()!=EChaosImpactScreen::Title || !PC->GetMenuWidget()) { return false; }
				if(auto* Loading=UChaosImpactLoadingSubsystem::Get(PC);Loading && Loading->IsShowing()) { return false; }
				PC->GetMenuWidget()->SetKeyboardFocus();
				for(FKey Key:{EKeys::Up,EKeys::Up,EKeys::Down,EKeys::Down,EKeys::Left,EKeys::Right,EKeys::Left,EKeys::Right,EKeys::B,EKeys::A})
				{
					FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
					FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
				}
				Stage=18; NextAt=0; return false;
			}
			if (Stage==14)
			{
				Test->TestTrue(TEXT("Main pause still pauses RPG world"),World->IsPaused());
				PC->ResumeGameplay(); Stage=15; NextAt=World->GetTimeSeconds()+0.15; return false;
			}
			if (Stage >= 2 && World->GetTimeSeconds() < NextAt) { return false; }
			if (Stage == 0)
			{
				// The same entry function used by the title's hidden command.
				// Combat regression stays on the preserved VS copy; the actual hidden entry is tested below.
				UGameplayStatics::OpenLevel(PC,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
				Stage = 1; NextAt=World->GetTimeSeconds()+1; return false;
			}
			auto* Mode = World->GetAuthGameMode<ARPGPrototypeGameMode>();
			auto* Player = Cast<ARPGPrototypeCharacter>(PC->GetPawn());
			if (!Mode || !Player || !PC->IsGameplayActive() || (Stage==1 && !Player->GetCharacterMovement()->IsMovingOnGround())) { return false; }
			const auto Shot = [](const TCHAR* Name)
			{ FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
			switch (Stage)
			{
			case 1:
			{
				Test->TestTrue(TEXT("Dedicated RPG GameMode"), Mode != nullptr);
				Test->TestEqual(TEXT("Player HP is RPG HP, not three hits"), Player->GetMaxHealth(), 100.0f);
				Test->TestEqual(TEXT("Normal attack always available"), Player->GetAttack(0).Type, EChaosImpactBallType::Normal);
				Test->TestFalse(TEXT("Special balls are not granted on startup"), Player->GetAttack(1).IsValid() || Player->GetAttack(2).IsValid());
				RPGPrototypeTest::SupplySpecials(Player);
				int32 Pads=0,Rooms=0,SpawnPoints=0;
				for (TActorIterator<AChaosImpactBallSpawner> It(World); It; ++It) { ++Pads; }
				for (TActorIterator<AChaosImpactSoloRoom> It(World); It; ++It) { ++Rooms; }
				for (TActorIterator<ARPGPrototypeSpawnPoint> It(World); It; ++It) { It->bEnabled=false; ++SpawnPoints; }
				Test->TestEqual(TEXT("Old ball spawners removed only from RPG copy"), Pads, 0);
				Test->TestEqual(TEXT("Old boss room removed"), Rooms, 0);
				Test->TestEqual(TEXT("Editable enemy spawn points"), SpawnPoints, 7);
				for (TActorIterator<ARPGPrototypeCharacter> It(World); It; ++It)
				{
					if (It->IsGrunt()) { It->SetTrainingMenuFrozen(true); if(!Cast<ARPGPrototypeBoss>(*It)) { Enemies.Add(*It); } }
				}
				if (!Test->TestTrue(TEXT("Weak grunts spawned"), Enemies.Num() >= 2)) { return true; }
				Player->SetPrototypeAutomatedInput(true);
				Test->TestNull(TEXT("No guaranteed starter loot"),Player->FindNearbyLoot());
				Mode->SpawnLoot(Player->GetActorLocation()+FVector(100,0,-45),
					FRPGPrototypeItem::Make(EChaosImpactBallType::Thunder,1,ERPGPrototypeRarity::Rare));
				Test->TestEqual(TEXT("Walking near loot does not auto-collect"), Player->GetBag().Num(), 3);
				Test->TestNotNull(TEXT("Starter drop is within manual pickup range"), Player->FindNearbyLoot());
				Test->TestTrue(TEXT("Manual pickup adds a ball to the bag"), Player->PickupNearbyLoot());
				Test->TestEqual(TEXT("Picked-up ball stored"), Player->GetBag().Num(), 4);
				Test->TestFalse(TEXT("Collected drop cannot be collected twice"), Player->PickupNearbyLoot());
				Test->TestFalse(TEXT("Cannot equip while bag is closed"), Player->EquipBagItem(3,1));
				Player->SetBagOpen(true);
				Test->TestTrue(TEXT("Bag stops only this player's actions"), Player->IsTrainingMenuFrozen());
				Test->TestTrue(TEXT("Equip picked-up thunder in special slot 1"), Player->EquipBagItem(3,1));
				Test->TestEqual(TEXT("Special slot 2 stays independent"), Player->GetAttack(2).Type,EChaosImpactBallType::Ice);
				Test->TestFalse(TEXT("Same physical item cannot fill both slots"), Player->EquipBagItem(3,2));
				Player->BagSelection=3; Player->NoticeUntil=0;
				Shot(TEXT("bag.png"));
				WorldTime = World->GetTimeSeconds();
				Stage=2; NextAt=World->GetTimeSeconds()+0.5; return false;
			}
			case 2:
			{
				Test->TestTrue(TEXT("World time keeps advancing in the bag"), World->GetTimeSeconds()>WorldTime+0.2);
				Player->SetBagOpen(false);
				Player->SelectAttack(0);
				Target=Enemies[0];
				Target->SetActorLocation(Player->GetActorLocation()+FVector(380,0,0),false,nullptr,ETeleportType::TeleportPhysics);
				BeforeHP=Target->GetHealth();
				Player->SetAIAimDirection(FVector::ForwardVector);
				Player->BeginThrowInput();
				Test->TestTrue(TEXT("Normal attack charges"),Player->IsChargingThrow());
				Stage=3; NextAt=World->GetTimeSeconds()+0.75; return false;
			}
			case 3:
				Player->EndThrowInput();
				Test->TestTrue(TEXT("Throw uses animated release"),Player->IsThrowReleasePending());
				Stage=4; NextAt=World->GetTimeSeconds()+0.32; return false;
			case 4:
				Test->TestTrue(TEXT("Normal ball has its short cooldown"),Player->GetCooldownLeft(0)>0);
				Test->TestEqual(TEXT("Other special cooldowns unaffected"),Player->GetCooldownLeft(1),0.0f);
				for (TActorIterator<ARPGPrototypeBall> It(World); It; ++It)
				{ if (It->WasThrownBy(Player)) { PlayerShot=*It; break; } }
				Test->TestTrue(TEXT("Physical projectile uses prototype class"),PlayerShot.IsValid());
				Stage=5; NextAt=World->GetTimeSeconds()+0.6; return false;
			case 5:
			{
				Test->TestTrue(TEXT("Real thrown ball damages the grunt"),Target.IsValid() && Target->GetHealth()<BeforeHP);
				Test->TestTrue(TEXT("A hit emits damage text data"),Mode->DamageTexts.Num()>0);
				Test->TestTrue(TEXT("Enemy health bar has a recent hit timestamp"),Target->GetLastDamagedAt()>0);
				Test->TestEqual(TEXT("Normal ball returns without pickup"),Player->GetCarriedBallCount(),1);
				if (PlayerShot.IsValid())
				{
					const float HP=Player->GetHealth();
					Test->TestEqual(TEXT("Self-thrown ball deals no damage"),Player->TakeDamage(18,FDamageEvent(),PC,PlayerShot.Get()),0.0f);
					Test->TestEqual(TEXT("Self hit leaves HP intact"),Player->GetHealth(),HP);
					Test->TestFalse(TEXT("Summoned projectiles cannot become loot"),PlayerShot->IsPickupAvailable());
					Test->TestFalse(TEXT("Summoned projectiles cannot be collected"),PlayerShot->ConsumePickup(Player));
				}
				Shot(TEXT("battle.png"));
				Player->SelectAttack(1); Player->BeginThrowInput();
				Stage=6; NextAt=World->GetTimeSeconds()+0.35; return false;
			}
			case 6:
				Player->EndThrowInput(); Stage=7; NextAt=World->GetTimeSeconds()+0.35; return false;
			case 7:
			{
				Test->TestTrue(TEXT("Special ball cooldown starts after release"),Player->GetCooldownLeft(1)>4);
				Test->TestTrue(TEXT("Normal ball can be selected during special cooldown"),Player->SelectAttack(0));
				Test->TestEqual(TEXT("Normal rearmed independently"),Player->GetCarriedBallCount(),1);
				Test->TestEqual(TEXT("Second special still ready"),Player->GetCooldownLeft(2),0.0f);
				Player->SelectAttack(2); Player->BeginThrowInput();
				Stage=8; NextAt=World->GetTimeSeconds()+0.35; return false;
			}
			case 8:
				Player->EndThrowInput(); Stage=9; NextAt=World->GetTimeSeconds()+0.35; return false;
			case 9:
			{
				Test->TestTrue(TEXT("Both equipped specials retain separate cooldowns"),Player->GetCooldownLeft(1)>0 && Player->GetCooldownLeft(2)>0);
				Player->SetBagOpen(true);
				const float CT=Player->GetCooldownLeft(1);
				Test->TestTrue(TEXT("Replace special 1 with initial fire"),Player->EquipBagItem(1,1));
				Test->TestTrue(TEXT("Equipment change does not reset a slot cooldown"),Player->GetCooldownLeft(1)>=CT-0.1f);
				Test->TestTrue(TEXT("Move thunder to other slot"),Player->EquipBagItem(3,2));
				Test->TestTrue(TEXT("The item's cooldown follows it to the other slot"),Player->GetCooldownLeft(2)>=CT-0.1f);
				Player->SetBagOpen(false); Player->SelectAttack(0);
				// A direct kill isolates guaranteed first-drop and XP logic from flight randomness.
				ARPGPrototypeCharacter* Victim=Enemies[1].Get();
				if (auto* Spawn=Victim->SpawnPoint.Get()) { Spawn->DropChance=1; }
				auto* KillBall=World->SpawnActor<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),Player->GetActorLocation()+FVector(0,0,1500),FRotator::ZeroRotator);
				KillBall->SetInstigator(Player); KillBall->Launch(FVector::UpVector,1);
				const int32 BeforeLoot=LootCount(World), BeforeXP=Player->GetXP();
				Victim->TakeDamage(1000,FDamageEvent(),PC,KillBall);
				Test->TestTrue(TEXT("Grunt is knocked out"),Victim->IsEliminated());
				Test->TestEqual(TEXT("A 100-percent drop spawn guarantees loot"),LootCount(World),BeforeLoot+1);
				Test->TestEqual(TEXT("Defeat grants RPG XP"),Player->GetXP(),BeforeXP+1);
				KillBall->Destroy();
				ARPGPrototypeCharacter* Fighter=Enemies.Last().Get();
				Fighter->SetActorLocation(Player->GetActorLocation()+FVector(-240,0,0),false,nullptr,ETeleportType::TeleportPhysics);
				Fighter->SetTrainingMenuFrozen(false);
				FighterHP=Fighter->GetHealth(); PlayerHP=Player->GetHealth();
				Stage=10; NextAt=World->GetTimeSeconds()+6; return false;
			}
			case 10:
			{
				Test->TestTrue(TEXT("Weak AI can throw and hit the player"),Player->GetHealth()<PlayerHP);
				Test->TestTrue(TEXT("Weak AI does modest RPG damage, not instant KO"),Player->GetHealth()>0 && PlayerHP-Player->GetHealth()<=32);
				Test->TestEqual(TEXT("Enemy does not attack itself"),Enemies.Last()->GetHealth(),FighterHP);
				Test->TestTrue(TEXT("Old damage text expires"),!Mode->DamageTexts.ContainsByPredicate([&](const FRPGPrototypeDamageText& T){return World->GetTimeSeconds()-T.StartedAt>1.15;}));
				Shot(TEXT("final.png"));
				Stage=11; NextAt=World->GetTimeSeconds()+0.4; return false;
			}
			case 11:
				Player->SetPrototypeAutomatedInput(false);
				for(const auto& Enemy:Enemies) { if(Enemy.IsValid() && !Enemy->IsEliminated()) { Enemy->SetTrainingMenuFrozen(true); } }
				PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::Tab,IE_Pressed,FPlatformTime::Cycles64()));
				Stage=12; NextAt=World->GetTimeSeconds()+0.2; return false;
			case 12:
			{
				PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::Tab,IE_Released,FPlatformTime::Cycles64()));
				Test->TestTrue(TEXT("Actual Tab input opens the bag"),Player->IsBagOpen());
				Player->SelectEquipment(2);
				const auto Key=[](FKey K)
				{ FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0));
				  FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K,FModifierKeysState(),0,false,0,0)); };
				Key(EKeys::Enter); Test->TestTrue(TEXT("Focused equipment opens compatible picker"),Player->bChoosingEquipment);
				Player->BagSelection=1; Key(EKeys::Enter);
				Test->TestTrue(TEXT("Focused bag rejects the item already in the other slot"),Player->GetAttack(2).Id!=Player->GetBag()[1].Id);
				Key(EKeys::Down); Key(EKeys::Enter);
				Test->TestEqual(TEXT("Focused bag confirms available equipment"),Player->GetAttack(2).Type,Player->GetBag()[2].Type);
				Key(EKeys::Escape); Test->TestFalse(TEXT("Escape closes the bag"),Player->IsBagOpen());
				Stage=13; NextAt=World->GetTimeSeconds()+0.2; return false;
			}
			case 13:
			{
				if(auto* HUD=Player->GetPrototypeHUD())
				{
					const auto& Geometry=HUD->GetCachedGeometry();
					const FVector2D Size=Geometry.GetLocalSize();
					const float S=FMath::Min(Size.X/1280.0,Size.Y/720.0);
					const FVector2D Original=FSlateApplication::Get().GetCursorPos();
					FSlateApplication::Get().SetCursorPos(Geometry.LocalToAbsolute(FVector2D(Size.X-226*S,Size.Y-104*S)));
					Test->TestEqual(TEXT("HUD click target matches painted slot after DPI scaling"),HUD->GetPointerAction(),2);
					PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));
					Test->TestEqual(TEXT("Mouse click chooses special slot 1"),Player->GetSelectedAttack(),1);
					Test->TestFalse(TEXT("Clicking HUD does not start a throw"),Player->IsChargingThrow());
					PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Released,FPlatformTime::Cycles64()));
					FSlateApplication::Get().SetCursorPos(Original);
				}
				PC->TogglePauseMenu(); Shot(TEXT("pause.png")); Stage=14; NextAt=0; return false;
			}
			case 15:
				Test->TestTrue(TEXT("Pause returns to RPG playing"),PC->IsGameplayActive());
				Shot(TEXT("ui-final.png")); Stage=16; NextAt=World->GetTimeSeconds()+0.3; return false;
			case 16:
				PC->ShowMenuScreen(EChaosImpactScreen::Title);
				Stage=17; NextAt=0; return false;
			case 18:
				Test->TestTrue(TEXT("Actual title command returns to prototype playing"),PC->IsGameplayActive() && Mode!=nullptr);
				Test->TestNotNull(TEXT("Actual title command enters the colony adventure"),ARPGAdventureWorld::Find(World));
				UGameplayStatics::OpenLevel(PC,TEXT("/Game/ChaosImpact/RPGPrototype/Lvl_RPGVSStage1"),true,TEXT("CIRPGPrototype=1?CIOpenWorld=1?CILocalPlayers=1?CIKeyboardPlayer=0"));
				Stage=19; NextAt=0; return false;
			case 19:
				if(ARPGAdventureWorld::Find(World)) { return false; } return true;
			}
			return true;
		}
	private:
		static int32 LootCount(UWorld* World) { int32 Count=0; for(TActorIterator<ARPGPrototypeLoot> It(World);It;++It){++Count;} return Count; }
		FAutomationTestBase* Test;
		double Started=FPlatformTime::Seconds(),NextAt=0,WorldTime=0;
		int32 Stage=0;
		float BeforeHP=0,PlayerHP=0,FighterHP=0;
		TArray<TWeakObjectPtr<ARPGPrototypeCharacter>> Enemies;
		TWeakObjectPtr<ARPGPrototypeCharacter> Target;
		TWeakObjectPtr<ARPGPrototypeBall> PlayerShot;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPrototypeIntegration,"ChaosImpact.RPGPrototype.Integration",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGPrototypeIntegration::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGPrototypeCommand(this));
	return true;
}
#endif
