/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_bot_webview.h"

#include "core/file_utilities.h"
#include "ui/effects/radial_animation.h"
#include "ui/layers/box_content.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "base/debug_log.h"
#include "styles/style_payments.h"
#include "styles/style_layers.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Ui::BotWebView {
namespace {

constexpr auto kProgressDuration = crl::time(200);
constexpr auto kProgressOpacity = 0.3;

} // namespace

struct Panel::Progress {
	Progress(QWidget *parent, Fn<QRect()> rect);

	RpWidget widget;
	InfiniteRadialAnimation animation;
	Animations::Simple shownAnimation;
	bool shown = true;
	rpl::lifetime geometryLifetime;
};

struct Panel::WebviewWithLifetime {
	WebviewWithLifetime(
		QWidget *parent = nullptr,
		Webview::WindowConfig config = Webview::WindowConfig());

	Webview::Window window;
	QPointer<RpWidget> lastHidingBox;
	rpl::lifetime lifetime;
};

Panel::WebviewWithLifetime::WebviewWithLifetime(
	QWidget *parent,
	Webview::WindowConfig config)
: window(parent, std::move(config)) {
}

Panel::Progress::Progress(QWidget *parent, Fn<QRect()> rect)
: widget(parent)
, animation(
	[=] { if (!anim::Disabled()) widget.update(rect()); },
	st::paymentsLoading) {
}

Panel::Panel(
	const QString &userDataPath,
	Fn<void(QByteArray)> sendData,
	Fn<void()> close,
	Fn<QByteArray()> themeParams)
: _userDataPath(userDataPath)
, _sendData(std::move(sendData))
, _close(std::move(close))
, _widget(std::make_unique<SeparatePanel>()) {
	_widget->setInnerSize(st::paymentsPanelSize);
	_widget->setWindowFlag(Qt::WindowStaysOnTopHint, false);

	_widget->closeRequests(
	) | rpl::start_with_next(_close, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::start_with_next(_close, _widget->lifetime());

	style::PaletteChanged(
	) | rpl::filter([=] {
		return !_themeUpdateScheduled;
	}) | rpl::start_with_next([=] {
		_themeUpdateScheduled = true;
		crl::on_main(_widget.get(), [=] {
			_themeUpdateScheduled = false;
			updateThemeParams(themeParams());
		});
	}, _widget->lifetime());

	setTitle(rpl::single(u"Title"_q));
}

Panel::~Panel() {
	_webview = nullptr;
	_progress = nullptr;
	_widget = nullptr;
}

void Panel::requestActivate() {
	_widget->showAndActivate();
}

void Panel::toggleProgress(bool shown) {
	if (!_progress) {
		if (!shown) {
			return;
		}
		_progress = std::make_unique<Progress>(
			_widget.get(),
			[=] { return progressRect(); });
		_progress->widget.paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
			auto p = QPainter(&_progress->widget);
			p.setOpacity(
				_progress->shownAnimation.value(_progress->shown ? 1. : 0.));
			auto thickness = st::paymentsLoading.thickness;
			if (progressWithBackground()) {
				auto color = st::windowBg->c;
				color.setAlphaF(kProgressOpacity);
				p.fillRect(clip, color);
			}
			const auto rect = progressRect().marginsRemoved(
				{ thickness, thickness, thickness, thickness });
			InfiniteRadialAnimation::Draw(
				p,
				_progress->animation.computeState(),
				rect.topLeft(),
				rect.size() - QSize(),
				_progress->widget.width(),
				st::paymentsLoading.color,
				thickness);
		}, _progress->widget.lifetime());
		_progress->widget.show();
		_progress->animation.start();
	} else if (_progress->shown == shown) {
		return;
	}
	const auto callback = [=] {
		if (!_progress->shownAnimation.animating() && !_progress->shown) {
			_progress = nullptr;
		} else {
			_progress->widget.update();
		}
	};
	_progress->shown = shown;
	_progress->shownAnimation.start(
		callback,
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kProgressDuration);
	if (shown) {
		setupProgressGeometry();
	}
}

bool Panel::progressWithBackground() const {
	return (_progress->widget.width() == _widget->innerGeometry().width());
}

