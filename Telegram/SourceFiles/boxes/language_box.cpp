/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/language_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "messenger.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "styles/style_boxes.h"

class LanguageBox::Inner : public TWidget, private base::Subscriber {
public:
	Inner(QWidget *parent, not_null<Languages*> languages);

	void setSelected(int index);
	void refresh();

private:
	void activateCurrent();
	void languageChanged(int languageIndex);

	not_null<Languages*> _languages;
	std::shared_ptr<Ui::RadiobuttonGroup> _group;
	std::vector<object_ptr<Ui::Radiobutton>> _buttons;

};

LanguageBox::Inner::Inner(QWidget *parent, not_null<Languages*> languages) : TWidget(parent)
, _languages(languages) {
	_group = std::make_shared<Ui::RadiobuttonGroup>(0);
	_group->setChangedCallback([this](int value) { languageChanged(value); });
	subscribe(Lang::Current().updated(), [this] {
		activateCurrent();
		refresh();
	});
}

void LanguageBox::Inner::setSelected(int index) {
	_group->setValue(index);
}

void LanguageBox::Inner::refresh() {
	for (auto &button : _buttons) {
		button.destroy();
	}
	_buttons.clear();

	auto y = st::boxOptionListPadding.top() + st::langsButton.margin.top();
	_buttons.reserve(_languages->size());
	auto index = 0;
	for_const (auto &language, *_languages) {
		_buttons.emplace_back(this, _group, index++, language.nativeName, st::langsButton);
		auto button = _buttons.back().data();
		button->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), y);
		button->show();
		y += button->heightNoMargins() + st::boxOptionListSkip;
	}
	auto newHeight = y - st::boxOptionListSkip + st::boxOptionListPadding.bottom() + st::langsButton.margin.bottom();
	resize(st::langsWidth, newHeight);
}

void LanguageBox::Inner::languageChanged(int languageIndex) {
	Expects(languageIndex >= 0 && languageIndex < _languages->size());

	activateCurrent();
	auto languageId = (*_languages)[languageIndex].id;
	if (Lang::Current().id() != languageId) {
		// "custom" is applied each time it is passed to switchToLanguage().
		// So we check that the language really has changed.
		Lang::CurrentCloudManager().switchToLanguage(languageId);
	}
}

void LanguageBox::Inner::activateCurrent() {
	auto currentId = Lang::Current().id();
	for (auto i = 0, count = _languages->size(); i != count; ++i) {
		auto languageId = (*_languages)[i].id;
		auto isCurrent = (languageId == currentId) || (languageId == Lang::DefaultLanguageId() && currentId.isEmpty());
		if (isCurrent) {
			_group->setValue(i);
			return;
		}
	}
}

void LanguageBox::prepare() {
	refreshLang();
	subscribe(Lang::Current().updated(), [this] {
		refreshLang();
	});

	_inner = setInnerWidget(object_ptr<Inner>(this, &_languages), st::boxLayerScroll);

	refresh();
	subscribe(Lang::CurrentCloudManager().languageListChanged(), [this] {
		refresh();
	});
}

void LanguageBox::refreshLang() {
	clearButtons();
	addButton(langFactory(lng_box_ok), [this] { closeBox(); });

	setTitle(langFactory(lng_languages));

	update();
}

void LanguageBox::refresh() {
	refreshLanguages();

	_inner->refresh();
	setDimensions(st::langsWidth, qMin(_inner->height(), st::boxMaxListHeight));
}

void LanguageBox::refreshLanguages() {
	_languages = Languages();
	auto list = Lang::CurrentCloudManager().languageList();
	_languages.reserve(list.size() + 1);
	auto currentId = Lang::Current().id();
	auto currentIndex = -1;
	_languages.push_back({ qsl("en"), qsl("English"), qsl("English") });
	for (auto &language : list) {
		auto isCurrent = (language.id == currentId) || (language.id == Lang::DefaultLanguageId() && currentId.isEmpty());
		if (language.id != qstr("en")) {
			if (isCurrent) {
				currentIndex = _languages.size();
			}
			_languages.push_back(language);
		} else if (isCurrent) {
			currentIndex = 0;
		}
	}
	if (currentId == qstr("custom")) {
		_languages.insert(_languages.begin(), { currentId, qsl("Custom LangPack"), qsl("Custom LangPack") });
		currentIndex = 0;
	} else if (currentIndex < 0) {
		currentIndex = _languages.size();
		_languages.push_back({ currentId, lang(lng_language_name), lang(lng_language_name) });
	}
	_inner->setSelected(currentIndex);
}

base::binary_guard LanguageBox::Show() {
	auto result = base::binary_guard();

	const auto manager = Messenger::Instance().langCloudManager();
	if (manager->languageList().isEmpty()) {
		auto guard = std::make_shared<base::binary_guard>();
		std::tie(result, *guard) = base::make_binary_guard();
		auto alive = std::make_shared<std::unique_ptr<base::Subscription>>(
			std::make_unique<base::Subscription>());
		**alive = manager->languageListChanged().add_subscription([=] {
			const auto show = guard->alive();
			*alive = nullptr;
			if (show) {
				Ui::show(Box<LanguageBox>());
			}
		});
	} else {
		Ui::show(Box<LanguageBox>());
	}
	manager->requestLanguageList();

	return result;
}
