/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_bot_webview.h"

#include "core/file_utilities.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/box_content.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "base/debug_log.h"
#include "styles/style_payments.h"
#include "styles/style_layers.h"

#include "base/timer_rpl.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Ui::BotWebView {
namespace {

constexpr auto kProgressDuration = crl::time(200);
constexpr auto kProgressOpacity = 0.3;
constexpr auto kLightnessThreshold = 128;
constexpr auto kLightnessDelta = 32;

[[nodiscard]] QJsonObject ParseMethodArgs(const QString &json) {
	auto error = QJsonParseError();
	const auto dictionary = QJsonDocument::fromJson(json.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("BotWebView Error: Could not parse \"%1\".").arg(json));
		return QJsonObject();
	}
	return dictionary.object();
}

[[nodiscard]] std::optional<QColor> ParseColor(const QString &text) {
	if (!text.startsWith('#') || text.size() != 7) {
		return {};
	}
	const auto data = text.data() + 1;
	const auto hex = [&](int from) -> std::optional<int> {
		const auto parse = [](QChar ch) -> std::optional<int> {
			const auto code = ch.unicode();
			return (code >= 'a' && code <= 'f')
				? std::make_optional(10 + (code - 'a'))
				: (code >= 'A' && code <= 'F')
				? std::make_optional(10 + (code - 'A'))
				: (code >= '0' && code <= '9')
				? std::make_optional(code - '0')
				: std::nullopt;
		};
		const auto h = parse(data[from]), l = parse(data[from + 1]);
		return (h && l) ? std::make_optional(*h * 16 + *l) : std::nullopt;
	};
	const auto r = hex(0), g = hex(2), b = hex(4);
	return (r && g && b) ? QColor(*r, *g, *b) : std::optional<QColor>();
}

[[nodiscard]] QColor ResolveRipple(QColor background) {
	auto hue = 0;
	auto saturation = 0;
	auto lightness = 0;
	auto alpha = 0;
	background.getHsv(&hue, &saturation, &lightness, &alpha);
	return QColor::fromHsv(
		hue,
		saturation,
		lightness - (lightness > kLightnessThreshold
			? kLightnessDelta
			: -kLightnessDelta),
		alpha);
}

} // namespace

class Panel::Button final : public RippleButton {
public:
	Button(QWidget *parent, const style::RoundButton &st);
	~Button();

	void updateBg(QColor bg);
	void updateFg(QColor fg);
	void updateArgs(MainButtonArgs &&args);

private:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

	void toggleProgress(bool shown);
	void setupProgressGeometry();

	std::unique_ptr<Progress> _progress;
	rpl::variable<QString> _textFull;
	Ui::Text::String _text;

	const style::RoundButton &_st;
	QColor _fg;
	style::owned_color _bg;
	RoundRect _roundRect;

};

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

