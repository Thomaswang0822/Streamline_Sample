# DLSS Offline Runner

Based on StreamlineSample

## Project Setup (Adapted from original README)

1. Ensure you have CMake 3.20+ and the vulkan sdk (run `vulkaninfo`) on your system.
2. Ensure that a VK-compatible dxc.exe is available in your system `PATH`.  The best way to do this is to install a recent (1.2.198.1 or newer) Vulkan SDK from [official site](https://vulkan.lunarg.com/sdk/home#windows) and ensure that its `bin` directory is in the build machine's system `PATH`.
3. Clone this repository, then run: `git submodule update --init --recursive`
4. Go to Streamline repo and download the [SDK Release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.9.0). Unzip the folder and put everything into `streamline/` folder in project root. i.e. `streamline/package.bat` should exist.
5. Switch to **capture-HDR** branch.
6. Run `make.bat` and fix any error in the CMake configure.
7. Open the solution in `_build/`.
8. Install Windows Implementation Library (wil). See section below for how to.
9. Build Solution.
10. Make sure you read and follow [this section](#true-hdr-capture-new-feature-and-issue) before starting any serious/actual run.

## Install WIL with NuGet

WIL is not implemented for typical VS installation (Desktip C++ Development). To confirm, open [StreamlineSample.cpp](src/StreamlineSample.cpp) and VS would complain `#include <wil/resource.h>` not found on line 71.

First, make sure you are currently in "StreamlineSample" project (the startup project) in the solution. Now open "Search" from the top menu bar, select "Feature Search", and select first option "Manage NuGet Packages". A NuGet tab will pop out. Double check that you have "NuGet Package Manager: StreamlineSample" on the top right. Switch to "Browse" and search for "Microsoft.Windows.ImplementationLibrary". Click install and follow the prompt. You will be notified success in the output window.

Finally, go back to [StreamlineSample.cpp](src/StreamlineSample.cpp). Now VS should be able to find wil. Then we can proceed to Build Solution.

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

If you want to disable hack or disable export output, you can go to `src\main.cpp` line 264.

The former is useful if you want to play with the GUI control panel, because we turned it off when hack is on (otherwise screen capture will include the GUI), or when you simply want to see how the original scene is rendered.

The latter is useful if you want to fix issues unrelated to export. We levarage `sleep_for()` to correctly capture screenshot, thus turning it off can save your time. Also, when export is off, the app keeps running instead of closing after `OutputMaxCount` frames.

## True HDR Capture: New Feature and Issue

This "capture-HDR" is branched out from "capture-LDR", with 2 squash merges of all of our attempts to capture the screen in true HDR format. In "capture-LDR" used Windows API `BitBlit()` which only supports LDR capture. Now, we leveraged `winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool` to capture the window content to a DX11 texture, which supports true HDR `RGBA16_FLOAT` format.

A even bigger improvement is frame consistency, i.e. how we ensure **frameN.exr** really captures frameN contents. Previously, we used the "sleep bubble" trick to give us enough time to take captures, by extending the `Present()` time for each frame from 1/60 sec (if 60FPS) to several seconds. For it's details, see the [confluence page on LDR capture](https://sh-code.mthreads.com/haoxuan.wang/dlss4-offline-runner/-/tree/capture-LDR).

Since we don't really care about execution speed, this should be acceptable if still giving consistent frame. However, this time the capture function is not synchronous (blocking). At the same time, `Present()` function, or more precisely, the following

```cpp
IDXGISwapChain : public IDXGIDeviceSubObject
{
public:
    virtual HRESULT STDMETHODCALLTYPE Present( 
        /* [in] */ UINT SyncInterval,
        /* [in] */ UINT Flags) = 0;
/// other function declarations
}
```

DXGI API being wrapped by `Present()`, is an async call. This means `bool presentSuccess = Present();` immediately returns and then `afterPresent()` callback, where we capture screen and force sleep, gets executed. It works under control when we use `BitBlit()`, a sync function which blocks and captures the frame displayed at that exact moment. However, our HDR capture has a more complex logic (event trigger and catcher) and is not **immediate**.

As a result, without special treatment, we end up unpredictably capture frame N-1 for frame N image. It's unpredictable since the error can happen or not happen on different machines, and even across different runs on the same machine. The cause is likely that the actual display async action being queued by frame N-1 `Present()` happens after frame N starts its `TryGetNextFrame()` catcher, thus catching frame N-1.

Our solution is to completely replace this hacky "sleep bubble" trick with something much more robust: duplication detection with image hash. Without "sleep bubble", our capture function cannot catch up with the 60 FPS (actually 120 FPS with FG turned on) frame rate. An extended frame display time let use accurately capture the frame we expect. However, this is nothing compared to image hash, which precisely results in the unique new frame we want.

We deprecated "sleep bubble" trick, and used image hash on the frame data we just captured. The key is not to simply compare with the previous frame hash, but to maintain a bin of these hashes. In the end, we ensure the bin has 2N hashes, N for rendered frames and FG frames each. This immediately ensures we get all 2N frames from the renderer, each exactly once.

When a duplication occurs, the capture thread simply sleeps for `DuplicateTimeout`. In theory, this could be as short as frame rate (e.g. 1/60 sec), but we found giving it a slightly bigger value (current choice is 500 ms = 0.5 sec) is better. Writing a 4K HDR image takes about the same time, and thus the actual frame rate when we take capture is equally slow.

In the end, we prepared a batch script and a Python helper script to perform a test run. It repeats 5/10 runs on the same input (Cmdline args for hack options) and double-confirm no duplication exists in the captured outputs.
