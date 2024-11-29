/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/starref/info_bot_starref_join_widget.h"

#include "apiwrap.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsTransfer.
#include "boxes/peer_list_box.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QApplication>

namespace Info::BotStarRef::Join {
namespace {

constexpr auto kPerPage = 50;

enum class JoinType {
	Joined,
	Suggested,
};

struct ConnectedBotState {
	StarRefProgram program;
	QString link;
	TimeId date = 0;
	int users = 0;
	bool revoked = false;
};
struct ConnectedBot {
	not_null<UserData*> bot;
	ConnectedBotState state;
};
using ConnectedBots = std::vector<ConnectedBot>;

class ListController final : public PeerListController {
public:
	ListController(
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		JoinType type);
	~ListController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] rpl::producer<int> rowCountValue() const;

	void setContentWidget(not_null<Ui::RpWidget*> widget);
	[[nodiscard]] rpl::producer<int> unlockHeightValue() const;

private:
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(ConnectedBot bot);

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	const JoinType _type = {};

	base::flat_map<not_null<PeerData*>, ConnectedBotState> _states;

	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	QString _offsetThing;
	bool _allLoaded = false;

	rpl::variable<int> _rowCount = 0;

};

[[nodiscard]] ConnectedBots Parse(
		not_null<Main::Session*> session,
		const MTPpayments_ConnectedStarRefBots &bots) {
	const auto &data = bots.data();
	session->data().processUsers(data.vusers());
	const auto &list = data.vconnected_bots().v;
	auto result = ConnectedBots();
	for (const auto &bot : list) {
		const auto &data = bot.data();
		const auto botId = UserId(data.vbot_id());
		const auto link = qs(data.vurl());
		const auto date = data.vdate().v;
		const auto commission = data.vcommission_permille().v;
		const auto durationMonths
			= data.vduration_months().value_or_empty();
		const auto users = int(data.vparticipants().v);
		const auto revoked = data.is_revoked();
		result.push_back({
			.bot = session->data().user(botId),
			.state = {
				.program = {
					.commission = ushort(commission),
					.durationMonths = uchar(durationMonths),
				},
				.link = link,
				.date = date,
				.users = users,
				.revoked = revoked,
			},
		});
	}
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> FormatProgramDuration(
		StarRefProgram program) {
	return !program.durationMonths
		? tr::lng_star_ref_one_about_for_forever(Ui::Text::RichLangValue)
		: (program.durationMonths < 12)
		? tr::lng_star_ref_one_about_for_months(
			lt_count,
			rpl::single(program.durationMonths * 1.),
			Ui::Text::RichLangValue)
		: tr::lng_star_ref_one_about_for_years(
			lt_count,
			rpl::single((program.durationMonths / 12) * 1.),
			Ui::Text::RichLangValue);
}

[[nodiscard]] not_null<Ui::RoundButton*> AddFullWidthButton(
		not_null<Ui::BoxContent*> box,
		rpl::producer<QString> text,
		Fn<void()> callback = nullptr) {
	const auto &boxSt = box->getDelegate()->style();
	const auto result = box->addButton(std::move(text), std::move(callback));
	rpl::combine(
		box->widthValue(),
		result->widthValue()
	) | rpl::start_with_next([=](int width, int buttonWidth) {
		const auto correct = width
			- boxSt.buttonPadding.left()
			- boxSt.buttonPadding.right();
		if (correct > 0 && buttonWidth != correct) {
			result->resizeToWidth(correct);
			result->moveToLeft(
				boxSt.buttonPadding.left(),
				boxSt.buttonPadding.top(),
				width);
		}
	}, result->lifetime());
	return result;
}

[[nodiscard]] object_ptr<Ui::BoxContent> StarRefLinkBox(
		ConnectedBot row,
		not_null<PeerData*> peer) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_star_ref_link_title());

