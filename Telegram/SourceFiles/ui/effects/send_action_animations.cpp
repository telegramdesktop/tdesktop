/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/send_action_animations.h"

#include "api/api_send_progress.h"
#include "base/never_freed_pointer.h"
#include "ui/effects/animation_value.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

constexpr int kTypingDotsCount = 3;
constexpr int kRecordArcsCount = 4;
constexpr int kUploadArrowsCount = 3;
constexpr auto kSpeakingDuration = 3200;
constexpr auto kSpeakingFadeDuration = 400;

} // namespace

class SendActionAnimation::Impl {
public:
	using Type = Api::SendProgressType;

	Impl(int period) : _period(period), _started(crl::now()) {
	}

	struct MetaData {
		int index;
		std::unique_ptr<Impl>(*creator)();
	};
	virtual const MetaData *metaData() const = 0;
	bool supports(Type type) const;

	virtual int width() const = 0;
	virtual int widthNoMargins() const {
		return width();
	}
	virtual void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) = 0;

	virtual void restartedAt(crl::time now) {
	}
	virtual bool finishNow() {
		return true;
	}

	virtual ~Impl() = default;

protected:
	[[nodiscard]] int period() const {
		return _period;
	}
	[[nodiscard]] crl::time started() const {
		return _started;
	}
	[[nodiscard]] int frameTime(crl::time now) const {
		return anim::Disabled()
			? 0
			: (std::max(now - _started, crl::time(0)) % _period);
	}

private:
	int _period = 1;
	crl::time _started = 0;

};

namespace {

using ImplementationsMap = QMap<
	Api::SendProgressType,
	const SendActionAnimation::Impl::MetaData*>;
base::NeverFreedPointer<ImplementationsMap> Implementations;

class TypingAnimation : public SendActionAnimation::Impl {
public:
	TypingAnimation() : Impl(st::historySendActionTypingDuration) {
	}

	static const MetaData kMeta;
	static std::unique_ptr<Impl> create() {
		return std::make_unique<TypingAnimation>();
	}
	const MetaData *metaData() const override {
		return &kMeta;
	}

	int width() const override {
		return st::historySendActionTypingPosition.x()
			+ kTypingDotsCount * st::historySendActionTypingDelta;
	}

	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) override;

};

const TypingAnimation::MetaData TypingAnimation::kMeta = {
	0,
	&TypingAnimation::create,
};

void TypingAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	auto frameMs = frameTime(now);
	auto position = QPointF(x + 0.5, y - 0.5)
		+ st::historySendActionTypingPosition;
	for (auto i = 0; i != kTypingDotsCount; ++i) {
		auto r = st::historySendActionTypingSmallNumerator
			/ st::historySendActionTypingDenominator;
		if (frameMs < 2 * st::historySendActionTypingHalfPeriod) {
			const auto delta = (st::historySendActionTypingLargeNumerator
					- st::historySendActionTypingSmallNumerator)
				/ st::historySendActionTypingDenominator;
			if (frameMs < st::historySendActionTypingHalfPeriod) {
				r += delta
					* anim::easeOutCirc(
						1.,
						float64(frameMs)
							/ st::historySendActionTypingHalfPeriod);
			} else {
				r += delta
					* (1. - anim::easeOutCirc(
						1.,
						float64(frameMs
								- st::historySendActionTypingHalfPeriod)
							/ st::historySendActionTypingHalfPeriod));
			}
		}
		p.drawEllipse(position, r, r);
		position.setX(position.x() + st::historySendActionTypingDelta);
		frameMs = (frameMs + period() - st::historySendActionTypingDeltaTime)
			% period();
	}
}

class RecordAnimation : public SendActionAnimation::Impl {
public:
	RecordAnimation() : Impl(st::historySendActionRecordDuration) {
	}

	static const MetaData kMeta;
	static std::unique_ptr<Impl> create() {
		return std::make_unique<RecordAnimation>();
	}
	const MetaData *metaData() const override {
		return &kMeta;
	}

	int width() const override {
		return st::historySendActionRecordPosition.x()
			+ (kRecordArcsCount + 1) * st::historySendActionRecordDelta;
	}

	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) override;

};

const RecordAnimation::MetaData RecordAnimation::kMeta = {
	0,
	&RecordAnimation::create,
};

void RecordAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) {
	PainterHighQualityEnabler hq(p);
	const auto frameMs = frameTime(now);
	auto pen = color->p;
	pen.setWidth(st::historySendActionRecordStrokeNumerator
		/ st::historySendActionRecordDenominator);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto progress = frameMs / float64(period());
	auto size = st::historySendActionRecordPosition.x()
		+ st::historySendActionRecordDelta * progress;
	y += st::historySendActionRecordPosition.y();
	constexpr auto kAngleStart = -arc::kFullLength / 24;
	constexpr auto kAngleSpan = arc::kFullLength / 12;
	for (auto i = 0; i != kRecordArcsCount; ++i) {
		p.setOpacity((i == 0)
			? progress
			: (i == kRecordArcsCount - 1)
			? (1. - progress)
			: 1.);
		auto rect = QRectF(x - size, y - size, 2 * size, 2 * size);
		p.drawArc(rect, kAngleStart, kAngleSpan);
		size += st::historySendActionRecordDelta;
	}
	p.setOpacity(1.);
}

class UploadAnimation : public SendActionAnimation::Impl {
public:
	UploadAnimation() : Impl(st::historySendActionUploadDuration) {
	}

	static const MetaData kMeta;
	static std::unique_ptr<Impl> create() {
		return std::make_unique<UploadAnimation>();
	}
	const MetaData *metaData() const override {
		return &kMeta;
	}

	int width() const override {
		return st::historySendActionUploadPosition.x()
			+ (kUploadArrowsCount + 1) * st::historySendActionUploadDelta;
	}

	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) override;

};

const UploadAnimation::MetaData UploadAnimation::kMeta = {
	0,
	&UploadAnimation::create,
};

void UploadAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) {
	PainterHighQualityEnabler hq(p);
	const auto frameMs = frameTime(now);
	auto pen = color->p;
	pen.setWidth(st::historySendActionUploadStrokeNumerator
		/ st::historySendActionUploadDenominator);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto progress = frameMs / float64(period());
	auto position = st::historySendActionUploadPosition
		+ QPointF(x + st::historySendActionUploadDelta * progress, y);
	auto path = QPainterPath();
	path.moveTo(
		0.,
		-st::historySendActionUploadSizeNumerator
			/ st::historySendActionUploadDenominator);
	path.lineTo(
		st::historySendActionUploadSizeNumerator
			/ st::historySendActionUploadDenominator,
		0.);
	path.lineTo(
		0.,
		st::historySendActionUploadSizeNumerator
			/ st::historySendActionUploadDenominator);
	p.translate(position);
	for (auto i = 0; i != kUploadArrowsCount; ++i) {
		p.setOpacity((i == 0)
			? progress
			: (i == kUploadArrowsCount - 1)
			? (1. - progress)
			: 1.);
		p.drawPath(path);
		position.setX(position.x() + st::historySendActionUploadDelta);
		p.translate(st::historySendActionUploadDelta, 0);
	}
	p.setOpacity(1.);
	p.translate(-position);
}

class SpeakingAnimation : public SendActionAnimation::Impl {
public:
	SpeakingAnimation();

	static const MetaData kMeta;
	static std::unique_ptr<Impl> create() {
		return std::make_unique<SpeakingAnimation>();
	}
	const MetaData *metaData() const override {
		return &kMeta;
	}

	int width() const override {
		const auto &numerator = st::dialogsSpeakingStrokeNumerator;
		const auto &denominator = st::dialogsSpeakingDenominator;
		return 4 * (numerator / denominator);
	}

	void restartedAt(crl::time now) override;
	bool finishNow() override;

	static void PaintIdle(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth);

	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) override;

private:
	static void PaintFrame(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		int frameMs,
		float64 started);

	crl::time _startStarted = 0;
	crl::time _finishStarted = 0;

};

const SpeakingAnimation::MetaData SpeakingAnimation::kMeta = {
	0,
	&SpeakingAnimation::create };

SpeakingAnimation::SpeakingAnimation()
: Impl(kSpeakingDuration)
, _startStarted(crl::now()) {
}

void SpeakingAnimation::restartedAt(crl::time now) {
	if (!_finishStarted) {
		return;
	}
	const auto finishFinishes = _finishStarted + kSpeakingFadeDuration;
	const auto leftToFinish = (finishFinishes - now);
	if (leftToFinish > 0) {
		_startStarted = now - leftToFinish;
	} else {
		_startStarted = now;
	}
	_finishStarted = 0;
}

bool SpeakingAnimation::finishNow() {
	const auto now = crl::now();
	if (_finishStarted) {
		return (_finishStarted + kSpeakingFadeDuration <= now);
	} else if (_startStarted >= now) {
		return true;
	}
	const auto startFinishes = _startStarted + kSpeakingFadeDuration;
	const auto leftToStart = (startFinishes - now);
	if (leftToStart > 0) {
		_finishStarted = now - leftToStart;
	} else {
		_finishStarted = now;
	}
	return false;
}

