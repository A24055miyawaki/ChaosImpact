#include "RPGWorldBuilder.h"
#include "RPGWorldLayout.h"
#include "RPGAdventure.h"
#include "RPGPrototype.h"
#include "RPGTreasureChest.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/ParallelFor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DistanceFieldAtlas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SkyAtmosphereComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "MeshCardBuild.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace RPGWorldBuild
{
constexpr float TileSize=4000;
constexpr int32 TileQuads=80;
inline int32 TilesX() { return FMath::CeilToInt((RPGWorld::MaxX-RPGWorld::MinX)/TileSize); }
inline int32 TilesY() { return FMath::CeilToInt((RPGWorld::MaxY-RPGWorld::MinY)/TileSize); }
const TCHAR* Root=TEXT("/Game/ChaosImpact/RPGPrototype/Adventure");

#if WITH_EDITOR
/** Triangles as vertex positions (three each), with a colour per vertex; drawn with flat facets or smooth normals. */
struct FKitMesh
{
	TArray<FVector3f> Positions;
	TArray<FLinearColor> Colors;
	void Tri(const FVector3f& A,const FVector3f& B,const FVector3f& C,const FLinearColor& Color=FLinearColor::White)
	{ Positions.Append({A,B,C}); Colors.Append({Color,Color,Color}); }
};

bool SaveAsset(UObject* Asset,const FString& PackageName)
{
	UPackage* Package=Asset->GetPackage();
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
	return UPackage::SavePackage(Package,Asset,*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args);
}

UStaticMesh* PrepareMesh(const FString& Folder,const FString& Name,FString& OutPackage)
{
	OutPackage=Folder/Name;
	UPackage* Package=CreatePackage(*OutPackage); Package->FullyLoad();
	UStaticMesh* Mesh=FindObject<UStaticMesh>(Package,*Name);
	if(!Mesh) { Mesh=NewObject<UStaticMesh>(Package,*Name,RF_Public|RF_Standalone); }
	else
	{
		// A mesh loaded from before may still be building in the background; it is let finish before being replaced.
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
		if(GDistanceFieldAsyncQueue) { GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Mesh,false); }
		if(GCardRepresentationAsyncQueue) { GCardRepresentationAsyncQueue->BlockUntilBuildComplete(Mesh,false); }
	}
	return Mesh;
}

/** One-LOD kit mesh with flat facets (the low-poly look). */
bool SaveKitMesh(const FString& Name,const FKitMesh& Kit,bool bCollision)
{
	FString PackageName; UStaticMesh* Mesh=PrepareMesh(FString(Root)/TEXT("Meshes"),Name,PackageName);
	Mesh->GetStaticMaterials().Reset();
	Mesh->GetStaticMaterials().Add(FStaticMaterial(LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),TEXT("Main"),TEXT("Main")));
	Mesh->SetNumSourceModels(0);
	FStaticMeshSourceModel& Source=Mesh->AddSourceModel();
	Source.BuildSettings.bRecomputeNormals=false; Source.BuildSettings.bRecomputeTangents=true; Source.BuildSettings.bGenerateLightmapUVs=false;
	Source.BuildSettings.DistanceFieldResolutionScale=0;
	FMeshDescription* Description=Mesh->CreateMeshDescription(0);
	FStaticMeshAttributes Attributes(*Description); Attributes.Register();
	auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
	auto UVs=Attributes.GetVertexInstanceUVs(); auto Colors=Attributes.GetVertexInstanceColors();
	UVs.SetNumChannels(1);
	const FPolygonGroupID Group=Description->CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Main");
	for(int32 I=0;I+2<Kit.Positions.Num();I+=3)
	{
		const FVector3f A=Kit.Positions[I],B=Kit.Positions[I+1],C=Kit.Positions[I+2];
		const FVector3f Normal=FVector3f::CrossProduct(C-A,B-A).GetSafeNormal();
		TArray<FVertexInstanceID> Corner;
		for(int32 K=0;K<3;++K)
		{
			const FVertexID V=Description->CreateVertex(); Positions[V]=Kit.Positions[I+K];
			const FVertexInstanceID Inst=Description->CreateVertexInstance(V);
			Normals[Inst]=Normal; UVs.Set(Inst,0,FVector2f(Kit.Positions[I+K].X/100+.5f,Kit.Positions[I+K].Z/100+.5f));
			const FLinearColor& Color=Kit.Colors[I+K]; Colors[Inst]=FVector4f(Color.R,Color.G,Color.B,Color.A);
			Corner.Add(Inst);
		}
		Description->CreateTriangle(Group,Corner);
	}
	Mesh->CommitMeshDescription(0);
	Mesh->CreateBodySetup();
	if(UBodySetup* Body=Mesh->GetBodySetup()) { Body->CollisionTraceFlag=bCollision?CTF_UseComplexAsSimple:CTF_UseSimpleAsComplex; }
	Mesh->Build(true); Mesh->PostEditChange();
	return SaveAsset(Mesh,PackageName);
}

/** An icosphere of radius 50 (subdivided), its points pushed in and out by Bumpiness for a hand-made look. */
FKitMesh Blob(int32 Subdivisions,float Bumpiness,int32 Seed,FVector3f Squash=FVector3f(1,1,1),float FlatBottom=-1)
{
	const float T=(1+FMath::Sqrt(5.f))/2;
	TArray<FVector3f> V={{-1,T,0},{1,T,0},{-1,-T,0},{1,-T,0},{0,-1,T},{0,1,T},{0,-1,-T},{0,1,-T},{T,0,-1},{T,0,1},{-T,0,-1},{-T,0,1}};
	TArray<FIntVector> F={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
	for(auto& P:V) { P.Normalize(); }
	for(int32 S=0;S<Subdivisions;++S)
	{
		TMap<uint64,int32> Mid; TArray<FIntVector> Next;
		const auto Middle=[&](int32 A,int32 B)
		{
			const uint64 Key=(static_cast<uint64>(FMath::Min(A,B))<<32)|FMath::Max(A,B);
			if(const int32* Found=Mid.Find(Key)) { return *Found; }
			const int32 Index=V.Add(((V[A]+V[B])*.5f).GetSafeNormal()); Mid.Add(Key,Index); return Index;
		};
		for(const auto& Face:F)
		{
			const int32 A=Middle(Face.X,Face.Y),B=Middle(Face.Y,Face.Z),C=Middle(Face.Z,Face.X);
			Next.Append({{Face.X,A,C},{Face.Y,B,A},{Face.Z,C,B},{A,B,C}});
		}
		F=Next;
	}
	FRandomStream Random(Seed);
	for(auto& P:V)
	{
		P*=50*(1+Random.FRandRange(-Bumpiness,Bumpiness));
		P.X*=Squash.X; P.Y*=Squash.Y; P.Z*=Squash.Z;
		if(FlatBottom>-1 && P.Z<FlatBottom*50) { P.Z=FlatBottom*50; }
	}
	FKitMesh Out;
	for(const auto& Face:F) { Out.Tri(V[Face.X],V[Face.Y],V[Face.Z]); }
	return Out;
}
/** Radius 50, height 100 about its middle (like the engine shapes), Sides round; Top radius for a frustum. */
FKitMesh Prism(int32 Sides,float TopRadius,bool bCaps=true)
{
	FKitMesh Out;
	for(int32 I=0;I<Sides;++I)
	{
		const float A=I*UE_TWO_PI/Sides,B=(I+1)*UE_TWO_PI/Sides;
		const FVector3f A0(FMath::Cos(A)*50,FMath::Sin(A)*50,-50),B0(FMath::Cos(B)*50,FMath::Sin(B)*50,-50);
		const FVector3f A1(FMath::Cos(A)*TopRadius,FMath::Sin(A)*TopRadius,50),B1(FMath::Cos(B)*TopRadius,FMath::Sin(B)*TopRadius,50);
		Out.Tri(A0,A1,B0); if(TopRadius>.01f) { Out.Tri(B0,A1,B1); }
		if(bCaps)
		{
			Out.Tri(FVector3f(0,0,-50),A0,B0);
			if(TopRadius>.01f) { Out.Tri(FVector3f(0,0,50),B1,A1); }
		}
	}
	return Out;
}
/** A tuft of grass blades (base dark, tips light: the colour is read by the blade material). */
FKitMesh Tuft(int32 Seed)
{
	FKitMesh Out; FRandomStream Random(Seed);
	for(int32 I=0;I<7;++I)
	{
		const float A=Random.FRandRange(0,UE_TWO_PI),Lean=Random.FRandRange(.15f,.55f),H=Random.FRandRange(55,100),W=Random.FRandRange(5,8);
		const FVector3f Out2(FMath::Cos(A),FMath::Sin(A),0),Side(-FMath::Sin(A),FMath::Cos(A),0);
		const FVector3f Base=Out2*Random.FRandRange(0,12);
		const FVector3f Tip=Base+Out2*H*Lean+FVector3f(0,0,H);
		const FLinearColor Low(0,0,0,1),High(1,1,1,1);
		Out.Positions.Append({Base-Side*W,Tip,Base+Side*W}); Out.Colors.Append({Low,High,Low});
		Out.Positions.Append({Base+Side*W,Tip,Base-Side*W}); Out.Colors.Append({Low,High,Low});
	}
	return Out;
}
#endif
}

int32 URPGWorldBuilder::BuildKitMeshes()
{
#if WITH_EDITOR
	using namespace RPGWorldBuild;
	int32 Saved=0;
	Saved+=SaveKitMesh(TEXT("SM_KitBlob"),Blob(2,.08f,11),false);
	Saved+=SaveKitMesh(TEXT("SM_KitBlobB"),Blob(1,.14f,23),false);
	Saved+=SaveKitMesh(TEXT("SM_KitRock"),Blob(1,.26f,37,FVector3f(1.1f,.95f,.72f),-.35f),true);
	Saved+=SaveKitMesh(TEXT("SM_KitRockB"),Blob(1,.3f,53,FVector3f(.9f,1.15f,.85f),-.4f),true);
	Saved+=SaveKitMesh(TEXT("SM_KitCone"),Prism(8,0),false);
	Saved+=SaveKitMesh(TEXT("SM_KitCylinder"),Prism(10,50),true);
	Saved+=SaveKitMesh(TEXT("SM_KitFrustum"),Prism(8,34),true);
	Saved+=SaveKitMesh(TEXT("SM_KitTuft"),Tuft(5),false);
	Saved+=SaveKitMesh(TEXT("SM_KitTuftB"),Tuft(77),false);
	{
		FKitMesh Gem; const FVector3f Top(0,0,50),Bottom(0,0,-50);
		for(int32 I=0;I<6;++I)
		{
			const float A=I*UE_TWO_PI/6,B=(I+1)*UE_TWO_PI/6;
			const FVector3f P(FMath::Cos(A)*40,FMath::Sin(A)*40,8),Q(FMath::Cos(B)*40,FMath::Sin(B)*40,8);
			Gem.Tri(Top,Q,P); Gem.Tri(Bottom,P,Q);
		}
		Saved+=SaveKitMesh(TEXT("SM_KitGem"),Gem,false);
	}
	return Saved;
#else
	return 0;
#endif
}

int32 URPGWorldBuilder::GetTerrainTileCount() { return RPGWorldBuild::TilesX()*RPGWorldBuild::TilesY(); }
float URPGWorldBuilder::GetGroundHeight(float X,float Y) { return RPGWorld::Height(X,Y); }

