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
#include "storage/localstorage.h"

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

constexpr auto kCheckTimeout = TimeMs(10000);

#ifdef Q_OS_WIN
using VersionInt = DWORD;
using VersionChar = WCHAR;
#else // Q_OS_WIN
using VersionInt = int;
using VersionChar = wchar_t;
#endif // Q_OS_WIN

using ErrorSignal = void(QNetworkReply::*)(QNetworkReply::NetworkError);
const auto QNetworkReply_error = ErrorSignal(&QNetworkReply::error);

std::weak_ptr<Updater> UpdaterInstance;

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

} // namespace

class Updater : public base::has_weak_ptr {
public:
	using Progress = UpdateChecker::Progress;
	using State = UpdateChecker::State;

	Updater();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait);
	void stop();

	State state() const;
	int already() const;
	int size() const;

	// Thread-safe methods.
	void onProgress(Progress progress);
	void onFailed();
	void onReady();

	~Updater();

private:
	class Private;

	void check();
	void handleFailed();
	void handleReady();
	void gotResponse();
	void gotFailure(QNetworkReply::NetworkError e);
	void clearSentRequest();
	void requestTimeout();

	bool _isReady = false;
	base::Timer _timer;
	base::Timer _retryTimer;
	rpl::event_stream<> _checking;
	rpl::event_stream<> _isLatest;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<> _failed;
	rpl::event_stream<> _ready;
	std::unique_ptr<QNetworkAccessManager> _manager;
	QNetworkReply *_reply = nullptr;
	std::unique_ptr<QThread> _thread;
	Private *_private = nullptr;

	rpl::lifetime _lifetime;

};

class Updater::Private : public QObject {
public:
	Private(
		not_null<Updater*> parent,
		not_null<QThread*> thread,
		const QString &url);

	void unpackUpdate();

	// Thread-safe methods.
	int already() const;
	int size() const;

private:
	void start();
	void sendRequest();
	void initOutput();

	void gotMetaData();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);

	void fatalFail();

	not_null<Updater*> _parent;
	QString _url;
	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _reply;
	int _already = 0;
	int _size = 0;
	QFile _output;

	mutable QMutex _mutex;

};

