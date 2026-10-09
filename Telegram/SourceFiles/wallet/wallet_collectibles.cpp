/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "core/local_url_handlers.h"
#include "data/data_star_gift.h"
#include "gram/api/gram_api_nft.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"

#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kPreloadScreens = 3;

constexpr auto kTitleTextOptions = TextParseOptions{
	TextParseMarkdown,
	0,
	0,
	Qt::LayoutDirectionAuto,
};

class CollectibleRow final : public Ui::RippleButton {
public:
	explicit CollectibleRow(QWidget *parent);

	void setTitle(TextWithEntities text);
	void setSubtitle(const QString &text);
	void setPaintThumb(Fn<void(Painter&, QRect)> paint);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _title;
	Ui::Text::String _subtitle;
	Fn<void(Painter&, QRect)> _paintThumb;

};

class CollectiblesList final : public Ui::VerticalLayout {
public:
	CollectiblesList(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media);

	void rebuild();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	void refreshWindow();

	const std::shared_ptr<Main::SessionShow> _show;
	const std::shared_ptr<CollectibleMedia> _media;
	std::vector<QString> _addresses;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _windowFrom = -1;
	int _windowTill = -1;

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

CollectibleRow::CollectibleRow(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimationBgOver) {
}

void CollectibleRow::setTitle(TextWithEntities text) {
	_title.setMarkedText(
		st::walletCollectibleTitleStyle,
		std::move(text),
		kTitleTextOptions);
	update();
}

void CollectibleRow::setSubtitle(const QString &text) {
	_subtitle.setText(
		st::walletRowDateLabel.style,
		text,
		Ui::NameTextOptions());
	update();
}

void CollectibleRow::setPaintThumb(Fn<void(Painter&, QRect)> paint) {
	_paintThumb = std::move(paint);
	update();
}

int CollectibleRow::resizeGetHeight(int newWidth) {
	return st::walletCollectibleRowHeight;
}

void CollectibleRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(p, 0, 0);

	if (_paintThumb) {
		_paintThumb(p, QRect(
			st::walletRowIconLeft,
			(st::walletCollectibleRowHeight - st::walletRowIconSize) / 2,
			st::walletRowIconSize,
			st::walletRowIconSize));
	}

	const auto titleHeight = st::walletCollectibleTitleStyle.font->height;
	const auto subtitleHeight = st::walletRowDateLabel.style.font->height;
	const auto top = (st::walletCollectibleRowHeight
		- titleHeight
		- st::walletRowSkip
		- subtitleHeight) / 2;
	const auto left = st::walletRowPadding.left();
	const auto available = width() - left - st::walletRowPadding.right();
	p.setPen(st::windowBoldFg);
	_title.draw(p, {
		.position = { left, top },
		.outerWidth = width(),
		.availableWidth = available,
		.palette = &st::walletCollectibleTitlePalette,
		.elisionLines = 1,
	});
	p.setPen(st::windowSubTextFg);
	_subtitle.draw(p, {
		.position = { left, top + titleHeight + st::walletRowSkip },
		.outerWidth = width(),
		.availableWidth = available,
		.elisionLines = 1,
	});
}

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
		return u"https://fragment.com/gift/"_q + item.key;
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

void Activate(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const Gram::NftItem &item) {
	const auto sellUrl = CollectibleFragmentUrl(item);
	if (item.kind == Gram::NftKind::TelegramGift && !item.key.isEmpty()) {
		const auto weak = std::weak_ptr(media);
		const auto address = item.address;
		auto actions = std::make_shared<::Settings::UniqueGiftCoverActions>();
		actions->transfer = [=] {
			if (const auto strong = weak.lock()) {
				ShowCollectibleTransfer(show, strong, address);
			}
		};
		if (!sellUrl.isEmpty()) {
			actions->sell = [=] {
				UrlClickHandler::Open(sellUrl);
			};
		}
		Core::ResolveAndShowUniqueGift(
			MakeChatShow(show, false),
			item.key,
			::Settings::CreditsEntryBoxStyleOverrides(),
			[=](const QString &error) {
				const auto strong = weak.lock();
				if (!strong || !show->valid()) {
					return;
				}
				const auto mismatch = (error == u"GIFT_ADDRESS_MISMATCH"_q);
				show->showBox(Box(
					CollectiblePreviewBox,
					show,
					strong,
					item,
					mismatch ? QString() : sellUrl));
			},
			[=](const Data::StarGift &gift) {
				return UniqueGiftMatchesAddress(gift.unique, address);
			},
			std::move(actions));
		return;
	}
	show->showBox(Box(
		CollectiblePreviewBox,
		show,
		std::move(media),
		item,
		sellUrl));
}

void AddRow(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<CollectibleMedia> media,
		std::shared_ptr<Main::SessionShow> show,
		const Gram::NftItem &item) {
	const auto address = item.address;
	const auto row = container->add(
		object_ptr<CollectibleRow>(container));
	row->setPaintThumb([=](Painter &p, QRect rect) {
		media->paint(
			p,
			address,
			rect,
			row->width(),
			st::walletCollectibleThumbRadius);
	});
	const auto apply = [=] {
		const auto view = media->view(address);
		row->setTitle(CollectibleTitleText(view));
		row->setSubtitle(CollectibleSubtitleText(view));
	};
	apply();

	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, row->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		row->update();
	}, row->lifetime());

	row->setClickedCallback([=] {
		Activate(show, media, item);
	});
}

CollectiblesList::CollectiblesList(
	QWidget *parent,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media)
: VerticalLayout(parent)
, _show(std::move(show))
, _media(std::move(media)) {
}

void CollectiblesList::rebuild() {
	clear();
	_addresses.clear();
	const auto wallet = &_show->session().wallet();
	for (const auto &item : wallet->collectibles()) {
		AddRow(this, _media, _show, item);
		_addresses.push_back(item.address);
	}
	Ui::AddSkip(this, st::walletRowsTopSkip);
	if (const auto width = this->width()) {
		resizeToWidth(width);
	}
	_windowFrom = _windowTill = -1;
	refreshWindow();
}

void CollectiblesList::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	VerticalLayout::visibleTopBottomUpdated(visibleTop, visibleBottom);
	refreshWindow();
}

void CollectiblesList::refreshWindow() {
	const auto count = int(_addresses.size());
	const auto row = st::walletCollectibleRowHeight;
	const auto page = std::max(_visibleBottom - _visibleTop, 0);
	const auto till = _visibleBottom + page * kPreloadScreens;
	const auto from = std::clamp(_visibleTop / row, 0, count);
	const auto last = std::clamp((till + row - 1) / row, from, count);
	if (_windowFrom == from && _windowTill == last) {
		return;
	}
	_windowFrom = from;
	_windowTill = last;
	auto ordered = std::vector<QString>();
	ordered.reserve(last - from);
	for (auto i = from; i != last; ++i) {
		ordered.push_back(_addresses[i]);
	}
	_media->setListWindow(std::move(ordered));
}

} // namespace

void AddCollectiblesList(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media) {
	const auto wallet = &show->session().wallet();
	const auto list = container->add(object_ptr<CollectiblesList>(
		container,
		std::move(show),
		std::move(media)));
	list->rebuild();
	wallet->collectiblesUpdates(
	) | rpl::on_next([=] {
		list->rebuild();
	}, list->lifetime());
}

} // namespace Wallet
