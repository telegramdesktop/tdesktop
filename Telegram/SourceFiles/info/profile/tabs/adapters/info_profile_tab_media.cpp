/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_media.h"

#include "base/options.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "info/info_controller.h"
#include "info/media/info_media_buttons.h"
#include "info/media/info_media_empty_widget.h"
#include "info/media/info_media_list_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/tabs/adapters/info_profile_tab_sub_controller.h"
#include "info/profile/tabs/info_profile_tab_skeleton.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace Info::Profile {
namespace {

using SharedMediaType = Storage::SharedMediaType;

base::options::toggle MediaTabsExpandedOption({
	.id = kOptionProfileMediaTabsExpanded,
	.name = "Split profile media tab into photos and videos.",
	.description = "Show separate photo and video tabs in profiles instead "
		"of a single combined media tab.",
});

[[nodiscard]] bool MediaTabGrid(SharedMediaType type) {
	return (type == SharedMediaType::Photo)
		|| (type == SharedMediaType::Video)
		|| (type == SharedMediaType::PhotoVideo);
}

[[nodiscard]] bool MediaTabSearchable(SharedMediaType type) {
	return (type == SharedMediaType::File)
		|| (type == SharedMediaType::Link)
		|| (type == SharedMediaType::MusicFile);
}

[[nodiscard]] rpl::producer<QString> MediaTabTitle(SharedMediaType type) {
	switch (type) {
	case SharedMediaType::PhotoVideo: return tr::lng_media_type_media();
	case SharedMediaType::Photo: return tr::lng_media_type_photos();
	case SharedMediaType::Video: return tr::lng_media_type_videos();
	case SharedMediaType::File: return tr::lng_media_type_files();
	case SharedMediaType::MusicFile: return tr::lng_all_music();
	case SharedMediaType::Link: return tr::lng_all_links();
	case SharedMediaType::RoundVoiceFile: return tr::lng_all_voice();
	case SharedMediaType::GIF: return tr::lng_media_type_gifs();
	case SharedMediaType::Poll: return tr::lng_media_type_polls();
	default: Unexpected("type in MediaTabTitle");
	}
}

[[nodiscard]] QString MediaTabId(SharedMediaType type) {
	return u"media:"_q + QString::number(int(type));
}

[[nodiscard]] Data::ProfileTab MediaProfileTab(SharedMediaType type) {
	switch (type) {
	case SharedMediaType::PhotoVideo:
	case SharedMediaType::Photo: return Data::ProfileTab::Media;
	case SharedMediaType::File: return Data::ProfileTab::Files;
	case SharedMediaType::MusicFile: return Data::ProfileTab::Music;
	case SharedMediaType::RoundVoiceFile: return Data::ProfileTab::Voice;
	case SharedMediaType::Link: return Data::ProfileTab::Links;
	case SharedMediaType::GIF: return Data::ProfileTab::Gifs;
	default: return Data::ProfileTab::None;
	}
}

class MediaTabAdapter final : public MediaTabContent {
public:
	MediaTabAdapter(MediaTabContext context, SharedMediaType type)
	: _context(context)
	, _type(type)
	, _countPeer(context.sublist
		? context.sublist->sublistPeer()
		: context.peer)
	, _topicRootId(context.topic ? context.topic->rootId() : MsgId())
	, _monoforumPeerId(context.sublist
		? context.sublist->sublistPeer()->id
		: PeerId())
	, _subController(
		context.controller,
		type,
		context.migrated,
		MediaTabSearchable(type))
	, _host(context.parent) {
		const auto host = _host.data();
		_list = Ui::CreateChild<Media::ListWidget>(host, &_subController);
		_list->setObjectName(u"profileTabMediaList"_q);
		_list->show();
		_skeleton = CreateTabSkeleton(host, _type);
		_skeleton->show();
		_empty = Ui::CreateChild<Media::EmptyWidget>(host);
		_empty->setType(_type);
		_empty->setSearchQuery(QString());
		_empty->hide();
		_empty->setFullHeight(_fullHeight.value());
		_empty->heightValue(
		) | rpl::on_next([this](int) {
			updateHostHeight();
		}, host->lifetime());
		_list->heightValue(
		) | rpl::on_next([this](int newHeight) {
			if (newHeight > 0 && !_listLoaded) {
				_listLoaded = true;
				_skeleton->hide();
			}
			updateHostHeight();
		}, host->lifetime());
		_list->scrollToRequests(
		) | rpl::on_next([this](int top) {
			const auto scrollTo = _context.scrollToRequest;
			// Above the dock a correction would drag the view down.
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
			_host->resize(newWidth, _host->height());
			_list->resizeToWidth(std::max(
				newWidth - st::infoMediaTabsRightSkip,
				1));
			if (_empty) {
				_empty->resizeToWidth(newWidth);
			}
		}
		updateHostHeight();
	}
	TabTopBarBindings topBarBindings() override {
		return {
			.title = MediaTabTitle(_type) | rpl::map([](const QString &text) {
				return TextWithEntities{ text };
			}),
			.subtitle = SharedMediaCountValue(
				_countPeer,
				_topicRootId,
				_monoforumPeerId,
				_context.migrated,
				_type
			) | rpl::map([phrase = Media::MediaTextPhrase(_type)](int count) {
				return TextWithEntities{ (count > 0)
					? phrase(tr::now, lt_count, count)
					: QString() };
			}),
			.fillMenu = (MediaTabGrid(_type)
				? Fn<void(const Ui::Menu::MenuCallback&)>(crl::guard(
					base::make_weak(_list),
					[this](const Ui::Menu::MenuCallback &addAction) {
						fillMenu(addAction);
					}))
				: nullptr),
			.selectedItems = _list->selectedListValue(),
			.searchEnabledByContent = rpl::single(
				MediaTabSearchable(_type)),
			.selectionAction = crl::guard(
				base::make_weak(_list),
				[list = _list](SelectionAction action) {
					list->selectionAction(action);
				}),
			.applySearchQuery = crl::guard(
				base::make_weak(_list),
				[this](const QString &query) {
					_subController.applySearchQuery(query);
					_empty->setSearchQuery(query);
				}),
		};
	}

	void deactivated() override {
		_list->selectionAction(SelectionAction::Clear);
		_subController.applySearchQuery(QString());
		_empty->setSearchQuery(QString());
	}

	void setVisibleRegion(int top, int bottom) override {
		_fullHeight = bottom - top;
		_list->setExternalViewportHeight(bottom - top);
		_list->setVisibleTopBottom(top, bottom);
	}

	void setTopOverlay(int height) override {
		_topOverlay = height;
		_list->setTopOverlayHeight(height);
	}

private:
	[[nodiscard]] bool skeletonShown() const {
		return !_listLoaded && (_list->height() <= 0);
	}

	void updateHostHeight() {
		auto height = 0;
		if (skeletonShown()) {
			if (_empty) {
				_empty->hide();
			}
			height = st::infoMediaSkeletonMinHeight;
		} else if (_empty && (_list->height() <= 0)) {
			_empty->moveToLeft(0, 0);
			_empty->show();
			height = _empty->height();
		} else {
			if (_empty) {
				_empty->hide();
			}
			height = _list->height();
		}
		if (_host->height() != height) {
			_host->resize(_host->width(), height);
		}
		if (_skeleton) {
			_skeleton->setGeometry(Rect(QSize(
				std::max(
					_host->width() - st::infoMediaTabsRightSkip,
					1),
				_host->height())));
		}
	}

	void fillMenu(const Ui::Menu::MenuCallback &addAction) {
		if (!MediaTabGrid(_type)) {
			return;
		}
		const auto list = _list;
		if (list->canZoomIn()) {
			addAction(tr::lng_media_zoom_in(tr::now), [=] {
				list->zoomIn();
			}, &st::menuIconZoomIn);
		}
		if (list->canZoomOut()) {
			addAction(tr::lng_media_zoom_out(tr::now), [=] {
				list->zoomOut();
			}, &st::menuIconZoomOut);
		}
		const auto type = _type;
		const auto peer = _context.peer;
		const auto controller = _subController.parentController();
		addAction(tr::lng_calendar(tr::now), [=] {
			controller->showCalendar({
				.chat = Dialogs::Key(peer->owner().history(peer)),
				.date = QDate::currentDate(),
				.mediaPhoto = (type != SharedMediaType::Video),
				.mediaVideo = (type != SharedMediaType::Photo),
				.customJump = crl::guard(
					base::make_weak(list),
					[=](FullMsgId id, Fn<void()> close) {
						list->jumpToMessage(id.msg);
						close();
					}),
			});
		}, &st::menuIconSchedule);
	}

	const MediaTabContext _context;
	const SharedMediaType _type;
	const not_null<PeerData*> _countPeer;
	const MsgId _topicRootId;
	const PeerId _monoforumPeerId;
	MediaSubController _subController;
	object_ptr<Ui::RpWidget> _host;
	Media::ListWidget *_list = nullptr;
	Media::EmptyWidget *_empty = nullptr;
	rpl::variable<int> _fullHeight = 0;
	int _topOverlay = 0;
	object_ptr<Ui::RpWidget> _skeleton = { nullptr };
	bool _listLoaded = false;

};

} // namespace

const char kOptionProfileMediaTabsExpanded[] = "profile-media-tabs-expanded";

bool MediaTabsExpanded() {
	return MediaTabsExpandedOption.value();
}

rpl::producer<bool> MediaTabsExpandedValue() {
	return rpl::single(rpl::empty) | rpl::then(
		MediaTabsExpandedOption.changes()
	) | rpl::map([] {
		return MediaTabsExpandedOption.value();
	});
}

void SetMediaTabsExpanded(bool expanded) {
	MediaTabsExpandedOption.set(expanded);
}

MediaTabDescriptor MakeMediaTabDescriptor(
		SharedMediaType type,
		rpl::producer<bool> shown) {
	return {
		.id = MediaTabId(type),
		.title = MediaTabTitle(type) | rpl::map(Ui::Text::WithEntities),
		.shown = std::move(shown),
		.sharedMediaType = type,
		.factory = [type](MediaTabContext context) {
			return std::make_unique<MediaTabAdapter>(
				std::move(context),
				type);
		},
		.profileTab = MediaProfileTab(type),
	};
}

} // namespace Info::Profile
