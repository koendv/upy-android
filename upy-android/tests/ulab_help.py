# test: required
#
# ulab.help() and the statements in it that a script can check.
import ulab
from ulab import numpy as np
from ulab import scipy, utils


def raises(exception, call):
    try:
        call()
    except exception:
        return True
    return False


def importable(statement):
    try:
        exec(statement)
    except ImportError:
        return False
    return True


text = ulab.help()
print("help:", text.startswith("module: ulab"))
print("help names the documentation:", "https://micropython-ulab.readthedocs.io/" in text and "<release>" in text)
print("help has no version number:", ulab.__version__.split("-")[0] not in text)
release, rest = ulab.__version__.split("-", 1)
print("version looks like <release>-(<dims>)D:", release.count(".") == 2 and rest.startswith("(") and "D" in rest)
print("version tells the dimensions:", np.zeros((2, 2, 2, 2)).shape == (2, 2, 2, 2) and rest.startswith("(4)"))
print("more dimensions raise TypeError:", raises(TypeError, lambda: np.zeros((2, 2, 2, 2, 2))))
print("complex numbers:", rest.endswith("-c") and np.array([1, 2], dtype=np.complex).dtype == np.complex)
print("numpy submodules:", all(hasattr(np, n) for n in ("fft", "linalg", "random")))
print("scipy submodules:", all(hasattr(scipy, n) for n in ("integrate", "linalg", "optimize", "signal", "special")))
print("utils:", hasattr(utils, "spectrogram"))
print("import numpy does not work:", not importable("import numpy"))
print("import ulab.numpy does not work:", not importable("import ulab.numpy"))
