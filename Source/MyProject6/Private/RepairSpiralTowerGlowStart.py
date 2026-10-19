"""Rebuild saved glow coordinates without changing the chosen animation settings."""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name('UpgradeSpiralTowerFlow.py')),
               init_globals={'REPAIR_START_ONLY': True})
