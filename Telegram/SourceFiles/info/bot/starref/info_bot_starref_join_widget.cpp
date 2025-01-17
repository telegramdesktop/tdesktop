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
#include "base/weak_ptr.h"
#include "boxes/peer_list_box.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
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
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
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
	Existing,
};

enum class SuggestedSort {
	Profitability,
	Revenue,
	Date
};

class ListController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	ListController(
		not_null<Window::SessionController*> controller,
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
	[[nodiscard]] rpl::producer<ConnectedBot> connected() const;
	[[nodiscard]] rpl::producer<ConnectedBot> revoked() const;
	[[nodiscard]] rpl::producer<> addForBotRequests() const;

	void setSort(SuggestedSort sort);

	void process(ConnectedBot row);

private:
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(ConnectedBot bot);
	void open(not_null<UserData*> bot, ConnectedBotState state);
	void requestRecipients();
	void setupAddForBot();
	void setupLinkBadge();
	void refreshRows();

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const JoinType _type = {};

	base::flat_map<not_null<PeerData*>, ConnectedBotState> _states;
	base::flat_set<not_null<PeerData*>> _resolving;
	UserData *_openOnResolve = nullptr;

	Fn<void()> _recipientsReady;
	std::vector<not_null<PeerData*>> _recipients;
	rpl::event_stream<ConnectedBot> _connected;
	rpl::event_stream<ConnectedBot> _revoked;
	rpl::event_stream<> _addForBot;

	mtpRequestId _requestId = 0;
	TimeId _offsetDate = 0;
	QString _offsetThing;
	bool _allLoaded = false;
	bool _recipientsRequested = false;
	SuggestedSort _sort = SuggestedSort::Profitability;
	QImage _linkBadge;

	rpl::variable<int> _rowCount = 0;

};

class Row final : public PeerListRow {
public:
	Row(
		not_null<PeerData*> peer,
		StarRefProgram program,
		QImage *link = nullptr);

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;

private:
	void refreshStatus() override;

	StarRefProgram _program;
	QImage *_link = nullptr;
	QImage _userpic;
	QImage _badge;

};

Row::Row(
	not_null<PeerData*> peer,
	StarRefProgram program,
	QImage *link)
: PeerListRow(peer)
, _program(program)
, _link(link) {
}

