"""Apply the railgun energy presentation and persistent Babylon opening sizes."""
from pathlib import Path
import runpy
for script in ['SetupRailgunEnergy.py','SetupTowerOpenings.py']:
    runpy.run_path(str(Path(__file__).with_name(script)),run_name='__main__')
