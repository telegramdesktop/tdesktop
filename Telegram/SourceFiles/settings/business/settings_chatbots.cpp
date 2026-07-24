/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chatbots.h"

#include "apiwrap.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/peer_list_box.h"
#include "config.h"
#include "core/application.h"
#include "data/business/data_business_chatbots.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kDebounceTimeout = crl::time(400);
constexpr auto kMaxSearchBots = 3;

enum class LookupState {
	Empty,
	Loading,
	Unsupported,
	Ready,
};

struct BotSearchState {
	std::vector<UserData*> bots;
	LookupState state = LookupState::Empty;
	bool exactUnsupported = false;
};

enum class PreviewActionKind {
	None,
	Add,
};

[[nodiscard]] constexpr Data::ChatbotsPermissions Defaults() {
	return Data::ChatbotsPermission::ViewMessages;
}

class Chatbots final : public Section<Chatbots> {
public:
	Chatbots(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Chatbots();

	[[nodiscard]] bool closeByOutsideClick() const override;
	void checkBeforeClose(Fn<void()> close) override;
	[[nodiscard]] rpl::producer<QString> title() override;
	void setInnerFocus() override;

	const Ui::RoundRect *bottomSkipRounding() const override;

private:
	[[nodiscard]] bool shouldConfirmLeaveWithoutAddedBot() const;
	void setupContent();
	void refreshDetails();
	void save();

	Ui::RoundRect _bottomSkipRounding;

	Ui::VerticalLayout *_detailsWrap = nullptr;
	Ui::VerticalLayout *_permissionsWrap = nullptr;
	Ui::SlideWrap<Ui::InputField> *_usernameWrap = nullptr;

	rpl::variable<Data::BusinessRecipients> _recipients;
	rpl::variable<UserData*> _committedBot;
	Data::BusinessRecipients _committedRecipients;
	Data::ChatbotsPermissions _committedPermissions = Defaults();
	rpl::variable<QString> _usernameValue;
	rpl::variable<BotSearchState> _lookupState;
	rpl::variable<bool> _chooserVisible = false;
	rpl::variable<Data::ChatbotsPermissions> _permissions = Defaults();
	Fn<Data::ChatbotsPermissions()> _resolvePermissions;

};

class PreviewController final : public PeerListController {
public:
	PreviewController(
		std::vector<UserData*> bots,
		UserData *committedBot,
		Fn<void(UserData*)> addBot);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const std::vector<UserData*> _bots;
	UserData *const _committedBot = nullptr;
	const Fn<void(UserData*)> _addBot;
	rpl::lifetime _lifetime;

};

class PreviewRow final : public PeerListRow {
public:
	PreviewRow(not_null<PeerData*> peer, PreviewActionKind kind);

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;
	void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) override;
	void rightActionStopLastRipple() override;

private:
	[[nodiscard]] QSize addPillSize() const;

	PreviewActionKind _kind = PreviewActionKind::None;
	QString _addText;
	int _addTextWidth = 0;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

PreviewRow::PreviewRow(
	not_null<PeerData*> peer,
	PreviewActionKind kind)
: PeerListRow(peer)
, _kind(kind)
, _addText((_kind == PreviewActionKind::Add)
	? tr::lng_chatbots_add(tr::now)
	: QString())
, _addTextWidth((_kind == PreviewActionKind::Add)
	? st::settingsChatbotsAddButton.style.font->width(_addText)
	: 0) {
	const auto username = peer->username();
	if (!username.isEmpty()) {
		setCustomStatus('@' + username);
	}
}

QSize PreviewRow::addPillSize() const {
	const auto &st = st::settingsChatbotsAddButton;
	const auto width = _addTextWidth - st.width;
	return QSize(std::max(width, st.height), st.height);
}

QSize PreviewRow::rightActionSize() const {
	return (_kind == PreviewActionKind::Add) ? addPillSize() : QSize();
}

QMargins PreviewRow::rightActionMargins() const {
	if (_kind == PreviewActionKind::None) {
		return QMargins();
	}
	const auto itemHeight = st::peerListSingleRow.item.height;
	const auto size = rightActionSize();
	const auto skipV = (itemHeight - size.height()) / 2;
	return QMargins(0, skipV, st::settingsChatbotsAddMargin, 0);
}

void PreviewRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_kind == PreviewActionKind::None) {
		return;
	}
	const auto size = rightActionSize();
	if (_kind == PreviewActionKind::Add) {
		const auto &st = st::settingsChatbotsAddButton;
		const auto rect = QRect(QPoint(x, y), size);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(actionSelected ? st.textBgOver : st.textBg);
		const auto radius = size.height() / 2.;
		p.drawRoundedRect(rect, radius, radius);
		if (_actionRipple) {
			_actionRipple->paint(p, x, y, outerWidth);
			if (_actionRipple->empty()) {
				_actionRipple.reset();
			}
		}
		p.setPen(actionSelected ? st.textFgOver : st.textFg);
		p.setFont(st.style.font);
		p.drawText(rect, Qt::AlignCenter, _addText);
	}
}

