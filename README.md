# Minecraft PE 0.6.1 for iOS 4.0 (feature for iPhone OS 3.1)
An attempt to add iPhone OS 3.1 support, which failed because when running on a real iPhone 3GS, the game shows an eternal black screen. However, it works on iOS 4.0, and OpenGL ES 2.0 has been completely removed in favor of OpenGL ES 1.1, so you can play on iOS 4.0+ with iPhone 3G (ARMv6).


# What changed?
- Project migrated from iOS 6.0 SDK + Xcode 4.5 or 4.6 to Xcode 4.1 + iOS 4.3 SDK
- OpenGL ES 2.0 has been completely replaced by OpenGL ES 1.1
- Game settings are working (almost)
- Objective-C code has been migrated to be compatible with iPhone OS 3.1, although on iPhone OS 3.1 the game freezes on a black screen. Tested on iPhone 3GS with iOS 4.2.1, should work on iOS 4.0 since glDiscardFramebufferEXT was introduced in iOS 4.0.
- Fixed building using VS2022 for quick testing of new changes

# Building

Install Xcode 4.1 (may work with 4.0 and 4.2–4.4.1, but not tested). Set the system date to February 25, 2012, then after installing open the folder `handheld/project/iosproj`, open the `.xcodeproj` file and compile.


# Help needed with iPhone OS 3.1

The project compiles and can run on iPhone OS 3.1, but the game shows a black screen. The problem may be related to `glDiscardFramebufferEXT`, which was not available in iPhone OS 3.1 and was introduced in iOS 4.0. The function is currently handled in `minecraftpeViewController.mm` around line 254. A proper implementation for iPhone OS 3.1 is needed — if you can fix this and get the game running on iPhone OS 3.1, please open a Pull Request and you will be mentioned in the README.
