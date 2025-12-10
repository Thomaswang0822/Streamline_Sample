# DLSS Offline Runner

Based on StreamlineSample. For more info on the original app by Nvidia, see <https://github.com/NVIDIA-RTX/Streamline_Sample>

## "Must Read" Contents

This doc contains both important usage guide and optional technical details. Here we only list out the "must read" ones.

- [Project Setup](#project-setup-adapted-from-original-readme)
- [Cmdline Args](#cmdline-args)
- [Test Machines and Possible Issue](#test-machines-and-possible-issue)
- [Cmdline Option `BatchIndex` and Automated Script](#cmdline-option-batchindex-and-automated-script)

## Project Setup (Adapted from original README)

1. Ensure you have CMake 3.20+ and the vulkan sdk (run `vulkaninfo`) on your system.
2. Ensure that a VK-compatible dxc.exe is available in your system `PATH`.  The best way to do this is to install a recent (1.2.198.1 or newer) Vulkan SDK from [official site](https://vulkan.lunarg.com/sdk/home#windows) and ensure that its `bin` directory is in the build machine's system `PATH`.
3. Clone this repository, then run: `git submodule update --init --recursive`
4. Go to Streamline repo and download the [SDK Release](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.9.0). Unzip the folder and put everything into `streamline/` folder in project root. i.e. `streamline/package.bat` should exist.
5. Switch to **capture-HDR** branch.
6. Run `make.bat` and fix any error in the CMake configure.
7. Open the solution in `_build/`.
8. ~~Install Windows Implementation Library (wil). See section below for how to.~~ UPDATE: it has been added as a git submodule and will be immediately usable.
9. Build Solution.
10. Make sure you read and follow [this section](#true-hdr-capture-new-feature-and-issue) before starting any serious/actual run.

## Cmdline Args

Unlike our AMD FSR Offline Runner, this app doesn't have loading config from json feature. The only way to specify runtime options is through cmdline args.

LATEST UPDATE: we deprecated the `OutputMaxCount` option for these 2 reasons:

1. It is more for a developer convenience (myself) but has little help in production use case. It should be moved in a "Release" product anyway.
2. To solve a tricky bug, we adpoted a multi-run approach (see [this section](#bypass-dlfg-frame-rate-check)), and keep supporting `OutputMaxCount` would make handling frame number very tricky.

It works almost the same. Below is copied from FSR Offline Runner README.

- EnableHack: a global switch, default false. If false, the app will run in its original behavior, rendering Sponza Palace.
- Identifier: a string that helps you identify this run, default "UNDEFINED". Usually set to scene name.
- DisplayResolution: a int that controls the display resolution in K, default 1. Only accepted values are 1, 2, and 4. For now we fix render resolution to 1K.
- ParseJitter: whether to parse and use the jitter data from input filenames, default false. Currently we only have it in 1K inputs, so it will be forced to false it render resolution is not 1K.
- HackPaths: **a single folder relative path** to the input frame capture folder, default *"../media/TEST_SCENE/NPP_JI"*. The path to the encoded MVs and Depths will be constructed automatically by replacing "NPP_JI" to "MVD_JI".
- StoreOutput: whether to store output (screenshots), default false.
- BatchIndex: each batch is 15 frames captured. For example, a 60-frame scene requires 4 runs with BatchIndex from 0 to 3. See [this section](#bypass-dlfg-frame-rate-check) for more details.
- OutputPath: **a single folder relative path** to the output screenshots folder, like *"../media/TEST_SCENE/outputs"*

```shell
# cd to <Project Root>/_bin

./StreamlineSample.exe  -EnableHack \
                        -Identifier "Cmdline_TEST" \
                        -DisplayResolution 4 \
                        -ParseJitter \
                        -HackPaths "../media/TEST_SCENE/NPP_JI" \
                        -StoreOutput \
                        -BatchIndex [0-3] \
                        -OutputPath "../media/TEST_SCENE/outputs"
```

Also, it would be very convenient to set them up in the VS Debugger such that each test run is a one-click. Put this single-line arg list to StreamlineSample  Property Pages, in Configuration Properties -> Debugging -> Commandline Arguments. Adjust if needed. Make sure the `OutputPath` (e.g. `media/TEST_SCENE/screenshots`) exists.

```shell
-EnableHack -Identifier FG_TEST -DisplayResolution 4 -ParseJitter -HackPaths "../media/TEST_SCENE/NPP_JI" -StoreOutput -BatchIndex 2 -OutputPath "../media/TEST_SCENE/screenshots"
```

## Tips

### Turn Off Fullscreen mode

Since we uses a frontend approach to capture screenshot, we have to turn on fullscreen mode to get 3840 x 2160 output. But if you stop on a break point or the app encounters an error, fullscreen will cause machine to halt (you have to Ctrl + Alt + Del and Sign Out and log in again), it's much easier to turn it off when doing test runs.

Go to `src\main.cpp` line 267 to turn it off.

### Temporarily Disable Hack or StoreOutput

If you want to disable hack or disable export output, you can go to `src\main.cpp` line 264.

The former is useful if you want to play with the GUI control panel, because we turned it off when hack is on (otherwise screen capture will include the GUI), or when you simply want to see how the original scene is rendered.

The latter is useful if you want to fix issues unrelated to export. We levarage `sleep_for()` to correctly capture screenshot, thus turning it off can save your time. Also, when export is off, the app keeps running instead of closing after `OutputMaxCount` frames.

## Test Machines and Possible Issue

This offline runner has been tested on the following 2 machines:

- (MT Arch Lab Machine) i5-12400, 6 cores 12 threads 2.50-4.00 GHz; RTX 5080; 4K 60Hz monitor.
- (My game PC) AMD R9-9950X3D, 16 cores 32 threads 4.3-5.7 GHz; RTX 5090; 4K 240Hz.

While I have tested every feature on both machines as I develop, I cannot 100% guarantee no issue found on other machines. First, both machines equip high-end GPU, and things may not happen in the same way (e.g. frame computed too slow) on a mainstream GPU like RTX 5060. Also, we have found that certain issues had existed (not now on our production release) on one machine but had never occurred on the other. This implies that hardware-dependent issues have occurred and may occur in the future on different machines.

For now, a possible (but quite low chance: after timeout parameter tuning, it didn't occur in 5 test runs) issue is: out of the 60 FG outputs we generated, there could be 1 or 2 rendered frames instead of FG frames. And this issue, if happens, happens unpredicatably across different runs on the same machine, i.e. it's incorrect on random 1 or 2 frames. This can be overcome by simply doing another run and replacing wrong frames with correct ones in the new run. It only ever happened on 5080 Lab Machine. My personal guess is the very different monitor refresh rate (240 vs 60) is the cause, as frame gets displayed and can be captured "more on time" with higher refresh rate.

If any issue occurs, including the above one, and is untolerable, please contact me (<haoxuan.wang@mthreads.com>)

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

Our solution is to completely replace this hacky "sleep bubble" trick with something much more robust: duplication detection with image hash. Previously without "sleep bubble", our capture function could not catch up with the 60 FPS (actually 120 FPS with FG turned on) frame rate. An extended frame display time let us accurately capture the frame we expect. However, this is nothing compared to image hash, which precisely results in the unique new frame we want.

We deprecated "sleep bubble" trick, and used image hash on the frame data we just captured. The key is not to simply compare with the previous frame hash, but to maintain a bin of these hashes. In the end, we ensure the bin has 2N hashes, N for rendered frames and FG frames each. This immediately ensures we get all 2N frames from the renderer, each exactly once.

When a duplication occurs, the capture thread simply sleeps for `DuplicateTimeout`. In theory, this could be as short as frame rate (e.g. 1/60 sec), but we found giving it a slightly bigger value (current choice is 500 ms = 0.5 sec) is better. Writing a 4K HDR image takes about the same time, and thus the actual frame rate when we take capture is equally slow.

In the end, we prepared a batch script and a Python helper script to perform a test run. It repeats 5 or 10 runs on the same input (Cmdline args for hack options) and double-confirm no duplication exists in the captured outputs.

UPDATE: we have provided a new script [run_OneScene.bat](run_OneScene.bat) which extracts per-scene run. It is more powerful and flexible (itself takes cmdline args), and is recommended to use in production case. And our old test script [run_MultiTimes.bat](run_MultiTimes.bat) now simply repeats that several times. And it should be used only for
confirming correctness on certain machines, not for production.

## Bypass DLFG Frame Rate Check

Previously when debugging, to speed up each run, we only loaded 10/60 of total input frames (and this is where the deprecated `OutputMaxCount` option originated). After using all 60 inputs, we found a critical issue:

```console
WARNING: [13-53-08][streamline][warn][tid:30096][41s:619ms:032us]dlfgPresent.cpp:1260[presentCommon] Frame rate over 100.00ms, reseting frame timer
```

After some careful experiments, we found that this 100 ms or 10 FPS redline would be reached if we do ANYTHING additional in a regular pipeline. The result of frame timer being reset is that dlfg will disable presenting FG frames, and thus breaks our screen capture design entirely.

We've tried to postpone the file IO to app shutdown, since taking screen capture is essentially a screen-to-memory + memory-to-file 2-step operation. But screen-to-memory itself must be done on-the-fly. And as we said, itself would make the frame rate too slow.

Fortunately, frame rate can only be measured after some frames have been presented, and this number for dlfg is about 20. Thus, our solution is what I called "multi-run batch captures" approach. Put it simple, each run of the app only captures 15 frames before dlfg notices the app is running slow, and we launch the app multiple times but reading in different input batches. Together they become full N frames of output.

## Cmdline Option `BatchIndex` and Automated Script

A cmdline option `BatchIndex` has been added to support the above feature. **NOTE: When running without a script (e.g. with VS Debugger), users should know the total number of input frames and pass in the correct 0-indexed `BatchIndex`.** Fortunately, this easy counting + index computing can be automated by the provided script, which is the typical use case.

We don't recommend you do so, but our automated script does take cmdline options itself at runtime. i.e. For example, you can use

```sh
.\run_OneScene.bat Cyberpunk2077_fgTest ".\media\TEST_SCENE\NPP_JI" "..\media\TEST_SCENE\outputs"
```

In this way, users can use a master script (we didn't provide it) to call the automated script on different test scenes. Our script does NOT support other cmdline options other than `Identifier`, `HackPaths`, and `StoreOutput`, because they should be either fixed at production stage (e.g. `StoreOutput`) or internal and auto-computed (e.g. `BatchIndex`).

Low-level edge-case details like incomplete batch (batch 5 should capture frame 75 to 79 in the example) and head/tail frame correctness (yes, we will read some "safety frames" in addition to ensure they are computed with their neighbor frames) are handled and users don't need to worry about them.
