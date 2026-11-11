#include "ChaosImpactBodyPaint.h"

#include "ChaosImpactPuppetComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"

using namespace ChaosImpactBodyPaint;

namespace
{
	/**
	 * How a pixel is put on a texture: a see-through one takes its most solid neighbour's colour (still see-through), so
	 * the filtering between a drawn pixel and an empty one does not darken the edge toward black.
	 */
	FColor Spread(const FColor* Pixels, const int32 Width, const int32 X, const int32 Y)
	{
		const FColor Own = Pixels[Y * Width + X];
		if (Own.A > 0)
		{
			return Own;
		}
		FColor Best = Own;
		uint8 BestAlpha = 0;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const int32 NY = Y + DY;
			if (NY < 0 || NY >= Width)
			{
				continue;
			}
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 NX = X + DX;
				if (NX < 0 || NX >= Width)
				{
					continue;
				}
				const FColor& Other = Pixels[NY * Width + NX];
				if (Other.A > BestAlpha)
				{
					BestAlpha = Other.A;
					Best = Other;
				}
			}
		}
		return BestAlpha > 0 ? FColor(Best.R, Best.G, Best.B, 0) : Own;
	}
}

UTexture2D* ChaosImpactBodyPaint::MakeTexture(const TArray<FColor>& Pixels, UObject* Outer)
{
	if (Pixels.Num() != Size * Size)
	{
		return nullptr;
	}
	UTexture2D* Texture = NewObject<UTexture2D>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	FTexturePlatformData* Data = new FTexturePlatformData();
	Data->SizeX = Size;
	Data->SizeY = Size;
	Data->SetNumSlices(1);
	Data->PixelFormat = PF_B8G8R8A8;
	TArray<FColor> Level = Pixels;
	int32 Width = Size;
	TArray<FColor> Shown;
	for (;;)
	{
		Shown.SetNumUninitialized(Width * Width);
		for (int32 Y = 0; Y < Width; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Shown[Y * Width + X] = Spread(Level.GetData(), Width, X, Y);
			}
		}
		FTexture2DMipMap* Mip = new FTexture2DMipMap(Width, Width, 1);
		Data->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Out = Mip->BulkData.Realloc(static_cast<int64>(Width) * Width * 4);
		FMemory::Memcpy(Out, Shown.GetData(), static_cast<SIZE_T>(Width) * Width * 4);
		Mip->BulkData.Unlock();
		if (Width == 1)
		{
			break;
		}
		// The next size down: each four pixels as one, their colours weighed by how solid each is.
		const int32 Half = Width / 2;
		TArray<FColor> Next;
		Next.SetNumUninitialized(Half * Half);
		for (int32 Y = 0; Y < Half; ++Y)
		{
			for (int32 X = 0; X < Half; ++X)
			{
				float R = 0.0f, G = 0.0f, B = 0.0f, A = 0.0f;
				for (int32 K = 0; K < 4; ++K)
				{
					const FColor& P = Level[(Y * 2 + K / 2) * Width + X * 2 + K % 2];
					const float W = P.A / 255.0f;
					R += P.R * W;
					G += P.G * W;
					B += P.B * W;
					A += W;
				}
				Next[Y * Half + X] = A > 0.0f
					? FColor(static_cast<uint8>(FMath::RoundToInt(R / A)), static_cast<uint8>(FMath::RoundToInt(G / A)),
						static_cast<uint8>(FMath::RoundToInt(B / A)), static_cast<uint8>(FMath::RoundToInt(A / 4.0f * 255.0f)))
					: FColor(0, 0, 0, 0);
			}
		}
		Level = MoveTemp(Next);
		Width = Half;
	}
	Texture->SetPlatformData(Data);
	Texture->SRGB = true;
	Texture->Filter = TF_Trilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->UpdateResource();
	return Texture;
}

TArray<uint8> ChaosImpactBodyPaint::Pack(const TArray<FChaosImpactPaintLayerData>& Layers)
{
	TArray<uint8> Out;
	const auto Put = [&Out](const int32 Value)
	{
		for (int32 Byte = 0; Byte < 4; ++Byte)
		{
			Out.Add(static_cast<uint8>((Value >> (Byte * 8)) & 0xFF));
		}
	};
	int32 Count = 0;
	for (const FChaosImpactPaintLayerData& Layer : Layers)
	{
		Count += Layer.Png.IsEmpty() ? 0 : 1;
	}
	if (Count == 0)
	{
		return Out;
	}
	Out.Append(reinterpret_cast<const uint8*>("CIP1"), 4);
	Put(Count);
	for (const FChaosImpactPaintLayerData& Layer : Layers)
	{
		if (Layer.Png.IsEmpty())
		{
			continue;
		}
		Put(Layer.Layer);
		Put(Layer.Png.Num());
		Out.Append(Layer.Png);
	}
	return Out;
}

bool ChaosImpactBodyPaint::Unpack(const TArray<uint8>& Packed, TArray<FChaosImpactPaintLayerData>& OutLayers)
{
	OutLayers.Reset();
	int32 At = 0;
	const auto Get = [&Packed, &At](int32& Value)
	{
		if (At + 4 > Packed.Num())
		{
			return false;
		}
		Value = Packed[At] | (Packed[At + 1] << 8) | (Packed[At + 2] << 16) | (Packed[At + 3] << 24);
		At += 4;
		return true;
	};
	if (Packed.Num() < 8 || FMemory::Memcmp(Packed.GetData(), "CIP1", 4) != 0)
	{
		return false;
	}
	At = 4;
	int32 Count = 0;
	if (!Get(Count) || Count < 0 || Count > MaxLayers)
	{
		return false;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		int32 Layer = 0, Bytes = 0;
		if (!Get(Layer) || !Get(Bytes) || Layer < 0 || Layer >= MaxLayers || Bytes <= 0 || At + Bytes > Packed.Num())
		{
			OutLayers.Reset();
			return false;
		}
		FChaosImpactPaintLayerData& Data = OutLayers.AddDefaulted_GetRef();
		Data.Layer = Layer;
		Data.Png.Append(Packed.GetData() + At, Bytes);
		At += Bytes;
	}
	return true;
}

int32 ChaosImpactBodyPaint::KeyOf(const TArray<uint8>& Packed)
{
	if (Packed.IsEmpty())
	{
		return 0;
	}
	const uint32 Crc = FCrc::MemCrc32(Packed.GetData(), Packed.Num(), static_cast<uint32>(Packed.Num()) * 2654435761u);
	const int32 Key = static_cast<int32>(Crc & 0x7FFFFFFF);
	return Key == 0 ? 1 : Key;
}

int32 ChaosImpactBodyPaint::WeightOf(const TArray<FChaosImpactPaintLayerData>& Layers)
{
	int32 Bytes = 0;
	for (const FChaosImpactPaintLayerData& Layer : Layers)
	{
		Bytes += Layer.Png.Num();
	}
	return Bytes;
}

