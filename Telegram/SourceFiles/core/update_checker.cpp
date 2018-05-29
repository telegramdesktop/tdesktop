/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_checker.h"

#include "application.h"
#include "platform/platform_specific.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "storage/localstorage.h"
#include "messenger.h"
#include "mtproto/session.h"

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#ifdef Q_OS_WIN // use Lzma SDK for win
#include <LzmaLib.h>
#else // Q_OS_WIN
#include <lzma.h>
#endif // else of Q_OS_WIN

namespace Core {

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

namespace {

constexpr auto kUpdaterTimeout = 10 * TimeMs(1000);
constexpr auto kMaxResponseSize = 1024 * 1024;
constexpr auto kMaxUpdateSize = 256 * 1024 * 1024;
constexpr auto kChunkSize = 128 * 1024;

std::weak_ptr<Updater> UpdaterInstance;

using ErrorSignal = void(QNetworkReply::*)(QNetworkReply::NetworkError);
const auto QNetworkReply_error = ErrorSignal(&QNetworkReply::error);

using Progress = UpdateChecker::Progress;
using State = UpdateChecker::State;

#ifdef Q_OS_WIN
using VersionInt = DWORD;
using VersionChar = WCHAR;
#else // Q_OS_WIN
using VersionInt = int;
using VersionChar = wchar_t;
#endif // Q_OS_WIN

class Loader : public base::has_weak_ptr {
public:
	Loader(const QString &filename, int chunkSize);

	void start();

	int alreadySize() const;
	int totalSize() const;

	rpl::producer<Progress> progress() const;
	rpl::producer<QString> ready() const;
	rpl::producer<> failed() const;

	rpl::lifetime &lifetime();

	virtual ~Loader() = default;

protected:
	bool startOutput();
	void threadSafeFailed();

	// Single threaded.
	void writeChunk(bytes::const_span data, int totalSize);

private:
	virtual void startLoading() = 0;

	bool validateOutput();
	void threadSafeProgress(Progress progress);
	void threadSafeReady();

	QString _filename;
	QString _filepath;
	int _chunkSize = 0;

	QFile _output;
	int _alreadySize = 0;
	int _totalSize = 0;
	mutable QMutex _sizesMutex;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<QString> _ready;
	rpl::event_stream<> _failed;

	rpl::lifetime _lifetime;

};

class Checker : public base::has_weak_ptr {
public:
	Checker(bool testing);

	virtual void start() = 0;

	rpl::producer<std::shared_ptr<Loader>> ready() const;
	rpl::producer<> failed() const;

	rpl::lifetime &lifetime();

	virtual ~Checker() = default;

protected:
	bool testing() const;
	void done(std::shared_ptr<Loader> result);
	void fail();

private:
	bool _testing = false;
	rpl::event_stream<std::shared_ptr<Loader>> _ready;
	rpl::event_stream<> _failed;

	rpl::lifetime _lifetime;

};

struct Implementation {
	std::unique_ptr<Checker> checker;
	std::shared_ptr<Loader> loader;
	bool failed = false;

};

class HttpChecker : public Checker {
public:
	HttpChecker(bool testing);

	void start() override;

	~HttpChecker();

private:
	void gotResponse();
	void gotFailure(QNetworkReply::NetworkError e);
	void clearSentRequest();
	bool handleResponse(const QByteArray &response);
	base::optional<QString> parseOldResponse(
		const QByteArray &response) const;
	base::optional<QString> parseResponse(const QByteArray &response) const;
	QString validateLatestUrl(
		uint64 availableVersion,
		bool isAvailableBeta,
		QString url) const;

	std::unique_ptr<QNetworkAccessManager> _manager;
	QNetworkReply *_reply = nullptr;

};

class HttpLoaderActor;

class HttpLoader : public Loader {
public:
	HttpLoader(const QString &url);

	~HttpLoader();

private:
	void startLoading() override;

	friend class HttpLoaderActor;

	QString _url;
	std::unique_ptr<QThread> _thread;
	HttpLoaderActor *_actor = nullptr;

};

class HttpLoaderActor : public QObject {
public:
	HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url);

private:
	void start();
	void sendRequest();

	void gotMetaData();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);

	not_null<HttpLoader*> _parent;
	QString _url;
	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _reply;

};

class MtpWeak : private QObject, private base::Subscriber {
public:
	MtpWeak(QPointer<MTP::Instance> instance);

	template <typename T>
	void send(
		const T &request,
		base::lambda<void(const typename T::ResponseType &result)> done,
		base::lambda<void(const RPCError &error)> fail,
		MTP::ShiftedDcId dcId = 0);

	bool valid() const;
	QPointer<MTP::Instance> instance() const;

	~MtpWeak();

private:
	void die();
	bool removeRequest(mtpRequestId requestId);

	QPointer<MTP::Instance> _instance;
	std::map<mtpRequestId, base::lambda<void(const RPCError &)>> _requests;

};

class MtpChecker : public Checker {
public:
	MtpChecker(QPointer<MTP::Instance> instance, bool testing);

	void start() override;

private:
	struct FileLocation {
		QString username;
		int32 postId = 0;
	};
	struct ParsedFile {
		QString name;
		int32 size = 0;
		MTP::DcId dcId = 0;
		MTPInputFileLocation location;
	};

	using Checker::fail;
	base::lambda<void(const RPCError &error)> failHandler();

	void gotFeed(const MTPcontacts_ResolvedPeer &result);
	void gotMessage(const MTPmessages_Messages &result);
	base::optional<FileLocation> parseMessage(
		const MTPmessages_Messages &result) const;
	base::optional<FileLocation> parseText(const QByteArray &text) const;
	FileLocation validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const;
	void resolvedFiles(
		const MTPcontacts_ResolvedPeer &result,
		const FileLocation &location);
	void gotFile(const MTPmessages_Messages &result);
	base::optional<ParsedFile> parseFile(
		const MTPmessages_Messages &result) const;

	MtpWeak _mtp;

};

class MtpLoader : public Loader {
public:
	MtpLoader(
		QPointer<MTP::Instance> instance,
		const QString &name,
		int32 size,
		MTP::DcId dcId,
		const MTPInputFileLocation &location);

private:
	struct Request {
		int offset = 0;
		QByteArray bytes;
	};
	void startLoading() override;
	void sendRequest();
	void gotPart(int offset, const MTPupload_File &result);
	base::lambda<void(const RPCError &)> failHandler();

