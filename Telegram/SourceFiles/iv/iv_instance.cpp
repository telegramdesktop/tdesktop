/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_instance.h"

#include "apiwrap.h"
#include "boxes/share_box.h"
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
#include "data/data_thread.h"
#include "data/data_web_page.h"
#include "data/data_user.h"
#include "history/history_item_helpers.h"
#include "info/profile/info_profile_values.h"
#include "iv/iv_controller.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h"
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
#include "ui/layers/layer_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/basic_click_handlers.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_interface.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

namespace Iv {
namespace {

constexpr auto kGeoPointScale = 1;
constexpr auto kGeoPointZoomMin = 13;
constexpr auto kMaxLoadParts = 5;
constexpr auto kKeepLoadingParts = 8;

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

	void showWindowed(Prepared result);
	[[nodiscard]] ShareBoxResult shareBox(ShareBoxDescriptor &&descriptor);

	[[nodiscard]] ::Data::FileOrigin fileOrigin(
		not_null<WebPageData*> page) const;
	void streamPhoto(QStringView idWithPageId, Webview::DataRequest request);
	void streamFile(QStringView idWithPageId, Webview::DataRequest request);
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
	data->prepare({}, [=](Prepared result) {
		result.hash = hash;
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			result.url = id;
			if (_id != id || !_preparing) {
				return;
			}
			_preparing = false;
			fillChannelJoinedValues(result);
			fillEmbeds(std::move(result.embeds));
			showWindowed(std::move(result));
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

ShareBoxResult Shown::shareBox(ShareBoxDescriptor &&descriptor) {
	class Show final : public Ui::Show {
	public:
		Show(QPointer<QWidget> parent, Fn<Ui::LayerStackWidget*()> lookup)
		: _parent(parent)
		, _lookup(lookup) {
		}
		void showOrHideBoxOrLayer(
				std::variant<
				v::null_t,
				object_ptr<Ui::BoxContent>,
				std::unique_ptr<Ui::LayerWidget>> &&layer,
				Ui::LayerOptions options,
				anim::type animated) const override {
			using UniqueLayer = std::unique_ptr<Ui::LayerWidget>;
			using ObjectBox = object_ptr<Ui::BoxContent>;
			const auto stack = _lookup();
			if (!stack) {
				return;
			} else if (auto layerWidget = std::get_if<UniqueLayer>(&layer)) {
				stack->showLayer(std::move(*layerWidget), options, animated);
			} else if (auto box = std::get_if<ObjectBox>(&layer)) {
				stack->showBox(std::move(*box), options, animated);
			} else {
				stack->hideAll(animated);
			}
		}
		not_null<QWidget*> toastParent() const override {
			return _parent.data();
		}
		bool valid() const override {
			return _lookup() != nullptr;
		}
		operator bool() const override {
			return valid();
		}

	private:
		const QPointer<QWidget> _parent;
		const Fn<Ui::LayerStackWidget*()> _lookup;

	};

	const auto url = descriptor.url;
	const auto wrap = descriptor.parent;

	struct State {
		Ui::LayerStackWidget *stack = nullptr;
		rpl::event_stream<> destroyRequests;
	};
	const auto state = wrap->lifetime().make_state<State>();

	const auto weak = QPointer<Ui::RpWidget>(wrap);
	const auto lookup = crl::guard(weak, [state] { return state->stack; });
	const auto layer = Ui::CreateChild<Ui::LayerStackWidget>(
		wrap.get(),
		[=] { return std::make_shared<Show>(weak.data(), lookup); });
	state->stack = layer;
	const auto show = layer->showFactory()();

	layer->setHideByBackgroundClick(false);
	layer->move(0, 0);
	wrap->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		layer->resize(size);
	}, layer->lifetime());
	layer->hideFinishEvents(
	) | rpl::filter([=] {
		return !!lookup(); // Last hide finish is sent from destructor.
	}) | rpl::start_with_next([=] {
		state->destroyRequests.fire({});
	}, wrap->lifetime());

	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto sending = std::make_shared<bool>();
	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(url);
		show->showToast(tr::lng_background_link_copied(tr::now));
	};
	auto submitCallback = [=](
			std::vector<not_null<::Data::Thread*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options,
			::Data::ForwardOptions) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto thread : result) {
				const auto error = GetErrorTextForSending(
					thread,
					{ .text = &comment });
				if (!error.isEmpty()) {
					return std::make_pair(error, thread);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->chatListName())
				).append("\n\n");
			}
			text.append(error.first);
			if (const auto weak = *box) {
				weak->getDelegate()->show(Ui::MakeConfirmBox({
					.text = text,
					.inform = true,
				}));
			}
			return;
		}

		*sending = true;
		if (!comment.text.isEmpty()) {
			comment.text = url + "\n" + comment.text;
			const auto add = url.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		} else {
			comment.text = url;
		}
		auto &api = _session->api();
		for (const auto thread : result) {
			auto message = Api::MessageToSend(
				Api::SendAction(thread, options));
			message.textWithTags = comment;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		if (*box) {
			(*box)->closeBox();
		}
		show->showToast(tr::lng_share_done(tr::now));
	};
	auto filterCallback = [](not_null<::Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreRequirePremium()) {
				return true;
			}
		}
		return ::Data::CanSend(thread, ChatRestriction::SendOther);
	};
	const auto focus = crl::guard(layer, [=] {
		if (!layer->window()->isActiveWindow()) {
			layer->window()->activateWindow();
			layer->window()->setFocus();
		}
		layer->setInnerFocus();
	});
	auto result = ShareBoxResult{
		.focus = focus,
		.hide = [=] { show->hideLayer(); },
		.destroyRequests = state->destroyRequests.events(),
	};
	*box = show->show(
		Box<ShareBox>(ShareBox::Descriptor{
			.session = _session,
			.copyCallback = std::move(copyCallback),
			.submitCallback = std::move(submitCallback),
			.filterCallback = std::move(filterCallback),
			.premiumRequiredError = SharePremiumRequiredError(),
		}),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
	return result;
}

