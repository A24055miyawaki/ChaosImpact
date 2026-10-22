#include "RPGPrototype.h"
#include "RPGPrototypeHUD.h"
#include "RPGTreasureChest.h"
#include "ChaosImpactGameMode.h"
#include "ChaosImpactSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "GameMapsSettings.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "RPGAdventure.h"
#include "RPGTuning.h"
#include "RPGTitle.h"

int32 ARPGPrototypeCharacter::GetLocalRPGIndex() const
{
    const auto* PC=Cast<APlayerController>(GetController()); const auto* GI=GetGameInstance();
    return PC && GI ? GI->GetLocalPlayers().IndexOfByKey(PC->GetLocalPlayer()) : INDEX_NONE;
}
bool ARPGPrototypeCharacter::UsesMiniMenu() const
{ return bPreferMiniMenu || (GetGameInstance() && GetGameInstance()->GetLocalPlayers().Num()>1); }
void ARPGPrototypeCharacter::InitializeRPGPreferences()
{
    if(bPreferencesReady || GetLocalRPGIndex()<0) { return; }
    bPreferencesReady=true;
    if(GConfig)
    {
        GConfig->GetBool(TEXT("ChaosImpact.RPGPrototype"),*FString::Printf(TEXT("MiniMenuP%d"),GetLocalRPGIndex()+1),bPreferMiniMenu,GGameUserSettingsIni);
        GConfig->GetInt(TEXT("ChaosImpact.RPGPrototype"),*FString::Printf(TEXT("BagSortP%d"),GetLocalRPGIndex()+1),BagSort,GGameUserSettingsIni);
        BagSort=FMath::Clamp(BagSort,0,ERPGBagSort::Count-1);
    }
    if(UGameplayStatics::DoesSaveGameExist(GetTestSaveSlot(),0))
    { if(const auto* Save=Cast<URPGPrototypeSave>(UGameplayStatics::LoadGameFromSlot(GetTestSaveSlot(),0))) { SaveSummary=Save->SavedAt.ToString(TEXT("%m/%d %H:%M")); } }
}
void ARPGPrototypeCharacter::ToggleMenuStyle()
{
    if(GetGameInstance() && GetGameInstance()->GetLocalPlayers().Num()>1)
    { Notice=TEXT("分割プレイ中は自分用ミニメニューです"); NoticeUntil=GetWorld()->GetTimeSeconds()+3; return; }
    bPreferMiniMenu=!bPreferMiniMenu;
    if(GConfig)
    { GConfig->SetBool(TEXT("ChaosImpact.RPGPrototype"),*FString::Printf(TEXT("MiniMenuP%d"),GetLocalRPGIndex()+1),bPreferMiniMenu,GGameUserSettingsIni); GConfig->Flush(false,GGameUserSettingsIni); }
    if(bBagOpen) { SetBagOpen(false); SetBagOpen(true); BagPage=3; }
}
FString ARPGPrototypeCharacter::GetTestSaveSlot() const
{ return HeroId.IsEmpty()?FString::Printf(TEXT("CI_RPGPrototype_Test_P%d"),FMath::Clamp(GetLocalRPGIndex(),0,3)+1):RPGHeroes::SaveSlot(HeroId); }
bool ARPGPrototypeCharacter::SaveTestProgress(const FString& SlotOverride)
{
    if(bGrunt || IsEliminated() || IsChargingThrow() || IsThrowReleasePending() || IsDriving() || GetLocalRPGIndex()<0) { return false; }
    // A chest's claim is saved, so whatever it threw out that is still lying about is taken now rather than lost.
    ARPGRubyGem::CollectAllFor(this);
    auto* Save=Cast<URPGPrototypeSave>(UGameplayStatics::CreateSaveGameObject(URPGPrototypeSave::StaticClass()));
    if(!Save) { return false; }
    Save->Items=Bag; Save->Level=RPGLevel; Save->XP=XP; Save->Rubies=Rubies; Save->SelectedAttack=SelectedAttack;
    Save->HP=Health; Save->Stamina=Stamina; Save->SavedAt=FDateTime::Now();
    Save->Quests=Quests;
    Save->OpenedChestIds=OpenedChestIds.Array();
    Save->TravelPoints=TravelPoints;
    for(const auto& I:Attacks) { Save->AttackIds.Add(I.Id); }
    for(const auto& I:Badges) { Save->BadgeIds.Add(I.Id); }
    for(const auto& I:Gears) { Save->GearIds.Add(I.Id); }
    for(const auto& I:Bag)
    {
        double Ready=ItemReadyAt.FindRef(I.Id);
        for(int32 S=0;S<3;++S) { if(Attacks[S].Id==I.Id) { Ready=FMath::Max(Ready,ReadyAt[S]); } }
        Save->Cooldowns.Add({I.Id,static_cast<float>(FMath::Max(0.0,Ready-GetWorld()->GetTimeSeconds()))});
    }
    const bool OK=UGameplayStatics::SaveGameToSlot(Save,SlotOverride.IsEmpty()?GetTestSaveSlot():SlotOverride,0);
    if(!bQuietSave || !OK)
    { Notice=OK?TEXT("自分の装備・育成データを保存しました"):TEXT("保存できませんでした。空き容量などを確認してください"); NoticeUntil=GetWorld()->GetTimeSeconds()+4; }
    if(OK)
    {
        SaveSummary=Save->SavedAt.ToString(TEXT("%m/%d %H:%M"));
        if(!HeroId.IsEmpty())
        {
            int32 Done=0; for(const auto& Q:Quests) { Done+=Q.Stage==3?1:0; }
            const double Now=GetWorld()->GetTimeSeconds();
            RPGHeroes::Touch(HeroId,RPGLevel,Done,static_cast<float>(Now-HeroStartedAt)); HeroStartedAt=Now;
        }
    }
    return OK;
}
bool ARPGPrototypeCharacter::LoadTestProgress(const FString& SlotOverride)
{
    if(bGrunt || IsEliminated() || IsChargingThrow() || IsThrowReleasePending() || IsDriving() || GetLocalRPGIndex()<0) { return false; }
    auto* Save=Cast<URPGPrototypeSave>(UGameplayStatics::LoadGameFromSlot(SlotOverride.IsEmpty()?GetTestSaveSlot():SlotOverride,0));
    const auto Fail=[&]() { Notice=TEXT("読み込めるテストセーブがありません（データは変更していません）"); NoticeUntil=GetWorld()->GetTimeSeconds()+4; return false; };
    if(!Save || (Save->Version<1 || Save->Version>3) || Save->Items.Num()<1 || Save->Items.Num()>RPGTuning::BagCapacity || Save->AttackIds.Num()!=3 || Save->BadgeIds.Num()!=3
        || (Save->GearIds.Num()!=0 && Save->GearIds.Num()!=4)
        || Save->Level<1 || Save->Level>10000 || Save->XP<0 || Save->XP>=RPGTuning::RequiredXP(Save->Level) || Save->Rubies<0
        || !FMath::IsFinite(Save->HP) || !FMath::IsFinite(Save->Stamina) || Save->SelectedAttack<0 || Save->SelectedAttack>2 || Save->Cooldowns.Num()>RPGTuning::BagCapacity) { return Fail(); }
    TMap<FGuid,FRPGPrototypeItem> Items;
    for(const auto& I:Save->Items)
    {
        if(!I.IsValid() || Items.Contains(I.Id) || I.Level<1 || I.Level>10000 || I.Enhancement<0
            || static_cast<int32>(I.Rarity)>3 || (static_cast<int32>(I.Kind)>3 && !RPGEquipment::IsGear(I.Kind)) || I.Type==EChaosImpactBallType::Nova
            || (I.Kind==ERPGItemKind::Ball && !RPGEquipment::BallRoster().Contains(I.Type))
            || (I.Rarity!=ERPGPrototypeRarity::Legend && I.Enhancement>10) || !FMath::IsFinite(I.Power)
            || !FMath::IsFinite(I.Cooldown) || (I.Kind==ERPGItemKind::Ball?(I.Power<=0 || I.Cooldown<=0 || I.Cooldown>60):(I.Power!=0 || I.Cooldown!=0))
            || I.Tunes.Num()>3 || I.Intrinsics.Num()>2) { return Fail(); }
        // A roll outside the current range means the catalog was re-tuned after saving: it is moved into the new range
        // (RPGEquipment::MigrateRoll) rather than refusing the whole save. Unknown keys and broken numbers still fail.
        const auto ValidAffix=[](const FRPGAffix& A)
        { const auto* D=RPGEquipment::Find(A.Key); return A.Key.IsNone() || (D && FMath::IsFinite(A.Roll) && A.Rank>=1 && A.Rank<=RPGTuning::TuneMaxRank); };
        if(!ValidAffix(I.Badge)) { return Fail(); }
        for(const auto& A:I.Tunes) { if(!ValidAffix(A)) { return Fail(); } }
        for(const auto& A:I.Intrinsics) { if(!ValidAffix(A)) { return Fail(); } }
        Items.Add(I.Id,I);
    }
    TArray<FRPGPrototypeItem> NewAttacks,NewBadges; TSet<FGuid> Equipped;
    for(int32 S=0;S<3;++S)
    {
        const auto* I=Items.Find(Save->AttackIds[S]);
        if(S>0 && !Save->AttackIds[S].IsValid()) { NewAttacks.Add(FRPGPrototypeItem{}); }
        else
        {
            if(!I || I->Kind!=ERPGItemKind::Ball || Equipped.Contains(I->Id) || (S==0)!=(I->Type==EChaosImpactBallType::Normal)) { return Fail(); }
            NewAttacks.Add(*I); Equipped.Add(I->Id);
        }
        const FGuid Id=Save->BadgeIds[S]; const auto* B=Items.Find(Id);
        if(Id.IsValid() && (!B || static_cast<int32>(B->Kind)!=S+1 || Equipped.Contains(Id))) { return Fail(); }
        NewBadges.Add(B?*B:FRPGPrototypeItem{}); if(B) { Equipped.Add(Id); }
    }
    TArray<FRPGPrototypeItem> NewGears; NewGears.SetNum(4);
    for(int32 S=0;S<Save->GearIds.Num();++S)
    {
        const FGuid Id=Save->GearIds[S]; const auto* I=Items.Find(Id);
        if(Id.IsValid() && (!I || static_cast<int32>(I->Kind)!=S+5 || Equipped.Contains(Id))) { return Fail(); }
        if(I) { NewGears[S]=*I; Equipped.Add(Id); }
    }
    for(const auto& C:Save->Cooldowns) { if(!Items.Contains(C.Item) || !FMath::IsFinite(C.Remaining) || C.Remaining<0 || C.Remaining>120) { return Fail(); } }
    // Older saves have fewer quests (the table only grows at its end); the rest start fresh.
    if(Save->Quests.Num()>RPGQuests::Num()) { return Fail(); }
    for(int32 I=0;I<Save->Quests.Num();++I)
    { const auto& Q=Save->Quests[I]; if(Q.Stage<0 || Q.Stage>3 || Q.Count<0 || Q.Count>RPGQuests::Get(I).Goal || Q.Flags<0 || (Q.Flags & ~RPGQuests::FlagMask(I))!=0) { return Fail(); } }
    TSet<FName> NewOpened;
    if(Save->OpenedChestIds.Num()>2048) { return Fail(); }
    if(Save->TravelPoints.Num()>256 || Save->TravelPoints.ContainsByPredicate([](const FString& Key) { return Key.Len()>32; })) { return Fail(); }
    if(Save->Version>=3) { for(FName Id:Save->OpenedChestIds) { if(Id.IsNone() || Id.ToString().Len()>128 || NewOpened.Contains(Id)) { return Fail(); } NewOpened.Add(Id); } }
    // Apply only after the complete payload has passed validation; a bad file cannot partly replace a loadout.
    const auto Migrate=[](FRPGPrototypeItem& I)
    { RPGEquipment::MigrateRoll(I.Badge); for(auto& A:I.Tunes) { RPGEquipment::MigrateRoll(A); } for(auto& A:I.Intrinsics) { RPGEquipment::MigrateRoll(A); } };
    for(auto& I:NewAttacks) { Migrate(I); } for(auto& I:NewBadges) { Migrate(I); } for(auto& I:NewGears) { Migrate(I); }
    Bag=Save->Items; for(auto& I:Bag) { Migrate(I); }
    Attacks=NewAttacks; Badges=NewBadges; Gears=NewGears; RPGLevel=Save->Level; XP=Save->XP; Rubies=Save->Rubies; SelectedAttack=Save->SelectedAttack;
    Quests=Save->Quests; Quests.SetNum(RPGQuests::Num()); TrackedQuest=INDEX_NONE;
    OpenedChestIds=MoveTemp(NewOpened);
    TravelPoints=Save->TravelPoints;
    if(!Attacks[SelectedAttack].IsValid()) { SelectedAttack=0; }
    ItemReadyAt.Empty(); for(const auto& C:Save->Cooldowns) { ItemReadyAt.Add(C.Item,GetWorld()->GetTimeSeconds()+C.Remaining); }
    for(int32 S=0;S<3;++S) { ReadyAt[S]=ItemReadyAt.FindRef(Attacks[S].Id); }
    Statuses.Empty(); StatusSources.Empty(); ComboTargets.Empty(); Shield=0;
    KnockbackDistance=0; KnockbackResistanceUntil=0;
    ClearCarriedBalls(); RebuildEquipment(); Health=FMath::Clamp(Save->HP,1.0f,MaxHealth); Stamina=FMath::Clamp(Save->Stamina,0.0f,MaxStamina);
    BagSelection=0; BagFilter=1; DetailScroll=TuneSelection=0; PendingSalvage=PendingTune=INDEX_NONE; PendingSessionAction=0; bPendingBulkSalvage=false; bChoosingEquipment=bChoosingDestination=false;
    UnseenItems.Empty();
    EnsureReadyBall(); SaveSummary=Save->SavedAt.ToString(TEXT("%m/%d %H:%M"));
    Notice=TEXT("自分のデータをロードしました。他のプレイヤーとステージはそのままです"); NoticeUntil=GetWorld()->GetTimeSeconds()+4; return true;
}
void ARPGPrototypeCharacter::RequestSessionAction(int32 Action)
{
    if(HasConfirmation()) { return; }
    if(Action==1 && !UGameplayStatics::DoesSaveGameExist(GetTestSaveSlot(),0)) { SaveTestProgress(); return; }
    if(Action==2 && !UGameplayStatics::DoesSaveGameExist(GetTestSaveSlot(),0)) { Notice=TEXT("このプレイヤーのセーブはまだありません"); NoticeUntil=GetWorld()->GetTimeSeconds()+3; return; }
    PendingSessionAction=Action;
}
void ARPGPrototypeCharacter::ConfirmSessionAction()
{ const int32 Action=PendingSessionAction; PendingSessionAction=0; if(Action==1) { SaveTestProgress(); } if(Action==2) { LoadTestProgress(); } }

