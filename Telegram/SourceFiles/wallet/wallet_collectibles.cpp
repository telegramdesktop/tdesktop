/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "apiwrap.h"
#include "base/weak_qptr.h"
#include "boxes/star_gift_box.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user_wallet_card.h"
#include "gram/api/gram_api_nft.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"

#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kPreloadScreens = 3;
constexpr auto kPreloadRows = 2;

class CollectiblesGrid final : public Ui::RpWidget {
public:
	CollectiblesGrid(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media);

	void rebuild();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Square {
		CollectibleRecord record;
		bool onCard = false;
		std::unique_ptr<Info::PeerGifts::GiftButton> gift;
		std::unique_ptr<Ui::AbstractButton> thumb;
	};

	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] std::pair<int, int> rowsRange(
		int top,
		int bottom,
		int preload) const;
	[[nodiscard]] QRect squareRect(int index) const;
	[[nodiscard]] int squareAt(QPoint point) const;
	[[nodiscard]] int indexOf(const QString &address) const;

	void invalidateWindows();
	void refreshWindows();
	void refreshSquares();
	void refreshMediaWindow();
	void applyOnCard(const std::vector<Data::WalletCardSticker> &stickers);
	void applySquare(Square &square, const CollectibleRecord &record);
	void activate(const QString &address);
	[[nodiscard]] auto createGiftButton(const QString &address)
	-> std::unique_ptr<Info::PeerGifts::GiftButton>;
	[[nodiscard]] std::unique_ptr<Ui::AbstractButton> createThumbButton(
		const QString &address);
	void paintThumb(not_null<QWidget*> thumb, const QString &address);

	const std::shared_ptr<Main::SessionShow> _show;
	const std::shared_ptr<CollectibleMedia> _media;
	Info::PeerGifts::Delegate _delegate;
	const QSize _singleMin;
	std::vector<CollectibleRecord> _records;
	base::flat_map<QString, Square> _squares;
	base::flat_set<QString> _onCard;
	QString _pressed;
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
		const CollectibleRecord &item) {
	const auto sellUrl = CollectibleFragmentUrl(item);
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
		if (!sellUrl.isEmpty()) {
			actions->sell = [=] {
				UrlClickHandler::Open(sellUrl);
			};
		}
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
		sellUrl));
}

CollectiblesGrid::CollectiblesGrid(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media)
: RpWidget(parent)
, _show(std::move(show))
, _media(std::move(media))
, _delegate(&_show->session(), Info::PeerGifts::GiftButtonMode::Minimal)
, _singleMin(_delegate.buttonSize()) {
	setMouseTracking(true);

	_media->repaint(
	) | rpl::on_next([=](const QString &address) {
		const auto i = _squares.find(address);
		if (i != end(_squares) && i->second.thumb) {
			i->second.thumb->update();
		}
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
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
}

void CollectiblesGrid::rebuild() {
	_records = _show->session().wallet().collectibles();
	invalidateWindows();
	if (const auto width = this->width()) {
		resizeToWidth(width);
	}
	refreshWindows();
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
}

void CollectiblesGrid::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = squareAt(mapFromGlobal(e->globalPos()));
	_pressed = (index >= 0) ? _records[index].address : QString();
}

void CollectiblesGrid::mouseMoveEvent(QMouseEvent *e) {
	const auto over = (squareAt(e->pos()) >= 0);
	if (_over != over) {
		_over = over;
		setCursor(over ? style::cur_pointer : style::cur_default);
	}
}

void CollectiblesGrid::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pressed = base::take(_pressed);
	const auto index = squareAt(mapFromGlobal(e->globalPos()));
	if (!pressed.isEmpty()
		&& index >= 0
		&& _records[index].address == pressed) {
		activate(pressed);
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

void CollectiblesGrid::invalidateWindows() {
	_squaresFrom = _squaresTill = -1;
	_mediaFrom = _mediaTill = -1;
}

void CollectiblesGrid::refreshWindows() {
	refreshSquares();
	refreshMediaWindow();
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
	if (!_pressed.isEmpty() && !_squares.contains(_pressed)) {
		_pressed = QString();
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
	if (kindChanged && _pressed == record.address) {
		_pressed = QString();
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
		const auto onCard = _onCard.contains(record.address);
		// A reparse allocates the gift anew, equal values keep the player.
		if (fresh
			|| !SameServedGift(square.record, record)
			|| square.onCard != onCard) {
			square.gift->setDescriptor(
				Info::PeerGifts::GiftTypeStars{
					.info = *record.gift,
					.mine = true,
					.onCard = onCard,
				},
				Info::PeerGifts::GiftButtonMode::Minimal);
		}
		square.onCard = onCard;
	}
	square.record = record;
}

void CollectiblesGrid::activate(const QString &address) {
	const auto index = indexOf(address);
	if (index >= 0) {
		const auto record = _records[index];
		Activate(_show, _media, record);
	}
}

auto CollectiblesGrid::createGiftButton(const QString &address)
-> std::unique_ptr<Info::PeerGifts::GiftButton> {
	auto result = std::make_unique<Info::PeerGifts::GiftButton>(
		this,
		&_delegate);
	const auto raw = result.get();
	raw->setClickedCallback([=] {
		activate(address);
	});
	raw->mouseEvents(
	) | rpl::on_next([=](QMouseEvent *e) {
		if (e->type() == QEvent::MouseButtonPress) {
			mousePressEvent(e);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			mouseReleaseEvent(e);
		}
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

void AddCollectiblesList(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media) {
	const auto wallet = &show->session().wallet();
	const auto grid = container->add(object_ptr<CollectiblesGrid>(
		container,
		std::move(show),
		std::move(media)));
	grid->rebuild();
	wallet->collectiblesUpdates(
	) | rpl::on_next([=] {
		grid->rebuild();
	}, grid->lifetime());
}

} // namespace Wallet
