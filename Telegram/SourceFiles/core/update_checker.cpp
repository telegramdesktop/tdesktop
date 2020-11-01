/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_checker.h"

#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "base/unixtime.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "core/click_handler_types.h"
#include "mainwindow.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "info/info_memento.h"
#include "info/settings/info_settings_widget.h"
#include "window/window_session_controller.h"
#include "settings/settings_intro.h"
#include "ui/layers/box_content.h"
#include "app.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

extern "C" {
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
} // extern "C"

#if defined Q_OS_WIN && !defined DESKTOP_APP_USE_PACKAGED // use Lzma SDK for win
#include <LzmaLib.h>
#else // Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED
#include <lzma.h>
#endif // else of Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED

namespace Core {
namespace {

constexpr auto kUpdaterTimeout = 10 * crl::time(1000);
constexpr auto kMaxResponseSize = 1024 * 1024;

#ifdef TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = true;
#else // TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE

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

using Loader = MTP::AbstractDedicatedLoader;

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
	std::optional<QString> parseOldResponse(
		const QByteArray &response) const;
	std::optional<QString> parseResponse(const QByteArray &response) const;
	QString validateLatestUrl(
		uint64 availableVersion,
		bool isAvailableAlpha,
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

class MtpChecker : public Checker {
public:
	MtpChecker(base::weak_ptr<Main::Session> session, bool testing);

	void start() override;

private:
	using FileLocation = MTP::DedicatedLoader::Location;

	using Checker::fail;
	Fn<void(const RPCError &error)> failHandler();

	void gotMessage(const MTPmessages_Messages &result);
	std::optional<FileLocation> parseMessage(
		const MTPmessages_Messages &result) const;
	std::optional<FileLocation> parseText(const QByteArray &text) const;
	FileLocation validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const;

	MTP::WeakInstance _mtp;

};

std::shared_ptr<Updater> GetUpdaterInstance() {
	if (const auto result = UpdaterInstance.lock()) {
		return result;
	}
	const auto result = std::make_shared<Updater>();
	UpdaterInstance = result;
	return result;
}

QString UpdatesFolder() {
	return cWorkingDir() + qsl("tupdates");
}

void ClearAll() {
	base::Platform::DeleteDirectory(UpdatesFolder());
}

QString FindUpdateFile() {
	QDir updates(UpdatesFolder());
	if (!updates.exists()) {
		return QString();
	}
	const auto list = updates.entryInfoList(QDir::Files);
	for (const auto &info : list) {
		if (QRegularExpression(
			"^("
			"tupdate|"
			"tmacupd|"
			"tosxupd|"
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

#if defined Q_OS_WIN && !defined DESKTOP_APP_USE_PACKAGED // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else // Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif // Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED

	QByteArray compressed = input.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return false;
	}
	input.close();

	QString tempDirPath = cWorkingDir() + qsl("tupdates/temp"), readyFilePath = cWorkingDir() + qsl("tupdates/temp/ready");
	base::Platform::DeleteDirectory(tempDirPath);

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

	RSA *pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppBetaVersion ? UpdatesPublicBetaKey : UpdatesPublicKey), -1), 0, 0, 0);
	if (!pbKey) {
		LOG(("Update Error: cant read public rsa key!"));
		return false;
	}
	if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
		RSA_free(pbKey);

		// try other public key, if we update from beta to stable or vice versa
		pbKey = PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(AppBetaVersion ? UpdatesPublicKey : UpdatesPublicBetaKey), -1), 0, 0, 0);
		if (!pbKey) {
			LOG(("Update Error: cant read public rsa key!"));
			return false;
		}
		if (RSA_verify(NID_sha1, (const uchar*)(compressed.constData() + hSigLen), hShaLen, (const uchar*)(compressed.constData()), hSigLen, pbKey) != 1) { // verify signature
			RSA_free(pbKey);
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
#if defined Q_OS_WIN && !defined DESKTOP_APP_USE_PACKAGED // use Lzma SDK for win
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(compressed.constData() + hSize), &srcLen, (const uchar*)(compressed.constData() + hSigLen + hShaLen), LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return false;
	}
#else // Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED
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
#endif // Q_OS_WIN && !DESKTOP_APP_USE_PACKAGED

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

