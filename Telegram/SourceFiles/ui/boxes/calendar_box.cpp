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
#include "ui/chat/chat_style.h"
#include "ui/ui_utility.h"
#include "ui/painter.h"
#include "ui/cached_round_corners.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

#include <QtCore/QLocale>

namespace Ui {
namespace {

constexpr auto kDaysInWeek = 7;
constexpr auto kTooltipDelay = crl::time(1000);
constexpr auto kJumpDelay = 2 * crl::time(1000);

} // namespace

class CalendarBox::Context {
public:
	Context(QDate month, QDate highlighted);

	void setAllowsSelection(bool allowsSelection);
	[[nodiscard]] bool allowsSelection() const {
		return _allowsSelection;
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

	[[nodiscard]] QDate month() const {
		return _month.current();
	}
	[[nodiscard]] rpl::producer<QDate> monthValue() const {
		return _month.value();
	}
	[[nodiscard]] int firstDayShift() const {
		return _firstDayShift;
	}

	[[nodiscard]] QDate dateFromIndex(int index) const;
	[[nodiscard]] QString labelFromIndex(int index) const;

	void toggleSelectionMode(bool enabled);
	[[nodiscard]] bool selectionMode() const;
	[[nodiscard]] rpl::producer<> selectionUpdates() const;
	[[nodiscard]] std::optional<int> selectedMin() const;
	[[nodiscard]] std::optional<int> selectedMax() const;

	void startSelection(int index);
	void updateSelection(int index);

private:
	struct Selection {
		QDate min;
		QDate max;
		int minIndex = 0;
		int maxIndex = 0;
	};
	void applyMonth(const QDate &month, bool forced = false);

	static int DaysShiftForMonth(QDate month, QDate min, int firstDayShift);
	static int RowsCountForMonth(
		QDate month,
		QDate min,
		QDate max,
		int firstDayShift);

	const int _firstDayShift = 0;
	bool _allowsSelection = false;

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

	Selection _selection;
	QDate _selectionStart;
	int _selectionStartIndex = 0;
	rpl::event_stream<> _selectionUpdates;
	bool _selectionMode = false;

};

CalendarBox::Context::Context(QDate month, QDate highlighted)
: _firstDayShift(static_cast<int>(QLocale().firstDayOfWeek())
	- static_cast<int>(Qt::Monday))
, _highlighted(highlighted) {
	showMonth(month);
}

void CalendarBox::Context::setAllowsSelection(bool allows) {
	_allowsSelection = allows;
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
	const auto was = _month.current();
	_daysCount = month.daysInMonth();
	_daysShift = DaysShiftForMonth(month, _min, _firstDayShift);
	_rowsCount = RowsCountForMonth(month, _min, _max, _firstDayShift);
	_highlightedIndex = month.daysTo(_highlighted);
	_minDayIndex = _min.isNull() ? INT_MIN : month.daysTo(_min);
	_maxDayIndex = _max.isNull() ? INT_MAX : month.daysTo(_max);
	const auto shift = was.isNull() ? 0 : month.daysTo(was);
	auto updated = false;
	const auto update = [&](const QDate &date, int &index) {
		if (shift && !date.isNull()) {
			index += shift;
		}
	};
	update(_selection.min, _selection.minIndex);
	update(_selection.max, _selection.maxIndex);
	update(_selectionStart, _selectionStartIndex);
	if (forced) {
		_month.force_assign(month);
	} else {
		_month = month;
	}
	if (updated) {
		_selectionUpdates.fire({});
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

int CalendarBox::Context::DaysShiftForMonth(
		QDate month,
		QDate min,
		int firstDayShift) {
	Expects(!month.isNull());

	constexpr auto kMaxRows = 6;
	const auto inMonthIndex = month.day() - 1;
	const auto inWeekIndex = month.dayOfWeek() - 1;
	const auto from = ((kMaxRows * kDaysInWeek) + inWeekIndex - inMonthIndex)
		% kDaysInWeek;
	if (min.isNull()) {
		min = month.addYears(-1);
	} else if (min >= month) {
		return from - firstDayShift;
	}
	if (min.day() != 1) {
		min = QDate(min.year(), min.month(), 1);
	}
	const auto add = min.daysTo(month) - inWeekIndex + (min.dayOfWeek() - 1);
	return from + add - firstDayShift;
}

int CalendarBox::Context::RowsCountForMonth(
		QDate month,
		QDate min,
		QDate max,
		int firstDayShift) {
	Expects(!month.isNull());

	const auto daysShift = DaysShiftForMonth(month, min, firstDayShift);
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

void CalendarBox::Context::toggleSelectionMode(bool enabled) {
	if (_selectionMode == enabled) {
		return;
	}
	_selectionMode = enabled;
	_selectionStart = {};
	_selection = {};
	_selectionUpdates.fire({});
}

bool CalendarBox::Context::selectionMode() const {
	return _selectionMode;
}

rpl::producer<> CalendarBox::Context::selectionUpdates() const {
	return _selectionUpdates.events();
}

std::optional<int> CalendarBox::Context::selectedMin() const {
	return _selection.min.isNull()
		? std::optional<int>()
		: _selection.minIndex;
}

std::optional<int> CalendarBox::Context::selectedMax() const {
	return _selection.max.isNull()
		? std::optional<int>()
		: _selection.maxIndex;
}

void CalendarBox::Context::startSelection(int index) {
	Expects(_selectionMode);

	if (!_selectionStart.isNull() && _selectionStartIndex == index) {
		return;
	}
	_selectionStartIndex = index;
	_selectionStart = dateFromIndex(index);
	updateSelection(index);
}

void CalendarBox::Context::updateSelection(int index) {
	Expects(_selectionMode);
	Expects(!_selectionStart.isNull());

	index = std::clamp(index, minDayIndex(), maxDayIndex());
	const auto start = _selectionStartIndex;
	const auto min = std::min(index, start);
	const auto max = std::max(index, start);
	if (!_selection.min.isNull()
		&& _selection.minIndex == min
		&& !_selection.max.isNull()
		&& _selection.maxIndex == max) {
		return;
	}
	_selection = Selection{
		.min = dateFromIndex(min),
		.max = dateFromIndex(max),
		.minIndex = min,
		.maxIndex = max,
	};
	_selectionUpdates.fire({});
}

class CalendarBox::Inner final : public RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Context*> context,
		const style::CalendarSizes &st,
		const style::CalendarColors &styleColors);

	[[nodiscard]] int countMaxHeight() const;
	void setDateChosenCallback(Fn<void(QDate)> callback);

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
	void paintRows(QPainter &p, QRect clip);

	const style::CalendarSizes &_st;
	const style::CalendarColors &_styleColors;
	const not_null<Context*> _context;
	bool _twoPressSelectionStarted = false;

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

class CalendarBox::FloatingDate final {
public:
	FloatingDate(QWidget *parent, not_null<Context*> context);

	[[nodiscard]] rpl::producer<int> widthValue() const;
	void move(int x, int y);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void paint();

	const not_null<Context*> _context;
	RpWidget _widget;
	CornersPixmaps _corners;
	QString _text;

};

CalendarBox::FloatingDate::FloatingDate(
	QWidget *parent,
	not_null<Context*> context)
: _context(context)
, _widget(parent)
, _corners(
	PrepareCornerPixmaps(
		HistoryServiceMsgRadius(),
		st::roundedBg)) {
	_context->monthValue(
	) | rpl::start_with_next([=](QDate month) {
		_text = langMonthOfYearFull(month.month(), month.year());
		const auto width = st::msgServiceFont->width(_text);
		const auto rect = QRect(0, 0, width, st::msgServiceFont->height);
		_widget.resize(rect.marginsAdded(st::msgServicePadding).size());
		_widget.update();
	}, _widget.lifetime());

	_widget.paintRequest(
	) | rpl::start_with_next([=] {
		paint();
	}, _widget.lifetime());

	_widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	_widget.show();
}

rpl::producer<int> CalendarBox::FloatingDate::widthValue() const {
	return _widget.widthValue();
}

void CalendarBox::FloatingDate::move(int x, int y) {
	_widget.move(x, y);
}

rpl::lifetime &CalendarBox::FloatingDate::lifetime() {
	return _widget.lifetime();
}

void CalendarBox::FloatingDate::paint() {
	auto p = QPainter(&_widget);

	FillRoundRect(p, _widget.rect(), st::roundedBg, _corners);

	p.setFont(st::msgServiceFont);
	p.setPen(st::roundedFg);
	p.drawText(
		st::msgServicePadding.left(),
		st::msgServicePadding.top() + st::msgServiceFont->ascent,
		_text);
}

CalendarBox::Inner::Inner(
	QWidget *parent,
	not_null<Context*> context,
	const style::CalendarSizes &st,
	const style::CalendarColors &styleColors)
: RpWidget(parent)
, _st(st)
, _styleColors(styleColors)
, _context(context) {
	setMouseTracking(true);

	context->monthValue(
	) | rpl::start_with_next([=](QDate month) {
		monthChanged(month);
	}, lifetime());

	context->selectionUpdates(
	) | rpl::start_with_next([=] {
		update();
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
	auto p = QPainter(this);

	auto clip = e->rect();

	paintRows(p, clip);
}

int CalendarBox::Inner::rowsLeft() const {
	return _st.padding.left();
}

int CalendarBox::Inner::rowsTop() const {
	return _st.padding.top();
}

void CalendarBox::Inner::paintRows(QPainter &p, QRect clip) {
	p.setFont(st::calendarDaysFont);
	auto y = rowsTop();
	auto index = -_context->daysShift();
	const auto selectionMode = _context->selectionMode();
	const auto impossible = index - 45;
	const auto selectedMin = _context->selectedMin().value_or(impossible);
	const auto selectedMax = _context->selectedMax().value_or(impossible);
	const auto highlightedIndex = selectionMode
		? impossible
		: _context->highlightedIndex();
	const auto daysCount = _context->daysCount();
	const auto rowsCount = _context->rowsCount();
	const auto rowHeight = _st.cellSize.height();
	const auto fromRow = std::max(clip.y() - y, 0) / rowHeight;
	const auto tillRow = std::min(
		(clip.y() + clip.height() + rowHeight - 1) / rowHeight,
		rowsCount);
	y += fromRow * rowHeight;
	index += fromRow * kDaysInWeek;
	const auto innerSkipLeft = (_st.cellSize.width() - _st.cellInner) / 2;
	const auto innerSkipTop = (_st.cellSize.height() - _st.cellInner) / 2;
	const auto fromCol = _context->firstDayShift();
	const auto toCol = fromCol + kDaysInWeek;
	for (auto row = fromRow; row != tillRow; ++row, y += rowHeight) {
		auto x = rowsLeft();
		const auto fromIndex = index;
		const auto tillIndex = (index + kDaysInWeek);
		const auto selectedFrom = std::max(fromIndex, selectedMin);
		const auto selectedTill = std::min(tillIndex, selectedMax + 1);
		const auto selectedInRow = (selectedTill - selectedFrom);
		if (selectedInRow > 0) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::activeButtonBg);
			p.drawRoundedRect(
				(x
					+ (selectedFrom - index) * _st.cellSize.width()
					+ innerSkipLeft
					- st::lineWidth),
				y + innerSkipTop - st::lineWidth,
				((selectedInRow - 1) * _st.cellSize.width()
					+ 2 * st::lineWidth
					+ _st.cellInner),
				_st.cellInner + 2 * st::lineWidth,
				(_st.cellInner / 2.) + st::lineWidth,
				(_st.cellInner / 2.) + st::lineWidth);
			p.setBrush(Qt::NoBrush);
		}
		for (auto col = fromCol; col != toCol; ++col, ++index, x += _st.cellSize.width()) {
			const auto rect = myrtlrect(x, y, _st.cellSize.width(), _st.cellSize.height());
			const auto selected = (index >= selectedMin) && (index <= selectedMax);
			const auto grayedOut = !selected && (index < 0 || index >= daysCount);
			const auto highlighted = (index == highlightedIndex);
			const auto enabled = _context->isEnabled(index);
			const auto innerLeft = x + innerSkipLeft;
			const auto innerTop = y + innerSkipTop;
			if (highlighted) {
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(grayedOut ? st::windowBgOver : st::dialogsBgActive);
				p.drawEllipse(myrtlrect(innerLeft, innerTop, _st.cellInner, _st.cellInner));
				p.setBrush(Qt::NoBrush);
			}
			const auto it = _ripples.find(index);
			if (it != _ripples.cend() && !selectionMode) {
				const auto colorOverride = (!highlighted
					? _styleColors.rippleColor
					: grayedOut
					? _styleColors.rippleGrayedOutColor
					: _styleColors.rippleColorHighlighted)->c;
				it->second->paint(p, innerLeft, innerTop, width(), &colorOverride);
				if (it->second->empty()) {
					_ripples.erase(it);
				}
			}
			p.setPen(selected
				? st::activeButtonFg
				: highlighted
				? (grayedOut
					? _styleColors.dayTextGrayedOutColor
					: st::dialogsNameFgActive)
				: enabled
				? (grayedOut
					? _styleColors.dayTextGrayedOutColor
					: _styleColors.dayTextColor)
				: st::windowSubTextFg);
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
	if (_pressed != kEmptySelection && _context->selectionMode()) {
		const auto row = (point.y() >= rowsTop())
			? (point.y() - rowsTop()) / size.height()
			: -1;
		const auto col = (point.y() < rowsTop())
			? 0
			: (point.x() >= rowsLeft())
			? std::min(
				(point.x() - rowsLeft()) / size.width(),
				kDaysInWeek - 1)
			: 0;
		const auto index = row * kDaysInWeek + col - _context->daysShift();
		_context->updateSelection(index);
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
			auto mask = RippleAnimation::EllipseMask(QSize(_st.cellInner, _st.cellInner));
			auto update = [this, cell] { rtlupdate(cell); };
			it = _ripples.emplace(_selected, std::make_unique<RippleAnimation>(st::defaultRippleAnimation, std::move(mask), std::move(update))).first;
		}
		auto ripplePosition = QPoint(cell.x() + (_st.cellSize.width() - _st.cellInner) / 2, cell.y() + (_st.cellSize.height() - _st.cellInner) / 2);
		it->second->add(e->pos() - ripplePosition);

		if (_context->selectionMode()) {
			if (_context->selectedMin().has_value()
				&& ((e->modifiers() & Qt::ShiftModifier)
					|| (_twoPressSelectionStarted
						&& (_context->selectedMin()
							== _context->selectedMax())))) {
				_context->updateSelection(_selected);
				_twoPressSelectionStarted = false;
			} else {
				_context->startSelection(_selected);
				_twoPressSelectionStarted = true;
			}
		}
	}
}

void CalendarBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(kEmptySelection);
	if (pressed != kEmptySelection
		&& pressed == _selected
		&& !_context->selectionMode()) {
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

CalendarBox::Inner::~Inner() = default;

class CalendarBox::Title final : public RpWidget {
public:
	Title(
		QWidget *parent,
		not_null<Context*> context,
		const style::CalendarSizes &st,
		const style::CalendarColors &styleColors);

protected:
	void paintEvent(QPaintEvent *e);

private:
	void setTextFromMonth(QDate month);
	void setText(QString text);
	void paintDayNames(Painter &p, QRect clip);

	const style::CalendarSizes &_st;
	const style::CalendarColors &_styleColors;
	const not_null<Context*> _context;

	QString _text;
	int _textWidth = 0;
	int _textLeft = 0;

};

CalendarBox::Title::Title(
	QWidget *parent,
	not_null<Context*> context,
	const style::CalendarSizes &st,
	const style::CalendarColors &styleColors)
: RpWidget(parent)
, _st(st)
, _styleColors(styleColors)
, _context(context) {
	const auto dayWidth = st::calendarDaysFont->width(langDayOfWeek(1));
	_textLeft = _st.padding.left() + (_st.cellSize.width() - dayWidth) / 2;

	_context->monthValue(
	) | rpl::filter([=] {
		return !_context->selectionMode();
	}) | rpl::start_with_next([=](QDate date) {
		setTextFromMonth(date);
	}, lifetime());

	_context->selectionUpdates(
	) | rpl::start_with_next([=] {
		if (!_context->selectionMode()) {
			setTextFromMonth(_context->month());
		} else if (!_context->selectedMin()) {
			setText(tr::lng_calendar_select_days(tr::now));
		} else {
			setText(tr::lng_calendar_days(
				tr::now,
				lt_count,
				(1 + *_context->selectedMax() - *_context->selectedMin())));
		}
	}, lifetime());
}

void CalendarBox::Title::setTextFromMonth(QDate month) {
	setText(langMonthOfYearFull(month.month(), month.year()));
}

void CalendarBox::Title::setText(QString text) {
	_text = std::move(text);
	_textWidth = st::calendarTitleFont->width(_text);
	update();
}

void CalendarBox::Title::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();

	p.setFont(st::calendarTitleFont);
	p.setPen(_styleColors.titleTextColor);
	p.drawTextLeft(
		_textLeft,
		(st::calendarTitleHeight - st::calendarTitleFont->height) / 2,
		width(),
		_text,
		_textWidth);

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
	const auto from = _context->firstDayShift();
	const auto to = from + kDaysInWeek;
	for (auto i = from; i != to; ++i, x += _st.cellSize.width()) {
		auto rect = myrtlrect(x, y, _st.cellSize.width(), _st.daysHeight);
		if (!rect.intersects(clip)) {
			continue;
		}
		p.drawText(rect, langDayOfWeek((i % 7) + 1), style::al_top);
	}
}

CalendarBox::CalendarBox(QWidget*, CalendarBoxArgs &&args)
: _st(args.st)
, _styleColors(args.stColors)
, _context(
	std::make_unique<Context>(args.month.value(), args.highlighted.value()))
, _scroll(std::make_unique<ScrollArea>(this, st::calendarScroll))
, _inner(_scroll->setOwnedWidget(object_ptr<Inner>(
	this,
	_context.get(),
	_st,
	_styleColors)))
, _title(this, _context.get(), _st, _styleColors)
, _previous(this, _styleColors.iconButtonPrevious)
, _next(this, _styleColors.iconButtonNext)
, _callback(std::move(args.callback.value()))
, _finalize(std::move(args.finalize))
, _jumpTimer([=] { jump(_jumpButton); })
, _selectionChanged(std::move(args.selectionChanged)) {
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_context->setAllowsSelection(args.allowsSelection);
	_context->setMinDate(args.minDate);
	_context->setMaxDate(args.maxDate);

	_scroll->scrolls(
	) | rpl::filter([=] {
		return _watchScroll;
	}) | rpl::start_with_next([=] {
		processScroll();
	}, lifetime());

	const auto setupJumps = [&](
			not_null<IconButton*> button,
			not_null<bool*> enabled) {
		button->events(
		) | rpl::filter([=] {
			return *enabled;
		}) | rpl::start_with_next([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::MouseMove
				&& !(static_cast<QMouseEvent*>(e.get())->buttons()
					& Qt::LeftButton)) {
				showJumpTooltip(button);
			} else if (type == QEvent::Leave) {
				Ui::Tooltip::Hide();
			} else if (type == QEvent::MouseButtonPress
				&& (static_cast<QMouseEvent*>(e.get())->button()
					== Qt::LeftButton)) {
				jumpAfterDelay(button);
			} else if (type == QEvent::MouseButtonRelease
				&& (static_cast<QMouseEvent*>(e.get())->button()
					== Qt::LeftButton)) {
				_jumpTimer.cancel();
			}
		}, lifetime());
	};
	setupJumps(_previous.data(), &_previousEnabled);
	setupJumps(_next.data(), &_nextEnabled);

	_context->selectionUpdates(
	) | rpl::start_with_next([=] {
		if (!_context->selectionMode()) {
			_floatingDate = nullptr;
		} else if (!_floatingDate) {
			_floatingDate = std::make_unique<FloatingDate>(
				this,
				_context.get());
			rpl::combine(
				_scroll->geometryValue(),
				_floatingDate->widthValue()
			) | rpl::start_with_next([=](QRect scroll, int width) {
				const auto shift = _st.daysHeight
					- _st.padding.top()
					- st::calendarDaysFont->height;
				_floatingDate->move(
					scroll.x() + (scroll.width() - width) / 2,
					scroll.y() - shift);
			}, _floatingDate->lifetime());
		}
	}, lifetime());
}

CalendarBox::~CalendarBox() = default;

void CalendarBox::toggleSelectionMode(bool enabled) {
	_context->toggleSelectionMode(enabled);
}

QDate CalendarBox::selectedFirstDate() const {
	const auto min = _context->selectedMin();
	return min.has_value() ? _context->dateFromIndex(*min) : QDate();
}

QDate CalendarBox::selectedLastDate() const {
	const auto max = _context->selectedMax();
	return max.has_value() ? _context->dateFromIndex(*max) : QDate();
}

void CalendarBox::showJumpTooltip(not_null<IconButton*> button) {
	_tooltipButton = button;
	Ui::Tooltip::Show(kTooltipDelay, this);
}

void CalendarBox::jumpAfterDelay(not_null<IconButton*> button) {
	_jumpButton = button;
	_jumpTimer.callOnce(kJumpDelay);
	Ui::Tooltip::Hide();
}

void CalendarBox::jump(QPointer<IconButton> button) {
	const auto jumpToIndex = [&](int index) {
		_watchScroll = false;
		_context->showMonth(_context->dateFromIndex(index));
		setExactScroll();
	};
	if (button == _previous.data() && _previousEnabled) {
		jumpToIndex(_context->minDayIndex());
	} else if (button == _next.data() && _nextEnabled) {
		jumpToIndex(_context->maxDayIndex());
	}
	_jumpButton = nullptr;
	_jumpTimer.cancel();
}

void CalendarBox::prepare() {
	_previous->setClickedCallback([=] { goPreviousMonth(); });
	_next->setClickedCallback([=] { goNextMonth(); });

	_inner->setDateChosenCallback(std::move(_callback));

	_context->monthValue(
	) | rpl::start_with_next([=](QDate month) {
		monthChanged(month);
	}, lifetime());
	setExactScroll();

	_context->selectionUpdates(
	) | rpl::start_with_next([=] {
		_selectionMode = _context->selectionMode();
		if (_selectionChanged) {
			const auto count = !_selectionMode
				? std::optional<int>()
				: !_context->selectedMin()
				? 0
				: (1 + *_context->selectedMax() - *_context->selectedMin());
			_selectionChanged(this, count);
		}
		if (!_selectionMode) {
			clearButtons();
			createButtons();
		}
	}, lifetime());
	createButtons();

	if (_finalize) {
		_finalize(this);
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
		_watchScroll = false;
		_context->skipMonth(-1);
		setExactScroll();
	}
}

void CalendarBox::goNextMonth() {
	if (isNextEnabled()) {
		_watchScroll = false;
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

void CalendarBox::createButtons() {
	if (!_context->allowsSelection()) {
		addButton(tr::lng_close(), [=] { closeBox(); });
	} else if (!_context->selectionMode()) {
		addButton(tr::lng_close(), [=] { closeBox(); });
		addLeftButton(tr::lng_calendar_select_days(), [=] {
			_context->toggleSelectionMode(true);
		});
	} else {
		addButton(tr::lng_cancel(), [=] {
			_context->toggleSelectionMode(false);
		});
	}
}

QString CalendarBox::tooltipText() const {
	if (_tooltipButton == _previous.data()) {
		return tr::lng_calendar_start_tip(tr::now);
	} else if (_tooltipButton == _next.data()) {
		return tr::lng_calendar_end_tip(tr::now);
	}
	return QString();
}

QPoint CalendarBox::tooltipPos() const {
	return QCursor::pos();
}

bool CalendarBox::tooltipWindowActive() const {
	return window()->isActiveWindow();
}

void CalendarBox::monthChanged(QDate month) {
	setDimensions(
		_st.width,
		st::calendarTitleHeight + _st.daysHeight + _inner->countMaxHeight());

	_previousEnabled = isPreviousEnabled();
	_previous->setIconOverride(_previousEnabled
		? nullptr
		: &_styleColors.iconButtonPreviousDisabled);
	_previous->setRippleColorOverride(_previousEnabled
		? nullptr
		: &_styleColors.iconButtonRippleColorDisabled);
	_previous->setPointerCursor(_previousEnabled);
	if (!_previousEnabled) {
		_previous->clearState();
	}
	_nextEnabled = isNextEnabled();
	_next->setIconOverride(_nextEnabled
		? nullptr
		: &_styleColors.iconButtonNextDisabled);
	_next->setRippleColorOverride(_nextEnabled
		? nullptr
		: &_styleColors.iconButtonRippleColorDisabled);
	_next->setPointerCursor(_nextEnabled);
	if (!_nextEnabled) {
		_next->clearState();
	}
}

void CalendarBox::resizeEvent(QResizeEvent *e) {
	const auto dayWidth = st::calendarDaysFont->width(langDayOfWeek(7));
	const auto skip = _st.padding.left()
		+ _st.cellSize.width() * (kDaysInWeek - 1)
		+ (_st.cellSize.width() - dayWidth) / 2
		+ dayWidth;
	const auto right = width() - skip;
	const auto shift = _next->width()
		- (_next->width() - st::calendarPrevious.icon.width()) / 2
		- st::calendarPrevious.icon.width();
	_next->moveToRight(right - shift, 0);
	_previous->moveToRight(right - shift + _next->width(), 0);
	const auto title = st::calendarTitleHeight + _st.daysHeight;
	_title->setGeometryToLeft(0, 0, width(), title);
	_scroll->setGeometryToLeft(0, title, width(), height() - title);
	BoxContent::resizeEvent(e);
}

void CalendarBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (_context->selectionMode()) {
			_context->toggleSelectionMode(false);
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Home) {
		jump(_previous.data());
	} else if (e->key() == Qt::Key_End) {
		jump(_next.data());
	} else if (e->key() == Qt::Key_Left
		|| e->key() == Qt::Key_Up
		|| e->key() == Qt::Key_PageUp) {
		goPreviousMonth();
	} else if (e->key() == Qt::Key_Right
		|| e->key() == Qt::Key_Down
		|| e->key() == Qt::Key_PageDown) {
		goNextMonth();
	}
}

} // namespace Ui
