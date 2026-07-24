/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_session_controller.h"

#include "apiwrap.h"
#include "boxes/send_files_box.h"
#include "data/components/ephemeral_messages.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_draw_to_reply.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_boxes.h"
#include "window/window_separate_id.h"

namespace Window {

void SessionController::handleDrawToReplyRequest(
		Data::DrawToReplyRequest request) {
	if (content()->handleDrawToReplyRequest(request)) {
		return;
	}
	auto image = HistoryView::ResolveDrawToReplyImage(
		&session().data(),
		request);
	if (image.isNull()) {
		return;
	}
	HistoryView::OpenDrawToReplyEditor(
		this,
		std::move(image),
		crl::guard(this, [=](QImage &&result) {
			if (result.isNull()) {
				return;
			}
			const auto thread = resolveDrawToReplyThread(request);
			if (!thread) {
				return;
			}
			auto list = Storage::PrepareMediaFromImage(
				std::move(result),
				QByteArray(),
				st::sendMediaPreviewSize);
			showDrawToReplyFilesBox(
				thread,
				request.messageId,
				std::move(list));
		}));
}

Data::Thread *SessionController::resolveDrawToReplyThread(
		const Data::DrawToReplyRequest &request) const {
	if (const auto item = session().data().message(request.messageId)) {
		if (const auto topic = item->topic()) {
			return topic;
		} else if (const auto sublist = item->savedSublist()) {
			return sublist;
		}
		return item->history();
	}
	if (const auto thread = activeChatCurrent().thread()) {
		if (thread->peer()->id == request.messageId.peer) {
			return thread;
		}
	}
	if (const auto thread = windowId().thread) {
		if (thread->peer()->id == request.messageId.peer) {
			return thread;
		}
	}
	return session().data().historyLoaded(request.messageId.peer);
}

void SessionController::showDrawToReplyFilesBox(
		not_null<Data::Thread*> thread,
		FullMsgId replyTo,
		Ui::PreparedList &&list) {
	const auto weak = base::make_weak(thread);
	const auto peer = thread->peer();
	const auto show = uiShow();
	show->show(Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = show,
		.list = std::move(list),
		.caption = TextWithTags(),
		.toPeer = peer,
		.limits = DefaultLimitsForPeer(peer),
		.check = DefaultCheckForPeer(show, peer),
		.sendType = Api::SendType::Normal,
		.confirmed = crl::guard(this, [=](
				std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions options,
				FullReplyTo currentReplyTo) {
			if (const auto thread = weak.get()) {
				sendDrawToReplyFiles(
					thread,
					currentReplyTo.messageId,
					std::move(bundle),
					options);
			}
		}),
		.replyTo = FullReplyTo{
			.messageId = replyTo,
			.topicRootId = thread->topicRootId(),
			.monoforumPeerId = thread->monoforumPeerId(),
		},
	}));
}

void SessionController::sendDrawToReplyFiles(
		not_null<Data::Thread*> thread,
		FullMsgId replyTo,
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options) {
	if (!bundle) {
		return;
	}
	const auto ephemeralReply = session().ephemeralMessages()
		.isEphemeralBotReply(replyTo);
	if (bundle->totalCount > 1 && ephemeralReply) {
		showToast(tr::lng_ephemeral_reply_single_message(tr::now));
		return;
	}
	if (!ephemeralReply) {
		const auto payment = std::make_shared<SendPaymentHelper>();
		const auto weak = base::make_weak(thread);
		const auto withPaymentApproved = crl::guard(this, [=](int approved) {
			payment->clear();
			if (const auto thread = weak.get()) {
				auto copy = options;
				copy.starsApproved = approved;
				sendDrawToReplyFiles(thread, replyTo, bundle, copy);
			}
		});
		const auto checked = payment->check(
			this,
			thread->peer(),
			options,
			bundle->totalCount,
			withPaymentApproved);
		if (!checked) {
			return;
		}
	}
	const auto type = bundle->way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = Api::SendAction(thread, options);
	action.clearDraft = false;
	action.replyTo = {
		.messageId = replyTo,
		.topicRootId = thread->topicRootId(),
		.monoforumPeerId = thread->monoforumPeerId(),
	};
	auto &api = session().api();
	auto sent = false;
	for (auto &group : bundle->groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		api.sendFiles(std::move(group.list), type, album, action);
		sent = true;
	}
	if (sent) {
		showToast(tr::lng_stories_reply_sent(tr::now));
	}
}

} // namespace Window
