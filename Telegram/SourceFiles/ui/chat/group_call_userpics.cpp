/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/group_call_userpics.h"

#include "ui/paint/blobs.h"
#include "base/openssl_help.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

constexpr auto kDuration = 160;
constexpr auto kMaxUserpics = 4;
constexpr auto kWideScale = 5;

constexpr auto kBlobsEnterDuration = crl::time(250);
constexpr auto kLevelDuration = 100. + 500. * 0.23;
constexpr auto kBlobScale = 0.605;
constexpr auto kMinorBlobFactor = 0.9f;
constexpr auto kUserpicMinScale = 0.8;
constexpr auto kMaxLevel = 1.;
constexpr auto kSendRandomLevelInterval = crl::time(100);

auto Blobs()->std::array<Ui::Paint::Blobs::BlobData, 2> {
	return { {
		{
			.segmentsCount = 6,
			.minScale = kBlobScale * kMinorBlobFactor,
			.minRadius = st::historyGroupCallBlobMinRadius * kMinorBlobFactor,
			.maxRadius = st::historyGroupCallBlobMaxRadius * kMinorBlobFactor,
			.speedScale = 1.,
			.alpha = .5,
		},
		{
			.segmentsCount = 8,
			.minScale = kBlobScale,
			.minRadius = (float)st::historyGroupCallBlobMinRadius,
			.maxRadius = (float)st::historyGroupCallBlobMaxRadius,
			.speedScale = 1.,
			.alpha = .2,
		},
	} };
}

} // namespace

struct GroupCallUserpics::BlobsAnimation {
	BlobsAnimation(
		std::vector<Ui::Paint::Blobs::BlobData> blobDatas,
		float levelDuration,
		float maxLevel)
	: blobs(std::move(blobDatas), levelDuration, maxLevel) {
	}

	Ui::Paint::Blobs blobs;
	crl::time lastTime = 0;
	crl::time lastSpeakingUpdateTime = 0;
	float64 enter = 0.;
};

struct GroupCallUserpics::Userpic {
	User data;
	std::pair<uint64, uint64> cacheKey;
	crl::time speakingStarted = 0;
	QImage cache;
	Animations::Simple leftAnimation;
	Animations::Simple shownAnimation;
	std::unique_ptr<BlobsAnimation> blobsAnimation;
	int left = 0;
	bool positionInited = false;
	bool topMost = false;
	bool hiding = false;
	bool cacheMasked = false;
};

GroupCallUserpics::GroupCallUserpics(
	const style::GroupCallUserpics &st,
	rpl::producer<bool> &&hideBlobs,
	Fn<void()> repaint)
: _st(st)
, _randomSpeakingTimer([=] { sendRandomLevels(); })
, _repaint(std::move(repaint)) {
	const auto limit = kMaxUserpics;
	const auto single = _st.size;
	const auto shift = _st.shift;
	// + 1 * single for the blobs.
	_maxWidth = 2 * single + (limit - 1) * (single - shift);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &userpic : _list) {
			userpic.cache = QImage();
		}
	}, lifetime());

	_speakingAnimation.init([=](crl::time now) {
		if (const auto &last = _speakingAnimationHideLastTime; (last > 0)
			&& (now - last >= kBlobsEnterDuration)) {
			_speakingAnimation.stop();
		}
		for (auto &userpic : _list) {
			if (const auto blobs = userpic.blobsAnimation.get()) {
				blobs->blobs.updateLevel(now - blobs->lastTime);
				blobs->lastTime = now;
			}
		}
		if (const auto onstack = _repaint) {
			onstack();
		}
	});

	rpl::combine(
		rpl::single(anim::Disabled()) | rpl::then(anim::Disables()),
		std::move(hideBlobs)
	) | rpl::start_with_next([=](bool animDisabled, bool deactivated) {
		const auto hide = animDisabled || deactivated;

		if (!(hide && _speakingAnimationHideLastTime)) {
			_speakingAnimationHideLastTime = hide ? crl::now() : 0;
		}
		_skipLevelUpdate = hide;
		for (auto &userpic : _list) {
			if (const auto blobs = userpic.blobsAnimation.get()) {
				blobs->blobs.setLevel(0.);
			}
		}
		if (!hide && !_speakingAnimation.animating()) {
			_speakingAnimation.start();
		}
		_skipLevelUpdate = hide;
	}, lifetime());
}

GroupCallUserpics::~GroupCallUserpics() = default;