void Row::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	const auto top = y
		+ st::contactsStatusFont->ascent
		- st::starrefCommissionFont->ascent
		- st::lineWidth;
	p.drawImage(x, top, _badge);

	const auto space = st::normalFont->spacew;
	auto shift = (_badge.width() / _badge.devicePixelRatio()) + space;
	x += shift;
	availableWidth -= shift;

	PeerListRow::paintStatusText(
		p,
		st,
		x,
		y,
		availableWidth,
		outerWidth,
		selected);
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(
		bool forceRound) {
	if (!_link) {
		return PeerListRow::generatePaintUserpicCallback(forceRound);
	}
	return [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		const auto ratio = style::DevicePixelRatio();
		const auto dimensions = QSize(size, size);
		if (_userpic.size() != dimensions * ratio) {
			_userpic = QImage(
				dimensions * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_userpic.setDevicePixelRatio(ratio);
		}
		_userpic.fill(Qt::transparent);

		auto q = Painter(&_userpic);
		auto hq = PainterHighQualityEnabler(q);
		auto paint = PeerListRow::generatePaintUserpicCallback(forceRound);
		paint(q, 0, 0, size, size);
		const auto corner = _link->size() / _link->devicePixelRatio();
		auto pen = QPen(Qt::transparent, st::lineWidth * 1.5);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setPen(pen);
		q.setBrush(st::historyPeer2UserpicBg2);
		const auto left = size - corner.width();
		const auto top = size - corner.height();
		q.drawEllipse(left, top, corner.width(), corner.height());
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		q.drawImage(left, top, *_link);
		q.end();

		p.drawImage(x, y, _userpic);
	};
}

void Row::refreshStatus() {
	const auto text = FormatCommission(_program.commission);
	const auto padding = st::starrefCommissionPadding;
	const auto font = st::starrefCommissionFont;
	const auto width = font->width(text);
	const auto inner = QRect(0, 0, width, font->height);
	const auto outer = inner.marginsAdded(padding);
	const auto ratio = style::DevicePixelRatio();
	_badge = QImage(
		outer.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_badge.setDevicePixelRatio(ratio);
	_badge.fill(Qt::transparent);

	auto p = QPainter(&_badge);
	p.setBrush(st::historyPeer2UserpicBg2);
	p.setPen(Qt::NoPen);
	const auto radius = st::roundRadiusSmall;
	p.drawRoundedRect(outer.translated(-outer.topLeft()), radius, radius);
	p.setFont(font);
	p.setBrush(Qt::NoBrush);
	p.setPen(st::historyPeerUserpicFg);
	p.drawText(padding.left(), padding.top() + font->ascent, text);
	p.end();

	setCustomStatus(FormatProgramDuration(_program.durationMonths));
}

void Resolve(
		not_null<PeerData*> peer,
		not_null<UserData*> bot,
		Fn<void(std::optional<ConnectedBotState>)> done) {
	peer->session().api().request(MTPpayments_GetConnectedStarRefBot(
		peer->input,
		bot->inputUser
	)).done([=](const MTPpayments_ConnectedStarRefBots &result) {
		const auto parsed = Parse(&peer->session(), result);
		if (parsed.empty()) {
			done(std::nullopt);
		} else {
			done(parsed.front().state);
		}
	}).fail([=] {
		done(std::nullopt);
	}).send();
}

ListController::ListController(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	JoinType type)
: PeerListController()
, _controller(controller)
, _peer(peer)
, _type(type) {
	setStyleOverrides(&st::peerListSingleRow);

	if (_type == JoinType::Joined) {
		setupLinkBadge();
		style::PaletteChanged() | rpl::start_with_next([=] {
			setupLinkBadge();
		}, lifetime());
	}
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
	const auto link = _linkBadge.isNull() ? nullptr : &_linkBadge;
	return std::make_unique<Row>(bot.bot, bot.state.program, link);
}

void ListController::setupLinkBadge() {
	const auto side = st::starrefLinkBadge;
	const auto size = QSize(side, side);
	const auto ratio = style::DevicePixelRatio();

	_linkBadge = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	_linkBadge.setDevicePixelRatio(ratio);
	_linkBadge.fill(Qt::transparent);

	const auto skip = st::starrefLinkBadgeSkip;
	const auto inner = QSize(side - 2 * skip, side - 2 * skip);

	auto p = QPainter(&_linkBadge);
	auto hq = PainterHighQualityEnabler(p);

	auto owned = Lottie::MakeIcon({
		.name = u"starref_link"_q,
		.color = &st::historyPeerUserpicFg,
		.sizeOverride = inner,
	});
	p.drawImage(QRect(QPoint(skip, skip), inner), owned->frame());
}

void ListController::prepare() {
	delegate()->peerListSetTitle((_type == JoinType::Joined)
		? tr::lng_star_ref_list_my()
		: tr::lng_star_ref_list_title());
	loadMoreRows();
}

void ListController::loadMoreRows() {
	if (_requestId || _allLoaded) {
		return;
	} else if (_type == JoinType::Joined) {
		using Flag = MTPpayments_GetConnectedStarRefBots::Flag;
		_requestId = session().api().request(
			MTPpayments_GetConnectedStarRefBots(
				MTP_flags(Flag()
					| (_offsetDate ? Flag::f_offset_date : Flag())
					| (_offsetThing.isEmpty() ? Flag() : Flag::f_offset_link)),
				_peer->input,
				MTP_int(_offsetDate),
				MTP_string(_offsetThing),
				MTP_int(kPerPage))
		).done([=](const MTPpayments_ConnectedStarRefBots &result) {
			const auto parsed = Parse(&session(), result);
			if (parsed.empty()) {
				_allLoaded = true;
			} else {
				for (const auto &bot : parsed) {
					if (!delegate()->peerListFindRow(bot.bot->id.value)) {
						delegate()->peerListAppendRow(createRow(bot));
					}
				}
				refreshRows();
			}
			_requestId = 0;
		}).fail([=](const MTP::Error &error) {
			_requestId = 0;
		}).send();
	} else {
		if (_type == JoinType::Existing) {
			setDescriptionText(tr::lng_contacts_loading(tr::now));
		}
		using Flag = MTPpayments_GetSuggestedStarRefBots::Flag;
		_requestId = session().api().request(
			MTPpayments_GetSuggestedStarRefBots(
				MTP_flags((_sort == SuggestedSort::Revenue)
					? Flag::f_order_by_revenue
					: (_sort == SuggestedSort::Date)
					? Flag::f_order_by_date
					: Flag()),
				_peer->input,
				MTP_string(_offsetThing),
				MTP_int(kPerPage))
		).done([=](const MTPpayments_SuggestedStarRefBots &result) {
			setDescriptionText(QString());
			setupAddForBot();

			if (_offsetThing.isEmpty()) {
				while (delegate()->peerListFullRowsCount() > 0) {
					delegate()->peerListRemoveRow(
						delegate()->peerListRowAt(0));
				}
			}

			const auto &data = result.data();
			if (data.vnext_offset()) {
				_offsetThing = qs(*data.vnext_offset());
			} else {
				_allLoaded = true;
			}
			session().data().processUsers(data.vusers());
			for (const auto &program : data.vsuggested_bots().v) {
				const auto botId = UserId(program.data().vbot_id());
				const auto user = session().data().user(botId);
				if (!delegate()->peerListFindRow(user->id.value)) {
					delegate()->peerListAppendRow(createRow({
						.bot = user,
						.state = {
							.program = Data::ParseStarRefProgram(&program),
							.unresolved = true,
						},
					}));
				}
			}
			refreshRows();
			_requestId = 0;
		}).fail([=](const MTP::Error &error) {
			_allLoaded = true;
			_requestId = 0;
		}).send();
	}
}

void ListController::setupAddForBot() {
	const auto user = _peer->asUser();
	if (_type != JoinType::Existing
		|| !user
		|| !user->isBot()
		|| user->botInfo->starRefProgram.commission > 0) {
		return;
	}
	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		nullptr,
		object_ptr<Ui::SettingsButton>(
			nullptr,
			tr::lng_star_ref_add_bot(lt_bot, rpl::single(user->name())),
			st::inviteViaLinkButton),
		style::margins(0, st::membersMarginTop, 0, 0));

	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		button->entity(),
		st::starrefAddForBotIcon,
		QPoint());
	button->entity()->heightValue(
	) | rpl::start_with_next([=](int height) {
		icon->moveToLeft(
			st::starrefAddForBotIconPosition.x(),
			(height - st::starrefAddForBotIcon.height()) / 2);
	}, icon->lifetime());

	button->entity()->setClickedCallback([=] {
		_addForBot.fire({});
	});
	button->entity()->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		delegate()->peerListMouseLeftGeometry();
	}, button->lifetime());
	delegate()->peerListSetAboveWidget(std::move(button));
}

