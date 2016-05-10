/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"

#include "mainwindow.h"
#include "mainwidget.h"
#include "application.h"
#include "core/click_handler_types.h"
#include "boxes/confirmbox.h"
#include "layerwidget.h"
#include "lang.h"

Q_DECLARE_METATYPE(ClickHandlerPtr);
Q_DECLARE_METATYPE(Qt::MouseButton);

namespace App {

void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) {
	if (auto m = main()) {
		m->sendBotCommand(peer, bot, cmd, replyTo);
	}
}

bool insertBotCommand(const QString &cmd, bool specialGif) {
	if (auto m = main()) {
		return m->insertBotCommand(cmd, specialGif);
	}
	return false;
}

void activateBotCommand(const HistoryItem *msg, int row, int col) {
	const HistoryMessageReplyMarkup::Button *button = nullptr;
	if (auto markup = msg->Get<HistoryMessageReplyMarkup>()) {
		if (row < markup->rows.size()) {
			const auto &buttonRow(markup->rows.at(row));
			if (col < buttonRow.size()) {
				button = &buttonRow.at(col);
			}
		}
	}
	if (!button) return;

	switch (button->type) {
	case HistoryMessageReplyMarkup::Button::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		MsgId replyTo = (msg->id > 0) ? msg->id : 0;
		sendBotCommand(msg->history()->peer, msg->fromOriginal()->asUser(), QString(button->text), replyTo);
	} break;

	case HistoryMessageReplyMarkup::Button::Callback: {
		if (MainWidget *m = main()) {
			m->app_sendBotCallback(button, msg, row, col);
		}
	} break;

	case HistoryMessageReplyMarkup::Button::Url: {
		auto url = QString::fromUtf8(button->data);
		HiddenUrlClickHandler(url).onClick(Qt::LeftButton);
	} break;

	case HistoryMessageReplyMarkup::Button::RequestLocation: {
		Ui::showLayer(new InformBox(lang(lng_bot_share_location_unavailable)));
	} break;

	case HistoryMessageReplyMarkup::Button::RequestPhone: {
		SharePhoneConfirmBox *box = new SharePhoneConfirmBox(msg->history()->peer);
		box->connect(box, SIGNAL(confirmed(PeerData*)), App::main(), SLOT(onSharePhoneWithBot(PeerData*)));
		Ui::showLayer(box);
	} break;

	case HistoryMessageReplyMarkup::Button::SwitchInline: {
		if (auto m = App::main()) {
			auto getMessageBot = [msg]() -> UserData* {
				if (auto bot = msg->viaBot()) {
					return bot;
				} else if (auto bot = msg->history()->peer->asUser()) {
					return bot;
				}
				return nullptr;
			};
			if (auto bot = getMessageBot()) {
				auto tryFastSwitch = [bot, &button]() -> bool {
					if (bot->botInfo && bot->botInfo->inlineReturnPeerId) {
						if (Notify::switchInlineBotButtonReceived(QString::fromUtf8(button->data))) {
							return true;
						}
					}
					return false;
				};
				if (!tryFastSwitch()) {
					m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
				}
			}
		}
	} break;
	}
}

void searchByHashtag(const QString &tag, PeerData *inPeer) {
	if (MainWidget *m = main()) m->searchMessages(tag + ' ', (inPeer && inPeer->isChannel() && !inPeer->isMegagroup()) ? inPeer : 0);
}

void openPeerByName(const QString &username, MsgId msgId, const QString &startToken) {
	if (MainWidget *m = main()) m->openPeerByName(username, msgId, startToken);
}

void joinGroupByHash(const QString &hash) {
	if (MainWidget *m = main()) m->joinGroupByHash(hash);
}

void stickersBox(const QString &name) {
	if (MainWidget *m = main()) m->stickersBox(MTP_inputStickerSetShortName(MTP_string(name)));
}

void openLocalUrl(const QString &url) {
	if (MainWidget *m = main()) m->openLocalUrl(url);
}

bool forward(const PeerId &peer, ForwardWhatMessages what) {
	if (MainWidget *m = main()) return m->onForward(peer, what);
	return false;
}

void removeDialog(History *history) {
	if (MainWidget *m = main()) {
		m->removeDialog(history);
	}
}

void showSettings() {
	if (auto w = wnd()) {
		w->showSettings();
	}
}

void activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button) {
	if (auto w = wnd()) {
		qRegisterMetaType<ClickHandlerPtr>();
		qRegisterMetaType<Qt::MouseButton>();
		QMetaObject::invokeMethod(w, "app_activateClickHandler", Qt::QueuedConnection, Q_ARG(ClickHandlerPtr, handler), Q_ARG(Qt::MouseButton, button));
	}
}

