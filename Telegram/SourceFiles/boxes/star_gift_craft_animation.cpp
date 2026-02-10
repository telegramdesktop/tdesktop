/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_craft_animation.h"

#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_cover_box.h"
#include "boxes/star_gift_craft_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/view/media/history_view_sticker_player.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/top_background_gradient.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

using namespace Info::PeerGifts;

constexpr auto kFrameDuration = 1000. / 60.;
constexpr auto kFlightDuration = crl::time(400);
constexpr auto kLoadingFadeInDuration = crl::time(150);
constexpr auto kLoadingFadeOutDuration = crl::time(300);
constexpr auto kLoadingMinDuration = crl::time(600);
constexpr auto kFailureFadeDuration = crl::time(400);
constexpr auto kSuccessFadeInDuration = crl::time(300);
constexpr auto kSuccessExpandDuration = crl::time(400);
constexpr auto kSuccessExpandStart = crl::time(100);
constexpr auto kProgressFadeInDuration = crl::time(300);
constexpr auto kFailureFadeInDuration = crl::time(300);

[[nodiscard]] QString FormatPercent(int permille) {
	const auto rounded = (permille + 5) / 10;
	return QString::number(rounded) + '%';
}

struct Rotation {
	float64 x = 0.;
	float64 y = 0.;

	Rotation operator+(Rotation other) const {
		return { x + other.x, y + other.y };
	}
	Rotation operator*(float64 scale) const {
		return { x * scale, y * scale };
	}
};

using RotationFn = Fn<Rotation(Rotation initial, crl::time t)>;

[[nodiscard]] int Slowing() {
	return anim::SlowMultiplier();
}

[[nodiscard]] RotationFn DecayingRotation(Rotation impulse, float64 decay) {
	const auto lambda = -std::log(decay) / kFrameDuration;
	return [=](Rotation initial, crl::time t) {
		const auto factor = (1. - std::exp(-lambda * t)) / lambda / 1000.;
		return initial + impulse * factor;
	};
}

[[nodiscard]] RotationFn DecayingEndRotation(
		Rotation impulse,
		float64 decay,
		Rotation target,
		crl::time duration) {
	const auto lambda = -std::log(decay) / kFrameDuration;
	target = target * M_PI;
	return [=](Rotation initial, crl::time t) {
		const auto factor = (1. - std::exp(-lambda * t)) / lambda / 1000.;
		const auto result = initial + impulse * factor;
		if (t <= duration - 200) {
			return result;
		} else if (t >= duration) {
			return target;
		}
		const auto progress = (duration - t) / 200.;
		return result * progress + target * (1. - progress);
	};
}

struct GiftAnimationConfig {
	RotationFn rotation;
	crl::time duration = 0;
	crl::time revealTime = 0;
	int targetFaceIndex = 0;
	int targetFaceRotation = 0;
};

const auto kGiftAnimations = std::array<GiftAnimationConfig, 7>{{ {
	.rotation = DecayingEndRotation({ 4.1, -8.2 }, 0.98, { 1, -2 }, 2200),
	.duration = 2200,
	.revealTime = 600,
	.targetFaceIndex = 1,
	.targetFaceRotation = 180,
}, {
	.rotation = DecayingRotation({ 3.2, -5.9 }, 0.97),
	.duration = 1200,
	.revealTime = 0,
	.targetFaceIndex = 4,
	.targetFaceRotation = 180,
}, {
	.rotation = DecayingEndRotation({ 6.2, 7.8 }, 0.98, { 2, 1 }, 2200),
	.duration = 2200,
	.revealTime = 1000,
	.targetFaceIndex = 1,
	.targetFaceRotation = 0,
}, {
	.rotation = DecayingRotation({ 7.0, 4.5 }, 0.97),
	.duration = 1200,
	.revealTime = 200,
	.targetFaceIndex = 5,
	.targetFaceRotation = 0,
}, {
	.rotation = DecayingEndRotation({ -6.5, 5.0 }, 0.98, { 0, 1 }, 2200),
	.duration = 2200,
	.revealTime = 800,
	.targetFaceIndex = 1,
	.targetFaceRotation = 0,
}, {
	.rotation = DecayingRotation({ -2.5, 3.5 }, 0.98),
	.duration = 1200,
	.revealTime = 200,
	.targetFaceIndex = 2,
	.targetFaceRotation = 180,
}, {
	.rotation = DecayingEndRotation({ -4.4, -8.2 }, 0.98, { 0, -1.5 }, 2200),
	.duration = 2200,
	.revealTime = 600,
	.targetFaceIndex = 3,
	.targetFaceRotation = 0,
} }};

[[nodiscard]] QImage CreateBgGradient(
		QSize size,
		const Data::UniqueGiftBackdrop &backdrop) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QRadialGradient(
		QPoint(size.width() / 2, size.height() / 2),
		size.height() / 2);
	gradient.setStops({
		{ 0., backdrop.centerColor },
		{ 1., backdrop.edgeColor },
	});
	p.setBrush(gradient);
	p.setPen(Qt::NoPen);
	p.drawRect(0, 0, size.width(), size.height());
	p.end();

	const auto mask = Images::CornersMask(st::boxRadius);
	return Images::Round(std::move(result), mask);
}

[[nodiscard]] std::optional<std::array<QPointF, 4>> ComputeCubeFaceCorners(
		QPointF center,
		float64 size,
		float64 rotationX,
		float64 rotationY,
		int faceIndex) {
	struct Vec3 {
		float64 x, y, z;

		Vec3 operator+(const Vec3 &o) const {
			return { x + o.x, y + o.y, z + o.z };
		}
		Vec3 operator-(const Vec3 &o) const {
			return { x - o.x, y - o.y, z - o.z };
		}
		Vec3 operator*(float64 s) const {
			return { x * s, y * s, z * s };
		}
		[[nodiscard]] Vec3 cross(const Vec3 &o) const {
			return {
				y * o.z - z * o.y,
				z * o.x - x * o.z,
				x * o.y - y * o.x
			};
		}
		[[nodiscard]] float64 dot(const Vec3 &o) const {
			return x * o.x + y * o.y + z * o.z;
		}
		[[nodiscard]] Vec3 normalized() const {
			const auto len = std::sqrt(x * x + y * y + z * z);
			return (len > 0.) ? Vec3{ x / len, y / len, z / len } : *this;
		}
	};

	const auto rotateY = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
	};
	const auto rotateX = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x, v.y * c - v.z * s, v.y * s + v.z * c };
	};

	const auto half = size / 2.;
	constexpr auto kFocalLength = 800.;

	struct FaceDefinition {
		std::array<Vec3, 4> corners;
		Vec3 normal;
	};

	const auto faces = std::array<FaceDefinition, 6>{{
		{{{{ -half, -half, -half },
		   {  half, -half, -half },
		   {  half,  half, -half },
		   { -half,  half, -half }}},
		 { 0., 0., -1. }},

		{{{{  half, -half,  half },
		   { -half, -half,  half },
		   { -half,  half,  half },
		   {  half,  half,  half }}},
		 { 0., 0., 1. }},

		{{{{ -half, -half,  half },
		   { -half, -half, -half },
		   { -half,  half, -half },
		   { -half,  half,  half }}},
		 { -1., 0., 0. }},

		{{{{  half, -half, -half },
		   {  half, -half,  half },
		   {  half,  half,  half },
		   {  half,  half, -half }}},
		 { 1., 0., 0. }},

		{{{{ -half, -half,  half },
		   {  half, -half,  half },
		   {  half, -half, -half },
		   { -half, -half, -half }}},
		 { 0., -1., 0. }},

		{{{{ -half,  half, -half },
		   {  half,  half, -half },
		   {  half,  half,  half },
		   { -half,  half,  half }}},
		 { 0., 1., 0. }},
	}};

	if (faceIndex < 0 || faceIndex >= 6) {
		return std::nullopt;
	}

	const auto &face = faces[faceIndex];

	const auto transformedNormal = rotateX(
		rotateY(face.normal, rotationY),
		rotationX);
	if (transformedNormal.z >= 0.) {
		return std::nullopt;
	}

	auto result = std::array<QPointF, 4>();
	for (auto i = 0; i != 4; ++i) {
		const auto p = rotateX(
			rotateY(face.corners[i], rotationY),
			rotationX);

		const auto viewZ = p.z + half + kFocalLength;
		if (viewZ <= 0.) {
			return std::nullopt;
		}

		const auto scale = kFocalLength / viewZ;
		result[i] = QPointF(
			center.x() + p.x * scale,
			center.y() + p.y * scale);
	}

	return result;
}