// ---- A layer ---------------------------------------------------------------------------------------------------------

void UChaosImpactPaintLayer::Init()
{
	Pixels.Init(FColor(0, 0, 0, 0), Size * Size);
	Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("CIPaintLayer"));
	if (Texture)
	{
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
	}
	MarkDirty(FIntRect(0, 0, Size, Size));
	Flush();
}

void UChaosImpactPaintLayer::MarkDirty(const FIntRect& Rect)
{
	const FIntRect Clamped(FMath::Clamp(Rect.Min.X, 0, Size), FMath::Clamp(Rect.Min.Y, 0, Size), FMath::Clamp(Rect.Max.X, 0, Size),
		FMath::Clamp(Rect.Max.Y, 0, Size));
	if (Clamped.Width() <= 0 || Clamped.Height() <= 0)
	{
		return;
	}
	if (!bDirty)
	{
		Dirty = Clamped;
		bDirty = true;
	}
	else
	{
		Dirty.Union(Clamped);
	}
}

void UChaosImpactPaintLayer::Flush()
{
	if (!bDirty || !Texture)
	{
		return;
	}
	bDirty = false;
	// (One pixel more round: the see-through pixels next to the changed ones take their colour too.)
	Dirty = FIntRect(FMath::Max(Dirty.Min.X - 1, 0), FMath::Max(Dirty.Min.Y - 1, 0), FMath::Min(Dirty.Max.X + 1, Size), FMath::Min(Dirty.Max.Y + 1, Size));
	const int32 W = Dirty.Width();
	const int32 H = Dirty.Height();
	// (The render thread reads its own copy of just the changed rows.)
	FColor* Copy = static_cast<FColor*>(FMemory::Malloc(static_cast<SIZE_T>(W) * H * 4));
	for (int32 Row = 0; Row < H; ++Row)
	{
		for (int32 Col = 0; Col < W; ++Col)
		{
			Copy[Row * W + Col] = Spread(Pixels.GetData(), Size, Dirty.Min.X + Col, Dirty.Min.Y + Row);
		}
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(Dirty.Min.X, Dirty.Min.Y, 0, 0, W, H);
	Texture->UpdateTextureRegions(0, 1, Region, W * 4, 4, reinterpret_cast<uint8*>(Copy), [](uint8* Data, const FUpdateTextureRegion2D* Regions)
	{
		FMemory::Free(Data);
		delete Regions;
	});
}

bool UChaosImpactPaintLayer::IsBlank() const
{
	for (const FColor& Pixel : Pixels)
	{
		if (Pixel.A != 0)
		{
			return false;
		}
	}
	return true;
}

void UChaosImpactPaintLayer::Clear()
{
	for (FColor& Pixel : Pixels)
	{
		Pixel = FColor(0, 0, 0, 0);
	}
	MarkDirty(FIntRect(0, 0, Size, Size));
	Flush();
}

TArray<uint8> UChaosImpactPaintLayer::ToPng() const
{
	TArray64<uint8> Out;
	FImageUtils::PNGCompressImageArray(Size, Size, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Out);
	return TArray<uint8>(Out.GetData(), static_cast<int32>(Out.Num()));
}

bool UChaosImpactPaintLayer::FromPng(const TArray<uint8>& Png)
{
	FImage Image;
	if (Png.IsEmpty() || !FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Image) || Image.SizeX != Size || Image.SizeY != Size)
	{
		return false;
	}
	FImage Bgra;
	Image.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> View = Bgra.AsBGRA8();
	if (View.Num() != static_cast<int64>(Size) * Size)
	{
		return false;
	}
	if (Pixels.Num() != Size * Size)
	{
		Init();
	}
	FMemory::Memcpy(Pixels.GetData(), View.GetData(), static_cast<SIZE_T>(Size) * Size * sizeof(FColor));
	MarkDirty(FIntRect(0, 0, Size, Size));
	Flush();
	return true;
}

// ---- The surface -----------------------------------------------------------------------------------------------------

namespace
{
	struct FIslandSets
	{
		TArray<int32> Parent;
		int32 Find(int32 A)
		{
			while (Parent[A] != A)
			{
				Parent[A] = Parent[Parent[A]];
				A = Parent[A];
			}
			return A;
		}
		void Join(const int32 A, const int32 B)
		{
			const int32 RA = Find(A), RB = Find(B);
			if (RA != RB)
			{
				Parent[RA] = RB;
			}
		}
	};

	/** Calls Visit(X, Y, Barycentric) for each pixel of the triangle (in pixel space), a little past its edges. */
	template <typename FVisit>
	void RasteriseUV(const FVector2f& A, const FVector2f& B, const FVector2f& C, FVisit&& Visit)
	{
		const float Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
		if (FMath::Abs(Area) < 1e-8f)
		{
			return;
		}
		const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)) - 1, 0, Size - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)) + 1, 0, Size - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)) - 1, 0, Size - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)) + 1, 0, Size - 1);
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2f P(X + 0.5f, Y + 0.5f);
				const float W0 = ((B.X - P.X) * (C.Y - P.Y) - (B.Y - P.Y) * (C.X - P.X)) / Area;
				const float W1 = ((C.X - P.X) * (A.Y - P.Y) - (C.Y - P.Y) * (A.X - P.X)) / Area;
				const float W2 = 1.0f - W0 - W1;
				if (W0 >= 0.0f && W1 >= 0.0f && W2 >= 0.0f)
				{
					Visit(X, Y, FVector3f(W0, W1, W2));
				}
			}
		}
	}

	FColor Composite(const FColor& Base, const FChaosImpactBrush& Brush, const float Weight)
	{
		if (Brush.bErase)
		{
			FColor Out = Base;
			Out.A = static_cast<uint8>(FMath::RoundToInt(Base.A * (1.0f - Weight)));
			return Out;
		}
		const float SrcA = Weight;
		const float BaseA = Base.A / 255.0f;
		const float OutA = SrcA + BaseA * (1.0f - SrcA);
		if (OutA <= 0.0f)
		{
			return FColor(0, 0, 0, 0);
		}
		const auto Mix = [&](const uint8 Src, const uint8 Dst)
		{
			return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Src * SrcA + Dst * BaseA * (1.0f - SrcA)) / OutA), 0, 255));
		};
		return FColor(Mix(Brush.Colour.R, Base.R), Mix(Brush.Colour.G, Base.G), Mix(Brush.Colour.B, Base.B),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(OutA * 255.0f), 0, 255)));
	}
}

