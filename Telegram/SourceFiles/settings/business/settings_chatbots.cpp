/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chatbots.h"

#include "apiwrap.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/business/data_business_chatbots.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kDebounceTimeout = crl::time(400);

enum class LookupState {
	Empty,
	Loading,
	Unsupported,
	Ready,
};

struct BotState {
	UserData *bot = nullptr;
	LookupState state = LookupState::Empty;
};

class Chatbots final : public BusinessSection<Chatbots> {
public:
	Chatbots(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Chatbots();

	[[nodiscard]] bool closeByOutsideClick() const override;
	[[nodiscard]] rpl::producer<QString> title() override;

	const Ui::RoundRect *bottomSkipRounding() const override {
		return &_bottomSkipRounding;
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	Ui::RoundRect _bottomSkipRounding;

	rpl::variable<Data::BusinessRecipients> _recipients;
	rpl::variable<QString> _usernameValue;
	rpl::variable<BotState> _botValue;
	rpl::variable<bool> _repliesAllowed = true;

};

class PreviewController final : public PeerListController {
public:
	PreviewController(not_null<PeerData*> peer, Fn<void()> resetBot);

	void prepare() override;
	void loadMoreRows() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	const not_null<PeerData*> _peer;
	const Fn<void()> _resetBot;
	rpl::lifetime _lifetime;

};

class PreviewRow final : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

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
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

QSize PreviewRow::rightActionSize() const {
	return QSize(
		st::settingsChatbotsDeleteIcon.width(),
		st::settingsChatbotsDeleteIcon.height()) * 2;
}

QMargins PreviewRow::rightActionMargins() const {
	const auto itemHeight = st::peerListSingleRow.item.height;
	const auto skip = (itemHeight - rightActionSize().height()) / 2;
	return QMargins(0, skip, skip, 0);
}

void PreviewRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_actionRipple) {
		_actionRipple->paint(
			p,
			x,
			y,
			outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	const auto rect = QRect(QPoint(x, y), PreviewRow::rightActionSize());
	(actionSelected
		? st::settingsChatbotsDeleteIconOver
		: st::settingsChatbotsDeleteIcon).paintInCenter(p, rect);
}

void PreviewRow::rightActionAddRipple(
	QPoint point,
	Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::EllipseMask(rightActionSize());
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
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
	not_null<PeerData*> peer,
	Fn<void()> resetBot)
: _peer(peer)
, _resetBot(std::move(resetBot)) {
}

void PreviewController::prepare() {
	delegate()->peerListAppendRow(std::make_unique<PreviewRow>(_peer));
	delegate()->peerListRefreshRows();
}

void PreviewController::loadMoreRows() {
}

void PreviewController::rowClicked(not_null<PeerListRow*> row) {
}

void PreviewController::rowRightActionClicked(not_null<PeerListRow*> row) {
	_resetBot();
}

Main::Session &PreviewController::session() const {
	return _peer->session();
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
		field->changes() | rpl::start_with_next([=] {
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

[[nodiscard]] QString ExtractUsername(QString text) {
	text = text.trimmed();
	static const auto expression = QRegularExpression(
		"^(https://)?([a-zA-Z0-9\\.]+/)?([a-zA-Z0-9_\\.]+)");
	const auto match = expression.match(text);
	return match.hasMatch() ? match.captured(3) : text;
}

[[nodiscard]] rpl::producer<BotState> LookupBot(
		not_null<Main::Session*> session,
		rpl::producer<QString> usernameChanges) {
	using Cache = base::flat_map<QString, UserData*>;
	const auto cache = std::make_shared<Cache>();
	return std::move(
		usernameChanges
	) | rpl::map([=](const QString &username) -> rpl::producer<BotState> {
		const auto extracted = ExtractUsername(username);
		const auto owner = &session->data();
		static const auto expression = QRegularExpression(
			"^[a-zA-Z0-9_\\.]+$");
		if (!expression.match(extracted).hasMatch()) {
			return rpl::single(BotState());
		} else if (const auto peer = owner->peerByUsername(extracted)) {
			if (const auto user = peer->asUser(); user && user->isBot()) {
				if (user->botInfo->supportsBusiness) {
					return rpl::single(BotState{
						.bot = user,
						.state = LookupState::Ready,
					});
				}
				return rpl::single(BotState{
					.state = LookupState::Unsupported,
				});
			}
			return rpl::single(BotState{
				.state = LookupState::Ready,
			});
		} else if (const auto i = cache->find(extracted); i != end(*cache)) {
			return rpl::single(BotState{
				.bot = i->second,
				.state = LookupState::Ready,
			});
		}

		return [=](auto consumer) {
			auto result = rpl::lifetime();

			const auto requestId = result.make_state<mtpRequestId>();
			*requestId = session->api().request(MTPcontacts_ResolveUsername(
				MTP_string(extracted)
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				const auto &data = result.data();
				session->data().processUsers(data.vusers());
				session->data().processChats(data.vchats());
				const auto peerId = peerFromMTP(data.vpeer());
				const auto peer = session->data().peer(peerId);
				if (const auto user = peer->asUser()) {
					if (user->isBot()) {
						cache->emplace(extracted, user);
						consumer.put_next(BotState{
							.bot = user,
							.state = LookupState::Ready,
						});
						return;
					}
				}
				cache->emplace(extracted, nullptr);
				consumer.put_next(BotState{ .state = LookupState::Ready });
			}).fail([=] {
				cache->emplace(extracted, nullptr);
				consumer.put_next(BotState{ .state = LookupState::Ready });
			}).send();

			result.add([=] {
				session->api().request(*requestId).cancel();
			});
			return result;
		};
	}) | rpl::flatten_latest();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeBotPreview(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<BotState> state,
		Fn<void()> resetBot) {
	auto result = object_ptr<Ui::SlideWrap<>>(
		parent.get(),
		object_ptr<Ui::RpWidget>(parent.get()));
	const auto raw = result.data();
	const auto inner = raw->entity();
	raw->hide(anim::type::instant);

	const auto child = inner->lifetime().make_state<Ui::RpWidget*>(nullptr);
	std::move(state) | rpl::filter([=](BotState state) {
		return state.state != LookupState::Loading;
	}) | rpl::start_with_next([=](BotState state) {
		raw->toggle(
			(state.state == LookupState::Ready
				|| state.state == LookupState::Unsupported),
			anim::type::normal);
		if (state.bot) {
			const auto delegate = parent->lifetime().make_state<
				PeerListContentDelegateSimple
			>();
			const auto controller = parent->lifetime().make_state<
				PreviewController
			>(state.bot, resetBot);
			controller->setStyleOverrides(&st::peerListSingleRow);
			const auto content = Ui::CreateChild<PeerListContent>(
				inner,
				controller);
			delegate->setContent(content);
			controller->setDelegate(delegate);
			delete base::take(*child);
			*child = content;
		} else if (state.state == LookupState::Ready
			|| state.state == LookupState::Unsupported) {
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
			) | rpl::start_with_next([=](QSize size, QSize inner) {
				label->move(
					(size.width() - inner.width()) / 2,
					(size.height() - inner.height()) / 2);
			}, label->lifetime());
			delete base::take(*child);
			*child = content;
		} else {
			return;
		}
		(*child)->show();

		inner->widthValue() | rpl::start_with_next([=](int width) {
			(*child)->resizeToWidth(width);
		}, (*child)->lifetime());

		(*child)->heightValue() | rpl::start_with_next([=](int height) {
			inner->resize(inner->width(), height + st::contactSkip);
		}, inner->lifetime());
	}, inner->lifetime());

	raw->finishAnimating();
	return result;
}

Chatbots::Chatbots(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller)
, _bottomSkipRounding(st::boxRadius, st::boxDividerBg) {
	setupContent(controller);
}

Chatbots::~Chatbots() {
	if (!Core::Quitting()) {
		save();
	}
}

bool Chatbots::closeByOutsideClick() const {
	return false;
}

rpl::producer<QString> Chatbots::title() {
	return tr::lng_chatbots_title();
}

void Chatbots::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto current = controller->session().data().chatbots().current();

	_recipients = Data::BusinessRecipients::MakeValid(current.recipients);
	_repliesAllowed = current.repliesAllowed;

	AddDividerTextWithLottie(content, {
		.lottie = u"robot"_q,
		.lottieSize = st::settingsCloudPasswordIconSize,
		.lottieMargins = st::peerAppearanceIconPadding,
		.showFinished = showFinishes(),
		.about = tr::lng_chatbots_about(
			lt_link,
			tr::lng_chatbots_about_link(
			) | Ui::Text::ToLink(tr::lng_chatbots_info_url(tr::now)),
			Ui::Text::WithEntities),
		.aboutMargins = st::peerAppearanceCoverLabelMargin,
	});

	const auto username = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::settingsChatbotsUsername,
			tr::lng_chatbots_placeholder(),
			(current.bot
				? current.bot->session().createInternalLink(
					current.bot->username())
				: QString())),
		st::settingsChatbotsUsernameMargins);

	_usernameValue = DebouncedValue(username);
	_botValue = rpl::single(BotState{
		current.bot,
		current.bot ? LookupState::Ready : LookupState::Empty
	}) | rpl::then(
		LookupBot(&controller->session(), _usernameValue.changes())
	);

	const auto resetBot = [=] {
		username->setText(QString());
		username->setFocus();
	};
	content->add(object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		content,
		MakeBotPreview(content, _botValue.value(), resetBot)));

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_add_about(),
		st::peerAppearanceDividerTextMargin);

