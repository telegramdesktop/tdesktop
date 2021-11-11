/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/calendar_box.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/ripple_animation.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kDaysInWeek = 7;
constexpr auto kMaxDaysForScroll = kDaysInWeek * 1000;

} // namespace

class CalendarBox::Context {
public:
	Context(QDate month, QDate highlighted);

	void setBeginningButton(bool enabled);
	[[nodiscard]] bool hasBeginningButton() const {
		return _beginningButton;
	}

	void setMinDate(QDate date);
	void setMaxDate(QDate date);

	[[nodiscard]] int minDayIndex() const {
		return _minDayIndex;
	}
	[[nodiscard]] int maxDayIndex() const {
		return _maxDayIndex;
	}

	void skipMonth(int skip);
	void showMonth(QDate month);
	[[nodiscard]] bool showsMonthOf(QDate date) const;

	[[nodiscard]] int highlightedIndex() const {
		return _highlightedIndex;
	}
	[[nodiscard]] int rowsCount() const {
		return _rowsCount;
	}
	[[nodiscard]] int rowsCountMax() const {
		return 6;
	}
	[[nodiscard]] int daysShift() const {
		return _daysShift;
	}
	[[nodiscard]] int daysCount() const {
		return _daysCount;
	}
	[[nodiscard]] bool isEnabled(int index) const {
		return (index >= _minDayIndex) && (index <= _maxDayIndex);
	}
	[[nodiscard]] bool atBeginning() const {
		return _highlighted == _min;
	}

	[[nodiscard]] rpl::producer<QDate> monthValue() const {
		return _month.value();
	}

	QDate dateFromIndex(int index) const;
	QString labelFromIndex(int index) const;

private:
	void applyMonth(const QDate &month, bool forced = false);

	static int DaysShiftForMonth(QDate month, QDate min);
	static int RowsCountForMonth(QDate month, QDate min, QDate max);

	bool _beginningButton = false;

	rpl::variable<QDate> _month;
	QDate _min, _max;
	QDate _highlighted;
	Fn<QString(int)> _dayOfWeek;
	Fn<QString(int, int)> _monthOfYear;

	int _highlightedIndex = 0;
	int _minDayIndex = 0;
	int _maxDayIndex = 0;
	int _daysCount = 0;
	int _daysShift = 0;
	int _rowsCount = 0;

};

CalendarBox::Context::Context(QDate month, QDate highlighted)
: _highlighted(highlighted) {
	showMonth(month);
}

void CalendarBox::Context::setBeginningButton(bool enabled) {
	_beginningButton = enabled;
}

void CalendarBox::Context::setMinDate(QDate date) {
	_min = date;
	applyMonth(_month.current(), true);
}

void CalendarBox::Context::setMaxDate(QDate date) {
	_max = date;
	applyMonth(_month.current(), true);
}

void CalendarBox::Context::showMonth(QDate month) {
	if (month.day() != 1) {
		month = QDate(month.year(), month.month(), 1);
	}
	applyMonth(month);
}

bool CalendarBox::Context::showsMonthOf(QDate date) const {
	const auto shown = _month.current();
	return (shown.year() == date.year()) && (shown.month() == date.month());
}

void CalendarBox::Context::applyMonth(const QDate &month, bool forced) {
	_daysCount = month.daysInMonth();
	_daysShift = DaysShiftForMonth(month, _min);
	_rowsCount = RowsCountForMonth(month, _min, _max);
	_highlightedIndex = month.daysTo(_highlighted);
	_minDayIndex = _min.isNull() ? INT_MIN : month.daysTo(_min);
	_maxDayIndex = _max.isNull() ? INT_MAX : month.daysTo(_max);
	if (forced) {
		_month.force_assign(month);
	} else {
		_month = month;
	}
}

void CalendarBox::Context::skipMonth(int skip) {
	auto year = _month.current().year();
	auto month = _month.current().month();
	month += skip;
	while (month < 1) {
		--year;
		month += 12;
	}
	while (month > 12) {
		++year;
		month -= 12;
	}
	showMonth(QDate(year, month, 1));
}

