/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/location_picker.h"

#include "apiwrap.h"
#include "base/platform/base_platform_info.h"
#include "boxes/peer_list_box.h"
#include "core/current_geo_location.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_location.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/webview_helpers.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/themes/window_theme.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Ui {
namespace {

#ifdef Q_OS_MAC
const auto kProtocolOverride = "mapboxapihelper";
#else // Q_OS_MAC
const auto kProtocolOverride = "";
#endif // Q_OS_MAC

Core::GeoLocation LastExactLocation;
QString MapsProviderToken;
QString GeocodingProviderToken;

using VenueData = Data::InputVenue;

class VenueRowDelegate {
public:
	virtual void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		const QString &type) = 0;
};

class VenueRow final : public PeerListRow {
public:
	VenueRow(not_null<VenueRowDelegate*> delegate, const VenueData &data);

	void update(const VenueData &data);

	[[nodiscard]] VenueData data() const;

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	const not_null<VenueRowDelegate*> _delegate;
	VenueData _data;

};

VenueRow::VenueRow(
	not_null<VenueRowDelegate*> delegate,
	const VenueData &data)
: PeerListRow(UniqueRowIdFromString(data.id))
, _delegate(delegate)
, _data(data) {
	setCustomStatus(data.address);
}

void VenueRow::update(const VenueData &data) {
	_data = data;
	setCustomStatus(data.address);
}

VenueData VenueRow::data() const {
	return _data;
}

QString VenueRow::generateName() {
	return _data.title;
}

QString VenueRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback VenueRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [=](
			QPainter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		_delegate->rowPaintIcon(p, x, y, size, _data.venueType);
	};
}

class LinksController final
	: public PeerListController
	, public VenueRowDelegate
	, public base::has_weak_ptr {
public:
	LinksController(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<VenueData>> content);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		const QString &type) override;

private:
	struct VenueIcon {
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> media;
		uint32 paletteVersion : 31 = 0;
		uint32 iconLoaded : 1 = 0;
		QImage image;
		QImage icon;
	};

	void appendRow(const VenueData &data);

	void rebuild(const std::vector<VenueData> &rows);

	const not_null<Main::Session*> _session;
	rpl::variable<std::vector<VenueData>> _rows;

	base::flat_map<QString, VenueIcon> _icons;

	rpl::lifetime _lifetime;

};

[[nodiscard]] QString NormalizeVenuesQuery(QString query) {
	return query.trimmed().toLower();
}

LinksController::LinksController(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<VenueData>> content)
: _session(session)
, _rows(std::move(content)) {
}

void LinksController::prepare() {
	_rows.value(
	) | rpl::start_with_next([=](const std::vector<VenueData> &rows) {
		rebuild(rows);
	}, _lifetime);
}

void LinksController::rebuild(const std::vector<VenueData> &rows) {
	auto i = 0;
	auto count = delegate()->peerListFullRowsCount();
	while (i < rows.size()) {
		if (i < count) {
			const auto row = delegate()->peerListRowAt(i);
			static_cast<VenueRow*>(row.get())->update(rows[i]);
		} else {
			appendRow(rows[i]);
		}
		++i;
	}
	while (i < count) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(i));
		--count;
	}
	delegate()->peerListRefreshRows();
}

void LinksController::rowClicked(not_null<PeerListRow*> row) {
	const auto venue = static_cast<VenueRow*>(row.get())->data();
	venue;
}

void LinksController::rowRightActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

Main::Session &LinksController::session() const {
	return *_session;
}

void LinksController::appendRow(const VenueData &data) {
	delegate()->peerListAppendRow(std::make_unique<VenueRow>(this, data));
}

