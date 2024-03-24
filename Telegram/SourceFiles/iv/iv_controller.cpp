/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "base/platform/base_platform_info.h"
#include "base/invoke_queued.h"
#include "base/qt_signal_producer.h"
#include "base/qthelp_url.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/rp_window.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QFile>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <charconv>

#include "base/call_delayed.h"

namespace Iv {
namespace {

[[nodiscard]] QByteArray ComputeStyles() {
	static const auto map = base::flat_map<QByteArray, const style::color*>{
		{ "shadow-fg", &st::shadowFg },
		{ "scroll-bg", &st::scrollBg },
		{ "scroll-bg-over", &st::scrollBgOver },
		{ "scroll-bar-bg", &st::scrollBarBg },
		{ "scroll-bar-bg-over", &st::scrollBarBgOver },
		{ "window-bg", &st::windowBg },
		{ "window-bg-over", &st::windowBgOver },
		{ "window-bg-ripple", &st::windowBgRipple },
		{ "window-bg-active", &st::windowBgActive },
		{ "window-fg", &st::windowFg },
		{ "window-sub-text-fg", &st::windowSubTextFg },
		{ "window-active-text-fg", &st::windowActiveTextFg },
		{ "window-shadow-fg", &st::windowShadowFg },
		{ "box-divider-bg", &st::boxDividerBg },
		{ "box-divider-fg", &st::boxDividerFg },
		{ "light-button-fg", &st::lightButtonFg },
		{ "light-button-bg-over", &st::lightButtonBgOver },
		{ "menu-icon-fg", &st::menuIconFg },
		{ "menu-icon-fg-over", &st::menuIconFgOver },
		{ "menu-bg", &st::menuBg },
		{ "menu-bg-over", &st::menuBgOver },
		{ "history-to-down-fg", &st::historyToDownFg },
		{ "history-to-down-fg-over", &st::historyToDownFgOver },
		{ "history-to-down-bg", &st::historyToDownBg },
		{ "history-to-down-bg-over", &st::historyToDownBgOver },
		{ "history-to-down-bg-ripple", &st::historyToDownBgRipple },
		{ "history-to-down-shadow", &st::historyToDownShadow },
		{ "toast-bg", &st::toastBg },
		{ "toast-fg", &st::toastFg },
	};
	static const auto phrases = base::flat_map<QByteArray, tr::phrase<>>{
		{ "iv-join-channel", tr::lng_iv_join_channel },
	};
	static const auto serialize = [](const style::color *color) {
		const auto qt = (*color)->c;
		if (qt.alpha() == 255) {
			return '#'
				+ QByteArray::number(qt.red(), 16).right(2)
				+ QByteArray::number(qt.green(), 16).right(2)
				+ QByteArray::number(qt.blue(), 16).right(2);
		}
		return "rgba("
			+ QByteArray::number(qt.red()) + ","
			+ QByteArray::number(qt.green()) + ","
			+ QByteArray::number(qt.blue()) + ","
			+ QByteArray::number(qt.alpha() / 255.) + ")";
	};
	static const auto escape = [](tr::phrase<> phrase) {
		const auto text = phrase(tr::now);

		auto result = QByteArray();
		for (auto i = 0; i != text.size(); ++i) {
			uint ucs4 = text[i].unicode();
			if (QChar::isHighSurrogate(ucs4) && i + 1 != text.size()) {
				ushort low = text[i + 1].unicode();
				if (QChar::isLowSurrogate(low)) {
					ucs4 = QChar::surrogateToUcs4(ucs4, low);
					++i;
				}
			}
			if (ucs4 == '\'' || ucs4 == '\"' || ucs4 == '\\') {
				result.append('\\').append(char(ucs4));
			} else if (ucs4 < 32 || ucs4 > 127) {
				result.append('\\' + QByteArray::number(ucs4, 16) + ' ');
			} else {
				result.append(char(ucs4));
			}
		}
		return result;
	};
	auto result = QByteArray();
	for (const auto &[name, phrase] : phrases) {
		result += "--td-lng-" + name + ":'" + escape(phrase) + "'; ";
	}
	for (const auto &[name, color] : map) {
		result += "--td-" + name + ':' + serialize(color) + ';';
	}
	return result;
}

[[nodiscard]] QByteArray EscapeForAttribute(QByteArray value) {
	return value
		.replace('&', "&amp;")
		.replace('"', "&quot;")
		.replace('\'', "&#039;")
		.replace('<', "&lt;")
		.replace('>', "&gt;");
}

[[nodiscard]] QByteArray EscapeForScriptString(QByteArray value) {
	return value
		.replace('\\', "\\\\")
		.replace('"', "\\\"")
		.replace('\'', "\\\'");
}

[[nodiscard]] QByteArray WrapPage(const Prepared &page) {
#ifdef Q_OS_MAC
	const auto classAttribute = ""_q;
#else // Q_OS_MAC
	const auto classAttribute = " class=\"custom_scroll\""_q;
#endif // Q_OS_MAC

	const auto js = QByteArray()
		+ (page.hasCode ? "IV.initPreBlocks();" : "")
		+ (page.hasEmbeds ? "IV.initEmbedBlocks();" : "")
		+ "IV.init();"
		+ page.script;

	const auto contentAttributes = page.rtl
		? " dir=\"rtl\" class=\"rtl\""_q
		: QByteArray();

	return R"(<!DOCTYPE html>
<html)"_q
	+ classAttribute
	+ R"( style=")"
	+ EscapeForAttribute(ComputeStyles())
	+ R"(">
	<head>
		<meta charset="utf-8">
		<meta name="robots" content="noindex, nofollow">
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<script src="/iv/page.js"></script>
		<link rel="stylesheet" href="/iv/page.css" />
	</head>
	<body>
		<div id="top_shadow"></div>
		<button class="fixed_button hidden" id="bottom_up" onclick="IV.scrollTo(0);">
			<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
				<path d="M14.9972363,18 L9.13865768,12.1414214 C9.06055283,12.0633165 9.06055283,11.9366835 9.13865768,11.8585786 L14.9972363,6 L14.9972363,6" transform="translate(11.997236, 12.000000) scale(-1, -1) rotate(-90.000000) translate(-11.997236, -12.000000) "></path>
			</svg>
		</button>
		<div class="page-scroll" tabindex="-1"><div class="page-slide">
			<article)"_q + contentAttributes + ">"_q + page.content + R"(</article>
		</div></div>
		<script>)"_q + js + R"(</script>
	</body>
</html>
)"_q;
}

