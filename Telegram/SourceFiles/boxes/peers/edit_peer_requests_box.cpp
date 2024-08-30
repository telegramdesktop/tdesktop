/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_requests_box.h"

#include "ui/effects/ripple_animation.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration
#include "boxes/peers/edit_peer_invite_link.h" // PrepareRequestedRowStatus
#include "boxes/peers/prepare_short_info_box.h" // PrepareShortInfoBox
#include "history/view/history_view_requests_bar.h" // kRecentRequestsLimit
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "ui/round_rect.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kFirstPageCount = 16;
constexpr auto kPerPage = 200;
constexpr auto kServerSearchDelay = crl::time(1000);
constexpr auto kAcceptButton = 1;
constexpr auto kRejectButton = 2;

class RowDelegate {
public:
	[[nodiscard]] virtual QSize rowAcceptButtonSize() = 0;
	[[nodiscard]] virtual QSize rowRejectButtonSize() = 0;
	virtual void rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) = 0;
	virtual void rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) = 0;
};

class Row final : public PeerListRow {
public:
	Row(
		not_null<RowDelegate*> delegate,
		not_null<UserData*> user,
		TimeId date);

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	const not_null<RowDelegate*> _delegate;
	std::unique_ptr<Ui::RippleAnimation> _acceptRipple;
	std::unique_ptr<Ui::RippleAnimation> _rejectRipple;

};

Row::Row(
	not_null<RowDelegate*> delegate,
	not_null<UserData*> user,
	TimeId date)
: PeerListRow(user)
, _delegate(delegate) {
	setCustomStatus(PrepareRequestedRowStatus(date));
}

int Row::elementsCount() const {
	return 2;
}

QRect Row::elementGeometry(int element, int outerWidth) const {
	switch (element) {
	case kAcceptButton: {
		const auto size = _delegate->rowAcceptButtonSize();
		return QRect(st::requestAcceptPosition, size);
	} break;
	case kRejectButton: {
		const auto accept = _delegate->rowAcceptButtonSize();
		const auto size = _delegate->rowRejectButtonSize();
		return QRect(
			(st::requestAcceptPosition
				+ QPoint(accept.width() + st::requestButtonsSkip, 0)),
			size);
	} break;
	}
	return QRect();
}

bool Row::elementDisabled(int element) const {
	return false;
}

bool Row::elementOnlySelect(int element) const {
	return true;
}

void Row::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	const auto pointer = (element == kAcceptButton)
		? &_acceptRipple
		: (element == kRejectButton)
		? &_rejectRipple
		: nullptr;
	if (!pointer) {
		return;
	}
	auto &ripple = *pointer;
	if (!ripple) {
		auto mask = Ui::RippleAnimation::RoundRectMask(
			(element == kAcceptButton
				? _delegate->rowAcceptButtonSize()
				: _delegate->rowRejectButtonSize()),
			st::buttonRadius);
		ripple = std::make_unique<Ui::RippleAnimation>(
			(element == kAcceptButton
				? st::requestsAcceptButton.ripple
				: st::requestsRejectButton.ripple),
			std::move(mask),
			std::move(updateCallback));
	}
	ripple->add(point);
}

void Row::elementsStopLastRipple() {
	if (_acceptRipple) {
		_acceptRipple->lastStop();
	}
	if (_rejectRipple) {
		_rejectRipple->lastStop();
	}
}

void Row::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	const auto accept = elementGeometry(kAcceptButton, outerWidth);
	const auto reject = elementGeometry(kRejectButton, outerWidth);

	const auto over = [&](int element) {
		return (selectedElement == element);
	};
	_delegate->rowPaintAccept(
		p,
		accept,
		_acceptRipple,
		outerWidth,
		over(kAcceptButton));
	_delegate->rowPaintReject(
		p,
		reject,
		_rejectRipple,
		outerWidth,
		over(kRejectButton));
}

} // namespace

class RequestsBoxController::RowHelper final : public RowDelegate {
public:
	explicit RowHelper(bool isGroup);

	[[nodiscard]] QSize rowAcceptButtonSize() override;
	[[nodiscard]] QSize rowRejectButtonSize() override;
	void rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) override;
	void rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) override;

