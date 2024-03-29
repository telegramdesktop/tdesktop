/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_pinned_bar.h"

#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_poll.h"
#include "history/view/history_view_pinned_tracker.h"
#include "history/history_item.h"
#include "history/history.h"
#include "core/ui_integration.h"
#include "base/weak_ptr.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

[[nodiscard]] Ui::MessageBarContent ContentWithoutPreview(
		not_null<HistoryItem*> item,
		Fn<void()> repaint) {
	return Ui::MessageBarContent{
		.text = item->inReplyText(),
		.context = Core::MarkedTextContext{
			.session = &item->history()->session(),
			.customEmojiRepaint = std::move(repaint),
		},
	};
}

[[nodiscard]] Ui::MessageBarContent ContentWithPreview(
		not_null<HistoryItem*> item,
		Image *preview,
		bool spoiler,
		Fn<void()> repaint) {
	auto result = ContentWithoutPreview(item, repaint);
	if (!preview) {
		static const auto kEmpty = [&] {
			const auto size = st::historyReplyHeight
				* style::DevicePixelRatio();
			auto result = QImage(
				QSize(size, size),
				QImage::Format_ARGB32_Premultiplied);
			result.fill(Qt::transparent);
			result.setDevicePixelRatio(style::DevicePixelRatio());
			return result;
		}();
		result.preview = kEmpty;
		result.spoilerRepaint = nullptr;
	} else {
		result.preview = Images::Round(
			preview->original(),
			ImageRoundRadius::Small);
		result.spoilerRepaint = spoiler ? repaint : nullptr;
	}
	return result;
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItem(
		not_null<HistoryItem*> item,
		Fn<void()> repaint) {
	return item->history()->session().changes().messageFlagsValue(
		item,
		Data::MessageUpdate::Flag::Edited
	) | rpl::map([=]() -> rpl::producer<Ui::MessageBarContent> {
		const auto media = item->media();
		if (!media || !media->hasReplyPreview()) {
			return rpl::single(ContentWithoutPreview(item, repaint));
		}
		constexpr auto kFullLoaded = 2;
		constexpr auto kSomeLoaded = 1;
		constexpr auto kNotLoaded = 0;
		const auto loadedLevel = [=] {
			const auto preview = media->replyPreview();
			return media->replyPreviewLoaded()
				? kFullLoaded
				: preview
				? kSomeLoaded
				: kNotLoaded;
		};
		return rpl::single(
			loadedLevel()
		) | rpl::then(
			item->history()->session().downloaderTaskFinished(
			) | rpl::map(loadedLevel)
		) | rpl::distinct_until_changed(
		) | rpl::take_while([=](int loadLevel) {
			return loadLevel < kFullLoaded;
		}) | rpl::then(
			rpl::single(kFullLoaded)
		) | rpl::map([=] {
			return ContentWithPreview(
				item,
				media->replyPreview(),
				media->hasSpoiler(),
				repaint);
		});
	}) | rpl::flatten_latest();
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		Fn<void()> repaint,
		bool alreadyLoaded = false) {
	if (!id) {
		return rpl::single(Ui::MessageBarContent());
	} else if (const auto item = session->data().message(id)) {
		return ContentByItem(item, repaint);
	} else if (alreadyLoaded) {
		return rpl::single(Ui::MessageBarContent()); // Deleted message?..
	}
	auto load = rpl::make_producer<Ui::MessageBarContent>([=](auto consumer) {
		consumer.put_next(Ui::MessageBarContent{
			.text = { tr::lng_contacts_loading(tr::now) },
		});
		const auto peer = session->data().peer(id.peer);
		const auto callback = [=] { consumer.put_done(); };
		session->api().requestMessageData(peer, id.msg, callback);
		return rpl::lifetime();
	});
	return std::move(
		load
	) | rpl::then(rpl::deferred([=] {
		return ContentByItemId(session, id, repaint, true);
	}));
}

auto WithPinnedTitle(not_null<Main::Session*> session, PinnedId id) {
	return [=](Ui::MessageBarContent &&content) {
		const auto item = session->data().message(id.message);
		if (!item) {
			return std::move(content);
		}
		content.title = (id.index + 1 >= id.count)
			? tr::lng_pinned_message(tr::now)
			: (id.count == 2)
			? tr::lng_pinned_previous(tr::now)
			: (tr::lng_pinned_message(tr::now)
				+ " #"
				+ QString::number(id.index + 1));
		content.count = std::max(id.count, 1);
		content.index = std::clamp(id.index, 0, content.count - 1);
		return std::move(content);
	};
}

} // namespace

rpl::producer<Ui::MessageBarContent> MessageBarContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		Fn<void()> repaint) {
	return ContentByItemId(session, id, std::move(repaint));
}

rpl::producer<Ui::MessageBarContent> PinnedBarContent(
		not_null<Main::Session*> session,
		rpl::producer<PinnedId> id,
		Fn<void()> repaint) {
	return std::move(
		id
	) | rpl::distinct_until_changed(
	) | rpl::map([=](PinnedId id) {
		return ContentByItemId(
			session,
			id.message,
			repaint
		) | rpl::map(WithPinnedTitle(session, id));
	}) | rpl::flatten_latest();
}

rpl::producer<HistoryItem*> PinnedBarItemWithReplyMarkup(
		not_null<Main::Session*> session,
		rpl::producer<PinnedId> id) {
	return rpl::make_producer<HistoryItem*>([=,
			id = std::move(id)](auto consumer) {
		auto lifetime = rpl::lifetime();
		consumer.put_next(nullptr);

		struct State {
			bool hasReplyMarkup = false;
			base::has_weak_ptr guard;
			rpl::lifetime lifetime;
			FullMsgId resolvedId;
		};
		const auto state = lifetime.make_state<State>();

		const auto pushUnique = [=](not_null<HistoryItem*> item) {
			const auto replyMarkup = item->inlineReplyMarkup();
			if (!state->hasReplyMarkup && !replyMarkup) {
				return;
			}
			state->hasReplyMarkup = (replyMarkup != nullptr);
			consumer.put_next(item.get());
		};

		rpl::duplicate(
			id
		) | rpl::filter([=](PinnedId current) {
			return current.message && (current.message != state->resolvedId);
		}) | rpl::start_with_next([=](PinnedId current) {
			const auto fullId = current.message;
			state->lifetime.destroy();
			state->resolvedId = fullId;
			invalidate_weak_ptrs(&state->guard);

			const auto messageFlag = [=](not_null<HistoryItem*> item) {
				using Update = Data::MessageUpdate;
				session->changes().messageUpdates(
					item,
					Update::Flag::ReplyMarkup | Update::Flag::Destroyed
				) | rpl::start_with_next([=](const Update &update) {
					if (update.flags & Update::Flag::Destroyed) {
						state->lifetime.destroy();
						invalidate_weak_ptrs(&state->guard);
						state->hasReplyMarkup = false;
						consumer.put_next(nullptr);
					} else {
						pushUnique(update.item);
					}
				}, state->lifetime);
				pushUnique(item);
			};
			if (const auto item = session->data().message(fullId)) {
				messageFlag(item);
				return;
			}
			const auto resolved = crl::guard(&state->guard, [=] {
				if (const auto item = session->data().message(fullId)) {
					messageFlag(item);
				}
			});
			session->api().requestMessageData(
				session->data().peer(fullId.peer),
				fullId.msg,
				resolved);
		}, lifetime);
		return lifetime;
	});
}

} // namespace HistoryView
