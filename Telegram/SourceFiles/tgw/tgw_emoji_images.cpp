/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_emoji_images.h"

#include "chat_helpers/stickers_emoji_image_loader.h"
#include "core/application.h"
#include "ui/emoji_config.h"

#include "styles/style_chat.h"

namespace Tgw {
namespace {

constexpr auto kMaxTileBytes = int64(8 * 1024 * 1024);
constexpr auto kMaxIsolatedBytes = int64(16 * 1024 * 1024);
constexpr auto kPixelBytes = 4;

class EmojiImages final : public base::has_weak_ptr {
public:
	EmojiImages();

	void paint(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide);
	[[nodiscard]] rpl::producer<> refreshes() const;

private:
	struct TileKey {
		int index = 0;
		int side = 0;

		friend inline constexpr auto operator<=>(TileKey, TileKey) = default;
	};
	struct Tile {
		QImage image;
		uint64 used = 0;
	};
	struct Isolated {
		QImage image;
		bool pending = false;
	};

	[[nodiscard]] QImage tile(EmojiPtr emoji, int side);
	[[nodiscard]] QImage isolated(EmojiPtr emoji);
	[[nodiscard]] bool request(EmojiPtr emoji);
	void insertTile(TileKey key, const QImage &image);
	void landed(
		EmojiPtr emoji,
		uint64 generation,
		int64 reserved,
		QImage image);
	void reset();
	void requestRefresh();

	base::flat_map<TileKey, Tile> _tiles;
	base::flat_map<int, Isolated> _isolated;
	rpl::event_stream<> _refreshes;
	rpl::lifetime _lifetime;
	uint64 _generation = 0;
	uint64 _lastUse = 0;
	int64 _tileBytes = 0;
	int64 _readyBytes = 0;
	int64 _reservedBytes = 0;
	bool _refreshPosted = false;

};

[[nodiscard]] int64 ImageBytes(QSize size) {
	return int64(size.width()) * size.height() * kPixelBytes;
}

[[nodiscard]] QImage DrawTile(EmojiPtr emoji, int side) {
	auto result = QImage(
		QSize(side, side),
		QImage::Format_ARGB32_Premultiplied);
	// WHY: Ui::Emoji::Draw scales its sprites by the app's ratio but its
	// fallback by the target's; widget layers have ratio 1, so a tile at
	// the app's ratio keeps both paths at the tier's exact size.
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	Ui::Emoji::Draw(p, emoji, side, 0, 0);
	p.end();
	return result;
}

[[nodiscard]] EmojiImages &Images() {
	static auto result = EmojiImages();
	return result;
}

EmojiImages::EmojiImages() {
	Ui::Emoji::Updated() | rpl::on_next([=] {
		reset();
	}, _lifetime);
}

void EmojiImages::paint(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide) {
	const auto normal = Ui::Emoji::GetSizeNormal();
	const auto large = Ui::Emoji::GetSizeLarge();
	const auto image = (deviceSide <= normal)
		? tile(emoji, normal)
		: (deviceSide <= large)
		? tile(emoji, large)
		: isolated(emoji);
	p.drawImage(box, image);
}

rpl::producer<> EmojiImages::refreshes() const {
	return _refreshes.events();
}

QImage EmojiImages::tile(EmojiPtr emoji, int side) {
	const auto key = TileKey{ emoji->index(), side };
	const auto i = _tiles.find(key);
	if (i != end(_tiles)) {
		i->second.used = ++_lastUse;
		return i->second.image;
	}
	const auto generation = _generation;
	auto result = DrawTile(emoji, side);
	if (generation == _generation) {
		insertTile(key, result);
	}
	return result;
}

QImage EmojiImages::isolated(EmojiPtr emoji) {
	const auto index = emoji->index();
	const auto i = _isolated.find(index);
	if (i == end(_isolated)) {
		_isolated.emplace(index, Isolated{ .pending = request(emoji) });
	} else if (!i->second.image.isNull()) {
		return i->second.image;
	}
	return tile(emoji, Ui::Emoji::GetSizeLarge());
}

bool EmojiImages::request(EmojiPtr emoji) {
	const auto side = st::largeEmojiSize * style::DevicePixelRatio();
	const auto reserved = ImageBytes(QSize(side, side));
	if (_readyBytes + _reservedBytes + reserved > kMaxIsolatedBytes) {
		return false;
	}
	_reservedBytes += reserved;
	const auto weak = base::make_weak(this);
	const auto generation = _generation;
	Core::App().emojiImageLoader().with([=](
			const Stickers::EmojiImageLoader &loader) {
		crl::on_main(weak, [
			=,
			image = loader.prepareWithoutOutline(emoji)
		]() mutable {
			landed(emoji, generation, reserved, std::move(image));
		});
	});
	return true;
}

void EmojiImages::insertTile(TileKey key, const QImage &image) {
	const auto bytes = ImageBytes(image.size());
	while (!_tiles.empty() && (_tileBytes + bytes > kMaxTileBytes)) {
		const auto oldest = ranges::min_element(
			_tiles,
			ranges::less(),
			[](const auto &entry) { return entry.second.used; });
		_tileBytes -= ImageBytes(oldest->second.image.size());
		_tiles.erase(oldest);
	}
	_tiles.emplace(key, Tile{ image, ++_lastUse });
	_tileBytes += bytes;
}

void EmojiImages::landed(
		EmojiPtr emoji,
		uint64 generation,
		int64 reserved,
		QImage image) {
	_reservedBytes -= reserved;
	if (generation != _generation) {
		return;
	}
	const auto i = _isolated.find(emoji->index());
	if (i == end(_isolated) || !i->second.pending) {
		return;
	}
	i->second.pending = false;
	const auto bytes = ImageBytes(image.size());
	if (!image.isNull() && bytes <= reserved) {
		i->second.image = std::move(image);
		_readyBytes += bytes;
	}
	requestRefresh();
}

void EmojiImages::reset() {
	_tiles.clear();
	_isolated.clear();
	_tileBytes = 0;
	_readyBytes = 0;
	++_generation;
	requestRefresh();
}

void EmojiImages::requestRefresh() {
	if (_refreshPosted) {
		return;
	}
	_refreshPosted = true;
	crl::on_main(this, [=] {
		_refreshPosted = false;
		_refreshes.fire({});
	});
}

} // namespace

void PaintEmoji(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide) {
	Images().paint(p, emoji, box, deviceSide);
}

rpl::producer<> EmojiRefreshes() {
	return Images().refreshes();
}

} // namespace Tgw
