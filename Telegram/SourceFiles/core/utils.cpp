/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/utils.h"

#include "base/qthelp_url.h"
#include "platform/platform_specific.h"

extern "C" {
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/engine.h>
#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
} // extern "C"

#ifdef Q_OS_WIN
#elif defined Q_OS_MAC
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

uint64 _SharedMemoryLocation[4] = { 0x00, 0x01, 0x02, 0x03 };

// Base types compile-time check
static_assert(sizeof(char) == 1, "Basic types size check failed");
static_assert(sizeof(uchar) == 1, "Basic types size check failed");
static_assert(sizeof(int16) == 2, "Basic types size check failed");
static_assert(sizeof(uint16) == 2, "Basic types size check failed");
static_assert(sizeof(int32) == 4, "Basic types size check failed");
static_assert(sizeof(uint32) == 4, "Basic types size check failed");
static_assert(sizeof(int64) == 8, "Basic types size check failed");
static_assert(sizeof(uint64) == 8, "Basic types size check failed");
static_assert(sizeof(float32) == 4, "Basic types size check failed");
static_assert(sizeof(float64) == 8, "Basic types size check failed");
static_assert(sizeof(mtpPrime) == 4, "Basic types size check failed");
static_assert(sizeof(MTPint) == 4, "Basic types size check failed");
static_assert(sizeof(MTPlong) == 8, "Basic types size check failed");
static_assert(sizeof(MTPint128) == 16, "Basic types size check failed");
static_assert(sizeof(MTPint256) == 32, "Basic types size check failed");
static_assert(sizeof(MTPdouble) == 8, "Basic types size check failed");

static_assert(sizeof(int) >= 4, "Basic types size check failed");

// Precise timing functions / rand init

namespace ThirdParty {

	void start() {
		Platform::ThirdParty::start();

		if (!RAND_status()) { // should be always inited in all modern OS
			const auto FeedSeed = [](auto value) {
				RAND_seed(&value, sizeof(value));
			};
#ifdef Q_OS_WIN
			LARGE_INTEGER li;
			QueryPerformanceFrequency(&li);
			FeedSeed(li.QuadPart);
			QueryPerformanceCounter(&li);
			FeedSeed(li.QuadPart);
#elif defined Q_OS_MAC
			mach_timebase_info_data_t tb = { 0 };
			mach_timebase_info(&tb);
			FeedSeed(tb);
			FeedSeed(mach_absolute_time());
#else
			timespec ts = { 0 };
			clock_gettime(CLOCK_MONOTONIC, &ts);
			FeedSeed(ts);
#endif
			if (!RAND_status()) {
				LOG(("MTP Error: Could not init OpenSSL rand, RAND_status() is 0..."));
			}
		}
	}

	void finish() {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
		EVP_default_properties_enable_fips(nullptr, 0);
#else
		FIPS_mode_set(0);
#endif
		CONF_modules_unload(1);

		Platform::ThirdParty::finish();
	}
}

int32 *hashSha1(const void *data, uint32 len, void *dest) {
	return (int32*)SHA1((const uchar*)data, (size_t)len, (uchar*)dest);
}

int32 *hashSha256(const void *data, uint32 len, void *dest) {
	return (int32*)SHA256((const uchar*)data, (size_t)len, (uchar*)dest);
}

// md5 hash, taken somewhere from the internet

namespace {

	inline void _md5_decode(uint32 *output, const uchar *input, uint32 len) {
		for (uint32 i = 0, j = 0; j < len; i++, j += 4) {
			output[i] = ((uint32)input[j]) | (((uint32)input[j + 1]) << 8) | (((uint32)input[j + 2]) << 16) | (((uint32)input[j + 3]) << 24);
		}
	}

	inline void _md5_encode(uchar *output, const uint32 *input, uint32 len) {
		for (uint32 i = 0, j = 0; j < len; i++, j += 4) {
			output[j + 0] = (input[i]) & 0xFF;
			output[j + 1] = (input[i] >> 8) & 0xFF;
			output[j + 2] = (input[i] >> 16) & 0xFF;
			output[j + 3] = (input[i] >> 24) & 0xFF;
		}
	}

