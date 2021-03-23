/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/countryinput.h"

#include "lang/lang_keys.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/multi_select.h"
#include "ui/effects/ripple_animation.h"
#include "ui/boxes/country_select_box.h"
#include "data/data_countries.h"
#include "base/qt_adapters.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"

namespace {

QString LastValidISO;

} // namespace

CountryInput::CountryInput(QWidget *parent, const style::InputField &st)
: RpWidget(parent)
, _st(st)
, _text(tr::lng_country_code(tr::now)) {
	resize(_st.width, _st.heightMin);

	auto availableWidth = width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	auto placeholderFont = _st.placeholderFont->f;
	placeholderFont.setStyleStrategy(QFont::PreferMatch);
	//auto metrics = QFontMetrics(placeholderFont);
	auto placeholder = QString();// metrics.elidedText(tr::lng_country_fake_ph(tr::now), Qt::ElideRight, availableWidth);
	if (!placeholder.isNull()) {
		_placeholderPath.addText(0, QFontMetrics(placeholderFont).ascent(), placeholderFont, placeholder);
	}
}

void CountryInput::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(rect().intersected(e->rect()));
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(r, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}

	st::introCountryIcon.paint(p, width() - st::introCountryIcon.width() - st::introCountryIconPosition.x(), st::introCountryIconPosition.y(), width());

	p.setFont(_st.font);
	p.setPen(_st.textFg);
	p.drawText(rect().marginsRemoved(_st.textMargins), _text, _st.textAlign);
	if (!_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = 1.;
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(_st.textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (rtl()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, 0.);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, 0.);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	}
}

void CountryInput::mouseMoveEvent(QMouseEvent *e) {
	bool newActive = rect().contains(e->pos());
	if (_active != newActive) {
		_active = newActive;
		setCursor(_active ? style::cur_pointer : style::cur_default);
	}
}

void CountryInput::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_active) {
		auto box = Ui::show(Box<Ui::CountrySelectBox>());
		box->countryChosen(
		) | rpl::start_with_next([=](QString iso) {
			chooseCountry(iso);
		}, lifetime());
	}
}

void CountryInput::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void CountryInput::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	_active = false;
	setCursor(style::cur_default);
}

void CountryInput::onChooseCode(const QString &code) {
	Ui::hideLayer();
	_chosenIso = QString();
	if (code.length()) {
		const auto &byCode = Data::CountriesByCode();
		const auto i = byCode.constFind(code);
		if (i != byCode.cend()) {
			const auto info = *i;
			_chosenIso = LastValidISO = info->iso2;
			setText(QString::fromUtf8(info->name));
		} else {
			setText(tr::lng_bad_country_code(tr::now));
		}
	} else {
		setText(tr::lng_country_code(tr::now));
	}
	update();
}

bool CountryInput::chooseCountry(const QString &iso) {
	Ui::hideLayer();

	const auto &byISO2 = Data::CountriesByISO2();
	const auto i = byISO2.constFind(iso);
	const auto info = (i != byISO2.cend()) ? (*i) : nullptr;

	_chosenIso = QString();
	if (info) {
		_chosenIso = LastValidISO = info->iso2;
		setText(QString::fromUtf8(info->name));
		codeChanged(info->code);
		update();
		return true;
	}
	return false;
}

void CountryInput::setText(const QString &newText) {
	_text = _st.font->elided(newText, width() - _st.textMargins.left() - _st.textMargins.right());
}
