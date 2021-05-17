/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "packer.h"

#include <QtCore/QtPlugin>

#ifdef Q_OS_MAC
//Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif

bool BetaChannel = false;
quint64 AlphaVersion = 0;
bool OnlyAlphaKey = false;

const char *PublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

const char *PublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";

extern const char *PrivateKey;
extern const char *PrivateBetaKey;
#include "../../../../DesktopPrivate/packer_private.h" // RSA PRIVATE KEYS for update signing
#include "../../../../DesktopPrivate/alpha_private.h" // private key for alpha version file generation

QString countAlphaVersionSignature(quint64 version);

// sha1 hash
typedef unsigned char uchar;
typedef unsigned int uint32;
typedef signed int int32;

namespace{

struct BIODeleter {
	void operator()(BIO *value) {
		BIO_free(value);
	}
};

inline auto makeBIO(const void *buf, int len) {
	return std::unique_ptr<BIO, BIODeleter>{
		BIO_new_mem_buf(buf, len),
	};
}

inline uint32 sha1Shift(uint32 v, uint32 shift) {
	return ((v << shift) | (v >> (32 - shift)));
}

void sha1PartHash(uint32 *sha, uint32 *temp) {
	uint32 a = sha[0], b = sha[1], c = sha[2], d = sha[3], e = sha[4], round = 0;

#define _shiftswap(f, v) { \
		uint32 t = sha1Shift(a, 5) + (f) + e + v + temp[round]; \
		e = d; \
		d = c; \
		c = sha1Shift(b, 30); \
		b = a; \
		a = t; \
		++round; \
	}

#define _shiftshiftswap(f, v) { \
		temp[round] = sha1Shift((temp[round - 3] ^ temp[round - 8] ^ temp[round - 14] ^ temp[round - 16]), 1); \
		_shiftswap(f, v) \
	}

	while (round < 16) _shiftswap((b & c) | (~b & d), 0x5a827999)
	while (round < 20) _shiftshiftswap((b & c) | (~b & d), 0x5a827999)
	while (round < 40) _shiftshiftswap(b ^ c ^ d, 0x6ed9eba1)
	while (round < 60) _shiftshiftswap((b & c) | (b & d) | (c & d), 0x8f1bbcdc)
	while (round < 80) _shiftshiftswap(b ^ c ^ d, 0xca62c1d6)

#undef _shiftshiftswap
#undef _shiftswap

	sha[0] += a;
	sha[1] += b;
	sha[2] += c;
	sha[3] += d;
	sha[4] += e;
}

} // namespace