int32 URPGWorldBuilder::BuildTerrain(UMaterialInterface* Material,int32 FirstTile,int32 LastTile)
{
#if WITH_EDITOR
	using namespace RPGWorldBuild;
	const int32 Count=TilesX()*TilesY();
	const int32 First=FirstTile<0?0:FirstTile, Last=LastTile<0?Count-1:FMath::Min(LastTile,Count-1);
	int32 Saved=0;
	for(int32 Tile=First;Tile<=Last;++Tile)
	{
		const int32 IX=Tile/TilesY(), IY=Tile%TilesY();
		const float X0=RPGWorld::MinX+IX*TileSize, Y0=RPGWorld::MinY+IY*TileSize;
		// Under water everywhere (nothing to walk on, no rock): a handful of quads is enough.
		bool bAnything=false;
		for(int32 I=0;I<=10 && !bAnything;++I) { for(int32 J=0;J<=10;++J) { if(RPGWorld::Height(X0+I*TileSize/10,Y0+J*TileSize/10)>-700) { bAnything=true; break; } } }
		const int32 Quads=bAnything?TileQuads:8;
		const int32 LodQuads[]={Quads,FMath::Max(4,Quads/2),FMath::Max(4,Quads/4)};
		const float LodScreens[]={1.0f,.3f,.1f};
		const int32 Lods=bAnything?3:1;
		const FString Name=FString::Printf(TEXT("SM_RPGTerrain_%02d_%02d"),IX,IY);
		FString PackageName; UStaticMesh* Mesh=PrepareMesh(FString(Root)/TEXT("Terrain"),Name,PackageName);
		Mesh->GetStaticMaterials().Reset();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Material,TEXT("Ground"),TEXT("Ground")));
		Mesh->SetNumSourceModels(0);
		for(int32 Lod=0;Lod<Lods;++Lod)
		{
			FStaticMeshSourceModel& Source=Mesh->AddSourceModel();
			Source.BuildSettings.bRecomputeNormals=false; Source.BuildSettings.bRecomputeTangents=true;
			Source.BuildSettings.bGenerateLightmapUVs=false; Source.BuildSettings.bRemoveDegenerates=false;
			Source.BuildSettings.DistanceFieldResolutionScale=0;
			Source.ScreenSize.Default=LodScreens[Lod];
			FMeshDescription* Description=Mesh->CreateMeshDescription(Lod);
			FStaticMeshAttributes Attributes(*Description); Attributes.Register();
			auto Positions=Attributes.GetVertexPositions(); auto Normals=Attributes.GetVertexInstanceNormals();
			auto UVs=Attributes.GetVertexInstanceUVs(); auto Colors=Attributes.GetVertexInstanceColors();
			UVs.SetNumChannels(2);
			const FPolygonGroupID Group=Description->CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("Ground");
			const int32 Q=LodQuads[Lod]; const float Step=TileSize/Q; const int32 N=Q+3;
			TArray<RPGWorld::FSample> Grid; Grid.SetNum(N*N);
			ParallelFor(N,[&](int32 I) { for(int32 J=0;J<N;++J) { Grid[I*N+J]=RPGWorld::Sample(X0+(I-1)*Step,Y0+(J-1)*Step); } });
			const auto At=[&](int32 I,int32 J) -> const RPGWorld::FSample& { return Grid[(I+1)*N+(J+1)]; };
			const int32 Side=Q+1;
			TArray<FVertexInstanceID> Instances; Instances.SetNum(Side*Side);
			const auto Make=[&](const FVector3f& Position,const FVector3f& Normal,const FVector4f& Color,const FVector2f& Extra,float WX,float WY)
			{
				const FVertexID V=Description->CreateVertex(); Positions[V]=Position;
				const FVertexInstanceID Inst=Description->CreateVertexInstance(V);
				Normals[Inst]=Normal; UVs.Set(Inst,0,FVector2f(WX/100,WY/100)); UVs.Set(Inst,1,Extra); Colors[Inst]=Color;
				return Inst;
			};
			for(int32 I=0;I<Side;++I)
			{
				for(int32 J=0;J<Side;++J)
				{
					const auto& S=At(I,J);
					const float DX=(At(I+1,J).Height-At(I-1,J).Height)/(2*Step), DY=(At(I,J+1).Height-At(I,J-1).Height)/(2*Step);
					const FVector3f Normal=FVector3f(-DX,-DY,1).GetSafeNormal();
					// Colour: dirt, pavers, sand, moss; second UVs: cave floor, and how near the cliff's lip.
					const FVector4f Color(S.Dirt,S.Pavers,S.Sand,S.Moss);
					const FVector2f Extra(S.Cave,FMath::Clamp(1-S.Inside/220.0f,0.0f,1.0f));
					Instances[I*Side+J]=Make(FVector3f(I*Step,J*Step,S.Height),Normal,Color,Extra,X0+I*Step,Y0+J*Step);
				}
			}
			for(int32 I=0;I<Q;++I)
			{
				for(int32 J=0;J<Q;++J)
				{
					const auto A=Instances[I*Side+J],B=Instances[(I+1)*Side+J],C=Instances[(I+1)*Side+J+1],D=Instances[I*Side+J+1];
					// Split each square along the diagonal that follows the ground (no false ridges on a cliff's edge).
					const float H0=Positions[Description->GetVertexInstanceVertex(A)].Z,H1=Positions[Description->GetVertexInstanceVertex(C)].Z;
					const float H2=Positions[Description->GetVertexInstanceVertex(B)].Z,H3=Positions[Description->GetVertexInstanceVertex(D)].Z;
					if(FMath::Abs(H0-H1)<=FMath::Abs(H2-H3)) { Description->CreateTriangle(Group,{A,C,B}); Description->CreateTriangle(Group,{A,D,C}); }
					else { Description->CreateTriangle(Group,{A,D,B}); Description->CreateTriangle(Group,{B,D,C}); }
				}
			}
			// Skirts hanging down every edge: no gaps where neighbours show another level of detail.
			const auto Skirt=[&](TArray<int32> Edge,const FVector3f& Outward,bool bFlip)
			{
				TArray<FVertexInstanceID> Low;
				for(int32 Index:Edge)
				{
					const FVector3f P=Positions[Description->GetVertexInstanceVertex(Instances[Index])];
					Low.Add(Make(P-FVector3f(0,0,600),Outward,Colors[Instances[Index]],FVector2f(0,1),0,0));
				}
				for(int32 K=0;K+1<Edge.Num();++K)
				{
					if(bFlip) { Description->CreateTriangle(Group,{Instances[Edge[K]],Low[K+1],Instances[Edge[K+1]]}); Description->CreateTriangle(Group,{Instances[Edge[K]],Low[K],Low[K+1]}); }
					else { Description->CreateTriangle(Group,{Instances[Edge[K]],Instances[Edge[K+1]],Low[K+1]}); Description->CreateTriangle(Group,{Instances[Edge[K]],Low[K+1],Low[K]}); }
				}
			};
			TArray<int32> South,North,West,East;
			for(int32 K=0;K<Side;++K) { South.Add(K); North.Add(Q*Side+K); West.Add(K*Side); East.Add(K*Side+Q); }
			Skirt(South,FVector3f(-1,0,0),false); Skirt(North,FVector3f(1,0,0),true); Skirt(West,FVector3f(0,-1,0),true); Skirt(East,FVector3f(0,1,0),false);
			Mesh->CommitMeshDescription(Lod);
		}
		Mesh->SetAutoComputeLODScreenSize(false);
		Mesh->LODForCollision=0;
		Mesh->CreateBodySetup();
		if(UBodySetup* Body=Mesh->GetBodySetup()) { Body->CollisionTraceFlag=CTF_UseComplexAsSimple; }
		Mesh->Build(true); Mesh->PostEditChange();
		Saved+=SaveAsset(Mesh,PackageName);
		UE_LOG(LogTemp,Display,TEXT("RPGWORLD terrain %s saved (%d/%d)%s"),*Name,Tile-First+1,Last-First+1,bAnything?TEXT(""):TEXT(" water only"));
	}
	return Saved;
#else
	return 0;
#endif
}

#if WITH_EDITOR
namespace RPGWorldBuild
{
using RPGWorld::Height;
enum class EBiome : uint8 { Village, Meadow, Rocky, Forest, Blossom, Beach, Ruins, Canyon, Garden, Cave };

struct FKit
{
	UWorld* World=nullptr;
	int32 Made=0;
	TMap<FString,UMaterialInterface*> Materials;
	TMap<FString,UStaticMesh*> Meshes;
	/** Where gameplay things stand: scenery keeps clear of these. */
	TArray<FVector4> Reserved;

	UMaterialInterface* Mat(const FString& Name)
	{
		if(UMaterialInterface** Found=Materials.Find(Name)) { return *Found; }
		UMaterialInterface* M=LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("%s/Materials/M_%s.M_%s"),Root,*Name,*Name),nullptr,LOAD_Quiet|LOAD_NoWarn);
		if(!M) { M=LoadObject<UMaterialInterface>(nullptr,*Name,nullptr,LOAD_Quiet|LOAD_NoWarn); }
		if(!M) { UE_LOG(LogTemp,Warning,TEXT("RPGWORLD missing material %s"),*Name); M=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")); }
		Materials.Add(Name,M); return M;
	}
	UStaticMesh* Mesh(const FString& Name)
	{
		if(UStaticMesh** Found=Meshes.Find(Name)) { return *Found; }
		FString Path=Name.StartsWith(TEXT("/"))?Name:Name.StartsWith(TEXT("Kit"))?FString::Printf(TEXT("%s/Meshes/SM_%s.SM_%s"),Root,*Name,*Name)
			:FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),*Name,*Name);
		UStaticMesh* M=LoadObject<UStaticMesh>(nullptr,*Path);
		if(!M) { UE_LOG(LogTemp,Warning,TEXT("RPGWORLD missing mesh %s"),*Path); M=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")); }
		Meshes.Add(Name,M); return M;
	}
	ARPGAdventureSceneryPiece* Piece(const FString& Label,const FString& Folder,const FVector& At,bool bMovable=false)
	{
		FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transactional; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* A=World->SpawnActor<ARPGAdventureSceneryPiece>(At,FRotator::ZeroRotator,Params);
		if(bMovable) { A->GetRootComponent()->SetMobility(EComponentMobility::Movable); }
		A->SetActorLabel(Label); A->SetFolderPath(FName(*(TEXT("RPGWorld/")+Folder))); A->Tags.Add(TEXT("RPGWorld"));
		++Made; return A;
	}
	/** A part: Size in cm (all the meshes are 100 across), At and rotation in the world. */
	UStaticMeshComponent* Part(AActor* A,const FString& MeshName,const FString& Material,const FVector& At,const FVector& Size,const FRotator& Rotation=FRotator::ZeroRotator,
		bool bSolid=false,bool bShadow=true)
	{
		auto* C=NewObject<UStaticMeshComponent>(A,MakeUniqueObjectName(A,UStaticMeshComponent::StaticClass(),FName(*FPaths::GetBaseFilename(MeshName))),RF_Transactional);
		C->SetupAttachment(A->GetRootComponent()); C->SetMobility(A->GetRootComponent()->Mobility);
		C->SetStaticMesh(Mesh(MeshName)); C->SetMaterial(0,Mat(Material));
		C->SetCollisionEnabled(bSolid?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision); C->SetCollisionObjectType(ECC_WorldStatic);
		C->SetCollisionResponseToAllChannels(bSolid?ECR_Block:ECR_Ignore);
		C->SetGenerateOverlapEvents(false); C->SetCanEverAffectNavigation(false); C->SetCastShadow(bShadow);
		A->AddInstanceComponent(C); C->RegisterComponent();
		C->SetWorldTransform(FTransform(Rotation,At,Size/100));
		return C;
	}
	FVector Ground(float X,float Y,float Lift=0) const { return FVector(X,Y,Height(X,Y)+Lift); }
	bool IsReserved(const FVector& At,float Margin) const
	{ for(const auto& R:Reserved) { if(FVector::Dist2D(At,FVector(R.X,R.Y,0))<R.W+Margin) { return true; } } return false; }
	void Reserve(const FVector& At,float Radius) { Reserved.Add(FVector4(At.X,At.Y,At.Z,Radius)); }
};

EBiome BiomeAt(float X,float Y)
{
	if(RPGWorld::IsCave(X,Y)) { return EBiome::Cave; }
	FName Best; float Nearest=MAX_flt;
	for(const auto& A:RPGWorld::Areas())
	{
		if(A.Id==TEXT("ColonyPlaza")) { continue; }
		const float R=FMath::Sqrt(FMath::Square((X-A.At.X)/A.Radius.X)+FMath::Square((Y-A.At.Y)/A.Radius.Y))*FMath::Min(A.Radius.X,A.Radius.Y);
		if(R<Nearest) { Nearest=R; Best=A.Id; }
	}
	static const TMap<FName,EBiome> Table={
		{TEXT("Colony"),EBiome::Village},{TEXT("Junction"),EBiome::Meadow},{TEXT("Meadow"),EBiome::Meadow},{TEXT("Mill"),EBiome::Meadow},{TEXT("Outcrop"),EBiome::Meadow},
		{TEXT("Quarry"),EBiome::Rocky},{TEXT("Pit"),EBiome::Rocky},{TEXT("Tower"),EBiome::Rocky},{TEXT("Fort"),EBiome::Rocky},
		{TEXT("Forest"),EBiome::Forest},{TEXT("Grove"),EBiome::Forest},{TEXT("Hills"),EBiome::Blossom},{TEXT("Windmill"),EBiome::Blossom},
		{TEXT("Cove"),EBiome::Beach},{TEXT("Islet"),EBiome::Beach},{TEXT("Ruins"),EBiome::Ruins},{TEXT("CanyonGate"),EBiome::Ruins},
		{TEXT("Lookout"),EBiome::Canyon},{TEXT("Camp"),EBiome::Canyon},{TEXT("Sluice"),EBiome::Canyon},{TEXT("Garden"),EBiome::Garden}};
	if(const EBiome* Found=Table.Find(Best)) { return *Found; }
	return X>17500?EBiome::Canyon:EBiome::Meadow;
}
FString RegionFolder(const FVector& At) { return RPGWorld::RegionAt(At); }

