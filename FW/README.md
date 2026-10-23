
# Wavelight-firmware

## Structure

| Path                                  | Purpose                                                                                                                             |
|---------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------|
| _build                                | The [CMake build tree](https://cmake.org/cmake/help/latest/manual/cmake.1.html#introduction-to-cmake-buildsystems), can be deleted. |
| cmake                                 | Generated [CMake](https://cmake.org/) files. May be deleted if user.cmake has not been added                                        |
| .vscode                               | See [VSCode](https://code.visualstudio.com/docs/getstarted/settings)                                                                |
| .vscode/settings.json                 | Workspace specific settings                                                                                                         |
| .vscode/Wavelight-firmware.mplab.json | The MPLAB project file, should not be deleted                                                                                       |
| out                                   | Final build artifacts                                                                                                               |

## Sensor range

The TMF8801 only sees ~64 cm without its RAM patch, which does not fit in the ATtiny416. See [PATCHING.md](PATCHING.md) for the ATtiny1616 upgrade.