rpl::producer<int> ListController::rowCountValue() const {
	return _rowCount.value();
}

rpl::producer<ConnectedBot> ListController::connected() const {
	return _connected.events();
}

rpl::producer<ConnectedBot> ListController::revoked() const {
	return _revoked.events();
}

rpl::producer<> ListController::addForBotRequests() const {
	return _addForBot.events();
}

void ListController::setSort(SuggestedSort sort) {
	if (_sort == sort) {
		return;
	}
	_sort = sort;
	if (const auto requestId = base::take(_requestId)) {
		session().api().request(requestId).cancel();
	}
	_allLoaded = false;
	_offsetThing = QString();
	loadMoreRows();
}

void ListController::process(ConnectedBot row) {
	if (_type != JoinType::Joined) {
		_states[row.bot] = { .program = row.state.program };
	}
	if (!delegate()->peerListFindRow(PeerListRowId(row.bot->id.value))) {
		delegate()->peerListPrependRow(createRow(row));
		refreshRows();
	}
}

void ListController::refreshRows() {
	delegate()->peerListRefreshRows();
	_rowCount = delegate()->peerListFullRowsCount();
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	const auto bot = row->peer()->asUser();
	const auto i = _states.find(bot);
	Assert(i != end(_states));
	if (i->second.unresolved) {
		if (!_resolving.emplace(bot).second) {
			return;
		}
		_openOnResolve = bot;
		const auto resolved = [=](std::optional<ConnectedBotState> state) {
			_resolving.remove(bot);
			auto &now = _states[bot];
			if (state) {
				now = *state;
			}
			if (_openOnResolve == bot) {
				open(bot, now);
			}
		};
		Resolve(_peer, bot, crl::guard(this, resolved));
	} else {
		_openOnResolve = nullptr;
		open(bot, i->second);
	}
}

