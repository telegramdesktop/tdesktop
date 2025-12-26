/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "dialogs/dialogs_common.h"

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
class PopupMenu;
class ScrollArea;
class SubsectionSlider;
class SubsectionSliderReorder;
} // namespace Ui

namespace HistoryView {

class SubsectionTabs final {
public:
	SubsectionTabs(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::Thread*> thread);
	~SubsectionTabs();

	[[nodiscard]] Main::Session &session();

	[[nodiscard]] bool switchTo(
		not_null<Data::Thread*> thread,
		not_null<Ui::RpWidget*> parent);

	[[nodiscard]] static bool UsedFor(not_null<Data::Thread*> thread);

	[[nodiscard]] bool dying() const;
	[[nodiscard]] rpl::producer<> removeRequests() const;

	void extractToParent(not_null<Ui::RpWidget*> parent);

	void setBoundingRect(QRect boundingRect);
	[[nodiscard]] rpl::producer<> layoutRequests() const;
	[[nodiscard]] int leftSkip() const;
	[[nodiscard]] int topSkip() const;

	void raise();
	void show();
	void hide();

private:
	struct Item {
		not_null<Data::Thread*> thread;
		Dialogs::BadgesState badges;
		DocumentId iconId = 0;
		QString name;

		friend inline auto operator<=>(
			const Item &,
			const Item &) = default;
		friend inline bool operator==(
			const Item &,
			const Item &) = default;
	};

	void track();
	void setupHorizontal(not_null<QWidget*> parent);
	void setupVertical(not_null<QWidget*> parent);
	void toggleModes();
	void setVisible(bool shown);
	void refreshSlice();
	void refreshAroundMiddle(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider);
	void scheduleRefresh();
	void loadMore();
	void setup(not_null<Ui::RpWidget*> parent);
	[[nodiscard]] rpl::producer<> dataChanged() const;

	void setupSlider(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider,
		bool vertical);
	void startScrollChecking(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider,
		bool vertical);
	void startFillingSlider(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider,
		bool vertical);
	void showThreadContextMenu(not_null<Data::Thread*> thread);
	void applyReorder(
		not_null<Ui::RpWidget*> widget,
		int oldPosition,
		int newPosition);

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;

	base::unique_qptr<Ui::PopupMenu> _menu;

	Ui::RpWidget *_horizontal = nullptr;
	Ui::RpWidget *_vertical = nullptr;
	Ui::RpWidget *_shadow = nullptr;
	std::unique_ptr<Ui::SubsectionSliderReorder> _reorder;
	int _reordering = 0;

	std::vector<Item> _slice;
	std::vector<Item> _sectionsSlice;

	not_null<Data::Thread*> _active;
	not_null<Data::Thread*> _around;
	int _beforeLimit = 0;
	int _afterLimit = 0;
	int _afterAvailable = 0;
	bool _loading = false;
	bool _refreshScheduled = false;
	std::optional<int> _beforeSkipped;
	std::optional<int> _afterSkipped;

	rpl::event_stream<> _layoutRequests;
	rpl::event_stream<> _refreshed;
	rpl::event_stream<> _scrollCheckRequests;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
