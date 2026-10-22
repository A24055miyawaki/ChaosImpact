#include "RPGAdventure.h"
#include "RPGQuests.h"
#include "RPGSnowField.h"
#include "RPGHealing.h"
#include "RPGWorldLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

namespace
{
struct FAdventureRoad { TArray<FVector> Points; float Width; };
struct FAdventureClearing { FVector At; FVector2D Radius; };
struct FAdventureBridge { FVector At; float Yaw; };
TArray<FAdventureBridge> AdventureBridges()
{ return {{{5700,1100,80},79.695f},{{9250,-250,280},36.254f}}; }
TArray<FAdventureRoad> AdventureRoads()
{
    return {
        {{{0,0,0},{2500,0,0},{4200,300,0},{5500,0,80}},720},
        {{{5500,0,80},{5900,-1800,80},{7600,-3000,240},{9600,-2900,320},{11200,-2600,540},{13600,-1000,600}},720},
        {{{5500,0,80},{5900,2200,80},{7800,3400,80},{10400,3300,240},{12800,2200,480},{13600,-1000,600}},800},
        {{{7600,-3000,240},{8500,-800,240},{10000,300,320},{10400,3300,240}},640},
        {{{10400,3300,240},{12000,4700,560},{14100,4500,560}},560},
        {{{13600,-1000,600},{14900,-1000,600},{16600,-1000,600},{17900,-400,600}},800},
        {{{17900,-400,600},{19000,-2400,720},{21100,-3300,960},{23400,-2800,1120},{25100,-700,960}},740},
        {{{17900,-400,600},{18600,1600,400},{20700,2400,240},{23000,1900,480},{25100,-700,960}},800},
        {{{21100,-3300,960},{21700,-1100,760},{23000,1900,480}},640},
        {{{20700,2400,240},{20500,4200,400},{22000,5000,640}},600},
        {{{25100,-700,960},{27000,-700,960},{28500,400,960}},820},
        {{{23400,-2800,1120},{24000,-4400,1280},{25300,-4800,1280}},560}
    };
}
TArray<FAdventureClearing> AdventureClearings()
{
    return {{{0,0,0},{2100,1700}},{{5500,0,80},{1000,850}},{{7600,-3000,240},{1300,1000}},
        {{7800,3400,80},{1500,1100}},{{10400,3300,240},{1100,950}},{{11200,-2600,540},{850,850}},
        {{13600,-1000,600},{1500,1200}},{{14100,4500,560},{750,650}},
        {{17900,-400,600},{1100,1050}},{{21100,-3300,960},{1100,950}},
        {{20700,2400,240},{1400,1000}},{{25100,-700,960},{1500,1250}},
        {{22000,5000,640},{950,750}},{{25300,-4800,1280},{850,700}},
        {{28500,400,960},{1400,1250}}};
}
}