bool ARPGPrototypePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    auto* P=Cast<ARPGPrototypeCharacter>(GetPawn());
    if(!P || !IsGameplayActive()) { return Super::InputKey(Params); }
    if(!AcceptsInputKey(Params.Key)) { return true; }
    const bool Pressed=Params.Event==IE_Pressed || Params.Event==IE_DoubleClick;
    if(auto* Title=P->GetTitleWidget();Title && P->IsInTitle())
    {
        if(Params.Key.IsAnalog() || Params.Event==IE_Released) { return APlayerController::InputKey(Params); }
        if(Pressed) { if(Params.Key==EKeys::LeftMouseButton) { Title->HandleClick(); } else { Title->HandleKey(Params.Key); } }
        return true;
    }
    if(P->IsQuestMenuOpen())
    {
        if(Params.Key.IsAnalog() || Params.Event==IE_Released) { return APlayerController::InputKey(Params); }
        if(Pressed)
        { if(Params.Key==EKeys::LeftMouseButton) { if(auto* HUD=P->GetPrototypeHUD()) { HUD->ActivateAdventureAction(HUD->GetAdventurePointerAction()); } }
          else { P->HandleQuestKey(Params.Key); } }
        return true;
    }
    if(Pressed && (Params.Key==EKeys::F5 || Params.Key==EKeys::F9))
    { if(P->HasConfirmation()) { return true; } P->SetBagOpen(true); if(P->IsBagOpen()) { P->BagPage=3; P->RequestSessionAction(Params.Key==EKeys::F5?1:2); } return true; }
    if(ChaosImpactSettings::IsActionKey(this,EChaosImpactAction::Pause,Params.Key) && P->UsesMiniMenu())
    { if(Pressed) { P->SetBagOpen(!P->IsBagOpen()); P->BagPage=3; } return true; }
    if(Params.Key==EKeys::Tab || Params.Key==EKeys::Gamepad_Special_Left)
    { if(Pressed) { if(P->IsBagOpen()) { P->HandleBagKey(Params.Key); } else { P->SetBagOpen(true); } } return true; }
    if(P->IsBagOpen())
    {
        // Keep axes/releases fresh without invoking the shared gameplay/pause shortcuts.
        // The pawn's menu freeze already prevents movement, while its stick can navigate the bag.
        if(Params.Key.IsAnalog() || Params.Event==IE_Released) { return APlayerController::InputKey(Params); }
        if(Pressed && (Params.Key==EKeys::MouseScrollUp || Params.Key==EKeys::MouseScrollDown)) { P->NavigateBag(Params.Key==EKeys::MouseScrollUp?-1:1); return true; }
        if(Pressed)
        {
            if(Params.Key==EKeys::LeftMouseButton) { if(auto* HUD=P->GetPrototypeHUD()) { HUD->ActivateBagAction(HUD->GetBagPointerAction()); } }
            else { P->HandleBagKey(Params.Key); }
        }
        return true; // Never feed menu keys to another pawn, gameplay throw, or the shared pause handler.
    }
    return Super::InputKey(Params);
}

