/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_emoji_fingerprint.h"

#include "calls/calls_call.h"
#include "calls/calls_signal_bars.h"
#include "lang/lang_keys.h"
#include "data/data_user.h"
#include "ui/widgets/tooltip.h"
#include "ui/emoji_config.h"
#include "ui/rp_widget.h"
#include "styles/style_calls.h"

namespace Calls {
namespace {

constexpr auto kTooltipShowTimeoutMs = 1000;

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

} // namespace

std::vector<EmojiPtr> ComputeEmojiFingerprint(not_null<Call*> call) {
	auto result = std::vector<EmojiPtr>();
	constexpr auto EmojiCount = (base::array_size(Offsets) - 1);
	for (auto index = 0; index != EmojiCount; ++index) {
		auto offset = Offsets[index];
		auto size = Offsets[index + 1] - offset;
		auto string = QString::fromRawData(
			reinterpret_cast<const QChar*>(Data + offset),
			size);
		auto emoji = Ui::Emoji::Find(string);
		Assert(emoji != nullptr);
	}
	if (call->isKeyShaForFingerprintReady()) {
		auto sha256 = call->getKeyShaForFingerprint();
		constexpr auto kPartSize = 8;
		for (auto partOffset = 0; partOffset != sha256.size(); partOffset += kPartSize) {
			auto value = ComputeEmojiIndex(gsl::make_span(sha256).subspan(partOffset, kPartSize));
			auto index = value % EmojiCount;
			auto offset = Offsets[index];
			auto size = Offsets[index + 1] - offset;
			auto string = QString::fromRawData(
				reinterpret_cast<const QChar*>(Data + offset),
				size);
			auto emoji = Ui::Emoji::Find(string);
			Assert(emoji != nullptr);
			result.push_back(emoji);
		}
	}
	return result;
}

object_ptr<Ui::RpWidget> CreateFingerprintAndSignalBars(
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

	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();

	// Emoji tooltip.
	const auto shower = raw->lifetime().make_state<EmojiTooltipShower>(
		parent->window(),
		tr::lng_call_fingerprint_tooltip(
			tr::now,
			lt_user,
			call->user()->name));
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
	auto size = realSize / cIntRetinaFactor();
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
		fullSize * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	background->setDevicePixelRatio(cRetinaFactor());
	rpl::merge(
		rpl::single(rpl::empty_value()),
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
		const auto size = realSize / cIntRetinaFactor();
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

} // namespace Calls
