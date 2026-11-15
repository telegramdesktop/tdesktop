/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_move_to_gifts.h"

#include "base/timer_rpl.h"
#include "boxes/peer_list_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "gram/api/gram_api_nft.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_gift_move_presentation.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_session_deposit.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kAddressWait = 15 * crl::time(1000);
constexpr auto kQuietErrors = std::array{
	SendError::None,
	SendError::SigningUnavailable,
	SendError::QuoteExpired,
};
constexpr auto kExplainedErrors = std::array{
	SendError::InsufficientBalance,
	SendError::InsufficientFees,
	SendError::CollectibleUnavailable,
	SendError::PreviousUnresolved,
	SendError::AlreadySending,
	SendError::Locked,
	SendError::Silent,
};

class MoveToGiftsRow final : public PeerListRow {
public:
	MoveToGiftsRow();

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const override;

private:
	mutable style::PeerListItem _st;

};

class MoveToGiftsController final : public PeerListController {
public:
	MoveToGiftsController(
		not_null<Main::Session*> session,
		Fn<void()> open);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	const not_null<Main::Session*> _session;
	const Fn<void()> _open;

};

class AccountCard final : public Ui::RpWidget {
public:
	AccountCard(QWidget *parent, not_null<UserData*> self);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<UserData*> _self;
	const QString _phone;
	style::TextStyle _nameStyle;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;

};

class MoveToGiftsFlow final : public base::has_weak_ptr {
public:
	MoveToGiftsFlow(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible);

	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;
	[[nodiscard]] rpl::producer<QString> refusalValue() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;
	[[nodiscard]] rpl::producer<bool> pressableValue() const;

	void start();
	void press();

private:
	[[nodiscard]] bool sessionValid() const;
	[[nodiscard]] bool current() const;
	[[nodiscard]] std::optional<SendArgs> args() const;
	[[nodiscard]] bool shortfall() const;
	[[nodiscard]] bool blocked() const;
	[[nodiscard]] QString refusalText() const;
	void refresh();
	void subscribe();
	void requested(GiftDeposit deposit);
	void started(const SendStarted &value);
	void sent(SendError error);
	void keyChanged();
	void bind(const std::string &operationId);
	void handOver(bool uncertain);
	void track();
	void resolve();
	void finish();
	void closing();

	const not_null<Ui::GenericBox*> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const std::shared_ptr<const CollectibleTransfer> _collectible;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Session*> _wallet;
	const std::optional<TransferWalletIdentity> _identity;
	GiftMovePreview _preview;
	std::optional<GiftDeposit> _answer;
	rpl::variable<QString> _refusal;
	rpl::variable<bool> _pressable = false;
	std::string _operationId;
	crl::time _submittedAt = 0;
	int64 _balance = 0;
	bool _handedOver = false;
	bool _uncertain = false;
	bool _finished = false;
	bool _closing = false;
	rpl::lifetime _watch;
	rpl::lifetime _lifetime;
	SendPreparation _preparation;

};

MoveToGiftsRow::MoveToGiftsRow()
: PeerListRow(Data::FakePeerIdForJustName(u"move-to-gifts"_q).value) {
}

QString MoveToGiftsRow::generateName() {
	return tr::lng_gift_transfer_to_gifts(tr::now);
}

QString MoveToGiftsRow::generateShortName() {
	return generateName();
}

PaintRoundImageCallback MoveToGiftsRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [](Painter &p, int x, int y, int outerWidth, int size) {
		PaintGiftMoveUserpic(
			p,
			x,
			y,
			outerWidth,
			size,
			st::giftMoveToGiftsUserpic);
	};
}

const style::PeerListItem &MoveToGiftsRow::computeSt(
		const style::PeerListItem &st) const {
	_st = st;
	_st.namePosition.setY(st::recentPeersSpecialName.namePosition.y());
	return _st;
}

MoveToGiftsController::MoveToGiftsController(
	not_null<Main::Session*> session,
	Fn<void()> open)
: _session(session)
, _open(std::move(open)) {
}

Main::Session &MoveToGiftsController::session() const {
	return *_session;
}

void MoveToGiftsController::prepare() {
	delegate()->peerListAppendRow(std::make_unique<MoveToGiftsRow>());
	delegate()->peerListRefreshRows();
}

void MoveToGiftsController::rowClicked(not_null<PeerListRow*> row) {
	_open();
}

