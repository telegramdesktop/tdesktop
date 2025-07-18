/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_emoji_fingerprint.h"

#include "base/random.h"
#include "calls/calls_call.h"
#include "calls/calls_signal_bars.h"
#include "lang/lang_keys.h"
#include "data/data_user.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/abstract_button.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_calls.h"

namespace Calls {
namespace {

constexpr auto kTooltipShowTimeoutMs = crl::time(1000);
constexpr auto kCarouselOneDuration = crl::time(100);
constexpr auto kStartTimeShift = crl::time(50);
constexpr auto kEmojiInFingerprint = 4;
constexpr auto kEmojiInCarousel = 10;

const ushort Data[] = {
0xd83d, 0xde09, 0xd83d, 0xde0d, 0xd83d, 0xde1b, 0xd83d, 0xde2d, 0xd83d, 0xde31, 0xd83d, 0xde21,
0xd83d, 0xde0e, 0xd83d, 0xde34, 0xd83d, 0xde35, 0xd83d, 0xde08, 0xd83d, 0xde2c, 0xd83d, 0xde07,
0xd83d, 0xde0f, 0xd83d, 0xdc6e, 0xd83d, 0xdc77, 0xd83d, 0xdc82, 0xd83d, 0xdc76, 0xd83d, 0xdc68,
0xd83d, 0xdc69, 0xd83d, 0xdc74, 0xd83d, 0xdc75, 0xd83d, 0xde3b, 0xd83d, 0xde3d, 0xd83d, 0xde40,
0xd83d, 0xdc7a, 0xd83d, 0xde48, 0xd83d, 0xde49, 0xd83d, 0xde4a, 0xd83d, 0xdc80, 0xd83d, 0xdc7d,
0xd83d, 0xdca9, 0xd83d, 0xdd25, 0xd83d, 0xdca5, 0xd83d, 0xdca4, 0xd83d, 0xdc42, 0xd83d, 0xdc40,
0xd83d, 0xdc43, 0xd83d, 0xdc45, 0xd83d, 0xdc44, 0xd83d, 0xdc4d, 0xd83d, 0xdc4e, 0xd83d, 0xdc4c,
0xd83d, 0xdc4a, 0x270c, 0x270b, 0xd83d, 0xdc50, 0xd83d, 0xdc46, 0xd83d, 0xdc47, 0xd83d, 0xdc49,
0xd83d, 0xdc48, 0xd83d, 0xde4f, 0xd83d, 0xdc4f, 0xd83d, 0xdcaa, 0xd83d, 0xdeb6, 0xd83c, 0xdfc3,
0xd83d, 0xdc83, 0xd83d, 0xdc6b, 0xd83d, 0xdc6a, 0xd83d, 0xdc6c, 0xd83d, 0xdc6d, 0xd83d, 0xdc85,
0xd83c, 0xdfa9, 0xd83d, 0xdc51, 0xd83d, 0xdc52, 0xd83d, 0xdc5f, 0xd83d, 0xdc5e, 0xd83d, 0xdc60,
0xd83d, 0xdc55, 0xd83d, 0xdc57, 0xd83d, 0xdc56, 0xd83d, 0xdc59, 0xd83d, 0xdc5c, 0xd83d, 0xdc53,
0xd83c, 0xdf80, 0xd83d, 0xdc84, 0xd83d, 0xdc9b, 0xd83d, 0xdc99, 0xd83d, 0xdc9c, 0xd83d, 0xdc9a,
0xd83d, 0xdc8d, 0xd83d, 0xdc8e, 0xd83d, 0xdc36, 0xd83d, 0xdc3a, 0xd83d, 0xdc31, 0xd83d, 0xdc2d,
0xd83d, 0xdc39, 0xd83d, 0xdc30, 0xd83d, 0xdc38, 0xd83d, 0xdc2f, 0xd83d, 0xdc28, 0xd83d, 0xdc3b,
0xd83d, 0xdc37, 0xd83d, 0xdc2e, 0xd83d, 0xdc17, 0xd83d, 0xdc34, 0xd83d, 0xdc11, 0xd83d, 0xdc18,
0xd83d, 0xdc3c, 0xd83d, 0xdc27, 0xd83d, 0xdc25, 0xd83d, 0xdc14, 0xd83d, 0xdc0d, 0xd83d, 0xdc22,
0xd83d, 0xdc1b, 0xd83d, 0xdc1d, 0xd83d, 0xdc1c, 0xd83d, 0xdc1e, 0xd83d, 0xdc0c, 0xd83d, 0xdc19,
0xd83d, 0xdc1a, 0xd83d, 0xdc1f, 0xd83d, 0xdc2c, 0xd83d, 0xdc0b, 0xd83d, 0xdc10, 0xd83d, 0xdc0a,
0xd83d, 0xdc2b, 0xd83c, 0xdf40, 0xd83c, 0xdf39, 0xd83c, 0xdf3b, 0xd83c, 0xdf41, 0xd83c, 0xdf3e,
0xd83c, 0xdf44, 0xd83c, 0xdf35, 0xd83c, 0xdf34, 0xd83c, 0xdf33, 0xd83c, 0xdf1e, 0xd83c, 0xdf1a,
0xd83c, 0xdf19, 0xd83c, 0xdf0e, 0xd83c, 0xdf0b, 0x26a1, 0x2614, 0x2744, 0x26c4, 0xd83c, 0xdf00,
0xd83c, 0xdf08, 0xd83c, 0xdf0a, 0xd83c, 0xdf93, 0xd83c, 0xdf86, 0xd83c, 0xdf83, 0xd83d, 0xdc7b,
0xd83c, 0xdf85, 0xd83c, 0xdf84, 0xd83c, 0xdf81, 0xd83c, 0xdf88, 0xd83d, 0xdd2e, 0xd83c, 0xdfa5,
0xd83d, 0xdcf7, 0xd83d, 0xdcbf, 0xd83d, 0xdcbb, 0x260e, 0xd83d, 0xdce1, 0xd83d, 0xdcfa, 0xd83d,
0xdcfb, 0xd83d, 0xdd09, 0xd83d, 0xdd14, 0x23f3, 0x23f0, 0x231a, 0xd83d, 0xdd12, 0xd83d, 0xdd11,
0xd83d, 0xdd0e, 0xd83d, 0xdca1, 0xd83d, 0xdd26, 0xd83d, 0xdd0c, 0xd83d, 0xdd0b, 0xd83d, 0xdebf,
0xd83d, 0xdebd, 0xd83d, 0xdd27, 0xd83d, 0xdd28, 0xd83d, 0xdeaa, 0xd83d, 0xdeac, 0xd83d, 0xdca3,
0xd83d, 0xdd2b, 0xd83d, 0xdd2a, 0xd83d, 0xdc8a, 0xd83d, 0xdc89, 0xd83d, 0xdcb0, 0xd83d, 0xdcb5,
0xd83d, 0xdcb3, 0x2709, 0xd83d, 0xdceb, 0xd83d, 0xdce6, 0xd83d, 0xdcc5, 0xd83d, 0xdcc1, 0x2702,
0xd83d, 0xdccc, 0xd83d, 0xdcce, 0x2712, 0x270f, 0xd83d, 0xdcd0, 0xd83d, 0xdcda, 0xd83d, 0xdd2c,
0xd83d, 0xdd2d, 0xd83c, 0xdfa8, 0xd83c, 0xdfac, 0xd83c, 0xdfa4, 0xd83c, 0xdfa7, 0xd83c, 0xdfb5,
0xd83c, 0xdfb9, 0xd83c, 0xdfbb, 0xd83c, 0xdfba, 0xd83c, 0xdfb8, 0xd83d, 0xdc7e, 0xd83c, 0xdfae,
0xd83c, 0xdccf, 0xd83c, 0xdfb2, 0xd83c, 0xdfaf, 0xd83c, 0xdfc8, 0xd83c, 0xdfc0, 0x26bd, 0x26be,
0xd83c, 0xdfbe, 0xd83c, 0xdfb1, 0xd83c, 0xdfc9, 0xd83c, 0xdfb3, 0xd83c, 0xdfc1, 0xd83c, 0xdfc7,
0xd83c, 0xdfc6, 0xd83c, 0xdfca, 0xd83c, 0xdfc4, 0x2615, 0xd83c, 0xdf7c, 0xd83c, 0xdf7a, 0xd83c,
0xdf77, 0xd83c, 0xdf74, 0xd83c, 0xdf55, 0xd83c, 0xdf54, 0xd83c, 0xdf5f, 0xd83c, 0xdf57, 0xd83c,
0xdf71, 0xd83c, 0xdf5a, 0xd83c, 0xdf5c, 0xd83c, 0xdf61, 0xd83c, 0xdf73, 0xd83c, 0xdf5e, 0xd83c,
0xdf69, 0xd83c, 0xdf66, 0xd83c, 0xdf82, 0xd83c, 0xdf70, 0xd83c, 0xdf6a, 0xd83c, 0xdf6b, 0xd83c,
0xdf6d, 0xd83c, 0xdf6f, 0xd83c, 0xdf4e, 0xd83c, 0xdf4f, 0xd83c, 0xdf4a, 0xd83c, 0xdf4b, 0xd83c,
0xdf52, 0xd83c, 0xdf47, 0xd83c, 0xdf49, 0xd83c, 0xdf53, 0xd83c, 0xdf51, 0xd83c, 0xdf4c, 0xd83c,
0xdf50, 0xd83c, 0xdf4d, 0xd83c, 0xdf46, 0xd83c, 0xdf45, 0xd83c, 0xdf3d, 0xd83c, 0xdfe1, 0xd83c,
0xdfe5, 0xd83c, 0xdfe6, 0x26ea, 0xd83c, 0xdff0, 0x26fa, 0xd83c, 0xdfed, 0xd83d, 0xddfb, 0xd83d,
0xddfd, 0xd83c, 0xdfa0, 0xd83c, 0xdfa1, 0x26f2, 0xd83c, 0xdfa2, 0xd83d, 0xdea2, 0xd83d, 0xdea4,
0x2693, 0xd83d, 0xde80, 0x2708, 0xd83d, 0xde81, 0xd83d, 0xde82, 0xd83d, 0xde8b, 0xd83d, 0xde8e,
0xd83d, 0xde8c, 0xd83d, 0xde99, 0xd83d, 0xde97, 0xd83d, 0xde95, 0xd83d, 0xde9b, 0xd83d, 0xdea8,
0xd83d, 0xde94, 0xd83d, 0xde92, 0xd83d, 0xde91, 0xd83d, 0xdeb2, 0xd83d, 0xdea0, 0xd83d, 0xde9c,
0xd83d, 0xdea6, 0x26a0, 0xd83d, 0xdea7, 0x26fd, 0xd83c, 0xdfb0, 0xd83d, 0xddff, 0xd83c, 0xdfaa,
0xd83c, 0xdfad, 0xd83c, 0xddef, 0xd83c, 0xddf5, 0xd83c, 0xddf0, 0xd83c, 0xddf7, 0xd83c, 0xdde9,
0xd83c, 0xddea, 0xd83c, 0xdde8, 0xd83c, 0xddf3, 0xd83c, 0xddfa, 0xd83c, 0xddf8, 0xd83c, 0xddeb,
0xd83c, 0xddf7, 0xd83c, 0xddea, 0xd83c, 0xddf8, 0xd83c, 0xddee, 0xd83c, 0xddf9, 0xd83c, 0xddf7,
0xd83c, 0xddfa, 0xd83c, 0xddec, 0xd83c, 0xdde7, 0x0031, 0x20e3, 0x0032, 0x20e3, 0x0033, 0x20e3,
0x0034, 0x20e3, 0x0035, 0x20e3, 0x0036, 0x20e3, 0x0037, 0x20e3, 0x0038, 0x20e3, 0x0039, 0x20e3,
0x0030, 0x20e3, 0xd83d, 0xdd1f, 0x2757, 0x2753, 0x2665, 0x2666, 0xd83d, 0xdcaf, 0xd83d, 0xdd17,
0xd83d, 0xdd31, 0xd83d, 0xdd34, 0xd83d, 0xdd35, 0xd83d, 0xdd36, 0xd83d, 0xdd37 };

const ushort Offsets[] = {
0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22,
24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46,
48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70,
72, 74, 76, 78, 80, 82, 84, 86, 87, 88, 90, 92,
94, 96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116,
118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140,
142, 144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164,
166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188,
190, 192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212,
214, 216, 218, 220, 222, 224, 226, 228, 230, 232, 234, 236,
238, 240, 242, 244, 246, 248, 250, 252, 254, 256, 258, 259,
260, 261, 262, 264, 266, 268, 270, 272, 274, 276, 278, 280,
282, 284, 286, 288, 290, 292, 294, 295, 297, 299, 301, 303,
305, 306, 307, 308, 310, 312, 314, 316, 318, 320, 322, 324,
326, 328, 330, 332, 334, 336, 338, 340, 342, 344, 346, 348,
350, 351, 353, 355, 357, 359, 360, 362, 364, 365, 366, 368,
370, 372, 374, 376, 378, 380, 382, 384, 386, 388, 390, 392,
394, 396, 398, 400, 402, 404, 406, 407, 408, 410, 412, 414,
416, 418, 420, 422, 424, 426, 427, 429, 431, 433, 435, 437,
439, 441, 443, 445, 447, 449, 451, 453, 455, 457, 459, 461,
463, 465, 467, 469, 471, 473, 475, 477, 479, 481, 483, 485,
487, 489, 491, 493, 495, 497, 499, 501, 503, 505, 507, 508,
510, 511, 513, 515, 517, 519, 521, 522, 524, 526, 528, 529,
531, 532, 534, 536, 538, 540, 542, 544, 546, 548, 550, 552,
554, 556, 558, 560, 562, 564, 566, 567, 569, 570, 572, 574,
576, 578, 582, 586, 590, 594, 598, 602, 606, 610, 614, 618,
620, 622, 624, 626, 628, 630, 632, 634, 636, 638, 640, 641,
642, 643, 644, 646, 648, 650, 652, 654, 656, 658 };

constexpr auto kEmojiCount = (base::array_size(Offsets) - 1);

uint64 ComputeEmojiIndex(bytes::const_span bytes) {
	Expects(bytes.size() == 8);

	return ((gsl::to_integer<uint64>(bytes[0]) & 0x7F) << 56)
		| (gsl::to_integer<uint64>(bytes[1]) << 48)
		| (gsl::to_integer<uint64>(bytes[2]) << 40)
		| (gsl::to_integer<uint64>(bytes[3]) << 32)
		| (gsl::to_integer<uint64>(bytes[4]) << 24)
		| (gsl::to_integer<uint64>(bytes[5]) << 16)
		| (gsl::to_integer<uint64>(bytes[6]) << 8)
		| (gsl::to_integer<uint64>(bytes[7]));
}

[[nodiscard]] EmojiPtr EmojiByIndex(int index) {
	Expects(index >= 0 && index < kEmojiCount);

	const auto offset = Offsets[index];
	const auto size = Offsets[index + 1] - offset;
	const auto string = QString::fromRawData(
		reinterpret_cast<const QChar*>(Data + offset),
		size);
	return Ui::Emoji::Find(string);
}

} // namespace

std::vector<EmojiPtr> ComputeEmojiFingerprint(not_null<Call*> call) {
	if (!call->isKeyShaForFingerprintReady()) {
		return {};
	}
	return ComputeEmojiFingerprint(call->getKeyShaForFingerprint());
}

std::vector<EmojiPtr> ComputeEmojiFingerprint(
		bytes::const_span fingerprint) {
	auto result = std::vector<EmojiPtr>();
	constexpr auto kPartSize = 8;
	for (auto partOffset = 0
		; partOffset != fingerprint.size()
		; partOffset += kPartSize) {
		const auto value = ComputeEmojiIndex(
			fingerprint.subspan(partOffset, kPartSize));
		result.push_back(EmojiByIndex(value % kEmojiCount));
	}
	return result;
}

base::unique_qptr<Ui::RpWidget> CreateFingerprintAndSignalBars(
		not_null<QWidget*> parent,
		not_null<Call*> call) {
	class EmojiTooltipShower final : public Ui::AbstractTooltipShower {
	public:
		EmojiTooltipShower(not_null<QWidget*> window, const QString &text)
		: _window(window)
		, _text(text) {
		}

		QString tooltipText() const override {
			return _text;
		}
		QPoint tooltipPos() const override {
			return QCursor::pos();
		}
		bool tooltipWindowActive() const override {
			return _window->isActiveWindow();
		}

	private:
		const not_null<QWidget*> _window;
		const QString _text;

	};

	auto result = base::make_unique_q<Ui::RpWidget>(parent);
	const auto raw = result.get();

	// Emoji tooltip.
	const auto shower = raw->lifetime().make_state<EmojiTooltipShower>(
		parent->window(),
		tr::lng_call_fingerprint_tooltip(
			tr::now,
			lt_user,
			call->user()->name()));
	raw->setMouseTracking(true);
	raw->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseMove) {
			Ui::Tooltip::Show(kTooltipShowTimeoutMs, shower);
		} else if (e->type() == QEvent::Leave) {
			Ui::Tooltip::Hide();
		}
	}, raw->lifetime());

	// Signal bars.
	const auto bars = Ui::CreateChild<SignalBars>(
		raw,
		call,
		st::callPanelSignalBars);
	bars->setAttribute(Qt::WA_TransparentForMouseEvents);

	// Geometry.
	const auto print = ComputeEmojiFingerprint(call);
	auto realSize = Ui::Emoji::GetSizeNormal();
	auto size = realSize / style::DevicePixelRatio();
	auto count = print.size();
	const auto printSize = QSize(
		count * size + (count - 1) * st::callFingerprintSkip,
		size);
	const auto fullPrintSize = QRect(
		QPoint(),
		printSize
	).marginsAdded(st::callFingerprintPadding).size();
	const auto fullBarsSize = bars->rect().marginsAdded(
		st::callSignalBarsPadding
	).size();
	const auto fullSize = QSize(
		(fullPrintSize.width()
			+ st::callFingerprintSignalBarsSkip
			+ fullBarsSize.width()),
		fullPrintSize.height());
	raw->resize(fullSize);
	bars->moveToRight(
		st::callSignalBarsPadding.right(),
		st::callSignalBarsPadding.top());

	// Paint.
	const auto background = raw->lifetime().make_state<QImage>(
		fullSize * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	background->setDevicePixelRatio(style::DevicePixelRatio());
	rpl::merge(
		rpl::single(rpl::empty),
		Ui::Emoji::Updated(),
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		background->fill(Qt::transparent);

		// Prepare.
		auto p = QPainter(background);
		const auto height = fullSize.height();
		const auto fullPrintRect = QRect(QPoint(), fullPrintSize);
		const auto fullBarsRect = QRect(
			fullSize.width() - fullBarsSize.width(),
			0,
			fullBarsSize.width(),
			height);
		const auto bigRadius = height / 2;
		const auto smallRadius = st::roundRadiusSmall;
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::callBgButton);

		// Fingerprint part.
		p.setClipRect(0, 0, fullPrintSize.width() / 2, height);
		p.drawRoundedRect(fullPrintRect, bigRadius, bigRadius);
		p.setClipRect(fullPrintSize.width() / 2, 0, fullSize.width(), height);
		p.drawRoundedRect(fullPrintRect, smallRadius, smallRadius);

		// Signal bars part.
		const auto middle = fullBarsRect.center().x();
		p.setClipRect(0, 0, middle, height);
		p.drawRoundedRect(fullBarsRect, smallRadius, smallRadius);
		p.setClipRect(middle, 0, fullBarsRect.width(), height);
		p.drawRoundedRect(fullBarsRect, bigRadius, bigRadius);

		// Emoji.
		const auto realSize = Ui::Emoji::GetSizeNormal();
		const auto size = realSize / style::DevicePixelRatio();
		auto left = st::callFingerprintPadding.left();
		const auto top = st::callFingerprintPadding.top();
		p.setClipping(false);
		for (const auto emoji : print) {
			Ui::Emoji::Draw(p, emoji, realSize, left, top);
			left += st::callFingerprintSkip + size;
		}

		raw->update();
	}, raw->lifetime());

	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).drawImage(raw->rect(), *background);
	}, raw->lifetime());

	raw->show();
	return result;
}

