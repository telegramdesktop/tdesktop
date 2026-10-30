/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/location_picker.h"

#include "base/platform/base_platform_info.h"
#include "core/current_geo_location.h"
#include "core/file_utilities.h"
#include "data/data_location.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/webview_helpers.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "core/cached_webview_availability.h"
#include "window/themes/window_theme.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_location_picker.h"
#include "styles/style_payments.h" // paymentsCriticalError
#include "styles/style_settings.h" // settingsCloudPasswordIconSize

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

namespace Ui {
namespace {

constexpr auto kResolveAddressDelay = 3 * crl::time(1000);

Core::GeoLocation LastExactLocation;

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
	};
	return Ui::ComputeStyles(map, 100, Window::Theme::IsNightMode());
}

[[nodiscard]] QByteArray ReadResource(const QString &name) {
	auto file = QFile(u":/picker/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QSize PickerInnerSize() {
	const auto body = st::pickLocationMapHeight
		+ st::pickLocationButtonSkip
		+ st::pickLocationButton.height
		+ st::pickLocationButtonSkip;
	return QSize(
		st::pickLocationWidth,
		st::separatePanelTitleHeight + body);
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

[[nodiscard]] not_null<AbstractButton*> MakeChooseLocationButton(
		not_null<RpWidget*> parent,
		rpl::producer<QString> label,
		rpl::producer<QString> address) {
	const auto raw = CreateChild<FlatButton>(
		parent.get(),
		QString(),
		st::pickLocationButton);

	const auto st = &st::pickLocationVenueItem;
	const auto icon = CreateChild<RpWidget>(raw);
	icon->setGeometry(
		st->photoPosition.x(),
		st->photoPosition.y(),
		st->photoSize,
		st->photoSize);
	icon->paintRequest() | rpl::on_next([=] {
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
		raw->widthValue(),
		std::move(statusText)
	) | rpl::on_next([=](int width, const QString &statusText) {
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

	return raw;
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

not_null<RpWidget*> SetupMapPlaceholder(
		not_null<RpWidget*> parent,
		Fn<void()> choose) {
	const auto result = CreateChild<RpWidget>(parent);

	const auto top = CreateChild<BoxContentDivider>(result);

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
	) | rpl::on_next([=] {
		auto p = QPainter(icon);
		lottie->paint(p, (icon->width() - iconSize) / 2, 0);
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
	button->setClickedCallback(choose);

	parent->sizeValue() | rpl::on_next([=](QSize size) {
		result->setGeometry(QRect(QPoint(), size));

		const auto width = size.width();
		top->setGeometry(0, 0, width, top->height());

		const auto available = size.height() - top->height();
		const auto delta = (available - iconSize - button->height()) / 2;
		button->move(
			(width - button->width()) / 2,
			size.height() - delta - button->height());
		const auto wide = available - delta - button->height();
		const auto skip = (wide - iconSize) / 2;
		icon->setGeometry(0, top->height() + skip, width, iconSize);
	}, result->lifetime());

	top->show();
	icon->show();
	result->show();

	return result;
}

} // namespace

LocationPicker::LocationPicker(Descriptor &&descriptor)
: _config(std::move(descriptor.config))
, _callback(std::move(descriptor.callback))
, _quit(std::move(descriptor.quit))
, _window(std::make_unique<SeparatePanel>())
, _body((_window->setInnerSize(PickerInnerSize())
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
, _geocoderResolveTimer([=] { resolveAddressByTimer(); }) {
	std::move(
		descriptor.closeRequests
	) | rpl::on_next([=] {
		// Close the webview before the panel: destroying the widget tree
		// calls DestroyWindow() on the HWND the WebView2 controller is
		// still attached to, which re-enters the window procedure.
		base::take(_webview);
		_window = nullptr;
		delete this;
	}, _lifetime);

	setup(descriptor);
}

bool LocationPicker::Available(const LocationPickerConfig &config) {
#ifdef _DEBUG
	return true;
#endif
	const auto &availability = Core::CachedWebviewAvailability();
	return availability.customSchemeRequests
		&& availability.customReferer
		&& !config.mapsToken.isEmpty();
}

LocationPickerConfig ResolveMapsConfig(not_null<Main::Session*> session) {
	const auto &appConfig = session->appConfig();
	auto map = appConfig.get<base::flat_map<QString, QString>>(
		u"tdesktop_config_map"_q,
		base::flat_map<QString, QString>());
	return {
		.mapsToken = map[u"maps"_q],
		.geoToken = map[u"geo"_q],
	};
}

void LocationPicker::setup(const Descriptor &descriptor) {
	setupWindow(descriptor);

	_initialProvided = descriptor.initial;
	if (!_initialProvided) {
		resolveCurrentLocation();
	}
}

void LocationPicker::setupWindow(const Descriptor &descriptor) {
	const auto window = _window.get();

	window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	window->closeRequests() | rpl::on_next([=] {
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
	_body->widthValue() | rpl::on_next([=](int width) {
		_container->setGeometry(0, 0, width, st::pickLocationMapHeight);
	}, _container->lifetime());

	_container->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_mapPlaceholder = SetupMapPlaceholder(_body, [=] {
		setupWebview();
	});

	_container->show();
	window->show();
}

void LocationPicker::setupWebview() {
	Expects(!_webview);

	delete base::take(_mapPlaceholder);

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.storageId = _webviewStorageId,
			.dataRequestRedirectHost = u"api.mapbox.com"_q,
			.safe = true,
		});
	const auto raw = _webview.get();
	if (!raw->widget()) {
		_webview = nullptr;
		showWebviewError();
		return;
	}

	window->lifetime().add([=] {
		_webview = nullptr;
	});

	const auto initial = _initialProvided.exact()
		? _initialProvided
		: LastExactLocation;
	if (initial) {
		resolveAddress(initial);
	}

	_mapButton = MakeChooseLocationButton(
		_body,
		_chooseButtonLabel.value(),
		_geocoderAddress.value());
	_body->widthValue() | rpl::on_next([=](int width) {
		_mapButton->setGeometry(
			0,
			st::pickLocationMapHeight + st::pickLocationButtonSkip,
			width,
			st::pickLocationButton.height);
	}, _mapButton->lifetime());
	_mapButton->setClickedCallback([=] {
		_webview->eval("LocationPicker.send();");
	});
	_mapButton->hide();

	_mapLoading = CreateChild<RpWidget>(_body.get());

	_container->geometryValue() | rpl::on_next([=](QRect rect) {
		_mapLoading->setGeometry(rect);
	}, _mapLoading->lifetime());

	SetupLoadingView(_mapLoading);
	_mapLoading->show();

	window->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
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
	) | rpl::on_next([=](QSize size) {
		raw->widget()->setGeometry(QRect(QPoint(), size));
	}, _container->lifetime());

	raw->setNavigationPolicyHandler([=](const QString &uri, bool newWindow) {
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
				const auto point = Data::LocationPoint::FromCoordinates(
					lat,
					lon);
				// Another send can be handled before the queued close() runs.
				const auto onstack = base::take(_callback);
				if (onstack && point) {
					auto address = _geocoderFull;
					address.name = _geocoderAddress.current();
					onstack(*point, std::move(address));
				}
				close();
			} else if (event == u"move_start"_q) {
				if (const auto now = _geocoderAddress.current()
					; !now.isEmpty()) {
					_geocoderSavedAddress = now;
					_geocoderSavedFull = _geocoderFull;
					_geocoderAddress = QString();
					_geocoderFull = {};
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
					_geocoderFull = base::take(_geocoderSavedFull);
					_geocoderResolveTimer.cancel();
				} else {
					_geocoderResolvePostponed = location;
					_geocoderResolveTimer.callOnce(kResolveAddressDelay);
				}
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

			style::PaletteChanged(
			) | rpl::on_next([=] {
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

void LocationPicker::showWebviewError() {
	delete base::take(_mapPlaceholder);

	const auto wrap = CreateChild<RpWidget>(_container);

	const auto available = Webview::Availability();
	auto text = (available.error != Webview::Available::Error::None)
		? BotWebView::ErrorText(available)
		: TextWithEntities{ u"Error: Could not initialize WebView."_q };

	const auto error = CreateChild<PaddingWrap<FlatLabel>>(
		wrap,
		object_ptr<FlatLabel>(
			wrap,
			rpl::single(std::move(text)),
			st::paymentsCriticalError),
		st::paymentsCriticalErrorPadding);
	error->entity()->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		const auto entity = handler->getTextEntity();
		if (entity.type != EntityType::CustomUrl) {
			return true;
		}
		File::OpenUrl(entity.data);
		return false;
	});
	wrap->show();

	wrap->widthValue() | rpl::on_next([=](int width) {
		error->resizeToWidth(width);
		wrap->resize(width, error->height());
	}, wrap->lifetime());

	_container->sizeValue() | rpl::on_next([=](QSize size) {
		wrap->setGeometry(0, 0, size.width(), size.height());
	}, wrap->lifetime());
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
		}
		_geocoderFull = address;
		if (address) {
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
	Expects(_mapButton != nullptr);

	delete base::take(_mapLoading);

	const auto token = _config.mapsToken.toUtf8();
	const auto center = DefaultCenter(_initialProvided);
	const auto bounds = DefaultBounds();
	const auto params = "token: '" + token + "'"
		+ ", center: " + center
		+ ", bounds: " + bounds;
	_webview->eval("LocationPicker.init({ " + params + " });");

	const auto handle = _window->window()->windowHandle();
	if (handle && QGuiApplication::focusWindow() == handle) {
		_webview->focus();
	}
	_mapButton->show();
}

void LocationPicker::resolveCurrentLocation() {
	using namespace Core;
	const auto window = _window.get();
	ResolveCurrentGeoLocation(crl::guard(window, [=](GeoLocation location) {
		const auto changed = !AreTheSame(LastExactLocation, location);
		if (location.accuracy != GeoLocationAccuracy::Exact || !changed) {
			return;
		}
		LastExactLocation = location;
		if (!_webview) {
			return;
		}
		resolveAddress(location);
		const auto point = QByteArray::number(location.point.x())
			+ ","_q
			+ QByteArray::number(location.point.y());
		_webview->eval("LocationPicker.narrowTo([" + point + "]);");
	}));
}

void LocationPicker::processKey(
		const QString &key,
		const QString &modifier) {
	const auto ctrl = ::Platform::IsMac() ? u"cmd"_q : u"ctrl"_q;
	if (key == u"escape"_q) {
		close();
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
		// Close the webview before the panel: destroying the widget tree
		// calls DestroyWindow() on the HWND the WebView2 controller is
		// still attached to, which re-enters the window procedure.
		base::take(_webview);
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