AccountCard::AccountCard(QWidget *parent, not_null<UserData*> self)
: RpWidget(parent)
, _self(self)
, _phone(Ui::FormatPhone(self->phone()))
, _nameStyle(st::defaultTextStyle)
, _userpic(self->createUserpicView()) {
	_nameStyle.font = st::walletSendUserCardNameFont;
	Info::Profile::NameValue(self) | rpl::on_next([=](const QString &name) {
		_name.setText(_nameStyle, name, Ui::NameTextOptions());
		update();
	}, lifetime());
	self->session().downloaderTaskFinished() | rpl::on_next([=] {
		update();
	}, lifetime());
}

int AccountCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::walletSendUserCardPadding;
	const auto text = st::walletSendUserCardNameFont->height
		+ st::walletSendUserCardTextSkip
		+ st::boxTextFont->height;
	return padding.top()
		+ std::max(st::walletSendUserCardPhoto, text)
		+ padding.bottom();
}

void AccountCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto radius = st::walletSendUserCardRadius;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), radius, radius);
	}

	const auto &padding = st::walletSendUserCardPadding;
	const auto photo = st::walletSendUserCardPhoto;
	const auto inner = height() - padding.top() - padding.bottom();
	_self->paintUserpicLeft(
		p,
		_userpic,
		padding.left(),
		padding.top() + (inner - photo) / 2,
		width(),
		photo);

	const auto left = padding.left()
		+ photo
		+ st::walletSendUserCardPhotoSkip;
	const auto available = width() - left - padding.right();
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(p, left, padding.top(), available, width(), 1);

	const auto &font = st::boxTextFont;
	const auto phone = font->elided(_phone, available);
	const auto phoneTop = padding.top()
		+ st::walletSendUserCardNameFont->height
		+ st::walletSendUserCardTextSkip;
	p.setFont(font);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(left, phoneTop, width(), phone, font->width(phone));
}

[[nodiscard]] rpl::producer<TextWithEntities> MyGiftsPathText() {
	const auto prefixed = [](not_null<const style::IconEmoji*> emoji) {
		return rpl::map([=](const QString &text) {
			return Ui::Text::IconEmoji(emoji).append(' ').append(text);
		});
	};
	return tr::lng_gift_transfer_to_gifts_path_icons(
		lt_profile,
		tr::lng_menu_my_profile(
		) | prefixed(&st::giftMoveToGiftsProfileEmoji),
		lt_arrow,
		rpl::single(Ui::Text::IconEmoji(&st::giftMoveToWalletArrowEmoji)),
		lt_gifts,
		tr::lng_peer_gifts_title(
		) | prefixed(&st::giftMoveToWalletCollectiblesEmoji),
		tr::marked);
}

[[nodiscard]] bool QuietError(SendError error) {
	return ranges::contains(kQuietErrors, error);
}

[[nodiscard]] QString MoveRefusalText(SendError error) {
	const auto failed = tr::lng_wallet_send_error_failed(tr::now);
	if (QuietError(error)) {
		return QString();
	} else if (error == SendError::CollectibleRejected) {
		return ErrorWithType(failed, u"DEPOSIT_EMULATION_REJECTED"_q);
	} else if (error == SendError::InvalidRequest) {
		return ErrorWithType(failed, u"DEPOSIT_PAYLOAD_INVALID"_q);
	}
	return ranges::contains(kExplainedErrors, error)
		? SendErrorText(error, kTransferMinNanosDefault)
		: failed;
}

MoveToGiftsFlow::MoveToGiftsFlow(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<const CollectibleTransfer> collectible)
: _box(box)
, _show(std::move(show))
, _collectible(std::move(collectible))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _identity(_wallet->transferWalletIdentity())
, _preview(_session)
, _preparation({
	.show = _show,
	.current = [=] { return current(); },
	.args = [=] { return args(); },
	.blocked = [=](SendError, std::optional<int64>) { return blocked(); },
	.invalidated = _wallet->collectiblesUpdates(),
	.started = crl::guard(this, [=](SendStarted value) { started(value); }),
	.sent = crl::guard(this, [=](SendError error) { sent(error); }),
	.attempt = _preview.attempts(),
}) {
	Expects(_collectible->unique != nullptr);
}

rpl::producer<std::optional<int64>> MoveToGiftsFlow::feeValue() const {
	return _preparation.feeValue();
}

rpl::producer<QString> MoveToGiftsFlow::refusalValue() const {
	return _refusal.value();
}

