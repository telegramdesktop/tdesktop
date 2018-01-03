/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "autoupdater.h"

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#ifdef Q_OS_WIN // use Lzma SDK for win
#include <LzmaLib.h>
#else // Q_OS_WIN
#include <lzma.h>
#endif // else of Q_OS_WIN

#include "application.h"
#include "platform/platform_specific.h"

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

#ifdef Q_OS_WIN
typedef DWORD VerInt;
typedef WCHAR VerChar;
#else // Q_OS_WIN
typedef int VerInt;
typedef wchar_t VerChar;
#endif // Q_OS_WIN

UpdateChecker::UpdateChecker(QThread *thread, const QString &url) : reply(0), already(0), full(0) {
	updateUrl = url;
	moveToThread(thread);
	manager.moveToThread(thread);
	App::setProxySettings(manager);

	connect(thread, SIGNAL(started()), this, SLOT(start()));
	initOutput();
}

void UpdateChecker::initOutput() {
	QString fileName;
	QRegularExpressionMatch m = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)")).match(updateUrl);
	if (m.hasMatch()) {
		fileName = m.captured(1).replace(QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")), QString());
	}
	if (fileName.isEmpty()) {
		fileName = qsl("tupdate-%1").arg(rand_value<uint32>() % 1000000);
	}
	QString dirStr = cWorkingDir() + qsl("tupdates/");
	fileName = dirStr + fileName;
	QFileInfo file(fileName);

	QDir dir(dirStr);
	if (dir.exists()) {
		QFileInfoList all = dir.entryInfoList(QDir::Files);
		for (QFileInfoList::iterator i = all.begin(), e = all.end(); i != e; ++i) {
			if (i->absoluteFilePath() != file.absoluteFilePath()) {
				QFile::remove(i->absoluteFilePath());
			}
		}
	} else {
		dir.mkdir(dir.absolutePath());
	}
	outputFile.setFileName(fileName);
	if (file.exists()) {
		uint64 fullSize = file.size();
		if (fullSize < INT_MAX) {
			int32 goodSize = (int32)fullSize;
			if (goodSize % UpdateChunk) {
				goodSize = goodSize - (goodSize % UpdateChunk);
				if (goodSize) {
					if (outputFile.open(QIODevice::ReadOnly)) {
						QByteArray goodData = outputFile.readAll().mid(0, goodSize);
						outputFile.close();
						if (outputFile.open(QIODevice::WriteOnly)) {
							outputFile.write(goodData);
							outputFile.close();

							QMutexLocker lock(&mutex);
							already = goodSize;
						}
					}
				}
			} else {
				QMutexLocker lock(&mutex);
				already = goodSize;
			}
		}
		if (!already) {
			QFile::remove(fileName);
		}
	}
}

void UpdateChecker::start() {
	sendRequest();
}

void UpdateChecker::sendRequest() {
	QNetworkRequest req(updateUrl);
	QByteArray rangeHeaderValue = "bytes=" + QByteArray::number(already) + "-";
	req.setRawHeader("Range", rangeHeaderValue);
	req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
	if (reply) reply->deleteLater();
	reply = manager.get(req);
	connect(reply, SIGNAL(downloadProgress(qint64,qint64)), this, SLOT(partFinished(qint64,qint64)));
	connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(partFailed(QNetworkReply::NetworkError)));
	connect(reply, SIGNAL(metaDataChanged()), this, SLOT(partMetaGot()));
}

void UpdateChecker::partMetaGot() {
	typedef QList<QNetworkReply::RawHeaderPair> Pairs;
	Pairs pairs = reply->rawHeaderPairs();
	for (Pairs::iterator i = pairs.begin(), e = pairs.end(); i != e; ++i) {
		if (QString::fromUtf8(i->first).toLower() == "content-range") {
			QRegularExpressionMatch m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(i->second));
			if (m.hasMatch()) {
				{
					QMutexLocker lock(&mutex);
					full = m.captured(1).toInt();
				}

				Sandbox::updateProgress(already, full);
			}
		}
	}
}

int32 UpdateChecker::ready() {
	QMutexLocker lock(&mutex);
	return already;
}

int32 UpdateChecker::size() {
	QMutexLocker lock(&mutex);
	return full;
}