QRect Panel::progressRect() const {
	const auto rect = _progress->widget.rect();
	if (!progressWithBackground()) {
		return rect;
	}
	const auto size = st::defaultBoxButton.height;
	return QRect(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - size) / 2,
		size,
		size);
}

void Panel::setupProgressGeometry() {
	if (!_progress || !_progress->shown) {
		return;
	}
	_progress->geometryLifetime.destroy();
	if (_webviewBottom) {
		_webviewBottom->geometryValue(
		) | rpl::start_with_next([=](QRect bottom) {
			const auto height = bottom.height();
			const auto size = st::paymentsLoading.size;
			const auto skip = (height - size.height()) / 2;
			const auto inner = _widget->innerGeometry();
			const auto right = inner.x() + inner.width();
			const auto top = inner.y() + inner.height() - height;
			// This doesn't work, because first we get the correct bottom
			// geometry and after that we get the previous event (which
			// triggered the 'fire' of correct geometry before getting here).
			//const auto right = bottom.x() + bottom.width();
			//const auto top = bottom.y();
			_progress->widget.setGeometry(QRect{
				QPoint(right - skip - size.width(), top + skip),
				size });
		}, _progress->geometryLifetime);
	}
	_progress->widget.show();
	_progress->widget.raise();
	if (_progress->shown) {
		_progress->widget.setFocus();
	}
}

void Panel::showWebviewProgress() {
	if (_webviewProgress && _progress && _progress->shown) {
		return;
	}
	_webviewProgress = true;
	toggleProgress(true);
}

void Panel::hideWebviewProgress() {
	if (!_webviewProgress) {
		return;
	}
	_webviewProgress = false;
	toggleProgress(false);
}

bool Panel::showWebview(
		const QString &url,
		rpl::producer<QString> bottomText) {
	if (!_webview && !createWebview()) {
		return false;
	}
	const auto allowBack = false;
	showWebviewProgress();
	_widget->destroyLayer();
	_webview->window.navigate(url);
	_widget->setBackAllowed(allowBack);
	if (bottomText) {
		const auto &padding = st::paymentsPanelPadding;
		const auto label = CreateChild<FlatLabel>(
			_webviewBottom.get(),
			std::move(bottomText),
			st::paymentsWebviewBottom);
		const auto height = padding.top()
			+ label->heightNoMargins()
			+ padding.bottom();
		rpl::combine(
			_webviewBottom->widthValue(),
			label->widthValue()
		) | rpl::start_with_next([=](int outerWidth, int width) {
			label->move((outerWidth - width) / 2, padding.top());
		}, label->lifetime());
		label->show();
		_webviewBottom->resize(_webviewBottom->width(), height);
	}
	return true;
}

bool Panel::createWebview() {
	auto container = base::make_unique_q<RpWidget>(_widget.get());

	_webviewBottom = std::make_unique<RpWidget>(_widget.get());
	const auto bottom = _webviewBottom.get();
	bottom->show();

	bottom->heightValue(
	) | rpl::start_with_next([=, raw = container.get()](int height) {
		const auto inner = _widget->innerGeometry();
		bottom->move(inner.x(), inner.y() + inner.height() - height);
		raw->resize(inner.width(), inner.height() - height);
		bottom->resizeToWidth(inner.width());
	}, bottom->lifetime());
	container->show();

	_webview = std::make_unique<WebviewWithLifetime>(
		container.get(),
		Webview::WindowConfig{
			.userDataPath = _userDataPath,
		});
	const auto raw = &_webview->window;
	QObject::connect(container.get(), &QObject::destroyed, [=] {
		if (_webview && &_webview->window == raw) {
			_webview = nullptr;
			if (_webviewProgress) {
				hideWebviewProgress();
				if (_progress && !_progress->shown) {
					_progress = nullptr;
				}
			}
		}
		if (_webviewBottom.get() == bottom) {
			_webviewBottom = nullptr;
		}
	});
	if (!raw->widget()) {
		return false;
	}

	container->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		raw->widget()->setGeometry(geometry);
	}, _webview->lifetime);

	raw->setMessageHandler([=](const QJsonDocument &message) {
		if (!message.isArray()) {
			LOG(("BotWebView Error: "
				"Not an array received in buy_callback arguments."));
			return;
		}
		const auto list = message.array();
		const auto command = list.at(0).toString();
		if (command == "webview_close") {
			_close();
		} else if (command == "webview_data_send") {
			//const auto tmp = list.at(1).toObject()["data"].toString().toUtf8();
			const auto send = [send = _sendData, message] {
				send(message.toJson(QJsonDocument::Compact));
			};
			_close();
			send();
		}
	});

	raw->setNavigationStartHandler([=](const QString &uri) {
		showWebviewProgress();
		return true;
	});
	raw->setNavigationDoneHandler([=](bool success) {
		hideWebviewProgress();
	});

	raw->init(R"(
window.TelegramWebviewProxy = {
postEvent: function(eventType, eventData) {
	if (window.external && window.external.invoke) {
		window.external.invoke(JSON.stringify([eventType, eventData]));
	}
}
};)");

	_widget->showInner(std::move(container));

	setupProgressGeometry();

	return true;
}

