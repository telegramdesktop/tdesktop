/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "apiwrap.h"
#include "base/qt/qt_common_adapters.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "base/weak_qptr.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_cover_box.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user_wallet_card.h"
#include "gram/api/gram_api_nft.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_colors.h"
#include "wallet/wallet_card_drag.h"
#include "wallet/wallet_card_flight.h"
#include "wallet/wallet_card_look.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_collectibles_hint.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_session.h"

#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_wallet.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Wallet {
namespace {

constexpr auto kPreloadScreens = 3;
constexpr auto kPreloadRows = 2;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using TouchScreenKey = const QTouchDevice*;
using TouchScreenDevice = TouchScreenKey;

[[nodiscard]] TouchScreenKey DeviceOf(not_null<QTouchEvent*> e) {
	return e->device();
}

[[nodiscard]] QTouchEvent TouchCancelFrom(TouchScreenDevice device) {
	return QTouchEvent(
		QEvent::TouchCancel,
		const_cast<QTouchDevice*>(device));
}
#else // Qt < 6.0.0
using TouchScreenKey = const QPointingDevice*;
using TouchScreenDevice = QPointer<const QPointingDevice>;

[[nodiscard]] TouchScreenKey DeviceOf(not_null<QTouchEvent*> e) {
	return e->pointingDevice();
}

[[nodiscard]] QTouchEvent TouchCancelFrom(const TouchScreenDevice &device) {
	return QTouchEvent(QEvent::TouchCancel, device.data());
}
#endif // Qt >= 6.0.0

struct CoverCardState {
	bool onCard = false;
	bool source = false;
	bool allowed = false;
	bool listed = false;

	friend inline bool operator==(
		const CoverCardState &,
		const CoverCardState &) = default;
};

class CollectiblesGrid final : public Ui::RpWidget {
public:
	CollectiblesGrid(
		QWidget *parent,
		not_null<Ui::ScrollArea*> scroll,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		CardHost host);
	~CollectiblesGrid();

	void rebuild();
	[[nodiscard]] CollectiblesSquares squares();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	bool eventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Square {
		CollectibleRecord record;
		bool onCard = false;
		bool silhouette = false;
		std::unique_ptr<Info::PeerGifts::GiftButton> gift;
		std::unique_ptr<Ui::AbstractButton> thumb;
	};
	struct Flight {
		std::unique_ptr<CardFlight> flight;
		uint64 id = 0;
	};
	struct Press {
		QString address;
		QPoint origin;
		QPointF grab;
		bool draggable = false;
		bool grabbed = false;
	};
	struct Drag {
		QString address;
		DocumentData *document = nullptr;
		QImage firstFrame;
		uint64 generation = 0;
		std::unique_ptr<DraggedGift> gift;
	};
	enum class TouchPhase {
		Waiting,
		Held,
		Ignored,
	};
	struct Touch {
		QString address;
		QPoint origin;
		TouchScreenDevice device = {};
		int id = 0;
		TouchPhase phase = TouchPhase::Waiting;
		bool moved = false;
	};

	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] std::pair<int, int> rowsRange(
		int top,
		int bottom,
		int preload) const;
	[[nodiscard]] QRect squareRect(int index) const;
	[[nodiscard]] int squareAt(QPoint point) const;
	[[nodiscard]] int indexOf(const QString &address) const;
	[[nodiscard]] std::optional<QRect> hintTarget() const;

	void invalidateWindows();
	void refreshWindows();
	void refreshSquares();
	void refreshMediaWindow();
	void applyOnCard(const std::vector<Data::WalletCardSticker> &stickers);
	void applySquare(Square &square, const CollectibleRecord &record);
	void activate(const QString &address);
	[[nodiscard]] std::optional<CoverCardState> coverState(
		const QString &address) const;
	[[nodiscard]] std::vector<Ui::UniqueGiftCoverAction> coverActions(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void coverColorPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void coverCardPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void closeCoverThen(not_null<Ui::GenericBox*> box, Fn<void()> action);
	void showCardMenu(const QString &address, QPoint point);
	void prepareColor(int index);
	void cardAction(const QString &address, bool add);
	void colorAction(const QString &address, bool apply);
	void addToCard(const CollectibleRecord &record, bool color = false);
	void removeFromCard(const QString &address);
	void applyFailure(const OwnCardFailure &failure);
	void showRefusal(OwnCardChange change);
	void flyToCard(const QString &address, QImage frame);
	void flyToSquare(
		const QString &address,
		Data::StoryArea area,
		QImage frame);
	void startFlight(const QString &address, CardFlightArgs &&args);
	void cancelFlight(const QString &address);
	void dropFlight(const QString &address);
	void holdSticker(const QString &address);
	void releaseSticker(const QString &address);
	void landSticker(
		const QString &address,
		float64 fromScale = 1.,
		QImage frame = QImage(),
		TravelArtwork artwork = TravelArtwork());
	void setReturning(const QString &address, bool returning);
	void setLifted(const QString &address);
	void setGripped(const QString &address);
	void setLeaving(const QString &address, bool leaving);
	void refreshSquare(const QString &address);
	[[nodiscard]] std::optional<FlightPose> dragSource(
		const QString &address) const;
	[[nodiscard]] bool dragFollowing() const;
	void pressAt(const QString &address, QPoint global);
	void pointerMoved(QPoint global);
	void startDrag(QPoint global);
	void endGesture(std::optional<QPoint> release);
	void clearPress();
	[[nodiscard]] bool touchOwned() const;
	[[nodiscard]] int touchContacts() const;
	[[nodiscard]] bool takeTouch(not_null<QTouchEvent*> e);
	void handleTouch(not_null<QTouchEvent*> e);
	void holdTouch();
	void moveHeldTouch(QPoint global);
	void finishHeldTouch(QPoint global);
	void finishWaitingTouch(not_null<QTouchEvent*> e);
	void interruptTouch();
	void watchTouchWindow();
	void observeTouchFrame(not_null<QEvent*> e);
	void dropDrag(const FlightPose &released);
	[[nodiscard]] bool addDragged(
		std::shared_ptr<const Data::StarGift> gift,
		QPointF local);
	void landDrag(uint64 generation, float64 releasedSide);
	void returnDrag();
	void restoreSquare(uint64 generation);
	void vanishDrag();
	void dragFinished(uint64 generation);
	void finishTravel();
	[[nodiscard]] OwnCard &ownCard() const;
	[[nodiscard]] OwnCardState stackedCard() const;
	[[nodiscard]] bool colorSource(const QString &address) const;
	[[nodiscard]] QImage squareFrame(const QString &address) const;
	[[nodiscard]] std::optional<Data::StoryArea> shownArea(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> shownPose(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> squarePose(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> cardPose(
		const Data::StoryArea &area) const;
	[[nodiscard]] auto createGiftButton(const QString &address)
	-> std::unique_ptr<Info::PeerGifts::GiftButton>;
	[[nodiscard]] std::unique_ptr<Ui::AbstractButton> createThumbButton(
		const QString &address);
	void paintThumb(not_null<QWidget*> thumb, const QString &address);

	const not_null<Ui::ScrollArea*> _scroll;
	const std::shared_ptr<Main::SessionShow> _show;
	const std::shared_ptr<CollectibleMedia> _media;
	const CardHost _host;
	Info::PeerGifts::Delegate _delegate;
	const QSize _singleMin;
	CardColorToast _colorToast;
	std::vector<CollectibleRecord> _records;
	base::flat_set<QString> _seen;
	base::flat_map<QString, Square> _squares;
	base::flat_set<QString> _onCard;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::flat_map<QString, Flight> _flights;
	base::flat_set<QString> _held;
	base::flat_set<QString> _returning;
	Drag _drag;
	QString _lifted;
	QString _gripped;
	QString _leaving;
	uint64 _lastFlightId = 0;
	uint64 _lastDragGeneration = 0;
	Press _press;
	QSize _single;
	int _perRow = 0;
	int _left = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _squaresFrom = -1;
	int _squaresTill = -1;
	int _mediaFrom = -1;
	int _mediaTill = -1;
	bool _over = false;
	bool _scrollPressing = false;
	std::vector<std::pair<TouchScreenDevice, int>> _touchContacts;
	std::optional<Touch> _touch;
	base::Timer _touchTimer;
	base::unique_qptr<QObject> _touchFilter;
	std::unique_ptr<CollectiblesHint> _hint;

};

class CollectibleActionButton final : public Ui::RippleButton {
public:
	CollectibleActionButton(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::icon &icon);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

private:
	const style::icon &_icon;
	QString _text;

};

CollectibleActionButton::CollectibleActionButton(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::icon &icon)
: RippleButton(parent, st::defaultRippleAnimation)
, _icon(icon) {
	std::move(text) | rpl::on_next([=](QString value) {
		_text = std::move(value);
		update();
	}, lifetime());
}

void CollectibleActionButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto radius = st::walletCollectibleActionRadius;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);

	_icon.paint(
		p,
		(width() - _icon.width()) / 2,
		st::walletCollectibleActionIconTop,
		width());

	const auto &font = st::semiboldFont;
	const auto text = font->elided(
		_text,
		width() - 2 * st::walletCollectibleActionTextSkip);
	const auto textWidth = font->width(text);
	p.setFont(font);
	p.setPen(st::windowBoldFg);
	p.drawTextLeft(
		(width() - textWidth) / 2,
		st::walletCollectibleActionTextTop,
		width(),
		text,
		textWidth);
}

QImage CollectibleActionButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::walletCollectibleActionRadius);
}

