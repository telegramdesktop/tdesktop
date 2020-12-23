/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/polls/info_polls_results_inner_widget.h"

#include "info/polls/info_polls_results_widget.h"
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
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info {
namespace Polls {
namespace {

constexpr auto kFirstPage = 15;
constexpr auto kPerPage = 50;
constexpr auto kLeavePreloaded = 5;

class PeerListDummy final : public Ui::RpWidget {
public:
	PeerListDummy(QWidget *parent, int count, const style::PeerList &st);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::PeerList &_st;
	int _count = 0;

	std::vector<Ui::Animations::Simple> _animations;

};

class ListDelegate final : public PeerListContentDelegate {
public:
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;

};

PeerListDummy::PeerListDummy(
	QWidget *parent,
	int count,
	const style::PeerList &st)
: _st(st)
, _count(count) {
	resize(width(), _count * _st.item.height);
}

void PeerListDummy::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	PainterHighQualityEnabler hq(p);

	const auto fill = e->rect();
	const auto bottom = fill.top() + fill.height();
	const auto from = floorclamp(fill.top(), _st.item.height, 0, _count);
	const auto till = ceilclamp(bottom, _st.item.height, 0, _count);
	p.translate(0, _st.item.height * from);
	p.setPen(Qt::NoPen);
	for (auto i = from; i != till; ++i) {
		p.setBrush(st::windowBgOver);
		p.drawEllipse(
			_st.item.photoPosition.x(),
			_st.item.photoPosition.y(),
			_st.item.photoSize,
			_st.item.photoSize);

		const auto small = int(1.5 * _st.item.photoSize);
		const auto large = 2 * small;
		const auto second = (i % 2) ? large : small;
		const auto height = _st.item.nameStyle.font->height / 2;
		const auto radius = height / 2;
		const auto left = _st.item.namePosition.x();
		const auto top = _st.item.namePosition.y()
			+ (_st.item.nameStyle.font->height - height) / 2;
		const auto skip = _st.item.namePosition.x()
			- _st.item.photoPosition.x()
			- _st.item.photoSize;
		const auto next = left + small + skip;
		p.drawRoundedRect(left, top, small, height, radius, radius);
		p.drawRoundedRect(next, top, second, height, radius, radius);

		p.translate(0, _st.item.height);
	}
}

void ListDelegate::peerListSetTitle(rpl::producer<QString> title) {
}

void ListDelegate::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool ListDelegate::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int ListDelegate::peerListSelectedRowsCount() {
	return 0;
}

void ListDelegate::peerListScrollToTop() {
}

void ListDelegate::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void ListDelegate::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void ListDelegate::peerListFinishSelectedRowsBunch() {
}

void ListDelegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace

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

	void allowLoadMore();
	void collapse();

	[[nodiscard]] auto showPeerInfoRequests() const
		-> rpl::producer<not_null<PeerData*>>;
	[[nodiscard]] rpl::producer<int> scrollToRequests() const;
	[[nodiscard]] rpl::producer<int> count() const;
	[[nodiscard]] rpl::producer<int> fullCount() const;
	[[nodiscard]] rpl::producer<int> loadMoreCount() const;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	void scrollTo(int y);

private:
	struct SavedState : SavedStateBase {
		QString offset;
		QString loadForOffset;
		int leftToLoad = 0;
		int fullCount = 0;
		std::vector<not_null<UserData*>> preloaded;
		bool wasLoading = false;
	};

	bool appendRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;
	void addPreloaded();
	bool addPreloadedPage();
	void preloadedAdded();

	const not_null<Main::Session*> _session;
	const not_null<PollData*> _poll;
	const FullMsgId _context;
	const QByteArray _option;

	MTP::Sender _api;

	QString _offset;
	mtpRequestId _loadRequestId = 0;
	QString _loadForOffset;
	std::vector<not_null<UserData*>> _preloaded;
	rpl::variable<int> _count = 0;
	rpl::variable<int> _fullCount;
	rpl::variable<int> _leftToLoad;

	rpl::event_stream<not_null<PeerData*>> _showPeerInfoRequests;
	rpl::event_stream<int> _scrollToRequests;

};

ListController::ListController(
	not_null<Main::Session*> session,
	not_null<PollData*> poll,
	FullMsgId context,
	QByteArray option)
: _session(session)
, _poll(poll)
, _context(context)
, _option(option)
, _api(&_session->mtp()) {
	const auto i = ranges::find(poll->answers, option, &PollAnswer::option);
	Assert(i != poll->answers.end());
	_fullCount = i->votes;
	_leftToLoad = i->votes;
}

Main::Session &ListController::session() const {
	return *_session;
}

void ListController::prepare() {
	delegate()->peerListRefreshRows();
}

void ListController::loadMoreRows() {
	if (_loadRequestId
		|| !_leftToLoad.current()
		|| (!_offset.isEmpty() && _loadForOffset != _offset)
		|| !_preloaded.empty()) {
		return;
	}
	const auto item = session().data().message(_context);
	if (!item || !IsServerMsgId(item->id)) {
		_leftToLoad = 0;
		return;
	}

	using Flag = MTPmessages_GetPollVotes::Flag;
	const auto flags = Flag::f_option
		| (_offset.isEmpty() ? Flag(0) : Flag::f_offset);
	const auto limit = _offset.isEmpty() ? kFirstPage : kPerPage;
	_loadRequestId = _api.request(MTPmessages_GetPollVotes(
		MTP_flags(flags),
		item->history()->peer->input,
		MTP_int(item->id),
		MTP_bytes(_option),
		MTP_string(_offset),
		MTP_int(limit)
	)).done([=](const MTPmessages_VotesList &result) {
		const auto count = result.match([&](
				const MTPDmessages_votesList &data) {
			_offset = data.vnext_offset().value_or_empty();
			auto &owner = session().data();
			owner.processUsers(data.vusers());
			auto add = limit - kLeavePreloaded;
			for (const auto &vote : data.vvotes().v) {
				vote.match([&](const auto &data) {
					const auto user = owner.user(data.vuser_id().v);
					if (user->isMinimalLoaded()) {
						if (add) {
							appendRow(user);
							--add;
						} else {
							_preloaded.push_back(user);
						}
					}
				});
			}
			return data.vcount().v;
		});
		if (_offset.isEmpty()) {
			addPreloaded();
			_fullCount = delegate()->peerListFullRowsCount();
			_leftToLoad = 0;
		} else {
			_count = delegate()->peerListFullRowsCount();
			_fullCount = count;
			_leftToLoad = count - delegate()->peerListFullRowsCount();
			delegate()->peerListRefreshRows();
		}
		_loadRequestId = 0;
	}).fail([=](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ListController::allowLoadMore() {
	if (!addPreloadedPage()) {
		_loadForOffset = _offset;
		addPreloaded();
		loadMoreRows();
	}
}

void ListController::collapse() {
	const auto count = delegate()->peerListFullRowsCount();
	if (count <= kFirstPage) {
		return;
	}
	const auto remove = count - (kFirstPage - kLeavePreloaded);
	ranges::action::reverse(_preloaded);
	_preloaded.reserve(_preloaded.size() + remove);
	for (auto i = 0; i != remove; ++i) {
		const auto row = delegate()->peerListRowAt(count - i - 1);
		_preloaded.push_back(row->peer()->asUser());
		delegate()->peerListRemoveRow(row);
	}
	ranges::action::reverse(_preloaded);

	delegate()->peerListRefreshRows();
	const auto now = count - remove;
	_count = now;
	_leftToLoad = _fullCount.current() - now;
}

void ListController::addPreloaded() {
	for (const auto user : base::take(_preloaded)) {
		appendRow(user);
	}
	preloadedAdded();
}

bool ListController::addPreloadedPage() {
	if (_preloaded.size() < kPerPage + kLeavePreloaded) {
		return false;
	}
	const auto from = begin(_preloaded);
	const auto till = from + kPerPage;
	for (auto i = from; i != till; ++i) {
		appendRow(*i);
	}
	_preloaded.erase(from, till);
	preloadedAdded();
	return true;
}

void ListController::preloadedAdded() {
	_count = delegate()->peerListFullRowsCount();
	_leftToLoad = _fullCount.current() - _count.current();
	delegate()->peerListRefreshRows();
}

auto ListController::showPeerInfoRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return _showPeerInfoRequests.events();
}

rpl::producer<int> ListController::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<int> ListController::count() const {
	return _count.value();
}

rpl::producer<int> ListController::fullCount() const {
	return _fullCount.value();
}

rpl::producer<int> ListController::loadMoreCount() const {
	const auto initial = (_fullCount.current() <= kFirstPage)
		? _fullCount.current()
		: (kFirstPage - kLeavePreloaded);
	return rpl::combine(
		_count.value(),
		_leftToLoad.value()
	) | rpl::map([=](int count, int leftToLoad) {
		return (count > 0) ? leftToLoad : (leftToLoad - initial);
	});
}

auto ListController::saveState() const -> std::unique_ptr<PeerListState> {
	auto result = PeerListController::saveState();

	auto my = std::make_unique<SavedState>();
	my->offset = _offset;
	my->fullCount = _fullCount.current();
	my->leftToLoad = _leftToLoad.current();
	my->preloaded = _preloaded;
	my->wasLoading = (_loadRequestId != 0);
	my->loadForOffset = _loadForOffset;
	result->controllerState = std::move(my);

	return result;
}

void ListController::restoreState(std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (const auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (const auto requestId = base::take(_loadRequestId)) {
			_api.request(requestId).cancel();
		}

		_offset = my->offset;
		_loadForOffset = my->loadForOffset;
		_preloaded = std::move(my->preloaded);
		_count = int(state->list.size());
		_fullCount = my->fullCount;
		_leftToLoad = my->leftToLoad;
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
	}
}

std::unique_ptr<PeerListRow> ListController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
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

void ListController::scrollTo(int y) {
	_scrollToRequests.fire_copy(y);
}

ListController *CreateAnswerRows(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<int> visibleTop,
		not_null<Main::Session*> session,
		not_null<PollData*> poll,
		FullMsgId context,
		const PollAnswer &answer) {
	using namespace rpl::mappers;

	if (!answer.votes) {
		return nullptr;
	}

	const auto delegate = container->lifetime().make_state<ListDelegate>();
	const auto controller = container->lifetime().make_state<ListController>(
		session,
		poll,
		context,
		answer.option);

	const auto percent = answer.votes * 100 / poll->totalVoters;
	const auto phrase = poll->quiz()
		? tr::lng_polls_answers_count
		: tr::lng_polls_votes_count;
	const auto sampleText = phrase(
			tr::now,
			lt_count_decimal,
			answer.votes);
	const auto &font = st::boxDividerLabel.style.font;
	const auto sampleWidth = font->width(sampleText);
	const auto rightSkip = sampleWidth + font->spacew * 4;
	const auto headerWrap = container->add(
		object_ptr<Ui::RpWidget>(
			container));

	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::boxLittleSkip));

	controller->setStyleOverrides(&st::infoCommonGroupsList);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	const auto count = (answer.votes <= kFirstPage)
		? answer.votes
		: (kFirstPage - kLeavePreloaded);
	const auto placeholder = container->add(object_ptr<PeerListDummy>(
		container,
		count,
		st::infoCommonGroupsList));

	controller->count(
	) | rpl::filter(_1 > 0) | rpl::start_with_next([=] {
		delete placeholder;
	}, placeholder->lifetime());

	const auto header = Ui::CreateChild<Ui::DividerLabel>(
		container.get(),
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
			st::pollResultsHeaderPadding.bottom()));

	const auto votes = Ui::CreateChild<Ui::FlatLabel>(
		header,
		phrase(
			lt_count_decimal,
			controller->fullCount() | rpl::map(_1 + 0.)),
		st::pollResultsVotesCount);
	const auto collapse = Ui::CreateChild<Ui::LinkButton>(
		header,
		tr::lng_polls_votes_collapse(tr::now),
		st::defaultLinkButton);
	collapse->setClickedCallback([=] {
		controller->scrollTo(headerWrap->y());
		controller->collapse();
	});
	rpl::combine(
		controller->fullCount(),
		controller->count()
	) | rpl::start_with_next([=](int fullCount, int count) {
		const auto many = (fullCount > kFirstPage)
			&& (count > kFirstPage - kLeavePreloaded);
		collapse->setVisible(many);
		votes->setVisible(!many);
	}, collapse->lifetime());

	headerWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		header->resizeToWidth(width);
		votes->moveToRight(
			st::pollResultsHeaderPadding.right(),
			st::pollResultsHeaderPadding.top(),
			width);
		collapse->moveToRight(
			st::pollResultsHeaderPadding.right(),
			st::pollResultsHeaderPadding.top(),
			width);
	}, header->lifetime());

	header->heightValue(
	) | rpl::start_with_next([=](int height) {
		headerWrap->resize(headerWrap->width(), height);
	}, header->lifetime());

	auto moreTopWidget = object_ptr<Ui::RpWidget>(container);
	moreTopWidget->resize(0, 0);
	const auto moreTop = container->add(std::move(moreTopWidget));
	const auto more = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_polls_show_more(
					lt_count_decimal,
					controller->loadMoreCount() | rpl::map(_1 + 0.),
					Ui::Text::Upper),
				st::pollResultsShowMore)));
	more->entity()->setClickedCallback([=] {
		controller->allowLoadMore();
	});
	controller->loadMoreCount(
	) | rpl::map(_1 > 0) | rpl::start_with_next([=](bool visible) {
		more->toggle(visible, anim::type::instant);
	}, more->lifetime());

	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::boxLittleSkip));

	rpl::combine(
		std::move(visibleTop),
		headerWrap->geometryValue(),
		moreTop->topValue()
	) | rpl::filter([=](int, QRect headerRect, int moreTop) {
		return moreTop >= headerRect.y() + headerRect.height();
	}) | rpl::start_with_next([=](
			int visibleTop,
			QRect headerRect,
			int moreTop) {
		const auto skip = st::pollResultsHeaderPadding.top()
			- st::pollResultsHeaderPadding.bottom();
		const auto top = std::clamp(
			visibleTop - skip,
			headerRect.y(),
			moreTop - headerRect.height());
		header->move(0, top);
	}, header->lifetime());

	return controller;
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PollData*> poll,
	FullMsgId contextId)