// ---------------------------------------------------------------- scenery pieces
void Tree(FKit& K,const FVector& At,float S,FRandomStream& R,int32 Style)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_樹木_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/樹木"),At);
	const float Yaw=R.FRandRange(0,360);
	if(Style==1)
	{
		// A pine: a short trunk under three tiers of needles.
		K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,90*S),FVector(48,48,180)*S,FRotator(0,Yaw,0),true);
		for(int32 I=0;I<3;++I)
		{ K.Part(A,TEXT("KitCone"),I==2?TEXT("LeavesLight"):TEXT("Pine"),At+FVector(0,0,(230+I*125)*S),FVector(330-I*80,330-I*80,260-I*40)*S,FRotator(0,Yaw+I*22,0)); }
		return;
	}
	if(Style==3)
	{
		// An old giant of the misty wood: a thick flared trunk, roots, a crown wider than a house.
		K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,260*S),FVector(190,190,520)*S,FRotator(0,Yaw,0),true);
		for(int32 I=0;I<5;++I)
		{
			const float Ang=Yaw+I*72+R.FRandRange(-12,12); const FVector Dir=FRotator(0,Ang,0).Vector();
			K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+Dir*95*S+FVector(0,0,30*S),FVector(60,60,230)*S,FRotator(0,Ang,0)+FRotator(-62,0,0),false);
		}
		for(int32 I=0;I<7;++I)
		{
			const FVector Off(R.FRandRange(-260,260),R.FRandRange(-260,260),R.FRandRange(520,760));
			K.Part(A,I%2?TEXT("KitBlob"):TEXT("KitBlobB"),I==0?TEXT("LeavesLight"):TEXT("Leaves"),At+Off*S,FVector(R.FRandRange(380,520),R.FRandRange(380,520),R.FRandRange(260,340))*S,FRotator(0,R.FRandRange(0,360),0));
		}
		return;
	}
	if(Style==4)
	{
		// A palm by the sea: a leaning, bending trunk and drooping fronds.
		FVector P=At;
		for(int32 I=0;I<4;++I)
		{
			const FVector Dir=(FVector::UpVector+FRotator(0,Yaw,0).Vector()*(.18f+I*.12f)).GetSafeNormal();
			K.Part(A,TEXT("KitCylinder"),TEXT("Bark"),P+Dir*55*S,FVector(34-I*3,34-I*3,115)*S,FRotationMatrix::MakeFromZ(Dir).Rotator(),I==0);
			P+=Dir*110*S;
		}
		for(int32 I=0;I<7;++I)
		{
			const float Ang=I*51.0f+R.FRandRange(-10,10);
			const FVector Dir=(FRotator(0,Ang,0).Vector()+FVector(0,0,-.25f)).GetSafeNormal();
			K.Part(A,TEXT("KitBlobB"),TEXT("LeavesLight"),P+Dir*120*S,FVector(260,70,24)*S,FRotationMatrix::MakeFromX(Dir).Rotator());
		}
		return;
	}
	if(Style==5)
	{
		// A dead tree: bare forks.
		K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,150*S),FVector(40,40,300)*S,FRotator(0,Yaw,0),true);
		for(int32 I=0;I<3;++I)
		{ K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,(210+I*40)*S)+FRotator(0,Yaw+I*120,0).Vector()*45*S,FVector(16,16,150)*S,FRotator(0,Yaw+I*120,0)+FRotator(-50,0,0)); }
		return;
	}
	// A round tree (Style 2: in blossom).
	const TCHAR* Crown=Style==2?TEXT("Blossom"):TEXT("Leaves");
	K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,170*S),FVector(60,60,340)*S,FRotator(0,Yaw,0),true);
	for(int32 I=0;I<5;++I)
	{
		const FVector Off(R.FRandRange(-110,110),R.FRandRange(-110,110),R.FRandRange(340,470));
		K.Part(A,I%2?TEXT("KitBlob"):TEXT("KitBlobB"),I==0?(Style==2?TEXT("BlossomLight"):TEXT("LeavesLight")):Crown,At+Off*S,FVector(R.FRandRange(240,310),R.FRandRange(240,310),R.FRandRange(190,240))*S,FRotator(0,R.FRandRange(0,360),0));
	}
}
void Rock(FKit& K,const FVector& At,float S,FRandomStream& R,const FString& Folder=TEXT(""))
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_岩_%.0f_%.0f"),At.X,At.Y),(Folder.IsEmpty()?RegionFolder(At):Folder)+TEXT("/岩"),At);
	const int32 Count=R.RandRange(1,3);
	for(int32 I=0;I<Count;++I)
	{
		const float Size=(I==0?1:R.FRandRange(.4f,.7f))*S;
		const FVector Off=I==0?FVector::ZeroVector:FRotator(0,R.FRandRange(0,360),0).Vector()*S*R.FRandRange(60,110);
		K.Part(A,R.FRand()<.5f?TEXT("KitRock"):TEXT("KitRockB"),TEXT("Rock"),At+Off+FVector(0,0,-8*Size),FVector(R.FRandRange(140,200),R.FRandRange(130,190),R.FRandRange(100,150))*Size,
			FRotator(R.FRandRange(-8,8),R.FRandRange(0,360),R.FRandRange(-8,8)),Size>.6f);
	}
}
void Bush(FKit& K,const FVector& At,float S,FRandomStream& R,bool bFlowers)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_茂み_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/茂み"),At);
	for(int32 I=0;I<3;++I)
	{
		const FVector Off(R.FRandRange(-50,50),R.FRandRange(-50,50),R.FRandRange(30,55));
		K.Part(A,TEXT("KitBlobB"),I==0?TEXT("LeavesLight"):TEXT("Leaves"),At+Off*S,FVector(R.FRandRange(110,150),R.FRandRange(110,150),R.FRandRange(80,110))*S,FRotator(0,R.FRandRange(0,360),0));
	}
	if(bFlowers) { for(int32 I=0;I<4;++I) { K.Part(A,TEXT("KitGem"),I%2?TEXT("FlowerPink"):TEXT("FlowerWhite"),At+FVector(R.FRandRange(-55,55),R.FRandRange(-55,55),R.FRandRange(75,100))*S,FVector(16,16,12)*S,FRotator(0,R.FRandRange(0,360),0),false,false); } }
}
void Mushroom(FKit& K,const FVector& At,float S,FRandomStream& R,bool bGlow)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_キノコ_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/キノコ"),At);
	const int32 Count=R.RandRange(1,3);
	for(int32 I=0;I<Count;++I)
	{
		const float Sz=S*(I==0?1:R.FRandRange(.45f,.7f)); const FVector P=At+(I?FRotator(0,R.FRandRange(0,360),0).Vector()*60*S:FVector::ZeroVector);
		K.Part(A,TEXT("KitFrustum"),TEXT("Plaster"),P+FVector(0,0,45*Sz),FVector(26,26,90)*Sz,FRotator::ZeroRotator,I==0);
		K.Part(A,TEXT("KitBlob"),bGlow?TEXT("GlowMushroom"):TEXT("Mushroom"),P+FVector(0,0,95*Sz),FVector(110,110,55)*Sz,FRotator(0,R.FRandRange(0,360),0));
	}
}
void Crystal(FKit& K,const FVector& At,float S,FRandomStream& R,const TCHAR* Material=TEXT("Crystal"))
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_水晶_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/水晶"),At);
	const int32 Count=R.RandRange(3,6);
	for(int32 I=0;I<Count;++I)
	{
		const FRotator Tilt(R.FRandRange(-28,28),R.FRandRange(0,360),R.FRandRange(-28,28));
		const float H=R.FRandRange(80,220)*S*(I==0?1.4f:1);
		K.Part(A,TEXT("KitGem"),Material,At+Tilt.RotateVector(FVector(0,0,H*.42f)),FVector(H*.32f,H*.32f,H),Tilt,I==0,false);
	}
}
void Lantern(FKit& K,const FVector& At,float H=230)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_街灯_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/街灯"),At);
	K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),At+FVector(0,0,H*.5f),FVector(18,18,H),FRotator::ZeroRotator,true);
	K.Part(A,TEXT("Cube"),TEXT("Timber"),At+FVector(28,0,H-10),FVector(70,10,10));
	K.Part(A,TEXT("KitFrustum"),TEXT("Copper"),At+FVector(58,0,H-14),FVector(40,40,14),FRotator(180,0,0));
	K.Part(A,TEXT("KitGem"),TEXT("GlowWarm"),At+FVector(58,0,H-44),FVector(26,26,40),FRotator::ZeroRotator,false,false);
}
void Fence(FKit& K,const FVector& From,const FVector& To,const FString& Folder)
{
	const float Length=FVector::Dist2D(From,To); const int32 Posts=FMath::Max(2,FMath::CeilToInt(Length/180)+1);
	auto* A=K.Piece(FString::Printf(TEXT("RPG_柵_%.0f_%.0f"),From.X,From.Y),Folder+TEXT("/柵"),From);
	FVector Prev;
	for(int32 I=0;I<Posts;++I)
	{
		const float T=I/float(Posts-1); FVector P=FMath::Lerp(From,To,T); P.Z=Height(P.X,P.Y);
		K.Part(A,TEXT("Cube"),TEXT("Timber"),P+FVector(0,0,55),FVector(16,16,110),FRotator(0,(To-From).Rotation().Yaw,0),true);
		if(I>0)
		{
			for(float Z:{45.0f,85.0f})
			{
				const FVector M=(Prev+P)*.5f+FVector(0,0,Z); const FVector D=P-Prev;
				K.Part(A,TEXT("Cube"),TEXT("Planks"),M,FVector(D.Size()+10,8,12),D.Rotation());
			}
		}
		Prev=P;
	}
}
void Sign(FKit& K,const FVector& At,float Yaw)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_道標_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/道標"),At);
	K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),At+FVector(0,0,90),FVector(16,16,180),FRotator::ZeroRotator,true);
	for(int32 I=0;I<2;++I)
	{
		const FRotator Facing(0,Yaw+I*140,0);
		K.Part(A,TEXT("Cube"),TEXT("Planks"),At+FVector(0,0,150-I*38)+Facing.Vector()*40,FVector(90,8,26),Facing);
		K.Part(A,TEXT("Cone"),TEXT("Planks"),At+FVector(0,0,150-I*38)+Facing.Vector()*92,FVector(24,8,26),Facing+FRotator(-90,0,0));
	}
}
void Barrels(FKit& K,const FVector& At,FRandomStream& R)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_樽と木箱_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/小物"),At);
	for(int32 I=0;I<3;++I)
	{
		const FVector P=At+FVector(R.FRandRange(-70,70),R.FRandRange(-70,70),0);
		if(R.FRand()<.5f)
		{
			K.Part(A,TEXT("KitCylinder"),TEXT("Planks"),P+FVector(0,0,45),FVector(64,64,90),FRotator(0,R.FRandRange(0,90),0),true);
			for(float Z:{18.0f,72.0f}) { K.Part(A,TEXT("KitCylinder"),TEXT("Copper"),P+FVector(0,0,Z),FVector(68,68,8)); }
		}
		else { K.Part(A,TEXT("Cube"),TEXT("Planks"),P+FVector(0,0,35),FVector(70,70,70),FRotator(0,R.FRandRange(0,90),0),true); }
	}
}
void House(FKit& K,const FVector& At,float Yaw,const TCHAR* Roof,FRandomStream& R)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_建物_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/建物"),At);
	const FRotator F(0,Yaw,0);
	const auto P=[&](const TCHAR* Mesh,const TCHAR* Mat,FVector Off,FVector Size,FRotator Rot=FRotator::ZeroRotator,bool Solid=false)
	{ K.Part(A,Mesh,Mat,At+F.RotateVector(Off),Size,FRotator(F.Quaternion()*Rot.Quaternion()),Solid); };
	P(TEXT("Cube"),TEXT("Stone"),{0,0,30},{700,560,60},FRotator::ZeroRotator,true);
	P(TEXT("Cube"),TEXT("Plaster"),{0,0,210},{620,480,330},FRotator::ZeroRotator,true);
	for(float X:{-305.0f,305.0f}) { for(float Y:{-235.0f,235.0f}) { P(TEXT("Cube"),TEXT("Timber"),{X,Y,215},{30,30,350}); } }
	for(float Y:{-248.0f,248.0f})
	{
		P(TEXT("Cube"),TEXT("Timber"),{0,Y,70},{625,22,24}); P(TEXT("Cube"),TEXT("Timber"),{0,Y,370},{625,22,26});
		for(float X:{-170.0f,170.0f})
		{ P(TEXT("Cube"),TEXT("Timber"),{X,Y,220},{112,24,130}); P(TEXT("Cube"),TEXT("GlowWarm"),{X,Y*1.02f,220},{82,10,96}); P(TEXT("Cube"),TEXT("Timber"),{X,Y*1.045f,220},{10,10,100});
		  P(TEXT("Cube"),TEXT("Planks"),{X,Y*1.09f,150},{110,30,26}); for(int32 I=0;I<3;++I) { P(TEXT("KitGem"),I%2?TEXT("FlowerGold"):TEXT("FlowerPink"),{X-30+I*30.0f,Y*1.09f,172},{22,22,18}); } }
	}
	P(TEXT("Cube"),TEXT("Timber"),{0,-252,125},{110,24,200}); P(TEXT("KitGem"),TEXT("Copper"),{38,-268,120},{12,12,12});
	// Roof: two slabs and a ridge, eaves over the walls; a chimney.
	for(float S:{-1.0f,1.0f}) { P(TEXT("Cube"),Roof,{0,S*160,470},{760,390,42},FRotator(0,0,S*30)); }
	P(TEXT("Cube"),TEXT("Timber"),{0,0,568},{780,36,34});
	P(TEXT("Cube"),TEXT("Stone"),{190,110,540},{90,90,240}); P(TEXT("Cube"),TEXT("StoneDark"),{190,110,666},{108,108,18});
	P(TEXT("Cube"),TEXT("Stone"),{0,-320,16},{260,130,32},FRotator::ZeroRotator,true);
	Barrels(K,At+F.RotateVector(FVector(-260,-330,0)),R);
}
void Waterfall(FKit& K,const FVector& Top,float Yaw,float Drop,float Width,const FString& Folder)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_滝_%.0f_%.0f"),Top.X,Top.Y),Folder+TEXT("/滝"),Top);
	// The pack's falling-water mesh hangs 18 m down from 0 along -Y; scaled to this drop and width.
	auto* Fall=K.Part(A,TEXT("/Game/WaterMaterials/Meshes/SM_Waterfall_Arc.SM_Waterfall_Arc"),TEXT("/Game/WaterMaterials/Materials/MIC_Waterfall_Arc.MIC_Waterfall_Arc"),
		Top,FVector(Width,Drop/18.2f,Drop/18.2f),FRotator(0,Yaw,0),false,false);
	Fall->SetWorldScale3D(FVector(Width/216.0f,Drop/1820.0f,Drop/2000.0f)); Fall->SetWorldLocation(Top-FVector(0,0,Drop*.9f));
}
void Bridge(FKit& K,const RPGWorld::FBridge& B)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_橋_%.0f_%.0f"),B.At.X,B.At.Y),RegionFolder(B.At)+TEXT("/橋"),B.At);
	const FRotator F(0,B.Yaw,0);
	const int32 Planks=FMath::CeilToInt(B.Length/70);
	// A gentle arch so its ends meet the banks.
	for(int32 I=0;I<Planks;++I)
	{
		const float T=(I+.5f)/Planks-.5f; const float Lift=40*(1-4*T*T);
		K.Part(A,TEXT("Cube"),I%3?TEXT("Planks"):TEXT("Timber"),B.At+F.RotateVector(FVector(T*B.Length,0,Lift*.6f-12)),FVector(66,B.Width,14),F,true);
	}
	K.Part(A,TEXT("Cube"),TEXT("Timber"),B.At+F.RotateVector(FVector(0,0,-28)),FVector(B.Length,B.Width*.8f,20),F,true);
	for(float Y:{-1.0f,1.0f})
	{
		for(int32 I=0;I<=4;++I)
		{
			const float T=I/4.0f-.5f;
			K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),B.At+F.RotateVector(FVector(T*B.Length,Y*B.Width*.52f,40*(1-4*T*T)+50)),FVector(22,22,110),F,true);
		}
		K.Part(A,TEXT("Cube"),TEXT("Timber"),B.At+F.RotateVector(FVector(0,Y*B.Width*.52f,105)),FVector(B.Length,12,12),F);
	}
}
void Pillar(FKit& K,const FVector& At,float H,bool bBroken,FRandomStream& R)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_石柱_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/遺跡"),At);
	K.Part(A,TEXT("Cube"),TEXT("StoneDark"),At+FVector(0,0,20),FVector(150,150,40),FRotator(0,R.FRandRange(0,90),0),true);
	K.Part(A,TEXT("KitCylinder"),TEXT("Stone"),At+FVector(0,0,H*.5f+40),FVector(90,90,H),FRotator::ZeroRotator,true);
	if(bBroken) { K.Part(A,TEXT("KitCylinder"),TEXT("Stone"),At+FVector(R.FRandRange(-180,180),R.FRandRange(-180,180),40),FVector(88,88,160),FRotator(90,R.FRandRange(0,180),0),true); }
	else { K.Part(A,TEXT("Cube"),TEXT("Stone"),At+FVector(0,0,H+60),FVector(140,140,36)); K.Part(A,TEXT("KitBlobB"),TEXT("Moss"),At+FVector(20,0,H+82),FVector(120,100,30)); }
}
void Wall(FKit& K,const FVector& From,const FVector& To,float H,const FString& Folder,bool bCrenel=true)
{
	const FVector D=To-From; const FRotator F(0,D.Rotation().Yaw,0);
	auto* A=K.Piece(FString::Printf(TEXT("RPG_城壁_%.0f_%.0f"),From.X,From.Y),Folder+TEXT("/砦"),From);
	const FVector M=(From+To)*.5f; const float Base=FMath::Min(Height(From.X,From.Y),Height(To.X,To.Y))-60;
	K.Part(A,TEXT("Cube"),TEXT("Stone"),FVector(M.X,M.Y,Base+H*.5f+30),FVector(D.Size2D(),110,H+60),F,true);
	if(bCrenel)
	{
		const int32 N=FMath::FloorToInt(D.Size2D()/120);
		for(int32 I=0;I<N;I+=2) { FVector P=From+D*((I+.5f)/N); P.Z=Base+H+90; K.Part(A,TEXT("Cube"),TEXT("Stone"),P,FVector(D.Size2D()/N,120,60),F); }
	}
}
void Tower(FKit& K,const FVector& At,float Radius,float H,const TCHAR* Roof,const FString& Folder)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_塔_%.0f_%.0f"),At.X,At.Y),Folder+TEXT("/塔"),At);
	K.Part(A,TEXT("KitFrustum"),TEXT("Stone"),At+FVector(0,0,H*.5f-40),FVector(Radius*2,Radius*2,H+80),FRotator::ZeroRotator,true);
	K.Part(A,TEXT("KitCylinder"),TEXT("StoneDark"),At+FVector(0,0,H+10),FVector(Radius*1.6f,Radius*1.6f,40));
	K.Part(A,TEXT("KitCone"),Roof,At+FVector(0,0,H+170),FVector(Radius*2,Radius*2,300));
	for(int32 I=0;I<4;++I) { K.Part(A,TEXT("Cube"),TEXT("GlowWarm"),At+FRotator(0,I*90+45,0).Vector()*Radius*.72f+FVector(0,0,H*.6f),FVector(30,30,60),FRotator(0,I*90+45,0),false,false); }
}
void Tent(FKit& K,const FVector& At,float Yaw,const TCHAR* Cloth)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_テント_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/野営"),At);
	for(float S:{-1.0f,1.0f}) { K.Part(A,TEXT("Cube"),Cloth,At+FRotator(0,Yaw,0).RotateVector(FVector(0,S*85,95)),FVector(320,210,10),FRotator(0,Yaw,S*48),true); }
	K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),At+FVector(0,0,95)+FRotator(0,Yaw,0).Vector()*170,FVector(14,14,190));
}
void Campfire(FKit& K,const FVector& At)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_焚き火_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/野営"),At,true);
	for(int32 I=0;I<8;++I) { K.Part(A,TEXT("KitRock"),TEXT("StoneDark"),At+FRotator(0,I*45,0).Vector()*70+FVector(0,0,8),FVector(40,34,28),FRotator(0,I*45,0)); }
	for(int32 I=0;I<3;++I) { K.Part(A,TEXT("KitCylinder"),TEXT("Bark"),At+FVector(0,0,18),FVector(14,14,100),FRotator(70,I*60,0)); }
	if(auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/NiagaraExamples/FX_Misc/NS_Fire.NS_Fire")))
	{
		auto* Flames=NewObject<UNiagaraComponent>(A); Flames->SetupAttachment(A->GetRootComponent()); Flames->SetAsset(System);
		Flames->SetRelativeLocation(FVector(0,0,15)); Flames->SetRelativeScale3D(FVector(.6f)); A->AddInstanceComponent(Flames); Flames->RegisterComponent();
	}
	auto* Light=NewObject<UPointLightComponent>(A); Light->SetupAttachment(A->GetRootComponent()); Light->SetMobility(EComponentMobility::Movable);
	Light->SetRelativeLocation(FVector(0,0,90)); Light->SetLightColor(FLinearColor(1,.55f,.2f)); Light->SetIntensity(5000); Light->SetAttenuationRadius(900); Light->SetCastShadows(false);
	A->AddInstanceComponent(Light); Light->RegisterComponent();
}
void Glow(FKit& K,AActor* A,const FVector& At,const FLinearColor& Color,float Intensity,float Radius)
{
	auto* Light=NewObject<UPointLightComponent>(A); Light->SetupAttachment(A->GetRootComponent()); Light->SetMobility(EComponentMobility::Movable);
	Light->SetWorldLocation(At); Light->SetLightColor(Color); Light->SetIntensity(Intensity); Light->SetAttenuationRadius(Radius); Light->SetCastShadows(false);
	A->AddInstanceComponent(Light); Light->RegisterComponent();
}

