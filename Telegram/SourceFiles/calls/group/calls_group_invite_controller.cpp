/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_invite_controller.h"

#include "api/api_chat_participants.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_menu.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "boxes/peer_lists_box.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_group_call.h"
#include "info/profile/info_profile_icon.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h" // membersMarginTop
#include "styles/style_calls.h"
#include "styles/style_dialogs.h" // searchedBarHeight
#include "styles/style_layers.h" // boxWideWidth

namespace Calls::Group {
namespace {

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSectionSubtitle(
		QWidget *parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::searchedBarHeight);

	const auto raw = result.data();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::groupCallMembersBgOver);
	}, raw->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::groupCallBoxLabel);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::groupCallInviteDividerPadding;
		const auto available = width - padding.left() - padding.right();
		label->resizeToNaturalWidth(available);
		label->moveToLeft(padding.left(), padding.top(), width);
	}, label->lifetime());

	return result;
}

struct ConfInviteStyles {
	const style::IconButton *video = nullptr;
	const style::icon *videoActive = nullptr;
	const style::IconButton *audio = nullptr;
	const style::icon *audioActive = nullptr;
	const style::SettingsButton *inviteViaLink = nullptr;
	const style::icon *inviteViaLinkIcon = nullptr;
};

class ConfInviteRow final : public PeerListRow {
public:
	ConfInviteRow(not_null<UserData*> user, const ConfInviteStyles &st);

	void setAlreadyIn(bool alreadyIn);
	void setVideo(bool video);

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
	[[nodiscard]] const style::IconButton &buttonSt(int element) const;

	const ConfInviteStyles &_st;
	std::unique_ptr<Ui::RippleAnimation> _videoRipple;
	std::unique_ptr<Ui::RippleAnimation> _audioRipple;
	bool _alreadyIn = false;
	bool _video = false;

};

struct PrioritizedSelector {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<void()> init;
	Fn<bool(int, int, int)> overrideKey;
	Fn<void(PeerListRowId)> deselect;
	Fn<void()> activate;
	rpl::producer<Ui::ScrollToRequest> scrollToRequests;
};

class ConfInviteController final : public ContactsBoxController {
public:
	ConfInviteController(
		not_null<Main::Session*> session,
		ConfInviteStyles st,
		base::flat_set<not_null<UserData*>> alreadyIn,
		Fn<void()> shareLink,
		std::vector<not_null<UserData*>> prioritize);

	[[nodiscard]] rpl::producer<bool> hasSelectedValue() const;
	[[nodiscard]] std::vector<InviteRequest> requests(
		const std::vector<not_null<PeerData*>> &peers) const;

	void noSearchSubmit();
	[[nodiscard]] auto prioritizeScrollRequests() const
		-> rpl::producer<Ui::ScrollToRequest>;

protected:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(not_null<PeerListRow*> row, int element) override;
	bool handleDeselectForeignRow(PeerListRowId itemId) override;

	bool overrideKeyboardNavigation(
			int direction,
			int fromIndex,
			int toIndex) override;
private:
	[[nodiscard]] int fullCount() const;
	void toggleRowSelected(not_null<PeerListRow*> row, bool video);
	[[nodiscard]] bool toggleRowGetChecked(
		not_null<PeerListRow*> row,
		bool video);
	void addShareLinkButton();
	void addPriorityInvites();

	const ConfInviteStyles _st;
	const base::flat_set<not_null<UserData*>> _alreadyIn;
	const std::vector<not_null<UserData*>> _prioritize;
	const Fn<void()> _shareLink;
	PrioritizedSelector _prioritizeRows;
	rpl::event_stream<Ui::ScrollToRequest> _prioritizeScrollRequests;
	base::flat_set<not_null<UserData*>> _skip;
	rpl::variable<bool> _hasSelected;
	base::flat_set<not_null<UserData*>> _withVideo;
	bool _lastSelectWithVideo = false;

};

