/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/stars_rating.h"

#include "lang/lang_keys.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_media_view.h"

namespace Ui {
namespace {

constexpr auto kAutoCollapseTimeout = 4 * crl::time(1000);

} // namespace

StarsRating::StarsRating(
	QWidget *parent,
	const style::StarsRating &st,
	rpl::producer<Data::StarsRating> value,
	Fn<not_null<QWidget*>()> parentForTooltip)
: _widget(std::make_unique<Ui::AbstractButton>(parent))
, _st(st)
, _parentForTooltip(std::move(parentForTooltip))
, _value(std::move(value))
, _collapseTimer([=] { _expanded = false; }) {
	init();
}

StarsRating::~StarsRating() = default;

void StarsRating::init() {
	_expanded.value() | rpl::start_with_next([=](bool expanded) {
		_widget->setPointerCursor(!expanded);
		const auto from = expanded ? 0. : 1.;
		const auto till = expanded ? 1. : 0.;
		_expandedAnimation.start([=] {
			updateWidth();
			if (!_expandedAnimation.animating()) {
				updateStarsTooltipGeometry();
			}
		}, from, till, st::slideDuration);
		toggleTooltips(expanded);
	}, lifetime());

	_widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_widget.get());
		paint(p);
	}, lifetime());

	_widget->setClickedCallback([=] {
		if (!_value.current()) {
			return;
		}
		_expanded = true;
		_collapseTimer.callOnce(kAutoCollapseTimeout);
	});

	const auto added = _st.margin + _st.padding;
	const auto border = 2 * _st.border;
	const auto fontHeight = _st.style.font->height;
	const auto height = added.top() + fontHeight + added.bottom() + border;
	_widget->resize(_widget->width(), height);

	_value.value() | rpl::start_with_next([=](Data::StarsRating rating) {
		if (!rating) {
			_widget->resize(0, _widget->height());
			_collapsedWidthValue = 0;
			_expanded = false;
			updateExpandedWidth();
			_expandedAnimation.stop();
			return;
		}
		updateTexts(rating);
	}, lifetime());
}

void StarsRating::updateTexts(Data::StarsRating rating) {
	_collapsedText.setText(
		_st.style,
		Lang::FormatCountDecimal(rating.level));
	_expandedText.setText(
		_st.style,
		tr::lng_boost_level(tr::now, lt_count_decimal, rating.level));
	_nextText.setText(
		_st.style,
		(rating.nextLevelStars
			? Lang::FormatCountDecimal(rating.level + 1)
			: QString()));

	const auto added = _st.padding;
	const auto border = 2 * _st.border;
	const auto add = added.left() + added.right() + border;
	const auto min = _expandedText.maxWidth() + _nextText.maxWidth();
	const auto height = _widget->height();
	_minimalContentWidth = add + min + _st.minSkip;
	_collapsedWidthValue = _st.margin.right()
		+ std::max(
			add + _collapsedText.maxWidth(),
			height - _st.margin.top() - _st.margin.bottom());
	updateExpandedWidth();
	updateWidth();
}

void StarsRating::updateExpandedWidth() {
	_expandedWidthValue = _st.margin.right() + std::max(
		_collapsedWidthValue.current() + _minimalAddedWidth.current(),
		_minimalContentWidth.current());
}

void StarsRating::updateWidth() {
	const auto widthToRight = anim::interpolate(
		_collapsedWidthValue.current(),
		_expandedWidthValue.current(),
		_expandedAnimation.value(_expanded.current() ? 1. : 0.));
	_widget->resize(_st.margin.left() + widthToRight, _widget->height());
	_widget->update();
	updateStarsTooltipGeometry();
}

void StarsRating::toggleTooltips(bool shown) {
	if (!shown) {
		if (const auto strong = _about.get()) {
			strong->hideAnimated();
		}
		if (const auto strong = _stars.release()) {
			strong->toggleAnimated(false);
		}
		return;
	}
	const auto value = _value.current();
	const auto parent = _parentForTooltip
		? _parentForTooltip().get()
		: _widget->window();
	const auto text = value.nextLevelStars
		? (Lang::FormatCountDecimal(value.currentStars)
			+ u" / "_q
			+ Lang::FormatCountDecimal(value.nextLevelStars))
		: Lang::FormatCountDecimal(value.currentStars);
	_stars = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			_widget.get(),
			rpl::single(TextWithEntities{ text }),
			st::storiesInfoTooltipMaxWidth,
			st::storiesInfoTooltipLabel),
		st::infoStarsRatingTooltip);
	const auto stars = _stars.get();
	const auto weak = QPointer<QWidget>(stars);
	const auto destroy = [=] {
		delete weak.data();
	};
	stars->setAttribute(Qt::WA_TransparentForMouseEvents);
	stars->setHiddenCallback(destroy);
	updateStarsTooltipGeometry();
	stars->toggleAnimated(true);

	_aboutSt = std::make_unique<style::Toast>(st::defaultMultilineToast);
	const auto learn = u"Learn More"_q;
	_aboutSt->padding.setRight(
		(st::infoStarsRatingLearn.style.font->width(learn)
			- st::infoStarsRatingLearn.width));

	_about = Ui::Toast::Show(parent, {
		.text = TextWithEntities{
			u"Profile level reflects the user's payment reliability."_q,
		},
		.st = _aboutSt.get(),
		.attach = RectPart::Top,
		.dark = true,
		.adaptive = true,
		.acceptinput = true,
		.duration = kAutoCollapseTimeout,
	});
	const auto strong = _about.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	const auto hideToast = [weak = _about] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	};

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(learn),
		st::infoStarsRatingLearn);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->show();
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
	button->setClickedCallback([=] {
		UrlClickHandler::Open(u"https://telegram.org/"_q);
	});
}

