/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_schedule_box.h"

#include "api/api_common.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_scheduled_messages.h" // kScheduledUntilOnlineTimestamp
#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/calendar_box.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "chat_helpers/send_context_menu.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_chat.h"

#include <QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kMinimalSchedule = TimeId(10);

tr::phrase<> MonthDay(int index) {
	switch (index) {
	case 1: return tr::lng_month_day1;
	case 2: return tr::lng_month_day2;
	case 3: return tr::lng_month_day3;
	case 4: return tr::lng_month_day4;
	case 5: return tr::lng_month_day5;
	case 6: return tr::lng_month_day6;
	case 7: return tr::lng_month_day7;
	case 8: return tr::lng_month_day8;
	case 9: return tr::lng_month_day9;
	case 10: return tr::lng_month_day10;
	case 11: return tr::lng_month_day11;
	case 12: return tr::lng_month_day12;
	}
	Unexpected("Index in MonthDay.");
}

QString DayString(const QDate &date) {
	return tr::lng_month_day(
		tr::now,
		lt_month,
		MonthDay(date.month())(tr::now),
		lt_day,
		QString::number(date.day()));
}

QString TimeString(TimeId time) {
	const auto parsed = base::unixtime::parse(time).time();
	return QString("%1:%2"
	).arg(parsed.hour()
	).arg(parsed.minute(), 2, 10, QLatin1Char('0'));
}

int ProcessWheelEvent(not_null<QWheelEvent*> e) {
	// Only a mouse wheel is accepted.
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
	const auto delta = e->angleDelta().y();
	const auto absDelta = std::abs(delta);
	if (absDelta != step) {
		return 0;
	}
	return (delta / absDelta);
}

class TimePart final : public Ui::MaskedInputField {
public:
	using MaskedInputField::MaskedInputField;

	void setMaxValue(int value);
	void setWheelStep(int value);

	rpl::producer<> erasePrevious() const;
	rpl::producer<QChar> putNext() const;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	int _maxValue = 0;
	int _maxDigits = 0;
	int _wheelStep = 0;
	rpl::event_stream<> _erasePrevious;
	rpl::event_stream<QChar> _putNext;

};

int Number(not_null<TimePart*> field) {
	const auto text = field->getLastText();
	auto ref = text.midRef(0);
	while (!ref.isEmpty() && ref.at(0) == '0') {
		ref = ref.mid(1);
	}
	return ref.toInt();
}

class TimeInput final : public Ui::RpWidget {
public:
	TimeInput(QWidget *parent, const QString &value);

	bool setFocusFast();
	rpl::producer<QString> value() const;
	rpl::producer<> submitRequests() const;
	QString valueCurrent() const;
	void showError();

	int resizeGetHeight(int width) override;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void setInnerFocus();
	void putNext(const object_ptr<TimePart> &field, QChar ch);
	void erasePrevious(const object_ptr<TimePart> &field);
	void finishInnerAnimating();
	void setErrorShown(bool error);
	void setFocused(bool focused);
	void startBorderAnimation();
	template <typename Widget>
	bool insideSeparator(QPoint position, const Widget &widget) const;

	int hour() const;
	int minute() const;

	object_ptr<TimePart> _hour;
	object_ptr<Ui::PaddingWrap<Ui::FlatLabel>> _separator1;
	object_ptr<TimePart> _minute;
	rpl::variable<QString> _value;
	rpl::event_stream<> _submitRequests;

	style::cursor _cursor = style::cur_default;
	Ui::Animations::Simple _a_borderShown;
	int _borderAnimationStart = 0;
	Ui::Animations::Simple _a_borderOpacity;
	bool _borderVisible = false;

	Ui::Animations::Simple _a_error;
	bool _error = false;
	Ui::Animations::Simple _a_focused;
	bool _focused = false;

};

