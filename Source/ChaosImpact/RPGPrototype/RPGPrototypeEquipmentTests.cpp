#if WITH_DEV_AUTOMATION_TESTS
#include "RPGPrototypeTestLoadout.h"
#include "RPGPrototype.h"
#include "RPGTuning.h"
#include "RPGPrototypeHUD.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactBallSpawner.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactVersusStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentCatalogTest,"ChaosImpact.RPGPrototype.EquipmentCatalog",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentCatalogTest::RunTest(const FString&)
{
    TestEqual(TEXT("Normal plus ten specials, no Nova"),RPGEquipment::BallRoster().Num(),11);
    TestFalse(TEXT("RPG cannot generate Nova"),FRPGPrototypeItem::Make(EChaosImpactBallType::Nova).IsValid());
    int32 IntrinsicDefinitions=0;
    for(const auto Type:RPGEquipment::BallRoster())
    {
        IntrinsicDefinitions+=RPGEquipment::Intrinsics(Type).Num();
        TestTrue(TEXT("Each ball has exclusive and common tunes"),RPGEquipment::Tunes(Type).Num()>=16);
        TSet<FName> Keys;
        for(const auto& D:RPGEquipment::Tunes(Type))
        { TestFalse(TEXT("No duplicate catalog key within a ball"),Keys.Contains(D.Key)); Keys.Add(D.Key); TestNotNull(TEXT("Catalog lookup"),RPGEquipment::Find(D.Key)); }
        auto Untuned=FRPGPrototypeItem::Make(Type);
        for(int32 Socket=0;Socket<3;++Socket)
        {
            for(int32 Index:Untuned.TuneOfferIndices(Socket))
            { TestEqual(TEXT("Tune slots remain correct when catalog grows"),RPGEquipment::IsCommonTune(RPGEquipment::Tunes(Type)[Index].Key),Socket==2); }
        }
        TestEqual(TEXT("Only direct projectiles offer direct knockback"),Keys.Contains(TEXT("Knockback")),Type!=EChaosImpactBallType::Wind);
        TMap<FName,float> FirstByKey; bool Varied=false;
        for(int32 Roll=0;Roll<80;++Roll)
        {
            const auto I=FRPGPrototypeItem::Make(Type,4,ERPGPrototypeRarity::Legend);
            TestEqual(TEXT("Legend rolls two intrinsics"),I.Intrinsics.Num(),2);
            if(I.Intrinsics.Num()!=2) { return false; }
            TestNotEqual(TEXT("No duplicated intrinsic"),I.Intrinsics[0].Key,I.Intrinsics[1].Key);
            int32 Transforms=0;
            for(const auto& A:I.Intrinsics)
            {
                const auto* D=RPGEquipment::Find(A.Key); if(!D) { return false; }
                TestTrue(TEXT("Rolled effect within its bounds"),A.Roll>=D->Min && A.Roll<=D->Max);
                Transforms+=D->bTransform?1:0;
                TestFalse(TEXT("No unresolved format placeholder"),RPGEquipment::Describe(A).Contains(TEXT("%.")));
                if(const float* First=FirstByKey.Find(A.Key)) { if(!FMath::IsNearlyEqual(*First,A.Roll)) { Varied=true; } }
                else { FirstByKey.Add(A.Key,A.Roll); }
            }
            TestTrue(TEXT("At most one trajectory transformation"),Transforms<=1);
        }
        TestTrue(TEXT("Effect values really vary"),Varied);
        TestEqual(TEXT("Unique rolls one intrinsic"),FRPGPrototypeItem::Make(Type,1,ERPGPrototypeRarity::Unique).Intrinsics.Num(),1);
        TestEqual(TEXT("Rare has no intrinsic"),FRPGPrototypeItem::Make(Type,1,ERPGPrototypeRarity::Rare).Intrinsics.Num(),0);
    }
    TestEqual(TEXT("54 ball intrinsic candidates"),IntrinsicDefinitions,54);
    int32 BadgeDefinitions=0;
    for(auto K:{ERPGItemKind::LifeBadge,ERPGItemKind::MobilityBadge,ERPGItemKind::TechniqueBadge})
    {
        BadgeDefinitions+=RPGEquipment::Badges(K).Num();
        TestEqual(TEXT("Six random intrinsic candidates per badge family"),RPGEquipment::BadgeIntrinsics(K).Num(),6);
        for(int32 Archetype=0;Archetype<8;++Archetype)
        {
            const auto B=FRPGPrototypeItem::MakeBadge(K,3,ERPGPrototypeRarity::Legend,Archetype);
            TestTrue(TEXT("Badge generated"),B.IsValid()); TestEqual(TEXT("Badge legendary two traits"),B.Intrinsics.Num(),2);
            TestTrue(TEXT("Rare powerful items dismantle for more rubies"),B.SalvageValue()>FRPGPrototypeItem::MakeBadge(K,1,ERPGPrototypeRarity::Normal,Archetype).SalvageValue());
        }
    }
    TestEqual(TEXT("24 badge archetypes"),BadgeDefinitions,24);
    auto L=FRPGPrototypeItem::Make(EChaosImpactBallType::Fire,10,ERPGPrototypeRarity::Legend);
    L.Enhancement=10; const float P10=L.EffectivePower(); L.Enhancement=11;
    TestTrue(TEXT("Legend continues growing beyond +10"),L.EffectivePower()>P10);
    auto A=FRPGAffix{TEXT("FireTrail"),1.5f,1}; const float V=A.Value(); A.Rank=3;
    TestTrue(TEXT("Tune rank grows its original random roll"),FMath::IsNearlyEqual(A.Value(),V*(1+RPGTuning::TuneRankStep*2)));
    // Tuning table and catalog tools.
    for(const auto Type:RPGEquipment::BallRoster())
    { TestEqual(TEXT("Every ball has its own archetype row in RPGTuning"),RPGTuning::BallArchetype(Type).Type,Type); }
    const FString Csv=RPGEquipment::CatalogCsv();
    for(const auto Type:RPGEquipment::BallRoster()) { for(const auto& D:RPGEquipment::Tunes(Type)) { TestTrue(TEXT("Catalog export lists every tune"),Csv.Contains(D.Key.ToString())); } }
    // A save made before a range was re-tuned is moved into the new range, not rejected.
    FRPGAffix Old{TEXT("KillRefund"),.3f,2}; RPGEquipment::MigrateRoll(Old);
    const auto* Refund=RPGEquipment::Find(TEXT("KillRefund"));
    TestTrue(TEXT("Old roll moved into the re-tuned range"),Refund && Old.Roll>=Refund->Min && Old.Roll<=Refund->Max && Old.Rank==2);
    FRPGAffix Count{TEXT("NormalPierce"),7.6f,9}; RPGEquipment::MigrateRoll(Count);
    TestTrue(TEXT("Counts migrate to whole numbers and rank stays capped"),FMath::IsNearlyEqual(Count.Roll,2.0f) && Count.Rank==RPGTuning::TuneMaxRank);
    // Counts never grow with badge level/enhancement (only with rank).
    auto Crowd=FRPGPrototypeItem::MakeBadge(ERPGItemKind::TechniqueBadge,30,ERPGPrototypeRarity::Legend,0);
    Crowd.Intrinsics={FRPGAffix{TEXT("TechCrowd"),2,1}}; Crowd.Enhancement=40;
    TestTrue(TEXT("Badge growth does not add extra pierces"),FMath::IsNearlyEqual(Crowd.Mod(TEXT("TechCrowd")),2.0f));
    TestTrue(TEXT("Badge growth still raises percentages"),Crowd.Mod(Crowd.Badge.Key)>Crowd.Badge.Roll);
    return true;
}

