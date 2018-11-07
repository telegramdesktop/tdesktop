/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item_components.h"
#include "base/weak_ptr.h"

class SharedMediaWithLastSlice;
class UserPhotosSlice;
struct WebPageCollage;

namespace Media {
namespace View {

class GroupThumbs : public base::has_weak_ptr {
public:
	struct CollageKey {
		int index = 0;

		inline bool operator<(const CollageKey &other) const {
			return index < other.index;
		}
	};
	struct CollageSlice {
		FullMsgId context;
		not_null<const WebPageCollage*> data;

		int size() const;
	};
	using Key = base::variant<PhotoId, FullMsgId, CollageKey>;

	static void Refresh(
		std::unique_ptr<GroupThumbs> &instance,
		const SharedMediaWithLastSlice &slice,
		int index,
		int availableWidth);
	static void Refresh(
		std::unique_ptr<GroupThumbs> &instance,
		const UserPhotosSlice &slice,
		int index,
		int availableWidth);
	static void Refresh(
		std::unique_ptr<GroupThumbs> &instance,
		const CollageSlice &slice,
		int index,
		int availableWidth);
	void clear();

	void resizeToWidth(int newWidth);
	int height() const;
	bool hiding() const;
	bool hidden() const;
	void checkForAnimationStart();

	void paint(Painter &p, int x, int y, int outerWidth, TimeMs ms);
	ClickHandlerPtr getState(QPoint point) const;

	rpl::producer<QRect> updateRequests() const {
		return _updateRequests.events();
	}

	rpl::producer<Key> activateRequests() const {
		return _activateStream.events();
	}

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	using Context = base::optional_variant<
		PeerId,
		MessageGroupId,
		FullMsgId>;

	GroupThumbs(Context context);
	~GroupThumbs();

private:
	class Thumb;

	template <typename Slice>
	static void RefreshFromSlice(
		std::unique_ptr<GroupThumbs> &instance,
		const Slice &slice,
		int index,
		int availableWidth);
	template <typename Slice>
	void fillItems(const Slice &slice, int from, int index, int till);
	void updateContext(Context context);
	void markCacheStale();
	not_null<Thumb*> validateCacheEntry(Key key);
	std::unique_ptr<Thumb> createThumb(Key key);
	std::unique_ptr<Thumb> createThumb(
		Key key,
		const WebPageCollage &collage,
		int index);
	std::unique_ptr<Thumb> createThumb(Key key, ImagePtr image);

	void update();
	void countUpdatedRect();
	void animateAliveItems(int current);
	void fillDyingItems(const std::vector<not_null<Thumb*>> &old);
	void markAsDying(not_null<Thumb*> thumb);
	void markRestAsDying();
	void animatePreviouslyAlive(const std::vector<not_null<Thumb*>> &old);
	void startDelayedAnimation();

	Context _context;
	bool _waitingForAnimationStart = true;
	Animation _animation;
	std::vector<not_null<Thumb*>> _items;
	std::vector<not_null<Thumb*>> _dying;
	base::flat_map<Key, std::unique_ptr<Thumb>> _cache;
	int _width = 0;
	QRect _updatedRect;

	rpl::event_stream<QRect> _updateRequests;
	rpl::event_stream<Key> _activateStream;
	rpl::lifetime _lifetime;

};

} // namespace View
} // namespace Media
