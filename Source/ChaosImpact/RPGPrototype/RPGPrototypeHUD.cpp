#include "RPGPrototypeHUD.h"
#include "RPGPrototype.h"
#include "RPGPrototypeHUDStyle.h"
#include "RPGPrototypeUIKit.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactSettings.h"
#include "ChaosImpactShoulderCameraComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "RPGAdventure.h"
#include "RPGTreasureChest.h"
#include "RPGSnowField.h"
#include "RPGOnline.h"
#include "ChaosImpactSimaeBoss.h"

namespace
{
using namespace ChaosImpactPaint;
const TCHAR* RarityName(ERPGPrototypeRarity Rarity)
{
    switch(Rarity) { case ERPGPrototypeRarity::Rare:return TEXT("レア"); case ERPGPrototypeRarity::Unique:return TEXT("ユニーク");
    case ERPGPrototypeRarity::Legend:return TEXT("レジェンド"); default:return TEXT("ノーマル"); }
}
FLinearColor RarityColor(ERPGPrototypeRarity Rarity)
{
    switch(Rarity) { case ERPGPrototypeRarity::Rare:return Ice; case ERPGPrototypeRarity::Unique:return Violet;
    case ERPGPrototypeRarity::Legend:return Gold; default:return Paper; }
}
void Card(const FPainter& P,float X,float Y,float W,float H,const FLinearColor& Accent,bool bSelected=false)
{
    const FHudFill Fill=bSelected ? MakeFill(FLinearColor(.025f,.07f,.13f,1),FLinearColor(.008f,.018f,.04f,1))
        : MakeFill(FLinearColor(.012f,.02f,.04f,.96f),FLinearColor(.003f,.006f,.014f,.96f));
    HudShard(P,{FVector2D(X+10,Y),FVector2D(X+W,Y),FVector2D(X+W-10,Y+H),FVector2D(X,Y+H)},Fill,3,FVector2D(3,4));
    HudAbove(P).Line(FVector2D(X+11,Y+2),FVector2D(X+W-2,Y+2),Accent,2);
}
void Keycap(const FPainter& P,FVector2D At,const FString& Text)
{
    HudShard(P,{At+FVector2D(-13,-13),At+FVector2D(15,-13),At+FVector2D(11,14),At+FVector2D(-17,14)},SilverFill,2,FVector2D(2,3));
    HudAbove(P).Text(Text,At.X-1,At.Y-11,18,Navy,ETextAlign::Center);
}
FVector2D AttackCenter(float W,float H,int32 Index) { return FVector2D(W-350+Index*124,H-104); }
/** "[E] Label" centered at Y: the interaction prompt shared by merchant, retry, chests and quest objects. */
void InteractionPrompt(const FGeometry& Design,FSlateWindowElementList& Elements,int32 Layer,float W,float Y,const FString& Key,const FString& Label,const FLinearColor& Accent)
{
    RPGPrototypeUI::FCanvas C; C.Geometry=&Design; C.Elements=&Elements; C.Layer=Layer;
    const float KeyW=FMath::Max(34.0f,RPGPrototypeUI::Measure(Key,17)+20),Width=FMath::Max(260.0f,KeyW+RPGPrototypeUI::Measure(Label,22)+64);
    const float X=W*.5f-Width*.5f;
    RPGPrototypeUI::Panel(C,0,X,Y,Width,54,RPGPrototypeUI::EStyle::Card,Accent,Accent);
    RPGPrototypeUI::Keycap(C,0,Key,X+26,Y+12,30);
    RPGPrototypeUI::Label(C,0,Label,X+26+KeyW+14,Y+12,22,RPGPrototypeUI::TextMain);
}
float BattleScale(FVector2D Size,bool Split) { return FMath::Min(Size.X/(Split?960.0f:1280.0f),Size.Y/(Split?600.0f:720.0f)); }
}

