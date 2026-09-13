import unreal

MAP_PATH = "/Game/ThirdPerson/Lvl_ThirdPerson"
LEGACY_WALL_LABELS = {
    "SM_Cube2", "SM_Cube3", "SM_Cube4", "SM_Cube5",
    "SM_Cube17", "SM_Cube18", "SM_Cube19", "SM_Cube20",
}

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if not world:
    raise RuntimeError(f"Could not load {MAP_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = list(actor_subsystem.get_all_level_actors())

removed_legacy = 0
removed_previous_training = 0
for actor in actors:
    label = actor.get_actor_label()
    class_name = actor.get_class().get_name()
    if label in LEGACY_WALL_LABELS and class_name == "StaticMeshActor":
        actor_subsystem.destroy_actor(actor)
        removed_legacy += 1
    elif label.startswith("CI_Training_"):
        actor_subsystem.destroy_actor(actor)
        removed_previous_training += 1


def spawn(actor_class, label, location, folder):
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*location),
        unreal.Rotator(0.0, 0.0, 0.0),
        False,
    )
    if not actor:
        raise RuntimeError(f"Failed to spawn {label}")
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


arena = spawn(
    unreal.ChaosImpactTrainingArena,
    "CI_Training_Arena",
    (0.0, 0.0, 2.0),
    "Chaos Impact/Training Stage",
)

spawner_locations = [
    (420.0, 0.0, 4.0),
    (-620.0, 680.0, 4.0),
    (-620.0, -680.0, 4.0),
    (2250.0, 1480.0, 124.0),
    (1900.0, -1550.0, 4.0),
    (-1900.0, 1500.0, 4.0),
    (-2750.0, -1100.0, 4.0),
]
for index, location in enumerate(spawner_locations, start=1):
    spawn(
        unreal.ChaosImpactBallSpawner,
        f"CI_Training_BallSpawn_{index:02d}",
        location,
        "Chaos Impact/Training Stage/Ball Spawns",
    )

target_setups = [
    ((3000.0, 0.0, 4.0), unreal.ChaosImpactTargetMotion.STATIONARY, 0.0, 0.30, 0.00),
    ((2250.0, 1660.0, 124.0), unreal.ChaosImpactTargetMotion.STATIONARY, 0.0, 0.30, 0.00),
    ((-2450.0, -1660.0, 84.0), unreal.ChaosImpactTargetMotion.STATIONARY, 0.0, 0.30, 0.00),
    ((2200.0, -1900.0, 4.0), unreal.ChaosImpactTargetMotion.SIDE_TO_SIDE, 250.0, 0.26, 0.00),
    ((650.0, 2050.0, 4.0), unreal.ChaosImpactTargetMotion.SIDE_TO_SIDE, 260.0, 0.31, 0.30),
    ((-1250.0, 1800.0, 4.0), unreal.ChaosImpactTargetMotion.SIDE_TO_SIDE, 220.0, 0.36, 0.58),
    ((-2250.0, 1050.0, 4.0), unreal.ChaosImpactTargetMotion.FORWARD_BACK, 240.0, 0.30, 0.15),
    ((1450.0, 900.0, 4.0), unreal.ChaosImpactTargetMotion.FORWARD_BACK, 190.0, 0.40, 0.72),
]
for index, (location, motion, distance, speed, phase) in enumerate(target_setups, start=1):
    target = spawn(
        unreal.ChaosImpactTrainingTarget,
        f"CI_Training_Target_{index:02d}",
        location,
        "Chaos Impact/Training Stage/Targets",
    )
    target.set_editor_property("motion_mode", motion)
    target.set_editor_property("travel_distance", distance)
    target.set_editor_property("cycles_per_second", speed)
    # Motion phase is runtime state, so offset equivalent targets through their
    # initial placement instead of relying on a hidden editor property.

unreal.EditorLevelLibrary.save_current_level()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
unreal.log(
    "CI_TRAINING_MAP_DONE "
    f"removed_legacy={removed_legacy} "
    f"removed_previous_training={removed_previous_training} "
    f"arena=1 spawners={len(spawner_locations)} targets={len(target_setups)}"
)

