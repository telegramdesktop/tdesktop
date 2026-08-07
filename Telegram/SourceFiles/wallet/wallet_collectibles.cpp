/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "boxes/peers/replace_boost_box.h"
#include "core/local_url_handlers.h"
#include "core/ton_explorer_url.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "gram/api/gram_api_nft.h"
#include "gram/ton/gram_address.h"
#include "lang/lang_tag.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "ui/image/image_location.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_session.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

#include <QtCore/QUrl>

namespace Wallet {
namespace {

constexpr auto kTitleTextOptions = TextParseOptions{
	TextParseMarkdown,
	0,
	0,
	Qt::LayoutDirectionAuto,
};

struct ItemMedia {
	QString name;
	QString number;
	QImage thumbnail;
	PaintRoundImageCallback giftPaint;
	std::unique_ptr<FileLoader> loader;
	bool resolveStarted = false;
};

struct CollectiblesMedia : base::has_weak_ptr {
	base::flat_map<QString, ItemMedia> map;
	rpl::event_stream<QString> changed;
	rpl::event_stream<QString> repaint;
};

class CollectibleRow final : public Ui::RippleButton {
public:
	explicit CollectibleRow(QWidget *parent);

	void setTitle(const QString &name, const QString &number);
	void setThumbnail(QImage image);
	void setGiftPaint(PaintRoundImageCallback paint);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::Text::String _title;
	QImage _thumbnail;
	PaintRoundImageCallback _giftPaint;

};

CollectibleRow::CollectibleRow(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimationBgOver) {
}

void CollectibleRow::setTitle(const QString &name, const QString &number) {
	auto text = Ui::Text::Semibold(name);
	if (!number.isEmpty()) {
		text.append(Ui::Text::Colorized(number));
	}
	_title.setMarkedText(
		st::walletCollectibleTitleStyle,
		text,
		kTitleTextOptions);
	update();
}

void CollectibleRow::setThumbnail(QImage image) {
	_thumbnail = std::move(image);
	update();
}

void CollectibleRow::setGiftPaint(PaintRoundImageCallback paint) {
	_giftPaint = std::move(paint);
	update();
}

int CollectibleRow::resizeGetHeight(int newWidth) {
	return st::walletCollectibleRowHeight;
}

void CollectibleRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(p, 0, 0);

