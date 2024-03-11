/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_instance.h"

#include "base/call_delayed.h"
#include "apiwrap.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/shortcuts.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "info/profile/info_profile_values.h"
#include "iv/iv_controller.h"
#include "iv/iv_data.h"
#include "lottie/lottie_common.h" // Lottie::ReadContent.
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/view/media_view_open_common.h"
#include "storage/file_download.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/basic_click_handlers.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_interface.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QtGui/QDesktopServices>

namespace Iv {
namespace {

constexpr auto kGeoPointScale = 1;
constexpr auto kGeoPointZoomMin = 13;
constexpr auto kMaxLoadParts = 3;
constexpr auto kKeepLoadingParts = 8;

[[nodiscard]] QString LookupLocalPath(
		const std::shared_ptr<Main::SessionShow> show) {
	const auto &domain = show->session().account().domain();
	const auto &base = domain.local().webviewDataPath();
	static auto counter = 0;
	return base + u"/iv/"_q + QString::number(++counter);
}

[[nodiscard]] Storage::Cache::Key IvBaseCacheKey(
		not_null<DocumentData*> document) {
	auto big = document->bigFileBaseCacheKey();
	big.low += 0x7FF;
	return big;
}

} // namespace

class Shown final : public base::has_weak_ptr {
public:
	Shown(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		QString hash);

	[[nodiscard]] bool showing(
		not_null<Main::Session*> session,
		not_null<Data*> data) const;
	[[nodiscard]] bool showingFrom(not_null<Main::Session*> session) const;
	[[nodiscard]] bool activeFor(not_null<Main::Session*> session) const;
	[[nodiscard]] bool active() const;

	void moveTo(not_null<Data*> data, QString hash);
	void update(not_null<Data*> data);

	void showJoinedTooltip();
	void minimize();

	[[nodiscard]] rpl::producer<Controller::Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct MapPreview {
		std::unique_ptr<::Data::CloudFile> file;
		QByteArray bytes;
	};
	struct PartRequest {
		Webview::DataRequest request;
		QByteArray data;
		std::vector<bool> loaded;
		int64 offset = 0;
	};
	struct FileStream {
		not_null<DocumentData*> document;
		std::unique_ptr<Media::Streaming::Loader> loader;
		std::vector<PartRequest> requests;
		std::string mime;
		rpl::lifetime lifetime;
	};
	struct FileLoad {
		std::shared_ptr<::Data::DocumentMedia> media;
		std::vector<Webview::DataRequest> requests;
	};

	void prepare(not_null<Data*> data, const QString &hash);
	void createController();

	void showLocal(Prepared result);
	void showWindowed(Prepared result);

	// Local.
	void showProgress(int index);
	void loadResource(int index);
	void finishLocal(const QString &path);
	[[nodiscard]] QString localRoot() const;
	void writeLocal(const QString &relative, const QByteArray &data);
	void loadPhoto(QString id, PhotoId photoId);
	void loadDocument(QString id, DocumentId documentId);
	void loadPage(QString id, QString tag);
	void loadMap(QString id, QString params);
	void writeEmbed(QString id, QString hash);

	// Windowed.
	void streamPhoto(PhotoId photoId, Webview::DataRequest request);
	void streamFile(DocumentId documentId, Webview::DataRequest request);
	void streamFile(FileStream &file, Webview::DataRequest request);
	void processPartInFile(
		FileStream &file,
		Media::Streaming::LoadedPart &&part);
	bool finishRequestWithPart(
		PartRequest &request,
		const Media::Streaming::LoadedPart &part);
	void streamMap(QString params, Webview::DataRequest request);
	void sendEmbed(QByteArray hash, Webview::DataRequest request);

	void fillChannelJoinedValues(const Prepared &result);
	void fillEmbeds(base::flat_map<QByteArray, QByteArray> added);
	void subscribeToDocuments();
	[[nodiscard]] QByteArray readFile(
		const std::shared_ptr<::Data::DocumentMedia> &media);
	void requestDone(
		Webview::DataRequest request,
		QByteArray bytes,
		std::string mime,
		int64 offset = 0,
		int64 total = 0);
	void requestFail(Webview::DataRequest request);

