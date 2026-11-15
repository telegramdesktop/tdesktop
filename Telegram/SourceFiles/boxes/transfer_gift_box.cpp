/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/transfer_gift_box.h"

#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "api/api_credits.h"
#include "api/api_text_entities.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "boxes/filters/edit_filter_chats_list.h" // CreatePe...tionSubtitle.
#include "boxes/peers/replace_boost_box.h"
#include "boxes/gift_premium_box.h"
#include "boxes/passcode_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_cover_box.h"
#include "boxes/star_gift_resale_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_cloud_themes.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_checkout_process.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/custom_emoji_toast_icon.h"
#include "ui/controls/sub_tabs.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h" // peerListSingleRow.
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h" // starIconEmoji.
#include "styles/style_dialogs.h" // recentPeersSpecialName.
#include "styles/style_info.h" // defaultSubTabs.
#include "styles/style_layers.h" // boxLabel.
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace {

constexpr auto kPlateGroupsPerLine = 4;
constexpr auto kMovedToWalletToastDuration = 5 * crl::time(1000);
constexpr auto kTmpPasswordPeriod = 10 * TimeId(60);
constexpr auto kPasswordStateTimeout = 10 * crl::time(1000);
constexpr auto kGiftMoveKeepOnProfilePref = "gift_move_keep_on_profile"_cs;

struct ExportOption {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<bool(int, int, int)> overrideKey;
	Fn<void()> activate;
};

class Controller final : public ContactsBoxController {
public:
	Controller(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		bool shownOnProfile,
		Fn<void(not_null<PeerData*>, Fn<void()>)> choose);

	void init(not_null<PeerListBox*> box);

	void noSearchSubmit();

private:
	void prepareViewHook() override;

	bool overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;
	void rowClicked(not_null<PeerListRow*> row) override;

	const not_null<Window::SessionController*> _window;
	const std::shared_ptr<Data::UniqueGift> _gift;
	const Data::SavedStarGiftId _giftId;
	const bool _shownOnProfile = false;
	const Fn<void(not_null<PeerData*>, Fn<void()>)> _choose;
	ExportOption _exportOption;
	QPointer<PeerListBox> _box;

};

struct GiftPasswordStep {
	Fn<void(
		const QByteArray &tmpPassword,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail)> send;
	Fn<void()> boxShown;
	Fn<void()> succeeded;
	Fn<void(const MTP::Error &)> failed;
	Fn<void()> dismissed;
};

class GiftPasswordFlow final
	: public std::enable_shared_from_this<GiftPasswordFlow> {
public:
	GiftPasswordFlow(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step);

	void start();

private:
	[[nodiscard]] bool canPrompt() const;
	void confirmationClosing();
	void waitForState();
	void stateReady(std::optional<Core::CloudPasswordState> state);
	void stateTimeout();
	void showPasswordBox(const Core::CloudPasswordState &state);
	bool showPrePasswordBox(const QString &type);
	void showBox(object_ptr<Ui::BoxContent> box);
	void boxClosing(not_null<Ui::BoxContent*> box);
	void requestTmpPassword(
		const Core::CloudPasswordResult &check,
		base::weak_qptr<PasscodeBox> box);
	void tmpPasswordReady(const MTPaccount_TmpPassword &result);
	void tmpPasswordFailed(
		const MTP::Error &error,
		base::weak_qptr<PasscodeBox> box);
	void sendGift(const QByteArray &tmpPassword, bool stored);
	void giftSent();
	void giftFailed(
		const MTP::Error &error,
		const QByteArray &tmpPassword,
		bool stored);
	void fail(const MTP::Error &error);

	const base::weak_ptr<Main::Session> _session;
	const std::shared_ptr<Ui::Show> _show;
	const base::weak_qptr<Ui::BoxContent> _confirmation;
	const GiftPasswordStep _step;
	base::weak_qptr<Ui::BoxContent> _box;
	bool _waiting = false;
	bool _issuing = false;
	bool _sending = false;
	bool _handedOff = false;
	bool _abandoned = false;
	bool _done = false;
	rpl::lifetime _lifetime;

};

void ConfirmExportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Data::UniqueGift> gift,
		Fn<void(not_null<Ui::GenericBox*> confirmation)> confirmed) {
	box->setTitle(tr::lng_gift_transfer_confirm_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_gift_transfer_confirm_text(
			lt_name,
			rpl::single(tr::bold(UniqueGiftName(*gift))),
			tr::marked),
		st::boxLabel));
	box->addButton(tr::lng_gift_transfer_confirm_button(), [=] {
		confirmed(box);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

// WHY: a gift call carries only the temporary password, so a password
// error from it means the server refused that temporary password.
[[nodiscard]] bool TmpPasswordRefused(const QString &type) {
	const auto refusals = std::array{
		u"TMP_PASSWORD_INVALID"_q,
		u"PASSWORD_HASH_INVALID"_q,
		u"SRP_PASSWORD_CHANGED"_q,
	};
	return ranges::contains(refusals, type);
}

GiftPasswordFlow::GiftPasswordFlow(
	not_null<Window::SessionController*> window,
	not_null<Ui::BoxContent*> confirmation,
	GiftPasswordStep step)
: _session(base::make_weak(&window->session()))
, _show(window->uiShow())
, _confirmation(confirmation)
, _step(std::move(step)) {
}

void GiftPasswordFlow::start() {
	const auto session = _session.get();
	const auto confirmation = _confirmation.get();
	if (!session || !confirmation) {
		return;
	}
	confirmation->boxClosing() | rpl::on_next([=] {
		confirmationClosing();
	}, _lifetime);
	const auto stored = session->validTmpPassword();
	if (stored.isEmpty()) {
		waitForState();
	} else {
		sendGift(stored, true);
	}
}

bool GiftPasswordFlow::canPrompt() const {
	return !_abandoned
		&& (_handedOff ? (_box != nullptr) : (_confirmation != nullptr));
}

void GiftPasswordFlow::confirmationClosing() {
	if (!_handedOff) {
		_abandoned = true;
	}
}

void GiftPasswordFlow::waitForState() {
	const auto session = _session.get();
	if (!session) {
		return;
	}
	_waiting = true;
	base::call_delayed(
		kPasswordStateTimeout,
		_confirmation.get(),
		[self = shared_from_this()] { self->stateTimeout(); });
	session->api().cloudPassword().reload([weak = weak_from_this()](
			std::optional<Core::CloudPasswordState> state) {
		if (const auto strong = weak.lock()) {
			strong->stateReady(std::move(state));
		}
	});
}

void GiftPasswordFlow::stateReady(
		std::optional<Core::CloudPasswordState> state) {
	if (!base::take(_waiting) || !canPrompt()) {
		return;
	} else if (!state) {
		fail(MTP::Error::Local(
			u"PASSWORD_STATE_FAILED"_q,
			u"account.getPassword failed"_q));
	} else if (!state->hasPassword) {
		showPrePasswordBox(u"PASSWORD_MISSING"_q);
	} else {
		showPasswordBox(*state);
	}
}

void GiftPasswordFlow::stateTimeout() {
	if (base::take(_waiting) && canPrompt()) {
		fail(MTP::Error::Local(
			u"PASSWORD_STATE_TIMEOUT"_q,
			u"account.getPassword timed out"_q));
	}
}

void GiftPasswordFlow::showPasswordBox(
		const Core::CloudPasswordState &state) {
	const auto session = _session.get();
	if (!session) {
		return;
	}
	auto fields = PasscodeBox::CloudFields::From(state);
	fields.customTitle = tr::lng_gift_transfer_password_title();
	fields.customDescription
		= tr::lng_gift_transfer_password_description(tr::now);
	fields.customSubmitButton = tr::lng_passcode_submit();
	fields.customCheckCallback = [self = shared_from_this()](
			const Core::CloudPasswordResult &check,
			base::weak_qptr<PasscodeBox> box) {
		self->requestTmpPassword(check, box);
	};
	showBox(Box<PasscodeBox>(session, fields));
}

bool GiftPasswordFlow::showPrePasswordBox(const QString &type) {
	const auto session = _session.get();
	if (!session) {
		return false;
	}
	auto box = PrePasswordErrorBox(
		type,
		session,
		tr::lng_gift_transfer_password_about(tr::now, tr::marked));
	if (!box) {
		return false;
	}
	showBox(std::move(box));
	return true;
}

void GiftPasswordFlow::showBox(object_ptr<Ui::BoxContent> box) {
	const auto raw = box.data();
	const auto previous = std::exchange(_box, raw);
	_handedOff = true;
	raw->boxClosing() | rpl::on_next([self = shared_from_this(), raw] {
		self->boxClosing(raw);
	}, raw->lifetime());
	if (const auto strong = previous.get()) {
		strong->closeBox();
	}
	_show->show(std::move(box));
	if (_step.boxShown) {
		_step.boxShown();
	}
}

void GiftPasswordFlow::boxClosing(not_null<Ui::BoxContent*> box) {
	if (_box.get() != box.get()) {
		return;
	}
	_box = nullptr;
	if (_done || _sending) {
		return;
	}
	_abandoned = true;
	if (_step.dismissed) {
		_step.dismissed();
	}
}

void GiftPasswordFlow::requestTmpPassword(
		const Core::CloudPasswordResult &check,
		base::weak_qptr<PasscodeBox> box) {
	const auto session = _session.get();
	if (!session || !canPrompt() || _issuing || _sending || _done) {
		return;
	}
	_issuing = true;
	session->api().request(MTPaccount_GetTmpPassword(
		check.result,
		MTP_int(kTmpPasswordPeriod)
	)).done([self = shared_from_this()](
			const MTPaccount_TmpPassword &result) {
		self->tmpPasswordReady(result);
	}).fail([self = shared_from_this(), box](const MTP::Error &error) {
		self->tmpPasswordFailed(error, box);
	}).handleFloodErrors().send();
}

void GiftPasswordFlow::tmpPasswordReady(
		const MTPaccount_TmpPassword &result) {
	_issuing = false;
	const auto session = _session.get();
	if (!session || !canPrompt()) {
		return;
	}
	const auto &data = result.data();
	const auto tmpPassword = data.vtmp_password().v;
	if (tmpPassword.isEmpty()) {
		fail(MTP::Error::Local(
			u"TMP_PASSWORD_EMPTY"_q,
			u"account.getTmpPassword returned no bytes"_q));
		return;
	}
	session->setTmpPassword(tmpPassword, data.vvalid_until().v);
	sendGift(tmpPassword, false);
}

void GiftPasswordFlow::tmpPasswordFailed(
		const MTP::Error &error,
		base::weak_qptr<PasscodeBox> box) {
	_issuing = false;
	if (!canPrompt()) {
		return;
	}
	const auto &type = error.type();
	if (showPrePasswordBox(type)) {
		return;
	}
	const auto strong = box.get();
	if (!strong || !strong->handleCustomCheckError(type)) {
		fail(error);
	}
}

void GiftPasswordFlow::sendGift(const QByteArray &tmpPassword, bool stored) {
	Expects(!tmpPassword.isEmpty());

	_sending = true;
	const auto self = shared_from_this();
	_step.send(
		tmpPassword,
		[=] { self->giftSent(); },
		[=](const MTP::Error &error) {
			self->giftFailed(error, tmpPassword, stored);
		});
}

void GiftPasswordFlow::giftSent() {
	_sending = false;
	_done = true;
	_step.succeeded();
	if (const auto strong = _box.get()) {
		strong->closeBox();
	}
}

void GiftPasswordFlow::giftFailed(
		const MTP::Error &error,
		const QByteArray &tmpPassword,
		bool stored) {
	_sending = false;
	const auto &type = error.type();
	const auto refused = TmpPasswordRefused(type);
	const auto session = _session.get();
	if (refused && session) {
		session->dropTmpPassword(tmpPassword);
	}
	if (_abandoned) {
		return;
	} else if (refused && stored && canPrompt()) {
		waitForState();
	} else if (!canPrompt() || !showPrePasswordBox(type)) {
		fail(error);
	}
}

void GiftPasswordFlow::fail(const MTP::Error &error) {
	if (_show->valid()) {
		_step.failed(error);
	}
}

void RequestWithGiftPassword(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		GiftPasswordStep step) {
	const auto flow = std::make_shared<GiftPasswordFlow>(
		window,
		confirmation,
		std::move(step));
	flow->start();
}

void ExportOnBlockchain(
		not_null<Window::SessionController*> window,
		not_null<Ui::BoxContent*> confirmation,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId giftId,
		Fn<void()> boxShown,
		Fn<void()> wentToUrl,
		Fn<void()> failed) {
	const auto session = &window->session();
	const auto show = window->uiShow();
	const auto send = [=](
			const QByteArray &tmpPassword,
			Fn<void()> done,
			Fn<void(const MTP::Error &)> fail) {
		using ExportUrl = MTPpayments_StarGiftWithdrawalUrl;
		session->api().request(
			MTPpayments_GetStarGiftWithdrawalUrl(
				MTP_flags(0),
				Api::InputSavedStarGiftId(giftId),
				MTP_bytes(tmpPassword))
		).done([=](const ExportUrl &result) {
			UrlClickHandler::Open(qs(result.data().vurl()));
			done();
		}).fail(fail).send();
	};
	RequestWithGiftPassword(window, confirmation, {
		.send = send,
		.boxShown = boxShown,
		.succeeded = wentToUrl,
		.failed = [=](const MTP::Error &error) {
			failed();
			show->showToast(error.type());
		},
	});
}

[[nodiscard]] bool ListedGift(
		const Gram::NftItem &item,
		const std::shared_ptr<Data::UniqueGift> &gift) {
	return Wallet::UniqueGiftMatchesAddress(gift, item.address)
		|| (item.kind == Gram::NftKind::TelegramGift
			&& !item.key.isEmpty()
			&& !item.key.compare(gift->slug, Qt::CaseInsensitive));
}

[[nodiscard]] rpl::producer<TextWithEntities> CollectiblesPathText() {
	const auto prefixed = [](not_null<const style::IconEmoji*> emoji) {
		return rpl::map([=](const QString &text) {
			return Ui::Text::IconEmoji(emoji).append(' ').append(text);
		});
	};
	const auto arrow = Ui::Text::IconEmoji(&st::giftMoveToWalletArrowEmoji);
	return tr::lng_gift_transfer_to_wallet_path_icons(
		lt_menu,
		rpl::single(Ui::Text::IconEmoji(&st::giftMoveToWalletMenuEmoji)),
		lt_arrow,
		rpl::single(arrow),
		lt_money,
		tr::lng_wallet_menu() | prefixed(&st::giftMoveToWalletMoneyEmoji),
		lt_arrow_next,
		rpl::single(arrow),
		lt_collectibles,
		tr::lng_wallet_rows_collectibles(
		) | prefixed(&st::giftMoveToWalletCollectiblesEmoji),
		tr::marked);
}

void MoveToWalletBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		base::weak_qptr<PeerListBox> chooser,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		bool shownOnProfile) {
	struct MoveState {
		rpl::variable<bool> busy;
		uint64 expectId = 0;
		bool watching = false;
		bool withdrawn = false;
		bool finished = false;
	};
	const auto state = std::make_shared<MoveState>();
	const auto session = &window->session();
	const auto wallet = &session->wallet();
	const auto show = window->uiShow();
	const auto weakSession = base::make_weak(session);
	const auto weakBox = base::make_weak(box);
	const auto falseError = MTP::Error::Local(
		u"STARGIFT_WITHDRAW_FALSE"_q,
		u"payments.withdrawStarGift returned false"_q);

	wallet->startPolling();
	box->lifetime().add([=] {
		if (const auto strong = weakSession.get()) {
			strong->wallet().stopPolling();
		}
	});

	const auto &padding = st::boxPadding;
	box->setTitle(tr::lng_gift_transfer_to_wallet());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_gift_transfer_to_wallet_text(),
		st::boxLabel));
	const auto plate = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)),
		style::margins());
	auto address = rpl::merge(
		wallet->presenceValue() | rpl::to_empty,
		wallet->transferWalletIdentityChanges()
	) | rpl::map([=] {
		return wallet->addressFriendly(false);
	}) | rpl::distinct_until_changed();
	const auto plateShown = [=] {
		return wallet->presenceValue(
		) | rpl::map([](Wallet::Presence presence) {
			return (presence == Wallet::Presence::Unknown)
				|| (presence == Wallet::Presence::Provisioning)
				|| (presence == Wallet::Presence::Ready);
		});
	};
	Wallet::AddAddressPlate(
		plate->entity(),
		std::move(address),
		QMargins(
			padding.left(),
			st::walletAddressPlateSkip,
			padding.right(),
			0),
		kPlateGroupsPerLine,
		show,
		plateShown());
	plate->toggleOn(plateShown());
	plate->finishAnimating();
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_transfer_to_wallet_appear(),
			st::giftMoveToWalletAppear),
		QMargins(
			padding.left(),
			st::walletAddressPlateSkip,
			padding.right(),
			0),
		style::al_justify);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			CollectiblesPathText(),
			st::giftMoveToWalletPath),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()),
		style::al_justify);
	const auto checkbox = shownOnProfile
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_gift_transfer_to_wallet_keep_on_profile(),
				session->local().readPref<bool>(kGiftMoveKeepOnProfilePref, true),
				st::defaultBoxCheckbox),
			st::boxRowPadding + QMargins(0, padding.top(), 0, padding.bottom()))
		: nullptr;
	if (checkbox) {
		checkbox->setAllowTextLines(0);
	}

	const auto finish = [=](bool keepOnProfile) {
		if (state->finished) {
			return;
		}
		state->finished = true;
		state->busy = false;
		if (keepOnProfile) {
			session->data().notifyGiftUpdate({
				.id = savedId,
				.slug = gift->slug,
				.action = Data::GiftUpdate::Action::HostChange,
			});
		}
		if (show->valid()) {
			ShowGiftMovedToast(
				show,
				gift->model.document,
				tr::lng_gift_transfer_to_wallet_done(tr::now, tr::marked),
				[=] {
					if (const auto strong = weakSession.get()) {
						strong->wallet().setCollectiblesTab(true);
						Wallet::ShowWallet(strong);
					}
				});
		}
		if (const auto strong = weakBox.get()) {
			strong->closeBox();
		}
		if (const auto strong = chooser.get()) {
			strong->closeBox();
		}
	};
	const auto disarm = [=] {
		if (const auto id = base::take(state->expectId)) {
			wallet->cancelIncomingCollectible(id);
		}
		state->watching = false;
	};
	const auto arm = [=](bool keepOnProfile) {
		disarm();
		state->expectId = wallet->expectIncomingCollectible([=](
				const Gram::NftItem &item) {
			return ListedGift(item, gift);
		}, [=](bool) {
			state->expectId = 0;
			state->watching = false;
			if (state->withdrawn) {
				finish(keepOnProfile);
			}
		});
		state->watching = (state->expectId != 0);
	};
	const auto withdraw = [=](
			bool keepOnProfile,
			const QByteArray &tmpPassword,
			Fn<void()> done,
			Fn<void(const MTP::Error &)> fail) {
		state->busy = true;
		using Flag = MTPpayments_WithdrawStarGift::Flag;
		arm(keepOnProfile);
		session->api().request(MTPpayments_WithdrawStarGift(
			MTP_flags(keepOnProfile ? Flag::f_keep_on_profile : Flag()),
			Api::InputSavedStarGiftId(savedId, gift),
			MTP_bytes(tmpPassword)
		)).done([=](const MTPBool &result) {
			if (mtpIsTrue(result)) {
				done();
				return;
			}
			disarm();
			fail(falseError);
		}).fail([=](const MTP::Error &error) {
			disarm();
			fail(error);
		}).send();
	};
	const auto withdrawn = [=](bool keepOnProfile) {
		state->withdrawn = true;
		state->busy = true;
		if (!keepOnProfile) {
			session->data().notifyGiftUpdate({
				.id = savedId,
				.action = Data::GiftUpdate::Action::Transfer,
			});
		}
		if (!state->watching) {
			finish(keepOnProfile);
		}
	};
	const auto failed = [=](const MTP::Error &error) {
		if (!state->withdrawn) {
			state->busy = false;
		}
		if (MTP::IgnoreError(error)) {
			return;
		}
		if (!Ui::ShowGiftErrorToast(show, error)) {
			show->showToast((error.type() == falseError.type())
				? Lang::Hard::ServerError()
				: error.type());
		}
	};
	const auto press = [=] {
		if (state->busy.current()
			|| state->finished
			|| state->withdrawn) {
			return;
		}
		const auto keepOnProfile = checkbox && checkbox->checked();
		if (checkbox) {
			session->local().writePref<bool>(
				kGiftMoveKeepOnProfilePref,
				keepOnProfile);
		}
		state->busy = true;
		RequestWithGiftPassword(window, box, {
			.send = [=](
					const QByteArray &tmpPassword,
					Fn<void()> done,
					Fn<void(const MTP::Error &)> fail) {
				withdraw(
					keepOnProfile,
					tmpPassword,
					std::move(done),
					std::move(fail));
			},
			.succeeded = [=] { withdrawn(keepOnProfile); },
			.failed = failed,
			.dismissed = [=] {
				if (!state->withdrawn) {
					state->busy = false;
				}
			},
		});
	};

	const auto move = box->addButton(
		tr::lng_gift_transfer_to_wallet_button(),
		press).data();
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		move,
		move->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(move, loading);
	loading->showOn(state->busy.value());
	state->busy.value() | rpl::on_next([=](bool busy) {
		move->setTextFgOverride(busy
			? std::make_optional(QColor(Qt::transparent))
			: std::optional<QColor>());
	}, move->lifetime());
}

