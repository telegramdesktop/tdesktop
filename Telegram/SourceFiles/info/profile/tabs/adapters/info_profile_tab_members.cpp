/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/adapters/info_profile_tab_members.h"

#include "data/data_peer.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/scroll_area.h"

namespace Info::Profile {
namespace {

constexpr auto kEnableSearchMembersAfterCount = 20;

class MembersTabAdapter final : public MediaTabContent {
public:
	explicit MembersTabAdapter(MediaTabContext context)
	: _peer(context.peer)
	, _host(context.parent) {
		const auto host = _host.data();
		_members = Ui::CreateChild<Members>(host, context.controller, true);
		_members->show();
		_members->heightValue(
		) | rpl::on_next([this](int newHeight) {
			_host->resize(_host->width(), newHeight);
		}, host->lifetime());
		_members->scrollToRequests(
		) | rpl::on_next([scrollTo = context.scrollToRequest](
				Ui::ScrollToRequest request) {
			if (!scrollTo) {
				return;
			} else if (request.ymin < 0) {
				scrollTo(0, -1);
			} else {
				scrollTo(request.ymin, request.ymax);
			}
		}, host->lifetime());
		if (const auto onlineChanged = context.onlineCountChanged) {
			_members->onlineCountValue(
			) | rpl::on_next([onlineChanged](int count) {
				onlineChanged(count);
			}, host->lifetime());
		}
	}

	not_null<Ui::RpWidget*> widget() override {
		return _host.data();
	}
	void resizeToWidth(int newWidth) override {
		if (_host->width() != newWidth) {
			_members->resizeToWidth(newWidth);
		}
		_host->resize(newWidth, _members->height());
	}
	TabTopBarBindings topBarBindings() override {
		using namespace rpl::mappers;
		return {
			.title = tr::lng_profile_participants_section(
			) | rpl::map([](const QString &text) {
				return TextWithEntities{ text };
			}),
			.subtitle = MembersCountValue(
				_peer
			) | rpl::map([](int count) {
				return TextWithEntities{ (count > 0)
					? tr::lng_chat_status_members(
						tr::now,
						lt_count_decimal,
						count)
					: QString() };
			}),
			.searchEnabledByContent = MembersCountValue(
				_peer
			) | rpl::map(_1 >= kEnableSearchMembersAfterCount),
			.applySearchQuery = crl::guard(
				base::make_weak(_members),
				[this](const QString &query) {
					if (_searchQuery != query) {
						_searchQuery = query;
						_members->applySearchQuery(query);
					}
				}),
			.groupByRoleState = _members->groupByRoleValue(),
			.setGroupByRole = crl::guard(
				base::make_weak(_members),
				[this](bool grouped) {
					_members->setGroupByRole(grouped);
				}),
			.groupByRoleAvailable = _members->groupByRoleAvailableValue(),
		};
	}

	void deactivated() override {
		if (!_searchQuery.isEmpty()) {
			_searchQuery = QString();
			_members->applySearchQuery(QString());
		}
	}

	void setVisibleRegion(int top, int bottom) override {
		_members->setVisibleTopBottom(top, bottom);
	}

private:
	const not_null<PeerData*> _peer;
	object_ptr<Ui::RpWidget> _host;
	Members *_members = nullptr;
	QString _searchQuery;

};

} // namespace

MediaTabDescriptor MakeMembersTabDescriptor(
		not_null<PeerData*> peer,
		rpl::producer<bool> shown) {
	return {
		.id = u"members"_q,
		.title = tr::lng_profile_participants_section(tr::marked),
		.shown = std::move(shown),
		.factory = [](MediaTabContext context) {
			return std::make_unique<MembersTabAdapter>(std::move(context));
		},
	};
}

} // namespace Info::Profile
