/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_download_web.h"

#include "storage/cache/storage_cache_types.h"
#include "base/qt_adapters.h"

#include <QtNetwork/QAuthenticator>

namespace {

constexpr auto kMaxWebFileQueries = 8;
constexpr auto kMaxHttpRedirects = 5;
constexpr auto kResetDownloadPrioritiesTimeout = crl::time(200);

std::weak_ptr<WebLoadManager> GlobalLoadManager;

[[nodiscard]] std::shared_ptr<WebLoadManager> GetManager() {
	auto result = GlobalLoadManager.lock();
	if (!result) {
		GlobalLoadManager = result = std::make_shared<WebLoadManager>();
	}
	return result;
}

enum class ProcessResult {
	Error,
	Progress,
	Finished,
};

enum class Error {
};

struct Progress {
	qint64 ready = 0;
	qint64 total = 0;
};

using Update = std::variant<Progress, QByteArray, Error>;

struct UpdateForLoader {
	not_null<webFileLoader*> loader;
	Update data;
};

} // namespace

class WebLoadManager final : public QObject {
public:
	WebLoadManager();
	~WebLoadManager();

	void enqueue(not_null<webFileLoader*> loader);
	void remove(not_null<webFileLoader*> loader);

	[[nodiscard]] rpl::producer<Update> updates(
		not_null<webFileLoader*> loader) const;

private:
	struct Enqueued {
		int id = 0;
		QString url;
	};
	struct Sent {
		QString url;
		not_null<QNetworkReply*> reply;
		QByteArray data;
		int64 ready = 0;
		int64 total = 0;
		int redirectsLeft = kMaxHttpRedirects;
	};

	// Constructor.
	void handleNetworkErrors();

	// Worker thread.
	void enqueue(int id, const QString &url);
	void remove(int id);
	void resetGeneration();
	void checkSendNext();
	void send(const Enqueued &entry);
	[[nodiscard]] not_null<QNetworkReply*> send(int id, const QString &url);
	[[nodiscard]] Sent *findSent(int id, not_null<QNetworkReply*> reply);
	void removeSent(int id);
	void progress(
		int id,
		not_null<QNetworkReply*> reply,
		int64 ready,
		int64 total);
	void failed(
		int id,
		not_null<QNetworkReply*> reply,
		QNetworkReply::NetworkError error);
	void redirect(int id, not_null<QNetworkReply*> reply);
	void notify(
		int id,
		not_null<QNetworkReply*> reply,
		int64 ready,
		int64 total);
	void failed(int id, not_null<QNetworkReply*> reply);
	void finished(int id, not_null<QNetworkReply*> reply);
	void deleteDeferred(not_null<QNetworkReply*> reply);
	void queueProgressUpdate(int id, int64 ready, int64 total);
	void queueFailedUpdate(int id);
	void queueFinishedUpdate(int id, const QByteArray &data);
	void clear();

	// Main thread.
	void sendUpdate(int id, Update &&data);

	QThread _thread;
	QNetworkAccessManager _network;
	base::Timer _resetGenerationTimer;

	// Main thread.
	rpl::event_stream<UpdateForLoader> _updates;
	int _autoincrement = 0;
	base::flat_map<not_null<webFileLoader*>, int> _ids;

	// Worker thread.
	std::deque<Enqueued> _queue;
	std::deque<Enqueued> _previousGeneration;
	base::flat_map<int, Sent> _sent;
	std::vector<QPointer<QNetworkReply>> _repliesBeingDeleted;

};

WebLoadManager::WebLoadManager()
: _resetGenerationTimer(&_thread, [=] { resetGeneration(); }) {
	handleNetworkErrors();

	const auto original = QThread::currentThread();
	moveToThread(&_thread);
	_network.moveToThread(&_thread);
	connect(&_thread, &QThread::finished, [=] {
		clear();
		moveToThread(original);
		_network.moveToThread(original);
	});
	_thread.start();
}

void WebLoadManager::handleNetworkErrors() {
	const auto fail = [=](QNetworkReply *reply) {
		for (const auto &[id, sent] : _sent) {
			if (sent.reply == reply) {
				failed(id, reply);
				return;
			}
		}
	};
	connect(&_network, &QNetworkAccessManager::authenticationRequired, fail);
	connect(&_network, &QNetworkAccessManager::sslErrors, fail);
}

WebLoadManager::~WebLoadManager() {
	_thread.quit();
	_thread.wait();
}

[[nodiscard]] rpl::producer<Update> WebLoadManager::updates(
		not_null<webFileLoader*> loader) const {
	return _updates.events(
	) | rpl::filter([=](const UpdateForLoader &update) {
		return (update.loader == loader);
	}) | rpl::map([=](UpdateForLoader &&update) {
		return std::move(update.data);
	});
}

