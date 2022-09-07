/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_contact_status.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "main/main_session.h"
#include "base/unixtime.h"
#include "boxes/peers/edit_contact_box.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace HistoryView {
namespace {

bool BarCurrentlyHidden(not_null<PeerData*> peer) {
	const auto settings = peer->settings();
	if (!settings) {
		return false;
	} else if (!(*settings)) {
		return true;
	}
	if (const auto user = peer->asUser()) {
		if (user->isBlocked()) {
			return true;
		} else if (user->isContact()
			&& !((*settings) & PeerSetting::ShareContact)) {
			return true;
		}
	} else if (!((*settings) & PeerSetting::ReportSpam)) {
		return true;
	}
	return false;
}

} // namespace

class ContactStatus::BgButton final : public Ui::RippleButton {
public:
	BgButton(QWidget *parent, const style::FlatButton &st);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	const style::FlatButton &_st;

};

class ContactStatus::Bar final : public Ui::RpWidget {
public:
	Bar(QWidget *parent, const QString &name);

	void showState(State state);

	[[nodiscard]] rpl::producer<> unarchiveClicks() const;
	[[nodiscard]] rpl::producer<> addClicks() const;
	[[nodiscard]] rpl::producer<> blockClicks() const;
	[[nodiscard]] rpl::producer<> shareClicks() const;
	[[nodiscard]] rpl::producer<> reportClicks() const;
	[[nodiscard]] rpl::producer<> closeClicks() const;
	[[nodiscard]] rpl::producer<> requestInfoClicks() const;

private:
	int resizeGetHeight(int newWidth) override;

	QString _name;
	object_ptr<Ui::FlatButton> _add;
	object_ptr<Ui::FlatButton> _unarchive;
	object_ptr<Ui::FlatButton> _block;
	object_ptr<Ui::FlatButton> _share;
	object_ptr<Ui::FlatButton> _report;
	object_ptr<Ui::IconButton> _close;
	object_ptr<BgButton> _requestChatBg;
	object_ptr<Ui::FlatLabel> _requestChatInfo;

};

ContactStatus::BgButton::BgButton(
	QWidget *parent,
	const style::FlatButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
}

void ContactStatus::BgButton::onStateChanged(
		State was,
		StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

void ContactStatus::BgButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	p.fillRect(e->rect(), isOver() ? _st.overBgColor : _st.bgColor);
	paintRipple(p, 0, 0);
}

ContactStatus::Bar::Bar(
	QWidget *parent,
	const QString &name)
: RpWidget(parent)
, _name(name)
, _add(
	this,
	QString(),
	st::historyContactStatusButton)
, _unarchive(
	this,
	tr::lng_new_contact_unarchive(tr::now).toUpper(),
	st::historyContactStatusButton)
, _block(
	this,
	tr::lng_new_contact_block(tr::now).toUpper(),
	st::historyContactStatusBlock)
, _share(
	this,
	tr::lng_new_contact_share(tr::now).toUpper(),
	st::historyContactStatusButton)
, _report(
	this,
	QString(),
	st::historyContactStatusBlock)
