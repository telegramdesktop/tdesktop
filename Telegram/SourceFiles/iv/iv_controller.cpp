/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_controller.h"

#include "iv/iv_data.h"
#include "ui/widgets/rp_window.h"
#include "webview/webview_data_stream_memory.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QFile>

namespace Iv {

Controller::Controller() = default;

Controller::~Controller() {
	_webview = nullptr;
	_window = nullptr;
}

void Controller::show(const QString &dataPath, Prepared page) {
	_window = std::make_unique<Ui::RpWindow>();
	const auto window = _window.get();

	window->setGeometry({ 200, 200, 800, 600 });

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
		_webview = nullptr;
		_window = nullptr;
		return;
	}
	raw->widget()->show();

	container->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		raw->widget()->setGeometry(geometry);
	}, _lifetime);

	raw->setNavigationStartHandler([=](const QString &uri, bool newWindow) {
		return true;
	});
	raw->setNavigationDoneHandler([=](bool success) {
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

rpl::producer<Webview::DataRequest> Controller::dataRequests() const {
	return _dataRequests.events();
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

} // namespace Iv