void PaintCubeFace(
		QPainter &p,
		const std::array<QPointF, 4> &corners,
		const QImage &face,
		QSize faceSize = QSize(),
		const QRect &source = QRect(),
		int rotation = 0) {
	if (face.isNull()) {
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto origin = source.isEmpty() ? face.size() : source.size();
	if (faceSize.isEmpty()) {
		faceSize = origin / ratio;
	}
	const auto w = faceSize.width();
	const auto h = faceSize.height();

	const auto srcRect = QPolygonF({
		QPointF(0, 0),
		QPointF(w, 0),
		QPointF(w, h),
		QPointF(0, h)
	});

	const auto rotationSteps = (rotation / 90) % 4;
	auto rotatedCorners = corners;
	for (auto i = 0; i < rotationSteps; ++i) {
		rotatedCorners = {
			rotatedCorners[1],
			rotatedCorners[2],
			rotatedCorners[3],
			rotatedCorners[0],
		};
	}

	const auto dstRect = QPolygonF({
		rotatedCorners[0],
		rotatedCorners[1],
		rotatedCorners[2],
		rotatedCorners[3]
	});

	auto transform = QTransform();
	if (!QTransform::quadToQuad(srcRect, dstRect, transform)) {
		return;
	}

	PainterHighQualityEnabler hq(p);
	p.save();
	p.setTransform(transform, true);
	const auto delta = source.isEmpty()
		? QPointF()
		: QPointF(
			((origin.width() / ratio) - faceSize.width()) / 2.,
			((origin.height() / ratio) - faceSize.height()) / 2.);
	p.drawImage(QRectF(-delta, QSizeF(origin) / ratio), face, source);
	p.restore();
}

[[nodiscard]] std::vector<int> GetVisibleCubeFaces(
		float64 rotationX,
		float64 rotationY) {
	struct Vec3 {
		float64 x, y, z;

		Vec3 operator*(float64 s) const {
			return { x * s, y * s, z * s };
		}
	};

	const auto rotateY = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
	};
	const auto rotateX = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x, v.y * c - v.z * s, v.y * s + v.z * c };
	};

	constexpr auto normals = std::array<Vec3, 6>{{
		{ 0., 0., -1. },
		{ 0., 0., 1. },
		{ -1., 0., 0. },
		{ 1., 0., 0. },
		{ 0., -1., 0. },
		{ 0., 1., 0. },
	}};

	struct FaceDepth {
		int index;
		float64 z;
	};
	auto visible = std::vector<FaceDepth>();
	visible.reserve(3);

	for (auto i = 0; i != 6; ++i) {
		const auto transformed = rotateX(
			rotateY(normals[i], rotationY),
			rotationX);
		if (transformed.z < 0.) {
			visible.push_back({ i, transformed.z });
		}
	}

	ranges::sort(visible, ranges::greater(), &FaceDepth::z);

	auto result = std::vector<int>();
	result.reserve(visible.size());
	for (const auto &face : visible) {
		result.push_back(face.index);
	}
	return result;
}

void PaintCubeFirstFlight(
		QPainter &p,
		not_null<const CraftAnimationState*> state,
		float64 progress,
		bool skipForgeIcon = false) {
	const auto shared = state->shared.get();

	const auto overlayBg = shared->forgeBgOverlay;
	auto sideBg = shared->forgeBg1;
	sideBg.setAlphaF(progress);

	auto hq = PainterHighQualityEnabler(p);
	const auto forge = shared->forgeRect;
	const auto radius = (1. - progress) * st::boxRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(overlayBg);
	p.drawRoundedRect(forge, radius, radius);
	p.setBrush(sideBg);
	p.drawRoundedRect(forge, radius, radius);

	if (!skipForgeIcon) {
		st::craftForge.paintInCenter(p, forge, st::white->c);
	}
}

std::unique_ptr<CraftFailAnimation> SetupFailureAnimation(
		not_null<RpWidget*> canvas,
		not_null<CraftAnimationState*> state) {
	auto result = std::make_unique<CraftFailAnimation>();
	result->lottie = Lottie::MakeIcon({
		.name = u"craft_failed"_q,
		.sizeOverride = st::craftFailLottieSize,
	});
	return result;
}

void FailureAnimationCheck(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas,
		crl::time now) {
	const auto animation = state->failureAnimation.get();
	if (!animation || animation->started) {
		return;
	}
	const auto elapsed = (now - state->animationStartTime);
	const auto left = (state->animationDuration * Slowing()) - elapsed;
	if (left <= 0 || left > kFailureFadeDuration * Slowing()) {
		return;
	}
	animation->started = true;
	auto &covers = state->shared->covers;
	for (auto i = 0; i != 5; ++i) {
		auto &cover = covers[i];
		if (!cover.shown) {
			animation->finalCoverIndex = i;
			if (i != 4) {
				auto &last = covers.back();
				cover.backdrop = last.backdrop;
				cover.pattern = std::move(last.pattern);
				cover.button1 = last.button1;
				cover.button2 = last.button2;
			}
			cover.shown = true;
			cover.shownAnimation.start([=] {
				canvas->update();
			}, 0., 1., left);
			break;
		}
	}

	const auto lottie = animation->lottie.get();
	if (lottie->framesCount() > 1) {
		lottie->animate([=] {
			canvas->update();
		}, 0, lottie->framesCount() - 1);
	}

	state->failureShown = true;
	state->progressShown = false;
}

