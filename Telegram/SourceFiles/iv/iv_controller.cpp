/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "base/platform/base_platform_info.h"
#include "base/invoke_queued.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/rp_window.h"
#include "ui/painter.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QFile>
#include <QtGui/QPainter>

namespace Iv {
namespace {

[[nodiscard]] QByteArray ComputeStyles() {
	static const auto map = base::flat_map<QByteArray, const style::color*>{
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
		{ "group-call-join", tr::lng_group_call_join },
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

[[nodiscard]] QByteArray WrapPage(
		const Prepared &page,
		const QByteArray &initScript) {
#ifdef Q_OS_MAC
	const auto classAttribute = ""_q;
#else // Q_OS_MAC
	const auto classAttribute = " class=\"custom_scroll\""_q;
#endif // Q_OS_MAC

	const auto js = QByteArray()
		+ (page.hasCode ? "IV.initPreBlocks();" : "")
		+ (page.hasEmbeds ? "IV.initEmbedBlocks();" : "")
		+ "IV.init();"
		+ initScript;

	const auto contentAttributes = page.rtl
		? " dir=\"rtl\" class=\"rtl\""_q
		: QByteArray();

	return R"(<!DOCTYPE html>
<html)"_q
	+ classAttribute
	+ R"("" style=")"
	+ EscapeForAttribute(ComputeStyles())
	+ R"(">
	<head>
		<meta charset="utf-8">
		<meta name="robots" content="noindex, nofollow">
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<script src="/iv/page.js"></script>
		<script src="/iv/highlight.js"></script>
		<link rel="stylesheet" href="/iv/page.css" />
		<link rel="stylesheet" href="/iv/highlight.css">
	</head>
	<body>
		<button class="fixed_button hidden" id="top_back" onclick="IV.back();">
			<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
				<line x1="5.37464142" y1="12" x2="18.5" y2="12"></line>
				<path d="M11.5,18.3 L5.27277119,12.0707223 C5.23375754,12.0316493 5.23375754,11.9683507 5.27277119,11.9292777 L11.5,5.7 L11.5,5.7"></path>
			</svg>
		</button>
		<button class="fixed_button" id="top_menu" onclick="IV.menu();">
			<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
				<circle cx="12" cy="17.4" r="1.7"></circle>
				<circle cx="12" cy="12" r="1.7"></circle>
				<circle cx="12" cy="6.6" r="1.7"></circle>
			</svg>
		</button>
		<button class="fixed_button hidden" id="bottom_up" onclick="IV.toTop();">
			<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
				<path d="M14.9972363,18 L9.13865768,12.1414214 C9.06055283,12.0633165 9.06055283,11.9366835 9.13865768,11.8585786 L14.9972363,6 L14.9972363,6" transform="translate(11.997236, 12.000000) scale(-1, -1) rotate(-90.000000) translate(-11.997236, -12.000000) "></path>
			</svg>
		</button>
		<article)"_q + contentAttributes + ">"_q + page.content + R"(</article>
		<script>)"_q + js + R"(</script>
	</body>
</html>
)"_q;
}

} // namespace

Controller::Controller()
: _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("IV.updateStyles('" + str + "');");
	}
}) {
}

Controller::~Controller() {
	_ready = false;
	_webview = nullptr;
	_title = nullptr;
	_window = nullptr;
}

void Controller::show(
		const QString &dataPath,
		Prepared page,
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues) {
	createWindow();
	const auto js = fillInChannelValuesScript(std::move(inChannelValues));

	_titleText.setText(st::ivTitle.style, page.title);
	InvokeQueued(_container, [=, page = std::move(page)]() mutable {
		showInWindow(dataPath, std::move(page), js);
		if (!_webview) {
			return;
		}
	});
}

