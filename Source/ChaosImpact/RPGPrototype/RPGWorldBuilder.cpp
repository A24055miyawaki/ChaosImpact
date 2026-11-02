#include "RPGWorldBuilder.h"
#include "RPGWorldLayout.h"
#include "RPGAdventure.h"
#include "RPGPrototype.h"
#include "RPGTreasureChest.h"
#include "RPGAbilityGates.h"
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
			UVs.SetNumChannels(4);
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
					// Second UVs: cave floor, snow.
					const FVector2f Extra(S.Cave,S.Snow);
					Instances[I*Side+J]=Make(FVector3f(I*Step,J*Step,S.Height),Normal,Color,Extra,X0+I*Step,Y0+J*Step);
					// Third and fourth UVs: regional colour (ash, dry grass; marsh).
					UVs.Set(Instances[I*Side+J],2,FVector2f(S.Ash,S.Dry)); UVs.Set(Instances[I*Side+J],3,FVector2f(S.Marsh,0));
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
					Low.Add(Make(P-FVector3f(0,0,600),Outward,Colors[Instances[Index]],UVs.Get(Instances[Index],1),0,0));
					UVs.Set(Low.Last(),2,UVs.Get(Instances[Index],2)); UVs.Set(Low.Last(),3,UVs.Get(Instances[Index],3));
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
enum class EBiome : uint8 { Village, Meadow, Rocky, Forest, Blossom, Beach, Ruins, Canyon, Garden, Cave, Desert, Snow, Volcano, Swamp };

struct FKit
{
	UWorld* World=nullptr;
	int32 Made=0;
	TMap<FString,UMaterialInterface*> Materials;
	TMap<FString,UStaticMesh*> Meshes;
	/** Where gameplay things stand: scenery keeps clear of these. */
	TArray<FVector4> Reserved;
	/** Also on everything made (ExtendLevel's "RPGWorld2": a second run replaces only those). */
	FName ExtraTag;

	UMaterialInterface* Mat(const FString& Name)
	{
		if(UMaterialInterface** Found=Materials.Find(Name)) { return *Found; }
		// A full path ("/Game/...") is loaded as it is; a short name is one of the adventure's own materials.
		UMaterialInterface* M=Name.StartsWith(TEXT("/"))?nullptr:LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("%s/Materials/M_%s.M_%s"),Root,*Name,*Name),nullptr,LOAD_Quiet|LOAD_NoWarn);
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
		A->SetActorLabel(Label); A->SetFolderPath(FName(*(TEXT("RPGWorld/")+Folder))); A->Tags.Add(TEXT("RPGWorld")); if(!ExtraTag.IsNone()) { A->Tags.Add(ExtraTag); }
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
		{TEXT("Lookout"),EBiome::Canyon},{TEXT("Camp"),EBiome::Canyon},{TEXT("Sluice"),EBiome::Canyon},{TEXT("Garden"),EBiome::Garden},
		// 2026-11.
		{TEXT("Strand"),EBiome::Beach},{TEXT("Harbor"),EBiome::Village},{TEXT("Cape"),EBiome::Blossom},{TEXT("Marsh"),EBiome::Swamp},
		{TEXT("VolcanoFoot"),EBiome::Volcano},{TEXT("Crater"),EBiome::Volcano},{TEXT("Dunes"),EBiome::Desert},{TEXT("Oasis"),EBiome::Desert},
		{TEXT("SandTemple"),EBiome::Desert},{TEXT("Teahouse"),EBiome::Blossom},{TEXT("Mesa"),EBiome::Canyon},{TEXT("Pillars"),EBiome::Canyon},
		{TEXT("SnowPass"),EBiome::Snow},{TEXT("SnowCamp"),EBiome::Snow},{TEXT("SnowWood"),EBiome::Snow},{TEXT("FrozenLake"),EBiome::Snow},{TEXT("Nest"),EBiome::Snow}};
	if(const EBiome* Found=Table.Find(Best)) { return *Found; }
	return X>17500?EBiome::Canyon:EBiome::Meadow;
}
FString RegionFolder(const FVector& At) { return RPGWorld::RegionAt(At); }

// ---------------------------------------------------------------- scenery pieces
void Tree(FKit& K,const FVector& At,float S,FRandomStream& R,int32 Style)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_樹木_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/樹木"),At);
	const float Yaw=R.FRandRange(0,360);
	if(Style==6)
	{
		// A pine under snow (the pass and the snowfield).
		K.Part(A,TEXT("KitFrustum"),TEXT("Bark"),At+FVector(0,0,90*S),FVector(48,48,180)*S,FRotator(0,Yaw,0),true);
		for(int32 I=0;I<3;++I)
		{
			const FVector Tier=At+FVector(0,0,(230+I*125)*S), Size=FVector(330-I*80,330-I*80,260-I*40)*S;
			K.Part(A,TEXT("KitCone"),TEXT("Pine"),Tier,Size,FRotator(0,Yaw+I*22,0));
			K.Part(A,TEXT("KitCone"),TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_Snow.MI_Snow"),Tier+FVector(0,0,Size.Z*.2f),Size*FVector(.74f,.74f,.62f),FRotator(0,Yaw+I*22+10,0));
		}
		return;
	}
	if(Style==7)
	{
		// A cactus: a column with an arm or two turned up.
		K.Part(A,TEXT("KitCylinder"),TEXT("Moss"),At+FVector(0,0,150*S),FVector(72,72,300)*S,FRotator(0,Yaw,0),true);
		K.Part(A,TEXT("Sphere"),TEXT("Moss"),At+FVector(0,0,298*S),FVector(72,72,46)*S);
		for(const float Side:{-1.0f,1.0f})
		{
			if(Side>0 && R.FRand()<.4f) { continue; }
			const FVector Out=FRotator(0,Yaw,0).RotateVector(FVector(0,Side,0));
			const float Up=R.FRandRange(120,190)*S;
			K.Part(A,TEXT("KitCylinder"),TEXT("Moss"),At+Out*70*S+FVector(0,0,Up),FVector(46,46,110)*S,FRotationMatrix::MakeFromZ(Out).Rotator());
			K.Part(A,TEXT("KitCylinder"),TEXT("Moss"),At+Out*118*S+FVector(0,0,Up+70*S),FVector(46,46,140)*S);
			K.Part(A,TEXT("Sphere"),TEXT("Moss"),At+Out*118*S+FVector(0,0,Up+140*S),FVector(46,46,30)*S);
		}
		return;
	}
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

// ---------------------------------------------------------------- pieces of the 2026-11 lands
const TCHAR* SnowMat=TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_Snow.MI_Snow");
const TCHAR* SnowBankMat=TEXT("/Game/ChaosImpact/solo/Stage08/Materials/MI_SnowBank.MI_SnowBank");
const TCHAR* IceMat=TEXT("/Game/ChaosImpact/FX/M_CI_IceSurface.M_CI_IceSurface");

/** On (or right beside) a road or an area of the new lands. */
/** On a new road (its width plus a margin). */
bool OnNewRoad(const FVector& At,float Margin)
{
	for(const auto& Road:RPGWorld::Roads())
	{
		if(Road.Part!=1) { continue; }
		for(int32 I=0;I+1<Road.Points.Num();++I)
		{
			FVector A=Road.Points[I],B=Road.Points[I+1]; A.Z=B.Z=0;
			if(FVector::Dist2D(FMath::ClosestPointOnSegment(FVector(At.X,At.Y,0),A,B),FVector(At.X,At.Y,0))<Road.Width*.5f+Margin) { return true; }
		}
	}
	return false;
}
bool OnNewGround(const FVector& At,float Margin)
{
	for(const auto& A:RPGWorld::Areas())
	{
		if(A.Part!=1) { continue; }
		const float Rn=FMath::Sqrt(FMath::Square((At.X-A.At.X)/A.Radius.X)+FMath::Square((At.Y-A.At.Y)/A.Radius.Y));
		if(Rn<1+Margin/FMath::Min(A.Radius.X,A.Radius.Y)) { return true; }
	}
	for(const auto& Road:RPGWorld::Roads())
	{
		if(Road.Part!=1) { continue; }
		for(int32 I=0;I+1<Road.Points.Num();++I)
		{
			FVector A=Road.Points[I],B=Road.Points[I+1]; A.Z=B.Z=0;
			if(FVector::Dist2D(FMath::ClosestPointOnSegment(FVector(At.X,At.Y,0),A,B),FVector(At.X,At.Y,0))<Road.Width*.5f+Margin) { return true; }
		}
	}
	return false;
}
/** A boulder of any stone (sandstone, red rock, cinders, snow-capped). */
void Boulder(FKit& K,const FVector& At,float S,FRandomStream& R,const TCHAR* Material,const TCHAR* Cap=nullptr)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_岩_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/岩"),At);
	const int32 Count=R.RandRange(1,3);
	for(int32 I=0;I<Count;++I)
	{
		const float Size=(I==0?1:R.FRandRange(.4f,.7f))*S;
		const FVector Off=I==0?FVector::ZeroVector:FRotator(0,R.FRandRange(0,360),0).Vector()*S*R.FRandRange(60,110);
		const FVector Dim(R.FRandRange(140,200)*Size,R.FRandRange(130,190)*Size,R.FRandRange(100,150)*Size);
		const FRotator Rot(R.FRandRange(-8,8),R.FRandRange(0,360),R.FRandRange(-8,8));
		K.Part(A,R.FRand()<.5f?TEXT("KitRock"):TEXT("KitRockB"),Material,At+Off+FVector(0,0,-8*Size),Dim,Rot,Size>.6f);
		if(Cap) { K.Part(A,TEXT("KitBlobB"),Cap,At+Off+FVector(0,0,Dim.Z*.36f),FVector(Dim.X*.8f,Dim.Y*.8f,Dim.Z*.28f),Rot); }
	}
}
/** A tall stone shape (mesa towers, temple blocks, the pillar valley's needles). */
void Spire(FKit& K,const FVector& At,float H,float W,const TCHAR* Material,FRandomStream& R,const TCHAR* Cap=nullptr)
{
	auto* A=K.Piece(FString::Printf(TEXT("RPG_岩柱_%.0f_%.0f"),At.X,At.Y),RegionFolder(At)+TEXT("/岩柱"),At);
	const float Yaw=R.FRandRange(0,360);
	K.Part(A,TEXT("KitFrustum"),Material,At+FVector(0,0,H*.5f-30),FVector(W,W*R.FRandRange(.75f,1.0f),H),FRotator(0,Yaw,0),true);
	K.Part(A,TEXT("KitRock"),Material,At+FVector(0,0,H-20),FVector(W*.9f,W*.8f,W*.5f),FRotator(0,Yaw+40,0));
	if(Cap) { K.Part(A,TEXT("KitBlobB"),Cap,At+FVector(0,0,H+W*.12f),FVector(W*.85f,W*.75f,W*.22f),FRotator(0,Yaw,0)); }
}
void Palm(FKit& K,const FVector& At,FRandomStream& R) { Tree(K,At,R.FRandRange(.9f,1.2f),R,4); }