[[nodiscard]] QString CollectibleFragmentUrl(const Gram::NftItem &item) {
	if (item.key.isEmpty()) {
		return QString();
	}
	switch (item.kind) {
	case Gram::NftKind::TelegramGift:
		return u"https://fragment.com/gift/"_q + item.key.toLower();
	case Gram::NftKind::TelegramUsername:
		return u"https://fragment.com/username/"_q + item.key;
	case Gram::NftKind::TelegramNumber:
		return u"https://fragment.com/number/"_q + item.key;
	case Gram::NftKind::Generic:
		break;
	}
	return QString();
}

[[nodiscard]] rpl::producer<QString> CollectibleAboutText(
		const Gram::NftItem &item) {
	if (!item.key.isEmpty()) {
		if (item.kind == Gram::NftKind::TelegramUsername) {
			return tr::lng_wallet_collectible_username_about(
				lt_username,
				rpl::single('@' + item.key));
		} else if (item.kind == Gram::NftKind::TelegramNumber) {
			return tr::lng_wallet_collectible_number_about(
				lt_number,
				rpl::single(Ui::FormatPhone(item.key)));
		}
	}
	return tr::lng_wallet_collectible_nft_about();
}

void AddCollectibleActions(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const Gram::NftItem &item,
		const QString &sellUrl) {
	const auto &padding = st::giveawayGiftCodeBox.buttonPadding;
	const auto row = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::walletCollectibleActionHeight),
		style::margins(
			padding.left(),
			st::walletCollectibleActionsTopSkip,
			padding.right(),
			st::walletCollectibleActionsBottomSkip));
	const auto address = item.address;
	const auto transfer = Ui::CreateChild<CollectibleActionButton>(
		row,
		tr::lng_gift_transfer_button(),
		st::menuIconReplace);
	transfer->setClickedCallback([=] {
		ShowCollectibleTransfer(show, media, address);
	});
	const auto sell = sellUrl.isEmpty()
		? nullptr
		: Ui::CreateChild<CollectibleActionButton>(
			row,
			tr::lng_gift_transfer_sell(),
			st::menuIconTagSell);
	if (sell) {
		sell->setClickedCallback([=] {
			UrlClickHandler::Open(sellUrl);
		});
	}
	row->widthValue(
	) | rpl::on_next([=](int width) {
		const auto height = st::walletCollectibleActionHeight;
		if (!sell) {
			transfer->resize(width, height);
			transfer->moveToLeft(0, 0, width);
			return;
		}
		const auto single = (width - st::walletButtonsSkip) / 2;
		transfer->resize(single, height);
		transfer->moveToLeft(0, 0, width);
		const auto left = single + st::walletButtonsSkip;
		sell->resize(width - left, height);
		sell->moveToLeft(left, 0, width);
	}, row->lifetime());
}

void CollectiblePreviewBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		Gram::NftItem item,
		QString sellUrl) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto address = item.address;
	media->resolve(address);
	const auto artwork = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::walletDetailsCollectibleSize),
		style::margins(
			0,
			st::boxTitleHeight,
			0,
			st::walletDetailsCollectibleNameSkip));
	artwork->setAttribute(Qt::WA_TransparentForMouseEvents);
	artwork->paintRequest(
	) | rpl::on_next([=] {
		const auto side = st::walletDetailsCollectibleSize;
		auto p = Painter(artwork);
		media->paint(
			p,
			address,
			QRect((artwork->width() - side) / 2, 0, side, side),
			artwork->width(),
			st::walletDetailsCollectibleRadius);
	}, artwork->lifetime());

	const auto title = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			st::walletCollectiblePreviewTitle),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			CollectibleAboutText(item),
			st::walletCollectiblePreviewAbout),
		st::walletPhraseTextMargin,
		style::al_top);
	AddCollectibleActions(box, show, media, item, sellUrl);

	const auto apply = [=] {
		title->setMarkedText(CollectibleTitleText(media->view(address)));
	};
	apply();

	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, box->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		artwork->update();
	}, box->lifetime());

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

void AddShowInGiftsCheckbox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<Data::UniqueGift> unique) {
	const auto session = &show->session();
	const auto self = session->userPeerId();
	const auto slug = unique->slug;
	const auto &cst = st::walletCollectibleShowCheckbox;
	const auto checkbox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_wallet_collectible_show_in_gifts(),
			(unique->hostId == self),
			cst),
		st::boxRowPadding + QMargins(
			cst.checkPosition.x(),
			cst.margin.top(),
			0,
			cst.margin.bottom()),
		style::al_top);
	const auto weak = base::make_weak(checkbox);
	checkbox->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		using Notify = Ui::Checkbox::NotifyAboutChange;
		if (checkbox->isDisabled()) {
			checkbox->setChecked(!checked, Notify::DontNotify);
			return;
		}
		checkbox->setDisabled(true);
		const auto restore = [=] {
			if (const auto strong = weak.get()) {
				strong->setDisabled(false);
				strong->setChecked(!checked, Notify::DontNotify);
			}
		};
		using Flag = MTPpayments_DisplayStarGift::Flag;
		session->api().request(MTPpayments_DisplayStarGift(
			MTP_flags(checked ? Flag::f_peer : Flag()),
			checked ? MTP_inputPeerSelf() : MTP_inputPeerEmpty(),
			MTP_string(slug)
		)).done([=](const MTPBool &result) {
			if (!mtpIsTrue(result)) {
				restore();
				if (show->valid()) {
					show->showToast(Lang::Hard::ServerError());
				}
				return;
			}
			unique->hostId = checked ? self : PeerId();
			session->data().notifyGiftUpdate({
				.slug = slug,
				.action = Data::GiftUpdate::Action::HostChange,
			});
			if (const auto strong = weak.get()) {
				strong->setDisabled(false);
			}
		}).fail([=](const MTP::Error &error) {
			restore();
			if (MTP::IgnoreError(error) || !show->valid()) {
				return;
			}
			if (!Ui::ShowGiftErrorToast(show, error)) {
				show->showToast(error.type());
			}
		}).send();
	}, checkbox->lifetime());
}

void Activate(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const CollectibleRecord &item,
		Fn<std::vector<Ui::UniqueGiftCoverAction>(
			not_null<Ui::GenericBox*>)> extra) {
	if (item.gift) {
		const auto weak = std::weak_ptr(media);
		const auto address = item.address;
		const auto slug = item.key;
		const auto unique = item.gift->unique;
		auto actions = std::make_shared<::Settings::UniqueGiftCoverActions>();
		actions->transfer = [=] {
			if (const auto strong = weak.lock()) {
				ShowCollectibleTransfer(show, strong, address, slug, unique);
			}
		};
		actions->extra = std::move(extra);
		actions->aboveButtons = [=](not_null<Ui::GenericBox*> box) {
			if (unique) {
				AddShowInGiftsCheckbox(box, show, unique);
			}
		};
		show->showBox(Box(
			::Settings::GlobalStarGiftBox,
			MakeChatShow(show, false),
			*item.gift,
			::Settings::StarGiftResaleInfo(),
			::Settings::CreditsEntryBoxStyleOverrides(),
			std::move(actions)));
		return;
	}
	show->showBox(Box(
		CollectiblePreviewBox,
		show,
		std::move(media),
		item,
		CollectibleFragmentUrl(item)));
}

