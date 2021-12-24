/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "app.h"

#include "history/view/history_view_element.h"
#include "core/update_checker.h"
#include "core/sandbox.h"
#include "core/application.h"
#include "mainwindow.h"

#include <QtCore/QBuffer>
#include <QtGui/QFontDatabase>

namespace {

App::LaunchState _launchState = App::Launched;

HistoryView::Element *hoveredItem = nullptr,
	*pressedItem = nullptr,
	*hoveredLinkItem = nullptr,
	*pressedLinkItem = nullptr,
	*mousedItem = nullptr;

} // namespace

namespace App {

	void hoveredItem(HistoryView::Element *item) {
		::hoveredItem = item;
	}

	HistoryView::Element *hoveredItem() {
		return ::hoveredItem;
	}

	void pressedItem(HistoryView::Element *item) {
		::pressedItem = item;
	}

	HistoryView::Element *pressedItem() {
		return ::pressedItem;
	}

	void hoveredLinkItem(HistoryView::Element *item) {
		::hoveredLinkItem = item;
	}

	HistoryView::Element *hoveredLinkItem() {
		return ::hoveredLinkItem;
	}

	void pressedLinkItem(HistoryView::Element *item) {
		::pressedLinkItem = item;
	}

	HistoryView::Element *pressedLinkItem() {
		return ::pressedLinkItem;
	}

	void mousedItem(HistoryView::Element *item) {
		::mousedItem = item;
	}

	HistoryView::Element *mousedItem() {
		return ::mousedItem;
	}

	void clearMousedItems() {
		hoveredItem(nullptr);
		pressedItem(nullptr);
		hoveredLinkItem(nullptr);
		pressedLinkItem(nullptr);
		mousedItem(nullptr);
	}

	void quit() {
		if (quitting()) {
			return;
		} else if (Core::IsAppLaunched()
			&& Core::App().exportPreventsQuit()) {
			return;
		}
		setLaunchState(QuitRequested);

		if (auto window = App::wnd()) {
			if (!Core::Sandbox::Instance().isSavingSession()) {
				window->hide();
			}
		}
		Core::Application::QuitAttempt();
	}

	bool quitting() {
		return _launchState != Launched;
	}

	LaunchState launchState() {
		return _launchState;
	}

	void setLaunchState(LaunchState state) {
		_launchState = state;
	}

	void restart() {
		using namespace Core;
		const auto updateReady = !UpdaterDisabled()
			&& (UpdateChecker().state() == UpdateChecker::State::Ready);
		if (updateReady) {
			cSetRestartingUpdate(true);
		} else {
			cSetRestarting(true);
			cSetRestartingToSettings(true);
		}
		App::quit();
	}

}