void FailureAnimationPrepareFrame(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas,
		int faceIndex) {
	if (!state->failureAnimation) {
		state->failureAnimation = SetupFailureAnimation(
			canvas,
			state);
	}
	const auto shared = state->shared.get();
	const auto animation = state->failureAnimation.get();
	if (animation->frame.isNull()) {
		animation->frame = shared->forgeSides[faceIndex].frame;
	}
	const auto lastIndex = animation->finalCoverIndex;
	const auto progress = (lastIndex >= 0)
		? shared->covers[lastIndex].shownAnimation.value(1.)
		: 0.;
	const auto bg = anim::color(
		shared->forgeSides[faceIndex].bg,
		shared->forgeFail,
		progress);
	const auto radius = progress * st::boxRadius;
	const auto rounded = (radius > 0.);
	if (rounded) {
		animation->frame.fill(Qt::transparent);
	}

	auto p = QPainter(&animation->frame);
	const auto size = shared->forgeRect.size();
	const auto rect = QRect(QPoint(), size);
	if (rounded) {
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(bg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(rect, radius, radius);
	} else {
		p.fillRect(rect, bg);
	}
	animation->lottie->paintInCenter(p, rect);
	p.setOpacity(progress);
	p.drawImage(
		st::craftForgePadding,
		st::craftForgePadding,
		state->shared->lostRadial);
}

[[nodiscard]] QRect PrepareCraftFrame(
		not_null<CraftAnimationState*> state,
		int faceIndex,
		crl::time now) {
	Expects(state->successAnimation != nullptr);

	const auto &shared = state->shared.get();
	const auto success = state->successAnimation.get();
	const auto elapsed = now - state->animationStartTime;
	const auto end = state->animationDuration * Slowing();
	const auto left = end - elapsed;
	const auto fadeDuration = kSuccessFadeInDuration * Slowing();
	const auto fadeInProgress = std::clamp(
		(1. - (left / float64(fadeDuration))),
		0.,
		1.);
	const auto expandStart = kSuccessExpandStart * Slowing();
	const auto expanding = kSuccessExpandDuration * Slowing();
	const auto expandProgress = std::clamp(
		(expandStart - left) / float64(expanding),
		0.,
		1.);
	if (expandProgress == 1. && !success->finished) {
		success->finished = true;
	}
	const auto expanded = anim::easeOutCubic(1., expandProgress);
	success->expanded = expanded;
	return success->widget->prepareCraftFrame(success->frame, {
		.initial = shared->forgeRect.size(),
		.initialBg = shared->forgeSides[faceIndex].bg,
		.fadeInProgress = fadeInProgress,
		.expandProgress = expanded,
	});
}

void PaintCube(
		QPainter &p,
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas,
		QPointF center,
		float64 size) {
	const auto shared = state->shared.get();

	const auto now = crl::now();
	FailureAnimationCheck(state, canvas, now);

	const auto success = state->successAnimation.get();
	const auto failure = state->failureAnimation.get();

	const auto finalCoverIndex = failure ? failure->finalCoverIndex : -1;
	const auto finishingFailure = (finalCoverIndex >= 0)
		? shared->covers[finalCoverIndex].shownAnimation.value(1.)
		: 0.;

	if (!state->nextFaceRevealed
		&& state->currentPhaseIndex >= 0) {
		const auto &config = kGiftAnimations[state->currentPhaseIndex];
		const auto elapsed = (crl::now() - state->animationStartTime);
		if (elapsed >= config.revealTime * Slowing()) {
			state->nextFaceRevealed = true;
		}
	}

	const auto faces = GetVisibleCubeFaces(
		state->rotationX,
		state->rotationY);

	auto paintFailure = Fn<void()>();
	for (const auto faceIndex : faces) {
		const auto corners = ComputeCubeFaceCorners(
			center,
			size,
			state->rotationX,
			state->rotationY,
			faceIndex);
		if (!corners) {
			continue;
		}

		const auto thisFaceRevealed = state->nextFaceRevealed
			&& (faceIndex == state->nextFaceIndex);
		auto faceSize = shared->forgeRect.size();
		auto faceImage = shared->forgeSides[faceIndex].frame;
		auto faceSource = QRect();
		auto faceRotation = thisFaceRevealed
			? state->nextFaceRotation
			: 0;

		for (auto i = 0; i < 4; ++i) {
			if (i != state->currentlyFlying
				&& state->giftToSide[i].face == faceIndex
				&& shared->corners[i].giftButton) {
				faceSize = QSize(); // Take from the frame.
				faceImage = shared->corners[i].gift(1.);
				faceRotation = state->giftToSide[i].rotation;
				break;
			}
		}

		if (thisFaceRevealed && state->allGiftsLanded) {
			const auto cover = success ? success->widget : nullptr;
			if (cover) {
				faceSource = PrepareCraftFrame(state, faceIndex, now);
				faceImage = success->frame;
			} else {
				FailureAnimationPrepareFrame(
					state,
					canvas,
					faceIndex);
				faceImage = state->failureAnimation->frame;
			}
		}
		if (finishingFailure > 0.) {
			p.setOpacity((faceIndex != state->nextFaceIndex)
				? (1. - finishingFailure)
				: 1.);
		}
		PaintCubeFace(
			p,
			*corners,
			faceImage,
			faceSize,
			faceSource,
			faceRotation);
	}
	if (finishingFailure > 0.) {
		p.setOpacity(1.);
	}
}

struct GiftFlightPosition {
	QPointF origin;
	float64 scale;
};

[[nodiscard]] GiftFlightPosition ComputeGiftFlightPosition(
		QRect originalRect,
		QPointF targetCenter,
		float64 targetSize,
		float64 progress,
		float64 startOffsetY) {
	const auto eased = progress;

	const auto startOrigin = QPointF(originalRect.topLeft())
		+ QPointF(0, startOffsetY);
	const auto targetOrigin = targetCenter
		- QPointF(targetSize, targetSize) / 2.;
	const auto currentOrigin = startOrigin
		+ (targetOrigin - startOrigin) * eased;

	const auto originalSize = std::max(
		originalRect.width(),
		originalRect.height());
	const auto endScale = (originalSize > 0.)
		? (targetSize / float64(originalSize))
		: 1.;
	const auto currentScale = 1. + (endScale - 1.) * eased;

	return {
		.origin = currentOrigin,
		.scale = currentScale,
	};
}

[[nodiscard]] FacePlacement GetCameraFacingFreeFace(
		not_null<const CraftAnimationState*> state) {
	struct Vec3 {
		float64 x, y, z;
	};

	const auto rotateY = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
	};
	const auto rotateX = [](Vec3 v, float64 angle) {
		const auto c = std::cos(angle);
		const auto s = std::sin(angle);
		return Vec3{ v.x, v.y * c - v.z * s, v.y * s + v.z * c };
	};

	constexpr auto normals = std::array<Vec3, 6>{{
		{ 0., 0., -1. },
		{ 0., 0., 1. },
		{ -1., 0., 0. },
		{ 1., 0., 0. },
		{ 0., -1., 0. },
		{ 0., 1., 0. },
	}};

	constexpr auto faceUpVectors = std::array<Vec3, 6>{{
		{ 0., -1., 0. },
		{ 0., -1., 0. },
		{ 0., -1., 0. },
		{ 0., -1., 0. },
		{ 0., 0., -1. },
		{ 0., 0., 1. },
	}};

	const auto isOccupied = [&](int faceIndex) {
		for (const auto &placement : state->giftToSide) {
			if (placement.face == faceIndex) {
				return true;
			}
		}
		return false;
	};

	auto bestFace = -1;
	auto bestZ = 0.;

	for (auto i = 0; i != 6; ++i) {
		if (isOccupied(i)) {
			continue;
		}
		const auto transformed = rotateX(
			rotateY(normals[i], state->rotationY),
			state->rotationX);
		if (bestFace < 0 || transformed.z < bestZ) {
			bestFace = i;
			bestZ = transformed.z;
		}
	}

	if (bestFace < 0) {
		return {};
	}

	const auto faceUp = rotateX(
		rotateY(faceUpVectors[bestFace], state->rotationY),
		state->rotationX);

	const auto screenUpY = faceUp.y;
	const auto screenUpX = faceUp.x;

	auto rotation = 0;
	if (std::abs(screenUpY) >= std::abs(screenUpX)) {
		rotation = (screenUpY < 0.) ? 0 : 180;
	} else {
		rotation = (screenUpX < 0.) ? 90 : 270;
	}

	return { .face = bestFace, .rotation = rotation };
}