void StarsRating::updateStarsTooltipGeometry() {
	if (!_stars) {
		return;
	}
	const auto weakParent = base::make_weak(_stars->parentWidget());
	const auto weak = base::make_weak(_widget.get());
	const auto point = _st.margin.left()
		+ _st.border
		+ (_activeWidth / (_value.current().nextLevelStars ? 1 : 2));
	const auto countPosition = [=](QSize size) {
		const auto strong = weak.get();
		const auto parent = weakParent.get();
		if (!strong || !parent) {
			return QPoint();
		}
		const auto geometry = Ui::MapFrom(parent, strong, strong->rect());
		const auto shift = size.width() / 2;
		const auto left = geometry.x() + point - shift;
		const auto margin = st::defaultImportantTooltip.margin;
		return QPoint(
			std::min(
				std::max(left, margin.left()),
				parent->width() - size.width() - margin.right()),
			geometry.y() + geometry.height());
	};
	_stars->pointAt(
		Ui::MapFrom(
			_stars->parentWidget(),
			_widget.get(),
			QRect(point, 0, st::lineWidth, _widget->height())),
		RectPart::Bottom,
		countPosition);
}

void StarsRating::raise() {
	_widget->raise();
}

void StarsRating::moveTo(int x, int y) {
	_widget->move(x - _st.margin.left(), y - _st.margin.top());
}

void StarsRating::paint(QPainter &p) {
	const auto outer = _widget->rect().marginsRemoved(_st.margin);
	if (outer.isEmpty()) {
		return;
	}
	const auto border = _st.border;
	const auto middle = outer.marginsRemoved(
		{ border, border, border, border });
	const auto mradius = middle.height() / 2.;
	const auto inner = middle.marginsRemoved(_st.padding);

	const auto expanded = _expandedAnimation.value(
		_expanded.current() ? 1. : 0.);

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_st.inactiveBg);
	const auto oradius = outer.height() / 2.;
	p.drawRoundedRect(outer, oradius, oradius);
	p.setBrush(_st.activeBg);

	const auto value = _value.current();
	const auto expandedRatio = (value.nextLevelStars > value.levelStars)
		? ((value.currentStars - value.levelStars)
			/ float64(value.nextLevelStars - value.levelStars))
		: 1.;
	const auto expandedFilled = _st.padding.left()
		+ _expandedText.maxWidth()
		+ _st.padding.right()
		+ expandedRatio * (middle.width()
			- _st.padding.left()
			- _expandedText.maxWidth()
			- _st.padding.right()
			- _st.padding.left()
			- _nextText.maxWidth()
			- _st.padding.right());
	const auto collapsedFilled = _collapsedWidthValue.current()
		- _st.margin.right()
		- 2 * _st.border;
	_activeWidth = anim::interpolate(
		collapsedFilled,
		expandedFilled,
		expanded);
	p.drawRoundedRect(
		middle.x(),
		middle.y(),
		_activeWidth,
		middle.height(),
		mradius,
		mradius);
	p.setPen(_st.activeFg);
	if (expanded < 1.) {
		p.setOpacity(1. - expanded);
		const auto skip = (inner.width() - _collapsedText.maxWidth()) / 2;
		_collapsedText.draw(p, {
			.position = inner.topLeft() + QPoint(skip, 0),
			.availableWidth = _collapsedText.maxWidth(),
		});
	}
	if (expanded > 0.) {
		p.setOpacity(expanded);
		_expandedText.draw(p, {
			.position = inner.topLeft(),
			.availableWidth = _expandedText.maxWidth(),
		});

		p.setPen(_st.inactiveFg);
		_nextText.draw(p, {
			.position = (inner.topLeft()
				+ QPoint(inner.width() - _nextText.maxWidth(), 0)),
			.availableWidth = _nextText.maxWidth(),
		});
	}
}

void StarsRating::setMinimalAddedWidth(int addedWidth) {
	_minimalAddedWidth = addedWidth + (_st.style.font->spacew * 2);
	updateExpandedWidth();
	updateWidth();
}

rpl::producer<int> StarsRating::collapsedWidthValue() const {
	return _collapsedWidthValue.value();
}

rpl::lifetime &StarsRating::lifetime() {
	return _widget->lifetime();
}

} // namespace Ui