void ListController::open(not_null<UserData*> bot, ConnectedBotState state) {
	const auto show = _controller->uiShow();
	if (_type == JoinType::Joined
		|| (!state.link.isEmpty() && !state.revoked)) {
		_recipientsReady = nullptr;
		show->show(StarRefLinkBox({ bot, state }, _peer));
	} else {
		const auto requireOthers = (_type == JoinType::Existing)
			|| _peer->isSelf();
		const auto requestOthers = requireOthers && _recipients.empty();
		if (requestOthers) {
			_recipientsReady = [=] {
				Expects(!_recipients.empty());

				open(bot, state);
			};
			requestRecipients();
			return;
		}
		const auto connected = crl::guard(this, [=](ConnectedBotState now) {
			_states[bot] = now;
			_connected.fire({ bot, now });
		});
		show->show(JoinStarRefBox(
			{ bot, state },
			_peer,
			requireOthers ? _recipients : std::vector<not_null<PeerData*>>(),
			connected));
	}
}

void ListController::requestRecipients() {
	if (_recipientsRequested) {
		return;
	}
	_recipientsRequested = true;
	const auto session = &this->session();
	ResolveRecipients(session, crl::guard(this, [=](
			std::vector<not_null<PeerData*>> list) {
		_recipients = std::move(list);
		if (const auto callback = base::take(_recipientsReady)) {
			callback();
		}
	}));
}

void RevokeLink(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &link,
		Fn<void()> revoked) {
	peer->session().api().request(MTPpayments_EditConnectedStarRefBot(
		MTP_flags(MTPpayments_EditConnectedStarRefBot::Flag::f_revoked),
		peer->input,
		MTP_string(link)
	)).done([=] {
		controller->showToast({
			.title = tr::lng_star_ref_revoked_title(tr::now),
			.text = { tr::lng_star_ref_revoked_text(tr::now) },
		});
		revoked();
	}).fail([=](const MTP::Error &error) {
		controller->showToast(u"Failed: "_q + error.type());
	}).send();
}

base::unique_qptr<Ui::PopupMenu> ListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto bot = row->peer()->asUser();
	const auto i = _states.find(bot);
	Assert(i != end(_states));
	const auto state = i->second;
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(result.get());

	const auto revoked = crl::guard(this, [=] {
		if (const auto row = delegate()->peerListFindRow(bot->id.value)) {
			delegate()->peerListRemoveRow(row);
			refreshRows();
		}
		_revoked.fire({ bot, state });
	});

	addAction(tr::lng_star_ref_list_my_open(tr::now), [=] {
		_controller->showPeerHistory(bot);
	}, &st::menuIconBot);
	if (!state.link.isEmpty()) {
		addAction(tr::lng_star_ref_list_my_copy(tr::now), [=] {
			QApplication::clipboard()->setText(state.link);
			_controller->showToast(tr::lng_username_copied(tr::now));
		}, &st::menuIconLinks);
		const auto revoke = [=] {
			const auto link = state.link;
			const auto sure = [=](Fn<void()> close) {
				RevokeLink(_controller, _peer, link, revoked);
				close();
			};
			_controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_star_ref_revoke_text(
					lt_bot,
					rpl::single(Ui::Text::Bold(bot->name())),
					Ui::Text::RichLangValue),
				.confirmed = sure,
				.title = tr::lng_star_ref_revoke_title(),
			}));
		};
		addAction({
			.text = tr::lng_star_ref_list_my_leave(tr::now),
			.handler = revoke,
			.icon = &st::menuIconLeaveAttention,
			.isAttention = true,
		});
	}
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
	not_null<ListController*> setupMy();
	not_null<ListController*> setupSuggested();
	void setupSort(not_null<Ui::RpWidget*> label);

	[[nodiscard]] object_ptr<Ui::RpWidget> infoRow(
		rpl::producer<QString> title,
		rpl::producer<QString> text,
		not_null<const style::icon*> icon);

	const not_null<Controller*> _controller;
	const not_null<Ui::VerticalLayout*> _container;
	rpl::variable<SuggestedSort> _sort = SuggestedSort::Profitability;
	ListController *_my = nullptr;
	ListController *_suggested = nullptr;

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
	_my = setupMy();
	_suggested = setupSuggested();
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

