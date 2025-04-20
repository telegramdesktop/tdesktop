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
#include "dialogs/ui/chat_search_empty.h" // Dialogs::SearchEmpty.
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
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
#include "styles/style_settings.h" // settingsCloudPasswordIconSize
#include "styles/style_layers.h" // boxDividerHeight

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Ui {
namespace {

constexpr auto kResolveAddressDelay = 3 * crl::time(1000);
constexpr auto kSearchDebounceDelay = crl::time(900);

#if defined Q_OS_MAC || defined Q_OS_LINUX
const auto kProtocolOverride = "mapboxapihelper";
#else // Q_OS_MAC
const auto kProtocolOverride = "";
#endif // Q_OS_MAC

Core::GeoLocation LastExactLocation;

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
	refreshName(st::pickLocationVenueItem);
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

class VenuesController final
	: public PeerListController
	, public VenueRowDelegate
	, public base::has_weak_ptr {
public:
	VenuesController(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<VenueData>> content,
		Fn<void(VenueData)> callback);

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
	const Fn<void(VenueData)> _callback;
	rpl::variable<std::vector<VenueData>> _rows;

	base::flat_map<QString, VenueIcon> _icons;

	rpl::lifetime _lifetime;

};

[[nodiscard]] QString NormalizeVenuesQuery(QString query) {
	return query.trimmed().toLower();
}

[[nodiscard]] object_ptr<RpWidget> MakeFoursquarePromo() {
	auto result = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto skip = st::defaultVerticalListSkip;
	const auto raw = result.data();
	raw->resize(0, skip + st::pickLocationPromoHeight);
	const auto shadow = CreateChild<PlainShadow>(raw);
	raw->widthValue() | rpl::start_with_next([=](int width) {
		shadow->setGeometry(0, skip, width, st::lineWidth);
	}, raw->lifetime());
	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::windowBg);
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		p.drawText(
			raw->rect().marginsRemoved({ 0, skip, 0, 0 }),
			tr::lng_maps_venues_source(tr::now),
			style::al_center);
	}, raw->lifetime());
	return result;
}

VenuesController::VenuesController(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<VenueData>> content,
	Fn<void(VenueData)> callback)
: _session(session)
, _callback(std::move(callback))
, _rows(std::move(content)) {
}

void VenuesController::prepare() {
	_rows.value(
	) | rpl::start_with_next([=](const std::vector<VenueData> &rows) {
		rebuild(rows);
	}, _lifetime);
}

void VenuesController::rebuild(const std::vector<VenueData> &rows) {
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
	if (i > 0) {
		delegate()->peerListSetBelowWidget(MakeFoursquarePromo());
	} else {
		delegate()->peerListSetBelowWidget({ nullptr });
	}
	delegate()->peerListRefreshRows();
}

void VenuesController::rowClicked(not_null<PeerListRow*> row) {
	_callback(static_cast<VenueRow*>(row.get())->data());
}

void VenuesController::rowRightActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

Main::Session &VenuesController::session() const {
	return *_session;
}

void VenuesController::appendRow(const VenueData &data) {
	delegate()->peerListAppendRow(std::make_unique<VenueRow>(this, data));
}

