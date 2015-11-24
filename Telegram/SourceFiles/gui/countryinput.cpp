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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "application.h"
#include "gui/countryinput.h"
#include "gui/scrollarea.h"
#include "boxes/contactsbox.h"

#include "countries.h"

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
}

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

CountryInput::CountryInput(QWidget *parent, const style::countryInput &st) : QWidget(parent), _st(st), _active(false), _text(lang(lng_country_code)) {
	initCountries();

	resize(_st.width, _st.height + _st.ptrSize.height());
	QImage trImage(_st.ptrSize.width(), _st.ptrSize.height(), QImage::Format_ARGB32_Premultiplied);
	{
		static const QPoint trPoints[3] = {
			QPoint(0, 0),
			QPoint(_st.ptrSize.width(), 0),
			QPoint(qCeil(trImage.width() / 2.), trImage.height())
		};
		QPainter p(&trImage);
		p.setRenderHint(QPainter::Antialiasing);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, trImage.width(), trImage.height(), st::transparent->b);

		p.setPen(Qt::NoPen);
		p.setBrush(_st.bgColor->b);
		p.drawPolygon(trPoints, 3);
	}
	_arrow = QPixmap::fromImage(trImage, Qt::ColorOnly);
	_inner = QRect(0, 0, _st.width, _st.height);
	_arrowRect = QRect((st::inpIntroCountryCode.width - _arrow.width() - 1) / 2, _st.height, _arrow.width(), _arrow.height());
}

void CountryInput::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(_inner, _st.bgColor->b);
	p.drawPixmap(_arrowRect.x(), _arrowRect.top(), _arrow);

	p.setFont(_st.font->f);

	p.drawText(rect().marginsRemoved(_st.textMrg), _text, QTextOption(_st.align));
}

void CountryInput::mouseMoveEvent(QMouseEvent *e) {
	bool newActive = _inner.contains(e->pos()) || _arrowRect.contains(e->pos());
	if (_active != newActive) {
		_active = newActive;
		setCursor(_active ? style::cur_pointer : style::cur_default);
	}
}

void CountryInput::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_active) {
		CountrySelectBox *box = new CountrySelectBox();
		connect(box, SIGNAL(countryChosen(const QString&)), this, SLOT(onChooseCountry(const QString&)));
		App::wnd()->showLayer(box);
	}
}

void CountryInput::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void CountryInput::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	_active = false;
	setCursor(style::cur_default);
}

void CountryInput::onChooseCode(const QString &code) {
	App::wnd()->hideLayer();
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
	App::wnd()->hideLayer();

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
	_text = _st.font->elided(newText, width() - _st.textMrg.left() - _st.textMrg.right());
}

CountryInput::~CountryInput() {
}

CountrySelectInner::CountrySelectInner() : TWidget()
, _rowHeight(st::countryRowHeight)
, _sel(0)
, _mouseSel(false) {
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

void CountrySelectInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());
	p.setClipRect(r);

	int l = countriesNow->size();
	if (l) {
		if (r.intersects(QRect(0, 0, width(), st::countriesSkip))) {
			p.fillRect(r.intersected(QRect(0, 0, width(), st::countriesSkip)), st::white->b);
		}
		int32 from = floorclamp(r.y() - st::countriesSkip, _rowHeight, 0, l);
		int32 to = ceilclamp(r.y() + r.height() - st::countriesSkip, _rowHeight, 0, l);
		for (int32 i = from; i < to; ++i) {
			bool sel = (i == _sel);
			int32 y = st::countriesSkip + i * _rowHeight;

			p.fillRect(0, y, width(), _rowHeight, (sel ? st::countryRowBgOver : st::white)->b);

			QString code = QString("+") + (*countriesNow)[i]->code;
			int32 codeWidth = st::countryRowCodeFont->width(code);

			QString name = QString::fromUtf8((*countriesNow)[i]->name);
			int32 nameWidth = st::countryRowNameFont->width(name);
			int32 availWidth = width() - st::countryRowPadding.left() - st::countryRowPadding.right() - codeWidth - st::contactsScroll.width;
			if (nameWidth > availWidth) {
				name = st::countryRowNameFont->elided(name, availWidth);
				nameWidth = st::countryRowNameFont->width(name);
			}

			p.setFont(st::countryRowNameFont);
			p.setPen(st::black);
			p.drawTextLeft(st::countryRowPadding.left(), y + st::countryRowPadding.top(), width(), name);
			p.setFont(st::countryRowCodeFont);
			p.setPen(sel ? st::countryRowCodeFgOver : st::countryRowCodeFg);
			p.drawTextLeft(st::countryRowPadding.left() + nameWidth + st::countryRowPadding.right(), y + st::countryRowPadding.top(), width(), code);
		}
	} else {
		p.fillRect(r, st::white->b);
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_country_none), style::al_center);
	}
}

void CountrySelectInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void CountrySelectInner::leaveEvent(QEvent *e) {
	_mouseSel = false;
	setMouseTracking(false);
	if (_sel >= 0) {
		updateSelectedRow();
		_sel = -1;
	}
}

void CountrySelectInner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
}