void UpdateChecker::partFinished(qint64 got, qint64 total) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: Bad HTTP status received in partFinished(): %1").arg(status));
			return fatalFail();
		}
	}

	if (!already && !full) {
		QMutexLocker lock(&mutex);
		full = total;
	}
	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	if (!outputFile.isOpen()) {
		if (!outputFile.open(QIODevice::Append)) {
			LOG(("Update Error: Could not open output file '%1' for appending").arg(outputFile.fileName()));
			return fatalFail();
		}
	}
	QByteArray r = reply->readAll();
	if (!r.isEmpty()) {
		outputFile.write(r);

		QMutexLocker lock(&mutex);
		already += r.size();
	}
	if (got >= total) {
		reply->deleteLater();
		reply = 0;
		outputFile.close();
		unpackUpdate();
	} else {
		Sandbox::updateProgress(already, full);
	}
}

void UpdateChecker::partFailed(QNetworkReply::NetworkError e) {
	if (!reply) return;

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	reply->deleteLater();
	reply = 0;
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			outputFile.close();
			unpackUpdate();
			return;
		}
	}
	LOG(("Update Error: failed to download part starting from %1, error %2").arg(already).arg(e));
	Sandbox::updateFailed();
}

void UpdateChecker::fatalFail() {
	clearAll();
	Sandbox::updateFailed();
}

void UpdateChecker::clearAll() {
	psDeleteDir(cWorkingDir() + qsl("tupdates"));
}

//QString winapiErrorWrap() {
//	WCHAR errMsg[2048];
//	DWORD errorCode = GetLastError();
//	LPTSTR errorText = NULL, errorTextDefault = L"(Unknown error)";
//	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorText, 0, 0);
//	if (!errorText) {
//		errorText = errorTextDefault;
//	}
//	StringCbPrintf(errMsg, sizeof(errMsg), L"Error code: %d, error message: %s", errorCode, errorText);
//	if (errorText != errorTextDefault) {
//		LocalFree(errorText);
//	}
//	return QString::fromWCharArray(errMsg);
//}

void UpdateChecker::unpackUpdate() {
	QByteArray packed;
	if (!outputFile.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}

#ifdef Q_OS_WIN // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else // Q_OS_WIN
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif // Q_OS_WIN

	QByteArray compressed = outputFile.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return fatalFail();
	}
	outputFile.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyFilePath = cWorkingDir() + qsl("tupdates/temp/ready");
	psDeleteDir(tempDirPath);

	QDir tempDir(tempDirPath);
	if (tempDir.exists() || QFile(readyFilePath).exists()) {
		LOG(("Update Error: cant clear tupdates/temp dir!"));
		return fatalFail();
	}

	uchar sha1Buffer[20];
	bool goodSha1 = !memcmp(compressed.constData() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, compressedLen + hPropsLen + hOriginalSizeLen, sha1Buffer), hShaLen);
	if (!goodSha1) {
		LOG(("Update Error: bad SHA1 hash of update file!"));
		return fatalFail();
	}

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppAlphaVersion ? UpdatesPublicAlphaKey : UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return fatalFail();
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		if (cAlphaVersion() || cBetaVersion()) { // try other public key, if we are in alpha or beta version
			pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppAlphaVersion ? UpdatesPublicKey : UpdatesPublicAlphaKey), -1), 0, 0, 0);
			if (!pbKey) {
				LOG(("Update Error: cant read public rsa key!"));
				return fatalFail();
			}
			if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
				RSA_free(pbKey);
				LOG(("Update Error: bad RSA signature of update file!"));
				return fatalFail();
			}
		} else {
			LOG(("Update Error: bad RSA signature of update file!"));
			return fatalFail();
		}
	}
	RSA_free(pbKey);

	QByteArray uncompressed;

	int32 uncompressedLen;
	memcpy(&uncompressedLen, compressed.constData() + hSigLen + hShaLen + hPropsLen, hOriginalSizeLen);
	uncompressed.resize(uncompressedLen);

	size_t resultLen = uncompressed.size();
#ifdef Q_OS_WIN // use Lzma SDK for win
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return fatalFail();
	}
#else // Q_OS_WIN
	lzma_stream stream = LZMA_STREAM_INIT;

	lzma_ret ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
		case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
		case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
		case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
		default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error initializing the decoder: %1 (error code %2)").arg(msg).arg(ret));
		return fatalFail();
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)uncompressed.data();

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		LOG(("Error in decompression, %1 bytes left in _in of %2 whole.").arg(stream.avail_in).arg(compressedLen));
		return fatalFail();
	} else if (stream.avail_out) {
		LOG(("Error in decompression, %1 bytes free left in _out of %2 whole.").arg(stream.avail_out).arg(resultLen));
		return fatalFail();
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
		LOG(("Error in decompression: %1 (error code %2)").arg(msg).arg(res));
		return fatalFail();
	}