void Panel::setTitle(rpl::producer<QString> title) {
	_widget->setTitle(std::move(title));
}

void Panel::showBox(object_ptr<BoxContent> box) {
	if (const auto widget = _webview ? _webview->window.widget() : nullptr) {
		const auto hideNow = !widget->isHidden();
		if (hideNow || _webview->lastHidingBox) {
			const auto raw = _webview->lastHidingBox = box.data();
			box->boxClosing(
			) | rpl::start_with_next([=] {
				const auto widget = _webview
					? _webview->window.widget()
					: nullptr;
				if (widget
					&& widget->isHidden()
					&& _webview->lastHidingBox == raw) {
					widget->show();
				}
			}, _webview->lifetime);
			if (hideNow) {
				widget->hide();
			}
		}
	}
	_widget->showBox(
		std::move(box),
		LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(const TextWithEntities &text) {
	_widget->showToast(text);
}

void Panel::showCriticalError(const TextWithEntities &text) {
	_progress = nullptr;
	_webviewProgress = false;
	auto error = base::make_unique_q<PaddingWrap<FlatLabel>>(
		_widget.get(),
		object_ptr<FlatLabel>(
			_widget.get(),
			rpl::single(text),
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
	_widget->showInner(std::move(error));
}

void Panel::updateThemeParams(const QByteArray &json) {
	if (!_webview || !_webview->window.widget()) {
		return;
	}
	_webview->window.eval(R"(
if (window.TelegramGameProxy) {
	window.TelegramGameProxy.receiveEvent(
		"theme_changed",
		{ "theme_params": )" + json + R"( });
}
)");
}

void Panel::showWebviewError(
		const QString &text,
		const Webview::Available &information) {
	using Error = Webview::Available::Error;
	Expects(information.error != Error::None);

	auto rich = TextWithEntities{ text };
	rich.append("\n\n");
	switch (information.error) {
	case Error::NoWebview2: {
		const auto command = QString(QChar(TextCommand));
		const auto text = tr::lng_payments_webview_install_edge(
			tr::now,
			lt_link,
			command);
		const auto parts = text.split(command);
		rich.append(parts.value(0))
			.append(Text::Link(
				"Microsoft Edge WebView2 Runtime",
				"https://go.microsoft.com/fwlink/p/?LinkId=2124703"))
			.append(parts.value(1));
	} break;
	case Error::NoGtkOrWebkit2Gtk:
		rich.append(tr::lng_payments_webview_install_webkit(tr::now));
		break;
	case Error::MutterWM:
		rich.append(tr::lng_payments_webview_switch_mutter(tr::now));
		break;
	case Error::Wayland:
		rich.append(tr::lng_payments_webview_switch_wayland(tr::now));
		break;
	default:
		rich.append(QString::fromStdString(information.details));
		break;
	}
	showCriticalError(rich);
}

rpl::lifetime &Panel::lifetime() {
	return _widget->lifetime();
}

std::unique_ptr<Panel> Show(Args &&args) {
	auto result = std::make_unique<Panel>(
		args.userDataPath,
		std::move(args.sendData),
		std::move(args.close),
		std::move(args.themeParams));
	result->showWebview(args.url, rpl::single(u"smth"_q));
	return result;
}

} // namespace Ui::BotWebView