void VenuesController::rowPaintIcon(
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
				if (!data.icon.isNull()) {
					data.icon = data.icon.convertToFormat(
						QImage::Format_ARGB32_Premultiplied);
				}
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

[[nodiscard]] QByteArray DefaultCenter(Core::GeoLocation initial) {
	const auto &use = initial.exact() ? initial : LastExactLocation;
	if (!use) {
		return "null";
	}
	return "["_q
		+ QByteArray::number(use.point.x())
		+ ","_q
		+ QByteArray::number(use.point.y())
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
		{ "history-to-down-shadow", &st::historyToDownShadow },
	};
	static const auto phrases = base::flat_map<QByteArray, tr::phrase<>>{
		{ "maps-places-in-area", tr::lng_maps_places_in_area },
	};
	return Ui::ComputeStyles(map, phrases, 100, Window::Theme::IsNightMode());
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
		<div id="search_venues">
			<div id="search_venues_inner"><span id="search_venues_content"></span></div>
		</div>
		<div id="marker">
			<div id="marker_shadow" style="transform: translate(0px, -14px);">
<svg display="block" height="41px" width="27px" viewBox="0 0 27 41">
	<defs>
		<radialGradient id="shadowGradient">
			<stop offset="10%" stop-opacity="0.4"></stop>
			<stop offset="100%" stop-opacity="0.05"></stop>
		</radialGradient>
	</defs>
	<ellipse
		cx="13.5"
		cy="34.8"
		rx="10.5"
		ry="5.25"
		fill=")" + "url(#shadowGradient)" + R"("></ellipse>
</svg>
			</div>
			<div id="marker_drop" style="transform: translate(0px, -14px);">
<svg display="block" height="41px" width="27px" viewBox="0 0 27 41">
	<path fill="#3FB1CE" d="M27,13.5C27,19.07 20.25,27 14.75,34.5C14.02,35.5 12.98,35.5 12.25,34.5C6.75,27 0,19.22 0,13.5C0,6.04 6.04,0 13.5,0C20.96,0 27,6.04 27,13.5Z"></path><path opacity="0.25" d="M13.5,0C6.04,0 0,6.04 0,13.5C0,19.22 6.75,27 12.25,34.5C13,35.52 14.02,35.5 14.75,34.5C20.25,27 27,19.07 27,13.5C27,6.04 20.96,0 13.5,0ZM13.5,1C20.42,1 26,6.58 26,13.5C26,15.9 24.5,19.18 22.22,22.74C19.95,26.3 16.71,30.14 13.94,33.91C13.74,34.18 13.61,34.32 13.5,34.44C13.39,34.32 13.26,34.18 13.06,33.91C10.28,30.13 7.41,26.31 5.02,22.77C2.62,19.23 1,15.95 1,13.5C1,6.58 6.58,1 13.5,1Z"></path>
	<circle fill="white" cx="13.5" cy="13.5" r="5.5"></circle>
</svg>
			</div>
		</div>
		<div id="map"></div>
		<script>LocationPicker.notify({ event: 'ready' });</script>
	</body>
</html>
)"_q;
}

[[nodiscard]] object_ptr<AbstractButton> MakeChooseLocationButton(
		QWidget *parent,
		rpl::producer<QString> label,
		rpl::producer<QString> address) {
	auto result = object_ptr<FlatButton>(
		parent,
		QString(),
		st::pickLocationButton);
	const auto raw = result.data();

	const auto st = &st::pickLocationVenueItem;
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
		std::move(label),
		st::pickLocationButtonText);
	name->show();
	const auto status = CreateChild<FlatLabel>(
		raw,
		rpl::duplicate(statusText),
		st::pickLocationButtonStatus);
	status->showOn(rpl::duplicate(
		statusText
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}) | rpl::distinct_until_changed());
	rpl::combine(
		result->widthValue(),
		std::move(statusText)
	) | rpl::start_with_next([=](int width, const QString &statusText) {
		const auto available = width
			- st->namePosition.x()
			- st->button.padding.right();
		const auto namePosition = st->namePosition;
		const auto statusPosition = st->statusPosition;
		name->resizeToWidth(available);
		const auto nameTop = statusText.isEmpty()
			? ((st->height - name->height()) / 2)
			: namePosition.y();
		name->moveToLeft(namePosition.x(), nameTop, width);
		status->resizeToNaturalWidth(available);
		status->moveToLeft(statusPosition.x(), statusPosition.y(), width);
	}, name->lifetime());

	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	status->setAttribute(Qt::WA_TransparentForMouseEvents);

	return result;
}

void SetupLoadingView(not_null<RpWidget*> container) {
	class Loading final : public RpWidget {
	public:
		explicit Loading(QWidget *parent)
		: RpWidget(parent)
		, animation(
				[=] { if (!anim::Disabled()) update(); },
				st::pickLocationLoading) {
			animation.start(st::pickLocationLoading.sineDuration);
		}

	private:
		void paintEvent(QPaintEvent *e) override {
			auto p = QPainter(this);
			const auto size = st::pickLocationLoading.size;
			const auto inner = QRect(QPoint(), size);
			const auto positioned = style::centerrect(rect(), inner);
			animation.draw(p, positioned.topLeft(), size, width());
		}

		InfiniteRadialAnimation animation;

	};

	const auto view = CreateChild<Loading>(container);
	view->resize(container->width(), st::recentPeersEmptyHeightMin);
	view->show();

	ResizeFitChild(container, view);
}

void SetupEmptyView(
		not_null<RpWidget*> container,
		std::optional<QString> query) {
	using Icon = Dialogs::SearchEmptyIcon;
	const auto view = CreateChild<Dialogs::SearchEmpty>(
		container,
		(query ? Icon::NoResults : Icon::Search),
		(query
			? tr::lng_maps_no_places
			: tr::lng_maps_choose_to_search)(Text::WithEntities));
	view->setMinimalHeight(st::recentPeersEmptyHeightMin);
	view->show();

	ResizeFitChild(container, view);

	InvokeQueued(view, [=] { view->animate(); });
}

