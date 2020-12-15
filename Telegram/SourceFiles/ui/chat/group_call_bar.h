/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"
#include "base/timer.h"

class Painter;

namespace Ui {

class PlainShadow;
class RoundButton;

struct GroupCallBarContent {
	struct User {
		QImage userpic;
		std::pair<uint64, uint64> userpicKey = {};
		int32 id = 0;
		bool speaking = false;
	};
	int count = 0;
	bool shown = false;
	std::vector<User> users;
};

class GroupCallBar final {
public:
	GroupCallBar(
		not_null<QWidget*> parent,
		rpl::producer<GroupCallBarContent> content);
	~GroupCallBar();

	void show();
	void hide();
	void raise();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> barClicks() const;
	[[nodiscard]] rpl::producer<> joinClicks() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	using User = GroupCallBarContent::User;
	struct BlobsAnimation;
	struct Userpic;

	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);
	void updateUserpicsFromContent();
	void setupInner();
	void paint(Painter &p);
	void paintUserpics(Painter &p);

	void toggleUserpic(Userpic &userpic, bool shown);
	void updateUserpics();
	void updateUserpicsPositions();
	void validateUserpicCache(Userpic &userpic);
	[[nodiscard]] bool needUserpicCacheRefresh(Userpic &userpic);
	void ensureBlobsAnimation(Userpic &userpic);
	void sendRandomLevels();

	SlideWrap<> _wrap;
	not_null<RpWidget*> _inner;
	std::unique_ptr<RoundButton> _join;
	std::unique_ptr<PlainShadow> _shadow;
	rpl::event_stream<> _barClicks;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	std::vector<Userpic> _userpics;
	base::Timer _randomSpeakingTimer;
	Ui::Animations::Basic _speakingAnimation;
	int _maxUserpicsWidth = 0;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

	GroupCallBarContent _content;

};

} // namespace Ui