Updater::Updater()
: _timer([=] { check(); })
, _retryTimer([=] { requestTimeout(); }) {
	failed() | rpl::start_with_next([=] {
		handleFailed();
	}, _lifetime);
	ready() | rpl::start_with_next([=] {
		handleReady();
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

void Updater::onProgress(Progress progress) {
	crl::on_main(this, [=] {
		_progress.fire_copy(progress);
	});
}

void Updater::onFailed() {
	crl::on_main(this, [=] {
		_failed.fire({});
	});
}

void Updater::onReady() {
	crl::on_main(this, [=] {
		_ready.fire({});
	});
}

void Updater::check() {
	start(false);
}

void Updater::gotResponse() {
	if (!_reply || _thread) {
		return;
	}

	cSetLastUpdateCheck(unixtime());
	const auto m = QRegularExpression(qsl("^\\s*(\\d+)\\s*:\\s*([\\x21-\\x7f]+)\\s*$")).match(QString::fromLatin1(_reply->readAll()));
	if (m.hasMatch()) {
		uint64 currentVersion = m.captured(1).toULongLong();
		QString url = m.captured(2);
		bool betaVersion = false;
		if (url.startsWith(qstr("beta_"))) {
			betaVersion = true;
			url = url.mid(5) + '_' + Core::countBetaVersionSignature(currentVersion);
		}
		if ((!betaVersion || cBetaVersion()) && currentVersion > (betaVersion ? cBetaVersion() : uint64(AppVersion))) {
			_thread = std::make_unique<QThread>();
			_private = new Private(this, _thread.get(), url);
			_thread->start();
		}
	}
	clearSentRequest();
	if (!_thread) {
		if (const auto update = FindUpdateFile(); !update.isEmpty()) {
			QFile(update).remove();
		}
		_isLatest.fire({});
	}
	start(true);
	Local::writeSettings();
}

void Updater::gotFailure(QNetworkReply::NetworkError e) {
	LOG(("App Error: could not get current version (update check): %1").arg(e));
	if (const auto reply = base::take(_reply)) {
		reply->deleteLater();
	}

	_failed.fire({});
	start(true);
}

void Updater::handleReady() {
	_isReady = true;
	stop();

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}

void Updater::handleFailed() {
	stop();

	cSetLastUpdateCheck(unixtime());
	Local::writeSettings();
}

auto Updater::state() const -> State {
	if (_isReady) {
		return State::Ready;
	} else if (!_thread) {
		return State::None;
	}
	return State::Download;
}

int Updater::size() const {
	return _private ? _private->size() : 0;
}

int Updater::already() const {
	return _private ? _private->already() : 0;
}

void Updater::clearSentRequest() {
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

void Updater::stop() {
	clearSentRequest();
	if (const auto checker = base::take(_private)) {
		InvokeQueued(checker, [=] { checker->deleteLater(); });
		if (const auto thread = base::take(_thread)) {
			thread->quit();
			thread->wait();
		}
	}
}

void Updater::start(bool forceWait) {
	if (!Sandbox::started() || _isReady) {
		return;
	}

	_timer.cancel();
	if (_thread || _reply || !cAutoUpdate() || cExeName().isEmpty()) {
		return;
	}

	_retryTimer.cancel();
	const auto constDelay = cBetaVersion() ? 600 : UpdateDelayConstPart;
	const auto randDelay = cBetaVersion() ? 300 : UpdateDelayRandPart;
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
		clearSentRequest();

		auto url = QUrl(cUpdateURL());
		if (cBetaVersion()) {
			url.setQuery(qsl("version=%1&beta=%2"
			).arg(AppVersion
			).arg(cBetaVersion()));
		} else if (cAlphaVersion()) {
			url.setQuery(qsl("version=%1&alpha=1").arg(AppVersion));
		} else {
			url.setQuery(qsl("version=%1").arg(AppVersion));
		}
		DEBUG_LOG(("App Info: requesting update state from '%1'"
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
		_checking.fire({});
		_retryTimer.callOnce(kCheckTimeout);
	} else {
		_timer.callOnce((updateInSecs + 5) * TimeMs(1000));
	}
}

void Updater::requestTimeout() {
	if (_reply) {
		stop();
		_failed.fire({});
		cSetLastUpdateCheck(0);
		_timer.callOnce(kCheckTimeout);
	}
}

Updater::~Updater() {
	stop();
}

UpdateChecker::UpdateChecker()
: _updater(GetUpdaterInstance()) {
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

Updater::Private::Private(
		not_null<Updater*> parent,
		not_null<QThread*> thread,
		const QString &url)
: _parent(parent) {
	_url = url;
	moveToThread(thread);
	_manager.moveToThread(thread);

	connect(thread, &QThread::started, this, [=] { start(); });
	initOutput();
}

void Updater::Private::initOutput() {
	auto fileName = QString();
	const auto nameRegExp = QRegularExpression(qsl("/([^/\\?]+)(\\?|$)"));
	const auto nameMatch = nameRegExp.match(_url);

	if (nameMatch.hasMatch()) {
		fileName = nameMatch.captured(1).replace(
			QRegularExpression(qsl("[^a-zA-Z0-9_\\-]")),
			QString());
	} else if (fileName.isEmpty()) {
		fileName = qsl("tupdate-%1").arg(rand() % 1000000);
	}
	const auto folder = cWorkingDir() + qsl("tupdates/");
	const auto finalName = folder + fileName;

	QFileInfo info(finalName);
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
	_output.setFileName(finalName);
	if (info.exists()) {
		uint64 fullSize = info.size();
		if (fullSize < INT_MAX) {
			int32 goodSize = (int32)fullSize;
			if (goodSize % UpdateChunk) {
				goodSize = goodSize - (goodSize % UpdateChunk);
				if (goodSize) {
					if (_output.open(QIODevice::ReadOnly)) {
						QByteArray goodData = _output.readAll().mid(0, goodSize);
						_output.close();
						if (_output.open(QIODevice::WriteOnly)) {
							_output.write(goodData);
							_output.close();

							QMutexLocker lock(&_mutex);
							_already = goodSize;
						}
					}
				}
			} else {
				QMutexLocker lock(&_mutex);
				_already = goodSize;
			}
		}
		if (!_already) {
			QFile::remove(finalName);
		}
	}
}

void Updater::Private::start() {
	sendRequest();
}

void Updater::Private::sendRequest() {
	auto request = QNetworkRequest(_url);
	const auto rangeHeaderValue = "bytes="
		+ QByteArray::number(_already)
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
		&Private::partFinished);
	connect(_reply.get(), QNetworkReply_error, [=](auto error) {
		partFailed(error);
	});
	connect(_reply.get(), &QNetworkReply::metaDataChanged, [=] {
		gotMetaData();
	});
}

void Updater::Private::gotMetaData() {
	const auto pairs = _reply->rawHeaderPairs();
	for (const auto pair : pairs) {
		if (QString::fromUtf8(pair.first).toLower() == "content-range") {
			const auto m = QRegularExpression(qsl("/(\\d+)([^\\d]|$)")).match(QString::fromUtf8(pair.second));
			if (m.hasMatch()) {
				{
					QMutexLocker lock(&_mutex);
					_size = m.captured(1).toInt();
				}
				_parent->onProgress({ _already, _size });
			}
		}
	}
}

int Updater::Private::already() const {
	QMutexLocker lock(&_mutex);
	return _already;
}

int Updater::Private::size() const {
	QMutexLocker lock(&_mutex);
	return _size;
}

void Updater::Private::partFinished(qint64 got, qint64 total) {
	if (!_reply) return;

	QVariant statusCode = _reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: Bad HTTP status received in partFinished(): %1").arg(status));
			return fatalFail();
		}
	}

	if (!_already && !_size) {
		QMutexLocker lock(&_mutex);
		_size = total;
	}
	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	if (!_output.isOpen()) {
		if (!_output.open(QIODevice::Append)) {
			LOG(("Update Error: Could not open output file '%1' for appending").arg(_output.fileName()));
			return fatalFail();
		}
	}
	QByteArray r = _reply->readAll();
	if (!r.isEmpty()) {
		_output.write(r);

		QMutexLocker lock(&_mutex);
		_already += r.size();
	}
	if (got >= total) {
		_reply.release()->deleteLater();
		_output.close();
		unpackUpdate();
	} else {
		_parent->onProgress({ _already, _size });
	}
}

void Updater::Private::partFailed(QNetworkReply::NetworkError e) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	_reply.release()->deleteLater();
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			_output.close();
			unpackUpdate();
			return;
		}
	}
	LOG(("Update Error: failed to download part starting from %1, error %2").arg(_already).arg(e));
	_parent->onFailed();
}

