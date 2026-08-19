/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_star_gift.h"
#include "ui/rp_widget.h"

namespace Data {
struct UniqueGift;
struct GiftUpgradeSpinner;
} // namespace Data

namespace Ui {

class RpWidget;
class VerticalLayout;

struct UniqueGiftCoverArgs {
	rpl::producer<QString> pretitle;
	rpl::producer<QString> numberText;
	rpl::producer<TextWithEntities> subtitle;
	Fn<void()> subtitleClick;
	bool subtitleLinkColored = false;
	bool subtitleOutlined = false;
	rpl::producer<CreditsAmount> resalePrice;
	Fn<void()> resaleClick;
	bool attributesInfo = false;
	Fn<void(
		std::optional<Data::UniqueGift> now,
		std::optional<Data::UniqueGift> next,
		float64 progress)> repaintedHook;
	std::shared_ptr<Data::GiftUpgradeSpinner> upgradeSpinner;
};

struct UniqueGiftCover {
	Data::UniqueGift values;
	bool spinner = false;
	bool force = false;
};

void GiftReleasedByHandler(not_null<PeerData*> peer);

class UniqueGiftCoverWidget final : public RpWidget {
public:
	UniqueGiftCoverWidget(
		QWidget *parent,
		rpl::producer<UniqueGiftCover> data,
		UniqueGiftCoverArgs &&args);
	~UniqueGiftCoverWidget();

	struct PaintContext {
		int width = 0;
		int patternAreaHeight = 0;
	};

	struct CraftContext {
		QSize initial;
		QColor initialBg;
		float64 fadeInProgress = 0.;
		float64 expandProgress = 0.;
	};
	[[nodiscard]] QRect prepareCraftFrame(
		QImage &canvas,
		const CraftContext &context);

	[[nodiscard]] QColor backgroundColor() const;
	[[nodiscard]] QColor foregroundColor() const;

private:
	struct BackdropView;
	struct PatternView;
	struct ModelView;
	struct GiftView;

	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] QImage prepareBackdrop(
		BackdropView &backdrop,
		const PaintContext &context);
	void paintBackdrop(
		QPainter &p,
		BackdropView &backdrop,
		const PaintContext &context);
	void paintPattern(
		QPainter &p,
		PatternView &pattern,
		const BackdropView &backdrop,
		const PaintContext &context,
		float64 shown);
	bool paintModel(
		QPainter &p,
		ModelView &model,
		const PaintContext &context,
		float64 scale = 1.,
		bool paused = false);
	bool paintGift(
		QPainter &p,
		GiftView &gift,
		const PaintContext &context,
		float64 shown);

	void paintSpinnerAnimation(QPainter &p, const PaintContext &context);
	void paintNormalAnimation(
		QPainter &p,
		const PaintContext &context,
		float64 progress);

	struct State;
	const std::unique_ptr<State> _state;

};

[[nodiscard]] object_ptr<UniqueGiftCoverWidget> MakeUniqueGiftCover(
	QWidget *parent,
	rpl::producer<UniqueGiftCover> data,
	UniqueGiftCoverArgs &&args);

void AddUniqueGiftCover(
	not_null<VerticalLayout*> container,
	rpl::producer<UniqueGiftCover> data,
	UniqueGiftCoverArgs &&args);

} // namespace Ui
