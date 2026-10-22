#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RPGWorldBuilder.generated.h"

class UMaterialInterface;

/**
 * Editor only: builds the RPG adventure map from RPGWorldLayout (Scripts/RPGPrototype/build_rpg_world.py runs it).
 * The results are ordinary assets and level actors, so everything can be moved and edited by hand afterwards; running
 * it again rebuilds the terrain and scenery (gameplay actors keep their settings and are only moved).
 */
UCLASS()
class URPGWorldBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Low-poly kit meshes (blob, rock, cone, cylinder, grass tuft, gem) in Adventure/Meshes. Returns how many were saved. */
	UFUNCTION(BlueprintCallable,Category="RPG World")
	static int32 BuildKitMeshes();
	/** The terrain as 40 m tiles (SM_RPGTerrain_XX_YY) in Adventure/Terrain, with Material. Returns how many were saved. */
	UFUNCTION(BlueprintCallable,Category="RPG World")
	static int32 BuildTerrain(UMaterialInterface* Material,int32 FirstTile=-1,int32 LastTile=-1);
	UFUNCTION(BlueprintPure,Category="RPG World")
	static int32 GetTerrainTileCount();
	/**
	 * Replaces the open level's terrain and scenery with the new map's, and places its gameplay actors (encounters,
	 * chests, checkpoints, quest points, the merchant, the boss, the start). Returns the number of actors made.
	 */
	UFUNCTION(BlueprintCallable,Category="RPG World",meta=(WorldContext="WorldContextObject"))
	static int32 PopulateLevel(UObject* WorldContextObject);
	UFUNCTION(BlueprintPure,Category="RPG World")
	static float GetGroundHeight(float X,float Y);
};
