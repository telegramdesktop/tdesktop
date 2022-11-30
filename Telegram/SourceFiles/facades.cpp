/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "facades.h"

#include "api/api_bot.h"
#include "info/info_memento.h"
#include "inline_bots/bot_attach_web_view.h"
#include "core/click_handler_types.h"
#include "core/application.h"
#include "media/clip/media_clip_reader.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "history/history_item_components.h"
#include "base/platform/base_platform_info.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/url_auth_box.h"
#include "ui/layers/layer_widget.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "payments/payments_checkout_process.h"
#include "data/data_session.h"
#include "styles/style_chat.h"

namespace Ui {

void showChatsList(not_null<Main::Session*> session) {
	if (const auto window = session->tryResolveWindow()) {
		window->clearSectionStack();
	}
}

void showPeerHistory(not_null<History*> history, MsgId msgId) {
	if (const auto window = history->session().tryResolveWindow()) {
		window->showPeerHistory(
			history,
			::Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

void showPeerHistory(not_null<PeerData*> peer, MsgId msgId) {
	if (const auto window = peer->session().tryResolveWindow()) {
		window->showPeerHistory(
			peer,
			::Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

bool skipPaintEvent(QWidget *widget, QPaintEvent *event) {
	if (auto w = App::wnd()) {
		if (w->contentOverlapped(widget, event)) {
			return true;
		}
	}
	return false;
}

} // namespace Ui
