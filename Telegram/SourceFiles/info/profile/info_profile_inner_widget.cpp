/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_inner_widget.h"

#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_widget.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/adapters/info_profile_tab_members.h"
#include "info/profile/tabs/adapters/info_profile_tab_peer_lists.h"
#include "info/profile/tabs/adapters/info_profile_tab_polls.h"
#include "info/profile/tabs/adapters/info_profile_tab_saved.h"
#include "info/profile/tabs/adapters/info_profile_tab_stories.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_music_button.h"
#include "info/profile/info_profile_shared_media_classic.h"
#include "info/profile/info_profile_top_bar.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/info_profile_values.h"
#include "info/saved/info_saved_music_widget.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_saved_music.h"
#include "data/data_saved_sublist.h"
#include "info/saved/info_saved_music_common.h"
#include "info_profile_actions.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "lang/lang_keys.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {

namespace {

void AddSavedMusic(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		rpl::producer<std::optional<QColor>> topBarColor) {
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout)));
	Info::Saved::SetupSavedMusic(
		wrap->entity(),
		controller,
		peer,
		std::move(topBarColor));
	using namespace rpl::mappers;
	wrap->toggleOn(
		wrap->entity()->heightValue() | rpl::map(_1 > 0),
		anim::type::instant);
}

void AddUnofficialSecurityRiskWarning(
		not_null<Ui::VerticalLayout*> layout,
		not_null<UserData*> user) {
	const auto wrap = layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			layout,
			object_ptr<Ui::VerticalLayout>(layout)));
	const auto content = wrap->entity();
	user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::on_next([=] {
		while (content->count()) {
			delete content->widgetAt(0);
		}
		if (user->unofficialSecurityRisk()) {
			auto helper = Ui::Text::CustomEmojiHelper();
			auto icon = helper.paletteDependent({
				.factory = [] {
					const auto s = st::infoSecurityRiskIconSize;
					const auto ratio = style::DevicePixelRatio();
					const auto rect = QRect(0, 0, s, s);
					auto result = QImage(
						rect.size() * ratio,
						QImage::Format_ARGB32_Premultiplied);
					result.setDevicePixelRatio(ratio);
					result.fill(Qt::transparent);

					auto p = QPainter(&result);
					auto hq = PainterHighQualityEnabler(p);
					p.setPen(Qt::NoPen);
					p.setBrush(st::attentionButtonFg);
					p.drawEllipse(rect);

					p.setPen(st::windowFgActive);
					p.setFont(st::semiboldFont);
					p.drawText(rect, u"!"_q, style::al_center);

					p.end();
					return result;
				},
				.margin = st::infoSecurityRiskIconMargin,
			});
			auto label = object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_profile_unofficial_warning(
					lt_icon,
					rpl::single(std::move(icon)),
					lt_name,
					rpl::single(TextWithEntities{ user->firstName }),
					tr::marked),
				st::defaultDividerLabel.label,
				st::defaultPopupMenu,
				helper.context([=] { content->update(); }));
			content->add(object_ptr<Ui::DividerLabel>(
				content,
				std::move(label),
				st::defaultBoxDividerLabelPadding,
				st::defaultDividerLabel.bar,
				RectParts()));
		}
		content->resizeToWidth(content->width());
	}, content->lifetime());
	using namespace rpl::mappers;
	wrap->toggleOn(
		content->heightValue() | rpl::map(_1 > 0),
		anim::type::instant);
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	Origin origin)
: RpWidget(parent)
, _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _topic(_controller->key().topic())
, _sublist(_controller->key().sublist())
, _content(setupContent(this, origin)) {
	_content->heightValue(
	) | rpl::on_next([this](int height) {
		if (!_inResize) {
			resizeToWidth(width());
			updateDesiredHeight();
		}
	}, lifetime());
}

rpl::producer<> InnerWidget::backRequest() const {
	return _backClicks.events();
}