int CalendarBox::Context::DaysShiftForMonth(QDate month, QDate min) {
	Expects(!month.isNull());

	constexpr auto kMaxRows = 6;
	const auto inMonthIndex = month.day() - 1;
	const auto inWeekIndex = month.dayOfWeek() - 1;
	const auto from = ((kMaxRows * kDaysInWeek) + inWeekIndex - inMonthIndex)
		% kDaysInWeek;
	if (min.isNull()) {
		min = month.addYears(-1);
	} else if (min >= month) {
		return from;
	}
	if (min.day() != 1) {
		min = QDate(min.year(), min.month(), 1);
	}
	const auto add = min.daysTo(month) - inWeekIndex + (min.dayOfWeek() - 1);
	return from + add;
}

int CalendarBox::Context::RowsCountForMonth(
		QDate month,
		QDate min,
		QDate max) {
	Expects(!month.isNull());

	const auto daysShift = DaysShiftForMonth(month, min);
	const auto daysCount = month.daysInMonth();
	const auto cellsCount = daysShift + daysCount;
	auto result = (cellsCount / kDaysInWeek);
	if (cellsCount % kDaysInWeek) {
		++result;
	}
	if (max.isNull()) {
		max = month.addYears(1);
	}
	if (max < month.addMonths(1)) {
		return result;
	}
	if (max.day() != 1) {
		max = QDate(max.year(), max.month(), 1);
	}
	max = max.addMonths(1);
	max = max.addDays(1 - max.dayOfWeek());
	const auto cellsFull = daysShift + (month.day() - 1) + month.daysTo(max);
	return cellsFull / kDaysInWeek;
}

QDate CalendarBox::Context::dateFromIndex(int index) const {
	constexpr auto kMonthsCount = 12;
	auto month = _month.current().month();
	auto year = _month.current().year();
	while (index < 0) {
		if (!--month) {
			month += kMonthsCount;
			--year;
		}
		index += QDate(year, month, 1).daysInMonth();
	}
	for (auto maxIndex = QDate(year, month, 1).daysInMonth(); index >= maxIndex; maxIndex = QDate(year, month, 1).daysInMonth()) {
		index -= maxIndex;
		if (month++ == kMonthsCount) {
			month -= kMonthsCount;
			++year;
		}
	}
	return QDate(year, month, index + 1);
}

QString CalendarBox::Context::labelFromIndex(int index) const {
	auto day = [this, index] {
		if (index >= 0 && index < daysCount()) {
			return index + 1;
		}
		return dateFromIndex(index).day();
	};
	return QString::number(day());
}

class CalendarBox::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Context*> context,
		const style::CalendarSizes &st);

	[[nodiscard]] int countMaxHeight() const;
	void setDateChosenCallback(Fn<void(QDate)> callback);
	void selectBeginning();

	~Inner();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void monthChanged(QDate month);
	void setSelected(int selected);
	void setPressed(int pressed);

	int rowsLeft() const;
	int rowsTop() const;
	void resizeToCurrent();
	void paintRows(Painter &p, QRect clip);

	const style::CalendarSizes &_st;
	const not_null<Context*> _context;

	std::map<int, std::unique_ptr<RippleAnimation>> _ripples;

	Fn<void(QDate)> _dateChosenCallback;

	static constexpr auto kEmptySelection = INT_MIN / 2;
	int _selected = kEmptySelection;
	int _pressed = kEmptySelection;
	bool _pointerCursor = false;
	bool _cursorSetWithoutMouseMove = false;

	QPoint _lastGlobalPosition;
	bool _mouseMoved = false;

};

CalendarBox::Inner::Inner(
	QWidget *parent,
	not_null<Context*> context,
	const style::CalendarSizes &st)
: RpWidget(parent)
, _st(st)
, _context(context) {
	setMouseTracking(true);

	context->monthValue(
	) | rpl::start_with_next([=](QDate month) {
		monthChanged(month);
	}, lifetime());
}

void CalendarBox::Inner::monthChanged(QDate month) {
	setSelected(kEmptySelection);
	_ripples.clear();
	resizeToCurrent();
	update();
	SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
}

void CalendarBox::Inner::resizeToCurrent() {
	const auto height = _context->rowsCount() * _st.cellSize.height();
	resize(_st.width, _st.padding.top() + height + _st.padding.bottom());
}

void CalendarBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();

	paintRows(p, clip);
}

int CalendarBox::Inner::rowsLeft() const {
	return _st.padding.left();
}

int CalendarBox::Inner::rowsTop() const {
	return _st.padding.top();
}

