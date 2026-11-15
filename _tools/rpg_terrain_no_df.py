"""
Turns off the mesh distance field of the RPG's terrain meshes (no Lumen in this game: it only filled the package).
  UnrealEditor-Cmd.exe C:/ChaosImpact/ChaosImpact.uproject -run=pythonscript -script=C:/ChaosImpact/_tools/rpg_terrain_no_df.py
    -EnablePlugins=PythonScriptPlugin -unattended -nosplash -nullrhi
"""
import unreal

FOLDER = "/Game/ChaosImpact/RPGPrototype/Adventure/Terrain"
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
changed = 0
paths = unreal.EditorAssetLibrary.list_assets(FOLDER, recursive=True, include_folder=False)
for path in paths:
    asset = unreal.load_asset(path)
    if not isinstance(asset, unreal.StaticMesh):
        continue
    settings = meshes.get_lod_build_settings(asset, 0)
    before = settings.get_editor_property("distance_field_resolution_scale")
    if before > 0.0:
        settings.set_editor_property("distance_field_resolution_scale", 0.0)
        meshes.set_lod_build_settings(asset, 0, settings)
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
        changed += 1
unreal.log(f"RPGTERRAINDF {changed} of {len(paths)} meshes changed")
