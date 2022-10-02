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
#include "ui/painter.h"
#include "countries/countries_instance.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"

CountryInput::CountryInput(
	QWidget *parent,
	std::shared_ptr<Window::Show> show,
	const style::InputField &st)
: RpWidget(parent)
, _show(show)
, _st(st)
, _text(tr::lng_country_code(tr::now)) {
	resize(_st.width, _st.heightMin);

	auto placeholderFont = _st.placeholderFont->f;
	placeholderFont.setStyleStrategy(QFont::PreferMatch);
	//auto metrics = QFontMetrics(placeholderFont);
	auto placeholder = QString();// metrics.elidedText(tr::lng_country_fake_ph(tr::now), Qt::ElideRight, availableWidth);
	if (!placeholder.isNull()) {
		_placeholderPath.addText(
			0,
			QFontMetrics(placeholderFont).ascent(),
			placeholderFont,
			placeholder);
	}
}

void CountryInput::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	QRect r(rect().intersected(e->rect()));
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(r, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(
			0,
			height() - _st.border,
			width(),
			_st.border,
			_st.borderFg);
	}

	st::introCountryIcon.paint(
		p,
		width()
			- st::introCountryIcon.width()
			- st::introCountryIconPosition.x(),
		st::introCountryIconPosition.y(),
		width());

	p.setFont(_st.font);
	p.setPen(_st.textFg);
	p.drawText(rect().marginsRemoved(_st.textMargins), _text, _st.textAlign);
	if (!_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = 1.;
		p.save();
		p.setClipRect(r);

		const auto placeholderTop = anim::interpolate(
			0,
			_st.placeholderShift,
			placeholderShiftDegree);

		auto r = QRect(rect() - (_st.textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (rtl()) {
			r.moveLeft(width() - r.left() - r.width());
		}

		const auto placeholderScale = 1.
			- (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(
			_st.placeholderFg,
			_st.placeholderFgActive,
			0.);
		placeholderFg = anim::color(
			placeholderFg,
			_st.placeholderFgError,
			0.);

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
		auto object = Box<Ui::CountrySelectBox>();
		const auto box = Ui::MakeWeak(object.data());
		_show->showBox(std::move(object), Ui::LayerOption::CloseOther);
		box->entryChosen(
		) | rpl::start_with_next([=](
				const Ui::CountrySelectBox::Entry &entry) {
			if (box) {
				box->closeBox();
			}

			const auto &list = Countries::Instance().list();
			const auto infoIt = ranges::find(
				list,
				entry.iso2,
				&Countries::Info::iso2);
			if (infoIt == end(list)) {
				return;
			}
			const auto info = *infoIt;
			const auto it = ranges::find(
				info.codes,
				entry.code,
				&Countries::CallingCodeInfo::callingCode);
			if (it != end(info.codes)) {
				chooseCountry(
					&info,
					std::distance(begin(info.codes), it));
			}
		}, lifetime());
	}
}

void CountryInput::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void CountryInput::leaveEventHook(QEvent *e) {
	setMouseTracking(false);
	_active = false;
	setCursor(style::cur_default);
}

void CountryInput::onChooseCode(const QString &code) {
	_show->hideLayer();
	_chosenIso = QString();
	if (code.length()) {
		const auto &byCode = Countries::Instance().byCode();
		const auto i = byCode.constFind(code);
		if (i != byCode.cend()) {
			const auto info = *i;
			_chosenIso = info->iso2;
			setText(info->name);
		} else {
			setText(tr::lng_bad_country_code(tr::now));
		}
	} else {
		setText(tr::lng_country_code(tr::now));
	}
	update();
}

bool CountryInput::chooseCountry(const QString &iso) {
	const auto &byISO2 = Countries::Instance().byISO2();
	const auto i = byISO2.constFind(iso);
	const auto info = (i != byISO2.cend()) ? (*i) : nullptr;

	_chosenIso = QString();
	if (info) {
		chooseCountry(info, 0);
		return true;
	}
	return false;
}

void CountryInput::chooseCountry(
		not_null<const Countries::Info*> info,
		int codeIndex) {
	_chosenIso = info->iso2;
	setText(info->name);
	_codeChanged.fire_copy(info->codes[codeIndex].callingCode);
	update();
}

rpl::producer<QString> CountryInput::codeChanged() const {
	return _codeChanged.events();
}

void CountryInput::setText(const QString &newText) {
	_text = _st.font->elided(
		newText,
		width() - _st.textMargins.left() - _st.textMargins.right());
}