	AddBusinessRecipientsSelector(content, {
		.controller = controller,
		.title = tr::lng_chatbots_access_title(),
		.data = &_recipients,
		.type = Data::BusinessRecipientsType::Bots,
	});

	Ui::AddSkip(content, st::settingsChatbotsAccessSkip);
	Ui::AddDividerText(
		content,
		tr::lng_chatbots_exclude_about(),
		st::peerAppearanceDividerTextMargin);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_chatbots_permissions_title());
	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_chatbots_reply(),
		st::settingsButtonNoIcon
	))->toggleOn(_repliesAllowed.value())->toggledChanges(
	) | rpl::start_with_next([=](bool value) {
		_repliesAllowed = value;
	}, content->lifetime());
	Ui::AddSkip(content);

	Ui::AddDividerText(
		content,
		tr::lng_chatbots_reply_about(),
		st::settingsChatbotsBottomTextMargin,
		RectPart::Top);

	Ui::ResizeFitChild(this, content);
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
	controller()->session().data().chatbots().save({
		.bot = _botValue.current().bot,
		.recipients = _recipients.current(),
		.repliesAllowed = _repliesAllowed.current(),
	}, [=] {
	}, fail);
}

} // namespace

Type ChatbotsId() {
	return Chatbots::Id();
}

} // namespace Settings
