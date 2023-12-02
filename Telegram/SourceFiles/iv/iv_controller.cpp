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
#include "ui/widgets/rp_window.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"

#include "base/call_delayed.h"
#include "ui/effects/animations.h"

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
		{ "window-fg", &st::windowFg },
		{ "window-sub-text-fg", &st::windowSubTextFg },
		{ "window-active-text-fg", &st::windowActiveTextFg },
		{ "window-bg-active", &st::windowBgActive },
		{ "box-divider-bg", &st::boxDividerBg },
		{ "box-divider-fg", &st::boxDividerFg },
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

} // namespace
Controller::Controller()
: _updateStyles([=] {
	const auto str = EscapeForScriptString(ComputeStyles());
	if (_webview) {
		_webview->eval("IV.updateStyles(\"" + str + "\");");
	}
}) {
}

Controller::~Controller() {
	_webview = nullptr;
	_window = nullptr;
}

void Controller::show(const QString &dataPath, Prepared page) {
	createWindow();
	InvokeQueued(_container, [=, page = std::move(page)]() mutable {
		showInWindow(dataPath, std::move(page));
	});
}

void Controller::createWindow() {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	window->setGeometry({ 200, 200, 600, 800 });

	const auto skip = window->lifetime().make_state<rpl::variable<int>>(0);

	_container = Ui::CreateChild<Ui::RpWidget>(window->body().get());
	rpl::combine(
		window->body()->sizeValue(),
		skip->value()
	) | rpl::start_with_next([=](QSize size, int skip) {
		_container->setGeometry(QRect(QPoint(), size).marginsRemoved({ 0, skip, 0, 0 }));
	}, _container->lifetime());

	base::call_delayed(5000, window, [=] {
		const auto animation = window->lifetime().make_state<Ui::Animations::Simple>();
		animation->start([=] {
			*skip = animation->value(64);
			if (!animation->animating()) {
				base::call_delayed(4000, window, [=] {
					animation->start([=] {
						*skip = animation->value(0);
					}, 64, 0, 200, anim::easeOutCirc);
				});
			}
		}, 0, 64, 200, anim::easeOutCirc);
	});

	window->body()->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(window->body());
		p.fillRect(clip, st::windowBg);
		p.fillRect(clip, QColor(0, 128, 0, 128));
	}, window->body()->lifetime());

	_container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_container).fillRect(clip, st::windowBg);
	}, _container->lifetime());

	_container->show();
	window->show();
}

void Controller::showInWindow(const QString &dataPath, Prepared page) {
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
		_webview = nullptr;
	});
	if (!raw->widget()) {
		_events.fire(Event::Close);
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
			const auto i = page.html.indexOf("<html"_q);
			Assert(i >= 0);
			const auto colored = page.html.mid(0, i + 5)
				+ " style=\"" + EscapeForAttribute(ComputeStyles()) + "\""
				+ page.html.mid(i + 5);
			if (!_subscribedToColors) {
				_subscribedToColors = true;

				rpl::merge(
					Lang::Updated(),
					style::PaletteChanged()
				) | rpl::start_with_next([=] {
					_updateStyles.call();
				}, _webview->lifetime());
			}
			return finishWith(colored, "text/html");
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

	raw->init(R"(
)");
	raw->navigateToData("iv/page.html");
}

bool Controller::active() const {
	return _window && _window->isActiveWindow();
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
	_events.fire(Event::Close);
}

void Controller::quit() {
	_events.fire(Event::Quit);
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

} // namespace Iv
