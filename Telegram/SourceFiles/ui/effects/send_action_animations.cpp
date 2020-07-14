/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/send_action_animations.h"

#include "api/api_send_progress.h"
#include "ui/effects/animation_value.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr int kTypingDotsCount = 3;
constexpr int kRecordArcsCount = 4;
constexpr int kUploadArrowsCount = 3;

using ImplementationsMap = QMap<Api::SendProgressType, const SendActionAnimation::Impl::MetaData*>;
NeverFreedPointer<ImplementationsMap> Implementations;

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
		return st::historySendActionTypingPosition.x() + kTypingDotsCount * st::historySendActionTypingDelta;
	}

private:
	void paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) override;

};

const TypingAnimation::MetaData TypingAnimation::kMeta = { 0, &TypingAnimation::create };

void TypingAnimation::paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	auto position = QPointF(x + 0.5, y - 0.5) + st::historySendActionTypingPosition;
	for (auto i = 0; i != kTypingDotsCount; ++i) {
		auto r = st::historySendActionTypingSmallNumerator / st::historySendActionTypingDenominator;
		if (frameMs < 2 * st::historySendActionTypingHalfPeriod) {
			auto delta = (st::historySendActionTypingLargeNumerator - st::historySendActionTypingSmallNumerator) / st::historySendActionTypingDenominator;
			if (frameMs < st::historySendActionTypingHalfPeriod) {
				r += delta * anim::easeOutCirc(1., float64(frameMs) / st::historySendActionTypingHalfPeriod);
			} else {
				r += delta * (1. - anim::easeOutCirc(1., float64(frameMs - st::historySendActionTypingHalfPeriod) / st::historySendActionTypingHalfPeriod));
			}
		}
		p.drawEllipse(position, r, r);
		position.setX(position.x() + st::historySendActionTypingDelta);
		frameMs = (frameMs + st::historySendActionTypingDuration - st::historySendActionTypingDeltaTime) % st::historySendActionTypingDuration;
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
		return st::historySendActionRecordPosition.x() + (kRecordArcsCount + 1) * st::historySendActionRecordDelta;
	}

private:
	void paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) override;

};

const RecordAnimation::MetaData RecordAnimation::kMeta = { 0, &RecordAnimation::create };

void RecordAnimation::paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) {
	PainterHighQualityEnabler hq(p);
	auto pen = color->p;
	pen.setWidth(st::historySendActionRecordStrokeNumerator / st::historySendActionRecordDenominator);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto progress = frameMs / float64(st::historySendActionRecordDuration);
	auto size = st::historySendActionRecordPosition.x() + st::historySendActionRecordDelta * progress;
	y += st::historySendActionRecordPosition.y();
	for (auto i = 0; i != kRecordArcsCount; ++i) {
		p.setOpacity((i == 0) ? progress : (i == kRecordArcsCount - 1) ? (1. - progress) : 1.);
		auto rect = QRectF(x - size, y - size, 2 * size, 2 * size);
		p.drawArc(rect, -FullArcLength / 24, FullArcLength / 12);
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
		return st::historySendActionUploadPosition.x() + (kUploadArrowsCount + 1) * st::historySendActionUploadDelta;
	}

private:
	void paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) override;

};

const UploadAnimation::MetaData UploadAnimation::kMeta = { 0, &UploadAnimation::create };

void UploadAnimation::paintFrame(Painter &p, style::color color, int x, int y, int outerWidth, int frameMs) {
	PainterHighQualityEnabler hq(p);
	auto pen = color->p;
	pen.setWidth(st::historySendActionUploadStrokeNumerator / st::historySendActionUploadDenominator);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	auto progress = frameMs / float64(st::historySendActionUploadDuration);
	auto position = QPointF(x + st::historySendActionUploadDelta * progress, y) + st::historySendActionUploadPosition;
	auto path = QPainterPath();
	path.moveTo(0., -st::historySendActionUploadSizeNumerator / st::historySendActionUploadDenominator);
	path.lineTo(st::historySendActionUploadSizeNumerator / st::historySendActionUploadDenominator, 0.);
	path.lineTo(0., st::historySendActionUploadSizeNumerator / st::historySendActionUploadDenominator);
	p.translate(position);
	for (auto i = 0; i != kUploadArrowsCount; ++i) {
		p.setOpacity((i == 0) ? progress : (i == kUploadArrowsCount - 1) ? (1. - progress) : 1.);
		p.drawPath(path);
		position.setX(position.x() + st::historySendActionUploadDelta);
		p.translate(st::historySendActionUploadDelta, 0);
	}
	p.setOpacity(1.);
	p.translate(-position);
}

void CreateImplementationsMap() {
	if (Implementations) {
		return;
	}
	using Type = Api::SendProgressType;
	Implementations.createIfNull();
	Type recordTypes[] = {
		Type::RecordVideo,
		Type::RecordVoice,
		Type::RecordRound,
	};
	for_const (auto type, recordTypes) {
		Implementations->insert(type, &RecordAnimation::kMeta);
	}
	Type uploadTypes[] = {
		Type::UploadFile,
		Type::UploadPhoto,
		Type::UploadVideo,
		Type::UploadVoice,
		Type::UploadRound,
	};
	for_const (auto type, uploadTypes) {
		Implementations->insert(type, &UploadAnimation::kMeta);
	}
}

} // namespace

bool SendActionAnimation::Impl::supports(Type type) const {
	CreateImplementationsMap();
	return Implementations->value(type, &TypingAnimation::kMeta) == metaData();
}

void SendActionAnimation::Impl::paint(
		Painter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time ms) {
	paintFrame(
		p,
		color,
		x,
		y,
		outerWidth,
		anim::Disabled() ? 0 : (qMax(ms - _started, crl::time(0)) % _period));
}


void SendActionAnimation::start(Type type) {
	if (!_impl || !_impl->supports(type)) {
		_impl = createByType(type);
	}
}

void SendActionAnimation::stop() {
	_impl.reset();
}

std::unique_ptr<SendActionAnimation::Impl> SendActionAnimation::createByType(Type type) {
	CreateImplementationsMap();
	return Implementations->value(type, &TypingAnimation::kMeta)->creator();
}

SendActionAnimation::~SendActionAnimation() = default;

} // namespace Ui
