#include "RPGPrototype.h"
#include "RPGAdventure.h"
#include "RPGSnowField.h"
#include "ChaosImpactSfx.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

// Fast travel: the colony and every rest beacon this hero has lit (saved with the hero, URPGPrototypeSave::TravelPoints).
// Opened with M (pad: D-pad right), the "ファストトラベル" chip at the top left, or E at a rest beacon. Not while
// fighting (RPGTuning::OutOfCombatSeconds), down, or inside the Shima-enaga's shut nest. The list is drawn as a page
// of the quest panel (QuestGiver == TravelPanel, RPGAdventureHUD.cpp).

FString ARPGPrototypeCharacter::TravelKey(const AActor* Beacon)
{
	// The place, not the actor's name: stable across sessions and machines, also for beacons made at run time.
	return Beacon ? FString::Printf(TEXT("%d_%d"), FMath::RoundToInt(Beacon->GetActorLocation().X / 100), FMath::RoundToInt(Beacon->GetActorLocation().Y / 100)) : FString();
}

TArray<ARPGPrototypeCharacter::FTravelPoint> ARPGPrototypeCharacter::TravelList() const
{
	TArray<FTravelPoint> Out;
	const auto* Adventure = ARPGAdventureWorld::Find(GetWorld());
	if (!Adventure) { return Out; }
	const int32 Local = FMath::Max(0, GetLocalRPGIndex());
	FTravelPoint& Colony = Out.AddDefaulted_GetRef();
	Colony.Key = TEXT("colony"); Colony.bColony = Colony.bVisited = true;
	Colony.At = Adventure->PlayerSpawn(Local); Colony.Name = Adventure->RegionName(Adventure->PlayerSpawn(0));
	// Beacons, nearest to the colony first.
	TArray<ARPGAdventureCheckpoint*> Beacons;
	for (TActorIterator<ARPGAdventureCheckpoint> It(GetWorld()); It; ++It) { Beacons.Add(*It); }
	const FVector Home = Colony.At;
	Beacons.Sort([&](const ARPGAdventureCheckpoint& A, const ARPGAdventureCheckpoint& B)
	{ return FVector::DistSquared2D(A.GetActorLocation(), Home) < FVector::DistSquared2D(B.GetActorLocation(), Home); });
	for (const ARPGAdventureCheckpoint* Beacon : Beacons)
	{
		FTravelPoint& Point = Out.AddDefaulted_GetRef();
		Point.Key = TravelKey(Beacon);
		Point.Name = Beacon->DisplayName;
		Point.At = Beacon->RespawnFor(Local); Point.Rotation = Beacon->GetActorRotation();
		Point.bVisited = TravelPoints.Contains(Point.Key);
	}
	return Out;
}

ARPGAdventureCheckpoint* ARPGPrototypeCharacter::NearestBeacon() const
{
	if (bGrunt || !GetWorld()) { return nullptr; }
	for (TActorIterator<ARPGAdventureCheckpoint> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) < FMath::Square(It->ActivationRadius + 60)
			&& FMath::Abs(It->GetActorLocation().Z - GetActorLocation().Z) < 260) { return *It; }
	}
	return nullptr;
}

bool ARPGPrototypeCharacter::CanFastTravel(FString* Why) const
{
	const auto Fail = [Why](const TCHAR* Reason) { if (Why) { *Why = Reason; } return false; };
	if (bGrunt || IsEliminated() || !GetWorld()) { return Fail(TEXT("いまは移動できません")); }
	if (bDowned) { return Fail(TEXT("ダウン中は移動できません")); }
	if (IsThrowReleasePending() || IsDriving()) { return Fail(TEXT("投げている間は移動できません")); }
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastDamagedAt < RPGTuning::OutOfCombatSeconds || Now - LastAttackAt < RPGTuning::OutOfCombatSeconds)
	{ return Fail(TEXT("戦っている間は移動できません。少し離れてから")); }
	if (const auto* Field = ARPGSnowField::Find(GetWorld());
		Field && Field->IsNestFight() && FVector::Dist2D(GetActorLocation(), Field->NestCenter()) < Field->NestRadius() + 200)
	{ return Fail(TEXT("巣の中からは移動できません")); }
	return true;
}

bool ARPGPrototypeCharacter::OpenFastTravel()
{
	FString Why;
	if (!CanFastTravel(&Why)) { Notice = Why; NoticeUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + 2.5 : 0; return false; }
	if (!OpenQuestPanel(TravelPanel)) { return false; }
	// Start on the visited place closest to here.
	const auto List = TravelList();
	TravelSelection = 0; float Best = MAX_flt;
	for (int32 Index = 0; Index < List.Num(); ++Index)
	{
		const float Distance = FVector::DistSquared2D(List[Index].At, GetActorLocation());
		if (List[Index].bVisited && Distance < Best) { Best = Distance; TravelSelection = Index; }
	}
	return true;
}

bool ARPGPrototypeCharacter::HandleTravelKey(FKey Key)
{
	if (!IsTravelOpen()) { return false; }
	if (Key == EKeys::W) { Key = EKeys::Up; } else if (Key == EKeys::S) { Key = EKeys::Down; }
	const auto List = TravelList();
	if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::M || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_DPad_Right)
	{ SetQuestMenuOpen(false); return true; }
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::MouseScrollUp || Key == EKeys::Gamepad_LeftStick_Up)
	{ TravelSelection = FMath::Clamp(TravelSelection - 1, 0, FMath::Max(0, List.Num() - 1)); return true; }
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::MouseScrollDown || Key == EKeys::Gamepad_LeftStick_Down)
	{ TravelSelection = FMath::Clamp(TravelSelection + 1, 0, FMath::Max(0, List.Num() - 1)); return true; }
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom) { FastTravelTo(TravelSelection); }
	return true;
}

bool ARPGPrototypeCharacter::FastTravelTo(int32 Index)
{
	const auto List = TravelList();
	if (!List.IsValidIndex(Index)) { return false; }
	const FTravelPoint& Point = List[Index];
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Point.bVisited) { Notice = TEXT("まだ灯りをともしていない場所です"); NoticeUntil = Now + 2.5; return false; }
	FString Why;
	if (!CanFastTravel(&Why)) { Notice = Why; NoticeUntil = Now + 2.5; return false; }
	SetQuestMenuOpen(false);
	CancelChargingThrow();
	TeleportHero(Point.At, Point.Rotation);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (auto* PC = Cast<APlayerController>(GetController())) { PC->SetControlRotation(Point.Rotation); }
	SafeGround.Reset(); GroundedSince = -1;
	// The place travelled to is where this player gets up again (no free healing: resting by the beacon does that).
	ActiveAdventureCheckpoint.Reset();
	if (Point.bColony) { SetRespawnPoint(Point.At, Point.Rotation); }
	else
	{
		for (TActorIterator<ARPGAdventureCheckpoint> It(GetWorld()); It; ++It)
		{ if (TravelKey(*It) == Point.Key) { ActiveAdventureCheckpoint = *It; SetRespawnPoint(Point.At, Point.Rotation); break; } }
	}
	// Online guest: the host keeps the respawn point.
	if (IsNetClientHero()) { ServerRestAt(Point.At, Point.Rotation, false); }
	Notice = Point.Name + TEXT(" へ移動した"); NoticeUntil = Now + 2.5;
	ChaosImpactSfx::Play2D(this, EChaosImpactSfx::UiConfirm, .7f);
	return true;
}
