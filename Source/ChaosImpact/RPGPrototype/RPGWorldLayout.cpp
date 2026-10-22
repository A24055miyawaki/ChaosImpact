#include "RPGWorldLayout.h"

namespace RPGWorld
{
namespace
{
using S=ESurface;
FVector P(float X,float Y,float Z) { return FVector(X,Y,Z); }

/**
 * The map (see Docs/RPGPrototype/RPGマップ2_冒険の地図.md for a drawing). Every road's ends lie inside an area or on
 * another road at the same height, so heights blend without steps.
 */
TArray<FArea> MakeAreas()
{
	return {
		{TEXT("Colony"),P(0,0,0),{2300,1900},S::Grass,TEXT("こもれびコロニー")},
		{TEXT("ColonyPlaza"),P(250,0,0),{950,760},S::Pavers,TEXT("こもれびコロニー")},
		{TEXT("Junction"),P(5400,0,80),{1050,900},S::Grass,TEXT("分かれ道の広場")},
		{TEXT("Quarry"),P(7400,-4300,240),{1300,1100},S::Dirt,TEXT("苔むした採石場")},
		{TEXT("Pit"),P(9100,-6100,60),{1100,900},S::Dirt,TEXT("採石場の底")},
		{TEXT("Forest"),P(2600,-8600,160),{2700,2000},S::Moss,TEXT("霧の古森")},
		{TEXT("Grove"),P(-200,-12500,260),{950,800},S::Moss,TEXT("古森の奥・木漏れ日の庭")},
		{TEXT("Tower"),P(12400,-4200,720),{1000,900},S::Grass,TEXT("風見の中継塔")},
		{TEXT("Ruins"),P(15600,-600,600),{1600,1250},S::Pavers,TEXT("水脈の遺跡")},
		{TEXT("Meadow"),P(6800,4600,40),{1800,1400},S::Grass,TEXT("せせらぎの草原")},
		{TEXT("Fort"),P(10200,9200,360),{1400,1150},S::Pavers,TEXT("崩れた砦")},
		{TEXT("Mill"),P(11600,4800,240),{1050,900},S::Grass,TEXT("旧水車小屋")},
		{TEXT("Outcrop"),P(13000,7000,560),{700,600},S::Grass,TEXT("水車小屋の高台")},
		{TEXT("Hills"),P(-5400,200,200),{1500,1300},S::Grass,TEXT("花風の丘")},
		{TEXT("Windmill"),P(-7200,1700,320),{900,800},S::Grass,TEXT("風車の丘")},
		{TEXT("Cove"),P(-2600,8900,20),{2000,1600},S::Sand,TEXT("さざなみの入り江")},
		{TEXT("Islet"),P(-4600,11900,20),{650,560},S::Sand,TEXT("入り江の小島")},
		{TEXT("CanyonGate"),P(19000,-600,600),{1100,1000},S::Pavers,TEXT("水脈峡谷・入口")},
		{TEXT("Lookout"),P(25000,-5800,1280),{850,700},S::Grass,TEXT("峡谷の見晴らし台")},
		{TEXT("Camp"),P(22800,5400,560),{950,800},S::Dirt,TEXT("旅人の野営跡")},
		{TEXT("Sluice"),P(26200,-400,960),{1500,1300},S::Pavers,TEXT("忘れられた大水門")},
		{TEXT("Garden"),P(29600,400,960),{1500,1300},S::Moss,TEXT("静かな水源庭園")},
		{TEXT("CaveHall"),P(33800,400,960),{900,800},S::Cave,TEXT("水晶洞窟・入口回廊")},
		{TEXT("Store"),P(35000,-3800,960),{700,600},S::Cave,TEXT("水晶洞窟・古い倉庫")},
		{TEXT("Pool"),P(35400,4300,960),{650,560},S::Cave,TEXT("水晶洞窟・地底の泉")},
		{TEXT("Crystal"),P(37200,200,960),{1250,1100},S::Cave,TEXT("水晶洞窟・水晶の広間")},
		{TEXT("BossHall"),P(40500,200,960),{1800,1550},S::Cave,TEXT("水晶洞窟・水底の大広間")},
	};
}
TArray<FRoad> MakeRoads()
{
	return {
		{{P(2100,0,0),P(3700,250,40),P(5400,0,80)},700,S::Dirt,TEXT("外縁の小径")},
		{{P(5400,0,80),P(5900,-1900,140),P(7400,-4300,240)},650,S::Dirt,TEXT("苔むした採石場")},
		{{P(8000,-4900,240),P(8600,-5500,160),P(9100,-6100,60)},440,S::Dirt,TEXT("採石場の底")},
		{{P(0,-1900,0),P(700,-4300,60),P(1800,-6800,160)},600,S::Dirt,TEXT("霧の古森")},
		{{P(4600,-8200,160),P(6200,-6400,200),P(7400,-4300,240)},520,S::Dirt,TEXT("霧の古森")},
		{{P(1600,-10300,200),P(600,-11400,240),P(-200,-12500,260)},330,S::Moss,TEXT("古森の奥")},
		{{P(7400,-4300,240),P(9200,-2900,330),P(10900,-2300,460),P(12900,-2700,600),P(12400,-4200,720)},540,S::Dirt,TEXT("風見の中継塔")},
		{{P(12900,-2700,600),P(14400,-1700,600),P(15600,-600,600)},600,S::Dirt,TEXT("水脈の遺跡")},
		{{P(5400,0,80),P(5900,2300,60),P(6800,4600,40)},700,S::Dirt,TEXT("せせらぎの草原")},
		{{P(0,1900,0),P(-900,4600,10),P(-2600,8900,20)},600,S::Dirt,TEXT("さざなみの入り江")},
		{{P(900,1700,0),P(2400,3600,20),P(4600,4900,30),P(6800,4600,40)},600,S::Dirt,TEXT("せせらぎの草原")},
		{{P(6800,4600,40),P(8200,6900,180),P(10200,9200,360)},560,S::Dirt,TEXT("崩れた砦")},
		{{P(6800,4600,40),P(9200,3900,140),P(11600,4800,240)},650,S::Dirt,TEXT("旧水車小屋")},
		{{P(11600,4800,240),P(13800,2700,420),P(15600,-600,600)},700,S::Dirt,TEXT("水脈の遺跡")},
		{{P(11600,4800,240),P(12500,6000,420),P(13000,7000,560)},420,S::Dirt,TEXT("水車小屋の高台")},
		{{P(-2000,0,0),P(-3500,-400,90),P(-5400,200,200)},600,S::Dirt,TEXT("花風の丘")},
		{{P(-5400,200,200),P(-6400,1000,260),P(-7200,1700,320)},520,S::Dirt,TEXT("風車の丘")},
		{{P(-5400,200,200),P(-4900,3600,110),P(-3800,6600,30),P(-2600,8900,20)},560,S::Dirt,TEXT("花風の丘")},
		// The shallows to the islet: under the sea, but only ankle deep.
		{{P(-3400,10200,-110),P(-3900,10900,-110),P(-4600,11900,-110)},300,S::Sand,TEXT("入り江の浅瀬")},
		{{P(15600,-600,600),P(17200,-600,600),P(19000,-600,600)},800,S::Pavers,TEXT("水脈峡谷・入口")},
		{{P(19000,-600,600),P(20200,-2800,760),P(22400,-3700,960),P(24600,-3100,1120)},600,S::Pavers,TEXT("水脈峡谷・上層回廊")},
		{{P(24600,-3100,1120),P(24900,-4600,1220),P(25000,-5800,1280)},460,S::Dirt,TEXT("峡谷の見晴らし台")},
		{{P(19000,-600,600),P(20000,1600,420),P(22000,2700,280),P(24200,2100,480)},700,S::Moss,TEXT("苔の水路")},
		{{P(22000,2700,280),P(22200,4100,420),P(22800,5400,560)},540,S::Dirt,TEXT("旅人の野営跡")},
		{{P(22400,-3700,960),P(23000,-1100,760),P(24200,2100,480)},520,S::Dirt,TEXT("峡谷の連絡路")},
		{{P(24600,-3100,1120),P(25600,-1800,1040),P(26200,-400,960)},600,S::Pavers,TEXT("忘れられた大水門")},
		{{P(24200,2100,480),P(25400,1200,720),P(26200,-400,960)},600,S::Pavers,TEXT("忘れられた大水門")},
		{{P(26200,-400,960),P(28000,-400,960),P(29600,400,960)},800,S::Pavers,TEXT("静かな水源庭園")},
		{{P(29600,400,960),P(31300,400,960)},700,S::Pavers,TEXT("静かな水源庭園")},
		{{P(31300,400,960),P(33000,400,960),P(33800,400,960)},620,S::Cave,TEXT("水晶洞窟・入口回廊")},
		{{P(33800,400,960),P(34800,-1700,960),P(36400,-2200,960),P(37200,200,960)},520,S::Cave,TEXT("水晶洞窟・北の回廊")},
		{{P(34800,-1700,960),P(35000,-3800,960)},380,S::Cave,TEXT("水晶洞窟・古い倉庫")},
		{{P(33800,400,960),P(34800,2500,960),P(36400,2700,960),P(37200,200,960)},520,S::Cave,TEXT("水晶洞窟・南の回廊")},
		{{P(34800,2500,960),P(35400,4300,960)},380,S::Cave,TEXT("水晶洞窟・地底の泉")},
		{{P(37200,200,960),P(38700,200,960),P(40500,200,960)},620,S::Cave,TEXT("水晶洞窟・ボス前")},
	};
}
TArray<FRock> MakeRocks()
{
	return {
		{{3200,-2700},{1100,850},700}, {{9800,-600},{1200,900},950}, {{4400,7400},{900,700},650},
		{{8600,-8900},{1300,1000},950}, {{14400,8800},{1000,1200},1150}, {{-7000,-3600},{1200,1000},650},
		{{21600,-6800},{2600,900},1900}, {{21400,7800},{2200,1000},1350}, {{21000,-300},{850,850},1500},
		{{27400,-3800},{1400,1000},1800}, {{27400,3500},{1400,1000},1800}, {{29900,-2900},{1000,800},1500},
		{{29900,3500},{1000,800},1500}, {{26800,-7000},{1000,800},1800}, {{13900,-5600},{800,700},1050},
		{{17200,-3800},{1300,900},1200}, {{17200,2800},{1300,900},1000}, {{-1400,-6200},{1000,900},700},
		{{6400,-11200},{1400,1000},900}, {{-9600,1200},{1200,1800},800},
	};
}
TArray<FGap> MakeGaps()
{
	// The brook: down from the falls under the crag, past the colony's east gate, then away from the cove road.
	return {{{{4600,8000},{3800,6600},{2600,5200},{1500,3300},{-300,3500},{-2200,3300},{-3200,3100}},620}};
}
float Noise(float X,float Y)
{
	return 85*FMath::PerlinNoise2D(FVector2D(X/760,Y/760))+28*FMath::PerlinNoise2D(FVector2D(X/180+31.7f,Y/180-12.3f));
}
float Smooth(float A,float B,float X)
{
	const float T=FMath::Clamp((X-A)/(B-A),0.0f,1.0f);
	return T*T*(3-2*T);
}
/** Distance to a polyline's nearest point, and the height interpolated there. */
float Segment(const TArray<FVector>& Points,float X,float Y,float& OutHeight,FVector* OutPoint=nullptr)
{
	float Best=MAX_flt; const FVector At(X,Y,0);
	for(int32 I=0;I+1<Points.Num();++I)
	{
		FVector A=Points[I],B=Points[I+1]; const float AZ=A.Z,BZ=B.Z; A.Z=B.Z=0;
		const FVector C=FMath::ClosestPointOnSegment(At,A,B); const float D=FVector::Dist2D(At,C);
		if(D<Best)
		{
			Best=D; const float T=FVector::Dist2D(A,C)/FMath::Max(1.0,FVector::Dist2D(A,B));
			OutHeight=FMath::Lerp(AZ,BZ,T); if(OutPoint) { *OutPoint=FVector(C.X,C.Y,OutHeight); }
		}
	}
	return Best;
}
float Polyline2D(const TArray<FVector2D>& Points,float X,float Y)
{
	float Best=MAX_flt;
	for(int32 I=0;I+1<Points.Num();++I)
	{
		const FVector C=FMath::ClosestPointOnSegment(FVector(X,Y,0),FVector(Points[I],0),FVector(Points[I+1],0));
		Best=FMath::Min(Best,static_cast<float>(FVector::Dist2D(FVector(X,Y,0),C)));
	}
	return Best;
}
}

const TArray<FRoad>& Roads() { static const TArray<FRoad> Data=MakeRoads(); return Data; }
const TArray<FArea>& Areas() { static const TArray<FArea> Data=MakeAreas(); return Data; }
const TArray<FRock>& Rocks() { static const TArray<FRock> Data=MakeRocks(); return Data; }
const TArray<FGap>& Gaps() { static const TArray<FGap> Data=MakeGaps(); return Data; }

bool IsCave(float X,float Y)
{
	const float Edge=30900+700*FMath::PerlinNoise2D(FVector2D(Y/1900+3.1f,.7f))+260*FMath::PerlinNoise2D(FVector2D(Y/500,4.2f));
	const float Side=650*FMath::PerlinNoise2D(FVector2D(X/2100,9.3f));
	return X>Edge && X<43200 && Y>-5600+Side && Y<6000-Side;
}
bool IsSea(float X,float Y) { return X<1800 && Y>5300; }

const FArea* FindArea(FName Id)
{ for(const auto& A:Areas()) { if(A.Id==Id) { return &A; } } return nullptr; }

FVector AreaPoint(FName Id,FVector2D Offset)
{
	const FArea* A=FindArea(Id); if(!A) { return FVector::ZeroVector; }
	const float X=A->At.X+Offset.X, Y=A->At.Y+Offset.Y;
	return FVector(X,Y,Height(X,Y));
}

float RoadDistance(float X,float Y,float* HalfWidth,FVector* Nearest)
{
	float Best=MAX_flt;
	for(const auto& R:Roads())
	{
		float H=0; FVector C; const float D=Segment(R.Points,X,Y,H,&C);
		if(D<Best) { Best=D; if(HalfWidth) { *HalfWidth=R.Width*.5f; } if(Nearest) { *Nearest=C; } }
	}
	return Best;
}

FSample Sample(float X,float Y)
{
	FSample Out;
	const bool bCave=IsCave(X,Y);
	const float Wobble=Noise(X,Y)*(bCave?.45f:1.0f);
	// Signed distance to every walkable shape (negative inside), the height there, and how firmly inside.
	float BestInside=-MAX_flt, NearestDistance=MAX_flt, NearestHeight=0, WeightSum=0, HeightSum=0;
	ESurface NearestFill=ESurface::Grass;
	const auto Consider=[&](float D,float H,ESurface Fill)
	{
		const float E=D-Wobble;
		if(E<NearestDistance) { NearestDistance=E; NearestHeight=H; NearestFill=Fill; }
		if(E<0) { const float W=FMath::Clamp(-E/260.0f,.02f,1.0f); WeightSum+=W; HeightSum+=W*H; }
		BestInside=FMath::Max(BestInside,-E);
	};
	for(const auto& A:Areas())
	{
		const float R=FMath::Sqrt(FMath::Square((X-A.At.X)/A.Radius.X)+FMath::Square((Y-A.At.Y)/A.Radius.Y));
		const float D=(R-1)*FMath::Min(A.Radius.X,A.Radius.Y);
		Consider(D,A.At.Z,A.Fill);
		// The area's own ground: its fill fades in from its edge.
		const float Fill=Smooth(60.0f,-260.0f,D-Wobble);
		switch(A.Fill)
		{
		case ESurface::Dirt: Out.Dirt=FMath::Max(Out.Dirt,Fill*.85f); break;
		case ESurface::Pavers: Out.Pavers=FMath::Max(Out.Pavers,Fill); break;
		case ESurface::Sand: Out.Sand=FMath::Max(Out.Sand,Fill); break;
		case ESurface::Cave: Out.Cave=FMath::Max(Out.Cave,Fill); break;
		case ESurface::Moss: Out.Moss=FMath::Max(Out.Moss,Fill); break;
		default: break;
		}
	}
	for(const auto& Road:Roads())
	{
		float H=0; const float C=Segment(Road.Points,X,Y,H);
		Consider(C-Road.Width*.5f,H,Road.Path);
		// The worn track down the middle of the road (a ragged edge).
		const float Track=Smooth(Road.Width*.31f,Road.Width*.17f,C+Wobble*.35f);
		switch(Road.Path)
		{
		case ESurface::Dirt: Out.Dirt=FMath::Max(Out.Dirt,Track); break;
		case ESurface::Pavers: Out.Pavers=FMath::Max(Out.Pavers,Track); break;
		case ESurface::Sand: Out.Sand=FMath::Max(Out.Sand,Smooth(Road.Width*.6f,Road.Width*.2f,C)); break;
		case ESurface::Cave: Out.Cave=FMath::Max(Out.Cave,Smooth(Road.Width*.7f,Road.Width*.4f,C)); break;
		case ESurface::Moss: Out.Moss=FMath::Max(Out.Moss,Track); break;
		default: break;
		}
	}
	float GapCut=MAX_flt;
	for(const auto& G:Gaps()) { GapCut=FMath::Min(GapCut,Polyline2D(G.Points,X,Y)-G.Width*.5f-Wobble*.4f); }

	const float Land=WeightSum>0?HeightSum/WeightSum:NearestHeight;
	// Off the walkable ground: up into rock (the cave's walls, or a crag) or down into the water.
	float Outside=VoidHeight+40*FMath::PerlinNoise2D(FVector2D(X/400,Y/400));
	float Band=NearestFill==ESurface::Sand?700.0f:150.0f;
	if(bCave) { Outside=NearestHeight+430+70*FMath::PerlinNoise2D(FVector2D(X/300,Y/300)); Band=110; }
	for(const auto& R:Rocks())
	{
		const float Rn=FMath::Sqrt(FMath::Square((X-R.At.X)/R.Radius.X)+FMath::Square((Y-R.At.Y)/R.Radius.Y));
		const float Rise=Smooth(1.08f,.86f,Rn+Wobble/FMath::Min(R.Radius.X,R.Radius.Y));
		if(Rise>0)
		{
			const float Top=R.Top+60*FMath::PerlinNoise2D(FVector2D(X/350+7,Y/350))+(1-Rn)*120;
			Outside=FMath::Lerp(Outside,FMath::Max(Outside,Top),Rise);
			if(Top>NearestHeight) { Band=FMath::Min(Band,120.0f); }
		}
	}
	const float Off=Smooth(0.0f,Band,NearestDistance);
	Out.Height=FMath::Lerp(Land,Outside,Off);
	if(bCave) { Out.Cave=1; Out.Dirt=Out.Pavers=Out.Sand=Out.Moss=0; }
	Out.Inside=BestInside;
	Out.bLand=BestInside>0;
	if(GapCut<100)
	{
		const float Cut=Smooth(100.0f,0.0f,GapCut);
		Out.Height=FMath::Lerp(Out.Height,FMath::Min(Out.Height,VoidHeight),Cut);
		if(GapCut<0) { Out.bLand=false; Out.Inside=FMath::Min(Out.Inside,GapCut); }
	}
	return Out;
}

float Height(float X,float Y) { return Sample(X,Y).Height; }

TArray<FBridge> Bridges()
{
	TArray<FBridge> Out;
	for(const auto& G:Gaps())
	{
		for(int32 I=0;I+1<G.Points.Num();++I)
		{
			for(const auto& Road:Roads())
			{
				for(int32 J=0;J+1<Road.Points.Num();++J)
				{
					FVector Hit;
					const FVector A(G.Points[I],0),B(G.Points[I+1],0),C(Road.Points[J].X,Road.Points[J].Y,0),D(Road.Points[J+1].X,Road.Points[J+1].Y,0);
					if(!FMath::SegmentIntersection2D(A,B,C,D,Hit)) { continue; }
					const float T=FVector::Dist2D(C,Hit)/FMath::Max(1.0,FVector::Dist2D(C,D));
					const FVector Along=(D-C).GetSafeNormal2D();
					const float Across=FMath::Abs(FVector::CrossProduct((B-A).GetSafeNormal2D(),Along).Z);
					Out.Add({FVector(Hit.X,Hit.Y,FMath::Lerp(Road.Points[J].Z,Road.Points[J+1].Z,T)),static_cast<float>(Along.Rotation().Yaw),
						G.Width/FMath::Max(.35f,Across)+520,FMath::Min(Road.Width*.7f,460.0f)});
				}
			}
		}
	}
	return Out;
}

FString RegionAt(const FVector& At)
{
	// The area it is deepest inside; else the nearest road's; else the nearest area's.
	const FArea* BestArea=nullptr; float Deepest=0;
	for(const auto& A:Areas())
	{
		if(A.Id==TEXT("ColonyPlaza")) { continue; }
		const float R=FMath::Sqrt(FMath::Square((At.X-A.At.X)/A.Radius.X)+FMath::Square((At.Y-A.At.Y)/A.Radius.Y));
		const float Depth=(1-R)*FMath::Min(A.Radius.X,A.Radius.Y);
		if(Depth>Deepest) { Deepest=Depth; BestArea=&A; }
	}
	if(BestArea) { return BestArea->Region; }
	const FRoad* BestRoad=nullptr; float Nearest=MAX_flt;
	for(const auto& Road:Roads()) { float H=0; const float D=Segment(Road.Points,At.X,At.Y,H); if(D<Nearest) { Nearest=D; BestRoad=&Road; } }
	if(BestRoad && Nearest<1500) { return BestRoad->Region; }
	float Closest=MAX_flt; const FArea* Near=nullptr;
	for(const auto& A:Areas()) { const float D=FVector::Dist2D(At,A.At); if(D<Closest) { Closest=D; Near=&A; } }
	return Near?Near->Region:TEXT("");
}
}
