/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace App {
	void hoveredItem(HistoryView::Element *item);
	HistoryView::Element *hoveredItem();
	void pressedItem(HistoryView::Element *item);
	HistoryView::Element *pressedItem();
	void hoveredLinkItem(HistoryView::Element *item);
	HistoryView::Element *hoveredLinkItem();
	void pressedLinkItem(HistoryView::Element *item);
	HistoryView::Element *pressedLinkItem();
	void mousedItem(HistoryView::Element *item);
	HistoryView::Element *mousedItem();
	void clearMousedItems();

	enum LaunchState {
		Launched = 0,
		QuitRequested = 1,
		QuitProcessed = 2,
	};
	void quit();
	bool quitting();
	LaunchState launchState();
	void setLaunchState(LaunchState state);
	void restart();

};
