/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_emoji_images.h"

#include "chat_helpers/stickers_emoji_image_loader.h"
#include "core/application.h"
#include "tgw/tgw_render_thread.h"
#include "ui/emoji_config.h"

#include "styles/style_chat.h"

namespace Tgw {
namespace {

constexpr auto kMaxTileBytes = int64(8 * 1024 * 1024);
constexpr auto kMaxTileCeiling = 4 * kMaxTileBytes;
constexpr auto kMaxIsolatedBytes = int64(16 * 1024 * 1024);
constexpr auto kPixelBytes = 4;
// A tile painted this recently is never evicted, so refills settle.
constexpr auto kEmojiKeep = crl::time(1000);
constexpr auto kUntilAnswered = std::numeric_limits<crl::time>::max();

struct TileKey {
	int index = 0;
	int side = 0;

	friend inline constexpr auto operator<=>(TileKey, TileKey) = default;
};

struct EmojiRequest {
	EmojiPtr emoji = nullptr;
	int side = 0;
	int64 reserved = 0;
	bool isolated = false;
};

struct EmojiTile {
	TileKey key;
	QImage image;
};

class EmojiTiles final {
public:
	void configure(EmojiSides sides, Fn<void()> landed);
	void paint(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide);
	[[nodiscard]] uint64 waits() const;
	void flush();
	void landTiles(uint64 generation, const std::vector<EmojiTile> &tiles);
	void landIsolated(
		uint64 generation,
		EmojiPtr emoji,
		int64 reserved,
		const QImage &image);
	void reset(uint64 generation);

private:
	struct Tile {
		QImage image;
		uint64 used = 0;
		crl::time touched = 0;
	};

	[[nodiscard]] QImage tileImage(EmojiPtr emoji, int side, crl::time now);
	[[nodiscard]] QImage isolatedImage(EmojiPtr emoji, crl::time now);
	[[nodiscard]] bool requestTile(EmojiPtr emoji, int side, crl::time now);
	void requestIsolated(EmojiPtr emoji);
	void enqueue(EmojiRequest request);
	[[nodiscard]] bool adopt(uint64 generation);
	void makeRoom(int64 bytes, crl::time now);
	void insertTile(const EmojiTile &tile, crl::time now);

	EmojiSides _sides;
	Fn<void()> _landed;
	base::flat_map<TileKey, Tile> _tiles;
	base::flat_map<int, QImage> _isolated;
	base::flat_map<TileKey, crl::time> _requested;
	std::vector<EmojiRequest> _batch;
	uint64 _generation = 0;
	uint64 _lastUse = 0;
	uint64 _waits = 0;
	crl::time _keptUntil = 0;
	int64 _tileBytes = 0;
	int64 _pendingBytes = 0;
	int64 _batchBytes = 0;
	int64 _readyBytes = 0;
	int64 _reservedBytes = 0;

};

class EmojiImages final : public base::has_weak_ptr {
public:
	EmojiImages();

	[[nodiscard]] rpl::producer<> refreshes() const;
	void requested(const std::vector<EmojiRequest> &batch);
	void finish();

private:
	void load(EmojiPtr emoji, int64 reserved);
	void landed(
		EmojiPtr emoji,
		uint64 generation,
		int64 reserved,
		QImage image);
	void reset();
	void requestRefresh();

