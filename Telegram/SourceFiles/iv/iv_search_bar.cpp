/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_search_bar.h"

#include "dialogs/ui/dialogs_pill.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/painter.h"

#include "styles/palette.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>

namespace Iv {

SearchBar::SearchBar(
	not_null<QWidget*> parent,
	rpl::producer<int> width,
	SearchBarMode mode)
: _mode(mode)
, _wrap(parent, object_ptr<Ui::RpWidget>(parent.get())) {
	if (_mode == SearchBarMode::EditorPill) {
		_wrap.setDirectionUp(true);
		_pillShadow.emplace(st::ivEditorPillShadow);
		_pillShadowMargins = _pillShadow->extend();
	} else {
		_shadow = std::make_unique<Ui::PlainShadow>(parent.get());
	}
	setup(std::move(width));
	_wrap.hide(anim::type::instant);
	if (_shadow) {
		_shadow->hide();
	}
}

void SearchBar::setup(rpl::producer<int> width) {
	const auto inner = _wrap.entity();
	const auto pill = (_mode == SearchBarMode::EditorPill);
	inner->resize(inner->width(), pill
		? (st::ivEditorToolbarPadding.top()
			+ _pillShadowMargins.top()
			+ pillHeight()
			+ _pillShadowMargins.bottom())
		: st::ivSearchBarHeight);

	const auto &selectSt = pill
		? st::ivEditorSearchMultiSelect
		: st::searchInChatMultiSelect;
	_select = Ui::CreateChild<Ui::MultiSelect>(
		inner,
		selectSt,
		tr::lng_dlg_filter());
	_select->setCancelButtonShown(false);
	_counter = Ui::CreateChild<Ui::FlatLabel>(
		inner,
		QString(),
		st::ivSearchCounterLabel);
	_counter->setAttribute(Qt::WA_TransparentForMouseEvents);
	_counter->hide();
	const auto &upSt = pill
		? st::ivEditorSearchPrevious
		: st::calendarPrevious;
	const auto &downSt = pill ? st::ivEditorSearchNext : st::calendarNext;
	const auto &closeSt = pill
		? st::ivEditorSearchCancel
		: st::defaultMultiSelectSearchCancel;
	_up = Ui::CreateChild<Ui::IconButton>(inner, upSt);
	_down = Ui::CreateChild<Ui::IconButton>(inner, downSt);
	_close = Ui::CreateChild<Ui::CrossButton>(inner, closeSt);
	_close->show(anim::type::instant);

	inner->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(inner);
		if (_mode == SearchBarMode::EditorPill) {
			paintPill(p);
		} else {
			p.fillRect(clip, st::windowBg);
		}
	}, inner->lifetime());

	std::move(width) | rpl::on_next([=](int width) {
		_wrap.resizeToWidth(width);
	}, _wrap.lifetime());

	inner->sizeValue(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, inner->lifetime());

	if (_shadow) {
		_wrap.geometryValue(
		) | rpl::on_next([=](QRect geometry) {
			_shadow->setGeometry(
				geometry.x(),
				geometry.y() + geometry.height(),
				geometry.width(),
				st::lineWidth);
		}, _shadow->lifetime());

		_shadow->showOn(rpl::combine(
			_wrap.shownValue(),
			_wrap.heightValue(),
			rpl::mappers::_1 && rpl::mappers::_2 > 0
		) | rpl::filter([=](bool shown) {
			return (shown == _shadow->isHidden());
		}));
	}

	_select->setQueryChangedCallback([=](const QString &query) {
		_queryChanges.fire_copy(query);
	});
	_select->setSubmittedCallback([=](Qt::KeyboardModifiers modifiers) {
		_navigates.fire((modifiers & Qt::ShiftModifier) ? -1 : 1);
	});
	_select->setCancelledCallback([=] {
		_closeRequests.fire({});
	});
	_select->setFocusedChangedCallback([=](bool focused) {
		_focusChanges.fire_copy(focused);
	});

	setResults(0, 0);
}

int SearchBar::pillHeight() const {
	return st::ivEditorToolbarButtonSize + 2 * st::ivEditorPillPadding;
}

QRect SearchBar::pillRect() const {
	const auto inner = _wrap.entity();
	return QRect(
		_pillShadowMargins.left(),
		st::ivEditorToolbarPadding.top() + _pillShadowMargins.top(),
		(inner->width()
			- _pillShadowMargins.left()
			- _pillShadowMargins.right()),
		pillHeight());
}

