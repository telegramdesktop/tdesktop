/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/calendar_box.h"

#include "ui/widgets/buttons.h"
#include "ui/effects/ripple_animation.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

constexpr auto kDaysInWeek = 7;

} // namespace

class CalendarBox::Context {
public:
	Context(QDate month, QDate highlighted);

	void start() {
		_month.setForced(_month.value(), true);
	}

	void setBeginningButton(bool enabled);
	bool hasBeginningButton() const {
		return _beginningButton;
	}

	void setMinDate(QDate date);
	void setMaxDate(QDate date);

	int minDayIndex() const {
		return _minDayIndex;
	}
	int maxDayIndex() const {
		return _maxDayIndex;
	}

	void skipMonth(int skip);
	void showMonth(QDate month);

	int highlightedIndex() const {
		return _highlightedIndex;
	}
	int rowsCount() const {
		return _rowsCount;
	}
	int daysShift() const {
		return _daysShift;
	}
	int daysCount() const {
		return _daysCount;
	}
	bool isEnabled(int index) const {
		return (index >= _minDayIndex) && (index <= _maxDayIndex);
	}
	bool atBeginning() const {
		return _highlighted == _min;
	}

	const base::Variable<QDate> &month() {
		return _month;
	}

	QDate dateFromIndex(int index) const;
	QString labelFromIndex(int index) const;

private:
	void applyMonth(const QDate &month, bool forced = false);

	static int daysShiftForMonth(QDate month);
	static int rowsCountForMonth(QDate month);

	bool _beginningButton = false;

	base::Variable<QDate> _month;
	QDate _min, _max;
	QDate _highlighted;

	int _highlightedIndex = 0;
	int _minDayIndex = 0;
	int _maxDayIndex = 0;
	int _daysCount = 0;
	int _daysShift = 0;
	int _rowsCount = 0;

};

CalendarBox::Context::Context(QDate month, QDate highlighted) : _highlighted(highlighted) {
	showMonth(month);
}

void CalendarBox::Context::setBeginningButton(bool enabled) {
	_beginningButton = enabled;
}

void CalendarBox::Context::setMinDate(QDate date) {
	_min = date;
	applyMonth(_month.value(), true);
}

void CalendarBox::Context::setMaxDate(QDate date) {
	_max = date;
	applyMonth(_month.value(), true);
}

void CalendarBox::Context::showMonth(QDate month) {
	if (month.day() != 1) {
		month = QDate(month.year(), month.month(), 1);
	}
	applyMonth(month);
}

void CalendarBox::Context::applyMonth(const QDate &month, bool forced) {
	_daysCount = month.daysInMonth();
	_daysShift = daysShiftForMonth(month);
	_rowsCount = rowsCountForMonth(month);
	_highlightedIndex = month.daysTo(_highlighted);
	_minDayIndex = _min.isNull() ? INT_MIN : month.daysTo(_min);
	_maxDayIndex = _max.isNull() ? INT_MAX : month.daysTo(_max);
	if (forced) {
		_month.setForced(month, true);
	} else {
		_month.set(month, true);
	}
}