void GroupCallUserpics::paint(Painter &p, int x, int y, int size) {
	const auto factor = style::DevicePixelRatio();
	const auto &minScale = kUserpicMinScale;
	for (auto &userpic : ranges::views::reverse(_list)) {
		const auto shown = userpic.shownAnimation.value(
			userpic.hiding ? 0. : 1.);
		if (shown == 0.) {
			continue;
		}
		validateCache(userpic);
		p.setOpacity(shown);
		const auto left = x + userpic.leftAnimation.value(userpic.left);
		const auto blobs = userpic.blobsAnimation.get();
		const auto shownScale = 0.5 + shown / 2.;
		const auto scale = shownScale * (!blobs
			? 1.
			: (minScale
				+ (1. - minScale) * (_speakingAnimationHideLastTime
					? (1. - blobs->blobs.currentLevel())
					: blobs->blobs.currentLevel())));
		if (blobs) {
			auto hq = PainterHighQualityEnabler(p);

			const auto shift = QPointF(left + size / 2., y + size / 2.);
			p.translate(shift);
			blobs->blobs.paint(p, st::windowActiveTextFg);
			p.translate(-shift);
			p.setOpacity(1.);
		}
		if (std::abs(scale - 1.) < 0.001) {
			const auto skip = ((kWideScale - 1) / 2) * size * factor;
			p.drawImage(
				QRect(left, y, size, size),
				userpic.cache,
				QRect(skip, skip, size * factor, size * factor));
		} else {
			auto hq = PainterHighQualityEnabler(p);

			auto target = QRect(
				left + (1 - kWideScale) / 2 * size,
				y + (1 - kWideScale) / 2 * size,
				kWideScale * size,
				kWideScale * size);
			auto shrink = anim::interpolate(
				(1 - kWideScale) / 2 * size,
				0,
				scale);
			auto margins = QMargins(shrink, shrink, shrink, shrink);
			p.drawImage(target.marginsAdded(margins), userpic.cache);
		}
	}
	p.setOpacity(1.);

	const auto hidden = [](const Userpic &userpic) {
		return userpic.hiding && !userpic.shownAnimation.animating();
	};
	_list.erase(ranges::remove_if(_list, hidden), end(_list));
}

int GroupCallUserpics::maxWidth() const {
	return _maxWidth;
}

rpl::producer<int> GroupCallUserpics::widthValue() const {
	return _width.value();
}

bool GroupCallUserpics::needCacheRefresh(Userpic &userpic) {
	if (userpic.cache.isNull()) {
		return true;
	} else if (userpic.hiding) {
		return false;
	} else if (userpic.cacheKey != userpic.data.userpicKey) {
		return true;
	}
	const auto shouldBeMasked = !userpic.topMost;
	if (userpic.cacheMasked == shouldBeMasked || !shouldBeMasked) {
		return true;
	}
	return !userpic.leftAnimation.animating();
}

void GroupCallUserpics::ensureBlobsAnimation(Userpic &userpic) {
	if (userpic.blobsAnimation) {
		return;
	}
	userpic.blobsAnimation = std::make_unique<BlobsAnimation>(
		Blobs() | ranges::to_vector,
		kLevelDuration,
		kMaxLevel);
	userpic.blobsAnimation->lastTime = crl::now();
}

void GroupCallUserpics::sendRandomLevels() {
	if (_skipLevelUpdate) {
		return;
	}
	for (auto &userpic : _list) {
		if (const auto blobs = userpic.blobsAnimation.get()) {
			const auto value = 30 + (openssl::RandomValue<uint32>() % 70);
			userpic.blobsAnimation->blobs.setLevel(float64(value) / 100.);
		}
	}
}

void GroupCallUserpics::validateCache(Userpic &userpic) {
	if (!needCacheRefresh(userpic)) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = _st.size;
	const auto shift = _st.shift;
	const auto full = QSize(size, size) * kWideScale * factor;
	if (userpic.cache.isNull()) {
		userpic.cache = QImage(full, QImage::Format_ARGB32_Premultiplied);
		userpic.cache.setDevicePixelRatio(factor);
	}
	userpic.cacheKey = userpic.data.userpicKey;
	userpic.cacheMasked = !userpic.topMost;
	userpic.cache.fill(Qt::transparent);
	{
		Painter p(&userpic.cache);
		const auto skip = (kWideScale - 1) / 2 * size;
		p.drawImage(skip, skip, userpic.data.userpic);

		if (userpic.cacheMasked) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = QPen(Qt::transparent);
			pen.setWidth(_st.stroke);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setBrush(Qt::transparent);
			p.setPen(pen);
			p.drawEllipse(skip - size + shift, skip, size, size);
		}
	}
}