FingerprintBadge SetupFingerprintBadge(
		rpl::lifetime &on,
		rpl::producer<QByteArray> fingerprint) {
	struct State {
		FingerprintBadgeState data;
		Ui::Animations::Basic animation;
		Fn<void(crl::time)> update;
		rpl::event_stream<> repaints;
	};
	const auto state = on.make_state<State>();

	state->data.speed = 1. / kCarouselOneDuration;
	state->update = [=](crl::time now) {
		// speed-up-duration = 2 * one / speed.
		const auto one = 1.;
		const auto speedUpDuration = 2 * kCarouselOneDuration;
		const auto speed0 = one / kCarouselOneDuration;

		auto updated = false;
		auto animating = false;
		for (auto &entry : state->data.entries) {
			if (!entry.time) {
				continue;
			}
			animating = true;
			if (entry.time >= now) {
				continue;
			}

			updated = true;
			const auto elapsed = (now - entry.time) * 1.;
			entry.time = now;

			Assert(!entry.emoji || entry.sliding.size() > 1);
			const auto slideCount = entry.emoji
				? (int(entry.sliding.size()) - 1) * one
				: (kEmojiInCarousel + (elapsed / kCarouselOneDuration));
			const auto finalPosition = slideCount * one;
			const auto distance = finalPosition - entry.position;

			const auto accelerate0 = speed0 - entry.speed;
			const auto decelerate0 = speed0;
			const auto acceleration0 = speed0 / speedUpDuration;
			const auto taccelerate0 = accelerate0 / acceleration0;
			const auto tdecelerate0 = decelerate0 / acceleration0;
			const auto paccelerate0 = entry.speed * taccelerate0
				+ acceleration0 * taccelerate0 * taccelerate0 / 2.;
			const auto pdecelerate0 = 0
				+ acceleration0 * tdecelerate0 * tdecelerate0 / 2.;
			const auto ttozero = entry.speed / acceleration0;
			if (paccelerate0 + pdecelerate0 <= distance) {
				// We have time to accelerate to speed0,
				// maybe go some time on speed0 and then decelerate to 0.
				const auto uaccelerate0 = std::min(taccelerate0, elapsed);
				const auto left = distance - paccelerate0 - pdecelerate0;
				const auto tconstant = left / speed0;
				const auto uconstant = std::min(
					tconstant,
					elapsed - uaccelerate0);
				const auto udecelerate0 = std::min(
					tdecelerate0,
					elapsed - uaccelerate0 - uconstant);
				if (udecelerate0 >= tdecelerate0) {
					Assert(entry.emoji != nullptr);
					entry = { .emoji = entry.emoji };
				} else {
					entry.position += entry.speed * uaccelerate0
						+ acceleration0 * uaccelerate0 * uaccelerate0 / 2.
						+ speed0 * uconstant
						+ speed0 * udecelerate0
						- acceleration0 * udecelerate0 * udecelerate0 / 2.;
					entry.speed += acceleration0
						* (uaccelerate0 - udecelerate0);
				}
			} else if (acceleration0 * ttozero * ttozero / 2 <= distance) {
				// We have time to accelerate at least for some time >= 0,
				// and then decelerate to 0 to make it to final position.
				//
				// peak = entry.speed + acceleration0 * t
				// tdecelerate = peak / acceleration0
				// distance = entry.speed * t
				//     + acceleration0 * t * t / 2
				//     + acceleration0 * tdecelerate * tdecelerate / 2
				const auto det = entry.speed * entry.speed / 2
					+ distance * acceleration0;
				const auto t = std::max(
					(sqrt(det) - entry.speed) / acceleration0,
					0.);

				const auto taccelerate = t;
				const auto uaccelerate = std::min(taccelerate, elapsed);
				const auto tdecelerate = t + (entry.speed / acceleration0);
				const auto udecelerate = std::min(
					tdecelerate,
					elapsed - uaccelerate);
				if (udecelerate >= tdecelerate) {
					Assert(entry.emoji != nullptr);
					entry = { .emoji = entry.emoji };
				} else {
					const auto topspeed = entry.speed
						+ acceleration0 * taccelerate;
					entry.position += entry.speed * uaccelerate
						+ acceleration0 * uaccelerate * uaccelerate / 2.
						+ topspeed * udecelerate
						- acceleration0 * udecelerate * udecelerate / 2.;
					entry.speed += acceleration0
						* (uaccelerate - udecelerate);
				}
			} else {
				// We just need to decelerate to 0,
				// faster than acceleration0.
				Assert(entry.speed > 0);
				const auto tdecelerate = 2 * distance / entry.speed;
				const auto udecelerate = std::min(tdecelerate, elapsed);
				if (udecelerate >= tdecelerate) {
					Assert(entry.emoji != nullptr);
					entry = { .emoji = entry.emoji };
				} else {
					const auto a = entry.speed / tdecelerate;
					entry.position += entry.speed * udecelerate
						- a * udecelerate * udecelerate / 2;
					entry.speed -= a * udecelerate;
				}
			}

			if (entry.position >= kEmojiInCarousel) {
				entry.position -= qFloor(entry.position / kEmojiInCarousel)
					* kEmojiInCarousel;
			}
			while (entry.position >= 1.) {
				Assert(!entry.sliding.empty());
				entry.position -= 1.;
				entry.sliding.erase(begin(entry.sliding));
				if (entry.emoji && entry.sliding.size() < 2) {
					entry = { .emoji = entry.emoji };
					break;
				} else if (entry.sliding.empty()) {
					const auto index = (entry.added++) % kEmojiInCarousel;
					entry.sliding.push_back(entry.carousel[index]);
				}
			}
			if (!entry.emoji
				&& entry.position > 0.
				&& entry.sliding.size() < 2) {
				const auto index = (entry.added++) % kEmojiInCarousel;
				entry.sliding.push_back(entry.carousel[index]);
			}
		}
		if (!animating) {
			state->animation.stop();
		} else if (updated) {
			state->repaints.fire({});
		}
	};
	state->animation.init(state->update);
	state->data.entries.resize(kEmojiInFingerprint);

	const auto fillCarousel = [=](
			int index,
			base::BufferedRandom<uint32> &buffered) {
		auto &entry = state->data.entries[index];
		auto indices = std::vector<int>();
		indices.reserve(kEmojiInCarousel);
		auto count = kEmojiCount;
		for (auto i = 0; i != kEmojiInCarousel; ++i, --count) {
			auto index = base::RandomIndex(count, buffered);
			for (const auto &already : indices) {
				if (index >= already) {
					++index;
				}
			}
			indices.push_back(index);
		}

		entry.carousel.clear();
		entry.carousel.reserve(kEmojiInCarousel);
		for (const auto index : indices) {
			entry.carousel.push_back(EmojiByIndex(index));
		}
	};

	const auto startTo = [=](
			int index,
			EmojiPtr emoji,
			crl::time now,
			base::BufferedRandom<uint32> &buffered) {
		auto &entry = state->data.entries[index];
		if ((entry.emoji == emoji) && (emoji || entry.time)) {
			return;
		} else if (!entry.time) {
			Assert(entry.sliding.empty());

			if (entry.emoji) {
				entry.sliding.push_back(entry.emoji);
			} else if (emoji) {
				// Just initialize if we get emoji right from the start.
				entry.emoji = emoji;
				return;
			}
			entry.time = now + index * kStartTimeShift;

			fillCarousel(index, buffered);
		}
		entry.emoji = emoji;
		if (entry.emoji) {
			entry.sliding.push_back(entry.emoji);
		} else {
			const auto index = (entry.added++) % kEmojiInCarousel;
			entry.sliding.push_back(entry.carousel[index]);
		}
	};

	std::move(
		fingerprint
	) | rpl::start_with_next([=](const QByteArray &fingerprint) {
		auto buffered = base::BufferedRandom<uint32>(
			kEmojiInCarousel * kEmojiInFingerprint);
		const auto now = crl::now();
		const auto emoji = (fingerprint.size() >= 32)
			? ComputeEmojiFingerprint(
				bytes::make_span(fingerprint).subspan(0, 32))
			: std::vector<EmojiPtr>();
		state->update(now);

		if (emoji.size() == kEmojiInFingerprint) {
			for (auto i = 0; i != kEmojiInFingerprint; ++i) {
				startTo(i, emoji[i], now, buffered);
			}
		} else {
			for (auto i = 0; i != kEmojiInFingerprint; ++i) {
				startTo(i, nullptr, now, buffered);
			}
		}
		if (!state->animation.animating()) {
			state->animation.start();
		}
	}, on);

	return { .state = &state->data, .repaints = state->repaints.events() };
}