// ---------------------------------------------------------------- landmarks, one per place
/** Round the edge of every open place, by what kind of place it is; tall things only on its far side. */
void DressAreaEdges(FKit& K,FRandomStream& R)
{
	int32 CaveLights=0;
	for(const auto& Area:RPGWorld::Areas())
	{
		if(Area.Id==TEXT("ColonyPlaza") || Area.Id==TEXT("Colony")) { continue; }
		const EBiome Biome=BiomeAt(Area.At.X,Area.At.Y);
		const int32 Count=FMath::Clamp(FMath::RoundToInt((Area.Radius.X+Area.Radius.Y)/260),5,16);
		for(int32 I=0;I<Count;++I)
		{
			const float A=(I+R.FRandRange(-.3f,.3f))*UE_TWO_PI/Count, D=R.FRandRange(.8f,.93f);
			const float X=Area.At.X+FMath::Cos(A)*Area.Radius.X*D, Y=Area.At.Y+FMath::Sin(A)*Area.Radius.Y*D;
			const RPGWorld::FSample S=RPGWorld::Sample(X,Y);
			float Half=0; const float Clear=RPGWorld::RoadDistance(X,Y,&Half)-Half;
			const FVector At(X,Y,S.Height);
			if(!S.bLand || S.Inside<70 || Clear<60 || K.IsReserved(At,200)) { continue; }
			const bool bFar=FMath::Cos(A)>-.25f;
			const float Roll=R.FRand();
			switch(Biome)
			{
			case EBiome::Ruins:
				if(Area.Fill==RPGWorld::ESurface::Pavers && bFar && Roll<.55f) { Pillar(K,At,R.FRandRange(240,420),Roll<.25f,R); }
				else if(Roll<.75f) { Lantern(K,At,210); } else { Rock(K,At,R.FRandRange(.4f,.7f),R); }
				break;
			case EBiome::Rocky:
				if(Area.Id==TEXT("Fort") && bFar && Roll<.5f) { Pillar(K,At,R.FRandRange(200,320),true,R); }
				else if(Roll<.6f) { Rock(K,At,R.FRandRange(.6f,1.2f),R); } else if(bFar) { Tree(K,At,R.FRandRange(.9f,1.2f),R,1); } else { Barrels(K,At,R); }
				break;
			case EBiome::Forest:
				if(bFar && Roll<.6f) { Tree(K,At,R.FRandRange(.9f,1.2f),R,3); } else if(Roll<.8f) { Mushroom(K,At,R.FRandRange(1,1.8f),R,R.FRand()<.5f); } else { Bush(K,At,1.2f,R,false); }
				break;
			case EBiome::Blossom: case EBiome::Garden:
				if(bFar && Roll<.5f) { Tree(K,At,R.FRandRange(.9f,1.2f),R,2); } else if(Roll<.75f) { Bush(K,At,R.FRandRange(.8f,1.1f),R,true); } else { Lantern(K,At,200); }
				break;
			case EBiome::Beach:
				if(bFar && Roll<.5f) { Tree(K,At,R.FRandRange(.9f,1.2f),R,4); } else { Rock(K,At,R.FRandRange(.5f,1.0f),R); }
				break;
			case EBiome::Canyon:
				if(Area.Fill==RPGWorld::ESurface::Pavers && bFar && Roll<.45f) { Pillar(K,At,R.FRandRange(260,460),Roll<.2f,R); }
				else if(bFar && Roll<.7f) { Tree(K,At,R.FRandRange(.9f,1.25f),R,R.FRand()<.2f?5:1); } else if(Roll<.85f) { Rock(K,At,R.FRandRange(.6f,1.1f),R); } else { Lantern(K,At,220); }
				break;
			case EBiome::Cave:
			{
				Crystal(K,At,R.FRandRange(.9f,1.6f),R,R.FRand()<.35f?TEXT("CrystalPink"):TEXT("Crystal"));
				if(CaveLights<28 && (I%2==0))
				{
					auto* L=K.Piece(FString::Printf(TEXT("RPG_洞窟の光_%.0f_%.0f"),X,Y),RegionFolder(At)+TEXT("/光"),At);
					Glow(K,L,At+FVector(0,0,180),R.FRand()<.35f?FLinearColor(1,.4f,.85f):FLinearColor(.3f,.75f,1),7000,900); ++CaveLights;
				}
				break;
			}
			default:
				if(bFar && Roll<.4f) { Tree(K,At,R.FRandRange(.9f,1.2f),R,0); } else if(Roll<.75f) { Bush(K,At,R.FRandRange(.8f,1.1f),R,R.FRand()<.5f); } else { Rock(K,At,R.FRandRange(.4f,.7f),R); }
				break;
			}
		}
	}
}
void BuildColony(FKit& K,FRandomStream& R)
{
	const FString F=TEXT("こもれびコロニー");
	House(K,K.Ground(-1250,-950),0,TEXT("RoofClay"),R); House(K,K.Ground(-1250,950),180,TEXT("RoofTeal"),R);
	House(K,K.Ground(700,-1250),0,TEXT("RoofTeal"),R); House(K,K.Ground(700,1250),180,TEXT("RoofClay"),R);
	// (No house at the south: the way out to the hills goes through there.)
	{
		// The square's fountain with the ball on top.
		auto* A=K.Piece(TEXT("RPG_コロニー_噴水"),F+TEXT("/広場"),K.Ground(250,0));
		const FVector C=K.Ground(250,0);
		K.Part(A,TEXT("KitCylinder"),TEXT("Stone"),C+FVector(0,0,30),FVector(300,300,60),FRotator::ZeroRotator,true);
		K.Part(A,TEXT("KitCylinder"),TEXT("Water"),C+FVector(0,0,58),FVector(262,262,6),FRotator::ZeroRotator,false,false);
		K.Part(A,TEXT("KitFrustum"),TEXT("Stone"),C+FVector(0,0,110),FVector(70,70,120));
		K.Part(A,TEXT("Sphere"),TEXT("Copper"),C+FVector(0,0,205),FVector(100,100,100));
		K.Part(A,TEXT("Sphere"),TEXT("GlowTeal"),C+FVector(0,0,300),FVector(60,60,60),FRotator::ZeroRotator,false,false);
	}
	// Gates at the four ways out: north to the field, west to the wood, east over the brook, south to the hills.
	const auto Gate=[&](const FVector& At,float Yaw,const TCHAR* Label)
	{
		auto* A=K.Piece(FString(TEXT("RPG_コロニー_"))+Label,F+TEXT("/門"),At);
		const FRotator Fc(0,Yaw,0);
		for(float S:{-1.0f,1.0f})
		{
			const FVector P=At+Fc.RotateVector(FVector(0,S*330,0)); const FVector G=K.Ground(P.X,P.Y);
			K.Part(A,TEXT("Cube"),TEXT("Stone"),G+FVector(0,0,170),FVector(150,150,340),Fc,true);
			K.Part(A,TEXT("Cube"),TEXT("Copper"),G+FVector(0,0,350),FVector(180,180,24),Fc);
			K.Part(A,TEXT("KitGem"),TEXT("GlowWarm"),G+FVector(0,0,395),FVector(50,50,60),Fc,false,false);
		}
		K.Part(A,TEXT("Cube"),TEXT("Timber"),At+FVector(0,0,330),FVector(60,820,46),Fc);
		K.Part(A,TEXT("Cube"),TEXT("Banner"),At+FVector(0,0,262)+Fc.Vector()*-6,FVector(16,200,90),Fc);
	};
	Gate(K.Ground(2150,0),0,TEXT("北門")); Gate(K.Ground(0,-1950),90,TEXT("西門")); Gate(K.Ground(0,1950),90,TEXT("東門")); Gate(K.Ground(-2150,0),0,TEXT("南門"));
	// Fields and a little court.
	for(int32 I=0;I<6;++I)
	{
		auto* A=K.Piece(FString::Printf(TEXT("RPG_コロニー_畑_%d"),I),F+TEXT("/畑"),K.Ground(-600+I*230,-1650));
		const FVector P=K.Ground(-600+I*230,-1650);
		K.Part(A,TEXT("Cube"),TEXT("Soil"),P+FVector(0,0,8),FVector(170,260,22));
		for(int32 J=0;J<3;++J) { K.Part(A,TEXT("KitBlob"),J%2?TEXT("LeavesLight"):TEXT("Leaves"),P+FVector(0,J*80-80,40),FVector(70,60,55)); }
	}
	for(const FVector& At:{K.Ground(-650,-560),K.Ground(-650,560),K.Ground(1250,-700),K.Ground(1250,700)})
	{
		auto* A=K.Piece(FString::Printf(TEXT("RPG_コロニー_ベンチ_%.0f_%.0f"),At.X,At.Y),F+TEXT("/広場"),At);
		K.Part(A,TEXT("Cube"),TEXT("Planks"),At+FVector(0,0,45),FVector(220,60,14));
		for(float S:{-80.0f,80.0f}) { K.Part(A,TEXT("Cube"),TEXT("StoneDark"),At+FVector(S,0,20),FVector(26,50,40),FRotator::ZeroRotator,true); }
	}
	for(const FVector& At:{K.Ground(1700,-500),K.Ground(1700,500),K.Ground(-900,-300),K.Ground(-900,1500),K.Ground(-200,-1300),K.Ground(-300,1250)}) { Lantern(K,At); }
	Fence(K,K.Ground(-1900,-1500),K.Ground(-1100,-1900),F); Fence(K,K.Ground(1600,1500),K.Ground(900,1850),F);
	Barrels(K,K.Ground(-850,700),R); Barrels(K,K.Ground(1000,-600),R);
}
void BuildLandmarks(FKit& K,FRandomStream& R)
{
	// Signposts where the ways divide.
	Sign(K,K.Ground(5000,650),45); Sign(K,K.Ground(6700,-3500),-60); Sign(K,K.Ground(6300,3900),20); Sign(K,K.Ground(-4800,900),100);
	Sign(K,K.Ground(18600,250),-10); Sign(K,K.Ground(11200,4000),-30); Sign(K,K.Ground(1700,-6300),0);
	// Quarry: cut blocks, a crane and a ramp down into the pit.
	{
		const FString F=TEXT("苔むした採石場");
		for(int32 I=0;I<7;++I)
		{
			const FVector P=K.Ground(7400+R.FRandRange(-900,900),-4300+R.FRandRange(-800,800));
			if(K.IsReserved(P,200) || RPGWorld::RoadDistance(P.X,P.Y)<260) { continue; }
			auto* A=K.Piece(FString::Printf(TEXT("RPG_切り石_%d"),I),F+TEXT("/切り石"),P);
			K.Part(A,TEXT("Cube"),TEXT("Stone"),P+FVector(0,0,60),FVector(R.FRandRange(140,220),R.FRandRange(110,170),120),FRotator(0,R.FRandRange(0,90),0),true);
			K.Part(A,TEXT("Cube"),TEXT("Stone"),P+FVector(R.FRandRange(-30,30),R.FRandRange(-30,30),150),FVector(110,90,70),FRotator(0,R.FRandRange(0,90),0),true);
		}
		auto* A=K.Piece(TEXT("RPG_採石場_クレーン"),F+TEXT("/クレーン"),K.Ground(8300,-3400));
		const FVector C=K.Ground(8300,-3400);
		K.Part(A,TEXT("Cube"),TEXT("Timber"),C+FVector(0,0,300),FVector(40,40,600),FRotator::ZeroRotator,true);
		K.Part(A,TEXT("Cube"),TEXT("Timber"),C+FVector(150,-250,560),FVector(30,620,30),FRotator(0,30,-8));
		K.Part(A,TEXT("KitCylinder"),TEXT("Copper"),C+FVector(300,-520,400),FVector(8,8,300));
		K.Part(A,TEXT("Cube"),TEXT("Stone"),C+FVector(300,-520,230),FVector(110,110,90));
	}
	// The weathercock tower on its hill (the first survey).
	Tower(K,K.Ground(13150,-4600),330,1050,TEXT("RoofTeal"),TEXT("風見の中継塔"));
	// Ruins: a ring of pillars, a broken arch, the great gate to the canyon.
	{
		const FString F=TEXT("水脈の遺跡");
		for(int32 I=0;I<9;++I)
		{
			const float A=I*UE_TWO_PI/9; const FVector P=K.Ground(15600+FMath::Cos(A)*1150,-600+FMath::Sin(A)*900);
			if(RPGWorld::RoadDistance(P.X,P.Y)<380 || K.IsReserved(P,150)) { continue; }
			Pillar(K,P,R.FRandRange(260,420),I%3==1,R);
		}
		auto* G=K.Piece(TEXT("RPG_遺跡_峡谷への大門"),F+TEXT("/大門"),K.Ground(17300,-600));
		for(float Y:{-470.0f,470.0f}) { const FVector P=K.Ground(17300,-600+Y); K.Part(G,TEXT("Cube"),TEXT("Stone"),P+FVector(0,0,330),FVector(240,240,660),FRotator::ZeroRotator,true); K.Part(G,TEXT("KitBlobB"),TEXT("Moss"),P+FVector(0,0,670),FVector(250,250,60)); }
		const FVector Top=K.Ground(17300,-600)+FVector(0,0,700);
		K.Part(G,TEXT("Cube"),TEXT("StoneDark"),Top,FVector(260,1200,180));
		K.Part(G,TEXT("Cube"),TEXT("GlowTeal"),Top-FVector(0,0,110),FVector(270,960,26),FRotator::ZeroRotator,false,false);
	}
	// The broken fort on its rise: walls, two towers, a keep where the gold chest waits.
	{
		const FString F=TEXT("崩れた砦");
		const FVector C=RPGWorld::AreaPoint(TEXT("Fort"));
		Wall(K,C+FVector(1150,-900,0),C+FVector(1150,500,0),360,F); Wall(K,C+FVector(1150,900,0),C+FVector(300,1050,0),300,F);
		Wall(K,C+FVector(-500,950,0),C+FVector(700,1050,0),260,F); Wall(K,C+FVector(-200,-1050,0),C+FVector(900,-1000,0),200,F,false);
		Tower(K,K.Ground(C.X+1150,C.Y-1000),260,700,TEXT("RoofClay"),F); Tower(K,K.Ground(C.X+1100,C.Y+950),260,620,TEXT("RoofClay"),F);
		for(int32 I=0;I<6;++I) { const FVector P=K.Ground(C.X+R.FRandRange(-900,900),C.Y+R.FRandRange(-800,800)); if(!K.IsReserved(P,250) && RPGWorld::RoadDistance(P.X,P.Y)>300) { Rock(K,P,.6f,R,F); } }
		Wall(K,C+FVector(400,-650,0),C+FVector(400,-150,0),180,F,false); Wall(K,C+FVector(-300,500,0),C+FVector(300,600,0),150,F,false);
		Wall(K,C+FVector(-700,-300,0),C+FVector(-650,150,0),120,F,false);
		{
			auto* Fallen=K.Piece(TEXT("RPG_砦_倒れた塔"),F+TEXT("/塔"),C+FVector(700,600,0));
			for(int32 I=0;I<4;++I) { K.Part(Fallen,TEXT("KitCylinder"),TEXT("Stone"),K.Ground(C.X+600-I*150,C.Y+700+I*90)+FVector(0,0,90),FVector(200,200,170),FRotator(90,30,R.FRandRange(-15,15)),true); }
		}
		Barrels(K,K.Ground(C.X-400,C.Y-700),R); Barrels(K,K.Ground(C.X+200,C.Y+900),R);
		auto* B=K.Piece(TEXT("RPG_砦_旗"),F+TEXT("/旗"),C);
		for(float Y:{-500.0f,500.0f}) { const FVector P=K.Ground(C.X+900,C.Y+Y); K.Part(B,TEXT("KitCylinder"),TEXT("Timber"),P+FVector(0,0,250),FVector(16,16,500)); K.Part(B,TEXT("Cube"),TEXT("Banner"),P+FVector(0,60,400),FVector(8,120,170)); }
	}
	// The old mill and its turning wheel.
	{
		House(K,K.Ground(11200,5550),90,TEXT("RoofClay"),R);
		const FVector W=K.Ground(10760,5550)+FVector(0,0,240);
		auto* A=K.Piece(TEXT("RPG_旧水車小屋_水車"),TEXT("旧水車小屋/水車"),W,true);
		K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),W,FVector(380,380,40),FRotator(0,0,90));
		for(int32 I=0;I<8;++I) { K.Part(A,TEXT("Cube"),TEXT("Planks"),W,FVector(400,40,30),FRotator(0,0,90)+FRotator(I*45,0,0)); }
		auto* Spin=NewObject<URotatingMovementComponent>(A); Spin->RotationRate=FRotator(-35,0,0); A->AddInstanceComponent(Spin); Spin->RegisterComponent();
	}
	// The windmill on the hill of flowers.
	{
		const FVector Base=RPGWorld::AreaPoint(TEXT("Windmill"),{520,150});
		Tower(K,Base,240,620,TEXT("RoofClay"),TEXT("風車の丘"));
		auto* A=K.Piece(TEXT("RPG_風車の丘_羽根"),TEXT("風車の丘/風車"),Base+FVector(-270,0,560),true);
		const FVector Hub=Base+FVector(-270,0,560);
		K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),Hub,FVector(60,60,80),FRotator(90,0,0));
		for(int32 I=0;I<4;++I)
		{
			const FRotator Arm(0,0,I*90);
			K.Part(A,TEXT("Cube"),TEXT("Timber"),Hub+Arm.RotateVector(FVector(-30,0,260)),FVector(14,16,520),Arm);
			K.Part(A,TEXT("Cube"),TEXT("Sail"),Hub+Arm.RotateVector(FVector(-34,60,300)),FVector(6,110,420),Arm);
		}
		auto* Spin=NewObject<URotatingMovementComponent>(A); Spin->RotationRate=FRotator(0,0,28); A->AddInstanceComponent(Spin); Spin->RegisterComponent();
	}
	// The cove: a wrecked ship on the sand, a jetty, and the rock at the end of the shallows.
	{
		const FString F=TEXT("さざなみの入り江");
		const FVector S=K.Ground(-3700,7900);
		auto* A=K.Piece(TEXT("RPG_入り江_難破船"),F+TEXT("/難破船"),S);
		const FRotator Fc(8,35,-14);
		K.Part(A,TEXT("Cube"),TEXT("Planks"),S+FVector(0,0,90),FVector(900,320,160),Fc,true);
		K.Part(A,TEXT("KitCone"),TEXT("Planks"),S+Fc.RotateVector(FVector(560,0,0))+FVector(0,0,90),FVector(320,160,260),Fc+FRotator(-90,0,0),true);
		K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),S+FVector(0,0,420),FVector(30,30,620),FRotator(18,35,-10));
		K.Part(A,TEXT("Cube"),TEXT("Sail"),S+FVector(80,-60,520),FVector(10,300,240),FRotator(18,35,-30));
		auto* J=K.Piece(TEXT("RPG_入り江_桟橋"),F+TEXT("/桟橋"),K.Ground(-1600,9900));
		for(int32 I=0;I<8;++I)
		{
			const FVector P=K.Ground(-1600,9900)+FVector(-I*140,I*120,0); const float Z=FMath::Max(-10.0f,K.Ground(-1600,9900).Z);
			K.Part(J,TEXT("Cube"),TEXT("Planks"),FVector(P.X,P.Y,Z+30),FVector(150,260,16),FRotator(0,-40,0),true);
			K.Part(J,TEXT("KitCylinder"),TEXT("Timber"),FVector(P.X,P.Y,Z-120)+FRotator(0,-40,0).RotateVector(FVector(0,130,0)),FVector(24,24,300));
		}
		for(int32 I=0;I<6;++I) { Rock(K,K.Ground(-4300+R.FRandRange(-500,500),11900+R.FRandRange(-400,400)),R.FRandRange(.5f,.9f),R,TEXT("入り江の小島")); }
	}
	// The canyon: stone stairs' rails, banners, the great sluice gate pouring its falls.
	{
		const FString F=TEXT("忘れられた大水門");
		const FVector C=RPGWorld::AreaPoint(TEXT("Sluice"));
		// Two halves of the great gate either side of the way on to the garden, each pouring its falls.
		auto* A=K.Piece(TEXT("RPG_大水門_水門"),F+TEXT("/水門"),C+FVector(1300,0,0));
		for(float S:{-1.0f,1.0f})
		{
			K.Part(A,TEXT("Cube"),TEXT("StoneDark"),C+FVector(1500,S*1050,500),FVector(400,1100,1000),FRotator::ZeroRotator,true);
			for(float Y:{520.0f,1100.0f,1580.0f})
			{
				K.Part(A,TEXT("Cube"),TEXT("Stone"),C+FVector(1300,S*Y,640),FVector(260,180,1300),FRotator::ZeroRotator,true);
				K.Part(A,TEXT("Cube"),TEXT("GlowTeal"),C+FVector(1180,S*Y,1050),FVector(20,90,120),FRotator::ZeroRotator,false,false);
			}
			K.Part(A,TEXT("Cube"),TEXT("Copper"),C+FVector(1300,S*1050,1320),FVector(300,1250,80));
			Waterfall(K,C+FVector(1150,S*820,1200),0,2100,420,F);
		}
	}
	// The falls under the crag, feeding the brook past the colony.
	Waterfall(K,FVector(4300,7900,640),180,1300,520,TEXT("せせらぎの草原"));
	Waterfall(K,FVector(27500,2600,1700),-90,2600,600,TEXT("忘れられた大水門"));
	// The camp: tents round a fire.
	{
		const FVector C=RPGWorld::AreaPoint(TEXT("Camp"));
		Campfire(K,C+FVector(150,200,0)); Tent(K,K.Ground(C.X+500,C.Y-300),20,TEXT("Sail")); Tent(K,K.Ground(C.X+250,C.Y+650),-60,TEXT("Banner"));
		Barrels(K,K.Ground(C.X-300,C.Y+500),R);
	}
	// The garden of the spring: a still pool and blossom.
	{
		const FVector C=RPGWorld::AreaPoint(TEXT("Garden"));
		auto* A=K.Piece(TEXT("RPG_水源庭園_泉"),TEXT("静かな水源庭園/泉"),C+FVector(400,-700,0));
		const FVector P=C+FVector(400,-700,0);
		K.Part(A,TEXT("KitCylinder"),TEXT("Stone"),P+FVector(0,0,20),FVector(520,520,40),FRotator::ZeroRotator,true);
		K.Part(A,TEXT("KitCylinder"),TEXT("Water"),P+FVector(0,0,42),FVector(470,470,4),FRotator::ZeroRotator,false,false);
		for(int32 I=0;I<4;++I)
		{
			const float Ang=.785f+I*1.571f; const float X=C.X+FMath::Cos(Ang)*900, Y=C.Y+FMath::Sin(Ang)*800;
			float Half=0; if(RPGWorld::RoadDistance(X,Y,&Half)-Half>80) { Lantern(K,K.Ground(X,Y),200); }
		}
	}
	// The cave mouth: an arch of rock with glowing crystals; crystals and pools inside.
	{
		const FString F=TEXT("水晶洞窟・入口回廊");
		auto* A=K.Piece(TEXT("RPG_洞窟_入口"),F+TEXT("/入口"),FVector(31000,400,960));
		for(float Y:{-430.0f,430.0f}) { K.Part(A,TEXT("KitRock"),TEXT("Cliff"),FVector(31000,400+Y,1300),FVector(420,300,800),FRotator(0,R.FRandRange(0,90),0),true); }
		K.Part(A,TEXT("KitRockB"),TEXT("Cliff"),FVector(31000,400,1700),FVector(500,1300,300),FRotator::ZeroRotator,false);
		Crystal(K,FVector(30800,-30,960),.9f,R); Crystal(K,FVector(30800,830,960),.9f,R);
		auto* P=K.Piece(TEXT("RPG_洞窟_地底の泉"),TEXT("水晶洞窟・地底の泉/泉"),RPGWorld::AreaPoint(TEXT("Pool"),{150,250}));
		const FVector Pool=RPGWorld::AreaPoint(TEXT("Pool"),{150,250});
		K.Part(P,TEXT("KitCylinder"),TEXT("Water"),Pool+FVector(0,0,6),FVector(560,420,4),FRotator::ZeroRotator,false,false);
		Glow(K,P,Pool+FVector(0,0,200),FLinearColor(.3f,.8f,1),9000,1100);
	}
	// The boss's flooded hall: pillars and great crystals round the edge.
	{
		const FVector C=RPGWorld::AreaPoint(TEXT("BossHall"));
		for(int32 I=0;I<10;++I)
		{
			const float A=I*UE_TWO_PI/10; const FVector P=K.Ground(C.X+FMath::Cos(A)*1450,C.Y+FMath::Sin(A)*1250);
			if(RPGWorld::RoadDistance(P.X,P.Y)<400) { continue; }
			if(I%2) { Pillar(K,P,520,I%4==1,R); } else { Crystal(K,P,1.6f,R); }
		}
		auto* L=K.Piece(TEXT("RPG_洞窟_大広間の灯り"),TEXT("水晶洞窟・水底の大広間/灯り"),C);
		Glow(K,L,C+FVector(0,0,600),FLinearColor(.35f,.75f,1),30000,3200);
	}
}
#endif
}

