/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"

namespace Ui {
class DynamicImage;
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class MediaGeneric;

class MediaGenericPart : public Object {
public:
	virtual ~MediaGenericPart() = default;

	virtual void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const = 0;
	[[nodiscard]] virtual TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const;
	virtual void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed);
	[[nodiscard]] virtual bool hasHeavyPart();
	virtual void unloadHeavyPart();
	[[nodiscard]] virtual auto stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements
	) -> std::unique_ptr<StickerPlayer>;
};

struct MediaGenericDescriptor {
	int maxWidth = 0;
	ClickHandlerPtr serviceLink;
	bool service = false;
	bool hideServiceText = false;
};

class MediaGeneric final : public Media {
public:
	using Part = MediaGenericPart;

	MediaGeneric(
		not_null<Element*> parent,
		Fn<void(Fn<void(std::unique_ptr<Part>)>)> generate,
		MediaGenericDescriptor &&descriptor = {});
	~MediaGeneric();

	[[nodiscard]] bool service() const {
		return _service;
	}

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool needsBubble() const override {
		return !_service;
	}
	bool customInfoLayout() const override {
		return false;
	}

	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool hideFromName() const override;
	bool hideServiceText() const override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

private:
	struct Entry {
		std::unique_ptr<Part> object;
	};

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] QMargins inBubblePadding() const;

	std::vector<Entry> _entries;
	int _maxWidthCap = 0;
	bool _service : 1 = false;
	bool _hideServiceText : 1 = false;

};

class MediaGenericTextPart final : public MediaGenericPart {
public:
	MediaGenericTextPart(
		TextWithEntities text,
		QMargins margins,
		const base::flat_map<uint16, ClickHandlerPtr> &links = {});

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	Ui::Text::String _text;
	QMargins _margins;

};

class TextDelimeterPart final : public MediaGenericPart {
public:
	TextDelimeterPart(const QString &text, QMargins margins);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	Ui::Text::String _text;
	QMargins _margins;

};

class StickerInBubblePart final : public MediaGenericPart {
public:
	struct Data {
		DocumentData *sticker = nullptr;
		int skipTop = 0;
		int size = 0;
		ChatHelpers::StickerLottieSize cacheTag = {};
		bool singleTimePlayback = false;
		ClickHandlerPtr link;

		explicit operator bool() const {
			return sticker != nullptr;
		}
	};
	StickerInBubblePart(
		not_null<Element*> parent,
		Element *replacing,
		Fn<Data()> lookup,
		QMargins padding);

	[[nodiscard]] not_null<Element*> parent() const {
		return _parent;
	}
	[[nodiscard]] bool resolved() const {
		return _sticker.has_value();
	}

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;
	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

private:
	void ensureCreated(Element *replacing = nullptr) const;

	const not_null<Element*> _parent;
	Fn<Data()> _lookup;
	mutable int _skipTop = 0;
	mutable QMargins _padding;
	mutable std::optional<Sticker> _sticker;
	mutable ClickHandlerPtr _link;

};

class StickerWithBadgePart final : public MediaGenericPart {
public:
	using Data = StickerInBubblePart::Data;
	StickerWithBadgePart(
		not_null<Element*> parent,
		Element *replacing,
		Fn<Data()> lookup,
		QMargins padding,
		QString badge);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;
	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

private:
	void validateBadge(const PaintContext &context) const;
	void paintBadge(Painter &p, const PaintContext &context) const;

	StickerInBubblePart _sticker;
	QString _badgeText;
	mutable QColor _badgeFg;
	mutable QColor _badgeBorder;
	mutable QImage _badge;
	mutable QImage _badgeCache;

};

class PeerBubbleListPart final : public MediaGenericPart {
public:
	PeerBubbleListPart(
		not_null<Element*> parent,
		const std::vector<not_null<PeerData*>> &list);
	~PeerBubbleListPart();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;
	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	int layout(int x, int y, int available);

	struct Peer {
		Ui::Text::String name;
		std::shared_ptr<Ui::DynamicImage> thumbnail;
		QRect geometry;
		ClickHandlerPtr link;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
		mutable std::array<QImage, 4> corners;
		mutable QColor bg;
		uint8 colorIndex = 0;
	};

	const not_null<Element*> _parent;
	std::vector<Peer> _peers;
	mutable QPoint _lastPoint;
	mutable bool _subscribed = false;

};

} // namespace HistoryView