private:
	void paintButton(
		Painter &p,
		QRect geometry,
		const style::RoundButton &st,
		const Ui::RoundRect &rect,
		const Ui::RoundRect &rectOver,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		const QString &text,
		int textWidth,
		int outerWidth,
		bool over);

	Ui::RoundRect _acceptRect;
	Ui::RoundRect _acceptRectOver;
	Ui::RoundRect _rejectRect;
	Ui::RoundRect _rejectRectOver;
	QString _acceptText;
	QString _rejectText;
	int _acceptTextWidth = 0;
	int _rejectTextWidth = 0;

};

RequestsBoxController::RowHelper::RowHelper(bool isGroup)
: _acceptRect(st::buttonRadius, st::requestsAcceptButton.textBg)
, _acceptRectOver(st::buttonRadius, st::requestsAcceptButton.textBgOver)
, _rejectRect(st::buttonRadius, st::requestsRejectButton.textBg)
, _rejectRectOver(st::buttonRadius, st::requestsRejectButton.textBgOver)
, _acceptText(isGroup
	? tr::lng_group_requests_add(tr::now)
	: tr::lng_group_requests_add_channel(tr::now))
, _rejectText(tr::lng_group_requests_dismiss(tr::now))
, _acceptTextWidth(st::requestsAcceptButton.style.font->width(_acceptText))
, _rejectTextWidth(st::requestsRejectButton.style.font->width(_rejectText)) {
}

RequestsBoxController::RequestsBoxController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: PeerListController(CreateSearchController(peer))
, _navigation(navigation)
, _helper(std::make_unique<RowHelper>(!peer->isBroadcast()))
, _peer(peer)
, _api(&_peer->session().mtp()) {
	setStyleOverrides(&st::requestsBoxList);
	subscribeToMigration();
}

RequestsBoxController::~RequestsBoxController() = default;

void RequestsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	auto controller = std::make_unique<RequestsBoxController>(
		navigation,
		peer->migrateToOrMe());
	const auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	};
	navigation->parentController()->show(
		Box<PeerListBox>(std::move(controller), initBox));
}

Main::Session &RequestsBoxController::session() const {
	return _peer->session();
}

auto RequestsBoxController::CreateSearchController(not_null<PeerData*> peer)
-> std::unique_ptr<PeerListSearchController> {
	return std::make_unique<RequestsBoxSearchController>(peer);
}

std::unique_ptr<PeerListRow> RequestsBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void RequestsBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(_peer->isBroadcast()
		? tr::lng_manage_peer_requests_channel()
		: tr::lng_manage_peer_requests());
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
	loadMoreRows();
}

void RequestsBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (_loadRequestId || _allLoaded) {
		return;
	}

	// First query is small and fast, next loads a lot of rows.
	const auto limit = _offsetDate ? kPerPage : kFirstPageCount;
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	_loadRequestId = _api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(Flag::f_requested),
		_peer->input,
		MTPstring(), // link
		MTPstring(), // q
		MTP_int(_offsetDate),
		_offsetUser ? _offsetUser->inputUser : MTP_inputUserEmpty(),
		MTP_int(limit)
	)).done([=](const MTPmessages_ChatInviteImporters &result) {
		const auto firstLoad = !_offsetDate;
		_loadRequestId = 0;

		result.match([&](const MTPDmessages_chatInviteImporters &data) {
			session().data().processUsers(data.vusers());
			const auto &importers = data.vimporters().v;
			auto &owner = _peer->owner();
			for (const auto &importer : importers) {
				importer.match([&](const MTPDchatInviteImporter &data) {
					_offsetDate = data.vdate().v;
					_offsetUser = owner.user(data.vuser_id());
					appendRow(_offsetUser, _offsetDate);
				});
			}
			// To be sure - wait for a whole empty result list.
			_allLoaded = importers.isEmpty();
		});

		if (_allLoaded
			|| (firstLoad && delegate()->peerListFullRowsCount() > 0)) {
			refreshDescription();
		}
		delegate()->peerListRefreshRows();
	}).fail([=] {
		_loadRequestId = 0;
		_allLoaded = true;
	}).send();
}