int32 *hashSha1(const void *data, uint32 len, void *dest) {
	const uchar *buf = (const uchar *)data;

	uint32 temp[80], block = 0, end;
	uint32 sha[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
	for (end = block + 64; block + 64 <= len; end = block + 64) {
		for (uint32 i = 0; block < end; block += 4) {
			temp[i++] = (uint32) buf[block + 3]
			        | (((uint32) buf[block + 2]) << 8)
			        | (((uint32) buf[block + 1]) << 16)
			        | (((uint32) buf[block]) << 24);
		}
		sha1PartHash(sha, temp);
	}

	end = len - block;
	memset(temp, 0, sizeof(uint32) * 16);
	uint32 last = 0;
	for (; last < end; ++last) {
		temp[last >> 2] |= (uint32)buf[last + block] << ((3 - (last & 0x03)) << 3);
	}
	temp[last >> 2] |= 0x80 << ((3 - (last & 3)) << 3);
	if (end >= 56) {
		sha1PartHash(sha, temp);
		memset(temp, 0, sizeof(uint32) * 16);
	}
	temp[15] = len << 3;
	sha1PartHash(sha, temp);

	uchar *sha1To = (uchar*)dest;

	for (int32 i = 19; i >= 0; --i) {
		sha1To[i] = (sha[i >> 2] >> (((3 - i) & 0x03) << 3)) & 0xFF;
	}

	return (int32*)sha1To;
}

QString AlphaSignature;

int writeAlphaKey() {
	if (!AlphaVersion) {
		return 0;
	}
	QString keyName(QString("talpha_%1_key").arg(AlphaVersion));
	QFile key(keyName);
	if (!key.open(QIODevice::WriteOnly)) {
		cout << "Can't open '" << keyName.toUtf8().constData() << "' for write..\n";
		return -1;
	}
	key.write(AlphaSignature.toUtf8());
	key.close();
	return 0;
}

int main(int argc, char *argv[])
{
	QString workDir;

	QString remove;
	int version = 0;
	bool targetosx = false;
	bool targetwin64 = false;
	QFileInfoList files;
	for (int i = 0; i < argc; ++i) {
		if (string("-path") == argv[i] && i + 1 < argc) {
			QString path = workDir + QString(argv[i + 1]);
			QFileInfo info(path);
			files.push_back(info);
			if (remove.isEmpty()) remove = info.canonicalPath() + "/";
		} else if (string("-target") == argv[i] && i + 1 < argc) {
			targetosx = (string("osx") == argv[i + 1]);
			targetwin64 = (string("win64") == argv[i + 1]);
		} else if (string("-version") == argv[i] && i + 1 < argc) {
			version = QString(argv[i + 1]).toInt();
		} else if (string("-beta") == argv[i]) {
			BetaChannel = true;
		} else if (string("-alphakey") == argv[i]) {
			OnlyAlphaKey = true;
		} else if (string("-alpha") == argv[i] && i + 1 < argc) {
			AlphaVersion = QString(argv[i + 1]).toULongLong();
			if (AlphaVersion > version * 1000ULL && AlphaVersion < (version + 1) * 1000ULL) {
				BetaChannel = false;
				AlphaSignature = countAlphaVersionSignature(AlphaVersion);
				if (AlphaSignature.isEmpty()) {
					return -1;
				}
			} else {
				cout << "Bad -alpha param value passed, should be for the same version: " << version << ", alpha: " << AlphaVersion << "\n";
				return -1;
			}
		}
	}
	if (OnlyAlphaKey) {
		return writeAlphaKey();
	}

	if (files.isEmpty() || remove.isEmpty() || version <= 1016 || version > 999999999) {
#ifdef Q_OS_WIN
		cout << "Usage: Packer.exe -path {file} -version {version} OR Packer.exe -path {dir} -version {version}\n";
#elif defined Q_OS_MAC
		cout << "Usage: Packer.app -path {file} -version {version} OR Packer.app -path {dir} -version {version}\n";
#else
		cout << "Usage: Packer -path {file} -version {version} OR Packer -path {dir} -version {version}\n";
#endif
		return -1;
	}

	bool hasDirs = true;
	while (hasDirs) {
		hasDirs = false;
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			QString fullPath = info.canonicalFilePath();
			if (info.isDir()) {
				hasDirs = true;
				files.erase(i);
				QDir d = QDir(info.absoluteFilePath());
				QString fullDir = d.canonicalPath();
				QStringList entries = d.entryList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
				files.append(d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot));
				break;
			} else if (!info.isReadable()) {
				cout << "Can't read: " << info.absoluteFilePath().toUtf8().constData() << "\n";
				return -1;
			} else if (info.isHidden()) {
				hasDirs = true;
				files.erase(i);
				break;
			}
		}
	}
	for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
		QFileInfo info(*i);
		if (!info.canonicalFilePath().startsWith(remove)) {
			cout << "Can't find '" << remove.toUtf8().constData() << "' in file '" << info.canonicalFilePath().toUtf8().constData() << "' :(\n";
			return -1;
		}
	}

	QByteArray result;
	{
		QBuffer buffer(&result);
		buffer.open(QIODevice::WriteOnly);
		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);

		if (AlphaVersion) {
			stream << quint32(0x7FFFFFFF);
			stream << quint64(AlphaVersion);
		} else {
			stream << quint32(version);
		}

		stream << quint32(files.size());
		cout << "Found " << files.size() << " file" << (files.size() == 1 ? "" : "s") << "..\n";
		for (QFileInfoList::iterator i = files.begin(); i != files.end(); ++i) {
			QFileInfo info(*i);
			QString fullName = info.canonicalFilePath();
			QString name = fullName.mid(remove.length());
			cout << name.toUtf8().constData() << " (" << info.size() << ")\n";

			QFile f(fullName);
			if (!f.open(QIODevice::ReadOnly)) {
				cout << "Can't open '" << fullName.toUtf8().constData() << "' for read..\n";
				return -1;
			}
			QByteArray inner = f.readAll();
			stream << name << quint32(inner.size()) << inner;
#ifdef Q_OS_UNIX
			stream << (QFileInfo(fullName).isExecutable() ? true : false);
#endif
		}
		if (stream.status() != QDataStream::Ok) {
			cout << "Stream status is bad: " << stream.status() << "\n";
			return -1;
		}
	}

	int32 resultSize = result.size();
	cout << "Compression start, size: " << resultSize << "\n";

	QByteArray compressed, resultCheck;
