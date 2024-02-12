/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_sticker.h"

namespace Data {
struct GiveawayStart;
struct GiveawayResults;
} // namespace Data

namespace Ui {
class DynamicImage;
class RippleAnimation;
} // namespace Ui

namespace HistoryView {

class MediaInBubble final : public Media {
public:
	class Part : public Object {
	public:
		virtual ~Part() = default;

		virtual void draw(
			Painter &p,
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
	};

	MediaInBubble(
		not_null<Element*> parent,
		Fn<void(Fn<void(std::unique_ptr<Part>)>)> generate);
	~MediaInBubble();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool hideFromName() const override;

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

};

class TextMediaInBubblePart final : public MediaInBubble::Part {
public:
	TextMediaInBubblePart(
		TextWithEntities text,
		QMargins margins,
		const base::flat_map<uint16, ClickHandlerPtr> &links = {});

	void draw(
		Painter &p,
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

class TextDelimeterPart final : public MediaInBubble::Part {
public:
	TextDelimeterPart(const QString &text, QMargins margins);

	void draw(
		Painter &p,
		const PaintContext &context,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	Ui::Text::String _text;
	QMargins _margins;

};

class StickerWithBadgePart final : public MediaInBubble::Part {
public:
	struct Data {
		DocumentData *sticker = nullptr;
		int skipTop = 0;
		bool isGiftBoxSticker = false;

		explicit operator bool() const {
			return sticker != nullptr;
		}
	};
	StickerWithBadgePart(
		not_null<Element*> parent,
		Fn<Data()> lookup,
		QString badge);

	void draw(
		Painter &p,
		const PaintContext &context,
		int outerWidth) const override;
	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	void ensureCreated() const;
	void validateBadge(const PaintContext &context) const;
	void paintBadge(Painter &p, const PaintContext &context) const;

	const not_null<Element*> _parent;
	Fn<Data()> _lookup;
	QString _badgeText;
	mutable int _skipTop = 0;
	mutable std::optional<Sticker> _sticker;
	mutable QColor _badgeFg;
	mutable QColor _badgeBorder;
	mutable QImage _badge;
	mutable QImage _badgeCache;

};

class PeerBubbleListPart final : public MediaInBubble::Part {
public:
	PeerBubbleListPart(
		not_null<Element*> parent,
		const std::vector<not_null<PeerData*>> &list);
	~PeerBubbleListPart();

	void draw(
		Painter &p,
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

[[nodiscard]] auto GenerateGiveawayStart(
	not_null<Element*> parent,
	not_null<Data::GiveawayStart*> data)
-> Fn<void(Fn<void(std::unique_ptr<MediaInBubble::Part>)>)>;

[[nodiscard]] auto GenerateGiveawayResults(
	not_null<Element*> parent,
	not_null<Data::GiveawayResults*> data)
-> Fn<void(Fn<void(std::unique_ptr<MediaInBubble::Part>)>)>;

} // namespace HistoryView
