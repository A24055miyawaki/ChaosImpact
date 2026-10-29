#if WITH_DEV_AUTOMATION_TESTS
#include "ChaosImpactCharacter.h"
#include "ChaosImpactCharacterRoster.h"
#include "ChaosImpactCharacterSelect.h"
#include "ChaosImpactPuppetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCapCharacterAssetsTest, "ChaosImpact.Characters.Cap.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCapCharacterAssetsTest::RunTest(const FString&)
{
	TestEqual(TEXT("New character's name is マグ・カープ"),
		FString(ChaosImpactRoster::Get(ChaosImpactRoster::CapIndex).Name), FString(TEXT("マグ・カープ")));
	TestTrue(TEXT("Cap tile is selectable"), UChaosImpactCharacterSelect::IsTileUnlocked(ChaosImpactRoster::CapIndex));
	TestFalse(TEXT("Cap uses its custom multi-material look without colour variant step"),
		ChaosImpactRoster::Get(ChaosImpactRoster::CapIndex).bColourVariants);

	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/ChaosImpact/Cap/SK_Cap"));
	auto* MugMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/Cap/M_CI_Cap"));
	auto* CoffeeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/Cap/M_CI_Cap_Coffee"));
	if (!TestNotNull(TEXT("Cap skeletal mesh"), Mesh)
		|| !TestNotNull(TEXT("Cap mug material"), MugMat)
		|| !TestNotNull(TEXT("Cap coffee material"), CoffeeMat))
	{
		return false;
	}

	TestEqual(TEXT("SK_Cap has 5 material slots (Limb, White, Mug, Black, Coffee)"), Mesh->GetMaterials().Num(), 5);
	if (Mesh->GetMaterials().Num() >= 5)
	{
		TestTrue(TEXT("Slot 4 uses Coffee material"), Mesh->GetMaterials()[4].MaterialInterface == CoffeeMat);
	}

	// Verify 究極電子生命体 セル (Cell)
	TestEqual(TEXT("Cell character's name is 究極電子生命体 セル"),
		FString(ChaosImpactRoster::Get(ChaosImpactRoster::CellIndex).Name), FString(TEXT("究極電子生命体 セル")));
	TestTrue(TEXT("Cell tile is selectable"), UChaosImpactCharacterSelect::IsTileUnlocked(ChaosImpactRoster::CellIndex));
	auto* CellMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/ChaosImpact/Cell/SK_Cell"));
	auto* CellMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ChaosImpact/Cell/M_CI_Cell"));
	if (!TestNotNull(TEXT("Cell skeletal mesh"), CellMesh) || !TestNotNull(TEXT("Cell material"), CellMat))
	{
		return false;
	}

	// Verify UChaosImpactPuppetComponent initializes cleanly with both CapIndex and CellIndex
	UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (TestWorld)
	{
		AActor* DummyOwner = TestWorld->SpawnActor<AActor>();
		auto* SourceMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
		if (DummyOwner && SourceMesh)
		{
			auto* SourceComp = NewObject<USkeletalMeshComponent>(DummyOwner);
			SourceComp->SetSkeletalMeshAsset(SourceMesh);
			SourceComp->RegisterComponent();

			for (const int32 TestCharIdx : {ChaosImpactRoster::CapIndex, ChaosImpactRoster::CellIndex})
			{
				auto* Puppet = NewObject<UChaosImpactPuppetComponent>(DummyOwner);
				Puppet->SetupAttachment(SourceComp);
				Puppet->RegisterComponent();
				TestTrue(FString::Printf(TEXT("PuppetComponent initializes for index %d"), TestCharIdx),
					Puppet->Initialize(SourceComp, TestCharIdx));
				TestTrue(TEXT("PuppetComponent is ready"), Puppet->IsReady());
				Puppet->UpdatePose(true, 0.016f);
				FVector Palm;
				TestTrue(TEXT("PuppetComponent computes right palm location"), Puppet->GetPalmLocation(true, Palm));
			}
		}
		TestWorld->DestroyWorld(false);
	}

	for (const TCHAR* Bone : {
		TEXT("Character1_Hips"),
		TEXT("Character1_Spine"),
		TEXT("Character1_Spine1"),
		TEXT("Character1_Spine2"),
		TEXT("Character1_Neck"),
		TEXT("Character1_Head"),
		TEXT("Character1_LeftShoulder"),
		TEXT("Character1_RightShoulder"),
		TEXT("Character1_LeftArm"),
		TEXT("Character1_LeftForeArm"),
		TEXT("Character1_LeftHand"),
		TEXT("Character1_RightArm"),
		TEXT("Character1_RightForeArm"),
		TEXT("Character1_RightHand"),
		TEXT("Character1_LeftUpLeg"),
		TEXT("Character1_LeftLeg"),
		TEXT("Character1_LeftFoot"),
		TEXT("Character1_RightUpLeg"),
		TEXT("Character1_RightLeg"),
		TEXT("Character1_RightFoot")})
	{
		TestTrue(FString::Printf(TEXT("Rig has %s"), Bone), Mesh->GetRefSkeleton().FindBoneIndex(Bone) != INDEX_NONE);
	}

#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif
	return true;
}
#endif
