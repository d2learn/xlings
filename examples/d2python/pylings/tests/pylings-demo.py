import sys, os
# Add the project root to PYTHONPATH
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# ----------------------------

import exercises.pylings as pylings

print("hello pylings: 1 + 2 = ", pylings.add(1, 2)