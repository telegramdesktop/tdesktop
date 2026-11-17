/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_emoji_images.h"

#include "base/timer.h"
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
constexpr auto kShareBatch = kMaxTileBytes / 2;
constexpr auto kShareHeld = kMaxTileCeiling - kMaxTileBytes;
constexpr auto kShareBurst = kMaxTileCeiling;
constexpr auto kShareRate = kMaxTileBytes;
constexpr auto kShareIsolated = kMaxIsolatedBytes / 2;
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
	uint64 owner = 0;
	int64 reserved = 0;
	int side = 0;
	bool isolated = false;
};

struct EmojiTile {
	TileKey key;
	QImage image;
	uint64 owner = 0;
};

class EmojiTiles final {
public:
	void configure(EmojiSides sides, Fn<void()> landed);
	void open(uint64 owner);
	void close(uint64 owner);
	void paint(
		uint64 owner,
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide);
	[[nodiscard]] uint64 waits() const;
	void flush();
	void landTiles(uint64 generation, const std::vector<EmojiTile> &tiles);
	void landIsolated(
		uint64 generation,
		uint64 owner,
		EmojiPtr emoji,
		int64 reserved,
		const QImage &image);
	void reset(uint64 generation);

private:
	struct Tile {
		QImage image;
		uint64 owner = 0;
		uint64 used = 0;
		crl::time touched = 0;
	};
	struct Share {
		int64 held = 0;
		int64 pending = 0;
		int64 batch = 0;
		int64 isolatedHeld = 0;
		int64 isolatedPending = 0;
		float64 tokens = 0.;
		crl::time refilled = 0;
		crl::time keptUntil = 0;
		crl::time isolatedKeptUntil = 0;
	};

	static void Refill(Share &share, crl::time now);

	[[nodiscard]] Share *share(uint64 owner);
	[[nodiscard]] QImage tileImage(
		uint64 owner,
		EmojiPtr emoji,
		int side,
		crl::time now);
	[[nodiscard]] QImage isolatedImage(
		uint64 owner,
		EmojiPtr emoji,
		crl::time now);
	[[nodiscard]] bool requestTile(
		uint64 owner,
		EmojiPtr emoji,
		int side,
		crl::time now);
	[[nodiscard]] bool admitTile(
		Share &share,
		uint64 owner,
		int64 bytes,
		crl::time now);
	[[nodiscard]] bool requestIsolated(
		uint64 owner,
		EmojiPtr emoji,
		crl::time now);
	void enqueue(EmojiRequest request);
	[[nodiscard]] bool adopt(uint64 generation);
	void makeRoom(int64 bytes, crl::time now);
	template <typename Key>
	void evict(
		base::flat_map<Key, Tile> &cache,
		int64 &cached,
		int64 Share::*held,
		std::optional<uint64> owner,
		crl::time now,
		crl::time &keptUntil,
		Fn<bool()> fits);
	void touch(Tile &tile, uint64 owner, crl::time now);
	void insertTile(const EmojiTile &tile, crl::time now);
	void retryAt(crl::time when);
	void retry();