int32 ARPGPrototypeGameMode::GetLocalPlayerCount() const
{ return GetGameInstance()?GetGameInstance()->GetLocalPlayers().Num():1; }
void ARPGPrototypeGameMode::SetLocalPlayerCount(int32 Count)
{
    auto* GI=GetGameInstance(); if(!GI || !GetWorld()) { return; }
    DesiredLocalPlayers=FMath::Clamp(Count,1,4);
    int32 Keyboard=FCString::Atoi(GetWorld()->URL.GetOption(TEXT("CIKeyboardPlayer="),TEXT("0")));
    if(Keyboard>=DesiredLocalPlayers || Keyboard<INDEX_NONE) { Keyboard=0; }
    GetWorld()->URL.RemoveOption(TEXT("CIKeyboardPlayer=")); GetWorld()->URL.AddOption(*FString::Printf(TEXT("CIKeyboardPlayer=%d"),Keyboard));
    // Canonicalize users before reapplying the shared device mapper. Otherwise repeated
    // live changes with keyboard on P2+ can give two LocalPlayers the primary platform user.
    const FPlatformUserId Primary=IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();
    if(!GI->GetLocalPlayers().IsEmpty())
    {
        auto* First=GI->GetLocalPlayers()[0];
        for(auto* Local:GI->GetLocalPlayers())
        { if(Local!=First && Local->GetPlatformUserId()==Primary) { Local->SetPlatformUserId(First->GetPlatformUserId()); First->SetPlatformUserId(Primary); break; } }
    }
    GetWorld()->URL.RemoveOption(TEXT("CILocalPlayers=")); GetWorld()->URL.AddOption(*FString::Printf(TEXT("CILocalPlayers=%d"),DesiredLocalPlayers));
    while(GI->GetLocalPlayers().Num()>DesiredLocalPlayers)
    {
        auto* PC=GI->GetLocalPlayers().Last()->GetPlayerController(GetWorld());
        if(auto* P=PC?Cast<ARPGPrototypeCharacter>(PC->GetPawn()):nullptr) { P->SetBagOpen(false); if(P->GetPrototypeHUD()) { P->GetPrototypeHUD()->RemoveFromParent(); } }
        if(PC) { UGameplayStatics::RemovePlayer(PC,true); }
        else { GI->RemoveLocalPlayer(GI->GetLocalPlayers().Last()); }
    }
    while(GI->GetLocalPlayers().Num()<DesiredLocalPlayers)
    {
        auto* PC=UGameplayStatics::CreatePlayer(this,-1,true); if(!PC) { break; }
        if(auto* P=Cast<ARPGPrototypeCharacter>(PC->GetPawn()))
        { const int32 I=GI->GetLocalPlayers().Num()-1; const auto* Adventure=ARPGAdventureWorld::Find(GetWorld()); const FVector At=Adventure?Adventure->PlayerSpawn(I):FVector(-2650,180*I,120); P->SetActorLocation(At); P->SetRespawnPoint(At,FRotator::ZeroRotator); }
    }
    if(auto* Settings=GetMutableDefault<UGameMapsSettings>())
    { Settings->bUseSplitscreen=true; Settings->TwoPlayerSplitscreenLayout=ETwoPlayerSplitScreenType::Vertical; Settings->ThreePlayerSplitscreenLayout=EThreePlayerSplitScreenType::FavorTop; }
    if(auto* Viewport=GetWorld()->GetGameViewport()) { Viewport->SetForceDisableSplitscreen(false); Viewport->LayoutPlayers(); }
    AChaosImpactGameMode::ApplyLocalControllerAssignments(GetWorld(),DesiredLocalPlayers,Keyboard);
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        if(auto* PC=Cast<ARPGPrototypePlayerController>(It->Get());PC && PC->IsLocalController()) { PC->ConfigureRPGInput(DesiredLocalPlayers,Keyboard); }
        if(auto* P=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn());P && P->IsBagOpen())
        { const int32 Page=P->BagPage; P->SetBagOpen(false); P->SetBagOpen(true); P->BagPage=Page; }
    }
    ApplyRPGScreenLayout();
}
void ARPGPrototypeGameMode::ApplyLocalSetup(const FString& Options)
{
    if(!GetWorld()) { return; }
    FURL& URL=GetWorld()->URL;
    for(int32 Pad=0;Pad<4;++Pad) { URL.RemoveOption(*FString::Printf(TEXT("CIPadDevice%d="),Pad)); URL.RemoveOption(*FString::Printf(TEXT("CIPadController%d="),Pad)); }
    TArray<FString> Parts; Options.ParseIntoArray(Parts,TEXT("?"));
    int32 Count=GetLocalPlayerCount();
    for(const FString& Part:Parts)
    {
        FString Key,Value; if(!Part.Split(TEXT("="),&Key,&Value)) { continue; }
        URL.RemoveOption(*(Key+TEXT("="))); URL.AddOption(*Part);
        if(Key==TEXT("CILocalPlayers")) { Count=FCString::Atoi(*Value); }
    }
    SetLocalPlayerCount(Count);
}
bool ARPGPrototypeGameMode::HandleUnownedPad(const FInputKeyEventArgs& EventArgs)
{
    if(EventArgs.Event!=IE_Pressed || !GetWorld() || GetLocalPlayerCount()>=4) { return false; }
    // On the title the controller assignment adds players instead.
    const auto* First=Cast<ARPGPrototypeCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!First || (First->IsInTitle() && First->GetTitleWidget()->GetMode()==URPGTitleWidget::EMode::Title)) { return false; }
    if(EventArgs.Key==EKeys::Gamepad_Special_Right) { return JoinWithPad(EventArgs.InputDevice.GetId()); }
    JoinPromptUntil=GetWorld()->GetRealTimeSeconds()+6;
    return true;
}
bool ARPGPrototypeGameMode::IsJoinPromptShown() const
{ return GetWorld() && GetWorld()->GetRealTimeSeconds()<JoinPromptUntil && GetLocalPlayerCount()<4; }
bool ARPGPrototypeGameMode::JoinWithPad(int32 InputDeviceId)
{
    auto* GI=GetGameInstance(); UWorld* World=GetWorld();
    if(!GI || !World || InputDeviceId<0) { return false; }
    const int32 Count=GI->GetLocalPlayers().Num(); if(Count>=4) { return false; }
    const int32 Keyboard=FCString::Atoi(World->URL.GetOption(TEXT("CIKeyboardPlayer="),TEXT("0")));
    // Write down the pad each player holds now, so re-mapping keeps theirs and the new player gets this one.
    IPlatformInputDeviceMapper& Mapper=IPlatformInputDeviceMapper::Get();
    FString Options=FString::Printf(TEXT("CILocalPlayers=%d?CIKeyboardPlayer=%d"),Count+1,Keyboard);
    int32 Pad=0;
    for(int32 Index=0;Index<Count;++Index)
    {
        if(Index==Keyboard) { continue; }
        TArray<FInputDeviceId> Devices; Mapper.GetAllInputDevicesForUser(GI->GetLocalPlayers()[Index]->GetPlatformUserId(),Devices);
        const FInputDeviceId* Held=Devices.FindByPredicate([&](const FInputDeviceId& D) { return D!=Mapper.GetDefaultInputDevice() && D.GetId()!=InputDeviceId; });
        const FString Was=World->URL.GetOption(*FString::Printf(TEXT("CIPadDevice%d="),Pad),TEXT(""));
        const int32 Id=Held?Held->GetId():Was.IsEmpty()?INDEX_NONE:FCString::Atoi(*Was);
        if(Id>=0 && Id!=InputDeviceId) { Options+=FString::Printf(TEXT("?CIPadDevice%d=%d"),Pad,Id); }
        ++Pad;
    }
    Options+=FString::Printf(TEXT("?CIPadDevice%d=%d"),Pad,InputDeviceId);
    JoinPromptUntil=0;
    ApplyLocalSetup(Options);
    return GI->GetLocalPlayers().Num()>Count;
}
ARPGPrototypeCharacter* ARPGPrototypeGameMode::NearestPlayer(UWorld* World,const FVector& At)
{
    ARPGPrototypeCharacter* Best=nullptr; double Distance=MAX_dbl;
    if(!World) { return nullptr; }
    for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
    {
        auto* P=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!P || P->IsEliminated() || P->IsGrunt() || P->IsDowned()) { continue; }
        if(const auto* Adventure=ARPGAdventureWorld::Find(World);Adventure && Adventure->IsInColony(P->GetActorLocation())) { continue; }
        const double D=FVector::DistSquared2D(P->GetActorLocation(),At); if(D<Distance) { Best=P; Distance=D; }
    }
    return Best;
}