bool FChaosImpactPaintSurface::Build(const UChaosImpactPuppetComponent* Model)
{
	Tris.Reset();
	Covered.Reset();
	LayerCount = 0;
	ForgetHistory();
	if (!Model)
	{
		return false;
	}
	TArray<UChaosImpactPuppetComponent::FPaintPiece> Pieces;
	Model->GetPaintPieces(Pieces);
	LayerCount = Model->GetPaintLayerCount();
	MirrorPoint = Model->GetComponentLocation();
	MirrorNormal = Model->GetRightVector();
	FIslandSets Sets;
	int32 Base = 0;
	for (const UChaosImpactPuppetComponent::FPaintPiece& Piece : Pieces)
	{
		const UMeshComponent* Mesh = Piece.Mesh;
		if (!Mesh)
		{
			continue;
		}
		const FTransform World = Mesh->GetComponentTransform();
		TArray<FVector3f> Positions;
		TArray<FVector3f> BindPositions;
		TArray<FVector3f> BindNormals;
		TArray<FVector2f> UVs;
		TArray<uint32> Indices;
		struct FSection { int32 First; int32 Count; int32 Material; };
		TArray<FSection> Sections;
		if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh))
		{
			const FStaticMeshRenderData* Data = Static->GetStaticMesh() ? Static->GetStaticMesh()->GetRenderData() : nullptr;
			if (!Data || Data->LODResources.IsEmpty())
			{
				continue;
			}
			const FStaticMeshLODResources& LOD = Data->LODResources[0];
			// (A packaged game keeps these only for meshes that allow CPU access: Scripts/Characters/CustomColour/allow_cpu_access.py.)
			if (!LOD.VertexBuffers.PositionVertexBuffer.GetVertexData() || !LOD.VertexBuffers.StaticMeshVertexBuffer.GetTangentData()
				|| !LOD.VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData())
			{
				continue;
			}
			const int32 Verts = LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices();
			for (int32 V = 0; V < Verts; ++V)
			{
				Positions.Add(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(V));
				UVs.Add(LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V, 0));
				BindNormals.Add(FVector3f(LOD.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V)));
			}
			BindPositions = Positions;
			LOD.IndexBuffer.GetCopy(Indices);
			for (const FStaticMeshSection& Section : LOD.Sections)
			{
				Sections.Add({static_cast<int32>(Section.FirstIndex), static_cast<int32>(Section.NumTriangles), Section.MaterialIndex});
			}
		}
		else if (const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Mesh))
		{
			FSkeletalMeshRenderData* Data = Skinned->GetSkeletalMeshRenderData();
			if (!Data || Data->LODRenderData.IsEmpty())
			{
				continue;
			}
			const FSkeletalMeshLODRenderData& LOD = Data->LODRenderData[0];
			if (!LOD.StaticVertexBuffers.PositionVertexBuffer.GetVertexData() || !LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentData()
				|| !LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordData() || !LOD.SkinWeightVertexBuffer.GetDataVertexBuffer()
				|| !LOD.SkinWeightVertexBuffer.GetDataVertexBuffer()->GetWeightData())
			{
				continue;
			}
			TArray<FMatrix44f> RefToLocals;
			Skinned->CacheRefToLocalMatrices(RefToLocals);
			USkinnedMeshComponent::ComputeSkinnedPositions(const_cast<USkinnedMeshComponent*>(Skinned), Positions, RefToLocals, LOD,
				LOD.SkinWeightVertexBuffer);
			const int32 Verts = LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
			if (Positions.Num() != Verts)
			{
				continue;
			}
			for (int32 V = 0; V < Verts; ++V)
			{
				BindPositions.Add(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V));
				UVs.Add(LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V, 0));
				BindNormals.Add(FVector3f(LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V)));
			}
			LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
			for (const FSkelMeshRenderSection& Section : LOD.RenderSections)
			{
				Sections.Add({static_cast<int32>(Section.BaseIndex), static_cast<int32>(Section.NumTriangles), Section.MaterialIndex});
			}
		}
		if (Positions.IsEmpty() || Indices.IsEmpty())
		{
			continue;
		}
		// Which way the mesh's triangles face: their winding against its own normals (as built, before posing).
		int32 Agree = 0;
		for (int32 I = 0; I + 2 < Indices.Num(); I += 3)
		{
			const FVector3f G = FVector3f::CrossProduct(BindPositions[Indices[I + 1]] - BindPositions[Indices[I]],
				BindPositions[Indices[I + 2]] - BindPositions[Indices[I]]);
			Agree += FVector3f::DotProduct(G, BindNormals[Indices[I]] + BindNormals[Indices[I + 1]] + BindNormals[Indices[I + 2]]) >= 0.0f ? 1 : -1;
		}
		const float Facing = Agree >= 0 ? 1.0f : -1.0f;
		const int32 VertexBase = Base;
		Base += Positions.Num();
		Sets.Parent.Reserve(Base);
		for (int32 V = VertexBase; V < Base; ++V)
		{
			Sets.Parent.Add(V);
		}
		for (const FSection& Section : Sections)
		{
			const int32 Layer = Piece.SlotLayers.IsValidIndex(Section.Material) ? Piece.SlotLayers[Section.Material] : 0;
			if (Layer < 0)
			{
				continue;
			}
			for (int32 T = 0; T < Section.Count; ++T)
			{
				const int32 I = Section.First + T * 3;
				if (I + 2 >= Indices.Num())
				{
					break;
				}
				FTri Tri;
				for (int32 K = 0; K < 3; ++K)
				{
					Tri.P[K] = FVector3f(World.TransformPosition(FVector(Positions[Indices[I + K]])));
					Tri.UV[K] = UVs[Indices[I + K]];
				}
				Tri.Normal = FVector3f::CrossProduct(Tri.P[1] - Tri.P[0], Tri.P[2] - Tri.P[0]).GetSafeNormal() * Facing;
				Tri.Centre = (Tri.P[0] + Tri.P[1] + Tri.P[2]) / 3.0f;
				for (int32 K = 0; K < 3; ++K)
				{
					Tri.R[K] = BindPositions[Indices[I + K]];
				}
				Tri.RestNormal = FVector3f::CrossProduct(Tri.R[1] - Tri.R[0], Tri.R[2] - Tri.R[0]).GetSafeNormal() * Facing;
				Tri.RestCentre = (Tri.R[0] + Tri.R[1] + Tri.R[2]) / 3.0f;
				Tri.RestReach = FMath::Max3((Tri.R[0] - Tri.RestCentre).Size(), (Tri.R[1] - Tri.RestCentre).Size(), (Tri.R[2] - Tri.RestCentre).Size());
				Tri.Reach = FMath::Max3((Tri.P[0] - Tri.Centre).Size(), (Tri.P[1] - Tri.Centre).Size(), (Tri.P[2] - Tri.Centre).Size());
				Tri.Layer = Layer;
				Tri.Island = VertexBase + static_cast<int32>(Indices[I]);
				Sets.Join(VertexBase + static_cast<int32>(Indices[I]), VertexBase + static_cast<int32>(Indices[I + 1]));
				Sets.Join(VertexBase + static_cast<int32>(Indices[I]), VertexBase + static_cast<int32>(Indices[I + 2]));
				Tris.Add(Tri);
			}
		}
	}
	for (FTri& Tri : Tris)
	{
		Tri.Island = Sets.Find(Tri.Island);
	}
	Bounds = FBox(ForceInit);
	for (const FTri& Tri : Tris)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			Bounds += FVector(Tri.P[K]);
		}
	}
	FindMirror(Model);
	BuildGrid();
	BuildNeighbours();
	// One mesh: mirrored in its own rest pose. (The limbs' model is five meshes: mirrored where it stands.)
	bRestMirror = false;
	if (Pieces.Num() == 1 && Pieces[0].Mesh)
	{
		RestScale = FMath::Max(static_cast<float>(Pieces[0].Mesh->GetComponentTransform().GetMaximumAxisScale()), 0.001f);
		FindRestMirror();
	}
	LayerCount = FMath::Clamp(LayerCount, 1, MaxLayers);
	Covered.SetNum(LayerCount);
	for (TBitArray<>& Bits : Covered)
	{
		Bits.Init(false, Size * Size);
	}
	for (const FTri& Tri : Tris)
	{
		if (!Covered.IsValidIndex(Tri.Layer))
		{
			continue;
		}
		RasteriseUV(Tri.UV[0] * Size, Tri.UV[1] * Size, Tri.UV[2] * Size, [this, &Tri](const int32 X, const int32 Y, const FVector3f&)
		{
			Covered[Tri.Layer][Y * Size + X] = true;
		});
	}
	return !Tris.IsEmpty();
}

