/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "history/history_item_components.h"
#include "base/weak_ptr.h"

class SharedMediaWithLastSlice;
class UserPhotosSlice;

namespace Media {
namespace View {

class GroupThumbs : public base::has_weak_ptr {
public:
	using Key = base::variant<PhotoId, FullMsgId>;

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

	using Context = base::optional_variant<PeerId, MessageGroupId>;

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