void logOutDelayed() {
	if (auto w = App::wnd()) {
		QMetaObject::invokeMethod(w, "onLogoutSure", Qt::QueuedConnection);
	}
}

} // namespace App

namespace Ui {

void showMediaPreview(DocumentData *document) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(document);
	}
}

void showMediaPreview(PhotoData *photo) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(photo);
	}
}

void hideMediaPreview() {
	if (auto w = App::wnd()) {
		w->ui_hideMediaPreview();
	}
}

void showLayer(LayeredWidget *box, ShowLayerOptions options) {
	if (auto w = App::wnd()) {
		w->ui_showLayer(box, options);
	} else {
		delete box;
	}
}

void hideLayer(bool fast) {
	if (auto w = App::wnd()) w->ui_showLayer(0, ShowLayerOptions(CloseOtherLayers) | (fast ? ForceFastShowLayer : AnimatedShowLayer));
}

bool isLayerShown() {
	if (auto w = App::wnd()) return w->ui_isLayerShown();
	return false;
}

bool isMediaViewShown() {
	if (auto w = App::wnd()) return w->ui_isMediaViewShown();
	return false;
}

bool isInlineItemBeingChosen() {
	if (MainWidget *m = App::main()) return m->ui_isInlineItemBeingChosen();
	return false;
}

void repaintHistoryItem(const HistoryItem *item) {
	if (!item) return;
	if (MainWidget *m = App::main()) m->ui_repaintHistoryItem(item);
}

void repaintInlineItem(const InlineBots::Layout::ItemBase *layout) {
	if (!layout) return;
	if (MainWidget *m = App::main()) m->ui_repaintInlineItem(layout);
}

bool isInlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	if (MainWidget *m = App::main()) return m->ui_isInlineItemVisible(layout);
	return false;
}

void autoplayMediaInlineAsync(const FullMsgId &msgId) {
	if (MainWidget *m = App::main()) {
		QMetaObject::invokeMethod(m, "ui_autoplayMediaInlineAsync", Qt::QueuedConnection, Q_ARG(qint32, msgId.channel), Q_ARG(qint32, msgId.msg));
	}
}

void showPeerHistory(const PeerId &peer, MsgId msgId, bool back) {
	if (MainWidget *m = App::main()) m->ui_showPeerHistory(peer, msgId, back);
}

void showPeerHistoryAsync(const PeerId &peer, MsgId msgId) {
	if (MainWidget *m = App::main()) {
		QMetaObject::invokeMethod(m, "ui_showPeerHistoryAsync", Qt::QueuedConnection, Q_ARG(quint64, peer), Q_ARG(qint32, msgId));
	}
}

PeerData *getPeerForMouseAction() {
	if (auto w = App::wnd()) {
		return w->ui_getPeerForMouseAction();
	}
	return nullptr;
}

bool hideWindowNoQuit() {
	if (!App::quitting()) {
		if (auto w = App::wnd()) {
			if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
				return w->minimizeToTray();
			} else if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
				w->hide();
				w->updateIsActive(Global::OfflineBlurTimeout());
				w->updateGlobalMenu();
				return true;
			}
		}
	}
	return false;
}

} // namespace Ui

namespace Notify {

void userIsBotChanged(UserData *user) {
	if (MainWidget *m = App::main()) m->notify_userIsBotChanged(user);
}

void userIsContactChanged(UserData *user, bool fromThisApp) {
	if (MainWidget *m = App::main()) m->notify_userIsContactChanged(user, fromThisApp);
}

void botCommandsChanged(UserData *user) {
	if (MainWidget *m = App::main()) m->notify_botCommandsChanged(user);
}

void inlineBotRequesting(bool requesting) {
	if (MainWidget *m = App::main()) m->notify_inlineBotRequesting(requesting);
}

void replyMarkupUpdated(const HistoryItem *item) {
	if (MainWidget *m = App::main()) {
		m->notify_replyMarkupUpdated(item);
	}
}

void inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (MainWidget *m = App::main()) {
		m->notify_inlineKeyboardMoved(item, oldKeyboardTop, newKeyboardTop);
	}
}

bool switchInlineBotButtonReceived(const QString &query) {
	if (MainWidget *m = App::main()) {
		return m->notify_switchInlineBotButtonReceived(query);
	}
	return false;
}

