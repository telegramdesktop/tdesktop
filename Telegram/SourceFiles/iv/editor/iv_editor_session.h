/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/basic_types.h"
#include "menu/menu_send_details.h"
#include <rpl/producer.h>

#include <memory>

class HistoryItem;
class PeerData;

namespace Data {
struct Draft;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class SendButton;
} // namespace Ui

namespace Iv {
struct RichPage;
} // namespace Iv

namespace Iv::Editor {

using ThreadFieldDraftReader = Fn<std::unique_ptr<::Data::Draft>()>;
using ThreadFieldDraftSaver = Fn<void(std::unique_ptr<::Data::Draft>)>;
using ThreadFieldMigratedAway = Fn<void()>;

[[nodiscard]] bool CheckRichMessagesPremium(
	not_null<Window::SessionController*> controller);
void ShowRichMessagesPremiumToast(std::shared_ptr<ChatHelpers::Show> show);
[[nodiscard]] bool CanAuthorRichMessages(not_null<Main::Session*> session);
void OfferRichMessagePremiumChoice(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Main::Session*> session,
	const RichPage &page,
	Fn<void()> sendWithoutFormatting);
void SetupSendLockBadge(
	not_null<Ui::SendButton*> button,
	QPoint position,
	rpl::producer<bool> locked);
void ShowComposeBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Api::SendAction action,
	SendMenu::Details sendMenuDetails);
void ShowEditBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
void ShowEditFromFieldBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	Api::SendAction action);
[[nodiscard]] bool IsComposeBoxOpen(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId);
[[nodiscard]] bool SaveOpenComposeDraftThenEdit(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	Fn<void()> onSaved);
[[nodiscard]] bool RequestCloseOpenEditWindowThenCompose(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer,
	Fn<void()> onClosed);
[[nodiscard]] rpl::producer<bool> FieldVisibleValue(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId);
void RegisterThreadFieldBridge(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	ThreadFieldDraftReader readDraft,
	ThreadFieldDraftSaver saveDraft,
	ThreadFieldMigratedAway migratedAway);
void UnregisterThreadFieldBridge(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId);

// Synchronously destroys all open editor windows. Called on application
// shutdown (before ~Sandbox) so that no editor top-level widget survives
// to be destroyed from ~QApplication, where the lib_ui native event filter
// would re-enter the already destroyed Sandbox machinery and crash.
void CloseAllWindows();

} // namespace Iv::Editor
