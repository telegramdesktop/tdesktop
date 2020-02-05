/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/dictionaries_manager.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "mtproto/dedicated_file_loader.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "lang/lang_keys.h"
#include "base/zlib_help.h"
#include "layout.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

#include "chat_helpers/spellchecker_common.h"

#include <QString>

namespace Ui {
namespace {

using Dictionaries = std::vector<int>;
using namespace Storage::CloudBlob;

using Loading = MTP::DedicatedLoader::Progress;
using DictState = BlobState;

class Loader : public BlobLoader {
public:
	Loader(
		QObject *parent,
		int id,
		MTP::DedicatedLoader::Location location,
		const QString &folder,
		int size);

	void destroy() override;
	void unpack(const QString &path) override;

};

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent, Dictionaries enabledDictionaries);

	Dictionaries enabledRows() const;

private:
	void setupContent(Dictionaries enabledDictionaries);

	Dictionaries _enabledRows;

};

base::unique_qptr<Loader> GlobalLoader;
rpl::event_stream<Loader*> GlobalLoaderValues;

QLocale LocaleFromLangId(int langId) {
	if (langId > 1000) {
		const auto l = langId / 1000;
		const auto lang = static_cast<QLocale::Language>(l);
		const auto country = static_cast<QLocale::Country>(langId - l * 1000);
		return QLocale(lang, country);
	}
	return QLocale(static_cast<QLocale::Language>(langId));
}

void SetGlobalLoader(base::unique_qptr<Loader> loader) {
	GlobalLoader = std::move(loader);
	GlobalLoaderValues.fire(GlobalLoader.get());
}

int GetDownloadSize(int id) {
	const auto sets = Spellchecker::Dictionaries();
	return ranges::find(sets, id, &Spellchecker::Dict::id)->size;
}

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	const auto username = kCloudLocationUsername.utf16();
	const auto sets = Spellchecker::Dictionaries();
	const auto i = ranges::find(sets, id, &Spellchecker::Dict::id);
	return MTP::DedicatedLoader::Location{ username, i->postId };
}

DictState ComputeState(int id) {
	// if (id == CurrentSetId()) {
		// return Active();
	if (Spellchecker::DictionaryExists(id)) {
		return Ready();
	}
	return Available{ GetDownloadSize(id) };
}

QString StateDescription(const DictState &state) {
	return state.match([](const Available &data) {
		return tr::lng_emoji_set_download(tr::now, lt_size, formatSizeText(data.size));
	}, [](const Ready &data) -> QString {
		return tr::lng_emoji_set_ready(tr::now);
	}, [](const Active &data) -> QString {
		return tr::lng_settings_manage_enabled_dictionary(tr::now);
		// return tr::lng_emoji_set_active(tr::now);
	}, [](const Loading &data) {
		const auto percent = (data.size > 0)
			? snap((data.already * 100) / float64(data.size), 0., 100.)
			: 0.;
		return tr::lng_emoji_set_loading(
			tr::now,
			lt_percent,
			QString::number(int(std::round(percent))) + '%',
			lt_progress,
			formatDownloadText(data.already, data.size));
	}, [](const Failed &data) {
		return tr::lng_attach_failed(tr::now);
	});
}

Loader::Loader(
	QObject *parent,
	int id,
	MTP::DedicatedLoader::Location location,
	const QString &folder,
	int size) : BlobLoader(parent, id, location, folder, size) {
}

void Loader::unpack(const QString &path) {
	const auto weak = Ui::MakeWeak(this);
	crl::async([=] {
		if (Spellchecker::UnpackDictionary(path, id())) {
			QFile(path).remove();
			crl::on_main(weak, [=] {
				destroy();
			});
		} else {
			crl::on_main(weak, [=] {
				fail();
			});
		}
	});
}

void Loader::destroy() {
	Expects(GlobalLoader == this);

	SetGlobalLoader(nullptr);
}

Inner::Inner(
	QWidget *parent,
	Dictionaries enabledDictionaries) : RpWidget(parent) {
	setupContent(std::move(enabledDictionaries));
}

Dictionaries Inner::enabledRows() const {
	return _enabledRows;
}