void FChaosImpactPaintSurface::FindMirror(const UChaosImpactPuppetComponent* Model)
{
	// The body's middle, across its sideways axis: each character faces +Y on its stand (the preview's Pose), so
	// sideways is the stand's X. (Through the model's root, which stands in the middle of its feet.)
	const USceneComponent* Stand = Model->GetAttachParent();
	MirrorNormal = Stand ? Stand->GetForwardVector() : Model->GetRightVector();
	MirrorNormal.Z = 0.0;
	if (!MirrorNormal.Normalize())
	{
		MirrorNormal = FVector::ForwardVector;
	}
	MirrorPoint = Model->GetComponentLocation();
}

void FChaosImpactPaintSurface::FindRestMirror()
{
	// Of the mesh's own X and Y, the one the rest pose matches itself best across (a few pieces mirrored, the nearest found).
	if (Tris.Num() < 8)
	{
		return;
	}
	const int32 Samples = FMath::Min(200, Tris.Num());
	const int32 Every = FMath::Max(1, Tris.Num() / Samples);
	float BestScore = TNumericLimits<float>::Max();
	float Extent = 0.0f;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		float Low = TNumericLimits<float>::Max(), High = -TNumericLimits<float>::Max();
		for (const FTri& Tri : Tris)
		{
			Low = FMath::Min(Low, Tri.RestCentre[Axis]);
			High = FMath::Max(High, Tri.RestCentre[Axis]);
		}
		const float Middle = (Low + High) * 0.5f;
		float Score = 0.0f;
		int32 Count = 0;
		for (int32 Index = 0; Index < Tris.Num(); Index += Every)
		{
			FVector3f Mirrored = Tris[Index].RestCentre;
			Mirrored[Axis] = 2.0f * Middle - Mirrored[Axis];
			float Nearest = TNumericLimits<float>::Max();
			for (const FTri& Other : Tris)
			{
				Nearest = FMath::Min(Nearest, (Other.RestCentre - Mirrored).SizeSquared());
			}
			Score += FMath::Sqrt(Nearest);
			++Count;
		}
		Score /= FMath::Max(Count, 1);
		if (Score < BestScore)
		{
			BestScore = Score;
			RestAxis = Axis;
			RestMiddle = Middle;
			Extent = High - Low;
		}
	}
	// (A shape that is not two-sided at all keeps the mirror where it stands.)
	bRestMirror = Extent > 0.0f && BestScore < Extent * 0.08f;
}

void FChaosImpactPaintSurface::RestOf(const FHit& Hit, FVector3f& OutRest, FVector3f& OutNormal) const
{
	OutRest = FVector3f::ZeroVector;
	OutNormal = FVector3f::UpVector;
	if (Tris.IsValidIndex(Hit.Triangle))
	{
		const FTri& Tri = Tris[Hit.Triangle];
		OutRest = Tri.R[0] * Hit.Bary.X + Tri.R[1] * Hit.Bary.Y + Tri.R[2] * Hit.Bary.Z;
		OutNormal = Tri.RestNormal;
	}
}

bool FChaosImpactPaintSurface::GetMirrorLocation(const FHit& Hit, FVector& OutLocation) const
{
	if (!bRestMirror)
	{
		OutLocation = Hit.Location - MirrorNormal * (2.0 * FVector::DotProduct(Hit.Location - MirrorPoint, MirrorNormal));
		return true;
	}
	FVector3f Rest, Normal;
	RestOf(Hit, Rest, Normal);
	Rest[RestAxis] = 2.0f * RestMiddle - Rest[RestAxis];
	Normal[RestAxis] = -Normal[RestAxis];
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Tris.Num(); ++Index)
	{
		const float Distance = (Tris[Index].RestCentre - Rest).SizeSquared();
		if (Distance < BestDistance && FVector3f::DotProduct(Tris[Index].RestNormal, Normal) > 0.0f)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutLocation = FVector(Tris[Best].Centre);
	return true;
}

void FChaosImpactPaintSurface::BuildNeighbours()
{
	Neighbours.Init(FIntVector(INDEX_NONE), Tris.Num());
	TMap<FIntVector, int32> Welds;
	const auto WeldOf = [&Welds](const FVector3f& P)
	{
		const FIntVector Key(FMath::RoundToInt(P.X * 20.0f), FMath::RoundToInt(P.Y * 20.0f), FMath::RoundToInt(P.Z * 20.0f));
		if (const int32* Found = Welds.Find(Key))
		{
			return *Found;
		}
		const int32 Id = Welds.Num();
		Welds.Add(Key, Id);
		return Id;
	};
	TMap<uint64, int32> FirstOnEdge;
	for (int32 Index = 0; Index < Tris.Num(); ++Index)
	{
		const int32 W[3] = {WeldOf(Tris[Index].P[0]), WeldOf(Tris[Index].P[1]), WeldOf(Tris[Index].P[2])};
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const int32 A = W[Edge], B = W[(Edge + 1) % 3];
			if (A == B)
			{
				continue;
			}
			const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint32>(FMath::Max(A, B));
			if (const int32* Other = FirstOnEdge.Find(Key))
			{
				if (Neighbours[Index][Edge] == INDEX_NONE)
				{
					Neighbours[Index][Edge] = *Other;
				}
				FIntVector& Theirs = Neighbours[*Other];
				for (int32 Slot = 0; Slot < 3; ++Slot)
				{
					if (Theirs[Slot] == INDEX_NONE)
					{
						Theirs[Slot] = Index;
						break;
					}
				}
			}
			else
			{
				FirstOnEdge.Add(Key, Index);
			}
		}
	}
}