rpl::producer<bool> MoveToGiftsFlow::busyValue() const {
	return _preparation.sendingValue();
}

rpl::producer<bool> MoveToGiftsFlow::pressableValue() const {
	return _pressable.value();
}

void MoveToGiftsFlow::start() {
	if (!current()) {
		_box->closeBox();
		return;
	}
	_preparation.start();
	_wallet->startPolling();
	_lifetime.add([weak = _weakSession] {
		if (const auto strong = weak.get()) {
			strong->wallet().stopPolling();
		}
	});
	subscribe();
	RequestGiftDeposit(
		_session,
		_collectible->address,
		_collectible->slug,
		crl::guard(this, [=](GiftDeposit deposit) {
			requested(std::move(deposit));
		}));
}

void MoveToGiftsFlow::subscribe() {
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	_wallet->transferWalletIdentityChanges(
	) | rpl::filter([=] {
		return !current();
	}) | rpl::take(1) | rpl::on_next([=] {
		_box->closeBox();
	}, _lifetime);
	_wallet->balanceNanoValue() | rpl::on_next([=](int64 balance) {
		_balance = balance;
		refresh();
	}, _lifetime);
	rpl::merge(
		Lang::Updated(),
		_preparation.feeValue() | rpl::to_empty,
		_preparation.errorValue() | rpl::to_empty,
		_preparation.estimatingValue() | rpl::to_empty,
		_preparation.sendingValue() | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, _lifetime);
	_preparation.sendingValue(
	) | rpl::filter(!rpl::mappers::_1) | rpl::on_next([=] {
		_watch.destroy();
	}, _lifetime);
	_preparation.sendingValue(
	) | rpl::skip(1) | rpl::on_next([=](bool sending) {
		if (!sending) {
			_preview.stop();
		} else if (_identity && _answer) {
			_preview.show(*_identity, _answer->args);
		}
	}, _lifetime);
}

bool MoveToGiftsFlow::sessionValid() const {
	return _weakSession
		&& _show->valid()
		&& (&_show->session() == _session);
}

bool MoveToGiftsFlow::current() const {
	return !_closing
		&& sessionValid()
		&& _identity
		&& _wallet->transferWalletIdentityCurrent(*_identity);
}

std::optional<SendArgs> MoveToGiftsFlow::args() const {
	return (_answer && (_answer->error == GiftDepositError::None))
		? std::make_optional(_answer->args)
		: std::nullopt;
}

bool MoveToGiftsFlow::shortfall() const {
	const auto fee = _preparation.fee();
	if (_preparation.submitted() || !fee || !_answer) {
		return false;
	}
	const auto amount = _answer->args.amountNano;
	return (amount > _balance) || (*fee > _balance - amount);
}

bool MoveToGiftsFlow::blocked() const {
	return !_answer
		|| (_answer->error != GiftDepositError::None)
		|| !QuietError(_preparation.error())
		|| shortfall();
}

QString MoveToGiftsFlow::refusalText() const {
	if (!_answer) {
		return QString();
	}
	const auto failed = tr::lng_wallet_send_error_failed(tr::now);
	switch (_answer->error) {
	case GiftDepositError::Request:
		return _answer->silent
			? QString()
			: ErrorWithType(failed, _answer->type);
	case GiftDepositError::AddressMismatch:
		return ErrorWithType(failed, u"DEPOSIT_ADDRESS_MISMATCH"_q);
	case GiftDepositError::InvalidAnswer:
		return ErrorWithType(failed, u"DEPOSIT_PAYLOAD_INVALID"_q);
	case GiftDepositError::None:
		break;
	}
	return shortfall()
		? tr::lng_wallet_send_error_insufficient(tr::now)
		: MoveRefusalText(_preparation.error());
}

void MoveToGiftsFlow::refresh() {
	_refusal = refusalText();
	_pressable = _preparation.pressable();
}

void MoveToGiftsFlow::requested(GiftDeposit deposit) {
	if (!current()) {
		return;
	}
	_answer = std::move(deposit);
	refresh();
	_preparation.estimate();
}

void MoveToGiftsFlow::press() {
	_preparation.press();
}

void MoveToGiftsFlow::started(const SendStarted &value) {
	_submittedAt = crl::now();
	_handedOver = true;
	bind(value.operationId);
}

