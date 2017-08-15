/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/countryinput.h"

#include "lang/lang_keys.h"
#include "application.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/multi_select.h"
#include "ui/effects/ripple_animation.h"
#include "countries.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"

namespace {

	typedef QList<const CountryInfo *> CountriesFiltered;
	typedef QVector<int> CountriesIds;
	typedef QHash<QChar, CountriesIds> CountriesByLetter;
	typedef QVector<QString> CountryNames;
	typedef QVector<CountryNames> CountriesNames;

	CountriesByCode _countriesByCode;
	CountriesByISO2 _countriesByISO2;
	CountriesFiltered countriesFiltered, countriesAll, *countriesNow = &countriesAll;
	CountriesByLetter countriesByLetter;
	CountriesNames countriesNames;

	QString lastValidISO;
	int countriesCount = sizeof(countries) / sizeof(countries[0]);

	void initCountries() {
		if (!_countriesByCode.isEmpty()) return;

		_countriesByCode.reserve(countriesCount);
		_countriesByISO2.reserve(countriesCount);
		for (int i = 0; i < countriesCount; ++i) {
			const CountryInfo *info(countries + i);
			_countriesByCode.insert(info->code, info);
			CountriesByISO2::const_iterator already = _countriesByISO2.constFind(info->iso2);
			if (already != _countriesByISO2.cend()) {
				QString badISO = info->iso2;
				(void)badISO;
			}
			_countriesByISO2.insert(info->iso2, info);
		}
		countriesAll.reserve(countriesCount);
		countriesFiltered.reserve(countriesCount);
		countriesNames.resize(countriesCount);
	}

} // namespace

const CountriesByCode &countriesByCode() {
	initCountries();
	return _countriesByCode;
}

const CountriesByISO2 &countriesByISO2() {
	initCountries();
	return _countriesByISO2;
}

QString findValidCode(QString fullCode) {
	while (fullCode.length()) {
		CountriesByCode::const_iterator i = _countriesByCode.constFind(fullCode);
		if (i != _countriesByCode.cend()) {
			return (*i)->code;
		}
		fullCode = fullCode.mid(0, fullCode.length() - 1);
	}
	return "";
}

CountryInput::CountryInput(QWidget *parent, const style::InputField &st) : TWidget(parent)
, _st(st)
, _text(lang(lng_country_code)) {
	initCountries();
	resize(_st.width, _st.heightMin);

	auto availableWidth = width() - _st.textMargins.left() - _st.textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	auto placeholderFont = _st.placeholderFont->f;
	placeholderFont.setStyleStrategy(QFont::PreferMatch);
	auto metrics = QFontMetrics(placeholderFont);
	auto placeholder = QString();// metrics.elidedText(lang(lng_country_fake_ph), Qt::ElideRight, availableWidth);
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
		auto box = Ui::show(Box<CountrySelectBox>());
		connect(box, SIGNAL(countryChosen(const QString&)), this, SLOT(onChooseCountry(const QString&)));
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
	if (code.length()) {
		CountriesByCode::const_iterator i = _countriesByCode.constFind(code);
		if (i != _countriesByCode.cend()) {
			const CountryInfo *info = *i;
			lastValidISO = info->iso2;
			setText(QString::fromUtf8(info->name));
		} else {
			setText(lang(lng_bad_country_code));
		}
	} else {
		setText(lang(lng_country_code));
	}
	update();
}

bool CountryInput::onChooseCountry(const QString &iso) {
	Ui::hideLayer();

	CountriesByISO2::const_iterator i = _countriesByISO2.constFind(iso);
	const CountryInfo *info = (i == _countriesByISO2.cend()) ? 0 : (*i);

	if (info) {
		lastValidISO = info->iso2;
		setText(QString::fromUtf8(info->name));
		emit codeChanged(info->code);
		update();
		return true;
	}
	return false;
}

void CountryInput::setText(const QString &newText) {
	_text = _st.font->elided(newText, width() - _st.textMargins.left() - _st.textMargins.right());
}

CountrySelectBox::CountrySelectBox(QWidget*)
: _select(this, st::contactsMultiSelect, langFactory(lng_country_ph)) {
}

void CountrySelectBox::prepare() {
	setTitle(langFactory(lng_country_select));

	_select->resizeToWidth(st::boxWidth);
	_select->setQueryChangedCallback([this](const QString &query) { onFilterUpdate(query); });
	_select->setSubmittedCallback([this](bool) { onSubmit(); });

	_inner = setInnerWidget(object_ptr<Inner>(this), st::countriesScroll, _select->height());

	addButton(langFactory(lng_close), [this] { closeBox(); });

	setDimensions(st::boxWidth, st::boxMaxListHeight);

	connect(_inner, SIGNAL(mustScrollTo(int, int)), this, SLOT(onScrollToY(int, int)));
	connect(_inner, SIGNAL(countryChosen(const QString&)), this, SIGNAL(countryChosen(const QString&)));
}

