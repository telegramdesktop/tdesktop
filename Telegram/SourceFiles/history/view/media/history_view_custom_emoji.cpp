/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_custom_emoji.h"

#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_isolated_emoji.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

using SizeTag = Data::CustomEmojiManager::SizeTag;
using LottieSize = ChatHelpers::StickerLottieSize;
using CustomPtr = std::unique_ptr<Ui::Text::CustomEmoji>;
using StickerPtr = std::unique_ptr<Sticker>;

struct CustomEmojiSizeInfo {
	LottieSize tag = LottieSize::MessageHistory;
	float64 scale = 1.;
};

[[nodiscard]] const base::flat_map<int, CustomEmojiSizeInfo> &SizesInfo() {
	// size = i->second.scale * st::maxAnimatedEmojiSize.
	// CustomEmojiManager::SizeTag caching uses first ::EmojiInteraction-s.
	using Info = CustomEmojiSizeInfo;
	static auto result = base::flat_map<int, Info>{
		{ 1, Info{ LottieSize::EmojiInteractionReserved7, 1. } },
		{ 2, Info{ LottieSize::EmojiInteractionReserved6, 0.7 } },
		{ 3, Info{ LottieSize::EmojiInteractionReserved5, 0.52 } },
	};
	return result;
}

[[nodiscard]] SizeTag EmojiSize(int dimension) {
	return (dimension == 4 || dimension == 5)
		? SizeTag::Isolated
		: (dimension == 6 || dimension == 7)
		? SizeTag::Large
		: SizeTag::Normal;
}

[[nodiscard]] bool IsLargeSizeDimension(int dimension) {
	return (dimension == 5) || (dimension == 6);
}

} //namespace

CustomEmoji::CustomEmoji(
	not_null<Element*> parent,
	const Ui::Text::OnlyCustomEmoji &emoji)
: _parent(parent) {
	Expects(!emoji.lines.empty());

	auto resolving = false;
	const auto owner = &parent->data()->history()->owner();
	const auto manager = &owner->customEmojiManager();
	const auto max = ranges::max_element(
		emoji.lines,
		std::less<>(),
		&std::vector<Ui::Text::OnlyCustomEmoji::Item>::size);
	const auto dimension = int(std::max(emoji.lines.size(), max->size()));
	const auto &sizes = SizesInfo();
	const auto i = sizes.find(dimension);
	const auto useCustomEmoji = (i == end(sizes));
	const auto tag = EmojiSize(dimension);
	_singleSize = !useCustomEmoji
		? int(base::SafeRound(i->second.scale * st::maxAnimatedEmojiSize))
		: Data::FrameSizeFromTag(tag);
	for (const auto &line : emoji.lines) {
		_lines.emplace_back();
		for (const auto &element : line) {
			if (useCustomEmoji) {
				_lines.back().push_back(
					manager->create(
						element.entityData,
						[=] { parent->customEmojiRepaint(); },
						tag));
			} else {
				const auto &data = element.entityData;
				const auto id = Data::ParseCustomEmojiData(data).id;
				const auto document = owner->document(id);
				if (document->sticker()) {
					const auto skipPremiumEffect = false;
					auto sticker = std::make_unique<Sticker>(
						parent,
						document,
						skipPremiumEffect);
					sticker->setCustomEmojiPart(_singleSize, i->second.tag);
					_lines.back().push_back(std::move(sticker));
				} else {
					_lines.back().push_back(element.entityData);
					resolving = true;
				}
			}
		}
	}
	if (resolving) {

	}
}

