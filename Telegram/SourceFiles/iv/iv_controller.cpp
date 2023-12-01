/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "base/platform/base_platform_info.h"
#include "iv/iv_data.h"
#include "ui/widgets/rp_window.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/palette.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QFile>
#include <QtGui/QPainter>

namespace Iv {

Controller::Controller() = default;

Controller::~Controller() {
	_webview = nullptr;
	_window = nullptr;
}

void Controller::show(const QString &dataPath, Prepared page) {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	window->setGeometry({ 200, 200, 600, 800 });

	const auto container = Ui::CreateChild<Ui::RpWidget>(
		window->body().get());
	window->sizeValue() | rpl::start_with_next([=](QSize size) {
		container->setGeometry(QRect(QPoint(), size));
	}, container->lifetime());
	container->show();

	_webview = std::make_unique<Webview::Window>(
		container,
		Webview::WindowConfig{ .userDataPath = dataPath });
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

	container->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		raw->widget()->setGeometry(geometry);
	}, container->lifetime());

	container->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(container).fillRect(clip, st::windowBg);
	}, container->lifetime());

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
			return finishWith(page.html, "text/html");
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

	window->show();
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