void LinksController::rowPaintIcon(
		QPainter &p,
		int x,
		int y,
		int size,
		const QString &icon) {
	auto i = _icons.find(icon);
	if (i == end(_icons)) {
		i = _icons.emplace(icon, VenueIcon{
			.document = _session->data().venueIconDocument(icon),
		}).first;
		i->second.media = i->second.document->createMediaView();
		i->second.document->forceToCache(true);
		i->second.document->save({}, QString(), LoadFromCloudOrLocal, true);
	}
	auto &data = i->second;
	const auto version = uint32(style::PaletteVersion());
	const auto loaded = (!data.media || data.media->loaded()) ? 1 : 0;
	const auto prepare = data.image.isNull()
		|| (data.iconLoaded != loaded)
		|| (data.paletteVersion != version);
	if (prepare) {
		const auto skip = st::pickLocationIconSkip;
		const auto inner = size - skip * 2;
		const auto ratio = style::DevicePixelRatio();

		if (loaded && data.media) {
			const auto bytes = base::take(data.media)->bytes();
			data.icon = Images::Read({ .content = bytes }).image;
			if (!data.icon.isNull()) {
				data.icon = data.icon.scaled(
					QSize(inner, inner) * ratio,
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
		}

		const auto full = QSize(size, size) * ratio;
		auto image = (data.image.size() == full)
			? base::take(data.image)
			: QImage(full, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		image.setDevicePixelRatio(ratio);

		const auto bg = EmptyUserpic::UserpicColor(
			EmptyUserpic::ColorIndex(UniqueRowIdFromString(icon)));
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		{
			auto gradient = QLinearGradient(0, 0, 0, size);
			gradient.setStops({
				{ 0., bg.color1->c },
				{ 1., bg.color2->c }
			});
			p.setBrush(gradient);
		}
		p.setPen(Qt::NoPen);
		p.drawEllipse(QRect(0, 0, size, size));
		if (!data.icon.isNull()) {
			p.drawImage(
				QRect(skip, skip, inner, inner),
				style::colorizeImage(data.icon, st::historyPeerUserpicFg));
		}
		p.end();

		data.paletteVersion = version;
		data.iconLoaded = loaded;
		data.image = std::move(image);
	}
	p.drawImage(x, y, data.image);
}

[[nodiscard]] QByteArray DefaultCenter() {
	if (!LastExactLocation) {
		return "null";
	}
	return "["_q
		+ QByteArray::number(LastExactLocation.point.x())
		+ ","_q
		+ QByteArray::number(LastExactLocation.point.y())
		+ "]"_q;
}

[[nodiscard]] QByteArray DefaultBounds() {
	const auto country = Core::ResolveCurrentCountryLocation();
	if (!country) {
		return "null";
	}
	return "[["_q
		+ QByteArray::number(country.bounds.x())
		+ ","_q
		+ QByteArray::number(country.bounds.y())
		+ "],["_q
		+ QByteArray::number(country.bounds.x() + country.bounds.width())
		+ ","_q
		+ QByteArray::number(country.bounds.y() + country.bounds.height())
		+ "]]"_q;
}

[[nodiscard]] QByteArray ComputeStyles() {
	static const auto map = base::flat_map<QByteArray, const style::color*>{
		{ "window-bg", &st::windowBg },
		{ "window-bg-over", &st::windowBgOver },
		{ "window-bg-ripple", &st::windowBgRipple },
		{ "window-active-text-fg", &st::windowActiveTextFg },
	};
	static const auto phrases = base::flat_map<QByteArray, tr::phrase<>>{
		{ "maps-places-in-area", tr::lng_maps_places_in_area },
	};
	return Ui::ComputeStyles(map, phrases, Window::Theme::IsNightMode());
}

[[nodiscard]] QByteArray ReadResource(const QString &name) {
	auto file = QFile(u":/picker/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QByteArray PickerContent() {
	return R"(<!DOCTYPE html>
<html style=")"
+ EscapeForAttribute(ComputeStyles())
+ R"(">
	<head>
		<meta charset="utf-8">
		<meta name="robots" content="noindex, nofollow">
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<script src="/location/picker.js"></script>
		<link rel="stylesheet" href="/location/picker.css" />
		<script src='https://api.mapbox.com/mapbox-gl-js/v3.4.0/mapbox-gl.js'></script>
		<link href='https://api.mapbox.com/mapbox-gl-js/v3.4.0/mapbox-gl.css' rel='stylesheet' />
	</head>
	<body>
		<div id="marker"><div id="marker_drop"></div></div>
		<div id="map"></div>
		<script>LocationPicker.notify({ event: 'ready' });</script>
	</body>
</html>
)"_q;
}

[[nodiscard]] object_ptr<AbstractButton> MakeSendLocationButton(
		QWidget *parent,
		rpl::producer<QString> address) {
	auto result = object_ptr<FlatButton>(
		parent,
		QString(),
		st::pickLocationButton);
	const auto raw = result.data();

	const auto st = &st::pickLocationVenue;
	const auto icon = CreateChild<RpWidget>(raw);
	icon->setGeometry(
		st->photoPosition.x(),
		st->photoPosition.y(),
		st->photoSize,
		st->photoSize);
	icon->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		p.drawEllipse(icon->rect());
		st::pickLocationSendIcon.paintInCenter(p, icon->rect());
	}, icon->lifetime());
	icon->show();

	const auto hadAddress = std::make_shared<bool>(false);
	auto statusText = std::move(
		address
	) | rpl::map([=](const QString &text) {
		if (!text.isEmpty()) {
			*hadAddress = true;
			return text;
		}
		return *hadAddress ? tr::lng_contacts_loading(tr::now) : QString();
	});
	const auto name = CreateChild<FlatLabel>(
		raw,
		tr::lng_maps_point_send(tr::now),
		st::pickLocationButtonText);
	name->show();
	const auto status = CreateChild<FlatLabel>(
		raw,
		rpl::duplicate(statusText),
		st::pickLocationButtonStatus);
	status->showOn(std::move(
		statusText
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}) | rpl::distinct_until_changed());
	rpl::combine(
		result->widthValue(),
		status->shownValue()
	) | rpl::start_with_next([=](int width, bool statusShown) {
		const auto available = width
			- st->namePosition.x()
			- st->button.padding.right();
		const auto namePosition = st->namePosition;
		const auto statusPosition = st->statusPosition;
		name->resizeToWidth(available);
		const auto nameTop = statusShown
			? namePosition.y()
			: (st->height - name->height()) / 2;
		name->moveToLeft(namePosition.x(), nameTop, width);
		status->resizeToWidth(available);
		status->moveToLeft(statusPosition.x(), statusPosition.y(), width);
	}, name->lifetime());

	return result;
}