void FChaosImpactPaintSurface::BuildGrid()
{
	Grid.Reset();
	NearMark.Init(0, Tris.Num());
	NearStamp = 0;
	if (!Bounds.IsValid || Tris.IsEmpty())
	{
		GridCells = FIntVector(1, 1, 1);
		return;
	}
	const FVector Extent = Bounds.GetSize();
	GridCell = FMath::Max(static_cast<float>(Extent.GetMax()) / 48.0f, 2.0f);
	GridCells = FIntVector(FMath::Clamp(FMath::CeilToInt(Extent.X / GridCell), 1, 64), FMath::Clamp(FMath::CeilToInt(Extent.Y / GridCell), 1, 64),
		FMath::Clamp(FMath::CeilToInt(Extent.Z / GridCell), 1, 64));
	Grid.SetNum(GridCells.X * GridCells.Y * GridCells.Z);
	const FVector3f Origin(Bounds.Min);
	for (int32 Index = 0; Index < Tris.Num(); ++Index)
	{
		const FTri& Tri = Tris[Index];
		FVector3f Low = Tri.P[0], High = Tri.P[0];
		for (int32 K = 1; K < 3; ++K)
		{
			Low = FVector3f(FMath::Min(Low.X, Tri.P[K].X), FMath::Min(Low.Y, Tri.P[K].Y), FMath::Min(Low.Z, Tri.P[K].Z));
			High = FVector3f(FMath::Max(High.X, Tri.P[K].X), FMath::Max(High.Y, Tri.P[K].Y), FMath::Max(High.Z, Tri.P[K].Z));
		}
		const FIntVector A(FMath::Clamp(FMath::FloorToInt((Low.X - Origin.X) / GridCell), 0, GridCells.X - 1),
			FMath::Clamp(FMath::FloorToInt((Low.Y - Origin.Y) / GridCell), 0, GridCells.Y - 1),
			FMath::Clamp(FMath::FloorToInt((Low.Z - Origin.Z) / GridCell), 0, GridCells.Z - 1));
		const FIntVector B(FMath::Clamp(FMath::FloorToInt((High.X - Origin.X) / GridCell), 0, GridCells.X - 1),
			FMath::Clamp(FMath::FloorToInt((High.Y - Origin.Y) / GridCell), 0, GridCells.Y - 1),
			FMath::Clamp(FMath::FloorToInt((High.Z - Origin.Z) / GridCell), 0, GridCells.Z - 1));
		for (int32 Z = A.Z; Z <= B.Z; ++Z)
		{
			for (int32 Y = A.Y; Y <= B.Y; ++Y)
			{
				for (int32 X = A.X; X <= B.X; ++X)
				{
					Grid[(Z * GridCells.Y + Y) * GridCells.X + X].Add(Index);
				}
			}
		}
	}
}

void FChaosImpactPaintSurface::Near(const FVector3f& At, const float Reach, TArray<int32>& Out) const
{
	Out.Reset();
	if (Grid.IsEmpty())
	{
		for (int32 Index = 0; Index < Tris.Num(); ++Index)
		{
			Out.Add(Index);
		}
		return;
	}
	if (++NearStamp == 0)
	{
		NearMark.Init(0, Tris.Num());
		NearStamp = 1;
	}
	const FVector3f Origin(Bounds.Min);
	const FIntVector A(FMath::Clamp(FMath::FloorToInt((At.X - Reach - Origin.X) / GridCell), 0, GridCells.X - 1),
		FMath::Clamp(FMath::FloorToInt((At.Y - Reach - Origin.Y) / GridCell), 0, GridCells.Y - 1),
		FMath::Clamp(FMath::FloorToInt((At.Z - Reach - Origin.Z) / GridCell), 0, GridCells.Z - 1));
	const FIntVector B(FMath::Clamp(FMath::FloorToInt((At.X + Reach - Origin.X) / GridCell), 0, GridCells.X - 1),
		FMath::Clamp(FMath::FloorToInt((At.Y + Reach - Origin.Y) / GridCell), 0, GridCells.Y - 1),
		FMath::Clamp(FMath::FloorToInt((At.Z + Reach - Origin.Z) / GridCell), 0, GridCells.Z - 1));
	for (int32 Z = A.Z; Z <= B.Z; ++Z)
	{
		for (int32 Y = A.Y; Y <= B.Y; ++Y)
		{
			for (int32 X = A.X; X <= B.X; ++X)
			{
				for (const int32 Index : Grid[(Z * GridCells.Y + Y) * GridCells.X + X])
				{
					if (NearMark[Index] != NearStamp)
					{
						NearMark[Index] = NearStamp;
						Out.Add(Index);
					}
				}
			}
		}
	}
}

bool FChaosImpactPaintSurface::Raycast(const FVector& Origin, const FVector& Direction, FHit& OutHit) const
{
	const FVector3f O(Origin);
	const FVector3f D(Direction.GetSafeNormal());
	float Best = TNumericLimits<float>::Max();
	int32 Found = INDEX_NONE;
	FVector3f BestBary(1.0f, 0.0f, 0.0f);
	for (int32 Index = 0; Index < Tris.Num(); ++Index)
	{
		const FTri& Tri = Tris[Index];
		// (Only the side facing the camera.)
		if (FVector3f::DotProduct(Tri.Normal, D) > 0.0f)
		{
			continue;
		}
		const FVector3f E1 = Tri.P[1] - Tri.P[0];
		const FVector3f E2 = Tri.P[2] - Tri.P[0];
		const FVector3f PVec = FVector3f::CrossProduct(D, E2);
		const float Det = FVector3f::DotProduct(E1, PVec);
		if (FMath::Abs(Det) < 1e-9f)
		{
			continue;
		}
		const float Inv = 1.0f / Det;
		const FVector3f TVec = O - Tri.P[0];
		const float U = FVector3f::DotProduct(TVec, PVec) * Inv;
		if (U < 0.0f || U > 1.0f)
		{
			continue;
		}
		const FVector3f QVec = FVector3f::CrossProduct(TVec, E1);
		const float V = FVector3f::DotProduct(D, QVec) * Inv;
		if (V < 0.0f || U + V > 1.0f)
		{
			continue;
		}
		const float T = FVector3f::DotProduct(E2, QVec) * Inv;
		if (T > 0.0f && T < Best)
		{
			Best = T;
			Found = Index;
			BestBary = FVector3f(1.0f - U - V, U, V);
		}
	}
	if (Found == INDEX_NONE)
	{
		return false;
	}
	const FTri& Tri = Tris[Found];
	OutHit.Location = FVector(O + D * Best);
	OutHit.Normal = FVector(Tri.Normal);
	OutHit.Triangle = Found;
	OutHit.Layer = Tri.Layer;
	OutHit.UV = Tri.UV[0] * BestBary.X + Tri.UV[1] * BestBary.Y + Tri.UV[2] * BestBary.Z;
	OutHit.Bary = BestBary;
	return true;
}

