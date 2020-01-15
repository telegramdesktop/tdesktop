/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/polls/info_polls_results_inner_widget.h"

#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "data/data_poll.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "boxes/peer_list_box.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "apiwrap.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info {
namespace Polls {
namespace {

constexpr auto kFirstPage = 10;
constexpr auto kPerPage = 100;

class ListDelegate final : public PeerListContentDelegate {
public:
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowSelected(not_null<PeerData*> peer) override;
	int peerListSelectedRowsCount() override;
	std::vector<not_null<PeerData*>> peerListCollectSelectedRows() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerData*> peer) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;

};

class ListController final : public PeerListController {
public:
	ListController(
		not_null<Main::Session*> session,
		not_null<PollData*> poll,
		FullMsgId context,
		QByteArray option);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void allowLoadAll();

	rpl::producer<not_null<PeerData*>> showPeerInfoRequests() const;

private:
	bool appendRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

	const not_null<Main::Session*> _session;
	const not_null<PollData*> _poll;
	const FullMsgId _context;
	const QByteArray _option;

	MTP::Sender _api;

	QString _offset;
	mtpRequestId _loadRequestId = 0;
	int _fullCount = 0;
	bool _allLoaded = false;
	bool _loadingAll = false;

	rpl::event_stream<not_null<PeerData*>> _showPeerInfoRequests;

};

void ListDelegate::peerListSetTitle(rpl::producer<QString> title) {
}

void ListDelegate::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool ListDelegate::peerListIsRowSelected(not_null<PeerData*> peer) {
	return false;
}

int ListDelegate::peerListSelectedRowsCount() {
	return 0;
}

auto ListDelegate::peerListCollectSelectedRows()
-> std::vector<not_null<PeerData*>> {
	return {};
}

void ListDelegate::peerListScrollToTop() {
}

void ListDelegate::peerListAddSelectedRowInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void ListDelegate::peerListFinishSelectedRowsBunch() {
}

void ListDelegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

ListController::ListController(
	not_null<Main::Session*> session,
	not_null<PollData*> poll,
	FullMsgId context,
	QByteArray option)
: _session(session)
, _poll(poll)
, _context(context)
, _option(option)
, _api(_session->api().instance()) {
}

Main::Session &ListController::session() const {
	return *_session;
}

void ListController::prepare() {
	delegate()->peerListRefreshRows();
}

void ListController::loadMoreRows() {
	if (_loadRequestId
		|| _allLoaded
		|| (!_loadingAll && !_offset.isEmpty())) {
		return;
	}
	const auto item = session().data().message(_context);
	if (!item || !IsServerMsgId(item->id)) {
		_allLoaded = true;
		return;
	}

	using Flag = MTPmessages_GetPollVotes::Flag;
	const auto flags = Flag::f_option
		| (_offset.isEmpty() ? Flag(0) : Flag::f_offset);
	_loadRequestId = _api.request(MTPmessages_GetPollVotes(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(item->id),
		MTP_bytes(_option),
		MTP_string(_offset),
		MTP_int(_offset.isEmpty() ? kFirstPage : kPerPage)
	)).done([=](const MTPmessages_VotesList &result) {
		_loadRequestId = 0;
		result.match([&](const MTPDmessages_votesList &data) {
			_fullCount = data.vcount().v;
			_offset = data.vnext_offset().value_or_empty();
			auto &owner = session().data();
			owner.processUsers(data.vusers());
			for (const auto &vote : data.vvotes().v) {
				vote.match([&](const auto &data) {
					const auto user = owner.user(data.vuser_id().v);
					if (user->loadedStatus != PeerData::NotLoaded) {
						appendRow(user);
					}
				});
			}
		});
		_allLoaded = _offset.isEmpty();
		delegate()->peerListRefreshRows();
	}).fail([=](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ListController::allowLoadAll() {
	_loadingAll = true;
	loadMoreRows();
}

auto ListController::showPeerInfoRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return _showPeerInfoRequests.events();
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_showPeerInfoRequests.fire(row->peer());
}

bool ListController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRow>(user);
	row->setCustomStatus(QString());
	return row;
}

ListController *CreateAnswerRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		not_null<PollData*> poll,
		FullMsgId context,
		const PollAnswer &answer) {
	if (!answer.votes) {
		return nullptr;
	}

	const auto percent = answer.votes * 100 / poll->totalVoters;
	const auto rightText = (poll->quiz()
		? tr::lng_polls_answers_count
		: tr::lng_polls_votes_count)(
			tr::now,
			lt_count_decimal,
			answer.votes);
	const auto &font = st::boxDividerLabel.style.font;
	const auto rightWidth = font->width(rightText);
	const auto rightSkip = rightWidth + font->spacew * 4;
	const auto header = container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				(answer.text
					+ QString::fromUtf8(" \xe2\x80\x94 ")
					+ QString::number(percent)
					+ "%"),
				st::boxDividerLabel),
			style::margins(
				st::pollResultsHeaderPadding.left(),
				st::pollResultsHeaderPadding.top(),
				st::pollResultsHeaderPadding.right() + rightSkip,
				st::pollResultsHeaderPadding.bottom())));
	const auto votes = Ui::CreateChild<Ui::FlatLabel>(
		header,
		rightText,
		st::pollResultsVotesCount);
	header->widthValue(
	) | rpl::start_with_next([=](int width) {
		votes->moveToRight(
			st::pollResultsHeaderPadding.right(),
			st::pollResultsHeaderPadding.top(),
			width);
	}, votes->lifetime());
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::boxLittleSkip));

	const auto delegate = container->lifetime().make_state<ListDelegate>();
	const auto controller = container->lifetime().make_state<ListController>(
		session,
		poll,
		context,
		answer.option);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller,
		st::infoCommonGroupsList));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto more = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_polls_show_more(
					lt_count_decimal,
					rpl::single(answer.votes + 0.),
					Ui::Text::Upper),
				st::pollResultsShowMore)));
	more->toggle(answer.votes > kFirstPage, anim::type::instant);
	more->entity()->setClickedCallback([=] {
		controller->allowLoadAll();
		more->hide(anim::type::instant);
	});

	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::boxLittleSkip));

	return controller;
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PollData*> poll,
	FullMsgId contextId)
