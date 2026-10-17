# test: required
#
# location is a top-level module. No fix is asked for:
# that needs the permission prompt and a view of the sky.
import location

print("methods:", all(hasattr(location, n) for n in ("start", "stop", "read", "last", "help")))
print("help:", location.help().startswith("module: location"))
