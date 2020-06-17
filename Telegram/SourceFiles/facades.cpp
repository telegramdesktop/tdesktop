/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "facades.h"

#include "info/info_memento.h"
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
#include "boxes/confirm_box.h"
#include "boxes/url_auth_box.h"
#include "ui/layers/layer_widget.h"
#include "lang/lang_keys.h"
#include "base/observer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "styles/style_history.h"
#include "data/data_session.h"

namespace App {

void sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd, MsgId replyTo) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == &peer->session()) {
			m->sendBotCommand(peer, bot, cmd, replyTo);
		}
	}
}

void hideSingleUseKeyboard(not_null<const HistoryItem*> message) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == &message->history()->session()) {
			m->hideSingleUseKeyboard(message->history()->peer, message->id);
		}
	}
}

bool insertBotCommand(const QString &cmd) {
	if (const auto m = App::main()) { // multi good
		return m->insertBotCommand(cmd);
	}
	return false;
}

void activateBotCommand(
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	const auto button = HistoryMessageMarkupButton::Get(
		&msg->history()->owner(),
		msg->fullId(),
		row,
		column);
	if (!button) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		MsgId replyTo = (msg->id > 0) ? msg->id : 0;
		sendBotCommand(
			msg->history()->peer,
			msg->fromOriginal()->asUser(),
			QString(button->text),
			replyTo);
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		if (const auto m = App::main()) { // multi good
			if (&m->session() == &msg->history()->session()) {
				m->app_sendBotCallback(button, msg, row, column);
			}
		}
	} break;

	case ButtonType::Buy: {
		Ui::show(Box<InformBox>(tr::lng_payments_not_supported(tr::now)));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (const auto bot = msg->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		if (skipConfirmation) {
			UrlClickHandler::Open(url);
		} else {
			HiddenUrlClickHandler::Open(url);
		}
	} break;

	case ButtonType::RequestLocation: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<InformBox>(
			tr::lng_bot_share_location_unavailable(tr::now)));
	} break;

	case ButtonType::RequestPhone: {
		hideSingleUseKeyboard(msg);
		const auto msgId = msg->id;
		const auto history = msg->history();
		Ui::show(Box<ConfirmBox>(tr::lng_bot_share_phone(tr::now), tr::lng_bot_share_phone_confirm(tr::now), [=] {
			Ui::showPeerHistory(history, ShowAtTheEndMsgId);
			auto action = Api::SendAction(history);
			action.clearDraft = false;
			action.replyTo = msgId;
			history->session().api().shareContact(
				history->session().user(),
				action);
		}));
	} break;

	case ButtonType::RequestPoll: {
		hideSingleUseKeyboard(msg);
		auto chosen = PollData::Flags();
		auto disabled = PollData::Flags();
		if (!button->data.isEmpty()) {
			disabled |= PollData::Flag::Quiz;
			if (button->data[0]) {
				chosen |= PollData::Flag::Quiz;
			}
		}
		if (const auto m = App::main()) { // multi good
			if (&m->session() == &msg->history()->session()) {
				Window::PeerMenuCreatePoll(m->controller(), msg->history()->peer, chosen, disabled);
			}
		}
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		const auto session = &msg->history()->session();
		if (const auto m = App::main()) { // multi good
			if (&m->session() == session) {
				if (const auto bot = msg->getMessageBot()) {
					const auto fastSwitchDone = [&] {
						auto samePeer = (button->type == ButtonType::SwitchInlineSame);
						if (samePeer) {
							Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data), bot, msg->id);
							return true;
						} else if (bot->isBot() && bot->botInfo->inlineReturnPeerId) {
							if (Notify::switchInlineBotButtonReceived(session, QString::fromUtf8(button->data))) {
								return true;
							}
						}
						return false;
					}();
					if (!fastSwitchDone) {
						m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
					}
				}
			}
		}
	} break;

	case ButtonType::Auth:
		UrlAuthBox::Activate(msg, row, column);
		break;
	}
}

void searchByHashtag(const QString &tag, PeerData *inPeer) {
	if (const auto m = App::main()) { // multi good
		if (!inPeer || &m->session() == &inPeer->session()) {
			if (m->controller()->openedFolder().current()) {
				m->controller()->closeFolder();
			}
			Ui::hideSettingsAndLayer();
			Core::App().hideMediaView();
			m->searchMessages(
				tag + ' ',
				(inPeer && !inPeer->isUser())
				? inPeer->owner().history(inPeer).get()
				: Dialogs::Key());
		}
	}
}

