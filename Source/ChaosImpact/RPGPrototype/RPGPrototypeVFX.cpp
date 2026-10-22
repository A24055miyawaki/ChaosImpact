#include "RPGPrototypeVFX.h"
#include "RPGOnline.h"
#include "RPGPrototype.h"
#include "ChaosImpactIceMeshes.h"
#include "ChaosImpactHazardZone.h"
#include "ChaosImpactTornado.h"
#include "ChaosImpactDriveBall.h"
#include "ChaosImpactLightning.h"
#include "ChaosImpactSfx.h"
#include "ChaosImpactSimaeBird.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "ProceduralMeshComponent.h"

namespace
{
using FMesh = ChaosImpactIceMeshes::FMeshBuffers;
namespace Effects = ChaosImpactBallTypes::Effects;
using ChaosImpactBallTypes::GetColor;
using ChaosImpactBallTypes::LoadEffect;
using ChaosImpactBallTypes::SetEffectFloat;
using ChaosImpactBallTypes::SetEffectColor;
using ChaosImpactBallTypes::SetEffectSize;
using ChaosImpactBallTypes::MakeAdditive;
using ChaosImpactBallTypes::MakeEmissive;
using ChaosImpactBallTypes::MakeIceSurface;
using ChaosImpactBallTypes::MakeIceCrystal;
using ChaosImpactBallTypes::MakeSnow;
using ChaosImpactBallTypes::PlayIceShatter;
using ChaosImpactBallTypes::PlaySnowBurst;
using ChaosImpactBallTypes::PlayBeamBurst;

// World-oriented strips/triangles, not P1-facing billboards: visible in every split-screen view.
void Quad(FMesh& M, const FVector& A, const FVector& B, const FVector& C, const FVector& D)
{
	const int32 Start=M.Vertices.Num();
	M.Vertices.Append({A,B,C,D});
	const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
	for(int32 I=0;I<4;++I) { M.Normals.Add(Normal); }
	M.UVs.Append({FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)});
	M.Triangles.Append({Start,Start+1,Start+2,Start,Start+2,Start+3,
		Start,Start+2,Start+1,Start,Start+3,Start+2});
}
void Arc(FMesh& M, float R, float Height, float Start, float Sweep, float Width, float Tilt=0)
{
	constexpr int32 Segments=32;
	for(int32 I=0;I<Segments;++I)
	{
		const float A=Start+Sweep*I/Segments, B=Start+Sweep*(I+1)/Segments;
		const float T=FMath::Sin(PI*(I+.5f)/Segments);
		const float W=Width*(.15f+.85f*T);
		const auto P=[&](float Angle,float Rad) { return FVector(FMath::Cos(Angle)*Rad,FMath::Sin(Angle)*Rad,Height+FMath::Sin(Angle)*Tilt); };
		Quad(M,P(A,R-W),P(A,R+W),P(B,R+W),P(B,R-W));
	}
}
UStaticMeshComponent* Shape(AActor* A, const TCHAR* Path, UMaterialInterface* Material)
{
	auto* C=NewObject<UStaticMeshComponent>(A);
	C->SetupAttachment(A->GetRootComponent());
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path)); C->SetMaterial(0,Material);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetGenerateOverlapEvents(false);
	C->SetCastShadow(false); C->RegisterComponent(); return C;
}
bool HasBudget(UWorld* World)
{
	if(!World || World->GetNetMode()==NM_DedicatedServer) { return false; }
	int32 VisualCount=0;
	for(TActorIterator<ARPGPrototypeVFX> It(World);It;++It) { if(++VisualCount>=64) { return false; } }
	return true;
}
}