void CalendarBox::Inner::paintRows(Painter &p, QRect clip) {
	p.setFont(st::calendarDaysFont);
	auto y = rowsTop();
	auto index = -_context->daysShift();
	auto highlightedIndex = _context->highlightedIndex();
	const auto daysCount = _context->daysCount();
	const auto rowsCount = _context->rowsCount();
	const auto rowHeight = _st.cellSize.height();
	const auto fromRow = std::max(clip.y() - y, 0) / rowHeight;
	const auto tillRow = std::min(
		(clip.y() + clip.height() + rowHeight - 1) / rowHeight,
		rowsCount);
	y += fromRow * rowHeight;
	index += fromRow * kDaysInWeek;
	for (auto row = fromRow; row != tillRow; ++row, y += rowHeight) {
		auto x = rowsLeft();
		for (auto col = 0; col != kDaysInWeek; ++col, ++index, x += _st.cellSize.width()) {
			auto rect = myrtlrect(x, y, _st.cellSize.width(), _st.cellSize.height());
			auto grayedOut = (index < 0 || index >= daysCount || !rect.intersects(clip));
			auto highlighted = (index == highlightedIndex);
			auto enabled = _context->isEnabled(index);
			auto innerLeft = x + (_st.cellSize.width() - _st.cellInner) / 2;
			auto innerTop = y + (_st.cellSize.height() - _st.cellInner) / 2;
			if (highlighted) {
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(grayedOut ? st::windowBgOver : st::dialogsBgActive);
				p.drawEllipse(myrtlrect(innerLeft, innerTop, _st.cellInner, _st.cellInner));
				p.setBrush(Qt::NoBrush);
			}
			auto it = _ripples.find(index);
			if (it != _ripples.cend()) {
				auto colorOverride = [highlighted, grayedOut] {
					if (highlighted) {
						return grayedOut ? st::windowBgRipple : st::dialogsRippleBgActive;
					}
					return st::windowBgOver;
				};
				it->second->paint(p, innerLeft, innerTop, width(), &(colorOverride()->c));
				if (it->second->empty()) {
					_ripples.erase(it);
				}
			}
			if (highlighted) {
				p.setPen(grayedOut ? st::windowSubTextFg : st::dialogsNameFgActive);
			} else if (enabled) {
				p.setPen(grayedOut ? st::windowSubTextFg : st::boxTextFg);
			} else {
				p.setPen(st::windowSubTextFg);
			}
			p.drawText(rect, _context->labelFromIndex(index), style::al_center);
		}
	}
}

void CalendarBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	_mouseMoved = (_lastGlobalPosition != globalPosition);
	_lastGlobalPosition = globalPosition;

	const auto size = _st.cellSize;
	const auto point = e->pos();
	const auto inner = QRect(
		rowsLeft(),
		rowsTop(),
		kDaysInWeek * size.width(),
		_context->rowsCount() * size.height());
	if (inner.contains(point)) {
		const auto row = (point.y() - rowsTop()) / size.height();
		const auto col = (point.x() - rowsLeft()) / size.width();
		const auto index = row * kDaysInWeek + col - _context->daysShift();
		setSelected(index);
	} else {
		setSelected(kEmptySelection);
	}
}

void CalendarBox::Inner::setSelected(int selected) {
	if (selected != kEmptySelection && !_context->isEnabled(selected)) {
		selected = kEmptySelection;
	}
	_selected = selected;
	const auto pointer = (_selected != kEmptySelection);
	const auto force = (_mouseMoved && _cursorSetWithoutMouseMove);
	if (_pointerCursor != pointer || force) {
		if (force) {
			// Workaround some strange bug. When I call setCursor while
			// scrolling by touchpad the new cursor is not applied and
			// then it is not applied until it is changed.
			setCursor(pointer ? style::cur_default : style::cur_pointer);
		}
		setCursor(pointer ? style::cur_pointer : style::cur_default);
		_cursorSetWithoutMouseMove = !_mouseMoved;
		_pointerCursor = pointer;
	}
	_mouseMoved = false;
}

void CalendarBox::Inner::mousePressEvent(QMouseEvent *e) {
	setPressed(_selected);
	if (_selected != kEmptySelection) {
		auto index = _selected + _context->daysShift();
		Assert(index >= 0);

		auto row = index / kDaysInWeek;
		auto col = index % kDaysInWeek;
		auto cell = QRect(rowsLeft() + col * _st.cellSize.width(), rowsTop() + row * _st.cellSize.height(), _st.cellSize.width(), _st.cellSize.height());
		auto it = _ripples.find(_selected);
		if (it == _ripples.cend()) {
			auto mask = RippleAnimation::ellipseMask(QSize(_st.cellInner, _st.cellInner));
			auto update = [this, cell] { rtlupdate(cell); };
			it = _ripples.emplace(_selected, std::make_unique<RippleAnimation>(st::defaultRippleAnimation, std::move(mask), std::move(update))).first;
		}
		auto ripplePosition = QPoint(cell.x() + (_st.cellSize.width() - _st.cellInner) / 2, cell.y() + (_st.cellSize.height() - _st.cellInner) / 2);
		it->second->add(e->pos() - ripplePosition);
	}
}

void CalendarBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(kEmptySelection);
	if (pressed != kEmptySelection && pressed == _selected) {
		crl::on_main(this, [=] {
			const auto onstack = _dateChosenCallback;
			onstack(_context->dateFromIndex(pressed));
		});
	}
}

void CalendarBox::Inner::setPressed(int pressed) {
	if (_pressed != pressed) {
		if (_pressed != kEmptySelection) {
			auto it = _ripples.find(_pressed);
			if (it != _ripples.cend()) {
				it->second->lastStop();
			}
		}
		_pressed = pressed;
	}
}

int CalendarBox::Inner::countMaxHeight() const {
	const auto innerHeight = _context->rowsCountMax() * _st.cellSize.height();
	return _st.padding.top()
		+ innerHeight
		+ _st.padding.bottom();
}

void CalendarBox::Inner::setDateChosenCallback(Fn<void(QDate)> callback) {
	_dateChosenCallback = std::move(callback);
}

void CalendarBox::Inner::selectBeginning() {
	_dateChosenCallback(_context->dateFromIndex(_context->minDayIndex()));
}

CalendarBox::Inner::~Inner() = default;

class CalendarBox::Title final : public RpWidget {
public:
	Title(
		QWidget *parent,
		not_null<Context*> context,
		const style::CalendarSizes &st);

protected:
	void paintEvent(QPaintEvent *e);

private:
	void monthChanged(QDate month);
	void paintDayNames(Painter &p, QRect clip);

	const style::CalendarSizes &_st;
	const not_null<Context*> _context;

	QString _text;
	int _textWidth = 0;

};

CalendarBox::Title::Title(
	QWidget *parent,
	not_null<Context*> context,
	const style::CalendarSizes &st)
: RpWidget(parent)
, _st(st)
, _context(context) {
	_context->monthValue(
	) | rpl::start_with_next([=](QDate date) {
		monthChanged(date);
	}, lifetime());
}

void CalendarBox::Title::monthChanged(QDate month) {
	_text = langMonthOfYearFull(month.month(), month.year());
	_textWidth = st::calendarTitleFont->width(_text);
	update();
}

void CalendarBox::Title::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();

	p.setFont(st::calendarTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft((width() - _textWidth) / 2, (st::calendarTitleHeight - st::calendarTitleFont->height) / 2, width(), _text, _textWidth);

	paintDayNames(p, clip);
}

void CalendarBox::Title::paintDayNames(Painter &p, QRect clip) {
	p.setFont(st::calendarDaysFont);
	p.setPen(st::calendarDaysFg);
	auto y = st::calendarTitleHeight + _st.padding.top();
	auto x = _st.padding.left();
	if (!myrtlrect(x, y, _st.cellSize.width() * kDaysInWeek, _st.daysHeight).intersects(clip)) {
		return;
	}
	for (auto i = 0; i != kDaysInWeek; ++i, x += _st.cellSize.width()) {
		auto rect = myrtlrect(x, y, _st.cellSize.width(), _st.daysHeight);
		if (!rect.intersects(clip)) {
			continue;
		}
		p.drawText(rect, langDayOfWeek(i + 1), style::al_top);
	}
}

CalendarBox::CalendarBox(QWidget*, CalendarBoxArgs &&args)
: _st(args.st)
, _context(
	std::make_unique<Context>(args.month.value(), args.highlighted.value()))
, _scroll(std::make_unique<ScrollArea>(this, st::calendarScroll))
, _inner(
	_scroll->setOwnedWidget(object_ptr<Inner>(this, _context.get(), _st)))
, _title(this, _context.get(), _st)
, _previous(this, st::calendarPrevious)
, _next(this, st::calendarNext)
, _callback(std::move(args.callback.value()))
, _finalize(std::move(args.finalize)) {
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_context->setBeginningButton(args.hasBeginningButton);
	_context->setMinDate(args.minDate);
	_context->setMaxDate(args.maxDate);

	_scroll->scrolls(
	) | rpl::filter([=] {
		return _watchScroll;
	}) | rpl::start_with_next([=] {
		processScroll();
	}, lifetime());
}