void WebLoadManager::enqueue(not_null<webFileLoader*> loader) {
	const auto id = [&] {
		const auto i = _ids.find(loader);
		return (i != end(_ids))
			? i->second
			: _ids.emplace(loader, ++_autoincrement).first->second;
	}();
	const auto url = loader->url();
	InvokeQueued(this, [=] {
		enqueue(id, url);
	});
}

void WebLoadManager::remove(not_null<webFileLoader*> loader) {
	const auto i = _ids.find(loader);
	if (i == end(_ids)) {
		return;
	}
	const auto id = i->second;
	_ids.erase(i);
	InvokeQueued(this, [=] {
		remove(id);
	});
}

void WebLoadManager::enqueue(int id, const QString &url) {
	const auto i = ranges::find(_queue, id, &Enqueued::id);
	if (i != end(_queue)) {
		return;
	}
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, id, &Enqueued::id),
		end(_previousGeneration));
	_queue.push_back(Enqueued{ id, url });
	if (!_resetGenerationTimer.isActive()) {
		_resetGenerationTimer.callOnce(kResetDownloadPrioritiesTimeout);
	}
	checkSendNext();
}

void WebLoadManager::remove(int id) {
	_queue.erase(ranges::remove(_queue, id, &Enqueued::id), end(_queue));
	_previousGeneration.erase(
		ranges::remove(_previousGeneration, id, &Enqueued::id),
		end(_previousGeneration));
	removeSent(id);
}

void WebLoadManager::resetGeneration() {
	if (!_previousGeneration.empty()) {
		std::copy(
			begin(_previousGeneration),
			end(_previousGeneration),
			std::back_inserter(_queue));
		_previousGeneration.clear();
	}
	std::swap(_queue, _previousGeneration);
}

void WebLoadManager::checkSendNext() {
	if (_sent.size() >= kMaxWebFileQueries
		|| (_queue.empty() && _previousGeneration.empty())) {
		return;
	}
	const auto entry = _queue.empty()
		? _previousGeneration.front()
		: _queue.front();
	(_queue.empty() ? _previousGeneration : _queue).pop_front();
	send(entry);
}

void WebLoadManager::send(const Enqueued &entry) {
	const auto id = entry.id;
	const auto url = entry.url;
	_sent.emplace(id, Sent{ url, send(id, url) });
}

void WebLoadManager::removeSent(int id) {
	if (const auto i = _sent.find(id); i != end(_sent)) {
		deleteDeferred(i->second.reply);
		_sent.erase(i);
		checkSendNext();
	}
}

not_null<QNetworkReply*> WebLoadManager::send(int id, const QString &url) {
	const auto result = _network.get(QNetworkRequest(url));
	const auto handleProgress = [=](qint64 ready, qint64 total) {
		progress(id, result, ready, total);
	};
	const auto handleError = [=](QNetworkReply::NetworkError error) {
		failed(id, result, error);
	};
	connect(result, &QNetworkReply::downloadProgress, handleProgress);
	connect(result, base::QNetworkReply_error, handleError);
	return result;
}

WebLoadManager::Sent *WebLoadManager::findSent(
		int id,
		not_null<QNetworkReply*> reply) {
	const auto i = _sent.find(id);
	return (i != end(_sent) && i->second.reply == reply)
		? &i->second
		: nullptr;
}

void WebLoadManager::progress(
		int id,
		not_null<QNetworkReply*> reply,
		int64 ready,
		int64 total) {
	const auto statusCode = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	const auto status = statusCode.isValid() ? statusCode.toInt() : 200;
	if (status == 301 || status == 302) {
		redirect(id, reply);
	} else if (status != 200 && status != 206 && status != 416) {
		LOG(("Network Error: "
			"Bad HTTP status received in WebLoadManager::onProgress() %1"
			).arg(status));
		failed(id, reply);
	} else {
		notify(id, reply, ready, total);
	}
}

void WebLoadManager::redirect(int id, not_null<QNetworkReply*> reply) {
	const auto header = reply->header(QNetworkRequest::LocationHeader);
	const auto url = header.toString();
	if (url.isEmpty()) {
		return;
	}

	if (const auto sent = findSent(id, reply)) {
		if (!sent->redirectsLeft--) {
			LOG(("Network Error: "
				"Too many HTTP redirects in onFinished() "
				"for web file loader: %1").arg(url));
			failed(id, reply);
			return;
		}
		deleteDeferred(reply);
		sent->url = url;
		sent->reply = send(id, url);
	}
}

void WebLoadManager::notify(
		int id,
		not_null<QNetworkReply*> reply,
		int64 ready,
		int64 total) {
	if (const auto sent = findSent(id, reply)) {
		sent->ready = ready;
		sent->total = std::max(total, int64(0));
		sent->data.append(reply->readAll());
		if (total == 0
			|| total > Storage::kMaxFileInMemory
			|| sent->data.size() > Storage::kMaxFileInMemory) {
			LOG(("Network Error: "
				"Bad size received for HTTP download progress "
				"in WebLoadManager::onProgress(): %1 / %2 (bytes %3)"
				).arg(ready
				).arg(total
				).arg(sent->data.size()));
			failed(id, reply);
		} else if (total > 0 && ready >= total) {
			finished(id, reply);
		} else {
			queueProgressUpdate(id, sent->ready, sent->total);
		}
	}
}