int32 URPGWorldBuilder::PopulateLevel(UObject* WorldContextObject)
{
#if WITH_EDITOR
	using namespace RPGWorldBuild;
	UWorld* World=GEngine?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::LogAndReturnNull):nullptr;
	if(!World || World->IsGameWorld()) { return 0; }
	FKit K; K.World=World;
	FRandomStream R(20261020);

	// 1. Clear the old map's terrain and scenery (gameplay actors that are reused are only moved below).
	int32 Removed=0;
	for(TActorIterator<AActor> It(World);It;++It)
	{
		AActor* A=*It;
		const bool bScenery=A->IsA<ARPGAdventureSceneryPiece>() || A->IsA<AStaticMeshActor>() || A->IsA<ATextRenderActor>() || A->IsA<APointLight>()
			|| A->IsA<ARPGPrototypeSpawnPoint>() || A->IsA<ARPGTreasureChest>() || A->IsA<ARPGAdventureCheckpoint>()
			|| A->IsA<ADirectionalLight>() || A->IsA<ASkyLight>() || A->IsA<AExponentialHeightFog>() || A->IsA<APostProcessVolume>()
			|| A->FindComponentByClass<USkyAtmosphereComponent>() || A->Tags.Contains(TEXT("RPGWorld"));
		if(bScenery) { A->Destroy(); ++Removed; }
	}
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD removed %d old actors"),Removed);

	// 2. The adventure world actor (anchor for the colony, region names and camera) stays, its old generator off.
	ARPGAdventureWorld* Adventure=ARPGAdventureWorld::Find(World);
	if(!Adventure) { Adventure=World->SpawnActor<ARPGAdventureWorld>(FVector::ZeroVector,FRotator::ZeroRotator); Adventure->SetActorLabel(TEXT("RPG_冒険の地図")); }
	Adventure->bUseEditableScenery=true; Adventure->SetActorLocation(FVector::ZeroVector); Adventure->SetFolderPath(TEXT("RPGAdventure/Scenery"));

	// 3. Gameplay places first (scenery keeps clear of them).
	struct FChestDef { const TCHAR* Name; float X,Y; ERPGChestStyle Style; int32 Min,Max; float Rare,Unique,Legend; ERPGChestRewardKind Kind; bool bSpecial; };
	const TArray<FChestDef> Chests={
		{TEXT("コロニーの裏庭の宝箱"),-1050,-1420,ERPGChestStyle::Wood,30,60,0,0,0,ERPGChestRewardKind::Ball,true},
		{TEXT("コロニーの東の宝箱"),1050,1500,ERPGChestStyle::Wood,30,60,0,0,0,ERPGChestRewardKind::Gear,false},
		{TEXT("分かれ道の宝箱"),5900,850,ERPGChestStyle::Wood,30,70,.3f,.08f,.01f},
		{TEXT("採石場の切り石の陰"),8250,-3650,ERPGChestStyle::Wood,40,80,.3f,.08f,.01f},
		{TEXT("採石場の底の宝箱"),9750,-6650,ERPGChestStyle::Silver,80,140,.45f,.18f,.03f},
		{TEXT("霧の古森の倒木の陰"),1300,-9500,ERPGChestStyle::Wood,40,80,.3f,.08f,.01f},
		{TEXT("霧の古森の大樹の根元"),4300,-9900,ERPGChestStyle::Silver,80,140,.45f,.18f,.03f},
		{TEXT("木漏れ日の庭の宝箱"),-650,-12900,ERPGChestStyle::Gold,180,300,.35f,.45f,.12f},
		{TEXT("中継塔の見晴らしの宝箱"),12900,-3750,ERPGChestStyle::Silver,90,150,.45f,.18f,.03f},
		{TEXT("塔への坂道の宝箱"),10850,-2700,ERPGChestStyle::Wood,40,90,.3f,.08f,.01f},
		{TEXT("遺跡の祭壇の宝箱"),16400,-1350,ERPGChestStyle::Silver,90,160,.45f,.18f,.03f},
		{TEXT("せせらぎの岸辺の宝箱"),5700,5350,ERPGChestStyle::Wood,30,70,.3f,.08f,.01f},
		{TEXT("花畑の宝箱"),8000,5150,ERPGChestStyle::Wood,30,70,.3f,.08f,.01f},
		{TEXT("砦の奥の宝箱"),10700,9750,ERPGChestStyle::Gold,180,300,.35f,.45f,.12f},
		{TEXT("砦の門の宝箱"),9300,8550,ERPGChestStyle::Wood,40,90,.3f,.08f,.01f},
		{TEXT("水車小屋の宝箱"),11950,4500,ERPGChestStyle::Wood,40,90,.3f,.08f,.01f},
		{TEXT("高台の宝箱"),13250,7250,ERPGChestStyle::Silver,90,150,.45f,.18f,.03f},
		{TEXT("花風の丘の宝箱"),-5900,-550,ERPGChestStyle::Wood,30,70,.3f,.08f,.01f},
		{TEXT("風車の裏の宝箱"),-7650,2050,ERPGChestStyle::Silver,80,140,.45f,.18f,.03f},
		{TEXT("難破船の宝箱"),-3350,8450,ERPGChestStyle::Wood,40,90,.3f,.08f,.01f},
		{TEXT("小島の宝箱"),-4850,12150,ERPGChestStyle::Gold,180,300,.35f,.45f,.12f},
		{TEXT("丘の下り道の宝箱"),-5150,3500,ERPGChestStyle::Wood,30,70,.3f,.08f,.01f},
		{TEXT("峡谷入口の宝箱"),19500,-1250,ERPGChestStyle::Wood,50,100,.3f,.1f,.01f},
		{TEXT("上層回廊の宝箱"),21700,-3950,ERPGChestStyle::Silver,100,170,.45f,.18f,.03f},
		{TEXT("見晴らし台の宝箱"),25250,-6100,ERPGChestStyle::Gold,200,320,.35f,.45f,.12f},
		{TEXT("苔の水路の宝箱"),21500,3000,ERPGChestStyle::Wood,50,100,.3f,.1f,.01f},
		{TEXT("野営跡の宝箱"),23150,5700,ERPGChestStyle::Silver,100,170,.45f,.18f,.03f},
		{TEXT("連絡路の宝箱"),23050,-1350,ERPGChestStyle::Wood,50,100,.3f,.1f,.01f},
		{TEXT("大水門の宝箱"),26800,-1150,ERPGChestStyle::Silver,100,180,.45f,.18f,.03f},
		{TEXT("水源庭園の宝箱"),30200,1150,ERPGChestStyle::Wood,60,110,.35f,.12f,.02f},
		{TEXT("洞窟入口の宝箱"),34200,900,ERPGChestStyle::Wood,75,140,.45f,.18f,.02f},
		{TEXT("古い倉庫の宝箱"),35300,-4000,ERPGChestStyle::Gold,200,340,.35f,.45f,.12f},
		{TEXT("地底の泉の宝箱"),35650,4500,ERPGChestStyle::Silver,110,190,.45f,.18f,.03f},
		{TEXT("水晶の広間の宝箱"),37900,850,ERPGChestStyle::Silver,110,190,.45f,.2f,.03f},
		{TEXT("大広間の奥の宝箱"),41700,900,ERPGChestStyle::Gold,240,400,.3f,.5f,.15f},
	};
	const auto Inland=[](float& X,float& Y,float Margin)
	{
		for(int32 Step=0;Step<40 && RPGWorld::Sample(X,Y).Inside<Margin;++Step)
		{
			FVector Road; RPGWorld::RoadDistance(X,Y,nullptr,&Road);
			const FVector2D To=FVector2D(Road.X-X,Road.Y-Y).GetSafeNormal()*50; X+=To.X; Y+=To.Y;
		}
	};
	for(int32 I=0;I<Chests.Num();++I)
	{
		auto C=Chests[I];
		Inland(C.X,C.Y,170);
		const FVector At=K.Ground(C.X,C.Y,2);
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Facing the nearest road, so it opens towards whoever comes.
		FVector Road; RPGWorld::RoadDistance(C.X,C.Y,nullptr,&Road);
		const float Yaw=(Road-At).GetSafeNormal2D().Rotation().Yaw;
		auto* Chest=World->SpawnActorDeferred<ARPGTreasureChest>(ARPGTreasureChest::StaticClass(),FTransform(FRotator(0,Yaw,0),At),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Chest->ChestId=FName(*FString::Printf(TEXT("FrontierTreasure_%03d"),I+1)); Chest->DisplayName=C.Name; Chest->Style=C.Style;
		Chest->RubyMin=C.Min; Chest->RubyMax=C.Max; Chest->RareChance=C.Rare; Chest->UniqueChance=C.Unique; Chest->LegendChance=C.Legend;
		Chest->RewardKind=C.Kind; Chest->bSpecialBallsOnly=C.bSpecial;
		Chest->FinishSpawning(FTransform(FRotator(0,Yaw,0),At));
		Chest->SetActorLabel(FString::Printf(TEXT("RPG_宝箱_%02d_%s"),I+1,C.Name));
		Chest->SetFolderPath(FName(*(TEXT("RPGAdventure/TreasureChests/")+RPGWorld::RegionAt(At))));
		K.Reserve(At,260); ++K.Made;
	}
	struct FGroupDef { float X,Y; int32 Level,Chasers,Throwers; float Respawn; };
	const TArray<FGroupDef> Groups={
		{3500,350,1,3,2,35},{6150,-2050,2,3,2,35},{7300,-4100,2,3,2,35},{9100,-6000,2,3,2,40},{2600,-8000,2,3,2,35},{3900,-9500,2,3,2,35},
		{-150,-12400,3,3,2,45},{10800,-2350,2,3,2,35},{12300,-4100,3,3,2,40},{15000,-500,3,3,2,40},{16300,-1100,3,3,2,40},
		{6800,4500,1,3,2,35},{8200,6900,2,3,2,35},{10200,9100,3,3,2,45},{9200,3900,2,3,2,35},{11600,4700,2,3,2,35},{13800,2700,3,3,2,40},
		{-5300,250,1,3,2,35},{-7100,1650,2,3,2,35},{-2700,8800,1,3,2,35},
		{19100,-600,4,3,2,40},{20200,-2800,4,3,2,40},{22400,-3600,4,3,2,40},{20000,1600,4,3,2,40},{22100,2700,4,3,2,40},{23000,-1100,4,3,2,40},
		{22800,5300,4,3,2,40},{25000,-5700,5,3,2,45},{26200,-300,5,3,2,45},{25400,1200,5,3,2,45},{29500,500,5,3,2,45},
		{33700,400,5,3,2,45},{35900,-2100,5,3,2,45},{35000,-3700,6,3,2,45},{35900,2700,5,3,2,45},{37200,200,6,3,2,45},
	};
	for(int32 I=0;I<Groups.Num();++I)
	{
		const auto& G=Groups[I]; const FVector At=K.Ground(G.X,G.Y,120);
		auto* Spawn=World->SpawnActor<ARPGPrototypeSpawnPoint>(At,FRotator::ZeroRotator);
		Spawn->EnemyLevel=G.Level; Spawn->DropChance=.25f; Spawn->RespawnSeconds=G.Respawn; Spawn->bEncounterGroup=true;
		Spawn->ChaserCount=G.Chasers; Spawn->ThrowerCount=G.Throwers;
		Spawn->SetActorLabel(FString::Printf(TEXT("RPG_敵集団_%02d_Lv%d_接近%d投球%d"),I+1,G.Level,G.Chasers,G.Throwers));
		Spawn->SetFolderPath(FName(*(TEXT("RPGAdventure/EnemySpawns/")+RPGWorld::RegionAt(At))));
		K.Reserve(At,420); ++K.Made;
	}
	struct FCheckDef { const TCHAR* Name; float X,Y; FVector Offset; };
	const TArray<FCheckDef> Checks={
		{TEXT("遺跡の灯り"),14500,-1500,{150,150,120}},{TEXT("大水門の灯り"),25500,-600,{150,0,120}},
		{TEXT("水晶洞窟・入口の灯り"),32300,850,{0,-250,120}},{TEXT("ボス前の灯り"),38600,750,{0,-300,120}},
	};
	for(auto C:Checks)
	{
		Inland(C.X,C.Y,220);
		const FVector At=K.Ground(C.X,C.Y,2);
		auto* Point=World->SpawnActor<ARPGAdventureCheckpoint>(At,FRotator::ZeroRotator);
		Point->DisplayName=C.Name; Point->RespawnOffset=C.Offset;
		Point->SetActorLabel(FString(TEXT("RPG_"))+C.Name); Point->SetFolderPath(TEXT("RPGAdventure/Checkpoints"));
		K.Reserve(At,300); ++K.Made;
	}
	for(TActorIterator<ARPGAdventureNode> It(World);It;++It)
	{
		FVector2D XY(-300,450);
		switch(It->Kind)
		{
		case ERPGAdventureNodeKind::Supply: XY=It->ObjectiveId==0?FVector2D(9350,-6350):It->ObjectiveId==1?FVector2D(5900,5650):FVector2D(12800,6750); break;
		case ERPGAdventureNodeKind::Survey: XY=It->ObjectiveId==0?FVector2D(12300,-4000):FVector2D(15250,-250); break;
		case ERPGAdventureNodeKind::DungeonEntrance: XY=FVector2D(16900,-600); break;
		default: break;
		}
		const FVector At=K.Ground(XY.X,XY.Y);
		It->SetActorLocation(At); It->RebuildNode(); K.Reserve(At,260);
	}
	for(TActorIterator<ARPGPrototypeMerchant> It(World);It;++It) { It->SetActorLocation(K.Ground(-850,500)); K.Reserve(It->GetActorLocation(),250); }
	for(TActorIterator<ARPGPrototypeBoss> It(World);It;++It)
	{ It->SetActorLocation(K.Ground(40700,200,120)); It->SetActorRotation(FRotator(0,180,0)); It->SetFolderPath(TEXT("RPGAdventure/Boss")); }
	for(TActorIterator<APlayerStart> It(World);It;++It) { It->SetActorLocation(K.Ground(-450,-200,120)); }
	K.Reserve(FVector(-450,-200,0),400);

	// 4. Terrain tiles and the sea.
	for(int32 Tile=0;Tile<GetTerrainTileCount();++Tile)
	{
		const int32 IX=Tile/TilesY(), IY=Tile%TilesY();
		const FString Name=FString::Printf(TEXT("SM_RPGTerrain_%02d_%02d"),IX,IY);
		UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("%s/Terrain/%s.%s"),Root,*Name,*Name));
		if(!Mesh) { continue; }
		auto* A=World->SpawnActor<AStaticMeshActor>(FVector(RPGWorld::MinX+IX*TileSize,RPGWorld::MinY+IY*TileSize,0),FRotator::ZeroRotator);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh); A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		A->SetActorLabel(FString::Printf(TEXT("RPG_地形_%02d_%02d"),IX,IY)); A->SetFolderPath(TEXT("RPGWorld/地形")); A->Tags.Add(TEXT("RPGWorld")); ++K.Made;
	}
	{
		// The water pack's plane, tiled (its ocean material is not made for instancing).
		auto* Sea=K.Piece(TEXT("RPG_水面"),TEXT("水面"),FVector(0,0,RPGWorld::SeaLevel));
		for(float X=RPGWorld::MinX-24000;X<RPGWorld::MaxX+24000;X+=12000) { for(float Y=RPGWorld::MinY-24000;Y<RPGWorld::MaxY+24000;Y+=12000)
		{ K.Part(Sea,TEXT("/Game/WaterMaterials/Meshes/SM_Water_Plane.SM_Water_Plane"),TEXT("Sea"),FVector(X+6000,Y+6000,RPGWorld::SeaLevel),FVector(800,800,100),FRotator::ZeroRotator,false,false); } }
	}
	for(const auto& B:RPGWorld::Bridges()) { Bridge(K,B); K.Reserve(B.At,B.Length*.5f); }

	// 5. Places' own buildings and landmarks, then trees, rocks and the small things over all the land.
	BuildColony(K,R);
	BuildLandmarks(K,R);
	auto* Grass=K.Piece(TEXT("RPG_草むら"),TEXT("草花"),FVector::ZeroVector);
	auto* Flowers=K.Piece(TEXT("RPG_花"),TEXT("草花"),FVector::ZeroVector);
	auto* Pebbles=K.Piece(TEXT("RPG_小石"),TEXT("草花"),FVector::ZeroVector);
	const auto MakeHISM=[&](AActor* Owner,const TCHAR* Mesh,const TCHAR* Material)
	{
		auto* H=NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner); H->SetupAttachment(Owner->GetRootComponent());
		H->SetStaticMesh(K.Mesh(Mesh)); H->SetMaterial(0,K.Mat(Material)); H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetCastShadow(false); H->SetCullDistances(0,9000); Owner->AddInstanceComponent(H); H->RegisterComponent(); return H;
	};
	auto* TuftA=MakeHISM(Grass,TEXT("KitTuft"),TEXT("GrassBlade")); auto* TuftB=MakeHISM(Grass,TEXT("KitTuftB"),TEXT("GrassBlade"));
	auto* FlowerA=MakeHISM(Flowers,TEXT("KitGem"),TEXT("FlowerGold")); auto* FlowerB=MakeHISM(Flowers,TEXT("KitGem"),TEXT("FlowerWhite"));
	auto* FlowerC=MakeHISM(Flowers,TEXT("KitGem"),TEXT("FlowerPink")); auto* Stones=MakeHISM(Pebbles,TEXT("KitRock"),TEXT("Rock"));
	int32 Trees=0,Small=0;
	for(float X=RPGWorld::MinX;X<RPGWorld::MaxX;X+=230)
	{
		for(float Y=RPGWorld::MinY;Y<RPGWorld::MaxY;Y+=230)
		{
			const float PX=X+R.FRandRange(-90,90), PY=Y+R.FRandRange(-90,90);
			const RPGWorld::FSample S=RPGWorld::Sample(PX,PY);
			if(!S.bLand || S.Inside<60) { continue; }
			const FVector At(PX,PY,S.Height);
			const EBiome Biome=BiomeAt(PX,PY);
			const float Track=FMath::Max3(S.Dirt,S.Pavers,S.Cave);
			const bool bOpen=Track<.3f && (Biome==EBiome::Beach || S.Sand<.4f);
			float Half=0; FVector Nearest; const float RoadD=RPGWorld::RoadDistance(PX,PY,&Half,&Nearest);
			const float Clear=RoadD-Half;
			// Nothing tall in front of (below on screen) a road, where it would hide the player walking it.
			const bool bInFront=PX<Nearest.X-50 && Clear<900;
			const bool bColonyPlaza=FMath::Square(PX/2100)+FMath::Square(PY/1750)<1;
			const float Roll=R.FRand();
			const bool bTreeSpot=bOpen && Clear>260 && !bInFront && !K.IsReserved(At,260) && !bColonyPlaza && S.Inside>140;
			bool bPlaced=false;
			switch(Biome)
			{
			case EBiome::Forest:
				if(bTreeSpot && Roll<.07f && S.Inside>420 && Clear>520) { Tree(K,At,R.FRandRange(.85f,1.2f),R,3); bPlaced=true; }
				else if(bTreeSpot && Roll<.2f) { Tree(K,At,R.FRandRange(.8f,1.15f),R,R.FRand()<.3f?1:0); bPlaced=true; }
				else if(bOpen && Roll<.28f && !K.IsReserved(At,120)) { Mushroom(K,At,R.FRandRange(.8f,1.6f),R,R.FRand()<.4f); bPlaced=true; }
				else if(bOpen && Roll<.36f && Clear>120 && !K.IsReserved(At,150)) { Bush(K,At,R.FRandRange(.8f,1.3f),R,false); bPlaced=true; }
				break;
			case EBiome::Meadow: case EBiome::Village:
				if(bTreeSpot && Roll<(Biome==EBiome::Village?.02f:.05f)) { Tree(K,At,R.FRandRange(.85f,1.15f),R,0); bPlaced=true; }
				else if(bOpen && Roll<.11f && Clear>120 && !K.IsReserved(At,150) && !bColonyPlaza) { Bush(K,At,R.FRandRange(.7f,1.1f),R,R.FRand()<.4f); bPlaced=true; }
				break;
			case EBiome::Blossom: case EBiome::Garden:
				if(bTreeSpot && Roll<.07f) { Tree(K,At,R.FRandRange(.85f,1.15f),R,2); bPlaced=true; }
				else if(bOpen && Roll<.13f && Clear>120 && !K.IsReserved(At,150)) { Bush(K,At,R.FRandRange(.7f,1.0f),R,true); bPlaced=true; }
				break;
			case EBiome::Rocky: case EBiome::Ruins:
				if(bTreeSpot && Roll<.05f) { Tree(K,At,R.FRandRange(.85f,1.2f),R,1); bPlaced=true; }
				else if(bOpen && Roll<.14f && Clear>150 && !K.IsReserved(At,200)) { Rock(K,At,R.FRandRange(.5f,1.1f),R); bPlaced=true; }
				break;
			case EBiome::Canyon:
				if(bTreeSpot && Roll<.06f) { Tree(K,At,R.FRandRange(.85f,1.2f),R,R.FRand()<.15f?5:1); bPlaced=true; }
				else if(bOpen && Roll<.14f && Clear>150 && !K.IsReserved(At,200)) { Rock(K,At,R.FRandRange(.5f,1.0f),R); bPlaced=true; }
				break;
			case EBiome::Beach:
				if(bTreeSpot && Roll<.05f && S.Height>-20) { Tree(K,At,R.FRandRange(.9f,1.2f),R,4); bPlaced=true; }
				else if(Roll<.1f && Clear>150 && !K.IsReserved(At,200) && S.Height>-60) { Rock(K,At,R.FRandRange(.4f,.8f),R); bPlaced=true; }
				break;
			case EBiome::Cave:
				if(bOpen && Roll<.1f && Clear>120 && !K.IsReserved(At,220)) { Crystal(K,At,R.FRandRange(.6f,1.2f),R,R.FRand()<.3f?TEXT("CrystalPink"):TEXT("Crystal")); bPlaced=true; }
				else if(bOpen && Roll<.16f && Clear>150 && !K.IsReserved(At,220)) { Mushroom(K,At,R.FRandRange(.6f,1.1f),R,true); bPlaced=true; }
				break;
			}
			if(bPlaced) { ++Trees; continue; }
			// Small things: grass, flowers, pebbles (instanced, no collision).
			if(Biome!=EBiome::Cave && Biome!=EBiome::Beach && Track<.5f)
			{
				const int32 Count=Biome==EBiome::Village?1:Biome==EBiome::Forest||Biome==EBiome::Meadow||Biome==EBiome::Garden?3:2;
				for(int32 I=0;I<Count;++I)
				{
					const FVector P(PX+R.FRandRange(-110,110),PY+R.FRandRange(-110,110),0);
					const RPGWorld::FSample Q=RPGWorld::Sample(P.X,P.Y);
					if(!Q.bLand || FMath::Max3(Q.Dirt,Q.Pavers,Q.Cave)>.45f || Q.Inside<30) { continue; }
					(R.FRand()<.5f?TuftA:TuftB)->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(P.X,P.Y,Q.Height-3),FVector(R.FRandRange(.7f,1.3f))),true); ++Small;
				}
				const float FlowerOdds=Biome==EBiome::Blossom?.75f:Biome==EBiome::Garden||Biome==EBiome::Meadow?.4f:Biome==EBiome::Village?.15f:.08f;
				if(R.FRand()<FlowerOdds)
				{
					auto* H=Biome==EBiome::Blossom?(R.FRand()<.6f?FlowerC:FlowerB):(R.FRand()<.5f?FlowerA:FlowerB);
					for(int32 I=0;I<4;++I)
					{
						const FVector P(PX+R.FRandRange(-80,80),PY+R.FRandRange(-80,80),0); const RPGWorld::FSample Q=RPGWorld::Sample(P.X,P.Y);
						if(Q.bLand && Q.Inside>30 && FMath::Max(Q.Dirt,Q.Pavers)<.4f) { H->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(P.X,P.Y,Q.Height+14),FVector(.14f,.14f,.1f)),true); ++Small; }
					}
				}
			}
			if(R.FRand()<(Biome==EBiome::Rocky||Biome==EBiome::Canyon||Biome==EBiome::Cave||Biome==EBiome::Beach?.25f:.06f))
			{ Stones->AddInstance(FTransform(FRotator(R.FRandRange(-10,10),R.FRandRange(0,360),0),FVector(PX,PY,S.Height-4),FVector(R.FRandRange(.18f,.4f))),true); ++Small; }
		}
	}
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD scattered %d trees/rocks and %d small things"),Trees,Small);
	DressAreaEdges(K,R);
	for(const auto& Rk:RPGWorld::Rocks())
	{
		if(RPGWorld::IsCave(Rk.At.X,Rk.At.Y)) { continue; }
		const int32 Count=R.RandRange(1,4);
		for(int32 I=0;I<Count;++I)
		{
			const float A=R.FRandRange(0,UE_TWO_PI),D=R.FRandRange(0,.55f);
			const float X=Rk.At.X+FMath::Cos(A)*Rk.Radius.X*D, Y=Rk.At.Y+FMath::Sin(A)*Rk.Radius.Y*D;
			const EBiome B=BiomeAt(X,Y);
			Tree(K,FVector(X,Y,Height(X,Y)),R.FRandRange(.9f,1.3f),R,B==EBiome::Blossom||B==EBiome::Garden?2:B==EBiome::Canyon||B==EBiome::Rocky?1:B==EBiome::Beach?4:R.FRand()<.5f?0:1);
		}
	}
	// Rocky crags get boulders round their feet, where they meet the water.
	for(const auto& Rk:RPGWorld::Rocks())
	{
		for(int32 I=0;I<6;++I)
		{
			const float A=R.FRandRange(0,UE_TWO_PI); const float X=Rk.At.X+FMath::Cos(A)*Rk.Radius.X*1.08f, Y=Rk.At.Y+FMath::Sin(A)*Rk.Radius.Y*1.08f;
			if(RPGWorld::IsCave(X,Y) || RPGWorld::Sample(X,Y).bLand) { continue; }
			Rock(K,FVector(X,Y,RPGWorld::SeaLevel-20),R.FRandRange(1.5f,3.0f),R,TEXT("岩場"));
		}
	}

	// 6. Light: a warm afternoon sun, sky and a soft haze into the distance.
	{
		auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,5000),FRotator(-52,-38,0));
		Sun->GetLightComponent()->SetIntensity(7.5f); Sun->GetLightComponent()->SetLightColor(FLinearColor(1,.9f,.76f));
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		if(auto* D=Cast<UDirectionalLightComponent>(Sun->GetLightComponent())) { D->SetAtmosphereSunLight(true); D->SetDynamicShadowDistanceMovableLight(9000); }
		Sun->SetActorLabel(TEXT("RPG_太陽")); Sun->SetFolderPath(TEXT("RPGWorld/光")); Sun->Tags.Add(TEXT("RPGWorld"));
		auto* SkyA=World->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator);
		auto* SkyRoot=NewObject<USceneComponent>(SkyA,TEXT("SkyRoot")); SkyA->SetRootComponent(SkyRoot); SkyA->AddInstanceComponent(SkyRoot); SkyRoot->RegisterComponent();
		auto* Atmo=NewObject<USkyAtmosphereComponent>(SkyA,TEXT("SkyAtmosphere")); Atmo->SetupAttachment(SkyRoot); SkyA->AddInstanceComponent(Atmo); Atmo->RegisterComponent();
		SkyA->SetActorLabel(TEXT("RPG_空")); SkyA->SetFolderPath(TEXT("RPGWorld/光")); SkyA->Tags.Add(TEXT("RPGWorld"));
		auto* Sky=World->SpawnActor<ASkyLight>(FVector(0,0,3000),FRotator::ZeroRotator);
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable); Sky->GetLightComponent()->SetRealTimeCapture(true);
		Sky->GetLightComponent()->SetIntensity(1.35f); Sky->SetActorLabel(TEXT("RPG_空の光")); Sky->SetFolderPath(TEXT("RPGWorld/光")); Sky->Tags.Add(TEXT("RPGWorld"));
		auto* Fog=World->SpawnActor<AExponentialHeightFog>(FVector(0,0,-200),FRotator::ZeroRotator);
		Fog->GetComponent()->SetFogDensity(.018f); Fog->GetComponent()->SetFogHeightFalloff(.32f); Fog->GetComponent()->SetStartDistance(2600);
		Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.45f,.62f,.82f));
		Fog->SetActorLabel(TEXT("RPG_霞")); Fog->SetFolderPath(TEXT("RPGWorld/光")); Fog->Tags.Add(TEXT("RPGWorld"));
		auto* Post=World->SpawnActor<APostProcessVolume>(FVector::ZeroVector,FRotator::ZeroRotator);
		Post->bUnbound=true;
		Post->Settings.bOverride_BloomIntensity=true; Post->Settings.BloomIntensity=.05f;
		Post->Settings.bOverride_AutoExposureMinBrightness=true; Post->Settings.AutoExposureMinBrightness=1;
		Post->Settings.bOverride_AutoExposureMaxBrightness=true; Post->Settings.AutoExposureMaxBrightness=1;
		Post->Settings.bOverride_ColorSaturation=true; Post->Settings.ColorSaturation=FVector4(1.12f,1.12f,1.12f,1);
		Post->Settings.bOverride_ColorContrast=true; Post->Settings.ColorContrast=FVector4(1.06f,1.06f,1.06f,1);
		Post->Settings.bOverride_VignetteIntensity=true; Post->Settings.VignetteIntensity=.3f;
		Post->SetActorLabel(TEXT("RPG_色調")); Post->SetFolderPath(TEXT("RPGWorld/光")); Post->Tags.Add(TEXT("RPGWorld"));
		K.Made+=5;
	}
	World->MarkPackageDirty();
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD populated %d actors"),K.Made);
	return K.Made;
#else
	return 0;
#endif
}