namespace
{
class FRPGEquipmentPlayCommand : public IAutomationLatentCommand
{
public:
    explicit FRPGEquipmentPlayCommand(FAutomationTestBase* Test):Test(Test){}
    bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>120) { Test->AddError(FString::Printf(TEXT("Equipment test timeout stage %d"),Stage)); return true; }
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) { if(C.World() && C.World()->IsGameWorld()) { W=C.World(); break; } }
        if(!W) { return false; }
        auto* PC=Cast<AChaosImpactPlayerController>(W->GetFirstPlayerController()); if(!PC || !PC->GetPawn()) { return false; }
        auto* Mode=W->GetAuthGameMode<ARPGPrototypeGameMode>(); auto* P=Cast<ARPGPrototypeCharacter>(PC->GetPawn());
        if(!Mode || !P) { if(!Travel) { Travel=PC->OpenExplorationMap(); } return false; }
        if(!PC->IsGameplayActive() || W->GetTimeSeconds()<Next || !P->GetPrototypeHUD()) { return false; }
        auto Shot=[](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RPGPrototypeQA"),Name),true,false); };
        if(Stage==0)
        {
            Test->TestTrue(TEXT("RPG uses new VS1 level"),W->GetMapName().Contains(TEXT("Lvl_RPGVSStage1")));
            int32 Stages=0,Pads=0;
            for(TActorIterator<AChaosImpactVersusStage> It(W);It;++It) { ++Stages; Test->TestEqual(TEXT("No original ball points"),It->BallPointOffsets.Num(),0); }
            for(TActorIterator<AChaosImpactBallSpawner> It(W);It;++It) { ++Pads; }
            Test->TestEqual(TEXT("One copy of VS Stage1"),Stages,1); Test->TestEqual(TEXT("No legacy ball spawners"),Pads,0);
            const auto* OriginalClass=LoadClass<AChaosImpactCharacter>(nullptr,TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter_C"));
            if(!Test->TestNotNull(TEXT("Original VS pawn Blueprint"),OriginalClass)) { return true; }
            const auto* Original=Cast<AChaosImpactCharacter>(OriginalClass->GetDefaultObject());
            Test->TestEqual(TEXT("Same initial walking speed as VS"),P->GetCharacterMovement()->MaxWalkSpeed,Original->GetCharacterMovement()->MaxWalkSpeed);
            Test->TestTrue(TEXT("Experience progression slower than four kills"),P->GetRequiredXP()>=20);
            Test->TestEqual(TEXT("No free starter rubies"),P->GetRubies(),0);
            Test->TestEqual(TEXT("Only the permanent normal core on startup"),P->GetBag().Num(),1);
            RPGPrototypeTest::SupplySpecials(P);
            TArray<ARPGPrototypeCharacter*> Enemies;
            for(TActorIterator<ARPGPrototypeSpawnPoint> It(W);It;++It) { It->bEnabled=false; It->DropChance=0; }
            for(TActorIterator<ARPGPrototypeCharacter> It(W);It;++It) { if(It->IsGrunt()) { It->SetTrainingMenuFrozen(true); if(!Cast<ARPGPrototypeBoss>(*It)) { Enemies.Add(*It); } } }
            if(!Enemies.Num()) { Test->AddError(TEXT("No spawned enemies")); return true; }
            P->SetPrototypeAutomatedInput(true);
            FActorSpawnParameters Param; Param.Instigator=P;
            auto* Credit=W->SpawnActor<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),P->GetActorLocation()+FVector(0,0,1000),FRotator::ZeroRotator,Param);
            int32 Before=0; for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It) { ++Before; }
            Enemies[0]->TakeDamage(10000,FDamageEvent(),PC,Credit); Credit->Destroy();
            int32 After=0; for(TActorIterator<ARPGPrototypeLoot> It(W);It;++It) { ++After; }
            Test->TestEqual(TEXT("Zero percent means no first-kill guarantee"),After,Before);
            Test->TestEqual(TEXT("One XP per kill"),P->GetXP(),1); Test->TestTrue(TEXT("Enemy defeat earns rubies even without equipment drop"),P->GetRubies()>0);
            P->SetBagOpen(true); Test->TestFalse(TEXT("Cannot upgrade away from a merchant"),P->UpgradeItem(0)); P->SetBagOpen(false);
            for(TActorIterator<ARPGPrototypeMerchant> It(W);It;++It) { Merchant=*It; break; }
            if(!Test->TestTrue(TEXT("Placed merchant exists"),Merchant.IsValid())) { return true; }
            P->SetActorLocation(Merchant->GetActorLocation()+FVector(210,0,110),false,nullptr,ETeleportType::TeleportPhysics);
            if(!Test->TestTrue(TEXT("Interaction opens merchant"),P->OpenMerchant(Merchant.Get()))) { return true; }
            P->AddRubies(10000); auto Ball=FRPGPrototypeItem::Make(EChaosImpactBallType::Fire,3,ERPGPrototypeRarity::Legend);
            Ball.Intrinsics={{TEXT("FPillar"),32,1},{TEXT("FWalk"),16,1}};
            const auto& Pool=RPGEquipment::Tunes(EChaosImpactBallType::Fire);
            Trail=Pool.IndexOfByPredicate([](const FRPGModDefinition& D){return D.Key==TEXT("FireTrail");});
            if(!Test->TestTrue(TEXT("FireTrail exists regardless of catalog order"),Trail!=INDEX_NONE)) { return true; }
            // Pick a test identity whose random offers contain FireTrail, without adding guaranteed items to real play.
            for(int32 Try=0;Try<100 && !Ball.TuneOfferIndices(0).Contains(Trail);++Try) { Ball.Id=FGuid::NewGuid(); }
            Index=P->GetBag().Num(); P->AddLoot(Ball); P->BagSelection=Index; P->EquipBagItem(Index,1);
            const int32 Cost=Ball.UpgradeCost(), Money=P->GetRubies(); const float Power=Ball.EffectivePower();
            Test->TestTrue(TEXT("Merchant upgrade succeeds"),P->UpgradeItem(Index));
            Test->TestEqual(TEXT("Upgrade spends exact ruby cost"),P->GetRubies(),Money-Cost);
            Test->TestTrue(TEXT("Equipped item updates after strengthening"),P->GetAttack(1).EffectivePower()>Power);
            for(int32 I=0;I<Pool.Num();++I) { if(Pool[I].Key==TEXT("FireTrail")) { Trail=I; } if(Pool[I].Key==TEXT("FireFloor")) { Floor=I; } if(Pool[I].Key==TEXT("Damage")) { Damage=I; } }
            const auto InitialOffers=P->GetTuneOffers(Index,0);
            Test->TestEqual(TEXT("Exactly three random tune offers"),InitialOffers.Num(),3);
            Test->TestTrue(TEXT("Reopening cannot reroll offers for free"),InitialOffers==P->GetTuneOffers(Index,0));
            Test->TestTrue(TEXT("First main tune applied"),P->ApplyTune(Index,Trail,0));
            const float BaseRoll=P->GetBag()[Index].Tunes[0].Roll;
            Test->TestTrue(TEXT("Tune ranked up"),P->UpgradeTune(Index,0));
            Test->TestEqual(TEXT("Rank-up preserves random base value"),P->GetBag()[Index].Tunes[0].Roll,BaseRoll);
            Test->TestTrue(TEXT("Tune rank increases actual effect"),P->GetBag()[Index].Mod(TEXT("FireTrail"))>BaseRoll);
            Test->TestFalse(TEXT("Cannot duplicate the same tune"),P->ApplyTune(Index,Trail,1));
            Test->TestFalse(TEXT("Cannot put exclusive tune in the sub socket"),P->ApplyTune(Index,Trail,2));
            Test->TestFalse(TEXT("Installed tune excluded from the next offers"),P->GetTuneOffers(Index,1).Contains(Trail));
            Floor=P->GetTuneOffers(Index,1)[0];
            Test->TestTrue(TEXT("Second main tune applied"),P->ApplyTune(Index,Floor,1));
            Damage=P->GetTuneOffers(Index,2)[0];
            Test->TestTrue(TEXT("Common tune applied in sub socket"),P->ApplyTune(Index,Damage,2));
            P->BagPage=1; P->NoticeUntil=0; Shot(TEXT("shop-upgrade.png"));
            Stage=1; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==1)
        {
            P->BagPage=2; P->TuneSocket=0; P->TuneSelection=0; P->NoticeUntil=0; Shot(TEXT("shop-tune.png"));
            Stage=2; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==2)
        {
            Test->TestFalse(TEXT("Replacing tune asks confirmation"),P->ApplyTune(Index,Floor,0));
            // Duplicate checks reject floor because it is already in slot2. Pick another exclusive tune.
            P->ApplyTune(Index,P->GetTuneOffers(Index,0)[0],0); Shot(TEXT("shop-confirm.png"));
            Stage=20; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==20)
        {
            P->HandleBagKey(EKeys::Escape); P->PendingTune=INDEX_NONE;
            auto Rare=FRPGPrototypeItem::Make(EChaosImpactBallType::Ice,1,ERPGPrototypeRarity::Rare); Rare.Enhancement=10;
            const int32 R=P->GetBag().Num(); P->AddLoot(Rare); Test->TestFalse(TEXT("Rare cannot strengthen beyond +10"),P->UpgradeItem(R));
            auto Legendary=FRPGPrototypeItem::Make(EChaosImpactBallType::Beam,1,ERPGPrototypeRarity::Legend); Legendary.Enhancement=10;
            const int32 L=P->GetBag().Num(); P->AddLoot(Legendary); Test->TestTrue(TEXT("Legend can strengthen to +11"),P->UpgradeItem(L));
            const int32 Life=P->GetBag().Num(); auto B=FRPGPrototypeItem::MakeBadge(ERPGItemKind::LifeBadge,2,ERPGPrototypeRarity::Legend,0); P->AddLoot(B);
            const float HP=P->GetHealth(); Test->TestTrue(TEXT("Life badge equips to its own slot"),P->EquipBagItem(Life,1));
            Test->TestTrue(TEXT("Life badge increases max HP"),P->GetMaxHealth()>100);
            Test->TestEqual(TEXT("Badge swap does not heal"),P->GetHealth(),HP);
            P->AddLoot(FRPGPrototypeItem::MakeBadge(ERPGItemKind::MobilityBadge,1,ERPGPrototypeRarity::Unique,0)); P->EquipBagItem(P->GetBag().Num()-1,1);
            P->AddLoot(FRPGPrototypeItem::MakeBadge(ERPGItemKind::TechniqueBadge,1,ERPGPrototypeRarity::Legend,0)); P->EquipBagItem(P->GetBag().Num()-1,1);
            Test->TestTrue(TEXT("Three independent badge slots"),P->GetBadge(0).IsValid() && P->GetBadge(1).IsValid() && P->GetBadge(2).IsValid());
            P->BagPage=0; P->BagSelection=Life; P->BagFilter=2; P->NoticeUntil=0; Shot(TEXT("bag-badges.png"));
            Stage=3; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==3)
        {
            Test->TestFalse(TEXT("Equipped badge cannot be dismantled"),P->SalvageItem(P->BagSelection));
            const int32 Spare=P->GetBag().Num(); auto Item=FRPGPrototypeItem::Make(EChaosImpactBallType::Black,3,ERPGPrototypeRarity::Unique); P->AddLoot(Item);
            Test->TestFalse(TEXT("Salvage requires confirmation"),P->SalvageItem(Spare)); const int32 Money=P->GetRubies();
            P->HandleBagKey(EKeys::Enter); Test->TestEqual(TEXT("Salvage grants exact rarity/level value"),P->GetRubies(),Money+Item.SalvageValue());
            P->SetBagOpen(false); P->SelectAttack(1); P->SetAIAimDirection(FVector::ForwardVector);
            const FTransform Where(FRotator::ZeroRotator,P->GetActorLocation()+FVector(330,0,0));
            auto* T=W->SpawnActorDeferred<ARPGPrototypeCharacter>(P->GetClass(),Where,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            T->SetEnemy(true); T->EnemyHP=1000; T->FinishSpawning(Where); T->SetTrainingMenuFrozen(true); Target=T; BeforeHP=T->GetHealth();
            FActorSpawnParameters Param; Param.Instigator=P;
            auto* Ball=W->SpawnActor<ARPGPrototypeBall>(ARPGPrototypeBall::StaticClass(),P->GetActorLocation()+FVector(60,0,40),FRotator::ZeroRotator,Param);
            Ball->Launch(FVector::ForwardVector,1300,EChaosImpactBallFlightMode::Straight);
            Stage=4; Next=W->GetTimeSeconds()+.8; return false;
        }
        if(Stage==4)
        {
            Test->TestTrue(TEXT("Strengthened tuned throw deals actual RPG damage"),Target.IsValid() && Target->GetHealth()<BeforeHP);
            int32 Fields=0,Shared=0;
            for(TActorIterator<ARPGPrototypeEffect> It(W);It;++It) { ++Fields; }
            for(TActorIterator<AChaosImpactHazardZone> It(W);It;++It) { Shared+=!It->IsVisualOnly(); }
            Test->TestTrue(TEXT("Fire trail / floor are RPG-specific fields"),Fields>0); Test->TestEqual(TEXT("No VS hazard rules leak into RPG"),Shared,0);
            P->SetBagOpen(true); P->BagFilter=0; P->BagSelection=Index; P->NoticeUntil=0; Shot(TEXT("bag-tuned.png"));
            Stage=5; Next=W->GetTimeSeconds()+.4; return false;
        }
        if(Stage==5)
        {
            P->SetBagOpen(false);
            auto Bird=FRPGPrototypeItem::Make(EChaosImpactBallType::Simae,1,ERPGPrototypeRarity::Legend);
            Bird.Tunes={{TEXT("BirdCount"),8,3},{TEXT("BirdPeck"),2,1},{TEXT("Damage"),12,1}};
            BeforeHP=Target->GetHealth(); BirdField=ARPGPrototypeEffect::Create(P,Bird,TEXT("Birds"),Target->GetActorLocation(),550,3,1);
            for(auto Type:RPGEquipment::BallRoster())
            {
                if(Type==EChaosImpactBallType::Normal || Type==EChaosImpactBallType::Simae) { continue; }
                auto I=FRPGPrototypeItem::Make(Type,1,ERPGPrototypeRarity::Unique);
                ARPGPrototypeEffect::Create(P,I,Type==EChaosImpactBallType::Black?TEXT("Gravity"):Type==EChaosImpactBallType::Wind?TEXT("Wind"):
                    Type==EChaosImpactBallType::Smoke?TEXT("Smoke"):Type==EChaosImpactBallType::Ice?TEXT("Cold"):TEXT("Spark"),P->GetActorLocation()+FVector(450,0,0),170,1,.1f);
            }
            P->NoticeUntil=0; Shot(TEXT("combat-effects.png")); Stage=6; Next=W->GetTimeSeconds()+2; return false;
        }
        Test->TestTrue(TEXT("Distributed bird swarm can damage a target"),Target.IsValid() && Target->GetHealth()<BeforeHP-5);
        Test->TestTrue(TEXT("Birds resolve multiple sequential pecks"),BirdField.IsValid() && BirdField->GetPeckCount()>2);
        P->SetPrototypeAutomatedInput(false); return true;
    }
private:
    FAutomationTestBase* Test;
    double Started=FPlatformTime::Seconds(),Next=0;
    int32 Stage=0,Index=0,Trail=0,Floor=0,Damage=0;
    bool Travel=false;
    float BeforeHP=0;
    TWeakObjectPtr<ARPGPrototypeMerchant> Merchant;
    TWeakObjectPtr<ARPGPrototypeCharacter> Target;
    TWeakObjectPtr<ARPGPrototypeEffect> BirdField;
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentPlayTest,"ChaosImpact.RPGPrototype.EquipmentPlay",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentPlayTest::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FRPGEquipmentPlayCommand(this)); return true; }
#endif