#endif // Q_OS_WIN

	tempDir.mkdir(tempDir.absolutePath());

	quint32 version;
	{
		QDataStream stream(uncompressed);
		stream.setVersion(QDataStream::Qt_5_1);

		stream >> version;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read version from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}

		quint64 betaVersion = 0;
		if (version == 0x7FFFFFFF) { // beta version
			stream >> betaVersion;
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read beta version from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (!cBetaVersion() || betaVersion <= cBetaVersion()) {
				LOG(("Update Error: downloaded beta version %1 is not greater, than mine %2").arg(betaVersion).arg(cBetaVersion()));
				return fatalFail();
			}
		} else if (int32(version) <= AppVersion) {
			LOG(("Update Error: downloaded version %1 is not greater, than mine %2").arg(version).arg(AppVersion));
			return fatalFail();
		}

		quint32 filesCount;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read files count from downloaded stream, status: %1").arg(stream.status()));
			return fatalFail();
		}
		if (!filesCount) {
			LOG(("Update Error: update is empty!"));
			return fatalFail();
		}
		for (uint32 i = 0; i < filesCount; ++i) {
			QString relativeName;
			quint32 fileSize;
			QByteArray fileInnerData;
			bool executable = false;

			stream >> relativeName >> fileSize >> fileInnerData;
#if defined Q_OS_MAC || defined Q_OS_LINUX
			stream >> executable;
#endif // Q_OS_MAC || Q_OS_LINUX
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
				return fatalFail();
			}
			if (fileSize != quint32(fileInnerData.size())) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return fatalFail();
			}

			QFile f(tempDirPath + '/' + relativeName);
			if (!QDir().mkpath(QFileInfo(f).absolutePath())) {
				LOG(("Update Error: cant mkpath for file '%1'").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + relativeName));
				return fatalFail();
			}
			auto writtenBytes = f.write(fileInnerData);
			if (writtenBytes != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1', desiredSize: %2, write result: %3").arg(tempDirPath + '/' + relativeName).arg(fileSize).arg(writtenBytes));
				return fatalFail();
			}
			f.close();
			if (executable) {
				QFileDevice::Permissions p = f.permissions();
				p |= QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther;
				f.setPermissions(p);
			}
		}

		// create tdata/version file
		tempDir.mkdir(QDir(tempDirPath + qsl("/tdata")).absolutePath());
		std::wstring versionString = ((version % 1000) ? QString("%1.%2.%3").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000)).arg(int(version % 1000)) : QString("%1.%2").arg(int(version / 1000000)).arg(int((version % 1000000) / 1000))).toStdWString();

		VerInt versionNum = VerInt(version), versionLen = VerInt(versionString.size() * sizeof(VerChar));
		VerChar versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
		}
		fVersion.write((const char*)&versionNum, sizeof(VerInt));
		if (versionNum == 0x7FFFFFFF) { // beta version
			fVersion.write((const char*)&betaVersion, sizeof(quint64));
		} else {
			fVersion.write((const char*)&versionLen, sizeof(VerInt));
			fVersion.write((const char*)&versionStr[0], versionLen);
		}
		fVersion.close();
	}

	QFile readyFile(readyFilePath);
	if (readyFile.open(QIODevice::WriteOnly)) {
		if (readyFile.write("1", 1)) {
			readyFile.close();
		} else {
			LOG(("Update Error: cant write ready file '%1'").arg(readyFilePath));
			return fatalFail();
		}
	} else {
		LOG(("Update Error: cant create ready file '%1'").arg(readyFilePath));
		return fatalFail();
	}
	outputFile.remove();

	Sandbox::updateReady();
}

UpdateChecker::~UpdateChecker() {
	delete reply;
	reply = 0;
}

bool checkReadyUpdate() {
	QString readyFilePath = cWorkingDir() + qsl("tupdates/temp/ready"), readyPath = cWorkingDir() + qsl("tupdates/temp");
	if (!QFile(readyFilePath).exists() || cExeName().isEmpty()) {
		if (QDir(cWorkingDir() + qsl("tupdates/ready")).exists() || QDir(cWorkingDir() + qsl("tupdates/temp")).exists()) {
			UpdateChecker::clearAll();
		}
		return false;
	}

	// check ready version
	QString versionPath = readyPath + qsl("/tdata/version");
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			UpdateChecker::clearAll();
			return false;
		}
		VerInt versionNum;
		if (fVersion.read((char*)&versionNum, sizeof(VerInt)) != sizeof(VerInt)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			UpdateChecker::clearAll();
			return false;
		}
		if (versionNum == 0x7FFFFFFF) { // beta version
			quint64 betaVersion = 0;
			if (fVersion.read((char*)&betaVersion, sizeof(quint64)) != sizeof(quint64)) {
				LOG(("Update Error: cant read beta version from file '%1'").arg(versionPath));
				UpdateChecker::clearAll();
				return false;
			}
			if (!cBetaVersion() || betaVersion <= cBetaVersion()) {
				LOG(("Update Error: cant install beta version %1 having beta version %2").arg(betaVersion).arg(cBetaVersion()));
				UpdateChecker::clearAll();
				return false;
			}
		} else if (versionNum <= AppVersion) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			UpdateChecker::clearAll();
			return false;
		}
		fVersion.close();
	}