// ---------------------------------------------------------------- landmarks, one per place
/** Round the edge of every open place (OnlyPart >= 0: only those of that part of the map), by what kind of place it is; tall things only on its far side. */
void DressAreaEdges(FKit& K,FRandomStream& R,int32 OnlyPart=-1)
{
	int32 CaveLights=0;
	for(const auto& Area:RPGWorld::Areas())
	{
		if(Area.Id==TEXT("ColonyPlaza") || Area.Id==TEXT("Colony") || (OnlyPart>=0 && Area.Part!=OnlyPart)) { continue; }
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
			case EBiome::Snow:
				if(bFar && Roll<.6f) { Tree(K,At,R.FRandRange(.9f,1.4f),R,6); } else if(Roll<.85f) { Boulder(K,At,R.FRandRange(.6f,1.1f),R,TEXT("Rock"),SnowMat); } else { Lantern(K,At,220); }
				break;
			case EBiome::Desert:
				if(bFar && Roll<.4f) { Spire(K,At,R.FRandRange(300,520),R.FRandRange(200,300),TEXT("Plaster"),R); } else if(Roll<.7f) { Tree(K,At,R.FRandRange(.8f,1.2f),R,7); } else { Boulder(K,At,R.FRandRange(.5f,.9f),R,TEXT("Plaster")); }
				break;
			case EBiome::Volcano:
				if(bFar && Roll<.5f) { Spire(K,At,R.FRandRange(260,480),R.FRandRange(200,300),TEXT("StoneDark"),R); } else { Boulder(K,At,R.FRandRange(.6f,1.2f),R,TEXT("StoneDark")); }
				break;
			case EBiome::Swamp:
				if(bFar && Roll<.5f) { Tree(K,At,R.FRandRange(1.0f,1.4f),R,5); } else { Mushroom(K,At,R.FRandRange(1.0f,1.8f),R,true); }
				break;
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
void BuildNewLandmarks(FKit& K,FRandomStream& R)
{
	const auto G=[&](float X,float Y,float Lift=0) { return K.Ground(X,Y,Lift); };
	// Off the roads (a house or a tent a road runs into is a wall in the way): pushed straight out from the nearest
	// road until it is Clear of its edge, onto land.
	const auto GO=[&](float X,float Y,float Clear)
	{
		for(int32 Try=0;Try<4;++Try)
		{
			float Half=0; FVector Near=FVector::ZeroVector;
			if(RPGWorld::RoadDistance(X,Y,&Half,&Near)>=Half+Clear) { break; }
			FVector2D Away(X-Near.X,Y-Near.Y); Away=Away.IsNearlyZero()?FVector2D(1,0):Away.GetSafeNormal();
			FVector2D To=FVector2D(Near.X,Near.Y)+Away*(Half+Clear+10);
			if(!RPGWorld::Sample(To.X,To.Y).bLand) { To=FVector2D(Near.X,Near.Y)-Away*(Half+Clear+10); }
			X=To.X; Y=To.Y;
		}
		return K.Ground(X,Y);
	};
	// The white beach: palms, driftwood, a beached boat.
	for(const FVector2D P:{FVector2D(-2600,15600),FVector2D(-1100,15900),FVector2D(-2700,17900),FVector2D(-900,17800),FVector2D(-1500,18200)}) { Palm(K,G(P.X,P.Y),R); }
	{
		auto* A=K.Piece(TEXT("RPG_浜辺_小舟"),TEXT("白砂の浜辺/小物"),G(-2900,16900));
		const FVector B=G(-2900,16900);
		K.Part(A,TEXT("KitBlobB"),TEXT("Planks"),B+FVector(0,0,40),FVector(420,170,90),FRotator(0,25,8),true);
		K.Part(A,TEXT("Cube"),TEXT("Timber"),B+FVector(0,0,90),FVector(360,16,16),FRotator(0,25,8));
		K.Part(A,TEXT("KitCylinder"),TEXT("Bark"),G(-1300,16300)+FVector(0,0,20),FVector(36,36,320),FRotator(90,70,0),true);
	}
	// The fishing village: houses round the square, a pier out over the sea, nets, boats.
	House(K,GO(2600,21200,420),180,TEXT("RoofTeal"),R); House(K,GO(2400,22700,420),180,TEXT("RoofClay"),R); House(K,GO(500,22900,420),0,TEXT("RoofTeal"),R);
	{
		auto* A=K.Piece(TEXT("RPG_漁村_桟橋"),TEXT("潮騒の漁村/桟橋"),G(400,20700));
		const FVector Start=G(300,20900);
		for(int32 I=0;I<9;++I)
		{
			const FVector P=FVector(Start.X-I*150,Start.Y-I*40,Start.Z);
			K.Part(A,TEXT("Cube"),I%3?TEXT("Planks"):TEXT("Timber"),P+FVector(0,0,8),FVector(140,280,14),FRotator(0,-15,0),true);
			if(I%2==0) { for(float S:{-1.0f,1.0f}) { K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),P+FRotator(0,-15,0).RotateVector(FVector(0,S*150,-140)),FVector(22,22,300),FRotator::ZeroRotator,true); } }
		}
		for(const FVector& Boat:{G(-900,20500),G(-700,21200)})
		{
			K.Part(A,TEXT("KitBlobB"),TEXT("ChestBlue"),FVector(Boat.X,Boat.Y,RPGWorld::SeaLevel+20),FVector(380,150,80),FRotator(0,R.FRandRange(-30,30),0));
		}
	}
	for(const FVector2D Wanted:{FVector2D(1200,22300),FVector2D(1900,21000)})
	{
		const FVector Spot=GO(Wanted.X,Wanted.Y,200); const FVector2D P(Spot.X,Spot.Y);
		auto* A=K.Piece(FString::Printf(TEXT("RPG_漁村_網_%.0f"),P.X),TEXT("潮騒の漁村/小物"),G(P.X,P.Y));
		for(float S:{-1.0f,1.0f}) { K.Part(A,TEXT("KitCylinder"),TEXT("Timber"),G(P.X,P.Y+S*130)+FVector(0,0,90),FVector(14,14,180),FRotator::ZeroRotator,true); }
		K.Part(A,TEXT("Cube"),TEXT("Sail"),G(P.X,P.Y)+FVector(0,0,110),FVector(8,260,130));
	}
	for(const FVector2D P:{FVector2D(1500,21300),FVector2D(800,22200),FVector2D(2200,22100)}) { Lantern(K,G(P.X,P.Y)); }
	Barrels(K,GO(1000,21000,170),R); Barrels(K,GO(2000,22900,170),R);
	// The lighthouse on the cape.
	{
		const FVector L=G(-5100,22500);
		Tower(K,L,170,900,TEXT("RoofClay"),TEXT("灯台の岬"));
		auto* A=K.Piece(TEXT("RPG_灯台_光"),TEXT("灯台の岬/灯台"),L);
		K.Part(A,TEXT("Sphere"),TEXT("GlowWarm"),L+FVector(0,0,960),FVector(120,120,120),FRotator::ZeroRotator,false,false);
		Glow(K,A,L+FVector(0,0,960),FLinearColor(1,.75f,.4f),20000,2600);
		Fence(K,G(-4300,21600),G(-3900,22600),TEXT("灯台の岬"));
	}
	// The firefly marsh: dead trees, glowing mushrooms, reeds, still water, fireflies.
	for(int32 I=0;I<9;++I)
	{
		const float A=I*UE_TWO_PI/9+R.FRandRange(-.2f,.2f); const float D=R.FRandRange(.45f,.85f);
		const float X=-3000+FMath::Cos(A)*2200*D, Y=28500+FMath::Sin(A)*1700*D;
		if(RPGWorld::RoadDistance(X,Y)<450) { continue; }
		if(I%3==0) { Tree(K,G(X,Y),R.FRandRange(1.0f,1.4f),R,5); }
		else if(I%3==1) { Mushroom(K,G(X,Y),R.FRandRange(1.2f,2.0f),R,true); }
		else
		{
			auto* P=K.Piece(FString::Printf(TEXT("RPG_沼_水たまり_%d"),I),TEXT("ほたる沼/水"),G(X,Y));
			K.Part(P,TEXT("KitCylinder"),TEXT("Water"),G(X,Y)+FVector(0,0,4),FVector(R.FRandRange(320,520),R.FRandRange(260,420),4),FRotator(0,R.FRandRange(0,180),0),false,false);
			for(int32 J=0;J<6;++J) { K.Part(P,TEXT("KitCylinder"),TEXT("Moss"),G(X,Y)+FVector(R.FRandRange(-200,200),R.FRandRange(-200,200),60),FVector(8,8,R.FRandRange(90,160))); }
		}
	}
	{
		auto* Flies=K.Piece(TEXT("RPG_沼_ほたる"),TEXT("ほたる沼/光"),G(-3000,28500));
		for(int32 I=0;I<26;++I)
		{
			const FVector P=G(-3000+R.FRandRange(-1900,1900),28500+R.FRandRange(-1500,1500),R.FRandRange(60,220));
			K.Part(Flies,TEXT("Sphere"),TEXT("GlowTeal"),P,FVector(9,9,9),FRotator::ZeroRotator,false,false);
			if(I%5==0) { Glow(K,Flies,P,FLinearColor(.4f,1,.7f),900,500); }
		}
	}
	// The volcano: the researcher's camp at its foot; lava pools and glowing cracks in the crater.
	Tent(K,GO(1700,29100,260),30,TEXT("Sail")); Tent(K,GO(900,29900,260),-60,TEXT("Sail")); Campfire(K,GO(1400,29600,120)); Barrels(K,GO(800,29000,170),R);
	{
		auto* Lava=K.Piece(TEXT("RPG_火口_溶岩"),TEXT("灼熱の火口/溶岩"),G(2600,33500));
		const FVector2D Pools[]={FVector2D(2200,34300),FVector2D(3300,33000),FVector2D(1800,33200),FVector2D(3100,34600)};
		for(const FVector2D& P:Pools)
		{
			const FVector At=G(P.X,P.Y);
			const float Sz=R.FRandRange(260,420);
			K.Part(Lava,TEXT("KitCylinder"),TEXT("Lava"),At+FVector(0,0,3),FVector(Sz,Sz*R.FRandRange(.7f,1),6),FRotator(0,R.FRandRange(0,180),0),false,false);
			for(int32 I=0;I<5;++I) { K.Part(Lava,TEXT("KitRock"),TEXT("StoneDark"),At+FRotator(0,I*72+R.FRandRange(-15,15),0).Vector()*Sz*.55f,FVector(70,60,40)*R.FRandRange(.8f,1.4f),FRotator(0,R.FRandRange(0,360),0)); }
			Glow(K,Lava,At+FVector(0,0,120),FLinearColor(1,.35f,.08f),6000,900);
		}
		for(int32 I=0;I<10;++I)
		{
			const float A=I*UE_TWO_PI/10; const FVector At=G(2600+FMath::Cos(A)*1450,33500+FMath::Sin(A)*1250);
			if(RPGWorld::RoadDistance(At.X,At.Y)<400) { continue; }
			Boulder(K,At,R.FRandRange(.9f,1.5f),R,TEXT("StoneDark"));
		}
	}
	// The desert: sandstone arches and towers, cacti, the oasis pool and palms, the temple.
	{
		const auto Arch=[&](const FVector& At,float Yaw)
		{
			auto* A=K.Piece(FString::Printf(TEXT("RPG_砂岩のアーチ_%.0f"),At.X),RegionFolder(At)+TEXT("/岩柱"),At);
			const FRotator F(0,Yaw,0);
			for(float S:{-1.0f,1.0f}) { K.Part(A,TEXT("KitFrustum"),TEXT("Plaster"),At+F.RotateVector(FVector(0,S*300,220)),FVector(170,150,440),F,true); }
			K.Part(A,TEXT("KitBlobB"),TEXT("Plaster"),At+F.RotateVector(FVector(0,0,470)),FVector(220,820,140),F,true);
		};
		Arch(G(7600,26400),20); Arch(G(5200,23300),-30);
		for(const FVector2D P:{FVector2D(8200,23800),FVector2D(4600,25400),FVector2D(7900,27200)}) { Spire(K,G(P.X,P.Y),R.FRandRange(420,700),R.FRandRange(220,320),TEXT("Plaster"),R); }
		auto* Pool=K.Piece(TEXT("RPG_オアシス_泉"),TEXT("砂漠のオアシス/泉"),G(5600,30200));
		K.Part(Pool,TEXT("KitCylinder"),TEXT("Water"),G(5700,30500)+FVector(0,0,5),FVector(900,700,4),FRotator(0,20,0),false,false);
		for(int32 I=0;I<12;++I) { const float A=I*UE_TWO_PI/12; K.Part(Pool,TEXT("KitRock"),TEXT("Plaster"),G(5700+FMath::Cos(A)*470,30500+FMath::Sin(A)*370)+FVector(0,0,5),FVector(90,80,40),FRotator(0,R.FRandRange(0,360),0)); }
		for(int32 I=0;I<7;++I) { const float A=I*UE_TWO_PI/7+.3f; Palm(K,G(5700+FMath::Cos(A)*820,30500+FMath::Sin(A)*690),R); }
		Tent(K,GO(5000,29600,260),100,TEXT("Banner")); Tent(K,GO(6300,29500,260),70,TEXT("Sail"));
		for(const FVector2D P:{FVector2D(5200,30000),FVector2D(6000,29800)}) { Lantern(K,G(P.X,P.Y),210); }
		// The temple: a paved court of pillars, walls round three sides, a stair up to a broken gate.
		const FVector T=G(9800,33200);
		for(int32 I=0;I<4;++I) { for(float S:{-1.0f,1.0f}) { Pillar(K,G(9000+I*520,33200+S*700),R.FRandRange(380,520),I==2&&S>0,R); } }
		Wall(K,G(11000,32300),G(11000,34100),260,TEXT("砂に眠る神殿"));
		Wall(K,G(8700,34300),G(10900,34300),220,TEXT("砂に眠る神殿"));
		Wall(K,G(8700,32100),G(9400,32100),200,TEXT("砂に眠る神殿"),false);
		auto* Idol=K.Piece(TEXT("RPG_神殿_石像"),TEXT("砂に眠る神殿/石像"),T);
		K.Part(Idol,TEXT("Cube"),TEXT("Stone"),G(10650,33200)+FVector(0,0,60),FVector(260,260,120),FRotator::ZeroRotator,true);
		K.Part(Idol,TEXT("Sphere"),TEXT("Plaster"),G(10650,33200)+FVector(0,0,260),FVector(240,220,260),FRotator::ZeroRotator,true);
		K.Part(Idol,TEXT("KitGem"),TEXT("GlowTeal"),G(10650,33200)+FVector(-110,0,300),FVector(40,40,50),FRotator::ZeroRotator,false,false);
		Glow(K,Idol,G(10650,33200)+FVector(-200,0,320),FLinearColor(.3f,1,.9f),3000,700);
	}
	for(int32 I=0;I<16;++I)
	{
		const float X=R.FRandRange(4200,9000), Y=R.FRandRange(22800,27800);
		const RPGWorld::FSample S=RPGWorld::Sample(X,Y);
		if(!S.bLand || S.Inside<120 || RPGWorld::RoadDistance(X,Y)<420 || K.IsReserved(FVector(X,Y,0),260)) { continue; }
		Tree(K,FVector(X,Y,S.Height),R.FRandRange(.8f,1.3f),R,7);
	}
	// The teahouse on the pass: a long house, benches, a red gate, blossom.
	{
		House(K,GO(12300,17600,420),90,TEXT("RoofTeal"),R);
		auto* Gate=K.Piece(TEXT("RPG_茶屋_鳥居"),TEXT("峠の茶屋/門"),G(11100,16500));
		const FVector P=G(11100,16500); const FRotator F(0,40,0);
		for(float S:{-1.0f,1.0f}) { K.Part(Gate,TEXT("KitCylinder"),TEXT("Banner"),P+F.RotateVector(FVector(0,S*260,180)),FVector(44,44,360),FRotator::ZeroRotator,true); }
		K.Part(Gate,TEXT("Cube"),TEXT("Banner"),P+FVector(0,0,350),FVector(50,640,34),F); K.Part(Gate,TEXT("Cube"),TEXT("Timber"),P+FVector(0,0,385),FVector(60,720,26),F);
		for(const FVector2D Wanted:{FVector2D(11500,17700),FVector2D(11900,16600)})
		{
			const FVector Spot=GO(Wanted.X,Wanted.Y,170); const FVector2D B(Spot.X,Spot.Y);
			auto* A=K.Piece(FString::Printf(TEXT("RPG_茶屋_縁台_%.0f"),B.X),TEXT("峠の茶屋/縁台"),G(B.X,B.Y));
			K.Part(A,TEXT("Cube"),TEXT("Banner"),G(B.X,B.Y)+FVector(0,0,48),FVector(240,90,10)); K.Part(A,TEXT("Cube"),TEXT("Planks"),G(B.X,B.Y)+FVector(0,0,40),FVector(240,90,12),FRotator::ZeroRotator,true);
		}
		for(const FVector2D T:{FVector2D(11000,17800),FVector2D(12600,16700),FVector2D(11300,18300)}) { Tree(K,G(T.X,T.Y),R.FRandRange(.9f,1.1f),R,2); }
		Lantern(K,G(11600,16900)); Lantern(K,G(12000,17900));
	}
	// The red mesa and the pillar valley: tall stone needles.
	for(int32 I=0;I<7;++I)
	{
		const float A=I*UE_TWO_PI/7+R.FRandRange(-.2f,.2f);
		const FVector At=G(24200+FMath::Cos(A)*1350,12400+FMath::Sin(A)*1100);
		if(RPGWorld::RoadDistance(At.X,At.Y)<450) { continue; }
		Spire(K,At,R.FRandRange(500,900),R.FRandRange(220,340),TEXT("RoofClay"),R);
	}
	for(int32 I=0;I<9;++I)
	{
		const float A=I*UE_TWO_PI/9+R.FRandRange(-.2f,.2f), D=R.FRandRange(.55f,.9f);
		const FVector At=G(20600+FMath::Cos(A)*1500*D,18600+FMath::Sin(A)*1200*D);
		if(RPGWorld::RoadDistance(At.X,At.Y)<420) { continue; }
		Spire(K,At,R.FRandRange(700,1300),R.FRandRange(160,260),TEXT("Stone"),R);
	}
	// The misty pass and the snowfield: snowy pines, the camp, the frozen lake.
	{
		const FVector Shrine=G(16000,23300);
		auto* A=K.Piece(TEXT("RPG_峠_石の祠"),TEXT("白霧の峠/祠"),Shrine);
		K.Part(A,TEXT("Cube"),TEXT("Stone"),Shrine+FVector(0,0,70),FVector(160,140,140),FRotator(0,30,0),true);
		K.Part(A,TEXT("KitCone"),TEXT("StoneDark"),Shrine+FVector(0,0,180),FVector(220,200,90),FRotator(0,30,0));
		K.Part(A,TEXT("KitBlobB"),SnowMat,Shrine+FVector(0,0,215),FVector(200,180,40),FRotator(0,30,0));
		Lantern(K,G(16700,22300)); Lantern(K,G(18700,25300));
	}
	Tent(K,GO(21600,29200,260),-20,TEXT("RoofTeal")); Tent(K,GO(20600,28900,260),40,TEXT("RoofTeal")); Campfire(K,GO(21000,28500,120));
	for(const FVector2D P:{FVector2D(20800,28000),FVector2D(21800,28600)}) { Lantern(K,G(P.X,P.Y)); }
	Barrels(K,GO(21700,28000,170),R);
	{
		auto* Lake=K.Piece(TEXT("RPG_雪原_凍った湖"),TEXT("雪原・凍った湖/氷"),G(24600,33000));
		K.Part(Lake,TEXT("KitCylinder"),IceMat,G(24700,33300)+FVector(0,0,4),FVector(2400,2000,6),FRotator(0,15,0),false,false);
		for(int32 I=0;I<6;++I) { Crystal(K,G(24600+R.FRandRange(-1200,1200),33000+R.FRandRange(-1000,1000)),R.FRandRange(.8f,1.3f),R,TEXT("Crystal")); }
	}
	for(int32 I=0;I<46;++I)
	{
		const float X=R.FRandRange(16000,30000), Y=R.FRandRange(21800,34200);
		const RPGWorld::FSample S=RPGWorld::Sample(X,Y);
		if(!S.bLand || S.Inside<140 || S.Snow<.6f || RPGWorld::RoadDistance(X,Y)<480 || K.IsReserved(FVector(X,Y,0),300)) { continue; }
		if(FVector::Dist2D(FVector(X,Y,0),FVector(29800,31200,0))<2300) { continue; }
		Tree(K,FVector(X,Y,S.Height),R.FRandRange(.9f,1.5f),R,6);
	}
	// The legend's arena: tall pillars round it, banners, the way in lit, a seat of stone at the far side.
	{
		const FVector C=G(34000,14400);
		for(int32 I=0;I<14;++I)
		{
			const float A=I*UE_TWO_PI/14;
			if(FMath::Abs(FMath::FindDeltaAngleDegrees(FMath::RadiansToDegrees(A),180.0f))<20) { continue; }
			Pillar(K,G(34000+FMath::Cos(A)*1650,14400+FMath::Sin(A)*1650),R.FRandRange(700,900),false,R);
		}
		auto* Seat=K.Piece(TEXT("RPG_闘技場_玉座"),TEXT("覇者の闘技場/玉座"),C);
		const FVector Back=G(35500,14400);
		K.Part(Seat,TEXT("Cube"),TEXT("StoneDark"),Back+FVector(0,0,80),FVector(420,520,160),FRotator::ZeroRotator,true);
		K.Part(Seat,TEXT("Cube"),TEXT("Gold"),Back+FVector(60,0,300),FVector(120,420,320),FRotator::ZeroRotator,true);
		K.Part(Seat,TEXT("Cube"),TEXT("Banner"),Back+FVector(-10,0,520),FVector(20,300,200));
		Glow(K,Seat,Back+FVector(-200,0,500),FLinearColor(1,.7f,.35f),12000,1600);
		for(const float S:{-1.0f,1.0f}) { Lantern(K,G(32300,14400+S*420),260); }
	}
	// Signposts where the new roads leave the old map.
	Sign(K,G(10500,10500),90); Sign(K,G(-2350,11800),90); Sign(K,G(23600,6800),70); Sign(K,G(12700,9800),110); Sign(K,G(11700,18200),30);
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
	{ if(It->Kind!=ERPGBossKind::Maguro) { continue; } It->SetActorLocation(K.Ground(40700,200,120)); It->SetActorRotation(FRotator(0,180,0)); It->SetFolderPath(TEXT("RPGAdventure/Boss")); }
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
int32 URPGWorldBuilder::ExtendLevel(UObject* WorldContextObject)
{
#if WITH_EDITOR
	using namespace RPGWorldBuild;
	UWorld* World=GEngine?GEngine->GetWorldFromContextObject(WorldContextObject,EGetWorldErrorMode::LogAndReturnNull):nullptr;
	if(!World || World->IsGameWorld()) { return 0; }
	FKit K; K.World=World; K.ExtraTag=TEXT("RPGWorld2");
	FRandomStream R(20261102);

	// 1. A second run replaces what the first one made. Old scenery standing where the new roads and places go is cleared
	//    (nothing else of the old map is touched: hand edits stay).
	int32 Removed=0;
	for(TActorIterator<AActor> It(World);It;++It)
	{
		AActor* A=*It;
		if(A->Tags.Contains(TEXT("RPGWorld2"))) { A->Destroy(); ++Removed; continue; }
		if(!A->IsA<ARPGAdventureSceneryPiece>() || !A->Tags.Contains(TEXT("RPGWorld"))) { continue; }
		const FString Label=A->GetActorLabel();
		if(Label.StartsWith(TEXT("RPG_水面")) || Label.StartsWith(TEXT("RPG_草むら")) || Label.StartsWith(TEXT("RPG_花")) || Label.StartsWith(TEXT("RPG_小石"))) { continue; }
		if(OnNewGround(A->GetActorLocation(),180)) { A->Destroy(); ++Removed; continue; }
		// A solid part of it lying across a new road (the old fort's fallen tower): just that part goes.
		TArray<UStaticMeshComponent*> Parts; A->GetComponents(Parts);
		for(UStaticMeshComponent* Part:Parts)
		{
			if(!Part || Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision) { continue; }
			const FBoxSphereBounds Bounds=Part->Bounds;
			if(OnNewRoad(Bounds.Origin,FMath::Min(Bounds.BoxExtent.X,Bounds.BoxExtent.Y))) { A->RemoveInstanceComponent(Part); Part->DestroyComponent(); ++Removed; }
		}
	}
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD extend: removed %d"),Removed);

	// 2. Terrain tiles the level does not show yet.
	TSet<FString> Tiles;
	for(TActorIterator<AStaticMeshActor> It(World);It;++It) { if(It->GetActorLabel().StartsWith(TEXT("RPG_地形_"))) { Tiles.Add(It->GetActorLabel()); } }
	for(int32 Tile=0;Tile<GetTerrainTileCount();++Tile)
	{
		const int32 IX=Tile/TilesY(), IY=Tile%TilesY();
		const FString Label=FString::Printf(TEXT("RPG_地形_%02d_%02d"),IX,IY);
		if(Tiles.Contains(Label)) { continue; }
		const FString Name=FString::Printf(TEXT("SM_RPGTerrain_%02d_%02d"),IX,IY);
		UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,*FString::Printf(TEXT("%s/Terrain/%s.%s"),Root,*Name,*Name));
		if(!Mesh) { continue; }
		auto* A=World->SpawnActor<AStaticMeshActor>(FVector(RPGWorld::MinX+IX*TileSize,RPGWorld::MinY+IY*TileSize,0),FRotator::ZeroRotator);
		A->GetStaticMeshComponent()->SetStaticMesh(Mesh); A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		A->SetActorLabel(Label); A->SetFolderPath(TEXT("RPGWorld/地形")); A->Tags.Add(TEXT("RPGWorld")); ++K.Made;
	}

	// 3. Gameplay places: the people who give quests (spawned at run time from RPGQuests::Givers), chests, enemy groups, beacons.
	for(const auto& Giver:RPGQuests::Givers()) { if(!Giver.At.IsNearlyZero()) { K.Reserve(FVector(Giver.At.X,Giver.At.Y,0),300); } }
	K.Reserve(FVector(29800,31200,0),2300);
	struct FChestDef { const TCHAR* Name; float X,Y; ERPGChestStyle Style; int32 Min,Max; float Rare,Unique,Legend; };
	const TArray<FChestDef> Chests={
		{TEXT("白砂の浜辺の宝箱"),-2300,17600,ERPGChestStyle::Wood,50,100,.3f,.1f,.01f},
		{TEXT("漁村の網小屋の宝箱"),2100,22400,ERPGChestStyle::Wood,60,110,.3f,.1f,.01f},
		{TEXT("灯台の足元の宝箱"),-4800,22900,ERPGChestStyle::Silver,120,200,.45f,.2f,.03f},
		{TEXT("ほたる沼の倒木の宝箱"),-3600,29400,ERPGChestStyle::Silver,120,200,.45f,.2f,.03f},
		{TEXT("ほたる沼の奥の宝箱"),-4400,27800,ERPGChestStyle::Gold,260,420,.35f,.45f,.12f},
		{TEXT("火山のふもとの宝箱"),1500,28900,ERPGChestStyle::Wood,70,130,.35f,.12f,.02f},
		{TEXT("火口の縁の宝箱"),3400,34100,ERPGChestStyle::Gold,300,480,.3f,.5f,.15f},
		{TEXT("砂丘に埋もれた宝箱"),7400,23600,ERPGChestStyle::Wood,60,110,.3f,.1f,.01f},
		{TEXT("砂丘の岩陰の宝箱"),5100,26100,ERPGChestStyle::Silver,120,200,.45f,.2f,.03f},
		{TEXT("オアシスの泉の宝箱"),6300,31000,ERPGChestStyle::Wood,70,120,.35f,.12f,.02f},
		{TEXT("神殿の祭壇の宝箱"),10400,33700,ERPGChestStyle::Gold,280,460,.3f,.5f,.15f},
		{TEXT("神殿の柱の間の宝箱"),9300,32600,ERPGChestStyle::Silver,130,210,.45f,.2f,.03f},
		{TEXT("茶屋の裏の宝箱"),12500,18000,ERPGChestStyle::Wood,60,110,.3f,.1f,.01f},
		{TEXT("赤土の台地の宝箱"),25000,13000,ERPGChestStyle::Silver,130,210,.45f,.2f,.03f},
		{TEXT("石柱の谷の宝箱"),21300,19300,ERPGChestStyle::Silver,140,220,.45f,.2f,.03f},
		{TEXT("白霧の峠の宝箱"),16900,23300,ERPGChestStyle::Wood,80,140,.35f,.12f,.02f},
		{TEXT("雪原の野営地の宝箱"),21700,29400,ERPGChestStyle::Wood,80,140,.35f,.12f,.02f},
		{TEXT("針葉樹の森の宝箱"),26300,26600,ERPGChestStyle::Silver,150,240,.45f,.2f,.03f},
		{TEXT("凍った湖のほとりの宝箱"),23500,33800,ERPGChestStyle::Gold,300,500,.3f,.5f,.15f},
		{TEXT("巡礼の石段の宝箱"),13900,31200,ERPGChestStyle::Silver,140,220,.45f,.2f,.03f},
		{TEXT("覇者の道の宝箱"),29500,13700,ERPGChestStyle::Gold,320,520,.3f,.5f,.15f},
		{TEXT("覇者の闘技場の宝箱"),35400,13900,ERPGChestStyle::Gold,500,900,.2f,.55f,.25f},
		// Behind the boulder, the ice, the leap and the lava boulder.
		{TEXT("砂岩の隠し部屋の秘宝"),11200,24200,ERPGChestStyle::Gold,400,700,.25f,.5f,.2f},
		{TEXT("氷の祠の秘宝"),27000,35500,ERPGChestStyle::Gold,450,750,.25f,.5f,.2f},
		{TEXT("石柱の頂の秘宝"),22700,21000,ERPGChestStyle::Gold,500,800,.2f,.5f,.25f},
		{TEXT("火山の隠し洞の秘宝"),450,36150,ERPGChestStyle::Gold,500,850,.2f,.5f,.25f},
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
		FVector Road; RPGWorld::RoadDistance(C.X,C.Y,nullptr,&Road);
		const float Yaw=(Road-At).GetSafeNormal2D().Rotation().Yaw;
		auto* Chest=World->SpawnActorDeferred<ARPGTreasureChest>(ARPGTreasureChest::StaticClass(),FTransform(FRotator(0,Yaw,0),At),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Chest->ChestId=FName(*FString::Printf(TEXT("FrontierTreasure2_%03d"),I+1)); Chest->DisplayName=C.Name; Chest->Style=C.Style;
		Chest->RubyMin=C.Min; Chest->RubyMax=C.Max; Chest->RareChance=C.Rare; Chest->UniqueChance=C.Unique; Chest->LegendChance=C.Legend;
		Chest->FinishSpawning(FTransform(FRotator(0,Yaw,0),At));
		Chest->SetActorLabel(FString::Printf(TEXT("RPG_宝箱2_%02d_%s"),I+1,C.Name));
		Chest->SetFolderPath(FName(*(TEXT("RPGAdventure/TreasureChests/")+RPGWorld::RegionAt(At)))); Chest->Tags.Add(TEXT("RPGWorld2"));
		K.Reserve(At,260); ++K.Made;
	}
	struct FGroupDef { float X,Y; int32 Level,Chasers,Throwers; float Respawn; };
	const TArray<FGroupDef> Groups={
		{-2000,15400,4,3,2,40},{-200,19300,4,3,2,40},{-4300,21800,5,3,2,40},{3900,23300,5,3,2,40},
		{-600,25200,6,3,2,40},{-1000,29400,6,3,3,40},{600,29300,7,3,2,45},{2000,31300,8,3,3,45},{3400,30000,8,3,2,45},
		{6000,24000,6,3,2,40},{7300,25700,6,2,3,40},{6000,27600,6,3,2,40},{7600,32200,7,3,2,45},{11700,32400,8,3,3,45},
		{10900,12300,5,3,2,40},{11400,15000,5,3,2,40},{9800,20300,6,3,2,40},{14000,19800,7,3,2,45},
		{24400,11800,6,3,2,40},{24900,8100,6,3,2,40},{23400,14000,7,3,3,45},{22600,15600,7,3,2,45},{18400,20800,7,3,2,45},
		{28000,13600,10,3,3,50},{31200,14200,11,4,2,50},
		{16400,22900,8,3,2,45},{18600,25600,8,3,3,45},{25300,27400,9,3,2,45},{26200,28400,9,3,3,45},
		{24600,32900,9,3,2,45},{23100,31900,10,4,2,50},{13600,31600,8,3,2,45},
	};
	for(int32 I=0;I<Groups.Num();++I)
	{
		const auto& Def=Groups[I]; const FVector At=K.Ground(Def.X,Def.Y,120);
		auto* Spawn=World->SpawnActor<ARPGPrototypeSpawnPoint>(At,FRotator::ZeroRotator);
		Spawn->EnemyLevel=Def.Level; Spawn->DropChance=.28f; Spawn->RespawnSeconds=Def.Respawn; Spawn->bEncounterGroup=true;
		Spawn->ChaserCount=Def.Chasers; Spawn->ThrowerCount=Def.Throwers;
		Spawn->SetActorLabel(FString::Printf(TEXT("RPG_敵集団2_%02d_Lv%d_接近%d投球%d"),I+1,Def.Level,Def.Chasers,Def.Throwers));
		Spawn->SetFolderPath(FName(*(TEXT("RPGAdventure/EnemySpawns/")+RPGWorld::RegionAt(At)))); Spawn->Tags.Add(TEXT("RPGWorld2"));
		K.Reserve(At,420); ++K.Made;
	}
	struct FCheckDef { const TCHAR* Name; float X,Y; FVector Offset; };
	const TArray<FCheckDef> Checks={
		{TEXT("漁村の灯り"),1100,21300,{150,150,120}},{TEXT("峠の茶屋の灯り"),11400,16800,{150,0,120}},
		{TEXT("オアシスの灯り"),5100,30000,{0,250,120}},{TEXT("火山のふもとの灯り"),900,28900,{150,0,120}},
		{TEXT("赤土の台地の灯り"),23700,12000,{0,250,120}},{TEXT("雪原の灯り"),20700,28300,{250,0,120}},
		{TEXT("神殿の灯り"),9000,33700,{0,-250,120}},
		{TEXT("覇者の道の灯り"),31600,14200,{0,250,120}},
	};
	for(auto C:Checks)
	{
		Inland(C.X,C.Y,220);
		const FVector At=K.Ground(C.X,C.Y,2);
		auto* Point=World->SpawnActor<ARPGAdventureCheckpoint>(At,FRotator::ZeroRotator);
		Point->DisplayName=C.Name; Point->RespawnOffset=C.Offset;
		Point->SetActorLabel(FString(TEXT("RPG_"))+C.Name); Point->SetFolderPath(TEXT("RPGAdventure/Checkpoints")); Point->Tags.Add(TEXT("RPGWorld2"));
		K.Reserve(At,300); ++K.Made;
	}

	// The bosses of the new lands, in the middle of their places (the legend sleeps until every other boss is beaten).
	struct FBossDef { ERPGBossKind Kind; float X,Y,Yaw; };
	const FBossDef Bosses[]={
		{ERPGBossKind::MarshLord,-3000,28500,90},{ERPGBossKind::SandGiant,10100,33200,180},{ERPGBossKind::LavaWarden,2700,33600,200},
		{ERPGBossKind::PillarKing,20600,18600,150},{ERPGBossKind::Legend,34200,14400,180},
	};
	for(const auto& Def:Bosses)
	{
		const FVector At=K.Ground(Def.X,Def.Y,120);
		const FTransform Where(FRotator(0,Def.Yaw,0),At);
		// The same Blueprint as Maguro (its body, animation and controller set up there), at Maguro's size.
		UClass* BossClass=LoadClass<ARPGPrototypeBoss>(nullptr,TEXT("/Game/ChaosImpact/RPGPrototype/BP_RPGMaguroBoss.BP_RPGMaguroBoss_C"));
		auto* Boss=World->SpawnActorDeferred<ARPGPrototypeBoss>(BossClass?BossClass:ARPGPrototypeBoss::StaticClass(),Where,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if(!Boss) { continue; }
		Boss->Kind=Def.Kind;
		Boss->FinishSpawning(Where);
		Boss->SetActorScale3D(FVector(1.6f));
		Boss->SetActorLabel(FString::Printf(TEXT("RPG_ボス_%s"),*UEnum::GetValueAsString(Def.Kind).RightChop(14)));
		Boss->SetFolderPath(TEXT("RPGAdventure/Boss")); Boss->Tags.Add(TEXT("RPGWorld2"));
		K.Reserve(At,700); ++K.Made;
	}

	// The ways that open as a hero grows: boulders, ice, a leap (each across its secret place's road).
	struct FGateDef { uint8 Kind; float X,Y,Yaw,Width; };
	const FGateDef Gates[]={{ARPGGateObstacle::Boulder,10200,22700,55,420},{ARPGGateObstacle::Ice,25800,34500,40,420},{ARPGGateObstacle::Boulder,1500,34700,130,420}};
	for(const auto& Def:Gates)
	{
		const FTransform Where(FRotator(0,Def.Yaw,0),K.Ground(Def.X,Def.Y));
		auto* Gate=World->SpawnActorDeferred<ARPGGateObstacle>(ARPGGateObstacle::StaticClass(),Where,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if(!Gate) { continue; }
		Gate->Kind=Def.Kind; Gate->Width=Def.Width; Gate->FinishSpawning(Where);
		Gate->SetActorLabel(FString::Printf(TEXT("RPG_%s_%.0f"),Def.Kind==ARPGGateObstacle::Ice?TEXT("氷の壁"):TEXT("ひびの岩"),Def.X));
		Gate->SetFolderPath(TEXT("RPGAdventure/Gates")); Gate->Tags.Add(TEXT("RPGWorld2")); K.Reserve(Where.GetLocation(),400); ++K.Made;
	}
	{
		// The leap: from the end of the pillars' road to their top, and back.
		const FVector A=K.Ground(21450,19650), B=K.Ground(22350,20650);
		for(const auto& Pair:{TPair<FVector,FVector>(A,B),TPair<FVector,FVector>(B,A)})
		{
			auto* Leap=World->SpawnActorDeferred<ARPGLeapPoint>(ARPGLeapPoint::StaticClass(),FTransform(Pair.Key),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if(!Leap) { continue; }
			Leap->Partner=Pair.Value; Leap->FinishSpawning(FTransform(Pair.Key));
			Leap->SetActorLabel(FString::Printf(TEXT("RPG_跳躍台_%.0f"),Pair.Key.X)); Leap->SetFolderPath(TEXT("RPGAdventure/Gates")); Leap->Tags.Add(TEXT("RPGWorld2"));
			K.Reserve(Pair.Key,300); ++K.Made;
		}
	}
	{
		// The dojo in the colony: a mat, training posts and a rack beside the master.
		auto* Dojo=K.Piece(TEXT("RPG_コロニー_道場"),TEXT("こもれびコロニー/道場"),K.Ground(-450,-1050));
		const FVector C=K.Ground(-450,-1050);
		K.Part(Dojo,TEXT("Cube"),TEXT("Planks"),C+FVector(0,0,4),FVector(520,420,8));
		K.Part(Dojo,TEXT("Cube"),TEXT("Banner"),C+FVector(0,0,9),FVector(470,370,2));
		for(const float S:{-1.0f,1.0f})
		{
			const FVector Post=C+FVector(-120,S*150,0);
			K.Part(Dojo,TEXT("KitCylinder"),TEXT("Timber"),Post+FVector(0,0,80),FVector(30,30,160),FRotator::ZeroRotator,true);
			K.Part(Dojo,TEXT("KitBlob"),TEXT("Sail"),Post+FVector(0,0,150),FVector(70,70,90));
		}
		K.Part(Dojo,TEXT("Cube"),TEXT("Timber"),C+FVector(160,-200,90),FVector(30,160,180),FRotator::ZeroRotator,true);
		K.Reserve(C,350);
	}

	// 4. The places' own buildings and landmarks, their edges, then trees, rocks and small things over the new ground only.
	BuildNewLandmarks(K,R);
	DressAreaEdges(K,R,1);
	auto* Grass=K.Piece(TEXT("RPG_草むら2"),TEXT("草花"),FVector::ZeroVector);
	const auto MakeHISM=[&](AActor* Owner,const TCHAR* Mesh,const TCHAR* Material)
	{
		auto* H=NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner); H->SetupAttachment(Owner->GetRootComponent());
		H->SetStaticMesh(K.Mesh(Mesh)); H->SetMaterial(0,K.Mat(Material)); H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetCastShadow(false); H->SetCullDistances(0,9000); Owner->AddInstanceComponent(H); H->RegisterComponent(); return H;
	};
	auto* TuftA=MakeHISM(Grass,TEXT("KitTuft"),TEXT("GrassBlade")); auto* TuftB=MakeHISM(Grass,TEXT("KitTuftB"),TEXT("GrassBlade"));
	auto* Flowers=MakeHISM(Grass,TEXT("KitGem"),TEXT("FlowerWhite")); auto* Stones=MakeHISM(Grass,TEXT("KitRock"),TEXT("Rock"));
	auto* Cinders=MakeHISM(Grass,TEXT("KitRock"),TEXT("StoneDark")); auto* Drifts=MakeHISM(Grass,TEXT("KitBlobB"),SnowMat);
	int32 Big=0,Small=0;
	for(float X=RPGWorld::MinX;X<RPGWorld::MaxX;X+=230)
	{
		for(float Y=9000;Y<RPGWorld::MaxY;Y+=230)
		{
			const float PX=X+R.FRandRange(-90,90), PY=Y+R.FRandRange(-90,90);
			if(RPGWorld::PartAt(PX,PY)!=1) { continue; }
			const RPGWorld::FSample S=RPGWorld::Sample(PX,PY);
			if(!S.bLand || S.Inside<60) { continue; }
			const FVector At(PX,PY,S.Height);
			const EBiome Biome=BiomeAt(PX,PY);
			const float Track=FMath::Max3(S.Dirt,S.Pavers,S.Cave);
			float Half=0; FVector Nearest; const float Clear=RPGWorld::RoadDistance(PX,PY,&Half,&Nearest)-Half;
			const bool bInFront=PX<Nearest.X-50 && Clear<900;
			const float Roll=R.FRand();
			const bool bSpot=Track<.3f && Clear>260 && !bInFront && !K.IsReserved(At,260) && S.Inside>140;
			bool bPlaced=false;
			switch(Biome)
			{
			case EBiome::Snow:
				if(bSpot && Roll<.06f) { Tree(K,At,R.FRandRange(.9f,1.4f),R,6); bPlaced=true; }
				else if(Clear>150 && Roll<.1f && !K.IsReserved(At,200)) { Boulder(K,At,R.FRandRange(.5f,1.0f),R,TEXT("Rock"),SnowMat); bPlaced=true; }
				break;
			case EBiome::Desert:
				if(bSpot && Roll<.025f) { Tree(K,At,R.FRandRange(.8f,1.3f),R,7); bPlaced=true; }
				else if(Clear>150 && Roll<.05f && !K.IsReserved(At,200)) { Boulder(K,At,R.FRandRange(.4f,.9f),R,TEXT("Plaster")); bPlaced=true; }
				break;
			case EBiome::Volcano:
				if(Clear>150 && Roll<.08f && !K.IsReserved(At,200)) { Boulder(K,At,R.FRandRange(.5f,1.1f),R,TEXT("StoneDark")); bPlaced=true; }
				else if(bSpot && Roll<.1f) { Tree(K,At,R.FRandRange(.9f,1.2f),R,5); bPlaced=true; }
				break;
			case EBiome::Swamp:
				if(bSpot && Roll<.06f) { Tree(K,At,R.FRandRange(.9f,1.3f),R,5); bPlaced=true; }
				else if(Track<.4f && Roll<.12f && Clear>120 && !K.IsReserved(At,150)) { Mushroom(K,At,R.FRandRange(.7f,1.4f),R,R.FRand()<.6f); bPlaced=true; }
				break;
			case EBiome::Beach:
				if(bSpot && Roll<.04f && S.Height>-20) { Palm(K,At,R); bPlaced=true; }
				else if(Roll<.06f && Clear>150 && !K.IsReserved(At,200) && S.Height>-60) { Rock(K,At,R.FRandRange(.4f,.8f),R); bPlaced=true; }
				break;
			case EBiome::Canyon: case EBiome::Rocky:
				if(bSpot && Roll<.05f) { Tree(K,At,R.FRandRange(.85f,1.2f),R,1); bPlaced=true; }
				else if(Track<.3f && Roll<.12f && Clear>150 && !K.IsReserved(At,200)) { Boulder(K,At,R.FRandRange(.5f,1.0f),R,TEXT("Cliff")); bPlaced=true; }
				break;
			case EBiome::Blossom:
				if(bSpot && Roll<.06f) { Tree(K,At,R.FRandRange(.85f,1.15f),R,2); bPlaced=true; }
				else if(Track<.3f && Roll<.12f && Clear>120 && !K.IsReserved(At,150)) { Bush(K,At,R.FRandRange(.7f,1.0f),R,true); bPlaced=true; }
				break;
			default:
				if(bSpot && Roll<.03f) { Tree(K,At,R.FRandRange(.85f,1.15f),R,0); bPlaced=true; }
				else if(Track<.3f && Roll<.08f && Clear>120 && !K.IsReserved(At,150)) { Bush(K,At,R.FRandRange(.7f,1.1f),R,R.FRand()<.4f); bPlaced=true; }
				break;
			}
			if(bPlaced) { ++Big; continue; }
			// Small things: grass where it grows, snow drifts, cinders, pebbles.
			if(Biome==EBiome::Snow) { if(R.FRand()<.18f) { Drifts->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(PX,PY,S.Height-6),FVector(R.FRandRange(.6f,1.4f),R.FRandRange(.6f,1.2f),R.FRandRange(.15f,.3f))),true); ++Small; } continue; }
			if(Biome==EBiome::Volcano) { if(R.FRand()<.3f) { Cinders->AddInstance(FTransform(FRotator(R.FRandRange(-10,10),R.FRandRange(0,360),0),FVector(PX,PY,S.Height-4),FVector(R.FRandRange(.18f,.45f))),true); ++Small; } continue; }
			if(Biome==EBiome::Desert) { if(R.FRand()<.12f) { Stones->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(PX,PY,S.Height-4),FVector(R.FRandRange(.15f,.35f))),true); ++Small; } continue; }
			if(Track<.5f && Biome!=EBiome::Beach)
			{
				for(int32 I=0;I<2;++I)
				{
					const FVector P(PX+R.FRandRange(-110,110),PY+R.FRandRange(-110,110),0);
					const RPGWorld::FSample Q=RPGWorld::Sample(P.X,P.Y);
					if(!Q.bLand || FMath::Max3(Q.Dirt,Q.Pavers,Q.Cave)>.45f || Q.Inside<30 || Q.Sand>.4f) { continue; }
					(R.FRand()<.5f?TuftA:TuftB)->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(P.X,P.Y,Q.Height-3),FVector(R.FRandRange(.7f,1.3f))),true); ++Small;
				}
				if(Biome==EBiome::Blossom && R.FRand()<.4f) { Flowers->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(PX,PY,S.Height+14),FVector(.14f,.14f,.1f)),true); ++Small; }
			}
			if(R.FRand()<.06f) { Stones->AddInstance(FTransform(FRotator(0,R.FRandRange(0,360),0),FVector(PX,PY,S.Height-4),FVector(R.FRandRange(.18f,.4f))),true); ++Small; }
		}
	}
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD extend: scattered %d trees/rocks and %d small things"),Big,Small);
	// Round the new rocks: trees on their tops, boulders at their feet.
	for(const auto& Rk:RPGWorld::Rocks())
	{
		if(Rk.Part!=1) { continue; }
		const EBiome B=BiomeAt(Rk.At.X,Rk.At.Y);
		const int32 Count=R.RandRange(2,4);
		for(int32 I=0;I<Count;++I)
		{
			const float A=R.FRandRange(0,UE_TWO_PI),D=R.FRandRange(0,.55f);
			const float X=Rk.At.X+FMath::Cos(A)*Rk.Radius.X*D, Y=Rk.At.Y+FMath::Sin(A)*Rk.Radius.Y*D;
			const FVector At(X,Y,RPGWorld::Height(X,Y));
			if(B==EBiome::Snow) { Tree(K,At,R.FRandRange(1.0f,1.5f),R,6); }
			else if(B==EBiome::Desert) { Spire(K,At,R.FRandRange(300,600),R.FRandRange(220,320),TEXT("Plaster"),R); }
			else if(B==EBiome::Volcano) { Boulder(K,At,R.FRandRange(1.0f,1.8f),R,TEXT("StoneDark")); }
			else { Tree(K,At,R.FRandRange(.9f,1.3f),R,B==EBiome::Canyon||B==EBiome::Rocky?1:B==EBiome::Beach?4:0); }
		}
	}
	World->MarkPackageDirty();
	UE_LOG(LogTemp,Display,TEXT("RPGWORLD extend: made %d actors"),K.Made);
	return K.Made;
#else
	return 0;
#endif
}
