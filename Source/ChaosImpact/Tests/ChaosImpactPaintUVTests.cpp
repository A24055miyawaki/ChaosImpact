#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactCharacterRoster.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"

namespace
{
	struct FUVReport
	{
		int32 Triangles = 0;
		int32 Sections = 0;
		float Covered = 0.0f;
		float Overlap = 0.0f;
		float Outside = 0.0f;
	};

	/** UV0 at 256 x 256: how much of the square is used, how much of that twice or more, and how much falls outside it. */
	void Rasterise(const TArray<FVector2f>& UVs, const TArray<uint32>& Indices, TArray<uint8>& Hits, FUVReport& Out)
	{
		constexpr int32 N = 256;
		int32 OutsideTris = 0;
		for (int32 Tri = 0; Tri + 2 < Indices.Num(); Tri += 3)
		{
			const FVector2f A = UVs[Indices[Tri]] * N, B = UVs[Indices[Tri + 1]] * N, C = UVs[Indices[Tri + 2]] * N;
			const auto IsOut = [](const FVector2f& P) { return P.X < -0.5f || P.Y < -0.5f || P.X > N + 0.5f || P.Y > N + 0.5f; };
			if (IsOut(A) || IsOut(B) || IsOut(C))
			{
				++OutsideTris;
			}
			const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, N - 1);
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, N - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, N - 1);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, N - 1);
			const float Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (FMath::Abs(Area) < 1e-6f)
			{
				continue;
			}
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
						uint8& H = Hits[Y * N + X];
						H = static_cast<uint8>(FMath::Min(H + 1, 255));
					}
				}
			}
		}
		Out.Outside += Indices.Num() > 0 ? static_cast<float>(OutsideTris) / (Indices.Num() / 3) : 0.0f;
	}

	void Finish(const TArray<uint8>& Hits, FUVReport& Out)
	{
		int32 Used = 0;
		int32 Twice = 0;
		for (const uint8 H : Hits)
		{
			Used += H > 0 ? 1 : 0;
			Twice += H > 1 ? 1 : 0;
		}
		Out.Covered = Used / static_cast<float>(Hits.Num());
		Out.Overlap = Used > 0 ? Twice / static_cast<float>(Used) : 0.0f;
	}

	bool ReadSkeletal(USkeletalMesh* Mesh, TArray<uint8>& Hits, FUVReport& Out)
	{
		FSkeletalMeshRenderData* Data = Mesh ? Mesh->GetResourceForRendering() : nullptr;
		if (!Data || Data->LODRenderData.IsEmpty())
		{
			return false;
		}
		const FSkeletalMeshLODRenderData& LOD = Data->LODRenderData[0];
		const int32 Verts = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
		TArray<FVector2f> UVs;
		UVs.SetNum(Verts);
		for (int32 V = 0; V < Verts; ++V)
		{
			UVs[V] = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V, 0);
		}
		TArray<uint32> Indices;
		LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
		Out.Triangles += Indices.Num() / 3;
		Out.Sections += LOD.RenderSections.Num();
		Rasterise(UVs, Indices, Hits, Out);
		return Indices.Num() > 0;
	}

	bool ReadStatic(UStaticMesh* Mesh, TArray<uint8>& Hits, FUVReport& Out)
	{
		FStaticMeshRenderData* Data = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!Data || Data->LODResources.IsEmpty())
		{
			return false;
		}
		const FStaticMeshLODResources& LOD = Data->LODResources[0];
		const int32 Verts = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
		TArray<FVector2f> UVs;
		UVs.SetNum(Verts);
		for (int32 V = 0; V < Verts; ++V)
		{
			UVs[V] = LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V, 0);
		}
		TArray<uint32> Indices;
		LOD.IndexBuffer.GetCopy(Indices);
		Out.Triangles += Indices.Num() / 3;
		Out.Sections += LOD.Sections.Num();
		Rasterise(UVs, Indices, Hits, Out);
		return Indices.Num() > 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactPaintUVReport, "ChaosImpact.Paint.UVReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FChaosImpactPaintUVReport::RunTest(const FString& Parameters)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChaosImpactRoster::Characters); ++Index)
	{
		const FChaosImpactCharacterInfo& Info = ChaosImpactRoster::Characters[Index];
		const auto Path = [&Info](const TCHAR* Kind, const TCHAR* Part)
		{
			const FString Name = *Part == 0 ? FString::Printf(TEXT("%s_%s"), Kind, Info.AssetPrefix) : FString::Printf(TEXT("%s_%s_%s"), Kind, Info.AssetPrefix, Part);
			return FString::Printf(TEXT("%s/%s.%s"), Info.AssetFolder, *Name, *Name);
		};
		TArray<uint8> Hits;
		Hits.SetNumZeroed(256 * 256);
		FUVReport Report;
		TArray<FString> Pieces;
		if (USkeletalMesh* Whole = LoadObject<USkeletalMesh>(nullptr, *Path(TEXT("SK"), TEXT(""))); Whole && ReadSkeletal(Whole, Hits, Report))
		{
			Pieces.Add(TEXT("SK"));
		}
		if (UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, *Path(TEXT("SM"), TEXT("Body"))); Body && ReadStatic(Body, Hits, Report))
		{
			Pieces.Add(TEXT("SM_Body"));
		}
		for (const TCHAR* Limb : {TEXT("ArmL"), TEXT("ArmR"), TEXT("LegL"), TEXT("LegR")})
		{
			if (USkeletalMesh* Part = LoadObject<USkeletalMesh>(nullptr, *Path(TEXT("SK"), Limb)); Part && ReadSkeletal(Part, Hits, Report))
			{
				Pieces.Add(Limb);
			}
		}
		Finish(Hits, Report);
		UE_LOG(LogTemp, Display, TEXT("PAINTUV %-10s [%s] tris=%d sections=%d covered=%.0f%% overlap=%.0f%% outside=%.0f%%"), Info.AssetPrefix,
			*FString::Join(Pieces, TEXT("+")), Report.Triangles, Report.Sections, Report.Covered * 100.0f, Report.Overlap * 100.0f, Report.Outside * 100.0f);
	}
	return true;
}

#endif