[[nodiscard]] QByteArray ReadResource(const QString &name) {
	auto file = QFile(u":/iv/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

Controller::Controller(
	not_null<Delegate*> delegate,
	Fn<ShareBoxResult(ShareBoxDescriptor)> showShareBox)
: _delegate(delegate)
, _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("IV.updateStyles('" + str + "');");
	}
})
, _showShareBox(std::move(showShareBox)) {
	createWindow();
}

Controller::~Controller() {
	destroyShareMenu();
	if (_window) {
		_window->hide();
	}
	_ready = false;
	_webview = nullptr;
	_back.destroy();
	_menu = nullptr;
	_menuToggle.destroy();
	_subtitle = nullptr;
	_subtitleWrap = nullptr;
	_window = nullptr;
}

void Controller::updateTitleGeometry(int newWidth) const {
	_subtitleWrap->setGeometry(
		0,
		st::windowTitleHeight,
		newWidth,
		st::ivSubtitleHeight);
	_subtitleWrap->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_subtitleWrap.get()).fillRect(clip, st::windowBg);
	}, _subtitleWrap->lifetime());

	const auto progress = _subtitleLeft.value(_back->toggled() ? 1. : 0.);
	const auto left = anim::interpolate(
		st::ivSubtitleLeft,
		_back->width() + st::ivSubtitleSkip,
		progress);
	_subtitle->resizeToWidth(newWidth - left - _menuToggle->width());
	_subtitle->moveToLeft(left, st::ivSubtitleTop);

	_back->moveToLeft(0, 0);
	_menuToggle->moveToRight(0, 0);
}

