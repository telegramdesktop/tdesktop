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
#include "boxes/languagebox.h"

#include "lang.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "localstorage.h"
#include "boxes/confirmbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "langloaderplain.h"
#include "styles/style_boxes.h"

void LanguageBox::prepare() {
	addButton(lang(lng_box_ok), [this] { closeBox(); });

	setTitle(lang(lng_languages));

	auto haveTestLang = (cLang() == languageTest);

	auto y = st::boxOptionListPadding.top();
	_langs.reserve(languageCount + (haveTestLang ? 1 : 0));
	if (haveTestLang) {
		_langs.push_back(new Ui::Radiobutton(this, qsl("lang"), languageTest, qsl("Custom Lang"), (cLang() == languageTest), st::langsButton));
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->heightNoMargins() + st::boxOptionListSkip;
		connect(_langs.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}
	for (auto i = 0; i != languageCount; ++i) {
		LangLoaderResult result;
		if (i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i].c_str() + qsl(".strings"), langLoaderRequest(lng_language_name));
			result = loader.found();
		} else {
			result.insert(lng_language_name, langOriginal(lng_language_name));
		}
		_langs.push_back(new Ui::Radiobutton(this, qsl("lang"), i, result.value(lng_language_name, LanguageCodes[i].c_str() + qsl(" language")), (cLang() == i), st::langsButton));
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->heightNoMargins() + st::boxOptionListSkip;
		connect(_langs.back(), SIGNAL(changed()), this, SLOT(onChange()));
	}

	auto optionsCount = languageCount + (haveTestLang ? 1 : 0);
	setDimensions(st::langsWidth, st::boxOptionListPadding.top() + optionsCount * st::langsButton.height + (optionsCount - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

void LanguageBox::mousePressEvent(QMouseEvent *e) {
	if ((e->modifiers() & Qt::CTRL) && (e->modifiers() & Qt::ALT) && (e->modifiers() & Qt::SHIFT)) {
		for (int32 i = 1; i < languageCount; ++i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i].c_str() + qsl(".strings"), langLoaderRequest(lngkeys_cnt));
			if (!loader.errors().isEmpty()) {
				Ui::show(Box<InformBox>(qsl("Lang \"") + LanguageCodes[i].c_str() + qsl("\" error :(\n\nError: ") + loader.errors()));
				return;
			} else if (!loader.warnings().isEmpty()) {
				QString warn = loader.warnings();
				if (warn.size() > 256) warn = warn.mid(0, 253) + qsl("...");
				Ui::show(Box<InformBox>(qsl("Lang \"") + LanguageCodes[i].c_str() + qsl("\" warnings :(\n\nWarnings: ") + warn));
				return;
			}
		}
		Ui::show(Box<InformBox>(qsl("Everything seems great in all %1 languages!").arg(languageCount - 1)));
	}
}

void LanguageBox::onChange() {
	if (!isBoxShown()) return;

	for (int32 i = 0, l = _langs.size(); i < l; ++i) {
		int32 langId = _langs[i]->val();
		if (_langs[i]->checked() && langId != cLang()) {
			LangLoaderResult result;
			if (langId > 0) {
				LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[langId].c_str() + qsl(".strings"), langLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
				result = loader.found();
			} else if (langId == languageTest) {
				LangLoaderPlain loader(cLangFile(), langLoaderRequest(lng_sure_save_language, lng_cancel, lng_box_ok));
				result = loader.found();
			}
			auto text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
				save = result.value(lng_box_ok, langOriginal(lng_box_ok)),
				cancel = result.value(lng_cancel, langOriginal(lng_cancel));
			Ui::show(Box<ConfirmBox>(text, save, cancel, base::lambda_guarded(this, [this] {
				saveLanguage();
			}), base::lambda_guarded(this, [this] {
				restoreLanguage();
			})), KeepOtherLayers);
		}
	}
}

void LanguageBox::restoreLanguage() {
	for (auto i = 0, l = _langs.size(); i != l; ++i) {
		if (_langs[i]->val() == cLang()) {
			_langs[i]->setChecked(true);
		}
	}
}

void LanguageBox::saveLanguage() {
	for (auto i = 0, l = _langs.size(); i != l; ++i) {
		if (_langs[i]->checked()) {
			cSetLang(_langs[i]->val());
			Local::writeSettings();
			App::restart();
		}
	}
}
