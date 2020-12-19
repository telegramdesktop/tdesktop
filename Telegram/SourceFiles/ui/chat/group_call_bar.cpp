/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/group_call_bar.h"

#include "ui/chat/message_bar.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/paint/blobs.h"
#include "lang/lang_keys.h"
#include "base/openssl_help.h"
#include "styles/style_chat.h"
#include "styles/style_calls.h"
#include "styles/style_info.h" // st::topBarArrowPadding, like TopBarWidget.
#include "styles/palette.h"

#include <QtGui/QtEvents>

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

struct GroupCallBar::BlobsAnimation {
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

struct GroupCallBar::Userpic {
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

GroupCallBar::GroupCallBar(
	not_null<QWidget*> parent,
	rpl::producer<GroupCallBarContent> content,
	rpl::producer<bool> &&hideBlobs)
: _wrap(parent, object_ptr<RpWidget>(parent))
, _inner(_wrap.entity())
, _join(std::make_unique<RoundButton>(
	_inner.get(),
	tr::lng_group_call_join(),
	st::groupCallTopBarJoin))
, _shadow(std::make_unique<PlainShadow>(_wrap.parentWidget()))
, _randomSpeakingTimer([=] { sendRandomLevels(); }) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	const auto limit = kMaxUserpics;
	const auto single = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
	// + 1 * single for the blobs.
	_maxUserpicsWidth = 2 * single + (limit - 1) * (single - shift);

	_wrap.entity()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(_wrap.entity()).fillRect(clip, st::historyPinnedBg);
	}, lifetime());
	_wrap.setAttribute(Qt::WA_OpaquePaintEvent);

	auto copy = std::move(
		content
	) | rpl::start_spawning(_wrap.lifetime());

	rpl::duplicate(
		copy
	) | rpl::start_with_next([=](GroupCallBarContent &&content) {
		_content = content;
		updateUserpicsFromContent();
		_inner->update();
	}, lifetime());

	std::move(
		copy
	) | rpl::map([=](const GroupCallBarContent &content) {
		return !content.shown;
	}) | rpl::start_with_next_done([=](bool hidden) {
		_shouldBeShown = !hidden;
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		}
	}, [=] {
		_forceHidden = true;
		_wrap.toggle(false, anim::type::normal);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &userpic : _userpics) {
			userpic.cache = QImage();
		}
	}, lifetime());

	_speakingAnimation.init([=](crl::time now) {
		if (const auto &last = _speakingAnimationHideLastTime; (last > 0)
			&& (now - last >= kBlobsEnterDuration)) {
			_speakingAnimation.stop();
		}
		for (auto &userpic : _userpics) {
			if (const auto blobs = userpic.blobsAnimation.get()) {
				blobs->blobs.updateLevel(now - blobs->lastTime);
				blobs->lastTime = now;
			}
		}
		updateUserpics();
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
		for (auto &userpic : _userpics) {
			if (const auto blobs = userpic.blobsAnimation.get()) {
				blobs->blobs.setLevel(0.);
			}
		}
		if (!hide && !_speakingAnimation.animating()) {
			_speakingAnimation.start();
		}
		_skipLevelUpdate = hide;
	}, lifetime());

	setupInner();
}

GroupCallBar::~GroupCallBar() {
}

void GroupCallBar::setupInner() {
	_inner->resize(0, st::historyReplyHeight);
	_inner->paintRequest(
	) | rpl::start_with_next([=](QRect rect) {
		auto p = Painter(_inner);
		paint(p);
	}, _inner->lifetime());

	// Clicks.
	_inner->setCursor(style::cur_pointer);
	_inner->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return (event->type() == QEvent::MouseButtonPress);
	}) | rpl::map([=] {
		return _inner->events(
		) | rpl::filter([=](not_null<QEvent*> event) {
			return (event->type() == QEvent::MouseButtonRelease);
		}) | rpl::take(1) | rpl::filter([=](not_null<QEvent*> event) {
			return _inner->rect().contains(
				static_cast<QMouseEvent*>(event.get())->pos());
		});
	}) | rpl::flatten_latest(
	) | rpl::map([] {
		return rpl::empty_value();
	}) | rpl::start_to_stream(_barClicks, _inner->lifetime());

	rpl::combine(
		_inner->widthValue(),
		_join->widthValue()
	) | rpl::start_with_next([=](int outerWidth, int) {
		// Skip shadow of the bar above.
		const auto top = (st::historyReplyHeight
			- st::lineWidth
			- _join->height()) / 2 + st::lineWidth;
		_join->moveToRight(top, top, outerWidth);
	}, _join->lifetime());

	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
		updateControlsGeometry(rect);
	}, _inner->lifetime());
}

