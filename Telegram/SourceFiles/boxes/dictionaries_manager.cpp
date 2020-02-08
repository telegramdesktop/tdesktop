/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/dictionaries_manager.h"

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "base/event_filter.h"
#include "chat_helpers/spellchecker_common.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/dedicated_file_loader.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"

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
		int size,
		Fn<void()> destroyCallback);

	void destroy() override;
	void unpack(const QString &path) override;

private:
	Fn<void()> _destroyCallback;

};

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent, Dictionaries enabledDictionaries);

	Dictionaries enabledRows() const;

private:
	void setupContent(Dictionaries enabledDictionaries);

	Dictionaries _enabledRows;

};

inline auto DictExists(int langId) {
	return Spellchecker::DictionaryExists(langId);
}

inline auto FilterEnabledDict(Dictionaries dicts) {
	return dicts | ranges::views::filter(
		DictExists
	) | ranges::to_vector;
}

DictState ComputeState(int id, bool enabled) {
	const auto result = enabled ? DictState(Active()) : DictState(Ready());
	if (DictExists(id)) {
		return result;
	}
	return Available{ Spellchecker::GetDownloadSize(id) };
}

QString StateDescription(const DictState &state) {
	return StateDescription(
		state,
		tr::lng_settings_manage_enabled_dictionary);
}

Loader::Loader(
	QObject *parent,
	int id,
	MTP::DedicatedLoader::Location location,
	const QString &folder,
	int size,
	Fn<void()> destroyCallback)
: BlobLoader(parent, id, location, folder, size)
, _destroyCallback(std::move(destroyCallback)) {
}

void Loader::unpack(const QString &path) {
	Expects(_destroyCallback);
	crl::async([=] {
		const auto success = Spellchecker::UnpackDictionary(path, id());
		if (success) {
			QFile(path).remove();
		}
		crl::on_main(success ? _destroyCallback : [=] { fail(); });
	});
}

void Loader::destroy() {
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
		const Spellchecker::Dict &dict,
		bool buttonEnabled) {
	const auto id = dict.id;
	buttonEnabled &= DictExists(id);

	const auto button = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(dict.name),
				st::dictionariesSectionButton
			)
		)
	)->entity();


	const auto localLoader = button->lifetime()
		.make_state<base::unique_qptr<Loader>>();
	const auto localLoaderValues = button->lifetime()
		.make_state<rpl::event_stream<Loader*>>();
	const auto setLocalLoader = [=](base::unique_qptr<Loader> loader) {
		*localLoader = std::move(loader);
		localLoaderValues->fire(localLoader->get());
	};
	const auto destroyLocalLoader = [=] {
		setLocalLoader(nullptr);
	};


	const auto buttonState = button->lifetime()
		.make_state<rpl::variable<DictState>>();
	const auto dictionaryRemoved = button->lifetime()
		.make_state<rpl::event_stream<>>();

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
			rpl::merge(
				dictionaryRemoved->events(),
				buttonState->value(
				) | rpl::filter([](const DictState &state) {
					return state.is<Failed>();
				}) | rpl::map([] {
					return rpl::empty_value();
				})
			) | rpl::map([]() {
				return false;
			})
		)
	);

	 *buttonState = localLoaderValues->events_starting_with(
	 	localLoader->get()
	 ) | rpl::map([=](Loader *loader) {
		return (loader && loader->id() == id)
			? loader->state()
			: rpl::single(
				buttonEnabled
			) | rpl::then(
				rpl::merge(
					dictionaryRemoved->events(
					) | rpl::map([] {
						return false;
					}),
					button->toggledValue()
				)
			) | rpl::map([=](auto enabled) {
				return ComputeState(id, enabled);
			});
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const DictState &state) {
		return !buttonState->current().is<Failed>() || !state.is<Available>();
	});

	button->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		const auto &state = buttonState->current();
		if (toggled && (state.is<Available>() || state.is<Failed>())) {
			const auto weak = Ui::MakeWeak(button);
			setLocalLoader(base::make_unique_q<Loader>(
				App::main(),
				id,
				Spellchecker::GetDownloadLocation(id),
				Spellchecker::DictPathByLangId(id),
				Spellchecker::GetDownloadSize(id),
				crl::guard(weak, destroyLocalLoader)));
		} else if (!toggled && state.is<Loading>()) {
			if (localLoader && localLoader->get()->id() == id) {
				destroyLocalLoader();
			}
		}
	}, button->lifetime());

	const auto contextMenu = button->lifetime()
		.make_state<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		if (!DictExists(id)) {
			return false;
		}
		*contextMenu = base::make_unique_q<Ui::PopupMenu>(button);
		contextMenu->get()->addAction(
			tr::lng_settings_manage_remove_dictionary(tr::now), [=] {
			Spellchecker::RemoveDictionary(id);
			dictionaryRemoved->fire({});
		});
		contextMenu->get()->popup(QCursor::pos());
		return true;
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	return button;
}

void Inner::setupContent(Dictionaries enabledDictionaries) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	for (const auto &dict : Spellchecker::Dictionaries()) {
		const auto id = dict.id;
		const auto row = AddButtonWithLoader(
			content,
			dict,
			ranges::contains(enabledDictionaries, id));
		row->toggledValue(
		) | rpl::start_with_next([=](auto enabled) {
			if (enabled) {
				_enabledRows.push_back(id);
			} else {
				auto &rows = _enabledRows;
				rows.erase(ranges::remove(rows, id), end(rows));
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

	// The initial list of enabled rows may differ from the list of languages
	// in settings, so we should store it when box opens
	// and save it when box closes (don't do it when "Save" was pressed).
	const auto initialEnabledRows = inner->enabledRows();

	setTitle(tr::lng_settings_manage_dictionaries());

	addButton(tr::lng_settings_save(), [=] {
		_session->settings().setDictionariesEnabled(
			FilterEnabledDict(inner->enabledRows()));
		_session->saveSettingsDelayed();
		// Ignore boxClosing() when the Save button was pressed.
		lifetime().destroy();
		closeBox();
	});
	addButton(tr::lng_close(), [=] { closeBox(); });

	boxClosing() | rpl::start_with_next([=] {
		_session->settings().setDictionariesEnabled(
			FilterEnabledDict(initialEnabledRows));
		_session->saveSettingsDelayed();
	}, lifetime());

	setDimensionsToContent(st::boxWidth, inner);

	inner->heightValue(
	) | rpl::start_with_next([=](int height) {
		using std::min;
		setDimensions(st::boxWidth, min(height, st::boxMaxListHeight));
	}, inner->lifetime());
}

} // namespace Ui

#endif // !TDESKTOP_DISABLE_SPELLCHECK