[[nodiscard]] ConfInviteStyles ConfInviteDarkStyles() {
	return {
		.video = &st::confcallInviteVideo,
		.videoActive = &st::confcallInviteVideoActive,
		.audio = &st::confcallInviteAudio,
		.audioActive = &st::confcallInviteAudioActive,
		.inviteViaLink = &st::groupCallInviteLink,
		.inviteViaLinkIcon = &st::groupCallInviteLinkIcon,
	};
}

[[nodiscard]] ConfInviteStyles ConfInviteDefaultStyles() {
	return {
		.video = &st::createCallVideo,
		.videoActive = &st::createCallVideoActive,
		.audio = &st::createCallAudio,
		.audioActive = &st::createCallAudioActive,
		.inviteViaLink = &st::createCallInviteLink,
		.inviteViaLinkIcon = &st::createCallInviteLinkIcon,
	};
}

ConfInviteRow::ConfInviteRow(not_null<UserData*> user, const ConfInviteStyles &st)
: PeerListRow(user)
, _st(st) {
}

void ConfInviteRow::setAlreadyIn(bool alreadyIn) {
	_alreadyIn = alreadyIn;
	setDisabledState(alreadyIn ? State::DisabledChecked : State::Active);
}

void ConfInviteRow::setVideo(bool video) {
	_video = video;
}

const style::IconButton &ConfInviteRow::buttonSt(int element) const {
	return (element == 1)
		? (_st.video ? *_st.video : st::createCallVideo)
		: (_st.audio ? *_st.audio : st::createCallAudio);
}

int ConfInviteRow::elementsCount() const {
	return _alreadyIn ? 0 : 2;
}

QRect ConfInviteRow::elementGeometry(int element, int outerWidth) const {
	if (_alreadyIn || (element != 1 && element != 2)) {
		return QRect();
	}
	const auto &st = buttonSt(element);
	const auto size = QSize(st.width, st.height);
	const auto margins = (element == 1)
		? st::createCallVideoMargins
		: st::createCallAudioMargins;
	const auto right = margins.right();
	const auto top = margins.top();
	const auto side = (element == 1)
		? outerWidth
		: elementGeometry(1, outerWidth).x();
	const auto left = side - right - size.width();
	return QRect(QPoint(left, top), size);
}

bool ConfInviteRow::elementDisabled(int element) const {
	return _alreadyIn
		|| (checked()
			&& ((_video && element == 1) || (!_video && element == 2)));
}

bool ConfInviteRow::elementOnlySelect(int element) const {
	return false;
}

void ConfInviteRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (_alreadyIn || (element != 1 && element != 2)) {
		return;
	}
	auto &ripple = (element == 1) ? _videoRipple : _audioRipple;
	const auto &st = buttonSt(element);
	if (!ripple) {
		auto mask = Ui::RippleAnimation::EllipseMask(QSize(
				st.rippleAreaSize,
				st.rippleAreaSize));
		ripple = std::make_unique<Ui::RippleAnimation>(
			st.ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	ripple->add(point - st.rippleAreaPosition);
}

void ConfInviteRow::elementsStopLastRipple() {
	if (_videoRipple) {
		_videoRipple->lastStop();
	}
	if (_audioRipple) {
		_audioRipple->lastStop();
	}
}

void ConfInviteRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	if (_alreadyIn) {
		return;
	}
	const auto paintElement = [&](int element) {
		const auto &st = buttonSt(element);
		auto &ripple = (element == 1) ? _videoRipple : _audioRipple;
		const auto active = checked() && ((element == 1) ? _video : !_video);
		const auto geometry = elementGeometry(element, outerWidth);
		if (ripple) {
			ripple->paint(
				p,
				geometry.x() + st.rippleAreaPosition.x(),
				geometry.y() + st.rippleAreaPosition.y(),
				outerWidth);
			if (ripple->empty()) {
				ripple.reset();
			}
		}
		const auto selected = (element == selectedElement);
		const auto &icon = active
			? (element == 1
				? (_st.videoActive
					? *_st.videoActive
					: st::createCallVideoActive)
				: (_st.audioActive
					? *_st.audioActive
					: st::createCallAudioActive))
			: (selected ? st.iconOver : st.icon);
		icon.paintInCenter(p, geometry);
	};
	paintElement(1);
	paintElement(2);
}

