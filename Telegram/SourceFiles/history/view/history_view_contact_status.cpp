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
#include "ui/text/text_utilities.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "boxes/confirm_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

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

ContactStatus::Bar::Bar(QWidget *parent, const QString &name)
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
, _close(this, st::historyReplyCancel) {
	resize(_close->size());
}

void ContactStatus::Bar::showState(State state) {
	_add->setVisible(state == State::AddOrBlock || state == State::Add);
	_unarchive->setVisible(state == State::UnarchiveOrBlock
		|| state == State::UnarchiveOrReport);
	_block->setVisible(state == State::AddOrBlock
		|| state == State::UnarchiveOrBlock);
	_share->setVisible(state == State::SharePhoneNumber);
	_report->setVisible(state == State::ReportSpam
		|| state == State::UnarchiveOrReport);
	_add->setText((state == State::Add)
		? tr::lng_new_contact_add_name(tr::now, lt_user, _name).toUpper()
		: tr::lng_new_contact_add(tr::now).toUpper());
	_report->setText((state == State::ReportSpam)
		? tr::lng_report_spam_and_leave(tr::now).toUpper()
		: tr::lng_report_spam(tr::now).toUpper());
	updateButtonsGeometry();
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

void ContactStatus::Bar::resizeEvent(QResizeEvent *e) {
	_close->moveToRight(0, 0);
	updateButtonsGeometry();
}

void ContactStatus::Bar::updateButtonsGeometry() {
	const auto full = width();
	const auto closeWidth = _close->width();
	const auto available = full - closeWidth;
	const auto skip = st::historyContactStatusMinSkip;
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
		placeButton(button, full, margin);
	};
	const auto &leftButton = _unarchive->isHidden() ? _add : _unarchive;
	const auto &rightButton = _block->isHidden() ? _report : _block;
	if (!leftButton->isHidden() && !rightButton->isHidden()) {
		const auto leftWidth = buttonWidth(leftButton);
		const auto rightWidth = buttonWidth(rightButton);
		const auto half = full / 2;
		if (leftWidth <= half
			&& rightWidth + 2 * closeWidth <= full - half) {
			placeButton(leftButton, half);
			placeButton(rightButton, full - half);
		} else if (leftWidth + rightWidth <= available) {
			const auto margin = std::clamp(
				leftWidth + rightWidth + closeWidth - available,
				0,
				closeWidth);
			const auto realBlockWidth = rightWidth + 2 * closeWidth - margin;
			if (leftWidth > realBlockWidth) {
				placeButton(leftButton, leftWidth);
				placeButton(rightButton, full - leftWidth, margin);
			} else {
				placeButton(leftButton, full - realBlockWidth);
				placeButton(rightButton, realBlockWidth, margin);
			}
		} else {
			const auto forLeft = (available * leftWidth)
				/ (leftWidth + rightWidth);
			placeButton(leftButton, forLeft);
			placeButton(rightButton, full - forLeft, closeWidth);
		}
	} else {
		placeOne(_add);
		placeOne(_share);
		placeOne(_report);
	}
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
				SettingsChange settings) {
			if (flags.value & Flag::Blocked) {
				return State::None;
			} else if (user->isContact()) {
				if (settings.value & PeerSetting::ShareContact) {
					return State::SharePhoneNumber;
				} else {
					return State::None;
				}
			} else if (settings.value & PeerSetting::AutoArchived) {
				return State::UnarchiveOrBlock;
			} else if (settings.value & PeerSetting::BlockContact) {
				return State::AddOrBlock;
			} else if (settings.value & PeerSetting::AddContact) {
				return State::Add;
			} else {
				return State::None;
			}
		});
	}

	return peer->settingsValue(
	) | rpl::map([=](SettingsChange settings) {
		return (settings.value & PeerSetting::AutoArchived)
			? State::UnarchiveOrReport
			: (settings.value & PeerSetting::ReportSpam)
			? State::ReportSpam
			: State::None;
	});
}

void ContactStatus::setupState(not_null<PeerData*> peer) {
	if (!BarCurrentlyHidden(peer)) {
		peer->session().api().requestPeerSettings(peer);
	}

	PeerState(
		peer
	) | rpl::start_with_next([=](State state) {
		_state = state;
		if (state == State::None) {
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
		const auto share = [=](Fn<void()> &&close) {
			user->setSettings(0);
			user->session().api().request(MTPcontacts_AcceptContact(
				user->inputUser
			)).done([=](const MTPUpdates &result) {
				user->session().api().applyUpdates(result);

				Ui::Toast::Show(tr::lng_new_contact_share_done(
					tr::now,
					lt_user,
					user->shortName()));
			}).send();
			close();
		};
		_controller->window().show(Box<ConfirmBox>(
			tr::lng_new_contact_share_sure(
				tr::now,
				lt_phone,
				Ui::Text::WithEntities(
					App::formatPhone(user->session().user()->phone())),
				lt_user,
				Ui::Text::Bold(user->name),
				Ui::Text::WithEntities),
			tr::lng_box_ok(tr::now),
			share));
	}, _bar.lifetime());
}

void ContactStatus::setupUnarchiveHandler(not_null<PeerData*> peer) {
	_bar.entity()->unarchiveClicks(
	) | rpl::start_with_next([=] {
		Window::ToggleHistoryArchived(peer->owner().history(peer), false);
		peer->owner().resetNotifySettingsToDefault(peer);
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

			Ui::Toast::Show(tr::lng_report_spam_done(tr::now));

			// Destroys _bar.
			_controller->showBackFromStack();
		});
		if (const auto user = peer->asUser()) {
			peer->session().api().blockPeer(user);
		}
		const auto text = ((peer->isChat() || peer->isMegagroup())
			? tr::lng_report_spam_sure_group
			: tr::lng_report_spam_sure_channel)(tr::now);
		_controller->window().show(Box<ConfirmBox>(
			text,
			tr::lng_report_spam_ok(tr::now),
			st::attentionBoxButton,
			callback));
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

void ContactStatus::show() {
	const auto visible = (_state != State::None);
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