	const not_null<Main::Session*> _session;
	std::shared_ptr<Main::SessionShow> _show;
	QString _id;
	std::unique_ptr<Controller> _controller;
	base::flat_map<DocumentId, FileStream> _streams;
	base::flat_map<DocumentId, FileLoad> _files;
	base::flat_map<QByteArray, rpl::producer<bool>> _inChannelValues;

	bool _preparing = false;

	QString _localBase;
	base::flat_map<QByteArray, QByteArray> _embeds;
	base::flat_map<QString, MapPreview> _maps;
	std::vector<QByteArray> _resources;
	int _resource = -1;

	rpl::event_stream<Controller::Event> _events;

	rpl::lifetime _documentLifetime;
	rpl::lifetime _lifetime;

};

Shown::Shown(
	std::shared_ptr<Main::SessionShow> show,
	not_null<Data*> data,
	QString hash)
: _session(&show->session())
, _show(show) {
	prepare(data, hash);
}

void Shown::prepare(not_null<Data*> data, const QString &hash) {
	const auto weak = base::make_weak(this);

	_preparing = true;
	const auto id = _id = data->id();
	const auto base = /*local ? LookupLocalPath(show) : */QString();
	data->prepare({ .saveToFolder = base }, [=](Prepared result) {
		result.hash = hash;
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			result.url = id;
			if (_id != id || !_preparing) {
				return;
			}
			_preparing = false;
			fillChannelJoinedValues(result);
			fillEmbeds(std::move(result.embeds));
			if (!base.isEmpty()) {
				_localBase = base;
				showLocal(std::move(result));
			} else {
				showWindowed(std::move(result));
			}
		});
	});
}

void Shown::fillChannelJoinedValues(const Prepared &result) {
	for (const auto &id : result.channelIds) {
		const auto channelId = ChannelId(id.toLongLong());
		const auto channel = _session->data().channel(channelId);
		if (!channel->isLoaded() && !channel->username().isEmpty()) {
			channel->session().api().request(MTPcontacts_ResolveUsername(
				MTP_string(channel->username())
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				channel->owner().processUsers(result.data().vusers());
				channel->owner().processChats(result.data().vchats());
			}).send();
		}
		_inChannelValues[id] = Info::Profile::AmInChannelValue(channel);
	}
}

void Shown::fillEmbeds(base::flat_map<QByteArray, QByteArray> added) {
	if (_embeds.empty()) {
		_embeds = std::move(added);
	} else {
		for (auto &[k, v] : added) {
			_embeds[k] = std::move(v);
		}
	}
}

void Shown::showLocal(Prepared result) {
	showProgress(0);

	QDir(_localBase).removeRecursively();
	QDir().mkpath(_localBase);

	_resources = std::move(result.resources);
	writeLocal(localRoot(), result.content);
}

void Shown::showProgress(int index) {
	const auto count = int(_resources.size() + 1);
	_show->showToast(u"Saving %1 / %2..."_q.arg(index + 1).arg(count));
}

void Shown::finishLocal(const QString &path) {
	if (path.isEmpty()) {
		_show->showToast(u"Failed!"_q);
	} else {
		_show->showToast(u"Done!"_q);
		File::Launch(path);
	}
	_id = QString();
}

QString Shown::localRoot() const {
	return u"page.html"_q;
}

void Shown::writeLocal(const QString &relative, const QByteArray &data) {
	const auto path = _localBase + '/' + relative;
	QFileInfo(path).absoluteDir().mkpath(".");

	auto f = QFile(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
		finishLocal({});
	} else {
		crl::on_main(this, [=] {
			loadResource(_resource + 1);
		});
	}
}

void Shown::loadResource(int index) {
	_resource = index;
	if (_resource == _resources.size()) {
		finishLocal(_localBase + '/' + localRoot());
		return;
	}
	showProgress(_resource + 1);
	const auto id = QString::fromUtf8(_resources[_resource]);
	if (id.startsWith(u"photo/"_q)) {
		loadPhoto(id, id.mid(6).toULongLong());
	} else if (id.startsWith(u"document/"_q)) {
		loadDocument(id, id.mid(9).toULongLong());
	} else if (id.startsWith(u"iv/"_q)) {
		loadPage(id, id.mid(3));
	} else if (id.startsWith(u"map/"_q)) {
		loadMap(id, id.mid(4));
	} else if (id.startsWith(u"html/"_q)) {
		writeEmbed(id, id.mid(5));
	} else {
		_show->show(
			Ui::MakeInformBox(u"Skipping resource %1..."_q.arg(id)));
		crl::on_main(this, [=] {
			loadResource(index + 1);
		});
	}
}

