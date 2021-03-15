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

#include <QtNetwork/QSslSocket>

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

struct CRYPTO_dynlock_value {
	QMutex mutex;
};

namespace {
	bool _sslInited = false;
	QMutex *_sslLocks = nullptr;
	void _sslLockingCallback(int mode, int type, const char *file, int line) {
		if (!_sslLocks) return; // not inited

		if (mode & CRYPTO_LOCK) {
			_sslLocks[type].lock();
		} else {
			_sslLocks[type].unlock();
		}
	}
	void _sslThreadId(CRYPTO_THREADID *id) {
		CRYPTO_THREADID_set_pointer(id, QThread::currentThreadId());
	}
	CRYPTO_dynlock_value *_sslCreateFunction(const char *file, int line) {
		return new CRYPTO_dynlock_value();
	}
	void _sslLockFunction(int mode, CRYPTO_dynlock_value *l, const char *file, int line) {
		if (mode & CRYPTO_LOCK) {
			l->mutex.lock();
		} else {
			l->mutex.unlock();
		}
	}
	void _sslDestroyFunction(CRYPTO_dynlock_value *l, const char *file, int line) {
		delete l;
	}

	int _ffmpegLockManager(void **mutex, AVLockOp op) {
		switch (op) {
		case AV_LOCK_CREATE: {
			Assert(*mutex == nullptr);
			*mutex = reinterpret_cast<void*>(new QMutex());
		} break;

		case AV_LOCK_OBTAIN: {
			Assert(*mutex != nullptr);
			reinterpret_cast<QMutex*>(*mutex)->lock();
		} break;

		case AV_LOCK_RELEASE: {
			Assert(*mutex != nullptr);
			reinterpret_cast<QMutex*>(*mutex)->unlock();
		}; break;

		case AV_LOCK_DESTROY: {
			Assert(*mutex != nullptr);
			delete reinterpret_cast<QMutex*>(*mutex);
			*mutex = nullptr;
		} break;
		}
		return 0;
	}
}

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

		// Force OpenSSL loading if it is linked in Qt,
		// so that we won't mess with our OpenSSL locking with Qt OpenSSL locking.
		auto sslSupported = QSslSocket::supportsSsl();
		if (!sslSupported) {
			LOG(("Error: current Qt build doesn't support SSL requests."));
		}
		if (!CRYPTO_get_locking_callback()) {
			// Qt didn't initialize OpenSSL, so we will.
			auto numLocks = CRYPTO_num_locks();
			if (numLocks) {
				_sslLocks = new QMutex[numLocks];
				CRYPTO_set_locking_callback(_sslLockingCallback);
			} else {
				LOG(("MTP Error: Could not init OpenSSL threads, CRYPTO_num_locks() returned zero!"));
			}
		}
		if (!CRYPTO_get_dynlock_create_callback()) {
			CRYPTO_set_dynlock_create_callback(_sslCreateFunction);
			CRYPTO_set_dynlock_lock_callback(_sslLockFunction);
			CRYPTO_set_dynlock_destroy_callback(_sslDestroyFunction);
		} else if (!CRYPTO_get_dynlock_lock_callback()) {
			LOG(("MTP Error: dynlock_create callback is set without dynlock_lock callback!"));
		}

		_sslInited = true;
	}

	void finish() {
		CRYPTO_cleanup_all_ex_data();
#ifndef LIBRESSL_VERSION_NUMBER
		FIPS_mode_set(0);
#endif
		ENGINE_cleanup();
		CONF_modules_unload(1);
		ERR_free_strings();
		EVP_cleanup();

		delete[] base::take(_sslLocks);

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

void memset_rand(void *data, uint32 len) {
	Assert(_sslInited);
	RAND_bytes((uchar*)data, len);
}

namespace {
	QMap<QString, QString> fastRusEng;
	QHash<QChar, QString> fastLetterRusEng;
	QMap<uint32, QString> fastDoubleLetterRusEng;
	QHash<QChar, QChar> fastRusKeyboardSwitch;
}

QString translitLetterRusEng(QChar letter, QChar next, int32 &toSkip) {
	if (fastDoubleLetterRusEng.isEmpty()) {
		fastDoubleLetterRusEng.insert((QString::fromUtf8("Ы").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), qsl("Y"));
		fastDoubleLetterRusEng.insert((QString::fromUtf8("и").at(0).unicode() << 16) | QString::fromUtf8("я").at(0).unicode(), qsl("ia"));
		fastDoubleLetterRusEng.insert((QString::fromUtf8("и").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), qsl("y"));
		fastDoubleLetterRusEng.insert((QString::fromUtf8("к").at(0).unicode() << 16) | QString::fromUtf8("с").at(0).unicode(), qsl("x"));
		fastDoubleLetterRusEng.insert((QString::fromUtf8("ы").at(0).unicode() << 16) | QString::fromUtf8("й").at(0).unicode(), qsl("y"));
		fastDoubleLetterRusEng.insert((QString::fromUtf8("ь").at(0).unicode() << 16) | QString::fromUtf8("е").at(0).unicode(), qsl("ye"));
	}
	QMap<uint32, QString>::const_iterator i = fastDoubleLetterRusEng.constFind((letter.unicode() << 16) | next.unicode());
	if (i != fastDoubleLetterRusEng.cend()) {
		toSkip = 2;
		return i.value();
	}

	toSkip = 1;
	if (fastLetterRusEng.isEmpty()) {
		fastLetterRusEng.insert(QString::fromUtf8("А").at(0), qsl("A"));
		fastLetterRusEng.insert(QString::fromUtf8("Б").at(0), qsl("B"));
		fastLetterRusEng.insert(QString::fromUtf8("В").at(0), qsl("V"));
		fastLetterRusEng.insert(QString::fromUtf8("Г").at(0), qsl("G"));
		fastLetterRusEng.insert(QString::fromUtf8("Ґ").at(0), qsl("G"));
		fastLetterRusEng.insert(QString::fromUtf8("Д").at(0), qsl("D"));
		fastLetterRusEng.insert(QString::fromUtf8("Е").at(0), qsl("E"));
		fastLetterRusEng.insert(QString::fromUtf8("Є").at(0), qsl("Ye"));
		fastLetterRusEng.insert(QString::fromUtf8("Ё").at(0), qsl("Yo"));
		fastLetterRusEng.insert(QString::fromUtf8("Ж").at(0), qsl("Zh"));
		fastLetterRusEng.insert(QString::fromUtf8("З").at(0), qsl("Z"));
		fastLetterRusEng.insert(QString::fromUtf8("И").at(0), qsl("I"));
		fastLetterRusEng.insert(QString::fromUtf8("Ї").at(0), qsl("Yi"));
		fastLetterRusEng.insert(QString::fromUtf8("І").at(0), qsl("I"));
		fastLetterRusEng.insert(QString::fromUtf8("Й").at(0), qsl("J"));
		fastLetterRusEng.insert(QString::fromUtf8("К").at(0), qsl("K"));
		fastLetterRusEng.insert(QString::fromUtf8("Л").at(0), qsl("L"));
		fastLetterRusEng.insert(QString::fromUtf8("М").at(0), qsl("M"));
		fastLetterRusEng.insert(QString::fromUtf8("Н").at(0), qsl("N"));
		fastLetterRusEng.insert(QString::fromUtf8("О").at(0), qsl("O"));
		fastLetterRusEng.insert(QString::fromUtf8("П").at(0), qsl("P"));
		fastLetterRusEng.insert(QString::fromUtf8("Р").at(0), qsl("R"));
		fastLetterRusEng.insert(QString::fromUtf8("С").at(0), qsl("S"));
		fastLetterRusEng.insert(QString::fromUtf8("Т").at(0), qsl("T"));
		fastLetterRusEng.insert(QString::fromUtf8("У").at(0), qsl("U"));
		fastLetterRusEng.insert(QString::fromUtf8("Ў").at(0), qsl("W"));
		fastLetterRusEng.insert(QString::fromUtf8("Ф").at(0), qsl("F"));
		fastLetterRusEng.insert(QString::fromUtf8("Х").at(0), qsl("Kh"));
		fastLetterRusEng.insert(QString::fromUtf8("Ц").at(0), qsl("Ts"));
		fastLetterRusEng.insert(QString::fromUtf8("Ч").at(0), qsl("Ch"));
		fastLetterRusEng.insert(QString::fromUtf8("Ш").at(0), qsl("Sh"));
		fastLetterRusEng.insert(QString::fromUtf8("Щ").at(0), qsl("Sch"));
		fastLetterRusEng.insert(QString::fromUtf8("Э").at(0), qsl("E"));
		fastLetterRusEng.insert(QString::fromUtf8("Ю").at(0), qsl("Yu"));
		fastLetterRusEng.insert(QString::fromUtf8("Я").at(0), qsl("Ya"));
		fastLetterRusEng.insert(QString::fromUtf8("Ў").at(0), qsl("W"));
		fastLetterRusEng.insert(QString::fromUtf8("а").at(0), qsl("a"));
		fastLetterRusEng.insert(QString::fromUtf8("б").at(0), qsl("b"));
		fastLetterRusEng.insert(QString::fromUtf8("в").at(0), qsl("v"));
		fastLetterRusEng.insert(QString::fromUtf8("г").at(0), qsl("g"));
		fastLetterRusEng.insert(QString::fromUtf8("ґ").at(0), qsl("g"));
		fastLetterRusEng.insert(QString::fromUtf8("д").at(0), qsl("d"));
		fastLetterRusEng.insert(QString::fromUtf8("е").at(0), qsl("e"));
		fastLetterRusEng.insert(QString::fromUtf8("є").at(0), qsl("ye"));
		fastLetterRusEng.insert(QString::fromUtf8("ё").at(0), qsl("yo"));
		fastLetterRusEng.insert(QString::fromUtf8("ж").at(0), qsl("zh"));
		fastLetterRusEng.insert(QString::fromUtf8("з").at(0), qsl("z"));
		fastLetterRusEng.insert(QString::fromUtf8("й").at(0), qsl("y"));
		fastLetterRusEng.insert(QString::fromUtf8("ї").at(0), qsl("yi"));
		fastLetterRusEng.insert(QString::fromUtf8("і").at(0), qsl("i"));
		fastLetterRusEng.insert(QString::fromUtf8("л").at(0), qsl("l"));
		fastLetterRusEng.insert(QString::fromUtf8("м").at(0), qsl("m"));
		fastLetterRusEng.insert(QString::fromUtf8("н").at(0), qsl("n"));
		fastLetterRusEng.insert(QString::fromUtf8("о").at(0), qsl("o"));
		fastLetterRusEng.insert(QString::fromUtf8("п").at(0), qsl("p"));
		fastLetterRusEng.insert(QString::fromUtf8("р").at(0), qsl("r"));
		fastLetterRusEng.insert(QString::fromUtf8("с").at(0), qsl("s"));
		fastLetterRusEng.insert(QString::fromUtf8("т").at(0), qsl("t"));
		fastLetterRusEng.insert(QString::fromUtf8("у").at(0), qsl("u"));
		fastLetterRusEng.insert(QString::fromUtf8("ў").at(0), qsl("w"));
		fastLetterRusEng.insert(QString::fromUtf8("ф").at(0), qsl("f"));
		fastLetterRusEng.insert(QString::fromUtf8("х").at(0), qsl("kh"));
		fastLetterRusEng.insert(QString::fromUtf8("ц").at(0), qsl("ts"));
		fastLetterRusEng.insert(QString::fromUtf8("ч").at(0), qsl("ch"));
		fastLetterRusEng.insert(QString::fromUtf8("ш").at(0), qsl("sh"));
		fastLetterRusEng.insert(QString::fromUtf8("щ").at(0), qsl("sch"));
		fastLetterRusEng.insert(QString::fromUtf8("ъ").at(0), QString());
		fastLetterRusEng.insert(QString::fromUtf8("э").at(0), qsl("e"));
		fastLetterRusEng.insert(QString::fromUtf8("ю").at(0), qsl("yu"));
		fastLetterRusEng.insert(QString::fromUtf8("я").at(0), qsl("ya"));
		fastLetterRusEng.insert(QString::fromUtf8("ў").at(0), qsl("w"));
		fastLetterRusEng.insert(QString::fromUtf8("Ы").at(0), qsl("Y"));
		fastLetterRusEng.insert(QString::fromUtf8("и").at(0), qsl("i"));
		fastLetterRusEng.insert(QString::fromUtf8("к").at(0), qsl("k"));
		fastLetterRusEng.insert(QString::fromUtf8("ы").at(0), qsl("y"));
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
		fastRusEng.insert(QString::fromUtf8("Александр"), qsl("Alexander"));
		fastRusEng.insert(QString::fromUtf8("александр"), qsl("alexander"));
		fastRusEng.insert(QString::fromUtf8("Филипп"), qsl("Philip"));
		fastRusEng.insert(QString::fromUtf8("филипп"), qsl("philip"));
		fastRusEng.insert(QString::fromUtf8("Пётр"), qsl("Petr"));
		fastRusEng.insert(QString::fromUtf8("пётр"), qsl("petr"));
		fastRusEng.insert(QString::fromUtf8("Гай"), qsl("Gai"));
		fastRusEng.insert(QString::fromUtf8("гай"), qsl("gai"));
		fastRusEng.insert(QString::fromUtf8("Ильин"), qsl("Ilyin"));
		fastRusEng.insert(QString::fromUtf8("ильин"), qsl("ilyin"));
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

QString rusKeyboardLayoutSwitch(const QString &from) {
	if (fastRusKeyboardSwitch.isEmpty()) {
		fastRusKeyboardSwitch.insert('Q', QString::fromUtf8("Й").at(0));
		fastRusKeyboardSwitch.insert('W', QString::fromUtf8("Ц").at(0));
		fastRusKeyboardSwitch.insert('E', QString::fromUtf8("У").at(0));
		fastRusKeyboardSwitch.insert('R', QString::fromUtf8("К").at(0));
		fastRusKeyboardSwitch.insert('T', QString::fromUtf8("Е").at(0));
		fastRusKeyboardSwitch.insert('Y', QString::fromUtf8("Н").at(0));
		fastRusKeyboardSwitch.insert('U', QString::fromUtf8("Г").at(0));
		fastRusKeyboardSwitch.insert('I', QString::fromUtf8("Ш").at(0));
		fastRusKeyboardSwitch.insert('O', QString::fromUtf8("Щ").at(0));
		fastRusKeyboardSwitch.insert('P', QString::fromUtf8("З").at(0));
		fastRusKeyboardSwitch.insert('{', QString::fromUtf8("Х").at(0));
		fastRusKeyboardSwitch.insert('}', QString::fromUtf8("Ъ").at(0));
		fastRusKeyboardSwitch.insert('A', QString::fromUtf8("Ф").at(0));
		fastRusKeyboardSwitch.insert('S', QString::fromUtf8("Ы").at(0));
		fastRusKeyboardSwitch.insert('D', QString::fromUtf8("В").at(0));
		fastRusKeyboardSwitch.insert('F', QString::fromUtf8("А").at(0));
		fastRusKeyboardSwitch.insert('G', QString::fromUtf8("П").at(0));
		fastRusKeyboardSwitch.insert('H', QString::fromUtf8("Р").at(0));
		fastRusKeyboardSwitch.insert('J', QString::fromUtf8("О").at(0));
		fastRusKeyboardSwitch.insert('K', QString::fromUtf8("Л").at(0));
		fastRusKeyboardSwitch.insert('L', QString::fromUtf8("Д").at(0));
		fastRusKeyboardSwitch.insert(':', QString::fromUtf8("Ж").at(0));
		fastRusKeyboardSwitch.insert('"', QString::fromUtf8("Э").at(0));
		fastRusKeyboardSwitch.insert('Z', QString::fromUtf8("Я").at(0));
		fastRusKeyboardSwitch.insert('X', QString::fromUtf8("Ч").at(0));
		fastRusKeyboardSwitch.insert('C', QString::fromUtf8("С").at(0));
		fastRusKeyboardSwitch.insert('V', QString::fromUtf8("М").at(0));
		fastRusKeyboardSwitch.insert('B', QString::fromUtf8("И").at(0));
		fastRusKeyboardSwitch.insert('N', QString::fromUtf8("Т").at(0));
		fastRusKeyboardSwitch.insert('M', QString::fromUtf8("Ь").at(0));
		fastRusKeyboardSwitch.insert('<', QString::fromUtf8("Б").at(0));
		fastRusKeyboardSwitch.insert('>', QString::fromUtf8("Ю").at(0));
		fastRusKeyboardSwitch.insert('q', QString::fromUtf8("й").at(0));
		fastRusKeyboardSwitch.insert('w', QString::fromUtf8("ц").at(0));
		fastRusKeyboardSwitch.insert('e', QString::fromUtf8("у").at(0));
		fastRusKeyboardSwitch.insert('r', QString::fromUtf8("к").at(0));
		fastRusKeyboardSwitch.insert('t', QString::fromUtf8("е").at(0));
		fastRusKeyboardSwitch.insert('y', QString::fromUtf8("н").at(0));
		fastRusKeyboardSwitch.insert('u', QString::fromUtf8("г").at(0));
		fastRusKeyboardSwitch.insert('i', QString::fromUtf8("ш").at(0));
		fastRusKeyboardSwitch.insert('o', QString::fromUtf8("щ").at(0));
		fastRusKeyboardSwitch.insert('p', QString::fromUtf8("з").at(0));
		fastRusKeyboardSwitch.insert('[', QString::fromUtf8("х").at(0));
		fastRusKeyboardSwitch.insert(']', QString::fromUtf8("ъ").at(0));
		fastRusKeyboardSwitch.insert('a', QString::fromUtf8("ф").at(0));
		fastRusKeyboardSwitch.insert('s', QString::fromUtf8("ы").at(0));
		fastRusKeyboardSwitch.insert('d', QString::fromUtf8("в").at(0));
		fastRusKeyboardSwitch.insert('f', QString::fromUtf8("а").at(0));
		fastRusKeyboardSwitch.insert('g', QString::fromUtf8("п").at(0));
		fastRusKeyboardSwitch.insert('h', QString::fromUtf8("р").at(0));
		fastRusKeyboardSwitch.insert('j', QString::fromUtf8("о").at(0));
		fastRusKeyboardSwitch.insert('k', QString::fromUtf8("л").at(0));
		fastRusKeyboardSwitch.insert('l', QString::fromUtf8("д").at(0));
		fastRusKeyboardSwitch.insert(';', QString::fromUtf8("ж").at(0));
		fastRusKeyboardSwitch.insert('\'', QString::fromUtf8("э").at(0));
		fastRusKeyboardSwitch.insert('z', QString::fromUtf8("я").at(0));
		fastRusKeyboardSwitch.insert('x', QString::fromUtf8("ч").at(0));
		fastRusKeyboardSwitch.insert('c', QString::fromUtf8("с").at(0));
		fastRusKeyboardSwitch.insert('v', QString::fromUtf8("м").at(0));
		fastRusKeyboardSwitch.insert('b', QString::fromUtf8("и").at(0));
		fastRusKeyboardSwitch.insert('n', QString::fromUtf8("т").at(0));
		fastRusKeyboardSwitch.insert('m', QString::fromUtf8("ь").at(0));
		fastRusKeyboardSwitch.insert(',', QString::fromUtf8("б").at(0));
		fastRusKeyboardSwitch.insert('.', QString::fromUtf8("ю").at(0));
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Й").at(0), 'Q');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ц").at(0), 'W');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("У").at(0), 'E');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("К").at(0), 'R');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Е").at(0), 'T');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Н").at(0), 'Y');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Г").at(0), 'U');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ш").at(0), 'I');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Щ").at(0), 'O');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("З").at(0), 'P');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Х").at(0), '{');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ъ").at(0), '}');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ф").at(0), 'A');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ы").at(0), 'S');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("В").at(0), 'D');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("А").at(0), 'F');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("П").at(0), 'G');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Р").at(0), 'H');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("О").at(0), 'J');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Л").at(0), 'K');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Д").at(0), 'L');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ж").at(0), ':');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Э").at(0), '"');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Я").at(0), 'Z');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ч").at(0), 'X');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("С").at(0), 'C');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("М").at(0), 'V');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("И").at(0), 'B');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Т").at(0), 'N');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ь").at(0), 'M');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Б").at(0), '<');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ю").at(0), '>');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("й").at(0), 'q');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ц").at(0), 'w');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("у").at(0), 'e');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("к").at(0), 'r');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("е").at(0), 't');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("н").at(0), 'y');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("г").at(0), 'u');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ш").at(0), 'i');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("щ").at(0), 'o');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("з").at(0), 'p');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("х").at(0), '[');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ъ").at(0), ']');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ф").at(0), 'a');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ы").at(0), 's');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("в").at(0), 'd');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("а").at(0), 'f');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("п").at(0), 'g');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("р").at(0), 'h');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("о").at(0), 'j');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("л").at(0), 'k');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("д").at(0), 'l');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ж").at(0), ';');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("э").at(0), '\'');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("я").at(0), 'z');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ч").at(0), 'x');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("с").at(0), 'c');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("м").at(0), 'v');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("и").at(0), 'b');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("т").at(0), 'n');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ь").at(0), 'm');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("б").at(0), ',');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ю").at(0), '.');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("І").at(0), 'S');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("і").at(0), 's');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("Ї").at(0), ']');
		fastRusKeyboardSwitch.insert(QString::fromUtf8("ї").at(0), ']');
	}

	QString result;
	result.reserve(from.size());
	for (QString::const_iterator i = from.cbegin(), e = from.cend(); i != e; ++i) {
		QHash<QChar, QChar>::const_iterator j = fastRusKeyboardSwitch.constFind(*i);
		if (j == fastRusKeyboardSwitch.cend()) {
			result += *i;
		} else {
			result += j.value();
		}
	}
	return result;
}