	inline uint32 _md5_rotate_left(uint32 x, int n) {
		return (x << n) | (x >> (32 - n));
	}

	inline uint32 _md5_F(uint32 x, uint32 y, uint32 z) {
		return (x & y) | (~x & z);
	}

	inline uint32 _md5_G(uint32 x, uint32 y, uint32 z) {
		return (x & z) | (y & ~z);
	}

	inline uint32 _md5_H(uint32 x, uint32 y, uint32 z) {
		return x ^ y ^ z;
	}

	inline uint32 _md5_I(uint32 x, uint32 y, uint32 z) {
		return y ^ (x | ~z);
	}

	inline void _md5_FF(uint32 &a, uint32 b, uint32 c, uint32 d, uint32 x, uint32 s, uint32 ac) {
		a = _md5_rotate_left(a + _md5_F(b, c, d) + x + ac, s) + b;
	}

	inline void _md5_GG(uint32 &a, uint32 b, uint32 c, uint32 d, uint32 x, uint32 s, uint32 ac) {
		a = _md5_rotate_left(a + _md5_G(b, c, d) + x + ac, s) + b;
	}

	inline void _md5_HH(uint32 &a, uint32 b, uint32 c, uint32 d, uint32 x, uint32 s, uint32 ac) {
		a = _md5_rotate_left(a + _md5_H(b, c, d) + x + ac, s) + b;
	}

	inline void _md5_II(uint32 &a, uint32 b, uint32 c, uint32 d, uint32 x, uint32 s, uint32 ac) {
		a = _md5_rotate_left(a + _md5_I(b, c, d) + x + ac, s) + b;
	}

