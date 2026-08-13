/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "menu/menu_send_details.h"
#include <rpl/producer.h>

#include <memory>
#include <optional>

class DocumentData;
class HistoryItem;
class PeerData;
class PhotoData;

namespace Api {
struct SendAction;
} // namespace Api

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
class InputField;
class SendButton;
} // namespace Ui

class QMimeData;

namespace Iv {
struct RichPage;
} // namespace Iv

namespace Iv::Editor {

struct ComposeBoxOptions {
	enum class Scope {
		Thread,
		Detached,
	};
	enum class SubmitPolicy {
		Immediate,
		Schedule,
	};

	Scope scope = Scope::Thread;
	std::shared_ptr<QMimeData> initialPaste;
	SubmitPolicy submitPolicy = SubmitPolicy::Immediate;
	Fn<void(TextWithTags)> returnText;
	bool welcomeTemplates = false;
};

[[nodiscard]] std::shared_ptr<ChatHelpers::Show> ActiveWindowShow(
	not_null<Main::Session*> session);
void ShowRichMessagesPremiumToast(std::shared_ptr<ChatHelpers::Show> show);
[[nodiscard]] bool CanAuthorRichMessages(not_null<Main::Session*> session);
[[nodiscard]] bool SessionPremium(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<bool> AmPremiumValue(
	not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> StarsPerMessageValue(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer);
[[nodiscard]] bool IsEmojiDocument(not_null<DocumentData*> document);
[[nodiscard]] bool PremiumEmojiForbidden(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer,
	not_null<DocumentData*> document);
[[nodiscard]] bool AllowEmojiWithoutPremium(
	not_null<PeerData*> peer,
	DocumentData *exactEmoji = nullptr);
void InsertCustomEmoji(
	not_null<Ui::InputField*> field,
	not_null<DocumentData*> document);
[[nodiscard]] PhotoData *UsablePhoto(
	not_null<Main::Session*> session,
	uint64 id);
[[nodiscard]] DocumentData *UsableDocument(
	not_null<Main::Session*> session,
	uint64 id);
void OfferRichMessagePremiumChoice(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Main::Session*> session,
	const RichPage &page,
	Fn<void()> sendWithoutFormatting,
	bool save = false);
void SetupSendLockBadge(
	not_null<Ui::SendButton*> button,
	QPoint position,
	rpl::producer<bool> locked);
void ShowComposeBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Api::SendAction action,
	SendMenu::Details sendMenuDetails,
	TextWithTags fieldText = {},
	Fn<void()> onMigrated = nullptr,
	ComposeBoxOptions options = {});
void ShowEditBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
void ShowEditFromFieldBox(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item,
	Api::SendAction action,
	std::optional<TextWithTags> fieldTextOverride = std::nullopt,
	Fn<void()> fieldMigratedOverride = nullptr);
[[nodiscard]] bool HasEditWindowFor(
	not_null<Main::Session*> session,
	FullMsgId itemId);
[[nodiscard]] bool ActivateEditWindowFor(
	not_null<Main::Session*> session,
	FullMsgId itemId);
[[nodiscard]] bool IsComposeBoxOpen(
	not_null<Main::Session*> session,
	PeerId peerId,
	MsgId topicRootId,
	PeerId monoforumPeerId);
[[nodiscard]] rpl::producer<bool> FieldVisibleValue(
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
