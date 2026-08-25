/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectible_media.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "boxes/peers/replace_boost_box.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "ui/image/image_location.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "wallet/wallet_session.h"

#include <QtCore/QUrl>

namespace Wallet {
namespace {

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

[[nodiscard]] QImage PrepareThumbnail(
		const QByteArray &bytes,
		int side,
		int radius) {
	auto image = Images::Read({ .content = bytes }).image;
	if (image.isNull()
		|| image.width() * 20 < image.height()
		|| image.height() * 20 < image.width()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto full = side * ratio;
	image = image.scaled(
		full,
		full,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	if (image.width() > full || image.height() > full) {
		image = image.copy(
			(image.width() - full) / 2,
			(image.height() - full) / 2,
			full,
			full);
	}
	const auto mask = Images::CornersMask(radius);
	image = Images::Round(std::move(image), mask);
	image.setDevicePixelRatio(ratio);
	return image;
}

} // namespace

bool UniqueGiftMatchesAddress(
		const std::shared_ptr<Data::UniqueGift> &unique,
		const Gram::Address &address) {
	if (!unique) {
		return false;
	}
	const auto parsed = Gram::ParseAddress(unique->giftAddress);
	return parsed && (parsed->address == address);
}

TextWithEntities CollectibleTitleText(const CollectibleView &view) {
	auto result = Ui::Text::Semibold(view.name);
	if (!view.number.isEmpty()) {
		result.append(Ui::Text::Colorized(view.number));
	}
	return result;
}

struct CollectibleMedia::Entry {
	Gram::Address address;
	Gram::NftItem record;
	QString fallback;
	QString name;
	QString number;
	QString collectionName;
	QByteArray imageBytes;
	base::flat_map<int, QImage> prepared;
	PaintRoundImageCallback giftPaint;
	std::unique_ptr<FileLoader> loader;
	bool resolveStarted = false;
};

CollectibleMedia::CollectibleMedia(not_null<Main::Session*> session)
: _session(session) {
}

CollectibleMedia::~CollectibleMedia() = default;

void CollectibleMedia::resolve(const Gram::Address &item) {
	auto &pointer = _map[Gram::FormatRaw(item)];
	if (!pointer) {
		pointer = std::make_unique<Entry>();
		pointer->address = item;
		pointer->fallback = Gram::FormatFriendly(item, true);
	}
	const auto entry = pointer.get();
	if (entry->resolveStarted) {
		return;
	}
	entry->resolveStarted = true;
	_session->wallet().resolveCollectibleInfo(item, crl::guard(this, [=](
			const Gram::NftItem &record) {
		resolveFromRecord(entry, record);
	}));
}

void CollectibleMedia::resolveFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record) {
	if (record.address.hash.isEmpty()) {
		entry->resolveStarted = false;
		return;
	}
	entry->record = record;
	entry->fallback = FallbackTitle(record);
	entry->collectionName = record.collectionName;
	_changed.fire_copy(entry->address);
	if (record.kind == Gram::NftKind::TelegramGift && !record.key.isEmpty()) {
		requestGift(entry, record.key);
	} else if (record.contentUriHttps) {
		startDescriptorLoad(entry, record.contentUri);
	}
}

void CollectibleMedia::requestGift(
		not_null<Entry*> entry,
		const QString &slug) {
	const auto session = _session;
	const auto apply = [=](const std::shared_ptr<Data::UniqueGift> &unique) {
		entry->giftPaint = GenerateGiftUniqueUserpicCallback(
			session,
			unique,
			[=] { _repaint.fire_copy(entry->address); });
		entry->name = unique->title;
		if (unique->number > 0) {
			entry->number = u" #"_q
				+ Lang::FormatCountDecimal(unique->number);
		}
		_changed.fire_copy(entry->address);
		_repaint.fire_copy(entry->address);
	};
	const auto fallback = [=] {
		if (entry->record.contentUriHttps) {
			startDescriptorLoad(entry, entry->record.contentUri);
		}
	};
	session->api().request(MTPpayments_GetUniqueStarGift(
		MTP_string(slug)
	)).done(crl::guard(this, [=](
			const MTPpayments_UniqueStarGift &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		const auto gift = ::Api::FromTL(session, data.vgift());
		if (!gift
			|| !UniqueGiftMatchesAddress(gift->unique, entry->address)) {
			fallback();
			return;
		}
		apply(gift->unique);
	})).fail(crl::guard(this, [=](const MTP::Error &) {
		fallback();
	})).send();
}

void CollectibleMedia::startLoad(
		std::unique_ptr<FileLoader> &slot,
		const QString &url,
		Fn<void(QByteArray)> done) {
	slot = CreateFileLoader(
		_session,
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
	const auto raw = slot.get();
	const auto clear = &slot;
	raw->updates() | rpl::on_next_error_done([] {
	}, [=](FileLoader::Error error) {
		crl::on_main(this, [=] {
			*clear = nullptr;
		});
		done(QByteArray());
	}, [=] {
		crl::on_main(this, [=] {
			*clear = nullptr;
		});
		// The loader dies on the next main loop turn and its bytes do not
		// outlive it, so hand out a copy the consumer owns: the artwork is
		// kept and decoded lazily, on the first paint of each thumbnail.
		const auto &loaded = raw->bytes();
		done(raw->cancelled()
			? QByteArray()
			: QByteArray(loaded.constData(), loaded.size()));
	}, raw->lifetime());
	raw->start();
}

void CollectibleMedia::startImageLoad(
		not_null<Entry*> entry,
		const QString &url) {
	startLoad(entry->loader, url, [=](QByteArray bytes) {
		if (bytes.isEmpty()) {
			return;
		}
		entry->imageBytes = std::move(bytes);
		entry->prepared.clear();
		_changed.fire_copy(entry->address);
		_repaint.fire_copy(entry->address);
	});
}

void CollectibleMedia::startDescriptorLoad(
		not_null<Entry*> entry,
		const QString &url) {
	startLoad(entry->loader, url, [=](QByteArray bytes) {
		const auto descriptor = Gram::ParseNftDescriptor(bytes);
		if (!descriptor) {
			return;
		}
		entry->name = descriptor->name;
		_changed.fire_copy(entry->address);
		const auto image = descriptor->imageUrl;
		if (!image.startsWith(u"https://"_q)) {
			return;
		}
		crl::on_main(this, [=] {
			startImageLoad(entry, image);
		});
	});
}

CollectibleView CollectibleMedia::view(const Gram::Address &item) const {
	const auto entry = find(item);
	if (!entry) {
		const auto title = SplitNumberTail(Gram::FormatFriendly(item, true));
		return { title.first, title.second, QString() };
	} else if (!entry->number.isEmpty()) {
		return {
			entry->name,
			entry->number,
			entry->collectionName,
			entry->record.kind,
			entry->record.key,
		};
	}
	const auto title = SplitNumberTail(entry->name.isEmpty()
		? entry->fallback
		: entry->name);
	return {
		title.first,
		title.second,
		entry->collectionName,
		entry->record.kind,
		entry->record.key,
	};
}

Gram::Address CollectibleMedia::collection(const Gram::Address &item) const {
	const auto entry = find(item);
	return entry ? entry->record.collection : Gram::Address();
}

void CollectibleMedia::paint(
		Painter &p,
		const Gram::Address &item,
		QRect rect,
		int outerWidth,
		int radius) {
	const auto entry = find(item);
	auto hq = PainterHighQualityEnabler(p);
	if (entry && entry->giftPaint) {
		auto path = QPainterPath();
		path.addRoundedRect(rect, radius, radius);
		p.save();
		p.setClipPath(path);
		entry->giftPaint(p, rect.x(), rect.y(), outerWidth, rect.width());
		p.restore();
		return;
	} else if (entry) {
		const auto &image = preparedFor(entry, rect.width(), radius);
		if (!image.isNull()) {
			p.drawImage(rect, image);
			return;
		}
	}
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);
}

rpl::producer<Gram::Address> CollectibleMedia::changed() const {
	return _changed.events();
}

rpl::producer<Gram::Address> CollectibleMedia::repaint() const {
	return _repaint.events();
}

CollectibleMedia::Entry *CollectibleMedia::find(
		const Gram::Address &item) const {
	const auto i = _map.find(Gram::FormatRaw(item));
	return (i != end(_map)) ? i->second.get() : nullptr;
}

const QImage &CollectibleMedia::preparedFor(
		not_null<Entry*> entry,
		int side,
		int radius) {
	const auto i = entry->prepared.find(side);
	return (i != end(entry->prepared))
		? i->second
		: entry->prepared.emplace(
			side,
			PrepareThumbnail(entry->imageBytes, side, radius)).first->second;
}

} // namespace Wallet
