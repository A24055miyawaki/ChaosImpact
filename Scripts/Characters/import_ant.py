"""Import Ant Kids only; reuse the established fixed-texture character pipeline."""
from pathlib import Path
import runpy
import unreal

library = runpy.run_path(str(Path(__file__).with_name("import_fairy_wood.py")),
                        init_globals={"CHARACTER_IMPORT_LIBRARY_ONLY": True})
library["import_character"]("Ant", "Ant_original.fbx", 0.68)
unreal.log("CHARACTER_IMPORT SUCCESS Ant Kids")
