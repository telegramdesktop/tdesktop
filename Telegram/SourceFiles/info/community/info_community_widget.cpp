/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/community/info_community_widget.h"

#include "boxes/peers/community_box.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "info/profile/info_profile_top_bar.h"
#include "info/profile/info_profile_values.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Info::Community {

class InnerWidget final : public Ui::VerticalLayout {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	[[nodiscard]] rpl::producer<> backRequest() const;
	void enableBackButton();
	void showFinished();

	[[nodiscard]] bool hasFlexibleTopBar() const;
	base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<Ui::RpWidget*> parent);

private:
	[[nodiscard]] rpl::producer<TextWithEntities> chatsStatusValue() const;

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;

	rpl::variable<bool> _backToggles;
	rpl::event_stream<> _backClicks;
	rpl::event_stream<> _showFinished;

};

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: Ui::VerticalLayout(parent)
, _controller(controller)
, _peer(peer) {
	if (const auto community = peer->asChannel()) {
		SetupCommunityContent(
			this,
			controller->parentController(),
			community,
			controller->uiShow());
		if (community->canManageLinkedPeers()) {
			Ui::AddSkip(
				this,
				st::communityAddChatButtonMargin.top()
					+ st::communityAddChatButton.height
					+ st::communityAddChatButtonMargin.bottom());
		}
	}
}

rpl::producer<> InnerWidget::backRequest() const {
	return _backClicks.events();
}

void InnerWidget::enableBackButton() {
	_backToggles.force_assign(true);
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

bool InnerWidget::hasFlexibleTopBar() const {
	const auto channel = _peer->asChannel();
	return channel && channel->isCommunity();
}

rpl::producer<TextWithEntities> InnerWidget::chatsStatusValue() const {
	const auto channel = _peer->asChannel();
	Assert(channel != nullptr);
	return channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::map([=] {
		return channel->communityInfo();
	}) | rpl::map([](Data::CommunityInfo *info)
			-> rpl::producer<int> {
		if (!info) {
			return rpl::single(0);
		}
		return info->linkedPeersValue() | rpl::map([info] {
			return int(info->linkedPeers().size());
		});
	}) | rpl::flatten_latest() | rpl::map([](int count) {
		return TextWithEntities{
			tr::lng_community_chats(tr::now, lt_count, float64(count)),
		};
	});
}

base::weak_qptr<Ui::RpWidget> InnerWidget::createPinnedToTop(
		not_null<Ui::RpWidget*> parent) {
	if (!hasFlexibleTopBar()) {
		return nullptr;
	}
	const auto content = Ui::CreateChild<Profile::TopBar>(
		parent,
		Profile::TopBar::Descriptor{
			.controller = _controller->parentController(),
			.key = _controller->key(),
			.wrap = _controller->wrapValue(),
			.source = Profile::TopBar::Source::Community,
			.peer = _peer,
			.backToggles = _backToggles.value(),
			.showFinished = _showFinished.events(),
			.customStatus = chatsStatusValue(),
		});
	content->backRequest(
	) | rpl::start_to_stream(_backClicks, content->lifetime());
	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(peer, nullptr, nullptr, PeerId()) {
}

Section Memento::section() const {
	return Section(Section::Type::Community);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, peer());
	result->setInternalState(geometry, this);
	return result;
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: ContentWidget(parent, controller)
, _inner(
	setupFlexibleInnerWidget(
		object_ptr<InnerWidget>(this, controller, peer),
		_flexibleScroll))
, _pinnedToTop(_inner->createPinnedToTop(this)) {
	_inner->move(0, 0);

	_inner->backRequest() | rpl::on_next([=] {
		checkBeforeClose([=] { controller->showBackFromStack(); });
	}, _inner->lifetime());

	if (_pinnedToTop) {
		_inner->widthValue(
		) | rpl::on_next([=](int w) {
			_pinnedToTop->resizeToWidth(w);
			setScrollTopSkip(_pinnedToTop->height());
		}, _pinnedToTop->lifetime());

		_pinnedToTop->heightValue(
		) | rpl::on_next([=](int h) {
			setScrollTopSkip(h);
		}, _pinnedToTop->lifetime());
	}

	if (_pinnedToTop
		&& _pinnedToTop->minimumHeight()
		&& _inner->hasFlexibleTopBar()) {
		_flexibleScrollHelper = std::make_unique<FlexibleScrollHelper>(
			scroll(),
			_inner,
			_pinnedToTop.get(),
			[=](QMargins margins) {
				ContentWidget::setPaintPadding(std::move(margins));
			},
			[=](rpl::producer<not_null<QEvent*>> &&events) {
				ContentWidget::setViewport(std::move(events));
			},
			_flexibleScroll);
	}

	if (const auto channel = peer->asChannel()) {
		if (channel->canManageLinkedPeers()) {
			_bottom = setupAddChatButton();
		}
	}
}

std::unique_ptr<Ui::RpWidget> Widget::setupAddChatButton() {
	const auto controller = this->controller()->parentController();
	const auto community = peer()->asChannel();

	auto result = std::make_unique<Ui::VerticalLayout>(this);
	const auto wrap = result.get();

	const auto row = wrap->add(
		object_ptr<Ui::RpWidget>(wrap),
		st::communityAddChatButtonMargin);
	const auto button = MakeCommunityAddChatButton(row, [=] {
		ShowChooseChatToAddBox(controller, community);
	});

	row->resize(row->width(), st::communityAddChatButton.height);
	row->widthValue() | rpl::on_next([=](int width) {
		button->setFullWidth(width);
		button->moveToLeft(0, 0, width);
	}, row->lifetime());

	wrap->paintOn([=](QPainter &p) {
		const auto fadeHeight = st::communityAddChatButtonMargin.top()
			+ st::communityAddChatButton.height
			+ st::communityAddChatButtonMargin.bottom();
		Dialogs::PaintBottomFade(p, wrap->width(), fadeHeight, st::boxBg);
	});

	widthValue() | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
	}, wrap->lifetime());

	rpl::combine(
		wrap->heightValue(),
		heightValue(),
		this->controller()->contentTillBottomValue()
	) | rpl::on_next([=](int height, int fullHeight, bool tillBottom) {
		const auto shift = tillBottom ? 0 : st::boxRadius;
		wrap->move(0, fullHeight - height + shift);

		const auto scrollSkip = st::communityAddChatButtonMargin.top()
			+ st::communityAddChatButton.height
			+ st::communityAddChatButtonMargin.bottom();
		scroll()->setVerticalBarBottomSkip(scrollSkip - shift);
	}, wrap->lifetime());

	return result;
}

void Widget::enableBackButton() {
	_inner->enableBackButton();
}

void Widget::showFinished() {
	_inner->showFinished();
}

rpl::producer<QString> Widget::title() {
	return Info::Profile::NameValue(peer());
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto communityMemento = dynamic_cast<Memento*>(memento.get())) {
		restoreState(communityMemento);
		return true;
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
	auto result = std::make_shared<Memento>(peer());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
}

void Widget::restoreState(not_null<Memento*> memento) {
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::Community