	static constexpr auto kRequestsCount = 2;
	static constexpr auto kNextRequestDelay = TimeMs(20);

	std::deque<Request> _requests;
	int32 _size = 0;
	int _offset = 0;
	MTP::DcId _dcId = 0;
	MTPInputFileLocation _location;
	MtpWeak _mtp;

};

std::shared_ptr<Updater> GetUpdaterInstance() {
	if (const auto result = UpdaterInstance.lock()) {
		return result;
	}
	const auto result = std::make_shared<Updater>();
	UpdaterInstance = result;
	return result;
}

void ClearAll() {
	psDeleteDir(cWorkingDir() + qsl("tupdates"));
}

QString FindUpdateFile() {
	QDir updates(cWorkingDir() + "tupdates");
	if (!updates.exists()) {
		return QString();
	}
	const auto list = updates.entryInfoList(QDir::Files);
	for (const auto &info : list) {
		if (QRegularExpression(
			"^("
			"tupdate|"
			"tmacupd|"
			"tmac32upd|"
			"tlinuxupd|"
			"tlinux32upd"
			")\\d+(_[a-z\\d]+)?$",
			QRegularExpression::CaseInsensitiveOption
		).match(info.fileName()).hasMatch()) {
			return info.absoluteFilePath();
		}
	}
	return QString();
}

QString ExtractFilename(const QString &url) {
	const auto expression = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)"));
	if (const auto match = expression.match(url); match.hasMatch()) {
		return match.captured(1).replace(
			QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")),
			QString());
	}
	return QString();
}

bool UnpackUpdate(const QString &filepath) {
	QFile input(filepath);
	QByteArray packed;
	if (!input.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return false;
	}

#ifdef Q_OS_WIN // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else // Q_OS_WIN
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif // Q_OS_WIN

	QByteArray compressed = input.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return false;
	}
	input.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyFilePath = cWorkingDir() + qsl("tupdates/temp/ready");
	psDeleteDir(tempDirPath);

	QDir tempDir(tempDirPath);
	if (tempDir.exists() || QFile(readyFilePath).exists()) {
		LOG(("Update Error: cant clear tupdates/temp dir!"));
		return false;
	}

	uchar sha1Buffer[20];
	bool goodSha1 = !memcmp(compressed.constData() + hSigLen, hashSha1(compressed.constData() + hSigLen + hShaLen, compressedLen + hPropsLen + hOriginalSizeLen, sha1Buffer), hShaLen);
	if (!goodSha1) {
		LOG(("Update Error: bad SHA1 hash of update file!"));
		return false;
	}

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppAlphaVersion ? UpdatesPublicAlphaKey : UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return false;
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);
		if (cAlphaVersion() || cBetaVersion()) { // try other public key, if we are in alpha or beta version
			pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppAlphaVersion ? UpdatesPublicKey : UpdatesPublicAlphaKey), -1), 0, 0, 0);
			if (!pbKey) {
				LOG(("Update Error: cant read public rsa key!"));
				return false;
			}
			if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
				RSA_free(pbKey);
				LOG(("Update Error: bad RSA signature of update file!"));
				return false;
			}
		} else {
			LOG(("Update Error: bad RSA signature of update file!"));
			return false;
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
		return false;
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
		return false;
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(compressed.constData() + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)uncompressed.data();

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		LOG(("Error in decompression, %1 bytes left in _in of %2 whole.").arg(stream.avail_in).arg(compressedLen));
		return false;
	} else if (stream.avail_out) {
		LOG(("Error in decompression, %1 bytes free left in _out of %2 whole.").arg(stream.avail_out).arg(resultLen));
		return false;
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
		return false;
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
			return false;
		}

		quint64 betaVersion = 0;
		if (version == 0x7FFFFFFF) { // beta version
			stream >> betaVersion;
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read beta version from downloaded stream, status: %1").arg(stream.status()));
				return false;
			}
			if (!cBetaVersion() || betaVersion <= cBetaVersion()) {
				LOG(("Update Error: downloaded beta version %1 is not greater, than mine %2").arg(betaVersion).arg(cBetaVersion()));
				return false;
			}
		} else if (int32(version) <= AppVersion) {
			LOG(("Update Error: downloaded version %1 is not greater, than mine %2").arg(version).arg(AppVersion));
			return false;
		}

		quint32 filesCount;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read files count from downloaded stream, status: %1").arg(stream.status()));
			return false;
		}
		if (!filesCount) {
			LOG(("Update Error: update is empty!"));
			return false;
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
				return false;
			}
			if (fileSize != quint32(fileInnerData.size())) {
				LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
				return false;
			}

			QFile f(tempDirPath + '/' + relativeName);
			if (!QDir().mkpath(QFileInfo(f).absolutePath())) {
				LOG(("Update Error: cant mkpath for file '%1'").arg(tempDirPath + '/' + relativeName));
				return false;
			}
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + relativeName));
				return false;
			}
			auto writtenBytes = f.write(fileInnerData);
			if (writtenBytes != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1', desiredSize: %2, write result: %3").arg(tempDirPath + '/' + relativeName).arg(fileSize).arg(writtenBytes));
				return false;
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

		const auto versionNum = VersionInt(version);
		const auto versionLen = VersionInt(versionString.size() * sizeof(VersionChar));
		VersionChar versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return false;
		}
		fVersion.write((const char*)&versionNum, sizeof(VersionInt));
		if (versionNum == 0x7FFFFFFF) { // beta version
			fVersion.write((const char*)&betaVersion, sizeof(quint64));
		} else {
			fVersion.write((const char*)&versionLen, sizeof(VersionInt));
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
			return false;
		}
	} else {
		LOG(("Update Error: cant create ready file '%1'").arg(readyFilePath));
		return false;
	}
	input.remove();

	return true;
}

base::optional<MTPInputChannel> ExtractChannel(
		const MTPcontacts_ResolvedPeer &result) {
	const auto &data = result.c_contacts_resolvedPeer();
	if (const auto peer = peerFromMTP(data.vpeer)) {
		for (const auto &chat : data.vchats.v) {
			if (chat.type() == mtpc_channel) {
				const auto &channel = chat.c_channel();
				if (peer == peerFromChannel(channel.vid)) {
					return MTP_inputChannel(
						channel.vid,
						channel.vaccess_hash);
				}
			}
		}
	}
	return base::none;
}