void SetupVenues(
		not_null<VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<PickerVenueState> value,
		Fn<void(VenueData)> callback) {
	const auto otherWrap = container->add(object_ptr<SlideWrap<RpWidget>>(
		container,
		object_ptr<RpWidget>(container)));
	const auto other = otherWrap->entity();
	rpl::duplicate(
		value
	) | rpl::start_with_next([=](const PickerVenueState &state) {
		while (!other->children().isEmpty()) {
			delete other->children()[0];
		}
		if (v::is<PickerVenueList>(state)) {
			otherWrap->hide(anim::type::instant);
			return;
		} else if (v::is<PickerVenueLoading>(state)) {
			SetupLoadingView(other);
		} else {
			const auto n = std::get_if<PickerVenueNothingFound>(&state);
			SetupEmptyView(other, n ? n->query : std::optional<QString>());
		}
		otherWrap->show(anim::type::instant);
	}, otherWrap->lifetime());

	auto &lifetime = container->lifetime();
	auto venuesList = rpl::duplicate(
		value
	) | rpl::map([=](PickerVenueState &&state) {
		return v::is<PickerVenueList>(state)
			? std::move(v::get<PickerVenueList>(state).list)
			: std::vector<VenueData>();
	});
	const auto delegate = lifetime.make_state<PeerListContentDelegateShow>(
		show);
	const auto controller = lifetime.make_state<VenuesController>(
		&show->session(),
		std::move(venuesList),
		std::move(callback));
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

not_null<RpWidget*> SetupMapPlaceholder(
		not_null<RpWidget*> parent,
		int minHeight,
		int maxHeight,
		Fn<void()> choose) {
	const auto result = CreateChild<RpWidget>(parent);

	const auto top = CreateChild<BoxContentDivider>(result);
	const auto bottom = CreateChild<BoxContentDivider>(result);

	const auto icon = CreateChild<RpWidget>(result);
	const auto iconSize = st::settingsCloudPasswordIconSize;
	auto ownedLottie = Lottie::MakeIcon({
		.name = u"location"_q,
		.sizeOverride = { iconSize, iconSize },
		.limitFps = true,
	});
	const auto lottie = ownedLottie.get();
	icon->lifetime().add([kept = std::move(ownedLottie)] {});

	icon->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		const auto left = (icon->width() - iconSize) / 2;
		const auto scale = icon->height() / float64(iconSize);
		auto hq = std::optional<PainterHighQualityEnabler>();
		if (scale < 1.) {
			const auto center = QPointF(
				icon->width() / 2.,
				icon->height() / 2.);
			hq.emplace(p);
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
			p.setOpacity(scale);
		}
		lottie->paint(p, left, 0);
	}, icon->lifetime());

	InvokeQueued(icon, [=] {
		const auto till = lottie->framesCount() - 1;
		lottie->animate([=] { icon->update(); }, 0, till);
	});

	const auto button = CreateChild<RoundButton>(
		result,
		tr::lng_maps_select_on_map(),
		st::pickLocationChooseOnMap);
	button->setFullRadius(true);
	button->setTextTransform(RoundButton::TextTransform::NoTransform);
	button->setClickedCallback(choose);

	parent->sizeValue() | rpl::start_with_next([=](QSize size) {
		result->setGeometry(QRect(QPoint(), size));

		const auto width = size.width();
		top->setGeometry(0, 0, width, top->height());
		bottom->setGeometry(QRect(
			QPoint(0, size.height() - bottom->height()),
			QSize(width, bottom->height())));
		const auto dividers = top->height() + bottom->height();

		const auto ratio = (size.height() - minHeight)
			/ float64(maxHeight - minHeight);
		const auto iconHeight = int(base::SafeRound(ratio * iconSize));

		const auto available = size.height() - dividers;
		const auto maxDelta = (maxHeight
			- dividers
			- iconSize
			- button->height()) / 2;
		const auto minDelta = (minHeight - dividers - button->height()) / 2;

		const auto delta = anim::interpolate(minDelta, maxDelta, ratio);
		button->move(
			(width - button->width()) / 2,
			size.height() - bottom->height() - delta - button->height());
		const auto wide = available - delta - button->height();
		const auto skip = (wide - iconHeight) / 2;
		icon->setGeometry(0, top->height() + skip, width, iconHeight);
	}, result->lifetime());

	top->show();
	icon->show();
	bottom->show();
	result->show();

	return result;
}

} // namespace

