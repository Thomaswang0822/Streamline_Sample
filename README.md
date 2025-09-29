# DLSS Offline Runner

Based on StreamlineSample

## Project Setup (Adapted from original README)

1. Ensure you have CMake 3.20+ and the vulkan sdk (https://vulkan.lunarg.com) on your system.
2. Ensure that a VK-compatible dxc.exe is available in your system `PATH`.  The best way to do this is to install a recent (1.2.198.1 or newer) Vulkan SDK from https://www.vulkan.org/ and ensure that its `bin` directory is in the build machine's system `PATH`.
3. Clone this repository, then run: `git submodule update --init --recursive`
4. Go to Streamline repo and download the [SDK Release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.9.0). Unzip the folder and put everything into `streamline/` folder in project root. i.e. `streamline/package.bat` should exist.
5. (TEMPORARY) Switch to **capture-LDR** branch.
6. Run `make.bat` and fix any error in the CMake configure.
7. Open the solution in `_build/`. Build Solution.

## Cmdline Args

Unlike our AMD FSR Offline Runner, this app doesn't have loading config from json feature. The only way to specify runtime options is through cmdline args.

It works almost the same. Below is copied from FSR Offline Runner README.

- EnableHack: a global switch, default false. If false, the app will run in its original behavior, rendering Sponza Palace.
- Identifier: a string that helps you identify this run, default "UNDEFINED". Usually set to scene name.
- RenderResolution: a int that controls the render resolution in K, default 1. Only accepted values are 1, 2, and 4.
- ParseJitter: whether to parse and use the jitter data from input filenames, default false. Currently we only have it in 1K inputs, so it will be forced to false it render resolution is not 1K.
- HackPaths: **a single folder relative path** to the input frame capture folder, default *"../media/TEST_SCENE/NPP_JI"*. The path to the encoded MVs and Depths will be constructed automatically by replacing "NPP_JI" to "MVD_JI".
- StoreOutput: whether to store output (screenshots), default false.
- OutputMaxCount: number of frames to take screenshots, default 0. When StoreOutput is true and OutputMaxCount is missing or bigger than number of input frames, it will default to capture all frames.
- OutputPath: **a single folder relative path** to the output screenshots folder, like *"../media/TEST_SCENE/outputs"*

```shell
# cd to <Project Root>/_bin

./StreamlineSample.exe  -EnableHack \
                        -Identifier "Cmdline_TEST" \
                        -RenderResolution 1 \
                        -ParseJitter \
                        -HackPaths "../media/TEST_SCENE/NPP_JI" \
                        -StoreOutput \
                        -OutputMaxCount 5 \
                        -OutputPath "../media/TEST_SCENE/outputs"
```

Also, it would be very convenient to set them up in the VS Debugger such that each test run is a one-click. Put this single-line arg list to StreamlineSample  Property Pages, in Configuration Properties -> Debugging -> Commandline Arguments. Adjust if needed. Make sure the `OutputPath` (e.g. `media/TEST_SCENE/screenshots`) exists.

```shell
-EnableHack -Identifier FG_TEST -RenderResolution 1 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -OutputMaxCount 10 -OutputPath "../media/TEST_SCENE/screenshots"
```

## Tips

### Turn Off Fullscreen mode

Since we uses a frontend approach to capture screenshot, we have to turn on fullscreen mode to get 3840 x 2160 output. But if you stop on a break point or the app encounters an error, fullscreen will cause machine to halt (you have to Ctrl + Alt + Del and Sign Out and log in again), it's much easier to turn it off when doing test runs.

Go to `src\main.cpp` line 267 to turn it off.

### Temporarily Disable Hack or StoreOutput

If you want to disable hack or disable export output, you can go to `src\StreamlineSample.cpp` line 185.

The former is useful if you want to play with the GUI control panel, because we turned it off when hack is on (otherwise screen capture will include the GUI), or when you simply want to see how the original scene is rendered.

The latter is useful if you want to fix issues unrelated to export. We levarage `sleep_for()` to correctly capture screenshot, thus turning it off can save your time. Also, when export is off, the app keeps running instead of closing after `OutputMaxCount` frames.