[[nodiscard]] ExportOption MakeExportOption(
		not_null<Window::SessionController*> window,
		not_null<PeerListBox*> box,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId giftId,
		TimeId when,
		bool toWallet,
		bool shownOnProfile) {
	struct State {
		bool exporting = false;
	};
	const auto activate = [=] {
		const auto now = base::unixtime::now();
		const auto weak = base::make_weak(box);
		const auto left = (when > now) ? (when - now) : 0;
		const auto hours = left ? std::max((left + 1800) / 3600, 1) : 0;
		if (!hours) {
			if (toWallet) {
				window->show(Box(
					MoveToWalletBox,
					window,
					weak,
					gift,
					giftId,
					shownOnProfile));
				return;
			}
			const auto state = std::make_shared<State>();
			const auto confirmed = [=](
					not_null<Ui::GenericBox*> confirmation) {
				if (state->exporting) {
					return;
				}
				state->exporting = true;
				const auto weakConfirmation = base::make_weak(confirmation);
				const auto close = [=] {
					if (const auto strong = weakConfirmation.get()) {
						strong->closeBox();
					}
				};
				ExportOnBlockchain(window, confirmation, gift, giftId, [=] {
					state->exporting = false;
					close();
				}, [=] {
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
					close();
				}, [=] {
					state->exporting = false;
				});
			};
			window->show(Box(ConfirmExportBox, gift, confirmed));
			return;
		}
		window->show(Ui::MakeInformBox({
			.text = tr::lng_gift_transfer_unlocks_about(
				lt_when,
				((hours >= 24)
					? tr::lng_gift_transfer_unlocks_when_days(
						lt_count,
						rpl::single((hours / 24) * 1.))
					: tr::lng_gift_transfer_unlocks_when_hours(
						lt_count,
						rpl::single(hours * 1.)))),
			.title = tr::lng_gift_transfer_unlocks_title(),
		}));
	};

	class ExportRow final : public PeerListRow {
	public:
		ExportRow(TimeId when, bool toWallet)
		: PeerListRow(Data::FakePeerIdForJustName("ton-export").value)
		, _toWallet(toWallet) {
			const auto now = base::unixtime::now();
			_available = (when <= now);
			if (const auto left = when - now; left > 0) {
				const auto hours = std::max((left + 1800) / 3600, 1);
				const auto days = hours / 24;
				setCustomStatus(days
					? tr::lng_gift_transfer_unlocks_days(
						tr::now,
						lt_count,
						days)
					: tr::lng_gift_transfer_unlocks_hours(
						tr::now,
						lt_count,
						hours));
			}
		}

		QString generateName() override {
			return _toWallet
				? tr::lng_gift_transfer_to_wallet(tr::now)
				: tr::lng_gift_transfer_via_blockchain(tr::now);
		}
		QString generateShortName() override {
			return generateName();
		}
		auto generatePaintUserpicCallback(bool forceRound)
		-> PaintRoundImageCallback override {
			const auto toWallet = _toWallet;
			return [=](
					Painter &p,
					int x,
					int y,
					int outerWidth,
					int size) mutable {
				if (toWallet) {
					PaintGiftMoveUserpic(
						p,
						x,
						y,
						outerWidth,
						size,
						st::giftMoveToWalletUserpic);
				} else {
					Ui::EmptyUserpic::PaintCurrency(p, x, y, outerWidth, size);
				}
			};
		}

		const style::PeerListItem &computeSt(
				const style::PeerListItem &st) const override {
			_st = st;
			_st.namePosition.setY(
				st::recentPeersSpecialName.namePosition.y());
			return _available ? _st : st;
		}

	private:
		mutable style::PeerListItem _st;
		bool _toWallet = false;
		bool _available = false;

	};

	class ExportController final : public PeerListController {
	public:
		ExportController(
			not_null<Main::Session*> session,
			TimeId when,
			bool toWallet,
			Fn<void()> activate)
		: _session(session)
		, _when(when)
		, _toWallet(toWallet)
		, _activate(std::move(activate)) {
		}

		void prepare() override {
			delegate()->peerListAppendRow(
				std::make_unique<ExportRow>(_when, _toWallet));
			delegate()->peerListRefreshRows();
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_activate();
		}
		Main::Session &session() const override {
			return *_session;
		}

	private:
		const not_null<Main::Session*> _session;
		TimeId _when = 0;
		bool _toWallet = false;
		Fn<void()> _activate;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	Ui::AddSkip(container);

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		ExportController
	>(&window->session(), when, toWallet, activate);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_contacts_header()));

	const auto overrideKey = [=](int direction, int from, int to) {
		if (!content->isVisible()) {
			return false;
		} else if (direction > 0 && from < 0 && to >= 0) {
			if (content->hasSelection()) {
				const auto was = content->selectedIndex();
				const auto now = content->selectSkip(1).reallyMovedTo;
				if (was != now) {
					return true;
				}
				content->clearSelection();
			} else {
				content->selectSkip(1);
				return true;
			}
		} else if (direction < 0 && to < 0) {
			if (!content->hasSelection()) {
				content->selectLast();
			} else if (from >= 0 || content->hasSelection()) {
				content->selectSkip(-1);
			}
		}
		return false;
	};

	return {
		.content = std::move(result),
		.overrideKey = overrideKey,
		.activate = activate,
	};
}

