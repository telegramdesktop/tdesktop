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

#include <map>

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
constexpr auto kGlyphBits = 64;
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
	uint64 admission = 0;
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
	void configure(
		EmojiSides sides,
		Fn<void(std::optional<int> glyph)> landed);
	void open(uint64 owner);
	void close(uint64 owner);
	void paint(
		uint64 owner,
		QPainter &p,
		EmojiPtr emoji,
		const QRectF &box,
		float64 deviceSide);
	void takeWaits(uint64 owner, EmojiWaits &into);
	void flush();
	void landTiles(uint64 generation, const std::vector<EmojiTile> &tiles);
	void landIsolated(
		uint64 generation,
		const EmojiRequest &request,
		const QImage &image);
	void reset(uint64 generation);

private:
	struct Tile {
		QImage image;
		uint64 owner = 0;
		uint64 used = 0;
		uint64 admission = 0;
		crl::time touched = 0;
	};
	struct Share {
		EmojiWaits paintWaits;
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
	struct Victims {
		std::vector<not_null<Tile*>> old;
		std::size_t count = 0;
		int64 bytes = 0;
		crl::time until = 0;
		bool gathered = false;
	};
	enum class Step : uchar {
		Skip,
		Wait,
		Refill,
		Retry,
		Evict,
		Commit,
	};
	struct TilePlan {
		Victims own;
		Victims all;
		std::map<TileKey, crl::time>::node_type node;
		TileKey key;
		int64 bytes = 0;
		crl::time retry = 0;
		Step step = Step::Skip;
		bool requested = false;
	};
	struct IsolatedPlan {
		Victims victims;
		std::map<int, Tile>::node_type node;
		int64 reserved = 0;
		crl::time retry = 0;
		Step step = Step::Skip;
		bool requested = false;
	};

	[[nodiscard]] static float64 Refilled(const Share &share, crl::time now);
	static void Refill(Share &share, crl::time now);
	template <typename Fits>
	static void Choose(Victims &victims, Fits fits);

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
	[[nodiscard]] TilePlan planTile(
		uint64 owner,
		TileKey key,
		crl::time now,
		int64 debit);
	[[nodiscard]] bool commitTile(
		uint64 owner,
		EmojiPtr emoji,
		TilePlan &plan,
		crl::time now);
	[[nodiscard]] IsolatedPlan planIsolated(
		uint64 owner,
		int index,
		crl::time now);
	void commitIsolated(
		uint64 owner,
		EmojiPtr emoji,
		IsolatedPlan &plan,
		crl::time now);
	void reserveRequests(int count);
	void enqueue(EmojiRequest request);
	[[nodiscard]] bool adopt(uint64 generation);
	void makeRoom(int64 bytes, crl::time now);
	template <typename Cache>
	[[nodiscard]] Victims gather(
		Cache &cache,
		std::optional<uint64> owner,
		crl::time now);
	void evict(
		const Victims &victims,
		int64 &cached,
		int64 Share::*held,
		crl::time &keptUntil);
	void touch(Tile &tile, uint64 owner, crl::time now);
	void insertTile(const EmojiTile &tile, crl::time now);
	void waitTile(uint64 owner);
	void waitGlyph(uint64 owner, int index);
	void landed(std::optional<int> glyph);
	void retryAt(crl::time when) noexcept;
	void retry();

	EmojiSides _sides;
	Fn<void(std::optional<int> glyph)> _landed;
	base::flat_map<TileKey, Tile> _tiles;
	std::map<int, Tile> _isolated;
	std::map<TileKey, crl::time> _requested;
	base::flat_map<uint64, Share> _shares;
	std::vector<EmojiRequest> _batch;
	std::unique_ptr<base::Timer> _retry;
	uint64 _generation = 0;
	uint64 _lastUse = 0;
	uint64 _admissions = 0;
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
	void load(const EmojiRequest &request);
	void landed(
		const EmojiRequest &request,
		uint64 generation,
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
	const auto from = std::remove_if(map.begin(), map.end(), [&](
			const auto &entry) {
		return drop(entry.second);
	});
	map.erase(from, map.end());
}

template <typename Key, typename Value, typename Drop>
void RemoveIf(std::map<Key, Value> &map, Drop drop) {
	std::erase_if(map, [&](const auto &entry) {
		return drop(entry.second);
	});
}

template <typename Cache>
void DropEvicted(Cache &cache, std::size_t count) {
	if (count) {
		RemoveIf(cache, [](const auto &tile) { return !tile.owner; });
	}
}

template <typename Map>
[[nodiscard]] typename Map::node_type MakeNode(
		typename Map::key_type key,
		typename Map::mapped_type value) {
	auto spare = Map();
	spare.emplace(std::move(key), std::move(value));
	return spare.extract(spare.begin());
}

void PostFlush() noexcept;

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

void PostFlush() noexcept {
	PostToRenderThread([] {
		Tiles().flush();
	});
}

[[nodiscard]] uint64 NextEmojiAccountId() {
	Expects(OnRenderThread());

	static auto serial = uint64();
	return ++serial;
}

void EmojiTiles::configure(
		EmojiSides sides,
		Fn<void(std::optional<int> glyph)> landed) {
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
		if ((entry.owner != owner) || entry.admission) {
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

void EmojiTiles::takeWaits(uint64 owner, EmojiWaits &into) {
	into.clear();
	if (const auto share = this->share(owner)) {
		std::swap(into, share->paintWaits);
	}
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
			_requested.erase(tile.key);
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
	landed(std::nullopt);
}

void EmojiTiles::landIsolated(
		uint64 generation,
		const EmojiRequest &request,
		const QImage &image) {
	const auto reserved = request.reserved;
	_reservedBytes -= reserved;
	const auto share = this->share(request.owner);
	if (share) {
		share->isolatedPending -= reserved;
	}
	const auto index = request.emoji->index();
	const auto i = adopt(generation) ? _isolated.find(index) : end(_isolated);
	if (i == end(_isolated) || (i->second.admission != request.admission)) {
		return;
	}
	auto &entry = i->second;
	const auto bytes = ImageBytes(image.size());
	entry.admission = 0;
	if (!share) {
		_isolated.erase(i);
	} else if (image.isNull() || (bytes > reserved)) {
		return;
	} else {
		entry.image = image;
		touch(entry, request.owner, crl::now());
		_readyBytes += bytes;
		share->isolatedHeld += bytes;
	}
	landed(index);
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

float64 EmojiTiles::Refilled(const Share &share, crl::time now) {
	const auto earned = (now - share.refilled)
		* float64(kShareRate)
		/ kEmojiKeep;
	return std::min(float64(kShareBurst), share.tokens + earned);
}

void EmojiTiles::Refill(Share &share, crl::time now) {
	share.tokens = Refilled(share, now);
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
	auto plan = planTile(owner, key, now, 0);
	reserveRequests(plan.requested ? 1 : 0);
	if (commitTile(owner, emoji, plan, now)) {
		waitTile(owner);
	}
	return QImage();
}

QImage EmojiTiles::isolatedImage(
		uint64 owner,
		EmojiPtr emoji,
		crl::time now) {
	const auto index = emoji->index();
	const auto i = _isolated.find(index);
	if (i != end(_isolated) && !i->second.image.isNull()) {
		touch(i->second, owner, now);
		return i->second.image;
	}
	auto isolated = (i == end(_isolated))
		? planIsolated(owner, index, now)
		: IsolatedPlan();
	const auto key = TileKey{ index, _sides.large };
	const auto cached = _tiles.contains(key);
	const auto debit = isolated.requested ? isolated.reserved : int64();
	auto fallback = cached ? TilePlan() : planTile(owner, key, now, debit);
	reserveRequests((isolated.requested ? 1 : 0)
		+ (fallback.requested ? 1 : 0));
	commitIsolated(owner, emoji, isolated, now);
	waitGlyph(owner, index);
	if (cached) {
		auto &tile = _tiles.find(key)->second;
		touch(tile, owner, now);
		return tile.image;
	} else if (commitTile(owner, emoji, fallback, now)) {
		waitTile(owner);
	}
	return QImage();
}

EmojiTiles::TilePlan EmojiTiles::planTile(
		uint64 owner,
		TileKey key,
		crl::time now,
		int64 debit) {
	auto result = TilePlan{ .key = key, .bytes = TileBytes(key.side) };
	const auto bytes = result.bytes;
	const auto i = _requested.find(key);
	const auto retry = (i != end(_requested)) ? i->second : crl::time();
	const auto share = this->share(owner);
	if (retry == kUntilAnswered) {
		result.step = Step::Wait;
		return result;
	} else if (retry > now) {
		return result;
	} else if (!share
		|| (_batchBytes && (_batchBytes + bytes > kMaxTileBytes))) {
		result.step = Step::Wait;
		return result;
	}
	const auto tokens = Refilled(*share, now) - debit;
	if (share->batch + bytes > kShareBatch) {
		result.step = Step::Refill;
		return result;
	} else if (tokens < bytes) {
		result.step = Step::Retry;
		result.retry = now + TokenWait(bytes - tokens);
		return result;
	}
	const auto held = [&](int64 freed, int64 room) {
		return (share->held - freed + share->pending + room <= kShareHeld);
	};
	auto &own = result.own;
	if (!held(0, bytes) && (now > share->keptUntil)) {
		const auto room = kShareBatch - share->batch;
		own = gather(_tiles, owner, now);
		Choose(own, [&](int64 freed) { return held(freed, room); });
	}
	if (!held(own.bytes, bytes)) {
		result.step = Step::Evict;
		result.retry = now + kEmojiKeep;
		return result;
	}
	const auto cached = _tileBytes - own.bytes;
	const auto over = [&](int64 freed) {
		return (cached - freed + _pendingBytes + bytes > kMaxTileCeiling);
	};
	auto &all = result.all;
	if (over(0) && (cached + bytes > kMaxTileBytes) && (now > _keptUntil)) {
		all = gather(_tiles, std::nullopt, now);
		if (own.count) {
			const auto last = own.old[own.count - 1]->used;
			std::erase_if(all.old, [&](not_null<Tile*> tile) {
				return (tile->owner == owner) && (tile->used <= last);
			});
		}
		Choose(all, [&](int64 freed) {
			return (cached - freed + bytes <= kMaxTileBytes);
		});
	}
	if (i == end(_requested)) {
		result.node = MakeNode<decltype(_requested)>(key, crl::time());
	}
	result.requested = !over(all.bytes);
	result.step = Step::Commit;
	return result;
}

bool EmojiTiles::commitTile(
		uint64 owner,
		EmojiPtr emoji,
		TilePlan &plan,
		crl::time now) {
	if (plan.step == Step::Skip) {
		return false;
	} else if (plan.step == Step::Wait) {
		return true;
	}
	const auto share = this->share(owner);
	Refill(*share, now);
	if (plan.step == Step::Refill) {
		return true;
	} else if (plan.step == Step::Retry) {
		retryAt(plan.retry);
		return true;
	}
	evict(plan.own, _tileBytes, &Share::held, share->keptUntil);
	if (plan.step == Step::Evict) {
		DropEvicted(_tiles, plan.own.count);
		retryAt(plan.retry);
		return true;
	}
	evict(plan.all, _tileBytes, &Share::held, _keptUntil);
	DropEvicted(_tiles, plan.own.count + plan.all.count);
	const auto until = plan.requested ? kUntilAnswered : (now + kEmojiKeep);
	if (plan.node) {
		plan.node.mapped() = until;
		_requested.insert(std::move(plan.node));
	} else {
		_requested.find(plan.key)->second = until;
	}
	if (!plan.requested) {
		return false;
	}
	const auto bytes = plan.bytes;
	_pendingBytes += bytes;
	_batchBytes += bytes;
	share->tokens -= bytes;
	share->batch += bytes;
	share->pending += bytes;
	enqueue({ .emoji = emoji, .owner = owner, .side = plan.key.side });
	return true;
}

EmojiTiles::IsolatedPlan EmojiTiles::planIsolated(
		uint64 owner,
		int index,
		crl::time now) {
	auto result = IsolatedPlan();
	const auto share = this->share(owner);
	if (!share) {
		return result;
	}
	const auto tokens = Refilled(*share, now);
	const auto side = _sides.isolated;
	result.reserved = ImageBytes(QSize(side, side));
	const auto reserved = result.reserved;
	if (tokens < reserved) {
		result.step = Step::Retry;
		result.retry = now + TokenWait(reserved - tokens);
		return result;
	}
	const auto fits = [&](int64 freed, int64 room) {
		return (share->isolatedHeld - freed + share->isolatedPending + room
			<= kShareIsolated);
	};
	auto &victims = result.victims;
	if (!fits(0, reserved) && (now > share->isolatedKeptUntil)) {
		const auto room = std::max(reserved, kShareBatch);
		victims = gather(_isolated, owner, now);
		Choose(victims, [&](int64 freed) { return fits(freed, room); });
	}
	if (!fits(victims.bytes, reserved)) {
		result.step = Step::Evict;
		return result;
	}
	result.requested = (_readyBytes - victims.bytes + _reservedBytes + reserved
		<= kMaxIsolatedBytes);
	if (result.requested) {
		result.node = MakeNode<decltype(_isolated)>(
			index,
			Tile{ .owner = owner });
	}
	result.step = Step::Commit;
	return result;
}

void EmojiTiles::commitIsolated(
		uint64 owner,
		EmojiPtr emoji,
		IsolatedPlan &plan,
		crl::time now) {
	if (plan.step == Step::Skip) {
		return;
	}
	const auto share = this->share(owner);
	Refill(*share, now);
	if (plan.step == Step::Retry) {
		waitTile(owner);
		retryAt(plan.retry);
		return;
	}
	evict(
		plan.victims,
		_readyBytes,
		&Share::isolatedHeld,
		share->isolatedKeptUntil);
	DropEvicted(_isolated, plan.victims.count);
	if (plan.step == Step::Evict) {
		waitTile(owner);
		retryAt(share->isolatedKeptUntil + 1);
		return;
	} else if (!plan.requested) {
		return;
	}
	const auto admission = ++_admissions;
	plan.node.mapped().admission = admission;
	_isolated.insert(std::move(plan.node));
	_reservedBytes += plan.reserved;
	share->tokens -= plan.reserved;
	share->isolatedPending += plan.reserved;
	enqueue({
		.emoji = emoji,
		.owner = owner,
		.admission = admission,
		.reserved = plan.reserved,
		.isolated = true,
	});
}

void EmojiTiles::reserveRequests(int count) {
	const auto needed = _batch.size() + std::size_t(count);
	if (needed > _batch.capacity()) {
		_batch.reserve(std::max({
			_batch.capacity() * 2,
			needed,
			std::size_t(16),
		}));
	}
}

void EmojiTiles::enqueue(EmojiRequest request) {
	const auto first = _batch.empty();
	_batch.push_back(request);
	if (first) {
		PostFlush();
	}
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
	const auto fits = [&](int64 freed) {
		return (_tileBytes - freed + bytes <= kMaxTileBytes);
	};
	if (fits(0) || (now <= _keptUntil)) {
		return;
	}
	auto victims = gather(_tiles, std::nullopt, now);
	Choose(victims, fits);
	evict(victims, _tileBytes, &Share::held, _keptUntil);
	DropEvicted(_tiles, victims.count);
}

template <typename Cache>
EmojiTiles::Victims EmojiTiles::gather(
		Cache &cache,
		std::optional<uint64> owner,
		crl::time now) {
	auto result = Victims{ .until = now + kEmojiKeep, .gathered = true };
	for (auto &entry : cache) {
		auto &tile = entry.second;
		if ((owner && (tile.owner != *owner)) || tile.image.isNull()) {
			continue;
		}
		const auto kept = tile.touched + kEmojiKeep;
		if (kept < now) {
			result.old.push_back(&tile);
		} else {
			result.until = std::min(result.until, kept);
		}
	}
	ranges::sort(result.old, ranges::less(), [](not_null<Tile*> tile) {
		return tile->used;
	});
	return result;
}

template <typename Fits>
void EmojiTiles::Choose(Victims &victims, Fits fits) {
	while ((victims.count < victims.old.size()) && !fits(victims.bytes)) {
		const auto tile = victims.old[victims.count++];
		victims.bytes += ImageBytes(tile->image.size());
	}
}

void EmojiTiles::evict(
		const Victims &victims,
		int64 &cached,
		int64 Share::*held,
		crl::time &keptUntil) {
	if (!victims.gathered) {
		return;
	}
	for (auto i = std::size_t(); i != victims.count; ++i) {
		const auto tile = victims.old[i];
		const auto bytes = ImageBytes(tile->image.size());
		cached -= bytes;
		if (const auto share = this->share(tile->owner)) {
			share->*held -= bytes;
		}
		tile->owner = 0;
	}
	if (victims.count == victims.old.size()) {
		keptUntil = victims.until;
	}
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
		_requested.erase(tile.key);
	} else if (tile.image.isNull()
		|| (_tileBytes + bytes > kMaxTileCeiling)) {
		_requested[tile.key] = now + kEmojiKeep;
	} else {
		_requested.erase(tile.key);
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

void EmojiTiles::waitTile(uint64 owner) {
	if (const auto share = this->share(owner)) {
		share->paintWaits.tile();
	}
}

void EmojiTiles::waitGlyph(uint64 owner, int index) {
	if (const auto share = this->share(owner)) {
		share->paintWaits.glyph(index);
	}
}

void EmojiTiles::landed(std::optional<int> glyph) {
	if (_landed) {
		_landed(glyph);
	}
}

void EmojiTiles::retryAt(crl::time when) noexcept {
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
	landed(std::nullopt);
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
			load(request);
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

void EmojiImages::load(const EmojiRequest &request) {
	const auto weak = base::make_weak(this);
	const auto generation = _generation;
	Core::App().emojiImageLoader().with([=](
			const Stickers::EmojiImageLoader &loader) {
		crl::on_main(weak, [
			=,
			image = loader.prepareWithoutOutline(request.emoji)
		]() mutable {
			landed(request, generation, std::move(image));
		});
	});
}

void EmojiImages::landed(
		const EmojiRequest &request,
		uint64 generation,
		QImage image) {
	if (_finishing) {
		return;
	}
	PostToRenderThread([=, image = std::move(image)] {
		Tiles().landIsolated(generation, request, image);
	});
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
	close();
}

uint64 EmojiAccount::id() const {
	return _id;
}

void EmojiAccount::close() {
	Tiles().close(_id);
}

EmojiWaits::EmojiWaits()
: _glyphs((Ui::Emoji::internal::FullCount() + kGlyphBits - 1) / kGlyphBits) {
}

void EmojiWaits::tile() {
	_tiles = true;
}

void EmojiWaits::glyph(int index) {
	Expects(index >= 0 && index < int(_glyphs.size() * kGlyphBits));

	_glyphs[index / kGlyphBits] |= (uint64(1) << (index % kGlyphBits));
}

void EmojiWaits::add(const EmojiWaits &other) {
	_tiles = _tiles || other._tiles;
	const auto count = std::min(_glyphs.size(), other._glyphs.size());
	for (auto i = std::size_t(); i != count; ++i) {
		_glyphs[i] |= other._glyphs[i];
	}
}

void EmojiWaits::clear() {
	ranges::fill(_glyphs, uint64());
	_tiles = false;
}

bool EmojiWaits::wants(std::optional<int> glyph) const {
	if (!glyph) {
		return _tiles;
	}
	const auto index = *glyph;
	return (index >= 0)
		&& (index < int(_glyphs.size() * kGlyphBits))
		&& ((_glyphs[index / kGlyphBits] >> (index % kGlyphBits)) & 1);
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

void ConfigureEmojiTiles(
		EmojiSides sides,
		Fn<void(std::optional<int> glyph)> landed) {
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

void TakeEmojiWaits(const EmojiAccount &account, EmojiWaits &into) {
	Tiles().takeWaits(account.id(), into);
}

void ClearEmojiTiles() {
	Tiles() = EmojiTiles();
}

} // namespace Tgw