Panel::Button::Button(QWidget *parent, const style::RoundButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _bg(st::windowBgActive->c)
, _roundRect(st::callRadius, st::windowBgActive) {
	_textFull.value(
	) | rpl::start_with_next([=](const QString &text) {
		_text.setText(st::semiboldTextStyle, text);
		update();
	}, lifetime());

	resize(
		_st.padding.left() + _text.maxWidth() + _st.padding.right(),
		_st.padding.top() + _st.height + _st.padding.bottom());
}

Panel::Button::~Button() = default;

void Panel::Button::updateBg(QColor bg) {
	_bg.update(bg);
	_roundRect.setColor(_bg.color());
	update();
}

void Panel::Button::updateFg(QColor fg) {
	_fg = fg;
	update();
}

void Panel::Button::updateArgs(MainButtonArgs &&args) {
	_textFull = std::move(args.text);
	setDisabled(!args.isActive);
	setVisible(args.isVisible);
	toggleProgress(args.isProgressVisible);
	update();
}

void Panel::Button::toggleProgress(bool shown) {
	if (!_progress) {
		if (!shown) {
			return;
		}
		_progress = std::make_unique<Progress>(
			this,
			[=] { return _progress->widget.rect(); });
		_progress->widget.paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
			auto p = QPainter(&_progress->widget);
			p.setOpacity(
				_progress->shownAnimation.value(_progress->shown ? 1. : 0.));
			auto thickness = st::paymentsLoading.thickness;
			const auto rect = _progress->widget.rect().marginsRemoved(
				{ thickness, thickness, thickness, thickness });
			InfiniteRadialAnimation::Draw(
				p,
				_progress->animation.computeState(),
				rect.topLeft(),
				rect.size() - QSize(),
				_progress->widget.width(),
				_fg,
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

void Panel::Button::setupProgressGeometry() {
	if (!_progress || !_progress->shown) {
		return;
	}
	_progress->geometryLifetime.destroy();
	sizeValue(
	) | rpl::start_with_next([=](QSize outer) {
		const auto height = outer.height();
		const auto size = st::paymentsLoading.size;
		const auto skip = (height - size.height()) / 2;
		const auto right = outer.width();
		const auto top = outer.height() - height;
		_progress->widget.setGeometry(QRect{
			QPoint(right - skip - size.width(), top + skip),
			size });
	}, _progress->geometryLifetime);

	_progress->widget.show();
	_progress->widget.raise();
	if (_progress->shown) {
		_progress->widget.setFocus();
	}
}

void Panel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_roundRect.paintSomeRounded(
		p,
		rect().marginsAdded({ 0, st::callRadius * 2, 0, 0 }),
		RectPart::BottomLeft | RectPart::BottomRight);

	if (!isDisabled()) {
		const auto ripple = ResolveRipple(_bg.color()->c);
		paintRipple(p, rect().topLeft(), &ripple);
	}

	p.setFont(_st.font);

	const auto height = rect().height();
	const auto progress = st::paymentsLoading.size;
	const auto skip = (height - progress.height()) / 2;
	const auto padding = skip + progress.width() + skip;

	const auto space = width() - padding * 2;
	const auto textWidth = std::min(space, _text.maxWidth());
	const auto textTop = _st.padding.top() + _st.textTop;
	const auto textLeft = padding + (space - textWidth) / 2;
	p.setPen(_fg);
	_text.drawLeftElided(p, textLeft, textTop, space, width());
}

QImage Panel::Button::prepareRippleMask() const {
	const auto drawMask = [&](QPainter &p) {
		p.drawRoundedRect(
			rect().marginsAdded({ 0, st::callRadius * 2, 0, 0 }),
			st::callRadius,
			st::callRadius);
	};
	return RippleAnimation::maskByDrawer(size(), false, drawMask);
}

QPoint Panel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos())
		- QPoint(_st.padding.left(), _st.padding.top());
}

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
	rpl::producer<QString> title,
	Fn<bool(QString)> handleLocalUri,
	Fn<void(QByteArray)> sendData,
	Fn<void()> close,
	Fn<QByteArray()> themeParams)
: _userDataPath(userDataPath)
, _handleLocalUri(std::move(handleLocalUri))
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

	setTitle(std::move(title));
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
	auto outer = base::make_unique_q<RpWidget>(_widget.get());
	const auto container = outer.get();
	_widget->showInner(std::move(outer));
	_webviewParent = container;

	_webviewBottom = std::make_unique<RpWidget>(_widget.get());
	const auto bottom = _webviewBottom.get();
	bottom->show();

	bottom->heightValue(
	) | rpl::start_with_next([=](int height) {
		const auto inner = _widget->innerGeometry();
		if (_mainButton && !_mainButton->isHidden()) {
			height = _mainButton->height();
		}
		bottom->move(inner.x(), inner.y() + inner.height() - height);
		container->resize(inner.width(), inner.height() - height);
		bottom->resizeToWidth(inner.width());
	}, bottom->lifetime());
	container->show();

	_webview = std::make_unique<WebviewWithLifetime>(
		container,
		Webview::WindowConfig{
			.userDataPath = _userDataPath,
		});
	const auto raw = &_webview->window;
	QObject::connect(container, &QObject::destroyed, [=] {
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
			_mainButton = nullptr;
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
		if (command == "web_app_close") {
			_close();
		} else if (command == "web_app_data_send") {
			sendDataMessage(list.at(1));
		} else if (command == "web_app_setup_main_button") {
			processMainButtonMessage(list.at(1));
		}
	});

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		if (_handleLocalUri(uri)) {
			return false;
		} else if (newWindow) {
			return true;
		}
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

	setupProgressGeometry();

	return true;
}