Controller::Controller(
	not_null<Window::SessionController*> window,
	std::shared_ptr<Data::UniqueGift> gift,
	Data::SavedStarGiftId giftId,
	bool shownOnProfile,
	Fn<void(not_null<PeerData*>, Fn<void()>)> choose)
: ContactsBoxController(&window->session())
, _window(window)
, _gift(std::move(gift))
, _giftId(giftId)
, _shownOnProfile(shownOnProfile)
, _choose(std::move(choose)) {
	if (_gift->exportAt) {
		setStyleOverrides(&st::peerListSmallSkips);
	}
}

void Controller::init(not_null<PeerListBox*> box) {
	_box = box;
	if (const auto when = _gift->exportAt) {
		_exportOption = MakeExportOption(
			_window,
			box,
			_gift,
			_giftId,
			when,
			_window->session().appConfig().walletAvailable(),
			_shownOnProfile);
		delegate()->peerListSetAboveWidget(std::move(_exportOption.content));
		delegate()->peerListRefreshRows();
	}
}

void Controller::noSearchSubmit() {
	if (const auto onstack = _exportOption.activate) {
		onstack();
	}
}

bool Controller::overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) {
	return _exportOption.overrideKey
		&& _exportOption.overrideKey(direction, fromIndex, toIndex);
}

