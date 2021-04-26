/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/group_call_bar.h"

#include "ui/chat/group_call_userpics.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "styles/style_chat.h"
#include "styles/style_calls.h"
#include "styles/style_info.h" // st::topBarArrowPadding, like TopBarWidget.
#include "styles/palette.h"

#include <QtGui/QtEvents>

namespace Ui {

GroupCallScheduledLeft::GroupCallScheduledLeft(TimeId date)
: _date(date)
, _datePrecise(computePreciseDate())
, _timer([=] { update(); }) {
	update();
	base::unixtime::updates(
	) | rpl::start_with_next([=] {
		restart();
	}, _lifetime);
}

crl::time GroupCallScheduledLeft::computePreciseDate() const {
	return crl::now() + (_date - base::unixtime::now()) * crl::time(1000);
}

void GroupCallScheduledLeft::setDate(TimeId date) {
	if (_date == date) {
		return;
	}
	_date = date;
	restart();
}

void GroupCallScheduledLeft::restart() {
	_datePrecise = computePreciseDate();
	_timer.cancel();
	update();
}

rpl::producer<QString> GroupCallScheduledLeft::text(Negative negative) const {
	return (negative == Negative::Show)
		? _text.value()
		: _textNonNegative.value();
}

rpl::producer<bool> GroupCallScheduledLeft::late() const {
	return _late.value();
}

void GroupCallScheduledLeft::update() {
	const auto now = crl::now();
	const auto duration = (_datePrecise - now);
	const auto left = crl::time(std::round(std::abs(duration) / 1000.));
	const auto late = (duration < 0) && (left > 0);
	_late = late;
	constexpr auto kDay = 24 * 60 * 60;
	if (left >= kDay) {
		const auto days = (left / kDay);
		_textNonNegative = tr::lng_group_call_duration_days(
			tr::now,
			lt_count,
			days);
		_text = late
			? tr::lng_group_call_duration_days(tr::now, lt_count, -days)
			: _textNonNegative.current();
	} else {
		const auto hours = left / (60 * 60);
		const auto minutes = (left % (60 * 60)) / 60;
		const auto seconds = (left % 60);
		_textNonNegative = (hours > 0)
			? (u"%1:%2:%3"_q
				.arg(hours, 2, 10, QChar('0'))
				.arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0')))
			: (u"%1:%2"_q
				.arg(minutes, 2, 10, QChar('0'))
				.arg(seconds, 2, 10, QChar('0')));
		_text = (late ? QString(QChar(0x2212)) : QString())
			+ _textNonNegative.current();
	}
	if (left >= kDay) {
		_timer.callOnce((left % kDay) * crl::time(1000));
	} else {
		const auto fraction = (std::abs(duration) + 500) % 1000;
		if (fraction < 400 || fraction > 600) {
			const auto next = std::abs(duration) % 1000;
			_timer.callOnce((duration < 0) ? (1000 - next) : next);
		} else if (!_timer.isActive()) {
			_timer.callEach(1000);
		}
	}
}

GroupCallBar::GroupCallBar(
	not_null<QWidget*> parent,
	rpl::producer<GroupCallBarContent> content,
	rpl::producer<bool> &&hideBlobs)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget()))
, _userpics(std::make_unique<GroupCallUserpics>(
		st::historyGroupCallUserpics,
		std::move(hideBlobs),
		[=] { updateUserpics(); })) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	_wrap.entity()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.entity()).fillRect(clip, st::historyPinnedBg);
	}, lifetime());
	_wrap.setAttribute(Qt::WA_OpaquePaintEvent);

	auto copy = std::move(
		content
	) | rpl::start_spawning(_wrap.lifetime());

	rpl::duplicate(
		copy
	) | rpl::start_with_next([=](GroupCallBarContent &&content) {
		_content = content;
		_userpics->update(_content.users, !_wrap.isHidden());
		_inner->update();
		refreshScheduledProcess();
	}, lifetime());
	if (!_open && !_join) {
		refreshScheduledProcess();
	}

	std::move(
		copy
	) | rpl::map([=](const GroupCallBarContent &content) {
		return !content.shown;
	}) | rpl::start_with_next_done([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		}
	}, [=] {
		_forceHidden = true;
		_wrap.toggle(false, anim::type::normal);
	}, lifetime());

	setupInner();
}

GroupCallBar::~GroupCallBar() = default;

void GroupCallBar::refreshOpenBrush() {
	Expects(_open != nullptr);

	const auto width = _open->width();
	if (_openBrushForWidth == width) {
		return;
	}
	auto gradient = QLinearGradient(QPoint(width, 0), QPoint(0, 0));
	gradient.setStops(QGradientStops{
		{ 0.0, st::groupCallForceMutedBar1->c },
		{ .7, st::groupCallForceMutedBar2->c },
		{ 1.0, st::groupCallForceMutedBar3->c }
	});
	_openBrushOverride = QBrush(std::move(gradient));
	_openBrushForWidth = width;
	_open->setBrushOverride(_openBrushOverride);
}