#if defined Q_OS_WIN && !defined DESKTOP_APP_USE_PACKAGED // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header

	compressed.resize(hSize + resultSize + 1024 * 1024); // rsa signature + sha1 + lzma props + max compressed size

	size_t compressedLen = compressed.size() - hSize;
	size_t outPropsSize = LZMA_PROPS_SIZE;
	uchar *_dest = (uchar*)(compressed.data() + hSize);
	size_t *_destLen = &compressedLen;
	const uchar *_src = (const uchar*)(result.constData());
	size_t _srcLen = result.size();
	uchar *_outProps = (uchar*)(compressed.data() + hSigLen + hShaLen);
	int res = LzmaCompress(_dest, _destLen, _src, _srcLen, _outProps, &outPropsSize, 9, 64 * 1024 * 1024, 4, 0, 2, 273, 2);
	if (res != SZ_OK) {
		cout << "Error in compression: " << res << "\n";
		return -1;
	}
	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data() + hSigLen + hShaLen + hPropsLen, &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	int32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)resultCheck.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		cout << "Uncompress failed: " << uncompressRes << "\n";
		return -1;
	}
	if (resultLen != size_t(result.size())) {
		cout << "Uncompress bad size: " << resultLen << ", was: " << result.size() << "\n";
		return -1;
	}
#else // use liblzma for others
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header

	compressed.resize(hSize + resultSize + 1024 * 1024); // rsa signature + sha1 + lzma props + max compressed size

	size_t compressedLen = compressed.size() - hSize;

	lzma_stream stream = LZMA_STREAM_INIT;

	int preset = 9 | LZMA_PRESET_EXTREME;
	lzma_ret ret = lzma_easy_encoder(&stream, preset, LZMA_CHECK_CRC64);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the encoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = resultSize;
	stream.next_in = (uint8_t*)result.constData();
	stream.avail_out = compressedLen;
	stream.next_out = (uint8_t*)(compressed.data() + hSize);

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	compressedLen -= stream.avail_out;
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_DATA_ERROR: msg = "File size limits exceeded"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in compression: " << msg << " (error code " << res << ")\n";
		return -1;
	}

	compressed.resize(int(hSize + compressedLen));
	memcpy(compressed.data() + hSigLen + hShaLen, &resultSize, hOriginalSizeLen);

	cout << "Compressed to size: " << compressedLen << "\n";

	cout << "Checking uncompressed..\n";

	int32 resultCheckLen;
	memcpy(&resultCheckLen, compressed.constData() + hSigLen + hShaLen, hOriginalSizeLen);
	if (resultCheckLen <= 0 || resultCheckLen > 1024 * 1024 * 1024) {
		cout << "Bad result len: " << resultCheckLen << "\n";
		return -1;
	}
	resultCheck.resize(resultCheckLen);

	size_t resultLen = resultCheck.size();

	stream = LZMA_STREAM_INIT;

	ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
			case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error initializing the decoder: " << msg << " (error code " << ret << ")\n";
		return -1;
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)resultCheck.data();

	res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		cout << "Error in decompression, " << stream.avail_in << " bytes left in _in of " << compressedLen << " whole.\n";
		return -1;
	} else if (stream.avail_out) {
		cout << "Error in decompression, " << stream.avail_out << " bytes free left in _out of " << resultLen << " whole.\n";
		return -1;
	}
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
			case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
			case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
			case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
			case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
			case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
			default: msg = "Unknown error, possibly a bug"; break;
		}
		cout << "Error in decompression: " << msg << " (error code " << res << ")\n";
		return -1;
	}
