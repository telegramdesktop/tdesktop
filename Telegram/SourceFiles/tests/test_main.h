/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/integration.h"
#include "ui/style/style_core_scale.h"
#include "ui/integration.h"

#include <crl/crl.h>
#include <rpl/rpl.h>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QThread>
#include <QDir>

namespace Ui {
class RpWidget;
class RpWindow;
} // namespace Ui

namespace Test {

[[nodiscard]] QString name();

void test(not_null<Ui::RpWindow*> window, not_null<Ui::RpWidget*> widget);

[[nodiscard]] inline int scale(int value) {
	return style::ConvertScale(value);
};

class App final : public QApplication, public QAbstractNativeEventFilter {
public:
	using QApplication::QApplication;

	template <typename Callable>
	auto customEnterFromEventLoop(Callable &&callable) {
		registerEnterFromEventLoop();
		const auto wrap = createEventNestingLevel();
		return callable();
	}

	void postponeCall(FnMut<void()> &&callable);

	[[nodiscard]] rpl::producer<> widgetUpdateRequests() const;

private:
	struct PostponedCall {
		int loopNestingLevel = 0;
		FnMut<void()> callable;
	};

	auto createEventNestingLevel() {
		incrementEventNestingLevel();
		return gsl::finally([=] { decrementEventNestingLevel(); });
	}

	void checkForEmptyLoopNestingLevel();
	void processPostponedCalls(int level);
	void incrementEventNestingLevel();
	void decrementEventNestingLevel();
	void registerEnterFromEventLoop();

	bool notifyOrInvoke(QObject *receiver, QEvent *e);
	bool notify(QObject *receiver, QEvent *e) override;
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) override;

	rpl::event_stream<> _widgetUpdateRequests;
	Qt::HANDLE _mainThreadId = QThread::currentThreadId();
	int _eventNestingLevel = 0;
	int _loopNestingLevel = 0;
	std::vector<int> _previousLoopNestingLevels;
	std::vector<PostponedCall> _postponedCalls;

};

[[nodiscard]] inline App &app() {
	return *static_cast<App*>(QCoreApplication::instance());
}

class BaseIntegration final : public base::Integration {
public:
	using Integration::Integration;

	void enterFromEventLoop(FnMut<void()> &&method);
	bool logSkipDebug();
	void logMessageDebug(const QString &message);
	void logMessage(const QString &message);
};

class UiIntegration final : public Ui::Integration {
public:
	void postponeCall(FnMut<void()> &&callable);
	void registerLeaveSubscription(not_null<QWidget*> widget);
	void unregisterLeaveSubscription(not_null<QWidget*> widget);
	QString emojiCacheFolder();
	QString openglCheckFilePath();
	QString angleBackendFilePath();
};

} // namespace Test
