/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Data {
class Thread;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
class ScrollArea;
class SubsectionSlider;
} // namespace Ui

namespace HistoryView {

class SubsectionTabs final {
public:
	SubsectionTabs(
		not_null<Window::SessionController*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::Thread*> thread);
	~SubsectionTabs();

	[[nodiscard]] bool switchTo(
		not_null<Data::Thread*> thread,
		not_null<Ui::RpWidget*> parent);

	[[nodiscard]] static bool UsedFor(not_null<Data::Thread*> thread);

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
	void track();
	void setupHorizontal(not_null<QWidget*> parent);
	void setupVertical(not_null<QWidget*> parent);
	void toggleModes();
	void setVisible(bool shown);
	void refreshSlice();
	void loadMore();
	[[nodiscard]] rpl::producer<> dataChanged() const;

	void setupSlider(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider,
		bool vertical);

	const not_null<Window::SessionController*> _controller;
	const not_null<History*> _history;

	Ui::RpWidget *_horizontal = nullptr;
	Ui::RpWidget *_vertical = nullptr;
	Ui::RpWidget *_shadow = nullptr;

	std::vector<not_null<Data::Thread*>> _slice;
	std::vector<not_null<Data::Thread*>> _sectionsSlice;

	not_null<Data::Thread*> _active;
	not_null<Data::Thread*> _around;
	int _beforeLimit = 0;
	int _afterLimit = 0;
	int _afterAvailable = 0;
	bool _loading = false;
	std::optional<int> _beforeSkipped;
	std::optional<int> _afterSkipped;

	rpl::event_stream<> _layoutRequests;
	rpl::event_stream<> _refreshed;
	rpl::event_stream<> _scrollCheckRequests;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
