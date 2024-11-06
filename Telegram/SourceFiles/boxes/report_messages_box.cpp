/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/report_messages_box.h"

#include "api/api_report.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "lang/lang_keys.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace {

[[nodiscard]] object_ptr<Ui::BoxContent> ReportPhoto(
		not_null<PeerData*> peer,
		not_null<PhotoData*> photo,
		const style::ReportBox *stOverride) {
	const auto source = [&] {
		return peer->isUser()
			? (photo->hasVideo()
				? Ui::ReportSource::ProfileVideo
				: Ui::ReportSource::ProfilePhoto)
			: (peer->isChat() || (peer->isChannel() && peer->isMegagroup()))
			? (photo->hasVideo()
				? Ui::ReportSource::GroupVideo
				: Ui::ReportSource::GroupPhoto)
			: (photo->hasVideo()
				? Ui::ReportSource::ChannelVideo
				: Ui::ReportSource::ChannelPhoto);
	}();
	const auto st = stOverride ? stOverride : &st::defaultReportBox;
	return Box([=](not_null<Ui::GenericBox*> box) {
		const auto show = box->uiShow();
		Ui::ReportReasonBox(box, *st, source, [=](Ui::ReportReason reason) {
			show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
				Ui::ReportDetailsBox(box, *st, [=](const QString &text) {
					Api::SendPhotoReport(show, peer, reason, text, photo);
					show->hideLayer();
				});
			}));
		});
	});
}

} // namespace

object_ptr<Ui::BoxContent> ReportProfilePhotoBox(
		not_null<PeerData*> peer,
		not_null<PhotoData*> photo) {
	return ReportPhoto(peer, photo, nullptr);
}

void ShowReportMessageBox(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		const std::vector<MsgId> &ids,
		const std::vector<StoryId> &stories,
		const style::ReportBox *stOverride) {
	const auto report = Api::CreateReportMessagesOrStoriesCallback(
		show,
		peer);

	auto performRequest = [=](
			const auto &repeatRequest,
			Data::ReportInput reportInput) -> void {
		report(reportInput, [=](const Api::ReportResult &result) {
			if (!result.error.isEmpty()) {
				if (result.error == u"MESSAGE_ID_REQUIRED"_q) {
					const auto widget = show->toastParent();
					const auto window = Core::App().findWindow(widget);
					const auto controller = window
						? window->sessionController()
						: nullptr;
					if (controller) {
						const auto callback = [=](std::vector<MsgId> ids) {
							auto copy = reportInput;
							copy.ids = std::move(ids);
							repeatRequest(repeatRequest, std::move(copy));
						};
						controller->showChooseReportMessages(
							peer,
							reportInput,
							std::move(callback));
					}
				} else {
					show->showToast(result.error);
				}
				return;
			}
			if (!result.options.empty() || result.commentOption) {
				show->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(
						rpl::single(
							result.title.isEmpty()
								? reportInput.optionText
								: result.title));

					for (const auto &option : result.options) {
						const auto button = Ui::AddReportOptionButton(
							box->verticalLayout(),
							option.text,
							stOverride);
						button->setClickedCallback([=] {
							auto copy = reportInput;
							copy.optionId = option.id;
							copy.optionText = option.text;
							repeatRequest(repeatRequest, std::move(copy));
						});
					}
					if (const auto commentOption = result.commentOption) {
						constexpr auto kReportReasonLengthMax = 512;
						const auto &st = stOverride
							? stOverride
							: &st::defaultReportBox;
						Ui::AddReportDetailsIconButton(box);
						Ui::AddSkip(box->verticalLayout());
						Ui::AddSkip(box->verticalLayout());
						const auto details = box->addRow(
							object_ptr<Ui::InputField>(
								box,
								st->field,
								Ui::InputField::Mode::MultiLine,
								commentOption->optional
									? tr::lng_report_details_optional()
									: tr::lng_report_details_non_optional(),
								QString()));
						Ui::AddSkip(box->verticalLayout());
						Ui::AddSkip(box->verticalLayout());
						{
							const auto container = box->verticalLayout();
							auto label = object_ptr<Ui::FlatLabel>(
								container,
								tr::lng_report_details_message_about(),
								st::boxDividerLabel);
							label->setTextColorOverride(st->dividerFg->c);
							using namespace Ui;
							const auto widget = container->add(
								object_ptr<PaddingWrap<>>(
									container,
									std::move(label),
									st::defaultBoxDividerLabelPadding));
							const auto background
								= CreateChild<BoxContentDivider>(
									widget,
									st::boxDividerHeight,
									st->dividerBg,
									RectPart::Top | RectPart::Bottom);
							background->lower();
							widget->sizeValue(
							) | rpl::start_with_next([=](const QSize &s) {
								background->resize(s);
							}, background->lifetime());
						}
						details->setMaxLength(kReportReasonLengthMax);
						box->setFocusCallback([=] {
							details->setFocusFast();
						});
						const auto submit = [=] {
							if (!commentOption->optional
								&& details->empty()) {
								details->showError();
								details->setFocus();
								return;
							}
							auto copy = reportInput;
							copy.optionId = commentOption->id;
							copy.comment = details->getLastText();
							repeatRequest(repeatRequest, std::move(copy));
						};
						details->submits(
						) | rpl::start_with_next(submit, details->lifetime());
						box->addButton(tr::lng_report_button(), submit);
					} else {
						box->addButton(
							tr::lng_close(),
							[=] { show->hideLayer(); });
					}
					if (!reportInput.optionId.isNull()) {
						box->addLeftButton(
							tr::lng_create_group_back(),
							[=] { box->closeBox(); });
					}
				}));
			} else if (result.successful) {
				constexpr auto kToastDuration = crl::time(4000);
				show->showToast(
					tr::lng_report_thanks(tr::now),
					kToastDuration);
				show->hideLayer();
			}
		});
	};
	performRequest(performRequest, { .ids = ids, .stories = stories });
}