[[nodiscard]] QString CardLimitText() {
	return tr::lng_wallet_card_limit(
		tr::now,
		lt_count,
		Data::kWalletCardMaxStickers);
}

[[nodiscard]] QString FailureText(const OwnCardFailure &failure) {
	const auto &type = failure.type;
	if (failure.notReady) {
		return tr::lng_wallet_card_not_ready(tr::now);
	} else if (type == u"WALLET_STICKER_LIMIT_EXCEEDED"_q) {
		return CardLimitText();
	} else if (type == u"WALLET_STICKER_SAVED_PROFILE"_q) {
		return tr::lng_wallet_card_saved_profile(tr::now);
	} else if (type == u"WALLET_NFT_NOT_OWNED"_q) {
		return tr::lng_wallet_collectible_not_owned(tr::now);
	} else if (type == u"WALLET_UNAVAILABLE"_q) {
		return tr::lng_wallet_unavailable(tr::now);
	} else if (type == u"WALLET_CARD_BG_NOT_ALLOWED"_q) {
		return tr::lng_wallet_card_color_not_allowed(tr::now);
	} else if (MTP::IsFloodError(type)) {
		return tr::lng_flood_error(tr::now);
	}
	return ErrorWithType(tr::lng_wallet_card_save_error(tr::now), type);
}

[[nodiscard]] QTouchEvent *TouchScreenEvent(not_null<QEvent*> e) {
	const auto type = e->type();
	if (type != QEvent::TouchBegin
		&& type != QEvent::TouchUpdate
		&& type != QEvent::TouchEnd
		&& type != QEvent::TouchCancel) {
		return nullptr;
	}
	const auto touch = static_cast<QTouchEvent*>(e.get());
	const auto device = touch->device();
	return (device && device->type() == base::TouchDevice::TouchScreen)
		? touch
		: nullptr;
}

void SendTouchCancel(
		not_null<Ui::ScrollArea*> scroll,
		const TouchScreenDevice &device) {
	if (device) {
		auto cancel = TouchCancelFrom(device);
		scroll->viewportEvent(&cancel);
	}
}

CollectiblesGrid::CollectiblesGrid(
	QWidget *parent,
	not_null<Ui::ScrollArea*> scroll,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media,
	CardHost host)
: RpWidget(parent)
, _scroll(scroll)
, _show(std::move(show))
, _media(std::move(media))
, _host(std::move(host))
, _delegate(&_show->session(), Info::PeerGifts::GiftButtonMode::Minimal)
, _singleMin(_delegate.buttonSize())
, _colorToast(_show)
, _touchTimer([=] { holdTouch(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_AcceptTouchEvents);

	_scroll->touchMaybePressing(
	) | rpl::on_next([=](bool pressing) {
		_scrollPressing = pressing;
		if (!pressing && _touch && _touch->phase == TouchPhase::Waiting) {
			_touchTimer.cancel();
		}
	}, lifetime());

	_media->repaint(
	) | rpl::on_next([=](const QString &address) {
		const auto i = _squares.find(address);
		if (i != end(_squares) && i->second.thumb) {
			i->second.thumb->update();
		}
	}, lifetime());

	WindowPaletteChanged(this) | rpl::on_next([=] {
		_delegate.invalidateCache();
		for (const auto &[address, square] : _squares) {
			if (square.thumb) {
				square.thumb->update();
			}
		}
	}, lifetime());

	OwnCardStickersValue(
		&_show->session()
	) | rpl::on_next([=](const std::vector<Data::WalletCardSticker> &list) {
		applyOnCard(list);
	}, lifetime());

	ownCard().failures(
	) | rpl::on_next([=](const OwnCardFailure &failure) {
		applyFailure(failure);
	}, lifetime());

	events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Show)
			|| (e->type() == QEvent::Hide)
			|| (e->type() == QEvent::WindowDeactivate);
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Show) {
			watchTouchWindow();
			// Ancestors finish their layout only after this event.
			crl::on_main(this, [=] {
				if (_hint) {
					_hint->refresh();
				}
			});
			return;
		}
		interruptTouch();
		endGesture(std::nullopt);
		finishTravel();
		if (_hint && e->type() == QEvent::Hide) {
			_hint->refresh();
		}
	}, lifetime());

	_hint = std::make_unique<CollectiblesHint>(
		_host.body,
		&_show->session(),
		[=] { return hintTarget(); });
}

CollectiblesGrid::~CollectiblesGrid() {
	_hint = nullptr;
	_drag = Drag();
	_flights.clear();
	setGripped(QString());
	if (const auto painter = _host.painter.get()) {
		for (const auto &address : _held) {
			painter->release(address);
		}
	}
}

void CollectiblesGrid::rebuild() {
	_records = _show->session().wallet().collectibles();
	invalidateWindows();
	if (const auto width = this->width()) {
		resizeToWidth(width);
	}
	refreshWindows();
}

CollectiblesSquares CollectiblesGrid::squares() {
	const auto weak = base::make_weak(this);
	return {
		.pose = [=](const QString &address) {
			const auto strong = weak.get();
			return strong
				? strong->squarePose(CanonicalAddress(address))
				: std::nullopt;
		},
		.leaving = [=](const QString &address, bool leaving) {
			if (const auto strong = weak.get()) {
				strong->setLeaving(CanonicalAddress(address), leaving);
			}
		},
	};
}

int CollectiblesGrid::resizeGetHeight(int newWidth) {
	const auto skip = st::giftBoxGiftSkip.x();
	const auto margin = st::walletSendButtonMargin;
	const auto available = newWidth - margin.left() - margin.right();
	const auto perRow = (available > 0)
		? std::max((available + skip) / (_singleMin.width() + skip), 1)
		: 0;
	auto single = QSize();
	auto left = 0;
	if (perRow) {
		single = QSize(
			std::min(
				(available + skip) / perRow - skip,
				2 * _singleMin.width()),
			_singleMin.height());
		const auto full = perRow * (single.width() + skip) - skip;
		left = margin.left() + (available - full) / 2;
	}
	if (_perRow != perRow || _single != single || _left != left) {
		_perRow = perRow;
		_single = single;
		_left = left;
		invalidateWindows();
		refreshWindows();
	}
	if (const auto rows = rowsCount()) {
		const auto last = squareRect(rows * _perRow - 1);
		return last.y() + last.height() + st::walletRowsTopSkip;
	}
	return 0;
}

void CollectiblesGrid::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	refreshWindows();
	if (dragFollowing() && _host.bring) {
		_host.bring();
	}
}

bool CollectiblesGrid::eventHook(QEvent *e) {
	const auto touch = TouchScreenEvent(e);
	if (touch
		&& (touch->type() != QEvent::TouchBegin || takeTouch(touch))) {
		touch->accept();
		handleTouch(touch);
		return true;
	}
	return RpWidget::eventHook(e);
}

void CollectiblesGrid::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton || touchOwned()) {
		return;
	}
	endGesture(std::nullopt);
	finishTravel();
	const auto global = e->globalPos();
	const auto index = squareAt(mapFromGlobal(global));
	if (index < 0) {
		return;
	}
	pressAt(_records[index].address, global);
	if (_press.draggable) {
		_press.grabbed = true;
		grabMouse();
	}
}

void CollectiblesGrid::mouseMoveEvent(QMouseEvent *e) {
	if (touchOwned()) {
		return;
	}
	const auto over = (squareAt(e->pos()) >= 0);
	if (_over != over) {
		_over = over;
		setCursor(over ? style::cur_pointer : style::cur_default);
	}
	const auto following = dragFollowing();
	if (!following && _press.address.isEmpty()) {
		return;
	}
	const auto expected = following || _press.grabbed;
	if (!(e->buttons() & Qt::LeftButton)
		|| (expected && mouseGrabber() != this)) {
		endGesture(std::nullopt);
		return;
	}
	pointerMoved(e->globalPos());
}