, _close(this, st::historyReplyCancel)
, _requestChatBg(this, st::historyContactStatusButton)
, _requestChatInfo(
		this,
		QString(),
		st::historyContactStatusLabel) {
	_requestChatInfo->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ContactStatus::Bar::showState(State state) {
	using Type = State::Type;
	const auto type = state.type;
	_add->setVisible(type == Type::AddOrBlock || type == Type::Add);
	_unarchive->setVisible(type == Type::UnarchiveOrBlock
		|| type == Type::UnarchiveOrReport);
	_block->setVisible(type == Type::AddOrBlock
		|| type == Type::UnarchiveOrBlock);
	_share->setVisible(type == Type::SharePhoneNumber);
	_close->setVisible(type != Type::RequestChatInfo);
	_report->setVisible(type == Type::ReportSpam
		|| type == Type::UnarchiveOrReport);
	_requestChatInfo->setVisible(type == Type::RequestChatInfo);
	_requestChatBg->setVisible(type == Type::RequestChatInfo);
	_add->setText((type == Type::Add)
		? tr::lng_new_contact_add_name(tr::now, lt_user, _name).toUpper()
		: tr::lng_new_contact_add(tr::now).toUpper());
	_report->setText((type == Type::ReportSpam)
		? tr::lng_report_spam_and_leave(tr::now).toUpper()
		: tr::lng_report_spam(tr::now).toUpper());
	_requestChatInfo->setMarkedText(
		(state.requestChatIsBroadcast
			? tr::lng_new_contact_from_request_channel
			: tr::lng_new_contact_from_request_group)(
			tr::now,
			lt_user,
			Ui::Text::Bold(_name),
			lt_name,
			Ui::Text::Bold(state.requestChatName),
			Ui::Text::WithEntities));
	resizeToWidth(width());
}

rpl::producer<> ContactStatus::Bar::unarchiveClicks() const {
	return _unarchive->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::addClicks() const {
	return _add->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::blockClicks() const {
	return _block->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::shareClicks() const {
	return _share->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::reportClicks() const {
	return _report->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::closeClicks() const {
	return _close->clicks() | rpl::to_empty;
}

rpl::producer<> ContactStatus::Bar::requestInfoClicks() const {
	return _requestChatBg->clicks() | rpl::to_empty;
}

int ContactStatus::Bar::resizeGetHeight(int newWidth) {
	_close->moveToRight(0, 0);

	const auto closeWidth = _close->width();
	const auto available = newWidth - closeWidth;
	const auto skip = st::historyContactStatusMinSkip;
	if (available <= 2 * skip) {
		return _close->height();
	}
	const auto buttonWidth = [&](const object_ptr<Ui::FlatButton> &button) {
		return button->textWidth() + 2 * skip;
	};

	auto accumulatedLeft = 0;
	const auto placeButton = [&](
			const object_ptr<Ui::FlatButton> &button,
			int buttonWidth,
			int rightTextMargin = 0) {
		button->setGeometry(accumulatedLeft, 0, buttonWidth, height());
		button->setTextMargins({ 0, 0, rightTextMargin, 0 });
		accumulatedLeft += buttonWidth;
	};
	const auto placeOne = [&](const object_ptr<Ui::FlatButton> &button) {
		if (button->isHidden()) {
			return;
		}
		const auto thatWidth = buttonWidth(button);
		const auto margin = std::clamp(
			thatWidth + closeWidth - available,
			0,
			closeWidth);
		placeButton(button, newWidth, margin);
	};
	const auto &leftButton = _unarchive->isHidden() ? _add : _unarchive;
	const auto &rightButton = _block->isHidden() ? _report : _block;
	if (!leftButton->isHidden() && !rightButton->isHidden()) {
		const auto leftWidth = buttonWidth(leftButton);
		const auto rightWidth = buttonWidth(rightButton);
		const auto half = newWidth / 2;
		if (leftWidth <= half
			&& rightWidth + 2 * closeWidth <= newWidth - half) {
			placeButton(leftButton, half);
			placeButton(rightButton, newWidth - half);
		} else if (leftWidth + rightWidth <= available) {
			const auto margin = std::clamp(
				leftWidth + rightWidth + closeWidth - available,
				0,
				closeWidth);
			const auto realBlockWidth = rightWidth + 2 * closeWidth - margin;
			if (leftWidth > realBlockWidth) {
				placeButton(leftButton, leftWidth);
				placeButton(rightButton, newWidth - leftWidth, margin);
			} else {
				placeButton(leftButton, newWidth - realBlockWidth);
				placeButton(rightButton, realBlockWidth, margin);
			}
		} else {
			const auto forLeft = (available * leftWidth)
				/ (leftWidth + rightWidth);
			placeButton(leftButton, forLeft);
			placeButton(rightButton, newWidth - forLeft, closeWidth);
		}
	} else {
		placeOne(_add);
		placeOne(_share);
		placeOne(_report);
	}
	if (_requestChatInfo->isHidden()) {
		return _close->height();
	}
	const auto vskip = st::topBarArrowPadding.top();
	_requestChatInfo->resizeToWidth(available - 2 * skip);
	_requestChatInfo->move(skip, vskip);
	const auto newHeight = _requestChatInfo->height() + 2 * vskip;
	_requestChatBg->setGeometry(0, 0, newWidth, newHeight);
	return newHeight;
}

ContactStatus::ContactStatus(
	not_null<Window::SessionController*> window,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(window)
, _bar(parent, object_ptr<Bar>(parent, peer->shortName()))
, _shadow(parent) {
	setupWidgets(parent);
	setupState(peer);
	setupHandlers(peer);
}

void ContactStatus::setupWidgets(not_null<Ui::RpWidget*> parent) {
	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		_bar.resizeToWidth(width);
	}, _bar.lifetime());

	_bar.geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		_shadow.setGeometry(
			geometry.x(),
			geometry.y() + geometry.height(),
			geometry.width(),
			st::lineWidth);
	}, _shadow.lifetime());

	_bar.shownValue(
	) | rpl::start_with_next([=](bool shown) {
		_shadow.setVisible(shown);
	}, _shadow.lifetime());
}

auto ContactStatus::PeerState(not_null<PeerData*> peer)
-> rpl::producer<State> {
	using SettingsChange = PeerData::Settings::Change;
	using Type = State::Type;
	if (const auto user = peer->asUser()) {
		using FlagsChange = UserData::Flags::Change;
		using Flag = UserDataFlag;

		auto changes = user->flagsValue(
		) | rpl::filter([](FlagsChange flags) {
			return flags.diff
				& (Flag::Contact | Flag::MutualContact | Flag::Blocked);
		});
		return rpl::combine(
			std::move(changes),
			user->settingsValue()
		) | rpl::map([=](
				FlagsChange flags,
				SettingsChange settings) -> State {
			if (flags.value & Flag::Blocked) {
				return { Type::None };
			} else if (user->isContact()) {
				if (settings.value & PeerSetting::ShareContact) {
					return { Type::SharePhoneNumber };
				} else {
					return { Type::None };
				}
			} else if (settings.value & PeerSetting::RequestChat) {
				return {
					.type = Type::RequestChatInfo,
					.requestChatName = peer->requestChatTitle(),
					.requestChatIsBroadcast = !!(settings.value
						& PeerSetting::RequestChatIsBroadcast),
					.requestDate = peer->requestChatDate(),
				};
			} else if (settings.value & PeerSetting::AutoArchived) {
				return { Type::UnarchiveOrBlock };
			} else if (settings.value & PeerSetting::BlockContact) {
				return { Type::AddOrBlock };
			} else if (settings.value & PeerSetting::AddContact) {
				return { Type::Add };
			} else {
				return { Type::None };
			}
		});
	}

	return peer->settingsValue(
	) | rpl::map([=](SettingsChange settings) {
		using Type = State::Type;
		return (settings.value & PeerSetting::AutoArchived)
			? State{ Type::UnarchiveOrReport }
			: (settings.value & PeerSetting::ReportSpam)
			? State{ Type::ReportSpam }
			: State{ Type::None };
	});
}

void ContactStatus::setupState(not_null<PeerData*> peer) {
	if (!BarCurrentlyHidden(peer)) {
		peer->session().api().requestPeerSettings(peer);
	}

	_bar.entity()->showState(State());
	PeerState(
		peer
	) | rpl::start_with_next([=](State state) {
		_state = state;
		if (state.type == State::Type::None) {
			_bar.hide(anim::type::normal);
		} else {
			_bar.entity()->showState(state);
			_bar.show(anim::type::normal);
		}
	}, _bar.lifetime());
}

void ContactStatus::setupHandlers(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		setupAddHandler(user);
		setupBlockHandler(user);
		setupShareHandler(user);
	}
	setupUnarchiveHandler(peer);
	setupReportHandler(peer);
	setupCloseHandler(peer);
	setupRequestInfoHandler(peer);
}

void ContactStatus::setupAddHandler(not_null<UserData*> user) {
	_bar.entity()->addClicks(
	) | rpl::start_with_next([=] {
		_controller->window().show(Box(EditContactBox, _controller, user));
	}, _bar.lifetime());
}

void ContactStatus::setupBlockHandler(not_null<UserData*> user) {
	_bar.entity()->blockClicks(
	) | rpl::start_with_next([=] {
		_controller->window().show(Box(
			Window::PeerMenuBlockUserBox,
			&_controller->window(),
			user,
			v::null,
			Window::ClearChat{}));
	}, _bar.lifetime());
}

void ContactStatus::setupShareHandler(not_null<UserData*> user) {
	_bar.entity()->shareClicks(
	) | rpl::start_with_next([=] {
		const auto show = std::make_shared<Window::Show>(_controller);
		const auto share = [=](Fn<void()> &&close) {
			user->setSettings(0);
			user->session().api().request(MTPcontacts_AcceptContact(
				user->inputUser
			)).done([=](const MTPUpdates &result) {
				user->session().api().applyUpdates(result);

				if (show->valid()) {
					Ui::Toast::Show(
						show->toastParent(),
						tr::lng_new_contact_share_done(
							tr::now,
							lt_user,
							user->shortName()));
				}
			}).send();
			close();
		};
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_new_contact_share_sure(
				tr::now,
				lt_phone,
				Ui::Text::WithEntities(
					Ui::FormatPhone(user->session().user()->phone())),
				lt_user,
				Ui::Text::Bold(user->name()),
				Ui::Text::WithEntities),
			.confirmed = share,
			.confirmText = tr::lng_box_ok(),
		}));
	}, _bar.lifetime());
}