QTime ValidateTime(const QString &value) {
	const auto match = QRegularExpression(
		"^(\\d{1,2})\\:(\\d\\d)$").match(value);
	if (!match.hasMatch()) {
		return QTime();
	}
	const auto readInt = [](const QString &value) {
		auto ref = value.midRef(0);
		while (!ref.isEmpty() && ref.at(0) == '0') {
			ref = ref.mid(1);
		}
		return ref.toInt();
	};
	return QTime(readInt(match.captured(1)), readInt(match.captured(2)));
}

QString GetHour(const QString &value) {
	if (const auto time = ValidateTime(value); time.isValid()) {
		return QString::number(time.hour());
	}
	return QString();
}

QString GetMinute(const QString &value) {
	if (const auto time = ValidateTime(value); time.isValid()) {
		return QString("%1").arg(time.minute(), 2, 10, QChar('0'));
	}
	return QString();
}

void TimePart::setMaxValue(int value) {
	_maxValue = value;
	_maxDigits = 0;
	while (value > 0) {
		++_maxDigits;
		value /= 10;
	}
}

void TimePart::setWheelStep(int value) {
	_wheelStep = value;
}

rpl::producer<> TimePart::erasePrevious() const {
	return _erasePrevious.events();
}

rpl::producer<QChar> TimePart::putNext() const {
	return _putNext.events();
}

void TimePart::keyPressEvent(QKeyEvent *e) {
	const auto isBackspace = (e->key() == Qt::Key_Backspace);
	const auto isBeginning = (cursorPosition() == 0);
	if (isBackspace && isBeginning && !hasSelectedText()) {
		_erasePrevious.fire({});
	} else {
		MaskedInputField::keyPressEvent(e);
	}
}

void TimePart::wheelEvent(QWheelEvent *e) {
	const auto direction = ProcessWheelEvent(e);
	auto time = Number(this) + (direction * _wheelStep);
	const auto max = _maxValue + 1;
	if (time < 0) {
		time += max;
	} else if (time >= max) {
		time -= max;
	}
	setText(QString::number(time));
}

void TimePart::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newText = QString();
	auto newCursor = -1;
	const auto oldCursor = nowCursor;
	const auto oldLength = now.size();
	auto accumulated = 0;
	auto limit = 0;
	for (; limit != oldLength; ++limit) {
		if (now[limit].isDigit()) {
			accumulated *= 10;
			accumulated += (now[limit].unicode() - '0');
			if (accumulated > _maxValue || limit == _maxDigits) {
				break;
			}
		}
	}
	for (auto i = 0; i != limit;) {
		if (now[i].isDigit()) {
			newText += now[i];
		}
		if (++i == oldCursor) {
			newCursor = newText.size();
		}
	}
	if (newCursor < 0) {
		newCursor = newText.size();
	}
	if (newText != now) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	if (newCursor != nowCursor) {
		nowCursor = newCursor;
		setCursorPosition(nowCursor);
	}
	if (accumulated > _maxValue
		|| (limit == _maxDigits && oldLength > _maxDigits)) {
		if (oldCursor > limit) {
			_putNext.fire('0' + (accumulated % 10));
		} else {
			_putNext.fire(0);
		}
	}
}

TimeInput::TimeInput(QWidget *parent, const QString &value)
: RpWidget(parent)
, _hour(
	this,
	st::scheduleTimeField,
	rpl::never<QString>(),
	GetHour(value))
, _separator1(
	this,
	object_ptr<Ui::FlatLabel>(
		this,
		QString(":"),
		st::scheduleTimeSeparator),
	st::scheduleTimeSeparatorPadding)
, _minute(
	this,
	st::scheduleTimeField,
	rpl::never<QString>(),
	GetMinute(value))