		quint64 alphaVersion = 0;
		if (version == 0x7FFFFFFF) { // alpha version
			stream >> alphaVersion;
			if (stream.status() != QDataStream::Ok) {
				LOG(("Update Error: cant read alpha version from downloaded stream, status: %1").arg(stream.status()));
				return false;
			}
			if (!cAlphaVersion() || alphaVersion <= cAlphaVersion()) {
				LOG(("Update Error: downloaded alpha version %1 is not greater, than mine %2").arg(alphaVersion).arg(cAlphaVersion()));
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
#ifdef Q_OS_UNIX
			stream >> executable;
#endif // Q_OS_UNIX
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
		std::wstring versionString = FormatVersionDisplay(version).toStdWString();

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
		if (versionNum == 0x7FFFFFFF) { // alpha version
			fVersion.write((const char*)&alphaVersion, sizeof(quint64));
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
	const auto platform = Platform::AutoUpdateKey();
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
		if (cAlphaVersion()) {
			return { "alpha", "beta", "stable" };
		} else if (cInstallBetaVersion()) {
			return { "beta", "stable" };
		}
		return { "stable" };
	}();
	auto bestIsAvailableAlpha = false;
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
		const auto isAvailableAlpha = (type == "alpha");
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
		const auto compare = isAvailableAlpha
			? availableVersion
			: availableVersion * 1000;
		const auto bestCompare = bestIsAvailableAlpha
			? bestAvailableVersion
			: bestAvailableVersion * 1000;
		if (compare > bestCompare) {
			bestAvailableVersion = availableVersion;
			bestIsAvailableAlpha = isAvailableAlpha;
			if (!callback(availableVersion, isAvailableAlpha, map)) {
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
	const auto updaterVersion = Platform::AutoUpdateVersion();
	const auto path = Local::readAutoupdatePrefix()
		+ qstr("/current")
		+ (updaterVersion > 1 ? QString::number(updaterVersion) : QString());
	auto url = QUrl(path);
	DEBUG_LOG(("Update Info: requesting update state"));
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

	cSetLastUpdateCheck(base::unixtime::now());
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

std::optional<QString> HttpChecker::parseOldResponse(
		const QByteArray &response) const {
	const auto string = QString::fromLatin1(response);
	const auto old = QRegularExpression(
		qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")
	).match(string);
	if (!old.hasMatch()) {
		return std::nullopt;
	}
	const auto availableVersion = old.captured(1).toULongLong();
	const auto url = old.captured(2);
	const auto isAvailableAlpha = url.startsWith(qstr("beta_"));
	return validateLatestUrl(
		availableVersion,
		isAvailableAlpha,
		isAvailableAlpha ? url.mid(5) + "_{signature}" : url);
}

std::optional<QString> HttpChecker::parseResponse(
		const QByteArray &response) const {
	auto bestAvailableVersion = 0ULL;
	auto bestIsAvailableAlpha = false;
	auto bestLink = QString();
	const auto accumulate = [&](
			uint64 version,
			bool isAlpha,
			const QJsonObject &map) {
		bestAvailableVersion = version;
		bestIsAvailableAlpha = isAlpha;
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
		return std::nullopt;
	}
	return validateLatestUrl(
		bestAvailableVersion,
		bestIsAvailableAlpha,
		Local::readAutoupdatePrefix() + bestLink);
}

QString HttpChecker::validateLatestUrl(
		uint64 availableVersion,
		bool isAvailableAlpha,
		QString url) const {
	const auto myVersion = isAvailableAlpha
		? cAlphaVersion()
		: uint64(AppVersion);
	const auto validVersion = (cAlphaVersion() || !isAvailableAlpha);
	if (!validVersion || availableVersion <= myVersion) {
		return QString();
	}
	const auto versionUrl = url.replace(
		"{version}",
		QString::number(availableVersion));
	const auto finalUrl = isAvailableAlpha
		? QString(versionUrl).replace(
			"{signature}",
			countAlphaVersionSignature(availableVersion))
		: versionUrl;
	return finalUrl;
}

HttpChecker::~HttpChecker() {
	clearSentRequest();
}

HttpLoader::HttpLoader(const QString &url)
: Loader(UpdatesFolder() + '/' + ExtractFilename(url), kChunkSize)
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

MtpChecker::MtpChecker(
	base::weak_ptr<Main::Session> session,
	bool testing)
: Checker(testing)
, _mtp(session) {
}

void MtpChecker::start() {
	if (!_mtp.valid()) {
		LOG(("Update Info: MTP is unavailable."));
		crl::on_main(this, [=] { fail(); });
		return;
	}
	const auto updaterVersion = Platform::AutoUpdateVersion();
	const auto feed = "tdhbcfeed"
		+ (updaterVersion > 1 ? QString::number(updaterVersion) : QString());
	MTP::ResolveChannel(&_mtp, feed, [=](
			const MTPInputChannel &channel) {
		_mtp.send(
			MTPmessages_GetHistory(
				MTP_inputPeerChannel(
					channel.c_inputChannel().vchannel_id(),
					channel.c_inputChannel().vaccess_hash()),
				MTP_int(0),  // offset_id
				MTP_int(0),  // offset_date
				MTP_int(0),  // add_offset
				MTP_int(1),  // limit
				MTP_int(0),  // max_id
				MTP_int(0),  // min_id
				MTP_int(0)), // hash
			[=](const MTPmessages_Messages &result) { gotMessage(result); },
			failHandler());
	}, [=] { fail(); });
}

void MtpChecker::gotMessage(const MTPmessages_Messages &result) {
	const auto location = parseMessage(result);
	if (!location) {
		fail();
		return;
	} else if (location->username.isEmpty()) {
		done(nullptr);
		return;
	}
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			done(std::move(loader));
		} else {
			fail();
		}
	};
	MTP::StartDedicatedLoader(&_mtp, *location, UpdatesFolder(), ready);
}

auto MtpChecker::parseMessage(const MTPmessages_Messages &result) const
-> std::optional<FileLocation> {
	const auto message = MTP::GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Update Error: MTP feed message not found."));
		return std::nullopt;
	}
	return parseText(message->c_message().vmessage().v);
}

auto MtpChecker::parseText(const QByteArray &text) const
-> std::optional<FileLocation> {
	auto bestAvailableVersion = 0ULL;
	auto bestLocation = FileLocation();
	const auto accumulate = [&](
			uint64 version,
			bool isAlpha,
			const QJsonObject &map) {
		if (isAlpha) {
			LOG(("Update Error: MTP closed alpha found."));
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
		return std::nullopt;
	}
	return validateLatestLocation(bestAvailableVersion, bestLocation);
}

auto MtpChecker::validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const -> FileLocation {
	const auto myVersion = uint64(AppVersion);
	return (availableVersion <= myVersion) ? FileLocation() : location;
}

Fn<void(const RPCError &error)> MtpChecker::failHandler() {
	return [=](const RPCError &error) {
		LOG(("Update Error: MTP check failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		fail();
	};
}

} // namespace

bool UpdaterDisabled() {
	return UpdaterIsDisabled;
}

void SetUpdaterDisabledAtStartup() {
	Expects(UpdaterInstance.lock() == nullptr);

	UpdaterIsDisabled = true;
}

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

	void setMtproto(base::weak_ptr<Main::Session> session);

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
	bool tryLoaders();
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
	bool _usingMtprotoLoader = (cAlphaVersion() != 0);
	base::weak_ptr<Main::Session> _session;

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
	if (!App::quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
	}
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
	if (!App::quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
		start(true);
	}
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
	if (cExeName().isEmpty()) {
		return;
	}

	_timer.cancel();
	if (!cAutoUpdate() || _action != Action::Waiting) {
		return;
	}

	_retryTimer.cancel();
	const auto constDelay = cAlphaVersion() ? 600 : UpdateDelayConstPart;
	const auto randDelay = cAlphaVersion() ? 300 : UpdateDelayRandPart;
	const auto updateInSecs = cLastUpdateCheck()
		+ constDelay
		+ int(rand() % randDelay)
		- base::unixtime::now();
	auto sendRequest = (updateInSecs <= 0)
		|| (updateInSecs > constDelay + randDelay);
	if (!sendRequest && !forceWait) {
		if (!FindUpdateFile().isEmpty()) {
			sendRequest = true;
		}
	}
	if (cManyInstance() && !Logs::DebugEnabled()) {
		// Only main instance is updating.
		return;
	}

	if (sendRequest) {
		startImplementation(
			&_httpImplementation,
			std::make_unique<HttpChecker>(_testing));
		startImplementation(
			&_mtpImplementation,
			std::make_unique<MtpChecker>(_session, _testing));

		_checking.fire({});
	} else {
		_timer.callOnce((updateInSecs + 5) * crl::time(1000));
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

void Updater::setMtproto(base::weak_ptr<Main::Session> session) {
	_session = session;
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
		if (!tryLoaders()) {
			cSetLastUpdateCheck(0);
			_timer.callOnce(kUpdaterTimeout);
		}
	} else if (_action == Action::Loading) {
		_failed.fire({});
	}
}

bool Updater::tryLoaders() {
	if (_httpImplementation.checker || _mtpImplementation.checker) {
		// Some checkers didn't finish yet.
		return true;
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
			loader->wipeFolder();
			loader->start();
		} else {
			_isLatest.fire({});
		}
	};
	if (_mtpImplementation.failed && _httpImplementation.failed) {
		_failed.fire({});
		return false;
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
	return true;
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
	if (IsAppLaunched() && Core::App().domain().started()) {
		if (const auto session = Core::App().activeAccount().maybeSession()) {
			_updater->setMtproto(session);
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

void UpdateChecker::setMtproto(base::weak_ptr<Main::Session> session) {
	_updater->setMtproto(session);
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
		if (versionNum == 0x7FFFFFFF) { // alpha version
			quint64 alphaVersion = 0;
			if (fVersion.read((char*)&alphaVersion, sizeof(quint64)) != sizeof(quint64)) {
				LOG(("Update Error: cant read alpha version from file '%1'").arg(versionPath));
				ClearAll();
				return false;
			}
			if (!cAlphaVersion() || alphaVersion <= cAlphaVersion()) {
				LOG(("Update Error: cant install alpha version %1 having alpha version %2").arg(alphaVersion).arg(cAlphaVersion()));
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
#elif defined Q_OS_UNIX // Q_OS_MAC
	QString curUpdater = (cExeDir() + qsl("Updater"));
	QFileInfo updater(cWorkingDir() + qsl("tupdates/temp/Updater"));
#endif // Q_OS_UNIX
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
#elif defined Q_OS_UNIX // Q_OS_MAC
	if (!linuxMoveFile(QFile::encodeName(updater.absoluteFilePath()).constData(), QFile::encodeName(curUpdater).constData())) {
		ClearAll();
		return false;
	}
#endif // Q_OS_UNIX

#ifdef Q_OS_MAC
	base::Platform::RemoveQuarantine(QFileInfo(curUpdater).absolutePath());
	base::Platform::RemoveQuarantine(updater.absolutePath());
#endif // Q_OS_MAC

	return true;
}

void UpdateApplication() {
	if (UpdaterDisabled()) {
		const auto url = [&] {
#ifdef OS_WIN_STORE
			return "https://www.microsoft.com/en-us/store/p/telegram-desktop/9nztwsqntd0s";
#elif defined OS_MAC_STORE // OS_WIN_STORE
			return "https://itunes.apple.com/ae/app/telegram-desktop/id946399090";
#elif defined Q_OS_UNIX && !defined Q_OS_MAC // OS_WIN_STORE || OS_MAC_STORE
			if (Platform::InFlatpak()) {
				return "https://flathub.org/apps/details/org.telegram.desktop";
			} else if (Platform::InSnap()) {
				return "https://snapcraft.io/telegram-desktop";
			}
			return "https://desktop.telegram.org";
#else // OS_WIN_STORE || OS_MAC_STORE || (defined Q_OS_UNIX && !defined Q_OS_MAC)
			return "https://desktop.telegram.org";
#endif // OS_WIN_STORE || OS_MAC_STORE || (defined Q_OS_UNIX && !defined Q_OS_MAC)
		}();
		UrlClickHandler::Open(url);
	} else {
		cSetAutoUpdate(true);
		if (const auto window = App::wnd()) {
			if (const auto controller = window->sessionController()) {
				controller->showSection(
					Info::Memento(
						Info::Settings::Tag{ controller->session().user() },
						Info::Section::SettingsType::Advanced),
					Window::SectionShow());
			} else {
				window->showSpecialLayer(
					Box<::Settings::LayerWidget>(&window->controller()),
					anim::type::normal);
			}
			window->showFromTray();
		}
		cSetLastUpdateCheck(0);
		Core::UpdateChecker().start();
	}
}

QString countAlphaVersionSignature(uint64 version) { // duplicated in packer.cpp
	if (cAlphaPrivateKey().isEmpty()) {
		LOG(("Error: Trying to count alpha version signature without alpha private key!"));
		return QString();
	}

	QByteArray signedData = (qstr("TelegramBeta_") + QString::number(version, 16).toLower()).toUtf8();

	static const int32 shaSize = 20, keySize = 128;

	uchar sha1Buffer[shaSize];
	hashSha1(signedData.constData(), signedData.size(), sha1Buffer); // count sha1

	uint32 siglen = 0;

	RSA *prKey = PEM_read_bio_RSAPrivateKey(BIO_new_mem_buf(const_cast<char*>(cAlphaPrivateKey().constData()), -1), 0, 0, 0);
	if (!prKey) {
		LOG(("Error: Could not read alpha private key!"));
		return QString();
	}
	if (RSA_size(prKey) != keySize) {
		LOG(("Error: Bad alpha private key size: %1").arg(RSA_size(prKey)));
		RSA_free(prKey);
		return QString();
	}
	QByteArray signature;
	signature.resize(keySize);
	if (RSA_sign(NID_sha1, (const uchar*)(sha1Buffer), shaSize, (uchar*)(signature.data()), &siglen, prKey) != 1) { // count signature
		LOG(("Error: Counting alpha version signature failed!"));
		RSA_free(prKey);
		return QString();
	}
	RSA_free(prKey);

	if (siglen != keySize) {
		LOG(("Error: Bad alpha version signature length: %1").arg(siglen));
		return QString();
	}

	signature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	signature = signature.replace('-', '8').replace('_', 'B');
	return QString::fromUtf8(signature.mid(19, 32));
}

} // namespace Core