auto AddButtonWithLoader(
		not_null<Ui::VerticalLayout*> content,
		const Spellchecker::Dict &set,
		bool buttonEnabled) {
	const auto id = set.id;

	const auto button = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(set.name),
				st::dictionariesSectionButton
			)
		)
	)->entity();

	const auto buttonState = button->lifetime()
		.make_state<rpl::variable<DictState>>();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		buttonState->value() | rpl::map(StateDescription),
		st::settingsUpdateState);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		button->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=] {
		label->moveToLeft(
			st::settingsUpdateStatePosition.x(),
			st::settingsUpdateStatePosition.y());
	}, label->lifetime());

	buttonState->value(
	) | rpl::start_with_next([=](const DictState &state) {
		const auto isToggledSet = state.is<Active>();
		const auto toggled = isToggledSet ? 1. : 0.;
		const auto over = !button->isDisabled()
			&& (button->isDown() || button->isOver());

		if (toggled == 0. && !over) {
			label->setTextColorOverride(std::nullopt);
		} else {
			label->setTextColorOverride(anim::color(
				over ? st::contactsStatusFgOver : st::contactsStatusFg,
				st::contactsStatusFgOnline,
				toggled));
		}
	}, label->lifetime());

	button->toggleOn(
		rpl::single(
			buttonEnabled
		) | rpl::then(
			buttonState->value(
			) | rpl::filter([](const DictState &state) {
				return state.is<Failed>();
			}) | rpl::map([](const auto &state) {
				return false;
			})
		)
	);

	*buttonState = GlobalLoaderValues.events_starting_with(
		GlobalLoader.get()
	) | rpl::map([=](Loader *loader) {
		return (loader && loader->id() == id)
			? loader->state()
			: rpl::single(
				buttonEnabled
			) | rpl::then(
				button->toggledValue()
			) | rpl::map([=](auto enabled) {
				const auto &state = buttonState->current();
				if (enabled && state.is<Ready>()) {
					return DictState(Active());
				}
				if (!enabled && state.is<Active>()) {
					return DictState(Ready());
				}
				return ComputeState(id);
			});
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const DictState &state) {
		return !buttonState->current().is<Failed>() || !state.is<Available>();
	});

	button->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		const auto &state = buttonState->current();
		if (toggled && (state.is<Available>() || state.is<Failed>())) {
			SetGlobalLoader(base::make_unique_q<Loader>(
				App::main(),
				id,
				GetDownloadLocation(id),
				Spellchecker::DictPathByLangId(id),
				GetDownloadSize(id)));
		} else if (!toggled && state.is<Loading>()) {
			if (GlobalLoader && GlobalLoader->id() == id) {
				GlobalLoader->destroy();
			}
		}
	}, button->lifetime());

	return button;
}

void Inner::setupContent(Dictionaries enabledDictionaries) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto sets = Spellchecker::Dictionaries();
	for (const auto &set : sets) {
		const auto row = AddButtonWithLoader(
			content,
			set,
			ranges::contains(enabledDictionaries, set.id));
		row->toggledValue(
		) | rpl::start_with_next([=](auto enabled) {
			if (enabled && Spellchecker::DictionaryExists(set.id)) {
				_enabledRows.push_back(set.id);
			} else {
				auto &rows = _enabledRows;
				rows.erase(ranges::remove(rows, set.id), end(rows));
			}
		}, row->lifetime());
	}

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

} // namespace

ManageDictionariesBox::ManageDictionariesBox(
	QWidget*,
	not_null<Main::Session*> session)
: _session(session) {
}

void ManageDictionariesBox::prepare() {
	const auto inner = setInnerWidget(object_ptr<Inner>(
		this,
		_session->settings().dictionariesEnabled()));

	setTitle(tr::lng_settings_manage_dictionaries());

	addButton(tr::lng_settings_save(), [=] {
		_session->settings().setDictionariesEnabled(inner->enabledRows());
		_session->saveSettingsDelayed();
		closeBox();
	});
	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, inner);

	inner->heightValue(
	) | rpl::start_with_next([=](int height) {
		using std::min;
		setDimensions(st::boxWidth, min(height, st::boxMaxListHeight));
	}, inner->lifetime());
}

} // namespace Ui

#endif // !TDESKTOP_DISABLE_SPELLCHECK