void SetupVenues(
		not_null<VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<std::vector<VenueData>> value) {
	auto &lifetime = container->lifetime();
	const auto delegate = lifetime.make_state<PeerListContentDelegateShow>(
		show);
	const auto controller = lifetime.make_state<LinksController>(
		&show->session(),
		std::move(value));
	controller->setStyleOverrides(&st::pickLocationVenueList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	show->session().downloaderTaskFinished() | rpl::start_with_next([=] {
		content->update();
	}, content->lifetime());
}

[[nodiscard]] PickerVenueList ParseVenues(
		not_null<Main::Session*> session,
		const MTPmessages_BotResults &venues) {
	const auto &data = venues.data();
	session->data().processUsers(data.vusers());

	auto &list = data.vresults().v;
	auto result = PickerVenueList();
	result.list.reserve(list.size());
	for (const auto &found : list) {
		found.match([&](const auto &data) {
			data.vsend_message().match([&](
					const MTPDbotInlineMessageMediaVenue &data) {
				data.vgeo().match([&](const MTPDgeoPoint &geo) {
					result.list.push_back({
						.lat = geo.vlat().v,
						.lon = geo.vlong().v,
						.title = qs(data.vtitle()),
						.address = qs(data.vaddress()),
						.provider = qs(data.vprovider()),
						.id = qs(data.vvenue_id()),
						.venueType = qs(data.vvenue_type()),
					});
				}, [](const auto &) {});
			}, [](const auto &) {});
		});
	}
	return result;
}

} // namespace