#endif
	if (memcmp(result.constData(), resultCheck.constData(), resultLen)) {
		cout << "Data differ :(\n";
		return -1;
	}
	/**/
	result = resultCheck = QByteArray();

	cout << "Counting SHA1 hash..\n";

	uchar sha1Buffer[20];
	memcpy(compressed.data() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, uint32(compressedLen + hPropsLen + hOriginalSizeLen), sha1Buffer), hShaLen); // count sha1

	uint32 siglen = 0;

	cout << "Signing..\n";
	RSA *prKey = [] {
		const auto bio = makeBIO(
			const_cast<char*>(
				(BetaChannel || AlphaVersion)
					? PrivateBetaKey
					: PrivateKey),
			-1);
		return PEM_read_bio_RSAPrivateKey(bio.get(), 0, 0, 0);
	}();
	if (!prKey) {
		cout << "Could not read RSA private key!\n";
		return -1;
	}
	if (RSA_size(prKey) != hSigLen) {
		cout << "Bad private key, size: " << RSA_size(prKey) << "\n";
		RSA_free(prKey);
		return -1;
	}
	if (RSA_sign(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (uchar*)(compressed.data()), &siglen, prKey) != 1) { // count signature
		cout << "Signing failed!\n";
		RSA_free(prKey);
		return -1;
	}
	RSA_free(prKey);

	if (siglen != hSigLen) {
		cout << "Bad signature length: " << siglen << "\n";
		return -1;
	}

	cout << "Checking signature..\n";
	RSA *pbKey = [] {
		const auto bio = makeBIO(
			const_cast<char*>(
				(BetaChannel || AlphaVersion)
					? PublicBetaKey
					: PublicKey),
			-1);
		return PEM_read_bio_RSAPublicKey(bio.get(), 0, 0, 0);
	}();
	if (!pbKey) {
		cout << "Could not read RSA public key!\n";
		return -1;
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), siglen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		cout << "Signature verification failed!\n";
		return -1;
	}
	cout << "Signature verified!\n";
	RSA_free(pbKey);
#ifdef Q_OS_WIN
	QString outName((targetwin64 ? QString("tx64upd%1") : QString("tupdate%1")).arg(AlphaVersion ? AlphaVersion : version));
#elif defined Q_OS_MAC
	QString outName((targetosx ? QString("tosxupd%1") : QString("tmacupd%1")).arg(AlphaVersion ? AlphaVersion : version));
#elif defined Q_OS_UNIX
#ifndef _LP64
	QString outName(QString("tlinux32upd%1").arg(AlphaVersion ? AlphaVersion : version));
#else
	QString outName(QString("tlinuxupd%1").arg(AlphaVersion ? AlphaVersion : version));
#endif
#else
#error Unknown platform!
#endif
	if (AlphaVersion) {
		outName += "_" + AlphaSignature;
	}
	QFile out(outName);
	if (!out.open(QIODevice::WriteOnly)) {
		cout << "Can't open '" << outName.toUtf8().constData() << "' for write..\n";
		return -1;
	}
	out.write(compressed);
	out.close();

	cout << "Update file '" << outName.toUtf8().constData() << "' written successfully!\n";

	return writeAlphaKey();
}

QString countAlphaVersionSignature(quint64 version) { // duplicated in autoupdater.cpp
	QByteArray cAlphaPrivateKey(AlphaPrivateKey);
	if (cAlphaPrivateKey.isEmpty()) {
		cout << "Error: Trying to count alpha version signature without alpha private key!\n";
		return QString();
	}

	QByteArray signedData = (QLatin1String("TelegramBeta_") + QString::number(version, 16).toLower()).toUtf8();

	static const int32 shaSize = 20, keySize = 128;

	uchar sha1Buffer[shaSize];
	hashSha1(signedData.constData(), signedData.size(), sha1Buffer); // count sha1

	uint32 siglen = 0;

	RSA *prKey = [&] {
		const auto bio = makeBIO(
			const_cast<char*>(cAlphaPrivateKey.constData()),
			-1);
		return PEM_read_bio_RSAPrivateKey(bio.get(), 0, 0, 0);
	}();
	if (!prKey) {
		cout << "Error: Could not read alpha private key!\n";
		return QString();
	}
	if (RSA_size(prKey) != keySize) {
		cout << "Error: Bad alpha private key size: " << RSA_size(prKey) << "\n";
		RSA_free(prKey);
		return QString();
	}
	QByteArray signature;
	signature.resize(keySize);
	if (RSA_sign(NID_sha1, (const uchar*)(sha1Buffer), shaSize, (uchar*)(signature.data()), &siglen, prKey) != 1) { // count signature
		cout << "Error: Counting alpha version signature failed!\n";
		RSA_free(prKey);
		return QString();
	}
	RSA_free(prKey);

	if (siglen != keySize) {
		cout << "Error: Bad alpha version signature length: " << siglen << "\n";
		return QString();
	}

	signature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	signature = signature.replace('-', '8').replace('_', 'B');
	return QString::fromUtf8(signature.mid(19, 32));
}