void MoveToGiftsFlow::sent(SendError error) {
	if (error == SendError::None || error == SendError::SubmissionUnknown) {
		const auto pending = _wallet->pendingSend();
		if (pending
			&& _answer
			&& (pending->walletIdentity == _identity)
			&& !pending->collectible.compare(
				_answer->args.collectible,
				Qt::CaseInsensitive)) {
			bind(pending->operationId);
		}
		handOver(error == SendError::SubmissionUnknown);
	} else if (error == SendError::KeyChanged) {
		keyChanged();
	} else if (_handedOver) {
		_preparation.fail(QuietError(error) ? SendError::Failed : error);
	} else {
		_preparation.refused(error);
	}
}

void MoveToGiftsFlow::keyChanged() {
	const auto show = _show;
	const auto valid = sessionValid();
	if (_handedOver) {
		_preparation.fail(SendError::Failed);
	} else {
		_box->closeBox();
	}
	if (valid) {
		ShowWalletKeyChanged(show);
	}
}

void MoveToGiftsFlow::bind(const std::string &operationId) {
	if (!_operationId.empty() || operationId.empty()) {
		return;
	}
	_operationId = operationId;
	if (_uncertain) {
		track();
	}
}

void MoveToGiftsFlow::handOver(bool uncertain) {
	_handedOver = true;
	_uncertain = uncertain;
	if (!_submittedAt) {
		_submittedAt = crl::now();
	}
	// WHY: the gift's service message did not reach the running session in
	// time to track (only the next launch's dialogs had it), so the move
	// ends at the accepted submission, as the owner's fallback asks.
	if (uncertain) {
		track();
	} else {
		finish();
	}
}

void MoveToGiftsFlow::track() {
	if (_operationId.empty()) {
		return;
	}
	_wallet->historyUpdates() | rpl::on_next([=] {
		resolve();
	}, _watch);
	resolve();
}

void MoveToGiftsFlow::resolve() {
	const auto item = _wallet->trackedTransaction(_operationId);
	if (!item) {
		return;
	} else if (item->status == TransferItem::Status::Failure) {
		_preparation.fail(SendError::Failed);
	} else if (item->status == TransferItem::Status::Success) {
		finish();
	}
}

void MoveToGiftsFlow::finish() {
	if (_finished || _closing) {
		return;
	}
	_finished = true;
	LOG(("Wallet Info: gift deposit accepted %1 ms after submission start."
		).arg(crl::now() - _submittedAt));
	_watch.destroy();
	const auto show = _show;
	const auto weak = _weakSession;
	const auto sticker = _collectible->unique->model.document;
	const auto view = [=] {
		const auto strong = weak.get();
		const auto window = strong ? strong->tryResolveWindow() : nullptr;
		if (window) {
			window->showSection(Info::PeerGifts::Make(strong->user()));
			window->window().activate();
		}
	};
	show->hideLayer();
	if (show->valid()) {
		ShowGiftMovedToast(
			show,
			sticker,
			tr::lng_gift_transfer_to_gifts_done(tr::now, tr::marked),
			view);
	}
}

void MoveToGiftsFlow::closing() {
	_closing = true;
	_watch.destroy();
	const auto weak = base::make_weak(this);
	_preview.close();
	if (weak) {
		_preparation.cancel();
	}
}

void MoveToMyGiftsBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible) {
	const auto flow = box->lifetime().make_state<MoveToGiftsFlow>(
		box,
		show,
		collectible);
	const auto &padding = st::boxPadding;
	const auto margins = [&](int top) {
		return QMargins(padding.left(), top, padding.right(), 0);
	};

	box->setTitle(tr::lng_gift_transfer_to_gifts());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_gift_transfer_to_gifts_text(),
		st::boxLabel));
	box->addRow(
		object_ptr<AccountCard>(box, show->session().user()),
		margins(st::walletAddressPlateSkip));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_transfer_to_gifts_appear(),
			st::giftMoveToWalletAppear),
		margins(st::walletAddressPlateSkip),
		style::al_justify);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			MyGiftsPathText(),
			st::giftMoveToWalletPath),
		margins(0),
		style::al_justify);

	auto feeAmount = flow->feeValue(
	) | rpl::filter([](std::optional<int64> fee) {
		return fee.has_value();
	}) | rpl::map([](std::optional<int64> fee) {
		return Ui::FormatTonAmount(*fee).full;
	});
	const auto fee = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_send_network_fee(
					lt_amount,
					std::move(feeAmount)),
				st::walletSendUserBalanceLabel)),
		margins(0),
		style::al_justify);
	fee->toggleOn(flow->feeValue(
	) | rpl::map([](std::optional<int64> fee) {
		return fee.has_value();
	}));
	fee->finishAnimating();

	const auto refusal = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				flow->refusalValue(),
				st::walletSendUserErrorLabel)),
		margins(0),
		style::al_justify);
	refusal->toggleOn(flow->refusalValue(
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));
	refusal->finishAnimating();
	box->addSkip(padding.bottom());

	const auto move = box->addButton(
		tr::lng_gift_transfer_to_gifts_button(),
		crl::guard(flow, [=] { flow->press(); })).data();
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		move,
		move->height() / 2,
		&st::editStickerSetNameLoading);
	Info::Statistics::AddChildToWidgetCenter(move, loading);
	loading->showOn(flow->busyValue());
	rpl::combine(
		flow->busyValue(),
		flow->pressableValue(),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::on_next([=](bool busy, bool pressable, rpl::empty_value) {
		move->setDisabled(busy || !pressable);
		move->setTextFgOverride(busy
			? std::make_optional(QColor(Qt::transparent))
			: pressable
			? std::optional<QColor>()
			: std::make_optional(st::windowSubTextFg->c));
	}, move->lifetime());
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	flow->start();
}