		const auto bot = row.bot;
		const auto program = row.state.program;
		auto duration = !program.durationMonths
			? tr::lng_star_ref_one_about_for_forever(Ui::Text::RichLangValue)
			: (program.durationMonths < 12)
			? tr::lng_star_ref_one_about_for_months(
				lt_count,
				rpl::single(program.durationMonths * 1.),
				Ui::Text::RichLangValue)
			: tr::lng_star_ref_one_about_for_years(
				lt_count,
				rpl::single((program.durationMonths / 12) * 1.),
				Ui::Text::RichLangValue);
		auto text = tr::lng_star_ref_link_about_channel(
			lt_amount,
			rpl::single(Ui::Text::Bold(FormatStarRefCommission(program.commission))),
			lt_app,
			rpl::single(Ui::Text::Bold(bot->name())),
			lt_duration,
			std::move(duration),
			Ui::Text::WithEntities);
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, std::move(text), st::boxLabel));
		Ui::AddSkip(box->verticalLayout());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(row.state.link) | Ui::Text::ToLink(),
				st::boxLabel));
		Ui::AddSkip(box->verticalLayout());
		if (row.state.users > 0) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_link_copy_users(
						lt_count,
						rpl::single(row.state.users * 1.),
						lt_app,
						rpl::single(bot->name())),
					st::boxLabel));
		} else {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_link_copy_none(
						lt_app,
						rpl::single(bot->name())),
					st::boxLabel));
		}

		box->addButton(tr::lng_star_ref_link_copy(), [=] {
			QApplication::clipboard()->setText(row.state.link);
			box->uiShow()->showToast(tr::lng_username_copied(tr::now));
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	});
}

[[nodiscard]] object_ptr<Ui::BoxContent> JoinStarRefBox(
		ConnectedBot row,
		not_null<PeerData*> peer) {
	Expects(row.bot->isUser());

	return Box([=](not_null<Ui::GenericBox*> box) {
		const auto show = box->uiShow();

		const auto bot = row.bot;
		const auto program = row.state.program;

		box->setStyle(st::starrefFooterBox);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		box->addRow(
			CreateUserpicsTransfer(
				box,
				rpl::single(std::vector{ not_null<PeerData*>(bot) }),
				peer,
				UserpicsTransferType::StarRefJoin),
			st::boxRowPadding + st::starrefJoinUserpicsPadding);
		box->addRow(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				box,
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_star_ref_title(),
					st::boxTitle)),
			st::boxRowPadding + st::starrefJoinTitlePadding);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_star_ref_one_about(
					lt_app,
					rpl::single(Ui::Text::Bold(bot->name())),
					lt_amount,
					rpl::single(Ui::Text::Bold(
						FormatStarRefCommission(program.commission))),
					lt_duration,
					FormatProgramDuration(program),
					Ui::Text::WithEntities),
				st::boxLabel),
			st::boxRowPadding);
		struct State {
			QPointer<Ui::GenericBox> weak;
			bool sent = false;
		};
		const auto state = std::make_shared<State>();
		state->weak = box;

		const auto send = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			bot->session().api().request(MTPpayments_ConnectStarRefBot(
				peer->input,
				bot->asUser()->inputUser
			)).done([=](const MTPpayments_ConnectedStarRefBots &result) {
				const auto parsed = Parse(&bot->session(), result);
				if (parsed.empty()) {
					state->sent = false;
					show->showToast(u"Failed."_q);
				} else {
					show->show(StarRefLinkBox(parsed.front(), peer));
					if (const auto strong = state->weak.data()) {
						strong->closeBox();
					}
				}
			}).fail([=](const MTP::Error &error) {
				state->sent = false;
				show->showToast(u"Failed: "_q + error.type());
			}).send();
		};
		const auto button = AddFullWidthButton(
			box,
			tr::lng_star_ref_one_join(),
			send);
		const auto footer = Ui::CreateChild<Ui::FlatLabel>(
			button->parentWidget(),
			tr::lng_star_ref_one_join_text(
				lt_terms,
				tr::lng_star_ref_button_link(
				) | Ui::Text::ToLink(tr::lng_star_ref_tos_url(tr::now)),
				Ui::Text::WithEntities),
			st::starrefJoinFooter);
		button->geometryValue() | rpl::start_with_next([=](QRect geometry) {
			footer->resizeToWidth(geometry.width());
			const auto &st = box->getDelegate()->style();
			const auto top = geometry.y() + geometry.height();
			const auto available = st.buttonPadding.bottom();
			footer->moveToLeft(
				geometry.left(),
				top + (available - footer->height()) / 2);
		}, footer->lifetime());
	});
}

