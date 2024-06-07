/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_history_visibility_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

void EditPeerHistoryVisibilityBox(
		not_null<Ui::GenericBox*> box,
		bool isLegacy,
		Fn<void(HistoryVisibility)> savedCallback,
		HistoryVisibility historyVisibilitySavedValue) {
	const auto historyVisibility = std::make_shared<
		Ui::RadioenumGroup<HistoryVisibility>
	>(historyVisibilitySavedValue);

	const auto addButton = [=](
			not_null<Ui::RpWidget*> inner,
			HistoryVisibility v) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(inner.get());
		inner->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			button->resize(s);
		}, button->lifetime());
		button->setClickedCallback([=] { historyVisibility->setValue(v); });
	};

	box->setTitle(tr::lng_manage_history_visibility_title());
	box->addButton(tr::lng_settings_save(), [=] {
		savedCallback(historyVisibility->current());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	const auto visible = box->addRow(object_ptr<Ui::VerticalLayout>(box));
	visible->add(object_ptr<Ui::Radioenum<HistoryVisibility>>(
		box,
		historyVisibility,
		HistoryVisibility::Visible,
		tr::lng_manage_history_visibility_shown(tr::now),
		st::defaultBoxCheckbox));
	visible->add(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_manage_history_visibility_shown_about(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins);
	addButton(visible, HistoryVisibility::Visible);

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	const auto hidden = box->addRow(object_ptr<Ui::VerticalLayout>(box));
	hidden->add(object_ptr<Ui::Radioenum<HistoryVisibility>>(
		box,
		historyVisibility,
		HistoryVisibility::Hidden,
		tr::lng_manage_history_visibility_hidden(tr::now),
		st::defaultBoxCheckbox));
	hidden->add(
		object_ptr<Ui::FlatLabel>(
			box,
			(isLegacy
				? tr::lng_manage_history_visibility_hidden_legacy
				: tr::lng_manage_history_visibility_hidden_about)(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins);
	addButton(hidden, HistoryVisibility::Hidden);
}
