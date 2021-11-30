/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "ui/widgets/tooltip.h"
#include "base/required.h"
#include "base/timer.h"

namespace style {
struct CalendarSizes;
} // namespace style

namespace st {
extern const style::CalendarSizes &defaultCalendarSizes;
} // namespace st

namespace Ui {

class IconButton;
class ScrollArea;
class CalendarBox;

struct CalendarBoxArgs {
	template <typename T>
	using required = base::required<T>;

	required<QDate> month;
	required<QDate> highlighted;
	required<Fn<void(QDate date)>> callback;
	FnMut<void(not_null<CalendarBox*>)> finalize;
	const style::CalendarSizes &st = st::defaultCalendarSizes;
	QDate minDate;
	QDate maxDate;
	bool allowsSelection = false;
	Fn<void(
		not_null<Ui::CalendarBox*>,
		std::optional<int>)> selectionChanged;
};

class CalendarBox final : public BoxContent, private AbstractTooltipShower {
public:
	CalendarBox(QWidget*, CalendarBoxArgs &&args);
	~CalendarBox();

	void toggleSelectionMode(bool enabled);

	[[nodiscard]] QDate selectedFirstDate() const;
	[[nodiscard]] QDate selectedLastDate() const;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void monthChanged(QDate month);

	bool isPreviousEnabled() const;
	bool isNextEnabled() const;

	void goPreviousMonth();
	void goNextMonth();
	void setExactScroll();
	void processScroll();
	void createButtons();

	void showJumpTooltip(not_null<IconButton*> button);
	void jumpAfterDelay(not_null<IconButton*> button);
	void jump(QPointer<IconButton> button);

	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	const style::CalendarSizes &_st;

	class Context;
	std::unique_ptr<Context> _context;

	std::unique_ptr<ScrollArea> _scroll;

	class Inner;
	not_null<Inner*> _inner;

	class FloatingDate;
	std::unique_ptr<FloatingDate> _floatingDate;

	class Title;
	object_ptr<Title> _title;
	object_ptr<IconButton> _previous;
	object_ptr<IconButton> _next;
	bool _previousEnabled = false;
	bool _nextEnabled = false;

	Fn<void(QDate date)> _callback;
	FnMut<void(not_null<CalendarBox*>)> _finalize;
	bool _watchScroll = false;

	QPointer<IconButton> _tooltipButton;
	QPointer<IconButton> _jumpButton;
	base::Timer _jumpTimer;

	bool _selectionMode = false;
	Fn<void(
		not_null<Ui::CalendarBox*>,
		std::optional<int>)> _selectionChanged;

};

} // namespace Ui
