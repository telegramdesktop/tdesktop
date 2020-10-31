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
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] Ui::MessageBarContent ContentWithoutPreview(
		not_null<HistoryItem*> item) {
	return Ui::MessageBarContent{
		.text = { item->inReplyText() },
	};
}

[[nodiscard]] Ui::MessageBarContent ContentWithPreview(
		not_null<HistoryItem*> item,
		Image *preview) {
	auto result = ContentWithoutPreview(item);
	if (!preview) {
		static const auto kEmpty = [&] {
			const auto size = st::historyReplyHeight * cIntRetinaFactor();
			auto result = QImage(
				QSize(size, size),
				QImage::Format_ARGB32_Premultiplied);
			result.fill(Qt::transparent);
			result.setDevicePixelRatio(cRetinaFactor());
			return result;
		}();
		result.preview = kEmpty;
	} else {
		result.preview = preview->original();
	}
	return result;
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItem(
		not_null<HistoryItem*> item) {
	return item->history()->session().changes().messageFlagsValue(
		item,
		Data::MessageUpdate::Flag::Edited
	) | rpl::map([=]() -> rpl::producer<Ui::MessageBarContent> {
		const auto media = item->media();
		if (!media || !media->hasReplyPreview()) {
			return rpl::single(ContentWithoutPreview(item));
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
			return ContentWithPreview(item, media->replyPreview());
		});
	}) | rpl::flatten_latest();
}

[[nodiscard]] rpl::producer<Ui::MessageBarContent> ContentByItemId(
		not_null<Main::Session*> session,
		FullMsgId id,
		bool alreadyLoaded = false) {
	if (!id) {
		return rpl::single(Ui::MessageBarContent());
	} else if (const auto item = session->data().message(id)) {
		return ContentByItem(item);
	} else if (alreadyLoaded) {
		return rpl::single(Ui::MessageBarContent()); // Deleted message?..
	}
	auto load = rpl::make_producer<Ui::MessageBarContent>([=](auto consumer) {
		consumer.put_next(Ui::MessageBarContent{
			.text = { tr::lng_contacts_loading(tr::now) },
		});
		const auto channel = id.channel
			? session->data().channel(id.channel).get()
			: nullptr;
		const auto callback = [=](ChannelData *channel, MsgId id) {
			consumer.put_done();
		};
		session->api().requestMessageData(channel, id.msg, callback);
		return rpl::lifetime();
	});
	return std::move(
		load
	) | rpl::then(rpl::deferred([=] {
		return ContentByItemId(session, id, true);
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
		FullMsgId id) {
	return ContentByItemId(session, id);
}

rpl::producer<Ui::MessageBarContent> PinnedBarContent(
		not_null<Main::Session*> session,
		rpl::producer<PinnedId> id) {
	return std::move(
		id
	) | rpl::distinct_until_changed(
	) | rpl::map([=](PinnedId id) {
		return ContentByItemId(
			session,
			id.message
		) | rpl::map(WithPinnedTitle(session, id));
	}) | rpl::flatten_latest();
}

} // namespace HistoryView