[[nodiscard]] QString HeldGiftAddress(
		not_null<Session*> wallet,
		const Data::UniqueGift &unique) {
	const auto address = CanonicalAddress(unique.giftAddress);
	if (!address.isEmpty()) {
		return address;
	}
	for (const auto &item : wallet->collectibles()) {
		if (item.kind == Gram::NftKind::TelegramGift
			&& !item.key.compare(unique.slug, Qt::CaseInsensitive)) {
			return CanonicalAddress(item.address);
		}
	}
	return QString();
}

void OpenHeldGiftTransfer(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		Fn<void()> locked) {
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	const auto own = wallet->address();
	const auto owner = CanonicalAddress(unique->ownerAddress);
	const auto item = HeldGiftAddress(wallet, *unique);
	if (!own
		|| owner.isEmpty()
		|| item.isEmpty()
		|| owner.compare(CanonicalAddress(*own), Qt::CaseInsensitive) != 0) {
		locked();
		return;
	}
	ShowCollectibleTransfer(
		show,
		std::make_shared<CollectibleMedia>(session),
		item,
		unique->slug,
		unique);
}

} // namespace

bool AddMoveToMyGiftsRow(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible,
		rpl::producer<bool> hidden) {
	if (!collectible || collectible->slug.isEmpty() || !collectible->unique) {
		return false;
	}
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	struct State {
		PeerListContentDelegateSimple delegate;
		base::weak_qptr<Ui::BoxContent> confirmation;
	};
	const auto state = inner->lifetime().make_state<State>();
	const auto open = [=] {
		if (state->confirmation || !show->valid()) {
			return;
		}
		auto box = Box(MoveToMyGiftsBox, show, collectible);
		state->confirmation = box.data();
		show->showBox(std::move(box));
	};
	const auto controller = inner->lifetime().make_state<
		MoveToGiftsController
	>(&show->session(), open);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = inner->add(
		object_ptr<PeerListContent>(inner, controller));
	state->delegate.setContent(content);
	controller->setDelegate(&state->delegate);
	wrap->toggleOn(
		std::move(hidden) | rpl::map(!rpl::mappers::_1),
		anim::type::instant);
	return true;
}

void ShowAddressHeldGiftTransfer(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		Fn<void()> locked) {
	if (!show || !show->valid()) {
		return;
	}
	const auto session = &show->session();
	if (!unique
		|| unique->ownerId
		|| unique->ownerAddress.isEmpty()
		|| unique->slug.isEmpty()
		|| !session->appConfig().walletAvailable()) {
		locked();
		return;
	}
	const auto wallet = &session->wallet();
	if (wallet->presence() != Presence::Unknown) {
		OpenHeldGiftTransfer(show, unique, locked);
		return;
	}
	const auto weak = base::make_weak(session);
	const auto lifetime = std::make_shared<rpl::lifetime>();
	rpl::merge(
		wallet->presenceValue() | rpl::filter(
			rpl::mappers::_1 != Presence::Unknown
		) | rpl::to_empty,
		base::timer_once(kAddressWait)
	) | rpl::on_next([=] {
		lifetime->destroy();
		if (weak && show->valid()) {
			OpenHeldGiftTransfer(show, unique, locked);
		}
	}, *lifetime);
	wallet->refreshState();
}

} // namespace Wallet
