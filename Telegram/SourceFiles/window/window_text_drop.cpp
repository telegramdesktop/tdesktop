/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_text_drop.h"

#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_drafts.h"
#include "data/data_msg_id.h"
#include "data/data_thread.h"
#include "data/data_types.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h"

namespace Window {
namespace {

void ShowThread(
		not_null<SessionController*> controller,
		not_null<Data::Thread*> thread) {
	controller->showThread(
		thread,
		ShowAtTheEndMsgId,
		SectionShow::Way::ClearStack);
}

} // namespace

void DropTextToThread(
		not_null<SessionController*> controller,
		not_null<Data::Thread*> thread,
		const QString &text,
		Fn<void(const QString &text)> insertIntoActive) {
	if (thread->asForum()
		|| (thread->asHistory() && thread->asHistory()->peer->monoforum())) {
		ShowThread(controller, thread);
		return;
	} else if (text.isEmpty()) {
		return;
	} else if (!Data::CanSendTexts(thread)) {
		controller->show(Ui::MakeInformBox(tr::lng_share_cant()));
		return;
	}
	if (controller->activeChatCurrent().thread() == thread) {
		insertIntoActive(text);
		return;
	}
	const auto history = thread->owningHistory();
	const auto topicRootId = thread->topicRootId();
	const auto monoforumPeerId = thread->monoforumPeerId();
	if (history->localEditDraft(topicRootId, monoforumPeerId)) {
		ShowThread(controller, thread);
		return;
	}
	const auto existing = history->localDraft(topicRootId, monoforumPeerId);
	auto textWithTags = existing
		? existing->textWithTags
		: TextWithTags();
	if (!textWithTags.text.isEmpty() && !textWithTags.text.back().isSpace()) {
		textWithTags.text += u" "_q;
	}
	textWithTags.text += text;
	const auto cursor = MessageCursor{
		int(textWithTags.text.size()),
		int(textWithTags.text.size()),
		Ui::kQFixedMax
	};
	history->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		existing ? existing->reply : FullReplyTo{
			.topicRootId = topicRootId,
			.monoforumPeerId = monoforumPeerId,
		},
		existing ? existing->suggest : SuggestOptions(),
		cursor,
		existing ? existing->webpage : Data::WebPageDraft()));
	history->session().changes().entryUpdated(
		thread,
		Data::EntryUpdate::Flag::LocalDraftSet);
}

} // namespace Window