void SetupFingerprintTooltip(not_null<Ui::RpWidget*> widget) {
	struct State {
		std::unique_ptr<Ui::ImportantTooltip> tooltip;
		Fn<void()> updateGeometry;
		Fn<void(bool)> toggleTooltip;
	};
	const auto state = widget->lifetime().make_state<State>();
	state->updateGeometry = [=] {
		if (!state->tooltip.get()) {
			return;
		}
		const auto geometry = Ui::MapFrom(
			widget->window(),
			widget,
			widget->rect());
		if (geometry.isEmpty()) {
			state->toggleTooltip(false);
			return;
		}
		const auto weak = QPointer<QWidget>(state->tooltip.get());
		const auto countPosition = [=](QSize size) {
			const auto result = geometry.bottomLeft()
				+ QPoint(
					geometry.width() / 2,
					st::confcallFingerprintTooltipSkip)
				- QPoint(size.width() / 2, 0);
			return result;
		};
		state->tooltip.get()->pointAt(
			geometry,
			RectPart::Bottom,
			countPosition);
	};
	state->toggleTooltip = [=](bool show) {
		if (const auto was = state->tooltip.release()) {
			was->toggleAnimated(false);
		}
		if (!show) {
			return;
		}
		const auto text = tr::lng_confcall_e2e_about(
			tr::now,
			Ui::Text::WithEntities);
		if (text.empty()) {
			return;
		}
		state->tooltip = std::make_unique<Ui::ImportantTooltip>(
			widget->window(),
			Ui::MakeNiceTooltipLabel(
				widget,
				rpl::single(text),
				st::confcallFingerprintTooltipMaxWidth,
				st::confcallFingerprintTooltipLabel),
			st::confcallFingerprintTooltip);
		const auto raw = state->tooltip.get();
		const auto weak = base::make_weak(raw);
		const auto destroy = [=] {
			delete weak.get();
		};
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		raw->setHiddenCallback(destroy);
		state->updateGeometry();
		raw->toggleAnimated(true);
	};

	widget->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter) {
			state->toggleTooltip(true);
		} else if (type == QEvent::Leave) {
			state->toggleTooltip(false);
		}
	}, widget->lifetime());
}