void CollectiblesGrid::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton || touchOwned()) {
		return;
	}
	const auto global = e->globalPos();
	const auto press = base::take(_press);
	const auto lost = (mouseGrabber() != this)
		&& (press.grabbed || dragFollowing());
	endGesture(lost ? std::nullopt : std::make_optional(global));
	if (lost || press.address.isEmpty()) {
		return;
	}
	const auto index = squareAt(mapFromGlobal(global));
	if (index >= 0 && _records[index].address == press.address) {
		activate(press.address);
	}
}

bool CollectiblesGrid::touchOwned() const {
	return _touch && (_touch->phase != TouchPhase::Ignored);
}

int CollectiblesGrid::touchContacts() const {
	auto result = 0;
	for (const auto &[device, contacts] : _touchContacts) {
		result += contacts;
	}
	return result;
}

bool CollectiblesGrid::takeTouch(not_null<QTouchEvent*> e) {
	const auto device = DeviceOf(e);
	if (_touch && _touch->device && _touch->device != device) {
		interruptTouch();
		return false;
	}
	interruptTouch();
	_touch = std::nullopt;
	const auto &points = e->touchPoints();
	if (points.size() != 1
		|| touchContacts() > 1
		|| !_press.address.isEmpty()
		|| dragFollowing()) {
		return false;
	}
	const auto &point = points.front();
	const auto origin = point.screenPos().toPoint();
	const auto index = squareAt(mapFromGlobal(origin));
	if (index < 0 || !dragSource(_records[index].address)) {
		return false;
	}
	_touch = Touch{
		.address = _records[index].address,
		.origin = origin,
		.device = device,
		.id = point.id(),
	};
	return true;
}

void CollectiblesGrid::handleTouch(not_null<QTouchEvent*> e) {
	const auto type = e->type();
	if (!_touch || _touch->device != DeviceOf(e)) {
		return;
	} else if (type == QEvent::TouchCancel) {
		interruptTouch();
		_touch = std::nullopt;
		return;
	}
	const auto &points = e->touchPoints();
	const auto i = ranges::find(
		points,
		_touch->id,
		&QTouchEvent::TouchPoint::id);
	if (points.size() != 1 || i == points.end()) {
		interruptTouch();
	}
	const auto end = (type == QEvent::TouchEnd);
	switch (_touch->phase) {
	case TouchPhase::Waiting:
		if (end) {
			finishWaitingTouch(e);
		} else {
			_scroll->viewportEvent(e);
			if (type == QEvent::TouchBegin && _scrollPressing) {
				_touchTimer.callOnce(QApplication::startDragTime());
			}
		}
		break;
	case TouchPhase::Held: {
		const auto global = i->screenPos().toPoint();
		moveHeldTouch(global);
		if (end) {
			finishHeldTouch(global);
		}
	} break;
	case TouchPhase::Ignored:
		if (end) {
			_touch = std::nullopt;
		}
		break;
	}
}

void CollectiblesGrid::holdTouch() {
	if (!_touch || _touch->phase != TouchPhase::Waiting) {
		return;
	}
	finishTravel();
	const auto address = _touch->address;
	const auto origin = _touch->origin;
	const auto index = squareAt(mapFromGlobal(origin));
	if (index < 0
		|| _records[index].address != address
		|| !dragSource(address)) {
		return;
	}
	pressAt(address, origin);
	_touch->phase = TouchPhase::Held;
	SendTouchCancel(_scroll, _touch->device);
}

void CollectiblesGrid::moveHeldTouch(QPoint global) {
	if ((global - _touch->origin).manhattanLength()
		>= QApplication::startDragDistance()) {
		_touch->moved = true;
	}
	pointerMoved(global);
}

void CollectiblesGrid::finishHeldTouch(QPoint global) {
	const auto still = !base::take(_touch)->moved;
	const auto address = base::take(_press).address;
	endGesture(global);
	if (still) {
		showCardMenu(address, global);
	}
}

void CollectiblesGrid::finishWaitingTouch(not_null<QTouchEvent*> e) {
	const auto touch = *base::take(_touch);
	_touchTimer.cancel();
	const auto index = squareAt(mapFromGlobal(touch.origin));
	// The scroll clicks whatever square has moved under the finger.
	if (_scrollPressing
		&& (index < 0 || _records[index].address != touch.address)) {
		SendTouchCancel(_scroll, touch.device);
		return;
	}
	_scroll->viewportEvent(e);
}

void CollectiblesGrid::interruptTouch() {
	if (!touchOwned()) {
		return;
	}
	const auto waiting = (_touch->phase == TouchPhase::Waiting);
	_touch->phase = TouchPhase::Ignored;
	_touchTimer.cancel();
	if (waiting) {
		SendTouchCancel(_scroll, _touch->device);
	} else {
		endGesture(std::nullopt);
	}
}

void CollectiblesGrid::watchTouchWindow() {
	_touchContacts.clear();
	_touchFilter = nullptr;
	// WHY: Qt splits a second finger over a sibling widget away from the
	// grid, so only the window's own frames show every finger down.
	if (const auto handle = window()->windowHandle()) {
		_touchFilter.reset(base::install_event_filter(
			handle,
			[=](not_null<QEvent*> e) {
				observeTouchFrame(e);
				return base::EventFilterResult::Continue;
			}));
	}
}

void CollectiblesGrid::observeTouchFrame(not_null<QEvent*> e) {
	const auto touch = TouchScreenEvent(e);
	if (!touch) {
		return;
	}
	const auto device = DeviceOf(touch);
	const auto contacts = int(ranges::count_if(
		touch->touchPoints(),
		[](const QTouchEvent::TouchPoint &point) {
			return (int(point.state()) != int(Qt::TouchPointReleased));
		}));
	const auto outdated = [&](const auto &entry) {
		return !entry.first || (entry.first == device);
	};
	_touchContacts.erase(
		ranges::remove_if(_touchContacts, outdated),
		end(_touchContacts));
	if (contacts > 0) {
		_touchContacts.emplace_back(device, contacts);
	} else if (_touch && !touchOwned() && _touch->device == device) {
		_touch = std::nullopt;
	}
	if (_touch && !_touch->device) {
		interruptTouch();
		_touch = std::nullopt;
	}
	if (touchContacts() > 1) {
		interruptTouch();
	}
}

std::optional<FlightPose> CollectiblesGrid::dragSource(
		const QString &address) const {
	const auto index = indexOf(address);
	if (index < 0) {
		return std::nullopt;
	}
	const auto &gift = _records[index].gift;
	if (!gift
		|| !gift->unique
		|| !_host.painter
		|| ownCard().contains(address)
		|| _flights.contains(address)
		|| _held.contains(address)
		|| _returning.contains(address)
		|| _leaving == address) {
		return std::nullopt;
	}
	return squarePose(address);
}

bool CollectiblesGrid::dragFollowing() const {
	return _drag.gift && _drag.gift->following();
}

void CollectiblesGrid::pressAt(const QString &address, QPoint global) {
	const auto source = dragSource(address);
	_press = Press{
		.address = address,
		.origin = global,
		.grab = (source
			? (QPointF(_host.body->mapFromGlobal(global)) - source->center)
			: QPointF()),
		.draggable = source.has_value(),
	};
}

void CollectiblesGrid::pointerMoved(QPoint global) {
	if (dragFollowing()) {
		_drag.gift->move(_host.body->mapFromGlobal(global));
	} else if (_press.draggable
		&& ((global - _press.origin).manhattanLength()
			>= QApplication::startDragDistance())) {
		startDrag(global);
	}
}