void SpeakingAnimation::PaintIdle(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth) {
	PaintFrame(p, color, x, y, outerWidth, 0, 0.);
}

void SpeakingAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) {
	const auto started = _finishStarted
		? (1. - ((now - _finishStarted) / float64(kSpeakingFadeDuration)))
		: (now - _startStarted) / float64(kSpeakingFadeDuration);
	const auto progress = std::clamp(started, 0., 1.);
	PaintFrame(p, color, x, y, outerWidth, frameTime(now), progress);
}

void SpeakingAnimation::PaintFrame(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		int frameMs,
		float64 started) {
	PainterHighQualityEnabler hq(p);

	const auto line = st::dialogsSpeakingStrokeNumerator
		/ (2 * st::dialogsSpeakingDenominator);

	p.setPen(Qt::NoPen);
	p.setBrush(color);

	const auto duration = kSpeakingDuration;
	const auto stageDuration = duration / 8;
	const auto fullprogress = frameMs;
	const auto stage = fullprogress / stageDuration;
	const auto progress = (fullprogress - stage * stageDuration)
		/ float64(stageDuration);
	const auto half = st::dialogsCallBadgeSize / 2.;
	const auto center = QPointF(x + half, y + half);
	const auto middleSize = [&] {
		if (!started) {
			return 2 * line;
		}
		auto result = line;
		switch (stage) {
		case 0: result += 4 * line * progress; break;
		case 1: result += 4 * line * (1. - progress); break;
		case 2: result += 2 * line * progress; break;
		case 3: result += 2 * line * (1. - progress); break;
		case 4: result += 4 * line * progress; break;
		case 5: result += 4 * line * (1. - progress); break;
		case 6: result += 4 * line * progress; break;
		case 7: result += 4 * line * (1. - progress); break;
		}
		return (started == 1.)
			? result
			: (started * result) + ((1. - started) * 2 * line);
	}();
	const auto sideSize = [&] {
		if (!started) {
			return 2 * line;
		}
		auto result = line;
		switch (stage) {
		case 0: result += 2 * line * (1. - progress); break;
		case 1: result += 4 * line * progress; break;
		case 2: result += 4 * line * (1. - progress); break;
		case 3: result += 2 * line * progress; break;
		case 4: result += 2 * line * (1. - progress); break;
		case 5: result += 4 * line * progress; break;
		case 6: result += 4 * line * (1. - progress); break;
		case 7: result += 2 * line * progress; break;
		}
		return (started == 1.)
			? result
			: (started * result) + ((1. - started) * 2 * line);
	}();

	const auto drawRoundedRect = [&](float left, float size) {
		const auto top = center.y() - size;
		p.drawRoundedRect(QRectF(left, top, 2 * line, 2 * size), line, line);
	};

	auto left = center.x() - 4 * line;
	drawRoundedRect(left, sideSize);
	left += 3 * line;
	drawRoundedRect(left, middleSize);
	left += 3 * line;
	drawRoundedRect(left, sideSize);
}

class ChooseStickerAnimation : public SendActionAnimation::Impl {
public:
	ChooseStickerAnimation()
	: Impl(st::historySendActionChooseStickerDuration)
	, _eye({
		.outWidth = float64(st::historySendActionChooseStickerEyeWidth),
		.outHeight = float64(st::historySendActionChooseStickerEyeHeight),
		.step = float64(st::historySendActionChooseStickerEyeStep),
		.inLeftOffset = style::ConvertScale(1.5),
		.inRightOffset = -style::ConvertScale(2.5)
			+ st::historySendActionChooseStickerEyeWidth,
		.outXOffset = style::ConvertScale(1.5),
		.outStrokeWidth = style::ConvertScale(0.8 * 1.3),
		.inStrokeWidth = style::ConvertScale(1.2 * 1.3),
		.inSize = style::ConvertScale(2.),
		.minProgress = 0.3,
		.outHeightOffset = 1.5,
	}) {
	}

	static const MetaData kMeta;
	static std::unique_ptr<Impl> create() {
		return std::make_unique<ChooseStickerAnimation>();
	}
	const MetaData *metaData() const override {
		return &kMeta;
	}

	int width() const override {
		return widthNoMargins() + _eye.step * 2;
	}

	int widthNoMargins() const override {
		return st::historySendActionChooseStickerPosition.x()
			+ 2 * (_eye.outWidth + _eye.step)
			+ _eye.step;
	}

	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) override;

private:
	const struct {
		const float64 outWidth;
		const float64 outHeight;
		const float64 step;
		const float64 inLeftOffset;
		const float64 inRightOffset;
		const float64 outXOffset;
		const float64 outStrokeWidth;
		const float64 inStrokeWidth;
		const float64 inSize;
		const float64 minProgress;
		const float64 outHeightOffset;
	} _eye;

};