: RpWidget(parent)
, _controller(controller)
, _poll(poll)
, _contextId(contextId)
, _content(setupContent(this)) {
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	//memento->setListState(_listController->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	//_listController->restoreState(memento->listState());
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	//auto count = qMax(_user->commonChatsCount(), 1);
	//desired += qMax(count, _list->fullRowsCount())
	//	* st::infoCommonGroupsList.item.height;
	return qMax(height(), desired);
}

object_ptr<Ui::VerticalLayout> InnerWidget::setupContent(
		RpWidget *parent) {
	auto result = object_ptr<Ui::VerticalLayout>(parent);

	const auto quiz = _poll->quiz();
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			_poll->question,
			st::pollResultsQuestion),
		style::margins{
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::boxMediumSkip });
	for (const auto &answer : _poll->answers) {
		const auto session = &_controller->parentController()->session();
		const auto controller = CreateAnswerRows(
			result,
			session,
			_poll,
			_contextId,
			answer);
		if (controller) {
			controller->showPeerInfoRequests(
			) | rpl::start_to_stream(
				_showPeerInfoRequests,
				lifetime());
		}
	}
	parent->widthValue(
	) | rpl::start_with_next([content = result.data()](int newWidth) {
		content->resizeToWidth(newWidth);
	}, result->lifetime());
	result->heightValue(
	) | rpl::start_with_next([=](int height) {
		parent->resize(parent->width(), height);
	}, result->lifetime());
	return result;
}

auto InnerWidget::showPeerInfoRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return _showPeerInfoRequests.events();
}

} // namespace Polls
} // namespace Info

