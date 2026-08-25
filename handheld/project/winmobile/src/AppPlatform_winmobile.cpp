#include "AppPlatform_winmobile.h"
#include "png_decode.h"
#include "wince_compat.h"
#include "world/level/storage/FolderMethods.h"
#include "util/Mth.h"
#include "client/renderer/gles.h"

#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
/* sipapi.h declares SipGetCurrentIM/SipSetCurrentIM in terms of CLSID, which
   WIN32_LEAN_AND_MEAN keeps out of windows.h. */
#include <basetyps.h>
#include <sipapi.h>

namespace {

/* HD2: 3.7" diagonal, 480x800.  sqrt(480^2+800^2) = 933.1 px over
   3.7 * 25.4 = 93.98 mm. */
const float HD2_PIXELS_PER_MM = 9.93f;

}  /* namespace */

AppPlatform_winmobile::AppPlatform_winmobile()
:	_width(800),   /* HD2 rotated to landscape; corrected by setScreenSize */
	_height(480),
	_idleTickCounter(0)
{
}

void AppPlatform_winmobile::setScreenSize(int width, int height)
{
	if (width > 0 && height > 0) {
		_width  = width;
		_height = height;
	}
}

BinaryBlob AppPlatform_winmobile::readAssetFile(const std::string& filename)
{
	/* "rb", not the "r" the win32 version uses: these are binary blobs, and
	   CE's text mode would eat every 0x0d in them. */
	FILE* fp = fopen(("data/" + filename).c_str(), "rb");
	if (!fp) {
		LOGW("readAssetFile: missing data/%s\n", filename.c_str());
		return BinaryBlob();
	}

	int size = getRemainingFileSize(fp);
	if (size <= 0) {
		fclose(fp);
		return BinaryBlob();
	}

	BinaryBlob blob;
	blob.size = size;
	blob.data = new unsigned char[size];

	size_t got = fread(blob.data, 1, (size_t)size, fp);
	fclose(fp);

	if (got != (size_t)size) {
		LOGE("readAssetFile: short read on %s (%d of %d)\n",
		     filename.c_str(), (int)got, size);
		delete[] blob.data;
		return BinaryBlob();
	}
	return blob;
}

TextureData AppPlatform_winmobile::loadTexture(const std::string& filename_,
                                              bool textureFolder)
{
	TextureData out;

	const std::string filename = textureFolder ? "data/images/" + filename_
	                                           : filename_;

	unsigned char* pixels = 0;
	int w = 0, h = 0;
	bool hasAlpha = false;

	if (!pngDecodeFileRGBA(filename.c_str(), &pixels, &w, &h, &hasAlpha)) {
		LOGE("loadTexture: failed to load %s\n", filename.c_str());
		return out;
	}

	out.w = w;
	out.h = h;
	out.data = pixels;
	out.numBytes = w * h * 4;
	out.format = TEXF_UNCOMPRESSED_8888;
	out.memoryHandledExternally = false;

	/* out.transparent is deliberately left at its default of true, and hasAlpha
	   is deliberately ignored.

	   Despite the name, Textures::assignTexture does not use it as a hint about
	   whether the image has any translucent pixels -- it is the only thing
	   selecting the pixel format:

	       const GLint mode = img.transparent? GL_RGBA : GL_RGB;
	       glTexImage2D(..., mode, w, h, 0, mode, GL_UNSIGNED_BYTE, img.data);

	   pngDecodeFileRGBA always expands to 4 bytes per pixel, whatever the PNG's
	   colour type, so GL_RGB would make the driver walk the buffer 3 bytes at a
	   time: every row would start one pixel further into the previous row and
	   the channels would rotate R,G,B,A,R,G,B... The visible result on an opaque
	   texture was vertical colour stripes -- the 4-texel-per-row slip aliasing
	   against the 4-byte channel period -- which is exactly what
	   images/gui/background.png did on the device. Every other GUI asset happens
	   to carry an alpha channel, which is why the dirt background was the only
	   thing that looked wrong.

	   Reporting RGBA is also the only legal option on ES 1.1 regardless: the spec
	   requires internalformat == format in glTexImage2D, so there is no way to
	   ask for an RGB internal format from RGBA data here.  An extra byte per
	   texel costs ~1.2 MB across the 3.6 MB of textures the game loads; a
	   pre-pass that repacked opaque images to 3 bytes would save that, but it
	   would have to run on every load and it is not what the 32 MB budget is
	   tight on. */
	(void)hasAlpha;

	return out;
}

