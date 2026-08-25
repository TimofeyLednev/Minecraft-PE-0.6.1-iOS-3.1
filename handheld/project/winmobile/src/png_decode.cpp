#include "png_decode.h"
#include "platform/log.h"

#include <cstring>
#include <cstdio>

/* ============================================================ DEFLATE ==== *
 *
 * RFC 1951 inflate.  Structured after Mark Adler's puff.c -- the canonical
 * decode walks the code bit by bit, which is small and obviously correct at
 * the cost of speed.  That trade is right here: textures are decoded once at
 * load time, and code size competes with the 32 MB process budget.
 *
 * The caller always knows the exact inflated size (PNG gives it away as
 * height * (1 + width * bytesPerPixel)), so the output buffer is exact and
 * there is no grow-and-copy logic at all.
 */

namespace {

const short LEN_BASE[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
	59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const short LEN_EXTRA[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3,
	3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const short DIST_BASE[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
	513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
const short DIST_EXTRA[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
	8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
/* Order in which code lengths for the code-length alphabet are stored. */
const short CLEN_ORDER[19] = {
	16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

struct Huffman {
	short count[16];    /* number of symbols per code length */
	short symbol[288];  /* symbols in canonical order */
};

class Inflater {
public:
	Inflater(const unsigned char* in, size_t inLen,
	         unsigned char* out, size_t outLen)
	:	_in(in), _inLen(inLen), _inPos(0),
		_bitBuf(0), _bitCnt(0),
		_out(out), _outLen(outLen), _outPos(0),
		_ok(true)
	{}

	/** Inflates a raw DEFLATE stream. True only if the whole expected output
	    was produced -- a short image is as useless as a corrupt one. */
	bool run()
	{
		int last;
		do {
			last = getBits(1);
			int type = getBits(2);
			if (!_ok)
				return false;

			switch (type) {
			case 0:  stored();          break;
			case 1:  block(fixedLen(), fixedDist()); break;
			case 2:  dynamicBlock();    break;
			default: _ok = false;       break;  /* type 3 is reserved */
			}
		} while (_ok && !last);

		return _ok && _outPos == _outLen;
	}

private:
	int getBits(int need)
	{
		long val = _bitBuf;
		while (_bitCnt < need) {
			if (_inPos >= _inLen) {
				_ok = false;
				return 0;
			}
			val |= (long)_in[_inPos++] << _bitCnt;
			_bitCnt += 8;
		}
		_bitBuf = (int)(val >> need);
		_bitCnt -= need;
		return (int)(val & ((1L << need) - 1));
	}

	void stored()
	{
		_bitBuf = 0;
		_bitCnt = 0;  /* stored blocks are byte-aligned */

		if (_inPos + 4 > _inLen) { _ok = false; return; }
		unsigned len  = (unsigned)_in[_inPos] | ((unsigned)_in[_inPos + 1] << 8);
		unsigned nlen = (unsigned)_in[_inPos + 2] | ((unsigned)_in[_inPos + 3] << 8);
		_inPos += 4;

		if (len != (~nlen & 0xffff))     { _ok = false; return; }
		if (_inPos + len > _inLen)       { _ok = false; return; }
		if (_outPos + len > _outLen)     { _ok = false; return; }

		memcpy(_out + _outPos, _in + _inPos, len);
		_inPos  += len;
		_outPos += len;
	}

	/** Reads one canonical Huffman code. Returns the symbol, or -1. */
	int decode(const Huffman& h)
	{
		int code = 0, first = 0, index = 0;
		for (int len = 1; len <= 15; ++len) {
			code |= getBits(1);
			if (!_ok)
				return -1;

			int count = h.count[len];
			if (code - count < first)
				return h.symbol[index + (code - first)];

			index += count;
			first  = (first + count) << 1;
			code <<= 1;
		}
		return -1;  /* code longer than 15 bits: corrupt */
	}

	/** Builds @a h from @a length[0..n). Returns 0 if the code set is
	    complete, >0 if incomplete (only legal in special cases), <0 if
	    over-subscribed. */
	static int build(Huffman& h, const short* length, int n)
	{
		for (int len = 0; len < 16; ++len)
			h.count[len] = 0;
		for (int sym = 0; sym < n; ++sym)
			h.count[length[sym]]++;

		if (h.count[0] == n)
			return 0;  /* no codes at all */

		int left = 1;
		for (int len = 1; len < 16; ++len) {
			left <<= 1;
			left -= h.count[len];
			if (left < 0)
				return left;
		}

		short offs[16];
		offs[1] = 0;
		for (int len = 1; len < 15; ++len)
			offs[len + 1] = offs[len] + h.count[len];

		for (int sym = 0; sym < n; ++sym)
			if (length[sym] != 0)
				h.symbol[offs[length[sym]]++] = (short)sym;

		return left;
	}

	/** Emits literals and matches until the end-of-block symbol. */
	void block(const Huffman& lenCode, const Huffman& distCode)
	{
		for (;;) {
			int symbol = decode(lenCode);
			if (symbol < 0) { _ok = false; return; }

			if (symbol < 256) {
				if (_outPos >= _outLen) { _ok = false; return; }
				_out[_outPos++] = (unsigned char)symbol;
				continue;
			}
			if (symbol == 256)
				return;

			symbol -= 257;
			if (symbol >= 29) { _ok = false; return; }
			int len = LEN_BASE[symbol] + getBits(LEN_EXTRA[symbol]);

			symbol = decode(distCode);
			if (symbol < 0 || symbol >= 30) { _ok = false; return; }
			size_t dist = (size_t)(DIST_BASE[symbol] + getBits(DIST_EXTRA[symbol]));

			if (!_ok) return;
			/* A distance reaching before the start of the output would read
			   uninitialised memory; a length past the end would write out. */
			if (dist > _outPos)                    { _ok = false; return; }
			if (_outPos + (size_t)len > _outLen)   { _ok = false; return; }

			/* Byte-at-a-time on purpose: overlapping copies (dist < len) are
			   how DEFLATE encodes runs, so memcpy/memmove are both wrong. */
			for (int i = 0; i < len; ++i, ++_outPos)
				_out[_outPos] = _out[_outPos - dist];
		}
	}

	static const Huffman& fixedLen()
	{
		static Huffman h;
		static bool built = false;
		if (!built) {
			short lengths[288];
			int i = 0;
			for (; i < 144; ++i) lengths[i] = 8;
			for (; i < 256; ++i) lengths[i] = 9;
			for (; i < 280; ++i) lengths[i] = 7;
			for (; i < 288; ++i) lengths[i] = 8;
			build(h, lengths, 288);
			built = true;
		}
		return h;
	}

	static const Huffman& fixedDist()
	{
		static Huffman h;
		static bool built = false;
		if (!built) {
			short lengths[30];
			for (int i = 0; i < 30; ++i) lengths[i] = 5;
			build(h, lengths, 30);
			built = true;
		}
		return h;
	}

	void dynamicBlock()
	{
		int nLen  = getBits(5) + 257;
		int nDist = getBits(5) + 1;
		int nCode = getBits(4) + 4;
		if (!_ok) return;
		if (nLen > 286 || nDist > 30) { _ok = false; return; }

		/* lengths[] serves twice: first the 19-symbol code-length alphabet,
		   then the literal/length and distance lengths it encodes. */
		short lengths[320];
		memset(lengths, 0, sizeof(lengths));

		for (int i = 0; i < nCode; ++i)
			lengths[CLEN_ORDER[i]] = (short)getBits(3);
		if (!_ok) return;

		Huffman lenCode;
		if (build(lenCode, lengths, 19) != 0) { _ok = false; return; }

		memset(lengths, 0, sizeof(lengths));
		int index = 0;
		while (index < nLen + nDist) {
			int symbol = decode(lenCode);
			if (symbol < 0) { _ok = false; return; }

			if (symbol < 16) {
				lengths[index++] = (short)symbol;
				continue;
			}

			short len = 0;
			int repeat;
			if (symbol == 16) {
				if (index == 0) { _ok = false; return; }  /* nothing to copy */
				len = lengths[index - 1];
				repeat = 3 + getBits(2);
			} else if (symbol == 17) {
				repeat = 3 + getBits(3);
			} else {
				repeat = 11 + getBits(7);
			}
			if (!_ok) return;
			if (index + repeat > nLen + nDist) { _ok = false; return; }

			while (repeat--)
				lengths[index++] = len;
		}

		if (lengths[256] == 0) { _ok = false; return; }  /* no end-of-block */

		Huffman distCode;
		int err = build(lenCode, lengths, nLen);
		/* An incomplete literal code is only valid if it has a single symbol. */
		if (err < 0 || (err > 0 && nLen - lenCode.count[0] != 1)) { _ok = false; return; }
		err = build(distCode, lengths + nLen, nDist);
		if (err < 0 || (err > 0 && nDist - distCode.count[0] != 1)) { _ok = false; return; }

		block(lenCode, distCode);
	}

	const unsigned char* _in;
	size_t _inLen, _inPos;
	int    _bitBuf, _bitCnt;
	unsigned char* _out;
	size_t _outLen, _outPos;
	bool   _ok;
};

/** Strips the RFC 1950 zlib wrapper and inflates. */
bool zlibInflate(const unsigned char* in, size_t inLen,
                 unsigned char* out, size_t outLen)
{
	if (inLen < 2) {
		LOGE("png: zlib stream too short (%d bytes)\n", (int)inLen);
		return false;
	}

	unsigned cmf = in[0], flg = in[1];
	if ((cmf & 0x0f) != 8) {
		LOGE("png: unexpected zlib compression method %d\n", (int)(cmf & 0x0f));
		return false;
	}
	if (((cmf << 8) | flg) % 31 != 0) {
		LOGE("png: bad zlib header check\n");
		return false;
	}
	if (flg & 0x20) {
		/* FDICT: a preset dictionary the file does not carry. No encoder
		   produces this for PNG, so refuse rather than guess. */
		LOGE("png: zlib preset dictionary not supported\n");
		return false;
	}

	/* The trailing Adler-32 is left unverified: the whole file is on local
	   flash, and PNG's own per-chunk CRCs are skipped for the same reason. */
	Inflater inf(in + 2, inLen - 2, out, outLen);
	return inf.run();
}

/* ================================================================ PNG ==== */

unsigned readU32(const unsigned char* p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
	       ((unsigned)p[2] << 8)  |  (unsigned)p[3];
}

int paeth(int a, int b, int c)
{
	int p  = a + b - c;
	int pa = p > a ? p - a : a - p;
	int pb = p > b ? p - b : b - p;
	int pc = p > c ? p - c : c - p;
	if (pa <= pb && pa <= pc) return a;
	return pb <= pc ? b : c;
}

/** Reverses the per-scanline filters in place.
    @param bpp distance in bytes to the pixel to the left (PNG's "bpp",
               which is 1 for anything under 8 bits per sample) */
bool unfilter(unsigned char* raw, int w, int h, int bpp)
{
	const size_t stride = (size_t)w * bpp;
	unsigned char* prev = 0;
	unsigned char* row  = raw;

	for (int y = 0; y < h; ++y) {
		int filter = *row++;   /* the filter byte precedes each scanline */

		switch (filter) {
		case 0:
			break;
		case 1:
			for (size_t x = bpp; x < stride; ++x)
				row[x] = (unsigned char)(row[x] + row[x - bpp]);
			break;
		case 2:
			if (prev)
				for (size_t x = 0; x < stride; ++x)
					row[x] = (unsigned char)(row[x] + prev[x]);
			break;
		case 3:
			for (size_t x = 0; x < stride; ++x) {
				int left = (x >= (size_t)bpp) ? row[x - bpp] : 0;
				int up   = prev ? prev[x] : 0;
				row[x] = (unsigned char)(row[x] + ((left + up) >> 1));
			}
			break;
		case 4:
			for (size_t x = 0; x < stride; ++x) {
				int left     = (x >= (size_t)bpp) ? row[x - bpp] : 0;
				int up       = prev ? prev[x] : 0;
				int upleft   = (prev && x >= (size_t)bpp) ? prev[x - bpp] : 0;
				row[x] = (unsigned char)(row[x] + paeth(left, up, upleft));
			}
			break;
		default:
			LOGE("png: unknown row filter %d on line %d\n", filter, y);
			return false;
		}

		prev = row;
		row += stride;
	}
	return true;
}

}  /* anonymous namespace */

bool pngDecodeRGBA(const unsigned char* file, size_t fileSize,
                   unsigned char** outPixels, int* outW, int* outH,
                   bool* outHasAlpha)
{
	static const unsigned char SIG[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

	if (!file || !outPixels || !outW || !outH)
		return false;
	if (fileSize < 8 + 25 || memcmp(file, SIG, 8) != 0) {
		LOGE("png: not a PNG file\n");
		return false;
	}

	int w = 0, h = 0, bitDepth = 0, colorType = 0, interlace = 0;
	bool haveHeader = false;

	unsigned char palette[256 * 3];
	unsigned char palAlpha[256];
	int palCount = 0;
	memset(palAlpha, 0xff, sizeof(palAlpha));

	/* Two passes over the chunks: the first sizes the IDAT stream so it can
	   be allocated exactly, the second copies it. Cheaper than growing a
	   buffer, and 6 of the shipped files really do split IDAT. */
	size_t idatSize = 0;
	for (int pass = 0; pass < 2; ++pass) {
		size_t idatPos = 0;
		unsigned char* idat = 0;

		if (pass == 1) {
			if (!haveHeader) {
				LOGE("png: missing IHDR\n");
				return false;
			}
			if (idatSize == 0) {
				LOGE("png: no image data\n");
				return false;
			}
			idat = new unsigned char[idatSize];
			if (!idat)
				return false;
		}

		size_t off = 8;
		bool sawEnd = false;
		while (off + 12 <= fileSize && !sawEnd) {
			unsigned len = readU32(file + off);
			const unsigned char* type = file + off + 4;
			const unsigned char* data = file + off + 8;

			/* Guard against a length that runs off the end of the file. */
			if (len > fileSize || off + 12 + len > fileSize) {
				LOGE("png: chunk length %u overruns file\n", len);
				delete[] idat;
				return false;
			}

			if (memcmp(type, "IHDR", 4) == 0) {
				if (pass == 0) {
					if (len < 13) { LOGE("png: short IHDR\n"); return false; }
					w         = (int)readU32(data);
					h         = (int)readU32(data + 4);
					bitDepth  = data[8];
					colorType = data[9];
					interlace = data[12];
					haveHeader = true;
				}
			} else if (memcmp(type, "PLTE", 4) == 0) {
				if (pass == 0 && len <= sizeof(palette)) {
					memcpy(palette, data, len);
					palCount = (int)(len / 3);
				}
			} else if (memcmp(type, "tRNS", 4) == 0) {
				/* Only the palette form is honoured; colour-key transparency
				   for types 0 and 2 is unused by the shipped assets. */
				if (pass == 0 && colorType == 3 && len <= sizeof(palAlpha))
					memcpy(palAlpha, data, len);
			} else if (memcmp(type, "IDAT", 4) == 0) {
				if (pass == 0) {
					idatSize += len;
				} else {
					memcpy(idat + idatPos, data, len);
					idatPos += len;
				}
			} else if (memcmp(type, "IEND", 4) == 0) {
				sawEnd = true;
			}

			off += 12 + len;  /* length + type + data + CRC */
		}

		if (pass == 0) {
			if (!haveHeader) {
				LOGE("png: missing IHDR\n");
				return false;
			}
			/* Reject up front what this decoder deliberately does not do,
			   so a future asset change fails loudly instead of subtly. */
			if (bitDepth != 8) {
				LOGE("png: unsupported bit depth %d (only 8)\n", bitDepth);
				return false;
			}
			if (interlace != 0) {
				LOGE("png: interlaced PNGs are not supported\n");
				return false;
			}
			if (colorType != 0 && colorType != 2 && colorType != 3 &&
			    colorType != 4 && colorType != 6) {
				LOGE("png: unsupported colour type %d\n", colorType);
				return false;
			}
			if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
				LOGE("png: implausible dimensions %dx%d\n", w, h);
				return false;
			}
			if (colorType == 3 && palCount == 0) {
				LOGE("png: palette image with no PLTE\n");
				return false;
			}
			continue;
		}

		/* ---- inflate ---- */
		static const int SAMPLES[7] = { 1, 0, 3, 1, 2, 0, 4 };
		const int bpp = SAMPLES[colorType];
		const size_t rawSize = (size_t)h * (1 + (size_t)w * bpp);

		unsigned char* raw = new unsigned char[rawSize];
		if (!raw) {
			delete[] idat;
			return false;
		}

		bool ok = zlibInflate(idat, idatPos, raw, rawSize);
		delete[] idat;

		if (!ok) {
			LOGE("png: inflate failed (%dx%d, type %d)\n", w, h, colorType);
			delete[] raw;
			return false;
		}
		if (!unfilter(raw, w, h, bpp)) {
			delete[] raw;
			return false;
		}

		/* ---- expand to RGBA ---- */
		unsigned char* rgba = new unsigned char[(size_t)w * h * 4];
		if (!rgba) {
			delete[] raw;
			return false;
		}

		bool hasAlpha = false;
		const size_t stride = 1 + (size_t)w * bpp;
		for (int y = 0; y < h; ++y) {
			const unsigned char* src = raw + (size_t)y * stride + 1;
			unsigned char* dst = rgba + (size_t)y * w * 4;

			for (int x = 0; x < w; ++x, dst += 4, src += bpp) {
				switch (colorType) {
				case 0:  /* greyscale */
					dst[0] = dst[1] = dst[2] = src[0];
					dst[3] = 0xff;
					break;
				case 2:  /* rgb */
					dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
					dst[3] = 0xff;
					break;
				case 3: {  /* palette */
					int i = src[0];
					if (i >= palCount) i = 0;
					dst[0] = palette[i * 3];
					dst[1] = palette[i * 3 + 1];
					dst[2] = palette[i * 3 + 2];
					dst[3] = palAlpha[i];
					break;
				}
				case 4:  /* greyscale + alpha */
					dst[0] = dst[1] = dst[2] = src[0];
					dst[3] = src[1];
					break;
				default: /* 6: rgba */
					dst[0] = src[0]; dst[1] = src[1];
					dst[2] = src[2]; dst[3] = src[3];
					break;
				}
				if (dst[3] != 0xff)
					hasAlpha = true;
			}
		}

		delete[] raw;

		*outPixels = rgba;
		*outW = w;
		*outH = h;
		if (outHasAlpha)
			*outHasAlpha = hasAlpha;
		return true;
	}

	return false;  /* unreachable */
}

bool pngDecodeFileRGBA(const char* path,
                       unsigned char** outPixels, int* outW, int* outH,
                       bool* outHasAlpha)
{
	/* fopen is redirected to wce_fopen by wince_compat.h, so a relative path
	   lands next to the .exe rather than nowhere. */
	FILE* fp = fopen(path, "rb");
	if (!fp) {
		LOGE("png: cannot open %s\n", path);
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (size <= 0) {
		LOGE("png: %s is empty\n", path);
		fclose(fp);
		return false;
	}

	unsigned char* buf = new unsigned char[(size_t)size];
	if (!buf) {
		fclose(fp);
		return false;
	}

	size_t got = fread(buf, 1, (size_t)size, fp);
	fclose(fp);

	bool ok = (got == (size_t)size) &&
	          pngDecodeRGBA(buf, got, outPixels, outW, outH, outHasAlpha);
	if (got != (size_t)size)
		LOGE("png: short read on %s (%d of %ld)\n", path, (int)got, size);

	delete[] buf;
	return ok;
}