void migrateUpdated(PeerData *peer) {
	if (MainWidget *m = App::main()) m->notify_migrateUpdated(peer);
}

void clipStopperHidden(ClipStopperType type) {
	if (MainWidget *m = App::main()) m->notify_clipStopperHidden(type);
}

void historyItemLayoutChanged(const HistoryItem *item) {
	if (MainWidget *m = App::main()) m->notify_historyItemLayoutChanged(item);
}

void inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (MainWidget *m = App::main()) m->notify_inlineItemLayoutChanged(layout);
}

void historyMuteUpdated(History *history) {
	if (MainWidget *m = App::main()) m->notify_historyMuteUpdated(history);
}

void handlePendingHistoryUpdate() {
	if (MainWidget *m = App::main()) {
		m->notify_handlePendingHistoryUpdate();
	}
	for_const (HistoryItem *item, Global::PendingRepaintItems()) {
		Ui::repaintHistoryItem(item);
	}
	Global::RefPendingRepaintItems().clear();
}

void unreadCounterUpdated() {
	Global::RefHandleUnreadCounterUpdate().call();
}

} // namespace Notify

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::Ref" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::Set" #Name, __FILE__, __LINE__); \
	Namespace##Data->Name = Name; \
}

namespace Sandbox {
namespace internal {

struct Data {
	QString LangSystemISO;
	int32 LangSystem = languageDefault;

	QByteArray LastCrashDump;
	ConnectionProxy PreLaunchProxy;
};

} // namespace internal
} // namespace Sandbox

Sandbox::internal::Data *SandboxData = nullptr;
uint64 SandboxUserTag = 0;

namespace Sandbox {

bool CheckBetaVersionDir() {
	QFile beta(cExeDir() + qsl("TelegramBeta_data/tdata/beta"));
	if (cBetaVersion()) {
		cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
		QDir().mkpath(cWorkingDir() + qstr("tdata"));
		if (*BetaPrivateKey) {
			cSetBetaPrivateKey(QByteArray(BetaPrivateKey));
		}
		if (beta.open(QIODevice::WriteOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);
			dataStream << quint64(cRealBetaVersion()) << cBetaPrivateKey();
		} else {
			LOG(("FATAL: Could not open '%1' for writing private key!").arg(beta.fileName()));
			return false;
		}
	} else if (beta.exists()) {
		cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
		if (beta.open(QIODevice::ReadOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);

			quint64 v;
			QByteArray k;
			dataStream >> v >> k;
			if (dataStream.status() == QDataStream::Ok) {
				cSetBetaVersion(qMax(v, AppVersion * 1000ULL));
				cSetBetaPrivateKey(k);
				cSetRealBetaVersion(v);
			} else {
				LOG(("FATAL: '%1' is corrupted, reinstall private beta!").arg(beta.fileName()));
				return false;
			}
		} else {
			LOG(("FATAL: could not open '%1' for reading private key!").arg(beta.fileName()));
			return false;
		}
	}
	return true;
}

void WorkingDirReady() {
	if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
		cSetTestMode(true);
	}
	if (!cDebug() && QFile(cWorkingDir() + qsl("tdata/withdebug")).exists()) {
		cSetDebug(true);
	}
	if (cBetaVersion()) {
		cSetAlphaVersion(false);
	} else if (!cAlphaVersion() && QFile(cWorkingDir() + qsl("tdata/devversion")).exists()) {
		cSetAlphaVersion(true);
	} else if (AppAlphaVersion) {
		QFile f(cWorkingDir() + qsl("tdata/devversion"));
		if (!f.exists() && f.open(QIODevice::WriteOnly)) {
			f.write("1");
		}
	}

	srand((int32)time(NULL));

	SandboxUserTag = 0;
	QFile usertag(cWorkingDir() + qsl("tdata/usertag"));
	if (usertag.open(QIODevice::ReadOnly)) {
		if (usertag.read(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64)) != sizeof(uint64)) {
			SandboxUserTag = 0;
		}
		usertag.close();
	}
	if (!SandboxUserTag) {
		do {
			memsetrnd_bad(SandboxUserTag);
		} while (!SandboxUserTag);

		if (usertag.open(QIODevice::WriteOnly)) {
			usertag.write(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64));
			usertag.close();
		}
	}
}

void start() {
	SandboxData = new internal::Data();

	SandboxData->LangSystemISO = psCurrentLanguage();
	if (SandboxData->LangSystemISO.isEmpty()) SandboxData->LangSystemISO = qstr("en");
	QByteArray l = LangSystemISO().toLatin1();
	for (int32 i = 0; i < languageCount; ++i) {
		if (l.at(0) == LanguageCodes[i][0] && l.at(1) == LanguageCodes[i][1]) {
			SandboxData->LangSystem = i;
			break;
		}
	}
}