[[nodiscard]] std::array<QPointF, 4> InterpolateQuadCorners(
		const std::array<QPointF, 4> &from,
		const std::array<QPointF, 4> &to,
		float64 progress) {
	return {
		from[0] + (to[0] - from[0]) * progress,
		from[1] + (to[1] - from[1]) * progress,
		from[2] + (to[2] - from[2]) * progress,
		from[3] + (to[3] - from[3]) * progress,
	};
}

[[nodiscard]] std::array<QPointF, 4> RectToCorners(QRectF rect) {
	return {
		rect.topLeft(),
		rect.topRight(),
		rect.bottomRight(),
		rect.bottomLeft(),
	};
}

void PaintFlyingGiftWithQuad(
		QPainter &p,
		const CraftState::CornerSnapshot &corner,
		const std::array<QPointF, 4> &corners,
		float64 progress) {
	if (!corner.giftButton) {
		return;
	}

	const auto frame = corner.gift(progress);
	PaintCubeFace(p, corners, frame);
}

void PaintFlyingGift(
		QPainter &p,
		const CraftState::CornerSnapshot &corner,
		const GiftFlightPosition &position,
		float64 progress) {
	if (!corner.giftButton) {
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto frame = corner.gift(progress);
	const auto giftSize = QSizeF(frame.size()) / ratio;
	const auto scaledSize = giftSize * position.scale;
	const auto giftTopLeft = position.origin;

	auto hq = PainterHighQualityEnabler(p);
	p.drawImage(QRectF(giftTopLeft, scaledSize), frame);
}

void PaintSlideOutCorner(
		QPainter &p,
		const CraftState::CornerSnapshot &corner,
		float64 progress) {
	if (corner.originalRect.isEmpty()) {
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto currentTopLeft = QPointF(corner.originalRect.topLeft());
	if (corner.giftButton) {
		const auto frame = corner.gift(0.);
		const auto giftSize = QSizeF(frame.size()) / ratio;
		const auto giftTopLeft = currentTopLeft;

		const auto &extend = st::defaultDropdownMenu.wrap.shadow.extend;
		const auto innerTopLeft = giftTopLeft
			+ QPointF(extend.left(), extend.top());
		const auto innerWidth = giftSize.width()
			- extend.left()
			- extend.right();

		p.drawImage(giftTopLeft, frame);

		const auto badgeFade = std::clamp(1. - progress / 0.7, 0., 1.);
		if (!corner.percentBadge.isNull() && badgeFade > 0.) {
			const auto badgeSize = QSizeF(corner.percentBadge.size())
				/ ratio;
			const auto badgePos = innerTopLeft
				- QPointF(badgeSize.width() / 3., badgeSize.height() / 3.);
			p.setOpacity(badgeFade);
			p.drawImage(badgePos, corner.percentBadge);
			p.setOpacity(1.);
		}

		if (!corner.removeButton.isNull() && badgeFade > 0.) {
			const auto removeSize = QSizeF(corner.removeButton.size())
				/ ratio;
			const auto removePos = innerTopLeft
				+ QPointF(
					(innerWidth
						- removeSize.width()
						+ removeSize.width() / 3.),
					-removeSize.height() / 3.);
			p.setOpacity(badgeFade);
			p.drawImage(removePos, corner.removeButton);
			p.setOpacity(1.);
		}
	} else if (!corner.addButton.isNull()) {
		const auto fadeProgress = std::clamp(progress * 1.5, 0., 1.);
		const auto addTopLeft = currentTopLeft;
		p.setOpacity(1. - fadeProgress);
		p.drawImage(addTopLeft, corner.addButton);
		p.setOpacity(1.);
	}
}

void PaintSlideOutPhase(
		QPainter &p,
		not_null<CraftState*> shared,
		QSize canvasSize,
		float64 progress) {
	const auto ratio = style::DevicePixelRatio();

	if (!shared->bottomPart.isNull()) {
		const auto slideDistance = QSizeF(shared->bottomPart.size()).height()
			/ ratio;
		const auto offsetY = slideDistance * progress;
		const auto opacity = 1. - progress;

		p.setOpacity(opacity);
		p.drawImage(
			QPointF(0, shared->bottomPartY + offsetY),
			shared->bottomPart);
		p.setOpacity(1.);
	}

	for (const auto &corner : shared->corners) {
		PaintSlideOutCorner(p, corner, progress);
	}

	auto hq = PainterHighQualityEnabler(p);
	const auto forge = shared->forgeRect;
	const auto radius = st::boxRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(shared->forgeBgOverlay);
	p.drawRoundedRect(forge, radius, radius);
	st::craftForge.paintInCenter(p, forge, st::white->c);
	p.setOpacity(1. - progress);
	p.drawImage(
		forge.x() + st::craftForgePadding,
		forge.y() + st::craftForgePadding,
		shared->forgePercent);
}

void PaintFailureThumbnails(
		QPainter &p,
		not_null<const CraftState*> shared,
		QSize canvasSize,
		float64 fadeProgress) {
	if (fadeProgress <= 0. || shared->lostGifts.empty()) {
		return;
	}

	p.setOpacity(fadeProgress);

	const auto width = canvasSize.width();
	const auto giftsCount = int(shared->lostGifts.size());
	const auto &firstCorner = shared->corners[
		shared->lostGifts.front().cornerIndex];
	if (!firstCorner.giftButton) {
		p.setOpacity(1.);
		return;
	}
	const auto thumbSize = firstCorner.giftButton->size();
	const auto &extend = st::defaultDropdownMenu.wrap.shadow.extend;
	const auto thumbSpacing = st::boxRowPadding.left() / 2;
	const auto totalThumbWidth = giftsCount * thumbSize.width()
		+ (giftsCount - 1) * thumbSpacing;
	const auto available = width - extend.left() - extend.right();
	const auto skip = (totalThumbWidth > available)
		? (available - giftsCount * thumbSize.width()) / (giftsCount - 1)
		: thumbSpacing;
	const auto full = giftsCount * thumbSize.width()
		+ (giftsCount - 1) * skip;
	auto x = (width - full) / 2;
	const auto y = shared->forgeRect.bottom() + st::craftFailureThumbsTop;
	const auto rubberOut = st::lineWidth;

	for (const auto &gift : shared->lostGifts) {
		if (gift.cornerIndex < 0) {
			x += thumbSize.width() + skip;
			continue;
		}
		const auto &corner = shared->corners[gift.cornerIndex];
		const auto giftFrame = corner.gift(0.);
		if (!giftFrame.isNull()) {
			const auto targetRect = QRect(QPoint(x, y), thumbSize);
			p.drawImage(targetRect, giftFrame);

			if (!gift.number.isEmpty()) {
				if (gift.badgeCache.isNull()) {
					const auto burnedBg = BurnedBadgeBg();
					gift.badgeCache = ValidateRotatedBadge(GiftBadge{
						.text = gift.number,
						.bg1 = burnedBg,
						.bg2 = burnedBg,
						.fg = st::white->c,
						.small = true,
					}, QMargins());
				}
				const auto inner = targetRect.marginsRemoved(extend);
				p.save();
				p.setClipRect(inner.marginsAdded(
					{ rubberOut, rubberOut, rubberOut, rubberOut }));
				const auto badgeW = gift.badgeCache.width()
					/ gift.badgeCache.devicePixelRatio();
				p.drawImage(
					inner.x() + inner.width() + rubberOut - badgeW,
					inner.y() - rubberOut,
					gift.badgeCache);
				p.restore();
			}
		}
		x += thumbSize.width() + skip;
	}

	p.setOpacity(1.);
}

[[nodiscard]] std::array<QPointF, 4> RotateCornersForFace(
		std::array<QPointF, 4> corners,
		int rotation) {
	const auto steps = (rotation / 90) % 4;
	for (auto i = 0; i < steps; ++i) {
		corners = {
			corners[1],
			corners[2],
			corners[3],
			corners[0],
		};
	}
	return corners;
}

void StartGiftFlight(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas,
		int startIndex) {
	const auto &corners = state->shared->corners;

	auto nextGift = -1;
	for (auto i = startIndex; i < 4; ++i) {
		if (corners[i].giftButton) {
			nextGift = i;
			break;
		}
	}

	if (nextGift < 0) {
		return;
	}

	state->currentlyFlying = nextGift;
	state->giftToSide[nextGift] = GetCameraFacingFreeFace(state);
	state->flightTargetCorners = std::nullopt;

	state->flightAnimation.start(
		[=] { canvas->update(); },
		0.,
		1.,
		kFlightDuration,
		anim::easeInCubic);

	if (!state->continuousAnimation.animating()) {
		state->continuousAnimation.start();
	}
}

void StartGiftFlightToFace(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas,
		int targetFace) {
	const auto shared = state->shared.get();
	const auto &corners = shared->corners;

	auto nextCorner = -1;
	for (auto i = 0; i < 4; ++i) {
		if (corners[i].giftButton && state->giftToSide[i].face < 0) {
			nextCorner = i;
			break;
		}
	}

	if (nextCorner < 0) {
		return;
	}

	state->currentlyFlying = nextCorner;

	const auto faceRotation = state->nextFaceRotation;
	state->giftToSide[nextCorner] = FacePlacement{
		.face = targetFace,
		.rotation = faceRotation,
	};

	if (state->currentPhaseIndex >= 0) {
		const auto &config = kGiftAnimations[state->currentPhaseIndex];
		const auto initial = Rotation{
			state->initialRotationX,
			state->initialRotationY,
		};
		const auto endRotation = config.rotation(initial, config.duration);

		const auto cubeSize = float64(shared->forgeRect.width());
		const auto cubeCenter = QPointF(shared->forgeRect.topLeft())
			+ QPointF(cubeSize, cubeSize) / 2.;

		const auto targetCorners = ComputeCubeFaceCorners(
			cubeCenter,
			cubeSize,
			endRotation.x,
			endRotation.y,
			targetFace);
		if (targetCorners) {
			state->flightTargetCorners = RotateCornersForFace(
				*targetCorners,
				faceRotation);
		} else {
			state->flightTargetCorners = std::nullopt;
		}
	} else {
		state->flightTargetCorners = std::nullopt;
	}

	state->flightAnimation.start([=] {
		canvas->update();
	}, 0., 1., kFlightDuration, anim::easeInCubic);

	if (!state->continuousAnimation.animating()) {
		state->continuousAnimation.start();
	}
}

void LandCurrentGift(not_null<CraftAnimationState*> state, crl::time now) {
	if (state->currentlyFlying < 0) {
		return;
	}

	const auto giftNumber = ++state->giftsLanded;
	const auto isLastGift
		= state->allGiftsLanded
		= (giftNumber >= state->totalGifts);

	const auto configIndex = (giftNumber - 1) * 2 + (isLastGift ? 0 : 1);
	Assert(configIndex < 7);

	const auto &config = kGiftAnimations[configIndex];

	state->currentlyFlying = -1;

	state->initialRotationX = state->rotationX;
	state->initialRotationY = state->rotationY;
	state->currentPhaseIndex = configIndex;
	state->currentPhaseFinished = false;
	state->animationStartTime = now;
	state->animationDuration = config.duration;
	state->nextFaceIndex = config.targetFaceIndex;
	state->nextFaceRotation = config.targetFaceRotation;
	state->nextFaceRevealed = false;
}

void PaintLoadingAnimation(
		QPainter &p,
		not_null<CraftAnimationState*> state) {
	const auto &loading = state->loadingAnimation;
	if (!loading || !state->loadingStartedTime) {
		return;
	}

	const auto loadingShown = state->loadingShownAnimation.value(
		state->loadingFadingOut ? 0. : 1.);
	if (loadingShown <= 0.) {
		return;
	}

	const auto shared = state->shared.get();
	const auto forge = shared->forgeRect;
	const auto inner = forge.marginsRemoved({
		st::craftForgePadding,
		st::craftForgePadding,
		st::craftForgePadding,
		st::craftForgePadding,
	});

	const auto radial = loading->computeState();
	const auto adjustedState = RadialState{
		.shown = radial.shown * loadingShown,
		.arcFrom = radial.arcFrom,
		.arcLength = radial.arcLength,
	};

	auto pen = QPen(st::white->c);
	pen.setCapStyle(Qt::RoundCap);
	InfiniteRadialAnimation::Draw(
		p,
		adjustedState,
		inner.topLeft(),
		inner.size(),
		inner.width(),
		pen,
		st::craftForgeLoading.thickness);
}

} // namespace

QImage CraftState::CornerSnapshot::gift(float64 progress) const {
	if (!giftButton) {
		return QImage();
	} else if (progress == 1. && giftFrameFinal) {
		return giftFrame;
	} else if (progress < 1.) {
		giftFrameFinal = false;
	}
	if (giftButton->makeCraftFrameIsFinal(giftFrame, progress)) {
		giftFrameFinal = true;
	}
	return giftFrame;
}

void CraftState::paint(
		QPainter &p,
		QSize size,
		int craftingHeight,
		float64 slideProgress) {
	const auto width = size.width();
	const auto getBackdrop = [&](BackdropView &backdrop) {
		const auto ratio = style::DevicePixelRatio();
		const auto gradientSize = size;
		auto &gradient = backdrop.gradient;
		if (gradient.size() != gradientSize * ratio) {
			gradient = CreateBgGradient(gradientSize, backdrop.colors);
		}
		return gradient;
	};
	auto patternOffsetY = 0.;
	const auto paintPattern = [&](
			QPainter &q,
			PatternView &pattern,
			const BackdropView &backdrop,
			float64 shown) {
		const auto color = backdrop.colors.patternColor;
		const auto key = (color.red() << 16)
			| (color.green() << 8)
			| color.blue();
		if (patternOffsetY != 0.) {
			q.translate(0, patternOffsetY);
		}
		PaintBgPoints(
			q,
			PatternBgPoints(),
			pattern.emojis[key],
			pattern.emoji.get(),
			color,
			QRect(0, 0, width, st::boxTitleHeight + craftingHeight),
			shown);
		if (patternOffsetY != 0.) {
			q.translate(0, -patternOffsetY);
		}
	};
	auto animating = false;
	auto newEdgeColor = std::optional<QColor>();
	auto newButton1 = QColor();
	auto newButton2 = QColor();
	for (auto i = 0; i != 5; ++i) {
		auto &cover = covers[i];
		if (cover.shownAnimation.animating()) {
			animating = true;
		}
		const auto finalValue = cover.shown ? 1. : 0.;
		const auto shown = cover.shownAnimation.value(finalValue);
		if (shown <= 0.) {
			break;
		} else if (shown == 1.) {
			const auto next = i + 1;
			if (next < 4
				&& covers[next].shown
				&& !covers[next].shownAnimation.animating()) {
				continue;
			}
		}
		p.setOpacity(shown);
		p.drawImage(0, 0, getBackdrop(cover.backdrop));
		paintPattern(p, cover.pattern, cover.backdrop, 1.);
		if (coversAnimate) {
			const auto edge = cover.backdrop.colors.edgeColor;
			if (!newEdgeColor) {
				newEdgeColor = edge;
				newButton1 = cover.button1;
				newButton2 = cover.button2;
			} else {
				newEdgeColor = anim::color(*newEdgeColor, edge, shown);
				newButton1 = anim::color(newButton1, cover.button1, shown);
				newButton2 = anim::color(newButton2, cover.button2, shown);
			}
		}
	}
	if (newEdgeColor) {
		edgeColor = *newEdgeColor;
		button1 = newButton1;
		button2 = newButton2;
	}
	if (animating) {
		p.setOpacity(1.);
	} else {
		coversAnimate = false;
	}
}

void CraftState::updateForGiftCount(int count, Fn<void()> repaint) {
	for (auto i = 5; i != 0;) {
		auto &cover = covers[--i];
		const auto shown = (i < count);
		if (cover.shown != shown) {
			cover.shown = shown;
			const auto from = shown ? 0. : 1.;
			const auto to = shown ? 1. : 0.;
			cover.shownAnimation.start(
				repaint,
				from,
				to,
				st::fadeWrapDuration);
			coversAnimate = true;
		}
	}
}

CraftState::EmptySide CraftState::prepareEmptySide(int index) const {
	const auto size = forgeRect.size();
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);

	const auto bg = anim::color(forgeBg1, forgeBg2, index / 5.);
	result.fill(bg);

	auto p = QPainter(&result);
	st::craftForge.paintInCenter(p, QRect(QPoint(), size), st::white->c);

//#if _DEBUG
//	p.setFont(st::semiboldFont);
//	p.setPen(st::white);
//	p.drawText(
//		size.width() / 10,
//		size.width() / 10 + st::semiboldFont->ascent,
//		QString::number(index));
//#endif // _DEBUG

	p.end();

	return { .bg = bg, .frame = result };
}

