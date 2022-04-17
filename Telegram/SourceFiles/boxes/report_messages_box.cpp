/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/report_messages_box.h"

#include "api/api_report.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/boxes/report_box.h"
#include "ui/layers/generic_box.h"
#include "window/window_session_controller.h"

object_ptr<Ui::BoxContent> ReportItemsBox(
		not_null<PeerData*> peer,
		MessageIdsList ids) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		using Source = Ui::ReportSource;
		using Reason = Ui::ReportReason;
		Ui::ReportReasonBox(box, Source::Message, [=](Reason reason) {
			Ui::BoxShow(box).showBox(Box([=](not_null<Ui::GenericBox*> box) {
				const auto show = Ui::BoxShow(box);
				Ui::ReportDetailsBox(box, [=](const QString &text) {
					Api::SendReport(peer, reason, text, ids);
					show.hideLayer();
				});
			}));
		});
	});
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
			Api::SendReport(peer, reason, text, std::move(state->ids));
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
