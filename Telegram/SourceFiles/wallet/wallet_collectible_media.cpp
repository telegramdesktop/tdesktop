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
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "ui/image/image_location.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_session.h"

#include <QtCore/QUrl>

namespace Wallet {
namespace {

constexpr auto kResolveBudget = 10;
constexpr auto kChainTimeout = 60 * crl::time(1000);

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
	if (!item.name.isEmpty()) {
		return item.name;
	} else if (!item.domain.isEmpty()) {
		return item.domain;
	}
	const auto slug = UriSlug(item.contentUri);
	return slug.isEmpty()
		? FormatFriendly(item.address, true)
		: slug;
}

[[nodiscard]] const std::optional<Gram::NftWebDocument> &ArtworkDocument(
		const Gram::NftItem &item) {
	return item.imageSmall ? item.imageSmall : item.image;
}

// WHY: artwork bytes follow the document URL, so a new access hash alone
// matters only to a load that failed. The members compared here mirror what
// startArtwork() reads and must be extended together with it.
[[nodiscard]] bool SameArtworkSource(
		const Gram::NftItem &was,
		const Gram::NftItem &now,
		bool exact) {
	if (was.kind != now.kind || was.key != now.key) {
		return false;
	}
	const auto &before = ArtworkDocument(was);
	const auto &after = ArtworkDocument(now);
	if (!before || !after) {
		return !before && !after;
	}
	return (before->url == after->url)
		&& (!exact || before->accessHash == after->accessHash);
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
		const QString &address) {
	if (!unique || address.isEmpty()) {
		return false;
	}
	// Both sides name the same on-chain address, but they are produced by
	// different canonicalizers: NftItem::address lowercases the hash, while
	// the engine's parser decides its own case, so compare case-insensitively
	// rather than depending on either one.
	const auto parsed = ParseAddress(unique->giftAddress);
	return parsed
		&& !parsed->raw.isEmpty()
		&& !parsed->raw.compare(address, Qt::CaseInsensitive);
}

TextWithEntities CollectibleTitleText(const CollectibleView &view) {
	auto result = Ui::Text::Semibold(view.name);
	if (!view.number.isEmpty()) {
		result.append(Ui::Text::Colorized(view.number));
	}
	return result;
}

QString CollectibleKindText(const CollectibleView &view) {
	using Kind = Gram::NftKind;
	return (view.kind == Kind::TelegramGift && view.authenticGift)
		? tr::lng_wallet_chip_gift(tr::now)
		: (view.kind == Kind::TelegramUsername)
		? tr::lng_wallet_chip_username(tr::now)
		: (view.kind == Kind::TelegramNumber)
		? tr::lng_wallet_chip_number(tr::now)
		: tr::lng_wallet_chip_nft(tr::now);
}

QString CollectibleSubtitleText(const CollectibleView &view) {
	using Kind = Gram::NftKind;
	if (view.kind == Kind::TelegramUsername
		|| view.kind == Kind::TelegramNumber) {
		return CollectibleKindText(view);
	} else if (view.kind == Kind::TelegramGift
		&& !view.model.isEmpty()
		&& !view.backdrop.isEmpty()) {
		return tr::lng_wallet_collectible_gift_attributes(
			tr::now,
			lt_model,
			view.model,
			lt_backdrop,
			view.backdrop);
	} else if (!view.collectionName.isEmpty()) {
		return view.collectionName;
	}
	return tr::lng_wallet_chip_nft(tr::now);
}

struct CollectibleMedia::Entry {
	QString address;
	Gram::NftItem record;
	QString fallback;
	QString name;
	QString number;
	QString model;
	QString backdrop;
	QString collectionName;
	QByteArray imageBytes;
	int artworkGeneration = 0;
	base::flat_map<int, QImage> prepared;
	PaintRoundImageCallback giftPaint;
	std::unique_ptr<FileLoader> loader;
	crl::time deadline = 0;
	State state = State::None;
	bool sticky = false;
	bool authenticGift = false;
};

CollectibleMedia::CollectibleMedia(not_null<Main::Session*> session)
: _session(session)
, _timeoutTimer([=] { checkTimeouts(); }) {
	_session->wallet().collectiblesUpdates(
	) | rpl::on_next([=] {
		refreshFromCollectibles();
	}, _lifetime);
}

CollectibleMedia::~CollectibleMedia() {
	// Cancelling a loader delivers its `done` synchronously; keep lanes shut.
	_starting = true;
	for (const auto &[raw, entry] : _map) {
		if (const auto loader = base::take(entry->loader)) {
			loader->cancel();
		}
	}
}

not_null<CollectibleMedia::Entry*> CollectibleMedia::prepare(
		const QString &item) {
	auto &pointer = _map[item];
	if (!pointer) {
		pointer = std::make_unique<Entry>();
		pointer->address = item;
		pointer->fallback = FormatFriendly(item, true);
	}
	return pointer.get();
}

void CollectibleMedia::resolve(const QString &item) {
	const auto entry = prepare(item);
	entry->sticky = true;
	if (entry->state != State::None
		&& entry->state != State::Window
		&& entry->state != State::Background) {
		return;
	}
	entry->state = State::Sticky;
	_sticky.push_back(item);
	checkStartNext();
}

void CollectibleMedia::resolveBackground(const QString &item) {
	const auto entry = prepare(item);
	if (entry->state != State::None) {
		return;
	}
	entry->state = State::Background;
	_background.push_back(item);
	checkStartNext();
}

void CollectibleMedia::setListWindow(std::vector<QString> ordered) {
	for (const auto &item : _window) {
		const auto entry = find(item);
		if (entry && entry->state == State::Window) {
			entry->state = State::None;
		}
	}
	_window.clear();
	_windowCursor = 0;
	_window.reserve(ordered.size());
	for (const auto &item : ordered) {
		const auto entry = prepare(item);
		if (entry->state == State::None) {
			entry->state = State::Window;
			_window.push_back(item);
		} else if (entry->state == State::Background) {
			// Listed by the window, still owned by the background lane: the
			// window drains first, and the claim survives the reset loop.
			_window.push_back(item);
		}
	}
	checkStartNext();
}

void CollectibleMedia::checkStartNext() {
	if (_starting) {
		return;
	}
	_starting = true;
	const auto guard = gsl::finally([&] { _starting = false; });
	while (_inFlight < kResolveBudget) {
		const auto entry = takeNextQueued();
		if (!entry) {
			break;
		}
		startChain(entry);
	}
	scheduleTimeoutCheck();
}

CollectibleMedia::Entry *CollectibleMedia::takeNextQueued() {
	const auto take = [&](
			std::vector<QString> &queue,
			int &cursor,
			State state,
			bool alsoBackground = false) -> Entry* {
		while (cursor < int(queue.size())) {
			const auto entry = find(queue[cursor++]);
			if (entry
				&& ((entry->state == state)
					|| (alsoBackground
						&& (entry->state == State::Background)))) {
				return entry;
			}
		}
		queue.clear();
		cursor = 0;
		return nullptr;
	};
	if (const auto entry = take(_sticky, _stickyCursor, State::Sticky)) {
		return entry;
	}
	if (const auto entry = take(_window, _windowCursor, State::Window, true)) {
		return entry;
	}
	return take(_background, _backgroundCursor, State::Background);
}

void CollectibleMedia::startChain(not_null<Entry*> entry) {
	entry->state = State::Flight;
	entry->deadline = crl::now() + kChainTimeout;
	++_inFlight;
	const auto generation = entry->artworkGeneration;
	_session->wallet().resolveCollectibleInfo(
		entry->address,
		crl::guard(this, [=](const Gram::NftItem &record) {
			resolveFromRecord(entry, record, generation);
		}));
}

void CollectibleMedia::finishChain(not_null<Entry*> entry, State state) {
	if (entry->state != State::Flight) {
		return;
	}
	entry->state = state;
	--_inFlight;
	checkStartNext();
}

void CollectibleMedia::requeue(not_null<Entry*> entry) {
	// A finished entry sits in no lane any more, so a refetch has to claim
	// one again. The sticky claim is the only one that outlives a chain and
	// is restored here; everything else goes to the background lane, out of
	// which the list rebuild that follows the same update lifts the rows
	// that are actually on screen back into the window.
	if (entry->state == State::Flight) {
		finishChain(entry, State::None);
	}
	if (entry->sticky) {
		entry->state = State::Sticky;
		_sticky.push_back(entry->address);
	} else {
		entry->state = State::Background;
		_background.push_back(entry->address);
	}
	checkStartNext();
}

void CollectibleMedia::scheduleTimeoutCheck() {
	auto earliest = crl::time(0);
	for (const auto &[raw, entry] : _map) {
		if (entry->state == State::Flight
			&& (!earliest || entry->deadline < earliest)) {
			earliest = entry->deadline;
		}
	}
	if (!earliest) {
		_timeoutTimer.cancel();
		return;
	}
	const auto now = crl::now();
	_timeoutTimer.callOnce((earliest > now) ? (earliest - now) : crl::time(0));
}

void CollectibleMedia::checkTimeouts() {
	// Only a callback reaches finishChain, and the web legs have no transfer
	// timeout of their own, so a host that answers the handshake and then
	// goes silent would hold its slot for the rest of the session. Expiring
	// entries are collected before anything is finished, because finishing
	// one drains the lanes and stamps fresh deadlines on the chains that
	// start; the entries themselves are stable, only their states are not.
	const auto now = crl::now();
	auto expired = std::vector<not_null<Entry*>>();
	for (const auto &[raw, entry] : _map) {
		if (entry->state == State::Flight && now >= entry->deadline) {
			expired.push_back(entry.get());
		}
	}
	for (const auto &entry : expired) {
		if (entry->state != State::Flight) {
			continue;
		} else if (const auto loader = entry->loader.get()) {
			loader->cancel();
		}
		entry->loader = nullptr;
		finishChain(entry, State::Failed);
	}
	scheduleTimeoutCheck();
}

void CollectibleMedia::resolveFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record,
		int generation) {
	// A refresh that lands mid-chain orphans this callback: it applies the
	// fresh record itself, stamps a new generation and re-queues the entry,
	// so the answer this chain was sent for describes a record that is gone
	// and its slot has already been handed back.
	if (entry->state != State::Flight
		|| entry->artworkGeneration != generation) {
		return;
	} else if (record.address.isEmpty()) {
		finishChain(entry, State::None);
		return;
	}
	applyRecord(entry, record);
	startArtwork(entry);
}