void Shown::loadPhoto(QString id, PhotoId photoId) {
	const auto photo = _session->data().photo(photoId);
	const auto media = photo->createMediaView();
	media->wanted(::Data::PhotoSize::Large, ::Data::FileOrigin());
	const auto finish = [=](QByteArray bytes) {
		writeLocal(id, bytes);
	};
	if (media->loaded()) {
		finish(media->imageBytes(::Data::PhotoSize::Large));
	} else {
		photo->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::start_with_next([=] {
			finish(media->imageBytes(::Data::PhotoSize::Large));
		}, _lifetime);
	}
}

void Shown::loadDocument(QString id, DocumentId documentId) {
	const auto path = _localBase + '/' + id;
	QFileInfo(path).absoluteDir().mkpath(".");

	const auto document = _session->data().document(documentId);
	document->save(::Data::FileOrigin(), path);
	if (!document->loading()) {
		crl::on_main(this, [=] {
			loadResource(_resource + 1);
		});
	}
	document->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !document->loading();
	}) | rpl::take(1) | rpl::start_with_next([=] {
		crl::on_main(this, [=] {
			loadResource(_resource + 1);
		});
	}, _lifetime);
}

void Shown::loadPage(QString id, QString tag) {
	if (!id.endsWith(u".css"_q) && !id.endsWith(u".js"_q)) {
		finishLocal({});
		return;
	}
	const auto pattern = u"^[a-zA-Z\\.\\-_0-9]+$"_q;
	if (QRegularExpression(pattern).match(tag).hasMatch()) {
		auto file = QFile(u":/iv/"_q + tag);
		if (file.open(QIODevice::ReadOnly)) {
			writeLocal(id, file.readAll());
			return;
		}
	}
	finishLocal({});
}

void Shown::loadMap(QString id, QString params) {
	using namespace ::Data;
	const auto i = _maps.find(params);
	if (i != end(_maps)) {
		writeLocal(id, i->second.bytes);
		return;
	}
	const auto parts = params.split(u'&');
	if (parts.size() != 3) {
		finishLocal({});
		return;
	}
	const auto point = GeoPointFromId(parts[0].toUtf8());
	const auto size = parts[1].split(',');
	const auto zoom = parts[2].toInt();
	if (size.size() != 2) {
		finishLocal({});
		return;
	}
	const auto location = GeoPointLocation{
		.lat = point.lat,
		.lon = point.lon,
		.access = point.access,
		.width = size[0].toInt(),
		.height = size[1].toInt(),
		.zoom = std::max(zoom, kGeoPointZoomMin),
		.scale = kGeoPointScale,
	};
	const auto prepared = ImageWithLocation{
		.location = ImageLocation(
			{ location },
			location.width,
			location.height)
	};
	auto &preview = _maps.emplace(params, MapPreview()).first->second;
	preview.file = std::make_unique<CloudFile>();

	UpdateCloudFile(
		*preview.file,
		prepared,
		_session->data().cache(),
		kImageCacheTag,
		[=](FileOrigin origin) { /* restartLoader not used here */ });
	const auto autoLoading = false;
	const auto finalCheck = [=] { return true; };
	const auto done = [=](QByteArray bytes) {
		const auto i = _maps.find(params);
		Assert(i != end(_maps));
		i->second.bytes = std::move(bytes);
		writeLocal(id, i->second.bytes);
	};
	LoadCloudFile(
		_session,
		*preview.file,
		FileOrigin(),
		LoadFromCloudOrLocal,
		autoLoading,
		kImageCacheTag,
		finalCheck,
		done,
		[=](bool) { done("failed..."); });
}

void Shown::writeEmbed(QString id, QString hash) {
	const auto i = _embeds.find(hash.toUtf8());
	if (i != end(_embeds)) {
		writeLocal(id, i->second);
	} else {
		finishLocal({});
	}
}