void CountrySelectInner::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_lastMousePos = e->globalPos();
	updateSel();
	if (e->button() == Qt::LeftButton) {
		chooseCountry();
	}
}

void CountrySelectInner::updateFilter(QString filter) {
	filter = textSearchKey(filter);

	QStringList f;
	if (!filter.isEmpty()) {
		QStringList filterList = filter.split(cWordSplit(), QString::SkipEmptyParts);
		int l = filterList.size();

		f.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (filterName.isEmpty()) continue;
			f.push_back(filterName);
		}
		filter = f.join(' ');
	}
	if (_filter != filter) {
		_filter = filter;

		if (_filter.isEmpty()) {
			countriesNow = &countriesAll;
		} else {
			QChar first = _filter[0].toLower();
			CountriesIds &ids(countriesByLetter[first]);

			QStringList::const_iterator fb = f.cbegin(), fe = f.cend(), fi;

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
		_sel = countriesNow->isEmpty() ? -1 : 0;
		update();
	}
}

void CountrySelectInner::selectSkip(int32 dir) {
	_mouseSel = false;

	int cur = (_sel >= 0) ? _sel : -1;
	cur += dir;
	if (cur <= 0) {
		_sel = countriesNow->isEmpty() ? -1 : 0;
	} else if (cur >= countriesNow->size()) {
		_sel = -1;
	} else {
		_sel = cur;
	}
	if (_sel >= 0) {
		emit mustScrollTo(st::countriesSkip + _sel * _rowHeight, st::countriesSkip + (_sel + 1) * _rowHeight);
	}
	update();
}

void CountrySelectInner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void CountrySelectInner::chooseCountry() {
	QString result;
	if (_filter.isEmpty()) {
		if (_sel >= 0 && _sel < countriesAll.size()) {
			result = countriesAll[_sel]->iso2;
		}
	} else {
		if (_sel >= 0 && _sel < countriesFiltered.size()) {
			result = countriesFiltered[_sel]->iso2;
		}
	}
	emit countryChosen(result);
}

void CountrySelectInner::refresh() {
	resize(width(), countriesNow->length() ? (countriesNow->length() * _rowHeight + st::countriesSkip) : st::noContactsHeight);
}

void CountrySelectInner::updateSel() {
	if (!_mouseSel) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	bool in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(_lastMousePos));

	int32 newSel = (in && p.y() >= st::countriesSkip && p.y() < st::countriesSkip + countriesNow->size() * _rowHeight) ? ((p.y() - st::countriesSkip) / _rowHeight) : -1;
	if (newSel != _sel) {
		updateSelectedRow();
		_sel = newSel;
		updateSelectedRow();
	}
}

void CountrySelectInner::updateSelectedRow() {
	if (_sel >= 0) {
		update(0, st::countriesSkip + _sel * _rowHeight, width(), _rowHeight);
	}
}

CountrySelectBox::CountrySelectBox() : ItemListBox(st::countriesScroll, st::boxWidth)
, _inner()
, _filter(this, st::boxSearchField, lang(lng_country_ph))
, _filterCancel(this, st::boxSearchCancel)
, _topShadow(this) {
	ItemListBox::init(&_inner, st::boxScrollSkip, st::boxTitleHeight + _filter.height());

	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSel()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_filter, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_filterCancel, SIGNAL(clicked()), this, SLOT(onFilterCancel()));
	connect(&_inner, SIGNAL(mustScrollTo(int, int)), &_scroll, SLOT(scrollToY(int, int)));
	connect(&_inner, SIGNAL(countryChosen(const QString&)), this, SIGNAL(countryChosen(const QString&)));

	_filterCancel.setAttribute(Qt::WA_OpaquePaintEvent);

	prepare();
}

void CountrySelectBox::onSubmit() {
	_inner.chooseCountry();
}

void CountrySelectBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner.selectSkipPage(_scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner.selectSkipPage(_scroll.height(), -1);
	} else {
		ItemListBox::keyPressEvent(e);
	}
}

void CountrySelectBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_country_select));
}

void CountrySelectBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	_filter.resize(width(), _filter.height());
	_filter.moveToLeft(0, st::boxTitleHeight);
	_filterCancel.moveToRight(0, st::boxTitleHeight);
	_inner.resize(width(), _inner.height());
	_topShadow.setGeometry(0, st::boxTitleHeight + _filter.height(), width(), st::lineWidth);
}

void CountrySelectBox::hideAll() {
	_filter.hide();
	_filterCancel.hide();
	_topShadow.hide();
	ItemListBox::hideAll();
}

void CountrySelectBox::showAll() {
	_filter.show();
	if (_filter.getLastText().isEmpty()) {
		_filterCancel.hide();
	} else {
		_filterCancel.show();
	}
	_topShadow.show();
	ItemListBox::showAll();
}

void CountrySelectBox::onFilterCancel() {
	_filter.setText(QString());
}

void CountrySelectBox::onFilterUpdate() {
	_scroll.scrollToY(0);
	if (_filter.getLastText().isEmpty()) {
		_filterCancel.hide();
	} else {
		_filterCancel.show();
	}
	_inner.updateFilter(_filter.getLastText());
}

void CountrySelectBox::showDone() {
	_filter.setFocus();
}