LocationPicker::LocationPicker(Descriptor &&descriptor)
: _config(std::move(descriptor.config))
, _callback(std::move(descriptor.callback))
, _quit(std::move(descriptor.quit))
, _window(std::make_unique<SeparatePanel>())
, _body((_window->setInnerSize(st::pickLocationWindow)
	, _window->showInner(base::make_unique_q<RpWidget>(_window.get()))
	, _window->inner()))
, _chooseButtonLabel(std::move(descriptor.chooseLabel))
, _webviewStorageId(descriptor.storageId)
, _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("LocationPicker.updateStyles('" + str + "');");
	}
})
, _geocoderResolveTimer([=] { resolveAddressByTimer(); })
, _venueState(PickerVenueLoading())
, _session(descriptor.session)
, _venuesSearchDebounceTimer([=] {
	Expects(_venuesSearchLocation.has_value());
	Expects(_venuesSearchQuery.has_value());

	venuesRequest(*_venuesSearchLocation, *_venuesSearchQuery);
})
, _api(&_session->mtp())
, _venueRecipient(descriptor.recipient) {
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

bool LocationPicker::Available(const LocationPickerConfig &config) {
	static const auto Supported = [&] {
		const auto availability = Webview::Availability();
		return availability.customSchemeRequests
			&& availability.customReferer;
	}();
	return Supported && !config.mapsToken.isEmpty();
}

void LocationPicker::setup(const Descriptor &descriptor) {
	setupWindow(descriptor);

	_initialProvided = descriptor.initial;
	const auto initial = _initialProvided.exact()
		? _initialProvided
		: LastExactLocation;
	if (initial) {
		venuesRequest(initial);
		resolveAddress(initial);
		venuesSearchEnableAt(initial);
	}
	if (!_initialProvided) {
		resolveCurrentLocation();
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
	_mapPlaceholderAdded = st::pickLocationButtonSkip
		+ st::pickLocationButton.height
		+ st::pickLocationButtonSkip
		+ st::boxDividerHeight;
	const auto min = st::pickLocationCollapsedHeight + _mapPlaceholderAdded;
	const auto max = st::pickLocationMapHeight + _mapPlaceholderAdded;
	_mapPlaceholder = SetupMapPlaceholder(_container, min, max, [=] {
		setupWebview();
	});
	_scroll = CreateChild<ScrollArea>(_body.get());
	const auto controls = _scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(_scroll));

	_mapControlsWrap = controls->add(
		object_ptr<SlideWrap<VerticalLayout>>(
			controls,
			object_ptr<VerticalLayout>(controls)));
	_mapControlsWrap->show(anim::type::instant);
	const auto mapControls = _mapControlsWrap->entity();

	const auto toppad = mapControls->add(object_ptr<RpWidget>(controls));

	AddSkip(mapControls);
	AddSubsectionTitle(mapControls, tr::lng_maps_or_choose());

	auto state = _venueState.value();
	SetupVenues(controls, uiShow(), std::move(state), [=](VenueData info) {
		_callback(std::move(info));
		close();
	});

	rpl::combine(
		_body->sizeValue(),
		_scroll->scrollTopValue(),
		_venuesSearchShown.value()
	) | rpl::start_with_next([=](QSize size, int scrollTop, bool search) {
		const auto width = size.width();
		const auto height = size.height();
		const auto sub = std::min(
			(st::pickLocationMapHeight - st::pickLocationCollapsedHeight),
			scrollTop);
		const auto mapHeight = st::pickLocationMapHeight
			- sub
			+ (_mapPlaceholder ? _mapPlaceholderAdded : 0);
		_container->setGeometry(0, 0, width, mapHeight);
		const auto scrollWidgetTop = search ? 0 : mapHeight;
		const auto scrollHeight = height - scrollWidgetTop;
		_scroll->setGeometry(0, scrollWidgetTop, width, scrollHeight);
		controls->resizeToWidth(width);
		toppad->resize(width, sub);
	}, _container->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	_scroll->show();
	controls->show();
	window->show();
}

void LocationPicker::setupWebview() {
	Expects(!_webview);

	delete base::take(_mapPlaceholder);

	const auto mapControls = _mapControlsWrap->entity();
	mapControls->insert(
		1,
		object_ptr<BoxContentDivider>(mapControls)
	)->show();

	_mapButton = mapControls->insert(
		1,
		MakeChooseLocationButton(
			mapControls,
			_chooseButtonLabel.value(),
			_geocoderAddress.value()),
		{ 0, st::pickLocationButtonSkip, 0, st::pickLocationButtonSkip });
	_mapButton->setClickedCallback([=] {
		_webview->eval("LocationPicker.send();");
	});
	_mapButton->hide();

	_scroll->scrollToY(0);
	_venuesSearchShown.force_assign(_venuesSearchShown.current());

	_mapLoading = CreateChild<RpWidget>(_body.get());

	_container->geometryValue() | rpl::start_with_next([=](QRect rect) {
		_mapLoading->setGeometry(rect);
	}, _mapLoading->lifetime());

	SetupLoadingView(_mapLoading);
	_mapLoading->show();

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = _webviewStorageId,
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
			if (event->key() == Qt::Key_Escape && !_venuesSearchQuery) {
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
			} else if (event == u"keydown"_q) {
				const auto key = object.value("key").toString();
				const auto modifier = object.value("modifier").toString();
				processKey(key, modifier);
			} else if (event == u"send"_q) {
				const auto lat = object.value("latitude").toDouble();
				const auto lon = object.value("longitude").toDouble();
				_callback({
					.lat = lat,
					.lon = lon,
					.address = _geocoderAddress.current(),
				});
				close();
			} else if (event == u"move_start"_q) {
				if (const auto now = _geocoderAddress.current()
					; !now.isEmpty()) {
					_geocoderSavedAddress = now;
					_geocoderAddress = QString();
				}
				base::take(_geocoderResolvePostponed);
				_geocoderResolveTimer.cancel();
			} else if (event == u"move_end"_q) {
				const auto lat = object.value("latitude").toDouble();
				const auto lon = object.value("longitude").toDouble();
				const auto location = Core::GeoLocation{
					.point = { lat, lon },
					.accuracy = Core::GeoLocationAccuracy::Exact,
				};
				if (AreTheSame(_geocoderResolvingFor, location)
					&& !_geocoderSavedAddress.isEmpty()) {
					_geocoderAddress = base::take(_geocoderSavedAddress);
					_geocoderResolveTimer.cancel();
				} else {
					_geocoderResolvePostponed = location;
					_geocoderResolveTimer.callOnce(kResolveAddressDelay);
				}
				if (!AreTheSame(_venuesRequestLocation, location)) {
					_webview->eval(
						"LocationPicker.toggleSearchVenues(true);");
				}
				venuesSearchEnableAt(location);
			} else if (event == u"search_venues"_q) {
				const auto lat = object.value("latitude").toDouble();
				const auto lon = object.value("longitude").toDouble();
				venuesRequest({
					.point = { lat, lon },
					.accuracy = Core::GeoLocationAccuracy::Exact,
				});
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

void LocationPicker::resolveAddressByTimer() {
	if (const auto location = base::take(_geocoderResolvePostponed)) {
		resolveAddress(location);
	}
}

void LocationPicker::resolveAddress(Core::GeoLocation location) {
	if (AreTheSame(_geocoderResolvingFor, location)) {
		return;
	}
	_geocoderResolvingFor = location;
	const auto done = [=](Core::GeoAddress address) {
		if (!AreTheSame(_geocoderResolvingFor, location)) {
			return;
		} else if (address) {
			_geocoderAddress = address.name;
		} else {
			_geocoderAddress = u"(%1, %2)"_q
				.arg(location.point.x(), 0, 'f')
				.arg(location.point.y(), 0, 'f');
		}
	};
	const auto baseLangId = Lang::GetInstance().baseId();
	const auto langId = baseLangId.isEmpty()
		? Lang::GetInstance().id()
		: baseLangId;
	const auto nonEmptyId = langId.isEmpty() ? u"en"_q : langId;
	Core::ResolveLocationAddress(
		location,
		langId,
		_config.geoToken,
		crl::guard(this, done));
}

void LocationPicker::mapReady() {
	Expects(_scroll != nullptr);

	delete base::take(_mapLoading);

	const auto token = _config.mapsToken.toUtf8();
	const auto center = DefaultCenter(_initialProvided);
	const auto bounds = DefaultBounds();
	const auto protocol = *kProtocolOverride
		? "'"_q + kProtocolOverride + "'"
		: "null";
	const auto params = "token: '" + token + "'"
		+ ", center: " + center
		+ ", bounds: " + bounds
		+ ", protocol: " + protocol;
	_webview->eval("LocationPicker.init({ " + params + " });");

	const auto handle = _window->window()->windowHandle();
	if (handle && QGuiApplication::focusWindow() == handle) {
		_webview->focus();
	}
	_mapButton->show();
}

bool LocationPicker::venuesFromCache(
		Core::GeoLocation location,
		QString query) {
	const auto normalized = NormalizeVenuesQuery(query);
	auto &cache = _venuesCache[normalized];
	const auto i = ranges::find_if(cache, [&](const VenuesCacheEntry &v) {
		return AreTheSame(v.location, location);
	});
	if (i == end(cache)) {
		return false;
	}
	_venuesRequestLocation = location;
	_venuesRequestQuery = normalized;
	_venuesInitialQuery = query;
	venuesApplyResults(i->result);
	return true;
}

void LocationPicker::venuesRequest(
		Core::GeoLocation location,
		QString query) {
	const auto normalized = NormalizeVenuesQuery(query);
	if (AreTheSame(_venuesRequestLocation, location)
		&& _venuesRequestQuery == normalized) {
		return;
	} else if (const auto oldRequestId = base::take(_venuesRequestId)) {
		_api.request(oldRequestId).cancel();
	}
	_venueState = PickerVenueLoading();
	_venuesRequestLocation = location;
	_venuesRequestQuery = normalized;
	_venuesInitialQuery = query;
	if (_venuesBot) {
		venuesSendRequest();
	} else if (_venuesBotRequestId) {
		return;
	}
	const auto username = _session->serverConfig().venueSearchUsername;
	_venuesBotRequestId = _api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
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
		(_venueRecipient ? _venueRecipient->input : MTP_inputPeerEmpty()),
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
		venuesApplyResults(std::move(parsed));
	}).fail([=] {
		venuesApplyResults({});
	}).send();
}

void LocationPicker::venuesApplyResults(PickerVenueList venues) {
	_venuesRequestId = 0;
	if (venues.list.empty()) {
		_venueState = PickerVenueNothingFound{ _venuesInitialQuery };
	} else {
		_venueState = std::move(venues);
	}
}

void LocationPicker::venuesSearchEnableAt(Core::GeoLocation location) {
	if (!_venuesSearchLocation) {
		_window->setSearchAllowed(
			tr::lng_dlg_filter(),
			[=](std::optional<QString> query) {
				venuesSearchChanged(query);
			});
	}
	_venuesSearchLocation = location;
}

void LocationPicker::venuesSearchChanged(
		const std::optional<QString> &query) {
	_venuesSearchQuery = query;

	const auto shown = query && !query->trimmed().isEmpty();
	_venuesSearchShown = shown;
	if (_container->isHidden() != shown) {
		_container->setVisible(!shown);
		_mapControlsWrap->toggle(!shown, anim::type::instant);
		if (shown) {
			_venuesNoSearchLocation = _venuesRequestLocation;
		} else if (_venuesNoSearchLocation) {
			if (!venuesFromCache(_venuesNoSearchLocation)) {
				venuesRequest(_venuesNoSearchLocation);
			}
		}
	}

	if (shown
		&& !venuesFromCache(
			*_venuesSearchLocation,
			*_venuesSearchQuery)) {
		_venueState = PickerVenueLoading();
		_venuesSearchDebounceTimer.callOnce(kSearchDebounceDelay);
	} else {
		_venuesSearchDebounceTimer.cancel();
	}
}

void LocationPicker::resolveCurrentLocation() {
	using namespace Core;
	const auto window = _window.get();
	ResolveCurrentGeoLocation(crl::guard(window, [=](GeoLocation location) {
		const auto changed = !AreTheSame(LastExactLocation, location);
		if (location.accuracy != GeoLocationAccuracy::Exact || !changed) {
			if (!_venuesSearchLocation) {
				_venueState = PickerVenueWaitingForLocation();
			}
			return;
		}
		LastExactLocation = location;
		if (location) {
			if (_venuesSearchQuery.value_or(QString()).isEmpty()) {
				venuesRequest(location);
			}
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
	if (key == u"escape"_q) {
		if (!_window->closeSearch()) {
			close();
		}
	} else if (key == u"w"_q && modifier == ctrl) {
		close();
	} else if (key == u"m"_q && modifier == ctrl) {
		minimize();
	} else if (key == u"q"_q && modifier == ctrl) {
		quit();
	}
}

void LocationPicker::activate() {
	if (_window) {
		_window->activateWindow();
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