void Controller::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_gift_transfer_title(
		lt_name,
		rpl::single(UniqueGiftName(*_gift))));
}

std::unique_ptr<PeerListRow> Controller::createRow(
	not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return ContactsBoxController::createRow(user);
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_choose(row->peer(), [parentBox = _box] {
		if (const auto strong = parentBox.data()) {
			strong->closeBox();
		}
	});
}

void TransferGift(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> to,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		Fn<void(Payments::CheckoutResult)> done,
		bool skipPaymentForm = false) {
	const auto session = &window->session();
	const auto weak = base::make_weak(window);
	auto formDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		if (result == Payments::CheckoutResult::Free) {
			Assert(!skipPaymentForm);
			TransferGift(window, to, gift, savedId, done, true);
			return;
		}
		done(result);
		if (result == Payments::CheckoutResult::Paid) {
			session->data().notifyGiftUpdate({
				.id = savedId,
				.action = Data::GiftUpdate::Action::Transfer,
			});
			if (const auto strong = weak.get()) {
				Ui::ShowGiftTransferredToast(strong->uiShow(), to, *gift);
			}
		}
	};
	if (skipPaymentForm) {
		// We can't check (gift->starsForTransfer <= 0) here.
		//
		// Sometimes we don't know the price for transfer.
		// Like when we transfer a gift from Resale tab.
		session->api().request(MTPpayments_TransferStarGift(
			Api::InputSavedStarGiftId(savedId, gift),
			to->input()
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			formDone(Payments::CheckoutResult::Paid, &result);
		}).fail([=](const MTP::Error &error) {
			formDone(Payments::CheckoutResult::Failed, nullptr);
			const auto earlyPrefix = u"STARGIFT_TRANSFER_TOO_EARLY_"_q;
			const auto type = error.type();
			if (type.startsWith(earlyPrefix)) {
				const auto seconds = type.mid(earlyPrefix.size()).toInt();
				const auto newAvailableAt = base::unixtime::now() + seconds;
				gift->canTransferAt = newAvailableAt;
				if (const auto strong = weak.get()) {
					ShowTransferGiftLater(strong->uiShow(), gift);
				}
			} else if (const auto strong = weak.get()) {
				if (!Ui::ShowGiftErrorToast(strong->uiShow(), error)) {
					strong->showToast(type);
				}
			}
		}).send();
	} else {
		Ui::RequestStarsFormAndSubmit(
			window->uiShow(),
			MTP_inputInvoiceStarGiftTransfer(
				Api::InputSavedStarGiftId(savedId, gift),
				to->input()),
			std::move(formDone));
	}
}