void CollectiblesGrid::startDrag(QPoint global) {
	_press.draggable = false;
	const auto address = _press.address;
	const auto source = dragSource(address);
	const auto i = source ? _squares.find(address) : end(_squares);
	const auto button = (i != end(_squares)) ? i->second.gift.get() : nullptr;
	auto player = button ? button->takePlayer() : nullptr;
	const auto frame = button ? button->currentStickerFrame() : QImage();
	if (frame.isNull()) {
		return;
	}
	const auto document = i->second.record.gift->document.get();
	const auto press = base::take(_press);
	holdSticker(address);
	setLifted(address);
	_drag = Drag{
		.address = address,
		.document = document,
		.firstFrame = frame,
		.generation = ++_lastDragGeneration,
		.gift = std::make_unique<DraggedGift>(DraggedGiftArgs{
			.body = _host.body,
			.titleExtension = st::separatePanelTitleHeight,
			.source = *source,
			.cursor = _host.body->mapFromGlobal(global),
			.grab = press.grab,
			.player = std::move(player),
			.firstFrame = frame,
			.document = document,
			.frameSize = st::giftBoxStickerSize,
			.outline = true,
		}),
	};
	setGripped(address);
	if (_hint) {
		_hint->record();
	}
	if (_host.bring) {
		_host.bring();
	}
}

void CollectiblesGrid::endGesture(std::optional<QPoint> release) {
	const auto following = dragFollowing();
	_press = Press();
	auto released = std::optional<FlightPose>();
	if (following && release) {
		_drag.gift->move(_host.body->mapFromGlobal(*release));
		released = _drag.gift->pose();
	}
	if (mouseGrabber() == this) {
		releaseMouse();
	}
	if (!following || !dragFollowing()) {
		return;
	} else if (released) {
		dropDrag(*released);
	} else {
		returnDrag();
	}
}

void CollectiblesGrid::clearPress() {
	if (base::take(_press).grabbed && mouseGrabber() == this) {
		releaseMouse();
	}
}

void CollectiblesGrid::dropDrag(const FlightPose &released) {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	const auto index = indexOf(address);
	const auto gift = (index >= 0) ? _records[index].gift : nullptr;
	if (!gift || !gift->unique || ownCard().contains(address)) {
		vanishDrag();
		return;
	}
	const auto fold = _host.fold();
	const auto local = CardFoldLocal(fold, released.center);
	if (!local
		|| fold.opacity <= 0.
		|| !CardFoldOutline(fold).containsPoint(
			released.center,
			Qt::OddEvenFill)) {
		returnDrag();
		return;
	}
	if (!addDragged(gift, *local)) {
		returnDrag();
		return;
	}
	landDrag(generation, released.side);
	_drag.gift = nullptr;
}

bool CollectiblesGrid::addDragged(
		std::shared_ptr<const Data::StarGift> gift,
		QPointF local) {
	auto &own = ownCard();
	const auto stickers = own.shown().stickers;
	const auto card = _host.fold().rest.size();
	const auto center = CardStickerClampedCenter(local, card);
	if (!own.ready()) {
		showRefusal(OwnCardChange::NotReady);
		return false;
	} else if (int(stickers.size()) >= Data::kWalletCardMaxStickers) {
		showRefusal(OwnCardChange::Limit);
		return false;
	} else if (!center) {
		return false;
	}
	auto desired = stackedCard();
	desired.stickers.push_back({
		.address = _drag.address,
		.area = CardStickerArea(
			*center,
			CardStickerPreferredSide(card),
			0.,
			card),
		.gift = std::move(gift),
	});
	const auto change = own.save(std::move(desired));
	if (change != OwnCardChange::Applied) {
		showRefusal(change);
		return false;
	}
	setLifted(QString());
	return true;
}

void CollectiblesGrid::landDrag(uint64 generation, float64 releasedSide) {
	if (_drag.generation != generation) {
		return;
	}
	const auto address = _drag.address;
	auto artwork = _drag.gift ? _drag.gift->takeArtwork() : TravelArtwork();
	if (const auto target = shownPose(address)) {
		const auto scale = releasedSide / target->side;
		landSticker(address, scale, QImage(), std::move(artwork));
	} else {
		releaseSticker(address);
	}
	dragFinished(generation);
}

void CollectiblesGrid::returnDrag() {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	if (!squarePose(address)) {
		vanishDrag();
		return;
	}
	_drag.gift->returnTo([=] {
		return squarePose(address);
	}, [=](bool arrived) {
		if (arrived) {
			restoreSquare(generation);
		}
		dragFinished(generation);
	});
	releaseSticker(address);
}

void CollectiblesGrid::restoreSquare(uint64 generation) {
	if (_drag.generation != generation || !_drag.gift || !_drag.document) {
		return;
	}
	auto artwork = _drag.gift->takeArtwork();
	const auto i = _squares.find(_drag.address);
	if (i != end(_squares) && i->second.gift) {
		i->second.gift->restorePlayer(
			not_null(_drag.document),
			std::move(artwork.playback.player),
			_drag.firstFrame);
	}
}

void CollectiblesGrid::vanishDrag() {
	const auto address = _drag.address;
	const auto generation = _drag.generation;
	_drag.gift->vanish([=] { dragFinished(generation); });
	setLifted(QString());
	releaseSticker(address);
}

void CollectiblesGrid::dragFinished(uint64 generation) {
	if (_drag.generation != generation) {
		return;
	}
	setLifted(QString());
	setGripped(QString());
	crl::on_main(this, [=] {
		if (_drag.generation == generation) {
			_drag = Drag();
		}
	});
}

void CollectiblesGrid::finishTravel() {
	if (_drag.gift) {
		_drag.gift->finish();
	}
}

int CollectiblesGrid::rowsCount() const {
	return _perRow
		? ((int(_records.size()) + _perRow - 1) / _perRow)
		: 0;
}

std::pair<int, int> CollectiblesGrid::rowsRange(
		int top,
		int bottom,
		int preload) const {
	const auto rows = rowsCount();
	if (!rows) {
		return { 0, 0 };
	}
	const auto padding = _delegate.buttonExtend().top();
	const auto pitch = _single.height() + st::giftBoxGiftSkip.y();
	const auto from = std::clamp(
		std::max(top - padding, 0) / pitch - preload,
		0,
		rows);
	const auto till = std::clamp(
		(std::max(bottom - padding, 0) + pitch - 1) / pitch + preload,
		from,
		rows);
	return { from, till };
}

QRect CollectiblesGrid::squareRect(int index) const {
	if (!_perRow || index < 0) {
		return QRect();
	}
	const auto skip = st::giftBoxGiftSkip;
	const auto column = index % _perRow;
	const auto row = index / _perRow;
	return QRect(
		QPoint(
			_left + column * (_single.width() + skip.x()),
			(_delegate.buttonExtend().top()
				+ row * (_single.height() + skip.y()))),
		_single);
}

int CollectiblesGrid::squareAt(QPoint point) const {
	if (!_perRow) {
		return -1;
	}
	const auto skip = st::giftBoxGiftSkip;
	const auto x = point.x() - _left;
	const auto y = point.y() - _delegate.buttonExtend().top();
	if (x < 0 || y < 0) {
		return -1;
	}
	const auto column = x / (_single.width() + skip.x());
	const auto index = (y / (_single.height() + skip.y())) * _perRow
		+ column;
	if (column >= _perRow || index >= int(_records.size())) {
		return -1;
	}
	return squareRect(index).contains(point) ? index : -1;
}

int CollectiblesGrid::indexOf(const QString &address) const {
	const auto i = ranges::find(
		_records,
		address,
		&CollectibleRecord::address);
	return (i != end(_records)) ? int(i - begin(_records)) : -1;
}

std::optional<QRect> CollectiblesGrid::hintTarget() const {
	const auto gift = [](const CollectibleRecord &record) {
		return (record.gift != nullptr);
	};
	const auto i = ranges::find_if(_records, gift);
	if (i == end(_records)) {
		return std::nullopt;
	}
	const auto j = _squares.find(i->address);
	const auto button = (j != end(_squares)) ? j->second.gift.get() : nullptr;
	if (!button || !button->isVisible()) {
		return std::nullopt;
	}
	const auto index = int(i - begin(_records));
	const auto shown = button->visibleRegion().translated(button->pos())
		& squareRect(index);
	const auto inner = shown.boundingRect();
	if (inner.isEmpty()) {
		return std::nullopt;
	}
	return QRect(mapTo(_host.body.get(), inner.topLeft()), inner.size());
}

void CollectiblesGrid::invalidateWindows() {
	_squaresFrom = _squaresTill = -1;
	_mediaFrom = _mediaTill = -1;
}

