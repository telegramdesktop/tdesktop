/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/report_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/toast/toast.h"
#include "info/profile/info_profile_icon.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace Ui {
namespace {

constexpr auto kReportReasonLengthMax = 512;

using Source = ReportSource;
using Reason = ReportReason;

} // namespace

void ReportReasonBox(
		not_null<GenericBox*> box,
		const style::ReportBox &st,
		ReportSource source,
		Fn<void(Reason)> done) {
	box->setTitle([&] {
		switch (source) {
		case Source::Message: return tr::lng_report_message_title();
		case Source::Channel: return tr::lng_report_title();
		case Source::Group: return tr::lng_report_group_title();
		case Source::Bot: return tr::lng_report_bot_title();
		case Source::ProfilePhoto:
			return tr::lng_report_profile_photo_title();
		case Source::ProfileVideo:
			return tr::lng_report_profile_video_title();
		case Source::GroupPhoto: return tr::lng_report_group_photo_title();
		case Source::GroupVideo: return tr::lng_report_group_video_title();
		case Source::ChannelPhoto:
			return tr::lng_report_channel_photo_title();
		case Source::ChannelVideo:
			return tr::lng_report_channel_video_title();
		case Source::Story:
			return tr::lng_report_story();
		}
		Unexpected("'source' in ReportReasonBox.");
	}());
	auto margin = style::margins{ 0, st::reportReasonTopSkip, 0, 0 };
	const auto add = [&](
			Reason reason,
			tr::phrase<> text,
			const style::icon &icon) {
		const auto layout = box->verticalLayout();
		const auto button = layout->add(
			object_ptr<Ui::SettingsButton>(layout.get(), text(), st.button),
			margin);
		margin = {};
		button->setClickedCallback([=] {
			done(reason);
		});
		const auto height = st.button.padding.top()
			+ st.button.height
			+ st.button.padding.bottom();
		object_ptr<Info::Profile::FloatingIcon>(
			button,
			icon,
			QPoint{
				st::infoSharedMediaButtonIconPosition.x(),
				(height - icon.height()) / 2,
			});
	};
	add(Reason::Spam, tr::lng_report_reason_spam, st.spam);
	if (source == Source::Channel
		|| source == Source::Group
		|| source == Source::Bot) {
		add(Reason::Fake, tr::lng_report_reason_fake, st.fake);
	}
	add(
		Reason::Violence,
		tr::lng_report_reason_violence,
		st.violence);
	add(
		Reason::ChildAbuse,
		tr::lng_report_reason_child_abuse,
		st.children);
	add(
		Reason::Pornography,
		tr::lng_report_reason_pornography,
		st.pornography);
	add(
		Reason::Copyright,
		tr::lng_report_reason_copyright,
		st.copyright);
	if (source == Source::Message || source == Source::Story) {
		add(
			Reason::IllegalDrugs,
			tr::lng_report_reason_illegal_drugs,
			st.drugs);
		add(
			Reason::PersonalDetails,
			tr::lng_report_reason_personal_details,
			st.personal);
	}
	add(Reason::Other, tr::lng_report_reason_other, st.other);

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ReportDetailsBox(
		not_null<GenericBox*> box,
		const style::ReportBox &st,
		Fn<void(QString)> done) {
	box->addRow(
		object_ptr<FlatLabel>(
			box, // #TODO reports
			tr::lng_report_details_about(),
			st.label),
		{
			st::boxRowPadding.left(),
			st::boxPadding.top(),
			st::boxRowPadding.right(),
			st::boxPadding.bottom() });
	const auto details = box->addRow(
		object_ptr<InputField>(
			box,
			st.field,
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
	details->submits(
	) | rpl::start_with_next(submit, details->lifetime());
	box->addButton(tr::lng_report_button(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Ui