template <typename Callback>
bool ParseCommonMap(
		const QByteArray &json,
		bool testing,
		Callback &&callback) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Update Error: MTP failed to parse JSON, error: %1"
			).arg(error.errorString()));
		return false;
	} else if (!document.isObject()) {
		LOG(("Update Error: MTP not an object received in JSON."));
		return false;
	}
	const auto platforms = document.object();
	const auto platform = [&] {
		switch (cPlatform()) {
		case dbipWindows: return "win";
		case dbipMac: return "mac";
		case dbipMacOld: return "mac32";
		case dbipLinux64: return "linux";
		case dbipLinux32: return "linux32";
		}
		Unexpected("Platform in ParseCommonMap.");
	}();
	const auto it = platforms.constFind(platform);
	if (it == platforms.constEnd()) {
		LOG(("Update Error: MTP platform '%1' not found in response."
			).arg(platform));
		return false;
	} else if (!(*it).isObject()) {
		LOG(("Update Error: MTP not an object found for platform '%1'."
			).arg(platform));
		return false;
	}
	const auto types = (*it).toObject();
	const auto list = [&]() -> std::vector<QString> {
		if (cBetaVersion()) {
			return { "alpha", "beta", "stable" };
		} else if (cAlphaVersion()) {
			return { "beta", "stable" };
		}
		return { "stable" };
	}();
	auto bestIsAvailableBeta = false;
	auto bestAvailableVersion = 0ULL;
	for (const auto &type : list) {
		const auto it = types.constFind(type);
		if (it == types.constEnd()) {
			continue;
		} else if (!(*it).isObject()) {
			LOG(("Update Error: Not an object found for '%1:%2'."
				).arg(platform).arg(type));
			return false;
		}
		const auto map = (*it).toObject();
		const auto key = testing ? "testing" : "released";
		const auto version = map.constFind(key);
		if (version == map.constEnd()) {
			continue;
		}
		const auto isAvailableBeta = (type == "alpha");
		const auto availableVersion = [&] {
			if ((*version).isString()) {
				const auto string = (*version).toString();
				if (const auto index = string.indexOf(':'); index > 0) {
					return string.midRef(0, index).toULongLong();
				}
				return string.toULongLong();
			} else if ((*version).isDouble()) {
				return uint64(std::round((*version).toDouble()));
			}
			return 0ULL;
		}();
		if (!availableVersion) {
			LOG(("Update Error: Version is not valid for '%1:%2:%3'."
				).arg(platform).arg(type).arg(key));
			return false;
		}
		const auto compare = isAvailableBeta
			? availableVersion
			: availableVersion * 1000;
		const auto bestCompare = bestIsAvailableBeta
			? bestAvailableVersion
			: bestAvailableVersion * 1000;
		if (compare > bestCompare) {
			bestAvailableVersion = availableVersion;
			bestIsAvailableBeta = isAvailableBeta;
			if (!callback(availableVersion, isAvailableBeta, map)) {
				return false;
			}
		}
	}
	if (!bestAvailableVersion) {
		LOG(("Update Error: No valid entry found for platform '%1'."
			).arg(platform));
		return false;
	}
	return true;
}

base::optional<MTPMessage> GetMessagesElement(
		const MTPmessages_Messages &list) {
	const auto get = [](auto &&data) -> base::optional<MTPMessage> {
		return data.vmessages.v.isEmpty()
			? base::none
			: base::make_optional(data.vmessages.v[0]);
	};
	switch (list.type()) {
	case mtpc_messages_messages:
		return get(list.c_messages_messages());
	case mtpc_messages_messagesSlice:
		return get(list.c_messages_messagesSlice());
	case mtpc_messages_channelMessages:
		return get(list.c_messages_channelMessages());
	case mtpc_messages_messagesNotModified:
		return base::none;
	default: Unexpected("Type of messages.Messages (GetMessagesElement)");
	}
}

Loader::Loader(const QString &filename, int chunkSize)
: _filename(filename)
, _chunkSize(chunkSize) {
}

void Loader::start() {
	if (!validateOutput()
		|| (!_output.isOpen() && !_output.open(QIODevice::Append))) {
		QFile(_filepath).remove();
		threadSafeFailed();
		return;
	}

	LOG(("Update Info: Starting loading '%1' from %2 offset."
		).arg(_filename
		).arg(alreadySize()));
	startLoading();
}

int Loader::alreadySize() const {
	QMutexLocker lock(&_sizesMutex);
	return _alreadySize;
}

int Loader::totalSize() const {
	QMutexLocker lock(&_sizesMutex);
	return _totalSize;
}

rpl::producer<QString> Loader::ready() const {
	return _ready.events();
}

rpl::producer<Progress> Loader::progress() const {
	return _progress.events();
}

rpl::producer<> Loader::failed() const {
	return _failed.events();
}

bool Loader::validateOutput() {
	if (_filename.isEmpty()) {
		return false;
	}
	const auto folder = cWorkingDir() + qsl("tupdates/");
	_filepath = folder + _filename;

	QFileInfo info(_filepath);
	QDir dir(folder);
	if (dir.exists()) {
		const auto all = dir.entryInfoList(QDir::Files);
		for (auto i = all.begin(), e = all.end(); i != e; ++i) {
			if (i->absoluteFilePath() != info.absoluteFilePath()) {
				QFile::remove(i->absoluteFilePath());
			}
		}
	} else {
		dir.mkdir(dir.absolutePath());
	}
	_output.setFileName(_filepath);

	if (!info.exists()) {
		return true;
	}
	const auto fullSize = info.size();
	if (fullSize < _chunkSize || fullSize > kMaxUpdateSize) {
		return _output.remove();
	}
	const auto goodSize = int((fullSize % _chunkSize)
		? (fullSize - (fullSize % _chunkSize))
		: fullSize);
	if (_output.resize(goodSize)) {
		_alreadySize = goodSize;
		return true;
	}
	return false;
}

void Loader::threadSafeProgress(Progress progress) {
	crl::on_main(this, [=] {
		_progress.fire_copy(progress);
	});
}

void Loader::threadSafeReady() {
	crl::on_main(this, [=] {
		_ready.fire_copy(_filepath);
	});
}

void Loader::threadSafeFailed() {
	crl::on_main(this, [=] {
		_failed.fire({});
	});
}

