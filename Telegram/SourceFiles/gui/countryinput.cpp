/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "application.h"
#include "gui/countryinput.h"
#include "gui/scrollarea.h"

namespace {

	struct CountryInfo {
		CountryInfo(const char *_name, const char *_iso2, const char *_code) : name(_name), iso2(_iso2), code(_code) {
		}
		const char *name, *iso2, *code;
	};

#include "countries.h"

	typedef QHash<QString, const CountryInfo *> CountriesByCode;
	typedef QHash<QString, const CountryInfo *> CountriesByISO2;
	typedef QList<const CountryInfo *> CountriesFiltered;
	typedef QVector<int> CountriesIds;
	typedef QHash<QChar, CountriesIds> CountriesByLetter;
	typedef QVector<QString> CountryNames;
	typedef QVector<CountryNames> CountriesNames;

	CountriesByCode countriesByCode;
	CountriesByISO2 countriesByISO2;
	CountriesFiltered countriesFiltered, countriesAll, *countriesNow = &countriesAll;
	CountriesByLetter countriesByLetter;
	CountriesNames countriesNames;

	QString lastFilter, lastValidISO;
	int countriesCount = sizeof(countries) / sizeof(countries[0]);

	void initCountries() {
		if (countriesByCode.size()) return;

		countriesByCode.reserve(countriesCount);
		countriesByISO2.reserve(countriesCount);
		for (int i = 0; i < countriesCount; ++i) {
			const CountryInfo *info(countries + i);
			countriesByCode.insert(info->code, info);
			CountriesByISO2::const_iterator already = countriesByISO2.constFind(info->iso2);
			if (already != countriesByISO2.cend()) {
				QString badISO = info->iso2;
				(void)badISO;
			}
			countriesByISO2.insert(info->iso2, info);
		}
		countriesAll.reserve(countriesCount);
		countriesFiltered.reserve(countriesCount);
		countriesNames.resize(countriesCount);
	}
}

QString findValidCode(QString fullCode) {
	while (fullCode.length()) {
		CountriesByCode::const_iterator i = countriesByCode.constFind(fullCode);
		if (i != countriesByCode.cend()) {
			return (*i)->code;
		}
		fullCode = fullCode.mid(0, fullCode.length() - 1);
	}
	return "";
}