: RpWidget(parent)
, _controller(controller)
, _poll(poll)
, _contextId(contextId)
, _content(this) {
	setupContent();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_content, visibleTop, visibleBottom);
	_visibleTop = visibleTop;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	auto states = base::flat_map<
		QByteArray,
		std::unique_ptr<PeerListState>>();
	for (const auto &[option, controller] : _sections) {
		states[option] = controller->saveState();
	}
	memento->setListStates(std::move(states));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	auto states = memento->listStates();
	for (const auto &[option, controller] : _sections) {
		const auto i = states.find(option);
		if (i != end(states)) {
			controller->restoreState(std::move(i->second));
		}
	}
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	//auto count = qMax(_user->commonChatsCount(), 1);
	//desired += qMax(count, _list->fullRowsCount())
	//	* st::infoCommonGroupsList.item.height;
	return qMax(height(), desired);
}

void InnerWidget::setupContent() {
	const auto quiz = _poll->quiz();
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
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
			_content,
			_visibleTop.value(),
			session,
			_poll,
			_contextId,
			answer);
		if (!controller) {
			continue;
		}
		controller->showPeerInfoRequests(
		) | rpl::start_to_stream(
			_showPeerInfoRequests,
			lifetime());
		controller->scrollToRequests(
		) | rpl::start_with_next([=](int y) {
			_scrollToRequests.fire({ y, -1 });
		}, lifetime());
		_sections.emplace(answer.option, controller);
	}

	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		_content->resizeToWidth(newWidth);
	}, _content->lifetime());

	_content->heightValue(
	) | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, _content->lifetime());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

auto InnerWidget::showPeerInfoRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return _showPeerInfoRequests.events();
}

} // namespace Polls
} // namespace Info

