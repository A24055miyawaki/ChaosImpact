#include "ChaosImpactCombatHealth.h"
#include "ChaosImpactChargeWidget.h"
#include "ChaosImpactCharacter.h"
#include "ChaosImpactPlayerController.h"
#include "ChaosImpactGameState.h"
#include "ChaosImpactPaint.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void ChaosImpactCombatHealth::PaintBreak(const ChaosImpactPaint::FPainter& Painter,const FVector2D& Center,
    float T,float Width,float Height,const ChaosImpactPaint::FHudFill& Fill,int32 Seed)
{
    using namespace ChaosImpactPaint;
    if(T<0 || T>=1) { return; }
    // Six actual pieces of the blade, not triangles all originating in one overlapping burst.
    const FVector2D Pieces[][3]={
        {{-.5f,.5f},{-.4f,-.5f},{-.18f,0}},{{-.4f,-.5f},{0,-.5f},{-.18f,0}},
        {{-.5f,.5f},{-.18f,0},{.15f,.5f}},{{0,-.5f},{.37f,-.5f},{.18f,0}},
        {{.18f,0},{.37f,-.5f},{.5f,0}},{{.15f,.5f},{.18f,0},{.5f,0}}};
    const FPainter Fade{Painter.Geometry,Painter.Elements,Painter.Layer,Painter.Alpha*FMath::Square(1-T)};
    for(int32 I=0;I<6;++I)
    {
        FVector2D Pivot=FVector2D::ZeroVector; for(const auto& V:Pieces[I]) { Pivot+=V*FVector2D(Width,Height)/3; }
        const float R=FMath::Frac(FMath::Sin((I+Seed*7+1)*78.233f)*43758.5453f);
        const FVector2D Offset(Pivot.X*T*2.5f,(I%2?-1.f:-.5f)*(12+Height*.4f)*T+42*T*T);
        const float Angle=(I%2?1:-1)*T*(1.2f+R*2),C=FMath::Cos(Angle),S=FMath::Sin(Angle);
        TArray<FVector2D> Poly;
        for(const auto& V:Pieces[I])
        {
            const auto Delta=V*FVector2D(Width,Height)-Pivot;
            Poly.Add(Center+Pivot+Offset+FVector2D(Delta.X*C-Delta.Y*S,Delta.X*S+Delta.Y*C));
        }
        HudPoly(Fade,HudInflate(Poly,.75f),SolidFill(Ink),1);
        HudPoly(HudAbove(Fade,1),Poly,Fill,2);
        HudAbove(Fade,2).Line(Poly[0],Poly[1],WithAlpha(Paper,.8f),.8f);
    }
}