[[nodiscard]] PrioritizedSelector PrioritizedInviteSelector(
		const ConfInviteStyles &st,
		std::vector<not_null<UserData*>> users,
		Fn<bool(not_null<PeerListRow*>, bool, anim::type)> toggleGetChecked,
		Fn<bool()> lastSelectWithVideo,
		Fn<void(bool)> setLastSelectWithVideo) {
	class PrioritizedController final : public PeerListController {
	public:
		PrioritizedController(
			const ConfInviteStyles &st,
			std::vector<not_null<UserData*>> users,
			Fn<bool(
				not_null<PeerListRow*>,
				bool,
				anim::type)> toggleGetChecked,
			Fn<bool()> lastSelectWithVideo,
			Fn<void(bool)> setLastSelectWithVideo)
		: _st(st)
		, _users(std::move(users))
		, _toggleGetChecked(std::move(toggleGetChecked))
		, _lastSelectWithVideo(std::move(lastSelectWithVideo))
		, _setLastSelectWithVideo(std::move(setLastSelectWithVideo)) {
			Expects(!_users.empty());
		}

		void prepare() override {
			for (const auto user : _users) {
				delegate()->peerListAppendRow(
					std::make_unique<ConfInviteRow>(user, _st));
			}
			delegate()->peerListRefreshRows();
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			toggleRowSelected(row, _lastSelectWithVideo());
		}
		void rowElementClicked(
				not_null<PeerListRow*> row,
				int element) override {
			if (row->checked()) {
				static_cast<ConfInviteRow*>(row.get())->setVideo(
					element == 1);
				_setLastSelectWithVideo(element == 1);
			} else if (element == 1) {
				toggleRowSelected(row, true);
			} else if (element == 2) {
				toggleRowSelected(row, false);
			}
		}

		void toggleRowSelected(not_null<PeerListRow*> row, bool video) {
			delegate()->peerListSetRowChecked(
				row,
				_toggleGetChecked(row, video, anim::type::normal));
		}

		Main::Session &session() const override {
			return _users.front()->session();
		}

		void toggleFirst() {
			rowClicked(delegate()->peerListRowAt(0));
		}

	private:
		const ConfInviteStyles &_st;
		std::vector<not_null<UserData*>> _users;
		Fn<bool(not_null<PeerListRow*>, bool, anim::type)> _toggleGetChecked;
		Fn<bool()> _lastSelectWithVideo;
		Fn<void(bool)> _setLastSelectWithVideo;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime(
	).make_state<PrioritizedController>(
		st,
		users,
		toggleGetChecked,
		lastSelectWithVideo,
		setLastSelectWithVideo);
	controller->setStyleOverrides(&st::createCallList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	const auto activate = [=] {
		content->submitted();
	};
	content->noSearchSubmits() | rpl::start_with_next([=] {
		controller->toggleFirst();
	}, content->lifetime());

	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddDivider(container);

	const auto overrideKey = [=](int direction, int from, int to) {
		if (!content->isVisible()) {
			return false;
		} else if (direction > 0 && from < 0 && to >= 0) {
			if (content->hasSelection()) {
				const auto was = content->selectedIndex();
				const auto now = content->selectSkip(1).reallyMovedTo;
				if (was != now) {
					return true;
				}
				content->clearSelection();
			} else {
				content->selectSkip(1);
				return true;
			}
		} else if (direction < 0 && to < 0) {
			if (!content->hasSelection()) {
				content->selectLast();
			} else if (from >= 0 || content->hasSelection()) {
				content->selectSkip(-1);
			}
		}
		return false;
	};

	const auto deselect = [=](PeerListRowId rowId) {
		if (const auto row = delegate->peerListFindRow(rowId)) {
			delegate->peerListSetRowChecked(row, false);
		}
	};

	const auto init = [=] {
		for (const auto &user : users) {
			if (const auto row = delegate->peerListFindRow(user->id.value)) {
				delegate->peerListSetRowChecked(
					row,
					toggleGetChecked(row, false, anim::type::instant));
			}
		}
	};

	return {
		.content = std::move(result),
		.init = init,
		.overrideKey = overrideKey,
		.deselect = deselect,
		.activate = activate,
		.scrollToRequests = content->scrollToRequests(),
	};
}

ConfInviteController::ConfInviteController(
	not_null<Main::Session*> session,
	ConfInviteStyles st,
	base::flat_set<not_null<UserData*>> alreadyIn,
	Fn<void()> shareLink,
	std::vector<not_null<UserData*>> prioritize)
: ContactsBoxController(session)
, _st(st)
, _alreadyIn(std::move(alreadyIn))
, _prioritize(std::move(prioritize))
, _shareLink(std::move(shareLink)) {
	if (!_shareLink) {
		_skip.reserve(_prioritize.size());
		for (const auto user : _prioritize) {
			_skip.emplace(user);
		}
	}
}

rpl::producer<bool> ConfInviteController::hasSelectedValue() const {
	return _hasSelected.value();
}

std::vector<InviteRequest> ConfInviteController::requests(
		const std::vector<not_null<PeerData*>> &peers) const {
	auto result = std::vector<InviteRequest>();
	result.reserve(peers.size());
	for (const auto &peer : peers) {
		if (const auto user = peer->asUser()) {
			result.push_back({ user, _withVideo.contains(user) });
		}
	}
	return result;
}

std::unique_ptr<PeerListRow> ConfInviteController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()
		|| _skip.contains(user)) {
		return nullptr;
	}
	auto result = std::make_unique<ConfInviteRow>(user, _st);
	if (_alreadyIn.contains(user)) {
		result->setAlreadyIn(true);
	}
	if (_withVideo.contains(user)) {
		result->setVideo(true);
	}
	return result;
}