void Controller::initControls() {
	_subtitleWrap = std::make_unique<Ui::RpWidget>(_window.get());
	_subtitleText = _index.value() | rpl::filter(
		rpl::mappers::_1 >= 0
	) | rpl::map([=](int index) {
		return _pages[index].name;
	});
	_subtitle = std::make_unique<Ui::FlatLabel>(
		_subtitleWrap.get(),
		_subtitleText.value(),
		st::ivSubtitle);
	_subtitleText.value(
	) | rpl::start_with_next([=](const QString &subtitle) {
		const auto prefix = tr::lng_iv_window_title(tr::now);
		_window->setWindowTitle(prefix + ' ' + QChar(0x2014) + ' ' + subtitle);
	}, _subtitle->lifetime());
	_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);

	_menuToggle.create(_subtitleWrap.get(), st::ivMenuToggle);
	_menuToggle->setClickedCallback([=] { showMenu(); });

	_back.create(
		_subtitleWrap.get(),
		object_ptr<Ui::IconButton>(_subtitleWrap.get(), st::ivBack));
	_back->entity()->setClickedCallback([=] {
		if (_webview) {
			_webview->eval("IV.back();");
		} else {
			_back->hide(anim::type::normal);
		}
	});

	_back->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		_subtitleLeft.start(
			[=] { updateTitleGeometry(_window->width()); },
			toggled ? 0. : 1.,
			toggled ? 1. : 0.,
			st::fadeWrapDuration);
	}, _back->lifetime());
	_back->hide(anim::type::instant);

	_subtitleLeft.stop();
}

void Controller::show(
		const QString &dataPath,
		Prepared page,
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues) {
	page.script = fillInChannelValuesScript(std::move(inChannelValues));
	InvokeQueued(_container, [=, page = std::move(page)]() mutable {
		showInWindow(dataPath, std::move(page));
	});
}

void Controller::update(Prepared page) {
	const auto url = page.url;
	auto i = _indices.find(url);
	if (i == end(_indices)) {
		return;
	}
	const auto index = i->second;
	_pages[index] = std::move(page);

	if (_ready) {
		_webview->eval(reloadScript(index));
	} else if (!index) {
		_reloadInitialWhenReady = true;
	}
}

QByteArray Controller::fillInChannelValuesScript(
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues) {
	auto result = QByteArray();
	for (auto &[id, in] : inChannelValues) {
		if (_inChannelSubscribed.emplace(id).second) {
			std::move(in) | rpl::start_with_next([=](bool in) {
				if (_ready) {
					_webview->eval(toggleInChannelScript(id, in));
				} else {
					_inChannelChanged[id] = in;
				}
			}, _lifetime);
		}
	}
	for (const auto &[id, in] : base::take(_inChannelChanged)) {
		result += toggleInChannelScript(id, in);
	}
	return result;
}

QByteArray Controller::toggleInChannelScript(
		const QByteArray &id,
		bool in) const {
	const auto value = in ? "true" : "false";
	return "IV.toggleChannelJoined('" + id + "', " + value + ");";
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	base::qt_signal_producer(
		window->window()->windowHandle(),
		&QWindow::activeChanged
	) | rpl::filter([=] {
		return _webview && window->window()->windowHandle()->isActive();
	}) | rpl::start_with_next([=] {
		setInnerFocus();
	}, window->lifetime());

	initControls();

	window->widthValue() | rpl::start_with_next([=](int width) {
		updateTitleGeometry(width);
	}, _subtitle->lifetime());

	window->setGeometry(_delegate->ivGeometry());
	window->setMinimumSize({ st::windowMinWidth, st::windowMinHeight });

	window->geometryValue(
	) | rpl::distinct_until_changed(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		_delegate->ivSaveGeometry(window);
	}, window->lifetime());

	_container = Ui::CreateChild<Ui::RpWidget>(window->window());
	rpl::combine(
		window->sizeValue(),
		_subtitleWrap->heightValue()
	) | rpl::start_with_next([=](QSize size, int title) {
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ 0, title + st::windowTitleHeight, 0, 0 }));
	}, _container->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	window->show();
}

