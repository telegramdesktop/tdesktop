/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_stories.h"

#include "data/data_peer.h"
#include "data/data_stories.h"
#include "data/data_stories_ids.h"
#include "info/info_controller.h"
#include "info/media/info_media_list_widget.h"
#include "info/stories/info_stories_common.h"
#include "lang/lang_keys.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

[[nodiscard]] rpl::producer<int> StoriesCountValue(
		not_null<PeerData*> peer) {
	return rpl::single(0) | rpl::then(Data::AlbumStoriesIds(
		peer,
		Data::kStoriesAlbumIdSaved,
		ServerMaxStoryId - 1,
		0
	) | rpl::map([](const Data::StoriesIdsSlice &slice) {
		return slice.fullCount().value_or(0);
	}));
}

class StoriesSubController final : public AbstractController {
public:
	StoriesSubController(
		not_null<AbstractController*> parent,
		not_null<PeerData*> peer)
	: AbstractController(parent->parentController())
	, _parent(parent)
	, _key(Stories::Tag(peer)) {
	}

	Key key() const override {
		return _key;
	}
	PeerData *migrated() const override {
		return nullptr;
	}
	::Info::Section section() const override {
		return ::Info::Section(::Info::Section::Type::Stories);
	}
	style::color listBackground() const override {
		return _parent->listBackground();
	}

private:
	const not_null<AbstractController*> _parent;
	const Key _key;

};

class StoriesTabAdapter final : public MediaTabContent {
public:
	StoriesTabAdapter(MediaTabContext context)
	: _peer(context.peer)
	, _subController(context.controller, context.peer)
	, _host(context.parent) {
		const auto host = _host.data();
		_list = Ui::CreateChild<Media::ListWidget>(host, &_subController);
		_list->show();
		_list->heightValue(
		) | rpl::on_next([this](int newHeight) {
			_host->resize(_host->width(), newHeight);
		}, host->lifetime());
		_list->scrollToRequests(
		) | rpl::on_next([this, scrollTo = context.scrollToRequest](
				int top) {
			if (scrollTo && _list->isVisible() && _topOverlay > 0) {
				scrollTo(std::max(top, 0), -1);
			}
		}, host->lifetime());
	}

	not_null<Ui::RpWidget*> widget() override {
		return _host.data();
	}
	void resizeToWidth(int newWidth) override {
		if (_host->width() != newWidth) {
			_list->resizeToWidth(std::max(
				newWidth - st::infoMediaTabsRightSkip,
				1));
		}
		_host->resize(newWidth, _list->height());
	}
	TabTopBarBindings topBarBindings() override {
		const auto channel = _peer->isChannel();
		return {
			.title = (channel
				? tr::lng_media_type_posts()
				: tr::lng_media_type_stories()
			) | rpl::map([](const QString &text) {
				return TextWithEntities{ text };
			}),
			.subtitle = StoriesCountValue(
				_peer
			) | rpl::map([channel](int count) {
				return TextWithEntities{ (count > 0)
					? (channel
						? tr::lng_profile_posts(tr::now, lt_count, count)
						: tr::lng_profile_saved_stories(
							tr::now,
							lt_count,
							count))
					: QString() };
			}),
			.selectedItems = _list->selectedListValue(),
			.selectionAction = crl::guard(
				base::make_weak(_list),
				[list = _list](SelectionAction action) {
					list->selectionAction(action);
				}),
		};
	}

	void deactivated() override {
		_list->selectionAction(SelectionAction::Clear);
	}

	void setVisibleRegion(int top, int bottom) override {
		_list->setExternalViewportHeight(bottom - top);
		_list->setVisibleTopBottom(top, bottom);
	}

	void setTopOverlay(int height) override {
		_topOverlay = height;
		_list->setTopOverlayHeight(height);
	}

private:
	const not_null<PeerData*> _peer;
	StoriesSubController _subController;
	object_ptr<Ui::RpWidget> _host;
	Media::ListWidget *_list = nullptr;
	int _topOverlay = 0;

};

} // namespace

MediaTabDescriptor MakeStoriesTabDescriptor(not_null<PeerData*> peer) {
	using namespace rpl::mappers;
	return {
		.id = u"stories"_q,
		.title = (peer->isChannel()
			? tr::lng_media_type_posts(tr::marked)
			: tr::lng_media_type_stories(tr::marked)),
		.shown = StoriesCountValue(peer) | rpl::map(_1 > 0),
		.factory = [](MediaTabContext context) {
			return std::make_unique<StoriesTabAdapter>(std::move(context));
		},
		.profileTab = Data::ProfileTab::Posts,
	};
}

} // namespace Info::Profile