int ConfInviteController::fullCount() const {
	return _alreadyIn.size()
		+ delegate()->peerListSelectedRowsCount()
		+ (_alreadyIn.contains(session().user()) ? 1 : 0);
}

void ConfInviteController::rowClicked(not_null<PeerListRow*> row) {
	toggleRowSelected(row, _lastSelectWithVideo);
}

void ConfInviteController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	if (row->checked()) {
		static_cast<ConfInviteRow*>(row.get())->setVideo(element == 1);
		_lastSelectWithVideo = (element == 1);
	} else if (element == 1) {
		toggleRowSelected(row, true);
	} else if (element == 2) {
		toggleRowSelected(row, false);
	}
}

bool ConfInviteController::handleDeselectForeignRow(PeerListRowId itemId) {
	if (_prioritizeRows.deselect) {
		const auto userId = peerToUser(PeerId(itemId));
		if (ranges::contains(_prioritize, session().data().user(userId))) {
			_prioritizeRows.deselect(itemId);
			return true;
		}
	}
	return false;
}

bool ConfInviteController::overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) {
	return _prioritizeRows.overrideKey
		&& _prioritizeRows.overrideKey(direction, fromIndex, toIndex);
}

void ConfInviteController::toggleRowSelected(
		not_null<PeerListRow*> row,
		bool video) {
	delegate()->peerListSetRowChecked(row, toggleRowGetChecked(row, video));

	// row may have been destroyed here, from search.
	_hasSelected = (delegate()->peerListSelectedRowsCount() > 0);
}

bool ConfInviteController::toggleRowGetChecked(
		not_null<PeerListRow*> row,
		bool video) {
	auto count = fullCount();
	const auto conferenceLimit = session().appConfig().confcallSizeLimit();
	if (!row->checked() && count >= conferenceLimit) {
		delegate()->peerListUiShow()->showToast(
			tr::lng_group_call_invite_limit(tr::now));
		return false;
	}
	const auto real = static_cast<ConfInviteRow*>(row.get());
	if (!row->checked()) {
		real->setVideo(video);
		_lastSelectWithVideo = video;
	}
	const auto user = row->peer()->asUser();
	if (!row->checked() && video) {
		_withVideo.emplace(user);
	} else {
		_withVideo.remove(user);
	}
	return !row->checked();
}

void ConfInviteController::noSearchSubmit() {
	if (const auto onstack = _prioritizeRows.activate) {
		onstack();
	} else if (delegate()->peerListFullRowsCount() > 0) {
		rowClicked(delegate()->peerListRowAt(0));
	}
}

