/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/poll_results_box.h"

#include "lang/lang_keys.h"
#include "data/data_poll.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/text/text_utilities.h"
#include "boxes/peer_list_box.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "apiwrap.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kFirstPage = 10;
constexpr auto kPerPage = 100;

class Delegate final : public PeerListContentDelegate {
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

class Controller final : public PeerListController {
public:
	Controller(
		not_null<Window::SessionController*> window,
		not_null<PollData*> poll,
		FullMsgId context,
		QByteArray option);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void allowLoadAll();

private:
	bool appendRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

	const not_null<Window::SessionController*> _window;
	const not_null<PollData*> _poll;
	const FullMsgId _context;
	const QByteArray _option;

	MTP::Sender _api;

	QString _offset;
	mtpRequestId _loadRequestId = 0;
	int _fullCount = 0;
	bool _allLoaded = false;
	bool _loadingAll = false;

};

void Delegate::peerListSetTitle(rpl::producer<QString> title) {
}

void Delegate::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool Delegate::peerListIsRowSelected(not_null<PeerData*> peer) {
	return false;
}

int Delegate::peerListSelectedRowsCount() {
	return 0;
}

std::vector<not_null<PeerData*>> Delegate::peerListCollectSelectedRows() {
	return {};
}

void Delegate::peerListScrollToTop() {
}

void Delegate::peerListAddSelectedRowInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void Delegate::peerListFinishSelectedRowsBunch() {
}

void Delegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

Controller::Controller(
	not_null<Window::SessionController*> window,
	not_null<PollData*> poll,
	FullMsgId context,
	QByteArray option)
: _window(window)
, _poll(poll)
, _context(context)
, _option(option)
, _api(_window->session().api().instance()) {
}

Main::Session &Controller::session() const {
	return _window->session();
}

void Controller::prepare() {
	delegate()->peerListRefreshRows();
}

void Controller::loadMoreRows() {
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

void Controller::allowLoadAll() {
	_loadingAll = true;
	loadMoreRows();
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_window->showPeerHistory(row->peer(), Window::SectionShow::Way::Forward);
}

bool Controller::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> Controller::createRow(
		not_null<UserData*> user) const {
	auto row = std::make_unique<PeerListRow>(user);
	row->setCustomStatus(QString());
	return row;
}

void CreateAnswerRows(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PollData*> poll,
		FullMsgId context,
		const PollAnswer &answer) {
	if (!answer.votes) {
		return;
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
	const auto header = box->addRow(
		object_ptr<Ui::DividerLabel>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				(answer.text.repeated(20)
					+ QString::fromUtf8(" \xe2\x80\x94 ")
					+ QString::number(percent)
					+ "%"),
				st::boxDividerLabel),
			style::margins(
				st::pollResultsHeaderPadding.left(),
				st::pollResultsHeaderPadding.top(),
				st::pollResultsHeaderPadding.right() + rightSkip,
				st::pollResultsHeaderPadding.bottom())),
		style::margins());
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
	box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::boxLittleSkip));

	const auto delegate = box->lifetime().make_state<Delegate>();
	const auto controller = box->lifetime().make_state<Controller>(
		window,
		poll,
		context,
		answer.option);
	const auto content = box->addRow(
		object_ptr<PeerListContent>(
			box,
			controller,
			st::infoCommonGroupsList),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto more = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			box,
			object_ptr<Ui::SettingsButton>(
				box,
				tr::lng_polls_show_more(
					lt_count_decimal,
					rpl::single(answer.votes + 0.),
					Ui::Text::Upper),
				st::infoMainButton)),
		style::margins());
	more->toggle(answer.votes > kFirstPage, anim::type::instant);
	more->entity()->setClickedCallback([=] {
		controller->allowLoadAll();
		more->hide(anim::type::instant);
	});

	box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::boxLittleSkip));
}

} // namespace

void PollResultsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PollData*> poll,
		FullMsgId context) {
	const auto quiz = poll->quiz();
	box->setWidth(st::boxWideWidth);
	box->setTitle(quiz
		? tr::lng_polls_quiz_results_title()
		: tr::lng_polls_poll_results_title());
	box->setAdditionalTitle((quiz
		? tr::lng_polls_answers_count
		: tr::lng_polls_votes_count)(
			lt_count_decimal,
			rpl::single(poll->totalVoters * 1.)));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			poll->question,
			st::pollResultsQuestion),
		style::margins{
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::boxMediumSkip });
	for (const auto &answer : poll->answers) {
		CreateAnswerRows(box, window, poll, context, answer);
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}
