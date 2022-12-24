/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/report_messages_box.h"

#include "api/api_report.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "lang/lang_keys.h"
#include "ui/boxes/report_box.h"
#include "ui/layers/generic_box.h"
#include "window/window_session_controller.h"

namespace {

[[nodiscard]] object_ptr<Ui::BoxContent> Report(
		not_null<PeerData*> peer,
		std::variant<v::null_t, MessageIdsList, not_null<PhotoData*>> data) {
	const auto source = v::match(data, [](const MessageIdsList &ids) {
		return Ui::ReportSource::Message;
	}, [&](not_null<PhotoData*> photo) {
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
	}, [](v::null_t) {
		Unexpected("Bad source report.");
		return Ui::ReportSource::Bot;
	});
	return Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ReportReasonBox(box, source, [=](Ui::ReportReason reason) {
			Ui::BoxShow(box).showBox(Box([=](not_null<Ui::GenericBox*> box) {
				const auto show = Ui::BoxShow(box);
				Ui::ReportDetailsBox(box, [=](const QString &text) {
					const auto toastParent = show.toastParent();
					Api::SendReport(toastParent, peer, reason, text, data);
					show.hideLayer();
				});
			}));
		});
	});
}

} // namespace

object_ptr<Ui::BoxContent> ReportItemsBox(
		not_null<PeerData*> peer,
		MessageIdsList ids) {
	return Report(peer, ids);
}

object_ptr<Ui::BoxContent> ReportProfilePhotoBox(
		not_null<PeerData*> peer,
		not_null<PhotoData*> photo) {
	return Report(peer, photo);
}

void ShowReportPeerBox(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	struct State {
		QPointer<Ui::BoxContent> reasonBox;
		QPointer<Ui::BoxContent> detailsBox;
		MessageIdsList ids;
	};
	const auto state = std::make_shared<State>();
	const auto chosen = [=](Ui::ReportReason reason) {
		const auto send = [=](const QString &text) {
			window->clearChooseReportMessages();
			Api::SendReport(
				Window::Show(window).toastParent(),
				peer,
				reason,
				text,
				std::move(state->ids));
			if (const auto strong = state->reasonBox.data()) {
				strong->closeBox();
			}
			if (const auto strong = state->detailsBox.data()) {
				strong->closeBox();
			}
		};
		if (reason == Ui::ReportReason::Fake
			|| reason == Ui::ReportReason::Other) {
			state->ids = {};
			state->detailsBox = window->show(Box(Ui::ReportDetailsBox, send));
			return;
		}
		window->showChooseReportMessages(peer, reason, [=](
				MessageIdsList ids) {
			state->ids = std::move(ids);
			state->detailsBox = window->show(Box(Ui::ReportDetailsBox, send));
		});
	};
	state->reasonBox = window->show(Box(
		Ui::ReportReasonBox,
		(peer->isBroadcast()
			? Ui::ReportSource::Channel
			: peer->isUser()
			? Ui::ReportSource::Bot
			: Ui::ReportSource::Group),
		chosen));
}
