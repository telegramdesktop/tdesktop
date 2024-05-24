/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/edit_factcheck_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

void EditFactcheckBox(
		not_null<Ui::GenericBox*> box,
		TextWithEntities current,
		int limit,
		Fn<void(TextWithEntities)> save,
		Fn<void(not_null<Ui::InputField*>)> initField) {
	box->setTitle(tr::lng_factcheck_title());

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::factcheckField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_factcheck_placeholder(),
		TextWithTags{
			current.text,
			TextUtilities::ConvertEntitiesToTextTags(current.entities)
		}));
	AddLengthLimitLabel(field, limit);
	initField(field);

	enum class State {
		Initial,
		Changed,
		Removed,
	};
	const auto state = box->lifetime().make_state<rpl::variable<State>>(
		State::Initial);
	field->changes() | rpl::start_with_next([=] {
		const auto now = field->getLastText().trimmed();
		*state = !now.isEmpty()
			? State::Changed
			: current.empty()
			? State::Initial
			: State::Removed;
	}, field->lifetime());

	state->value() | rpl::start_with_next([=](State state) {
		box->clearButtons();
		if (state == State::Removed) {
			box->addButton(tr::lng_box_remove(), [=] {
				box->closeBox();
				save({});
			}, st::attentionBoxButton);
		} else if (state == State::Initial) {
			box->addButton(tr::lng_settings_save(), [=] {
				if (current.empty()) {
					field->showError();
				} else {
					box->closeBox();
				}
			});
		} else {
			box->addButton(tr::lng_settings_save(), [=] {
				auto result = field->getTextWithAppliedMarkdown();
				if (result.text.size() > limit) {
					field->showError();
					return;
				}

				box->closeBox();
				save({
					result.text,
					TextUtilities::ConvertTextTagsToEntities(result.tags)
				});
			});
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}, box->lifetime());

	box->setFocusCallback([=] {
		field->setFocusFast();
	});
}