void Shown::createController() {
	Expects(!_controller);

	_controller = std::make_unique<Controller>();

	_controller->events(
	) | rpl::start_to_stream(_events, _controller->lifetime());

	_controller->dataRequests(
	) | rpl::start_with_next([=](Webview::DataRequest request) {
		const auto requested = QString::fromStdString(request.id);
		const auto id = QStringView(requested);
		if (id.startsWith(u"photo/")) {
			streamPhoto(id.mid(6).toULongLong(), std::move(request));
		} else if (id.startsWith(u"document/"_q)) {
			streamFile(id.mid(9).toULongLong(), std::move(request));
		} else if (id.startsWith(u"map/"_q)) {
			streamMap(id.mid(4).toUtf8(), std::move(request));
		} else if (id.startsWith(u"html/"_q)) {
			sendEmbed(id.mid(5).toUtf8(), std::move(request));
		}
	}, _controller->lifetime());
}

void Shown::showWindowed(Prepared result) {
	if (!_controller) {
		createController();
	}

	const auto domain = &_session->domain();
	_controller->show(
		domain->local().webviewDataPath(),
		std::move(result),
		base::duplicate(_inChannelValues));
}

void Shown::streamPhoto(PhotoId photoId, Webview::DataRequest request) {
	using namespace Data;

	const auto photo = _session->data().photo(photoId);
	if (photo->isNull()) {
		requestFail(std::move(request));
		return;
	}
	const auto media = photo->createMediaView();
	media->wanted(PhotoSize::Large, FileOrigin());
	const auto check = [=] {
		if (!media->loaded() && !media->owner()->failed(PhotoSize::Large)) {
			return false;
		}
		requestDone(
			request,
			media->imageBytes(PhotoSize::Large),
			"image/jpeg");
		return true;
	};
	if (!check()) {
		photo->session().downloaderTaskFinished(
		) | rpl::filter(
			check
		) | rpl::take(1) | rpl::start(_controller->lifetime());
	}
}

void Shown::streamFile(
		DocumentId documentId,
		Webview::DataRequest request) {
	using namespace Data;

	const auto i = _streams.find(documentId);
	if (i != end(_streams)) {
		streamFile(i->second, std::move(request));
		return;
	}
	const auto document = _session->data().document(documentId);
	auto loader = document->createStreamingLoader(FileOrigin(), false);
	if (!loader) {
		if (document->size >= Storage::kMaxFileInMemory) {
			requestFail(std::move(request));
		} else {
			auto media = document->createMediaView();
			if (const auto content = readFile(media); !content.isEmpty()) {
				requestDone(
					std::move(request),
					content,
					document->mimeString().toStdString());
			} else {
				subscribeToDocuments();
				auto &file = _files[documentId];
				file.media = std::move(media);
				file.requests.push_back(std::move(request));
				document->forceToCache(true);
				document->save(::Data::FileOrigin(), QString());
			}
		}
		return;
	}
	auto &file = _streams.emplace(
		documentId,
		FileStream{
			.document = document,
			.loader = std::move(loader),
			.mime = document->mimeString().toStdString(),
		}).first->second;

	file.loader->parts(
	) | rpl::start_with_next([=](Media::Streaming::LoadedPart &&part) {
		const auto i = _streams.find(documentId);
		Assert(i != end(_streams));
		processPartInFile(i->second, std::move(part));
	}, file.lifetime);

	streamFile(file, std::move(request));
}

void Shown::streamFile(FileStream &file, Webview::DataRequest request) {
	constexpr auto kPart = Media::Streaming::Loader::kPartSize;
	const auto size = file.document->size;
	const auto last = int((size + kPart - 1) / kPart);
	const auto from = int(std::min(request.offset, size) / kPart);
	const auto till = (request.limit > 0)
		? std::min(request.offset + request.limit, size)
		: size;
	const auto parts = std::min(
		int((till + kPart - 1) / kPart) - from,
		kMaxLoadParts);
	//auto base = IvBaseCacheKey(document);

	const auto length = std::min((from + parts) * kPart, size)
		- from * kPart;
	file.requests.push_back(PartRequest{
		.request = std::move(request),
		.data = QByteArray(length, 0),
		.loaded = std::vector<bool>(parts, false),
		.offset = from * kPart,
	});

	file.loader->resetPriorities();
	const auto load = std::min(from + kKeepLoadingParts, last) - from;
	for (auto i = 0; i != load; ++i) {
		file.loader->load((from + i) * kPart);
	}
}