ARPGPrototypeVFX::ARPGPrototypeVFX()
{
	PrimaryActorTick.bCanEverTick=true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot")));
	SetActorEnableCollision(false);
	// No ownership visibility filters: effects are shared world events, not private loot.
	bReplicates=false;
}
ARPGPrototypeVFX* ARPGPrototypeVFX::PlayField(ARPGPrototypeEffect* InField)
{
	if(!IsValid(InField) || !HasBudget(InField->GetWorld())) { return nullptr; }
	const FName K=InField->Effect;
	if(K==TEXT("Birds")) { return nullptr; } // The actual flock already uses imported birds and feathers.
	if(K==TEXT("Blast") || K==TEXT("Spread") || K==TEXT("Spark"))
	{
		return PlayImpact(InField->GetWorld(),InField->Item.Type,InField->GetActorLocation(),
			FMath::Clamp(InField->Radius/110,.45f,2.2f),K!=TEXT("Spark"));
	}
	const FTransform At(FRotator::ZeroRotator,InField->GetActorLocation());
	auto* V=InField->GetWorld()->SpawnActorDeferred<ARPGPrototypeVFX>(StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if(V)
	{
		V->Field=InField; V->Type=InField->Item.Type; V->Kind=K; V->bField=true;
		V->Radius=InField->Radius; V->Seconds=InField->Duration;
		V->Borrowed=PlayVersus(InField->GetWorld(),V->Type,K,InField->GetActorLocation(),InField->Radius,InField->Duration,InField->Source.Get());
		V->FinishSpawning(At);
	}
	return V;
}
AActor* ARPGPrototypeVFX::PlayVersus(UWorld* World,EChaosImpactBallType Type,FName Kind,const FVector& At,float Radius,float Seconds,APawn* Source,float Scale)
{
	if(!World || World->GetNetMode()==NM_DedicatedServer) { return nullptr; }
	// Fields: the VS zone of the same element, at the RPG field's own size and time (it follows the field; see Tick).
	if(Kind==TEXT("Cold")) { return AChaosImpactHazardZone::SpawnVisual(World,EChaosImpactBallType::Ice,At,Radius,Seconds,Source); }
	if(Kind==TEXT("Fire") || Kind==TEXT("Pillar")) { return AChaosImpactHazardZone::SpawnVisual(World,EChaosImpactBallType::Fire,At,Radius,Seconds,Source); }
	if(Kind==TEXT("Gravity")) { return AChaosImpactHazardZone::SpawnVisual(World,EChaosImpactBallType::Black,At,Radius,Seconds,Source); }
	if(Kind==TEXT("Smoke")) { return AChaosImpactHazardZone::SpawnVisual(World,EChaosImpactBallType::Smoke,At,Radius,Seconds,Source); }
	if(Kind==TEXT("Wind") || Kind==TEXT("WindGuard"))
	{ return AChaosImpactTornado::SpawnVisual(World,At,Seconds,FMath::Clamp(Radius/AChaosImpactTornado::CatchRadius,.45f,2.4f)); }
	if(!Kind.IsNone()) { return nullptr; }
	// Bursts where a ball ends.
	switch(Type)
	{
	case EChaosImpactBallType::Fire: return AChaosImpactHazardZone::SpawnVisual(World,Type,At,Radius,.35f,Source);
	case EChaosImpactBallType::Thunder: return AChaosImpactHazardZone::SpawnVisual(World,Type,At,Radius,-1,Source);
	case EChaosImpactBallType::Beam: return AChaosImpactHazardZone::SpawnVisual(World,Type,At,Radius,-1,Source);
	case EChaosImpactBallType::Snow: return AChaosImpactHazardZone::SpawnVisual(World,Type,At,Radius,-1,Source,Scale);
	case EChaosImpactBallType::Drive:
		AChaosImpactDriveBurst::Play(World,At,true);
		ChaosImpactSfx::PlayAt(World,EChaosImpactSfx::DriveBurst,At,.9f);
		return nullptr;
	default: return nullptr;
	}
}
ARPGPrototypeVFX* ARPGPrototypeVFX::PlayImpact(UWorld* World,EChaosImpactBallType InType,const FVector& At,float Scale,bool Terminal)
{
    if(World && World->GetNetMode()==NM_ListenServer) { if(auto* Relay=ARPGNetRelay::Get(World)) { Relay->MulticastImpact(InType,At,Scale,Terminal); } }
	if(!HasBudget(World) || InType==EChaosImpactBallType::Nova) { return nullptr; }
	if(Terminal)
	{
		// The VS burst of that ball (sized to the RPG reach). Black, smoke, wind and ice end in their field instead.
		switch(InType)
		{
		case EChaosImpactBallType::Fire: case EChaosImpactBallType::Thunder: case EChaosImpactBallType::Beam:
		case EChaosImpactBallType::Snow: case EChaosImpactBallType::Drive:
		{
			const float Reach=InType==EChaosImpactBallType::Thunder?150:InType==EChaosImpactBallType::Beam?120:InType==EChaosImpactBallType::Snow?120:110;
			PlayVersus(World,InType,NAME_None,At,Reach*FMath::Clamp(Scale,.25f,2.5f),-1,nullptr,FMath::Clamp(Scale,.6f,2.4f));
			return nullptr;
		}
		case EChaosImpactBallType::Black: case EChaosImpactBallType::Smoke: case EChaosImpactBallType::Wind: case EChaosImpactBallType::Ice:
			return nullptr;
		default: break;
		}
	}
	const FTransform Transform(FRotator::ZeroRotator,At);
	auto* V=World->SpawnActorDeferred<ARPGPrototypeVFX>(StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if(V)
	{
		V->Type=InType; V->bTerminal=Terminal; V->Radius=110*FMath::Clamp(Scale,.25f,2.5f);
		V->Seconds=Terminal?.75f:.4f; V->FinishSpawning(Transform);
	}
	return V;
}
UNiagaraComponent* ARPGPrototypeVFX::AddLoop(const TCHAR* Path,FVector Offset,float Scale)
{
	auto* System=LoadEffect(Path);
	auto* N=System?UNiagaraFunctionLibrary::SpawnSystemAttached(System,RootComponent,NAME_None,Offset,
		FRotator::ZeroRotator,EAttachLocation::KeepRelativeOffset,false,false,ENCPoolMethod::None,false):nullptr;
	if(N)
	{
		N->SetRelativeScale3D(FVector(Scale));
		SetEffectFloat(N,TEXT("Base Light Intentsity"),0);
		Loops.Add(N);
	}
	return N;
}
ARPGPrototypeVFX* ARPGPrototypeVFX::PlayLightning(UWorld* World,const FVector& Start,const FVector& End)
{
    if(World && World->GetNetMode()==NM_ListenServer) { if(auto* Relay=ARPGNetRelay::Get(World)) { Relay->MulticastLightning(Start,End); } }
	if(!HasBudget(World)) { return nullptr; }
	const FTransform At(FRotator::ZeroRotator,Start);
	auto* V=World->SpawnActorDeferred<ARPGPrototypeVFX>(StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if(V)
	{
		V->Type=EChaosImpactBallType::Thunder; V->Seconds=.3f; V->bTerminal=false;
		V->bChain=true; V->BoltEnd=End-Start; V->FinishSpawning(At);
	}
	return V;
}
void ARPGPrototypeVFX::BeginPlay()
{
	Super::BeginPlay(); Seed=GetUniqueID()*7919;
	RibbonLook=MakeAdditive(this,GetColor(Type),1.6f);
	DebrisLook=MakeAdditive(this,GetColor(Type),2.0f);
	Ribbons=ChaosImpactLightning::CreateComponent(this,RootComponent,RibbonLook);
	Debris=ChaosImpactLightning::CreateComponent(this,RootComponent,DebrisLook);
	FRandomStream Random(Seed);
	const int32 MoteCount=bField?20:(bTerminal?34:16);
	for(int32 I=0;I<MoteCount;++I)
	{
		const float A=Random.FRandRange(0,UE_TWO_PI);
		Motes.Add({FVector(FMath::Cos(A)*Random.FRandRange(100,330),FMath::Sin(A)*Random.FRandRange(100,330),Random.FRandRange(90,300)),
			static_cast<float>(Random.FRandRange(2,6)),static_cast<float>(Random.FRandRange(0,UE_TWO_PI))});
	}
	if(Borrowed.IsValid()) { } // Drawn by the VS effect; this actor only carries it along with the field.
	else if(bField) { BuildField(); } else { BuildImpact(); }
	for(const auto& N:Loops) { N->Activate(true); }
	// Hard limit even if a source disappears or a field never ticks again.
	SetLifeSpan(bField?12.0f:Seconds+.3f);
	UpdateGeometry(1);
}
void ARPGPrototypeVFX::BuildImpact()
{
	const float S=Radius/110;
	const FVector At=GetActorLocation();
	if(bTerminal)
	{
		switch(Type)
		{
		case EChaosImpactBallType::Fire:
			if(auto* System=LoadEffect(Effects::Explosion))
			{
				auto* N=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,At,FRotator::ZeroRotator,FVector(.36f*S));
				SetEffectColor(N,TEXT("Smoke Color"),FLinearColor(.12f,.08f,.05f));
			}
			ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::FireExplode,At,.7f); break;
		case EChaosImpactBallType::Ice: PlayIceShatter(this,At,.7f*S,28); ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::IceBurst,At,.65f); break;
		case EChaosImpactBallType::Snow: PlaySnowBurst(this,At,.7f*S); ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::SnowBurst,At,.65f); break;
		case EChaosImpactBallType::Beam: PlayBeamBurst(this,At,.7f*S); ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::BeamStrike,At,.65f); break;
		case EChaosImpactBallType::Thunder: ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::ThunderBurst,At,.65f); break;
		case EChaosImpactBallType::Drive: ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::DriveBurst,At,.65f); break;
		default: break;
		}
	}
	if(Type==EChaosImpactBallType::Ice || Type==EChaosImpactBallType::Snow)
	{
		Debris->SetMaterial(0,Type==EChaosImpactBallType::Ice?MakeIceCrystal(this,.9f):MakeSnow(this));
	}
	Light=NewObject<UPointLightComponent>(this); Light->SetupAttachment(RootComponent);
	Light->SetCastShadows(false); Light->SetLightColor(GetColor(Type));
	Light->SetAttenuationRadius(Radius*2.5f); Light->SetIntensity(180); Light->RegisterComponent();
}
void ARPGPrototypeVFX::BuildField()
{
	const float S=FMath::Clamp(Radius/150,.35f,3.0f);
	if(Kind==TEXT("Cold"))
	{
		Surface=Shape(this,TEXT("/Engine/BasicShapes/Plane.Plane"),MakeIceSurface(this));
		FRandomStream Random(Seed); FMesh IceGeometry;
		for(int32 I=0;I<14;++I)
		{
			const float A=I*UE_TWO_PI/14;
			ChaosImpactIceMeshes::AppendCrystal(IceGeometry,FTransform(FRotator(Random.FRandRange(-20,20),I*51,Random.FRandRange(-15,15)),
				FVector(FMath::Cos(A)*Radius*.86f,FMath::Sin(A)*Radius*.86f,0)),Random.FRandRange(4,9)*S,Random.FRandRange(18,50)*S,Random);
		}
		Crystals=ChaosImpactIceMeshes::CreateComponent(this,RootComponent,IceGeometry,MakeIceCrystal(this,.82f,.16f));
		auto* Mist=AddLoop(Effects::LastHitSmoke,FVector(0,0,5),.35f*S);
		SetEffectColor(Mist,TEXT("Smoke Color"),FLinearColor(.45f,.75f,1,.22f));
		SetEffectFloat(Mist,TEXT("SpawnRate"),4);
		PlayIceShatter(this,GetActorLocation(),.45f*S,18);
		ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::IceBurst,GetActorLocation(),.55f);
	}
	else if(Kind==TEXT("Fire") || Kind==TEXT("Pillar") || Kind==TEXT("Burn"))
	{
		const bool Burn=Kind==TEXT("Burn");
		const int32 FlameCount=Burn || Radius<100?1:3;
		for(int32 I=0;I<FlameCount;++I)
		{
			const float A=I*UE_TWO_PI/FlameCount;
			const FVector Offset=Burn?FVector(0,0,-30):FVector(FMath::Cos(A)*Radius*.4f,FMath::Sin(A)*Radius*.4f,0);
			auto* Flame=AddLoop(Effects::Fire,Offset,Burn?.32f:.65f*S);
			SetEffectFloat(Flame,TEXT("Flame Scale"),Kind==TEXT("Pillar")?3.0f:Burn?1.0f:1.6f);
			SetEffectFloat(Flame,TEXT("Smoke Spawn Scale"),Burn?.04f:.1f);
			SetEffectColor(Flame,TEXT("Smoke Color"),FLinearColor(.12f,.09f,.07f));
		}
		if(!Burn) { LoopSound=ChaosImpactSfx::PlayAttached(EChaosImpactSfx::FireLoop,RootComponent,.35f); }
	}
	else if(Kind==TEXT("Smoke"))
	{
		for(int32 I=0;I<3;++I)
		{
			const float A=I*UE_TWO_PI/3;
			auto* Cloud=AddLoop(Effects::Smoke,FVector(FMath::Cos(A)*Radius*.35f,FMath::Sin(A)*Radius*.35f,10),1);
			SetEffectColor(Cloud,TEXT("Smoke Color"),FLinearColor(.35f,.32f,.42f,.42f));
			SetEffectSize(Cloud,TEXT("Sprite Size"),Radius*1.35f);
			SetEffectSize(Cloud,TEXT("Uniform Sprite Size"),Radius*1.35f);
			SetEffectFloat(Cloud,TEXT("SpawnRate"),5);
		}
		ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::SmokePoof,GetActorLocation(),.65f);
	}
	else if(Kind==TEXT("Gravity"))
	{
		Core=Shape(this,TEXT("/Engine/BasicShapes/Sphere.Sphere"),MakeEmissive(this,FLinearColor(.006f,.002f,.014f),.1f));
		Core->SetRelativeLocation(FVector(0,0,45)); Core->SetRelativeScale3D(FVector(Radius*.004f));
		auto* Aura=AddLoop(Effects::DarkAura,FVector(0,0,20),.45f*S);
		SetEffectColor(Aura,TEXT("Base Color"),FLinearColor(.55f,.12f,1));
		ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::BlackOpen,GetActorLocation(),.65f);
	}
	else if(Kind==TEXT("Electric"))
	{
		auto* Arcs=AddLoop(Effects::Electricity,FVector(0,0,25),.7f*S);
		SetEffectColor(Arcs,TEXT("Base Color"),GetColor(Type));
	}
	else if(Kind==TEXT("Wind") || Kind==TEXT("WindGuard"))
	{
		LoopSound=ChaosImpactSfx::PlayAttached(EChaosImpactSfx::WindLoop,RootComponent,.4f);
		ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::WindTornado,GetActorLocation(),.55f);
	}
	else if(Kind==TEXT("Decoy"))
	{
		Core=Shape(this,TEXT("/Engine/BasicShapes/Sphere.Sphere"),MakeAdditive(this,GetColor(Type),.65f,1));
		Core->SetRelativeScale3D(FVector(.4f,.4f,1.25f)); Core->SetRelativeLocation(FVector(0,0,50));
	}
	else if(Kind==TEXT("Snow"))
	{
		FRandomStream Random(Seed); FMesh Snow;
		for(int32 I=0;I<22;++I)
		{
			const float A=Random.FRandRange(0,UE_TWO_PI), R=Radius*FMath::Sqrt(Random.FRand());
			ChaosImpactIceMeshes::AppendChunk(Snow,FTransform(FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,3)),Random.FRandRange(7,17),Random.FRandRange(3,8),Random);
		}
		Crystals=ChaosImpactIceMeshes::CreateComponent(this,RootComponent,Snow,MakeSnow(this));
	}
}
void ARPGPrototypeVFX::UpdateGeometry(float Fade)
{
	FMesh Lines, Flecks;
	FRandomStream Random(Seed+FMath::FloorToInt(Age*18));
	const float Grow=1-FMath::Exp(-Age*18);
	if(bChain)
	{
		ChaosImpactLightning::AppendBolt(Lines,FVector::ZeroVector,BoltEnd,5*Fade,FVector::UpVector,Random,3);
		// A crossed ribbon keeps the arc readable from oblique/shoulder cameras as well.
		ChaosImpactLightning::AppendBolt(Lines,FVector::ZeroVector,BoltEnd,3*Fade,FVector::RightVector,Random,1);
	}
	else if(bField)
	{
		if(Kind==TEXT("Wind") || Kind==TEXT("WindGuard"))
		{
			// Tapered helical ribbons: a moving vortex, open enough to see enemies inside.
			for(int32 Strand=0;Strand<3;++Strand)
			{
				for(int32 I=0;I<52;++I)
				{
					const auto P=[&](float T,float Side)
					{
						const float A=T*UE_TWO_PI*2+Age*8+Strand*UE_TWO_PI/3;
						const float R=Radius*(.22f+.73f*T);
						return FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,12+T*Radius*1.65f+Side*Radius*.025f);
					};
					Quad(Lines,P(I/52.0f,-1),P(I/52.0f,1),P((I+1)/52.0f,1),P((I+1)/52.0f,-1));
				}
			}
			Arc(Lines,Radius,5,Age*3,PI*1.6f,3);
		}
		else if(Kind==TEXT("Gravity"))
		{
			for(int32 I=0;I<5;++I) { Arc(Lines,Radius*(.28f+I*.13f),35+I*3,-Age*(3+I*.5f)+I,PI*1.65f,3+I*.5f,Radius*.08f); }
			Arc(Lines,Radius,4,-Age,PI*1.7f,2);
		}
		else if(Kind==TEXT("Electric") || Kind==TEXT("Delayed"))
		{
			for(int32 I=0;I<6;++I)
			{
				const float A=I*UE_TWO_PI/6+Age*.4f;
				const FVector End(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,10);
				ChaosImpactLightning::AppendBolt(Lines,FVector(0,0,30),End,3,FVector::UpVector,Random,1);
			}
		}
		else if(Kind==TEXT("Light") || Kind==TEXT("Orbit"))
		{
			for(int32 I=0;I<3;++I) { Arc(Lines,Radius*(.55f+I*.2f),15+I*12,Age*6+I*2,PI*.7f,4); }
		}
		else if(Kind!=TEXT("Burn") && Kind!=TEXT("Decoy") && Kind!=TEXT("Cold") && Kind!=TEXT("Snow"))
		{
			for(int32 I=0;I<4;++I) { Arc(Lines,Radius,4,I*PI*.5f,.95f,2.2f); }
		}
		// Attribute-specific particles with a bounded, batched mesh (not dozens of ticking actors).
		for(int32 I=0;I<Motes.Num();++I)
		{
			const auto& M=Motes[I];
			const float T=FMath::Frac(Age*.6f+I*.618f), A=M.Phase+Age*2;
			float R=Radius*(.25f+.7f*T);
			float Z=12+T*70;
			if(Kind==TEXT("Gravity")) { R=Radius*(1-T); Z=45+FMath::Sin(T*PI)*50; }
			if(Kind==TEXT("Wind") || Kind==TEXT("WindGuard")) { R=Radius*(.3f+.55f*T); Z=20+T*Radius*1.7f; }
			if(Kind==TEXT("Fire") || Kind==TEXT("Pillar") || Kind==TEXT("Burn")) { R*=.6f; Z=T*(Kind==TEXT("Pillar")?350:120); }
			const FVector At(FMath::Cos(A)*R,FMath::Sin(A)*R,Z);
			const FVector U(FMath::Cos(A)*M.Size,FMath::Sin(A)*M.Size,0), V(0,0,M.Size*2.2f);
			Quad(Flecks,At-U,At+V,At+U,At-V);
		}
	}
	else
	{
		const float T=FMath::Clamp(Age/Seconds,0.0f,1.0f);
		if(Type==EChaosImpactBallType::Thunder)
		{
			for(int32 I=0;I<8;++I)
			{
				const float A=I*UE_TWO_PI/8;
				ChaosImpactLightning::AppendBolt(Lines,FVector(0,0,8),FVector(FMath::Cos(A)*Radius*(.4f+T),FMath::Sin(A)*Radius*(.4f+T),15),4*(1-T),FVector::UpVector,Random,2);
			}
		}
		if(Type!=EChaosImpactBallType::Smoke)
		{
			for(int32 I=0;I<2;++I) { Arc(Lines,Radius*(.15f+(1-FMath::Pow(1-T,3))*(1-I*.25f)),I*6,0,UE_TWO_PI,4*(1-T)+.5f); }
		}
		FRandomStream Chunks(Seed);
		for(const auto& M:Motes)
		{
			const FVector At=M.Velocity*Age*(Radius/110)+FVector(0,0,-220*Age*Age);
			const float Size=M.Size*Fade;
			if(Type==EChaosImpactBallType::Ice || Type==EChaosImpactBallType::Snow)
			{
				ChaosImpactIceMeshes::AppendChunk(Flecks,FTransform(FRotator(Age*240,M.Phase*90,Age*120),At),Size*1.5f,Size,Chunks);
			}
			else
			{
				const FVector U=M.Velocity.GetSafeNormal()*Size*3, V(-FMath::Sin(M.Phase)*Size,FMath::Cos(M.Phase)*Size,Size);
				Quad(Flecks,At-U,At+V,At+U,At-V);
			}
		}
	}
	ChaosImpactLightning::SetMesh(Ribbons,Lines); ChaosImpactLightning::SetMesh(Debris,Flecks);
	if(RibbonLook) { RibbonLook->SetScalarParameterValue(TEXT("Intensity"),Fade*(bField?1.0f:2.6f)); }
	if(DebrisLook) { DebrisLook->SetScalarParameterValue(TEXT("Intensity"),Fade*(bField?.8f:2.3f)); }
	if(Surface) { Surface->SetRelativeScale3D(FVector(Radius*.02f*Grow,Radius*.02f*Grow,1)); Surface->SetVisibility(Fade>.02f); }
	if(Crystals) { Crystals->SetRelativeScale3D(FVector(Grow*Fade)); }
	if(Core)
	{
		const float S=Kind==TEXT("Gravity")?Radius*.004f*(.92f+.08f*FMath::Sin(Age*5)):1;
		Core->SetRelativeScale3D((Kind==TEXT("Gravity")?FVector(S):FVector(.4f,.4f,1.25f))*Grow*Fade);
	}
	if(Light) { Light->SetIntensity(180*FMath::Square(Fade)); }
}
void ARPGPrototypeVFX::Tick(float Dt)
{
	Super::Tick(Dt); Age+=Dt;
	if(bField && !bStopping)
	{
		if(Field.IsValid() && Field->GetWorld()->GetTimeSeconds()-Field->StartedAt<Field->Duration)
		{
			SetActorLocation(Field->GetActorLocation()); Radius=Field->Radius;
			if(Kind==TEXT("Wind") && Field->Item.Has(TEXT("WExpand")))
			{
				Radius*=FMath::Lerp(1.0f,Field->Item.Mod(TEXT("WExpand"))*.01f,FMath::Clamp(Age/Field->Duration,0.0f,1.0f));
			}
			if(Kind!=TEXT("Burn") && Kind!=TEXT("Delayed"))
			{
				FHitResult Hit; const FVector At=GetActorLocation();
				if(GetWorld()->LineTraceSingleByObjectType(Hit,At+FVector(0,0,150),At-FVector(0,0,500),FCollisionObjectQueryParams(ECC_WorldStatic)))
				{ SetActorLocation(FVector(At.X,At.Y,Hit.ImpactPoint.Z+3)); }
			}
		}
		else { bStopping=true; StopLoops(); }
	}
	float Fade=1;
	if(bStopping) { FadeAge+=Dt; Fade=FMath::Clamp(1-FadeAge/.55f,0.0f,1.0f); }
	else if(!bField) { Fade=FMath::Clamp(1-Age/Seconds,0.0f,1.0f); }
	if(AActor* Versus=Borrowed.Get())
	{
		// The field moves (wind, a drifting hole, one following an enemy): the VS effect goes with it.
		if(auto* Tornado=Cast<AChaosImpactTornado>(Versus)) { Tornado->SetVisualCenter(GetActorLocation()); }
		else { Versus->SetActorLocation(GetActorLocation()-FVector(0,0,3)); }
		if(bStopping && FadeAge<=Dt+KINDA_SMALL_NUMBER)
		{
			if(auto* Zone=Cast<AChaosImpactHazardZone>(Versus)) { Zone->EndVisual(); }
			if(auto* Tornado=Cast<AChaosImpactTornado>(Versus)) { Tornado->EndVisual(); }
		}
		if(Fade<=0) { Destroy(); }
		return;
	}
	MeshClock+=Dt;
	if(MeshClock>=1.0f/30) { MeshClock=0; UpdateGeometry(Fade); }
	if(Fade<=0) { Destroy(); }
}
void ARPGPrototypeVFX::StopLoops()
{
	if(Borrowed.IsValid()) { return; } // The VS effect makes its own closing sound.
	for(const auto& N:Loops) { if(N) { N->Deactivate(); } }
	ChaosImpactSfx::Stop(LoopSound,.25f); LoopSound=nullptr;
	if(bField && Kind==TEXT("Gravity")) { ChaosImpactSfx::PlayAt(this,EChaosImpactSfx::BlackClose,GetActorLocation(),.55f); }
}
void ARPGPrototypeVFX::EndPlay(const EEndPlayReason::Type Reason)
{
	ChaosImpactSfx::Stop(LoopSound,0); LoopSound=nullptr;
	for(const auto& N:Loops) { if(N) { N->DestroyComponent(); } }
	Super::EndPlay(Reason);
}
int32 ARPGPrototypeVFX::GetGeometryVertexCount() const
{
	int32 VertexCount=0;
	for(auto* C:{Ribbons.Get(),Debris.Get(),Crystals.Get()})
	{ if(C) { if(auto* S=C->GetProcMeshSection(0)) { VertexCount+=S->ProcVertexBuffer.Num(); } } }
	return VertexCount;
}