, _value(valueCurrent()) {
	const auto focused = [=](const object_ptr<TimePart> &field) {
		return [this, pointer = Ui::MakeWeak(field.data())]{
			_borderAnimationStart = pointer->borderAnimationStart()
				+ pointer->x()
				- _hour->x();
			setFocused(true);
		};
	};
	const auto blurred = [=] {
		setFocused(false);
	};
	const auto changed = [=] {
		_value = valueCurrent();
	};
	connect(_hour, &Ui::MaskedInputField::focused, focused(_hour));
	connect(_minute, &Ui::MaskedInputField::focused, focused(_minute));
	connect(_hour, &Ui::MaskedInputField::blurred, blurred);
	connect(_minute, &Ui::MaskedInputField::blurred, blurred);
	connect(_hour, &Ui::MaskedInputField::changed, changed);
	connect(_minute, &Ui::MaskedInputField::changed, changed);
	_hour->setMaxValue(23);
	_hour->setWheelStep(1);
	_hour->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(_minute, ch);
	}, lifetime());
	_minute->setMaxValue(59);
	_minute->setWheelStep(10);
	_minute->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(_hour);
	}, lifetime());
	_separator1->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);

	_value.changes(
	) | rpl::start_with_next([=] {
		setErrorShown(false);
	}, lifetime());

	const auto submitHour = [=] {
		if (hour()) {
			_minute->setFocus();
		}
	};
	const auto submitMinute = [=] {
		if (minute()) {
			if (hour()) {
				_submitRequests.fire({});
			} else {
				_hour->setFocus();
			}
		}
	};
	connect(
		_hour,
		&Ui::MaskedInputField::submitted,
		submitHour);
	connect(
		_minute,
		&Ui::MaskedInputField::submitted,
		submitMinute);
}

void TimeInput::putNext(const object_ptr<TimePart> &field, QChar ch) {
	field->setCursorPosition(0);
	if (ch.unicode()) {
		field->setText(ch + field->getLastText());
		field->setCursorPosition(1);
	}
	field->setFocus();
}

void TimeInput::erasePrevious(const object_ptr<TimePart> &field) {
	const auto text = field->getLastText();
	if (!text.isEmpty()) {
		field->setCursorPosition(text.size() - 1);
		field->setText(text.mid(0, text.size() - 1));
	}
	field->setFocus();
}

bool TimeInput::setFocusFast() {
	if (hour()) {
		_minute->setFocusFast();
	} else {
		_hour->setFocusFast();
	}
	return true;
}

int TimeInput::hour() const {
	return Number(_hour);
}

int TimeInput::minute() const {
	return Number(_minute);
}

QString TimeInput::valueCurrent() const {
	const auto result = QString("%1:%2"
		).arg(hour()
		).arg(minute(), 2, 10, QChar('0'));
	return ValidateTime(result).isValid() ? result : QString();
}

rpl::producer<QString> TimeInput::value() const {
	return _value.value();
}

rpl::producer<> TimeInput::submitRequests() const {
	return _submitRequests.events();
}

void TimeInput::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto &_st = st::scheduleDateField;
	const auto height = _st.heightMin;
	if (_st.border) {
		p.fillRect(0, height - _st.border, width(), _st.border, _st.borderFg);
	}
	auto errorDegree = _a_error.value(_error ? 1. : 0.);
	auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	auto borderShownDegree = _a_borderShown.value(1.);
	auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = snap(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}
}

template <typename Widget>
bool TimeInput::insideSeparator(QPoint position, const Widget &widget) const {
	const auto x = position.x();
	const auto y = position.y();
	return (x >= widget->x() && x < widget->x() + widget->width())
		&& (y >= _hour->y() && y < _hour->y() + _hour->height());
}

