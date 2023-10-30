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
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "webview/webview_embed.h"
#include "webview/webview_dialog.h"
#include "webview/webview_interface.h"
#include "base/debug_log.h"
#include "base/invoke_queued.h"
#include "styles/style_payments.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Ui::BotWebView {
namespace {

constexpr auto kProcessClickTimeout = crl::time(1000);
constexpr auto kProgressDuration = crl::time(200);
constexpr auto kProgressOpacity = 0.3;
constexpr auto kLightnessThreshold = 128;
constexpr auto kLightnessDelta = 32;

[[nodiscard]] QJsonObject ParseMethodArgs(const QString &json) {
	if (json.isEmpty()) {
		return {};
	}
	auto error = QJsonParseError();
	const auto dictionary = QJsonDocument::fromJson(json.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("BotWebView Error: Could not parse \"%1\".").arg(json));
		return {};
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
	if (_progress->shown
		&& Ui::AppInFocus()
		&& Ui::InFocusChain(_progress->widget.window())) {
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
	_text.drawLeftElided(p, textLeft, textTop, textWidth, width());
}

QImage Panel::Button::prepareRippleMask() const {
	return RippleAnimation::MaskByDrawer(size(), false, [&](QPainter &p) {
		p.drawRoundedRect(
			rect().marginsAdded({ 0, st::callRadius * 2, 0, 0 }),
			st::callRadius,
			st::callRadius);
	});
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
	not_null<Delegate*> delegate,
	MenuButtons menuButtons,
	bool allowClipboardRead)
: _userDataPath(userDataPath)
, _delegate(delegate)
, _menuButtons(menuButtons)
, _widget(std::make_unique<SeparatePanel>())
, _allowClipboardRead(allowClipboardRead) {
	_widget->setInnerSize(st::botWebViewPanelSize);
	_widget->setWindowFlag(Qt::WindowStaysOnTopHint, false);

	_widget->closeRequests(
	) | rpl::start_with_next([=] {
		if (_closeNeedConfirmation) {
			scheduleCloseWithConfirmation();
		} else {
			_delegate->botClose();
		}
	}, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::filter([=] {
		return !_hiddenForPayment;
	}) | rpl::start_with_next([=] {
		_delegate->botClose();
	}, _widget->lifetime());

	_widget->backRequests(
	) | rpl::start_with_next([=] {
		postEvent("back_button_pressed");
	}, _widget->lifetime());

	rpl::merge(
		style::PaletteChanged(),
		_themeUpdateForced.events()
	) | rpl::filter([=] {
		return !_themeUpdateScheduled;
	}) | rpl::start_with_next([=] {
		_themeUpdateScheduled = true;
		crl::on_main(_widget.get(), [=] {
			_themeUpdateScheduled = false;
			updateThemeParams(_delegate->botThemeParams());
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
		const Webview::ThemeParams &params,
		rpl::producer<QString> bottomText) {
	if (!_webview && !createWebview(params)) {
		return false;
	}
	const auto allowBack = false;
	showWebviewProgress();
	_widget->hideLayer(anim::type::instant);
	updateThemeParams(params);
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
	_widget->setMenuAllowed([=](const Ui::Menu::MenuCallback &callback) {
		if (_hasSettingsButton) {
			callback(tr::lng_bot_settings(tr::now), [=] {
				postEvent("settings_button_pressed");
			}, &st::menuIconSettings);
		}
		if (_menuButtons & MenuButton::OpenBot) {
			callback(tr::lng_bot_open(tr::now), [=] {
				_delegate->botHandleMenuButton(MenuButton::OpenBot);
			}, &st::menuIconLeave);
		}
		callback(tr::lng_bot_reload_page(tr::now), [=] {
			_webview->window.reload();
		}, &st::menuIconRestore);
		const auto main = (_menuButtons & MenuButton::RemoveFromMainMenu);
		if (main || (_menuButtons & MenuButton::RemoveFromMenu)) {
			const auto handler = [=] {
				_delegate->botHandleMenuButton(main
					? MenuButton::RemoveFromMainMenu
					: MenuButton::RemoveFromMenu);
			};
			callback({
				.text = (main
					? tr::lng_bot_remove_from_side_menu
					: tr::lng_bot_remove_from_menu)(tr::now),
				.handler = handler,
				.icon = &st::menuIconDeleteAttention,
				.isAttention = true,
			});
		}
	});
	return true;
}

bool Panel::createWebview(const Webview::ThemeParams &params) {
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
			.opaqueBg = params.opaqueBg,
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
		const auto arguments = ParseMethodArgs(list.at(1).toString());
		if (command == "web_app_close") {
			_delegate->botClose();
		} else if (command == "web_app_data_send") {
			sendDataMessage(arguments);
		} else if (command == "web_app_switch_inline_query") {
			switchInlineQueryMessage(arguments);
		} else if (command == "web_app_setup_main_button") {
			processMainButtonMessage(arguments);
		} else if (command == "web_app_setup_back_button") {
			processBackButtonMessage(arguments);
		} else if (command == "web_app_setup_settings_button") {
			processSettingsButtonMessage(arguments);
		} else if (command == "web_app_request_theme") {
			_themeUpdateForced.fire({});
		} else if (command == "web_app_request_viewport") {
			sendViewport();
		} else if (command == "web_app_open_tg_link") {
			openTgLink(arguments);
		} else if (command == "web_app_open_link") {
			openExternalLink(arguments);
		} else if (command == "web_app_open_invoice") {
			openInvoice(arguments);
		} else if (command == "web_app_open_popup") {
			openPopup(arguments);
		} else if (command == "web_app_request_write_access") {
			requestWriteAccess();
		} else if (command == "web_app_request_phone") {
			requestPhone();
		} else if (command == "web_app_invoke_custom_method") {
			invokeCustomMethod(arguments);
		} else if (command == "web_app_setup_closing_behavior") {
			setupClosingBehaviour(arguments);
		} else if (command == "web_app_read_text_from_clipboard") {
			requestClipboardText(arguments);
		} else if (command == "web_app_set_header_color") {
			processHeaderColor(arguments);
		}
	});

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		if (_delegate->botHandleLocalUri(uri, false)) {
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

void Panel::sendViewport() {
	postEvent("viewport_changed", "{ "
		"height: window.innerHeight, "
		"is_state_stable: true, "
		"is_expanded: true }");
}

void Panel::setTitle(rpl::producer<QString> title) {
	_widget->setTitle(std::move(title));
}

void Panel::sendDataMessage(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}
	const auto data = args["data"].toString();
	if (data.isEmpty()) {
		LOG(("BotWebView Error: Bad 'data' in sendDataMessage."));
		_delegate->botClose();
		return;
	}
	_delegate->botSendData(data.toUtf8());
}

void Panel::switchInlineQueryMessage(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}
	const auto query = args["query"].toString();
	if (query.isEmpty()) {
		LOG(("BotWebView Error: Bad 'query' in switchInlineQueryMessage."));
		_delegate->botClose();
		return;
	}
	const auto valid = base::flat_set<QString>{
		u"users"_q,
		u"bots"_q,
		u"groups"_q,
		u"channels"_q,
	};
	auto types = std::vector<QString>();
	for (const auto &value : args["chat_types"].toArray()) {
		const auto type = value.toString();
		if (valid.contains(type)) {
			types.push_back(type);
		} else {
			LOG(("BotWebView Error: "
				"Bad chat type in switchInlineQueryMessage: %1.").arg(type));
			types.clear();
			break;
		}
	}
	_delegate->botSwitchInlineQuery(types, query);
}

void Panel::openTgLink(const QJsonObject &args) {
	if (args.isEmpty()) {
		LOG(("BotWebView Error: Bad arguments in 'web_app_open_tg_link'."));
		_delegate->botClose();
		return;
	}
	const auto path = args["path_full"].toString();
	if (path.isEmpty()) {
		LOG(("BotWebView Error: Bad 'path_full' in 'web_app_open_tg_link'."));
		_delegate->botClose();
		return;
	}
	_delegate->botHandleLocalUri("https://t.me" + path, true);
}

void Panel::openExternalLink(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}
	const auto url = args["url"].toString();
	const auto lower = url.toLower();
	if (url.isEmpty()
		|| (!lower.startsWith("http://") && !lower.startsWith("https://"))) {
		LOG(("BotWebView Error: Bad 'url' in openExternalLink."));
		_delegate->botClose();
		return;
	} else if (!allowOpenLink()) {
		return;
	}
	File::OpenUrl(url);
}

void Panel::openInvoice(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}
	const auto slug = args["slug"].toString();
	if (slug.isEmpty()) {
		LOG(("BotWebView Error: Bad 'slug' in openInvoice."));
		_delegate->botClose();
		return;
	}
	_delegate->botHandleInvoice(slug);
}

void Panel::openPopup(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}
	using Button = Webview::PopupArgs::Button;
	using Type = Button::Type;
	const auto message = args["message"].toString();
	const auto types = base::flat_map<QString, Button::Type>{
		{ "default", Type::Default },
		{ "ok", Type::Ok },
		{ "close", Type::Close },
		{ "cancel", Type::Cancel },
		{ "destructive", Type::Destructive },
	};
	auto buttons = std::vector<Webview::PopupArgs::Button>();
	for (const auto button : args["buttons"].toArray()) {
		const auto fields = button.toObject();
		const auto i = types.find(fields["type"].toString());
		if (i == end(types)) {
			LOG(("BotWebView Error: Bad 'type' in openPopup buttons."));
			_delegate->botClose();
			return;
		}
		buttons.push_back({
			.id = fields["id"].toString(),
			.text = fields["text"].toString(),
			.type = i->second,
		});
	}
	if (message.isEmpty()) {
		LOG(("BotWebView Error: Bad 'message' in openPopup."));
		_delegate->botClose();
		return;
	} else if (buttons.empty()) {
		LOG(("BotWebView Error: Bad 'buttons' in openPopup."));
		_delegate->botClose();
		return;
	}
	const auto widget = _webview->window.widget();
	const auto weak = base::make_weak(this);
	const auto result = Webview::ShowBlockingPopup({
		.parent = widget ? widget->window() : nullptr,
		.title = args["title"].toString(),
		.text = message,
		.buttons = std::move(buttons),
	});
	if (weak) {
		postEvent("popup_closed", result.id
			? QJsonObject{ { u"button_id"_q, *result.id } }
			: EventData());
	}
}

void Panel::requestWriteAccess() {
	if (_inBlockingRequest) {
		replyRequestWriteAccess(false);
		return;
	}
	_inBlockingRequest = true;
	const auto finish = [=](bool allowed) {
		_inBlockingRequest = false;
		replyRequestWriteAccess(allowed);
	};
	const auto weak = base::make_weak(this);
	_delegate->botCheckWriteAccess([=](bool allowed) {
		if (!weak) {
			return;
		} else if (allowed) {
			finish(true);
			return;
		}
		using Button = Webview::PopupArgs::Button;
		const auto widget = _webview->window.widget();
		const auto integration = &Ui::Integration::Instance();
		const auto result = Webview::ShowBlockingPopup({
			.parent = widget ? widget->window() : nullptr,
			.title = integration->phraseBotAllowWriteTitle(),
			.text = integration->phraseBotAllowWrite(),
			.buttons = {
				{
					.id = "allow",
					.text = integration->phraseBotAllowWriteConfirm(),
				},
				{ .id = "cancel", .type = Button::Type::Cancel },
			},
		});
		if (!weak) {
			return;
		} else if (result.id == "allow") {
			_delegate->botAllowWriteAccess(crl::guard(this, finish));
		} else {
			finish(false);
		}
	});
}

void Panel::replyRequestWriteAccess(bool allowed) {
	postEvent("write_access_requested", QJsonObject{
		{ u"status"_q, allowed ? u"allowed"_q : u"cancelled"_q }
	});
}

void Panel::requestPhone() {
	if (_inBlockingRequest) {
		replyRequestPhone(false);
		return;
	}
	_inBlockingRequest = true;
	const auto finish = [=](bool shared) {
		_inBlockingRequest = false;
		replyRequestPhone(shared);
	};
	using Button = Webview::PopupArgs::Button;
	const auto widget = _webview->window.widget();
	const auto weak = base::make_weak(this);
	const auto integration = &Ui::Integration::Instance();
	const auto result = Webview::ShowBlockingPopup({
		.parent = widget ? widget->window() : nullptr,
		.title = integration->phraseBotSharePhoneTitle(),
		.text = integration->phraseBotSharePhone(),
		.buttons = {
			{
				.id = "share",
				.text = integration->phraseBotSharePhoneConfirm(),
			},
			{ .id = "cancel", .type = Button::Type::Cancel },
		},
	});
	if (!weak) {
		return;
	} else if (result.id == "share") {
		_delegate->botSharePhone(crl::guard(this, finish));
	} else {
		finish(false);
	}
}

void Panel::replyRequestPhone(bool shared) {
	postEvent("phone_requested", QJsonObject{
		{ u"status"_q, shared ? u"sent"_q : u"cancelled"_q }
	});
}

void Panel::invokeCustomMethod(const QJsonObject &args) {
	const auto requestId = args["req_id"];
	if (requestId.isUndefined()) {
		return;
	}
	const auto finish = [=](QJsonObject response) {
		replyCustomMethod(requestId, std::move(response));
	};
	auto callback = crl::guard(this, [=](CustomMethodResult result) {
		if (result) {
			auto error = QJsonParseError();
			const auto parsed = QJsonDocument::fromJson(
				"{ \"result\": " + *result + '}',
				&error);
			if (error.error != QJsonParseError::NoError
				|| !parsed.isObject()
				|| parsed.object().size() != 1) {
				finish({ { u"error"_q, u"Could not parse response."_q } });
			} else {
				finish(parsed.object());
			}
		} else {
			finish({ { u"error"_q, result.error() } });
		}
	});
	const auto params = QJsonDocument(
		args["params"].toObject()
	).toJson(QJsonDocument::Compact);
	_delegate->botInvokeCustomMethod({
		.method = args["method"].toString(),
		.params = params,
		.callback = std::move(callback),
	});
}

void Panel::replyCustomMethod(QJsonValue requestId, QJsonObject response) {
	response["req_id"] = requestId;
	postEvent(u"custom_method_invoked"_q, response);
}

void Panel::requestClipboardText(const QJsonObject &args) {
	const auto requestId = args["req_id"];
	if (requestId.isUndefined()) {
		return;
	}
	auto result = QJsonObject();
	result["req_id"] = requestId;
	if (allowClipboardQuery()) {
		result["data"] = QGuiApplication::clipboard()->text();
	}
	postEvent(u"clipboard_text_received"_q, result);
}

bool Panel::allowOpenLink() const {
	const auto now = crl::now();
	if (_mainButtonLastClick
		&& _mainButtonLastClick + kProcessClickTimeout >= now) {
		_mainButtonLastClick = 0;
		return true;
	}
	return true;
}

bool Panel::allowClipboardQuery() const {
	if (!_allowClipboardRead) {
		return false;
	}
	const auto now = crl::now();
	if (_mainButtonLastClick
		&& _mainButtonLastClick + kProcessClickTimeout >= now) {
		_mainButtonLastClick = 0;
		return true;
	}
	return true;
}

void Panel::scheduleCloseWithConfirmation() {
	if (!_closeWithConfirmationScheduled) {
		_closeWithConfirmationScheduled = true;
		InvokeQueued(_widget.get(), [=] { closeWithConfirmation(); });
	}
}

void Panel::closeWithConfirmation() {
	using Button = Webview::PopupArgs::Button;
	const auto widget = _webview->window.widget();
	const auto weak = base::make_weak(this);
	const auto integration = &Ui::Integration::Instance();
	const auto result = Webview::ShowBlockingPopup({
		.parent = widget ? widget->window() : nullptr,
		.title = integration->phrasePanelCloseWarning(),
		.text = integration->phrasePanelCloseUnsaved(),
		.buttons = {
			{
				.id = "close",
				.text = integration->phrasePanelCloseAnyway(),
				.type = Button::Type::Destructive,
			},
			{ .id = "cancel", .type = Button::Type::Cancel },
		},
		.ignoreFloodCheck = true,
	});
	if (!weak) {
		return;
	} else if (result.id != "cancel") {
		_delegate->botClose();
	} else {
		_closeWithConfirmationScheduled = false;
	}
}

void Panel::setupClosingBehaviour(const QJsonObject &args) {
	_closeNeedConfirmation = args["need_confirmation"].toBool();
}

void Panel::processMainButtonMessage(const QJsonObject &args) {
	if (args.isEmpty()) {
		_delegate->botClose();
		return;
	}

	const auto shown = [&] {
		return _mainButton && !_mainButton->isHidden();
	};
	const auto wasShown = shown();
	const auto guard = gsl::finally([&] {
		if (shown() != wasShown) {
			crl::on_main(this, [=] {
				sendViewport();
			});
		}
	});

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

void Panel::processBackButtonMessage(const QJsonObject &args) {
	_widget->setBackAllowed(args["is_visible"].toBool());
}

void Panel::processSettingsButtonMessage(const QJsonObject &args) {
	_hasSettingsButton = args["is_visible"].toBool();
}

void Panel::processHeaderColor(const QJsonObject &args) {
	if (const auto color = ParseColor(args["color"].toString())) {
		_widget->overrideTitleColor(color);
		_headerColorLifetime.destroy();
	} else if (args["color_key"].toString() == u"secondary_bg_color"_q) {
		_widget->overrideTitleColor(st::boxDividerBg->c);
		_headerColorLifetime = style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_widget->overrideTitleColor(st::boxDividerBg->c);
		});
	} else {
		_widget->overrideTitleColor(std::nullopt);
		_headerColorLifetime.destroy();
	}
}

void Panel::createMainButton() {
	_mainButton = std::make_unique<Button>(
		_widget.get(),
		st::botWebViewBottomButton);
	const auto button = _mainButton.get();

	button->setClickedCallback([=] {
		if (!button->isDisabled()) {
			postEvent("main_button_pressed");
			_mainButtonLastClick = crl::now();
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

void Panel::showToast(TextWithEntities &&text) {
	_widget->showToast(std::move(text));
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

void Panel::updateThemeParams(const Webview::ThemeParams &params) {
	if (!_webview || !_webview->window.widget()) {
		return;
	}
	_webview->window.updateTheme(
		params.opaqueBg,
		params.scrollBg,
		params.scrollBgOver,
		params.scrollBarBg,
		params.scrollBarBgOver);
	postEvent("theme_changed", "{\"theme_params\": " + params.json + "}");
}

void Panel::invoiceClosed(const QString &slug, const QString &status) {
	if (!_webview || !_webview->window.widget()) {
		return;
	}
	postEvent("invoice_closed", QJsonObject{
		{ u"slug"_q, slug },
		{ u"status"_q, status },
	});
	_widget->showAndActivate();
	_hiddenForPayment = false;
}

void Panel::hideForPayment() {
	_hiddenForPayment = true;
	_widget->hideGetDuration();
}

void Panel::postEvent(const QString &event) {
	postEvent(event, {});
}

void Panel::postEvent(const QString &event, EventData data) {
	auto written = v::is<QString>(data)
		? v::get<QString>(data).toUtf8()
		: QJsonDocument(
			v::get<QJsonObject>(data)).toJson(QJsonDocument::Compact);
	_webview->window.eval(R"(
if (window.TelegramGameProxy) {
	window.TelegramGameProxy.receiveEvent(
		")"
		+ event.toUtf8()
		+ '"' + (written.isEmpty() ? QByteArray() : ", " + written)
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
		rich.append(tr::lng_payments_webview_install_edge(
			tr::now,
			lt_link,
			Text::Link(
				"Microsoft Edge WebView2 Runtime",
				"https://go.microsoft.com/fwlink/p/?LinkId=2124703"),
			Ui::Text::WithEntities));
	} break;
	case Error::NoWebKitGTK:
		rich.append(tr::lng_payments_webview_install_webkit(tr::now));
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
		args.delegate,
		args.menuButtons,
		args.allowClipboardRead);
	const auto params = args.delegate->botThemeParams();
	if (!result->showWebview(args.url, params, std::move(args.bottom))) {
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