FChaosImpactPaintSurface::FTileEdit& FChaosImpactPaintSurface::Touch(const int32 Layer, const int32 Tile)
{
	const int64 Key = (static_cast<int64>(Layer) << 32) | Tile;
	if (const int32* At = StrokeTiles.Find(Key))
	{
		return StrokeStep.Tiles[*At];
	}
	FTileEdit& Edit = StrokeStep.Tiles.AddDefaulted_GetRef();
	Edit.Layer = Layer;
	Edit.Tile = Tile;
	Edit.Before.SetNumUninitialized(TileSize * TileSize);
	const TArray<FColor>& Pixels = StrokeLayers[Layer]->GetPixels();
	const int32 X0 = (Tile % TilesPerSide) * TileSize;
	const int32 Y0 = (Tile / TilesPerSide) * TileSize;
	for (int32 Row = 0; Row < TileSize; ++Row)
	{
		FMemory::Memcpy(&Edit.Before[Row * TileSize], &Pixels[(Y0 + Row) * Size + X0], TileSize * sizeof(FColor));
	}
	StrokeTiles.Add(Key, StrokeStep.Tiles.Num() - 1);
	return Edit;
}

void FChaosImpactPaintSurface::BeginStroke(const TArray<UChaosImpactPaintLayer*>& Layers, const FChaosImpactBrush& Brush)
{
	if (bStroking)
	{
		EndStroke();
	}
	bStroking = true;
	StrokeLayers = Layers;
	StrokeBrush = Brush;
	bHasLastDab = false;
	StrokeTiles.Reset();
	StrokeStep = FStep();
	StrokeWeights.Reset();
	StrokeRects.Reset();
	StrokeRects.SetNum(Layers.Num());
}

void FChaosImpactPaintSurface::StrokeTo(const FHit& Hit, const FVector& ViewDirection)
{
	if (!bStroking)
	{
		return;
	}
	const FVector Location = Hit.Location;
	FVector3f Rest, RestNormal;
	RestOf(Hit, Rest, RestNormal);
	// Dabs a quarter of the brush apart, from the last one to here (a fast stroke stays a line).
	const float Spacing = FMath::Max(StrokeBrush.Radius * 0.25f, 0.2f);
	if (!bHasLastDab)
	{
		Dab(Location, ViewDirection, Rest, RestNormal);
		LastDab = Location;
		LastRest = Rest;
		LastRestNormal = RestNormal;
		bHasLastDab = true;
	}
	else
	{
		const float Gap = static_cast<float>(FVector::Dist(LastDab, Location));
		const int32 Steps = FMath::Min(FMath::FloorToInt(Gap / Spacing), 200);
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const float T = Step / static_cast<float>(FMath::Max(Steps, 1));
			Dab(FMath::Lerp(LastDab, Location, T), ViewDirection, FMath::Lerp(LastRest, Rest, T),
				FMath::Lerp(LastRestNormal, RestNormal, T).GetSafeNormal());
		}
		if (Steps > 0)
		{
			LastDab = Location;
			LastRest = Rest;
			LastRestNormal = RestNormal;
		}
	}
	for (UChaosImpactPaintLayer* Layer : StrokeLayers)
	{
		if (Layer)
		{
			Layer->Flush();
		}
	}
}

void FChaosImpactPaintSurface::Dab(const FVector& Centre, const FVector& ViewDirection, const FVector3f& Rest, const FVector3f& RestNormal)
{
	const FVector3f C(Centre);
	const FVector3f View(ViewDirection.GetSafeNormal());
	const float R = StrokeBrush.Radius;
	const float Hard = FMath::Clamp(StrokeBrush.Hardness, 0.0f, 0.98f);
	TArray<int32> Close;
	const auto DabAt = [&](const FVector3f& At, const FVector3f& Look)
	{
		Near(At, R, Close);
		for (const int32 Index : Close)
		{
			const FTri& Tri = Tris[Index];
			if ((Tri.Centre - At).Size() > R + Tri.Reach || !StrokeLayers.IsValidIndex(Tri.Layer) || !StrokeLayers[Tri.Layer])
			{
				continue;
			}
			// (Not through to the far side of a thin part.)
			if (FVector3f::DotProduct(Tri.Normal, Look) > 0.15f)
			{
				continue;
			}
			RasteriseUV(Tri.UV[0] * Size, Tri.UV[1] * Size, Tri.UV[2] * Size, [&](const int32 X, const int32 Y, const FVector3f& Bary)
			{
				const FVector3f P = Tri.P[0] * Bary.X + Tri.P[1] * Bary.Y + Tri.P[2] * Bary.Z;
				const float Distance = (P - At).Size();
				if (Distance >= R)
				{
					return;
				}
				const float T = Distance / R;
				const float Falloff = T <= Hard ? 1.0f : 1.0f - FMath::SmoothStep(Hard, 1.0f, T);
				Apply(Tri.Layer, X, Y, Falloff * StrokeBrush.Opacity);
			});
		}
	};
	DabAt(C, View);
	if (StrokeBrush.bMirror && bRestMirror)
	{
		// The other side of the body as it was made: true to it whatever the pose.
		FVector3f At = Rest;
		At[RestAxis] = 2.0f * RestMiddle - At[RestAxis];
		FVector3f Facing = RestNormal;
		Facing[RestAxis] = -Facing[RestAxis];
		const float Reach = R / RestScale;
		for (const FTri& Tri : Tris)
		{
			if ((Tri.RestCentre - At).Size() > Reach + Tri.RestReach || !StrokeLayers.IsValidIndex(Tri.Layer) || !StrokeLayers[Tri.Layer]
				|| FVector3f::DotProduct(Tri.RestNormal, Facing) < -0.15f)
			{
				continue;
			}
			RasteriseUV(Tri.UV[0] * Size, Tri.UV[1] * Size, Tri.UV[2] * Size, [&](const int32 X, const int32 Y, const FVector3f& Bary)
			{
				const FVector3f P = Tri.R[0] * Bary.X + Tri.R[1] * Bary.Y + Tri.R[2] * Bary.Z;
				const float Distance = (P - At).Size();
				if (Distance >= Reach)
				{
					return;
				}
				const float T = Distance / Reach;
				const float Falloff = T <= Hard ? 1.0f : 1.0f - FMath::SmoothStep(Hard, 1.0f, T);
				Apply(Tri.Layer, X, Y, Falloff * StrokeBrush.Opacity);
			});
		}
	}
	else if (StrokeBrush.bMirror)
	{
		const FVector3f N(MirrorNormal);
		const FVector3f O(MirrorPoint);
		const FVector3f Mirrored = C - N * (2.0f * FVector3f::DotProduct(C - O, N));
		const FVector3f MirroredView = View - N * (2.0f * FVector3f::DotProduct(View, N));
		DabAt(Mirrored, MirroredView);
	}
}

