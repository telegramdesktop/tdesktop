/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/similar_channels/info_similar_channels_widget.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/tooltip.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "settings/settings_premium.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"

namespace Info::SimilarChannels {
namespace {

class ListController final : public PeerListController {
public:
	ListController(
		not_null<Controller*> controller,
		not_null<ChannelData*> channel);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	std::unique_ptr<PeerListRow> createRestoredRow(
			not_null<PeerData*> peer) override {
		return createRow(peer);
	}

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

	void setContentWidget(not_null<Ui::RpWidget*> widget);
	[[nodiscard]] rpl::producer<int> unlockHeightValue() const;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);
	void setupUnlock();
	void rebuild();

	struct SavedState : SavedStateBase {
	};
	const not_null<Controller*> _controller;
	const not_null<ChannelData*> _channel;
	Ui::RpWidget *_content = nullptr;
	Ui::RpWidget *_unlock = nullptr;
	rpl::variable<int> _unlockHeight;

};

ListController::ListController(
	not_null<Controller*> controller,
	not_null<ChannelData*> channel)
: PeerListController()
, _controller(controller)
, _channel(channel) {
}

Main::Session &ListController::session() const {
	return _channel->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	if (const auto channel = peer->asChannel()) {
		if (const auto count = channel->membersCount(); count > 1) {
			result->setCustomStatus(
				tr::lng_chat_status_subscribers(
					tr::now,
					lt_count_decimal,
					count));
		}
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetTitle(tr::lng_similar_channels_title());

	const auto participants = &_channel->session().api().chatParticipants();

	Data::AmPremiumValue(
		&_channel->session()
	) | rpl::start_with_next([=] {
		participants->loadSimilarChannels(_channel);
		rebuild();
	}, lifetime());

	participants->similarLoaded(
	) | rpl::filter(
		rpl::mappers::_1 == _channel
	) | rpl::start_with_next([=] {
		rebuild();
	}, lifetime());
}

void ListController::setContentWidget(not_null<Ui::RpWidget*> widget) {
	_content = widget;
}

rpl::producer<int> ListController::unlockHeightValue() const {
	return _unlockHeight.value();
}

void ListController::rebuild() {
	const auto participants = &_channel->session().api().chatParticipants();
	const auto &list = participants->similar(_channel);
	for (const auto channel : list.list) {
		if (!delegate()->peerListFindRow(channel->id.value)) {
			delegate()->peerListAppendRow(createRow(channel));
		}
	}
	if (!list.more
		|| _channel->session().premium()
		|| !_channel->session().premiumPossible()) {
		delete base::take(_unlock);
		_unlockHeight = 0;
	} else if (!_unlock) {
		setupUnlock();
	}
	delegate()->peerListRefreshRows();
}

void ListController::setupUnlock() {
	Expects(_content != nullptr);

	_unlock = Ui::CreateChild<Ui::RpWidget>(_content);
	_unlock->show();

	const auto button = ::Settings::CreateLockedButton(
		_unlock,
		tr::lng_similar_channels_show_more(),
		st::similarChannelsLock,
		rpl::single(true));
	button->setClickedCallback([=] {
		const auto window = _controller->parentController();
		::Settings::ShowPremium(window, u"similar_channels"_q);
	});

	const auto upto = Data::PremiumLimits(
		&_channel->session()).similarChannelsPremium();
	const auto about = Ui::CreateChild<Ui::FlatLabel>(
		_unlock,
		tr::lng_similar_channels_premium_all(
			lt_count,
			rpl::single(upto * 1.),
			lt_link,
			tr::lng_similar_channels_premium_all_link(
			) | Ui::Text::ToBold() | Ui::Text::ToLink(),
			Ui::Text::RichLangValue),
		st::similarChannelsLockAbout);
	about->setClickHandlerFilter([=](const auto &...) {
		const auto window = _controller->parentController();
		::Settings::ShowPremium(window, u"similar_channels"_q);
		return false;
	});

	rpl::combine(
		_content->sizeValue(),
		tr::lng_similar_channels_show_more()
	) | rpl::start_with_next([=](QSize size, const auto &) {
		auto top = st::similarChannelsLockFade
			+ st::similarChannelsLockPadding.top();
		button->setGeometry(
			st::similarChannelsLockPadding.left(),
			top,
			(size.width()
				- st::similarChannelsLockPadding.left()
				- st::similarChannelsLockPadding.right()),
			button->height());
		top += button->height() + st::similarChannelsLockPadding.bottom();

		const auto minWidth = st::similarChannelsLockAbout.minWidth;
		const auto maxWidth = std::max(
			minWidth + 1,
			(size.width()
				- st::similarChannelsLockAboutPadding.left()
				- st::similarChannelsLockAboutPadding.right()));
		const auto countAboutHeight = [&](int width) {
			about->resizeToWidth(width);
			return about->height();
		};
		const auto desired = Ui::FindNiceTooltipWidth(
			minWidth,
			maxWidth,
			countAboutHeight);
		about->resizeToWidth(desired);
		about->move((size.width() - about->width()) / 2, top);
		top += about->height()
			+ st::similarChannelsLockAboutPadding.bottom();
		_unlock->setGeometry(0, size.height() - top, size.width(), top);
	}, _unlock->lifetime());

	_unlockHeight = _unlock->heightValue();

	_unlock->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_unlock);
		const auto width = _unlock->width();
		const auto fade = st::similarChannelsLockFade;
		auto gradient = QLinearGradient(0, 0, 0, fade);
		gradient.setStops({
			{ 0., QColor(255, 255, 255, 0) },
			{ 1., st::windowBg->c },
		});
		p.fillRect(0, 0, width, fade, gradient);
		p.fillRect(0, fade, width, _unlock->height() - fade, st::windowBg);
	}, _unlock->lifetime());
}

