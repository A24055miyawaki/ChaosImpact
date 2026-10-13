"""UE 5.7 editor commandlet: import the two fixed-colour playable characters.
Run with -run=pythonscript -EnablePlugins=PythonScriptPlugin -script=<this file>.
Only writes /Game/ChaosImpact/Fairy and /Game/ChaosImpact/Wood.
"""
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")


def task(source, folder, name, options=None, factory=None):
    t = unreal.AssetImportTask()
    t.filename = str(source)
    t.destination_path = folder
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    if options is not None: t.options = options
    if factory is not None: t.factory = factory
    TOOLS.import_asset_tasks([t])
    asset = unreal.load_asset(folder + "/" + name)
    if not asset: raise RuntimeError(f"Import failed: {source}; paths={t.imported_object_paths}")
    return asset


def import_character(prefix, mesh_file, scale):
    folder = "/Game/ChaosImpact/" + prefix
    source = PROJECT / "SourceAssets" / "Characters" / prefix
    texture = task(source / (prefix + ".png"), folder, "T_" + prefix, factory=unreal.TextureFactory())
    texture.set_editor_property("srgb", True)
    material_path = folder + "/M_CI_" + prefix
    mat = unreal.load_asset(material_path)
    if not mat:
        mat = TOOLS.create_asset("M_CI_" + prefix, folder, unreal.Material, unreal.MaterialFactoryNew())
        tex = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -380, 0)
        tex.texture = texture
        unreal.MaterialEditingLibrary.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 180)
        rough.r = 0.78
        unreal.MaterialEditingLibrary.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        # Opaque fixed texture. Never expose BodyColor: team colours remain on HUD only.
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("used_with_skeletal_mesh", True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_as_skeletal = True
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.import_mesh = True
    options.import_animations = False
    options.import_materials = False
    options.import_textures = False
    options.create_physics_asset = False
    data = options.skeletal_mesh_import_data
    data.import_uniform_scale = scale
    data.convert_scene = True
    data.force_front_x_axis = False
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    mesh = task(source / mesh_file, folder, "SK_" + prefix, options, unreal.FbxFactory())
    if not isinstance(mesh, unreal.SkeletalMesh): raise RuntimeError("Expected a SkeletalMesh")
    leaf = None
    if prefix == "Wood":
        # The source FBX's first material (ki) is an untextured green leaf; KI is the textured bark.
        leaf = unreal.load_asset(folder + "/M_CI_Wood_Leaf")
        if not leaf:
            leaf = TOOLS.create_asset("M_CI_Wood_Leaf", folder, unreal.Material, unreal.MaterialFactoryNew())
            colour = unreal.MaterialEditingLibrary.create_material_expression(leaf, unreal.MaterialExpressionConstant3Vector, -300, 0)
            colour.constant = unreal.LinearColor(0.2192, 1.0, 0.0196, 1.0)
            unreal.MaterialEditingLibrary.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
            leaf.set_editor_property("two_sided", True)
            leaf.set_editor_property("used_with_skeletal_mesh", True)
            unreal.MaterialEditingLibrary.recompile_material(leaf)
        unreal.EditorAssetLibrary.save_loaded_asset(leaf, only_if_is_dirty=False)
    materials = mesh.get_editor_property("materials")
    for index in range(len(materials)):
        slot = materials[index]
        slot.material_interface = leaf if leaf is not None and index == 0 else mat
        materials[index] = slot  # UE struct arrays return copies, not editable element references.
    mesh.set_editor_property("materials", materials)
    mesh.set_editor_property("never_stream", True)
    for asset in (texture, mat, mesh):
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=True)
    bounds = mesh.get_bounds()
    unreal.log(f"CHARACTER_IMPORT {prefix}: {mesh.get_path_name()} slots={len(materials)} bounds={bounds}")


if not globals().get("CHARACTER_IMPORT_LIBRARY_ONLY", False):
    import_character("Fairy", "Fairy_game.fbx", 1.0)
    import_character("Wood", "Wood_original.fbx", 0.70)
    unreal.log("CHARACTER_IMPORT SUCCESS Fairy + Wood")