void ResolveGiftSaleOffer(
		not_null<Window::SessionController*> window,
		MsgId id,
		bool accept,
		Fn<void(bool)> done) {
	using Flag = MTPpayments_ResolveStarGiftOffer::Flag;
	const auto session = &window->session();
	const auto show = window->uiShow();
	session->api().request(MTPpayments_ResolveStarGiftOffer(
		MTP_flags(accept ? Flag() : Flag::f_decline),
		MTP_int(id.bare)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		done(true);
	}).fail([=](const MTP::Error &error) {
		if (!Ui::ShowGiftErrorToast(show, error)) {
			show->showToast(error.type());
		}
		done(false);
	}).send();
}

struct ResaleGiftPurchase {
	QString slug;
	not_null<PeerData*> recipient;
	std::shared_ptr<Data::UniqueGift> gift;
	TextWithEntities message;
	bool hideName = false;
	CreditsType currency = CreditsType::Stars;
};

struct ResaleGiftAttemptState {
	bool inFlight = false;
};

[[nodiscard]] MTPInputInvoice InputInvoiceForResale(
		const ResaleGiftPurchase &purchase) {
	using Flag = MTPDinputInvoiceStarGiftResale::Flag;
	const auto flags = ((purchase.currency == CreditsType::Ton)
		? Flag::f_ton
		: Flag())
		| (!purchase.hideName ? Flag::f_show_name : Flag())
		| (!purchase.message.empty() ? Flag::f_message : Flag());
	return MTP_inputInvoiceStarGiftResale(
		MTP_flags(flags),
		MTP_string(purchase.slug),
		purchase.recipient->input(),
		MTP_textWithEntities(
			MTP_string(purchase.message.text),
			Api::EntitiesToMTP(
				&purchase.recipient->session(),
				purchase.message.entities,
				Api::ConvertOption::SkipLocal)));
}

void BuyResaleGift(
		std::shared_ptr<ChatHelpers::Show> show,
		ResaleGiftPurchase purchase,
		Fn<void(Payments::CheckoutResult)> done) {
	const auto invoice = InputInvoiceForResale(purchase);
	const auto completed = std::make_shared<bool>(false);
	auto paymentDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		if (*completed) {
			return;
		}
		*completed = true;
		if (result == Payments::CheckoutResult::Paid) {
			purchase.gift->starsForResale = 0;
		}
		done(result);
		if (result == Payments::CheckoutResult::Paid) {
			purchase.recipient->owner().notifyGiftUpdate({
				.slug = purchase.slug,
				.action = Data::GiftUpdate::Action::ResaleChange,
			});
			Ui::ShowResaleGiftBoughtToast(
				show,
				purchase.recipient,
				*purchase.gift);
		}
	};

	Ui::RequestOurForm(show, invoice, [=](
			uint64 formId,
			CreditsAmount price,
			std::optional<Payments::CheckoutResult> failure) {
		if ((purchase.currency == CreditsType::Ton && price.stars())
			|| (purchase.currency == CreditsType::Stars && price.ton())) {
			paymentDone(Payments::CheckoutResult::Failed, nullptr);
			return;
		}
		const auto submit = [=] {
			if (price.stars()) {
				SubmitStarsForm(
					show,
					invoice,
					formId,
					price.whole(),
					paymentDone);
			} else {
				SubmitTonForm(show, invoice, formId, price, paymentDone);
			}
		};
		const auto was = (purchase.currency == CreditsType::Ton)
			? Data::UniqueGiftResaleTon(*purchase.gift)
			: Data::UniqueGiftResaleStars(*purchase.gift);
		if (failure) {
			paymentDone(*failure, nullptr);
		} else if (price != was) {
			const auto cost = price.ton()
				? Ui::Text::IconEmoji(&st::tonIconEmoji).append(
					Lang::FormatCreditsAmountDecimal(price))
				: Ui::Text::IconEmoji(&st::starIconEmoji).append(
					Lang::FormatCountDecimal(price.whole()));
			const auto cancelled = [=](Fn<void()> close) {
				close();
				paymentDone(Payments::CheckoutResult::Cancelled, nullptr);
			};
			show->show(Ui::MakeConfirmBox({
				.text = tr::lng_gift_buy_price_change_text(
					tr::now,
					lt_price,
					Ui::Text::Wrapped(cost, EntityType::Bold),
					tr::marked),
				.confirmed = [=](Fn<void()> close) {
					close();
					submit();
				},
				.cancelled = cancelled,
				.confirmText = tr::lng_gift_buy_resale_button(
					lt_cost,
					rpl::single(cost),
					tr::marked),
				.title = tr::lng_gift_buy_price_change_title(),
			}));
		} else {
			submit();
		}
	});
}

