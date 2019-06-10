/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_contact_status.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "styles/style_history.h"

namespace HistoryView {
namespace {

QString PeerFirstName(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->firstName;
	}
	return QString();
}

bool BarCurrentlyHidden(not_null<PeerData*> peer) {
	const auto settings = peer->settings();
	if (!settings) {
		return false;
	} else if (!(*settings)) {
		return true;
	}
	using Setting = MTPDpeerSettings::Flag;
	if (const auto user = peer->asUser()) {
		if (user->isContact()
			&& !((*settings) & Setting::f_share_contact)) {
			return true;
		}
	} else if (!((*settings) & Setting::f_report_spam)) {
		return true;
	}
	return false;
}

} // namespace
//
//void HistoryWidget::onReportSpamClicked() {
//	auto text = lang(_peer->isUser() ? lng_report_spam_sure : ((_peer->isChat() || _peer->isMegagroup()) ? lng_report_spam_sure_group : lng_report_spam_sure_channel));
//	Ui::show(Box<ConfirmBox>(text, lang(lng_report_spam_ok), st::attentionBoxButton, crl::guard(this, [this, peer = _peer] {
//		if (_reportSpamRequest) return;
//
//		Ui::hideLayer();
//		_reportSpamRequest = MTP::send(
//			MTPmessages_ReportSpam(peer->input),
//			rpcDone(&HistoryWidget::reportSpamDone, peer),
//			rpcFail(&HistoryWidget::reportSpamFail), 0, 5);
//		if (const auto user = peer->asUser()) {
//			session().api().blockUser(user);
//		}
//	})));
//}
//
//void HistoryWidget::reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
//	Expects(peer != nullptr);
//
//	if (req == _reportSpamRequest) {
//		_reportSpamRequest = 0;
//	}
//	cRefReportSpamStatuses().insert(peer->id, dbiprsReportSent);
//	Local::writeReportSpamStatuses();
//	if (_peer == peer) {
//		setReportSpamStatus(dbiprsReportSent);
//		if (_reportSpamPanel) {
//			_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, peer);
//		}
//	}
//}
//
//bool HistoryWidget::reportSpamFail(const RPCError &error, mtpRequestId req) {
//	if (MTP::isDefaultHandledError(error)) return false;
//
//	if (req == _reportSpamRequest) {
//		_reportSpamRequest = 0;
//	}
//	return false;
//}
//
//void HistoryWidget::onReportSpamHide() {
//	if (_peer) {
//		cRefReportSpamStatuses().insert(_peer->id, dbiprsHidden);
//		Local::writeReportSpamStatuses();
//
//		MTP::send(MTPmessages_HidePeerSettingsBar(_peer->input));
//	}
//	setReportSpamStatus(dbiprsHidden);
//	updateControlsVisibility();
//}
//
//void HistoryWidget::onReportSpamClear() {
//	Expects(_peer != nullptr);
//
//	InvokeQueued(App::main(), [peer = _peer] {
//		Ui::showChatsList();
//		if (const auto from = peer->migrateFrom()) {
//			peer->session().api().deleteConversation(from, false);
//		}
//		peer->session().api().deleteConversation(peer, false);
//	});
//
//	// Invalidates _peer.
//	controller()->showBackFromStack();
//}

ContactStatus::Bar::Bar(QWidget *parent, const QString &name)
: RpWidget(parent)
, _name(name)
, _block(
	this,
	lang(lng_new_contact_block).toUpper(),
	st::historyContactStatusBlock)
, _add(
	this,
	lang(lng_new_contact_add).toUpper(),
	st::historyContactStatusButton)
, _share(
	this,
	lang(lng_new_contact_share).toUpper(),
	st::historyContactStatusButton)
, _report(
	this,
	lang(lng_report_spam).toUpper(),
	st::historyContactStatusBlock)
, _close(this, st::historyReplyCancel) {
	resize(_close->size());
}

void ContactStatus::Bar::showState(State state) {
	_add->setVisible(state == State::AddOrBlock || state == State::Add);
	_block->setVisible(state == State::AddOrBlock);
	_share->setVisible(state == State::SharePhoneNumber);
	_report->setVisible(state == State::ReportSpam);
	_add->setText((state == State::Add)
		? lng_new_contact_add_name(lt_user, _name).toUpper()
		: lang(lng_new_contact_add).toUpper());
	updateButtonsGeometry();
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
	if (!_add->isHidden() && !_block->isHidden()) {
		const auto addWidth = buttonWidth(_add);
		const auto blockWidth = buttonWidth(_block);
		const auto half = full / 2;
		if (addWidth <= half
			&& blockWidth + 2 * closeWidth <= full - half) {
			placeButton(_add, half);
			placeButton(_block, full - half);
		} else if (addWidth + blockWidth <= available) {
			const auto margin = std::clamp(
				addWidth + blockWidth + closeWidth - available,
				0,
				closeWidth);
			const auto realBlockWidth = blockWidth + 2 * closeWidth - margin;
			if (addWidth > realBlockWidth) {
				placeButton(_add, addWidth);
				placeButton(_block, full - addWidth, margin);
			} else {
				placeButton(_add, full - realBlockWidth);
				placeButton(_block, realBlockWidth, margin);
			}
		} else {
			const auto forAdd = (available * addWidth)
				/ (addWidth + blockWidth);
			placeButton(_add, forAdd);
			placeButton(_block, full - forAdd, closeWidth);
		}
	} else {
		placeOne(_add);
		placeOne(_share);
		placeOne(_report);
	}
}

ContactStatus::ContactStatus(
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _bar(parent, object_ptr<Bar>(parent, PeerFirstName(peer)))
, _shadow(parent) {
	setupWidgets(parent);
	setupState(peer);
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
	using Settings = MTPDpeerSettings::Flags;
	using Setting = MTPDpeerSettings::Flag;
	if (const auto user = peer->asUser()) {
		using FlagsChange = UserData::Flags::Change;
		using Flags = MTPDuser::Flags;
		using Flag = MTPDuser::Flag;

		auto isContactChanges = user->flagsValue(
		) | rpl::filter([](FlagsChange flags) {
			return flags.diff
				& (Flag::f_contact | Flag::f_mutual_contact);
		});
		return rpl::combine(
			std::move(isContactChanges),
			user->settingsValue()
		) | rpl::map([=](FlagsChange flags, SettingsChange settings) {
			if (!settings.value) {
				return State::None;
			} else if (user->isContact()) {
				if (settings.value & Setting::f_share_contact) {
					return State::SharePhoneNumber;
				} else {
					return State::None;
				}
			}
			return State::AddOrBlock;
		});
	}

	return peer->settingsValue(
	) | rpl::map([=](SettingsChange settings) {
		return (settings.value & Setting::f_report_spam)
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

void ContactStatus::show() {
	if (_shown) {
		return;
	}
	_shown = true;
	const auto visible = (_state != State::None);
	if (visible) {
		_bar.entity()->showState(_state);
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