void SetupCraftProgressTitle(
		not_null<VerticalLayout*> container,
		not_null<rpl::variable<float64>*> opacity) {
	const auto row = container->add(
		object_ptr<RpWidget>(container),
		st::boxRowPadding + st::craftProgressTitleMargin,
		style::al_top);

	const auto label = CreateChild<FlatLabel>(
		row,
		tr::lng_gift_craft_progress(),
		st::uniqueGiftTitle);
	label->setTextColorOverride(st::white->c);

	const auto iconSize = st::craftProgressIconSize;
	const auto iconWidget = CreateChild<RpWidget>(row);
	iconWidget->resize(iconSize);

	auto owned = Lottie::MakeIcon({
		.name = u"craft_progress"_q,
		.sizeOverride = iconSize,
		.limitFps = true,
	});
	const auto icon = owned.get();
	iconWidget->lifetime().add([kept = std::move(owned)] {});

	const auto startAnimation = [=] {
		icon->animate(
			[=] { iconWidget->update(); },
			0,
			icon->framesCount() - 1);
	};
	startAnimation();

	iconWidget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(iconWidget);
		p.setOpacity(opacity->current());
		icon->paint(p, 0, 0);
		if (!icon->animating() && icon->frameIndex() > 0) {
			startAnimation();
		}
	}, iconWidget->lifetime());

	rpl::combine(
		label->sizeValue(),
		row->sizeValue()
	) | rpl::on_next([=](QSize labelSize, QSize rowSize) {
		const auto skip = st::craftProgressIconSkip;
		const auto totalWidth = iconSize.width() + skip + labelSize.width();
		const auto rowHeight = std::max(iconSize.height(), labelSize.height());
		if (rowSize.height() != rowHeight) {
			row->resize(rowSize.width(), rowHeight);
			return;
		}
		const auto left = (rowSize.width() - totalWidth) / 2;
		iconWidget->move(left, (rowHeight - iconSize.height()) / 2);
		label->move(
			left + iconSize.width() + skip,
			(rowHeight - labelSize.height()) / 2);
	}, row->lifetime());

	opacity->value(
	) | rpl::on_next([=](float64 value) {
		label->setOpacity(value);
		iconWidget->update();
	}, row->lifetime());

}