CountryInput::CountryInput(QWidget *parent, const style::countryInput &st) : QWidget(parent), _st(st), _active(false), _text(lang(lng_country_code)), _select(0) {
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
	_arrow = QPixmap::fromImage(trImage);
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
		Window *w = App::wnd();
		if (w->focusWidget()) w->focusWidget()->clearFocus();
		if (_select) {
			_select->hide();
			_select->deleteLater();
		}
		_select = new CountrySelect();
		connect(_select, SIGNAL(countryChosen(const QString &)), this, SLOT(onChooseCountry(const QString &)));
		connect(_select, SIGNAL(countryFinished()), this, SLOT(onFinishCountry()));
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
	if (_select) {
		_select->hide();
		_select->deleteLater();
		_select = 0;
		emit selectClosed();
	}
	if (code.length()) {
		CountriesByCode::const_iterator i = countriesByCode.constFind(code);
		if (i != countriesByCode.cend()) {
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
	CountriesByISO2::const_iterator i = countriesByISO2.constFind(iso);
	const CountryInfo *info = (i == countriesByISO2.cend()) ? 0 : (*i);

	if (info) {
		lastValidISO = info->iso2;
		setText(QString::fromUtf8(info->name));
		emit codeChanged(info->code);
		update();
		return true;
	}
	return false;
}

void CountryInput::onFinishCountry() {
	if (_select) {
		_select->hide();
		_select->deleteLater();
		_select = 0;
		emit selectClosed();
	}
}

void CountryInput::setText(const QString &newText) {
	_text = _st.font->m.elidedText(newText, Qt::ElideRight, width() - _st.textMrg.left() - _st.textMrg.right());
}

CountryInput::~CountryInput() {
	delete _select;
}

CountryList::CountryList(QWidget *parent, const style::countryList &st) : QWidget(parent), _sel(0),
	_st(st), _mouseSel(false) {
	CountriesByISO2::const_iterator l = countriesByISO2.constFind(lastValidISO);
	bool seenLastValid = false;
	int already = countriesAll.size();

	countriesByLetter.clear();
	const CountryInfo *lastValid = (l == countriesByISO2.cend()) ? 0 : (*l);
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

	lastFilter = "";
	resetList();
}

void CountryList::resetList() {
	countriesNow = &countriesAll;
	if (lastFilter.length()) {
		QChar first = lastFilter[0].toLower();
		CountriesIds &ids(countriesByLetter[first]);
		
		QStringList filterList = lastFilter.split(QRegularExpression("[\\s\\-]"), QString::SkipEmptyParts);
		int l = filterList.size();

		CountryNames filter;
		filter.reserve(l);
		for (int i = 0; i < l; ++i) {
			QString filterName = filterList[i].trimmed();
			if (!filterName.length()) continue;
			filter.push_back(filterName);
		}
		CountryNames::const_iterator fb = filter.cbegin(), fe = filter.cend(), fi;

		countriesFiltered.clear();
		for (CountriesIds::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
			int index = *i;
			CountryNames &names(countriesNames[index]);
			CountryNames::const_iterator nb = names.cbegin(), ne = names.cend(), ni;
			for (fi = fb; fi != fe; ++fi) {
				QString filterName(*fi);
				for (ni = nb; ni != ne; ++ni) {
					if ((*ni).indexOf(*fi) == 0) {
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
	resize(width(), countriesNow->length() ? (countriesNow->length() * _st.rowHeight + 2 * _st.verticalMargin) : parentWidget()->height());
	setSelected(0);
}

void CountryList::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	bool trivial = (rect() == r);

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(r);
	}

	int l = countriesNow->size();
	if (l) {
		int from = (r.top() > _st.verticalMargin) ? (r.top() - _st.verticalMargin) / _st.rowHeight : 0, to = from + r.height() / _st.rowHeight + 1;
		if (to >= l) {
			if (from >= l) return;
			to = l;
		}
		p.setFont(_st.font->f);
		QRectF textRect(_st.margin + _st.borderMargin, _st.verticalMargin + from * _st.rowHeight, width() - 2 * _st.margin - 2 * _st.borderMargin, _st.rowHeight - _st.borderWidth);
		for (int i = from; i < to; ++i) {
			bool sel = (i == _sel);
			if (sel) {
				p.fillRect(_st.borderMargin, _st.verticalMargin + i * _st.rowHeight, width() - 2 * _st.borderMargin, _st.rowHeight, _st.bgHovered->b);
			}
			p.setFont(_st.font->f);
			p.setPen(_st.color->p);
			p.drawText(textRect, _st.font->m.elidedText(QString::fromUtf8((*countriesNow)[i]->name), Qt::ElideRight, width() - 2 * _st.margin - _st.codeWidth), QTextOption(style::al_left));
			p.setFont(_st.codeFont->f);
			p.setPen(_st.codeColor->p);
			p.drawText(textRect, QString("+") + (*countriesNow)[i]->code, QTextOption(style::al_right));
			textRect.setBottom(textRect.bottom() + _st.rowHeight);
			textRect.setTop(textRect.top() + _st.rowHeight);
		}
	} else {
		p.setFont(_st.notFoundFont->f);
		p.setPen(_st.notFoundColor->p);
		p.drawText(r, lang(lng_country_none), QTextOption(style::al_center));
	}
}

void CountryList::mouseMoveEvent(QMouseEvent *e) {
	_mouseSel = true;
	_mousePos = mapToGlobal(e->pos());
	onUpdateSelected(true);
}

void CountryList::onUpdateSelected(bool force) {
	QPoint p(mapFromGlobal(_mousePos));
	if ((!force && !rect().contains(p)) || !_mouseSel) return;

	int newSelected = p.y();
	newSelected = (newSelected > _st.verticalMargin) ? (newSelected - _st.verticalMargin) / _st.rowHeight : 0;
	int l = countriesNow->size();

	if (newSelected >= l) newSelected = l - 1;
	if (newSelected < 0) newSelected = 0;
	if (newSelected != _sel) {
		_sel = newSelected;
		update();
	}
}

void CountryList::mousePressEvent(QMouseEvent *e) {
	_mouseSel = true;
	_mousePos = mapToGlobal(e->pos());
	onUpdateSelected(true);

	emit countrySelected();
}

void CountryList::enterEvent(QEvent *e) {
	setMouseTracking(true);
}

void CountryList::leaveEvent(QEvent *e) {
	setMouseTracking(false);
}

void CountryList::updateFiltered() {
	resetList();
}

void CountryList::onParentGeometryChanged() {
	_mousePos = QCursor::pos();
	if (rect().contains(mapFromGlobal(_mousePos))) {
		setMouseTracking(true);
		onUpdateSelected(true);
	}
}

void CountryList::selectSkip(int delta) {
	setSelected(_sel + delta);
}

void CountryList::selectSkipPage(int h, int delta) {
	setSelected(_sel + delta * (h / int(_st.rowHeight) - 1));
}

void CountryList::setSelected(int newSelected) {
	_mouseSel = false;
	if (newSelected >= countriesNow->size()) {
		newSelected = countriesNow->size() - 1;
	}
	if (newSelected < 0) {
		newSelected = 0;
	}
	_sel = newSelected;
	emit mustScrollTo(_sel * _st.rowHeight, (_sel + 1) * _st.rowHeight);
	update();
}

QString CountryList::getSelectedCountry() const {
	if (lastFilter.length()) {
		if (_sel < countriesFiltered.size()) {
			return countriesFiltered[_sel]->iso2;
		} else {
			return "";
		}
	}
	return countriesAll[_sel]->iso2;
}

CountrySelect::CountrySelect() : QWidget(App::wnd()),
	_result("none"),
    _filter(this, st::inpCountry, lang(lng_country_ph)), _scroll(this, st::scrollCountries), _list(&_scroll),
    _doneButton(this, lang(lng_country_done), st::btnSelectDone),
    _cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _innerLeft(0), _innerTop(0), _innerWidth(0), _innerHeight(0),
	a_alpha(0), a_bgAlpha(0), a_coord(st::countriesSlideShift), _shadow(st::boxShadow) {
	setGeometry(App::wnd()->rect());
	
	App::wnd()->topWidget(this);

	connect(App::wnd(), SIGNAL(resized(const QSize &)), this, SLOT(onParentResize(const QSize &)));
	connect(&_doneButton, SIGNAL(clicked()), this, SLOT(onCountryChoose()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCountryCancel()));
	connect(&_scroll, SIGNAL(scrollFinished()), this, SLOT(onScrollFinished()));
	connect(&_scroll, SIGNAL(geometryChanged()), &_list, SLOT(onParentGeometryChanged()));
	connect(&_scroll, SIGNAL(scrolled()), &_list, SLOT(onUpdateSelected()));
	connect(&_list, SIGNAL(countrySelected()), this, SLOT(onCountryChoose()));
	connect(&_filter, SIGNAL(changed()), this, SLOT(onFilterUpdate()));
	connect(&_list, SIGNAL(mustScrollTo(int, int)), &_scroll, SLOT(scrollToY(int, int)));

	show();
	setFocus();
	_scroll.setWidget(&_list);
	_scroll.setFocusPolicy(Qt::NoFocus);

	prepareAnimation(0);
}

void CountrySelect::prepareAnimation(int to) {
	if (to) {
		if (_result == "none") _result = "";
		a_alpha.start(0);
		af_alpha = st::countriesAlphaHideFunc;
		a_bgAlpha.start(0);
		af_bgAlpha = st::countriesBackHideFunc;
		a_coord.start(to * st::countriesSlideShift);
		af_coord = st::countriesHideFunc;
	} else {
		_result = "none";
		a_alpha.start(1);
		af_alpha = st::countriesAlphaShowFunc;
		a_bgAlpha.start(1);
		af_bgAlpha = st::countriesBackShowFunc;
		a_coord.start(0);
		af_coord = st::countriesShowFunc;
	}
	_cache = myGrab(this, QRect(_innerLeft, _innerTop, _innerWidth, _innerHeight));
	_scroll.hide();
	_doneButton.hide();
	_cancelButton.hide();
	_filter.hide();
	anim::start(this);
}

void CountrySelect::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.setOpacity(st::layerAlpha * a_bgAlpha.current());
	p.fillRect(rect(), st::layerBG->b);
	if (animating()) {
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current() + _innerLeft, _innerTop, _cache);
	} else {
		p.setOpacity(1);

		QRect inner(_innerLeft, _innerTop, _innerWidth, _innerHeight);
		_shadow.paint(p, inner);
		if (trivial || e->rect().intersects(inner)) {
			// fill bg
			p.fillRect(inner, st::white->b);

			// paint shadows
			p.fillRect(_innerLeft, _innerTop + st::participantFilter.height, _innerWidth, st::scrollDef.topsh, st::scrollDef.shColor->b);

			// paint button sep
			p.setPen(st::btnSelectSep->p);
			p.drawLine(_innerLeft + st::btnSelectCancel.width, _innerTop + _innerHeight - st::btnSelectCancel.height, _innerLeft + st::btnSelectCancel.width, _innerTop + _innerHeight - 1);

			// draw box title / text
			p.setPen(st::black->p);
			p.setFont(st::addContactTitleFont->f);
			p.drawText(_innerLeft + st::addContactTitlePos.x(), _innerTop + st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_country_select));
		}
	}
}

void CountrySelect::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onCountryCancel();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onCountryChoose();
	} else if (e->key() == Qt::Key_Down) {
		_list.selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_list.selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_list.selectSkipPage(_scroll.height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_list.selectSkipPage(_scroll.height(), -1);
	}
}

void CountrySelect::onFilterUpdate() {
	QString newFilter(_filter.text().trimmed().toLower());
	if (newFilter != lastFilter) {
		lastFilter = newFilter;
		_list.updateFiltered();
	}
}

void CountrySelect::resizeEvent(QResizeEvent *e) {
	if (width() != e->oldSize().width()) {
		_innerWidth = st::newGroupNamePadding.left() + _filter.width() + st::newGroupNamePadding.right();
		_innerLeft = (width() - _innerWidth) / 2;

		_list.resize(_innerWidth, _list.height());
	}
	if (height() != e->oldSize().height()) {
		_innerTop = st::introSelectDelta;
		_innerHeight = height() - _innerTop - st::introSelectDelta;
		if (_innerHeight > st::introSelectMaxHeight) {
			_innerHeight = st::introSelectMaxHeight;
			_innerTop = (height() - _innerHeight) / 2;
		}
	}

	_filter.move(_innerLeft + st::newGroupNamePadding.left(), _innerTop + st::contactsAdd.height + st::newGroupNamePadding.top());
	int32 scrollTop = _filter.y() + _filter.height() + st::newGroupNamePadding.bottom();
	int32 scrollHeight = _innerHeight - st::contactsAdd.height - st::newGroupNamePadding.top() - _filter.height() - st::newGroupNamePadding.bottom() - _cancelButton.height();
	_scroll.setGeometry(_innerLeft, scrollTop, _innerWidth, scrollHeight);

	int btnTop = scrollTop + scrollHeight;
	_cancelButton.move(_innerLeft, btnTop);
	_doneButton.move(_innerLeft + _innerWidth - _doneButton.width(), btnTop);
}

bool CountrySelect::animStep(float64 ms) {
	float64 dt = ms / st::countriesSlideDuration;
	bool res = true;
	if (dt >= 1) {
		a_alpha.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		_cache = QPixmap();
		_scroll.show();
		_doneButton.show();
		_cancelButton.show();
		_filter.show();
		_filter.setFocus();
		if (_result != "none") {
			QTimer::singleShot(0, this, SIGNAL(countryFinished()));
		}
		res = false;
	} else {
		a_alpha.update(dt, af_alpha);
		a_bgAlpha.update(dt, af_bgAlpha);
		a_coord.update(dt, af_coord);
	}
	update();
	return res;
}

void CountrySelect::onParentResize(const QSize &newSize) {
	resize(App::wnd()->size());
}

void CountrySelect::onCountryCancel() {
	finish("");
}

void CountrySelect::onCountryChoose() {
	finish(_list.getSelectedCountry());
}

void CountrySelect::finish(const QString &res) {
	_result = res;
	prepareAnimation(_result.length() ? -1 : 1);
	emit countryChosen(_result);
}

void CountrySelect::onScrollFinished() {
	_filter.setFocus();
}

CountrySelect::~CountrySelect() {
	if (App::wnd()) {
		App::wnd()->noTopWidget(this);
	}
}
