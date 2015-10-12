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
#include "lang.h"

#include "localstorage.h"

#include "languagebox.h"
#include "confirmbox.h"
#include "mainwidget.h"
#include "window.h"

#include "langloaderplain.h"

LanguageBox::LanguageBox() :
_close(this, lang(lng_box_ok), st::defaultBoxButton) {

	bool haveTestLang = (cLang() == languageTest);

	int32 y = st::boxTitleHeight + st::boxOptionListPadding.top();
	_langs.reserve(languageCount + (haveTestLang ? 1 : 0));
	if (haveTestLang) {
		_langs.push_back(new Radiobutton(this, qsl("lang"), languageTest, qsl("Custom Lang"), (cLang() == languageTest), st::langsButton));
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->height() + st::boxOptionListPadding.top();
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
		_langs.push_back(new Radiobutton(this, qsl("lang"), i, result.value(lng_language_name, LanguageCodes[i] + qsl(" language")), (cLang() == i), st::langsButton));
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->height() + st::boxOptionListPadding.top();
		connect(_langs.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	resizeMaxHeight(st::langsWidth, st::boxTitleHeight + (languageCount + (haveTestLang ? 1 : 0)) * (st::boxOptionListPadding.top() + st::langsButton.height) + st::boxOptionListPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _close.height() + st::boxButtonPadding.bottom());

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));

	_close.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _close.height());
	prepare();
}

void LanguageBox::hideAll() {
	_close.hide();
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		_langs[i]->hide();
	}
}

void LanguageBox::showAll() {
	_close.show();
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		_langs[i]->show();
	}
}

void LanguageBox::mousePressEvent(QMouseEvent *e) {
	if ((e->modifiers() & Qt::CTRL) && (e->modifiers() & Qt::ALT) && (e->modifiers() & Qt::SHIFT)) {
		for (int32 i = 1; i < languageCount; ++i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i] + qsl(".strings"), LangLoaderRequest(lngkeys_cnt));
			if (!loader.errors().isEmpty()) {
				App::wnd()->showLayer(new InformBox(qsl("Lang \"") + LanguageCodes[i] + qsl("\" error :(\n\nError: ") + loader.errors()));
				return;
			} else if (!loader.warnings().isEmpty()) {
				QString warn = loader.warnings();
				if (warn.size() > 256) warn = warn.mid(0, 254) + qsl("..");
				App::wnd()->showLayer(new InformBox(qsl("Lang \"") + LanguageCodes[i] + qsl("\" warnings :(\n\nWarnings: ") + warn));
				return;
			}
		}
		App::wnd()->showLayer(new InformBox(qsl("Everything seems great in all %1 languages!").arg(languageCount - 1)));
	}
}

void LanguageBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_languages));
}

void LanguageBox::onChange() {
	if (isHidden()) return;

	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		int32 langId = _langs[i]->val();
		if (_langs[i]->checked() && langId != cLang()) {
			LangLoaderResult result;
			if (langId > 0) {
				LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[langId] + qsl(".strings"), LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
				result = loader.found();
			} else if (langId == languageTest) {
				LangLoaderPlain loader(cLangFile(), LangLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
				result = loader.found();
			}
			QString text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
			        save = result.value(lng_box_ok, langOriginal(lng_box_ok)),
					cancel = result.value(lng_cancel, langOriginal(lng_cancel));
			ConfirmBox *box = new ConfirmBox(text, save, st::defaultBoxButton, cancel);
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
			Local::writeSettings();
			cSetRestarting(true);
			cSetRestartingToSettings(true);
			App::quit();
		}
	}
}

LanguageBox::~LanguageBox() {
	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		delete _langs[i];
	}
}