void SetupProgressControls(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas) {
	const auto add = [&](not_null<FlatLabel*> label) {
		state->progressOpacity.value() | rpl::on_next([=](float64 opacity) {
			label->setOpacity(opacity);
		}, label->lifetime());
	};

	const auto controls = CreateChild<VerticalLayout>(canvas);
	controls->resizeToWidth(canvas->width());
	controls->move(0, state->shared->craftingBottom);

	SetupCraftProgressTitle(controls, &state->progressOpacity);

	const auto subColor = QColor(255, 255, 255, 178);
	const auto about = controls->add(
		object_ptr<FlatLabel>(
			controls,
			tr::lng_gift_craft_progress_about(
				lt_gift,
				rpl::single(tr::marked(state->shared->giftName)),
				tr::rich),
			st::uniqueGiftSubtitle),
		st::boxRowPadding + st::craftProgressAboutMargin,
		style::al_top);
	add(about);
	about->setTextColorOverride(subColor);

	const auto warning = controls->add(
		object_ptr<FlatLabel>(
			controls,
			tr::lng_gift_craft_progress_fails(),
			st::craftAbout),
		st::boxRowPadding + st::craftProgressWarningMargin,
		style::al_top);
	add(warning);
	warning->setTextColorOverride(subColor);

	const auto chance = controls->add(
		object_ptr<FlatLabel>(
			controls,
			tr::lng_gift_craft_progress_chance(
				tr::now,
				lt_percent,
				FormatPercent(state->shared->successPermille)),
			st::craftChance),
		st::boxRowPadding + st::craftProgressChanceMargin,
		style::al_top);
	add(chance);
	chance->setTextColorOverride(st::white->c);
	chance->paintOn([=](QPainter &p) {
		if (const auto opacity = state->progressOpacity.current()) {
			p.setPen(Qt::NoPen);
			p.setOpacity(opacity);
			p.setBrush(state->shared->forgeBgOverlay);
			const auto radius = chance->height() / 2.;
			p.drawRoundedRect(chance->rect(), radius, radius);
		}
	});

	controls->showOn(state->progressOpacity.value(
	) | rpl::map(
		rpl::mappers::_1 > 0.
	) | rpl::distinct_until_changed());
}

void SetupFailureControls(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas) {
	const auto add = [&](not_null<FlatLabel*> label) {
		state->failureOpacity.value() | rpl::on_next([=](float64 opacity) {
			label->setOpacity(opacity);
		}, label->lifetime());
	};

	const auto controls = CreateChild<VerticalLayout>(canvas);
	controls->resizeToWidth(canvas->width());
	controls->move(0, state->shared->craftingBottom);

	const auto title = controls->add(
		object_ptr<FlatLabel>(
			controls,
			tr::lng_gift_craft_failed_title(),
			st::uniqueGiftTitle),
		st::boxRowPadding + st::craftFailureTitleMargin,
		style::al_top);
	add(title);
	title->setTextColorOverride(QColor(0xF8, 0x4A, 0x4A));

	const auto about = controls->add(
		object_ptr<FlatLabel>(
			controls,
			tr::lng_gift_craft_failed_about(
				lt_count,
				rpl::single(state->shared->lostGifts.size() * 1.),
				tr::rich),
			st::uniqueGiftSubtitle),
		st::boxRowPadding + st::craftFailureAboutMargin,
		style::al_top);
	add(about);
	about->setTextColorOverride(QColor(0xFF, 0xBC, 0x9B));

	controls->showOn(state->failureOpacity.value(
	) | rpl::map(
		rpl::mappers::_1 > 0.
	) | rpl::distinct_until_changed());
}