void PreviewRow::rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) {
	if (_kind == PreviewActionKind::None) {
		return;
	}
	if (!_actionRipple) {
		const auto size = rightActionSize();
		auto mask = Ui::RippleAnimation::RoundRectMask(
			size,
			size.height() / 2);
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::settingsChatbotsAddButton.ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point);
}

void PreviewRow::rightActionStopLastRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

PreviewController::PreviewController(
	std::vector<UserData*> bots,
	UserData *committedBot,
	Fn<void(UserData*)> addBot)
: _bots(std::move(bots))
, _committedBot(committedBot)
, _addBot(std::move(addBot)) {
}

void PreviewController::prepare() {
	for (const auto bot : _bots) {
		const auto kind = (bot == _committedBot)
			? PreviewActionKind::None
			: PreviewActionKind::Add;
		delegate()->peerListAppendRow(
			std::make_unique<PreviewRow>(bot, kind));
	}
	delegate()->peerListRefreshRows();
}

void PreviewController::loadMoreRows() {
}

void PreviewController::rowClicked(not_null<PeerListRow*> row) {
}

void PreviewController::rowRightActionClicked(
		not_null<PeerListRow*> row) {
	const auto user = row->peer()->asUser();
	if (user && (user != _committedBot)) {
		_addBot(user);
	}
}

Main::Session &PreviewController::session() const {
	return _bots.front()->session();
}

[[nodiscard]] rpl::producer<QString> DebouncedValue(
		not_null<Ui::InputField*> field) {
	return [=](auto consumer) {

		auto result = rpl::lifetime();
		struct State {
			base::Timer timer;
			QString lastText;
		};
		const auto state = result.make_state<State>();
		const auto push = [=] {
			state->timer.cancel();
			consumer.put_next_copy(state->lastText);
		};
		state->timer.setCallback(push);
		state->lastText = field->getLastText();
		consumer.put_next_copy(field->getLastText());
		field->changes() | rpl::on_next([=] {
			const auto &text = field->getLastText();
			const auto was = std::exchange(state->lastText, text);
			if (std::abs(int(text.size()) - int(was.size())) == 1) {
				state->timer.callOnce(kDebounceTimeout);
			} else {
				push();
			}
		}, result);
		return result;
	};
}

struct ParsedUsernameQuery {
	QString value;
	bool valid = false;
};

[[nodiscard]] ParsedUsernameQuery ParseUsernameQuery(QString text) {
	text = text.trimmed();
	if (text.startsWith(QChar('@'))) {
		text = text.mid(1);
	} else {
		static const auto extractExpression = QRegularExpression(
			"^(https://)?([a-zA-Z0-9\\.]+/)?([a-zA-Z0-9_\\.]+)");
		const auto match = extractExpression.match(text);
		text = match.hasMatch() ? match.captured(3) : text;
	}
	static const auto validateExpression = QRegularExpression(
		"^[a-zA-Z0-9_\\.]+$");
	return {
		.value = text,
		.valid = !text.isEmpty()
			&& validateExpression.match(text).hasMatch(),
	};
}

[[nodiscard]] bool IsUsableBusinessBot(UserData *user) {
	return user
		&& user->isBot()
		&& user->botInfo->supportsBusiness;
}

[[nodiscard]] UserData *FirstBot(const BotSearchState &state) {
	return state.bots.empty() ? nullptr : state.bots.front();
}

