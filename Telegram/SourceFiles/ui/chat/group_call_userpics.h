/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace style {
struct GroupCallUserpics;
} // namespace style

namespace Ui {

struct GroupCallUser {
	QImage userpic;
	std::pair<uint64, uint64> userpicKey = {};
	int32 id = 0;
	bool speaking = false;
};

class GroupCallUserpics final {
public:
	GroupCallUserpics(
		const style::GroupCallUserpics &st,
		rpl::producer<bool> &&hideBlobs,
		Fn<void()> repaint);
	~GroupCallUserpics();

	void update(
		const std::vector<GroupCallUser> &users,
		bool visible);
	void paint(Painter &p, int x, int y, int size);

	[[nodiscard]] int maxWidth() const;
	[[nodiscard]] rpl::producer<int> widthValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	using User = GroupCallUser;
	struct BlobsAnimation;
	struct Userpic;

	void toggle(Userpic &userpic, bool shown);
	void updatePositions();
	void validateCache(Userpic &userpic);
	[[nodiscard]] bool needCacheRefresh(Userpic &userpic);
	void ensureBlobsAnimation(Userpic &userpic);
	void sendRandomLevels();
	void recountAndRepaint();

	const style::GroupCallUserpics &_st;
	std::vector<Userpic> _list;
	base::Timer _randomSpeakingTimer;
	Fn<void()> _repaint;
	Ui::Animations::Basic _speakingAnimation;
	int _maxWidth = 0;
	bool _skipLevelUpdate = false;
	crl::time _speakingAnimationHideLastTime = 0;

	rpl::variable<int> _width;

	rpl::lifetime _lifetime;

};

} // namespace Ui