LocationPicker::LocationPicker(Descriptor &&descriptor)
: _callback(std::move(descriptor.callback))
, _quit(std::move(descriptor.quit))
, _window(std::make_unique<SeparatePanel>())
, _body((_window->setInnerSize(st::pickLocationWindow)
	, _window->showInner(base::make_unique_q<RpWidget>(_window.get()))
	, _window->inner()))
, _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("LocationPicker.updateStyles('" + str + "');");
	}
})
, _venueState(PickerVenueLoading())
, _session(descriptor.session)
, _api(&_session->mtp()) {
	std::move(
		descriptor.closeRequests
	) | rpl::start_with_next([=] {
		_window = nullptr;
		delete this;
	}, _lifetime);

	setup(descriptor);
}

std::shared_ptr<Main::SessionShow> LocationPicker::uiShow() {
	return Main::MakeSessionShow(nullptr, _session);
}

bool LocationPicker::Available(
		const QString &mapsToken,
		const QString &geocodingToken) {
	static const auto Supported = Webview::NavigateToDataSupported();
	MapsProviderToken = mapsToken;
	GeocodingProviderToken = geocodingToken;
	return Supported && !MapsProviderToken.isEmpty();
}

void LocationPicker::setup(const Descriptor &descriptor) {
	setupWindow(descriptor);
	setupWebview(descriptor);
	if (LastExactLocation) {
		venuesRequest(LastExactLocation);
		resolveAddress(LastExactLocation);
	}
}

void LocationPicker::setupWindow(const Descriptor &descriptor) {
	const auto window = _window.get();

	window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	window->closeRequests() | rpl::start_with_next([=] {
		close();
	}, _lifetime);

	const auto parent = descriptor.parent
		? descriptor.parent->window()->geometry()
		: QGuiApplication::primaryScreen()->availableGeometry();
	window->setTitle(tr::lng_maps_point());
	window->move(
		parent.x() + (parent.width() - window->width()) / 2,
		parent.y() + (parent.height() - window->height()) / 2);

	_container = CreateChild<RpWidget>(_body.get());
	_scroll = CreateChild<ScrollArea>(_body.get());
	const auto controls = _scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(_scroll));
	const auto toppad = controls->add(object_ptr<RpWidget>(controls));

	const auto button = controls->add(
		MakeSendLocationButton(controls, _geocoderAddress.value()),
		{ 0, st::pickLocationButtonSkip, 0, st::pickLocationButtonSkip });
	button->setClickedCallback([=] {
		_webview->eval("LocationPicker.send();");
	});

	AddDivider(controls);
	AddSkip(controls);
	AddSubsectionTitle(controls, tr::lng_maps_or_choose());

	SetupVenues(controls, uiShow(), _venueState.value(
	) | rpl::filter([=](const PickerVenueState &state) {
		return v::is<PickerVenueList>(state);
	}) | rpl::map([=](PickerVenueState &&state) {
		return std::move(v::get<PickerVenueList>(state).list);
	}));

	rpl::combine(
		_body->sizeValue(),
		_scroll->scrollTopValue()
	) | rpl::start_with_next([=](QSize size, int scrollTop) {
		const auto width = size.width();
		const auto height = size.height();
		const auto sub = std::min(
			(st::pickLocationMapHeight - st::pickLocationCollapsedHeight),
			scrollTop);
		const auto mapHeight = st::pickLocationMapHeight - sub;
		const auto scrollHeight = height - mapHeight;
		_container->setGeometry(0, 0, width, mapHeight);
		_scroll->setGeometry(0, mapHeight, width, scrollHeight);
		controls->resizeToWidth(width);
		toppad->resize(width, sub);
	}, _container->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	_scroll->hide();
	controls->show();
	button->show();
	window->show();
}