void FChaosImpactPaintSurface::Apply(const int32 Layer, const int32 X, const int32 Y, const float Weight)
{
	if (Weight <= 0.0f)
	{
		return;
	}
	const int64 Key = (static_cast<int64>(Layer) << 32) | (Y * Size + X);
	float& Strongest = StrokeWeights.FindOrAdd(Key, 0.0f);
	if (Weight <= Strongest)
	{
		return;
	}
	Strongest = Weight;
	const int32 Tile = (Y / TileSize) * TilesPerSide + X / TileSize;
	FTileEdit& Edit = Touch(Layer, Tile);
	const FColor& Before = Edit.Before[(Y % TileSize) * TileSize + X % TileSize];
	StrokeLayers[Layer]->GetPixels()[Y * Size + X] = Composite(Before, StrokeBrush, Weight);
	const FIntRect Pixel(X, Y, X + 1, Y + 1);
	StrokeLayers[Layer]->MarkDirty(Pixel);
	if (StrokeRects[Layer].Area() <= 0)
	{
		StrokeRects[Layer] = Pixel;
	}
	else
	{
		StrokeRects[Layer].Union(Pixel);
	}
}

void FChaosImpactPaintSurface::Bleed(const int32 Layer, const FIntRect& Rect)
{
	if (!Covered.IsValidIndex(Layer) || !StrokeLayers.IsValidIndex(Layer) || !StrokeLayers[Layer] || Rect.Area() <= 0)
	{
		return;
	}
	TArray<FColor>& Pixels = StrokeLayers[Layer]->GetPixels();
	const TBitArray<>& Inside = Covered[Layer];
	constexpr int32 Reach = 2;
	const FIntRect Area(FMath::Max(Rect.Min.X - Reach, 0), FMath::Max(Rect.Min.Y - Reach, 0), FMath::Min(Rect.Max.X + Reach, Size),
		FMath::Min(Rect.Max.Y + Reach, Size));
	for (int32 Y = Area.Min.Y; Y < Area.Max.Y; ++Y)
	{
		for (int32 X = Area.Min.X; X < Area.Max.X; ++X)
		{
			if (Inside[Y * Size + X])
			{
				continue;
			}
			// Outside every piece: the nearest piece's pixel (or nothing, when none is near).
			FColor Take(0, 0, 0, 0);
			int32 Nearest = MAX_int32;
			for (int32 DY = -Reach; DY <= Reach; ++DY)
			{
				for (int32 DX = -Reach; DX <= Reach; ++DX)
				{
					const int32 NX = X + DX, NY = Y + DY;
					if (NX < 0 || NY < 0 || NX >= Size || NY >= Size || !Inside[NY * Size + NX])
					{
						continue;
					}
					const int32 D = DX * DX + DY * DY;
					if (D < Nearest)
					{
						Nearest = D;
						Take = Pixels[NY * Size + NX];
					}
				}
			}
			if (Nearest == MAX_int32 || Pixels[Y * Size + X] == Take)
			{
				continue;
			}
			Touch(Layer, (Y / TileSize) * TilesPerSide + X / TileSize);
			Pixels[Y * Size + X] = Take;
			StrokeLayers[Layer]->MarkDirty(FIntRect(X, Y, X + 1, Y + 1));
		}
	}
}

void FChaosImpactPaintSurface::EndStroke()
{
	if (!bStroking)
	{
		return;
	}
	for (int32 Layer = 0; Layer < StrokeRects.Num(); ++Layer)
	{
		Bleed(Layer, StrokeRects[Layer]);
	}
	for (UChaosImpactPaintLayer* Layer : StrokeLayers)
	{
		if (Layer)
		{
			Layer->Flush();
		}
	}
	FinishStep();
	bStroking = false;
	StrokeWeights.Reset();
}

void FChaosImpactPaintSurface::FinishStep()
{
	if (StrokeStep.Tiles.IsEmpty())
	{
		return;
	}
	for (FTileEdit& Edit : StrokeStep.Tiles)
	{
		Edit.After.SetNumUninitialized(TileSize * TileSize);
		const TArray<FColor>& Pixels = StrokeLayers[Edit.Layer]->GetPixels();
		const int32 X0 = (Edit.Tile % TilesPerSide) * TileSize;
		const int32 Y0 = (Edit.Tile / TilesPerSide) * TileSize;
		for (int32 Row = 0; Row < TileSize; ++Row)
		{
			FMemory::Memcpy(&Edit.After[Row * TileSize], &Pixels[(Y0 + Row) * Size + X0], TileSize * sizeof(FColor));
		}
	}
	Undo.Add(MoveTemp(StrokeStep));
	if (Undo.Num() > MaxUndo)
	{
		Undo.RemoveAt(0);
	}
	Redo.Reset();
	StrokeStep = FStep();
	StrokeTiles.Reset();
}

