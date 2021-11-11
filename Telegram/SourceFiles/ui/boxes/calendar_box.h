/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/required.h"

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
	bool hasBeginningButton = false;
};

class CalendarBox final : public BoxContent {
public:
	CalendarBox(QWidget*, CalendarBoxArgs &&args);
	~CalendarBox();

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

	const style::CalendarSizes &_st;

	class Context;
	std::unique_ptr<Context> _context;

	std::unique_ptr<ScrollArea> _scroll;

	class Inner;
	not_null<Inner*> _inner;

	class Title;
	object_ptr<Title> _title;
	object_ptr<IconButton> _previous;
	object_ptr<IconButton> _next;

	Fn<void(QDate date)> _callback;
	FnMut<void(not_null<CalendarBox*>)> _finalize;
	bool _watchScroll = false;

};

} // namespace Ui