not_null<ListController*> InnerWidget::setupMy() {
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
		_controller->parentController(),
		peer(),
		JoinType::Joined);
	const auto content = inner->add(
		object_ptr<PeerListContent>(
			inner,
			controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(inner);
	Ui::AddDivider(inner);

	wrap->toggleOn(controller->rowCountValue(
	) | rpl::map(rpl::mappers::_1 > 0));

	controller->revoked(
	) | rpl::start_with_next([=](ConnectedBot row) {
		_suggested->process(row);
	}, content->lifetime());

	return controller;
}

void InnerWidget::setupSort(not_null<Ui::RpWidget*> label) {
	constexpr auto phrase = [](SuggestedSort sort) {
		return (sort == SuggestedSort::Profitability)
			? tr::lng_star_ref_sort_profitability(tr::now)
			: (sort == SuggestedSort::Revenue)
			? tr::lng_star_ref_sort_revenue(tr::now)
			: tr::lng_star_ref_sort_date(tr::now);
	};
	const auto sort = Ui::CreateChild<Ui::FlatLabel>(
		label->parentWidget(),
		tr::lng_star_ref_sort_text(
			lt_sort,
			_sort.value() | rpl::map(phrase) | Ui::Text::ToLink(),
		Ui::Text::WithEntities),
		st::defaultFlatLabel);
	rpl::combine(
		label->geometryValue(),
		widthValue(),
		sort->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int outer, int sortWidth) {
		const auto skip = st::boxRowPadding.right();
		const auto top = geometry.y()
			+ st::defaultSubsectionTitle.style.font->ascent
			- st::defaultFlatLabel.style.font->ascent;
		sort->moveToLeft(outer - sortWidth - skip, top, outer);
	}, sort->lifetime());
	sort->setClickHandlerFilter([=](const auto &...) {
		const auto menu = Ui::CreateChild<Ui::PopupMenu>(
			sort,
			st::popupMenuWithIcons);
		const auto orders = {
			SuggestedSort::Profitability,
			SuggestedSort::Revenue,
			SuggestedSort::Date
		};
		for (const auto order : orders) {
			const auto chosen = (order == _sort.current());
			menu->addAction(phrase(order), crl::guard(this, [=] {
				_sort = order;
			}), chosen ? &st::mediaPlayerMenuCheck : nullptr);
		}
		menu->popup(sort->mapToGlobal(QPoint(0, 0)));
		return false;
	});
}

not_null<ListController*> InnerWidget::setupSuggested() {
	const auto wrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	const auto inner = wrap->entity();

	Ui::AddSkip(inner);
	const auto subtitle = Ui::AddSubsectionTitle(
		inner,
		tr::lng_star_ref_list_subtitle());
	setupSort(subtitle);

	const auto delegate = lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	auto controller = lifetime().make_state<ListController>(
		_controller->parentController(),
		peer(),
		JoinType::Suggested);
	const auto content = inner->add(
		object_ptr<PeerListContent>(
			inner,
			controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	wrap->toggleOn(controller->rowCountValue(
	) | rpl::map(rpl::mappers::_1 > 0));

	controller->connected(
	) | rpl::start_with_next([=](ConnectedBot row) {
		_my->process(row);
	}, content->lifetime());

	_sort.value() | rpl::start_with_next([=](SuggestedSort sort) {
		controller->setSort(sort);
	}, content->lifetime());

	return controller;
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
		_back->entity()->setRippleColorOverride(
			&st::universalRippleAnimation.color);
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
			_close->setRippleColorOverride(
				&st::universalRippleAnimation.color);
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

bool Allowed(not_null<PeerData*> peer) {
	if (!peer->session().appConfig().starrefJoinAllowed()) {
		return false;
	} else if (const auto user = peer->asUser()) {
		return user->isSelf()
			|| (user->isBot() && user->botInfo->canEditInformation);
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() && channel->canPostMessages();
	}
	return false;
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer)));
}

object_ptr<Ui::BoxContent> ProgramsListBox(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	const auto initBox = [=](not_null<PeerListBox*> box) {
		*weak = box;
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	};

	auto controller = std::make_unique<ListController>(
		window,
		peer,
		JoinType::Existing);
	controller->addForBotRequests() | rpl::start_with_next([=] {
		if (const auto strong = weak->data()) {
			strong->closeBox();
		}
	}, controller->lifetime());

	return Box<PeerListBox>(std::move(controller), initBox);
}

} // namespace Info::BotStarRef::Join