void CollectiblesGrid::refreshWindows() {
	refreshSquares();
	refreshMediaWindow();
	if (_hint) {
		_hint->refresh();
	}
}

void CollectiblesGrid::refreshSquares() {
	const auto [from, till] = rowsRange(
		_visibleTop,
		_visibleBottom,
		kPreloadRows);
	if (_squaresFrom == from && _squaresTill == till) {
		return;
	}
	_squaresFrom = from;
	_squaresTill = till;

	auto old = base::take(_squares);
	auto now = base::flat_map<QString, Square>();
	const auto extend = _delegate.buttonExtend();
	const auto last = std::min(till * _perRow, int(_records.size()));
	for (auto index = from * _perRow; index < last; ++index) {
		const auto &record = _records[index];
		const auto i = old.find(record.address);
		auto square = (i != end(old)) ? std::move(i->second) : Square();
		applySquare(square, record);
		const auto inner = squareRect(index);
		if (square.gift) {
			square.gift->setGeometry(inner, extend);
		} else {
			square.thumb->setGeometry(inner.marginsAdded(extend));
		}
		now.emplace(record.address, std::move(square));
	}
	_squares = std::move(now);
	if (!_press.address.isEmpty() && !_squares.contains(_press.address)) {
		clearPress();
	}
}

void CollectiblesGrid::refreshMediaWindow() {
	const auto page = std::max(_visibleBottom - _visibleTop, 0);
	const auto [from, till] = rowsRange(
		_visibleTop,
		_visibleBottom + page * kPreloadScreens,
		0);
	if (_mediaFrom == from && _mediaTill == till) {
		return;
	}
	_mediaFrom = from;
	_mediaTill = till;

	auto ordered = std::vector<QString>();
	const auto last = std::min(till * _perRow, int(_records.size()));
	for (auto index = from * _perRow; index < last; ++index) {
		const auto &record = _records[index];
		if (!record.gift) {
			ordered.push_back(record.address);
		}
	}
	_media->setListWindow(std::move(ordered));
}

void CollectiblesGrid::applyOnCard(
		const std::vector<Data::WalletCardSticker> &stickers) {
	auto onCard = base::flat_set<QString>();
	for (const auto &sticker : stickers) {
		auto address = CanonicalAddress(sticker.address);
		if (!address.isEmpty()) {
			onCard.emplace(std::move(address));
		}
	}
	if (_onCard == onCard) {
		return;
	}
	_onCard = std::move(onCard);
	for (auto &[address, square] : _squares) {
		if (square.gift) {
			const auto record = square.record;
			applySquare(square, record);
		}
	}
}

void CollectiblesGrid::applySquare(
		Square &square,
		const CollectibleRecord &record) {
	const auto kindChanged = record.gift
		? (square.thumb != nullptr)
		: (square.gift != nullptr);
	if (kindChanged && _press.address == record.address) {
		clearPress();
	}
	if (!record.gift) {
		square.gift = nullptr;
		if (!square.thumb) {
			square.thumb = createThumbButton(record.address);
		}
	} else {
		square.thumb = nullptr;
		const auto fresh = !square.gift;
		if (fresh) {
			square.gift = createGiftButton(record.address);
		}
		const auto leaving = (_leaving == record.address);
		const auto onCard = !leaving
			&& (_onCard.contains(record.address)
				|| _returning.contains(record.address));
		const auto silhouette = leaving || (_lifted == record.address);
		// A reparse allocates the gift anew, equal values keep the player.
		if (fresh
			|| !SameServedGift(square.record, record)
			|| square.onCard != onCard
			|| square.silhouette != silhouette) {
			square.gift->setDescriptor(
				Info::PeerGifts::GiftTypeStars{
					.info = *record.gift,
					.mine = true,
					.onCard = onCard,
					.silhouette = silhouette,
				},
				Info::PeerGifts::GiftButtonMode::Minimal);
		}
		square.onCard = onCard;
		square.silhouette = silhouette;
	}
	square.record = record;
}

void CollectiblesGrid::activate(const QString &address) {
	const auto index = indexOf(address);
	if (index >= 0) {
		const auto record = _records[index];
		const auto weak = base::make_weak(this);
		const auto extra = [=](not_null<Ui::GenericBox*> box) {
			const auto strong = weak.get();
			return strong
				? strong->coverActions(box, address)
				: std::vector<Ui::UniqueGiftCoverAction>();
		};
		Activate(_show, _media, record, extra);
	}
}

std::optional<CoverCardState> CollectiblesGrid::coverState(
		const QString &address) const {
	const auto index = indexOf(address);
	if (index < 0 || !_records[index].gift) {
		return std::nullopt;
	}
	const auto collections = CardColorCollections(&_show->session());
	return CoverCardState{
		.onCard = ownCard().contains(address),
		.source = colorSource(address),
		.allowed = CardColorAllowed(_records[index], collections),
		.listed = !collections.empty(),
	};
}

std::vector<Ui::UniqueGiftCoverAction> CollectiblesGrid::coverActions(
		not_null<Ui::GenericBox*> box,
		const QString &address) {
	const auto initial = coverState(address);
	if (!initial) {
		return {};
	}
	const auto weak = base::make_weak(this);
	const auto state = box->lifetime().make_state<
		rpl::variable<CoverCardState>>(*initial);
	rpl::merge(
		ownCard().shownValue() | rpl::to_empty,
		_show->session().appConfig().refreshed()
	) | rpl::on_next([=] {
		if (const auto strong = weak.get()) {
			if (const auto now = strong->coverState(address)) {
				*state = *now;
			}
		}
	}, box->lifetime());
	const auto flag = [=](auto test) {
		return state->value()
			| rpl::map(test)
			| rpl::distinct_until_changed();
	};
	flag([](const CoverCardState &now) {
		return !now.source && now.allowed;
	}) | rpl::filter([](bool available) {
		return available;
	}) | rpl::on_next([=] {
		if (const auto strong = weak.get()) {
			const auto index = strong->indexOf(address);
			if (index >= 0) {
				strong->prepareColor(index);
			}
		}
	}, box->lifetime());
	const auto source = [](const CoverCardState &now) {
		return now.source;
	};
	const auto onCard = [](const CoverCardState &now) {
		return now.onCard;
	};
	auto result = std::vector<Ui::UniqueGiftCoverAction>();
	result.push_back({
		.text = flag(source) | rpl::map([](bool remove) {
			return remove
				? tr::lng_wallet_card_remove_color()
				: tr::lng_wallet_card_apply_color();
		}) | rpl::flatten_latest(),
		.icon = flag(source) | rpl::map([](bool remove) {
			return remove
				? &st::walletCoverColorOffIcon
				: &st::menuIconPalette;
		}),
		.callback = crl::guard(this, [=] {
			coverColorPressed(box, address);
		}),
		.faded = flag([](const CoverCardState &now) {
			return !now.source && !now.allowed;
		}),
		.shown = flag([](const CoverCardState &now) {
			return now.source || now.listed;
		}),
	});
	result.push_back({
		.text = flag(onCard) | rpl::map([](bool remove) {
			return remove
				? tr::lng_wallet_card_remove()
				: tr::lng_wallet_card_add();
		}) | rpl::flatten_latest(),
		.icon = flag(onCard) | rpl::map([](bool remove) {
			return remove
				? &st::walletCoverCardOffIcon
				: &st::walletCoverCardAddIcon;
		}),
		.callback = crl::guard(this, [=] {
			coverCardPressed(box, address);
		}),
	});
	return result;
}

void CollectiblesGrid::coverColorPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address) {
	const auto now = coverState(address);
	if (!now || _leaving == address) {
		return;
	}
	const auto apply = !now->source;
	if (apply && !now->allowed) {
		colorAction(address, true);
		return;
	}
	closeCoverThen(box, [=] { colorAction(address, apply); });
}

void CollectiblesGrid::coverCardPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address) {
	const auto now = coverState(address);
	if (!now || _leaving == address) {
		return;
	}
	const auto add = !now->onCard;
	closeCoverThen(box, [=] { cardAction(address, add); });
}

void CollectiblesGrid::closeCoverThen(
		not_null<Ui::GenericBox*> box,
		Fn<void()> action) {
	const auto weak = base::make_weak(this);
	box->closeBox();
	base::call_delayed(st::boxDuration, weak, std::move(action));
}