CustomEmoji::~CustomEmoji() {
	if (_hasHeavyPart) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

QSize CustomEmoji::countOptimalSize() {
	Expects(!_lines.empty());

	const auto max = ranges::max_element(
		_lines,
		std::less<>(),
		&std::vector<LargeCustomEmoji>::size);
	return {
		_singleSize * int(max->size()),
		_singleSize * int(_lines.size()),
	};
}

QSize CustomEmoji::countCurrentSize(int newWidth) {
	const auto perRow = std::max(newWidth / _singleSize, 1);
	auto width = 0;
	auto height = 0;
	for (const auto &line : _lines) {
		const auto count = int(line.size());
		accumulate_max(width, std::min(perRow, count) * _singleSize);
		height += std::max((count + perRow - 1) / perRow, 1) * _singleSize;
	}
	return { width, height };
}

void CustomEmoji::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &r) {
	_parent->clearCustomEmojiRepaint();

	auto x = r.x();
	auto y = r.y();
	const auto perRow = std::max(r.width() / _singleSize, 1);
	const auto paused = _parent->delegate()->elementIsGifPaused();
	for (auto &line : _lines) {
		const auto count = int(line.size());
		const auto rows = std::max((count + perRow - 1) / perRow, 1);
		for (auto row = 0; row != rows; ++row) {
			for (auto column = 0; column != perRow; ++column) {
				const auto index = row * perRow + column;
				if (index >= count) {
					break;
				}
				paintElement(p, x, y, line[index], context, paused);
				x += _singleSize;
			}
			x = r.x();
			y += _singleSize;
		}
	}
}

void CustomEmoji::paintElement(
		Painter &p,
		int x,
		int y,
		LargeCustomEmoji &element,
		const PaintContext &context,
		bool paused) {
	if (const auto sticker = std::get_if<StickerPtr>(&element)) {
		paintSticker(p, x, y, sticker->get(), context, paused);
	} else if (const auto custom = std::get_if<CustomPtr>(&element)) {
		paintCustom(p, x, y, custom->get(), context, paused);
	}
}

void CustomEmoji::paintSticker(
		Painter &p,
		int x,
		int y,
		not_null<Sticker*> sticker,
		const PaintContext &context,
		bool paused) {
	sticker->draw(p, context, { QPoint(x, y), sticker->countOptimalSize() });
}

void CustomEmoji::paintCustom(
		Painter &p,
		int x,
		int y,
		not_null<Ui::Text::CustomEmoji*> emoji,
		const PaintContext &context,
		bool paused) {
	if (!_hasHeavyPart) {
		_hasHeavyPart = true;
		_parent->history()->owner().registerHeavyViewPart(_parent);
	}
	const auto inner = st::largeEmojiSize + 2 * st::largeEmojiOutline;
	const auto outer = Ui::Text::AdjustCustomEmojiSize(inner);
	const auto skip = (inner - outer) / 2;
	const auto preview = context.imageStyle()->msgServiceBg->c;
	if (context.selected()) {
		const auto factor = style::DevicePixelRatio();
		const auto size = QSize(outer, outer) * factor;
		if (_selectedFrame.size() != size) {
			_selectedFrame = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			_selectedFrame.setDevicePixelRatio(factor);
		}
		_selectedFrame.fill(Qt::transparent);
		auto q = QPainter(&_selectedFrame);
		emoji->paint(q, 0, 0, context.now, preview, paused);
		q.end();

		_selectedFrame = Images::Colored(
			std::move(_selectedFrame),
			context.st->msgStickerOverlay()->c);
		p.drawImage(x + skip, y + skip, _selectedFrame);
	} else {
		emoji->paint(p, x + skip, y + skip, context.now, preview, paused);
	}
}

bool CustomEmoji::hasHeavyPart() const {
	return _hasHeavyPart;
}

void CustomEmoji::unloadHeavyPart() {
	if (!_hasHeavyPart) {
		return;
	}
	const auto unload = [&](const LargeCustomEmoji &element) {
		if (const auto sticker = std::get_if<StickerPtr>(&element)) {
			(*sticker)->unloadHeavyPart();
		} else if (const auto custom = std::get_if<CustomPtr>(&element)) {
			(*custom)->unload();
		}
	};
	_hasHeavyPart = false;
	for (const auto &line : _lines) {
		for (const auto &element : line) {
			unload(element);
		}
	}
}

} // namespace HistoryView
