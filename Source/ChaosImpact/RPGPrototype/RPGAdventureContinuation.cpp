#include "RPGAdventure.h"

// Scenery only. All pieces are also used by the editor conversion.
void ARPGAdventureWorld::ContinuationScenery(FRandomStream& Random)
{
    const auto Block=[&](const TCHAR* Mat,FVector At,FVector Size,bool Solid=false,FRotator R=FRotator::ZeroRotator)
    { Piece(TEXT("Cube"),Mat,At,Size,R,Solid); };
    // Roofless corridor: frame the road without hiding the player or the ball.
    for(int32 I=0;I<6;++I)
    {
        const float X=15500+I*380.f;
        EditingGroup=FString::Printf(TEXT("峡谷入口_側壁_%d"),I);
        for(float S:{-1.f,1.f})
        {
            Block(TEXT("StoneDark"),{X,-1000+S*660,720},{270,200,240},true);
            Block(TEXT("Stone"),{X,-1000+S*660,860},{310,230,40});
            Block(TEXT("Leaves"),{X+40,-1000+S*690,889},{190,150,22});
        }
    }
    // An old aqueduct silhouettes the upper route against the gorge below.
    for(int32 I=0;I<5;++I)
    {
        const FVector Base(19500+I*450.f,-3900,-600);
        EditingGroup=FString::Printf(TEXT("上層水道_橋脚_%d"),I);
        Block(TEXT("StoneDark"),Base+FVector(0,0,620),{220,230,1240},true);
        Block(TEXT("Stone"),Base+FVector(0,0,1300),{290,300,120});
        Block(TEXT("Stone"),Base+FVector(0,0,1520),{460,500,100});
        Block(TEXT("Water"),Base+FVector(0,0,1580),{460,260,12});
        for(float S:{-1.f,1.f}) { Block(TEXT("StoneDark"),Base+FVector(0,S*230,1600),{460,40,140}); }
    }
    // Lower pools beside, never over, the walking lane.
    for(int32 I=0;I<4;++I)
    {
        EditingGroup=FString::Printf(TEXT("苔の水路_池_%d"),I);
        const FVector P(19000+I*850.f,3100,80+I*35.f);
        Block(TEXT("StoneDark"),P-FVector(0,0,60),{650,600,120});
        Block(TEXT("Water"),P,{580,530,15});
        for(float S:{-1.f,1.f}) { Block(TEXT("Stone"),P+FVector(0,S*300,30),{700,65,80}); }
        for(int32 J=0;J<4;++J) { Piece(TEXT("Sphere"),TEXT("LeavesLight"),P+FVector(J*100-180,260,60),{110,100,75}); }
    }
    EditingGroup=TEXT("苔の水路_段瀑");
    for(int32 I=0;I<3;++I)
    {
        Block(TEXT("Water"),{21350+I*170.f,3300,-50-I*170.f},{190,700,18});
        Block(TEXT("Water"),{21440+I*170.f,3300,-135-I*170.f},{20,700,170});
        for(int32 J=0;J<5;++J)
        { Piece(TEXT("Sphere"),TEXT("FlowerWhite"),{21440+I*170.f,3050+J*120.f,-215-I*170.f},{90,80,18}); }
    }
    EditingGroup=TEXT("野営跡_テント");
    Block(TEXT("Planks"),{22280,5180,670},{440,400,50},true);
    for(float S:{-1.f,1.f}) { Block(TEXT("RoofTeal"),{22280,5180+S*110,850},{470,270,20},false,{0,0,S*40}); }
    Block(TEXT("Timber"),{22280,5180,955},{510,20,20});
    EditingGroup=TEXT("野営跡_焚き火と荷物");
    for(int32 I=0;I<9;++I)
    {
        const float A=I*2*PI/9;
        Piece(TEXT("Sphere"),TEXT("Stone"),{21750+FMath::Cos(A)*105,4800+FMath::Sin(A)*105,665},{60,55,40});
    }
    Block(TEXT("Timber"),{21750,4800,660},{160,30,25},false,{0,35,0});
    Block(TEXT("Timber"),{21750,4800,680},{160,30,25},false,{0,-35,0});
    for(int32 I=0;I<3;++I) { Block(TEXT("Planks"),{22450+I*110.f,4740,700},{95,100,120},true); }
    Lantern({21400,4880,640},180);
    // Open sluice: tall machinery to either side, central path remains readable.
    for(float S:{-1.f,1.f})
    {
        EditingGroup=S<0?TEXT("大水門_北側機構"):TEXT("大水門_南側機構");
        Block(TEXT("StoneDark"),{25350,-700+S*1000,1170},{700,440,420},true);
        for(int32 I=0;I<4;++I) { Block(TEXT("Stone"),{25350,-700+S*1000,1000+I*125.f},{740,480,30}); }
        Block(TEXT("Copper"),{25350,-700+S*1000,1430},{540,360,90});
        Piece(TEXT("Cylinder"),TEXT("Copper"),{25350,-700+S*1000,1550},{270,270,60});
        for(int32 I=0;I<8;++I) { Block(TEXT("Timber"),{25350,-700+S*1000,1590},{330,24,24},false,{0,I*45.f,0}); }
        Block(TEXT("Water"),{25600,-700+S*1180,200},{420,22,1420});
        Piece(TEXT("Sphere"),TEXT("FlowerWhite"),{25600,-700+S*1180,-520},{470,220,45});
    }
    EditingGroup=TEXT("大水門_床の紋章");
    Piece(TEXT("Cylinder"),TEXT("StoneDark"),{26000,-700,965},{620,620,8});
    Piece(TEXT("Cylinder"),TEXT("Copper"),{26000,-700,971},{460,460,5});
    Piece(TEXT("Cylinder"),TEXT("Stone"),{26000,-700,975},{370,370,5});
    EditingGroup=TEXT("見晴らし台_望遠台");
    Piece(TEXT("Cylinder"),TEXT("Stone"),{25550,-5100,1330},{280,280,100},FRotator::ZeroRotator,true);
    Block(TEXT("Copper"),{25550,-5100,1420},{130,50,70},false,{0,15,0});
    for(int32 I=0;I<5;++I)
    {
        EditingGroup=FString::Printf(TEXT("水源庭園_石柱_%d"),I);
        const float A=(I*55.f+35)*PI/180.f;
        const FVector P(28500+FMath::Cos(A)*1000,400+FMath::Sin(A)*1000,960);
        Block(TEXT("StoneDark"),P+FVector(0,0,170),{180,190,340},true);
        Block(TEXT("Stone"),P+FVector(0,0,350),{230,230,35});
        Piece(TEXT("Sphere"),TEXT("Leaves"),P+FVector(30,0,380),{220,200,60});
    }
    EditingGroup=TEXT("水源庭園_湧水の鉢");
    Piece(TEXT("Cylinder"),TEXT("Stone"),{29000,1150,1000},{480,480,80},FRotator::ZeroRotator,true);
    Piece(TEXT("Cylinder"),TEXT("Water"),{29000,1150,1045},{400,400,12});
    Piece(TEXT("Sphere"),TEXT("GlowTeal"),{29000,1150,1110},{80,80,80});
    Tree({28000,1150,960},1.4f,Random); Tree({29100,-400,960},1.25f,Random);
}