QByteArray Controller::fillInChannelValuesScript(
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues) {
	auto result = QByteArray();
	for (auto &[id, in] : inChannelValues) {
		std::move(in) | rpl::start_with_next([=](bool in) {
			if (_ready) {
				_webview->eval(toggleInChannelScript(id, in));
			} else {
				_inChannelChanged[id] = in;
			}
		}, _lifetime);
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

void Controller::updateTitleGeometry() {
	_title->setGeometry(0, 0, _window->width(), st::ivTitle.height);
}

void Controller::paintTitle(Painter &p, QRect clip) {
	const auto active = _window->isActiveWindow();
	const auto full = _title->width();
	p.setPen(active ? st::ivTitle.fgActive : st::ivTitle.fg);
	const auto available = QRect(
		_titleLeftSkip,
		0,
		full - _titleLeftSkip - _titleRightSkip,
		_title->height());
	const auto use = std::min(available.width(), _titleText.maxWidth());
	const auto center = full
		- 2 * std::max(_titleLeftSkip, _titleRightSkip);
	const auto left = (use <= center)
		? ((full - use) / 2)
		: (use < available.width() && _titleLeftSkip < _titleRightSkip)
		? (available.x() + available.width() - use)
		: available.x();
	const auto titleTextHeight = st::ivTitle.style.font->height;
	const auto top = (st::ivTitle.height - titleTextHeight) / 2;
	_titleText.drawLeftElided(p, left, top, available.width(), full);
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	_window->setTitleStyle(st::ivTitle);
	const auto window = _window.get();

	_title = std::make_unique<Ui::RpWidget>(window);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_title->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(_title.get());
		paintTitle(p, clip);
	}, _title->lifetime());
	window->widthValue() | rpl::start_with_next([=] {
		updateTitleGeometry();
	}, _title->lifetime());

#ifdef Q_OS_MAC
	_titleLeftSkip = 8 + 12 + 8 + 12 + 8 + 12 + 8;
	_titleRightSkip = st::ivTitle.style.font->spacew;
#else // Q_OS_MAC
	using namespace Ui::Platform;
	TitleControlsLayoutValue(
	) | rpl::start_with_next([=](TitleControls::Layout layout) {
		const auto accumulate = [](const auto &list) {
			auto result = 0;
			for (const auto control : list) {
				switch (control) {
				case TitleControl::Close:
					result += st::ivTitle.close.width;
					break;
				case TitleControl::Minimize:
					result += st::ivTitle.minimize.width;
					break;
				case TitleControl::Maximize:
					result += st::ivTitle.maximize.width;
					break;
				}
			}
			return result;
		};
		const auto space = st::ivTitle.style.font->spacew;
		_titleLeftSkip = accumulate(layout.left) + space;
		_titleRightSkip = accumulate(layout.right) + space;
		_title->update();
	}, _title->lifetime());
#endif // Q_OS_MAC

	window->setGeometry({ 200, 200, 600, 800 });
	window->setMinimumSize({ st::windowMinWidth, st::windowMinHeight });

	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	rpl::combine(
		window->body()->sizeValue(),
		_title->heightValue()
	) | rpl::start_with_next([=](QSize size, int title) {
		title -= window->body()->y();
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved(
			{ 0, title, 0, 0 }));
	}, _container->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	window->show();
}

void Controller::showInWindow(
		const QString &dataPath,
		Prepared page,
		const QByteArray &initScript) {
	Expects(_container != nullptr);

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
	if (!raw->widget()) {
		_events.fire({ Event::Type::Close });
		return;
	}
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
			} else if (event == u"ready"_q) {
				_ready = true;
				auto script = QByteArray();
				for (const auto &[id, in] : base::take(_inChannelChanged)) {
					script += toggleInChannelScript(id, in);
				}
				if (!script.isEmpty()) {
					_webview->eval(script);
				}
			}
		});
	});
	raw->setDataRequestHandler([=](Webview::DataRequest request) {
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
		if (id == "page.html") {
			if (!_subscribedToColors) {
				_subscribedToColors = true;

				rpl::merge(
					Lang::Updated(),
					style::PaletteChanged()
				) | rpl::start_with_next([=] {
					_updateStyles.call();
				}, _webview->lifetime());
			}
			return finishWith(WrapPage(page, initScript), "text/html");
		}
		const auto css = id.ends_with(".css");
		const auto js = !css && id.ends_with(".js");
		if (!css && !js) {
			return Webview::DataResult::Failed;
		}
		const auto qstring = QString::fromUtf8(id.data(), id.size());
		const auto pattern = u"^[a-zA-Z\\.\\-_0-9]+$"_q;
		if (QRegularExpression(pattern).match(qstring).hasMatch()) {
			auto file = QFile(u":/iv/"_q + qstring);
			if (file.open(QIODevice::ReadOnly)) {
				const auto mime = css ? "text/css" : "text/javascript";
				return finishWith(file.readAll(), mime);
			}
		}
		return Webview::DataResult::Failed;
	});

	raw->init(R"()");
	raw->navigateToData("iv/page.html");
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
	if (context.startsWith(channelPrefix)) {
		_events.fire({
			Event::Type::OpenChannel,
			context.mid(channelPrefix.size()),
		});
	} else if (context.startsWith(joinPrefix)) {
		_events.fire({
			Event::Type::JoinChannel,
			context.mid(joinPrefix.size()),
		});
	}
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
}

void Controller::showJoinedTooltip() {
	if (_webview) {
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

void Controller::escape() {
	close();
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

} // namespace Iv