ListController::ListController(
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	JoinType type)
: PeerListController()
, _controller(controller)
, _peer(peer)
, _type(type) {
}

ListController::~ListController() {
	if (_requestId) {
		session().api().request(_requestId).cancel();
	}
}

Main::Session &ListController::session() const {
	return _peer->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(ConnectedBot bot) {
	_states.emplace(bot.bot, bot.state);
	auto result = std::make_unique<PeerListRow>(bot.bot);
	const auto program = bot.state.program;
	const auto duration = !program.durationMonths
		? tr::lng_star_ref_duration_forever(tr::now)
		: (program.durationMonths < 12)
		? tr::lng_months(tr::now, lt_count, program.durationMonths)
		: tr::lng_years(tr::now, lt_count, program.durationMonths / 12);
	if (bot.state.revoked) {
		result->setCustomStatus(u"Revoked"_q);
	} else {
		result->setCustomStatus(u"+%1%, %2"_q.arg(
			QString::number(program.commission / 10.),
			duration));
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetTitle((_type == JoinType::Joined)
		? tr::lng_star_ref_list_my()
		: tr::lng_star_ref_list_subtitle());

	loadMoreRows();
}

void ListController::loadMoreRows() {
	if (_requestId || _allLoaded) {
		return;
	} else if (_type == JoinType::Joined) {
		using Flag = MTPpayments_GetConnectedStarRefBots::Flag;
		_requestId = session().api().request(MTPpayments_GetConnectedStarRefBots(
			MTP_flags(Flag()
				| (_offsetDate ? Flag::f_offset_date : Flag())
				| (_offsetThing.isEmpty() ? Flag() : Flag::f_offset_link)),
			_peer->input,
			MTP_int(_offsetDate),
			MTP_string(_offsetThing),
			MTP_int(kPerPage)
		)).done([=](const MTPpayments_ConnectedStarRefBots &result) {
			const auto parsed = Parse(&session(), result);
			if (parsed.empty()) {
				_allLoaded = true;
			} else {
				for (const auto &bot : parsed) {
					delegate()->peerListAppendRow(createRow(bot));
				}
				delegate()->peerListRefreshRows();
				_rowCount = delegate()->peerListFullRowsCount();
			}
			_requestId = 0;
		}).fail([=](const MTP::Error &error) {
			_requestId = 0;
		}).send();
	} else {
		using Flag = MTPpayments_GetSuggestedStarRefBots::Flag;
		_requestId = session().api().request(MTPpayments_GetSuggestedStarRefBots(
			MTP_flags(Flag::f_order_by_revenue),
			_peer->input,
			MTP_string(_offsetThing),
			MTP_int(kPerPage)
		)).done([=](const MTPpayments_SuggestedStarRefBots &result) {
			const auto &data = result.data();
			if (data.vnext_offset()) {
				_offsetThing = qs(*data.vnext_offset());
			} else {
				_allLoaded = true;
			}
			session().data().processUsers(data.vusers());
			for (const auto &bot : data.vsuggested_bots().v) {
				const auto &data = bot.data();
				const auto botId = UserId(data.vbot_id());
				const auto commission = data.vcommission_permille().v;
				const auto durationMonths
					= data.vduration_months().value_or_empty();
				const auto user = session().data().user(botId);
				delegate()->peerListAppendRow(createRow({
					.bot = user,
					.state = {
						.program = {
							.commission = ushort(commission),
							.durationMonths = uchar(durationMonths),
						},
					},
				}));
			}
			delegate()->peerListRefreshRows();
			_rowCount = delegate()->peerListFullRowsCount();
			_requestId = 0;
		}).fail([=](const MTP::Error &error) {
			_allLoaded = true;
			_requestId = 0;
		}).send();
	}
}

rpl::producer<int> ListController::rowCountValue() const {
	return _rowCount.value();
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	const auto bot = row->peer()->asUser();
	const auto i = _states.find(bot);
	Assert(i != end(_states));
	const auto state = i->second;
	const auto window = _controller->parentController();
	if (_type == JoinType::Joined || !state.link.isEmpty()) {
		window->show(StarRefLinkBox({ bot, state }, _peer));
	} else {
		window->show(JoinStarRefBox({ bot, state }, _peer));
	}
}

base::unique_qptr<Ui::PopupMenu> ListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto bot = row->peer()->asUser();
	const auto i = _states.find(bot);
	Assert(i != end(_states));
	const auto state = i->second;
	if (state.link.isEmpty()) {
		return nullptr;
	}
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(result.get());

	addAction(tr::lng_star_ref_list_my_open(tr::now), [=] {
		_controller->parentController()->showPeerHistory(bot);
	}, &st::menuIconBot);
	addAction(tr::lng_star_ref_list_my_copy(tr::now), [=] {
		QApplication::clipboard()->setText(state.link);
		_controller->parentController()->showToast(
			tr::lng_username_copied(tr::now));
	}, &st::menuIconLinks);
	const auto revoke = [=] {
		session().api().request(MTPpayments_EditConnectedStarRefBot(
			MTP_flags(MTPpayments_EditConnectedStarRefBot::Flag::f_revoked),
			_peer->input,
			MTP_string(state.link)
		)).done([=] {
			_controller->parentController()->showToast(u"Revoked!"_q);
		}).fail([=](const MTP::Error &error) {
			_controller->parentController()->showToast(u"Failed: "_q + error.type());
		}).send();
	};
	addAction({
		.text = tr::lng_star_ref_list_my_leave(tr::now),
		.handler = revoke,
		.icon = &st::menuIconLeaveAttention,
		.isAttention = true,
	});
	return result;
}

} // namespace

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(QWidget *parent, not_null<Controller*> controller);

	[[nodiscard]] not_null<PeerData*> peer() const;

	void showFinished();
	void setInnerFocus();

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