object_ptr<Ui::RpWidget> InnerWidget::setupContent(
		not_null<RpWidget*> parent,
		Origin origin) {
	if (const auto user = _peer->asUser()) {
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::on_next([=] {
			auto &photos = user->session().api().peerPhoto();
			if (const auto original = photos.nonPersonalPhoto(user)) {
				// Preload it for the edit contact box.
				_nonPersonalView = original->createMediaView();
				const auto id = peerToUser(user->id);
				original->load(Data::FileOriginFullUser{ id });
			}
		}, lifetime());
	}

	auto result = object_ptr<Ui::VerticalLayout>(parent);

	const auto musicPeer = _sublist
		? _sublist->sublistPeer().get()
		: _peer.get();
	AddSavedMusic(
		result.data(),
		_controller,
		musicPeer,
		_topBarColor.value());
	if (const auto user = _peer->asUser()) {
		AddUnofficialSecurityRiskWarning(result.data(), user);
	}

	auto stack = SectionStack(result.data());
	if (_topic && _topic->creating()) {
		stack.finalize();
		return result;
	}

	BuildProfileDetailsSections(
		stack,
		_controller,
		_peer,
		_topic,
		_sublist,
		origin);

	const auto tabs = UseProfileMediaTabs();
	auto sharedTracker = Ui::MultiSlideTracker();
	if (!tabs) {
		auto sharedMediaWidget = SetupSharedMediaClassic(
			result.data(),
			_controller,
			_peer,
			_topic,
			_sublist,
			_migrated,
			sharedTracker);
		const auto raw = sharedMediaWidget.data();
		_sharedMediaWrap = raw;
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(sharedMediaWidget),
			.shown = raw->toggledValue(),
		});
	}

	const auto addTabsHost = [&] {
		using namespace rpl::mappers;
		const auto tabsPeer = _sublist ? _sublist->sublistPeer() : _peer;
		const auto topicRootId = _topic ? _topic->rootId() : MsgId();
		const auto monoforumPeerId = _sublist
			? _sublist->sublistPeer()->id
			: PeerId();
		auto tabs = std::vector<MediaTabDescriptor>();
		const auto addTab = [&](Storage::SharedMediaType type) {
			tabs.push_back(MakeMediaTabDescriptor(
				type,
				SharedMediaCountValue(
					tabsPeer,
					topicRootId,
					monoforumPeerId,
					_migrated,
					type) | rpl::map(_1 > 0)));
		};
		if ((_peer->isChat() || _peer->isMegagroup())
			&& !_peer->isMonoforum()
			&& !_topic
			&& !_sublist) {
			tabs.push_back(MakeMembersTabDescriptor(_peer));
		}
		if (!_topic) {
			tabs.push_back(MakeStoriesTabDescriptor(tabsPeer));
			if (!_sublist) {
				tabs.push_back(MakeGiftsTabDescriptor(_peer));
			}
			tabs.push_back(MakeSavedTabDescriptor(tabsPeer));
		}
		addTab(Storage::SharedMediaType::Photo);
		addTab(Storage::SharedMediaType::Video);
		addTab(Storage::SharedMediaType::File);
		addTab(Storage::SharedMediaType::MusicFile);
		addTab(Storage::SharedMediaType::Link);
		tabs.push_back(MakePollsTabDescriptor(SharedMediaCountValue(
			tabsPeer,
			topicRootId,
			monoforumPeerId,
			_migrated,
			Storage::SharedMediaType::Poll) | rpl::map(_1 > 0)));
		addTab(Storage::SharedMediaType::RoundVoiceFile);
		addTab(Storage::SharedMediaType::GIF);
		if (!_topic && !_sublist) {
			if (const auto user = _peer->asUser()) {
				tabs.push_back(MakeCommonGroupsTabDescriptor(user));
			}
			if (_peer->asBot() || _peer->asBroadcast()) {
				tabs.push_back(MakeSimilarPeersTabDescriptor(_peer));
			}
		}
		auto tabsHost = object_ptr<TabsHost>(
			result.data(),
			TabsHost::Descriptor{
				.context = MediaTabContext{
					.controller = _controller,
					.peer = _peer,
					.topic = _topic,
					.sublist = _sublist,
					.migrated = _migrated,
					.onlineCountChanged = [this](int count) {
						_onlineCount.fire_copy(count);
					},
				},
				.tabs = std::move(tabs),
			});
		const auto raw = tabsHost.data();
		_tabsHost = raw;
		raw->scrollToRequests(
		) | rpl::on_next([this, raw](Ui::ScrollToRequest request) {
			const auto shift = MapFrom(this, raw, QPoint()).y();
			_scrollToRequests.fire({
				request.ymin + shift,
				(request.ymax < 0) ? -1 : (request.ymax + shift),
			});
		}, raw->lifetime());
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(tabsHost),
			.shown = raw->heightValue() | rpl::map(_1 > 0),
		});
	};
	if (_topic || _sublist) {
		if (tabs) {
			addTabsHost();
		}
		stack.finalize();
		return result;
	}
	if (auto manage = SetupChannelMembersAndManage(
			_controller,
			result.data(),
			_peer)) {
		const auto raw = static_cast<Ui::SlideWrap<Ui::RpWidget>*>(
			manage.data());
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(manage),
			.shown = raw->toggledValue(),
		});
	}
	if (auto actions = SetupActions(_controller, result.data(), _peer)) {
		stack.addPlainSeparator();
		stack.add(Section{
			.widget = std::move(actions),
			.shown = rpl::single(true),
		});
	}
	if (tabs) {
		addTabsHost();
	} else if ((_peer->isChat() || _peer->isMegagroup())
		&& !_peer->isMonoforum()) {
		stack.addPlainSeparator();
		stack.add(makeMembersSection(result.data()));
	}
	stack.finalize();
	return result;
}