void GroupCallUserpics::update(
		const std::vector<GroupCallUser> &users,
		bool visible) {
	const auto idFromUserpic = [](const Userpic &userpic) {
		return userpic.data.id;
	};

	// Use "topMost" as "willBeHidden" flag.
	for (auto &userpic : _list) {
		userpic.topMost = true;
	}
	for (const auto &user : users) {
		const auto i = ranges::find(_list, user.id, idFromUserpic);
		if (i == end(_list)) {
			_list.push_back(Userpic{ user });
			toggle(_list.back(), true);
			continue;
		}
		i->topMost = false;

		if (i->hiding) {
			toggle(*i, true);
		}
		i->data = user;

		// Put this one after the last we are not hiding.
		for (auto j = end(_list) - 1; j != i; --j) {
			if (!j->topMost) {
				ranges::rotate(i, i + 1, j + 1);
				break;
			}
		}
	}

	// Hide the ones that "willBeHidden" (currently having "topMost" flag).
	// Set correct real values of "topMost" flag.
	const auto userpicsBegin = begin(_list);
	const auto userpicsEnd = end(_list);
	auto markedTopMost = userpicsEnd;
	auto hasBlobs = false;
	for (auto i = userpicsBegin; i != userpicsEnd; ++i) {
		auto &userpic = *i;
		if (userpic.data.speaking) {
			ensureBlobsAnimation(userpic);
			hasBlobs = true;
		} else {
			userpic.blobsAnimation = nullptr;
		}
		if (userpic.topMost) {
			toggle(userpic, false);
			userpic.topMost = false;
		} else if (markedTopMost == userpicsEnd) {
			userpic.topMost = true;
			markedTopMost = i;
		}
	}
	if (markedTopMost != userpicsEnd && markedTopMost != userpicsBegin) {
		// Bring the topMost userpic to the very beginning, above all hiding.
		std::rotate(userpicsBegin, markedTopMost, markedTopMost + 1);
	}
	updatePositions();

	if (!hasBlobs) {
		_randomSpeakingTimer.cancel();
		_speakingAnimation.stop();
	} else if (!_randomSpeakingTimer.isActive()) {
		_randomSpeakingTimer.callEach(kSendRandomLevelInterval);
		_speakingAnimation.start();
	}

	if (!visible) {
		for (auto &userpic : _list) {
			userpic.shownAnimation.stop();
			userpic.leftAnimation.stop();
		}
	}
	recountAndRepaint();
}

void GroupCallUserpics::toggle(Userpic &userpic, bool shown) {
	userpic.hiding = !shown;
	userpic.shownAnimation.start(
		[=] { recountAndRepaint(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kDuration);
}

void GroupCallUserpics::updatePositions() {
	const auto shownCount = ranges::count(_list, false, &Userpic::hiding);
	if (!shownCount) {
		return;
	}
	const auto single = _st.size;
	const auto shift = _st.shift;
	// + 1 * single for the blobs.
	const auto fullWidth = single + (shownCount - 1) * (single - shift);
	auto left = (_st.align & Qt::AlignLeft)
		? 0
		: (_st.align & Qt::AlignHCenter)
		? (-fullWidth / 2)
		: -fullWidth;
	for (auto &userpic : _list) {
		if (userpic.hiding) {
			continue;
		}
		if (!userpic.positionInited) {
			userpic.positionInited = true;
			userpic.left = left;
		} else if (userpic.left != left) {
			userpic.leftAnimation.start(
				_repaint,
				userpic.left,
				left,
				kDuration);
			userpic.left = left;
		}
		left += (single - shift);
	}
}

void GroupCallUserpics::recountAndRepaint() {
	auto width = 0;
	auto maxShown = 0.;
	for (const auto &userpic : _list) {
		const auto shown = userpic.shownAnimation.value(
			userpic.hiding ? 0. : 1.);
		if (shown > maxShown) {
			maxShown = shown;
		}
		width += anim::interpolate(0, _st.size - _st.shift, shown);
	}
	_width = width + anim::interpolate(0, _st.shift, maxShown);
	if (_repaint) {
		_repaint();
	}
}

} // namespace Ui