void CollectibleMedia::refreshFromCollectibles() {
	{
		// Apply the whole update before draining the lanes: finishing an
		// orphaned chain frees a slot, and starting the next chain from the
		// middle of the loop would hand that slot to a row the loop has not
		// reached yet, ahead of the ones it has already re-queued.
		_starting = true;
		const auto guard = gsl::finally([&] { _starting = false; });
		for (const auto &item : _session->wallet().collectibles()) {
			if (const auto entry = find(item.address)) {
				refreshFromRecord(entry, item);
			}
		}
	}
	checkStartNext();
}

void CollectibleMedia::refreshFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record) {
	if (applyRecord(entry, record)) {
		requeue(entry);
	}
}

bool CollectibleMedia::applyRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record) {
	if (record == entry->record) {
		return false;
	}
	const auto artwork = !SameArtworkSource(
		entry->record,
		record,
		entry->state == State::Failed);
	entry->record = record;
	entry->fallback = FallbackTitle(record);
	entry->collectionName = record.collectionName;
	if (artwork) {
		clearArtwork(entry);
	}
	_changed.fire_copy(entry->address);
	if (artwork) {
		_repaint.fire_copy(entry->address);
	}
	return artwork;
}

void CollectibleMedia::startArtwork(not_null<Entry*> entry) {
	const auto &record = entry->record;
	const auto generation = entry->artworkGeneration;
	if (record.kind == Gram::NftKind::TelegramGift && !record.key.isEmpty()) {
		requestGift(entry, record.key, generation);
	} else if (const auto &document = ArtworkDocument(record)) {
		startImageLoad(entry, *document, generation);
	} else {
		finishChain(entry, State::Done);
	}
}