void LocationPicker::setupWebview(const Descriptor &descriptor) {
	Expects(!_webview);

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = descriptor.storageId,
			.dataProtocolOverride = kProtocolOverride,
		});
	const auto raw = _webview.get();

	window->lifetime().add([=] {
		_webview = nullptr;
	});

	window->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			const auto event = static_cast<QKeyEvent*>(e.get());
			if (event->key() == Qt::Key_Escape) {
				close();
			}
		}
	}, window->lifetime());
	raw->widget()->show();

	_container->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		raw->widget()->setGeometry(QRect(QPoint(), size));
	}, _container->lifetime());

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		return true;
	});
	raw->setNavigationDoneHandler([=](bool success) {
	});
	raw->setMessageHandler([=](const QJsonDocument &message) {
		crl::on_main(_window.get(), [=] {
			const auto object = message.object();
			const auto event = object.value("event").toString();
			if (event == u"ready"_q) {
				mapReady();
				resolveCurrentLocation();
			} else if (event == u"keydown"_q) {
				const auto key = object.value("key").toString();
				const auto modifier = object.value("modifier").toString();
				processKey(key, modifier);
			} else if (event == u"send"_q) {
				const auto lat = object.value("latitude").toDouble();
				const auto lon = object.value("longitude").toDouble();
				_callback({ lat, lon });
				close();
			}
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		const auto pos = request.id.find('#');
		if (pos != request.id.npos) {
			request.id = request.id.substr(0, pos);
		}
		if (!request.id.starts_with("location/")) {
			return Webview::DataResult::Failed;
		}
		const auto finishWith = [&](QByteArray data, std::string mime) {
			request.done({
				.stream = std::make_unique<Webview::DataStreamFromMemory>(
					std::move(data),
					std::move(mime)),
				});
			return Webview::DataResult::Done;
		};
		if (!_subscribedToColors) {
			_subscribedToColors = true;

			rpl::merge(
				Lang::Updated(),
				style::PaletteChanged()
			) | rpl::start_with_next([=] {
				_updateStyles.call();
			}, _webview->lifetime());
		}
		const auto id = std::string_view(request.id).substr(9);
		if (id == "picker.html") {
			return finishWith(PickerContent(), "text/html; charset=utf-8");
		}
		const auto css = id.ends_with(".css");
		const auto js = !css && id.ends_with(".js");
		if (!css && !js) {
			return Webview::DataResult::Failed;
		}
		const auto qstring = QString::fromUtf8(id.data(), id.size());
		const auto pattern = u"^[a-zA-Z\\.\\-_0-9]+$"_q;
		if (QRegularExpression(pattern).match(qstring).hasMatch()) {
			const auto bytes = ReadResource(qstring);
			if (!bytes.isEmpty()) {
				const auto mime = css ? "text/css" : "text/javascript";
				return finishWith(bytes, mime);
			}
		}
		return Webview::DataResult::Failed;
	});

	raw->init(R"()");
	raw->navigateToData("location/picker.html");
}

void LocationPicker::resolveAddress(Core::GeoLocation location) {
	if (_geocoderResolvingFor == location) {
		return;
	}
	_geocoderResolvingFor = location;
	const auto done = [=](Core::GeoAddress address) {
		if (_geocoderResolvingFor != location) {
			return;
		} else if (address) {
			_geocoderAddress = address.name;
		} else {
			_geocoderAddress = u"(%1, %2)"_q
				.arg(location.point.x(), 0, 'f')
				.arg(location.point.y(), 0, 'f');
		}
	};
	Core::ResolveLocationAddress(
		location,
		GeocodingProviderToken,
		crl::guard(this, done));
}

void LocationPicker::mapReady() {
	Expects(_scroll != nullptr);

	const auto token = MapsProviderToken.toUtf8();
	const auto center = DefaultCenter();
	const auto bounds = DefaultBounds();
	const auto protocol = *kProtocolOverride
		? "'"_q + kProtocolOverride + "'"
		: "null";
	const auto params = "token: '" + token + "'"
		+ ", center: " + center
		+ ", bounds: " + bounds
		+ ", protocol: " + protocol;
	_webview->eval("LocationPicker.init({ " + params + " });");

	_scroll->show();
}

void LocationPicker::venuesRequest(
		Core::GeoLocation location,
		QString query) {
	query = NormalizeVenuesQuery(query);
	auto &cache = _venuesCache[query];
	const auto i = ranges::find(
		cache,
		location,
		&VenuesCacheEntry::location);
	if (i != end(cache)) {
		_venueState = i->result;
		return;
	} else if (_venuesRequestLocation == location
		&& _venuesRequestQuery == query) {
		return;
	} else if (const auto oldRequestId = base::take(_venuesRequestId)) {
		_api.request(oldRequestId).cancel();
	}
	_venueState = PickerVenueLoading();
	_venuesRequestLocation = location;
	_venuesRequestQuery = query;
	if (_venuesBot) {
		venuesSendRequest();
	} else if (_venuesBotRequestId) {
		return;
	}
	const auto username = _session->serverConfig().venueSearchUsername;
	_venuesBotRequestId = _api.request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		const auto peer = _session->data().peerLoaded(
			peerFromMTP(data.vpeer()));
		const auto user = peer ? peer->asUser() : nullptr;
		if (user && user->isBotInlineGeo()) {
			_venuesBot = user;
			venuesSendRequest();
		} else {
			LOG(("API Error: Bad peer returned by: %1").arg(username));
		}
	}).fail([=] {
		LOG(("API Error: Error returned on lookup: %1").arg(username));
	}).send();
}

