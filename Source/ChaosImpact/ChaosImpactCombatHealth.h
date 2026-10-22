#pragma once
#include "CoreMinimal.h"
namespace ChaosImpactPaint { struct FPainter; struct FHudFill; }

/** Presentation only: never changes damage, KO, scoring or replication. */
namespace ChaosImpactCombatHealth
{
constexpr float DisplaySeconds=4.f;
constexpr float FadeSeconds=.4f;
constexpr float BreakSeconds=.7f;
void PaintBreak(const ChaosImpactPaint::FPainter& Painter,const FVector2D& Center,float T,float Width,float Height,
    const ChaosImpactPaint::FHudFill& Fill,int32 Seed);
struct FBreak
{
    int32 Cell=0;
    double At=0;
};
struct FPresentation
{
    int32 Cells=3,Remaining=3;
    float Alpha=0;
    bool Critical=false;
    TArray<FBreak> Breaks;
};
struct FState
{
    float Health=0,Maximum=3;
    bool Known=false,Eliminated=false;
    double HitAt=-100;
    TArray<FBreak> Breaks;

    static int32 Count(float Value) { return FMath::Max(0,FMath::CeilToInt(Value-KINDA_SMALL_NUMBER)); }
    void Update(float Current,float Max,bool KO,double Now)
    {
        if(!FMath::IsFinite(Current) || !FMath::IsFinite(Max) || Max<=0) { return; }
        Current=FMath::Clamp(Current,0.f,Max);
        Breaks.RemoveAll([Now](const FBreak& B) { return Now-B.At>=BreakSeconds; });
        if(!Known || !FMath::IsNearlyEqual(Maximum,Max))
        {
            Known=true; Breaks.Empty(); HitAt=Current>0 && Current<Max && !KO?Now:-100;
        }
        else if(Current<Health-KINDA_SMALL_NUMBER)
        {
            HitAt=Now;
            for(int32 I=Count(Current);I<Count(Health) && I<6;++I) { Breaks.Add({I,Now}); }
        }
        else if(Current>Health+KINDA_SMALL_NUMBER || (Eliminated && !KO))
        { HitAt=-100; Breaks.Empty(); }
        Health=Current; Maximum=Max; Eliminated=KO;
    }
    bool Present(double Now,FPresentation& Out) const
    {
        Out={}; if(!Known) { return false; }
        Out.Cells=FMath::Clamp(FMath::RoundToInt(Maximum),1,6);
        Out.Remaining=FMath::Clamp(Count(Health),0,Out.Cells);
        Out.Critical=!Eliminated && Health>0 && Health<=1;
        const float Age=static_cast<float>(Now-HitAt);
        const float End=Eliminated?BreakSeconds:DisplaySeconds;
        Out.Alpha=Out.Critical?1.f:FMath::Clamp((End-Age)/FadeSeconds,0.f,1.f);
        for(const auto& B:Breaks) { if(Now-B.At<BreakSeconds) { Out.Breaks.Add(B); } }
        return Out.Alpha>0;
    }
};
}