void CollectibleMedia::clearArtwork(not_null<Entry*> entry) {
	++entry->artworkGeneration;
	entry->loader = nullptr;
	entry->name.clear();
	entry->number.clear();
	entry->model.clear();
	entry->backdrop.clear();
	entry->authenticGift = false;
	entry->giftPaint = nullptr;
	entry->imageBytes.clear();
	entry->prepared.clear();
}

void CollectibleMedia::requestGift(
		not_null<Entry*> entry,
		const QString &slug,
		int generation) {
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
		entry->model = unique->model.name;
		entry->backdrop = unique->backdrop.name;
		entry->authenticGift = true;
		_changed.fire_copy(entry->address);
		_repaint.fire_copy(entry->address);
		finishChain(entry, State::Done);
	};
	const auto fallback = [=] {
		if (const auto &document = ArtworkDocument(entry->record)) {
			startImageLoad(entry, *document, generation);
		} else {
			finishChain(entry, State::Done);
		}
	};
	session->api().request(MTPpayments_GetUniqueStarGift(
		MTP_string(slug)
	)).done(crl::guard(this, [=](
			const MTPpayments_UniqueStarGift &result) {
		if (entry->artworkGeneration != generation) {
			return;
		}
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
		if (entry->artworkGeneration != generation) {
			return;
		}
		fallback();
	})).send();
}

