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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "languagebox.h"
#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

#include "langloaderplain.h"

LanguageBox::LanguageBox() :
_done(this, lang(lng_about_done), st::langsCloseButton),
_hiding(false), a_opacity(0, 1) {

	bool haveTestLang = (cLang() == languageTest);

	_width = st::langsWidth;
	_height = st::addContactTitleHeight + st::langsPadding.top() + st::langsPadding.bottom() + (languageCount + (haveTestLang ? 1 : 0)) * (st::langPadding.top() + st::rbDefFlat.height + st::langPadding.bottom()) + _done.height();

	int32 y = st::addContactTitleHeight + st::langsPadding.top();
	_langs.reserve(languageCount + (haveTestLang ? 1 : 0));
	if (haveTestLang) {
		_langs.push_back(new FlatRadiobutton(this, qsl("lang"), languageTest, qsl("Custom Lang"), (cLang() == languageTest), st::langButton));
		_langs.back()->move(st::langsPadding.left() + st::langPadding.left(), y + st::langPadding.top());
		y += st::langPadding.top() + _langs.back()->height() + st::langPadding.bottom();
		connect(_langs.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}
	for (int32 i = 0; i < languageCount; ++i) {
		LangLoaderResult result;
		if (i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i] + qsl(".strings"), LangLoaderRequest(lng_language_name));
			result = loader.found();
		} else {
			result.insert(lng_language_name, langOriginal(lng_language_name));
		}
		_langs.push_back(new FlatRadiobutton(this, qsl("lang"), i, result.value(lng_language_name, LanguageCodes[i] + qsl(" language")), (cLang() == i), st::langButton));
		_langs.back()->move(st::langsPadding.left() + st::langPadding.left(), y + st::langPadding.top());
		y += st::langPadding.top() + _langs.back()->height() + st::langPadding.bottom();
		connect(_langs.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	_done.move(0, _height - _done.height());

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void LanguageBox::hideAll() {
	_done.hide();
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		_langs[i]->hide();
	}
}

void LanguageBox::showAll() {
	_done.show();
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		_langs[i]->show();
	}
}

void LanguageBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	}
}

void LanguageBox::mousePressEvent(QMouseEvent *e) {
	if ((e->modifiers() & Qt::CTRL) && (e->modifiers() & Qt::ALT) && (e->modifiers() & Qt::SHIFT)) {
		for (int32 i = 1; i < languageCount; ++i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i] + qsl(".strings"), LangLoaderRequest(lngkeys_cnt));
			if (!loader.errors().isEmpty()) {
				App::wnd()->showLayer(new ConfirmBox(qsl("Lang \"") + LanguageCodes[i] + qsl("\" error :(\n\nError: ") + loader.errors(), true, lang(lng_close)));
				return;
			} else if (!loader.warnings().isEmpty()) {
				QString warn = loader.warnings();
				if (warn.size() > 256) warn = warn.mid(0, 254) + qsl("..");
				App::wnd()->showLayer(new ConfirmBox(qsl("Lang \"") + LanguageCodes[i] + qsl("\" warnings :(\n\nWarnings: ") + warn, true, lang(lng_close)));
				return;
			}
		}
	}
}

void LanguageBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void LanguageBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);

			// draw box title / text
			p.setFont(st::addContactTitleFont->f);
			p.setPen(st::black->p);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_languages));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void LanguageBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			setFocus();
		}
	} else {
		a_opacity.update(ms, anim::linear);
	}
	update();
}

void LanguageBox::onChange() {
	if (isHidden()) return;

	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		int32 langId = _langs[i]->val();
		if (_langs[i]->checked() && langId != cLang()) {
			LangLoaderResult result;
			if (langId > 0) {
				LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[langId] + qsl(".strings"), LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_continue));
				result = loader.found();
			} else if (langId == languageTest) {
				LangLoaderPlain loader(cLangFile(), LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_continue));
				result = loader.found();
			}
			QString text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
			        save = result.value(lng_continue, langOriginal(lng_continue)),
					cancel = result.value(lng_cancel, langOriginal(lng_cancel));
			ConfirmBox *box = new ConfirmBox(text, save, cancel);
			connect(box, SIGNAL(confirmed()), this, SLOT(onSave()));
			connect(box, SIGNAL(closed()), this, SLOT(onRestore()));
			App::wnd()->replaceLayer(box);
		}
	}
}

void LanguageBox::onRestore() {
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		if (_langs[i]->val() == cLang()) {
			_langs[i]->setChecked(true);
		}
	}
}

void LanguageBox::onSave() {
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		if (_langs[i]->checked()) {
			cSetLang(_langs[i]->val());
			App::writeConfig();
			cSetRestarting(true);
			cSetRestartingToSettings(true);
			App::quit();
		}
	}
}

void LanguageBox::onClose() {
	emit closed();
}

void LanguageBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

LanguageBox::~LanguageBox() {
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		delete _langs[i];
	}
}