void Shown::subscribeToDocuments() {
	if (_documentLifetime) {
		return;
	}
	_documentLifetime = _session->data().documentLoadProgress(
	) | rpl::filter([=](not_null<DocumentData*> document) {
		return !document->loading();
	}) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		const auto i = _files.find(document->id);
		if (i == end(_files)) {
			return;
		}
		auto requests = base::take(i->second.requests);
		const auto content = readFile(i->second.media);
		_files.erase(i);

		if (!content.isEmpty()) {
			for (auto &request : requests) {
				requestDone(
					std::move(request),
					content,
					document->mimeString().toStdString());
			}
		} else {
			for (auto &request : requests) {
				requestFail(std::move(request));
			}
		}
	});
}

QByteArray Shown::readFile(
		const std::shared_ptr<::Data::DocumentMedia> &media) {
	return Lottie::ReadContent(media->bytes(), media->owner()->filepath());
}

void Shown::processPartInFile(
		FileStream &file,
		Media::Streaming::LoadedPart &&part) {
	for (auto i = begin(file.requests); i != end(file.requests);) {
		if (finishRequestWithPart(*i, part)) {
			auto done = base::take(*i);
			i = file.requests.erase(i);
			requestDone(
				std::move(done.request),
				done.data,
				file.mime,
				done.offset,
				file.document->size);
		} else {
			++i;
		}
	}
}

bool Shown::finishRequestWithPart(
		PartRequest &request,
		const Media::Streaming::LoadedPart &part) {
	const auto offset = part.offset;
	if (offset == Media::Streaming::LoadedPart::kFailedOffset) {
		request.data = QByteArray();
		return true;
	} else if (offset < request.offset
		|| offset >= request.offset + request.data.size()) {
		return false;
	}
	constexpr auto kPart = Media::Streaming::Loader::kPartSize;
	const auto copy = std::min(
		int(part.bytes.size()),
		int(request.data.size() - (offset - request.offset)));
	const auto index = (offset - request.offset) / kPart;
	Assert(index < request.loaded.size());
	if (request.loaded[index]) {
		return false;
	}
	request.loaded[index] = true;
	memcpy(
		request.data.data() + index * kPart,
		part.bytes.constData(),
		copy);
	return !ranges::contains(request.loaded, false);
}

void Shown::streamMap(QString params, Webview::DataRequest request) {
	using namespace ::Data;

	const auto parts = params.split(u'&');
	if (parts.size() != 3) {
		finishLocal({});
		return;
	}
	const auto point = GeoPointFromId(parts[0].toUtf8());
	const auto size = parts[1].split(',');
	const auto zoom = parts[2].toInt();
	if (size.size() != 2) {
		finishLocal({});
		return;
	}
	const auto location = GeoPointLocation{
		.lat = point.lat,
		.lon = point.lon,
		.access = point.access,
		.width = size[0].toInt(),
		.height = size[1].toInt(),
		.zoom = std::max(zoom, kGeoPointZoomMin),
		.scale = kGeoPointScale,
	};
	const auto prepared = ImageWithLocation{
		.location = ImageLocation(
			{ location },
			location.width,
			location.height)
	};
	auto &preview = _maps.emplace(params, MapPreview()).first->second;
	preview.file = std::make_unique<CloudFile>();

	UpdateCloudFile(
		*preview.file,
		prepared,
		_session->data().cache(),
		kImageCacheTag,
		[=](FileOrigin origin) { /* restartLoader not used here */ });
	const auto autoLoading = false;
	const auto finalCheck = [=] { return true; };
	const auto done = [=](QByteArray bytes) {
		const auto i = _maps.find(params);
		Assert(i != end(_maps));
		i->second.bytes = std::move(bytes);
		requestDone(request, i->second.bytes, "image/png");
	};
	LoadCloudFile(
		_session,
		*preview.file,
		FileOrigin(),
		LoadFromCloudOrLocal,
		autoLoading,
		kImageCacheTag,
		finalCheck,
		done,
		[=](bool) { done("failed..."); });
}