void Loader::writeChunk(bytes::const_span data, int totalSize) {
	const auto size = data.size();
	if (size > 0) {
		const auto written = _output.write(QByteArray::fromRawData(
			reinterpret_cast<const char*>(data.data()),
			size));
		if (written != size) {
			threadSafeFailed();
			return;
		}
	}

	const auto progress = [&] {
		QMutexLocker lock(&_sizesMutex);
		if (!_totalSize) {
			_totalSize = totalSize;
		}
		_alreadySize += size;
		return Progress { _alreadySize, _totalSize };
	}();

	if (progress.size > 0 && progress.already >= progress.size) {
		_output.close();
		threadSafeReady();
	} else {
		threadSafeProgress(progress);
	}
}

rpl::lifetime &Loader::lifetime() {
	return _lifetime;
}

Checker::Checker(bool testing) : _testing(testing) {
}

rpl::producer<std::shared_ptr<Loader>> Checker::ready() const {
	return _ready.events();
}

rpl::producer<> Checker::failed() const {
	return _failed.events();
}

bool Checker::testing() const {
	return _testing;
}

void Checker::done(std::shared_ptr<Loader> result) {
	_ready.fire(std::move(result));
}

void Checker::fail() {
	_failed.fire({});
}

rpl::lifetime &Checker::lifetime() {
	return _lifetime;
}

HttpChecker::HttpChecker(bool testing) : Checker(testing) {
}

void HttpChecker::start() {
	auto url = QUrl(Local::readAutoupdatePrefix() + qstr("/current"));
	DEBUG_LOG(("Update Info: requesting update state from '%1'"
		).arg(url.toDisplayString()));
	const auto request = QNetworkRequest(url);
	_manager = std::make_unique<QNetworkAccessManager>();
	_reply = _manager->get(request);
	_reply->connect(_reply, &QNetworkReply::finished, [=] {
		gotResponse();
	});
	_reply->connect(_reply, QNetworkReply_error, [=](auto e) {
		gotFailure(e);
	});
}

void HttpChecker::gotResponse() {
	if (!_reply) {
		return;
	}

	cSetLastUpdateCheck(unixtime());
	const auto response = _reply->readAll();
	clearSentRequest();

	if (response.size() >= kMaxResponseSize || !handleResponse(response)) {
		LOG(("Update Error: Bad update map size: %1").arg(response.size()));
		gotFailure(QNetworkReply::UnknownContentError);
	}
}

bool HttpChecker::handleResponse(const QByteArray &response) {
	const auto handle = [&](const QString &url) {
		done(url.isEmpty() ? nullptr : std::make_shared<HttpLoader>(url));
		return true;
	};
	if (const auto url = parseOldResponse(response)) {
		return handle(*url);
	} else if (const auto url = parseResponse(response)) {
		return handle(*url);
	}
	return false;
}

void HttpChecker::clearSentRequest() {
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	reply->disconnect(reply, &QNetworkReply::finished, nullptr, nullptr);
	reply->disconnect(reply, QNetworkReply_error, nullptr, nullptr);
	reply->abort();
	reply->deleteLater();
	_manager = nullptr;
}

void HttpChecker::gotFailure(QNetworkReply::NetworkError e) {
	LOG(("Update Error: "
		"could not get current version %1").arg(e));
	if (const auto reply = base::take(_reply)) {
		reply->deleteLater();
	}

	fail();
}

base::optional<QString> HttpChecker::parseOldResponse(
		const QByteArray &response) const {
	const auto string = QString::fromLatin1(response);
	const auto old = QRegularExpression(
		qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")
	).match(string);
	if (!old.hasMatch()) {
		return base::none;
	}
	const auto availableVersion = old.captured(1).toULongLong();
	const auto url = old.captured(2);
	const auto isAvailableBeta = url.startsWith(qstr("beta_"));
	return validateLatestUrl(
		availableVersion,
		isAvailableBeta,
		isAvailableBeta ? url.mid(5) + "_{signature}" : url);
}

base::optional<QString> HttpChecker::parseResponse(
		const QByteArray &response) const {
	auto bestAvailableVersion = 0ULL;
	auto bestIsAvailableBeta = false;
	auto bestLink = QString();
	const auto accumulate = [&](
			uint64 version,
			bool isBeta,
			const QJsonObject &map) {
		bestAvailableVersion = version;
		bestIsAvailableBeta = isBeta;
		const auto link = map.constFind("link");
		if (link == map.constEnd()) {
			LOG(("Update Error: Link not found for version %1."
				).arg(version));
			return false;
		} else if (!(*link).isString()) {
			LOG(("Update Error: Link is not a string for version %1."
				).arg(version));
			return false;
		}
		bestLink = (*link).toString();
		return true;
	};
	const auto result = ParseCommonMap(response, testing(), accumulate);
	if (!result) {
		return base::none;
	}
	return validateLatestUrl(
		bestAvailableVersion,
		bestIsAvailableBeta,
		Local::readAutoupdatePrefix() + bestLink);
}

QString HttpChecker::validateLatestUrl(
		uint64 availableVersion,
		bool isAvailableBeta,
		QString url) const {
	const auto myVersion = isAvailableBeta
		? cBetaVersion()
		: uint64(AppVersion);
	const auto validVersion = (cBetaVersion() || !isAvailableBeta);
	if (!validVersion || availableVersion <= myVersion) {
		return QString();
	}
	const auto versionUrl = url.replace(
		"{version}",
		QString::number(availableVersion));
	const auto finalUrl = isAvailableBeta
		? QString(versionUrl).replace(
			"{signature}",
			countBetaVersionSignature(availableVersion))
		: versionUrl;
	return finalUrl;
}

HttpChecker::~HttpChecker() {
	clearSentRequest();
}

HttpLoader::HttpLoader(const QString &url)
: Loader(ExtractFilename(url), kChunkSize)
, _url(url) {
}

void HttpLoader::startLoading() {
	LOG(("Update Info: Loading using HTTP from '%1'.").arg(_url));

	_thread = std::make_unique<QThread>();
	_actor = new HttpLoaderActor(this, _thread.get(), _url);
	_thread->start();
}

HttpLoader::~HttpLoader() {
	if (const auto thread = base::take(_thread)) {
		if (const auto actor = base::take(_actor)) {
			QObject::connect(
				thread.get(),
				&QThread::finished,
				actor,
				&QObject::deleteLater);
		}
		thread->quit();
		thread->wait();
	}
}