void RequestsBoxController::refreshDescription() {
	setDescriptionText((delegate()->peerListFullRowsCount() > 0)
		? QString()
		: _peer->isBroadcast()
		? tr::lng_group_requests_none_channel(tr::now)
		: tr::lng_group_requests_none(tr::now));
}

void RequestsBoxController::rowClicked(not_null<PeerListRow*> row) {
	_navigation->parentController()->show(PrepareShortInfoBox(
		row->peer(),
		_navigation));
}

void RequestsBoxController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	processRequest(row->peer()->asUser(), (element == kAcceptButton));
}

void RequestsBoxController::processRequest(
		not_null<UserData*> user,
		bool approved) {
	const auto remove = [=] {
		if (const auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
			refreshDescription();
			delegate()->peerListRefreshRows();
		}
		static_cast<RequestsBoxSearchController*>(
			searchController())->removeFromCache(user);
	};
	const auto done = crl::guard(this, [=] {
		remove();
		if (approved) {
			delegate()->peerListUiShow()->showToast((_peer->isBroadcast()
				? tr::lng_group_requests_was_added_channel
				: tr::lng_group_requests_was_added)(
					tr::now,
					lt_user,
					Ui::Text::Bold(user->name()),
					Ui::Text::WithEntities));
		}
	});
	const auto fail = crl::guard(this, remove);
	session().api().inviteLinks().processRequest(
		_peer,
		QString(), // link
		user,
		approved,
		done,
		fail);
}

void RequestsBoxController::appendRow(
		not_null<UserData*> user,
		TimeId date) {
	if (!delegate()->peerListFindRow(user->id.value)) {
		if (auto row = createRow(user, date)) {
			delegate()->peerListAppendRow(std::move(row));
			setDescriptionText(QString());
		}
	}
}

QSize RequestsBoxController::RowHelper::rowAcceptButtonSize() {
	const auto &st = st::requestsAcceptButton;
	return {
		 (st.width <= 0) ? (_acceptTextWidth - st.width) : st.width,
		 st.height,
	};
}

QSize RequestsBoxController::RowHelper::rowRejectButtonSize() {
	const auto &st = st::requestsRejectButton;
	return {
		 (st.width <= 0) ? (_rejectTextWidth - st.width) : st.width,
		 st.height,
	};
}

void RequestsBoxController::RowHelper::rowPaintAccept(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) {
	paintButton(
		p,
		geometry,
		st::requestsAcceptButton,
		_acceptRect,
		_acceptRectOver,
		ripple,
		_acceptText,
		_acceptTextWidth,
		outerWidth,
		over);
}

void RequestsBoxController::RowHelper::rowPaintReject(
		Painter &p,
		QRect geometry,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		int outerWidth,
		bool over) {
	paintButton(
		p,
		geometry,
		st::requestsRejectButton,
		_rejectRect,
		_rejectRectOver,
		ripple,
		_rejectText,
		_rejectTextWidth,
		outerWidth,
		over);
}

void RequestsBoxController::RowHelper::paintButton(
		Painter &p,
		QRect geometry,
		const style::RoundButton &st,
		const Ui::RoundRect &rect,
		const Ui::RoundRect &rectOver,
		std::unique_ptr<Ui::RippleAnimation> &ripple,
		const QString &text,
		int textWidth,
		int outerWidth,
		bool over) {
	rect.paint(p, geometry);
	if (over) {
		rectOver.paint(p, geometry);
	}
	if (ripple) {
		ripple->paint(p, geometry.x(), geometry.y(), outerWidth);
		if (ripple->empty()) {
			ripple = nullptr;
		}
	}

	const auto textLeft = geometry.x()
		+ ((geometry.width() - textWidth) / 2);
	const auto textTop = geometry.y() + st.textTop;
	p.setFont(st.style.font);
	p.setPen(over ? st.textFgOver : st.textFg);
	p.drawTextLeft(textLeft, textTop, outerWidth, text);
}

std::unique_ptr<PeerListRow> RequestsBoxController::createRow(
		not_null<UserData*> user,
		TimeId date) {
	if (!date) {
		const auto search = static_cast<RequestsBoxSearchController*>(
			searchController());
		date = search->dateForUser(user);
	}
	return std::make_unique<Row>(_helper.get(), user, date);
}

