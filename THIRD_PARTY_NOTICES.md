# Dependencies and research references

## MinHook

- https://github.com/TsudaKageyu/minhook
- Pinned build commit: `8af6b4acae5a9388fd742b56fa79ece89d96f823`
- Copyright Tsuda Kageyu; HDE portions copyright Vyacheslav Patkov.
- BSD-style redistribution terms are reproduced in [MinHook-LICENSE.txt](MinHook-LICENSE.txt).
- CMake fetches this dependency from its upstream Git repository; its source is not vendored here.
  Distribute its license with compiled hook binaries.

## Game ABI / reverse engineering reference

- https://github.com/isledecomp/racers
- The decompilation project is licensed LGPL-3.0. Used to research original binary
  function addresses, object layouts, input semantics, audio paths and vehicle state.
- This hook calls/trampolines the user's original game functions. It does not ship
  the decompilation source tree or a rebuilt game executable.

## DualSense protocol references

- SDL PS5 HID implementation (zlib):
  https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_ps5.c
- Nielk1's trigger protocol documentation / generators:
  https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db
- DualSense Explorer:
  https://github.com/nondebug/dualsense

The implementation uses Windows SDK APIs (HID, SetupAPI, DirectSound, WASAPI) and
does not redistribute Windows system DLLs or any Sony SDK.