base::weak_qptr<Ui::GenericBox> ShowBuyResaleGiftConfirm(
		std::shared_ptr<ChatHelpers::Show> show,
		ResaleGiftPurchase purchase,
		std::shared_ptr<ResaleGiftAttemptState> attempt,
		Fn<void(bool)> finish) {
	const auto initiallyTon = (purchase.currency == CreditsType::Ton);
	return show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			ResaleGiftPurchase purchase;
			rpl::variable<bool> ton;
			bool sent = false;
		};
		const auto state = std::make_shared<State>(State{
			.purchase = purchase,
			.ton = initiallyTon,
		});
		const auto gift = state->purchase.gift;
		const auto to = state->purchase.recipient;

		if (gift->onlyAcceptTon) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_gift_buy_resale_only_ton(tr::rich),
					st::resaleConfirmTonOnly),
				st::boxRowPadding + st::resaleConfirmTonOnlyMargin);
		} else {
			const auto tabs = box->addRow(
				object_ptr<Ui::SubTabs>(
					box,
					st::defaultSubTabs,
					Ui::SubTabsOptions{
						.selected = (state->ton.current()
							? u"ton"_q
							: u"stars"_q),
						.centered = true,
					},
					std::vector<Ui::SubTabsTab>{
						{
							u"stars"_q,
							tr::lng_gift_buy_resale_pay_stars(
								tr::now,
								tr::marked),
						},
						{
							u"ton"_q,
							tr::lng_gift_buy_resale_pay_ton(
								tr::now,
								tr::marked),
						},
					}),
				st::boxRowPadding + st::resaleConfirmTonOnlyMargin);
			tabs->activated() | rpl::on_next([=](QString id) {
				tabs->setActiveTab(id);
				state->ton = (id == u"ton"_q);
			}, tabs->lifetime());
		}

		auto transfer = state->ton.value() | rpl::map([=](bool ton) {
			return tr::lng_gift_buy_resale_button(
				lt_cost,
				rpl::single(ton
					? Data::FormatGiftResaleTon(*gift)
					: Data::FormatGiftResaleStars(*gift)),
				tr::marked);
		}) | rpl::flatten_latest();

		auto callback = [=](Fn<void()>) {
			if (state->sent || attempt->inFlight) {
				return;
			}
			state->sent = true;
			attempt->inFlight = true;
			auto selected = state->purchase;
			selected.currency = state->ton.current()
				? CreditsType::Ton
				: CreditsType::Stars;
			const auto weak = base::make_weak(box);
			const auto done = [=](Payments::CheckoutResult result) {
				attempt->inFlight = false;
				if (result == Payments::CheckoutResult::Cancelled) {
					finish(false);
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
				} else if (result != Payments::CheckoutResult::Paid) {
					state->sent = false;
				} else {
					finish(true);
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
				}
			};
			BuyResaleGift(show, std::move(selected), done);
		};

		auto price = state->ton.value() | rpl::map([=](bool ton) {
			return ton
				? tr::lng_action_gift_for_ton(
					lt_count_decimal,
					rpl::single(gift->nanoTonForResale
						/ float64(Ui::kNanosInOne)),
					tr::bold)
				: tr::lng_action_gift_for_stars(
					lt_count_decimal,
					rpl::single(gift->starsForResale * 1.),
					tr::bold);
		}) | rpl::flatten_latest();
		Ui::ConfirmBox(box, {
			.text = to->isSelf()
				? tr::lng_gift_buy_resale_confirm_self(
					lt_name,
					rpl::single(tr::bold(UniqueGiftName(*gift))),
					lt_price,
					std::move(price),
					tr::marked)
				: tr::lng_gift_buy_resale_confirm(
					lt_name,
					rpl::single(tr::bold(UniqueGiftName(*gift))),
					lt_price,
					std::move(price),
					lt_user,
					rpl::single(tr::bold(to->shortName())),
					tr::marked),
			.confirmed = std::move(callback),
			.confirmText = std::move(transfer),
		});
	}), Ui::LayerOption::KeepOther);
}

} // namespace

void PaintGiftMoveUserpic(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int size,
		const style::icon &icon) {
	x = style::RightToLeft() ? (outerWidth - x - size) : x;

	auto bg = QLinearGradient(x, y, x, y + size);
	bg.setStops({
		{ 0., st::historyPeerSavedMessagesBg->c },
		{ 1., st::historyPeerSavedMessagesBg2->c }
	});
	PainterHighQualityEnabler hq(p);
	p.setBrush(bg);
	p.setPen(Qt::NoPen);
	p.drawEllipse(x, y, size, size);

	icon.paintInCenter(p, QRect(x, y, size, size));
}

void ShowGiftMovedToast(
		std::shared_ptr<Ui::Show> show,
		not_null<DocumentData*> sticker,
		TextWithEntities text,
		Fn<void()> view) {
	const auto &st = st::historyPremiumToast;
	const auto &viewSt = st::historyPremiumViewSet;
	const auto label = tr::lng_gift_transfer_to_wallet_view(tr::now);
	const auto side = Ui::Emoji::GetCustomSizeLarge();
	const auto rightSkip = viewSt.style.font->width(label)
		- viewSt.width
		- st.padding.right();
	const auto weak = show->showToast(Ui::Toast::Config{
		.text = std::move(text),
		.iconContent = Ui::MakeCustomEmojiToastIcon(
			&sticker->session(),
			sticker->id,
			QSize(side, side)),
		.padding = rpl::single(QMargins(0, 0, rightSkip, 0)),
		.st = &st,
		.acceptinput = true,
		.duration = kMovedToWalletToastDuration,
	});
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		tr::lng_gift_transfer_to_wallet_view(),
		viewSt);
	button->show();
	button->setClickedCallback([=] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
		view();
	});
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
}

void ShowTransferToBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		Fn<void()> closeParentBox) {
	const auto stars = gift->starsForTransfer;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		auto transfer = (stars > 0)
			? tr::lng_gift_transfer_button_for(
				lt_price,
				rpl::single(Ui::Text::IconEmoji(
					&st::starIconEmoji
				).append(Lang::FormatCreditsAmountDecimal(
					CreditsAmount(stars)
				))),
				tr::marked)
			: tr::lng_gift_transfer_button(tr::marked);

		struct State {
			bool sent = false;
		};
		const auto state = std::make_shared<State>();
		auto callback = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			const auto weak = base::make_weak(box);
			const auto done = [=](Payments::CheckoutResult result) {
				if (result == Payments::CheckoutResult::Cancelled) {
					closeParentBox();
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
				} else if (result != Payments::CheckoutResult::Paid) {
					state->sent = false;
				} else {
					if (savedId.isUser()) {
						controller->showPeerHistory(peer);
					}
					closeParentBox();
					if (const auto strong = weak.get()) {
						strong->closeBox();
					}
				}
			};
			TransferGift(controller, peer, gift, savedId, done);
		};

		box->addRow(
			CreateGiftTransfer(box->verticalLayout(), gift, peer),
			QMargins(0, st::boxPadding.top(), 0, 0));

		Ui::ConfirmBox(box, {
			.text = (stars > 0)
				? tr::lng_gift_transfer_sure_for(
					lt_name,
					rpl::single(tr::bold(UniqueGiftName(*gift))),
					lt_recipient,
					rpl::single(tr::bold(peer->shortName())),
					lt_price,
					tr::lng_action_gift_for_stars(
						lt_count,
						rpl::single(stars * 1.),
						tr::bold),
					tr::marked)
				: tr::lng_gift_transfer_sure(
					lt_name,
					rpl::single(tr::bold(UniqueGiftName(*gift))),
					lt_recipient,
					rpl::single(tr::bold(peer->shortName())),
					tr::marked),
			.confirmed = std::move(callback),
			.confirmText = std::move(transfer),
		});

		const auto show = controller->uiShow();
		AddTransferGiftTable(show, box->verticalLayout(), gift);
	}));
}