void URPGPrototypeHUD::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    SetClipping(EWidgetClipping::ClipToBoundsAlways);
    ForceVolatile(true);
    if(WidgetTree && !WidgetTree->RootWidget)
    {
        WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("RPGHUDRoot"));
        WidgetTree->RootWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
}
void URPGPrototypeHUD::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    if(!Player) { return; }
    // An item stops being "NEW" once it has been selected in the bag for a moment (not on a quick scroll past).
    if(Player->IsBagOpen() && Player->BagPage!=ERPGBagPage::Settings)
    {
        const int32 Shown=Player->BagPage==ERPGBagPage::Equipment && !Player->bChoosingEquipment?INDEX_NONE:Player->BagSelection;
        if(Shown!=SeenSelection) { SeenSelection=Shown; SeenSince=GetWorld()->GetTimeSeconds(); }
        else if(Shown!=INDEX_NONE && GetWorld()->GetTimeSeconds()-SeenSince>.6) { Player->MarkSeen(Shown); }
    }
    else { SeenSelection=INDEX_NONE; }
    const bool MiniOpen=Player->IsBagOpen() && Player->UsesMiniMenu();
    MiniRevealAlpha=FMath::FInterpConstantTo(MiniRevealAlpha,MiniOpen?1.0f:0.0f,DeltaSeconds,MiniOpen?4.5f:5.5f);
    if(MiniOpen) { LastMiniPage=Player->BagPage; LastMiniMerchant=Player->IsAtMerchant(); }
    QuestRevealAlpha=FMath::FInterpConstantTo(QuestRevealAlpha,Player->IsQuestMenuOpen()?1.0f:0.0f,DeltaSeconds,Player->IsQuestMenuOpen()?4.5f:5.5f);
    const double Now=GetWorld()->GetTimeSeconds();
    const float HP=Player->GetHealth();
    if(PreviousHP<0) { TrailingHP=HP; }
    else if(HP<PreviousHP) { HitAt=Now; }
    if(Now-HitAt>.25) { TrailingHP=FMath::FInterpTo(TrailingHP,HP,DeltaSeconds,7); }
    if(HP>TrailingHP) { TrailingHP=HP; }
    PreviousHP=HP;
    if(PreviousSelection!=Player->GetSelectedAttack()) { PreviousSelection=Player->GetSelectedAttack(); SelectionAt=Now; }
    for(int32 Index=0;Index<3;++Index)
    {
        const float Left=Player->GetCooldownLeft(Index);
        if(PreviousCooldown[Index]>0 && Left<=0) { ReadyFlashAt[Index]=Now; }
        PreviousCooldown[Index]=Left;
    }
}
int32 URPGPrototypeHUD::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    using namespace ChaosImpactPaint;
    const int32 Base=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,bEnabled);
    const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    if(!Player || !PC || !PC->IsGameplayActive()) { return Base; }
    const double Now=GetWorld()->GetTimeSeconds();
    const float Time=FMath::Fmod(Now,3600.0);
    const FVector2D Size=Geometry.GetLocalSize();
    if(const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();Mode && Mode->UsesSharedScreen())
    {
        PaintSharedHUD(Geometry,Elements,Base+3,Time);
        if(MiniRevealAlpha>0) { PaintMiniMenu(Geometry,Elements,Base+25,Time); }
        if(Player->IsQuestMenuOpen()) { PaintAdventure(Geometry,Elements,Base+30,Time); }
        return Base+49;
    }
    const bool Split=GetGameInstance() && GetGameInstance()->GetLocalPlayers().Num()>1;
    const float Scale=BattleScale(Size,Split);
    if(Scale<=0) { return Base; }
    const float W=Size.X/Scale,H=Size.Y/Scale;
    const FGeometry Design=MakeAnchor(Geometry,0,0,Scale);
    FPainter P{Design,Elements,Base+3};
    FPainter Mid{Design,Elements,Base+7};
    FPainter Front{Design,Elements,Base+11};
    FPainter WorldPaint{Geometry,Elements,Base+2};
    const auto Project=[&](const FVector& At,FVector2D& Screen)
    { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),At,Screen,true); };
    if(Split && GetOwningLocalPlayer())
    {
        const auto* Local=GetOwningLocalPlayer();
        if(Local->Origin.X+Local->Size.X<.99f) { WorldPaint.Box(Size.X-2,0,2,Size.Y,Ink); }
        if(Local->Origin.Y+Local->Size.Y<.99f) { WorldPaint.Box(0,Size.Y-2,Size.X,2,Ink); }
    }
    if(!Player->IsBagOpen() || Player->UsesMiniMenu())
    {
        for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It)
        {
            const auto* Boss=*It;
            const float Distance=FVector::Dist2D(Player->GetActorLocation(),Boss->GetActorLocation());
            const auto State=Boss->GetBossState();
            if(State==ERPGBossState::Defeated)
            {
                if(Boss->CanRetry(Player))
                {
                    InteractionPrompt(Design,Elements,Base+3,W,H-280,PC->IsUsingGamepad()?TEXT("上ボタン"):TEXT("E"),FString::Printf(TEXT("Lv.%d に再挑戦"),Boss->GetRPGLevel()),Gold);
                }
                continue;
            }
            if(Boss->IsBossActive() || (Distance<1500 && !Player->IsEliminated()))
            {
                const float X=W*.5f-210,Y=Split?80:14;
                Card(P,X,Y,420,62,Boss->IsEnraged()?Fire:Ice);
                Front.Text(TEXT("暴走マグロ"),X+16,Y+8,21,Paper,ETextAlign::Left,TEXT("Bold"));
                Front.Text(FString::Printf(TEXT("Lv.%d"),Boss->GetRPGLevel()),X+176,Y+12,16,Paper,ETextAlign::Left,TEXT("Bold"));
                Front.Text(FString::Printf(TEXT("%.0f / %.0f"),Boss->GetHealth(),Boss->GetMaxHealth()),X+404,Y+12,15,Paper,ETextAlign::Right,TEXT("Bold"));
                Mid.Box(X+16,Y+38,388,10,Ink);
                Front.Box(X+16,Y+38,388*FMath::Clamp(Boss->GetHealth()/Boss->GetMaxHealth(),0.0f,1.0f),10,State==ERPGBossState::Recovery?Gold:Fire);
            }
            const FVector Floor=Boss->GetActorLocation()-FVector(0,0,Boss->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-5);
            if(State==ERPGBossState::ChargeTell)
            {
                const FVector D=Boss->GetTellDirection(),Side=FVector::CrossProduct(FVector::UpVector,D)*100;
                FVector End=Floor+D*1200;
                FHitResult Wall; FCollisionQueryParams Q(SCENE_QUERY_STAT(RPGBossTell),false,Boss);
                if(GetWorld()->LineTraceSingleByObjectType(Wall,Floor+FVector(0,0,65),End+FVector(0,0,65),FCollisionObjectQueryParams(ECC_WorldStatic),Q)) { End=Wall.ImpactPoint-FVector(0,0,65); }
                TArray<FVector2D> Points; bool Visible=true;
                for(const FVector V:{Floor-Side,End-Side,End+Side,Floor+Side})
                { FVector2D Screen; if(!Project(V,Screen)) { Visible=false; break; } Points.Add(Screen); }
                if(Visible)
                { HudPoly(WorldPaint,Points,SolidFill(FLinearColor(1,.07f,.05f,.3f)),1); for(int32 I=0;I<4;++I) { if(FVector2D::DistSquared(Points[I],Points[(I+1)%4])>.01) { WorldPaint.Line(Points[I],Points[(I+1)%4],Fire,3*Scale); } } }
            }
            if(State==ERPGBossState::LeapTell || State==ERPGBossState::Wave)
            {
                const float Radius=State==ERPGBossState::Wave?Boss->GetWaveRadius():1000;
                FVector2D Previous=FVector2D::ZeroVector; bool HasPrevious=false;
                // The first wave frame has radius zero. Empty line batches can assert in Slate's renderer.
                for(int32 I=0;Radius>1 && I<=64;++I)
                {
                    const float A=2*PI*I/64; FVector2D Screen=FVector2D::ZeroVector;
                    const bool Visible=Project(Floor+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius,Screen);
                    if(Visible && HasPrevious && FVector2D::DistSquared(Previous,Screen)>.01) { WorldPaint.Line(Previous,Screen,State==ERPGBossState::Wave?Gold:Fire,State==ERPGBossState::Wave?6*Scale:2*Scale); }
                    Previous=Screen; HasPrevious=Visible;
                }
            }
        }
        for(TActorIterator<ARPGSimaeBody> It(GetWorld());It;++It)
        {
            if(!It->IsAwake() || FVector::Dist2D(Player->GetActorLocation(),It->GetActorLocation())>4500) { continue; }
            const float X=W*.5f-230,Y=Split?80:14;
            const bool bPower=It->IsBossPoweringUp();
            const FLinearColor BarColor=It->IsBossFullPower()?FLinearColor(1,.12f,.18f):Ice;
            Card(P,X,Y,460,62,It->IsBossFullPower()?Fire:Ice);
            Front.Text(TEXT("シマエナガ"),X+16,Y+8,21,Paper,ETextAlign::Left,TEXT("Bold"));
            Front.Text(FString::Printf(TEXT("Lv.%d"),It->GetRPGLevel()),X+190,Y+12,16,Paper,ETextAlign::Left,TEXT("Bold"));
            Front.Text(FString::Printf(TEXT("%.0f / %.0f"),It->GetHealth(),It->GetMaxHealth()),X+444,Y+12,15,Paper,ETextAlign::Right,TEXT("Bold"));
            Mid.Box(X+16,Y+38,428,10,Ink);
            FPainter Bar=Front; Bar.Alpha=bPower?.55f+.45f*FMath::Sin(Time*14):1;
            Bar.Box(X+16,Y+38,428*FMath::Clamp(It->GetHealth()/It->GetMaxHealth(),0.0f,1.0f),10,BarColor);
        }
        for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
        {
            if(Cast<ARPGPrototypeBoss>(*It) || It->IsCrowdControlImmune() || !It->IsGrunt() || It->IsEliminated() || It->GetLastDamagedAt()<=0 || Now-It->GetLastDamagedAt()>4) { continue; }
            FVector2D At;
            if(!Project(It->GetActorLocation()+FVector(0,0,112),At)) { continue; }
            const double L=At.X-48*Scale,R=At.X+48*Scale,Y=At.Y;
            const TArray<FVector2D> Shape{FVector2D(L+3*Scale,Y),FVector2D(R,Y),
                FVector2D(R-3*Scale,Y+8*Scale),FVector2D(L,Y+8*Scale)};
            HudShard(WorldPaint,Shape,SolidFill(Ink),2*Scale,FVector2D(2*Scale,2*Scale));
            const FPainter Above=HudAbove(WorldPaint);
            HudPoly(Above,HudClip(Shape,L+96*Scale*It->GetHealth()/It->GetMaxHealth(),false,true),FireFill,3);
            Above.Text(FString::Printf(TEXT("%.0f / %.0f"),It->GetHealth(),It->GetMaxHealth()),
                At.X,Y-22*Scale,15*Scale,Paper,ETextAlign::Center,TEXT("Bold"),2);
        }
        const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); const auto* Relay=ARPGNetRelay::Get(GetWorld());
        if(const TArray<FRPGPrototypeDamageText>* Texts=Mode?&Mode->DamageTexts:Relay?&Relay->DamageTexts:nullptr)
        {
            for(const auto& Damage:*Texts)
            {
                const float Age=Now-Damage.StartedAt;
                FVector2D At;
                if(!Project(Damage.Location+FVector(0,0,Age*65),At)) { continue; }
                FPainter Fade=WorldPaint; Fade.Layer=Base+9; Fade.Alpha=FMath::Clamp((1.15f-Age)*3,0.0f,1.0f);
                HudText3D(Fade,FString::Printf(TEXT("%.0f"),Damage.Amount),At.X+15*Scale,At.Y,
                    (Damage.Amount>=25?35:29)*Scale,Damage.bPlayer?FireFill.Top:Damage.Amount>=25?Gold:Paper,ETextAlign::Center);
            }
        }
        if(!Player->IsEliminated() && Player->IsChargingThrow() && !Player->IsShoulderViewActive())
        {
            FVector2D A,B;
            const FVector Start=Player->GetAimGuideStartWorldLocation();
            if(Project(Start,A) && Project(Start+Player->GetAimDirection().GetSafeNormal2D()*Player->GetAimGuideLength(),B))
            {
                const FVector2D Dir=(B-A).GetSafeNormal(),Side(-Dir.Y,Dir.X);
                const FLinearColor Arrow(.12f,.93f,1,.94f);
                WorldPaint.Line(A,B,Arrow,6);
                const float L=42*FMath::Cos(FMath::DegreesToRadians(35.0f)),S=42*FMath::Sin(FMath::DegreesToRadians(35.0f));
                WorldPaint.Line(B,B-Dir*L+Side*S,Arrow,7);
                WorldPaint.Line(B,B-Dir*L-Side*S,Arrow,7);
            }
        }
        if(!Player->IsEliminated())
        {
            FVector2D At=FVector2D::ZeroVector;
            bool Draw=false;
            if(Player->IsShoulderViewActive())
            {
                FVector View; FRotator Rotation; PC->GetPlayerViewPoint(View,Rotation);
                const FVector End=UChaosImpactShoulderCameraComponent::MakeReticleAnchor(View,Rotation,Player->GetActorLocation().Z+54);
                Draw=Project(End,At); At.X=Size.X*.5; At.Y=FMath::Clamp(At.Y,24.0,Size.Y-24.0);
            }
            else if(!PC->IsUsingGamepad() && FSlateApplication::IsInitialized())
            { At=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()); Draw=FBox2D(FVector2D::ZeroVector,Size).IsInside(At); }
            if(Draw)
            {
                WorldPaint.Ring(At,8*Scale,Ink,5*Scale); WorldPaint.Ring(At,8*Scale,Paper,2*Scale);
                WorldPaint.Line(At-FVector2D(13*Scale,0),At+FVector2D(13*Scale,0),Ink,5*Scale);
                WorldPaint.Line(At-FVector2D(13*Scale,0),At+FVector2D(13*Scale,0),Paper,2*Scale);
                WorldPaint.Disc(At,2*Scale,Ice);
                if(Player->IsChargingThrow()) { WorldPaint.Arc(At,13*Scale,-90,-90+360*Player->GetThrowChargeAlpha(),Gold,3*Scale); }
            }
        }
        Card(P,W-300,18,132,42,Gold);
        Front.Text(PC->IsUsingGamepad()?TEXT("－ バッグ"):TEXT("バッグ Tab"),W-234,27,19,Paper,ETextAlign::Center,TEXT("Bold"));
        if(Player->GetUnseenCount()>0)
        {
            RPGPrototypeUI::FCanvas Badge; Badge.Geometry=&Design; Badge.Elements=&Elements; Badge.Layer=Base+3;
            RPGPrototypeUI::Chip(Badge,1,FString::Printf(TEXT("NEW %d"),Player->GetUnseenCount()),W-170,8,RPGPrototypeUI::NewColor,11,true,ETextAlign::Right);
        }
        Card(P,W-150,18,128,42,Ice);
        Front.Text(PC->IsUsingGamepad()?TEXT("＋ ポーズ"):TEXT("ポーズ P"),W-86,27,19,Paper,ETextAlign::Center,TEXT("Bold"));
        if(!PC->IsUsingGamepad())
        {
            Card(P,W-162,H-238,136,40,Ice);
            Front.Text(TEXT("ダッシュ"),W-94,H-231,21,Paper,ETextAlign::Center,TEXT("Bold"));
        }

        // VS fire diamond + ice blade. HP is continuous here, not the main game's three cells.
        const float HitAge=Now-HitAt;
        const float Shake=HitAge<.3f?FMath::Sin(HitAge*70)*(.3f-HitAge)*13:0;
        const FVector2D Gem(79+Shake,H-94);
        const bool LowHP=Player->GetHealth()/Player->GetMaxHealth()<.25f;
        HudShard(P,{Gem+FVector2D(6,-47),Gem+FVector2D(46,-2),Gem+FVector2D(-6,45),Gem+FVector2D(-46,2)},FireFill,4,FVector2D(6,7));
        Mid.Line(Gem+FVector2D(-32,-2),Gem+FVector2D(4,-36),WithAlpha(Paper,.8f),2);
        HudText3D(Front,TEXT("Lv."),Gem.X,Gem.Y-30,16,Paper,ETextAlign::Center);
        HudText3D(Front,FString::FromInt(Player->GetRPGLevel()),Gem.X,Gem.Y-11,35,Paper,ETextAlign::Center);
        HudText3D(Front,TEXT("HP"),143,H-163,18,LowHP?FireFill.Top:IceFill.Top);
        // Down (waiting for help): shown as 0 with an empty bar.
        const float ShownHP=Player->IsDowned()?0.0f:Player->GetHealth();
        HudText3D(Front,FString::Printf(TEXT("%.0f"),ShownHP),178,H-174,37,Paper);
        Front.Text(FString::Printf(TEXT("/ %.0f"),Player->GetMaxHealth()),
            192+HudMeasure(FString::Printf(TEXT("%.0f"),ShownHP),37,TEXT("BlackItalic")),H-153,19,Paper,ETextAlign::Left,TEXT("Bold"),2);
        const TArray<FVector2D> HPBlade{FVector2D(140,H-126),FVector2D(391,H-126),FVector2D(405,H-111),
            FVector2D(389,H-96),FVector2D(134,H-96)};
        HudShard(P,HPBlade,SolidFill(Ink),3,FVector2D(4,5));
        const float Ratio=FMath::Clamp(ShownHP/Player->GetMaxHealth(),0.0f,1.0f);
        HudPoly(Mid,HudClip(HPBlade,134+271*FMath::Clamp(TrailingHP/Player->GetMaxHealth(),0.0f,1.0f),false,true),FireFill,4);
        HudPoly(Front,HudClip(HPBlade,134+271*Ratio,false,true),LowHP?FireFill:IceFill,5);
        Front.Line(FVector2D(145,H-123),FVector2D(134+271*Ratio,H-123),WithAlpha(Paper,.8f),2);
        if(HitAge>=0 && HitAge<.65f)
        { HudShatter(Front,FVector2D(134+271*Ratio,H-111),HitAge/.65f,13,IceFill,7,3); }
        const TArray<FVector2D> STBlade{FVector2D(137,H-81),FVector2D(384,H-81),FVector2D(400,H-75),FVector2D(384,H-69),FVector2D(131,H-69)};
        HudShard(P,STBlade,SolidFill(Ink),2,FVector2D(3,4));
        HudPoly(Mid,HudClip(STBlade,131+269*Player->GetStamina()/Player->GetMaxStamina(),false,true),FireFill,3);
        for(int32 Index=1;Index<5;++Index)
        { Mid.Line(FVector2D(131+269*Index/5.0+2,H-81),FVector2D(131+269*Index/5.0-1,H-69),Navy,3); }
        HudText3D(Front,TEXT("ST"),408,H-85,15,FireFill.Top);
        Front.Box(139,H-48,247,3,WithAlpha(Ink,.9f));
        Front.Box(139,H-48,247*Player->GetXP()/FMath::Max(1,Player->GetRequiredXP()),3,Ice);
        Front.Text(FString::Printf(TEXT("XP  %d / %d"),Player->GetXP(),Player->GetRequiredXP()),390,H-39,14,Paper,ETextAlign::Right,TEXT("Bold"),2);

        for(int32 AttackSlot=0;AttackSlot<3;++AttackSlot)
        {
            const auto& Item=Player->GetAttack(AttackSlot);
            const bool Selected=Player->GetSelectedAttack()==AttackSlot;
            const float Left=Player->GetCooldownLeft(AttackSlot);
            const float Kick=Selected?FMath::Exp(-12.0f*FMath::Max(0.0,Now-SelectionAt))*.12f:0;
            const FVector2D At=AttackCenter(W,H,AttackSlot);
            const float Radius=(Selected?48.0f:39.0f)*(1+Kick);
            if(!Item.IsValid())
            {
                Mid.Disc(At+FVector2D(4,5),Radius+4,Extrude); Mid.Disc(At,Radius+3,Navy);
                Mid.Disc(At,Radius,FLinearColor(.014f,.027f,.06f,1));
                Front.Ring(At,Radius-3,Muted,2);
                Front.Text(TEXT("—"),At.X,At.Y-20,29,Muted,ETextAlign::Center,TEXT("Bold"));
                HudText3D(Front,FString::Printf(TEXT("特殊 %d"),AttackSlot),At.X,At.Y-83,17,Paper,ETextAlign::Center,TEXT("Black"));
                Front.Text(TEXT("未装備"),At.X,At.Y+51,15,Muted,ETextAlign::Center,TEXT("Bold"));
                continue;
            }
            const FLinearColor Accent=ChaosImpactBallTypes::GetColor(Item.Type);
            if(Selected) { HudBurst(P,At,Radius+1,Radius+14,10,Left>0?NightFill:FireFill,Time*8,FVector2D(1,1),3,FVector2D(5,6),AttackSlot+2); }
            Mid.Disc(At+FVector2D(4,5),Radius+4,Extrude); Mid.Disc(At,Radius+3,Navy);
            Mid.Disc(At,Radius,FLinearColor(.014f,.027f,.06f,1));
            for(int32 Segment=0;Segment<6;++Segment)
            { Front.Arc(At,Radius-3,Time*24+Segment*60,Time*24+Segment*60+49,Left>0?Muted:Accent,3); }
            FPainter BallPaint=Front; BallPaint.Alpha=Left>0?.32f:1;
            RPGPrototypePaint::Ball(BallPaint,At,Selected?32:27,Item.Type,Time);
            if(Left>0)
            {
                Front.Arc(At,Radius-7,-90,-90+360*(1-FMath::Clamp(Left/Item.EffectiveCooldown(),0.0f,1.0f)),IceFill.Top,4);
                HudText3D(Front,FString::Printf(TEXT("%.1f"),Left),At.X,At.Y-18,29,Paper,ETextAlign::Center);
            }
            const float FlashAge=Now-ReadyFlashAt[AttackSlot];
            if(FlashAge>=0 && FlashAge<.55f)
            {
                FPainter Flash=Front; Flash.Alpha=1-FlashAge/.55f;
                Flash.Ring(At,Radius+FlashAge*35,IceFill.Top,4);
            }
            HudText3D(Front,AttackSlot==0?TEXT("通常球"):FString::Printf(TEXT("特殊 %d"),AttackSlot),
                At.X,At.Y-83,Selected?19:17,Selected?FireFill.Top:Paper,ETextAlign::Center,TEXT("Black"));
            Card(P,At.X-43,At.Y+49,86,24,Accent);
            Front.Text(Item.Name(),At.X,At.Y+51,15,Paper,ETextAlign::Center,TEXT("Bold"));
            Front.Text(FString::Printf(TEXT("パワー %.0f"),Player->GetDisplayedPower(Item)),At.X,At.Y+78,14,Gold,ETextAlign::Center,TEXT("Bold"),2);
            if(!PC->IsUsingGamepad()) { Keycap(Front,At+FVector2D(Radius-5,-Radius+3),FString::FromInt(AttackSlot+1)); }
        }
        const FString Swap=ChaosImpactSettings::DescribeAction(PC,EChaosImpactAction::SwapBall);
        Front.Text(Swap+TEXT(" 切替"),W-350,H-215,15,Paper,ETextAlign::Center,TEXT("Bold"),2);
        if(Player->IsChargingThrow())
        {
            const float Alpha=Player->GetThrowChargeAlpha();
            HudText3D(Front,TEXT("チャージ"),W*.5f-125,H-95,17,Paper,ETextAlign::Left,TEXT("Black"));
            HudText3D(Front,FString::Printf(TEXT("%.0f%%"),Alpha*100),W*.5f+130,H-102,25,Alpha>=.99f?FireFill.Top:Paper,ETextAlign::Right);
            const TArray<FVector2D> Charge{FVector2D(W*.5f-135,H-65),FVector2D(W*.5f+130,H-65),
                FVector2D(W*.5f+140,H-55),FVector2D(W*.5f+130,H-45),FVector2D(W*.5f-140,H-45)};
            HudShard(P,Charge,SolidFill(Ink),3,FVector2D(3,4));
            HudPoly(Mid,HudClip(Charge,W*.5f-140+280*Alpha,false,true),Alpha>=.99f?FireFill:IceFill,5);
        }
        if(auto* Loot=Player->FindNearbyLoot();Loot && !Player->IsChargingThrow() && !Player->IsBagOpen())
        {
            // What it is and whether it beats what is equipped, before picking it up.
            namespace UI=RPGPrototypeUI;
            const auto& I=Loot->Item; const float X=W*.5f-215,Y=H-182,CardW=430;
            UI::FCanvas C; C.Geometry=&Design; C.Elements=&Elements; C.Layer=Base+3; C.Time=Time;
            UI::Panel(C,0,X,Y,CardW,82,UI::EStyle::Card,UI::RarityColor(I.Rarity),UI::RarityColor(I.Rarity));
            UI::ItemIcon(C,0,FVector2D(X+46,Y+41),22,I);
            UI::Label(C,0,UI::Fit(I.Name(),220,21),X+86,Y+8,21,I.Rarity==ERPGPrototypeRarity::Normal?UI::TextMain:UI::RarityColor(I.Rarity));
            float ChipX=X+86;
            ChipX+=UI::Chip(C,0,UI::RarityName(I.Rarity),ChipX,Y+44,UI::RarityColor(I.Rarity),12,true)+5;
            ChipX+=UI::Chip(C,0,FString::Printf(TEXT("Lv.%d"),I.Level),ChipX,Y+44,UI::TextSub,12)+5;
            if(I.Kind==ERPGItemKind::Ball)
            {
                const float Power=Player->GetDisplayedPower(I);
                const int32 EquipSlot=UI::SlotForItem(I);
                const auto& A=Player->GetEquipment(EquipSlot); const auto& B=Player->GetEquipment(EquipSlot==1?2:EquipSlot);
                const auto& Weaker=!A.IsValid()?A:!B.IsValid()?B:Player->GetDisplayedPower(A)<=Player->GetDisplayedPower(B)?A:B;
                ChipX+=UI::Chip(C,0,FString::Printf(TEXT("パワー %.0f"),Power),ChipX,Y+44,UI::Highlight,12)+5;
                if(Weaker.IsValid()) { UI::Delta(C,0,Power-Player->GetDisplayedPower(Weaker),ChipX+2,Y+44,14); }
                else { UI::Label(C,0,TEXT("空き枠あり"),ChipX+2,Y+44,13,UI::Good); }
            }
            else { UI::Chip(C,0,RPGEquipment::KindName(I.Kind),ChipX,Y+44,UI::TextSub,12); }
            UI::Keycap(C,0,PC->IsUsingGamepad()?TEXT("↑"):TEXT("F"),X+CardW-92,Y+14,30);
            UI::Label(C,0,TEXT("拾う"),X+CardW-26,Y+18,19,UI::TextMain,ETextAlign::Right);
        }
        {
            // Under the buttons, right-aligned: clear of the boss bar in the middle even on a split view.
            RPGPrototypeUI::FCanvas Ruby; Ruby.Geometry=&Design; Ruby.Elements=&Elements; Ruby.Layer=Base+3;
            RPGPrototypeUI::Rubies(Ruby,1,Player->GetRubies(),W-24,70,22,ETextAlign::Right);
        }
        if(auto* Merchant=Player->FindMerchant();Merchant && !Player->IsEliminated() && !Player->IsBagOpen())
        {
            InteractionPrompt(Design,Elements,Base+3,W,H-280,PC->IsUsingGamepad()?TEXT("上ボタン"):TEXT("E"),TEXT("ルビー工房（強化・チューン）"),Gold);
        }
        for(TActorIterator<ARPGPrototypeLoot> It(GetWorld());It;++It)
        {
            if(!It->IsVisibleTo(Player)) { continue; }
            const float Range=550*(1+(Player->BadgeMod(TEXT("Scout"))+(It->Item.Rarity!=ERPGPrototypeRarity::Normal?Player->BadgeMod(TEXT("TechSense")):0))*.01f);
            if(FVector::DistSquared2D(It->GetActorLocation(),Player->GetActorLocation())>FMath::Square(Range)) { continue; }
            FVector2D At; if(Project(It->GetActorLocation()+FVector(0,0,60),At))
            { WorldPaint.Text(It->Item.Name(),At.X,At.Y,16*Scale,RarityColor(It->Item.Rarity),ETextAlign::Center,TEXT("Bold"),2); }
        }
        if(Player->IsEliminated())
        {
            HudText3D(Front,TEXT("KNOCK OUT"),W*.5f,H*.36f,61,FireFill.Top,ETextAlign::Center);
            const float Left=Player->GetRPGRespawnLeft();
            Card(P,W-300,H-295,270,90,Fire);
            Front.Text(TEXT("入口から復活"),W-165,H-282,22,Paper,ETextAlign::Center);
            HudText3D(Front,Left>0?FString::Printf(TEXT("%.1f"),Left):TEXT("..."),W-165,H-255,33,Paper,ETextAlign::Center);
        }
    }
    else
    {
        PaintEquipmentBag(Design,Elements,Base+3,W,H,Time);
    }
    if(MiniRevealAlpha>0 && (!Player->IsBagOpen() || Player->UsesMiniMenu())) { PaintMiniMenu(Geometry,Elements,Base+25,Time); }
    PaintAdventure(Geometry,Elements,Base+30,Time);
    if(Now<Player->NoticeUntil && !Player->HasConfirmation() && !Player->IsQuestMenuOpen())
    {
        // Over the menu footer while the bag is open (the hints come back when it fades).
        RPGPrototypeUI::FCanvas Toast; Toast.Geometry=&Design; Toast.Elements=&Elements; Toast.Layer=Base+40;
        // In play: below the quest banner and between the quest cards (left) and the buttons (right).
        const float Y=Player->IsBagOpen()?H-50:170;
        const float ToastWidth=FMath::Clamp(HudMeasure(Player->Notice,18,TEXT("Bold"))+64,320.0f,Player->IsBagOpen()?W-64:FMath::Max(320.0f,W-720));
        const float Fade=FMath::Clamp(static_cast<float>(Player->NoticeUntil-Now)*4.0f,0.0f,1.0f);
        if(Fade>0)
        {
            RPGPrototypeUI::Panel(Toast,0,W*.5f-ToastWidth*.5f,Y,ToastWidth,40,RPGPrototypeUI::EStyle::Card,RPGPrototypeUI::Highlight);
            FPainter Text=Toast.At(0,6); Text.Alpha=Fade;
            Text.Text(RPGPrototypeUI::Fit(Player->Notice,ToastWidth-30,18),W*.5f,Y+8,18,Paper,ETextAlign::Center,TEXT("Bold"));
        }
    }
    // Someone pressed a pad nobody holds: a small join hint on player 1's screen, between HP and the balls.
    if(Player->GetLocalRPGIndex()==0)
    {
        if(const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();Mode && Mode->IsJoinPromptShown())
        {
            RPGPrototypeUI::FCanvas Join; Join.Geometry=&Design; Join.Elements=&Elements; Join.Layer=Base+40;
            const FString Hint=FString::Printf(TEXT("＋ボタンで参加（%dP）"),Mode->GetLocalPlayerCount()+1);
            const float JW=HudMeasure(Hint,16,TEXT("Bold"))+44,JY=H-48;
            RPGPrototypeUI::Panel(Join,0,W*.5f-JW*.5f,JY,JW,32,RPGPrototypeUI::EStyle::Card,RPGPrototypeUI::Cyan);
            Join.At(0,6).Text(Hint,W*.5f,JY+6,16,Paper,ETextAlign::Center,TEXT("Bold"));
        }
    }
    return Base+49;
}
int32 URPGPrototypeHUD::GetPointerAction() const
{
    const auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    if(!Player || !PC || Player->IsBagOpen() || Player->IsQuestMenuOpen() || !PC->IsGameplayActive() || PC->IsUsingGamepad() || !FSlateApplication::IsInitialized()) { return 0; }
    const auto& Geometry=GetCachedGeometry();
    const FVector2D Size=Geometry.GetLocalSize();
    if(const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>();Mode && Mode->UsesSharedScreen())
    {
        const RPGPrototypePaint::FSharedDockLayout Dock(Size,Mode->GetLocalPlayerCount(),Player->GetLocalRPGIndex());
        const FVector2D Pos=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())/Dock.Scale;
        if(FBox2D({Dock.W-126,16},{Dock.W-16,52}).IsInside(Pos)) { return 13; }
        if(Dock.BagBounds().IsInside(Pos)) { return 10; }
        if(Dock.DashBounds().IsInside(Pos)) { return 12; }
        if(Dock.InteractionBounds().IsInside(Pos))
        {
            if(ARPGTreasureChest::Nearest(Player) || ARPGAdventureNode::Nearest(Player)) { return 16; }
            if(Player->FindNearbyLoot()) { return 11; } if(Player->FindMerchant()) { return 14; }
            for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It) { if(It->CanRetry(Player)) { return 15; } }
        }
        for(int32 I=0;I<3;++I) { if(FVector2D::DistSquared(Pos,Dock.BallCenter(I))<=FMath::Square(30.)) { return I+1; } }
        return 0;
    }
    const float Scale=BattleScale(Size,GetGameInstance() && GetGameInstance()->GetLocalPlayers().Num()>1);
    if(Scale<=0) { return 0; }
    const FVector2D At=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())/Scale;
    const float W=Size.X/Scale,H=Size.Y/Scale;
    // The "J 依頼の記録" chip under the place name (RPGAdventureHUD.cpp).
    if(ARPGAdventureWorld::Find(GetWorld()) && At.X>=18 && At.X<=170 && At.Y>=54 && At.Y<=86) { return 17; }
    if(ARPGAdventureWorld::Find(GetWorld()) && At.X>170 && At.X<=350 && At.Y>=54 && At.Y<=86) { return 18; }
    if(ARPGAdventureWorld::Find(GetWorld()) && At.X>350 && At.X<=470 && At.Y>=54 && At.Y<=86) { return 19; }
    if((ARPGTreasureChest::Nearest(Player) || ARPGAdventureNode::Nearest(Player) || ARPGQuestGiver::Nearest(Player) || ARPGWarpGate::Nearest(Player)) && At.X>=W*.5f-165 && At.X<=W*.5f+165 && At.Y>=H-285 && At.Y<=H-237) { return 16; }
    if(At.Y>=18 && At.Y<=60)
    {
        if(At.X>=W-300 && At.X<=W-168) { return 10; }
        if(At.X>=W-150 && At.X<=W-22) { return 13; }
    }
    if(At.X>=W-162 && At.X<=W-26 && At.Y>=H-238 && At.Y<=H-198) { return 12; }
    if(Player->FindMerchant() && At.X>=W*.5f-185 && At.X<=W*.5f+185 && At.Y>=H-280 && At.Y<=H-216) { return 14; }
    for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It)
    { if(It->CanRetry(Player) && At.X>=W*.5f-205 && At.X<=W*.5f+205 && At.Y>=H-280 && At.Y<=H-214) { return 15; } }
    for(int32 Index=0;Index<3;++Index)
    { if(FVector2D::DistSquared(At,AttackCenter(W,H,Index))<=FMath::Square(60.0)) { return Index+1; } }
    if(Player->FindNearbyLoot() && At.X>=W*.5f-205 && At.X<=W*.5f+205 && At.Y>=H-176 && At.Y<=H-88) { return 11; }
    return 0;
}
FReply URPGPrototypeHUD::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    if(!Player || !Player->IsBagOpen()) { return FReply::Unhandled(); }
    if((PC && PC->IsUsingGamepad()) || Event.GetEffectingButton()!=EKeys::LeftMouseButton) { return FReply::Handled(); }
    if(Player->UsesMiniMenu()) { ActivateBagAction(GetBagPointerAction()); return FReply::Handled(); }
    const float Scale=FMath::Min(Geometry.GetLocalSize().X/1280.0,Geometry.GetLocalSize().Y/720.0);
    if(Scale>0)
    {
        const FVector2D At=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())/Scale;
        ActivateBagAction(BagActionAt(At,Geometry.GetLocalSize().X/Scale,Geometry.GetLocalSize().Y/Scale));
    }
    return FReply::Handled();
}
FReply URPGPrototypeHUD::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    if(!Player || !Player->IsBagOpen() || (PC && PC->IsUsingGamepad())) { return FReply::Unhandled(); }
    if(Player->HasConfirmation()) { return FReply::Handled(); }
    const int32 Step=Event.GetWheelDelta()>0?-1:1;
    if(Player->BagPage==ERPGBagPage::Settings) { Player->NavigateBag(Step); return FReply::Handled(); }
    const float S=FMath::Min(Geometry.GetLocalSize().X/1280.0,Geometry.GetLocalSize().Y/720.0);
    const FVector2D At=S>0?Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())/S:FVector2D::ZeroVector;
    const float W=S>0?Geometry.GetLocalSize().X/S:1280;
    const bool bOverview=Player->BagPage==ERPGBagPage::Equipment && !Player->bChoosingEquipment;
    // Same split as the full menu layout: list on the left (≤41% of the width), details on the right.
    const bool bOverList=!bOverview && At.X<32+FMath::Min(560.0f,W*.41f)+12;
    if(bOverList)
    {
        const auto List=Player->FilteredBag(); const int32 I=List.Find(Player->BagSelection);
        if(List.Num()) { Player->BagSelection=List[FMath::Clamp((I<0?0:I)+Step,0,List.Num()-1)]; Player->DetailScroll=Player->TuneSelection=0; }
    }
    else if(Player->BagPage==ERPGBagPage::Tune) { Player->NavigateBag(Step); }
    else { Player->DetailScroll=FMath::Max(0,Player->DetailScroll+Step); }
    return FReply::Handled();
}
FReply URPGPrototypeHUD::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    auto* Player=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    if(!Player || !Player->IsBagOpen() || !PC || PC->IsUsingGamepad()!=Event.GetKey().IsGamepadKey()) { return FReply::Unhandled(); }
    return Player->HandleBagKey(Event.GetKey())?FReply::Handled():FReply::Unhandled();
}

