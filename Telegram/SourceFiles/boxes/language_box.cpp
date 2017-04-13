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
#include "lang/lang_instance.h"
#include "styles/style_boxes.h"

void LanguageBox::prepare() {
	addButton(lang(lng_box_ok), [this] { closeBox(); });

	setTitle(lang(lng_languages));

	request(MTPlangpack_GetLanguages()).done([this](const MTPVector<MTPLangPackLanguage> &result) {
		auto currentId = Lang::Current().id();
		auto currentFound = false;
		std::vector<QString> languageIds = { qsl("en") };
		std::vector<QString> languageNames = { qsl("English") };
		for (auto &language : result.v) {
			t_assert(language.type() == mtpc_langPackLanguage);
			auto &data = language.c_langPackLanguage();
			auto languageId = qs(data.vlang_code);
			auto languageName = qs(data.vname);
			if (languageId != qstr("en")) {
				languageIds.push_back(languageId);
				languageNames.push_back(languageName);
			}
		}
		if (currentId == qstr("custom")) {
			languageIds.insert(languageIds.begin(), currentId);
			languageNames.insert(languageNames.begin(), qsl("Custom LangPack"));
			currentFound = true;
		}

		auto languageCount = languageIds.size();
		_langGroup = std::make_shared<Ui::RadiobuttonGroup>(cLang());
		auto y = st::boxOptionListPadding.top();
		_langs.reserve(languageCount);
		for (auto i = 0; i != languageCount; ++i) {
			if (!currentFound && languageIds[i] == currentId) {
				currentFound = true;
			}
			_langs.emplace_back(this, _langGroup, i, languageNames[i], st::langsButton);
			auto button = _langs.back().data();
			button->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), y + st::langsButton.margin.top());
			button->show();
			y += button->heightNoMargins() + st::boxOptionListSkip;
		}
		_langGroup->setChangedCallback([this](int value) { languageChanged(value); });

		setDimensions(st::langsWidth, st::boxOptionListPadding.top() + languageCount * st::langsButton.height + languageCount * st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::boxPadding.bottom());
	}).fail([this](const RPCError &error) {
		closeBox();
	}).send();

	setDimensions(st::langsWidth, st::langsWidth);
}

void LanguageBox::languageChanged(int languageId) {
	//Expects(languageId == languageTest || (languageId >= 0 && languageId < base::array_size(LanguageCodes)));

	//if (languageId == cLang()) {
	//	return;
	//}

	//Lang::FileParser::Result result;
	//if (languageId > 0) {
	//	Lang::FileParser loader(qsl(":/langs/lang_") + LanguageCodes[languageId].c_str() + qsl(".strings"), { lng_sure_save_language, lng_cancel, lng_box_ok });
	//	result = loader.found();
	//} else if (languageId == languageTest) {
	//	Lang::FileParser loader(cLangFile(), { lng_sure_save_language, lng_cancel, lng_box_ok });
	//	result = loader.found();
	//}
	//auto text = result.value(lng_sure_save_language, Lang::GetOriginalValue(lng_sure_save_language)),
	//	save = result.value(lng_box_ok, Lang::GetOriginalValue(lng_box_ok)),
	//	cancel = result.value(lng_cancel, Lang::GetOriginalValue(lng_cancel));
	//Ui::show(Box<ConfirmBox>(text, save, cancel, base::lambda_guarded(this, [this, languageId] {
	//	cSetLang(languageId);
	//	Local::writeSettings();
	//	App::restart();
	//}), base::lambda_guarded(this, [this] {
	//	_langGroup->setValue(cLang());
	//})), KeepOtherLayers);
}
