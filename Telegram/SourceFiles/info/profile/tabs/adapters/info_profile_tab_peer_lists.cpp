/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_peer_lists.h"

#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "info/common_groups/info_common_groups_inner_widget.h"
#include "info/info_controller.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/similar_peers/info_similar_peers_widget.h"
#include "lang/lang_keys.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/rp_widget.h"

namespace Info::Profile {
namespace {

class WidgetTabAdapter final : public MediaTabContent {
public:
	WidgetTabAdapter(
		object_ptr<Ui::RpWidget> content,
		Fn<TabTopBarBindings()> bindings)
	: _content(std::move(content))
	, _bindings(std::move(bindings)) {
	}

	not_null<Ui::RpWidget*> widget() override {
		return _content.data();
	}
	TabTopBarBindings topBarBindings() override {
		return _bindings();
	}
	void setVisibleRegion(int top, int bottom) override {
		_content->setVisibleTopBottom(top, bottom);
	}

private:
	object_ptr<Ui::RpWidget> _content;
	Fn<TabTopBarBindings()> _bindings;

};

[[nodiscard]] rpl::producer<TextWithEntities> ToTitle(
		rpl::producer<QString> text) {
	return std::move(text) | rpl::map([](const QString &value) {
		return TextWithEntities{ value };
	});
}

} // namespace

MediaTabDescriptor MakeCommonGroupsTabDescriptor(
		not_null<UserData*> user) {
	using namespace rpl::mappers;
	return {
		.id = u"common-groups"_q,
		.title = tr::lng_media_type_groups(),
		.shown = CommonGroupsCountValue(user) | rpl::map(_1 > 0),
		.factory = [=](MediaTabContext context) {
			auto content = object_ptr<CommonGroups::InnerWidget>(
				context.parent,
				context.controller,
				user);
			return std::make_unique<WidgetTabAdapter>(
				std::move(content),
				[=] {
					return TabTopBarBindings{
						.title = ToTitle(tr::lng_media_type_groups()),
						.subtitle = CommonGroupsCountValue(
							user
						) | rpl::map([](int count) {
							return TextWithEntities{ (count > 0)
								? tr::lng_profile_common_groups(
									tr::now,
									lt_count,
									count)
								: QString() };
						}),
					};
				});
		},
	};
}

MediaTabDescriptor MakeSimilarPeersTabDescriptor(
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;
	const auto channel = (peer->asBroadcast() != nullptr);
	return {
		.id = u"similar"_q,
		.title = tr::lng_media_type_similar(),
		.shown = SimilarPeersCountValue(peer) | rpl::map(_1 > 0),
		.factory = [=](MediaTabContext context) {
			auto content = SimilarPeers::MakeSimilarPeersInner(
				context.parent,
				context.controller,
				peer);
			return std::make_unique<WidgetTabAdapter>(
				std::move(content),
				[=] {
					return TabTopBarBindings{
						.title = ToTitle(tr::lng_media_type_similar()),
						.subtitle = SimilarPeersCountValue(
							peer
						) | rpl::map([=](int count) {
							return TextWithEntities{ (count > 0)
								? (channel
									? tr::lng_profile_similar_channels(
										tr::now,
										lt_count,
										count)
									: tr::lng_profile_similar_bots(
										tr::now,
										lt_count,
										count))
								: QString() };
						}),
					};
				});
		},
	};
}

class GiftsTabAdapter final : public MediaTabContent {
public:
	GiftsTabAdapter(MediaTabContext context, not_null<PeerData*> peer)
	: _peer(peer) {
		auto inline_ = PeerGifts::MakePeerGiftsInner(
			context.parent,
			context.controller->parentController(),
			peer,
			_descriptor.value());
		_content = std::move(inline_.widget);
		_fillMenu = std::move(inline_.fillMenu);
		std::move(
			inline_.descriptorChanges
		) | rpl::on_next([this](PeerGifts::Descriptor descriptor) {
			_descriptor = descriptor;
		}, _content->lifetime());
	}

	not_null<Ui::RpWidget*> widget() override {
		return _content.data();
	}
	TabTopBarBindings topBarBindings() override {
		const auto peer = _peer;
		return {
			.title = ToTitle(tr::lng_media_type_gifts()),
			.subtitle = PeerGiftsCountValue(
				peer
			) | rpl::map([](int count) {
				return TextWithEntities{ (count > 0)
					? tr::lng_profile_peer_gifts(tr::now, lt_count, count)
					: QString() };
			}),
			.fillMenu = crl::guard(
				base::make_weak(_content.data()),
				[this](const Ui::Menu::MenuCallback &addAction) {
					_fillMenu(addAction);
				}),
		};
	}
	void setVisibleRegion(int top, int bottom) override {
		_content->setVisibleTopBottom(top, bottom);
	}

private:
	const not_null<PeerData*> _peer;
	rpl::variable<PeerGifts::Descriptor> _descriptor;
	object_ptr<Ui::RpWidget> _content = { nullptr };
	Fn<void(const Ui::Menu::MenuCallback&)> _fillMenu;

};

MediaTabDescriptor MakeGiftsTabDescriptor(not_null<PeerData*> peer) {
	using namespace rpl::mappers;
	return {
		.id = u"gifts"_q,
		.title = tr::lng_media_type_gifts(),
		.shown = PeerGiftsCountValue(peer) | rpl::map(_1 > 0),
		.factory = [=](MediaTabContext context) {
			return std::make_unique<GiftsTabAdapter>(
				std::move(context),
				peer);
		},
	};
}

} // namespace Info::Profile