private:
	void prepare();
	void setupInfo();
	void setupMy();
	void setupSuggested();

	[[nodiscard]] object_ptr<Ui::RpWidget> infoRow(
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		not_null<const style::icon*> icon);

	const not_null<Controller*> _controller;
	const not_null<Ui::VerticalLayout*> _container;

};

InnerWidget::InnerWidget(QWidget *parent, not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _container(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	prepare();
}

void InnerWidget::prepare() {
	Ui::ResizeFitChild(this, _container);

	setupInfo();
	Ui::AddSkip(_container);
	Ui::AddDivider(_container);
	setupMy();
	setupSuggested();
}

void InnerWidget::setupInfo() {
	AddSkip(_container, st::defaultVerticalListSkip * 2);

	_container->add(infoRow(
		tr::lng_star_ref_reliable_title(),
		tr::lng_star_ref_reliable_about(),
		&st::menuIconAntispam));

	_container->add(infoRow(
		tr::lng_star_ref_transparent_title(),
		tr::lng_star_ref_transparent_about(),
		&st::menuIconTransparent));

	_container->add(infoRow(
		tr::lng_star_ref_simple_title(),
		tr::lng_star_ref_simple_about(),
		&st::menuIconLike));
}

void InnerWidget::setupMy() {
	const auto wrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	Ui::AddSubsectionTitle(inner, tr::lng_star_ref_list_my());

	const auto delegate = lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime().make_state<ListController>(
		_controller,
		peer(),
		JoinType::Joined);
	const auto content = inner->add(
		object_ptr<PeerListContent>(
			_container,
			controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddDivider(inner);

	wrap->toggleOn(controller->rowCountValue(
	) | rpl::map(rpl::mappers::_1 > 0));
}

void InnerWidget::setupSuggested() {
	Ui::AddSkip(_container);
	Ui::AddSubsectionTitle(_container, tr::lng_star_ref_list_subtitle());

	const auto delegate = lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	auto controller = lifetime().make_state<ListController>(
		_controller,
		peer(),
		JoinType::Suggested);
	const auto content = _container->add(
		object_ptr<PeerListContent>(
			_container,
			controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);
}

object_ptr<Ui::RpWidget> InnerWidget::infoRow(
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		not_null<const style::icon*> icon) {
	auto result = object_ptr<Ui::VerticalLayout>(_container);
	const auto raw = result.data();

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(title) | Ui::Text::ToBold(),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);
	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			std::move(text),
			st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);
	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);

	return result;
}

not_null<PeerData*> InnerWidget::peer() const {
	return _controller->key().starrefPeer();
}

void InnerWidget::showFinished() {

}

void InnerWidget::setInnerFocus() {
	setFocus();
}

void InnerWidget::saveState(not_null<Memento*> memento) {

}

void InnerWidget::restoreState(not_null<Memento*> memento) {

}

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag(controller->starrefPeer(), controller->starrefType())) {
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(Tag(peer, Type::Join)) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::BotStarRef);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _inner(setInnerWidget(object_ptr<InnerWidget>(this, controller))) {
	_top = setupTop();
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return (memento->starrefPeer() == peer());
}