void FChaosImpactPaintSurface::Fill(const TArray<UChaosImpactPaintLayer*>& Layers, const FHit& Hit, const FChaosImpactBrush& Brush)
{
	if (!Tris.IsValidIndex(Hit.Triangle) || !Layers.IsValidIndex(Hit.Layer) || !Layers[Hit.Layer])
	{
		return;
	}
	BeginStroke(Layers, Brush);
	// The colour clicked (drawn there, or nothing), and everything joined to it across the surface that is that colour
	// too: a blank model fills whole, a shape drawn round an area keeps the fill inside it.
	const auto ColourAt = [this](const int32 Layer, const FVector2f& UV)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * Size), 0, Size - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * Size), 0, Size - 1);
		return StrokeLayers[Layer]->GetPixels()[Y * Size + X];
	};
	const FColor Target = ColourAt(Hit.Layer, Hit.UV);
	const auto Matches = [&Target](const FColor& Colour)
	{
		if (Colour.A < 8 || Target.A < 8)
		{
			return Colour.A < 8 && Target.A < 8;
		}
		constexpr int32 Tolerance = 36;
		return FMath::Abs(Colour.R - Target.R) <= Tolerance && FMath::Abs(Colour.G - Target.G) <= Tolerance
			&& FMath::Abs(Colour.B - Target.B) <= Tolerance && FMath::Abs(Colour.A - Target.A) <= Tolerance;
	};
	TBitArray<> Seen(false, Tris.Num());
	TArray<int32> Open = {Hit.Triangle};
	Seen[Hit.Triangle] = true;
	TArray<int32> Taken;
	while (!Open.IsEmpty())
	{
		const int32 Index = Open.Pop(EAllowShrinking::No);
		const FTri& Tri = Tris[Index];
		if (!StrokeLayers.IsValidIndex(Tri.Layer) || !StrokeLayers[Tri.Layer])
		{
			continue;
		}
		if (Index != Hit.Triangle && !Matches(ColourAt(Tri.Layer, (Tri.UV[0] + Tri.UV[1] + Tri.UV[2]) / 3.0f)))
		{
			continue;
		}
		Taken.Add(Index);
		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			const int32 Next = Neighbours.IsValidIndex(Index) ? Neighbours[Index][Edge] : INDEX_NONE;
			if (Next != INDEX_NONE && !Seen[Next])
			{
				Seen[Next] = true;
				Open.Add(Next);
			}
		}
	}
	for (const int32 Index : Taken)
	{
		const FTri& Tri = Tris[Index];
		TArray<FColor>& Pixels = StrokeLayers[Tri.Layer]->GetPixels();
		RasteriseUV(Tri.UV[0] * Size, Tri.UV[1] * Size, Tri.UV[2] * Size, [&](const int32 X, const int32 Y, const FVector3f&)
		{
			// (Pixel by pixel too: a line through the piece stays.)
			if (Matches(Pixels[Y * Size + X]) || StrokeWeights.Contains((static_cast<int64>(Tri.Layer) << 32) | (Y * Size + X)))
			{
				Apply(Tri.Layer, X, Y, Brush.Opacity);
			}
		});
	}
	EndStroke();
}

void FChaosImpactPaintSurface::ClearAll(const TArray<UChaosImpactPaintLayer*>& Layers)
{
	FChaosImpactBrush Eraser;
	Eraser.bErase = true;
	BeginStroke(Layers, Eraser);
	for (int32 Layer = 0; Layer < Layers.Num(); ++Layer)
	{
		if (!Layers[Layer])
		{
			continue;
		}
		TArray<FColor>& Pixels = Layers[Layer]->GetPixels();
		for (int32 Tile = 0; Tile < TilesPerSide * TilesPerSide; ++Tile)
		{
			const int32 X0 = (Tile % TilesPerSide) * TileSize;
			const int32 Y0 = (Tile / TilesPerSide) * TileSize;
			bool bAny = false;
			for (int32 Row = 0; Row < TileSize && !bAny; ++Row)
			{
				for (int32 Col = 0; Col < TileSize; ++Col)
				{
					if (Pixels[(Y0 + Row) * Size + X0 + Col].A != 0)
					{
						bAny = true;
						break;
					}
				}
			}
			if (!bAny)
			{
				continue;
			}
			Touch(Layer, Tile);
			for (int32 Row = 0; Row < TileSize; ++Row)
			{
				for (int32 Col = 0; Col < TileSize; ++Col)
				{
					Pixels[(Y0 + Row) * Size + X0 + Col] = FColor(0, 0, 0, 0);
				}
			}
			Layers[Layer]->MarkDirty(FIntRect(X0, Y0, X0 + TileSize, Y0 + TileSize));
		}
		Layers[Layer]->Flush();
	}
	FinishStep();
	bStroking = false;
}

bool FChaosImpactPaintSurface::PickColour(const TArray<UChaosImpactPaintLayer*>& Layers, const FHit& Hit, FColor& OutColour) const
{
	if (!Layers.IsValidIndex(Hit.Layer) || !Layers[Hit.Layer])
	{
		return false;
	}
	const int32 X = FMath::Clamp(FMath::FloorToInt(Hit.UV.X * Size), 0, Size - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(Hit.UV.Y * Size), 0, Size - 1);
	const FColor Pixel = Layers[Hit.Layer]->GetPixels()[Y * Size + X];
	if (Pixel.A < 24)
	{
		return false;
	}
	OutColour = FColor(Pixel.R, Pixel.G, Pixel.B, 255);
	return true;
}

void FChaosImpactPaintSurface::PutTile(UChaosImpactPaintLayer* Layer, const int32 Tile, const TArray<FColor>& Source)
{
	if (!Layer || Source.Num() != TileSize * TileSize)
	{
		return;
	}
	TArray<FColor>& Pixels = Layer->GetPixels();
	const int32 X0 = (Tile % TilesPerSide) * TileSize;
	const int32 Y0 = (Tile / TilesPerSide) * TileSize;
	for (int32 Row = 0; Row < TileSize; ++Row)
	{
		FMemory::Memcpy(&Pixels[(Y0 + Row) * Size + X0], &Source[Row * TileSize], TileSize * sizeof(FColor));
	}
	Layer->MarkDirty(FIntRect(X0, Y0, X0 + TileSize, Y0 + TileSize));
}

void FChaosImpactPaintSurface::UndoStep(const TArray<UChaosImpactPaintLayer*>& Layers)
{
	if (bStroking)
	{
		EndStroke();
	}
	if (Undo.IsEmpty())
	{
		return;
	}
	FStep Step = Undo.Pop();
	for (const FTileEdit& Edit : Step.Tiles)
	{
		PutTile(Layers.IsValidIndex(Edit.Layer) ? Layers[Edit.Layer] : nullptr, Edit.Tile, Edit.Before);
	}
	for (UChaosImpactPaintLayer* Layer : Layers)
	{
		if (Layer)
		{
			Layer->Flush();
		}
	}
	Redo.Add(MoveTemp(Step));
}

void FChaosImpactPaintSurface::RedoStep(const TArray<UChaosImpactPaintLayer*>& Layers)
{
	if (bStroking || Redo.IsEmpty())
	{
		return;
	}
	FStep Step = Redo.Pop();
	for (const FTileEdit& Edit : Step.Tiles)
	{
		PutTile(Layers.IsValidIndex(Edit.Layer) ? Layers[Edit.Layer] : nullptr, Edit.Tile, Edit.After);
	}
	for (UChaosImpactPaintLayer* Layer : Layers)
	{
		if (Layer)
		{
			Layer->Flush();
		}
	}
	Undo.Add(MoveTemp(Step));
}

void FChaosImpactPaintSurface::ForgetHistory()
{
	Undo.Reset();
	Redo.Reset();
	StrokeStep = FStep();
	StrokeTiles.Reset();
	bStroking = false;
}
