/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "core/local_url_handlers.h"
#include "core/ton_explorer_url.h"
#include "gram/api/gram_api_nft.h"
#include "gram/ton/gram_address.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

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

void Activate(
		std::shared_ptr<Main::SessionShow> show,
		const Gram::NftItem &item) {
	const auto session = &show->session();
	const auto window = session->tryResolveWindow();
	if (window && !item.key.isEmpty()) {
		if (item.kind == Gram::NftKind::TelegramGift) {
			Core::ResolveAndShowUniqueGift(window->uiShow(), item.key);
			return;
		} else if (item.kind == Gram::NftKind::TelegramNumber) {
			window->resolveCollectible(
				session->userPeerId(),
				'+' + item.key);
			return;
		} else if (item.kind == Gram::NftKind::TelegramUsername) {
			window->resolveCollectible(session->userPeerId(), item.key);
			return;
		}
	}
	UrlClickHandler::Open(Core::TonExplorerUrl(
		session,
		Gram::FormatFriendly(item.address, true)));
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

	const auto mine = [=](const Gram::Address &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, row->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		row->update();
	}, row->lifetime());

	row->setClickedCallback([=] {
		Activate(show, item);
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