void Shown::sendEmbed(QByteArray hash, Webview::DataRequest request) {
	const auto i = _embeds.find(hash);
	if (i != end(_embeds)) {
		requestDone(std::move(request), i->second, "text/html");
	} else {
		requestFail(std::move(request));
	}
}

void Shown::requestDone(
		Webview::DataRequest request,
		QByteArray bytes,
		std::string mime,
		int64 offset,
		int64 total) {
	if (bytes.isEmpty() && mime.empty()) {
		requestFail(std::move(request));
		return;
	}
	crl::on_main([
		done = std::move(request.done),
		data = std::move(bytes),
		mime = std::move(mime),
		offset,
		total
	] {
		using namespace Webview;
		done({
			.stream = std::make_unique<DataStreamFromMemory>(data, mime),
			.streamOffset = offset,
			.totalSize = total,
		});
	});
}

void Shown::requestFail(Webview::DataRequest request) {
	crl::on_main([done = std::move(request.done)] {
		done({});
	});
}

bool Shown::showing(
		not_null<Main::Session*> session,
		not_null<Data*> data) const {
	return showingFrom(session) && (_id == data->id());
}

bool Shown::showingFrom(not_null<Main::Session*> session) const {
	return (_session == session);
}

bool Shown::activeFor(not_null<Main::Session*> session) const {
	return showingFrom(session) && _controller;
}

bool Shown::active() const {
	return _controller && _controller->active();
}

void Shown::moveTo(not_null<Data*> data, QString hash) {
	if (!_controller || !_controller->showFast(data->id(), hash)) {
		prepare(data, hash);
	}
}

void Shown::update(not_null<Data*> data) {
	const auto weak = base::make_weak(this);

	const auto id = data->id();
	const auto base = /*local ? LookupLocalPath(show) : */QString();
	data->prepare({ .saveToFolder = base }, [=](Prepared result) {
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			result.url = id;
			fillChannelJoinedValues(result);
			fillEmbeds(std::move(result.embeds));
			if (_controller) {
				_controller->update(std::move(result));
			}
		});
	});
}

void Shown::showJoinedTooltip() {
	if (_controller) {
		_controller->showJoinedTooltip();
	}
}

void Shown::minimize() {
	if (_controller) {
		_controller->minimize();
	}
}

Instance::Instance() = default;

Instance::~Instance() = default;

