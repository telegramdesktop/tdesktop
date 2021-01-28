/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/observer.h"

namespace style {
struct CalendarSizes;
} // namespace style

namespace Ui {

class IconButton;

class CalendarBox : public BoxContent, private base::Subscriber {
public:
	CalendarBox(
		QWidget*,
		QDate month,
		QDate highlighted,
		Fn<void(QDate date)> callback,
		FnMut<void(not_null<CalendarBox*>)> finalize = nullptr);
	CalendarBox(
		QWidget*,
		QDate month,
		QDate highlighted,
		Fn<void(QDate date)> callback,
		FnMut<void(not_null<CalendarBox*>)> finalize,
		const style::CalendarSizes &st);

	void setBeginningButton(bool enabled);
	bool hasBeginningButton() const;

	void setMinDate(QDate date);
	void setMaxDate(QDate date);

	~CalendarBox();

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

private:
	void monthChanged(QDate month);

	bool isPreviousEnabled() const;
	bool isNextEnabled() const;

	void goPreviousMonth();
	void goNextMonth();

	const style::CalendarSizes &_st;

	class Context;
	std::unique_ptr<Context> _context;

	class Inner;
	object_ptr<Inner> _inner;

	class Title;
	object_ptr<Title> _title;
	object_ptr<IconButton> _previous;
	object_ptr<IconButton> _next;

	Fn<void(QDate date)> _callback;
	FnMut<void(not_null<CalendarBox*>)> _finalize;

};

} // namespace Ui
