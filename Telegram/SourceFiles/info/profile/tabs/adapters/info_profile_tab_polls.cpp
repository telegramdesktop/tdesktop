/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_polls.h"

#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "info/polls/info_polls_list_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/tabs/adapters/info_profile_tab_sub_controller.h"
#include "info/profile/tabs/info_profile_tab_skeleton.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace Info::Profile {
namespace {

using SharedMediaType = Storage::SharedMediaType;

class PollsTabAdapter final : public MediaTabContent {
public:
	explicit PollsTabAdapter(MediaTabContext context)
	: _subController(
		context.controller,
		SharedMediaType::Poll,
		context.migrated,
		true)
	, _countPeer(context.sublist
		? context.sublist->sublistPeer()
		: context.peer)
	, _topicRootId(context.topic ? context.topic->rootId() : MsgId())
	, _monoforumPeerId(context.sublist
		? context.sublist->sublistPeer()->id
		: PeerId())
	, _migrated(context.migrated)
	, _host(context.parent) {
		const auto host = _host.data();
		_polls = Polls::ListWidget::MakeInline(
			host,
			&_subController,
			[scrollTo = context.scrollToRequest](int top) {
				if (scrollTo) {
					scrollTo(top, -1);
				}
			});
		_skeleton = CreateTabSkeleton(host, SharedMediaType::Poll);
		_skeleton->show();

		host->paintRequest(
		) | rpl::on_next([this](QRect clip) {
			if (!skeletonShown()) {
				auto p = QPainter(_host.data());
				_polls.paintBackground(p, clip);
			}
		}, host->lifetime());
		host->widthValue(
		) | rpl::on_next([this](int newWidth) {
			_width = newWidth;
			updatePollsGeometry();
		}, host->lifetime());
		host->sizeValue(
		) | rpl::on_next([this](QSize size) {
			_skeleton->setGeometry(QRect(QPoint(), size));
		}, host->lifetime());
		_polls.list->heightValue(
		) | rpl::on_next([this](int newHeight) {
			if (newHeight > 0 && !_listLoaded) {
				_listLoaded = true;
				_skeleton->hide();
			}
			updateHostHeight();
		}, host->lifetime());
	}

	not_null<Ui::RpWidget*> widget() override {
		return _host.data();
	}
	TabTopBarBindings topBarBindings() override {
		return {
			.title = tr::lng_media_type_polls(
			) | rpl::map([](const QString &text) {
				return TextWithEntities{ text };
			}),
			.subtitle = SharedMediaCountValue(
				_countPeer,
				_topicRootId,
				_monoforumPeerId,
				_migrated,
				SharedMediaType::Poll
			) | rpl::map([](int count) {
				return TextWithEntities{ (count > 0)
					? tr::lng_profile_polls(tr::now, lt_count, count)
					: QString() };
			}),
			.fillMenu = (_polls.canCreatePoll
				? Fn<void(const Ui::Menu::MenuCallback&)>(crl::guard(
					base::make_weak(_host.data()),
					[this](const Ui::Menu::MenuCallback &addAction) {
						addAction(
							tr::lng_polls_create_title(tr::now),
							[create = _polls.createPoll] { create(); },
							&st::menuIconCreatePoll);
					}))
				: nullptr),
			.selectedItems = rpl::duplicate(_polls.selectedItems),
			.searchEnabledByContent = rpl::single(true),
			.selectionAction = crl::guard(
				base::make_weak(_host.data()),
				[this](SelectionAction action) {
					_polls.selectionAction(action);
				}),
			.applySearchQuery = crl::guard(
				base::make_weak(_host.data()),
				[this](const QString &query) {
					_subController.applySearchQuery(query);
					_polls.setSearchQuery(query);
				}),
		};
	}

	void deactivated() override {
		_polls.selectionAction(SelectionAction::Clear);
		_subController.applySearchQuery(QString());
		_polls.setSearchQuery(QString());
	}

	void setVisibleRegion(int top, int bottom) override {
		const auto height = bottom - top;
		if (_viewportHeight != height) {
			_viewportHeight = height;
			updatePollsGeometry();
		}
		_polls.setVisibleRegion(top, bottom);
		_host->update();
	}

private:
	[[nodiscard]] bool skeletonShown() const {
		return !_listLoaded && (_polls.list->height() <= 0);
	}

	void updatePollsGeometry() {
		if (_width > 0 && _viewportHeight > 0) {
			_polls.updateGeometry(_width, _viewportHeight);
		}
		updateHostHeight();
	}

	void updateHostHeight() {
		const auto height = skeletonShown()
			? st::infoMediaSkeletonMinHeight
			: _polls.list->height();
		if (_host->height() != height) {
			_host->resize(_host->width(), height);
		}
	}

	MediaSubController _subController;
	const not_null<PeerData*> _countPeer;
	const MsgId _topicRootId;
	const PeerId _monoforumPeerId;
	PeerData * const _migrated = nullptr;
	object_ptr<Ui::RpWidget> _host;
	Polls::InlinePolls _polls;
	object_ptr<Ui::RpWidget> _skeleton = { nullptr };
	int _width = 0;
	int _viewportHeight = 0;
	bool _listLoaded = false;

};

} // namespace

MediaTabDescriptor MakePollsTabDescriptor(rpl::producer<bool> shown) {
	return {
		.id = u"media:polls"_q,
		.title = tr::lng_media_type_polls(),
		.shown = std::move(shown),
		.factory = [](MediaTabContext context) {
			return std::make_unique<PollsTabAdapter>(std::move(context));
		},
	};
}

} // namespace Info::Profile