HttpLoaderActor::HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url)
: _parent(parent) {
	_url = url;
	moveToThread(thread);
	_manager.moveToThread(thread);

	connect(thread, &QThread::started, this, [=] { start(); });
}

void HttpLoaderActor::start() {
	sendRequest();
}

void HttpLoaderActor::sendRequest() {
	auto request = QNetworkRequest(_url);
	const auto rangeHeaderValue = "bytes="
		+ QByteArray::number(_parent->alreadySize())
		+ "-";
	request.setRawHeader("Range", rangeHeaderValue);
	request.setAttribute(
		QNetworkRequest::HttpPipeliningAllowedAttribute,
		true);
	_reply.reset(_manager.get(request));
	connect(
		_reply.get(),
		&QNetworkReply::downloadProgress,
		this,
		&HttpLoaderActor::partFinished);
	connect(
		_reply.get(),
		QNetworkReply_error,
		this,
		&HttpLoaderActor::partFailed);
	connect(
		_reply.get(),
		&QNetworkReply::metaDataChanged,
		this,
		&HttpLoaderActor::gotMetaData);
}

void HttpLoaderActor::gotMetaData() {
	const auto pairs = _reply->rawHeaderPairs();
	for (const auto pair : pairs) {
		if (QString::fromUtf8(pair.first).toLower() == "content-range") {
			const auto m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(pair.second));
			if (m.hasMatch()) {
				_parent->writeChunk({}, m.captured(1).toInt());
			}
		}
	}
}

void HttpLoaderActor::partFinished(qint64 got, qint64 total) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: "
				"Bad HTTP status received in partFinished(): %1"
				).arg(status));
			_parent->threadSafeFailed();
			return;
		}
	}

	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	const auto data = _reply->readAll();
	_parent->writeChunk(bytes::make_span(data), total);
}

void HttpLoaderActor::partFailed(QNetworkReply::NetworkError e) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	_reply.release()->deleteLater();
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			_parent->writeChunk({}, _parent->alreadySize());
			return;
		}
	}
	LOG(("Update Error: failed to download part after %1, error %2"
		).arg(_parent->alreadySize()
		).arg(e));
	_parent->threadSafeFailed();
}

MtpWeak::MtpWeak(QPointer<MTP::Instance> instance)
: _instance(instance) {
	if (!valid()) {
		return;
	}

	connect(_instance, &QObject::destroyed, this, [=] {
		_instance = nullptr;
		die();
	});
	subscribe(Messenger::Instance().authSessionChanged(), [=] {
		if (!AuthSession::Exists()) {
			die();
		}
	});
}

bool MtpWeak::valid() const {
	return (_instance != nullptr) && AuthSession::Exists();
}

QPointer<MTP::Instance> MtpWeak::instance() const {
	return _instance;
}

void MtpWeak::die() {
	const auto instance = _instance.data();
	for (const auto &[requestId, fail] : base::take(_requests)) {
		if (instance) {
			instance->cancel(requestId);
		}
		fail(MTP::internal::rpcClientError("UNAVAILABLE"));
	}
}

template <typename T>
void MtpWeak::send(
		const T &request,
		base::lambda<void(const typename T::ResponseType &result)> done,
		base::lambda<void(const RPCError &error)> fail,
		MTP::ShiftedDcId dcId) {
	using Response = typename T::ResponseType;
	if (!valid()) {
		InvokeQueued(this, [=] {
			fail(MTP::internal::rpcClientError("UNAVAILABLE"));
		});
		return;
	}
	const auto onDone = base::lambda_guarded(this, [=](
			const Response &result,
			mtpRequestId requestId) {
		if (removeRequest(requestId)) {
			done(result);
		}
	});
	const auto onFail = base::lambda_guarded(this, [=](
			const RPCError &error,
			mtpRequestId requestId) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		if (removeRequest(requestId)) {
			fail(error);
		}
		return true;
	});
	const auto requestId = _instance->send(
		request,
		rpcDone(onDone),
		rpcFail(onFail),
		dcId);
	_requests.emplace(requestId, fail);
}

bool MtpWeak::removeRequest(mtpRequestId requestId) {
	if (const auto i = _requests.find(requestId); i != end(_requests)) {
		_requests.erase(i);
		return true;
	}
	return false;
}

MtpWeak::~MtpWeak() {
	if (const auto instance = _instance.data()) {
		for (const auto &[requestId, fail] : base::take(_requests)) {
			instance->cancel(requestId);
		}
	}
}

MtpChecker::MtpChecker(QPointer<MTP::Instance> instance, bool testing)
: Checker(testing)
, _mtp(instance) {
}

void MtpChecker::start() {
	if (!_mtp.valid()) {
		LOG(("Update Info: MTP is unavailable."));
		InvokeQueued(this, [=] { fail(); });
		return;
	}
	constexpr auto kFeedUsername = "tdhbcfeed";

	_mtp.send(
		MTPcontacts_ResolveUsername(MTP_string(kFeedUsername)),
		[=](const MTPcontacts_ResolvedPeer &result) { gotFeed(result); },
		failHandler());
}

void MtpChecker::gotFeed(const MTPcontacts_ResolvedPeer &result) {
	Expects(result.type() == mtpc_contacts_resolvedPeer);

	if (const auto channel = ExtractChannel(result)) {
		_mtp.send(
			MTPmessages_GetHistory(
				MTP_inputPeerChannel(
					channel->c_inputChannel().vchannel_id,
					channel->c_inputChannel().vaccess_hash),
				MTP_int(0),  // offset_id
				MTP_int(0),  // offset_date
				MTP_int(0),  // add_offset
				MTP_int(1),  // limit
				MTP_int(0),  // max_id
				MTP_int(0),  // min_id
				MTP_int(0)), // hash
			[=](const MTPmessages_Messages &result) { gotMessage(result); },
			failHandler());
	} else {
		LOG(("Update Error: MTP feed channel not found."));
		fail();
	}
}

void MtpChecker::gotMessage(const MTPmessages_Messages &result) {
	if (const auto location = parseMessage(result)) {
		if (location->username.isEmpty()) {
			done(nullptr);
		} else {
			const auto got = [=](const MTPcontacts_ResolvedPeer &result) {
				resolvedFiles(result, *location);
			};
			_mtp.send(
				MTPcontacts_ResolveUsername(MTP_string(location->username)),
				got,
				failHandler());
		}
	} else {
		fail();
	}
}

