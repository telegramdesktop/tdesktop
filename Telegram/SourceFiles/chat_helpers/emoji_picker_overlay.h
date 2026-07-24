/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/emoji_config.h"
#include "ui/widgets/shadow.h"

namespace Ui {
class AbstractButton;
class FlatLabel;
class ScrollArea;
} // namespace Ui

namespace ChatHelpers {

struct EmojiPickerOverlayDescriptor {
	QString aboutText;
	std::vector<EmojiPtr> recent;
	int maxSelected = 0;
	bool allowExpand = true;
	std::vector<EmojiPtr> initialSelected;
};

class EmojiPickerOverlay final : public Ui::RpWidget {
public:
	EmojiPickerOverlay(
		QWidget *parent,
		EmojiPickerOverlayDescriptor descriptor);
	~EmojiPickerOverlay();

	struct Metrics {
		QMargins shadowExtent;
		int tailHeight = 0;
		int collapsedHeight = 0;
		int expandedHeight = 0;
		int totalCollapsedHeight = 0;
		int totalExpandedHeight = 0;
	};
	[[nodiscard]] static Metrics EstimateMetrics(const QString &aboutText);

	[[nodiscard]] const std::vector<EmojiPtr> &selected() const;
	[[nodiscard]] rpl::producer<std::vector<EmojiPtr>> selectedValue() const;

	void setExpanded(bool expanded);
	[[nodiscard]] bool expanded() const;
	[[nodiscard]] rpl::producer<bool> expandedValue() const;

	[[nodiscard]] int collapsedHeight() const;
	[[nodiscard]] int expandedHeight() const;
	[[nodiscard]] QMargins shadowExtent() const;
	[[nodiscard]] int totalCollapsedHeight() const;
	[[nodiscard]] int totalExpandedHeight() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	class Strip;
	class Grid;

	void relayout();
	void toggleEmoji(EmojiPtr emoji, bool fromGrid);
	void notifySelectionChanged();
	void startExpandAnimation(bool expanded);
	void applyExpandProgress();
	void paintTailBubble(QPainter &p, const QRect &bubble, float64 opacity);
	[[nodiscard]] float64 currentExpandValue() const;
	[[nodiscard]] int currentShownHeight() const;
	[[nodiscard]] int tailHeight() const;
	[[nodiscard]] QRect bubbleRect() const;
	[[nodiscard]] QRect bubbleShownRect() const;

	const QString _aboutText;
	const std::vector<EmojiPtr> _recent;
	const int _maxSelected;
	const bool _allowExpand;

	std::vector<EmojiPtr> _allForGrid;

	std::vector<EmojiPtr> _selectedList;
	rpl::variable<std::vector<EmojiPtr>> _selectedVar;
	rpl::variable<bool> _expanded = false;

	std::unique_ptr<Ui::FlatLabel> _about;
	Strip *_strip = nullptr;
	Ui::AbstractButton *_expandButton = nullptr;
	std::unique_ptr<Ui::ScrollArea> _scroll;
	Grid *_grid = nullptr;
	Ui::Animations::Simple _expandAnim;
	Ui::BoxShadow _shadow;

};

} // namespace ChatHelpers