void ShowTransferGiftBox(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId,
		bool shownOnProfile) {
	if (ShowTransferGiftLater(window->uiShow(), gift)) {
		return;
	}
	auto controller = std::make_unique<Controller>(
		window,
		gift,
		savedId,
		shownOnProfile,
		[=](not_null<PeerData*> peer, Fn<void()> done) {
			ShowTransferToBox(window, peer, gift, savedId, done);
		});
	const auto controllerRaw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		controllerRaw->init(box);

		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

		box->noSearchSubmits() | rpl::on_next([=] {
			controllerRaw->noSearchSubmit();
		}, box->lifetime());
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

void ShowGiftSaleAcceptBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<HistoryMessageSuggestion*> suggestion) {
	const auto id = item->id;
	const auto peer = item->history()->peer;
	const auto gift = suggestion->gift;
	const auto price = suggestion->price;

	const auto &appConfig = controller->session().appConfig();
	const auto starsThousandths = appConfig.giftResaleStarsThousandths();
	const auto nanoTonThousandths = appConfig.giftResaleNanoTonThousandths();

	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			bool sent = false;
		};
		const auto state = std::make_shared<State>();
		auto callback = [=] {
			if (state->sent) {
				return;
			}
			state->sent = true;
			const auto weak = base::make_weak(controller);
			const auto weakBox = base::make_weak(box);
			ResolveGiftSaleOffer(controller, id, true, [=](bool ok) {
				state->sent = false;
				if (ok) {
					if (const auto strong = weak.get()) {
						strong->showPeerHistory(peer->id);
					}
					if (const auto strong = weakBox.get()) {
						strong->closeBox();
					}
				}
			});
		};

		const auto receive = price.ton()
			? ((price.value() * nanoTonThousandths) / 1000.)
			: ((int64(price.value()) * starsThousandths) / 1000);

		auto button = tr::lng_gift_offer_sell_for(
			lt_price,
			rpl::single(Ui::Text::IconEmoji(price.ton()
				? &st::buttonTonIconEmoji
				: &st::buttonStarIconEmoji
			).append(Lang::FormatExactCountDecimal(receive))),
			tr::marked);

		box->addRow(
			CreateGiftTransfer(box->verticalLayout(), gift, peer),
			QMargins(0, st::boxPadding.top(), 0, 0));

		Ui::ConfirmBox(box, {
			.text = tr::lng_gift_offer_confirm_accept(
				tr::now,
				lt_name,
				tr::bold(UniqueGiftName(*gift)),
				lt_user,
				tr::bold(peer->shortName()),
				lt_cost,
				tr::bold(PrepareCreditsAmountText(price)),
				tr::marked
			).append(u"\n\n"_q).append(tr::lng_gift_offer_you_get(
				tr::now,
				lt_cost,
				tr::bold(price.stars()
					? tr::lng_action_gift_for_stars(
						tr::now,
						lt_count_decimal,
						receive)
					: tr::lng_action_gift_for_ton(
						tr::now,
						lt_count_decimal,
						receive)),
				tr::marked)),
			.confirmed = std::move(callback),
			.confirmText = std::move(button),
		});

		const auto show = controller->uiShow();
		auto taken = base::take(gift->value);
		AddTransferGiftTable(show, box->verticalLayout(), gift);
		gift->value = std::move(taken);

		if (gift->value.get()) {
			const auto appConfig = &show->session().appConfig();
			const auto rule = Ui::LookupCurrencyRule(u"USD"_q);
			const auto value = (gift->value->valuePriceUsd > 0 ? 1 : -1)
				* std::abs(gift->value->valuePriceUsd)
				/ std::pow(10., rule.exponent);
			if (std::abs(value) >= 0.01) {
				const auto rate = price.ton()
					? appConfig->currencySellRate()
					: (appConfig->starsSellRate() / 100.);
				const auto offered = receive * rate;
				const auto diff = offered - value;
				const auto percent = std::abs(diff / value * 100.);
				if (percent >= 1) {
					const auto higher = (diff > 0.);
					const auto good = higher || (percent < 10);
					const auto number = int(base::SafeRound(percent));
					const auto percentText = QString::number(number) + '%';
					box->addRow(
						object_ptr<Ui::FlatLabel>(
							box,
							(higher
								? tr::lng_gift_offer_higher
								: tr::lng_gift_offer_lower)(
									lt_percent,
									rpl::single(tr::bold(percentText)),
									lt_name,
									rpl::single(tr::marked(gift->title)),
									tr::marked),
							(good ? st::offerValueGood : st::offerValueBad)),
						st::boxRowPadding + st::offerValuePadding
					)->setTryMakeSimilarLines(true);
				}
			}
		}
	}));
}

void ShowGiftSaleRejectBox(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<HistoryMessageSuggestion*> suggestion) {
	struct State {
		bool sent = false;
	};
	const auto id = item->id;
	const auto state = std::make_shared<State>();
	auto callback = [=](Fn<void()> close) {
		if (state->sent) {
			return;
		}
		state->sent = true;
		const auto weak = base::make_weak(controller);
		ResolveGiftSaleOffer(controller, id, false, [=](bool ok) {
			state->sent = false;
			if (ok) {
				close();
			}
		});
	};
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_gift_offer_confirm_reject(
			lt_user,
			rpl::single(tr::bold(item->history()->peer->shortName())),
			tr::marked),
		.confirmed = std::move(callback),
		.confirmText = tr::lng_action_gift_offer_decline(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_gift_offer_reject_title(),
	}));
}

void SetThemeFromUniqueGift(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::UniqueGift> unique) {
	class Controller final : public ChooseRecipientBoxController {
	public:
		Controller(
			not_null<Window::SessionController*> window,
			std::shared_ptr<Data::UniqueGift> unique)
		: ChooseRecipientBoxController({
			.session = &window->session(),
			.callback = [=](not_null<Data::Thread*> thread) {
				const auto weak = base::make_weak(window);
				const auto peer = thread->peer();
				SendPeerThemeChangeRequest(window, peer, QString(), unique);
				if (weak) window->showPeerHistory(peer);
				if (weak) window->hideLayer(anim::type::normal);
			},
			.filter = [=](not_null<Data::Thread*> thread) {
				return thread->peer()->isUser();
			},
			.moneyRestrictionError = WriteMoneyRestrictionError,
		}) {
		}

	private:
		void prepareViewHook() override {
			ChooseRecipientBoxController::prepareViewHook();
			delegate()->peerListSetTitle(tr::lng_gift_transfer_choose());
		}

	};

	window->show(
		Box<PeerListBox>(
			std::make_unique<Controller>(window, std::move(unique)),
			[](not_null<PeerListBox*> box) {
				box->addButton(tr::lng_cancel(), [=] {
					box->closeBox();
				});
			}));
}

void SendPeerThemeChangeRequest(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &token,
		const std::shared_ptr<Data::UniqueGift> &unique,
		bool locallySet) {
	const auto api = &peer->session().api();

	api->request(MTPmessages_SetChatWallPaper(
		MTP_flags(0),
		peer->input(),
		MTPInputWallPaper(),
		MTPWallPaperSettings(),
		MTPint()
	)).afterDelay(10).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).send();

	api->request(MTPmessages_SetChatTheme(
		peer->input(),
		(unique
			? MTP_inputChatThemeUniqueGift(MTP_string(unique->slug))
			: MTP_inputChatTheme(MTP_string(token)))
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
		if (!locallySet) {
			peer->updateFullForced();
		}
	}).send();
}