	static uchar _md5_padding[64] = {
		0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
}

HashMd5::HashMd5(const void *input, uint32 length) : _finalized(false) {
	init();
	if (input && length > 0) feed(input, length);
}

void HashMd5::feed(const void *input, uint32 length) {
	uint32 index = _count[0] / 8 % _md5_block_size;

	const uchar *buf = (const uchar *)input;

	if ((_count[0] += (length << 3)) < (length << 3)) {
		_count[1]++;
	}
	_count[1] += (length >> 29);

	uint32 firstpart = 64 - index;

	uint32 i;

	if (length >= firstpart) {
		memcpy(&_buffer[index], buf, firstpart);
		transform(_buffer);

		for (i = firstpart; i + _md5_block_size <= length; i += _md5_block_size) {
			transform(&buf[i]);
		}

		index = 0;
	} else {
		i = 0;
	}

	memcpy(&_buffer[index], &buf[i], length - i);
}

int32 *HashMd5::result() {
	if (!_finalized) finalize();
	return (int32*)_digest;
}

void HashMd5::init() {
	_count[0] = 0;
	_count[1] = 0;

	_state[0] = 0x67452301;
	_state[1] = 0xefcdab89;
	_state[2] = 0x98badcfe;
	_state[3] = 0x10325476;
}

void HashMd5::finalize() {
	if (!_finalized) {
		uchar bits[8];
		_md5_encode(bits, _count, 8);

		uint32 index = _count[0] / 8 % 64, paddingLen = (index < 56) ? (56 - index) : (120 - index);
		feed(_md5_padding, paddingLen);
		feed(bits, 8);

		_md5_encode(_digest, _state, 16);

		_finalized = true;
	}
}

void HashMd5::transform(const uchar *block) {
	uint32 a = _state[0], b = _state[1], c = _state[2], d = _state[3], x[16];
	_md5_decode(x, block, _md5_block_size);

	_md5_FF(a, b, c, d, x[0] , 7 , 0xd76aa478);
	_md5_FF(d, a, b, c, x[1] , 12, 0xe8c7b756);
	_md5_FF(c, d, a, b, x[2] , 17, 0x242070db);
	_md5_FF(b, c, d, a, x[3] , 22, 0xc1bdceee);
	_md5_FF(a, b, c, d, x[4] , 7 , 0xf57c0faf);
	_md5_FF(d, a, b, c, x[5] , 12, 0x4787c62a);
	_md5_FF(c, d, a, b, x[6] , 17, 0xa8304613);
	_md5_FF(b, c, d, a, x[7] , 22, 0xfd469501);
	_md5_FF(a, b, c, d, x[8] , 7 , 0x698098d8);
	_md5_FF(d, a, b, c, x[9] , 12, 0x8b44f7af);
	_md5_FF(c, d, a, b, x[10], 17, 0xffff5bb1);
	_md5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
	_md5_FF(a, b, c, d, x[12], 7 , 0x6b901122);
	_md5_FF(d, a, b, c, x[13], 12, 0xfd987193);
	_md5_FF(c, d, a, b, x[14], 17, 0xa679438e);
	_md5_FF(b, c, d, a, x[15], 22, 0x49b40821);

	_md5_GG(a, b, c, d, x[1] , 5 , 0xf61e2562);
	_md5_GG(d, a, b, c, x[6] , 9 , 0xc040b340);
	_md5_GG(c, d, a, b, x[11], 14, 0x265e5a51);
	_md5_GG(b, c, d, a, x[0] , 20, 0xe9b6c7aa);
	_md5_GG(a, b, c, d, x[5] , 5 , 0xd62f105d);
	_md5_GG(d, a, b, c, x[10], 9 , 0x2441453);
	_md5_GG(c, d, a, b, x[15], 14, 0xd8a1e681);
	_md5_GG(b, c, d, a, x[4] , 20, 0xe7d3fbc8);
	_md5_GG(a, b, c, d, x[9] , 5 , 0x21e1cde6);
	_md5_GG(d, a, b, c, x[14], 9 , 0xc33707d6);
	_md5_GG(c, d, a, b, x[3] , 14, 0xf4d50d87);
	_md5_GG(b, c, d, a, x[8] , 20, 0x455a14ed);
	_md5_GG(a, b, c, d, x[13], 5 , 0xa9e3e905);
	_md5_GG(d, a, b, c, x[2] , 9 , 0xfcefa3f8);
	_md5_GG(c, d, a, b, x[7] , 14, 0x676f02d9);
	_md5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

	_md5_HH(a, b, c, d, x[5] , 4 , 0xfffa3942);
	_md5_HH(d, a, b, c, x[8] , 11, 0x8771f681);
	_md5_HH(c, d, a, b, x[11], 16, 0x6d9d6122);
	_md5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
	_md5_HH(a, b, c, d, x[1] , 4 , 0xa4beea44);
	_md5_HH(d, a, b, c, x[4] , 11, 0x4bdecfa9);
	_md5_HH(c, d, a, b, x[7] , 16, 0xf6bb4b60);
	_md5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
	_md5_HH(a, b, c, d, x[13], 4 , 0x289b7ec6);
	_md5_HH(d, a, b, c, x[0] , 11, 0xeaa127fa);
	_md5_HH(c, d, a, b, x[3] , 16, 0xd4ef3085);
	_md5_HH(b, c, d, a, x[6] , 23, 0x4881d05);
	_md5_HH(a, b, c, d, x[9] , 4 , 0xd9d4d039);
	_md5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
	_md5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
	_md5_HH(b, c, d, a, x[2] , 23, 0xc4ac5665);

	_md5_II(a, b, c, d, x[0] , 6 , 0xf4292244);
	_md5_II(d, a, b, c, x[7] , 10, 0x432aff97);
	_md5_II(c, d, a, b, x[14], 15, 0xab9423a7);
	_md5_II(b, c, d, a, x[5] , 21, 0xfc93a039);
	_md5_II(a, b, c, d, x[12], 6 , 0x655b59c3);
	_md5_II(d, a, b, c, x[3] , 10, 0x8f0ccc92);
	_md5_II(c, d, a, b, x[10], 15, 0xffeff47d);
	_md5_II(b, c, d, a, x[1] , 21, 0x85845dd1);
	_md5_II(a, b, c, d, x[8] , 6 , 0x6fa87e4f);
	_md5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
	_md5_II(c, d, a, b, x[6] , 15, 0xa3014314);
	_md5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
	_md5_II(a, b, c, d, x[4] , 6 , 0xf7537e82);
	_md5_II(d, a, b, c, x[11], 10, 0xbd3af235);
	_md5_II(c, d, a, b, x[2] , 15, 0x2ad7d2bb);
	_md5_II(b, c, d, a, x[9] , 21, 0xeb86d391);

	_state[0] += a;
	_state[1] += b;
	_state[2] += c;
	_state[3] += d;
}

int32 *hashMd5(const void *data, uint32 len, void *dest) {
	HashMd5 md5(data, len);
	memcpy(dest, md5.result(), 16);

	return (int32*)dest;
}

char *hashMd5Hex(const int32 *hashmd5, void *dest) {
	char *md5To = (char*)dest;
	const uchar *res = (const uchar*)hashmd5;

	for (int i = 0; i < 16; ++i) {
		uchar ch(res[i]), high = (ch >> 4) & 0x0F, low = ch & 0x0F;
		md5To[i * 2 + 0] = high + ((high > 0x09) ? ('a' - 0x0A) : '0');
		md5To[i * 2 + 1] = low + ((low > 0x09) ? ('a' - 0x0A) : '0');
	}

	return md5To;
}

namespace {
	QMap<QString, QString> fastRusEng;
	QHash<QChar, QString> fastLetterRusEng;
	QMap<uint32, QString> fastDoubleLetterRusEng;
	QHash<QChar, QChar> fastRusKeyboardSwitch;
	QHash<QChar, QChar> fastUkrKeyboardSwitch;
}

QString translitLetterRusEng(QChar letter, QChar next, int32 &toSkip) {
	if (fastDoubleLetterRusEng.isEmpty()) {
		fastDoubleLetterRusEng.insert((QString::fromUtf8("Ы").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), u"Y"_q);
		fastDoubleLetterRusEng.insert((QString::fromUtf8("и").at(0).unicode() << 16) | QString::fromUtf8("я").at(0).unicode(), u"ia"_q);
		fastDoubleLetterRusEng.insert((QString::fromUtf8("и").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), u"y"_q);
		fastDoubleLetterRusEng.insert((QString::fromUtf8("к").at(0).unicode() << 16) | QString::fromUtf8("с").at(0).unicode(), u"x"_q);
		fastDoubleLetterRusEng.insert((QString::fromUtf8("ы").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), u"y"_q);
		fastDoubleLetterRusEng.insert((QString::fromUtf8("ь").at(0).unicode() << 16) | QString::fromUtf8("е").at(0).unicode(), u"ye"_q);
	}
	QMap<uint32, QString>::const_iterator i = fastDoubleLetterRusEng.constFind((letter.unicode() << 16) | next.unicode());
	if (i != fastDoubleLetterRusEng.cend()) {
		toSkip = 2;
		return i.value();
	}

	toSkip = 1;
	if (fastLetterRusEng.isEmpty()) {
		fastLetterRusEng.insert(QString::fromUtf8("А").at(0), u"A"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Б").at(0), u"B"_q);
		fastLetterRusEng.insert(QString::fromUtf8("В").at(0), u"V"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Г").at(0), u"G"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ґ").at(0), u"G"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Д").at(0), u"D"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Е").at(0), u"E"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Є").at(0), u"Ye"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ё").at(0), u"Yo"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ж").at(0), u"Zh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("З").at(0), u"Z"_q);
		fastLetterRusEng.insert(QString::fromUtf8("И").at(0), u"I"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ї").at(0), u"Yi"_q);
		fastLetterRusEng.insert(QString::fromUtf8("І").at(0), u"I"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Й").at(0), u"J"_q);
		fastLetterRusEng.insert(QString::fromUtf8("К").at(0), u"K"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Л").at(0), u"L"_q);
		fastLetterRusEng.insert(QString::fromUtf8("М").at(0), u"M"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Н").at(0), u"N"_q);
		fastLetterRusEng.insert(QString::fromUtf8("О").at(0), u"O"_q);
		fastLetterRusEng.insert(QString::fromUtf8("П").at(0), u"P"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Р").at(0), u"R"_q);
		fastLetterRusEng.insert(QString::fromUtf8("С").at(0), u"S"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Т").at(0), u"T"_q);
		fastLetterRusEng.insert(QString::fromUtf8("У").at(0), u"U"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ў").at(0), u"W"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ф").at(0), u"F"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Х").at(0), u"Kh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ц").at(0), u"Ts"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ч").at(0), u"Ch"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ш").at(0), u"Sh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Щ").at(0), u"Sch"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Э").at(0), u"E"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ю").at(0), u"Yu"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Я").at(0), u"Ya"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ў").at(0), u"W"_q);
		fastLetterRusEng.insert(QString::fromUtf8("а").at(0), u"a"_q);
		fastLetterRusEng.insert(QString::fromUtf8("б").at(0), u"b"_q);
		fastLetterRusEng.insert(QString::fromUtf8("в").at(0), u"v"_q);
		fastLetterRusEng.insert(QString::fromUtf8("г").at(0), u"g"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ґ").at(0), u"g"_q);
		fastLetterRusEng.insert(QString::fromUtf8("д").at(0), u"d"_q);
		fastLetterRusEng.insert(QString::fromUtf8("е").at(0), u"e"_q);
		fastLetterRusEng.insert(QString::fromUtf8("є").at(0), u"ye"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ё").at(0), u"yo"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ж").at(0), u"zh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("з").at(0), u"z"_q);
		fastLetterRusEng.insert(QString::fromUtf8("й").at(0), u"y"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ї").at(0), u"yi"_q);
		fastLetterRusEng.insert(QString::fromUtf8("і").at(0), u"i"_q);
		fastLetterRusEng.insert(QString::fromUtf8("л").at(0), u"l"_q);
		fastLetterRusEng.insert(QString::fromUtf8("м").at(0), u"m"_q);
		fastLetterRusEng.insert(QString::fromUtf8("н").at(0), u"n"_q);
		fastLetterRusEng.insert(QString::fromUtf8("о").at(0), u"o"_q);
		fastLetterRusEng.insert(QString::fromUtf8("п").at(0), u"p"_q);
		fastLetterRusEng.insert(QString::fromUtf8("р").at(0), u"r"_q);
		fastLetterRusEng.insert(QString::fromUtf8("с").at(0), u"s"_q);
		fastLetterRusEng.insert(QString::fromUtf8("т").at(0), u"t"_q);
		fastLetterRusEng.insert(QString::fromUtf8("у").at(0), u"u"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ў").at(0), u"w"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ф").at(0), u"f"_q);
		fastLetterRusEng.insert(QString::fromUtf8("х").at(0), u"kh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ц").at(0), u"ts"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ч").at(0), u"ch"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ш").at(0), u"sh"_q);
		fastLetterRusEng.insert(QString::fromUtf8("щ").at(0), u"sch"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ъ").at(0), QString());
		fastLetterRusEng.insert(QString::fromUtf8("э").at(0), u"e"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ю").at(0), u"yu"_q);
		fastLetterRusEng.insert(QString::fromUtf8("я").at(0), u"ya"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ў").at(0), u"w"_q);
		fastLetterRusEng.insert(QString::fromUtf8("Ы").at(0), u"Y"_q);
		fastLetterRusEng.insert(QString::fromUtf8("и").at(0), u"i"_q);
		fastLetterRusEng.insert(QString::fromUtf8("к").at(0), u"k"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ы").at(0), u"y"_q);
		fastLetterRusEng.insert(QString::fromUtf8("ь").at(0), QString());
	}
	QHash<QChar, QString>::const_iterator j = fastLetterRusEng.constFind(letter);
	if (j != fastLetterRusEng.cend()) {
		return j.value();
	}
	return QString(1, letter);
}

QString translitRusEng(const QString &rus) {
	if (fastRusEng.isEmpty()) {
		fastRusEng.insert(QString::fromUtf8("Александр"), u"Alexander"_q);
		fastRusEng.insert(QString::fromUtf8("александр"), u"alexander"_q);
		fastRusEng.insert(QString::fromUtf8("Филипп"), u"Philip"_q);
		fastRusEng.insert(QString::fromUtf8("филипп"), u"philip"_q);
		fastRusEng.insert(QString::fromUtf8("Пётр"), u"Petr"_q);
		fastRusEng.insert(QString::fromUtf8("пётр"), u"petr"_q);
		fastRusEng.insert(QString::fromUtf8("Гай"), u"Gai"_q);
		fastRusEng.insert(QString::fromUtf8("гай"), u"gai"_q);
		fastRusEng.insert(QString::fromUtf8("Ильин"), u"Ilyin"_q);
		fastRusEng.insert(QString::fromUtf8("ильин"), u"ilyin"_q);
	}
	QMap<QString, QString>::const_iterator i = fastRusEng.constFind(rus);
	if (i != fastRusEng.cend()) {
		return i.value();
	}

	QString result;
	result.reserve(rus.size() * 2);

	int32 toSkip = 0;
	for (QString::const_iterator i = rus.cbegin(), e = rus.cend(); i != e; i += toSkip) {
		result += translitLetterRusEng(*i, (i + 1 == e) ? ' ' : *(i + 1), toSkip);
	}
	return result;
}

QString engAlphabet = "qwertyuiop[]asdfghjkl;'zxcvbnm,.";
QString engAlphabetUpper = engAlphabet.toUpper();

void initializeKeyboardSwitch() {
	if (fastRusKeyboardSwitch.isEmpty()) {
		QString rusAlphabet = "йцукенгшщзхъфывапролджэячсмитьбю";
		QString rusAlphabetUpper = rusAlphabet.toUpper();
		for (int i = 0; i < rusAlphabet.size(); ++i) {
			fastRusKeyboardSwitch.insert(engAlphabetUpper[i], rusAlphabetUpper[i]);
			fastRusKeyboardSwitch.insert(engAlphabet[i], rusAlphabet[i]);
			fastRusKeyboardSwitch.insert(rusAlphabetUpper[i], engAlphabetUpper[i]);
			fastRusKeyboardSwitch.insert(rusAlphabet[i], engAlphabet[i]);
		}
	}
	if (fastUkrKeyboardSwitch.isEmpty()) {
		QString ukrAlphabet = "йцукенгшщзхїфівапролджєячсмитьбю";
		QString ukrAlphabetUpper = ukrAlphabet.toUpper();
		for (int i = 0; i < ukrAlphabet.size(); ++i) {
			fastUkrKeyboardSwitch.insert(engAlphabetUpper[i], ukrAlphabetUpper[i]);
			fastUkrKeyboardSwitch.insert(engAlphabet[i], ukrAlphabet[i]);
			fastUkrKeyboardSwitch.insert(ukrAlphabetUpper[i], engAlphabetUpper[i]);
			fastUkrKeyboardSwitch.insert(ukrAlphabet[i], engAlphabet[i]);
		}
	}
}

QString switchKeyboardLayout(const QString& from, QHash<QChar, QChar>& keyboardSwitch) {
	QString result;
	result.reserve(from.size());
	for (QString::const_iterator i = from.cbegin(), e = from.cend(); i != e; ++i) {
		QHash<QChar, QChar>::const_iterator j = keyboardSwitch.constFind(*i);
		if (j == keyboardSwitch.cend()) {
			result += *i;
		} else {
			result += j.value();
		}
	}
	return result;
}

QString rusKeyboardLayoutSwitch(const QString& from) {
	initializeKeyboardSwitch();
	QString rus = switchKeyboardLayout(from, fastRusKeyboardSwitch);
	QString ukr = switchKeyboardLayout(from, fastUkrKeyboardSwitch);
	return rus == ukr ? rus : rus + ' ' + ukr;
}