void CollectiblesGrid::showCardMenu(const QString &address, QPoint point) {
	const auto index = indexOf(address);
	if (_menu
		|| index < 0
		|| !_records[index].gift
		|| _leaving == address
		|| !_press.address.isEmpty()
		|| dragFollowing()) {
		return;
	}
	const auto add = !ownCard().contains(address);
	const auto apply = !colorSource(address);
	const auto collections = CardColorCollections(&_show->session());
	const auto allowed = CardColorAllowed(_records[index], collections);
	if (apply && allowed) {
		prepareColor(index);
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	if (!apply || allowed) {
		_menu->addAction(
			Ui::Text::FixAmpersandInAction(apply
				? tr::lng_wallet_card_apply_color(tr::now)
				: tr::lng_wallet_card_remove_color(tr::now)),
			[=] { colorAction(address, apply); },
			&st::menuIconPalette);
	}
	_menu->addAction(
		Ui::Text::FixAmpersandInAction(add
			? tr::lng_wallet_card_add(tr::now)
			: tr::lng_wallet_card_remove(tr::now)),
		[=] { cardAction(address, add); },
		&st::menuIconStickers);
	const auto scope = WindowPaletteScope(_menu.get());
	_menu->popup(point);
}

void CollectiblesGrid::prepareColor(int index) {
	if (!_host.prepareColor) {
		return;
	}
	const auto &record = _records[index];
	const auto sticker = Data::WalletCardSticker{
		.address = record.address,
		.gift = record.gift,
	};
	_host.prepareColor(ResolveCardLook({ sticker }, record.address));
}

void CollectiblesGrid::cardAction(const QString &address, bool add) {
	const auto index = indexOf(address);
	if (index < 0 || !_records[index].gift || _leaving == address) {
		return;
	}
	const auto onCard = ownCard().contains(address);
	if (onCard == add) {
		return;
	} else if (add) {
		addToCard(_records[index]);
	} else {
		removeFromCard(address);
	}
}

void CollectiblesGrid::colorAction(const QString &address, bool apply) {
	const auto index = indexOf(address);
	if (index < 0
		|| !_records[index].gift
		|| _leaving == address
		|| colorSource(address) == apply) {
		return;
	}
	if (apply) {
		const auto collections = CardColorCollections(&_show->session());
		if (!CardColorAllowed(_records[index], collections)) {
			_colorToast.show(collections, _records);
			return;
		} else if (!ownCard().contains(address)) {
			addToCard(_records[index], true);
			return;
		}
	}
	auto desired = stackedCard();
	desired.background = apply ? address : QString();
	showRefusal(ownCard().save(std::move(desired)));
}

void CollectiblesGrid::addToCard(
		const CollectibleRecord &record,
		bool color) {
	auto &own = ownCard();
	const auto stickers = own.shown().stickers;
	if (!own.ready()) {
		showRefusal(OwnCardChange::NotReady);
		return;
	} else if (int(stickers.size()) >= Data::kWalletCardMaxStickers) {
		showRefusal(OwnCardChange::Limit);
		return;
	}
	const auto area = CardStickerAutoArea(
		stickers | ranges::views::transform(
			&Data::WalletCardSticker::area
		) | ranges::to_vector,
		_host.fold().rest.size(),
		style::DevicePixelRatio());
	if (!area) {
		return;
	}
	const auto address = record.address;
	auto frame = squareFrame(address);
	cancelFlight(address);
	holdSticker(address);
	auto desired = stackedCard();
	desired.stickers.push_back({
		.address = address,
		.area = *area,
		.gift = record.gift,
	});
	if (color) {
		desired.background = address;
	}
	const auto change = own.save(std::move(desired));
	if (change != OwnCardChange::Applied) {
		releaseSticker(address);
		showRefusal(change);
		return;
	} else if (const auto painter = _host.painter.get()) {
		painter->raise(address);
	}
	flyToCard(address, std::move(frame));
}

void CollectiblesGrid::removeFromCard(const QString &address) {
	auto &own = ownCard();
	const auto area = shownArea(address);
	if (!area) {
		return;
	} else if (!own.ready()) {
		showRefusal(OwnCardChange::NotReady);
		return;
	}
	auto frame = squareFrame(address);
	dropFlight(address);
	setReturning(address, true);
	auto desired = stackedCard();
	const auto change = RemoveOwnCardSticker(desired, address)
		? own.save(std::move(desired))
		: OwnCardChange::Unchanged;
	releaseSticker(address);
	if (change != OwnCardChange::Applied) {
		setReturning(address, false);
		showRefusal(change);
		return;
	}
	flyToSquare(address, *area, std::move(frame));
}

void CollectiblesGrid::applyFailure(const OwnCardFailure &failure) {
	for (const auto &sticker : failure.left) {
		const auto address = CanonicalAddress(sticker.address);
		auto frame = squareFrame(address);
		cancelFlight(address);
		flyToSquare(address, sticker.area, std::move(frame));
	}
	for (const auto &sticker : failure.returned) {
		const auto address = CanonicalAddress(sticker.address);
		cancelFlight(address);
		if (cardPose(sticker.area)) {
			landSticker(address);
		}
	}
	if (!failure.silent) {
		_show->showToast(FailureText(failure));
	}
}

void CollectiblesGrid::showRefusal(OwnCardChange change) {
	const auto text = OwnCardRefusalText(change);
	if (!text.isEmpty()) {
		_show->showToast(text);
	}
}

void CollectiblesGrid::flyToCard(const QString &address, QImage frame) {
	const auto card = [=] { return shownPose(address); };
	const auto eligible = !frame.isNull()
		&& !anim::Disabled()
		&& squarePose(address).has_value();
	if (eligible && _host.bring) {
		_host.bring();
	}
	if (!card()) {
		releaseSticker(address);
		return;
	} else if (!eligible) {
		landSticker(address);
		return;
	}
	startFlight(address, {
		.body = _host.body,
		.image = frame,
		.square = [=] { return squarePose(address); },
		.card = card,
		.done = [=](bool arrived) {
			const auto target = arrived ? card() : std::nullopt;
			if (target) {
				const auto side = st::giftBoxStickerSize.width();
				landSticker(address, side / target->side, frame);
			} else {
				releaseSticker(address);
			}
		},
	});
}

void CollectiblesGrid::flyToSquare(
		const QString &address,
		Data::StoryArea area,
		QImage frame) {
	setReturning(address, true);
	const auto card = [=] { return cardPose(area); };
	if (frame.isNull()
		|| anim::Disabled()
		|| !card()
		|| !squarePose(address)) {
		setReturning(address, false);
		return;
	}
	startFlight(address, {
		.body = _host.body,
		.image = std::move(frame),
		.square = [=] { return squarePose(address); },
		.card = card,
		.direction = CardFlightDirection::ToSquare,
		.done = [=](bool) { setReturning(address, false); },
	});
}

void CollectiblesGrid::startFlight(
		const QString &address,
		CardFlightArgs &&args) {
	const auto id = ++_lastFlightId;
	args.done = [=, done = std::move(args.done)](bool arrived) {
		crl::on_main(this, [=] {
			const auto i = _flights.find(address);
			if (i != end(_flights) && i->second.id == id) {
				_flights.erase(i);
			}
		});
		if (done) {
			done(arrived);
		}
	};
	auto flight = std::make_unique<CardFlight>(std::move(args));
	const auto raw = flight.get();
	_flights[address] = Flight{ std::move(flight), id };
	raw->start();
}

void CollectiblesGrid::cancelFlight(const QString &address) {
	dropFlight(address);
	releaseSticker(address);
	setReturning(address, false);
}

void CollectiblesGrid::dropFlight(const QString &address) {
	_flights.remove(address);
	if (_drag.gift && !_drag.gift->following() && _drag.address == address) {
		_drag = Drag();
		setLifted(QString());
		setGripped(QString());
	}
}

void CollectiblesGrid::holdSticker(const QString &address) {
	if (const auto painter = _host.painter.get()) {
		painter->hold(address);
		_held.emplace(address);
	}
}

void CollectiblesGrid::releaseSticker(const QString &address) {
	if (_held.remove(address)) {
		if (const auto painter = _host.painter.get()) {
			painter->release(address);
		}
	}
}

void CollectiblesGrid::landSticker(
		const QString &address,
		float64 fromScale,
		QImage frame,
		TravelArtwork artwork) {
	_held.remove(address);
	if (const auto painter = _host.painter.get()) {
		painter->land(
			address,
			fromScale,
			std::move(frame),
			std::move(artwork));
	}
}

void CollectiblesGrid::setReturning(const QString &address, bool returning) {
	const auto changed = returning
		? _returning.emplace(address).second
		: _returning.remove(address);
	if (changed) {
		refreshSquare(address);
	}
}

void CollectiblesGrid::setLifted(const QString &address) {
	const auto was = std::exchange(_lifted, address);
	if (was != address) {
		refreshSquare(was);
		refreshSquare(address);
	}
}

void CollectiblesGrid::setGripped(const QString &address) {
	const auto was = std::exchange(_gripped, address);
	if (was == address) {
		return;
	} else if (const auto painter = _host.painter.get()) {
		if (!address.isEmpty()) {
			painter->grip(address);
		}
		if (!was.isEmpty()) {
			painter->ungrip(was);
		}
	}
}

void CollectiblesGrid::setLeaving(const QString &address, bool leaving) {
	if (!leaving && _leaving != address) {
		return;
	}
	const auto now = leaving ? address : QString();
	const auto was = std::exchange(_leaving, now);
	if (was != now) {
		refreshSquare(was);
		refreshSquare(now);
	}
}

void CollectiblesGrid::refreshSquare(const QString &address) {
	const auto i = _squares.find(address);
	if (i != end(_squares) && i->second.gift) {
		const auto record = i->second.record;
		applySquare(i->second, record);
	}
}

OwnCard &CollectiblesGrid::ownCard() const {
	return _show->session().wallet().ownCard();
}

OwnCardState CollectiblesGrid::stackedCard() const {
	auto result = ownCard().shown();
	if (const auto painter = _host.painter.get()) {
		StackCardStickers(result.stickers, painter->stack());
	}
	return result;
}

bool CollectiblesGrid::colorSource(const QString &address) const {
	const auto &own = ownCard();
	return own.contains(address)
		&& (CanonicalAddress(own.shown().background) == address);
}

QImage CollectiblesGrid::squareFrame(const QString &address) const {
	const auto i = _squares.find(address);
	return (i != end(_squares) && i->second.gift)
		? i->second.gift->currentStickerFrame()
		: QImage();
}

std::optional<Data::StoryArea> CollectiblesGrid::shownArea(
		const QString &address) const {
	for (const auto &sticker : ownCard().shown().stickers) {
		if (CanonicalAddress(sticker.address) == address) {
			return sticker.area;
		}
	}
	return std::nullopt;
}

std::optional<FlightPose> CollectiblesGrid::shownPose(
		const QString &address) const {
	const auto area = shownArea(address);
	return area ? cardPose(*area) : std::nullopt;
}

std::optional<FlightPose> CollectiblesGrid::squarePose(
		const QString &address) const {
	const auto index = indexOf(address);
	const auto i = _squares.find(address);
	if (index < 0
		|| i == end(_squares)
		|| !i->second.gift
		|| !i->second.gift->isVisible()) {
		return std::nullopt;
	}
	const auto side = float64(st::giftBoxStickerSize.width());
	const auto center = QRectF(squareRect(index)).center();
	if (center.y() + side / 2. <= _visibleTop
		|| center.y() - side / 2. >= _visibleBottom) {
		return std::nullopt;
	}
	return FlightPose{
		.center = mapTo(_host.body.get(), center),
		.side = side,
	};
}

std::optional<FlightPose> CollectiblesGrid::cardPose(
		const Data::StoryArea &area) const {
	if (!_host.painter) {
		return std::nullopt;
	}
	const auto fold = _host.fold();
	if (!fold.valid || fold.opacity <= 0.) {
		return std::nullopt;
	}
	const auto size = fold.rest.size();
	const auto local = CardStickerCenter(area, size);
	return FlightPose{
		.center = CardFoldPoint(fold, local),
		.side = CardStickerSide(area, size) * CardFoldScale(fold, local),
		.rotation = CardStickerRotation(area),
	};
}

auto CollectiblesGrid::createGiftButton(const QString &address)
-> std::unique_ptr<Info::PeerGifts::GiftButton> {
	auto result = std::make_unique<Info::PeerGifts::GiftButton>(
		this,
		&_delegate);
	const auto raw = result.get();
	raw->setKeepFirstFrame(true);
	raw->setClickedCallback([=] {
		activate(address);
	});
	raw->contextMenuRequests(
	) | rpl::on_next([=](QPoint point) {
		showCardMenu(address, point);
	}, raw->lifetime());
	base::install_event_filter(raw, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ContextMenu) {
			return base::EventFilterResult::Continue;
		}
		const auto context = static_cast<QContextMenuEvent*>(e.get());
		if (context->reason() != QContextMenuEvent::Mouse) {
			return base::EventFilterResult::Continue;
		}
		// Shadow extends overlap neighbours, so the point picks the square.
		const auto global = context->globalPos();
		const auto index = squareAt(mapFromGlobal(global));
		if (index >= 0) {
			showCardMenu(_records[index].address, global);
		}
		return base::EventFilterResult::Cancel;
	});
	raw->mouseEvents(
	) | rpl::on_next([=](QMouseEvent *e) {
		if (e->type() == QEvent::MouseButtonPress) {
			mousePressEvent(e);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			mouseReleaseEvent(e);
		}
	}, raw->lifetime());
	raw->setPlayback(_seen.contains(address)
		? Info::PeerGifts::GiftButtonPlayback::Rest
		: Info::PeerGifts::GiftButtonPlayback::Once);
	raw->firstFrameShown(
	) | rpl::on_next([=] {
		_seen.emplace(address);
	}, raw->lifetime());
	raw->show();
	return result;
}