void SetupCraftNewButton(
		not_null<CraftAnimationState*> state,
		not_null<RpWidget*> canvas) {
	const auto button = CreateChild<GradientButton>(
		canvas,
		QGradientStops{
			{ 0., QColor(0xE2, 0x75, 0x19) },
			{ 1., QColor(0xDD, 0x48, 0x19) },
		});
	button->setFullRadius(true);
	button->setClickedCallback([=] {
		state->retryWithNewGift();
	});

	const auto buttonLabel = CreateChild<FlatLabel>(
		button,
		tr::lng_gift_craft_new_button(),
		st::creditsBoxButtonLabel);
	buttonLabel->setTextColorOverride(st::white->c);
	buttonLabel->setAttribute(
		Qt::WA_TransparentForMouseEvents);
	button->sizeValue() | rpl::on_next([=](QSize size) {
		buttonLabel->moveToLeft(
			(size.width() - buttonLabel->width()) / 2,
			st::giftBox.button.textTop);
	}, buttonLabel->lifetime());

	const auto buttonWidth = canvas->width()
		- st::craftBoxButtonPadding.left()
		- st::craftBoxButtonPadding.right();
	const auto buttonHeight = st::giftBox.button.height;
	button->setNaturalWidth(buttonWidth);
	button->setGeometry(
		st::craftBoxButtonPadding.left(),
		state->shared->originalButtonY,
		buttonWidth,
		buttonHeight);
	button->show();

	button->shownValue() | rpl::filter(
		rpl::mappers::_1
	) | rpl::take(1) | rpl::on_next([=] {
		button->startGlareAnimation();
	}, button->lifetime());
}