void AppPlatform_winmobile::saveScreenshot(const std::string& filename,
                                           int glWidth, int glHeight)
{
	if (glWidth <= 0 || glHeight <= 0)
		return;

	/* Always a BMP: encoding PNG would mean writing a deflate compressor, and
	   callers pass a ".jpg" name that was never honoured on any platform. */
	std::string path = filename;
	std::string::size_type dot = path.find_last_of('.');
	if (dot != std::string::npos && path.find_last_of('\\') < dot)
		path.erase(dot);
	path += ".bmp";

	FILE* fp = fopen(path.c_str(), "wb");
	if (!fp) {
		LOGE("saveScreenshot: cannot write %s\n", path.c_str());
		return;
	}

	unsigned char* rgba = new unsigned char[(size_t)glWidth * glHeight * 4];
	glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	const int rowBytes = glWidth * 3;
	const int pad = (4 - (rowBytes & 3)) & 3;
	const unsigned dataSize = (unsigned)(rowBytes + pad) * glHeight;

	/* Written field by field: the BMP headers are 2-byte aligned, so a struct
	   would need packing attributes to be laid out correctly. */
	unsigned char hdr[54];
	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 'B'; hdr[1] = 'M';
	*(unsigned*)(hdr + 2)  = 54 + dataSize;   /* file size */
	*(unsigned*)(hdr + 10) = 54;              /* pixel data offset */
	*(unsigned*)(hdr + 14) = 40;              /* BITMAPINFOHEADER size */
	*(int*)(hdr + 18) = glWidth;
	*(int*)(hdr + 22) = glHeight;             /* positive: bottom-up */
	*(unsigned short*)(hdr + 26) = 1;         /* planes */
	*(unsigned short*)(hdr + 28) = 24;        /* bits per pixel */
	*(unsigned*)(hdr + 34) = dataSize;
	fwrite(hdr, 1, sizeof(hdr), fp);

	/* GL hands back rows bottom-up, which is exactly BMP's own order, so the
	   rows go out as they come. BMP wants BGR. */
	unsigned char* row = new unsigned char[rowBytes + pad];
	memset(row + rowBytes, 0, (size_t)pad);
	for (int y = 0; y < glHeight; ++y) {
		const unsigned char* src = rgba + (size_t)y * glWidth * 4;
		for (int x = 0; x < glWidth; ++x) {
			row[x * 3 + 0] = src[x * 4 + 2];
			row[x * 3 + 1] = src[x * 4 + 1];
			row[x * 3 + 2] = src[x * 4 + 0];
		}
		fwrite(row, 1, (size_t)(rowBytes + pad), fp);
	}

	delete[] row;
	delete[] rgba;
	fclose(fp);

	LOGI("saveScreenshot: wrote %s (%dx%d)\n", path.c_str(), glWidth, glHeight);
}

std::string AppPlatform_winmobile::getDateString(int s)
{
	/* Unix epoch -> FILETIME: 100 ns ticks, based at 1601-01-01. */
	const long long EPOCH_DELTA = 116444736000000000LL;
	long long ticks = EPOCH_DELTA + (long long)s * 10000000LL;

	FILETIME ft;
	ft.dwLowDateTime  = (DWORD)(ticks & 0xffffffffLL);
	ft.dwHighDateTime = (DWORD)(ticks >> 32);

	FILETIME local;
	SYSTEMTIME st;
	if (!FileTimeToLocalFileTime(&ft, &local) ||
	    !FileTimeToSystemTime(&local, &st)) {
		char buf[32];
		sprintf(buf, "%d s", s);
		return std::string(buf);
	}

	char buf[32];
	sprintf(buf, "%04d-%02d-%02d %02d:%02d",
	        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
	return std::string(buf);
}

float AppPlatform_winmobile::getPixelsPerMillimeter()
{
	return HD2_PIXELS_PER_MM;
}

bool AppPlatform_winmobile::isNetworkEnabled(bool /*onlyWifiAllowed*/)
{
	/* Asking Connection Manager whether a connection could be established is
	   both slow and prone to dialling; the game only uses this to decide
	   whether to show multiplayer, so let the user try and fail. */
	return true;
}

void AppPlatform_winmobile::vibrate(int milliSeconds)
{
	/* All of it, including the reason the motor is not simply switched on here,
	   is in wince_vibrate.cpp. */
	wce_vibrate(milliSeconds);
}

std::string AppPlatform_winmobile::getPlatformStringVar(int stringId)
{
	if (stringId == PlatformStringVars::DEVICE_BUILD_MODEL) {
		WCHAR oem[128];
		memset(oem, 0, sizeof(oem));
		if (SystemParametersInfoW(SPI_GETOEMINFO, sizeof(oem), oem, 0) && oem[0])
			return wce_narrow(oem);
		return "Windows Mobile";
	}
	return AppPlatform::getPlatformStringVar(stringId);
}

void AppPlatform_winmobile::showKeyboard()
{
	AppPlatform::showKeyboard();
	SipShowIM(SIPF_ON);
}

void AppPlatform_winmobile::hideKeyboard()
{
	AppPlatform::hideKeyboard();
	SipShowIM(SIPF_OFF);
}

void AppPlatform_winmobile::_tick()
{
	/* Without this the backlight dims and then the device suspends mid-game:
	   WM only counts key and stylus events as activity, and a player holding
	   a movement button generates neither. Roughly once a second at 30 fps --
	   the timer only needs resetting well inside its timeout. */
	if (++_idleTickCounter >= 30) {
		_idleTickCounter = 0;
		SystemIdleTimerReset();
	}
}
