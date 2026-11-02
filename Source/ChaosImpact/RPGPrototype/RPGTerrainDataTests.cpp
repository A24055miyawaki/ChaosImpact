#if WITH_DEV_AUTOMATION_TESTS
#include "RPGWorldLayout.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"

// The terrain carries what its material paints: snow (second UVs), ash and dry grass (third), marsh (fourth).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTerrainDataTest, "ChaosImpact.RPGPrototype.TerrainData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRPGTerrainDataTest::RunTest(const FString&)
{
	const RPGWorld::FSample Crater = RPGWorld::Sample(2600, 33500), Marsh = RPGWorld::Sample(-3000, 28500), Camp = RPGWorld::Sample(21200, 28600), Colony = RPGWorld::Sample(0, 0);
	AddInfo(FString::Printf(TEXT("crater ash %.2f, marsh %.2f, camp snow %.2f, colony ash %.2f snow %.2f"), Crater.Ash, Marsh.Marsh, Camp.Snow, Colony.Ash, Colony.Snow));
	TestTrue(TEXT("Ash round the volcano"), Crater.Ash > 0.9f);
	TestTrue(TEXT("The marsh is marsh"), Marsh.Marsh > 0.9f);
	TestTrue(TEXT("Snow at the snowfield's camp"), Camp.Snow > 0.9f);
	TestTrue(TEXT("Nothing of the kind at the colony"), Colony.Ash < 0.01f && Colony.Snow < 0.01f && Colony.Marsh < 0.01f);
	// The tile under the crater: built with the regional UVs.
	const int32 IX = FMath::FloorToInt((2600 - RPGWorld::MinX) / 4000), IY = FMath::FloorToInt((33500 - RPGWorld::MinY) / 4000);
	const FString Name = FString::Printf(TEXT("SM_RPGTerrain_%02d_%02d"), IX, IY);
	const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/ChaosImpact/RPGPrototype/Adventure/Terrain/%s.%s"), *Name, *Name));
	if (!TestNotNull(TEXT("The crater's terrain tile"), Mesh) || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) { return true; }
	const int32 TexCoords = Mesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	AddInfo(FString::Printf(TEXT("%s has %d UV channels"), *Name, TexCoords));
	TestTrue(TEXT("The terrain has its four UV channels"), TexCoords >= 4);
	// And the values are there (the most ash on any vertex of this tile).
	const FStaticMeshVertexBuffer& UVs = Mesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;
	float MostAsh = 0, MostSnow = 0;
	if (TexCoords >= 4 && UVs.GetTexCoordData())
	{
		for (uint32 Vertex = 0; Vertex < UVs.GetNumVertices(); Vertex += 7) { MostAsh = FMath::Max(MostAsh, UVs.GetVertexUV(Vertex, 2).X); MostSnow = FMath::Max(MostSnow, UVs.GetVertexUV(Vertex, 1).Y); }
		AddInfo(FString::Printf(TEXT("%s most ash %.2f, most snow %.2f"), *Name, MostAsh, MostSnow));
		TestTrue(TEXT("The crater's tile carries its ash"), MostAsh > 0.9f);
	}
	else { AddInfo(TEXT("UV data not kept on the CPU here")); }
	return true;
}
#endif
