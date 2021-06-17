/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/report_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/toast/toast.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"

namespace Ui {
namespace {

constexpr auto kReportReasonLengthMax = 512;

using Source = ReportSource;
using Reason = ReportReason;

} // namespace

void ReportReasonBox(
		not_null<GenericBox*> box,
		ReportSource source,
		Fn<void(Reason)> done) {
	box->setTitle([&] {
		switch (source) {
		case Source::Message: return tr::lng_report_message_title();
		case Source::Channel: return tr::lng_report_title();
		case Source::Group: return tr::lng_report_group_title();
		case Source::Bot: return tr::lng_report_bot_title();
		}
		Unexpected("'source' in ReportReasonBox.");
	}());
	const auto add = [&](Reason reason, tr::phrase<> text) {
		const auto layout = box->verticalLayout();
		const auto button = layout->add(
			object_ptr<SettingsButton>(layout, text()));
		button->setClickedCallback([=] {
			done(reason);
		});
	};
	add(Reason::Spam, tr::lng_report_reason_spam);
	if (source != Source::Message) {
		add(Reason::Fake, tr::lng_report_reason_fake);
	}
	add(Reason::Violence, tr::lng_report_reason_violence);
	add(Reason::ChildAbuse, tr::lng_report_reason_child_abuse);
	add(Reason::Pornography, tr::lng_report_reason_pornography);
	add(Reason::Other, tr::lng_report_reason_other);

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ReportDetailsBox(
		not_null<GenericBox*> box,
		Fn<void(QString)> done) {
	box->addRow(
		object_ptr<FlatLabel>(
			box, // #TODO reports
			tr::lng_report_details_about(),
			st::boxLabel),
		{
			st::boxRowPadding.left(),
			st::boxPadding.top(),
			st::boxRowPadding.right(),
			st::boxPadding.bottom() });
	const auto details = box->addRow(
		object_ptr<InputField>(
			box,
			st::newGroupDescription,
			InputField::Mode::MultiLine,
			tr::lng_report_details(),
			QString()));
	details->setMaxLength(kReportReasonLengthMax);
	box->setFocusCallback([=] {
		details->setFocusFast();
	});

	const auto submit = [=] {
		const auto text = details->getLastText();
		done(text);
	};
	QObject::connect(details, &InputField::submitted, submit);
	box->addButton(tr::lng_report_button(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Ui