auto MtpChecker::parseMessage(const MTPmessages_Messages &result) const
-> base::optional<FileLocation> {
	const auto message = GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Update Error: MTP feed message not found."));
		return base::none;
	}
	return parseText(message->c_message().vmessage.v);
}

auto MtpChecker::parseText(const QByteArray &text) const
-> base::optional<FileLocation> {
	auto bestAvailableVersion = 0ULL;
	auto bestLocation = FileLocation();
	const auto accumulate = [&](
			uint64 version,
			bool isBeta,
			const QJsonObject &map) {
		if (isBeta) {
			LOG(("Update Error: MTP closed beta found."));
			return false;
		}
		bestAvailableVersion = version;
		const auto key = testing() ? "testing" : "released";
		const auto entry = map.constFind(key);
		if (entry == map.constEnd()) {
			LOG(("Update Error: MTP entry not found for version %1."
				).arg(version));
			return false;
		} else if (!(*entry).isString()) {
			LOG(("Update Error: MTP entry is not a string for version %1."
				).arg(version));
			return false;
		}
		const auto full = (*entry).toString();
		const auto start = full.indexOf(':');
		const auto post = full.indexOf('#');
		if (start <= 0 || post < start) {
			LOG(("Update Error: MTP entry '%1' is bad for version %2."
				).arg(full
				).arg(version));
			return false;
		}
		bestLocation.username = full.mid(start + 1, post - start - 1);
		bestLocation.postId = full.mid(post + 1).toInt();
		if (bestLocation.username.isEmpty() || !bestLocation.postId) {
			LOG(("Update Error: MTP entry '%1' is bad for version %2."
				).arg(full
				).arg(version));
			return false;
		}
		return true;
	};
	const auto result = ParseCommonMap(text, testing(), accumulate);
	if (!result) {
		return base::none;
	}
	return validateLatestLocation(bestAvailableVersion, bestLocation);
}

auto MtpChecker::validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const -> FileLocation {
	const auto myVersion = uint64(AppVersion);
	return (availableVersion <= myVersion) ? FileLocation() : location;
}

void MtpChecker::resolvedFiles(
		const MTPcontacts_ResolvedPeer &result,
		const FileLocation &location) {
	Expects(result.type() == mtpc_contacts_resolvedPeer);

	if (const auto channel = ExtractChannel(result)) {
		_mtp.send(
			MTPchannels_GetMessages(
				*channel,
				MTP_vector<MTPInputMessage>(
					1,
					MTP_inputMessageID(MTP_int(location.postId)))),
			[=](const MTPmessages_Messages &result) { gotFile(result); },
			failHandler());
	} else {
		LOG(("Update Error: MTP files channel not found: '%1'."
			).arg(location.username));
		fail();
	}
}

void MtpChecker::gotFile(const MTPmessages_Messages &result) {
	if (const auto file = parseFile(result)) {
		done(std::make_shared<MtpLoader>(
			_mtp.instance(),
			file->name,
			file->size,
			file->dcId,
			file->location));
	} else {
		fail();
	}
}

auto MtpChecker::parseFile(const MTPmessages_Messages &result) const
-> base::optional<ParsedFile> {
	const auto message = GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Update Error: MTP file message not found."));
		return base::none;
	}
	const auto &data = message->c_message();
	if (!data.has_media()
		|| data.vmedia.type() != mtpc_messageMediaDocument) {
		LOG(("Update Error: MTP file media not found."));
		return base::none;
	}
	const auto &document = data.vmedia.c_messageMediaDocument();
	if (!document.has_document()
		|| document.vdocument.type() != mtpc_document) {
		LOG(("Update Error: MTP file not found."));
		return base::none;
	}
	const auto &fields = document.vdocument.c_document();
	const auto name = [&] {
		for (const auto &attribute : fields.vattributes.v) {
			if (attribute.type() == mtpc_documentAttributeFilename) {
				const auto &data = attribute.c_documentAttributeFilename();
				return qs(data.vfile_name);
			}
		}
		return QString();
	}();
	if (name.isEmpty()) {
		LOG(("Update Error: MTP file name not found."));
		return base::none;
	}
	const auto size = fields.vsize.v;
	if (size <= 0) {
		LOG(("Update Error: MTP file size is invalid."));
		return base::none;
	}
	const auto location = MTP_inputDocumentFileLocation(
		fields.vid,
		fields.vaccess_hash,
		fields.vversion);
	return ParsedFile { name, size, fields.vdc_id.v, location };
}

base::lambda<void(const RPCError &error)> MtpChecker::failHandler() {
	return [=](const RPCError &error) {
		LOG(("Update Error: MTP check failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		fail();
	};
}

MtpLoader::MtpLoader(
	QPointer<MTP::Instance> instance,
	const QString &name,
	int32 size,
	MTP::DcId dcId,
	const MTPInputFileLocation &location)
: Loader(name, kChunkSize)
, _size(size)
, _dcId(dcId)
, _location(location)
, _mtp(instance) {
	Expects(_size > 0);
}

void MtpLoader::startLoading() {
	if (!_mtp.valid()) {
		LOG(("Update Error: MTP is unavailable."));
		threadSafeFailed();
		return;
	}

	LOG(("Update Info: Loading using MTP from '%1'.").arg(_dcId));
	_offset = alreadySize();
	writeChunk({}, _size);
	sendRequest();
}

void MtpLoader::sendRequest() {
	if (_requests.size() >= kRequestsCount || _offset >= _size) {
		return;
	}
	const auto offset = _offset;
	_requests.push_back({ offset });
	_mtp.send(
		MTPupload_GetFile(_location, MTP_int(offset), MTP_int(kChunkSize)),
		[=](const MTPupload_File &result) { gotPart(offset, result); },
		failHandler(),
		MTP::updaterDcId(_dcId));
	_offset += kChunkSize;

	if (_requests.size() < kRequestsCount) {
		App::CallDelayed(kNextRequestDelay, this, [=] { sendRequest(); });
	}
}

void MtpLoader::gotPart(int offset, const MTPupload_File &result) {
	Expects(!_requests.empty());

	if (result.type() == mtpc_upload_fileCdnRedirect) {
		LOG(("Update Error: MTP does not support cdn right now."));
		threadSafeFailed();
		return;
	}
	const auto &data = result.c_upload_file();
	if (data.vbytes.v.isEmpty()) {
		LOG(("Update Error: MTP empty part received."));
		threadSafeFailed();
		return;
	}

	const auto i = ranges::find(
		_requests,
		offset,
		[](const Request &request) { return request.offset; });
	Assert(i != end(_requests));

	i->bytes = data.vbytes.v;
	while (!_requests.empty() && !_requests.front().bytes.isEmpty()) {
		writeChunk(bytes::make_span(_requests.front().bytes), _size);
		_requests.pop_front();
	}
	sendRequest();
}

base::lambda<void(const RPCError &)> MtpLoader::failHandler() {
	return [=](const RPCError &error) {
		LOG(("Update Error: MTP load failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		threadSafeFailed();
	};
}

} // namespace

class Updater : public base::has_weak_ptr {
public:
	Updater();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait);
	void stop();
	void test();

	State state() const;
	int already() const;
	int size() const;

	void setMtproto(const QPointer<MTP::Instance> &mtproto);

	~Updater();

private:
	enum class Action {
		Waiting,
		Checking,
		Loading,
		Unpacking,
		Ready,
	};
	void check();
	void startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker);
	void tryLoaders();
	void handleTimeout();
	void checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader);
	void checkerFail(not_null<Implementation*> which);

	void finalize(QString filepath);
	void unpackDone(bool ready);
	void handleChecking();
	void handleProgress();
	void handleLatest();
	void handleFailed();
	void handleReady();
	void scheduleNext();

	bool _testing = false;
	Action _action = Action::Waiting;
	base::Timer _timer;
	base::Timer _retryTimer;
	rpl::event_stream<> _checking;
	rpl::event_stream<> _isLatest;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<> _failed;
	rpl::event_stream<> _ready;
	Implementation _httpImplementation;
	Implementation _mtpImplementation;
	std::shared_ptr<Loader> _activeLoader;
	bool _usingMtprotoLoader = (cBetaVersion() != 0);
	QPointer<MTP::Instance> _mtproto;

	rpl::lifetime _lifetime;

};

