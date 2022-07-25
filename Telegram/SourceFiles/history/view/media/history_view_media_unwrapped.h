/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

struct HistoryMessageVia;
struct HistoryMessageReply;
struct HistoryMessageForwarded;

namespace HistoryView {

class UnwrappedMedia final : public Media {
public:
	class Content {
	public:
		[[nodiscard]] virtual QSize size() = 0;

		virtual void draw(
			Painter &p,
			const PaintContext &context,
			const QRect &r) = 0;

		[[nodiscard]] virtual ClickHandlerPtr link() {
			return nullptr;
		}

		[[nodiscard]] virtual DocumentData *document() {
			return nullptr;
		}
		virtual void stickerClearLoopPlayed() {
		}
		virtual std::unique_ptr<Lottie::SinglePlayer> stickerTakeLottie(
			not_null<DocumentData*> data,
			const Lottie::ColorReplacements *replacements);

		//virtual void externalLottieProgressing(bool external) {
		//}
		//virtual bool externalLottieTill(ExternalLottieInfo info) {
		//	return true;
		//}
		//virtual ExternalLottieInfo externalLottieInfo() const {
		//	return {};
		//}

		virtual bool hasHeavyPart() const {
			return false;
		}
		virtual void unloadHeavyPart() {
		}
		virtual void refreshLink() {
		}
		[[nodiscard]] virtual bool alwaysShowOutTimestamp() {
			return false;
		}
		virtual bool hasTextForCopy() const {
			return false;
		}
		virtual ~Content() = default;
	};

	UnwrappedMedia(
		not_null<Element*> parent,
		std::unique_ptr<Content> content);

	void draw(Painter &p, const PaintContext &context) const override;
	PointState pointState(QPoint point) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool hasTextForCopy() const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItem() const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	DocumentData *getDocument() const override {
		return _content->document();
	}

	bool needsBubble() const override {
		return false;
	}
	bool customInfoLayout() const override {
		return true;
	}
	QRect contentRectForReactions() const override;
	std::optional<int> reactionButtonCenterOverride() const override;
	QPoint resolveCustomInfoRightBottom() const override;

	void stickerClearLoopPlayed() override {
		_content->stickerClearLoopPlayed();
	}
	std::unique_ptr<Lottie::SinglePlayer> stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	//void externalLottieProgressing(bool external) override;
	//bool externalLottieTill(ExternalLottieInfo info) override;
	//ExternalLottieInfo externalLottieInfo() const override;

	bool hasHeavyPart() const override {
		return _content->hasHeavyPart();
	}
	void unloadHeavyPart() override {
		_content->unloadHeavyPart();
	}

private:
	struct SurroundingInfo {
		int height = 0;
		int forwardedHeight = 0;
		bool forwardedBreakEverywhere = false;

		explicit operator bool() const {
			return (height > 0);
		}
	};
	[[nodiscard]] SurroundingInfo surroundingInfo(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded,
		int outerw) const;
	void drawSurrounding(
		Painter &p,
		const QRect &inner,
		const PaintContext &context,
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	bool needInfoDisplay() const;
	int additionalWidth(
		const HistoryMessageVia *via,
		const HistoryMessageReply *reply,
		const HistoryMessageForwarded *forwarded) const;

	int calculateFullRight(const QRect &inner) const;
	QPoint calculateFastActionPosition(
		int fullBottom,
		int replyRight,
		int fullRight,
		QSize size) const;

	const HistoryMessageForwarded *getDisplayedForwardedInfo() const;

	std::unique_ptr<Content> _content;
	QSize _contentSize;

};

} // namespace HistoryView