[[nodiscard]] BotSearchState SingleBotState(UserData *bot) {
	auto result = BotSearchState{
		.state = bot ? LookupState::Ready : LookupState::Empty,
	};
	if (bot) {
		result.bots.push_back(bot);
	}
	return result;
}

[[nodiscard]] std::pair<BotSearchState, UserData*> PreviewState(
		BotSearchState lookupState,
		UserData *committed,
		bool chooserVisible,
		const QString &usernameText) {
	if (committed && !chooserVisible) {
		return { SingleBotState(committed), committed };
	}
	if (chooserVisible && ParseUsernameQuery(usernameText).valid) {
		return { std::move(lookupState), nullptr };
	}
	return { BotSearchState(), nullptr };
}

void AppendUniqueBot(
		std::vector<UserData*> &bots,
		UserData *user) {
	if (!IsUsableBusinessBot(user)
		|| ranges::contains(bots, user)
		|| bots.size() >= kMaxSearchBots) {
		return;
	}
	bots.push_back(user);
}

[[nodiscard]] UserData *UserFromPeer(
		not_null<Main::Session*> session,
		const MTPPeer &peer) {
	return session->data().peer(peerFromMTP(peer))->asUser();
}

struct BotSearchRequest {
	UserData *exact = nullptr;
	std::vector<UserData*> my;
	std::vector<UserData*> global;
	mtpRequestId exactRequestId = 0;
	mtpRequestId searchRequestId = 0;
	bool exactUnsupported = false;
	bool exactDone = false;
	bool searchDone = false;
};

[[nodiscard]] BotSearchState MakeReadySearchState(
		const BotSearchRequest &request) {
	auto bots = std::vector<UserData*>();
	AppendUniqueBot(bots, request.exact);
	for (const auto user : request.my) {
		AppendUniqueBot(bots, user);
	}
	for (const auto user : request.global) {
		AppendUniqueBot(bots, user);
	}
	const auto unsupported = bots.empty()
		&& request.exactUnsupported;
	return {
		.bots = std::move(bots),
		.state = unsupported ? LookupState::Unsupported : LookupState::Ready,
		.exactUnsupported = request.exactUnsupported,
	};
}

void AppendUsersFromPeerList(
		not_null<Main::Session*> session,
		const MTPVector<MTPPeer> &list,
		std::vector<UserData*> &users) {
	users.reserve(users.size() + list.v.size());
	for (const auto &mtpPeer : list.v) {
		if (const auto user = UserFromPeer(session, mtpPeer)) {
			users.push_back(user);
		}
	}
}