void GroupCallBar::paint(Painter &p) {
	p.fillRect(_inner->rect(), st::historyComposeAreaBg);

	const auto left = st::topBarArrowPadding.right();
	const auto titleTop = st::msgReplyPadding.top();
	const auto textTop = titleTop + st::msgServiceNameFont->height;
	const auto width = _inner->width();
	p.setPen(st::defaultMessageBar.textFg);
	p.setFont(st::defaultMessageBar.title.font);
	p.drawTextLeft(left, titleTop, width, tr::lng_group_call_title(tr::now));
	p.setPen(st::historyStatusFg);
	p.setFont(st::defaultMessageBar.text.font);
	p.drawTextLeft(
		left,
		textTop,
		width,
		(_content.count > 0
			? tr::lng_group_call_members(tr::now, lt_count, _content.count)
			: tr::lng_group_call_no_members(tr::now)));

	// Skip shadow of the bar above.
	paintUserpics(p);
}

void GroupCallBar::paintUserpics(Painter &p) {
	const auto top = (st::historyReplyHeight
		- st::lineWidth
		- st::historyGroupCallUserpicSize) / 2 + st::lineWidth;
	const auto middle = _inner->width()  / 2;
	const auto size = st::historyGroupCallUserpicSize;
	const auto factor = style::DevicePixelRatio();
	const auto &minScale = kUserpicMinScale;
	for (auto &userpic : ranges::view::reverse(_userpics)) {
		const auto shown = userpic.shownAnimation.value(
			userpic.hiding ? 0. : 1.);
		if (shown == 0.) {
			continue;
		}
		validateUserpicCache(userpic);
		p.setOpacity(shown);
		const auto left = middle + userpic.leftAnimation.value(userpic.left);
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

			const auto shift = QPointF(left + size / 2., top + size / 2.);
			p.translate(shift);
			blobs->blobs.paint(p, st::windowActiveTextFg);
			p.translate(-shift);
			p.setOpacity(1.);
		}
		if (std::abs(scale - 1.) < 0.001) {
			const auto skip = ((kWideScale - 1) / 2) * size * factor;
			p.drawImage(
				QRect(left, top, size, size),
				userpic.cache,
				QRect(skip, skip, size * factor, size * factor));
		} else {
			auto hq = PainterHighQualityEnabler(p);

			auto target = QRect(
				left + (1 - kWideScale) / 2 * size,
				top + (1 - kWideScale) / 2 * size,
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
	_userpics.erase(ranges::remove_if(_userpics, hidden), end(_userpics));
}

bool GroupCallBar::needUserpicCacheRefresh(Userpic &userpic) {
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

void GroupCallBar::ensureBlobsAnimation(Userpic &userpic) {
	if (userpic.blobsAnimation) {
		return;
	}
	userpic.blobsAnimation = std::make_unique<BlobsAnimation>(
		Blobs() | ranges::to_vector,
		kLevelDuration,
		kMaxLevel);
	userpic.blobsAnimation->lastTime = crl::now();
}

void GroupCallBar::sendRandomLevels() {
	if (_skipLevelUpdate) {
		return;
	}
	for (auto &userpic : _userpics) {
		if (const auto blobs = userpic.blobsAnimation.get()) {
			const auto value = 30 + (openssl::RandomValue<uint32>() % 70);
			userpic.blobsAnimation->blobs.setLevel(float64(value) / 100.);
		}
	}
}

void GroupCallBar::validateUserpicCache(Userpic &userpic) {
	if (!needUserpicCacheRefresh(userpic)) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
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
			pen.setWidth(st::historyGroupCallUserpicStroke);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setBrush(Qt::transparent);
			p.setPen(pen);
			p.drawEllipse(skip - size + shift, skip, size, size);
		}
	}
}

void GroupCallBar::updateControlsGeometry(QRect wrapGeometry) {
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
}

void GroupCallBar::setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void GroupCallBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void GroupCallBar::updateUserpicsFromContent() {
	const auto idFromUserpic = [](const Userpic &userpic) {
		return userpic.data.id;
	};

	// Use "topMost" as "willBeHidden" flag.
	for (auto &userpic : _userpics) {
		userpic.topMost = true;
	}
	for (const auto &user : _content.users) {
		const auto i = ranges::find(_userpics, user.id, idFromUserpic);
		if (i == end(_userpics)) {
			_userpics.push_back(Userpic{ user });
			toggleUserpic(_userpics.back(), true);
			continue;
		}
		i->topMost = false;

		if (i->hiding) {
			toggleUserpic(*i, true);
		}
		i->data = user;

		// Put this one after the last we are not hiding.
		for (auto j = end(_userpics) - 1; j != i; --j) {
			if (!j->topMost) {
				ranges::rotate(i, i + 1, j + 1);
				break;
			}
		}
	}

	// Hide the ones that "willBeHidden" (currently having "topMost" flag).
	// Set correct real values of "topMost" flag.
	const auto userpicsBegin = begin(_userpics);
	const auto userpicsEnd = end(_userpics);
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
			toggleUserpic(userpic, false);
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
	updateUserpicsPositions();

	if (!hasBlobs) {
		_randomSpeakingTimer.cancel();
		_speakingAnimation.stop();
	} else if (!_randomSpeakingTimer.isActive()) {
		_randomSpeakingTimer.callEach(kSendRandomLevelInterval);
		_speakingAnimation.start();
	}

	if (_wrap.isHidden()) {
		for (auto &userpic : _userpics) {
			userpic.shownAnimation.stop();
			userpic.leftAnimation.stop();
		}
	}
}

void GroupCallBar::toggleUserpic(Userpic &userpic, bool shown) {
	userpic.hiding = !shown;
	userpic.shownAnimation.start(
		[=] { updateUserpics(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kDuration);
}

void GroupCallBar::updateUserpicsPositions() {
	const auto shownCount = ranges::count(_userpics, false, &Userpic::hiding);
	if (!shownCount) {
		return;
	}
	const auto single = st::historyGroupCallUserpicSize;
	const auto shift = st::historyGroupCallUserpicShift;
	// + 1 * single for the blobs.
	const auto fullWidth = single + (shownCount - 1) * (single - shift);
	auto left = (-fullWidth / 2);
	for (auto &userpic : _userpics) {
		if (userpic.hiding) {
			continue;
		}
		if (!userpic.positionInited) {
			userpic.positionInited = true;
			userpic.left = left;
		} else if (userpic.left != left) {
			userpic.leftAnimation.start(
				[=] { updateUserpics(); },
				userpic.left,
				left,
				kDuration);
			userpic.left = left;
		}
		left += (single - shift);
	}
}

void GroupCallBar::updateUserpics() {
	const auto widget = _wrap.entity();
	const auto middle = widget->width() / 2;
	_wrap.entity()->update(
		(middle - _maxUserpicsWidth / 2),
		0,
		_maxUserpicsWidth,
		widget->height());
}

void GroupCallBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void GroupCallBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void GroupCallBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void GroupCallBar::finishAnimating() {
	_wrap.finishAnimating();
}

void GroupCallBar::move(int x, int y) {
	_wrap.move(x, y);
}

void GroupCallBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
	_inner->resizeToWidth(width);
}

int GroupCallBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> GroupCallBar::heightValue() const {
	return _wrap.heightValue();
}

rpl::producer<> GroupCallBar::barClicks() const {
	return _barClicks.events();
}

rpl::producer<> GroupCallBar::joinClicks() const {
	return _join->clicks() | rpl::to_empty;
}

} // namespace Ui