void ListController::loadMoreRows() {
}

std::unique_ptr<PeerListState> ListController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	result->controllerState = std::move(my);
	return result;
}

void ListController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (dynamic_cast<SavedState*>(typeErasedState)) {
		PeerListController::restoreState(std::move(state));
	}
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	_controller->parentController()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

} // namespace

class InnerWidget final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<ChannelData*> channel);

	[[nodiscard]] not_null<ChannelData*> channel() const {
		return _channel;
	}

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	using ListWidget = PeerListContent;

	// PeerListContentDelegate interface.
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
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;

	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<ListController*> controller);

	const std::shared_ptr<Main::SessionShow> _show;
	not_null<Controller*> _controller;
	const not_null<ChannelData*> _channel;
	std::unique_ptr<ListController> _listController;
	object_ptr<ListWidget> _list;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<ChannelData*> channel)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _channel(channel)
, _listController(std::make_unique<ListController>(controller, _channel))
, _list(setupList(this, _listController.get())) {
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setListState(_listController->saveState());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_listController->restoreState(memento->listState());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

int InnerWidget::desiredHeight() const {
	auto desired = 0;
	desired += _list->fullRowsCount() * st::infoMembersList.item.height;
	return qMax(height(), desired);
}

object_ptr<InnerWidget::ListWidget> InnerWidget::setupList(
		RpWidget *parent,
		not_null<ListController*> controller) {
	controller->setStyleOverrides(&st::infoMembersList);
	auto result = object_ptr<ListWidget>(
		parent,
		controller);
	controller->setContentWidget(this);
	result->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
		auto addmax = (request.ymax < 0)
			? 0
			: st::infoCommonGroupsMargin.top();
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, result->lifetime());
	result->moveToLeft(0, st::infoCommonGroupsMargin.top());
	parent->widthValue(
	) | rpl::start_with_next([list = result.data()](int newWidth) {
		list->resizeToWidth(newWidth);
	}, result->lifetime());
	rpl::combine(
		result->heightValue(),
		controller->unlockHeightValue()
	) | rpl::start_with_next([=](int listHeight, int unlockHeight) {
		auto newHeight = st::infoCommonGroupsMargin.top()
			+ listHeight
			+ (unlockHeight
				? (unlockHeight - st::similarChannelsLockOverlap)
				: st::infoCommonGroupsMargin.bottom());
		parent->resize(parent->width(), std::max(newHeight, 0));
	}, result->lifetime());
	return result;
}

void InnerWidget::peerListSetTitle(rpl::producer<QString> title) {
}

void InnerWidget::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool InnerWidget::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int InnerWidget::peerListSelectedRowsCount() {
	return 0;
}

void InnerWidget::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void InnerWidget::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void InnerWidget::peerListFinishSelectedRowsBunch() {
}

void InnerWidget::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

std::shared_ptr<Main::SessionShow> InnerWidget::peerListUiShow() {
	return _show;
}

Memento::Memento(not_null<ChannelData*> channel)
: ContentMemento(channel, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::SimilarChannels);
}

not_null<ChannelData*> Memento::channel() const {
	return peer()->asChannel();
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, channel());
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setListState(std::unique_ptr<PeerListState> state) {
	_listState = std::move(state);
}

std::unique_ptr<PeerListState> Memento::listState() {
	return std::move(_listState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<ChannelData*> channel)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller,
		channel));
}

rpl::producer<QString> Widget::title() {
	return tr::lng_similar_channels_title();
}

not_null<ChannelData*> Widget::channel() const {
	return _inner->channel();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto similarMemento = dynamic_cast<Memento*>(memento.get())) {
		if (similarMemento->channel() == channel()) {
			restoreState(similarMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(channel());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::SimilarChannels