[[nodiscard]] rpl::producer<BotSearchState> LookupBots(
		not_null<Main::Session*> session,
		rpl::producer<QString> usernameChanges) {
	return std::move(
		usernameChanges
	) | rpl::map([=](
			const QString &username) -> rpl::producer<BotSearchState> {
		const auto query = ParseUsernameQuery(username);
		if (!query.valid) {
			return rpl::single(BotSearchState());
		}
		const auto extracted = query.value;

		return [=](auto consumer) {
			auto result = rpl::lifetime();

			const auto data = result.make_state<BotSearchRequest>();
			const auto finish = [=] {
				if (!data->exactDone || !data->searchDone) {
					return;
				}
				consumer.put_next(MakeReadySearchState(*data));
			};

			consumer.put_next(BotSearchState{
				.state = LookupState::Loading,
			});

			data->exactRequestId = session->api().request(MTPcontacts_ResolveUsername(
				MTP_flags(0),
				MTP_string(extracted),
				MTP_string()
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				const auto &resolved = result.data();
				session->data().processUsers(resolved.vusers());
				session->data().processChats(resolved.vchats());
				if (const auto user = UserFromPeer(session, resolved.vpeer())) {
					if (IsUsableBusinessBot(user)) {
						data->exact = user;
					} else if (user->isBot()) {
						data->exactUnsupported = true;
					}
				}
				data->exactDone = true;
				finish();
			}).fail([=] {
				data->exactDone = true;
				finish();
			}).send();
			data->searchRequestId = session->api().request(MTPcontacts_Search(
				MTP_flags(0),
				MTP_string(extracted),
				MTP_int(SearchPeopleLimit)
			)).done([=](const MTPcontacts_Found &result) {
				const auto &found = result.data();
				session->data().processUsers(found.vusers());
				session->data().processChats(found.vchats());
				AppendUsersFromPeerList(session, found.vmy_results(), data->my);
				AppendUsersFromPeerList(session, found.vresults(), data->global);
				data->searchDone = true;
				finish();
			}).fail([=] {
				data->searchDone = true;
				finish();
			}).send();

			result.add([=] {
				session->api().request(data->exactRequestId).cancel();
				session->api().request(data->searchRequestId).cancel();
			});
			return result;
		};
	}) | rpl::flatten_latest();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeBotPreview(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::pair<BotSearchState, UserData*>> stateAndBot,
		Fn<void(UserData*)> addBot) {
	auto result = object_ptr<Ui::SlideWrap<>>(
		parent.get(),
		object_ptr<Ui::RpWidget>(parent.get()));
	const auto raw = result.data();
	const auto inner = raw->entity();
	raw->hide(anim::type::instant);

	const auto child = inner->lifetime().make_state<Ui::RpWidget*>(nullptr);
	std::move(stateAndBot) | rpl::on_next([=](
			std::pair<BotSearchState, UserData*> pair) {
		const auto &state = pair.first;
		const auto committed = pair.second;
		const auto hasBots = !state.bots.empty();
		const auto hasLabel = !hasBots
			&& (state.state == LookupState::Ready
				|| state.state == LookupState::Unsupported);
		raw->toggle(hasBots || hasLabel, anim::type::normal);
		if (hasBots) {
			const auto delegate = parent->lifetime().make_state<
				PeerListContentDelegateSimple
			>();
			const auto controller = parent->lifetime().make_state<
				PreviewController
			>(state.bots, committed, addBot);
			controller->setStyleOverrides(&st::peerListSingleRow);
			const auto content = Ui::CreateChild<PeerListContent>(
				inner,
				controller);
			delegate->setContent(content);
			controller->setDelegate(delegate);
			delete base::take(*child);
			*child = content;
		} else if (hasLabel) {
			const auto content = Ui::CreateChild<Ui::RpWidget>(inner);
			const auto label = Ui::CreateChild<Ui::FlatLabel>(
				content,
				(state.state == LookupState::Unsupported
					? tr::lng_chatbots_not_supported()
					: tr::lng_chatbots_not_found()),
				st::settingsChatbotsNotFound);
			content->resize(
				inner->width(),
				st::peerListSingleRow.item.height);
			rpl::combine(
				content->sizeValue(),
				label->sizeValue()
			) | rpl::on_next([=](QSize size, QSize inner) {
				label->move(
					(size.width() - inner.width()) / 2,
					(size.height() - inner.height()) / 2);
			}, label->lifetime());
			delete base::take(*child);
			*child = content;
		} else {
			delete base::take(*child);
			return;
		}
		(*child)->show();

		inner->widthValue() | rpl::on_next([=](int width) {
			(*child)->resizeToWidth(width);
		}, (*child)->lifetime());

		(*child)->heightValue() | rpl::on_next([=](int height) {
			inner->resize(inner->width(), height + st::contactSkip);
		}, (*child)->lifetime());
	}, inner->lifetime());

	raw->finishAnimating();
	return result;
}

Chatbots::Chatbots(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent();
}

Chatbots::~Chatbots() {
	if (!Core::Quitting()) {
		save();
	}
}

bool Chatbots::shouldConfirmLeaveWithoutAddedBot() const {
	if (!_chooserVisible.current() || _committedBot.current()) {
		return false;
	}
	const auto parsed = ParseUsernameQuery(_usernameValue.current());
	const auto &current = _lookupState.current();
	return parsed.valid
		&& !current.bots.empty();
}

bool Chatbots::closeByOutsideClick() const {
	return false;
}

void Chatbots::checkBeforeClose(Fn<void()> close) {
	if (!shouldConfirmLeaveWithoutAddedBot()) {
		close();
		return;
	}
	controller()->show(Ui::MakeConfirmBox({
		.text = tr::lng_chatbots_leave_without_added_text(),
		.confirmed = crl::guard(this, [=](Fn<void()> closeBox) {
			closeBox();
			close();
		}),
		.confirmText = tr::lng_box_leave(),
		.cancelText = tr::lng_cancel(),
		.title = tr::lng_chatbots_leave_without_added_title(),
	}));
}

rpl::producer<QString> Chatbots::title() {
	return tr::lng_chat_automation_title();
}

void Chatbots::setInnerFocus() {
	if (_chooserVisible.current()) {
		_usernameWrap->entity()->setFocus();
	} else {
		AbstractSection::setInnerFocus();
	}
}

const Ui::RoundRect *Chatbots::bottomSkipRounding() const {
	return _permissionsWrap->count() ? nullptr : &_bottomSkipRounding;
}

void Chatbots::setupContent() {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto current = controller()->session().data().chatbots().current();

	_recipients = Data::BusinessRecipients::MakeValid(current.recipients);
	_permissions = current.permissions;
	_committedBot = current.bot;
	_committedRecipients = _recipients.current();
	_committedPermissions = _permissions.current();

	AddDividerTextWithLottie(content, {
		.lottie = u"settings/chat_automation"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_chat_automation_about(tr::marked),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	const auto usernameWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			content,
			object_ptr<Ui::InputField>(
				content,
				st::settingsChatbotsUsername,
				tr::lng_chatbots_placeholder(),
				QString()),
			st::settingsChatbotsUsernameMargins),
		QMargins(0, st::settingsChatbotsUsernameMargins.bottom(), 0, 0));
	usernameWrap->setDuration(0);
	_usernameWrap = usernameWrap;
	_chooserVisible = !current.bot;
	_usernameWrap->toggle(_chooserVisible.current(), anim::type::instant);
	const auto username = usernameWrap->entity();

	_usernameValue = rpl::single(
		username->getLastText()
	) | rpl::then(
		username->changes() | rpl::map([=] {
			return username->getLastText();
		}));
	_lookupState = LookupBots(&controller()->session(), DebouncedValue(username)
	);

	const auto resetBot = [=] {
		username->setText(QString());
		_committedBot = nullptr;
		_recipients = Data::BusinessRecipients::MakeValid({});
		_permissions = Defaults();
		_committedRecipients = _recipients.current();
		_committedPermissions = _permissions.current();
		_chooserVisible = true;
		_usernameWrap->toggle(true, anim::type::instant);
		username->setFocus();
	};
	const auto addBot = [=](UserData *resolved) {
		if (!resolved) {
			return;
		}
		_committedRecipients = _recipients.current();
		_committedPermissions = _resolvePermissions();
		_permissions = _committedPermissions;
		_committedBot = resolved;
		_chooserVisible = false;
		_usernameWrap->toggle(false, anim::type::instant);
		controller()->showToast(Ui::Toast::Config{
			.text = { tr::lng_chatbots_added_success(
				tr::now,
				lt_bot,
				resolved->name()) },
			.icon = &st::toastCheckIcon,
		});
	};
	auto stateAndBot = rpl::combine(
		_lookupState.value(),
		_committedBot.value(),
		_chooserVisible.value(),
		_usernameValue.value()
	) | rpl::map([](
			BotSearchState lookupState,
			UserData *committed,
			bool chooserVisible,
			const QString &usernameText) {
		return PreviewState(
			std::move(lookupState),
			committed,
			chooserVisible,
			usernameText);
	});

	content->add(object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		content,
		MakeBotPreview(
			content,
			std::move(stateAndBot),
			addBot)));

	Ui::AddDividerText(
		content,
		tr::lng_chat_automation_add_about(),
		st::peerAppearanceDividerTextMargin);

	_detailsWrap = content->add(object_ptr<Ui::VerticalLayout>(content));

	AddBusinessRecipientsSelector(_detailsWrap, {
		.controller = controller(),
		.title = tr::lng_chatbots_access_title(),
		.data = &_recipients,
		.type = Data::BusinessRecipientsType::Bots,
	});

	Ui::AddSkip(_detailsWrap, st::settingsChatbotsAccessSkip);
	Ui::AddDividerText(
		_detailsWrap,
		tr::lng_chatbots_exclude_about(),
		st::peerAppearanceDividerTextMargin,
		st::defaultDividerLabel,
		RectPart::Top);

	_permissionsWrap = _detailsWrap->add(
		object_ptr<Ui::VerticalLayout>(_detailsWrap));

	const auto removeWrap = _detailsWrap->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_detailsWrap,
			object_ptr<Ui::VerticalLayout>(_detailsWrap)));
	const auto removeInner = removeWrap->entity();
	Ui::AddDivider(removeInner);
	Ui::AddSkip(removeInner);
	const auto remove = removeInner->add(
		CreateButtonWithIcon(
			removeInner,
			tr::lng_chatbots_remove_bot(),
			st::settingsChatbotsRemove,
			{ &st::settingsChatbotsRemoveIcon }));
	remove->setClickedCallback(resetBot);
	removeWrap->toggleOn(
		_committedBot.value() | rpl::map(_1 != nullptr),
		anim::type::instant);
	removeWrap->setDuration(0);

	refreshDetails();
	rpl::merge(
		_chooserVisible.changes() | rpl::to_empty,
		_committedBot.changes() | rpl::to_empty,
		_lookupState.changes() | rpl::to_empty,
		_usernameValue.changes() | rpl::to_empty
	) | rpl::on_next([=] {
		refreshDetails();
	}, lifetime());

	Ui::ResizeFitChild(this, content);
}