void GroupCallBar::refreshScheduledProcess() {
	const auto date = _content.scheduleDate;
	if (!date) {
		if (_scheduledProcess) {
			_scheduledProcess = nullptr;
			_open = nullptr;
			_openBrushForWidth = 0;
		}
		if (!_join) {
			_join = std::make_unique<RoundButton>(
				_inner.get(),
				tr::lng_group_call_join(),
				st::groupCallTopBarJoin);
			setupRightButton(_join.get());
		}
	} else if (!_scheduledProcess) {
		_scheduledProcess = std::make_unique<GroupCallScheduledLeft>(date);
		_join = nullptr;
		_open = std::make_unique<RoundButton>(
			_inner.get(),
			_scheduledProcess->text(GroupCallScheduledLeft::Negative::Show),
			st::groupCallTopBarOpen);
		setupRightButton(_open.get());
		_open->widthValue(
		) | rpl::start_with_next([=] {
			refreshOpenBrush();
		}, _open->lifetime());
	} else {
		_scheduledProcess->setDate(date);
	}
}

void GroupCallBar::setupInner() {
	_inner->resize(0, st::historyReplyHeight);
	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		auto p = Painter(_inner);
		paint(p);
	}, _inner->lifetime());

	// Clicks.
	_inner->setCursor(style::cur_pointer);
	_inner->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress);
	}) | rpl::map([=] {
		return _inner->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return _inner->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos());
		});
	}) | rpl::flatten_latest(
	) | rpl::map([] {
		return rpl::empty_value();
	}) | rpl::start_to_stream(_barClicks, _inner->lifetime());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void GroupCallBar::setupRightButton(not_null<RoundButton*> button) {
	rpl::combine(
		_inner->widthValue(),
		button->widthValue()
	) | rpl::start_with_next([=](int outerWidth, int) {
		// Skip shadow of the bar above.
		const auto top = (st::historyReplyHeight
			- st::lineWidth
			- button->height()) / 2 + st::lineWidth;
		button->moveToRight(top, top, outerWidth);
	}, button->lifetime());

	button->clicks() | rpl::start_to_stream(_joinClicks, button->lifetime());
}

void GroupCallBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);

	const auto left = st::topBarArrowPadding.right();
	const auto titleTop = st::msgReplyPadding.top();
	const auto textTop = titleTop + st::msgServiceNameFont->height;
	const auto width = _inner->width();
	const auto &font = st::defaultMessageBar.title.font;
	p.setPen(st::defaultMessageBar.textFg);
	p.setFont(font);

	const auto available = (_join ? _join->x() : _open->x()) - left;
	const auto titleWidth = font->width(_content.title);
	p.drawTextLeft(
		left,
		titleTop,
		width,
		(!_content.scheduleDate
			? tr::lng_group_call_title(tr::now)
			: _content.title.isEmpty()
			? tr::lng_group_call_scheduled_title(tr::now)
			: (titleWidth > available)
			? font->elided(_content.title, available)
			: _content.title));
	p.setPen(st::historyStatusFg);
	p.setFont(st::defaultMessageBar.text.font);
	const auto when = [&] {
		if (!_content.scheduleDate) {
			return QString();
		}
		const auto parsed = base::unixtime::parse(_content.scheduleDate);
		const auto date = parsed.date();
		const auto time = parsed.time().toString(
			QLocale::system().timeFormat(QLocale::ShortFormat));
		const auto today = QDate::currentDate();
		if (date == today) {
			return tr::lng_group_call_starts_today(tr::now, lt_time, time);
		} else if (date == today.addDays(1)) {
			return tr::lng_group_call_starts_tomorrow(
				tr::now,
				lt_time,
				time);
		} else {
			return tr::lng_group_call_starts_date(
				tr::now,
				lt_date,
				langDayOfMonthFull(date),
				lt_time,
				time);
		}
	}();
	p.drawTextLeft(
		left,
		textTop,
		width,
		(_content.scheduleDate
			? (_content.title.isEmpty()
				? tr::lng_group_call_starts_short
				: tr::lng_group_call_starts)(tr::now, lt_when, when)
			: _content.count > 0
			? tr::lng_group_call_members(tr::now, lt_count, _content.count)
			: tr::lng_group_call_no_members(tr::now)));

	const auto size = st::historyGroupCallUserpics.size;
	// Skip shadow of the bar above.
	const auto top = (st::historyReplyHeight - st::lineWidth - size) / 2
		+ st::lineWidth;
	_userpics->paint(p, _inner->width() / 2, top, size);
}

void GroupCallBar::updateControlsGeometry(QRect wrapGeometry) {
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
}

void GroupCallBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void GroupCallBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void GroupCallBar::updateUserpics() {
	const auto widget = _wrap.entity();
	const auto middle = widget->width() / 2;
	const auto width = _userpics->maxWidth();
	widget->update(
		(middle - width / 2),
		0,
		width,
		widget->height());
}

void GroupCallBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void GroupCallBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void GroupCallBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void GroupCallBar::finishAnimating() {
	_wrap.finishAnimating();
}

void GroupCallBar::move(int x, int y) {
	_wrap.move(x, y);
}

void GroupCallBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
	_inner->resizeToWidth(width);
}

int GroupCallBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> GroupCallBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> GroupCallBar::barClicks() const {
	return _barClicks.events();
}

rpl::producer<> GroupCallBar::joinClicks() const {
	return _joinClicks.events() | rpl::to_empty;
}

} // namespace Ui