void showSettings() {
	if (auto w = App::wnd()) {
		w->showSettings();
	}
}

} // namespace App

namespace Ui {

void showPeerProfile(const PeerId &peer) {
	if (const auto window = App::wnd()) {
		if (const auto controller = window->sessionController()) {
			controller->showPeerInfo(peer);
		}
	}
}
void showPeerProfile(const PeerData *peer) {
	showPeerProfile(peer->id);
}

void showPeerProfile(not_null<const History*> history) {
	showPeerProfile(history->peer->id);
}

void showPeerHistory(
		const PeerId &peer,
		MsgId msgId) {
	if (const auto m = App::main()) { // multi good
		m->ui_showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			msgId);
	}
}

void showPeerHistoryAtItem(not_null<const HistoryItem*> item) {
	showPeerHistory(item->history()->peer, item->id);
}

void showPeerHistory(not_null<const History*> history, MsgId msgId) {
	showPeerHistory(history->peer, msgId);
}

void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == &peer->session()) {
			m->ui_showPeerHistory(
				peer->id,
				Window::SectionShow::Way::ClearStack,
				msgId);
		}
	}
}

PeerData *getPeerForMouseAction() {
	return Core::App().ui_getPeerForMouseAction();
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

namespace Notify {

void replyMarkupUpdated(not_null<const HistoryItem*> item) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == &item->history()->session()) {
			m->notify_replyMarkupUpdated(item);
		}
	}
}

void inlineKeyboardMoved(
		not_null<const HistoryItem*> item,
		int oldKeyboardTop,
		int newKeyboardTop) {
	if (const auto m = App::main()) { // multi good
		if (&m->session() == &item->history()->session()) {
			m->notify_inlineKeyboardMoved(
				item,
				oldKeyboardTop,
				newKeyboardTop);
		}
	}
}

bool switchInlineBotButtonReceived(
		not_null<Main::Session*> session,
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	if (const auto m = App::main()) { // multi good
		if (session == &m->session()) {
			return m->notify_switchInlineBotButtonReceived(
				query,
				samePeerBot,
				samePeerReplyTo);
		}
	}
	return false;
}

void unreadCounterUpdated() {
	Global::RefHandleUnreadCounterUpdate().call();
}

} // namespace Notify

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::" #Name); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Ref" #Name); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	AssertCustom(Namespace##Data != nullptr, #Namespace "Data != nullptr in " #Namespace "::Set" #Name); \
	Namespace##Data->Name = Name; \
}

namespace Global {
namespace internal {

struct Data {
	SingleQueuedInvokation HandleUnreadCounterUpdate = { [] { Core::App().call_handleUnreadCounterUpdate(); } };

	Adaptive::WindowLayout AdaptiveWindowLayout = Adaptive::WindowLayout::Normal;
	Adaptive::ChatLayout AdaptiveChatLayout = Adaptive::ChatLayout::Normal;
	bool AdaptiveForWide = true;
	base::Observable<void> AdaptiveChanged;

	bool DialogsFiltersEnabled = false;
	bool ModerateModeEnabled = false;

	bool ScreenIsLocked = false;

	int32 DebugLoggingFlags = 0;

	float64 RememberedSongVolume = kDefaultVolume;
	float64 SongVolume = kDefaultVolume;
	base::Observable<void> SongVolumeChanged;
	float64 VideoVolume = kDefaultVolume;
	base::Observable<void> VideoVolumeChanged;

	HiddenPinnedMessagesMap HiddenPinnedMessages;

	bool AskDownloadPath = false;
	QString DownloadPath;
	QByteArray DownloadPathBookmark;
	base::Observable<void> DownloadPathChanged;

	bool VoiceMsgPlaybackDoubled = false;
	bool SoundNotify = true;
	bool DesktopNotify = true;
	bool FlashBounceNotify = true;
	bool RestoreSoundNotifyFromTray = false;
	bool RestoreFlashBounceNotifyFromTray = false;
	DBINotifyView NotifyView = dbinvShowPreview;
	bool NativeNotifications = false;
	int NotificationsCount = 3;
	Notify::ScreenCorner NotificationsCorner = Notify::ScreenCorner::BottomRight;
	bool NotificationsDemoIsShown = false;