void CountrySelectBox::onSubmit() {
	_inner->chooseCountry();
}

void CountrySelectBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(height() - _select->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(height() - _select->height(), -1);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void CountrySelectBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, 0);

	_inner->resizeToWidth(width());
}

void CountrySelectBox::onFilterUpdate(const QString &query) {
	onScrollToY(0);
	_inner->updateFilter(query);
}

void CountrySelectBox::setInnerFocus() {
	_select->setInnerFocus();
}

CountrySelectBox::Inner::Inner(QWidget *parent) : TWidget(parent)
, _rowHeight(st::countryRowHeight) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	CountriesByISO2::const_iterator l = _countriesByISO2.constFind(lastValidISO);
	bool seenLastValid = false;
	int already = countriesAll.size();

	countriesByLetter.clear();
	const CountryInfo *lastValid = (l == _countriesByISO2.cend()) ? 0 : (*l);
	for (int i = 0; i < countriesCount; ++i) {
		const CountryInfo *ins = lastValid ? (i ? (countries + i - (seenLastValid ? 0 : 1)) : lastValid) : (countries + i);
		if (lastValid && i && ins == lastValid) {
			seenLastValid = true;
			++ins;
		}
		if (already > i) {
			countriesAll[i] = ins;
		} else {
			countriesAll.push_back(ins);
		}

		QStringList namesList = QString::fromUtf8(ins->name).toLower().split(QRegularExpression("[\\s\\-]"), QString::SkipEmptyParts);
		CountryNames &names(countriesNames[i]);
		int l = namesList.size();
		names.resize(0);
		names.reserve(l);
		for (int j = 0, l = namesList.size(); j < l; ++j) {
			QString name = namesList[j].trimmed();
			if (!name.length()) continue;

			QChar ch = name[0];
			CountriesIds &v(countriesByLetter[ch]);
			if (v.isEmpty() || v.back() != i) {
				v.push_back(i);
			}

			names.push_back(name);
		}
	}

	_filter = qsl("a");
	updateFilter();
}

void CountrySelectBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());
	p.setClipRect(r);

	auto ms = getms();
	int l = countriesNow->size();
	if (l) {
		if (r.intersects(QRect(0, 0, width(), st::countriesSkip))) {
			p.fillRect(r.intersected(QRect(0, 0, width(), st::countriesSkip)), st::countryRowBg);
		}
		int32 from = floorclamp(r.y() - st::countriesSkip, _rowHeight, 0, l);
		int32 to = ceilclamp(r.y() + r.height() - st::countriesSkip, _rowHeight, 0, l);
		for (int32 i = from; i < to; ++i) {
			auto selected = (i == (_pressed >= 0 ? _pressed : _selected));
			auto y = st::countriesSkip + i * _rowHeight;

			p.fillRect(0, y, width(), _rowHeight, selected ? st::countryRowBgOver : st::countryRowBg);
			if (_ripples.size() > i && _ripples[i]) {
				_ripples[i]->paint(p, 0, y, width(), ms);
				if (_ripples[i]->empty()) {
					_ripples[i].reset();
				}
			}

			auto code = QString("+") + (*countriesNow)[i]->code;
			auto codeWidth = st::countryRowCodeFont->width(code);

			auto name = QString::fromUtf8((*countriesNow)[i]->name);
			auto nameWidth = st::countryRowNameFont->width(name);
			auto availWidth = width() - st::countryRowPadding.left() - st::countryRowPadding.right() - codeWidth - st::boxLayerScroll.width;
			if (nameWidth > availWidth) {
				name = st::countryRowNameFont->elided(name, availWidth);
				nameWidth = st::countryRowNameFont->width(name);
			}

			p.setFont(st::countryRowNameFont);
			p.setPen(st::countryRowNameFg);
			p.drawTextLeft(st::countryRowPadding.left(), y + st::countryRowPadding.top(), width(), name);

			p.setFont(st::countryRowCodeFont);
			p.setPen(selected ? st::countryRowCodeFgOver : st::countryRowCodeFg);
			p.drawTextLeft(st::countryRowPadding.left() + nameWidth + st::countryRowPadding.right(), y + st::countryRowPadding.top(), width(), code);
		}
	} else {
		p.fillRect(r, st::boxBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_country_none), style::al_center);
	}
}

void CountrySelectBox::Inner::enterEventHook(QEvent *e) {
	setMouseTracking(true);
}

void CountrySelectBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	if (_selected >= 0) {
		updateSelectedRow();
		_selected = -1;
	}
}

void CountrySelectBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void CountrySelectBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());

	setPressed(_selected);
	if (_pressed >= 0 && _pressed < countriesNow->size()) {
		if (_ripples.size() <= _pressed) {
			_ripples.reserve(_pressed + 1);
			while (_ripples.size() <= _pressed) {
				_ripples.push_back(nullptr);
			}
		}
		if (!_ripples[_pressed]) {
			auto mask = Ui::RippleAnimation::rectMask(QSize(width(), _rowHeight));
			_ripples[_pressed] = std::make_unique<Ui::RippleAnimation>(st::countryRipple, std::move(mask), [this, index = _pressed] {
				updateRow(index);
			});
			_ripples[_pressed]->add(e->pos() - QPoint(0, st::countriesSkip + _pressed * _rowHeight));
		}
	}
}

void CountrySelectBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(-1);
	updateSelectedRow();
	if (e->button() == Qt::LeftButton) {
		if ((pressed >= 0) && pressed == _selected) {
			chooseCountry();
		}
	}
}

void CountrySelectBox::Inner::updateFilter(QString filter) {
	auto words = TextUtilities::PrepareSearchWords(filter);
	filter = words.isEmpty() ? QString() : words.join(' ');
	if (_filter != filter) {
		_filter = filter;

		if (_filter.isEmpty()) {
			countriesNow = &countriesAll;
		} else {
			QChar first = _filter[0].toLower();
			CountriesIds &ids(countriesByLetter[first]);

			QStringList::const_iterator fb = words.cbegin(), fe = words.cend(), fi;

			countriesFiltered.clear();
			for (CountriesIds::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
				int index = *i;
				CountryNames &names(countriesNames[index]);
				CountryNames::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
				for (fi = fb; fi != fe; ++fi) {
					QString filterName(*fi);
					for (ni = nb; ni != ne; ++ni) {
						if (ni->startsWith(*fi)) {
							break;
						}
					}
					if (ni == ne) {
						break;
					}
				}
				if (fi == fe) {
					countriesFiltered.push_back(countriesAll[index]);
				}
			}
			countriesNow = &countriesFiltered;
		}
		refresh();
		_selected = countriesNow->isEmpty() ? -1 : 0;
		update();
	}
}

void CountrySelectBox::Inner::selectSkip(int32 dir) {
	_mouseSelection = false;

	int cur = (_selected >= 0) ? _selected : -1;
	cur += dir;
	if (cur <= 0) {
		_selected = countriesNow->isEmpty() ? -1 : 0;
	} else if (cur >= countriesNow->size()) {
		_selected = -1;
	} else {
		_selected = cur;
	}
	if (_selected >= 0) {
		emit mustScrollTo(st::countriesSkip + _selected * _rowHeight, st::countriesSkip + (_selected + 1) * _rowHeight);
	}
	update();
}

void CountrySelectBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void CountrySelectBox::Inner::chooseCountry() {
	QString result;
	if (_filter.isEmpty()) {
		if (_selected >= 0 && _selected < countriesAll.size()) {
			result = countriesAll[_selected]->iso2;
		}
	} else {
		if (_selected >= 0 && _selected < countriesFiltered.size()) {
			result = countriesFiltered[_selected]->iso2;
		}
	}
	emit countryChosen(result);
}

void CountrySelectBox::Inner::refresh() {
	resize(width(), countriesNow->length() ? (countriesNow->length() * _rowHeight + st::countriesSkip) : st::noContactsHeight);
}

void CountrySelectBox::Inner::updateSelected(QPoint localPos) {
	if (!_mouseSelection) return;

	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(QCursor::pos()));

	auto selected = (in && localPos.y() >= st::countriesSkip && localPos.y() < st::countriesSkip + countriesNow->size() * _rowHeight) ? ((localPos.y() - st::countriesSkip) / _rowHeight) : -1;
	if (_selected != selected) {
		updateSelectedRow();
		_selected = selected;
		updateSelectedRow();
	}
}

void CountrySelectBox::Inner::updateSelectedRow() {
	updateRow(_selected);
}

void CountrySelectBox::Inner::updateRow(int index) {
	if (index >= 0) {
		update(0, st::countriesSkip + index * _rowHeight, width(), _rowHeight);
	}
}

void CountrySelectBox::Inner::setPressed(int pressed) {
	if (_pressed >= 0 && _pressed < _ripples.size() && _ripples[_pressed]) {
		_ripples[_pressed]->lastStop();
	}
	_pressed = pressed;
}

CountrySelectBox::Inner::~Inner() = default;