void Shown::createController() {
	Expects(!_controller);

	const auto showShareBox = [=](ShareBoxDescriptor &&descriptor) {
		return shareBox(std::move(descriptor));
	};
	_controller = std::make_unique<Controller>(std::move(showShareBox));

	_controller->events(
	) | rpl::start_to_stream(_events, _controller->lifetime());

	_controller->dataRequests(
	) | rpl::start_with_next([=](Webview::DataRequest request) {
		const auto requested = QString::fromStdString(request.id);
		const auto id = QStringView(requested);
		if (id.startsWith(u"photo/")) {
			streamPhoto(id.mid(6), std::move(request));
		} else if (id.startsWith(u"document/"_q)) {
			streamFile(id.mid(9), std::move(request));
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

::Data::FileOrigin Shown::fileOrigin(not_null<WebPageData*> page) const {
	return ::Data::FileOriginWebPage{ page->url };
}

void Shown::streamPhoto(
		QStringView idWithPageId,
		Webview::DataRequest request) {
	using namespace Data;

	const auto parts = idWithPageId.split('/');
	if (parts.size() != 2) {
		requestFail(std::move(request));
		return;
	}
	const auto photo = _session->data().photo(parts[0].toULongLong());
	const auto page = _session->data().webpage(parts[1].toULongLong());
	if (photo->isNull() || page->url.isEmpty()) {
		requestFail(std::move(request));
		return;
	}
	const auto media = photo->createMediaView();
	media->wanted(PhotoSize::Large, fileOrigin(page));
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
		QStringView idWithPageId,
		Webview::DataRequest request) {
	using namespace Data;

	const auto parts = idWithPageId.split('/');
	if (parts.size() != 2) {
		requestFail(std::move(request));
		return;
	}
	const auto documentId = DocumentId(parts[0].toULongLong());
	const auto i = _streams.find(documentId);
	if (i != end(_streams)) {
		streamFile(i->second, std::move(request));
		return;
	}
	const auto document = _session->data().document(documentId);
	const auto page = _session->data().webpage(parts[1].toULongLong());
	if (page->url.isEmpty()) {
		requestFail(std::move(request));
		return;
	}
	auto loader = document->createStreamingLoader(fileOrigin(page), false);
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
				document->save(fileOrigin(page), QString());
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
	const auto from = int(std::min(int64(request.offset), size) / kPart);
	const auto till = (request.limit > 0)
		? std::min(int64(request.offset + request.limit), size)
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
		requestFail(std::move(request));
		return;
	}
	const auto point = GeoPointFromId(parts[0].toUtf8());
	const auto size = parts[1].split(',');
	const auto zoom = parts[2].toInt();
	if (size.size() != 2) {
		requestFail(std::move(request));
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
	prepare(data, hash);
}

void Shown::update(not_null<Data*> data) {
	const auto weak = base::make_weak(this);

	const auto id = data->id();
	data->prepare({}, [=](Prepared result) {
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
			requestFull(session, data->id());
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
