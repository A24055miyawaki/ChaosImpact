#if WITH_DEV_AUTOMATION_TESTS

#include "ChaosImpactSfx.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaosImpactSfxAssetsTest, "ChaosImpact.Audio.SfxAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Every sound effect in the table has all its variants in the project; the loops loop, the others do not. */
bool FChaosImpactSfxAssetsTest::RunTest(const FString& Parameters)
{
	int32 Found = 0;
	for (int32 Index = 0; Index < static_cast<int32>(EChaosImpactSfx::Count); ++Index)
	{
		const EChaosImpactSfx Sound = static_cast<EChaosImpactSfx>(Index);
		const FString Name = ChaosImpactSfx::GetName(Sound);
		TestTrue(Name + TEXT(" has a variant"), ChaosImpactSfx::GetVariantCount(Sound) >= 1);
		for (int32 Variant = 0; Variant < ChaosImpactSfx::GetVariantCount(Sound); ++Variant)
		{
			const FString Path = ChaosImpactSfx::GetAssetPath(Sound, Variant);
			const USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Path);
			if (!TestNotNull(Path + TEXT(" exists"), Wave))
			{
				continue;
			}
			++Found;
			TestTrue(Path + TEXT(" has length"), Wave->Duration > 0.02f);
			TestEqual(Path + TEXT(" loops only if it is a loop"), static_cast<bool>(Wave->bLooping), Name.Contains(TEXT("_Loop")));
		}
	}
	AddInfo(FString::Printf(TEXT("%d sound effect waves"), Found));
	return true;
}

#endif