void Panel::setTitle(rpl::producer<QString> title) {
	_widget->setTitle(std::move(title));
}

void Panel::sendDataMessage(const QJsonValue &value) {
	const auto json = value.toString();
	const auto args = ParseMethodArgs(json);
	if (args.isEmpty()) {
		_close();
		return;
	}
	const auto data = args["data"].toString();
	if (data.isEmpty()) {
		LOG(("BotWebView Error: Bad data \"%1\".").arg(json));
		_close();
		return;
	}
	_sendData(data.toUtf8());
}

void Panel::processMainButtonMessage(const QJsonValue &value) {
	const auto json = value.toString();
	const auto args = ParseMethodArgs(json);
	if (args.isEmpty()) {
		_close();
		return;
	}

	if (!_mainButton) {
		if (args["is_visible"].toBool()) {
			createMainButton();
		} else {
			return;
		}
	}

	if (const auto bg = ParseColor(args["color"].toString())) {
		_mainButton->updateBg(*bg);
		_bgLifetime.destroy();
	} else {
		_mainButton->updateBg(st::windowBgActive->c);
		_bgLifetime = style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_mainButton->updateBg(st::windowBgActive->c);
		});
	}

	if (const auto fg = ParseColor(args["text_color"].toString())) {
		_mainButton->updateFg(*fg);
		_fgLifetime.destroy();
	} else {
		_mainButton->updateFg(st::windowFgActive->c);
		_fgLifetime = style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_mainButton->updateFg(st::windowFgActive->c);
		});
	}

	_mainButton->updateArgs({
		.isActive = args["is_active"].toBool(),
		.isVisible = args["is_visible"].toBool(),
		.isProgressVisible = args["is_progress_visible"].toBool(),
		.text = args["text"].toString(),
	});
}

void Panel::createMainButton() {
	_mainButton = std::make_unique<Button>(
		_widget.get(),
		st::botWebViewBottomButton);
	const auto button = _mainButton.get();

	button->setClickedCallback([=] {
		if (!button->isDisabled()) {
			postEvent("main_button_pressed");
		}
	});
	button->hide();

	rpl::combine(
		button->shownValue(),
		button->heightValue()
	) | rpl::start_with_next([=](bool shown, int height) {
		const auto inner = _widget->innerGeometry();
		if (!shown) {
			height = _webviewBottom->height();
		}
		button->move(inner.x(), inner.y() + inner.height() - height);
		if (const auto raw = _webviewParent.data()) {
			raw->resize(inner.width(), inner.height() - height);
		}
		button->resizeToWidth(inner.width());
		_webviewBottom->setVisible(!shown);
	}, button->lifetime());
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
	postEvent("theme_changed", "\"theme_params\": " + json);
}

void Panel::postEvent(const QString &event, const QString &data) {
	_webview->window.eval(R"(
if (window.TelegramGameProxy) {
	window.TelegramGameProxy.receiveEvent(
		")"
		+ event.toUtf8()
		+ '"' + (data.isEmpty() ? QByteArray() : ", {" + data.toUtf8() + '}')
		+ R"();
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
		std::move(args.title),
		std::move(args.handleLocalUri),
		std::move(args.sendData),
		std::move(args.close),
		std::move(args.themeParams));
	if (!result->showWebview(args.url, std::move(args.bottom))) {
		const auto available = Webview::Availability();
		if (available.error != Webview::Available::Error::None) {
			result->showWebviewError(
				tr::lng_bot_no_webview(tr::now),
				available);
		} else {
			result->showCriticalError({
				"Error: Could not initialize WebView." });
		}
	}
	return result;
}

} // namespace Ui::BotWebView
