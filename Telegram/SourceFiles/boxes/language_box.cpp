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
#include "boxes/language_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_file_parser.h"
#include "styles/style_boxes.h"

void LanguageBox::prepare() {
	addButton(lang(lng_box_ok), [this] { closeBox(); });

	setTitle(lang(lng_languages));

	auto haveTestLang = (cLang() == languageTest);

	_langGroup = std::make_shared<Ui::RadiobuttonGroup>(cLang());
	auto y = st::boxOptionListPadding.top();
	_langs.reserve(languageCount + (haveTestLang ? 1 : 0));
	if (haveTestLang) {
		_langs.emplace_back(this, _langGroup, languageTest, qsl("Custom Lang"), st::langsButton);
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->heightNoMargins() + st::boxOptionListSkip;
	}
	for (auto i = 0; i != languageCount; ++i) {
		LangLoaderResult result;
		if (i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i].c_str() + qsl(".strings"), { lng_language_name });
			result = loader.found();
		} else {
			result.insert(lng_language_name, langOriginal(lng_language_name));
		}
		_langs.emplace_back(this, _langGroup, i, result.value(lng_language_name, LanguageCodes[i].c_str() + qsl(" language")), st::langsButton);
		_langs.back()->move(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		y += _langs.back()->heightNoMargins() + st::boxOptionListSkip;
	}
	_langGroup->setChangedCallback([this](int value) { languageChanged(value); });

	auto optionsCount = languageCount + (haveTestLang ? 1 : 0);
	setDimensions(st::langsWidth, st::boxOptionListPadding.top() + optionsCount * st::langsButton.height + (optionsCount - 1) * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
}

void LanguageBox::mousePressEvent(QMouseEvent *e) {
	if ((e->modifiers() & Qt::CTRL) && (e->modifiers() & Qt::ALT) && (e->modifiers() & Qt::SHIFT)) {
		for (int32 i = 1; i < languageCount; ++i) {
			LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[i].c_str() + qsl(".strings"), { lngkeys_cnt });
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

void LanguageBox::languageChanged(int languageId) {
	Expects(languageId == languageTest || (languageId >= 0 && languageId < base::array_size(LanguageCodes)));

	if (languageId == cLang()) {
		return;
	}

	LangLoaderResult result;
	if (languageId > 0) {
		LangLoaderPlain loader(qsl(":/langs/lang_") + LanguageCodes[languageId].c_str() + qsl(".strings"), { lng_sure_save_language, lng_cancel, lng_box_ok });
		result = loader.found();
	} else if (languageId == languageTest) {
		LangLoaderPlain loader(cLangFile(), { lng_sure_save_language, lng_cancel, lng_box_ok });
		result = loader.found();
	}
	auto text = result.value(lng_sure_save_language, langOriginal(lng_sure_save_language)),
		save = result.value(lng_box_ok, langOriginal(lng_box_ok)),
		cancel = result.value(lng_cancel, langOriginal(lng_cancel));
	Ui::show(Box<ConfirmBox>(text, save, cancel, base::lambda_guarded(this, [this, languageId] {
		cSetLang(languageId);
		Local::writeSettings();
		App::restart();
	}), base::lambda_guarded(this, [this] {
		_langGroup->setValue(cLang());
	})), KeepOtherLayers);
}