void Controller::createWebview(const QString &dataPath) {
	Expects(!_webview);

	const auto window = _window.get();
	_webview = std::make_unique<Webview::Window>(
		_container,
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.userDataPath = dataPath,
		});
	const auto raw = _webview.get();

	window->lifetime().add([=] {
		_ready = false;
		_webview = nullptr;
	});

	window->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			close();
		} else if (e->type() == QEvent::KeyPress) {
			const auto event = static_cast<QKeyEvent*>(e.get());
			if (event->key() == Qt::Key_Escape) {
				escape();
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
			if (event == u"keydown"_q) {
				const auto key = object.value("key").toString();
				const auto modifier = object.value("modifier").toString();
				processKey(key, modifier);
			} else if (event == u"mouseenter"_q) {
				window->overrideSystemButtonOver({});
			} else if (event == u"mouseup"_q) {
				window->overrideSystemButtonDown({});
			} else if (event == u"link_click"_q) {
				const auto url = object.value("url").toString();
				const auto context = object.value("context").toString();
				processLink(url, context);
			} else if (event == "menu_page_blocker_click") {
				if (_menu) {
					_menu->hideMenu();
				}
			} else if (event == u"ready"_q) {
				_ready = true;
				auto script = QByteArray();
				for (const auto &[id, in] : base::take(_inChannelChanged)) {
					script += toggleInChannelScript(id, in);
				}
				if (_navigateToIndexWhenReady >= 0) {
					script += navigateScript(
						std::exchange(_navigateToIndexWhenReady, -1),
						base::take(_navigateToHashWhenReady));
				}
				if (base::take(_reloadInitialWhenReady)) {
					script += reloadScript(0);
				}
				if (_menu) {
					script += "IV.menuShown(true);";
				}
				if (!script.isEmpty()) {
					_webview->eval(script);
				}
			} else if (event == u"location_change"_q) {
				_index = object.value("index").toInt();
				_hash = object.value("hash").toString();
				_back->toggle(
					(object.value("position").toInt() > 0),
					anim::type::normal);
			}
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
		const auto pos = request.id.find('#');
		if (pos != request.id.npos) {
			request.id = request.id.substr(0, pos);
		}
		if (!request.id.starts_with("iv/")) {
			_dataRequests.fire(std::move(request));
			return Webview::DataResult::Pending;
		}
		const auto finishWith = [&](QByteArray data, std::string mime) {
			request.done({
				.stream = std::make_unique<Webview::DataStreamFromMemory>(
					std::move(data),
					std::move(mime)),
				});
			return Webview::DataResult::Done;
		};
		const auto id = std::string_view(request.id).substr(3);
		if (id.starts_with("page") && id.ends_with(".html")) {
			if (!_subscribedToColors) {
				_subscribedToColors = true;

				rpl::merge(
					Lang::Updated(),
					style::PaletteChanged()
				) | rpl::start_with_next([=] {
					_updateStyles.call();
				}, _webview->lifetime());
			}
			auto index = 0;
			const auto result = std::from_chars(
				id.data() + 4,
				id.data() + id.size() - 5,
				index);
			if (result.ec != std::errc()
				|| index < 0
				|| index >= _pages.size()) {
				return Webview::DataResult::Failed;
			}
			return finishWith(WrapPage(_pages[index]), "text/html; charset=utf-8");
		} else if (id.starts_with("page") && id.ends_with(".json")) {
			auto index = 0;
			const auto result = std::from_chars(
				id.data() + 4,
				id.data() + id.size() - 5,
				index);
			if (result.ec != std::errc()
				|| index < 0
				|| index >= _pages.size()) {
				return Webview::DataResult::Failed;
			}
			auto &page = _pages[index];
			return finishWith(QJsonDocument(QJsonObject{
				{ "html", QJsonValue(QString::fromUtf8(page.content)) },
				{ "js", QJsonValue(QString::fromUtf8(page.script)) },
			}).toJson(QJsonDocument::Compact), "application/json");
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
				const auto full = (qstring == u"page.js"_q)
					? (ReadResource("morphdom.js") + bytes)
					: bytes;
				return finishWith(full, mime);
			}
		}
		return Webview::DataResult::Failed;
	});

	raw->init(R"()");
}