void StartCraftAnimation(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<CraftState> shared,
		Fn<void(Fn<void(CraftResult)> callback)> startRequest,
		Fn<void(Fn<void()> closeCurrent)> retryWithNewGift) {
	const auto container = box->verticalLayout();
	while (container->count() > 0) {
		delete container->widgetAt(0);
	}

	auto canvas = object_ptr<RpWidget>(container);
	const auto raw = canvas.data();
	const auto state = raw->lifetime().make_state<CraftAnimationState>();

	state->retryWithNewGift = [=] {
		retryWithNewGift(crl::guard(box, [=] { box->closeBox(); }));
	};

	const auto title = CreateChild<FlatLabel>(
		raw,
		tr::lng_gift_craft_title(),
		st::uniqueGiftTitle);
	title->naturalWidthValue() | rpl::on_next([=](int titleWidth) {
		const auto width = st::boxWideWidth;
		title->moveToLeft(
			(width - titleWidth) / 2,
			st::craftTitleMargin.top(),
			width);
	}, title->lifetime());
	title->setTextColorOverride(st::white->c);

	const auto height = shared->containerHeight;
	const auto craftingHeight = shared->craftingBottom - shared->craftingTop;
	state->shared = std::move(shared);
	state->shared->craftingStarted = true;

	const auto craftingSize = QSize(container->width(), height);
	raw->resize(craftingSize);

	for (auto &corner : state->shared->corners) {
		if (corner.giftButton) {
			++state->totalGifts;
		}
	}

	state->loadingAnimation = std::make_unique<InfiniteRadialAnimation>(
		[=] { raw->update(); },
		st::craftForgeLoading);

	SetupProgressControls(state, raw);
	SetupFailureControls(state, raw);

	state->progressShown.value(
	) | rpl::combine_previous(
	) | rpl::on_next([=](bool wasShown, bool nowShown) {
		if (wasShown == nowShown) {
			return;
		}
		const auto from = wasShown ? 1. : 0.;
		const auto to = nowShown ? 1. : 0.;
		state->progressFadeIn.start([=] {
			raw->update();
		}, from, to, kProgressFadeInDuration, anim::easeOutCubic);
	}, raw->lifetime());

	state->failureShown.value(
	) | rpl::combine_previous(
	) | rpl::on_next([=](bool wasShown, bool nowShown) {
		if (wasShown == nowShown) {
			return;
		}
		const auto from = wasShown ? 1. : 0.;
		const auto to = nowShown ? 1. : 0.;
		state->failureFadeIn.start([=] {
			raw->update();
			if (!state->failureFadeIn.animating()) {
				SetupCraftNewButton(state, raw);
			}
		}, from, to, kFailureFadeDuration, anim::easeOutCubic);
	}, raw->lifetime());

	raw->paintOn([=](QPainter &p) {
		const auto shared = state->shared.get();
		const auto success = state->successAnimation.get();
		const auto failure = state->failureAnimation.get();
		const auto slideProgress = state->slideOutAnimation.value(1.);
		if (slideProgress < 1. || (failure && failure->started)) {
			shared->paint(p, craftingSize, craftingHeight, slideProgress);
		} else {
			if (shared->craftBg.isNull()) {
				const auto ratio = style::DevicePixelRatio();
				shared->craftBg = QImage(
					craftingSize * ratio,
					QImage::Format_ARGB32_Premultiplied);
				shared->craftBg.setDevicePixelRatio(ratio);
				shared->craftBg.fill(Qt::transparent);

				auto q = QPainter(&shared->craftBg);
				shared->paint(q, craftingSize, craftingHeight, 1.);
			}
			if (success && success->hiding) {
				p.setOpacity(1. - success->heightAnimation.value(1.));
			}
			p.drawImage(0, 0, shared->craftBg);
			if (success && success->hiding) {
				p.setOpacity(1.);
			}
		}

		if (slideProgress < 1.) {
			PaintSlideOutPhase(p, shared, craftingSize, slideProgress);
		} else {
			const auto craftingOffsetY = success
				? (-success->coverShift * success->expanded)
				: 0.;
			if (success && success->expanded > 0.) {
				const auto width = st::boxWideWidth;
				const auto top = st::craftTitleMargin.top();
				title->moveToLeft(
					(width - title->naturalWidth()) / 2,
					top - (success->expanded * (top + title->height())),
					width);
			}
			const auto cubeSize = float64(shared->forgeRect.width());
			const auto cubeCenter = QPointF(shared->forgeRect.topLeft())
				+ QPointF(cubeSize, cubeSize) / 2.
				+ QPointF(0, craftingOffsetY);

			for (auto i = 0; i < 4; ++i) {
				const auto &corner = shared->corners[i];
				if (corner.giftButton && state->giftToSide[i].face < 0) {
					const auto giftTopLeft = QPointF()
						+ QPointF(corner.originalRect.topLeft())
						+ QPointF(0, craftingOffsetY);
					p.drawImage(giftTopLeft, corner.gift(0));
				}
			}

			const auto flying = state->currentlyFlying;
			const auto firstFlyProgress = (flying == 0)
				? state->flightAnimation.value(1.)
				: state->giftsLanded
				? 1.
				: 0.;
			const auto loadingShown = state->loadingStartedTime
				&& !state->loadingFadingOut;
			if (firstFlyProgress < 1.) {
				const auto skipForgeIcon = loadingShown && anim::Disabled();
				PaintCubeFirstFlight(
					p,
					state,
					firstFlyProgress,
					skipForgeIcon);
			} else {
				PaintCube(p, state, raw, cubeCenter, cubeSize);
			}

			if (flying >= 0) {
				const auto &corner = shared->corners[flying];
				const auto progress = (flying > 0)
					? state->flightAnimation.value(1.)
					: firstFlyProgress;

				if (state->flightTargetCorners) {
					const auto sourceRect = QRectF(corner.originalRect)
						.translated(0, craftingOffsetY);
					const auto sourceCorners = RectToCorners(sourceRect);
					const auto interpolatedCorners = InterpolateQuadCorners(
						sourceCorners,
						*state->flightTargetCorners,
						progress);
					PaintFlyingGiftWithQuad(
						p,
						corner,
						interpolatedCorners,
						progress);
				} else {
					const auto position = ComputeGiftFlightPosition(
						corner.originalRect,
						cubeCenter,
						cubeSize,
						progress,
						craftingOffsetY);
					PaintFlyingGift(p, corner, position, progress);
				}
			}

			PaintLoadingAnimation(p, state);

			if (const auto opacity = state->failureOpacity.current()) {
				PaintFailureThumbnails(
					p,
					shared,
					craftingSize,
					opacity);
			}
		}

		state->progressOpacity = state->progressFadeIn.value(
			state->progressShown.current() ? 1. : 0.);
		state->failureOpacity = state->failureFadeIn.value(
			state->failureShown.current() ? 1. : 0.);
	});

	const auto startFlying = [=] {
		if (state->loadingStartedTime > 0) {
			state->loadingFadingOut = true;
			state->loadingShownAnimation.start(
				[=] { raw->update(); },
				1.,
				0.,
				kLoadingFadeOutDuration);
		}
		StartGiftFlight(state, raw, 0);
	};

	const auto tryStartFlying = [=] {
		const auto slideFinished = !state->slideOutAnimation.animating();
		const auto resultArrived = state->craftResult.has_value();
		const auto notYetFlying = (state->currentlyFlying < 0)
			&& (state->giftsLanded == 0);
		if (!slideFinished || !notYetFlying) {
			return;
		}
		if (!resultArrived) {
			if (!state->loadingStartedTime) {
				state->loadingStartedTime = crl::now();
				state->loadingAnimation->start();
				state->loadingShownAnimation.start(
					[=] { raw->update(); },
					0.,
					1.,
					kLoadingFadeInDuration);
			}
			return;
		}
		if (!state->loadingStartedTime) {
			startFlying();
			return;
		}
		const auto elapsed = crl::now() - state->loadingStartedTime;
		if (elapsed >= kLoadingMinDuration) {
			startFlying();
		} else {
			base::call_delayed(
				kLoadingMinDuration - elapsed,
				raw,
				startFlying);
		}
	};

	state->progressShown = true;

	state->slideOutAnimation.start([=] {
		raw->update();

		const auto progress = state->slideOutAnimation.value(1.);
		if (progress >= 1.) {
			tryStartFlying();
		}
	}, 0., 1., crl::time(300), anim::easeOutCubic);

	state->continuousAnimation.init([=](crl::time now) {
		if (state->currentPhaseIndex >= 0) {
			const auto &config = kGiftAnimations[state->currentPhaseIndex];
			const auto elapsed = state->currentPhaseFinished
				? (config.duration * Slowing())
				: (now - state->animationStartTime);
			const auto initial = Rotation{
				state->initialRotationX,
				state->initialRotationY,
			};
			const auto r = config.rotation(initial, elapsed / Slowing());
			state->rotationX = r.x;
			state->rotationY = r.y;
		}

		raw->update();

		if (state->currentlyFlying >= 0
			&& !state->flightAnimation.animating()) {
			LandCurrentGift(state, now);
		}

		if (!state->flightAnimation.animating()
			&& state->currentlyFlying < 0
			&& state->giftsLanded > 0
			&& state->giftsLanded < state->totalGifts
			&& state->currentPhaseIndex >= 0) {
			const auto elapsed = now - state->animationStartTime;
			const auto start = state->animationDuration - kFlightDuration;
			if (elapsed >= start * Slowing()) {
				StartGiftFlightToFace(state, raw, state->nextFaceIndex);
			}
		}

		const auto success = state->successAnimation.get();
		if (!state->currentPhaseFinished
			&& !state->flightAnimation.animating()
			&& state->currentlyFlying < 0
			&& state->giftsLanded >= state->totalGifts
			&& state->totalGifts > 0
			&& state->currentPhaseIndex >= 0) {
			const auto elapsed = now - state->animationStartTime;
			if (elapsed >= state->animationDuration * Slowing()) {
				state->currentPhaseFinished = true;
				if (success) {
					const auto wasContent = box->height();
					const auto wasTotal = box->parentWidget()->height();
					const auto wasButtons = wasTotal - wasContent;

					container->clear();
					container->add(std::move(success->owned));

					box->setStyle(st::giftBox);

					auto craftAnotherCallback = crl::guard(box, [=] {
						retryWithNewGift([=] { box->closeBox(); });
					});

					const auto entry = EntryForUpgradedGift(
						*state->craftResult,
						0,
						nullptr,
						craftAnotherCallback);
					Settings::GenericCreditsEntryBody(box, show, entry);
					container->resizeToWidth(st::boxWideWidth);

					const auto nowContent = box->height();
					const auto nowTotal = box->parentWidget()->height();
					const auto nowButtons = nowTotal - nowContent;

					box->animateHeightFrom(
						wasContent + wasButtons - nowButtons);
					success->hiding = true;
					const auto duration = kSuccessExpandDuration
						- kSuccessExpandStart;
					success->heightAnimation.start([=] {
						const auto height = anim::interpolate(
							craftingSize.height(),
							success->coverHeight,
							success->heightAnimation.value(1.));
						raw->resize(craftingSize.width(), height);
					}, 0., 1., duration, anim::easeOutCubic);

					raw->setParent(box->parentWidget());
					raw->show();
				}
			}
		}
		if (success && success->finished) {
			state->continuousAnimation.stop();
			raw->hide();

			StartFireworks(box->parentWidget());
		}
		return true;
	});

	container->add(std::move(canvas));

	if (startRequest) {
		const auto weak = base::make_weak(raw);
		startRequest([=](CraftResult result) {
			if (!weak) {
				return;
			} else if (v::is<CraftResultError>(result)) {
				box->uiShow()->show(MakeInformBox({
					.text = v::get<CraftResultError>(result).type,
				}));
				box->closeBox();
				return;
			} else if (v::is<CraftResultWait>(result)) {
				const auto when = base::unixtime::now()
					+ v::get<CraftResultWait>(result).seconds;
				ShowCraftLaterError(box->uiShow(), when);
				box->closeBox();
				return;
			}
			using Result = std::shared_ptr<Data::GiftUpgradeResult>;
			state->craftResult = v::get<Result>(result);
			if (const auto result = state->craftResult->get()) {
				auto numberText = (result->info.unique && result->info.unique->number > 0)
					? rpl::single(u"#"_q + Lang::FormatCountDecimal(result->info.unique->number))
					: rpl::producer<QString>();

				state->successAnimation = std::make_unique<
					CraftDoneAnimation
				>(CraftDoneAnimation{
					.owned = MakeUniqueGiftCover(
						container,
						rpl::single(UniqueGiftCover{ *result->info.unique }),
						{ .numberText = std::move(numberText) }),
				});
				const auto success = state->successAnimation.get();

				const auto raw = success->owned.get();
				success->widget = raw;
				raw->hide();
				raw->resizeToWidth(st::boxWideWidth);
				SendPendingMoveResizeEvents(raw);

				success->coverHeight = raw->height();
				success->coverShift = state->shared->forgeRect.y()
					+ (state->shared->forgeRect.height() / 2)
					- (raw->height() / 2);
			}
			tryStartFlying();
		});
	}
}

} // namespace Ui