void CollectibleMedia::startImageLoad(
		not_null<Entry*> entry,
		const Gram::NftWebDocument &document,
		int generation) {
	if (entry->state != State::Flight
		|| entry->artworkGeneration != generation) {
		return;
	}
	entry->loader = CreateFileLoader(
		_session,
		DownloadLocation{
			WebFileLocation(document.url, document.accessHash),
		},
		Data::FileOrigin(),
		QString(),
		0,
		0,
		UnknownFileLocation,
		LoadToCacheAsWell,
		LoadFromCloudOrLocal,
		false,
		0);
	const auto raw = entry->loader.get();
	const auto finish = [=](QByteArray bytes) {
		if (entry->artworkGeneration != generation) {
			return;
		}
		crl::on_main(this, [=] {
			if (entry->artworkGeneration == generation) {
				entry->loader = nullptr;
			}
		});
		if (bytes.isEmpty()) {
			finishChain(entry, State::Failed);
			return;
		}
		entry->imageBytes = std::move(bytes);
		entry->prepared.clear();
		_changed.fire_copy(entry->address);
		_repaint.fire_copy(entry->address);
		finishChain(entry, State::Done);
	};
	raw->updates() | rpl::on_next_error_done([] {
	}, [=](FileLoader::Error error) {
		finish(QByteArray());
	}, [=] {
		// The loader dies on the next main loop turn and its bytes do not
		// outlive it, so hand out a copy the consumer owns: the artwork is
		// kept and decoded lazily, on the first paint of each thumbnail.
		const auto &loaded = raw->bytes();
		finish(raw->cancelled()
			? QByteArray()
			: QByteArray(loaded.constData(), loaded.size()));
	}, raw->lifetime());
	raw->start();
}

CollectibleView CollectibleMedia::view(const QString &item) const {
	const auto entry = find(item);
	if (!entry) {
		const auto title = SplitNumberTail(FormatFriendly(item, true));
		return { title.first, title.second, QString() };
	} else if (!entry->number.isEmpty()) {
		return {
			entry->name,
			entry->number,
			entry->collectionName,
			entry->record.kind,
			entry->authenticGift,
			entry->record.key,
			entry->model,
			entry->backdrop,
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
		entry->authenticGift,
		entry->record.key,
		entry->model,
		entry->backdrop,
	};
}

QString CollectibleMedia::collection(const QString &item) const {
	const auto entry = find(item);
	return entry ? entry->record.collection : QString();
}

void CollectibleMedia::paint(
		Painter &p,
		const QString &item,
		QRect rect,
		int outerWidth,
		int radius) {
	if (paintArtwork(p, item, rect, outerWidth, radius)) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);
}

bool CollectibleMedia::paintArtwork(
		Painter &p,
		const QString &item,
		QRect rect,
		int outerWidth,
		int radius) {
	const auto entry = find(item);
	if (!entry) {
		return false;
	}
	auto hq = PainterHighQualityEnabler(p);
	if (entry->giftPaint) {
		auto path = QPainterPath();
		path.addRoundedRect(rect, radius, radius);
		p.save();
		p.setClipPath(path);
		entry->giftPaint(p, rect.x(), rect.y(), outerWidth, rect.width());
		p.restore();
		return true;
	}
	const auto &image = preparedFor(entry, rect.width(), radius);
	if (image.isNull()) {
		return false;
	}
	p.drawImage(rect, image);
	return true;
}

rpl::producer<QString> CollectibleMedia::changed() const {
	return _changed.events();
}

rpl::producer<QString> CollectibleMedia::repaint() const {
	return _repaint.events();
}

CollectibleMedia::Entry *CollectibleMedia::find(
		const QString &item) const {
	const auto i = _map.find(item);
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