void Instance::show(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data*> data,
		QString hash) {
	const auto session = &show->session();
	const auto guard = gsl::finally([&] {
		if (data->partial()) {
			base::call_delayed(10000, [=] {
				requestFull(session, data->id());
			});
		}
	});
	if (_shown && _shownSession == session) {
		_shown->moveTo(data, hash);
		return;
	}
	_shown = std::make_unique<Shown>(show, data, hash);
	_shownSession = session;
	_shown->events() | rpl::start_with_next([=](Controller::Event event) {
		using Type = Controller::Event::Type;
		switch (event.type) {
		case Type::Close:
			_shown = nullptr;
			break;
		case Type::Quit:
			Shortcuts::Launch(Shortcuts::Command::Quit);
			break;
		case Type::OpenChannel:
			processOpenChannel(event.context);
			break;
		case Type::JoinChannel:
			processJoinChannel(event.context);
			break;
		case Type::OpenLinkExternal:
			QDesktopServices::openUrl(event.url);
			break;
		case Type::OpenMedia:
			if (const auto window = Core::App().activeWindow()) {
				const auto current = window->sessionController();
				const auto controller = (current
					&& &current->session() == _shownSession)
					? current
					: nullptr;
				const auto item = (HistoryItem*)nullptr;
				const auto topicRootId = MsgId(0);
				if (event.context.startsWith("-photo")) {
					const auto id = event.context.mid(6).toULongLong();
					const auto photo = _shownSession->data().photo(id);
					if (!photo->isNull()) {
						window->openInMediaView({
							controller,
							photo,
							item,
							topicRootId
						});
					}
				} else if (event.context.startsWith("-video")) {
					const auto id = event.context.mid(6).toULongLong();
					const auto video = _shownSession->data().document(id);
					if (!video->isNull()) {
						window->openInMediaView({
							controller,
							video,
							item,
							topicRootId
						});
					}
				}
			}
			break;
		case Type::OpenPage:
		case Type::OpenLink:
			_shownSession->api().request(MTPmessages_GetWebPage(
				MTP_string(event.url),
				MTP_int(0)
			)).done([=](const MTPmessages_WebPage &result) {
				_shownSession->data().processUsers(result.data().vusers());
				_shownSession->data().processChats(result.data().vchats());
				const auto page = _shownSession->data().processWebpage(
					result.data().vwebpage());
				if (page && page->iv) {
					const auto parts = event.url.split('#');
					const auto hash = (parts.size() > 1) ? parts[1] : u""_q;
					this->show(show, page->iv.get(), hash);
				} else {
					UrlClickHandler::Open(event.url);
				}
			}).fail([=] {
				UrlClickHandler::Open(event.url);
			}).send();
			break;
		}
	}, _shown->lifetime());

	session->changes().peerUpdates(
		::Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::start_with_next([=](const ::Data::PeerUpdate &update) {
		if (const auto channel = update.peer->asChannel()) {
			if (channel->amIn()) {
				const auto i = _joining.find(session);
				const auto value = not_null{ channel };
				if (i != end(_joining) && i->second.remove(value)) {
					_shown->showJoinedTooltip();
				}
			}
		}
	}, _shown->lifetime());

	if (!_tracking.contains(session)) {
		_tracking.emplace(session);
		session->lifetime().add([=] {
			_tracking.remove(session);
			_joining.remove(session);
			_fullRequested.remove(session);
			if (_shownSession == session) {
				_shownSession = nullptr;
			}
			if (_shown && _shown->showingFrom(session)) {
				_shown = nullptr;
			}
		});
	}
}

void Instance::requestFull(
		not_null<Main::Session*> session,
		const QString &id) {
	if (!_tracking.contains(session)
		|| !_fullRequested[session].emplace(id).second) {
		return;
	}
	session->api().request(MTPmessages_GetWebPage(
		MTP_string(id),
		MTP_int(0)
	)).done([=](const MTPmessages_WebPage &result) {
		session->data().processUsers(result.data().vusers());
		session->data().processChats(result.data().vchats());
		const auto page = session->data().processWebpage(
			result.data().vwebpage());
		if (page && page->iv && _shown && _shownSession == session) {
			_shown->update(page->iv.get());
		}
	}).send();
}

void Instance::processOpenChannel(const QString &context) {
	if (!_shownSession) {
		return;
	} else if (const auto channelId = ChannelId(context.toLongLong())) {
		const auto channel = _shownSession->data().channel(channelId);
		if (channel->isLoaded()) {
			if (const auto window = Core::App().windowFor(channel)) {
				if (const auto controller = window->sessionController()) {
					controller->showPeerHistory(channel);
					_shown = nullptr;
				}
			}
		} else if (!channel->username().isEmpty()) {
			if (const auto window = Core::App().windowFor(channel)) {
				if (const auto controller = window->sessionController()) {
					controller->showPeerByLink({
						.usernameOrId = channel->username(),
					});
					_shown = nullptr;
				}
			}
		}
	}
}

void Instance::processJoinChannel(const QString &context) {
	if (!_shownSession) {
		return;
	} else if (const auto channelId = ChannelId(context.toLongLong())) {
		const auto channel = _shownSession->data().channel(channelId);
		_joining[_shownSession].emplace(channel);
		if (channel->isLoaded()) {
			_shownSession->api().joinChannel(channel);
		} else if (!channel->username().isEmpty()) {
			if (const auto window = Core::App().windowFor(channel)) {
				if (const auto controller = window->sessionController()) {
					controller->showPeerByLink({
						.usernameOrId = channel->username(),
						.joinChannel = true,
					});
				}
			}
		}
	}
}

bool Instance::hasActiveWindow(not_null<Main::Session*> session) const {
	return _shown && _shown->activeFor(session);
}

bool Instance::closeActive() {
	if (!_shown || !_shown->active()) {
		return false;
	}
	_shown = nullptr;
	return true;
}

bool Instance::minimizeActive() {
	if (!_shown || !_shown->active()) {
		return false;
	}
	_shown->minimize();
	return true;
}

void Instance::closeAll() {
	_shown = nullptr;
}

} // namespace Iv