void finish() {
	delete SandboxData;
	SandboxData = 0;
}

uint64 UserTag() {
	return SandboxUserTag;
}

DefineReadOnlyVar(Sandbox, QString, LangSystemISO);
DefineReadOnlyVar(Sandbox, int32, LangSystem);
DefineVar(Sandbox, QByteArray, LastCrashDump);
DefineVar(Sandbox, ConnectionProxy, PreLaunchProxy);

} // namespace Sandbox

namespace Global {
namespace internal {

struct Data {
	uint64 LaunchId = 0;
	SingleDelayedCall HandleHistoryUpdate = { App::app(), "call_handleHistoryUpdate" };
	SingleDelayedCall HandleUnreadCounterUpdate = { App::app(), "call_handleUnreadCounterUpdate" };

	Adaptive::Layout AdaptiveLayout = Adaptive::NormalLayout;
	bool AdaptiveForWide = true;
	bool DialogsModeEnabled = false;
	Dialogs::Mode DialogsMode = Dialogs::Mode::All;

	int32 DebugLoggingFlags = 0;

	// config
	int32 ChatSizeMax = 200;
	int32 MegagroupSizeMax = 1000;
	int32 ForwardedCountMax = 100;
	int32 OnlineUpdatePeriod = 120000;
	int32 OfflineBlurTimeout = 5000;
	int32 OfflineIdleTimeout = 30000;
	int32 OnlineFocusTimeout = 1000;
	int32 OnlineCloudTimeout = 300000;
	int32 NotifyCloudDelay = 30000;
	int32 NotifyDefaultDelay = 1500;
	int32 ChatBigSize = 10;
	int32 PushChatPeriod = 60000;
	int32 PushChatLimit = 2;
	int32 SavedGifsLimit = 200;
	int32 EditTimeLimit = 172800;

	HiddenPinnedMessagesMap HiddenPinnedMessages;

	PendingItemsMap PendingRepaintItems;

	Stickers::Sets StickerSets;
	Stickers::Order StickerSetsOrder;
	uint64 LastStickersUpdate = 0;

	MTP::DcOptions DcOptions;

	CircleMasksMap CircleMasks;
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

	memset_rand(&GlobalData->LaunchId, sizeof(GlobalData->LaunchId));
}

void finish() {
	delete GlobalData;
	GlobalData = nullptr;
}

DefineReadOnlyVar(Global, uint64, LaunchId);
DefineRefVar(Global, SingleDelayedCall, HandleHistoryUpdate);
DefineRefVar(Global, SingleDelayedCall, HandleUnreadCounterUpdate);

DefineVar(Global, Adaptive::Layout, AdaptiveLayout);
DefineVar(Global, bool, AdaptiveForWide);
DefineVar(Global, bool, DialogsModeEnabled);
DefineVar(Global, Dialogs::Mode, DialogsMode);

DefineVar(Global, int32, DebugLoggingFlags);

// config
DefineVar(Global, int32, ChatSizeMax);
DefineVar(Global, int32, MegagroupSizeMax);
DefineVar(Global, int32, ForwardedCountMax);
DefineVar(Global, int32, OnlineUpdatePeriod);
DefineVar(Global, int32, OfflineBlurTimeout);
DefineVar(Global, int32, OfflineIdleTimeout);
DefineVar(Global, int32, OnlineFocusTimeout);
DefineVar(Global, int32, OnlineCloudTimeout);
DefineVar(Global, int32, NotifyCloudDelay);
DefineVar(Global, int32, NotifyDefaultDelay);
DefineVar(Global, int32, ChatBigSize);
DefineVar(Global, int32, PushChatPeriod);
DefineVar(Global, int32, PushChatLimit);
DefineVar(Global, int32, SavedGifsLimit);
DefineVar(Global, int32, EditTimeLimit);

DefineVar(Global, HiddenPinnedMessagesMap, HiddenPinnedMessages);

DefineRefVar(Global, PendingItemsMap, PendingRepaintItems);

DefineVar(Global, Stickers::Sets, StickerSets);
DefineVar(Global, Stickers::Order, StickerSetsOrder);
DefineVar(Global, uint64, LastStickersUpdate);

DefineVar(Global, MTP::DcOptions, DcOptions);

DefineRefVar(Global, CircleMasksMap, CircleMasks);

} // namespace Global