auto ConfInviteController::prioritizeScrollRequests() const
-> rpl::producer<Ui::ScrollToRequest> {
	return _prioritizeScrollRequests.events();
}

void ConfInviteController::prepareViewHook() {
	if (_shareLink) {
		addShareLinkButton();
	} else if (!_prioritize.empty()) {
		addPriorityInvites();
	}
}

void ConfInviteController::addPriorityInvites() {
	const auto toggleGetChecked = [=](
			not_null<PeerListRow*> row,
			bool video,
			anim::type animated) {
		const auto result = toggleRowGetChecked(row, video);
		delegate()->peerListSetForeignRowChecked(
			row,
			result,
			animated);

		_hasSelected = (delegate()->peerListSelectedRowsCount() > 0);

		return result;
	};
	_prioritizeRows = PrioritizedInviteSelector(
		_st,
		_prioritize,
		toggleGetChecked,
		[=] { return _lastSelectWithVideo; },
		[=](bool video) { _lastSelectWithVideo = video; });
	if (auto &scrollTo = _prioritizeRows.scrollToRequests) {
		std::move(
			scrollTo
		) | rpl::start_to_stream(_prioritizeScrollRequests, lifetime());
	}
	if (const auto onstack = _prioritizeRows.init) {
		onstack();

		// Force finishing in instant adding checked rows bunch.
		delegate()->peerListAddSelectedPeers(
			std::vector<not_null<PeerData*>>());
	}
	delegate()->peerListSetAboveWidget(std::move(_prioritizeRows.content));
}

void ConfInviteController::addShareLinkButton() {
	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		nullptr,
		object_ptr<Ui::SettingsButton>(
			nullptr,
			tr::lng_profile_add_via_link(),
			(_st.inviteViaLink
				? *_st.inviteViaLink
				: st::createCallInviteLink)),
		style::margins(0, st::membersMarginTop, 0, 0));

	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		button->entity(),
		(_st.inviteViaLinkIcon
			? *_st.inviteViaLinkIcon
			: st::createCallInviteLinkIcon),
		QPoint());
	button->entity()->heightValue(
	) | rpl::start_with_next([=](int height) {
		icon->moveToLeft(
			st::createCallInviteLinkIconPosition.x(),
			(height - st::groupCallInviteLinkIcon.height()) / 2);
	}, icon->lifetime());

	button->entity()->setClickedCallback(_shareLink);
	button->entity()->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		delegate()->peerListMouseLeftGeometry();
	}, button->lifetime());
	delegate()->peerListSetAboveWidget(std::move(button));
}

} // namespace

InviteController::InviteController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn)
: ParticipantsBoxController(CreateTag{}, nullptr, peer, Role::Members)
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void InviteController::prepare() {
	delegate()->peerListSetHideEmpty(true);
	ParticipantsBoxController::prepare();
	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
}

void InviteController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

base::unique_qptr<Ui::PopupMenu> InviteController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

void InviteController::itemDeselectedHook(not_null<PeerData*> peer) {
}

bool InviteController::hasRowFor(not_null<PeerData*> peer) const {
	return (delegate()->peerListFindRow(peer->id.value) != nullptr);
}

bool InviteController::isAlreadyIn(not_null<UserData*> user) const {
	return _alreadyIn.contains(user);
}

std::unique_ptr<PeerListRow> InviteController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user
		|| user->isSelf()
		|| user->isBot()
		|| user->isInaccessible()) {
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	_rowAdded.fire_copy(user);
	_inGroup.emplace(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

auto InviteController::peersWithRows() const
-> not_null<const base::flat_set<not_null<UserData*>>*> {
	return &_inGroup;
}

rpl::producer<not_null<UserData*>> InviteController::rowAdded() const {
	return _rowAdded.events();
}

InviteContactsController::InviteContactsController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn,
	not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
	rpl::producer<not_null<UserData*>> discoveredInGroup)
: AddParticipantsBoxController(peer, std::move(alreadyIn))
, _inGroup(inGroup)
, _discoveredInGroup(std::move(discoveredInGroup)) {
}

void InviteContactsController::prepareViewHook() {
	AddParticipantsBoxController::prepareViewHook();

	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_contacts_header()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_search_results()));

	std::move(
		_discoveredInGroup
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
		}
	}, _lifetime);
}