	bool TryIPv6 = !Platform::IsWindows();
	std::vector<MTP::ProxyData> ProxiesList;
	MTP::ProxyData SelectedProxy;
	MTP::ProxyData::Settings ProxySettings = MTP::ProxyData::Settings::System;
	bool UseProxyForCalls = false;
	base::Observable<void> ConnectionTypeChanged;

	int AutoLock = 3600;
	bool LocalPasscode = false;
	base::Observable<void> LocalPasscodeChanged;

	base::Variable<DBIWorkMode> WorkMode = { dbiwmWindowAndTray };

	base::Observable<void> UnreadCounterUpdate;
	base::Observable<void> PeerChooseCancel;

	QString CallOutputDeviceID = qsl("default");
	QString CallInputDeviceID = qsl("default");
	int CallOutputVolume = 100;
	int CallInputVolume = 100;
	bool CallAudioDuckingEnabled = true;
};

} // namespace internal
} // namespace Global

Global::internal::Data *GlobalData = nullptr;

namespace Global {

bool started() {
	return GlobalData != nullptr;
}

void start() {
	GlobalData = new internal::Data();
}

void finish() {
	delete GlobalData;
	GlobalData = nullptr;
}

DefineRefVar(Global, SingleQueuedInvokation, HandleUnreadCounterUpdate);

DefineVar(Global, Adaptive::WindowLayout, AdaptiveWindowLayout);
DefineVar(Global, Adaptive::ChatLayout, AdaptiveChatLayout);
DefineVar(Global, bool, AdaptiveForWide);
DefineRefVar(Global, base::Observable<void>, AdaptiveChanged);

DefineVar(Global, bool, DialogsFiltersEnabled);
DefineVar(Global, bool, ModerateModeEnabled);

DefineVar(Global, bool, ScreenIsLocked);

DefineVar(Global, int32, DebugLoggingFlags);

DefineVar(Global, float64, RememberedSongVolume);
DefineVar(Global, float64, SongVolume);
DefineRefVar(Global, base::Observable<void>, SongVolumeChanged);
DefineVar(Global, float64, VideoVolume);
DefineRefVar(Global, base::Observable<void>, VideoVolumeChanged);

DefineVar(Global, HiddenPinnedMessagesMap, HiddenPinnedMessages);

DefineVar(Global, bool, AskDownloadPath);
DefineVar(Global, QString, DownloadPath);
DefineVar(Global, QByteArray, DownloadPathBookmark);
DefineRefVar(Global, base::Observable<void>, DownloadPathChanged);

DefineVar(Global, bool, VoiceMsgPlaybackDoubled);
DefineVar(Global, bool, SoundNotify);
DefineVar(Global, bool, DesktopNotify);
DefineVar(Global, bool, FlashBounceNotify);
DefineVar(Global, bool, RestoreSoundNotifyFromTray);
DefineVar(Global, bool, RestoreFlashBounceNotifyFromTray);
DefineVar(Global, DBINotifyView, NotifyView);
DefineVar(Global, bool, NativeNotifications);
DefineVar(Global, int, NotificationsCount);
DefineVar(Global, Notify::ScreenCorner, NotificationsCorner);
DefineVar(Global, bool, NotificationsDemoIsShown);

DefineVar(Global, bool, TryIPv6);
DefineVar(Global, std::vector<MTP::ProxyData>, ProxiesList);
DefineVar(Global, MTP::ProxyData, SelectedProxy);
DefineVar(Global, MTP::ProxyData::Settings, ProxySettings);
DefineVar(Global, bool, UseProxyForCalls);
DefineRefVar(Global, base::Observable<void>, ConnectionTypeChanged);

DefineVar(Global, int, AutoLock);
DefineVar(Global, bool, LocalPasscode);
DefineRefVar(Global, base::Observable<void>, LocalPasscodeChanged);

DefineRefVar(Global, base::Variable<DBIWorkMode>, WorkMode);

DefineRefVar(Global, base::Observable<void>, UnreadCounterUpdate);
DefineRefVar(Global, base::Observable<void>, PeerChooseCancel);

DefineVar(Global, QString, CallOutputDeviceID);
DefineVar(Global, QString, CallInputDeviceID);
DefineVar(Global, int, CallOutputVolume);
DefineVar(Global, int, CallInputVolume);
DefineVar(Global, bool, CallAudioDuckingEnabled);

} // namespace Global