	rpl::event_stream<> _refreshes;
	rpl::lifetime _lifetime;
	uint64 _generation = 0;
	bool _refreshPosted = false;
	bool _finishing = false;

};

[[nodiscard]] int64 ImageBytes(QSize size) {
	return int64(size.width()) * size.height() * kPixelBytes;
}

[[nodiscard]] int64 TileBytes(int side) {
	return ImageBytes(QSize(side, side));
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

[[nodiscard]] EmojiTiles &Tiles() {
	Expects(OnRenderThread());

	static auto &result = *new EmojiTiles();
	return result;
}

[[nodiscard]] EmojiImages &Images() {
	static auto result = EmojiImages();
	return result;
}

void EmojiTiles::configure(EmojiSides sides, Fn<void()> landed) {
	_sides = sides;
	_landed = std::move(landed);
}

void EmojiTiles::paint(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide) {
	const auto now = crl::now();
	const auto image = (deviceSide <= _sides.normal)
		? tileImage(emoji, _sides.normal, now)
		: (deviceSide <= _sides.large)
		? tileImage(emoji, _sides.large, now)
		: isolatedImage(emoji, now);
	if (!image.isNull()) {
		p.drawImage(box, image);
	}
}

uint64 EmojiTiles::waits() const {
	return _waits;
}

void EmojiTiles::flush() {
	_batchBytes = 0;
	crl::on_main([batch = base::take(_batch)] {
		Images().requested(batch);
	});
}

void EmojiTiles::landTiles(
		uint64 generation,
		const std::vector<EmojiTile> &tiles) {
	for (const auto &tile : tiles) {
		_pendingBytes -= TileBytes(tile.key.side);
	}
	if (!adopt(generation)) {
		for (const auto &tile : tiles) {
			_requested.remove(tile.key);
		}
	} else {
		const auto now = crl::now();
		auto bytes = int64(0);
		for (const auto &tile : tiles) {
			if (!_tiles.contains(tile.key)) {
				bytes += ImageBytes(tile.image.size());
			}
		}
		makeRoom(bytes, now);
		for (const auto &tile : tiles) {
			insertTile(tile, now);
		}
	}
	if (_landed) {
		_landed();
	}
}

void EmojiTiles::landIsolated(
		uint64 generation,
		EmojiPtr emoji,
		int64 reserved,
		const QImage &image) {
	_reservedBytes -= reserved;
	if (!adopt(generation)) {
		return;
	}
	auto &entry = _isolated[emoji->index()];
	const auto bytes = ImageBytes(image.size());
	if (entry.isNull() && !image.isNull() && bytes <= reserved) {
		entry = image;
		_readyBytes += bytes;
	}
}

void EmojiTiles::reset(uint64 generation) {
	_generation = generation;
	_tiles.clear();
	_isolated.clear();
	_requested.clear();
	_tileBytes = 0;
	_readyBytes = 0;
}

QImage EmojiTiles::tileImage(EmojiPtr emoji, int side, crl::time now) {
	const auto key = TileKey{ emoji->index(), side };
	const auto i = _tiles.find(key);
	if (i != end(_tiles)) {
		i->second.used = ++_lastUse;
		i->second.touched = now;
		return i->second.image;
	}
	const auto j = _requested.find(key);
	const auto retry = (j != end(_requested)) ? j->second : crl::time();
	if ((retry == kUntilAnswered)
		|| ((retry <= now) && requestTile(emoji, side, now))) {
		++_waits;
	}
	return QImage();
}

bool EmojiTiles::requestTile(EmojiPtr emoji, int side, crl::time now) {
	const auto bytes = TileBytes(side);
	if (_batchBytes && (_batchBytes + bytes > kMaxTileBytes)) {
		return true;
	} else if (_tileBytes + _pendingBytes + bytes > kMaxTileCeiling) {
		makeRoom(bytes, now);
	}
	const auto key = TileKey{ emoji->index(), side };
	if (_tileBytes + _pendingBytes + bytes > kMaxTileCeiling) {
		_requested[key] = now + kEmojiKeep;
		return false;
	}
	_requested[key] = kUntilAnswered;
	_pendingBytes += bytes;
	_batchBytes += bytes;
	enqueue({ .emoji = emoji, .side = side });
	return true;
}

QImage EmojiTiles::isolatedImage(EmojiPtr emoji, crl::time now) {
	const auto index = emoji->index();
	const auto i = _isolated.find(index);
	if (i == end(_isolated)) {
		_isolated.emplace(index, QImage());
		requestIsolated(emoji);
	} else if (!i->second.isNull()) {
		return i->second;
	}
	return tileImage(emoji, _sides.large, now);
}

void EmojiTiles::requestIsolated(EmojiPtr emoji) {
	const auto side = _sides.isolated;
	const auto reserved = ImageBytes(QSize(side, side));
	if (_readyBytes + _reservedBytes + reserved > kMaxIsolatedBytes) {
		return;
	}
	_reservedBytes += reserved;
	enqueue({ .emoji = emoji, .reserved = reserved, .isolated = true });
}

void EmojiTiles::enqueue(EmojiRequest request) {
	if (_batch.empty()) {
		PostToRenderThread([] {
			Tiles().flush();
		});
	}
	_batch.push_back(request);
}

bool EmojiTiles::adopt(uint64 generation) {
	if (generation < _generation) {
		return false;
	} else if (generation > _generation) {
		reset(generation);
	}
	return true;
}

void EmojiTiles::makeRoom(int64 bytes, crl::time now) {
	if ((_tileBytes + bytes <= kMaxTileBytes) || (now <= _keptUntil)) {
		return;
	}
	auto old = std::vector<not_null<Tile*>>();
	auto keptUntil = now + kEmojiKeep;
	for (auto &entry : _tiles) {
		const auto until = entry.second.touched + kEmojiKeep;
		if (until < now) {
			old.push_back(&entry.second);
		} else {
			keptUntil = std::min(keptUntil, until);
		}
	}
	if (old.empty()) {
		_keptUntil = keptUntil;
		return;
	}
	ranges::sort(old, ranges::less(), [](not_null<Tile*> tile) {
		return tile->used;
	});
	auto evicted = 0;
	for (const auto tile : old) {
		if (_tileBytes + bytes <= kMaxTileBytes) {
			break;
		}
		_tileBytes -= ImageBytes(tile->image.size());
		tile->image = QImage();
		++evicted;
	}
	if (evicted == int(old.size())) {
		_keptUntil = keptUntil;
	}
	auto kept = base::flat_map<TileKey, Tile>();
	kept.reserve(_tiles.size());
	for (auto &entry : _tiles) {
		if (!entry.second.image.isNull()) {
			kept.emplace(entry.first, std::move(entry.second));
		}
	}
	_tiles = std::move(kept);
}

void EmojiTiles::insertTile(const EmojiTile &tile, crl::time now) {
	const auto bytes = ImageBytes(tile.image.size());
	if (_tiles.contains(tile.key)) {
		_requested.remove(tile.key);
	} else if (tile.image.isNull()
		|| (_tileBytes + bytes > kMaxTileCeiling)) {
		_requested[tile.key] = now + kEmojiKeep;
	} else {
		_requested.remove(tile.key);
		_tiles.emplace(tile.key, Tile{
			.image = tile.image,
			.used = ++_lastUse,
			.touched = now,
		});
		_tileBytes += bytes;
	}
}

EmojiImages::EmojiImages() {
	Ui::Emoji::Updated() | rpl::on_next([=] {
		reset();
	}, _lifetime);
}

rpl::producer<> EmojiImages::refreshes() const {
	return _refreshes.events();
}

void EmojiImages::requested(const std::vector<EmojiRequest> &batch) {
	if (_finishing) {
		return;
	}
	auto tiles = std::vector<EmojiTile>();
	tiles.reserve(batch.size());
	for (const auto &request : batch) {
		if (request.isolated) {
			load(request.emoji, request.reserved);
		} else {
			tiles.push_back({
				.key = { request.emoji->index(), request.side },
				.image = DrawTile(request.emoji, request.side),
			});
		}
	}
	if (tiles.empty()) {
		return;
	}
	PostToRenderThread([generation = _generation, tiles = std::move(tiles)] {
		Tiles().landTiles(generation, tiles);
	});
}

void EmojiImages::finish() {
	_finishing = true;
}

void EmojiImages::load(EmojiPtr emoji, int64 reserved) {
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
}

void EmojiImages::landed(
		EmojiPtr emoji,
		uint64 generation,
		int64 reserved,
		QImage image) {
	if (_finishing) {
		return;
	}
	PostToRenderThread([=, image = std::move(image)] {
		Tiles().landIsolated(generation, emoji, reserved, image);
	});
	requestRefresh();
}

void EmojiImages::reset() {
	++_generation;
	if (RenderThreadStarted()) {
		PostToRenderThread([generation = _generation] {
			Tiles().reset(generation);
		});
	}
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

EmojiSides CurrentEmojiSides() {
	return {
		.normal = Ui::Emoji::GetSizeNormal(),
		.large = Ui::Emoji::GetSizeLarge(),
		.isolated = st::largeEmojiSize * style::DevicePixelRatio(),
	};
}

rpl::producer<> EmojiRefreshes() {
	return Images().refreshes();
}

void FinishEmojiImages() {
	Images().finish();
}

void ConfigureEmojiTiles(EmojiSides sides, Fn<void()> landed) {
	Tiles().configure(sides, std::move(landed));
}

void PaintEmoji(
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide) {
	Tiles().paint(p, emoji, box, deviceSide);
}

uint64 EmojiTileWaits() {
	return Tiles().waits();
}

void ClearEmojiTiles() {
	Tiles() = EmojiTiles();
}

} // namespace Tgw