void TimeInput::mouseMoveEvent(QMouseEvent *e) {
	const auto cursor = insideSeparator(e->pos(), _separator1)
		? style::cur_text
		: style::cur_default;
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void TimeInput::mousePressEvent(QMouseEvent *e) {
	const auto x = e->pos().x();
	const auto focus1 = [&] {
		if (_hour->getLastText().size() > 1) {
			_minute->setFocus();
		} else {
			_hour->setFocus();
		}
	};
	if (insideSeparator(e->pos(), _separator1)) {
		focus1();
		_borderAnimationStart = x - _hour->x();
	}
}

int TimeInput::resizeGetHeight(int width) {
	const auto &_st = st::scheduleTimeField;
	const auto &font = _st.placeholderFont;
	const auto addToWidth = st::scheduleTimeSeparatorPadding.left();
	const auto hourWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(QString("23"))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	const auto minuteWidth = _st.textMargins.left()
		+ _st.placeholderMargins.left()
		+ font->width(QString("59"))
		+ _st.placeholderMargins.right()
		+ _st.textMargins.right()
		+ addToWidth;
	const auto full = hourWidth
		- addToWidth
		+ _separator1->width()
		+ minuteWidth
		- addToWidth;
	auto left = (width - full) / 2;
	auto top = 0;
	_hour->setGeometry(left, top, hourWidth, _hour->height());
	left += hourWidth - addToWidth;
	_separator1->resizeToNaturalWidth(width);
	_separator1->move(left, top);
	left += _separator1->width();
	_minute->setGeometry(left, top, minuteWidth, _minute->height());
	return st::scheduleDateField.heightMin;
}

void TimeInput::showError() {
	setErrorShown(true);
	if (!_focused) {
		setInnerFocus();
	}
}

void TimeInput::setInnerFocus() {
	if (hour()) {
		_minute->setFocus();
	} else {
		_hour->setFocus();
	}
}

void TimeInput::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start(
			[=] { update(); },
			_error ? 0. : 1.,
			_error ? 1. : 0.,
			st::scheduleDateField.duration);
		startBorderAnimation();
	}
}

void TimeInput::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start(
			[=] { update(); },
			_focused ? 0. : 1.,
			_focused ? 1. : 0.,
			st::scheduleDateField.duration);
		startBorderAnimation();
	}
}

void TimeInput::finishInnerAnimating() {
	_hour->finishAnimating();
	_minute->finishAnimating();
	_a_borderOpacity.stop();
	_a_borderShown.stop();
	_a_error.stop();
}

void TimeInput::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		const auto duration = st::scheduleDateField.duration;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([=] { update(); }, 0., 1., duration);
			} else {
				_a_borderShown.start([=] { update(); }, 0., 1., duration);
			}
		} else {
			_a_borderOpacity.start([=] { update(); }, 1., 0., duration);
		}
	}
}

void FillSendUntilOnlineMenu(
		not_null<Ui::IconButton*> button,
		Fn<void()> callback) {
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	button->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(button);
		(*menu)->addAction(
			tr::lng_scheduled_send_until_online(tr::now),
			std::move(callback));
		(*menu)->popup(QCursor::pos());
		return true;
	});
}

} // namespace

TimeId DefaultScheduleTime() {
	return base::unixtime::now() + 600;
}

bool CanScheduleUntilOnline(not_null<PeerData*> peer) {
	return !peer->isSelf()
	&& peer->isUser()
	&& !peer->asUser()->isBot()
	&& (peer->asUser()->onlineTill > 0);
}

