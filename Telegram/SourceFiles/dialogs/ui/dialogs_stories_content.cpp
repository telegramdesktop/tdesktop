/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_content.h"

#include "api/api_stories.h"
#include "apiwrap.h"
#include "boxes/story_drafts_box.h"

#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story_drafts.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "info/stories/info_stories_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "media/stories/media_stories_post.h"
#include "media/stories/media_stories_stealth.h"
#include "lang/lang_keys.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Dialogs::Stories {
namespace {

constexpr auto kShownLastCount = 3;

class State final {
public:
	State(not_null<Data::Stories*> data, Data::StorySourcesList list);

	[[nodiscard]] Content next();

private:
	void applyUploads(Content &result);

	const not_null<Data::Stories*> _data;
	const Data::StorySourcesList _list;
	base::flat_map<
		not_null<PeerData*>,
		std::shared_ptr<Ui::DynamicImage>> _userpics;

};

State::State(not_null<Data::Stories*> data, Data::StorySourcesList list)
: _data(data)
, _list(list) {
}

Content State::next() {
	const auto &sources = _data->sources(_list);
	auto result = Content{ .total = int(sources.size()) };
	result.elements.reserve(sources.size());
	auto userpics = base::flat_map<
		not_null<PeerData*>,
		std::shared_ptr<Ui::DynamicImage>>();
	userpics.reserve(sources.size());
	for (const auto &info : sources) {
		const auto source = _data->source(info.id);
		Assert(source != nullptr);

		auto userpic = std::shared_ptr<Ui::DynamicImage>();
		const auto peer = source->peer;
		if (const auto i = _userpics.find(peer); i != end(_userpics)) {
			userpic = i->second;
		} else {
			userpic = Ui::MakeUserpicThumbnail(peer, true);
		}
		userpics.emplace(peer, userpic);
		result.elements.push_back({
			.id = uint64(peer->id.value),
			.name = peer->shortName(),
			.thumbnail = std::move(userpic),
			.count = info.count,
			.unreadCount = info.unreadCount,
			.hasVideoStream = info.hasVideoStream ? 1U : 0U,
			.skipSmall = peer->isSelf() ? 1U : 0U,
			.mine = peer->isSelf() ? 1U : 0U,
			.mineUnread = (peer->isSelf()
				&& !source->ids.empty()
				&& source->ids.back().id > _data->viewedSelfTill())
				? 1U
				: 0U,
		});
	}
	if (_list == Data::StorySourcesList::NotHidden) {
		applyUploads(result);
	}
	_userpics = std::move(userpics);
	return result;
}

void State::applyUploads(Content &result) {
	const auto &uploads = _data->session().api().stories().uploads();
	for (const auto &upload : uploads) {
		const auto peer = upload.peer;
		const auto id = uint64(peer->id.value);
		const auto i = ranges::find(result.elements, id, &Element::id);
		const auto progress = uint32(std::clamp(
			int(std::round(upload.progress * 100)),
			0,
			100));
		if (i != end(result.elements)) {
			i->uploading = 1;
			i->mineUnread = 1;
			i->uploadFailed = upload.error.isEmpty() ? 0U : 1U;
			i->uploadProgress = progress;
			continue;
		}
		auto userpic = std::shared_ptr<Ui::DynamicImage>();
		if (const auto j = _userpics.find(peer); j != end(_userpics)) {
			userpic = j->second;
		} else {
			userpic = Ui::MakeUserpicThumbnail(peer, true);
			_userpics.emplace(peer, userpic);
		}
		result.elements.insert(begin(result.elements), {
			.id = id,
			.name = (peer->isSelf()
				? tr::lng_story_my_story(tr::now)
				: peer->shortName()),
			.thumbnail = std::move(userpic),
			.skipSmall = peer->isSelf() ? 1U : 0U,
			.mine = peer->isSelf() ? 1U : 0U,
			.mineUnread = 1U,
			.uploading = 1,
			.uploadFailed = upload.error.isEmpty() ? 0U : 1U,
			.uploadProgress = progress,
		});
		++result.total;
	}
}

} // namespace

rpl::producer<Content> ContentForSession(
		not_null<Main::Session*> session,
		Data::StorySourcesList list) {
	return [=](auto consumer) {
		auto result = rpl::lifetime();
		const auto stories = &session->data().stories();
		const auto state = result.make_state<State>(stories, list);
		rpl::single(
			rpl::empty
		) | rpl::then(rpl::merge(
			stories->sourcesChanged(list),
			session->api().stories().uploadsChanges(),
			stories->viewedSelfTillChanged()
		)) | rpl::on_next([=] {
			consumer.put_next(state->next());
		}, result);
		return result;
	};
}

