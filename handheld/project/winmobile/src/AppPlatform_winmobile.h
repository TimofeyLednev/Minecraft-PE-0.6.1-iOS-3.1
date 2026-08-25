#ifndef APPPLATFORM_WINMOBILE_H__
#define APPPLATFORM_WINMOBILE_H__

#include "AppPlatform.h"
#include "platform/log.h"

/**
    AppPlatform for Windows Mobile 6.5 / Windows CE 5.2 on the HTC HD2.

    Differences from AppPlatform_win32 that are forced by the platform rather
    than chosen:

      * Asset paths are relative to the .exe, not to a build directory.  CE has
        no current directory at all, so "../../data/x.png" cannot work.  The
        paths below are still written relative ("data/x.png") because
        wce_fopen anchors them; that keeps them readable and keeps the one
        piece of knowledge -- where the .exe lives -- in one place.
      * Textures are decoded by png_decode.cpp.  There is no ARM libpng.
      * The screen size is whatever the window actually got, pushed in by
        main_winmobile.h after the display has been rotated to landscape,
        instead of being hardcoded to a desktop resolution.
*/
class AppPlatform_winmobile : public AppPlatform
{
public:
	AppPlatform_winmobile();

	/** Called by main once the window exists and the display orientation has
	    settled.  Until then getScreenWidth/Height report the HD2's landscape
	    default so that anything constructed early sees a sane value. */
	void setScreenSize(int width, int height);

	BinaryBlob readAssetFile(const std::string& filename);
	TextureData loadTexture(const std::string& filename_, bool textureFolder);

	void saveScreenshot(const std::string& filename, int glWidth, int glHeight);

	std::string getDateString(int s);

	int checkLicense() { return 0; }
	bool hasBuyButtonWhenInvalidLicense() { return false; }

	int getScreenWidth()  { return _width; }
	int getScreenHeight() { return _height; }
	float getPixelsPerMillimeter();

	bool supportsTouchscreen() { return true; }

	/** Adreno 200. The PowerVR path exists for iOS/PVRTC textures. */
	bool isPowerVR() { return false; }

	bool isNetworkEnabled(bool onlyWifiAllowed);

	void vibrate(int milliSeconds);

	std::string getPlatformStringVar(int stringId);

	void showKeyboard();
	void hideKeyboard();

	void _tick();

private:
	int _width;
	int _height;
	int _idleTickCounter;
};

#endif /*APPPLATFORM_WINMOBILE_H__*/
