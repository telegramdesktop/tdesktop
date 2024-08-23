/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_main.h"

#include "base/invoke_queued.h"
#include "base/integration.h"
#include "ui/effects/animations.h"
#include "ui/widgets/rp_window.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QScreen>
#include <QThread>
#include <QDir>

#include <qpa/qplatformscreen.h>

namespace Test {

bool App::notifyOrInvoke(QObject *receiver, QEvent *e) {
	if (e->type() == base::InvokeQueuedEvent::Type()) {
		static_cast<base::InvokeQueuedEvent*>(e)->invoke();
		return true;
	}
	return QApplication::notify(receiver, e);
}

bool App::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) {
	registerEnterFromEventLoop();
	return false;
}

void App::checkForEmptyLoopNestingLevel() {
	// _loopNestingLevel == _eventNestingLevel means that we had a
	// native event in a nesting loop that didn't get a notify() call
	// after. That means we already have exited the nesting loop and
	// there must not be any postponed calls with that nesting level.
	if (_loopNestingLevel == _eventNestingLevel) {
		Assert(_postponedCalls.empty()
			|| _postponedCalls.back().loopNestingLevel < _loopNestingLevel);
		Assert(!_previousLoopNestingLevels.empty());

		_loopNestingLevel = _previousLoopNestingLevels.back();
		_previousLoopNestingLevels.pop_back();
	}
}

void App::postponeCall(FnMut<void()> &&callable) {
	Expects(callable != nullptr);
	Expects(_eventNestingLevel >= _loopNestingLevel);

	checkForEmptyLoopNestingLevel();
	_postponedCalls.push_back({
		_loopNestingLevel,
		std::move(callable)
		});
}

void App::processPostponedCalls(int level) {
	while (!_postponedCalls.empty()) {
		auto &last = _postponedCalls.back();
		if (last.loopNestingLevel != level) {
			break;
		}
		auto taken = std::move(last);
		_postponedCalls.pop_back();
		taken.callable();
	}
}

void App::incrementEventNestingLevel() {
	++_eventNestingLevel;
}

void App::decrementEventNestingLevel() {
	Expects(_eventNestingLevel >= _loopNestingLevel);

	if (_eventNestingLevel == _loopNestingLevel) {
		_loopNestingLevel = _previousLoopNestingLevels.back();
		_previousLoopNestingLevels.pop_back();
	}
	const auto processTillLevel = _eventNestingLevel - 1;
	processPostponedCalls(processTillLevel);
	checkForEmptyLoopNestingLevel();
	_eventNestingLevel = processTillLevel;

	Ensures(_eventNestingLevel >= _loopNestingLevel);
}

void App::registerEnterFromEventLoop() {
	Expects(_eventNestingLevel >= _loopNestingLevel);

	if (_eventNestingLevel > _loopNestingLevel) {
		_previousLoopNestingLevels.push_back(_loopNestingLevel);
		_loopNestingLevel = _eventNestingLevel;
	}
}

bool App::notify(QObject *receiver, QEvent *e) {
	if (QThread::currentThreadId() != _mainThreadId) {
		return notifyOrInvoke(receiver, e);
	}

	const auto wrap = createEventNestingLevel();
	if (e->type() == QEvent::UpdateRequest) {
		const auto weak = QPointer<QObject>(receiver);
		_widgetUpdateRequests.fire({});
		if (!weak) {
			return true;
		}
	}
	return notifyOrInvoke(receiver, e);
}

rpl::producer<> App::widgetUpdateRequests() const {
	return _widgetUpdateRequests.events();
}

void BaseIntegration::enterFromEventLoop(FnMut<void()> &&method) {
	app().customEnterFromEventLoop(std::move(method));
}

bool BaseIntegration::logSkipDebug() {
	return true;
}

void BaseIntegration::logMessageDebug(const QString &message) {
}

void BaseIntegration::logMessage(const QString &message) {
}

void UiIntegration::postponeCall(FnMut<void()> &&callable) {
	app().postponeCall(std::move(callable));
}

void UiIntegration::registerLeaveSubscription(not_null<QWidget*> widget) {
}

void UiIntegration::unregisterLeaveSubscription(not_null<QWidget*> widget) {
}

QString UiIntegration::emojiCacheFolder() {
	return QDir().currentPath() + "/tests/" + name() + "/emoji";
}

QString UiIntegration::openglCheckFilePath() {
	return QDir().currentPath() + "/tests/" + name() + "/opengl";
}

QString UiIntegration::angleBackendFilePath() {
	return QDir().currentPath() + "/test/" + name() + "/angle";
}

} // namespace Test

int main(int argc, char *argv[]) {
	using namespace Test;

	auto app = App(argc, argv);
	app.installNativeEventFilter(&app);

	const auto ratio = app.devicePixelRatio();
	const auto useRatio = std::clamp(qCeil(ratio), 1, 3);
	style::SetDevicePixelRatio(useRatio);

	const auto screen = App::primaryScreen();
	const auto dpi = screen->logicalDotsPerInch();
	const auto basePair = screen->handle()->logicalBaseDpi();
	const auto baseMiddle = (basePair.first + basePair.second) * 0.5;
	const auto screenExact = dpi / baseMiddle;
	const auto screenScale = int(base::SafeRound(screenExact * 20)) * 5;
	const auto chosen = std::clamp(
		screenScale,
		style::kScaleMin,
		style::MaxScaleForRatio(useRatio));

	BaseIntegration base(argc, argv);
	base::Integration::Set(&base);

	UiIntegration ui;
	Ui::Integration::Set(&ui);

	InvokeQueued(&app, [=] {
		new Ui::Animations::Manager();
		style::StartManager(chosen);

		Ui::Emoji::Init();

		const auto window = new Ui::RpWindow();
		window->setGeometry(
			{ scale(100), scale(100), scale(800), scale(600) });
		window->show();

		window->setMinimumSize({ scale(240), scale(320) });

		test(window, window->body());
	});

	return app.exec();
}

namespace crl {

rpl::producer<> on_main_update_requests() {
	return Test::app().widgetUpdateRequests();
}

} // namespace crl