void CalendarBox::Context::skipMonth(int skip) {
	auto year = _month.value().year();
	auto month = _month.value().month();
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

int CalendarBox::Context::daysShiftForMonth(QDate month) {
	Assert(!month.isNull());
	constexpr auto kMaxRows = 6;
	auto inMonthIndex = month.day() - 1;
	auto inWeekIndex = month.dayOfWeek() - 1;
	return ((kMaxRows * kDaysInWeek) + inWeekIndex - inMonthIndex) % kDaysInWeek;
}

int CalendarBox::Context::rowsCountForMonth(QDate month) {
	Assert(!month.isNull());
	auto daysShift = daysShiftForMonth(month);
	auto daysCount = month.daysInMonth();
	auto cellsCount = daysShift + daysCount;
	auto result = (cellsCount / kDaysInWeek);
	if (cellsCount % kDaysInWeek) ++result;
	return result;
}

QDate CalendarBox::Context::dateFromIndex(int index) const {
	constexpr auto kMonthsCount = 12;
	auto month = _month.value().month();
	auto year = _month.value().year();
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

class CalendarBox::Inner : public TWidget, private base::Subscriber {
public:
	Inner(
		QWidget *parent,
		not_null<Context*> context,
		const style::CalendarSizes &st);

	int countHeight();
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
	void paintDayNames(Painter &p, QRect clip);
	void paintRows(Painter &p, QRect clip);

	const style::CalendarSizes &_st;
	not_null<Context*> _context;

	std::map<int, std::unique_ptr<RippleAnimation>> _ripples;

	Fn<void(QDate)> _dateChosenCallback;

	static constexpr auto kEmptySelection = -kDaysInWeek;
	int _selected = kEmptySelection;
	int _pressed = kEmptySelection;

};

CalendarBox::Inner::Inner(
	QWidget *parent,
	not_null<Context*> context,
	const style::CalendarSizes &st)
: TWidget(parent)
, _st(st)
, _context(context) {
	setMouseTracking(true);
	subscribe(context->month(), [this](QDate month) {
		monthChanged(month);
	});
}

void CalendarBox::Inner::monthChanged(QDate month) {
	setSelected(kEmptySelection);
	_ripples.clear();
	resizeToCurrent();
	update();
	SendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
}

void CalendarBox::Inner::resizeToCurrent() {
	resize(_st.width, countHeight());
}

void CalendarBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();

	paintDayNames(p, clip);
	paintRows(p, clip);
}

void CalendarBox::Inner::paintDayNames(Painter &p, QRect clip) {
	p.setFont(st::calendarDaysFont);
	p.setPen(st::calendarDaysFg);
	auto y = _st.padding.top();
	auto x = rowsLeft();
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

int CalendarBox::Inner::rowsLeft() const {
	return _st.padding.left();
}

int CalendarBox::Inner::rowsTop() const {
	return _st.padding.top() + _st.daysHeight;
}

void CalendarBox::Inner::paintRows(Painter &p, QRect clip) {
	p.setFont(st::calendarDaysFont);
	auto y = rowsTop();
	auto index = -_context->daysShift();
	auto highlightedIndex = _context->highlightedIndex();
	for (auto row = 0, rowsCount = _context->rowsCount(), daysCount = _context->daysCount()
		; row != rowsCount
		; ++row, y += _st.cellSize.height()) {
		auto x = rowsLeft();
		if (!myrtlrect(x, y, _st.cellSize.width() * kDaysInWeek, _st.cellSize.height()).intersects(clip)) {
			index += kDaysInWeek;
			continue;
		}
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
	setCursor((_selected == kEmptySelection)
		? style::cur_default
		: style::cur_pointer);
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

int CalendarBox::Inner::countHeight() {
	const auto innerHeight = _st.daysHeight
		+ _context->rowsCount() * _st.cellSize.height();
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

class CalendarBox::Title : public TWidget, private base::Subscriber {
public:
	Title(QWidget *parent, not_null<Context*> context)
	: TWidget(parent)
	, _context(context) {
		subscribe(_context->month(), [this](QDate date) { monthChanged(date); });
	}

protected:
	void paintEvent(QPaintEvent *e);

private:
	void monthChanged(QDate month);

	not_null<Context*> _context;

	QString _text;
	int _textWidth = 0;

};

void CalendarBox::Title::monthChanged(QDate month) {
	_text = langMonthOfYearFull(month.month(), month.year());
	_textWidth = st::calendarTitleFont->width(_text);
	update();
}

void CalendarBox::Title::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::calendarTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft((width() - _textWidth) / 2, (height() - st::calendarTitleFont->height) / 2, width(), _text, _textWidth);
}

CalendarBox::CalendarBox(
	QWidget*,
	QDate month,
	QDate highlighted,
	Fn<void(QDate date)> callback,
	FnMut<void(not_null<CalendarBox*>)> finalize)
: CalendarBox(
	nullptr,
	month,
	highlighted,
	std::move(callback),
	std::move(finalize),
	st::defaultCalendarSizes) {
}

CalendarBox::CalendarBox(
	QWidget*,
	QDate month,
	QDate highlighted,
	Fn<void(QDate date)> callback,
	FnMut<void(not_null<CalendarBox*>)> finalize,
	const style::CalendarSizes &st)
: _st(st)
, _context(std::make_unique<Context>(month, highlighted))
, _inner(this, _context.get(), _st)
, _title(this, _context.get())
, _previous(this, st::calendarPrevious)
, _next(this, st::calendarNext)
, _callback(std::move(callback))
, _finalize(std::move(finalize)) {
}

void CalendarBox::setMinDate(QDate date) {
	_context->setMinDate(date);
}

void CalendarBox::setMaxDate(QDate date) {
	_context->setMaxDate(date);
}

bool CalendarBox::hasBeginningButton() const {
	return _context->hasBeginningButton();
}

void CalendarBox::setBeginningButton(bool enabled) {
	_context->setBeginningButton(enabled);
}

void CalendarBox::prepare() {
	_previous->setClickedCallback([this] { goPreviousMonth(); });
	_next->setClickedCallback([this] { goNextMonth(); });

//	_inner = setInnerWidget(object_ptr<Inner>(this, _context.get()), st::calendarScroll, st::calendarTitleHeight);
	_inner->setDateChosenCallback(std::move(_callback));

	addButton(tr::lng_close(), [this] { closeBox(); });

	subscribe(_context->month(), [this](QDate month) { monthChanged(month); });

	_context->start();

	if (_finalize) {
		_finalize(this);
	}
	if (!_context->atBeginning() && hasBeginningButton()) {
		addLeftButton(tr::lng_calendar_beginning(), [this] { _inner->selectBeginning(); });
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
	}
}

void CalendarBox::goNextMonth() {
	if (isNextEnabled()) {
		_context->skipMonth(1);
	}
}

void CalendarBox::monthChanged(QDate month) {
	setDimensions(_st.width, st::calendarTitleHeight + _inner->countHeight());
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
	_title->setGeometryToLeft(_previous->width(), 0, width() - _previous->width() - _next->width(), st::calendarTitleHeight);
	_inner->setGeometryToLeft(0, st::calendarTitleHeight, width(), height() - st::calendarTitleHeight);
	BoxContent::resizeEvent(e);
}

void CalendarBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Home) {
		_inner->selectBeginning();
	} else if (e->key() == Qt::Key_Left) {
		goPreviousMonth();
	} else if (e->key() == Qt::Key_Right) {
		goNextMonth();
	}
}

void CalendarBox::wheelEvent(QWheelEvent *e) {
	// Only a mouse wheel is accepted.
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
	const auto delta = e->angleDelta().y();
	if (std::abs(delta) != step) {
		return;
	}

	if (delta < 0) {
		goPreviousMonth();
	} else {
		goNextMonth();
	}
}

CalendarBox::~CalendarBox() = default;

} // namespace Ui