Section InnerWidget::makeMembersSection(not_null<QWidget*> parent) {
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto inner = raw->entity();
	_members = inner->add(object_ptr<Members>(inner, _controller));
	_members->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		auto min = (request.ymin < 0)
			? request.ymin
			: MapFrom(this, _members, QPoint(0, request.ymin)).y();
		auto max = (request.ymin < 0)
			? MapFrom(this, _members, QPoint()).y()
			: (request.ymax < 0)
			? request.ymax
			: MapFrom(this, _members, QPoint(0, request.ymax)).y();
		_scrollToRequests.fire({ min, max });
	}, _members->lifetime());
	_members->onlineCountValue(
	) | rpl::on_next([=](int count) {
		_onlineCount.fire_copy(count);
	}, _members->lifetime());

	using namespace rpl::mappers;
	raw->toggleOn(
		_members->fullCountValue() | rpl::map(_1 > 0),
		anim::type::instant);
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

int InnerWidget::countDesiredHeight() const {
	return _content->height() + (_members
		? (_members->desiredHeight() - _members->height())
		: 0);
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
	if (_tabsHost) {
		const auto top = MapFrom(this, _tabsHost, QPoint()).y();
		_tabsHost->setVisibleRegion(visibleTop - top, visibleBottom - top);
		_tabsDocked = (visibleTop >= top);
	}
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	if (_members) {
		memento->setMembersState(_members->saveState());
	}
	if (_tabsHost) {
		memento->setActiveTab(_tabsHost->activeId());
	}
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	if (_members) {
		_members->restoreState(memento->membersState());
	}
	if (_sharedMediaWrap) {
		_sharedMediaWrap->finishAnimating();
	}
	if (_tabsHost) {
		if (const auto active = memento->activeTab(); !active.isEmpty()) {
			_tabsHost->restoreActiveTab(active);
		}
	}
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<int> InnerWidget::desiredHeightValue() const {
	return _desiredHeight.events_starting_with(countDesiredHeight());
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	updateDesiredHeight();
	return _content->heightNoMargins();
}

void InnerWidget::enableBackButton() {
	_backToggles.force_assign(true);
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

bool InnerWidget::hasFlexibleTopBar() const {
	return true;
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToTop(
		not_null<Ui::RpWidget*> parent) {
	const auto content = Ui::CreateChild<TopBar>(
		parent,
		TopBar::Descriptor{
			.controller = _controller->parentController(),
			.key = _controller->key(),
			.wrap = _controller->wrapValue(),
			.peer = _sublist ? _sublist->sublistPeer().get() : nullptr,
			.backToggles = _backToggles.value(),
			.showFinished = _showFinished.events(),
		});
	content->backRequest(
	) | rpl::start_to_stream(_backClicks, content->lifetime());
	content->setOnlineCount(_onlineCount.events());
	if (_tabsHost) {
		content->bindActiveTab(
			_tabsHost->activeTabBindings(),
			_tabsDocked.value());
	}
	_topBarColor = content->edgeColor();
	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	return nullptr;
}

} // namespace Profile
} // namespace Info
