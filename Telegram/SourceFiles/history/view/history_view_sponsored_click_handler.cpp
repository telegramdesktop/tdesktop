/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_sponsored_click_handler.h"

#include "api/api_chat_invite.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "data/data_session.h"
#include "data/data_sponsored_messages.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace HistoryView {

ClickHandlerPtr SponsoredLink(const QString &externalLink) {
	if (!externalLink.isEmpty()) {
		class ClickHandler : public UrlClickHandler {
		public:
			using UrlClickHandler::UrlClickHandler;

			QString copyToClipboardContextItemText() const override {
				return QString();
			}

		};

		return std::make_shared<ClickHandler>(externalLink, false);
	} else {
		return std::make_shared<LambdaClickHandler>([](ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			const auto controller = my.sessionWindow.get();
			if (!controller) {
				return;
			}
			const auto &data = controller->session().data();
			const auto details = data.sponsoredMessages().lookupDetails(
				my.itemId);
			if (!details.externalLink.isEmpty()) {
				File::OpenUrl(details.externalLink);
			} else if (details.hash) {
				Api::CheckChatInvite(controller, *details.hash);
			} else if (details.botLinkInfo) {
				controller->showPeerByLink(*details.botLinkInfo);
			} else if (details.peer) {
				controller->showPeerHistory(
					details.peer,
					Window::SectionShow::Way::Forward,
					details.msgId);
			}
		});
	}
}

} // namespace HistoryView
