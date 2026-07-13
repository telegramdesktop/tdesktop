/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_window.h"

#include "ui/layers/layer_manager.h"

#include <QtGui/QCloseEvent>

#ifdef Q_OS_WIN
#include <QtNetwork/QNetworkProxy>

#include "core/sandbox.h"

#include <windows.h>
#elif defined Q_OS_MAC // Q_OS_WIN
#include "core/sandbox.h"
#include "platform/mac/native_event_mac.h"
#endif // Q_OS_WIN || Q_OS_MAC

namespace Iv::Editor {
namespace {

[[nodiscard]] std::vector<not_null<Window*>> &LiveWindows() {
	static auto result = std::vector<not_null<Window*>>();
	return result;
}

} // namespace

Window::Window(QWidget *parent)
: Ui::RpWindow(parent)
, _layers(std::make_unique<Ui::LayerManager>(body())) {
	_layers->setHideByBackgroundClick(true);
	LiveWindows().push_back(this);
}

Window::~Window() {
	auto &list = LiveWindows();
	list.erase(
		ranges::remove(list, not_null<Window*>(this)),
		end(list));
}

void Window::setCloseRequestHandler(Fn<bool()> handler) {
	_closeRequestHandler = std::move(handler);
}

rpl::producer<> Window::imeCompositionStarts() const {
	return _imeCompositionStartReceived.events();
}

void Window::imeCompositionStartReceived() {
	_imeCompositionStartReceived.fire({});
}

void Window::showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) {
	_layers->showBox(std::move(box), options, animated);
}

void Window::showLayer(
		std::unique_ptr<Ui::LayerWidget> layer,
		Ui::LayerOptions options,
		anim::type animated) {
	_layers->showLayer(std::move(layer), options, animated);
}

void Window::hideLayer(anim::type animated) {
	_layers->hideAll(animated);
}

bool Window::isLayerShown() const {
	return (_layers->topShownLayer() != nullptr);
}

rpl::producer<bool> Window::layerShownValue() const {
	return _layers->layerShownValue();
}

std::shared_ptr<Ui::Show> Window::uiShow() {
	return _layers->uiShow();
}

bool Window::eventHook(QEvent *event) {
	if (event->type() == QEvent::Close && _closeRequestHandler) {
		const auto closeEvent = static_cast<QCloseEvent*>(event);
		if (_closeRequestHandler()) {
			closeEvent->accept();
		} else {
			closeEvent->ignore();
		}
		return true;
	}
	return Ui::RpWindow::eventHook(event);
}

#ifdef Q_OS_WIN

bool Window::nativeEvent(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) {
	if (message) {
		const auto msg = static_cast<MSG*>(message);
		if (msg->message == WM_IME_STARTCOMPOSITION) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				imeCompositionStartReceived();
			});
		}
	}
	return false;
}

#elif defined Q_OS_MAC // Q_OS_WIN

bool Window::nativeEvent(
		const QByteArray &eventType,
		void *message,
		qintptr *result) {
	if (message && eventType == "NSEvent") {
		if (Platform::PossiblyTextTypingEvent(message)) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				imeCompositionStartReceived();
			});
		}
	}
	return false;
}

#endif // Q_OS_WIN || Q_OS_MAC

bool CloseActiveWindow() {
	for (const auto &window : LiveWindows()) {
		if (window->isActiveWindow()) {
			window->close();
			return true;
		}
	}
	return false;
}

} // namespace Iv::Editor