void Updater::Private::fatalFail() {
	ClearAll();
	_parent->onFailed();
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

void Updater::Private::unpackUpdate() {
	QByteArray packed;
	if (!_output.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return fatalFail();
	}

#ifdef Q_OS_WIN // use Lzma SDK for win
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = LZMA_PROPS_SIZE, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hPropsLen + hOriginalSizeLen; // header
#else // Q_OS_WIN
	const int32 hSigLen = 128, hShaLen = 20, hPropsLen = 0, hOriginalSizeLen = sizeof(int32), hSize = hSigLen + hShaLen + hOriginalSizeLen; // header
#endif // Q_OS_WIN

	QByteArray compressed = _output.readAll();
	int32 compressedLen = compressed.size() - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(compressed.size()));
		return fatalFail();
	}
	_output.close();

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

		const auto versionNum = VersionInt(version);
		const auto versionLen = VersionInt(versionString.size() * sizeof(VersionChar));
		VersionChar versionStr[32];
		memcpy(versionStr, versionString.c_str(), versionLen);

		QFile fVersion(tempDirPath + qsl("/tdata/version"));
		if (!fVersion.open(QIODevice::WriteOnly)) {
			LOG(("Update Error: cant write version file '%1'").arg(tempDirPath + qsl("/version")));
			return fatalFail();
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
			return fatalFail();
		}
	} else {
		LOG(("Update Error: cant create ready file '%1'").arg(readyFilePath));
		return fatalFail();
	}
	_output.remove();

	_parent->onReady();
}

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
