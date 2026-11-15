import unreal
path = "/Game/ChaosImpact/RPGPrototype/Adventure/Terrain/SM_RPGTerrain_13_05"
sm = unreal.load_asset(path)
lib = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
out = []
out.append("lods %d" % lib.get_lod_count(sm))
for lod in range(lib.get_lod_count(sm)):
    out.append("lod%d tris %d verts %d" % (lod, lib.get_number_verts(sm, lod) if hasattr(lib, "get_number_verts") else -1, 0))
for prop in ["nanite_settings", "allow_cpu_access", "light_map_resolution", "complex_collision_mesh", "lod_group"]:
    try:
        out.append("%s = %s" % (prop, sm.get_editor_property(prop)))
    except Exception as e:
        out.append("%s ? %s" % (prop, e))
try:
    ns = sm.get_editor_property("nanite_settings")
    out.append("nanite enabled = %s" % ns.get_editor_property("enabled"))
except Exception as e:
    out.append("nanite ? %s" % e)
bs = lib.get_lod_build_settings(sm, 0)
for prop in ["distance_field_resolution_scale", "generate_lightmap_u_vs", "use_full_precision_u_vs", "use_high_precision_tangent_basis", "build_reversed_index_buffer"]:
    try:
        out.append("build.%s = %s" % (prop, bs.get_editor_property(prop)))
    except Exception as e:
        out.append("build.%s ? %s" % (prop, e))
body = sm.get_editor_property("body_setup")
try:
    out.append("collision trace = %s" % body.get_editor_property("collision_trace_flag"))
except Exception as e:
    out.append("body ? %s" % e)
unreal.log("PROBETERRAIN " + " | ".join(out))