void CalendarBox::prepare() {
	_previous->setClickedCallback([=] { goPreviousMonth(); });
	_next->setClickedCallback([=] { goNextMonth(); });

	_inner->setDateChosenCallback(std::move(_callback));

	addButton(tr::lng_close(), [=] { closeBox(); });

	_context->monthValue(
	) | rpl::start_with_next([=](QDate month) {
		monthChanged(month);
	}, lifetime());
	setExactScroll();

	if (_finalize) {
		_finalize(this);
	}
	if (!_context->atBeginning() && _context->hasBeginningButton()) {
		addLeftButton(tr::lng_calendar_beginning(), [=] {
			_inner->selectBeginning();
		});
	}
}

bool CalendarBox::isPreviousEnabled() const {
	return (_context->minDayIndex() < 0);
}

bool CalendarBox::isNextEnabled() const {
	return (_context->maxDayIndex() >= _context->daysCount());
}

void CalendarBox::goPreviousMonth() {
	if (isPreviousEnabled()) {
		_context->skipMonth(-1);
		setExactScroll();
	}
}

void CalendarBox::goNextMonth() {
	if (isNextEnabled()) {
		_context->skipMonth(1);
		setExactScroll();
	}
}

void CalendarBox::setExactScroll() {
	const auto top = _st.padding.top()
		+ (_context->daysShift() / kDaysInWeek) * _st.cellSize.height();
	_scroll->scrollToY(top);
	_watchScroll = true;
}

void CalendarBox::processScroll() {
	const auto wasTop = _scroll->scrollTop();
	const auto wasShift = _context->daysShift();
	const auto point = _scroll->rect().center() + QPoint(0, wasTop);
	const auto row = (point.y() - _st.padding.top()) / _st.cellSize.height();
	const auto col = (point.x() - _st.padding.left()) / _st.cellSize.width();
	const auto index = row * kDaysInWeek + col;
	const auto date = _context->dateFromIndex(index - wasShift);
	if (_context->showsMonthOf(date)) {
		return;
	}
	const auto wasFirst = _context->dateFromIndex(-wasShift);
	const auto month = QDate(date.year(), date.month(), 1);
	_watchScroll = false;
	_context->showMonth(month);
	const auto nowShift = _context->daysShift();
	const auto nowFirst = _context->dateFromIndex(-nowShift);
	const auto delta = nowFirst.daysTo(wasFirst) / kDaysInWeek;
	_scroll->scrollToY(wasTop + delta * _st.cellSize.height());
	_watchScroll = true;
}

void CalendarBox::monthChanged(QDate month) {
	setDimensions(_st.width, st::calendarTitleHeight + _st.daysHeight + _inner->countMaxHeight());
	auto previousEnabled = isPreviousEnabled();
	_previous->setIconOverride(previousEnabled ? nullptr : &st::calendarPreviousDisabled);
	_previous->setRippleColorOverride(previousEnabled ? nullptr : &st::boxBg);
	_previous->setCursor(previousEnabled ? style::cur_pointer : style::cur_default);
	auto nextEnabled = isNextEnabled();
	_next->setIconOverride(nextEnabled ? nullptr : &st::calendarNextDisabled);
	_next->setRippleColorOverride(nextEnabled ? nullptr : &st::boxBg);
	_next->setCursor(nextEnabled ? style::cur_pointer : style::cur_default);
}

void CalendarBox::resizeEvent(QResizeEvent *e) {
	_previous->moveToLeft(0, 0);
	_next->moveToRight(0, 0);
	const auto title = st::calendarTitleHeight + _st.daysHeight;
	_title->setGeometryToLeft(0, 0, width(), title);
	_scroll->setGeometryToLeft(0, title, width(), height() - title);
	BoxContent::resizeEvent(e);
}

void CalendarBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Home) {
		_inner->selectBeginning();
	} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) {
		goPreviousMonth();
	} else if (e->key() == Qt::Key_Right || e->key() == Qt::Key_Down) {
		goNextMonth();
	}
}

void CalendarBox::wheelEvent(QWheelEvent *e) {
	const auto direction = Ui::WheelDirection(e);

	if (direction < 0) {
		goPreviousMonth();
	} else if (direction > 0) {
		goNextMonth();
	}
}

CalendarBox::~CalendarBox() = default;

} // namespace Ui