	const auto thumbnail = QRect(
		st::walletRowIconLeft,
		(st::walletCollectibleRowHeight - st::walletRowIconSize) / 2,
		st::walletRowIconSize,
		st::walletRowIconSize);
	if (_giftPaint) {
		_giftPaint(
			p,
			thumbnail.x(),
			thumbnail.y(),
			width(),
			st::walletRowIconSize);
	} else if (!_thumbnail.isNull()) {
		p.drawImage(thumbnail, _thumbnail);
	} else {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			thumbnail,
			st::walletCollectibleThumbRadius,
			st::walletCollectibleThumbRadius);
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

[[nodiscard]] QString UriSlug(const QString &uri) {
	if (uri.isEmpty()) {
		return QString();
	}
	const auto path = QUrl(uri).path();
	auto slug = path.mid(path.lastIndexOf('/') + 1);
	const auto suffix = u".json"_q;
	if (slug.endsWith(suffix, Qt::CaseInsensitive)) {
		slug.chop(suffix.size());
	}
	return slug;
}

[[nodiscard]] QString FallbackTitle(const Gram::NftItem &item) {
	if (!item.domain.isEmpty()) {
		return item.domain;
	}
	const auto slug = UriSlug(item.contentUri);
	return slug.isEmpty()
		? Gram::FormatFriendly(item.address, true)
		: slug;
}

[[nodiscard]] std::pair<QString, QString> SplitNumberTail(
		const QString &name) {
	const auto hash = name.lastIndexOf('#');
	if (hash < 1 || hash + 1 >= name.size() || name[hash - 1] != ' ') {
		return { name, QString() };
	}
	for (auto i = hash + 1; i != name.size(); ++i) {
		if (!name[i].isDigit()) {
			return { name, QString() };
		}
	}
	return { name.left(hash), name.mid(hash) };
}

[[nodiscard]] QImage PrepareThumbnail(const QByteArray &bytes) {
	auto image = Images::Read({ .content = bytes }).image;
	if (image.isNull()
		|| image.width() * 20 < image.height()
		|| image.height() * 20 < image.width()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto side = st::walletRowIconSize * ratio;
	image = image.scaled(
		side,
		side,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	if (image.width() > side || image.height() > side) {
		image = image.copy(
			(image.width() - side) / 2,
			(image.height() - side) / 2,
			side,
			side);
	}
	const auto mask = Images::CornersMask(st::walletCollectibleThumbRadius);
	image = Images::Round(std::move(image), mask);
	image.setDevicePixelRatio(ratio);
	return image;
}

void StartLoad(
		not_null<Main::Session*> session,
		not_null<CollectiblesMedia*> media,
		const QString &key,
		const QString &url,
		Fn<void(QByteArray)> done) {
	media->map[key].loader = CreateFileLoader(
		session,
		DownloadLocation{ PlainUrlLocation{ url } },
		Data::FileOrigin(),
		QString(),
		0,
		0,
		UnknownFileLocation,
		LoadToCacheAsWell,
		LoadFromCloudOrLocal,
		false,
		0);
	const auto raw = media->map[key].loader.get();
	raw->updates() | rpl::on_next_error_done([] {
	}, [=](FileLoader::Error error) {
		crl::on_main(media, [=] {
			media->map[key].loader = nullptr;
		});
		done(QByteArray());
	}, [=] {
		crl::on_main(media, [=] {
			media->map[key].loader = nullptr;
		});
		done(raw->cancelled() ? QByteArray() : raw->bytes());
	}, raw->lifetime());
	raw->start();
}

void RequestGift(
		not_null<Main::Session*> session,
		not_null<CollectiblesMedia*> media,
		const QString &key,
		const QString &slug) {
	session->api().request(MTPpayments_GetUniqueStarGift(
		MTP_string(slug)
	)).done(crl::guard(media, [=](
			const MTPpayments_UniqueStarGift &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		const auto gift = ::Api::FromTL(session, data.vgift());
		if (!gift || !gift->unique) {
			return;
		}
		auto &entry = media->map[key];
		entry.giftPaint = GenerateGiftUniqueUserpicCallback(
			session,
			gift->unique,
			[=] { media->repaint.fire_copy(key); });
		entry.name = gift->unique->title;
		if (gift->unique->number > 0) {
			entry.number = u" #"_q
				+ Lang::FormatCountDecimal(gift->unique->number);
		}
		media->changed.fire_copy(key);
	})).send();
}

void StartImageLoad(
		not_null<Main::Session*> session,
		not_null<CollectiblesMedia*> media,
		const QString &key,
		const QString &url) {
	StartLoad(session, media, key, url, [=](QByteArray bytes) {
		auto image = PrepareThumbnail(bytes);
		if (!image.isNull()) {
			media->map[key].thumbnail = std::move(image);
			media->changed.fire_copy(key);
		}
	});
}

void StartDescriptorLoad(
		not_null<Main::Session*> session,
		not_null<CollectiblesMedia*> media,
		const QString &key,
		const QString &url) {
	StartLoad(session, media, key, url, [=](QByteArray bytes) {
		const auto descriptor = Gram::ParseNftDescriptor(bytes);
		if (!descriptor) {
			return;
		}
		media->map[key].name = descriptor->name;
		media->changed.fire_copy(key);
		const auto image = descriptor->imageUrl;
		if (!image.startsWith(u"https://"_q)) {
			return;
		}
		crl::on_main(media, [=] {
			StartImageLoad(session, media, key, image);
		});
	});
}

void EnsureResolved(
		not_null<Main::Session*> session,
		not_null<CollectiblesMedia*> media,
		const Gram::NftItem &item) {
	const auto key = Gram::FormatRaw(item.address);
	if (media->map[key].resolveStarted) {
		return;
	}
	media->map[key].resolveStarted = true;
	if (item.kind == Gram::NftKind::TelegramGift && !item.key.isEmpty()) {
		RequestGift(session, media, key, item.key);
	} else if (item.contentUriHttps) {
		StartDescriptorLoad(session, media, key, item.contentUri);
	}
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
		not_null<CollectiblesMedia*> media,
		std::shared_ptr<Main::SessionShow> show,
		const Gram::NftItem &item) {
	const auto session = &show->session();
	const auto key = Gram::FormatRaw(item.address);
	const auto row = container->add(
		object_ptr<CollectibleRow>(container));
	const auto apply = [=] {
		const auto &entry = media->map[key];
		if (entry.number.isEmpty()) {
			const auto title = SplitNumberTail(entry.name.isEmpty()
				? FallbackTitle(item)
				: entry.name);
			row->setTitle(title.first, title.second);
		} else {
			row->setTitle(entry.name, entry.number);
		}
		row->setThumbnail(entry.thumbnail);
		row->setGiftPaint(entry.giftPaint);
	};
	apply();

	const auto mine = [=](const QString &changed) {
		return (changed == key);
	};
	media->changed.events(
	) | rpl::filter(mine) | rpl::on_next(apply, row->lifetime());
	media->repaint.events(
	) | rpl::filter(mine) | rpl::on_next([=] {
		row->update();
	}, row->lifetime());

	row->setClickedCallback([=] {
		Activate(show, item);
	});
	EnsureResolved(session, media, item);
}

} // namespace

void AddCollectiblesList(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show) {
	const auto session = &show->session();
	const auto wallet = &session->wallet();
	const auto media = container->lifetime().make_state<CollectiblesMedia>();
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
