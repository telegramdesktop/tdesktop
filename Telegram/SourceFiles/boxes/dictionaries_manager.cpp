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
#include "mainwidget.h"
#include "mtproto/dedicated_file_loader.h"
#include "spellcheck/spellcheck_utils.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "window/window_session_controller.h"

namespace Ui {
namespace {

using Dictionaries = std::vector<int>;
using namespace Storage::CloudBlob;

using Loading = MTP::DedicatedLoader::Progress;
using DictState = BlobState;
using QueryCallback = Fn<void(const QString &)>;
constexpr auto kMaxQueryLength = 15;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
#define OLD_QT
using QStringView = QString;
#endif

class Inner : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Dictionaries enabledDictionaries);

	Dictionaries enabledRows() const;
	QueryCallback queryCallback() const;

private:
	void setupContent(
		not_null<Window::SessionController*> controller,
		Dictionaries enabledDictionaries);

	Dictionaries _enabledRows;
	QueryCallback _queryCallback;

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

auto CreateMultiSelect(QWidget *parent) {
	const auto result = Ui::CreateChild<Ui::MultiSelect>(
		parent,
		st::defaultMultiSelect,
		tr::lng_participant_filter());

	result->resizeToWidth(st::boxWidth);
	result->moveToLeft(0, 0);
	return result;
}

Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Dictionaries enabledDictionaries)
: RpWidget(parent) {
	setupContent(controller, std::move(enabledDictionaries));
}

QueryCallback Inner::queryCallback() const {
	return _queryCallback;
}

Dictionaries Inner::enabledRows() const {
	return _enabledRows;
}

auto AddButtonWithLoader(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		const Spellchecker::Dict &dict,
		bool buttonEnabled,
		rpl::producer<QStringView> query) {
	const auto id = dict.id;
	buttonEnabled &= DictExists(id);

	const auto locale = Spellchecker::LocaleFromLangId(id);
	const std::vector<QString> indexList = {
		dict.name,
		QLocale::languageToString(locale.language()),
		QLocale::countryToString(locale.country())
	};

	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			object_ptr<Ui::SettingsButton>(
				content,
				rpl::single(dict.name),
				st::dictionariesSectionButton
			)
		)
	);
	const auto button = wrap->entity();

	std::move(
		query
	) | rpl::start_with_next([=](auto string) {
		wrap->toggle(
			ranges::any_of(indexList, [&](const QString &s) {
				return s.startsWith(string, Qt::CaseInsensitive);
			}),
			anim::type::instant);
	}, button->lifetime());

	using Loader = Spellchecker::DictLoader;
	using GlobalLoaderPtr = std::shared_ptr<base::unique_qptr<Loader>>;

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
	const auto dictionaryFromGlobalLoader = button->lifetime()
		.make_state<rpl::event_stream<>>();

	const auto globalLoader = button->lifetime()
		.make_state<GlobalLoaderPtr>();

	const auto rawGlobalLoaderPtr = [=]() -> Loader* {
		if (!globalLoader || !*globalLoader || !*globalLoader->get()) {
			return nullptr;
		}
		return globalLoader->get()->get();
	};

	const auto setGlobalLoaderPtr = [=](GlobalLoaderPtr loader) {
		if (localLoader->get()) {
			if (loader && loader->get()) {
				loader->get()->destroy();
			}
			return;
		}
		*globalLoader = std::move(loader);
		localLoaderValues->fire(rawGlobalLoaderPtr());
		if (rawGlobalLoaderPtr()) {
			dictionaryFromGlobalLoader->fire({});
		}
	};

	Spellchecker::GlobalLoaderChanged(
	) | rpl::start_with_next([=](int langId) {
		if (!langId && rawGlobalLoaderPtr()) {
			setGlobalLoaderPtr(nullptr);
		} else if (langId == id) {
			setGlobalLoaderPtr(Spellchecker::GlobalLoader());
		}
	}, button->lifetime());

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
		const auto isToggledSet = v::is<Active>(state);
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
				// Events to toggle on.
				dictionaryFromGlobalLoader->events() | rpl::map_to(true),
				// Events to toggle off.
				rpl::merge(
					dictionaryRemoved->events(),
					buttonState->value(
					) | rpl::filter([](const DictState &state) {
						return v::is<Failed>(state);
					}) | rpl::to_empty
				) | rpl::map_to(false)
			)
		)
	);

	*buttonState = localLoaderValues->events_starting_with(
		rawGlobalLoaderPtr() ? rawGlobalLoaderPtr() : localLoader->get()
	) | rpl::map([=](Loader *loader) {
		return (loader && loader->id() == id)
			? loader->state()
			: rpl::single(
				buttonEnabled
			) | rpl::then(
				rpl::merge(
					dictionaryRemoved->events() | rpl::map_to(false),
					button->toggledValue()
				)
			) | rpl::map([=](auto enabled) {
				return ComputeState(id, enabled);
			});
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const DictState &state) {
		return !v::is<Failed>(buttonState->current())
			|| !v::is<Available>(state);
	});

	button->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		const auto &state = buttonState->current();
		if (toggled && (v::is<Available>(state) || v::is<Failed>(state))) {
			const auto weak = Ui::MakeWeak(button);
			setLocalLoader(base::make_unique_q<Loader>(
				QCoreApplication::instance(),
				&controller->session(),
				id,
				Spellchecker::GetDownloadLocation(id),
				Spellchecker::DictPathByLangId(id),
				Spellchecker::GetDownloadSize(id),
				crl::guard(weak, destroyLocalLoader)));
		} else if (!toggled && v::is<Loading>(state)) {
			if (const auto g = rawGlobalLoaderPtr()) {
				g->destroy();
				return;
			}
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

	if (const auto g = Spellchecker::GlobalLoader()) {
		if (g.get() && g->get()->id() == id) {
			setGlobalLoaderPtr(g);
		}
	}

	return button;
}