#ifdef Q_OS_WIN
	QString curUpdater = (cExeDir() + qsl("Updater.exe"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/temp/Updater.exe"));
#elif defined Q_OS_MAC // Q_OS_WIN
	QString curUpdater = (cExeDir() + cExeName() + qsl("/Contents/Frameworks/Updater"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/temp/Telegram.app/Contents/Frameworks/Updater"));
#elif defined Q_OS_LINUX // Q_OS_MAC
	QString curUpdater = (cExeDir() + qsl("Updater"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/temp/Updater"));
#endif // Q_OS_LINUX
	if (!updater.exists()) {
		QFileInfo current(curUpdater);
		if (!current.exists()) {
			UpdateChecker::clearAll();
			return false;
		}
		if (!QFile(current.absoluteFilePath()).copy(updater.absoluteFilePath())) {
			UpdateChecker::clearAll();
			return false;
		}
	}
#ifdef Q_OS_WIN
	if (CopyFile(updater.absoluteFilePath().toStdWString().c_str(), curUpdater.toStdWString().c_str(), FALSE) == FALSE) {
		DWORD errorCode = GetLastError();
		if (errorCode == ERROR_ACCESS_DENIED) { // we are in write-protected dir, like Program Files
			cSetWriteProtected(true);
			return true;
		} else {
			UpdateChecker::clearAll();
			return false;
		}
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		UpdateChecker::clearAll();
		return false;
	}
#elif defined Q_OS_MAC // Q_OS_WIN
	QDir().mkpath(QFileInfo(curUpdater).absolutePath());
	DEBUG_LOG(("Update Info: moving %1 to %2...").arg(updater.absoluteFilePath()).arg(curUpdater));
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		UpdateChecker::clearAll();
		return false;
	}
#elif defined Q_OS_LINUX // Q_OS_MAC
	if (!linuxMoveFile(QFile::encodeName(updater.absoluteFilePath()).constData(), QFile::encodeName(curUpdater).constData())) {
		UpdateChecker::clearAll();
		return false;
	}
#endif // Q_OS_LINUX
	return true;
}

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

QString countBetaVersionSignature(uint64 version) { // duplicated in packer.cpp
	if (cBetaPrivateKey().isEmpty()) {
		LOG(("Error: Trying to count beta version signature without beta private key!"));
		return QString();
	}

	QByteArray signedData = (qstr("TelegramBeta_") + QString::number(version, 16).toLower()).toUtf8();

	static const int32 shaSize = 20, keySize = 128;

	uchar sha1Buffer[shaSize];
	hashSha1(signedData.constData(), signedData.size(), sha1Buffer); // count sha1

	uint32 siglen = 0;

	RSA *prKey = PEM_read_bio_RSAPrivateKey(BIO_new_mem_buf(const_cast<char*>(cBetaPrivateKey().constData()), -1), 0, 0, 0);
	if (!prKey) {
		LOG(("Error: Could not read beta private key!"));
		return QString();
	}
	if (RSA_size(prKey) != keySize) {
		LOG(("Error: Bad beta private key size: %1").arg(RSA_size(prKey)));
		RSA_free(prKey);
		return QString();
	}
	QByteArray signature;
	signature.resize(keySize);
	if (RSA_sign(NID_sha1, (const uchar*)(sha1Buffer), shaSize, (uchar*)(signature.data()), &siglen, prKey) != 1) { // count signature
		LOG(("Error: Counting beta version signature failed!"));
		RSA_free(prKey);
		return QString();
	}
	RSA_free(prKey);

	if (siglen != keySize) {
		LOG(("Error: Bad beta version signature length: %1").arg(siglen));
		return QString();
	}

	signature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	signature = signature.replace('-', '8').replace('_', 'B');
	return QString::fromUtf8(signature.mid(19, 32));
}