QImage MakeVerticalShadow(int height) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(1, height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	auto p = QPainter(&result);
	auto g = QLinearGradient(0, 0, 0, height);
	auto color = st::groupCallMembersBg->c;
	auto trans = color;
	trans.setAlpha(0);
	g.setStops({
		{ 0.0, color },
		{ 0.4, trans },
		{ 0.6, trans },
		{ 1.0, color },
	});
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(0, 0, 1, height, g);
	p.end();

	return result;
}

void SetupFingerprintBadgeWidget(
		not_null<Ui::RpWidget*> widget,
		not_null<const FingerprintBadgeState*> state,
		rpl::producer<> repaints) {
	auto &lifetime = widget->lifetime();

	const auto button = Ui::CreateChild<Ui::RpWidget>(widget);
	button->show();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::confcallFingerprintText);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->show();

	const auto ratio = style::DevicePixelRatio();
	const auto esize = Ui::Emoji::GetSizeNormal();
	const auto size = esize / ratio;
	widget->widthValue() | rpl::start_with_next([=](int width) {
		static_assert(!(kEmojiInFingerprint % 2));

		const auto available = width
			- st::confcallFingerprintMargins.left()
			- st::confcallFingerprintMargins.right()
			- (kEmojiInFingerprint * size)
			- (kEmojiInFingerprint - 2) * st::confcallFingerprintSkip
			- st::confcallFingerprintTextMargins.left()
			- st::confcallFingerprintTextMargins.right();
		if (available <= 0) {
			return;
		}
		label->setText(tr::lng_confcall_e2e_badge(tr::now));
		if (label->textMaxWidth() > available) {
			label->setText(tr::lng_confcall_e2e_badge_small(tr::now));
		}
		const auto use = std::min(available, label->textMaxWidth());
		label->resizeToWidth(use);

		const auto ontheleft = kEmojiInFingerprint / 2;
		const auto ontheside = ontheleft * size
			+ (ontheleft - 1) * st::confcallFingerprintSkip;
		const auto text = QRect(
			(width - use) / 2,
			(st::confcallFingerprintMargins.top()
				+ st::confcallFingerprintTextMargins.top()),
			use,
			label->height());
		const auto textOuter = text.marginsAdded(
			st::confcallFingerprintTextMargins);
		const auto withEmoji = QRect(
			textOuter.x() - ontheside,
			textOuter.y(),
			textOuter.width() + ontheside * 2,
			size);
		const auto outer = withEmoji.marginsAdded(
			st::confcallFingerprintMargins);

		button->setGeometry(outer);
		label->moveToLeft(text.x() - outer.x(), text.y() - outer.y(), width);

		widget->resize(
			width,
			button->height() + st::confcallFingerprintBottomSkip);
	}, lifetime);

	const auto cache = lifetime.make_state<FingerprintBadgeCache>();
	button->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(button);

		const auto outer = button->rect();
		const auto radius = outer.height() / 2.;
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::groupCallMembersBg);
		p.drawRoundedRect(outer, radius, radius);
		p.setClipRect(outer);

		const auto withEmoji = outer.marginsRemoved(
			st::confcallFingerprintMargins);
		p.translate(withEmoji.topLeft());

		const auto text = label->geometry();
		const auto textOuter = text.marginsAdded(
			st::confcallFingerprintTextMargins);
		const auto count = int(state->entries.size());
		cache->entries.resize(count);
		cache->shadow = MakeVerticalShadow(outer.height());
		for (auto i = 0; i != count; ++i) {
			const auto &entry = state->entries[i];
			auto &cached = cache->entries[i];
			const auto shadowed = entry.speed / state->speed;
			PaintFingerprintEntry(p, entry, cached, esize);
			if (shadowed > 0.) {
				p.setOpacity(shadowed);
				p.drawImage(
					QRect(0, -st::confcallFingerprintMargins.top(), size, outer.height()),
					cache->shadow);
				p.setOpacity(1.);
			}
			if (i + 1 == count / 2) {
				p.translate(size + textOuter.width(), 0);
			} else {
				p.translate(size + st::confcallFingerprintSkip, 0);
			}
		}
	}, lifetime);

	std::move(repaints) | rpl::start_with_next([=] {
		button->update();
	}, lifetime);

	SetupFingerprintTooltip(button);
}