void ScheduleBox(
		not_null<Ui::GenericBox*> box,
		SendMenu::Type type,
		Fn<void(Api::SendOptions)> done,
		TimeId time) {
	box->setTitle((type == SendMenu::Type::Reminder)
		? tr::lng_remind_title()
		: tr::lng_schedule_title());
	box->setWidth(st::boxWideWidth);

	const auto date = Ui::CreateChild<rpl::variable<QDate>>(
		box.get(),
		base::unixtime::parse(time).date());
	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::scheduleHeight));
	const auto dayInput = Ui::CreateChild<Ui::InputField>(
		content,
		st::scheduleDateField);
	const auto timeInput = Ui::CreateChild<TimeInput>(
		content,
		TimeString(time));
	const auto at = Ui::CreateChild<Ui::FlatLabel>(
		content,
		tr::lng_schedule_at(),
		st::scheduleAtLabel);

	date->value(
	) | rpl::start_with_next([=](QDate date) {
		dayInput->setText(DayString(date));
		timeInput->setFocusFast();
	}, dayInput->lifetime());

	const auto minDate = QDate::currentDate();
	const auto maxDate = minDate.addYears(1).addDays(-1);

	const auto &dayViewport = dayInput->rawTextEdit()->viewport();
	base::install_event_filter(dayViewport, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			const auto e = static_cast<QWheelEvent*>(event.get());
			const auto direction = ProcessWheelEvent(e);
			if (!direction) {
				return base::EventFilterResult::Continue;
			}
			const auto d = date->current().addDays(direction);
			*date = std::clamp(d, minDate, maxDate);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto paddings = width
			- at->width()
			- 2 * st::scheduleAtSkip
			- st::scheduleDateWidth
			- st::scheduleTimeWidth;
		const auto left = paddings / 2;
		dayInput->resizeToWidth(st::scheduleDateWidth);
		dayInput->moveToLeft(left, st::scheduleDateTop, width);
		at->moveToLeft(
			left + st::scheduleDateWidth + st::scheduleAtSkip,
			st::scheduleAtTop,
			width);
		timeInput->resizeToWidth(st::scheduleTimeWidth);
		timeInput->moveToLeft(
			width - left - st::scheduleTimeWidth,
			st::scheduleDateTop,
			width);
	}, content->lifetime());

	const auto calendar =
		content->lifetime().make_state<QPointer<CalendarBox>>();
	QObject::connect(dayInput, &Ui::InputField::focused, [=] {
		if (*calendar) {
			return;
		}
		const auto chosen = [=](QDate chosen) {
			*date = chosen;
			(*calendar)->closeBox();
		};
		const auto finalize = [=](not_null<CalendarBox*> box) {
			box->setMinDate(minDate);
			box->setMaxDate(maxDate);
		};
		*calendar = box->getDelegate()->show(Box<CalendarBox>(
			date->current(),
			date->current(),
			crl::guard(box, chosen),
			finalize));
		(*calendar)->boxClosing(
		) | rpl::start_with_next(crl::guard(timeInput, [=] {
			timeInput->setFocusFast();
		}), (*calendar)->lifetime());
	});

	const auto collect = [=] {
		const auto timeValue = timeInput->valueCurrent().split(':');
		if (timeValue.size() != 2) {
			timeInput->showError();
			return 0;
		}
		const auto time = QTime(timeValue[0].toInt(), timeValue[1].toInt());
		if (!time.isValid()) {
			timeInput->showError();
			return 0;
		}
		const auto result = base::unixtime::serialize(
			QDateTime(date->current(), time));
		if (result <= base::unixtime::now() + kMinimalSchedule) {
			timeInput->showError();
			return 0;
		}
		return result;
	};
	const auto save = [=](bool silent, bool untilOnline = false) {
		// Pro tip: Hold Ctrl key to send a silent scheduled message!
		auto ctrl =
			(QGuiApplication::keyboardModifiers() == Qt::ControlModifier);
		auto result = Api::SendOptions();
		result.silent = silent || ctrl;
		result.scheduled = untilOnline
			? Data::ScheduledMessages::kScheduledUntilOnlineTimestamp
			: collect();
		if (!result.scheduled) {
			return;
		}

		auto copy = done;
		box->closeBox();
		copy(result);
	};
	timeInput->submitRequests(
	) | rpl::start_with_next([=] {
		save(false);
	}, timeInput->lifetime());

	box->setFocusCallback([=] { timeInput->setFocusFast(); });
	const auto submit = box->addButton(tr::lng_schedule_button(), [=] {
		save(false);
	});
	SendMenu::SetupMenuAndShortcuts(
		submit.data(),
		[=] { return SendMenu::Type::SilentOnly; },
		[=] { save(true); },
		nullptr);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	if (type == SendMenu::Type::ScheduledToUser) {
		const auto sendUntilOnline = box->addTopButton(st::infoTopBarMenu);
		FillSendUntilOnlineMenu(
			sendUntilOnline.data(),
			[=] { save(false, true); });
	}

}

} // namespace HistoryView