	EmojiSides _sides;
	Fn<void()> _landed;
	base::flat_map<TileKey, Tile> _tiles;
	base::flat_map<int, Tile> _isolated;
	base::flat_map<TileKey, crl::time> _requested;
	base::flat_map<uint64, Share> _shares;
	std::vector<EmojiRequest> _batch;
	std::unique_ptr<base::Timer> _retry;
	uint64 _generation = 0;
	uint64 _lastUse = 0;
	uint64 _waits = 0;
	crl::time _keptUntil = 0;
	crl::time _retryAt = 0;
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
	void load(EmojiPtr emoji, uint64 owner, int64 reserved);
	void landed(
		EmojiPtr emoji,
		uint64 owner,
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

[[nodiscard]] crl::time TokenWait(float64 missing) {
	return crl::time(std::ceil(missing * kEmojiKeep / float64(kShareRate)));
}

template <typename Key, typename Value, typename Drop>
void RemoveIf(base::flat_map<Key, Value> &map, Drop drop) {
	auto kept = base::flat_map<Key, Value>();
	kept.reserve(map.size());
	for (auto &entry : map) {
		if (!drop(entry.second)) {
			kept.emplace(entry.first, std::move(entry.second));
		}
	}
	map = std::move(kept);
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

[[nodiscard]] uint64 NextEmojiAccountId() {
	Expects(OnRenderThread());

	static auto serial = uint64();
	return ++serial;
}

void EmojiTiles::configure(EmojiSides sides, Fn<void()> landed) {
	_sides = sides;
	_landed = std::move(landed);
}

void EmojiTiles::open(uint64 owner) {
	_shares.emplace(owner, Share{
		.tokens = float64(kShareBurst),
		.refilled = crl::now(),
	});
}

void EmojiTiles::close(uint64 owner) {
	if (!_shares.remove(owner)) {
		return;
	}
	RemoveIf(_tiles, [&](const Tile &tile) {
		if (tile.owner != owner) {
			return false;
		}
		_tileBytes -= ImageBytes(tile.image.size());
		return true;
	});
	RemoveIf(_isolated, [&](const Tile &entry) {
		if (entry.owner != owner) {
			return false;
		}
		_readyBytes -= ImageBytes(entry.image.size());
		return true;
	});
}

void EmojiTiles::paint(
		uint64 owner,
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide) {
	const auto now = crl::now();
	const auto image = (deviceSide <= _sides.normal)
		? tileImage(owner, emoji, _sides.normal, now)
		: (deviceSide <= _sides.large)
		? tileImage(owner, emoji, _sides.large, now)
		: isolatedImage(owner, emoji, now);
	if (!image.isNull()) {
		p.drawImage(box, image);
	}
}

uint64 EmojiTiles::waits() const {
	return _waits;
}

void EmojiTiles::flush() {
	_batchBytes = 0;
	for (auto &entry : _shares) {
		entry.second.batch = 0;
	}
	crl::on_main([batch = base::take(_batch)] {
		Images().requested(batch);
	});
}

void EmojiTiles::landTiles(
		uint64 generation,
		const std::vector<EmojiTile> &tiles) {
	for (const auto &tile : tiles) {
		const auto bytes = TileBytes(tile.key.side);
		_pendingBytes -= bytes;
		if (const auto share = this->share(tile.owner)) {
			share->pending -= bytes;
		}
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
		uint64 owner,
		EmojiPtr emoji,
		int64 reserved,
		const QImage &image) {
	_reservedBytes -= reserved;
	const auto share = this->share(owner);
	if (share) {
		share->isolatedPending -= reserved;
	}
	if (!adopt(generation) || !share) {
		return;
	}
	auto &entry = _isolated[emoji->index()];
	if (!entry.image.isNull()) {
		return;
	}
	entry.owner = owner;
	const auto bytes = ImageBytes(image.size());
	if (!image.isNull() && bytes <= reserved) {
		entry.image = image;
		touch(entry, owner, crl::now());
		_readyBytes += bytes;
		share->isolatedHeld += bytes;
	}
}

void EmojiTiles::reset(uint64 generation) {
	_generation = generation;
	_tiles.clear();
	_isolated.clear();
	_requested.clear();
	_tileBytes = 0;
	_readyBytes = 0;
	for (auto &entry : _shares) {
		entry.second.held = entry.second.isolatedHeld = 0;
	}
}

void EmojiTiles::Refill(Share &share, crl::time now) {
	const auto earned = (now - share.refilled)
		* float64(kShareRate)
		/ kEmojiKeep;
	share.tokens = std::min(float64(kShareBurst), share.tokens + earned);
	share.refilled = now;
}

EmojiTiles::Share *EmojiTiles::share(uint64 owner) {
	const auto i = _shares.find(owner);
	return (i != end(_shares)) ? &i->second : nullptr;
}

QImage EmojiTiles::tileImage(
		uint64 owner,
		EmojiPtr emoji,
		int side,
		crl::time now) {
	const auto key = TileKey{ emoji->index(), side };
	const auto i = _tiles.find(key);
	if (i != end(_tiles)) {
		touch(i->second, owner, now);
		return i->second.image;
	}
	const auto j = _requested.find(key);
	const auto retry = (j != end(_requested)) ? j->second : crl::time();
	if ((retry == kUntilAnswered)
		|| ((retry <= now) && requestTile(owner, emoji, side, now))) {
		++_waits;
	}
	return QImage();
}

bool EmojiTiles::requestTile(
		uint64 owner,
		EmojiPtr emoji,
		int side,
		crl::time now) {
	const auto bytes = TileBytes(side);
	if (_batchBytes && (_batchBytes + bytes > kMaxTileBytes)) {
		return true;
	}
	const auto share = this->share(owner);
	if (!share || !admitTile(*share, owner, bytes, now)) {
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
	share->tokens -= bytes;
	share->batch += bytes;
	share->pending += bytes;
	enqueue({ .emoji = emoji, .owner = owner, .side = side });
	return true;
}

bool EmojiTiles::admitTile(
		Share &share,
		uint64 owner,
		int64 bytes,
		crl::time now) {
	Refill(share, now);
	if (share.batch + bytes > kShareBatch) {
		return false;
	} else if (share.tokens < bytes) {
		retryAt(now + TokenWait(bytes - share.tokens));
		return false;
	}
	const auto fits = [&](int64 room) {
		return (share.held + share.pending + room <= kShareHeld);
	};
	if (!fits(bytes)) {
		const auto room = kShareBatch - share.batch;
		evict(
			_tiles,
			_tileBytes,
			&Share::held,
			owner,
			now,
			share.keptUntil,
			[&] { return fits(room); });
	}
	if (fits(bytes)) {
		return true;
	}
	retryAt(now + kEmojiKeep);
	return false;
}

QImage EmojiTiles::isolatedImage(
		uint64 owner,
		EmojiPtr emoji,
		crl::time now) {
	const auto index = emoji->index();
	const auto i = _isolated.find(index);
	if (i == end(_isolated)) {
		if (requestIsolated(owner, emoji, now)) {
			_isolated.emplace(index, Tile{ .owner = owner });
		}
	} else if (!i->second.image.isNull()) {
		touch(i->second, owner, now);
		return i->second.image;
	}
	return tileImage(owner, emoji, _sides.large, now);
}

bool EmojiTiles::requestIsolated(
		uint64 owner,
		EmojiPtr emoji,
		crl::time now) {
	const auto share = this->share(owner);
	if (!share) {
		return false;
	}
	Refill(*share, now);
	const auto side = _sides.isolated;
	const auto reserved = ImageBytes(QSize(side, side));
	const auto fits = [&](int64 room) {
		return (share->isolatedHeld + share->isolatedPending + room
			<= kShareIsolated);
	};
	if (share->tokens < reserved) {
		++_waits;
		retryAt(now + TokenWait(reserved - share->tokens));
		return false;
	} else if (!fits(reserved)) {
		const auto room = std::max(reserved, kShareBatch);
		evict(
			_isolated,
			_readyBytes,
			&Share::isolatedHeld,
			owner,
			now,
			share->isolatedKeptUntil,
			[&] { return fits(room); });
		if (!fits(reserved)) {
			++_waits;
			retryAt(share->isolatedKeptUntil + 1);
			return false;
		}
	}
	if (_readyBytes + _reservedBytes + reserved > kMaxIsolatedBytes) {
		return false;
	}
	_reservedBytes += reserved;
	share->tokens -= reserved;
	share->isolatedPending += reserved;
	enqueue({
		.emoji = emoji,
		.owner = owner,
		.reserved = reserved,
		.isolated = true,
	});
	return true;
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
	evict(
		_tiles,
		_tileBytes,
		&Share::held,
		std::nullopt,
		now,
		_keptUntil,
		[&] { return (_tileBytes + bytes <= kMaxTileBytes); });
}

template <typename Key>
void EmojiTiles::evict(
		base::flat_map<Key, Tile> &cache,
		int64 &cached,
		int64 Share::*held,
		std::optional<uint64> owner,
		crl::time now,
		crl::time &keptUntil,
		Fn<bool()> fits) {
	if (fits() || (now <= keptUntil)) {
		return;
	}
	auto old = std::vector<not_null<Tile*>>();
	auto until = now + kEmojiKeep;
	for (auto &entry : cache) {
		auto &tile = entry.second;
		if ((owner && (tile.owner != *owner)) || tile.image.isNull()) {
			continue;
		}
		const auto kept = tile.touched + kEmojiKeep;
		if (kept < now) {
			old.push_back(&tile);
		} else {
			until = std::min(until, kept);
		}
	}
	if (old.empty()) {
		keptUntil = until;
		return;
	}
	ranges::sort(old, ranges::less(), [](not_null<Tile*> tile) {
		return tile->used;
	});
	auto evicted = 0;
	for (const auto &tile : old) {
		if (fits()) {
			break;
		}
		const auto bytes = ImageBytes(tile->image.size());
		cached -= bytes;
		if (const auto share = this->share(tile->owner)) {
			share->*held -= bytes;
		}
		tile->owner = 0;
		++evicted;
	}
	if (evicted == int(old.size())) {
		keptUntil = until;
	}
	RemoveIf(cache, [](const Tile &tile) {
		return !tile.owner;
	});
}

void EmojiTiles::touch(Tile &tile, uint64 owner, crl::time now) {
	if (tile.owner == owner) {
		tile.used = ++_lastUse;
		tile.touched = now;
	}
}

void EmojiTiles::insertTile(const EmojiTile &tile, crl::time now) {
	const auto bytes = ImageBytes(tile.image.size());
	const auto share = this->share(tile.owner);
	if (!share || _tiles.contains(tile.key)) {
		_requested.remove(tile.key);
	} else if (tile.image.isNull()
		|| (_tileBytes + bytes > kMaxTileCeiling)) {
		_requested[tile.key] = now + kEmojiKeep;
	} else {
		_requested.remove(tile.key);
		_tiles.emplace(tile.key, Tile{
			.image = tile.image,
			.owner = tile.owner,
			.used = ++_lastUse,
			.touched = now,
		});
		_tileBytes += bytes;
		share->held += bytes;
	}
}

void EmojiTiles::retryAt(crl::time when) {
	if (!_retry) {
		_retry = std::make_unique<base::Timer>([] {
			Tiles().retry();
		});
	} else if (_retry->isActive() && (_retryAt <= when)) {
		return;
	}
	_retryAt = when;
	_retry->callOnce(std::max(when - crl::now(), crl::time(0)));
}

void EmojiTiles::retry() {
	_retryAt = 0;
	if (_landed) {
		_landed();
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
			load(request.emoji, request.owner, request.reserved);
		} else {
			tiles.push_back({
				.key = { request.emoji->index(), request.side },
				.image = DrawTile(request.emoji, request.side),
				.owner = request.owner,
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

void EmojiImages::load(EmojiPtr emoji, uint64 owner, int64 reserved) {
	const auto weak = base::make_weak(this);
	const auto generation = _generation;
	Core::App().emojiImageLoader().with([=](
			const Stickers::EmojiImageLoader &loader) {
		crl::on_main(weak, [
			=,
			image = loader.prepareWithoutOutline(emoji)
		]() mutable {
			landed(emoji, owner, generation, reserved, std::move(image));
		});
	});
}

void EmojiImages::landed(
		EmojiPtr emoji,
		uint64 owner,
		uint64 generation,
		int64 reserved,
		QImage image) {
	if (_finishing) {
		return;
	}
	PostToRenderThread([=, image = std::move(image)] {
		Tiles().landIsolated(generation, owner, emoji, reserved, image);
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

EmojiAccount::EmojiAccount()
: _id(NextEmojiAccountId()) {
	Tiles().open(_id);
}

EmojiAccount::~EmojiAccount() {
	Tiles().close(_id);
}

uint64 EmojiAccount::id() const {
	return _id;
}

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
		float64 deviceSide,
		const EmojiAccount &account) {
	Tiles().paint(account.id(), p, emoji, box, deviceSide);
}

uint64 EmojiTileWaits() {
	return Tiles().waits();
}

void ClearEmojiTiles() {
	Tiles() = EmojiTiles();
}

} // namespace Tgw
