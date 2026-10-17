/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "gram/api/gram_api_nft.h"

#include <memory>
#include <vector>

class Painter;

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

[[nodiscard]] bool UniqueGiftMatchesAddress(
	const std::shared_ptr<Data::UniqueGift> &unique,
	const QString &address);

struct CollectibleView {
	QString name;
	QString number;
	QString collectionName;
	Gram::NftKind kind = Gram::NftKind::Generic;
	bool authenticGift = false;
	QString key;
	QString model;
	QString backdrop;
};

[[nodiscard]] TextWithEntities CollectibleTitleText(
	const CollectibleView &view);
[[nodiscard]] QString CollectibleKindText(
	const CollectibleView &view);
[[nodiscard]] QString CollectibleSubtitleText(
	const CollectibleView &view);

class CollectibleMedia final : public base::has_weak_ptr {
public:
	explicit CollectibleMedia(not_null<Main::Session*> session);
	~CollectibleMedia();

	void resolve(const QString &item);
	void resolveBackground(const QString &item);
	void setListWindow(std::vector<QString> ordered);

	[[nodiscard]] CollectibleView view(const QString &item) const;
	[[nodiscard]] QString collection(const QString &item) const;
	void paint(
		Painter &p,
		const QString &item,
		QRect rect,
		int outerWidth,
		int radius);
	bool paintArtwork(
		Painter &p,
		const QString &item,
		QRect rect,
		int outerWidth,
		int radius);

	[[nodiscard]] rpl::producer<QString> changed() const;
	[[nodiscard]] rpl::producer<QString> repaint() const;

private:
	enum class State : uchar {
		None,
		Sticky,
		Window,
		Background,
		Flight,
		Done,
		Failed,
	};
	struct Entry;

	void resolveFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record,
		int generation);
	void refreshFromCollectibles();
	void refreshFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record);
	bool applyRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record);
	void startArtwork(not_null<Entry*> entry);
	void clearArtwork(not_null<Entry*> entry);
	void requestGift(
		not_null<Entry*> entry,
		const QString &slug,
		int generation);
	void startImageLoad(
		not_null<Entry*> entry,
		const Gram::NftWebDocument &document,
		int generation);
	[[nodiscard]] Entry *find(const QString &item) const;
	[[nodiscard]] const QImage &preparedFor(
		not_null<Entry*> entry,
		int side,
		int radius);
	[[nodiscard]] not_null<Entry*> prepare(const QString &item);
	[[nodiscard]] Entry *takeNextQueued();
	void checkStartNext();
	void startChain(not_null<Entry*> entry);
	void finishChain(not_null<Entry*> entry, State state);
	void requeue(not_null<Entry*> entry);
	void scheduleTimeoutCheck();
	void checkTimeouts();

	const not_null<Main::Session*> _session;
	base::Timer _timeoutTimer;
	rpl::event_stream<QString> _changed;
	rpl::event_stream<QString> _repaint;
	base::flat_map<QString, std::unique_ptr<Entry>> _map;
	rpl::lifetime _lifetime;

	// The sticky lane is what a surface the user asked for waits on — a
	// details box, an opened collectible. It is never pruned and is always
	// drained first. The window lane is a projection of the collectibles
	// list's visible plus preload range, replaced whole on every change,
	// which is what keeps a fast scroll's landing point off the back of the
	// queue behind the rows it flew past. The background lane is drained
	// last and holds bulk enqueues nobody is looking at: history row chips
	// are built for every loaded transaction whatever the active tab, so
	// without a lane of their own they would starve the visible rows.
	std::vector<QString> _sticky;
	std::vector<QString> _window;
	std::vector<QString> _background;
	int _stickyCursor = 0;
	int _windowCursor = 0;
	int _backgroundCursor = 0;
	int _inFlight = 0;
	bool _starting = false;

};

} // namespace Wallet
