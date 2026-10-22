#include "RPGPrototypeHUD.h"
#include "RPGPrototype.h"
#include "RPGPrototypeHUDStyle.h"
#include "RPGAdventure.h"
#include "RPGTreasureChest.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"

FGeometry URPGPrototypeHUD::OwnerMenuGeometry(const FGeometry& Geometry) const
{
    const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>():nullptr;
    const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn());
    if(!Mode || !Mode->UsesSharedScreen() || !P) { return Geometry; }
    const auto Size=Geometry.GetLocalSize(); const int32 I=FMath::Max(0,P->GetLocalRPGIndex()),Count=Mode->GetLocalPlayerCount();
    const FVector2f Area(Size.X/2,Count>2?Size.Y/2:Size.Y);
    return Geometry.MakeChild(Area,FSlateLayoutTransform(1,FVector2f((I%2)*Area.X,Count>2?(I/2)*Area.Y:0)));
}
void URPGPrototypeHUD::PaintSharedHUD(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer,float Time) const
{
    using namespace ChaosImpactPaint;
    const auto* P=Cast<ARPGPrototypeCharacter>(GetOwningPlayerPawn()); auto* PC=GetOwningPlayer();
    const auto* Mode=GetWorld()->GetAuthGameMode<ARPGPrototypeGameMode>(); if(!P || !PC || !Mode) { return; }
    const auto Size=Geometry.GetLocalSize(); if(Size.X<=0 || Size.Y<=0) { return; }
    const RPGPrototypePaint::FSharedDockLayout Dock(Size,Mode->GetLocalPlayerCount(),P->GetLocalRPGIndex());
    const float Scale=Dock.Scale,W=Dock.W,H=Dock.H,Col=Dock.Col,X=Dock.X;
    const float Now=GetWorld()->GetTimeSeconds();
    const FLinearColor Colors[]={Fire,Ice,Gold,Violet}; const auto Color=Colors[FMath::Clamp(P->GetLocalRPGIndex(),0,3)];
    FPainter Back{MakeAnchor(Geometry,0,0,Scale),Elements,Layer},Mid=HudAbove(Back,2),F=HudAbove(Back,4);
    const auto Card=[&](float AtX,float AtY,float Width,float Height,FLinearColor C)
    { HudShard(Back,{{AtX+7,AtY},{AtX+Width,AtY},{AtX+Width-7,AtY+Height},{AtX,AtY+Height}},NightFill,2,{2,3}); F.Line({AtX+8,AtY+1},{AtX+Width-2,AtY+1},C,2); };
    // Same faceted metal, fire/ice blades and spherical sockets as the existing VS HUD.
    // No enclosing dashboard rectangle: each player's status is a compact floating crest.
    const FHudFill Metal=MakeFill(Color*.65f,Color*.28f);
    const FVector2D Crest(X+38,H-127);
    const float HitAge=Now-HitAt;
    const float Kick=HitAge>=0 && HitAge<.3f?FMath::Sin(HitAge*65)*(.3f-HitAge)*8:0;
    const FVector2D Gem=Crest+FVector2D(Kick,0);
    HudShard(Back,{Gem+FVector2D(0,-32),Gem+FVector2D(30,-3),Gem+FVector2D(0,31),Gem+FVector2D(-29,2)},Metal,3,{3,5});
    Mid.Line(Gem+FVector2D(-21,0),Gem+FVector2D(-1,-23),WithAlpha(Paper,.8f),2);
    HudText3D(F,FString::Printf(TEXT("%dP"),P->GetLocalRPGIndex()+1),Gem.X,Gem.Y-15,23,Paper,ETextAlign::Center);
    const auto Bag=Dock.BagBounds();
    HudShard(Back,{{Bag.Min.X+6,Bag.Min.Y},{Bag.Max.X,Bag.Min.Y},{Bag.Max.X-6,Bag.Max.Y},{Bag.Min.X,Bag.Max.Y}},SolidFill(Ink),2,{2,2});
    F.Text(TEXT("バッグ"),X+60,H-184,14,Paper,ETextAlign::Center,TEXT("Bold"));
    // Ruby gem instead of an ambiguous generic bullet.
    const FString RubyText=FString::FromInt(P->GetRubies());
    const FVector2D Ruby(X+Col-30-HudMeasure(RubyText,16,TEXT("Bold")),H-176);
    HudPoly(Mid,{Ruby+FVector2D(0,-7),Ruby+FVector2D(7,0),Ruby+FVector2D(0,7),Ruby+FVector2D(-7,0)},FireFill,3);
    F.Text(RubyText,X+Col-15,H-186,16,Gold,ETextAlign::Right,TEXT("Bold"),2);
    HudText3D(F,FString::Printf(TEXT("Lv.%d"),P->GetRPGLevel()),X+77,H-157,16,Paper);
    const bool Low=P->GetHealth()/P->GetMaxHealth()<.25f;
    F.Text(TEXT("HP"),X+77,H-132,11,Low?Fire:Ice,ETextAlign::Left,TEXT("Bold"),2);
    HudText3D(F,FString::Printf(TEXT("%.0f"),P->GetHealth()),X+101,H-145,27,Paper);
    F.Text(FString::Printf(TEXT("/ %.0f"),P->GetMaxHealth()),X+Col-19,H-133,13,Paper,ETextAlign::Right,TEXT("Bold"),2);
    const float Start=X+77,End=X+Col-17,Bar=End-Start;
    const TArray<FVector2D> HPBlade{{Start+3,H-111},{End-9,H-111},{End,H-101},{End-9,H-91},{Start-2,H-91}};
    HudShard(Back,HPBlade,SolidFill(Ink),2,{2,4});
    HudPoly(Mid,HudClip(HPBlade,Start-2+(Bar+2)*FMath::Clamp(TrailingHP/P->GetMaxHealth(),0.f,1.f),false,true),FireFill,3);
    const float Ratio=FMath::Clamp(P->GetHealth()/P->GetMaxHealth(),0.f,1.f);
    HudPoly(F,HudClip(HPBlade,Start-2+(Bar+2)*Ratio,false,true),Low?FireFill:IceFill,4);
    if(Ratio>0) { F.Line({Start+4,H-108},{Start+FMath::Max(4.f,Bar*Ratio-6),H-108},WithAlpha(Paper,.75f),1.5f); }
    if(HitAge>=0 && HitAge<.65f) { HudShatter(F,{Start+Bar*Ratio,H-101},HitAge/.65f,9,IceFill,6,3); }
    const TArray<FVector2D> STBlade{{Start,H-80},{End-9,H-80},{End-3,H-76},{End-10,H-72},{Start-3,H-72}};
    HudShard(Back,STBlade,SolidFill(Ink),1.5f,{2,2});
    HudPoly(Mid,HudClip(STBlade,Start-3+(Bar+3)*P->GetStamina()/P->GetMaxStamina(),false,true),FireFill,3);
    for(int32 I=1;I<5;++I) { F.Line({Start+Bar*I/5+1,H-80},{Start+Bar*I/5-1,H-72},Ink,2); }
    const auto* OwnerInput=Cast<AChaosImpactPlayerController>(PC);
    if(!OwnerInput || !OwnerInput->IsUsingGamepad())
    {
        const auto Dash=Dock.DashBounds();
        HudShard(Back,{{Dash.Min.X+4,Dash.Min.Y},{Dash.Max.X,Dash.Min.Y},{Dash.Max.X-4,Dash.Max.Y},{Dash.Min.X,Dash.Max.Y}},SolidFill(Ink),1,{2,2});
        F.Text(TEXT("回避"),X+38,H-88,12,Gold,ETextAlign::Center,TEXT("Bold"));
    }
    else { F.Text(TEXT("ST"),X+38,H-87,11,Gold,ETextAlign::Center,TEXT("Bold"),2); }
    Back.Box(X+18,H-6,Col-36,2,WithAlpha(Ink,.9f));
    Mid.Box(X+18,H-6,(Col-36)*P->GetXP()/FMath::Max(1,P->GetRequiredXP()),2,Ice);
    for(int32 I=0;I<3;++I)
    {
        const FVector2D At=Dock.BallCenter(I); const auto& Item=P->GetAttack(I);
        const bool Selected=P->GetSelectedAttack()==I;
        const float Left=P->GetCooldownLeft(I),R=Selected?26.f:22.f;
        const float SelectionKick=Selected?FMath::Exp(-12.f*FMath::Max(0.f,Now-static_cast<float>(SelectionAt)))*.1f:0;
        if(Selected && Item.IsValid()) { HudBurst(Back,At,R,R+8,8,Left>0?NightFill:FireFill,0,{1,1},2,{3,4},I+2); }
        Mid.Disc(At+FVector2D(2,3),R+2,Extrude); Mid.Disc(At,R+2,Navy); Mid.Disc(At,R,Ink);
        if(Item.IsValid())
        {
            const auto Accent=ChaosImpactBallTypes::GetColor(Item.Type);
            for(int32 S=0;S<6;++S) { F.Arc(At,R-1,Time*18+S*60,Time*18+S*60+48,Left>0?Muted:Accent,2); }
            FPainter Icon=F; Icon.Alpha=Left>0?.3f:1;
            RPGPrototypePaint::Ball(Icon,At,(Selected?18:16)*(1+SelectionKick),Item.Type,Time);
            if(Left>0)
            { F.Arc(At,R-4,-90,-90+360*(1-FMath::Clamp(Left/Item.EffectiveCooldown(),0.f,1.f)),Ice,2.5f);
              HudText3D(F,FString::Printf(TEXT("%.1f"),Left),At.X,At.Y-10,16,Paper,ETextAlign::Center); }
            const float FlashAge=Now-ReadyFlashAt[I];
            if(FlashAge>=0 && FlashAge<.55f) { FPainter Flash=F; Flash.Alpha=1-FlashAge/.55f; Flash.Ring(At,R+FlashAge*20,Paper,2); }
        }
        else { RPGPrototypePaint::Ball(F,At,R-3,EChaosImpactBallType::Normal,Time,false); F.Text(TEXT("—"),At.X,At.Y-10,16,Muted,ETextAlign::Center); }
    }
    if(P->IsChargingThrow()) { F.Arc(Dock.BallCenter(P->GetSelectedAttack()),32,-90,-90+360*P->GetThrowChargeAlpha(),Gold,3); }
    if(P->IsEliminated()) { HudText3D(F,FString::Printf(TEXT("復活まで %.1f秒"),P->GetRPGRespawnLeft()),X+Col*.5f,H-229,18,Paper,ETextAlign::Center); }
    else if(!P->IsBagOpen() && !P->IsQuestMenuOpen())
    {
        FString Hint;
        if(const auto* Chest=ARPGTreasureChest::Nearest(P)) { Hint=TEXT("E / 上側ボタン　")+Chest->DisplayName; }
        else if(const auto* Node=ARPGAdventureNode::Nearest(P)) { Hint=TEXT("E / 上側ボタン　")+Node->DisplayName; }
        else if(P->FindNearbyLoot()) { Hint=TEXT("F / 拾うボタン　拾う"); }
        else if(P->FindMerchant()) { Hint=TEXT("E / 上側ボタン　ルビー工房"); }
        if(Hint.IsEmpty()) { for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It) { if(It->CanRetry(P)) { Hint=TEXT("E / 上側ボタン　ボスに再挑戦"); break; } } }
        if(!Hint.IsEmpty()) { Card(X+12,H-240,Col-24,40,Gold); F.Text(Hint,X+Col*.5f,H-228,12,Paper,ETextAlign::Center,TEXT("Bold")); }
    }
    if(GetWorld()->GetTimeSeconds()<P->NoticeUntil)
    {
        FString Line; int32 Row=0;
        for(TCHAR Ch:P->Notice)
        {
            const FString Next=Line+FString::Chr(Ch);
            if(!Line.IsEmpty() && HudMeasure(Next,12,TEXT("Bold"))>Col-36)
            { F.Text(Line,X+18,H-276+Row*16,12,Gold,ETextAlign::Left,TEXT("Bold"),2); Line.Empty(); if(++Row>=2) { break; } }
            Line.AppendChar(Ch);
        }
        if(!Line.IsEmpty() && Row<2) { F.Text(Line,X+18,H-276+Row*16,12,Gold,ETextAlign::Left,TEXT("Bold"),2); }
    }
    if(P->GetLocalRPGIndex()!=0) { return; }
    FPainter WorldPaint{Geometry,Elements,Layer+3};
    Card(W-126,16,110,36,Ice);
    F.Text(TEXT("ポーズ"),W-71,23,16,Paper,ETextAlign::Center,TEXT("Bold"));
    const auto Project=[&](FVector At,FVector2D& Out) { return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,At,Out,true); };
    if(const auto* Place=ARPGAdventureWorld::Find(GetWorld())) { HudText3D(F,Place->RegionName(P->GetActorLocation()),20,16,21,Paper,ETextAlign::Left,TEXT("Bold")); }
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        const auto* Other=Cast<ARPGPrototypeCharacter>(It->Get()->GetPawn()); if(!Other || Other->IsEliminated()) { continue; }
        if(const auto* Input=Cast<AChaosImpactPlayerController>(It->Get());Input && !Input->IsUsingGamepad() && !Other->IsBagOpen() && FSlateApplication::IsInitialized())
        {
            const FVector2D SharedCursorAt=Geometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
            WorldPaint.Ring(SharedCursorAt,8*Scale,Ink,5*Scale); WorldPaint.Ring(SharedCursorAt,8*Scale,Paper,2*Scale);
            WorldPaint.Line(SharedCursorAt-FVector2D(13*Scale,0),SharedCursorAt+FVector2D(13*Scale,0),Paper,2*Scale);
            WorldPaint.Line(SharedCursorAt-FVector2D(0,13*Scale),SharedCursorAt+FVector2D(0,13*Scale),Paper,2*Scale);
        }
        const auto C=Colors[FMath::Clamp(Other->GetLocalRPGIndex(),0,3)]; FVector2D A,B;
        if(Project(Other->GetActorLocation()+FVector(0,0,125),A)) { WorldPaint.Text(FString::Printf(TEXT("%dP"),Other->GetLocalRPGIndex()+1),A.X,A.Y,17*Scale,C,ETextAlign::Center,TEXT("Bold"),2); }
        if(Other->IsChargingThrow() && Project(Other->GetAimGuideStartWorldLocation(),A) && Project(Other->GetAimGuideStartWorldLocation()+Other->GetAimDirection()*Other->GetAimGuideLength(),B))
        { const auto D=(B-A).GetSafeNormal(),S=FVector2D(-D.Y,D.X); WorldPaint.Line(A,B,C,4*Scale); WorldPaint.Line(B,B-D*25+S*13,C,5*Scale); WorldPaint.Line(B,B-D*25-S*13,C,5*Scale); }
    }
    for(TActorIterator<ARPGPrototypeCharacter> It(GetWorld());It;++It)
    {
        if(!It->IsGrunt() || It->IsEliminated() || Cast<ARPGPrototypeBoss>(*It) || It->GetLastDamagedAt()<=0 || GetWorld()->GetTimeSeconds()-It->GetLastDamagedAt()>4) { continue; }
        FVector2D At; if(Project(It->GetActorLocation()+FVector(0,0,112),At))
        { WorldPaint.Box(At.X-45*Scale,At.Y,90*Scale,7*Scale,Ink); WorldPaint.Box(At.X-45*Scale,At.Y,90*Scale*It->GetHealth()/It->GetMaxHealth(),7*Scale,Fire); }
    }
    for(const auto& Text:Mode->DamageTexts) // Shared screen: local players only, the host's numbers.
    { const float Age=GetWorld()->GetTimeSeconds()-Text.StartedAt; FVector2D At; if(Project(Text.Location+FVector(0,0,Age*65),At)) { HudText3D(WorldPaint,FString::Printf(TEXT("%.0f"),Text.Amount),At.X,At.Y,27*Scale,Text.bPlayer?Fire:Gold,ETextAlign::Center); } }
    for(TActorIterator<ARPGPrototypeLoot> It(GetWorld());It;++It)
    { FVector2D At; if(It->Recipient && Project(It->GetActorLocation()+FVector(0,0,45),At)) { WorldPaint.Text(FString::Printf(TEXT("%dP %s"),It->Recipient->GetLocalRPGIndex()+1,*It->Item.Name()),At.X,At.Y,12*Scale,Gold,ETextAlign::Center,TEXT("Bold"),1); } }
    for(TActorIterator<ARPGPrototypeBoss> It(GetWorld());It;++It)
    {
        if(!It->IsBossActive()) { continue; }
        Card(W*.5f-210,12,420,58,Ice);
        F.Text(FString::Printf(TEXT("暴走マグロ　Lv.%d"),It->GetRPGLevel()),W*.5f-195,20,19,Paper,ETextAlign::Left,TEXT("Bold"));
        F.Text(FString::Printf(TEXT("%.0f / %.0f"),It->GetHealth(),It->GetMaxHealth()),W*.5f+195,22,15,Paper,ETextAlign::Right,TEXT("Bold"));
        Back.Box(W*.5f-195,49,390,8,Ink); F.Box(W*.5f-195,49,390*It->GetHealth()/It->GetMaxHealth(),8,Fire);
        const FVector Floor=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-5);
        if(It->GetBossState()==ERPGBossState::ChargeTell)
        { FVector2D A,B; if(Project(Floor,A) && Project(Floor+It->GetTellDirection()*1200,B)) { WorldPaint.Line(A,B,Fire,12*Scale); } }
        if(It->GetBossState()==ERPGBossState::LeapTell || It->GetBossState()==ERPGBossState::Wave)
        {
            const float R=It->GetBossState()==ERPGBossState::Wave?It->GetWaveRadius():1000;
            for(int32 I=0;I<64 && R>1;++I)
            { FVector2D A,B; const float U=I*2*PI/64,V=(I+1)*2*PI/64;
              if(Project(Floor+FVector(FMath::Cos(U)*R,FMath::Sin(U)*R,0),A) && Project(Floor+FVector(FMath::Cos(V)*R,FMath::Sin(V)*R,0),B)) { WorldPaint.Line(A,B,Fire,3*Scale); } }
        }
    }
}