rpl::producer<QString> Widget::title() {
	return tr::lng_star_ref_list_title();
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

rpl::producer<bool> Widget::desiredShadowVisibility() const {
	return rpl::single<bool>(true);
}

void Widget::showFinished() {
	_inner->showFinished();
}

void Widget::setInnerFocus() {
	_inner->setInnerFocus();
}

void Widget::enableBackButton() {
	_backEnabled = true;
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
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

std::unique_ptr<Ui::Premium::TopBarAbstract> Widget::setupTop() {
	auto title = tr::lng_star_ref_list_title();
	auto about = tr::lng_star_ref_list_about_channel()
		| Ui::Text::ToWithEntities();

	const auto controller = this->controller();
	const auto weak = base::make_weak(controller->parentController());
	const auto clickContextOther = [=] {
		return QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
			.botStartAutoSubmit = true,
		});
	};
	auto result = std::make_unique<Ui::Premium::TopBar>(
		this,
		st::starrefCover,
		Ui::Premium::TopBarDescriptor{
			.clickContextOther = clickContextOther,
			.logo = u"affiliate"_q,
			.title = std::move(title),
			.about = std::move(about),
			.light = true,
		});
	const auto raw = result.get();

	controller->wrapValue(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		raw->setRoundEdges(wrap == Info::Wrap::Layer);
	}, raw->lifetime());

	const auto baseHeight = st::starrefCoverHeight;
	raw->resize(width(), baseHeight);

	raw->additionalHeight(
	) | rpl::start_with_next([=](int additionalHeight) {
		raw->setMaximumHeight(baseHeight + additionalHeight);
		raw->setMinimumHeight(baseHeight + additionalHeight);
		setPaintPadding({ 0, raw->height(), 0, 0 });
	}, raw->lifetime());

	controller->wrapValue(
	) | rpl::start_with_next([=](Info::Wrap wrap) {
		const auto isLayer = (wrap == Info::Wrap::Layer);
		_back = base::make_unique_q<Ui::FadeWrap<Ui::IconButton>>(
			raw,
			object_ptr<Ui::IconButton>(
				raw,
				(isLayer
					? st::infoLayerTopBar.back
					: st::infoTopBar.back)),
			st::infoTopBarScale);
		_back->setDuration(0);
		_back->toggleOn(isLayer
			? _backEnabled.value() | rpl::type_erased()
			: rpl::single(true));
		_back->entity()->addClickHandler([=] {
			controller->showBackFromStack();
		});
		_back->toggledValue(
		) | rpl::start_with_next([=](bool toggled) {
			const auto &st = isLayer ? st::infoLayerTopBar : st::infoTopBar;
			raw->setTextPosition(
				toggled ? st.back.width : st.titlePosition.x(),
				st.titlePosition.y());
		}, _back->lifetime());

		if (!isLayer) {
			_close = nullptr;
		} else {
			_close = base::make_unique_q<Ui::IconButton>(
				raw,
				st::infoTopBarClose);
			_close->addClickHandler([=] {
				controller->parentController()->hideLayer();
				controller->parentController()->hideSpecialLayer();
			});
			raw->widthValue(
			) | rpl::start_with_next([=] {
				_close->moveToRight(0, 0);
			}, _close->lifetime());
		}
	}, raw->lifetime());

	raw->move(0, 0);
	widthValue() | rpl::start_with_next([=](int width) {
		raw->resizeToWidth(width);
		setScrollTopSkip(raw->height());
	}, raw->lifetime());

	return result;
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer)));
}

} // namespace Info::BotStarRef::Join