void Controller::showInWindow(const QString &dataPath, Prepared page) {
	Expects(_container != nullptr);

	const auto url = page.url;
	_hash = page.hash;
	auto i = _indices.find(url);
	if (i == end(_indices)) {
		_pages.push_back(std::move(page));
		i = _indices.emplace(url, int(_pages.size() - 1)).first;
	}
	const auto index = i->second;
	_index = index;
	if (!_webview) {
		createWebview(dataPath);
		if (_webview && _webview->widget()) {
			auto id = u"iv/page%1.html"_q.arg(index);
			if (!_hash.isEmpty()) {
				id += '#' + _hash;
			}
			_webview->navigateToData(id);
			activate();
		} else {
			_events.fire({ Event::Type::Close });
		}
	} else if (_ready) {
		_webview->eval(navigateScript(index, _hash));
		activate();
	} else {
		_navigateToIndexWhenReady = index;
		_navigateToHashWhenReady = _hash;
		activate();
	}
}

void Controller::activate() {
	if (_window->isMinimized()) {
		_window->showNormal();
	} else if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
	setInnerFocus();
}

void Controller::setInnerFocus() {
	if (const auto onstack = _shareFocus) {
		onstack();
	} else if (_webview) {
		_webview->focus();
	}
}

QByteArray Controller::navigateScript(int index, const QString &hash) {
	return "IV.navigateTo("
		+ QByteArray::number(index)
		+ ", '"
		+ EscapeForScriptString(qthelp::url_decode(hash).toUtf8())
		+ "');";
}

QByteArray Controller::reloadScript(int index) {
	return "IV.reloadPage("
		+ QByteArray::number(index)
		+ ");";
}

void Controller::processKey(const QString &key, const QString &modifier) {
	const auto ctrl = Platform::IsMac() ? u"cmd"_q : u"ctrl"_q;
	if (key == u"escape"_q) {
		escape();
	} else if (key == u"w"_q && modifier == ctrl) {
		close();
	} else if (key == u"m"_q && modifier == ctrl) {
		minimize();
	} else if (key == u"q"_q && modifier == ctrl) {
		quit();
	}
}

void Controller::processLink(const QString &url, const QString &context) {
	const auto channelPrefix = u"channel"_q;
	const auto joinPrefix = u"join_link"_q;
	const auto webpagePrefix = u"webpage"_q;
	const auto viewerPrefix = u"viewer"_q;
	if (context.startsWith(channelPrefix)) {
		_events.fire({
			.type = Event::Type::OpenChannel,
			.context = context.mid(channelPrefix.size()),
		});
	} else if (context.startsWith(joinPrefix)) {
		_events.fire({
			.type = Event::Type::JoinChannel,
			.context = context.mid(joinPrefix.size()),
		});
	} else if (context.startsWith(webpagePrefix)) {
		_events.fire({
			.type = Event::Type::OpenPage,
			.url = url,
			.context = context.mid(webpagePrefix.size()),
		});
	} else if (context.startsWith(viewerPrefix)) {
		_events.fire({
			.type = Event::Type::OpenMedia,
			.url = url,
			.context = context.mid(viewerPrefix.size()),
		});
	} else if (context.isEmpty()) {
		_events.fire({ .type = Event::Type::OpenLink, .url = url });
	}
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
}

void Controller::showJoinedTooltip() {
	if (_webview && _ready) {
		_webview->eval("IV.showTooltip('"
			+ EscapeForScriptString(
				tr::lng_action_you_joined(tr::now).toUtf8())
			+ "');");
	}
}

void Controller::minimize() {
	if (_window) {
		_window->setWindowState(_window->windowState()
			| Qt::WindowMinimized);
	}
}

QString Controller::composeCurrentUrl() const {
	const auto index = _index.current();
	Assert(index >= 0 && index < _pages.size());

	return _pages[index].url
		+ (_hash.isEmpty() ? u""_q : ('#' + _hash));
}