void LocationPicker::venuesSendRequest() {
	Expects(_venuesBot != nullptr);

	if (_venuesRequestId || !_venuesRequestLocation) {
		return;
	}
	_venuesRequestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(MTPmessages_GetInlineBotResults::Flag::f_geo_point),
		_venuesBot->inputUser,
		MTP_inputPeerEmpty(),
		MTP_inputGeoPoint(
			MTP_flags(0),
			MTP_double(_venuesRequestLocation.point.x()),
			MTP_double(_venuesRequestLocation.point.y()),
			MTP_int(0)), // accuracy_radius
		MTP_string(_venuesRequestQuery),
		MTP_string() // offset
	)).done([=](const MTPmessages_BotResults &result) {
		auto parsed = ParseVenues(_session, result);
		_venuesCache[_venuesRequestQuery].push_back({
			.location = _venuesRequestLocation,
			.result = parsed,
		});
		if (parsed.list.empty()) {
			_venueState = PickerVenueNothingFound{ _venuesRequestQuery };
		} else {
			_venueState = std::move(parsed);
		}
	}).fail([=] {
		_venueState = PickerVenueNothingFound{ _venuesRequestQuery };
	}).send();
}

void LocationPicker::resolveCurrentLocation() {
	using namespace Core;
	const auto window = _window.get();
	ResolveCurrentGeoLocation(crl::guard(window, [=](GeoLocation location) {
		const auto changed = (LastExactLocation != location);
		if (location.accuracy != GeoLocationAccuracy::Exact || !changed) {
			return;
		}
		LastExactLocation = location;
		if (location) {
			venuesRequest(location);
			resolveAddress(location);
		}
		if (_webview) {
			const auto point = QByteArray::number(location.point.x())
				+ ","_q
				+ QByteArray::number(location.point.y());
			_webview->eval("LocationPicker.narrowTo([" + point + "]);");
		}
	}));
}

void LocationPicker::processKey(
		const QString &key,
		const QString &modifier) {
	const auto ctrl = ::Platform::IsMac() ? u"cmd"_q : u"ctrl"_q;
	if (key == u"escape"_q || (key == u"w"_q && modifier == ctrl)) {
		close();
	} else if (key == u"m"_q && modifier == ctrl) {
		minimize();
	} else if (key == u"q"_q && modifier == ctrl) {
		quit();
	}
}

void LocationPicker::close() {
	crl::on_main(this, [=] {
		_window = nullptr;
		delete this;
	});
}

void LocationPicker::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState()
			| Qt::WindowMinimized);
	}
}

void LocationPicker::quit() {
	if (const auto onstack = _quit) {
		onstack();
	}
}

not_null<LocationPicker*> LocationPicker::Show(Descriptor &&descriptor) {
	return new LocationPicker(std::move(descriptor));
}

} // namespace Ui