void ContactStatus::setupUnarchiveHandler(not_null<PeerData*> peer) {
	_bar.entity()->unarchiveClicks(
	) | rpl::start_with_next([=] {
		Window::ToggleHistoryArchived(peer->owner().history(peer), false);
		peer->owner().notifySettings().resetToDefault(peer);
		if (const auto settings = peer->settings()) {
			const auto flags = PeerSetting::AutoArchived
				| PeerSetting::BlockContact
				| PeerSetting::ReportSpam;
			peer->setSettings(*settings & ~flags);
		}
	}, _bar.lifetime());
}

void ContactStatus::setupReportHandler(not_null<PeerData*> peer) {
	_bar.entity()->reportClicks(
	) | rpl::start_with_next([=] {
		Expects(!peer->isUser());
		const auto show = std::make_shared<Window::Show>(_controller);

		const auto callback = crl::guard(&_bar, [=](Fn<void()> &&close) {
			close();

			peer->session().api().request(MTPmessages_ReportSpam(
				peer->input
			)).send();

			crl::on_main(&peer->session(), [=] {
				if (const auto from = peer->migrateFrom()) {
					peer->session().api().deleteConversation(from, false);
				}
				peer->session().api().deleteConversation(peer, false);
			});

			if (show->valid()) {
				Ui::Toast::Show(
					show->toastParent(),
					tr::lng_report_spam_done(tr::now));
			}

			// Destroys _bar.
			_controller->showBackFromStack();
		});
		if (const auto user = peer->asUser()) {
			peer->session().api().blockedPeers().block(user);
		}
		auto text = ((peer->isChat() || peer->isMegagroup())
			? tr::lng_report_spam_sure_group
			: tr::lng_report_spam_sure_channel)();
		show->showBox(Ui::MakeConfirmBox({
			.text= std::move(text),
			.confirmed = callback,
			.confirmText = tr::lng_report_spam_ok(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	}, _bar.lifetime());
}

void ContactStatus::setupCloseHandler(not_null<PeerData*> peer) {
	const auto request = _bar.lifetime().make_state<mtpRequestId>(0);
	_bar.entity()->closeClicks(
	) | rpl::filter([=] {
		return !(*request);
	}) | rpl::start_with_next([=] {
		peer->setSettings(0);
		*request = peer->session().api().request(
			MTPmessages_HidePeerSettingsBar(peer->input)
		).send();
	}, _bar.lifetime());
}

void ContactStatus::setupRequestInfoHandler(not_null<PeerData*> peer) {
	const auto request = _bar.lifetime().make_state<mtpRequestId>(0);
	_bar.entity()->requestInfoClicks(
	) | rpl::filter([=] {
		return !(*request);
	}) | rpl::start_with_next([=] {
		_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
			box->setTitle((_state.requestChatIsBroadcast
				? tr::lng_from_request_title_channel
				: tr::lng_from_request_title_group)());

			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_from_request_body(
					lt_name,
					rpl::single(Ui::Text::Bold(_state.requestChatName)),
					lt_date,
					rpl::single(langDateTimeFull(
						base::unixtime::parse(_state.requestDate)
					)) | Ui::Text::ToWithEntities(),
					Ui::Text::WithEntities),
				st::boxLabel));

			box->addButton(tr::lng_from_request_understand(), [=] {
				if (*request) {
					return;
				}
				peer->setSettings(0);
				*request = peer->session().api().request(
					MTPmessages_HidePeerSettingsBar(peer->input)
				).send();
				box->closeBox();
			});
		}));
	}, _bar.lifetime());
}

void ContactStatus::show() {
	const auto visible = (_state.type != State::Type::None);
	if (!_shown) {
		_shown = true;
		if (visible) {
			_bar.entity()->showState(_state);
		}
	}
	_bar.toggle(visible, anim::type::instant);
}

void ContactStatus::raise() {
	_bar.raise();
	_shadow.raise();
}

void ContactStatus::move(int x, int y) {
	_bar.move(x, y);
	_shadow.move(x, y + _bar.height());
}

int ContactStatus::height() const {
	return _bar.height();
}

rpl::producer<int> ContactStatus::heightValue() const {
	return _bar.heightValue();
}

} // namespace HistoryView