bool UChaosImpactChargeWidget::SupportsCombatHealth() const
{
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    const auto* World=GetWorld(); if(!PC || !World || PC->IsSoloMode()
        || World->URL.HasOption(TEXT("CIRPGPrototype=1")) || World->URL.HasOption(TEXT("CIOpenWorld=1"))) { return false; }
    const auto* Match=World->GetGameState<AChaosImpactGameState>();
    return PC->ShowsTrainingPauseEntries() || (Match && Match->bVersusMatch && Match->Phase==EChaosImpactOnlinePhase::Match);
}
void UChaosImpactChargeWidget::UpdateCombatHealth()
{
    if(!SupportsCombatHealth()) { CombatHealth.Empty(); return; }
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto It=CombatHealth.CreateIterator();It;++It) { if(!It.Key().IsValid()) { It.RemoveCurrent(); } }
    for(TActorIterator<AChaosImpactCharacter> It(GetWorld());It;++It)
    { CombatHealth.FindOrAdd(*It).Update(It->GetHealth(),It->GetMaxHealth(),It->IsEliminated(),Now); }
}
bool UChaosImpactChargeWidget::GetCombatHealthPresentation(const AChaosImpactCharacter* Character,
    ChaosImpactCombatHealth::FPresentation& Out) const
{
    Out={};
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    // This is per-view, not per-machine: in split screen another player's bar still appears.
    if(!SupportsCombatHealth() || !PC->IsGameplayActive() || !IsValid(Character)
        || Character==GetOwningPlayerPawn()) { return false; }
    const auto* State=CombatHealth.Find(Character);
    return State && State->Present(GetWorld()->GetTimeSeconds(),Out);
}
bool UChaosImpactChargeWidget::IsLastHealthWarningActive() const
{
    const auto* PC=Cast<AChaosImpactPlayerController>(GetOwningPlayer());
    const auto* Me=Cast<AChaosImpactCharacter>(GetOwningPlayerPawn());
    return SupportsCombatHealth() && PC->IsGameplayActive() && !bSpectatorView && !bRespawnVisible
        && !bVitalsHidden && Me && !Me->IsEliminated() && Me->GetHealth()>0 && Me->GetHealth()<=1;
}
void UChaosImpactChargeWidget::PaintCombatHealth(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const
{
    using namespace ChaosImpactPaint;
    if(!SupportsCombatHealth()) { return; }
    const auto Size=Geometry.GetLocalSize(); const float S=GetHudScale(Size.X,Size.Y);
    const double Now=GetWorld()->GetTimeSeconds(); const float Others=1-GetSmokeCover();
    for(const auto& Entry:CombatHealth)
    {
        const auto* Character=Entry.Key.Get(); ChaosImpactCombatHealth::FPresentation View;
        if(!GetCombatHealthPresentation(Character,View) || Character->IsHidden()) { continue; }
        const float Shown=Character==GetOwningPlayerPawn()?1.f:Others;
        if(Shown<.01f) { continue; }
        const FVector Head=Character->GetPresentationLocation()+FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+40);
        FVector2D At; if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Head,At,true)) { continue; }
        const float Width=View.Cells*37-5;
        if(At.X<(Width*.5f+28)*S || At.X>Size.X-(Width*.5f+28)*S || At.Y<66*S || At.Y>Size.Y-10*S) { continue; }
        const float HitAge=static_cast<float>(Now-Entry.Value.HitAt);
        const float Jolt=HitAge>=0 && HitAge<.2f?FMath::Sin(HitAge*70)*(.2f-HitAge)*9:0;
        const auto Space=MakeAnchor(Geometry,At.X+Jolt*S,At.Y-28*S,S);
        const FPainter Back{Space,Elements,Layer,View.Alpha*Shown},Top=HudAbove(Back,3);
        const float Pulse=.5f+.5f*FMath::Sin(static_cast<float>(Now)*6.f);
        const FHudFill Lit=View.Critical?MakeFill(FMath::Lerp(FireFill.Top,Paper,Pulse*.3f),FireFill.Bottom):IceFill;
        for(int32 Cell=0;Cell<View.Cells;++Cell)
        {
            const float L=-Width*.5f+Cell*37,R=L+32;
            const TArray<FVector2D> Blade{{L+3,-6},{R-4,-6},{R,0},{R-4,6},{L,6}};
            HudShard(Back,Blade,SolidFill(FLinearColor(.012f,.02f,.035f,.96f)),1.5f,{1,2},Ink);
            if(Cell<View.Remaining)
            {
                HudPoly(Top,Blade,Lit,3);
                Top.Line({L+5,-4},{R-6,-4},WithAlpha(Paper,.85f),1.2f);
            }
            for(const auto& Break:View.Breaks)
            {
                if(Break.Cell!=Cell) { continue; }
                const float Age=static_cast<float>(Now-Break.At);
                FPainter Flash=Top; Flash.Alpha*=FMath::Clamp(1-Age/.16f,0.f,1.f)*.65f;
                HudPoly(Flash,Blade,IceFill,2);
                ChaosImpactCombatHealth::PaintBreak(HudAbove(Top),{(L+R)*.5f,0},Age/ChaosImpactCombatHealth::BreakSeconds,32,12,IceFill,Cell+3);
            }
        }
        const FVector2D Badge(Width*.5f+15,0);
        HudShard(Back,{Badge+FVector2D(0,-11),Badge+FVector2D(11,0),Badge+FVector2D(0,11),Badge+FVector2D(-11,0)},
            View.Critical?FireFill:MakeFill(FLinearColor(.08f,.13f,.22f),FLinearColor(.015f,.025f,.055f)),1.5f,{1,2},Ink);
        HudText3D(Top,FString::FromInt(View.Remaining),Badge.X,Badge.Y-10,15,Paper,ETextAlign::Center,TEXT("Black"),1);
        if(View.Critical)
        {
            FPainter Alert=Top; Alert.Alpha*=.5f+.25f*Pulse;
            Alert.Line({-Width*.5f-5,-8},{-Width*.5f-7,8},Fire,2);
        }
    }
}
void UChaosImpactChargeWidget::PaintLastHealthWarning(const FGeometry& Geometry,FSlateWindowElementList& Elements,int32 Layer) const
{
    using namespace ChaosImpactPaint;
    if(!IsLastHealthWarningActive()) { return; }
    const auto Size=Geometry.GetLocalSize(); const float S=GetHudScale(Size.X,Size.Y);
    const float Pulse=.5f+.5f*FMath::Sin(static_cast<float>(GetWorld()->GetTimeSeconds())*6.f);
    const FPainter Cue{Geometry,Elements,Layer,.3f+.12f*Pulse};
    // Short corner accents, not an opaque red overlay over aiming/gameplay.
    for(int32 I=0;I<4;++I)
    {
        const FVector2D Origin(I%2?Size.X-5*S:5*S,I/2?Size.Y-5*S:5*S);
        const FVector2D AxisX(I%2?-1:1,0),AxisY(0,I/2?-1:1);
        Cue.Line(Origin+AxisX*52*S,Origin+AxisX*13*S,Fire,3*S);
        Cue.Line(Origin+AxisX*13*S,Origin+AxisY*13*S,Fire,3*S);
        Cue.Line(Origin+AxisY*13*S,Origin+AxisY*52*S,Fire,3*S);
    }
}