void Controller::showMenu() {
	const auto index = _index.current();
	if (_menu || index < 0 || index > _pages.size()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_window.get(),
		st::popupMenuWithIcons);
	if (_webview && _ready) {
		_webview->eval("IV.menuShown(true);");
	}
	_menu->setDestroyedCallback(crl::guard(_window.get(), [
			this,
			weakButton = Ui::MakeWeak(_menuToggle.data()),
			menu = _menu.get()] {
		if (_menu == menu && weakButton) {
			weakButton->setForceRippled(false);
		}
		if (const auto widget = _webview ? _webview->widget() : nullptr) {
			InvokeQueued(widget, crl::guard(_window.get(), [=] {
				if (_webview && _ready) {
					_webview->eval("IV.menuShown(false);");
				}
			}));
		}
	}));
	_menuToggle->setForceRippled(true);

	const auto url = composeCurrentUrl();
	const auto openInBrowser = crl::guard(_window.get(), [=] {
		_events.fire({ .type = Event::Type::OpenLinkExternal, .url = url });
	});
	_menu->addAction(
		tr::lng_iv_open_in_browser(tr::now),
		openInBrowser,
		&st::menuIconIpAddress);

	_menu->addAction(tr::lng_iv_share(tr::now), [=] {
		showShareMenu();
	}, &st::menuIconShare);

	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(_window->body()->mapToGlobal(
		QPoint(_window->body()->width(), 0) + st::ivMenuPosition));
}

void Controller::escape() {
	if (const auto onstack = _shareHide) {
		onstack();
	} else {
		close();
	}
}

void Controller::close() {
	_events.fire({ Event::Type::Close });
}

void Controller::quit() {
	_events.fire({ Event::Type::Quit });
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

void Controller::destroyShareMenu() {
	_shareHide = nullptr;
	if (_shareFocus) {
		_shareFocus = nullptr;
		setInnerFocus();
	}
	if (_shareWrap) {
		if (_shareContainer) {
			_shareWrap->windowHandle()->setParent(nullptr);
		}
		_shareWrap = nullptr;
		_shareContainer = nullptr;
	}
	if (_shareHidesContent) {
		_shareHidesContent = false;
		if (const auto content = _webview ? _webview->widget() : nullptr) {
			content->show();
		}
	}
}

void Controller::showShareMenu() {
	const auto index = _index.current();
	if (_shareWrap || index < 0 || index > _pages.size()) {
		return;
	}
	_shareHidesContent = Platform::IsMac();
	if (_shareHidesContent) {
		if (const auto content = _webview ? _webview->widget() : nullptr) {
			content->hide();
		}
	}

	_shareWrap = std::make_unique<Ui::RpWidget>(_shareHidesContent
		? _window->window()
		: nullptr);
	const auto margins = QMargins(0, st::windowTitleHeight, 0, 0);
	if (!_shareHidesContent) {
		_shareWrap->setGeometry(_window->geometry().marginsRemoved(margins));
		_shareWrap->setWindowFlag(Qt::FramelessWindowHint);
		_shareWrap->setAttribute(Qt::WA_TranslucentBackground);
		_shareWrap->setAttribute(Qt::WA_NoSystemBackground);
		_shareWrap->createWinId();

		_shareContainer.reset(QWidget::createWindowContainer(
			_shareWrap->windowHandle(),
			_window.get(),
			Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint));
	}
	_window->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto widget = _shareHidesContent
			? _shareWrap.get()
			: _shareContainer.get();
		widget->setGeometry(QRect(QPoint(), size).marginsRemoved(margins));
	}, _shareWrap->lifetime());

	auto result = _showShareBox({
		.parent = _shareWrap.get(),
		.url = composeCurrentUrl(),
	});
	_shareFocus = result.focus;
	_shareHide = result.hide;

	std::move(result.destroyRequests) | rpl::start_with_next([=] {
		destroyShareMenu();
	}, _shareWrap->lifetime());

	Ui::ForceFullRepaintSync(_shareWrap.get());

	if (_shareHidesContent) {
		_shareWrap->show();
	} else {
		_shareContainer->show();
	}
	activate();
}

} // namespace Iv