void WebLoadManager::failed(
		int id,
		not_null<QNetworkReply*> reply,
		QNetworkReply::NetworkError error) {
	if (const auto sent = findSent(id, reply)) {
		LOG(("Network Error: "
			"Failed to request '%1', error %2 (%3)"
			).arg(sent->url
			).arg(int(error)
			).arg(reply->errorString()));
		failed(id, reply);
	}
}

void WebLoadManager::failed(int id, not_null<QNetworkReply*> reply) {
	if (const auto sent = findSent(id, reply)) {
		removeSent(id);
		queueFailedUpdate(id);
	}
}

void WebLoadManager::deleteDeferred(not_null<QNetworkReply*> reply) {
	reply->deleteLater();
	_repliesBeingDeleted.erase(
		ranges::remove(_repliesBeingDeleted, nullptr),
		end(_repliesBeingDeleted));
	_repliesBeingDeleted.emplace_back(reply.get());
}

void WebLoadManager::finished(int id, not_null<QNetworkReply*> reply) {
	if (const auto sent = findSent(id, reply)) {
		const auto data = base::take(sent->data);
		removeSent(id);
		queueFinishedUpdate(id, data);
	}
}

void WebLoadManager::clear() {
	for (const auto &[id, sent] : base::take(_sent)) {
		sent.reply->abort();
		delete sent.reply;
	}
	for (const auto &reply : base::take(_repliesBeingDeleted)) {
		if (reply) {
			delete reply;
		}
	}
}

void WebLoadManager::queueProgressUpdate(int id, int64 ready, int64 total) {
	crl::on_main(this, [=] {
		sendUpdate(id, Progress{ ready, total });
	});
}

void WebLoadManager::queueFailedUpdate(int id) {
	crl::on_main(this, [=] {
		sendUpdate(id, Error{});
	});
}

void WebLoadManager::queueFinishedUpdate(int id, const QByteArray &data) {
	crl::on_main(this, [=] {
		LOG(("FINISHED UPDATE FOR: %1").arg(id));
		for (const auto &[loader, loaderId] : _ids) {
			if (loaderId == id) {
				LOG(("LOADER ID: %2").arg(quintptr(loader.get())));
				break;
			}
		}
		LOG(("SENT"));
		sendUpdate(id, QByteArray(data));
	});
}

void WebLoadManager::sendUpdate(int id, Update &&data) {
	for (const auto &[loader, loaderId] : _ids) {
		if (loaderId == id) {
			_updates.fire(UpdateForLoader{ loader, std::move(data) });
			return;
		}
	}
}

webFileLoader::webFileLoader(
	not_null<Main::Session*> session,
	const QString &url,
	const QString &to,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag)
: FileLoader(
	session,
	QString(),
	0,
	0,
	UnknownFileLocation,
	LoadToCacheAsWell,
	fromCloud,
	autoLoading,
	cacheTag)
, _url(url) {
}

webFileLoader::~webFileLoader() {
	if (!_finished) {
		cancel();
	}
}

QString webFileLoader::url() const {
	return _url;
}

void webFileLoader::startLoading() {
	if (_finished) {
		return;
	} else if (!_manager) {
		_manager = GetManager();
		_manager->updates(
			this
		) | rpl::start_with_next([=](const Update &data) {
			if (const auto progress = std::get_if<Progress>(&data)) {
				loadProgress(progress->ready, progress->total);
			} else if (const auto bytes = std::get_if<QByteArray>(&data)) {
				loadFinished(*bytes);
			} else {
				loadFailed();
			}
		}, _managerLifetime);
	}
	_manager->enqueue(this);
}

int webFileLoader::currentOffset() const {
	return _ready;
}

void webFileLoader::loadProgress(qint64 ready, qint64 total) {
	_fullSize = _loadSize = total;
	_ready = ready;
	notifyAboutProgress();
}

void webFileLoader::loadFinished(const QByteArray &data) {
	cancelRequest();
	if (writeResultPart(0, bytes::make_span(data))) {
		finalizeResult();
	}
}

void webFileLoader::loadFailed() {
	cancel(true);
}

Storage::Cache::Key webFileLoader::cacheKey() const {
	return Data::UrlCacheKey(_url);
}

std::optional<MediaKey> webFileLoader::fileLocationKey() const {
	return std::nullopt;
}

void webFileLoader::cancelHook() {
	cancelRequest();
}

void webFileLoader::cancelRequest() {
	if (!_manager) {
		return;
	}
	_managerLifetime.destroy();
	_manager->remove(this);
	_manager = nullptr;
}
