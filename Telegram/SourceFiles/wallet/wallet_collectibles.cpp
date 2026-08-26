/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/local_url_handlers.h"
#include "data/data_file_origin.h"
#include "data/data_star_gift.h"
#include "gram/api/gram_api_nft.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "menu/menu_send_details.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/delayed_activation.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_session.h"

#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

#include <rpl/never.h>

namespace Wallet {
namespace {

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
	void setPaintThumb(Fn<void(Painter&, QRect)> paint);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _title;
	Fn<void(Painter&, QRect)> _paintThumb;

};

class PanelChatShow final : public ChatHelpers::Show {
public:
	explicit PanelChatShow(std::shared_ptr<Main::SessionShow> show);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

	[[nodiscard]] Main::Session &session() const override;
	[[nodiscard]] Window::SessionController *resolveWindow() const override;
	[[nodiscard]] bool canResolveWindow() const override;

	void activate() override;
	[[nodiscard]] bool paused(
		ChatHelpers::PauseReason reason) const override;
	[[nodiscard]] rpl::producer<> pauseChanged() const override;
	[[nodiscard]] SendMenu::Details sendMenuDetails() const override;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) const override;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) const override;
	void processChosenSticker(
		ChatHelpers::FileChosen &&chosen) const override;

private:
	const std::shared_ptr<Main::SessionShow> _show;

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

	const auto font = st::walletCollectibleTitleStyle.font;
	p.setPen(st::windowBoldFg);
	_title.draw(p, {
		.position = {
			st::walletRowPadding.left(),
			(st::walletCollectibleRowHeight - font->height) / 2,
		},
		.outerWidth = width(),
		.availableWidth = (width()
			- st::walletRowPadding.left()
			- st::walletRowPadding.right()),
		.palette = &st::walletCollectibleTitlePalette,
		.elisionLines = 1,
	});
}

PanelChatShow::PanelChatShow(std::shared_ptr<Main::SessionShow> show)
: _show(std::move(show)) {
}

void PanelChatShow::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const {
	_show->showOrHideBoxOrLayer(std::move(layer), options, animated);
}

not_null<QWidget*> PanelChatShow::toastParent() const {
	return _show->toastParent();
}

bool PanelChatShow::valid() const {
	return _show->valid();
}

PanelChatShow::operator bool() const {
	return valid();
}

Main::Session &PanelChatShow::session() const {
	return _show->session();
}

Window::SessionController *PanelChatShow::resolveWindow() const {
	return nullptr;
}

bool PanelChatShow::canResolveWindow() const {
	return false;
}

void PanelChatShow::activate() {
	if (_show->valid()) {
		Ui::ActivateWindow(_show->toastParent());
	}
}

bool PanelChatShow::paused(ChatHelpers::PauseReason) const {
	return !_show->valid();
}

rpl::producer<> PanelChatShow::pauseChanged() const {
	return rpl::never<>();
}

SendMenu::Details PanelChatShow::sendMenuDetails() const {
	return { SendMenu::Type::Disabled };
}

bool PanelChatShow::showMediaPreview(
		Data::FileOrigin,
		not_null<DocumentData*>) const {
	return false;
}

bool PanelChatShow::showMediaPreview(
		Data::FileOrigin,
		not_null<PhotoData*>) const {
	return false;
}

void PanelChatShow::processChosenSticker(ChatHelpers::FileChosen &&) const {
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

void CollectiblePreviewBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<CollectibleMedia> media,
		Gram::NftItem item) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto address = item.address;
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
	if (item.kind == Gram::NftKind::TelegramGift && !item.key.isEmpty()) {
		const auto weak = std::weak_ptr(media);
		const auto address = item.address;
		Core::ResolveAndShowUniqueGift(
			std::make_shared<PanelChatShow>(show),
			item.key,
			[=](const QString &) {
				const auto strong = weak.lock();
				if (strong && show->valid()) {
					show->showBox(
						Box(CollectiblePreviewBox, strong, item));
				}
			},
			[=](const Data::StarGift &gift) {
				return UniqueGiftMatchesAddress(gift.unique, address);
			});
		return;
	}
	show->showBox(Box(CollectiblePreviewBox, std::move(media), item));
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
		row->setTitle(CollectibleTitleText(media->view(address)));
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
	media->resolve(address);
}

} // namespace

void AddCollectiblesList(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media) {
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	const auto rebuild = [=] {
		container->clear();
		for (const auto &item : wallet->collectibles()) {
			AddRow(container, media, show, item);
		}
		Ui::AddSkip(container, st::walletRowsTopSkip);
		if (const auto width = container->width()) {
			container->resizeToWidth(width);
		}
	};
	rebuild();
	wallet->collectiblesUpdates(
	) | rpl::on_next(rebuild, container->lifetime());
}

} // namespace Wallet