void Chatbots::refreshDetails() {
	_resolvePermissions = [=] {
		return Data::ChatbotsPermissions();
	};
	while (_permissionsWrap->count()) {
		delete _permissionsWrap->widgetAt(0);
	}

	auto bot = _committedBot.current();
	if (!bot && _chooserVisible.current()) {
		const auto parsed = ParseUsernameQuery(_usernameValue.current());
		if (parsed.valid) {
			bot = FirstBot(_lookupState.current());
		}
	}
	if (!bot) {
		_permissionsWrap->resizeToWidth(width());
		return;
	}

	const auto content = _permissionsWrap;
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_chatbots_permissions_title());

	auto permissions = CreateEditChatbotPermissions(
		content,
		_permissions.current());
	content->add(std::move(permissions.widget));
	_resolvePermissions = permissions.value;

	std::move(
		permissions.changes
	) | rpl::on_next([=](Data::ChatbotsPermissions now) {
		const auto warn = [&](tr::phrase<lngtag_bot> text) {
			controller()->show(Ui::MakeInformBox({
				.text = text(
					tr::now,
					lt_bot,
					tr::bold(bot->name()),
					tr::rich),
				.title = tr::lng_chatbots_warning_title(),
			}));
		};

		const auto was = _permissions.current();
		const auto diff = now ^ was;
		const auto enabled = diff & now;
		using Flag = Data::ChatbotsPermission;
		if (enabled & (Flag::TransferGifts | Flag::SellGifts)) {
			if (enabled & Flag::TransferStars) {
				warn(tr::lng_chatbots_warning_both_text);
			} else {
				warn(tr::lng_chatbots_warning_gifts_text);
			}
		} else if (enabled & Flag::TransferStars) {
			warn(tr::lng_chatbots_warning_stars_text);
		} else if (enabled & Flag::EditUsername) {
			warn(tr::lng_chatbots_warning_username_text);
		}
		_permissions = now;
	}, lifetime());

	Ui::AddSkip(content);

	_permissionsWrap->resizeToWidth(width());
}

void Chatbots::save() {
	const auto show = controller()->uiShow();
	const auto fail = [=](QString error) {
		if (error == u"BUSINESS_RECIPIENTS_EMPTY"_q) {
			show->showToast(tr::lng_greeting_recipients_empty(tr::now));
		} else if (error == u"BOT_BUSINESS_MISSING"_q) {
			show->showToast(tr::lng_chatbots_not_supported(tr::now));
		}
	};
	const auto bot = _committedBot.current();
	if (bot) {
		_committedRecipients = _recipients.current();
		_committedPermissions = _resolvePermissions();
	}
	controller()->session().data().chatbots().save({
		.bot = bot,
		.recipients = _committedRecipients,
		.permissions = _committedPermissions,
	}, [=] {
	}, fail);
}

} // namespace

Type ChatbotsId() {
	return Chatbots::Id();
}

} // namespace Settings