Updater::Updater()
: _timer([=] { check(); })
, _retryTimer([=] { handleTimeout(); }) {
	checking() | rpl::start_with_next([=] {
		handleChecking();
	}, _lifetime);
	progress() | rpl::start_with_next([=] {
		handleProgress();
	}, _lifetime);
	failed() | rpl::start_with_next([=] {
		handleFailed();
	}, _lifetime);
	ready() | rpl::start_with_next([=] {
		handleReady();
	}, _lifetime);
	isLatest() | rpl::start_with_next([=] {
		handleLatest();
	}, _lifetime);
}

rpl::producer<> Updater::checking() const {
	return _checking.events();
}

rpl::producer<> Updater::isLatest() const {
	return _isLatest.events();
}

auto Updater::progress() const
-> rpl::producer<Progress> {
	return _progress.events();
}

rpl::producer<> Updater::failed() const {
	return _failed.events();
}

rpl::producer<> Updater::ready() const {
	return _ready.events();
}

void Updater::check() {
	start(false);
}

void Updater::handleReady() {
	stop();
	_action = Action::Ready;

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}

void Updater::handleFailed() {
	scheduleNext();
}

void Updater::handleLatest() {
	if (const auto update = FindUpdateFile(); !update.isEmpty()) {
		QFile(update).remove();
	}
	scheduleNext();
}

void Updater::handleChecking() {
	_action = Action::Checking;
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::handleProgress() {
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::scheduleNext() {
	stop();

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
	start(true);
}

auto Updater::state() const -> State {
	if (_action == Action::Ready) {
		return State::Ready;
	} else if (_action == Action::Loading) {
		return State::Download;
	}
	return State::None;
}

int Updater::size() const {
	return _activeLoader ? _activeLoader->totalSize() : 0;
}

int Updater::already() const {
	return _activeLoader ? _activeLoader->alreadySize() : 0;
}

void Updater::stop() {
	_httpImplementation = Implementation();
	_mtpImplementation = Implementation();
	_activeLoader = nullptr;
	_action = Action::Waiting;
}

void Updater::start(bool forceWait) {
	if (!Sandbox::started() || cExeName().isEmpty()) {
		return;
	}

	_timer.cancel();
	if (!cAutoUpdate() || _action != Action::Waiting) {
		return;
	}

	_retryTimer.cancel();
	const auto constDelay = cBetaVersion() ? 60 : UpdateDelayConstPart;
	const auto randDelay = cBetaVersion() ? 30 : UpdateDelayRandPart;
	const auto updateInSecs = cLastUpdateCheck()
		+ constDelay
		+ int(rand() % randDelay)
		- unixtime();
	auto sendRequest = (updateInSecs <= 0)
		|| (updateInSecs > constDelay + randDelay);
	if (!sendRequest && !forceWait) {
		if (!FindUpdateFile().isEmpty()) {
			sendRequest = true;
		}
	}
	if (cManyInstance() && !cDebug()) {
		// Only main instance is updating.
		return;
	}

	if (sendRequest) {
		startImplementation(
			&_httpImplementation,
			std::make_unique<HttpChecker>(_testing));
		startImplementation(
			&_mtpImplementation,
			(cBetaVersion()
				? std::make_unique<MtpChecker>(_mtproto, _testing)
				: nullptr));

		_checking.fire({});
	} else {
		_timer.callOnce((updateInSecs + 5) * TimeMs(1000));
	}
}

void Updater::startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker) {
	if (!checker) {
		class EmptyChecker : public Checker {
		public:
			EmptyChecker() : Checker(false) {
			}

			void start() override {
				crl::on_main(this, [=] { fail(); });
			}

		};
		checker = std::make_unique<EmptyChecker>();
	}

	checker->ready(
	) | rpl::start_with_next([=](std::shared_ptr<Loader> &&loader) {
		checkerDone(which, std::move(loader));
	}, checker->lifetime());
	checker->failed(
	) | rpl::start_with_next([=] {
		checkerFail(which);
	}, checker->lifetime());

	*which = Implementation{ std::move(checker) };

	crl::on_main(which->checker.get(), [=] {
		which->checker->start();
	});
}

void Updater::checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader) {
	which->checker = nullptr;
	which->loader = std::move(loader);

	tryLoaders();
}

void Updater::checkerFail(not_null<Implementation*> which) {
	which->checker = nullptr;
	which->failed = true;

	tryLoaders();
}

void Updater::test() {
	_testing = true;
	cSetLastUpdateCheck(0);
	start(false);
}