void SearchBar::paintPill(QPainter &p) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto pill = pillRect();
	const auto radius = pill.height() / 2;
	_pillShadow->paint(p, pill, radius);
	p.setBrush(st::dialogsBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(pill, radius, radius);
	Dialogs::PaintPillOutline(p, pill, radius);
}

void SearchBar::updateControlsGeometry() {
	const auto inner = _wrap.entity();
	const auto width = inner->width();
	const auto height = inner->height();
	if (_mode == SearchBarMode::EditorPill) {
		const auto pill = pillRect();
		const auto pad = st::ivEditorPillPadding;
		const auto skip = st::ivEditorPillButtonSkip;
		const auto centered = [&](int controlHeight) {
			return pill.y() + (pill.height() - controlHeight) / 2;
		};
		auto right = _pillShadowMargins.right() + pad;
		_close->moveToRight(right, centered(_close->height()));
		right += _close->width() + skip;
		_down->moveToRight(right, centered(_down->height()));
		right += _down->width() + skip;
		_up->moveToRight(right, centered(_up->height()));
		right += _up->width();
		if (!_counter->isHidden()) {
			right += st::ivSearchBarCounterSkip;
			_counter->moveToRight(right, centered(_counter->height()));
			right += _counter->width() + st::ivSearchBarCounterSkip;
		}
		const auto left = _pillShadowMargins.left() + pad;
		_select->resizeToWidth(width - left - right);
		_select->moveToLeft(left, centered(_select->height()));
		return;
	}
	_close->moveToRight(0, (height - _close->height()) / 2);
	_down->moveToRight(_close->width(), (height - _down->height()) / 2);
	_up->moveToRight(
		_close->width() + _down->width(),
		(height - _up->height()) / 2);
	auto right = _close->width() + _down->width() + _up->width();
	if (!_counter->isHidden()) {
		right += st::ivSearchBarCounterSkip;
		_counter->moveToRight(right, (height - _counter->height()) / 2);
		right += _counter->width() + st::ivSearchBarCounterSkip;
	}
	_select->resizeToWidth(width - right);
	_select->moveToLeft(0, (height - _select->height()) / 2);
}

void SearchBar::setResults(int current, int total) {
	if (total > 0) {
		_counter->setText(u"%1 / %2"_q.arg(current).arg(total));
		_counter->show();
	} else {
		_counter->hide();
	}
	const auto disabled = (total <= 0);
	_up->setIconOverride(disabled
		? &st::calendarPreviousDisabled
		: nullptr);
	_down->setIconOverride(disabled
		? &st::calendarNextDisabled
		: nullptr);
	_up->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	_down->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	updateControlsGeometry();
}

void SearchBar::toggle(bool shown, anim::type animated) {
	_wrap.toggle(shown, animated);
}

void SearchBar::show(anim::type animated) {
	toggle(true, animated);
}

void SearchBar::hide(anim::type animated) {
	toggle(false, animated);
}

bool SearchBar::shown() const {
	return _wrap.toggled();
}

void SearchBar::setInnerFocus() {
	_select->setInnerFocus();
}

void SearchBar::raise() {
	_wrap.raise();
	if (_shadow) {
		_shadow->raise();
	}
}

void SearchBar::move(int x, int y) {
	_wrap.move(x, y);
}

rpl::producer<QString> SearchBar::queryChanges() const {
	return _queryChanges.events();
}

rpl::producer<int> SearchBar::navigateRequests() const {
	return rpl::merge(
		_navigates.events(),
		_up->clicks() | rpl::map_to(-1),
		_down->clicks() | rpl::map_to(1));
}

rpl::producer<> SearchBar::closeRequests() const {
	return rpl::merge(
		_closeRequests.events(),
		_close->clicks() | rpl::to_empty);
}

rpl::producer<bool> SearchBar::focusChanges() const {
	return _focusChanges.events();
}

rpl::producer<int> SearchBar::heightValue() const {
	if (_mode != SearchBarMode::EditorPill) {
		return _wrap.heightValue();
	}
	return _wrap.heightValue() | rpl::map([=](int height) {
		const auto slide = st::ivEditorToolbarPadding.top()
			+ pillHeight();
		const auto full = slide
			+ _pillShadowMargins.top()
			+ _pillShadowMargins.bottom();
		return (height * slide) / full;
	});
}

rpl::lifetime &SearchBar::lifetime() {
	return _wrap.lifetime();
}

} // namespace Iv
