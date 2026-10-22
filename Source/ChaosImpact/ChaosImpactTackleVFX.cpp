#include "ChaosImpactTackleVFX.h"
#include "ChaosImpactBallTypes.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

AChaosImpactTackleVFX::AChaosImpactTackleVFX()
{
    PrimaryActorTick.bCanEverTick=true;
    Mesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TackleStreaks"));
    SetRootComponent(Mesh);
    SetActorEnableCollision(false);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(false);
    Mesh->bUseAsOccluder=false;
    SetReplicates(false); // Each machine spawns its own cosmetics from the server's event.
}
AChaosImpactTackleVFX* AChaosImpactTackleVFX::Play(UWorld* World,const FVector& At,
    const FVector& Direction,bool bImpact,AActor* Follow)
{
    if(!World || World->GetNetMode()==NM_DedicatedServer) { return nullptr; }
    FActorSpawnParameters Params;
    Params.ObjectFlags|=RF_Transient;
    auto* FX=World->SpawnActor<AChaosImpactTackleVFX>(At,Direction.Rotation(),Params);
    if(!FX) { return nullptr; }
    FX->bHit=bImpact; FX->Following=Follow;
    FX->Look=ChaosImpactBallTypes::MakeAdditive(FX,
        FLinearColor(1,.50f,.045f),bImpact?1.6f:.6f);
    FX->Mesh->SetMaterial(0,FX->Look);
    if(!bImpact)
    {
        for(int32 I=0;I<3;++I)
        {
            auto* Glow=ChaosImpactBallTypes::MakeAdditive(FX,
                I==2?FLinearColor(1,.94f,.76f):FLinearColor(1,.46f,.025f),.2f);
            FX->GlowLooks.Add(Glow); FX->Mesh->SetMaterial(I+1,Glow);
        }
    }
    FX->SetLifeSpan(bImpact?.46f:.32f);
    FX->Rebuild();
    return FX;
}
void AChaosImpactTackleVFX::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); Age+=DeltaSeconds;
    if(Following.IsValid() && Age<.20f) { SetActorLocation(Following->GetActorLocation()); }
    const float Life=bHit?.46f:.32f;
    const float Fade=FMath::Square(FMath::Max(0.f,1-Age/Life));
    if(Look) { Look->SetScalarParameterValue(TEXT("Intensity"),(bHit?1.8f:.60f)*Fade); }
    for(int32 I=0;I<GlowLooks.Num();++I)
    { if(GlowLooks[I]) { GlowLooks[I]->SetScalarParameterValue(TEXT("Intensity"),(I==2?1.2f:I==1?.12f:.24f)*Fade); } }
    Rebuild();
}
int32 AChaosImpactTackleVFX::GetVertexCount() const
{
    const auto* Section=Mesh->GetProcMeshSection(0);
    return Section?Section->ProcVertexBuffer.Num():0;
}
void AChaosImpactTackleVFX::Rebuild()
{
    TArray<FVector> V,N;
    TArray<int32> T;
    TArray<FVector2D> UV;
    auto Quad=[&](FVector A,FVector B,FVector C,FVector D)
    {
        const int32 I=V.Num(); V.Append({A,B,C,D});
        const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        for(int32 J=0;J<4;++J) { N.Add(Normal); }
        UV.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
        T.Append({I,I+1,I+2,I,I+2,I+3,I,I+2,I+1,I,I+3,I+2});
    };
    for(int32 Pass=0;Pass<(bHit?1:4);++Pass)
    {
    V.Reset(); N.Reset(); T.Reset(); UV.Reset();
    if(!bHit)
    {
        // Layered golden sheets curl around the leading shoulder then sweep upward behind it.
        // Broad dim outer bands soften the silhouette; a narrow warm-white edge carries the motion.
        const float WidthScale=Pass==0?1.f:Pass==1?1.65f:Pass==2?2.35f:.10f;
        for(int32 Side:{-1,1})
        for(int32 Layer=0;Layer<3;++Layer)
        {
            const float Radius=57+Layer*16+Age*65;
            for(int32 I=0;I<40;++I)
            {
                auto P=[&](float U,float Edge)
                {
                    const float Angle=-.24f+U*3.10f+Age*1.6f;
                    const float Width=(14+Layer*4)*FMath::Pow(FMath::Sin(PI*U),1.4f)*WidthScale;
                    const float R=Radius+Edge*Width;
                    return FVector(FMath::Cos(Angle)*R-14-U*U*80,
                        Side*FMath::Sin(Angle)*R*.77f,
                        -25+Layer*20+FMath::Sin(U*PI*.85f)*48+U*U*38);
                };
                const float A=I/40.f, B=(I+1)/40.f;
                Quad(P(A,-1),P(A,1),P(B,1),P(B,-1));
            }
        }
        if(Pass==1 || Pass==3)
        {
            // Three open wind helices around the travel axis, visible from every camera.
            for(int32 Curl=0;Curl<3;++Curl)
            for(int32 I=0;I<42;++I)
            {
                auto P=[&](float U,float Edge)
                {
                    const float Angle=Curl*2*PI/3+U*PI*1.5f-Age*14;
                    const float Radius=42+U*22;
                    const float Width=FMath::Sin(U*PI)*(Pass==3?3.f:9.f);
                    return FVector(72-U*182,
                        FMath::Cos(Angle)*(Radius+Edge*Width),
                        FMath::Sin(Angle)*(Radius+Edge*Width)-8);
                };
                const float A=I/42.f, B=(I+1)/42.f;
                Quad(P(A,-1),P(A,1),P(B,1),P(B,-1));
            }
        }
    }
    else
    {
        for(int32 Layer=0;Layer<3;++Layer)
        for(int32 I=0;I<28;++I)
        {
            const float Radius=30.f+Layer*17+Age*290;
            const float Sweep=PI*1.65f;
            const float A=-Sweep*.5f+Sweep*I/28, B=-Sweep*.5f+Sweep*(I+1)/28;
            const float Width=5.f*FMath::Sin(PI*(I+.5f)/28);
            auto P=[&](float Angle,float R) { return FVector(FMath::Cos(Angle)*R,
                FMath::Sin(Angle)*R,-20+Layer*15+FMath::Sin(Angle)*8); };
            Quad(P(A,Radius-Width),P(A,Radius+Width),P(B,Radius+Width),P(B,Radius-Width));
        }
    }
    // Impact spits tapered streaks out radially; rush leaves narrow parallel wake lines.
    const int32 Count=bHit?20:Pass==0?5:0;
    for(int32 I=0;I<Count;++I)
    {
        FVector Head,Tail,Side;
        if(bHit)
        {
            const float Angle=I*2*PI/Count+.14f*FMath::Sin(I*7.f);
            const FVector Out(FMath::Cos(Angle),FMath::Sin(Angle),.25f*FMath::Sin(I*3.f));
            const float Radius=25+Age*(260+I%4*65);
            Head=Out*(Radius+24); Tail=Out*Radius;
            Side=FVector(-Out.Y,Out.X,0)*2.5f;
        }
        else
        {
            const float Y=(I-4)*13.f;
            Head=FVector(22-I%3*13,Y,-24+I%3*20);
            Tail=Head-FVector(60+I%4*17+Age*100,0,0);
            Side=FVector(0,1.5f,1.f);
        }
        Quad(Tail,Head+Side,Head+(Head-Tail).GetSafeNormal()*12,Head-Side);
    }
    Mesh->CreateMeshSection_LinearColor(Pass,V,T,N,UV,TArray<FLinearColor>(),TArray<FProcMeshTangent>(),false);
    }
}