std::unique_ptr<Ui::AbstractButton> CollectiblesGrid::createThumbButton(
		const QString &address) {
	auto result = std::make_unique<Ui::AbstractButton>(this);
	const auto raw = result.get();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->setClickedCallback([=] {
		activate(address);
	});
	raw->paintRequest(
	) | rpl::on_next([=] {
		paintThumb(raw, address);
	}, raw->lifetime());
	raw->show();
	return result;
}

void CollectiblesGrid::paintThumb(
		not_null<QWidget*> thumb,
		const QString &address) {
	auto p = Painter(thumb.get());
	const auto outer = thumb->rect();
	auto background = _delegate.background();
	const auto fill = Ui::RoundAreaWithShadow::FillWithImage(
		p,
		outer,
		{ .image = &background, .rect = background.rect() });
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::windowBg);
	}
	const auto inner = outer.marginsRemoved(_delegate.buttonExtend());
	const auto radius = st::giftBoxGiftRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(inner, radius, radius);
	p.setClipPath(clip);
	const auto side = std::max(inner.width(), inner.height());
	_media->paint(
		p,
		address,
		QRect(
			inner.x() + (inner.width() - side) / 2,
			inner.y() + (inner.height() - side) / 2,
			side,
			side),
		outer.width(),
		radius);
}

} // namespace

QString OwnCardRefusalText(OwnCardChange change) {
	switch (change) {
	case OwnCardChange::NotReady:
		return tr::lng_wallet_card_not_ready(tr::now);
	case OwnCardChange::Limit:
		return CardLimitText();
	case OwnCardChange::Invalid:
		return tr::lng_wallet_card_save_error(tr::now);
	case OwnCardChange::Applied:
	case OwnCardChange::Unchanged:
		break;
	}
	return QString();
}

CollectiblesSquares AddCollectiblesList(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::ScrollArea*> scroll,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		CardHost host) {
	const auto wallet = &show->session().wallet();
	const auto grid = container->add(object_ptr<CollectiblesGrid>(
		container,
		scroll,
		std::move(show),
		std::move(media),
		std::move(host)));
	grid->rebuild();
	wallet->collectiblesUpdates(
	) | rpl::on_next([=] {
		grid->rebuild();
	}, grid->lifetime());
	return grid->squares();
}

} // namespace Wallet
