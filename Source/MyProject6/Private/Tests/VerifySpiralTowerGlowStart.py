"""Load the repaired tower assets in a fresh process and verify their saved settings."""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name('VerifySpiralTowerFlow.py')),
               init_globals={'REPORT_FOLDER': 'GlowStart'})