void SetPeerTheme(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &token,
		const std::shared_ptr<Ui::ChatTheme> &theme) {
	const auto giftTheme = token.startsWith(u"gift:"_q)
		? peer->owner().cloudThemes().themeForToken(token)
		: std::optional<Data::CloudTheme>();

	peer->setThemeToken(token);
	const auto dropWallPaper = (peer->wallPaper() != nullptr);
	if (dropWallPaper) {
		peer->setWallPaper({});
	}

	if (theme) {
		// Remember while changes propagate through event loop.
		controller->pushLastUsedChatTheme(theme);
	}

	SendPeerThemeChangeRequest(
		controller,
		peer,
		token,
		giftTheme ? giftTheme->unique : nullptr,
		true);
}

void ShowBuyResaleGiftBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift,
		bool forceTon,
		not_null<PeerData*> to,
		Fn<void(bool ok)> closeParentBox) {
	const auto attempt = std::make_shared<ResaleGiftAttemptState>();
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<TextWithEntities> message;
			rpl::variable<bool> hideName;
			rpl::variable<bool> messageAllowed;
			std::shared_ptr<ResaleGiftAttemptState> attempt;
			bool confirmationOpen = false;
		};
		const auto state = box->lifetime().make_state<State>();
		state->hideName = to->isSelf();
		state->messageAllowed = Ui::StarGiftMessageAllowedValue(to);
		state->attempt = attempt;

		box->setStyle(st::giftBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		box->setTitle(tr::lng_gift_send_title());
		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});

		const auto container = box->verticalLayout();
		const auto initiallyTon = gift->onlyAcceptTon || forceTon;
		const auto initialCost = initiallyTon
			? Data::FormatGiftResaleTon(*gift)
			: Data::FormatGiftResaleStars(*gift);
		auto message = rpl::combine(
			state->message.value(),
			tr::lng_gift_send_message_preview(),
			state->hideName.value(),
			state->messageAllowed.value()
		) | rpl::map([sender = show->session().user()](
				TextWithEntities text,
				QString placeholder,
				bool hidden,
				bool allowed) {
			return Ui::UniqueGiftCoverMessage{
				.text = allowed ? std::move(text) : tr::marked(),
				.placeholder = allowed ? std::move(placeholder) : QString(),
				.sender = sender,
				.hidden = hidden,
			};
		});
		container->add(Ui::MakeUniqueGiftPreview(
			container,
			to,
			gift,
			initialCost,
			std::move(message)));

		const auto messageWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		messageWrap->toggleOn(state->messageAllowed.value());
		messageWrap->finishAnimating();
		const auto field = Ui::AddStarGiftMessageField(
			show,
			messageWrap->entity(),
			box->getDelegate()->outerContainer(),
			tr::lng_gift_send_message(),
			QString());
		field->changes() | rpl::on_next([=] {
			auto value = field->getTextWithAppliedMarkdown();
			state->message = TextWithEntities{
				std::move(value.text),
				TextUtilities::ConvertTextTagsToEntities(value.tags),
			};
		}, field->lifetime());
		box->setFocusCallback([=] {
			if (state->messageAllowed.current()) {
				field->setFocusFast();
			}
		});

		Ui::AddDivider(container);
		Ui::AddSkip(container);
		container->add(
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_gift_send_anonymous(),
				st::settingsButtonNoIcon)
		)->toggleOn(state->hideName.value())->toggledValue(
		) | rpl::on_next([=](bool toggled) {
			state->hideName = toggled;
		}, container->lifetime());
		Ui::AddSkip(container);
		Ui::AddDividerText(container, to->isSelf()
			? tr::lng_gift_send_anonymous_self()
			: to->isBroadcast()
			? tr::lng_gift_send_anonymous_about_channel()
			: rpl::conditional(
				state->messageAllowed.value(),
				tr::lng_gift_send_anonymous_about(
					lt_user,
					rpl::single(to->shortName()),
					lt_recipient,
					rpl::single(to->shortName())),
				tr::lng_gift_send_anonymous_about_paid(
					lt_user,
					rpl::single(to->shortName()),
					lt_recipient,
					rpl::single(to->shortName()))));

		const auto button = box->addButton(rpl::single(QString()), [=] {
			if (state->confirmationOpen || state->attempt->inFlight) {
				return;
			}
			state->confirmationOpen = true;
			const auto finish = crl::guard(box, [=](bool ok) {
				closeParentBox(ok);
				box->closeBox();
			});
			const auto confirmation = ShowBuyResaleGiftConfirm(
				show,
				ResaleGiftPurchase{
					.slug = gift->slug,
					.recipient = to,
					.gift = gift,
					.message = state->messageAllowed.current()
						? state->message.current()
						: tr::marked(),
					.hideName = state->hideName.current(),
					.currency = initiallyTon
						? CreditsType::Ton
						: CreditsType::Stars,
				},
				state->attempt,
				finish);
			if (const auto strong = confirmation.get()) {
				strong->boxClosing() | rpl::on_next(crl::guard(box, [=] {
					state->confirmationOpen = false;
				}), box->lifetime());
			} else {
				state->confirmationOpen = false;
			}
		});
		button->setText(tr::lng_gift_buy_resale_button(
			lt_cost,
			rpl::single(initialCost),
			tr::marked));
	}));
}

bool ShowResaleGiftLater(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift) {
	const auto now = base::unixtime::now();
	if (gift->canResellAt <= now) {
		return false;
	}
	const auto seconds = gift->canResellAt - now;
	const auto days = seconds / 86400;
	const auto hours = seconds / 3600;
	const auto minutes = std::max(seconds / 60, 1);
	show->showToast({
		.title = tr::lng_gift_resale_transfer_early_title(tr::now),
		.text = { tr::lng_gift_resale_early(tr::now, lt_duration, days
			? tr::lng_days(tr::now, lt_count, days)
			: hours
			? tr::lng_hours(tr::now, lt_count, hours)
			: tr::lng_minutes(tr::now, lt_count, minutes)) },
	});
	return true;
}

bool ShowTransferGiftLater(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> gift) {
	const auto seconds = gift->canTransferAt - base::unixtime::now();
	if (seconds <= 0) {
		return false;
	}
	const auto days = seconds / 86400;
	const auto hours = seconds / 3600;
	const auto minutes = std::max(seconds / 60, 1);
	show->showToast({
		.title = tr::lng_gift_resale_transfer_early_title(tr::now),
		.text = { tr::lng_gift_transfer_early(tr::now, lt_duration, days
			? tr::lng_days(tr::now, lt_count, days)
			: hours
			? tr::lng_hours(tr::now, lt_count, hours)
			: tr::lng_minutes(tr::now, lt_count, minutes)) },
	});
	return true;
}

void ShowActionLocked(
		std::shared_ptr<ChatHelpers::Show> show,
		const QString &slug) {
	const auto open = [=] {
		UrlClickHandler::Open(u"https://fragment.com/gift/"_q
			+ slug
			+ u"?collection=my"_q);
	};
	show->show(Ui::MakeConfirmBox({
		.text = tr::lng_gift_transfer_locked_text(),
		.confirmed = [=](Fn<void()> close) { open(); close(); },
		.confirmText = tr::lng_gift_transfer_confirm_button(),
		.title = tr::lng_gift_transfer_locked_title(),
	}));
}
