/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "poll/poll_link_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_polls.h"
#include "styles/style_widgets.h"

namespace Poll {

void AddPollOptionLinkBox(
		not_null<Ui::GenericBox*> box,
		const QString &initial,
		Fn<void(QString)> callback) {
	box->setTitle(tr::lng_polls_create_option_link_title());

	const auto content = box->verticalLayout();
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_polls_create_option_link_description(),
			st::defaultFlatLabel),
		st::boxRowPadding);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto field = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::createPollLinkUrlField,
			Ui::InputField::Mode::SingleLine,
			tr::lng_polls_create_option_link_placeholder(),
			initial),
		st::boxRowPadding);

	const auto submit = [=] {
		const auto url = field->getLastText().trimmed();
		if (url.isEmpty()) {
			field->showError();
			return;
		}
		const auto weak = base::make_weak(box);
		callback(url);
		if (weak) {
			box->closeBox();
		}
	};
	field->submits(
	) | rpl::on_next(submit, field->lifetime());

	box->addButton(tr::lng_box_done(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setFocusCallback([=] { field->setFocusFast(); });
}

} // namespace Poll