const ChooseStickerAnimation::MetaData ChooseStickerAnimation::kMeta = {
	0,
	&ChooseStickerAnimation::create,
};

void ChooseStickerAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time now) {
	PainterHighQualityEnabler hq(p);
	const auto frameMs = frameTime(now);
	auto pen = color->p;
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);

	const auto half = float64(period() / 2);
	const auto increment = (frameMs < half) ? true : false;
	// A double-progress within a period half.
	const auto progress = (frameMs / (half / 2)) - (increment ? 0 : 2);

	const auto animationProgress = std::min(progress, 1.);

	const auto k = _eye.minProgress;
	const auto pIn = anim::easeInCirc(1, std::min(animationProgress / k, 1.));
	const auto pInRev = 1. - pIn;
	const auto pOut = anim::easeOutCirc(1., (animationProgress < k)
		? 0.
		: (animationProgress - k) / (1. - k));

	const auto inX = _eye.inLeftOffset * (increment ? pIn : pInRev)
		+ _eye.inRightOffset * (increment ? pInRev : pIn);
	const auto inY = (_eye.outHeight - _eye.inSize) / 2.;

	const auto outLeft = _eye.outXOffset
		* (increment
			? (1. - anim::easeOutCirc(1., progress / 2.))
			: anim::easeOutQuint(1., progress / 2.));

	const auto outScaleOffset = (pIn - pOut) * _eye.outHeightOffset;
	const auto top = st::historySendActionChooseStickerPosition.y() + y;
	const auto left = st::historySendActionChooseStickerPosition.x()
		+ x
		+ outLeft;

	for (auto i = 0; i < 2; i++) {
		const auto currentLeft = left + (_eye.outWidth + _eye.step) * i;

		pen.setWidthF(_eye.outStrokeWidth);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(
			currentLeft,
			top + outScaleOffset,
			_eye.outWidth,
			_eye.outHeight - outScaleOffset));

		pen.setWidthF(_eye.inStrokeWidth);
		p.setPen(pen);
		p.setBrush(color->b);
		p.drawEllipse(QRectF(
			currentLeft + inX,
			top + inY,
			_eye.inSize,
			_eye.inSize));
	}
}

void CreateImplementationsMap() {
	if (Implementations) {
		return;
	}
	using Type = Api::SendProgressType;
	Implementations.createIfNull();
	static constexpr auto kRecordTypes = {
		Type::RecordVideo,
		Type::RecordVoice,
		Type::RecordRound,
	};
	for (const auto type : kRecordTypes) {
		Implementations->insert(type, &RecordAnimation::kMeta);
	}
	static constexpr auto kUploadTypes = {
		Type::UploadFile,
		Type::UploadPhoto,
		Type::UploadVideo,
		Type::UploadVoice,
		Type::UploadRound,
	};
	for (const auto type : kUploadTypes) {
		Implementations->insert(type, &UploadAnimation::kMeta);
	}
	Implementations->insert(Type::Speaking, &SpeakingAnimation::kMeta);
	Implementations->insert(
		Type::ChooseSticker,
		&ChooseStickerAnimation::kMeta);
}

} // namespace

SendActionAnimation::SendActionAnimation() = default;

SendActionAnimation::~SendActionAnimation() = default;

bool SendActionAnimation::Impl::supports(Type type) const {
	CreateImplementationsMap();
	return Implementations->value(type, &TypingAnimation::kMeta)
		== metaData();
}

void SendActionAnimation::start(Type type) {
	if (!_impl || !_impl->supports(type)) {
		_impl = CreateByType(type);
	} else {
		_impl->restartedAt(crl::now());
	}
}

void SendActionAnimation::tryToFinish() {
	if (!_impl) {
		return;
	} else if (_impl->finishNow()) {
		_impl.reset();
	}
}

int SendActionAnimation::width() const {
	return _impl ? _impl->width() : 0;
}

int SendActionAnimation::widthNoMargins() const {
	return _impl ? _impl->widthNoMargins() : 0;
}

void SendActionAnimation::paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time ms) const {
	if (_impl) {
		_impl->paint(p, color, x, y, outerWidth, ms);
	}
}

void SendActionAnimation::PaintSpeakingIdle(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth) {
	SpeakingAnimation::PaintIdle(p, color, x, y, outerWidth);
}

auto SendActionAnimation::CreateByType(Type type) -> std::unique_ptr<Impl> {
	CreateImplementationsMap();
	return Implementations->value(type, &TypingAnimation::kMeta)->creator();
}

} // namespace Ui