ARPGAdventureWorld::ARPGAdventureWorld()
{ SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("AdventureRoot"))); GetRootComponent()->SetMobility(EComponentMobility::Static); }
ARPGAdventureWorld* ARPGAdventureWorld::Find(const UWorld* World)
{ if(World) { for(TActorIterator<ARPGAdventureWorld> It(World);It;++It) { return *It; } } return nullptr; }
bool ARPGAdventureWorld::IsInColony(const FVector& At) const
{ const FVector L=GetActorTransform().InverseTransformPosition(At); return FMath::Square(L.X/2050)+FMath::Square(L.Y/1650)<1 && L.Z>-150 && L.Z<1200; }
FVector ARPGAdventureWorld::PlayerSpawn(int32 Index) const
{ return GetActorTransform().TransformPosition(FVector(-450,Index*170-200,120)); }
FString ARPGAdventureWorld::RegionName(const FVector& At) const
{
    // The snowfield names its own places; the rest are the adventure map's (RPGWorldLayout).
    const FString Snow=ARPGSnowField::RegionAt(GetWorld(),At);
    return Snow.IsEmpty()?RPGWorld::RegionAt(GetActorTransform().InverseTransformPosition(At)):Snow;
}
void ARPGAdventureWorld::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); Rebuild(); }
void ARPGAdventureWorld::BeginPlay() { Super::BeginPlay(); if(Parts.IsEmpty()) { Rebuild(); } ARPGQuestGiver::SpawnDefaults(GetWorld(),GetActorTransform()); ARPGSnowField::Ensure(GetWorld(),GetActorTransform()); ARPGHealHerb::SpawnField(GetWorld()); }
void ARPGAdventureWorld::Piece(const TCHAR* Shape,const TCHAR* Material,FVector At,FVector Dimensions,FRotator Rotation,bool Collision)
{
    const FString Key=FString(Shape)+TEXT("_")+Material+(Collision?TEXT("_solid"):TEXT("_decor"));
    auto*& Batch=Batches.FindOrAdd(Key);
    if(!Batch)
    {
        Batch=NewObject<UInstancedStaticMeshComponent>(this,MakeUniqueObjectName(this,UInstancedStaticMeshComponent::StaticClass(),FName(*Key)));
        Batch->SetupAttachment(GetRootComponent()); AddInstanceComponent(Batch); Parts.Add(Batch);
        Batch->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape)));
        const FString Asset=FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Materials/M_%s.M_%s"),Material,Material);
        Batch->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*Asset));
        Batch->SetCollisionEnabled(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        Batch->SetCollisionObjectType(ECC_WorldStatic); Batch->SetGenerateOverlapEvents(false);
        Batch->SetCanEverAffectNavigation(false); Batch->SetMobility(EComponentMobility::Static);
        Batch->RegisterComponent();
    }
    Batch->AddInstance(FTransform(Rotation,At,Dimensions/100.0f),false);
    EditingGroups.FindOrAdd(Batch).Add(EditingGroup.IsEmpty()?FString(Shape)+TEXT("_")+Material:EditingGroup);
}
void ARPGAdventureWorld::Lantern(FVector At,float Height)
{
    TGuardValue<FString> Group(EditingGroup,FString::Printf(TEXT("街灯_%.0f_%.0f"),At.X,At.Y));
    Piece(TEXT("Cube"),TEXT("Timber"),At+FVector(0,0,Height*.5),FVector(20,20,Height),FRotator::ZeroRotator,true);
    Piece(TEXT("Cube"),TEXT("Copper"),At+FVector(0,0,Height+7),FVector(68,68,14));
    Piece(TEXT("Cube"),TEXT("GlowWarm"),At+FVector(0,0,Height-30),FVector(34,34,50));
    Piece(TEXT("Cube"),TEXT("Copper"),At+FVector(0,0,Height-58),FVector(54,54,10));
}
void ARPGAdventureWorld::Tree(FVector At,float Scale,FRandomStream& Random)
{
    TGuardValue<FString> Group(EditingGroup,FString::Printf(TEXT("樹木_%.0f_%.0f"),At.X,At.Y));
    Piece(TEXT("Cylinder"),TEXT("Bark"),At+FVector(0,0,210*Scale),FVector(65,65,420)*Scale,FRotator::ZeroRotator,true);
    for(int32 I=0;I<5;++I)
    {
        const FVector Offset(Random.FRandRange(-120,120),Random.FRandRange(-130,130),Random.FRandRange(380,540));
        Piece(TEXT("Sphere"),I==0?TEXT("LeavesLight"):TEXT("Leaves"),At+Offset*Scale,FVector(310,290,230)*Scale,FRotator(Random.FRandRange(-15,15),Random.FRandRange(0,180),0));
    }
    for(int32 I=0;I<3;++I)
    { Piece(TEXT("Cube"),TEXT("Bark"),At+FVector(0,0,20*Scale),FVector(140,20,35)*Scale,FRotator(0,I*60+Random.FRandRange(0,15),0)); }
}
void ARPGAdventureWorld::House(FVector At,FRotator Facing,const TCHAR* Roof)
{
    TGuardValue<FString> Group(EditingGroup,FString::Printf(TEXT("建物_%.0f_%.0f"),At.X,At.Y));
    const auto P=[&](const TCHAR* Mat,FVector Offset,FVector Size,FRotator R=FRotator::ZeroRotator,bool Solid=false)
    { Piece(TEXT("Cube"),Mat,At+Facing.RotateVector(Offset),Size,FRotator(Facing.Quaternion()*R.Quaternion()),Solid); };
    P(TEXT("Stone"),{0,0,30},{690,550,60},FRotator::ZeroRotator,true);
    P(TEXT("Plaster"),{0,0,210},{620,480,360},FRotator::ZeroRotator,true);
    for(float X:{-305.0f,305.0f}) { for(float Y:{-235.0f,235.0f}) { P(TEXT("Timber"),{X,Y,235},{28,28,390}); } }
    for(float Y:{-248.0f,248.0f})
    {
        P(TEXT("Timber"),{0,Y,90},{625,22,25}); P(TEXT("Timber"),{0,Y,350},{625,22,26});
        for(float X:{-170.0f,170.0f})
        { P(TEXT("Timber"),{X,Y,235},{110,24,135}); P(TEXT("GlowWarm"),{X,Y*1.02f,235},{80,10,100}); P(TEXT("Timber"),{X,Y*1.04f,235},{12,12,110}); }
    }
    P(TEXT("Timber"),{0,-250,135},{100,28,210}); P(TEXT("Copper"),{35,-270,120},{10,10,10});
    for(float S:{-1.0f,1.0f})
    { P(Roof,{0,S*150,452},{730,370,40},FRotator(0,0,S*28)); }
    P(TEXT("Copper"),{0,0,540},{750,28,30});
    P(TEXT("Stone"),{180,100,520},{90,90,220}); P(TEXT("StoneDark"),{180,100,636},{106,106,16});
    P(TEXT("Plaster"),{0,-270,320},{210,15,46});
    P(TEXT("Stone"),{0,-310,18},{250,125,36},FRotator::ZeroRotator,true);
    Lantern(At+Facing.RotateVector(FVector(-260,-320,0)),210);
}
void ARPGAdventureWorld::Rebuild()
{
    if(bUseEditableScenery) { return; }
    for(auto Part:Parts) { if(IsValid(Part)) { Part->DestroyComponent(); } }
    Parts.Empty(); Batches.Empty(); EditingGroups.Empty(); TerrainTileCount=0;
    FRandomStream Random(Seed); const auto Roads=AdventureRoads(); const auto Clearings=AdventureClearings(); const auto Bridges=AdventureBridges();
    constexpr float Tile=200;
    for(float X=-2400;X<=30200;X+=Tile)
    {
        for(float Y=-5600;Y<=5800;Y+=Tile)
        {
            const FVector Center(X,Y,0); float Distance=MAX_flt,Height=0,Width=600; bool Land=false;
            for(const auto& Road:Roads)
            {
                for(int32 I=0;I+1<Road.Points.Num();++I)
                {
                    FVector A=Road.Points[I],B=Road.Points[I+1]; const float AZ=A.Z,BZ=B.Z; A.Z=B.Z=0;
                    const FVector Closest=FMath::ClosestPointOnSegment(Center,A,B); const float D=FVector::Dist2D(Center,Closest);
                    if(D<Distance) { Distance=D; Height=FMath::Lerp(AZ,BZ,FVector::Dist2D(A,Closest)/FMath::Max(1.0,FVector::Dist2D(A,B))); Width=Road.Width; }
                }
            }
            Land=Distance<Width*(.92f+.08f*FMath::Sin(X*.002f+Y*.003f));
            for(const auto& C:Clearings)
            {
                const float R=FMath::Square((X-C.At.X)/C.Radius.X)+FMath::Square((Y-C.At.Y)/C.Radius.Y);
                if(R<1.0f+.06f*FMath::Sin(X*.003f-Y*.002f))
                { Land=true; Height=FMath::Lerp(Height,static_cast<float>(C.At.Z),FMath::Clamp((Distance-240)/500,0.0f,1.0f)); }
            }
            for(const auto& Bridge:Bridges)
            { const FVector Local=FRotator(0,Bridge.Yaw,0).UnrotateVector(Center-FVector(Bridge.At.X,Bridge.At.Y,0)); if(FMath::Abs(Local.X)<380 && FMath::Abs(Local.Y)<800) { Land=false; } }
            if(!Land) { continue; }
            EditingGroup=FString::Printf(TEXT("地形_%.0f_%.0f"),X,Y);
            Height=FMath::RoundToFloat(Height/20)*20; ++TerrainTileCount;
            const bool Town=FMath::Square(X/2100)+FMath::Square(Y/1700)<1;
            const bool Road=Distance<Width*.42f || (Town && (FMath::Abs(Y)<230 || FMath::Abs(X+450)<230 || FVector2D(X-250,Y).Size()<480));
            const TCHAR* Top=Road?(Town?TEXT("Pavers"):(X>16500?TEXT("Pavers"):TEXT("Road"))):(Random.FRand()<.22f?TEXT("GrassLight"):TEXT("Grass"));
            Piece(TEXT("Cube"),TEXT("Cliff"),{X,Y,(Height-1195)*.5f},{Tile+1,Tile+1,Height+1105},FRotator::ZeroRotator,true);
            Piece(TEXT("Cube"),TEXT("StoneDark"),{X,Y,Height-105},{Tile+5,Tile+5,140});
            Piece(TEXT("Cube"),Top,{X,Y,Height-16},{Tile+1,Tile+1,32},FRotator::ZeroRotator,true);
            if(!Town && Distance>Width*.64f && Random.FRand()<.065f) { Tree({X,Y,Height},Random.FRandRange(.7f,1.2f),Random); }
            else if(!Town && Distance>Width*.55f && Random.FRand()<.11f)
            {
                const float R=Random.FRandRange(65,150);
                Piece(TEXT("Sphere"),TEXT("Stone"),{X,Y,Height+R*.22f},{R,R*.9f,R*.62f},FRotator(0,Random.FRandRange(0,180),0),true);
            }
            else if(!Road && Random.FRand()<.2f)
            {
                for(int32 I=0;I<3;++I)
                {
                    const FVector Flower(X+Random.FRandRange(-65,65),Y+Random.FRandRange(-65,65),Height+18);
                    Piece(TEXT("Sphere"),I%2?TEXT("FlowerGold"):TEXT("FlowerWhite"),Flower,FVector(16,16,12));
                }
            }
        }
    }
    // The negative space is a river gorge, not an enormous flat playable rectangle.
    EditingGroup=TEXT("背景_川面");
    Piece(TEXT("Cube"),TEXT("Water"),{13000,0,-760},{37000,15500,20});
    for(int32 I=0;I<26;++I)
    {
        EditingGroup=FString::Printf(TEXT("背景_遠景岩_%d"),I);
        const float X=Random.FRandRange(-4000,32000),Y=Random.FRand()<.5f?Random.FRandRange(-8500,-6500):Random.FRandRange(6500,8500);
        Piece(TEXT("Sphere"),TEXT("DistantRock"),{X,Y,Random.FRandRange(-600,-100)},{Random.FRandRange(1600,2800),Random.FRandRange(1100,1900),Random.FRandRange(1400,2300)});
    }
    // Colony: a useful central court, compact facilities, warm homes and everyday clutter.
    EditingGroup=TEXT("コロニー_広場と農園");
    House({-1200,-900,0},{0,0,0},TEXT("RoofClay")); House({-1200,850,0},{0,180,0},TEXT("RoofTeal"));
    House({650,-1100,0},{0,0,0},TEXT("RoofTeal")); House({650,1100,0},{0,180,0},TEXT("RoofClay"));
    House({-1700,0,0},{0,90,0},TEXT("RoofClay"));
    Piece(TEXT("Cylinder"),TEXT("Stone"),{250,0,34},{240,240,68},FRotator::ZeroRotator,true);
    Piece(TEXT("Cylinder"),TEXT("Water"),{250,0,72},{204,204,8});
    Piece(TEXT("Sphere"),TEXT("Copper"),{250,0,155},{95,95,95});
    Piece(TEXT("Sphere"),TEXT("GlowTeal"),{250,0,255},{62,62,62});
    for(float Y:{-520.0f,520.0f}) { Piece(TEXT("Cube"),TEXT("Pavers"),{650,Y,2},{850,35,4}); }
    for(float X:{225.0f,1075.0f}) { Piece(TEXT("Cube"),TEXT("Pavers"),{X,0,3},{35,1040,5}); }
    for(int32 I=0;I<6;++I)
    {
        const float Y=-700+I*280;
        Piece(TEXT("Cube"),TEXT("Timber"),{1950,Y,120},{55,55,240},FRotator::ZeroRotator,true);
        if(FMath::Abs(Y)>230) { Piece(TEXT("Cube"),TEXT("Timber"),{1950,Y+100,130},{28,220,26}); }
    }
    for(float Y:{-460.0f,460.0f})
    {
        Piece(TEXT("Cube"),TEXT("Stone"),{2200,Y,190},{200,200,380},FRotator::ZeroRotator,true);
        Piece(TEXT("Cube"),TEXT("Copper"),{2200,Y,390},{240,240,25}); Lantern({2350,Y,0},265);
    }
    for(int32 I=0;I<7;++I)
    {
        FVector At(-1750+I*240,-1400,0);
        Piece(TEXT("Cube"),TEXT("Soil"),At+FVector(0,0,10),{160,200,20});
        for(int32 J=0;J<3;++J) { Piece(TEXT("Sphere"),TEXT("LeavesLight"),At+FVector(0,J*55-55,40),{70,50,50}); }
    }
    for(FVector At:{FVector(-700,-580,0),FVector(-700,570,0),FVector(1250,-750,0),FVector(1250,750,0)})
    {
        Piece(TEXT("Cube"),TEXT("Timber"),At+FVector(0,0,45),{200,55,25});
        for(float S:{-70.0f,70.0f}) { Piece(TEXT("Cube"),TEXT("StoneDark"),At+FVector(S,0,20),{25,45,40}); }
    }
    // River crossings use planks and stone shoulders at the actual height of the walking route.
    for(const auto& Bridge:Bridges)
    {
        const FVector At=Bridge.At; const FRotator R(0,Bridge.Yaw,0);
        EditingGroup=FString::Printf(TEXT("橋_%.0f_%.0f"),At.X,At.Y);
        Piece(TEXT("Cube"),TEXT("Timber"),At+FVector(0,0,-2),{1210,900,12},R,true);
        for(int32 I=-6;I<=6;++I) { Piece(TEXT("Cube"),TEXT("Planks"),At+R.RotateVector(FVector(I*90,0,8)),{85,900,16},R,true); }
        for(float Y:{-470.0f,470.0f})
        { Piece(TEXT("Cube"),TEXT("Timber"),At+R.RotateVector(FVector(0,Y,100)),{1300,30,24},R); for(float X:{-540.0f,0.0f,540.0f}) { Piece(TEXT("Cube"),TEXT("Timber"),At+R.RotateVector(FVector(X,Y,60)),{30,30,120},R,true); } }
    }
    EditingGroup=TEXT("採石場_滝");
    Piece(TEXT("Cube"),TEXT("Water"),{7400,-4000,-260},{460,24,1000});
    for(int32 I=0;I<5;++I) { Piece(TEXT("Sphere"),TEXT("FlowerWhite"),{7250+I*75.0f,-4000,-720},{100,170,35}); }
    // A working-looking abandoned mill and a large landmark tower make branches distinct.
    House({10100,3900,240},{0,0,0},TEXT("RoofClay"));
    EditingGroup=TEXT("旧水車小屋_水車");
    Piece(TEXT("Cylinder"),TEXT("Timber"),{10100,3600,430},{360,360,32},{90,0,0});
    for(int32 I=0;I<8;++I) { Piece(TEXT("Cube"),TEXT("Planks"),{10100,3600,430},{390,32,24},{I*45.0f,0,0}); }
    EditingGroup=TEXT("風見の中継塔");
    for(int32 I=0;I<4;++I)
    { const float H=540+I*180; Piece(TEXT("Cylinder"),TEXT("Stone"),{11450,-2800,H+90},{420-I*35.0f,420-I*35.0f,180},FRotator::ZeroRotator,true); }
    Piece(TEXT("Cone"),TEXT("RoofTeal"),{11450,-2800,1360},{620,620,250});
    Piece(TEXT("Sphere"),TEXT("GlowTeal"),{11450,-2800,1160},{110,110,110});
    for(int32 I=0;I<7;++I)
    {
        const float A=I*2*PI/7; const FVector At(13800+FMath::Cos(A)*780,-1000+FMath::Sin(A)*780,600);
        bool OnRoad=false;
        for(const auto& Road:Roads) { for(int32 S=0;S+1<Road.Points.Num();++S)
        { FVector V0=Road.Points[S],V1=Road.Points[S+1]; V0.Z=V1.Z=0; if(FVector::Dist2D(At,FMath::ClosestPointOnSegment(FVector(At.X,At.Y,0),V0,V1))<460) { OnRoad=true; } } }
        if(OnRoad) { continue; }
        EditingGroup=FString::Printf(TEXT("遺跡_石柱_%d"),I);
        Piece(TEXT("Cube"),TEXT("StoneDark"),At+FVector(0,0,110),{180,180,220},{0,I*19.0f,0},true);
        Piece(TEXT("Cube"),TEXT("Stone"),At+FVector(0,0,230),{235,235,30});
        Piece(TEXT("Sphere"),TEXT("Leaves"),At+FVector(40,0,205),{170,150,40});
    }
    // This doorway now connects seamlessly to the next explorable region.
    EditingGroup=TEXT("遺跡_峡谷への門");
    for(float Y:{-340.0f,340.0f}) { Piece(TEXT("Cube"),TEXT("Stone"),{14900,-1000+Y,900},{220,220,600},FRotator::ZeroRotator,true); }
    Piece(TEXT("Cube"),TEXT("StoneDark"),{14900,-1000,1220},{240,960,160});
    Piece(TEXT("Cube"),TEXT("GlowTeal"),{14900,-1000,1160},{255,240,30});
    ContinuationScenery(Random);
    // Readable breadcrumbs, not UI labels pasted across the landscape.
    for(const auto& Road:Roads)
    { for(int32 I=1;I<Road.Points.Num();++I) { const FVector At=Road.Points[I]+FVector(0,Road.Width*.75f,0); Lantern(At,200); } }
}