void Inner::setupContent(
		not_null<Window::SessionController*> controller,
		Dictionaries enabledDictionaries) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto queryStream = content->lifetime()
		.make_state<rpl::event_stream<QStringView>>();

	for (const auto &dict : Spellchecker::Dictionaries()) {
		const auto id = dict.id;
		const auto row = AddButtonWithLoader(
			content,
			controller,
			dict,
			ranges::contains(enabledDictionaries, id),
			queryStream->events());
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

	_queryCallback = [=](const QString &query) {
		if (query.size() >= kMaxQueryLength) {
			return;
		}
		queryStream->fire_copy(query);
	};

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

} // namespace

ManageDictionariesBox::ManageDictionariesBox(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller) {
}

void ManageDictionariesBox::setInnerFocus() {
	_setInnerFocus();
}

void ManageDictionariesBox::prepare() {
	const auto multiSelect = CreateMultiSelect(this);

	const auto inner = setInnerWidget(
		object_ptr<Inner>(
			this,
			_controller,
			Core::App().settings().dictionariesEnabled()),
		st::boxScroll,
		multiSelect->height()
	);

	multiSelect->setQueryChangedCallback(inner->queryCallback());
	_setInnerFocus = [=] {
		multiSelect->setInnerFocus();
	};

	// The initial list of enabled rows may differ from the list of languages
	// in settings, so we should store it when box opens
	// and save it when box closes (don't do it when "Save" was pressed).
	const auto initialEnabledRows = inner->enabledRows();

	setTitle(tr::lng_settings_manage_dictionaries());

	addButton(tr::lng_settings_save(), [=] {
		Core::App().settings().setDictionariesEnabled(
			FilterEnabledDict(inner->enabledRows()));
		Core::App().saveSettingsDelayed();
		// Ignore boxClosing() when the Save button was pressed.
		lifetime().destroy();
		closeBox();
	});
	addButton(tr::lng_close(), [=] { closeBox(); });

	boxClosing() | rpl::start_with_next([=] {
		Core::App().settings().setDictionariesEnabled(
			FilterEnabledDict(initialEnabledRows));
		Core::App().saveSettingsDelayed();
	}, lifetime());

	setDimensionsToContent(st::boxWidth, inner);

	using namespace rpl::mappers;
	const auto max = lifetime().make_state<int>(0);
	rpl::combine(
		inner->heightValue(),
		multiSelect->heightValue(),
		_1 + _2
	) | rpl::start_with_next([=](int height) {
		using std::min;
		accumulate_max(*max, height);
		setDimensions(st::boxWidth, min(*max, st::boxMaxListHeight), true);
	}, inner->lifetime());
}

} // namespace Ui

#endif // !TDESKTOP_DISABLE_SPELLCHECK