std::unique_ptr<PeerListRow> InviteContactsController::createRow(
		not_null<UserData*> user) {
	return _inGroup->contains(user)
		? nullptr
		: AddParticipantsBoxController::createRow(user);
}

object_ptr<Ui::BoxContent> PrepareInviteBox(
		not_null<GroupCall*> call,
		Fn<void(TextWithEntities&&)> showToast,
		Fn<void()> shareConferenceLink) {
	const auto real = call->lookupReal();
	if (!real) {
		return nullptr;
	}
	const auto peer = call->peer();
	const auto conference = call->conference();
	const auto weak = base::make_weak(call);
	const auto &invited = peer->owner().invitedToCallUsers(real->id());
	auto alreadyIn = base::flat_set<not_null<UserData*>>();
	alreadyIn.reserve(invited.size() + real->participants().size() + 1);
	alreadyIn.emplace(peer->session().user());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	for (const auto &[user, calling] : invited) {
		if (!conference || calling) {
			alreadyIn.emplace(user);
		}
	}
	if (conference) {
		const auto close = std::make_shared<Fn<void()>>();
		const auto shareLink = [=] {
			Expects(shareConferenceLink != nullptr);

			shareConferenceLink();
			(*close)();
		};
		auto controller = std::make_unique<ConfInviteController>(
			&real->session(),
			ConfInviteDarkStyles(),
			alreadyIn,
			shareLink,
			std::vector<not_null<UserData*>>());
		const auto raw = controller.get();
		raw->setStyleOverrides(
			&st::groupCallInviteMembersList,
			&st::groupCallMultiSelect);
		auto initBox = [=](not_null<PeerListBox*> box) {
			box->setTitle(tr::lng_group_call_invite_conf());
			raw->hasSelectedValue() | rpl::start_with_next([=](bool has) {
				box->clearButtons();
				if (has) {
					box->addButton(tr::lng_group_call_confcall_add(), [=] {
						const auto call = weak.get();
						if (!call) {
							return;
						}
						const auto done = [=](InviteResult result) {
							(*close)();
							showToast({ ComposeInviteResultToast(result) });
						};
						call->inviteUsers(
							raw->requests(box->collectSelectedRows()),
							done);
					});
				}
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}, box->lifetime());
			*close = crl::guard(box, [=] { box->closeBox(); });
		};
		return Box<PeerListBox>(std::move(controller), initBox);
	}

	auto controller = std::make_unique<InviteController>(peer, alreadyIn);
	controller->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	auto contactsController = std::make_unique<InviteContactsController>(
		peer,
		std::move(alreadyIn),
		controller->peersWithRows(),
		controller->rowAdded());
	contactsController->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	const auto invite = [=](const std::vector<not_null<UserData*>> &users) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		auto requests = ranges::views::all(
			users
		) | ranges::views::transform([](not_null<UserData*> user) {
			return InviteRequest{ user };
		}) | ranges::to_vector;
		call->inviteUsers(std::move(requests), [=](InviteResult result) {
			if (result.invited.size() == 1) {
				showToast(tr::lng_group_call_invite_done_user(
					tr::now,
					lt_user,
					Ui::Text::Bold(result.invited.front()->firstName),
					Ui::Text::WithEntities));
			} else if (result.invited.size() > 1) {
				showToast(tr::lng_group_call_invite_done_many(
					tr::now,
					lt_count,
					result.invited.size(),
					Ui::Text::RichLangValue));
			}
		});
	};
	const auto inviteWithAdd = [=](
			std::shared_ptr<Ui::Show> show,
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		peer->session().api().chatParticipants().add(
			show,
			peer,
			nonMembers,
			true,
			[=](bool) { invite(users); finish(); });
	};
	const auto inviteWithConfirmation = [=](
			not_null<PeerListsBox*> parentBox,
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		if (nonMembers.empty()) {
			invite(users);
			finish();
			return;
		}
		const auto name = peer->name();
		const auto text = (nonMembers.size() == 1)
			? tr::lng_group_call_add_to_group_one(
				tr::now,
				lt_user,
				nonMembers.front()->shortName(),
				lt_group,
				name)
			: (nonMembers.size() < users.size())
			? tr::lng_group_call_add_to_group_some(tr::now, lt_group, name)
			: tr::lng_group_call_add_to_group_all(tr::now, lt_group, name);
		const auto shared = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
		const auto finishWithConfirm = [=] {
			if (*shared) {
				(*shared)->closeBox();
			}
			finish();
		};
		const auto done = [=] {
			const auto show = (*shared) ? (*shared)->uiShow() : nullptr;
			inviteWithAdd(show, users, nonMembers, finishWithConfirm);
		};
		auto box = ConfirmBox({
			.text = text,
			.confirmed = done,
			.confirmText = tr::lng_participant_invite(),
		});
		*shared = box.data();
		parentBox->getDelegate()->showBox(
			std::move(box),
			Ui::LayerOption::KeepOther,
			anim::type::normal);
	};
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListsBox*> box) {
		box->setTitle(tr::lng_group_call_invite_title());
		box->addButton(tr::lng_group_call_invite_button(), [=] {
			const auto rows = box->collectSelectedRows();

			const auto users = ranges::views::all(
				rows
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return not_null<UserData*>(peer->asUser());
			}) | ranges::to_vector;

			const auto nonMembers = ranges::views::all(
				users
			) | ranges::views::filter([&](not_null<UserData*> user) {
				return !controller->hasRowFor(user);
			}) | ranges::to_vector;

			const auto finish = [box = base::make_weak(box)]() {
				if (box) {
					box->closeBox();
				}
			};
			inviteWithConfirmation(box, users, nonMembers, finish);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	auto controllers = std::vector<std::unique_ptr<PeerListController>>();
	controllers.push_back(std::move(controller));
	controllers.push_back(std::move(contactsController));
	return Box<PeerListsBox>(std::move(controllers), initBox);
}

object_ptr<Ui::BoxContent> PrepareInviteBox(
		not_null<Call*> call,
		Fn<void(std::vector<InviteRequest>)> inviteUsers,
		Fn<void()> shareLink) {
	const auto user = call->user();
	const auto weak = base::make_weak(call);
	auto alreadyIn = base::flat_set<not_null<UserData*>>{ user };
	auto controller = std::make_unique<ConfInviteController>(
		&user->session(),
		ConfInviteDarkStyles(),
		alreadyIn,
		shareLink,
		std::vector<not_null<UserData*>>());
	const auto raw = controller.get();
	raw->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_group_call_invite_conf());
		raw->hasSelectedValue() | rpl::start_with_next([=](bool has) {
			box->clearButtons();
			if (has) {
				box->addButton(tr::lng_group_call_invite_button(), [=] {
					const auto call = weak.get();
					if (!call) {
						return;
					}
					inviteUsers(raw->requests(box->collectSelectedRows()));
				});
			}
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), initBox);
}