void RequestsBoxController::subscribeToMigration() {
	const auto chat = _peer->asChat();
	if (!chat) {
		return;
	}
	SubscribeToMigration(
		chat,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(chat, channel); });
}

void RequestsBoxController::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
}

RequestsBoxSearchController::RequestsBoxSearchController(
	not_null<PeerData*> peer)
: _peer(peer)
, _api(&_peer->session().mtp()) {
	_timer.setCallback([=] { searchOnServer(); });
}

void RequestsBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offsetDate = 0;
		_offsetUser = nullptr;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(kServerSearchDelay);
		} else {
			_timer.cancel();
		}
	}
}

void RequestsBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());

	loadMoreRows();
}

bool RequestsBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

void RequestsBoxSearchController::removeFromCache(not_null<UserData*> user) {
	for (auto &entry : _cache) {
		auto &items = entry.second.items;
		const auto j = ranges::remove(items, user, &Item::user);
		if (j != end(items)) {
			entry.second.requestedCount -= (end(items) - j);
			items.erase(j, end(items));
		}
	}
}

TimeId RequestsBoxSearchController::dateForUser(not_null<UserData*> user) {
	if (const auto i = _dates.find(user); i != end(_dates)) {
		return i->second;
	}
	return {};
}

bool RequestsBoxSearchController::searchInCache() {
	const auto i = _cache.find(_query);
	if (i != _cache.cend()) {
		_requestId = 0;
		searchDone(
			_requestId,
			i->second.items,
			i->second.requestedCount);
		return true;
	}
	return false;
}

bool RequestsBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	} else if (_allLoaded || isLoading()) {
		return true;
	}
	// For search we request a lot of rows from the first query.
	// (because we've waited for search request by timer already,
	// so we don't expect it to be fast, but we want to fill cache).
	const auto limit = kPerPage;
	using Flag = MTPmessages_GetChatInviteImporters::Flag;
	_requestId = _api.request(MTPmessages_GetChatInviteImporters(
		MTP_flags(Flag::f_requested | Flag::f_q),
		_peer->input,
		MTPstring(), // link
		MTP_string(_query),
		MTP_int(_offsetDate),
		_offsetUser ? _offsetUser->inputUser : MTP_inputUserEmpty(),
		MTP_int(limit)
	)).done([=](
			const MTPmessages_ChatInviteImporters &result,
			mtpRequestId requestId) {
		auto items = std::vector<Item>();
		result.match([&](const MTPDmessages_chatInviteImporters &data) {
			const auto &importers = data.vimporters().v;
			auto &owner = _peer->owner();
			owner.processUsers(data.vusers());
			items.reserve(importers.size());
			for (const auto &importer : importers) {
				importer.match([&](const MTPDchatInviteImporter &data) {
					items.push_back({
						owner.user(data.vuser_id()),
						data.vdate().v,
					});
				});
			}
		});
		searchDone(requestId, items, limit);

		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			const auto &query = it->second.text;
			if (it->second.offsetDate == 0) {
				auto &entry = _cache[query];
				entry.items = std::move(items);
				entry.requestedCount = limit;
			}
			_queries.erase(it);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_allLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();

	auto entry = Query();
	entry.text = _query;
	entry.offsetDate = _offsetDate;
	_queries.emplace(_requestId, entry);
	return true;
}

void RequestsBoxSearchController::searchDone(
		mtpRequestId requestId,
		const std::vector<Item> &items,
		int requestedCount) {
	if (_requestId != requestId) {
		return;
	}

	_requestId = 0;
	if (!_offsetDate) {
		_dates.clear();
	}
	for (const auto &[user, date] : items) {
		_offsetDate = date;
		_offsetUser = user;
		_dates.emplace(user, date);
		delegate()->peerListSearchAddRow(user);
	}
	if (items.size() < requestedCount) {
		// We want cache to have full information about a query with
		// small results count (that we don't need the second request).
		// So we don't wait for empty list unlike the non-search case.
		_allLoaded = true;
	}
	delegate()->peerListSearchRefreshRows();
}