void PaintFingerprintEntry(
		QPainter &p,
		const FingerprintBadgeState::Entry &entry,
		FingerprintBadgeCache::Entry &cache,
		int esize) {
	const auto stationary = !entry.time;
	if (stationary) {
		Ui::Emoji::Draw(p, entry.emoji, esize, 0, 0);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto size = esize / ratio;
	const auto add = 4;
	const auto height = size + 2 * add;
	const auto validateCache = [&](int index, EmojiPtr e) {
		if (cache.emoji.size() <= index) {
			cache.emoji.reserve(entry.carousel.size() + 2);
			cache.emoji.resize(index + 1);
		}
		auto &emoji = cache.emoji[index];
		if (emoji.ptr != e) {
			emoji.ptr = e;
			emoji.image = QImage(
				QSize(size, height) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			emoji.image.setDevicePixelRatio(ratio);
			emoji.image.fill(Qt::transparent);
			auto q = QPainter(&emoji.image);
			Ui::Emoji::Draw(q, e, esize, 0, add);
			q.end();

			//emoji.image = Images::Blur(
			//	std::move(emoji.image),
			//	false,
			//	Qt::Vertical);
		}
		return &emoji;
	};
	auto shift = entry.position * height - add;
	p.translate(0, shift);
	for (const auto &e : entry.sliding) {
		const auto index = [&] {
			const auto i = ranges::find(entry.carousel, e);
			if (i != end(entry.carousel)) {
				return int(i - begin(entry.carousel));
			}
			return int(entry.carousel.size())
				+ ((e == entry.sliding.back()) ? 1 : 0);
		}();
		const auto entry = validateCache(index, e);
		p.drawImage(0, 0, entry->image);
		p.translate(0, -height);
		shift -= height;
	}
	p.translate(0, -shift);
}

} // namespace Calls