not_null<Ui::RpWidget*> CreateReActivateHeader(not_null<QWidget*> parent) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(parent);
	result->add(
		MakeJoinCallLogo(result),
		st::boxRowPadding + st::confcallLinkHeaderIconPadding);

	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			tr::lng_confcall_inactive_title(),
			st::boxTitle),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			tr::lng_confcall_inactive_about(),
			st::confcallLinkCenteredText),
		st::boxRowPadding + st::confcallLinkTitlePadding,
		style::al_top
	)->setTryMakeSimilarLines(true);
	Ui::AddDivider(result);

	return result;
}

void InitReActivate(not_null<PeerListBox*> box) {
	box->setTitle(rpl::producer<TextWithEntities>(nullptr));
	box->setNoContentMargin(true);

	const auto header = CreateReActivateHeader(box);
	header->resizeToWidth(st::boxWideWidth);
	header->heightValue() | rpl::start_with_next([=](int height) {
		box->setAddedTopScrollSkip(height, true);
	}, header->lifetime());
	header->moveToLeft(0, 0);
}

object_ptr<Ui::BoxContent> PrepareInviteToEmptyBox(
		std::shared_ptr<Data::GroupCall> call,
		MsgId inviteMsgId,
		std::vector<not_null<UserData*>> prioritize) {
	auto controller = std::make_unique<ConfInviteController>(
		&call->session(),
		ConfInviteDefaultStyles(),
		base::flat_set<not_null<UserData*>>(),
		nullptr,
		std::move(prioritize));
	const auto raw = controller.get();
	raw->setStyleOverrides(&st::createCallList);
	const auto initBox = [=](not_null<PeerListBox*> box) {
		InitReActivate(box);

		box->noSearchSubmits() | rpl::start_with_next([=] {
			raw->noSearchSubmit();
		}, box->lifetime());

		raw->prioritizeScrollRequests(
		) | rpl::start_with_next([=](Ui::ScrollToRequest request) {
			box->scrollTo(request);
		}, box->lifetime());

		const auto join = [=] {
			const auto weak = base::make_weak(box);
			auto selected = raw->requests(box->collectSelectedRows());
			Core::App().calls().startOrJoinConferenceCall({
				.call = call,
				.joinMessageId = inviteMsgId,
				.invite = std::move(selected),
			});
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		box->addButton(
			rpl::conditional(
				raw->hasSelectedValue(),
				tr::lng_group_call_confcall_add(),
				tr::lng_create_group_create()),
			join);
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	};
	return Box<PeerListBox>(std::move(controller), initBox);
}

object_ptr<Ui::BoxContent> PrepareCreateCallBox(
		not_null<::Window::SessionController*> window,
		Fn<void()> created,
		MsgId discardedInviteMsgId,
		std::vector<not_null<UserData*>> prioritize) {
	struct State {
		bool creatingLink = false;
		QPointer<PeerListBox> box;
	};
	const auto state = std::make_shared<State>();
	const auto finished = [=](bool ok) {
		if (!ok) {
			state->creatingLink = false;
		} else {
			if (const auto strong = state->box.data()) {
				strong->closeBox();
			}
			if (const auto onstack = created) {
				onstack();
			}
		}
	};
	const auto shareLink = [=] {
		if (state->creatingLink) {
			return;
		}
		state->creatingLink = true;
		MakeConferenceCall({
			.show = window->uiShow(),
			.finished = finished,
		});
	};
	auto controller = std::make_unique<ConfInviteController>(
		&window->session(),
		ConfInviteDefaultStyles(),
		base::flat_set<not_null<UserData*>>(),
		discardedInviteMsgId ? Fn<void()>() : shareLink,
		std::move(prioritize));
	const auto raw = controller.get();
	if (discardedInviteMsgId) {
		raw->setStyleOverrides(&st::createCallList);
	}
	const auto initBox = [=](not_null<PeerListBox*> box) {
		if (discardedInviteMsgId) {
			InitReActivate(box);
		} else {
			box->setTitle(tr::lng_confcall_create_title());
		}

		box->noSearchSubmits() | rpl::start_with_next([=] {
			raw->noSearchSubmit();
		}, box->lifetime());

		raw->prioritizeScrollRequests(
		) | rpl::start_with_next([=](Ui::ScrollToRequest request) {
			box->scrollTo(request);
		}, box->lifetime());

		const auto create = [=] {
			auto selected = raw->requests(box->collectSelectedRows());
			if (selected.size() != 1 || discardedInviteMsgId) {
				Core::App().calls().startOrJoinConferenceCall({
					.show = window->uiShow(),
					.invite = std::move(selected),
				});
			} else {
				const auto &invite = selected.front();
				Core::App().calls().startOutgoingCall(
					invite.user,
					invite.video);
			}
			finished(true);
		};
		box->addButton(
			rpl::conditional(
				raw->hasSelectedValue(),
				tr::lng_group_call_confcall_add(),
				tr::lng_create_group_create()),
			create);
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	};
	auto result = Box<PeerListBox>(std::move(controller), initBox);
	state->box = result.data();
	return result;
}

} // namespace Calls::Group