void Updater::setMtproto(const QPointer<MTP::Instance> &mtproto) {
	_mtproto = mtproto;

}

void Updater::handleTimeout() {
	if (_action == Action::Checking) {
		const auto reset = [&](Implementation &which) {
			if (base::take(which.checker)) {
				which.failed = true;
			}
		};
		reset(_httpImplementation);
		reset(_mtpImplementation);
		tryLoaders();
	} else if (_action == Action::Loading) {
		_failed.fire({});
	}
	if (_action == Action::Waiting) {
		cSetLastUpdateCheck(0);
		_timer.callOnce(kUpdaterTimeout);
	}
}

void Updater::tryLoaders() {
	if (_httpImplementation.checker || _mtpImplementation.checker) {
		// Some checkers didn't finish yet.
		return;
	}
	_retryTimer.cancel();

	const auto tryOne = [&](Implementation &which) {
		_activeLoader = std::move(which.loader);
		if (const auto loader = _activeLoader.get()) {
			_action = Action::Loading;

			loader->progress(
			) | rpl::start_to_stream(_progress, loader->lifetime());
			loader->ready(
			) | rpl::start_with_next([=](QString &&filepath) {
				finalize(std::move(filepath));
			}, loader->lifetime());
			loader->failed(
			) | rpl::start_with_next([=] {
				_failed.fire({});
			}, loader->lifetime());

			_retryTimer.callOnce(kUpdaterTimeout);
			loader->start();
		} else {
			_isLatest.fire({});
		}
	};
	if (_mtpImplementation.failed && _httpImplementation.failed) {
		_failed.fire({});
	} else if (!_mtpImplementation.loader) {
		tryOne(_httpImplementation);
	} else if (!_httpImplementation.loader) {
		tryOne(_mtpImplementation);
	} else {
		tryOne(_usingMtprotoLoader
			? _mtpImplementation
			: _httpImplementation);
		_usingMtprotoLoader = !_usingMtprotoLoader;
	}
}

void Updater::finalize(QString filepath) {
	if (_action != Action::Loading) {
		return;
	}
	_retryTimer.cancel();
	_activeLoader = nullptr;
	_action = Action::Unpacking;
	crl::async([=] {
		const auto ready = UnpackUpdate(filepath);
		crl::on_main([=] {
			GetUpdaterInstance()->unpackDone(ready);
		});
	});
}

void Updater::unpackDone(bool ready) {
	if (ready) {
		_ready.fire({});
	} else {
		ClearAll();
		_failed.fire({});
	}
}

Updater::~Updater() {
	stop();
}

UpdateChecker::UpdateChecker()
: _updater(GetUpdaterInstance()) {
	if (const auto messenger = Messenger::InstancePointer()) {
		if (const auto mtproto = messenger->mtp()) {
			_updater->setMtproto(mtproto);
		}
	}
}

rpl::producer<> UpdateChecker::checking() const {
	return _updater->checking();
}

rpl::producer<> UpdateChecker::isLatest() const {
	return _updater->isLatest();
}

auto UpdateChecker::progress() const
-> rpl::producer<Progress> {
	return _updater->progress();
}

rpl::producer<> UpdateChecker::failed() const {
	return _updater->failed();
}

rpl::producer<> UpdateChecker::ready() const {
	return _updater->ready();
}

void UpdateChecker::start(bool forceWait) {
	_updater->start(forceWait);
}

void UpdateChecker::test() {
	_updater->test();
}

void UpdateChecker::setMtproto(const QPointer<MTP::Instance> &mtproto) {
	_updater->setMtproto(mtproto);
}

void UpdateChecker::stop() {
	_updater->stop();
}

auto UpdateChecker::state() const
-> State {
	return _updater->state();
}

int UpdateChecker::already() const {
	return _updater->already();
}

int UpdateChecker::size() const {
	return _updater->size();
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

bool checkReadyUpdate() {
	QString readyFilePath = cWorkingDir() + qsl("tupdates/temp/ready"), readyPath = cWorkingDir() + qsl("tupdates/temp");
	if (!QFile(readyFilePath).exists() || cExeName().isEmpty()) {
		if (QDir(cWorkingDir() + qsl("tupdates/ready")).exists() || QDir(cWorkingDir() + qsl("tupdates/temp")).exists()) {
			ClearAll();
		}
		return false;
	}

	// check ready version
	QString versionPath = readyPath + qsl("/tdata/version");
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'").arg(versionPath));
			ClearAll();
			return false;
		}
		auto versionNum = VersionInt();
		if (fVersion.read((char*)&versionNum, sizeof(VersionInt)) != sizeof(VersionInt)) {
			LOG(("Update Error: cant read version from file '%1'").arg(versionPath));
			ClearAll();
			return false;
		}
		if (versionNum == 0x7FFFFFFF) { // beta version
			quint64 betaVersion = 0;
			if (fVersion.read((char*)&betaVersion, sizeof(quint64)) != sizeof(quint64)) {
				LOG(("Update Error: cant read beta version from file '%1'").arg(versionPath));
				ClearAll();
				return false;
			}
			if (!cBetaVersion() || betaVersion <= cBetaVersion()) {
				LOG(("Update Error: cant install beta version %1 having beta version %2").arg(betaVersion).arg(cBetaVersion()));
				ClearAll();
				return false;
			}
		} else if (versionNum <= AppVersion) {
			LOG(("Update Error: cant install version %1 having version %2").arg(versionNum).arg(AppVersion));
			ClearAll();
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
			ClearAll();
			return false;
		}
		if (!QFile(current.absoluteFilePath()).copy(updater.absoluteFilePath())) {
			ClearAll();
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
			ClearAll();
			return false;
		}
	}
	if (DeleteFile(updater.absoluteFilePath().toStdWString().c_str()) == FALSE) {
		ClearAll();
		return false;
	}
#elif defined Q_OS_MAC // Q_OS_WIN
	QDir().mkpath(QFileInfo(curUpdater).absolutePath());
	DEBUG_LOG(("Update Info: moving %1 to %2...").arg(updater.absoluteFilePath()).arg(curUpdater));
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		ClearAll();
		return false;
	}
#elif defined Q_OS_LINUX // Q_OS_MAC
	if (!linuxMoveFile(QFile::encodeName(updater.absoluteFilePath()).constData(), QFile::encodeName(curUpdater).constData())) {
		ClearAll();
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

} // namespace Core
