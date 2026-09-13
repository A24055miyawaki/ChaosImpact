import unreal

MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Could not load {MAP_PATH}")

actors = unreal.EditorLevelLibrary.get_all_level_actors()
actors.sort(key=lambda actor: actor.get_actor_label())
unreal.log(f"CI_MAP_INSPECT actor_count={len(actors)}")
for actor in actors:
    location = actor.get_actor_location()
    scale = actor.get_actor_scale3d()
    unreal.log(
        "CI_MAP_ACTOR "
        f"name={actor.get_name()} "
        f"label={actor.get_actor_label()} "
        f"class={actor.get_class().get_name()} "
        f"location=({location.x:.1f},{location.y:.1f},{location.z:.1f}) "
        f"scale=({scale.x:.2f},{scale.y:.2f},{scale.z:.2f})"
    )