rpl::producer<Content> LastForPeer(not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	const auto stories = &peer->owner().stories();
	const auto peerId = peer->id;

	return rpl::single(
		peerId
	) | rpl::then(
		stories->sourceChanged() | rpl::filter(_1 == peerId)
	) | rpl::map([=] {
		auto ids = std::vector<StoryId>();
		auto readTill = StoryId();
		auto total = 0;
		if (const auto source = stories->source(peerId)) {
			readTill = source->readTill;
			total = int(source->ids.size());
			ids = ranges::views::all(source->ids)
				| ranges::views::reverse
				| ranges::views::take(kShownLastCount)
				| ranges::views::transform(&Data::StoryIdDates::id)
				| ranges::to_vector;
		}
		return rpl::make_producer<Content>([=](auto consumer) {
			auto lifetime = rpl::lifetime();
			if (ids.empty()) {
				consumer.put_next(Content());
				consumer.put_done();
				return lifetime;
			}

			struct State {
				Fn<void()> check;
				base::has_weak_ptr guard;
				int readTill = StoryId();
				bool pushed = false;
			};
			const auto state = lifetime.make_state<State>();
			state->readTill = readTill;
			state->check = [=] {
				if (state->pushed) {
					return;
				}
				auto done = true;
				auto resolving = false;
				auto result = Content{ .total = total };
				for (const auto id : ids) {
					const auto storyId = FullStoryId{ peerId, id };
					const auto maybe = stories->lookup(storyId);
					if (maybe) {
						if (!resolving) {
							const auto stream = (*maybe)->call();
							const auto unread = stream
								|| (id > state->readTill);
							result.elements.reserve(ids.size());
							result.elements.push_back({
								.id = uint64(id),
								.thumbnail = Ui::MakeStoryThumbnail(*maybe),
								.count = 1U,
								.unreadCount = unread ? 1U : 0U,
								.hasVideoStream = stream ? 1U : 0U,
							});
							if (unread) {
								done = false;
							}
						}
					} else if (maybe.error() == Data::NoStory::Unknown) {
						resolving = true;
						stories->resolve(
							storyId,
							crl::guard(&state->guard, state->check));
					}
				}
				if (resolving) {
					return;
				}
				state->pushed = true;
				consumer.put_next(std::move(result));
				if (done) {
					consumer.put_done();
				}
			};

			rpl::single(peerId) | rpl::then(
				stories->itemsChanged() | rpl::filter(_1 == peerId)
			) | rpl::on_next(state->check, lifetime);

			stories->session().changes().storyUpdates(
				Data::StoryUpdate::Flag::MarkRead
			) | rpl::on_next([=](const Data::StoryUpdate &update) {
				if (update.story->peer()->id == peerId) {
					if (update.story->id() > state->readTill) {
						state->readTill = update.story->id();
						if (ranges::contains(ids, state->readTill)
							|| state->readTill > ids.front()) {
							state->pushed = false;
							state->check();
						}
					}
				}
			}, lifetime);

			return lifetime;
		});
	}) | rpl::flatten_latest();
}

void FillSourceMenu(
		not_null<Window::SessionController*> controller,
		const ShowMenuRequest &request) {
	const auto owner = &controller->session().data();
	const auto peer = owner->peer(PeerId(request.id));
	const auto &add = request.callback;
	const auto api = &controller->session().api().stories();
	for (const auto &upload : api->uploads()) {
		if (upload.peer != peer || upload.error.isEmpty()) {
			continue;
		}
		const auto id = upload.id;
		add(tr::lng_story_upload_retry(tr::now), [=] {
			api->retry(id);
		}, &st::menuIconRestore);
		add(tr::lng_story_upload_cancel(tr::now), [=] {
			api->remove(id);
		}, &st::menuIconDelete);
	}
	if (Media::Stories::CanPostStory(peer)) {
		add(tr::lng_story_post_button(tr::now), [=] {
			Media::Stories::PostStory(controller, peer);
		}, &st::menuIconStoryPost);
	}
	if (peer->isSelf()) {
		if (owner->stories().drafts().count() > 0) {
			add(tr::lng_story_drafts_button(tr::now), [=] {
				ShowStoryDraftsBox(controller);
			}, &st::menuIconEdit);
		}
		add(tr::lng_stories_archive_button(tr::now), [=] {
			controller->showSection(Info::Stories::Make(
				peer,
				Info::Stories::ArchiveId()));
		}, &st::menuIconStoriesArchiveSection);
		add(tr::lng_stories_my_title(tr::now), [=] {
			controller->showSection(Info::Stories::Make(peer));
		}, &st::menuIconStoriesSavedSection);
	} else {
		const auto group = peer->isMegagroup();
		const auto channel = peer->isChannel();
		const auto showHistoryText = group
			? tr::lng_context_open_group(tr::now)
			: channel
			? tr::lng_context_open_channel(tr::now)
			: tr::lng_profile_send_message(tr::now);
		add(showHistoryText, [=] {
			controller->showPeerHistory(peer);
		}, channel ? &st::menuIconChannel : &st::menuIconChatBubble);
		const auto viewProfileText = group
			? tr::lng_context_view_group(tr::now)
			: channel
			? tr::lng_context_view_channel(tr::now)
			: tr::lng_context_view_profile(tr::now);
		add(viewProfileText, [=] {
			controller->showPeerInfo(peer);
		}, channel ? &st::menuIconInfo : &st::menuIconProfile);
		if (!peer->hasActiveVideoStream() && peer->hasUnreadStories()) {
			Media::Stories::AddStealthModeMenu(add, peer, controller);
		}
		const auto in = [&](Data::StorySourcesList list) {
			return ranges::contains(
				owner->stories().sources(list),
				peer->id,
				&Data::StoriesSourceInfo::id);
		};
		const auto toggle = [=](bool shown) {
			owner->stories().toggleHidden(
				peer->id,
				!shown,
				controller->uiShow());
		};
		if (in(Data::StorySourcesList::NotHidden)) {
			add(tr::lng_stories_archive(tr::now), [=] {
				toggle(false);
			}, &st::menuIconArchive);
		}
		if (in(Data::StorySourcesList::Hidden)) {
			add(tr::lng_stories_unarchive(tr::now), [=] {
				toggle(true);
			}, &st::menuIconUnarchive);
		}
	}
}

} // namespace Dialogs::Stories
