# Minecraft PE 0.6.1 for iOS 3.1
An attempt to add iPhone OS 3.1 support and this successfully, OpenGL ES 2.0 has been completely removed in favor of OpenGL ES 1.1, so you can play on iPhone 2G with iOS 3.1+.


# What changed?
- Project migrated from iOS 6.0 SDK + Xcode 4.5 or 4.6 to Xcode 4.1 + iOS 4.3 SDK
- OpenGL ES 2.0 has been completely replaced by OpenGL ES 1.1
- Game settings are working (almost)
- Objective-C code has been migrated to be compatible with iPhone OS 3.1
- Fixed building using VS2022 for quick testing of new changes

# Building

Install Xcode 4.1 (may work with 4.0 and 4.2–4.4.1, but not tested). Set the system date to February 25, 2012, then after installing open the folder `handheld/project/iosproj`, open the `.xcodeproj` file and compile.

# Tested on
- iPod Touch 1G (ARMv6)
- iPod Touch 2G (ARMv6)
- iPhone 2G (ARMv6)
- iPhone 3GS (ARMv7)
- iPhone 4S (ARMv7)
- iPhone 5 (ARMv7s) (There are no black frames on the sides of the game)
- iPad 1 (ARMv7) (Tested on iOS 3.2.2)

# Online
It also works online between the original version of Minecraft PE 0.6.1 and the modified one, as well as between two ARMv6 devices

# Credits
Thanks to [@yefengeeeeeeeeeee](https://github.com/yefengeeeeeeeeeee) for fixing the iPad xib files for Xcode 4.1!


- tiktok video (for proof in comments)

https://www.tiktok.com/t/ZP8gMyTYB/