/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "ui/widgets/tooltip.h"
#include "base/timer.h"

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Ui::Emoji {
enum class Section;
} // namespace Ui::Emoji

namespace Ui::CustomEmoji {
class Instance;
struct RepaintRequest;
} // namespace Ui::CustomEmoji

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {

inline constexpr auto kEmojiSectionCount = 8;

class EmojiColorPicker;

class EmojiListWidget
	: public TabbedSelector::Inner
	, public Ui::AbstractTooltipShower {
public:
	EmojiListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~EmojiListWidget();

	using Section = Ui::Emoji::Section;

	void refreshRecent() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showEmojiSection(Section section);
	[[nodiscard]] Section currentSection(int yOffset) const;

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	[[nodiscard]] rpl::producer<EmojiPtr> chosen() const;
	[[nodiscard]] auto customChosen() const
		-> rpl::producer<TabbedSelector::FileChosen>;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	bool eventHook(QEvent *e) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private:
	class Footer;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};
	struct CustomInstance;
	struct CustomOne {
		not_null<CustomInstance*> instance;
		not_null<DocumentData*> document;
	};
	struct CustomSet {
		uint64 id = 0;
		QString title;
		std::vector<CustomOne> list;
		bool painted = false;
	};
	struct RepaintSet {
		base::flat_set<uint64> ids;
		crl::time when = 0;
	};

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	[[nodiscard]] SectionInfo sectionInfo(int section) const;
	[[nodiscard]] SectionInfo sectionInfoByOffset(int yOffset) const;
	[[nodiscard]] int sectionsCount() const;

	void showPicker();
	void pickerHidden();
	void colorChosen(EmojiPtr emoji);
	bool checkPickerHide();
	void refreshCustom();
	void unloadNotSeenCustom(int visibleTop, int visibleBottom);

	void ensureLoaded(int section);
	void updateSelected();
	void setSelected(int newSelected);

	void selectEmoji(EmojiPtr emoji);
	void selectCustom(not_null<DocumentData*> document);
	void drawEmoji(
		QPainter &p,
		QPoint position,
		int section,
		int index);
	void drawCustom(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused,
		int set,
		int index);
	[[nodiscard]] QRect emojiRect(int section, int sel) const;

	void repaintLater(
		uint64 setId,
		Ui::CustomEmoji::RepaintRequest request);
	template <typename CheckId>
	void repaintCustom(CheckId checkId);
	void scheduleRepaintTimer();
	void invokeRepaints();

	Footer *_footer = nullptr;

	int _counts[kEmojiSectionCount];
	QVector<EmojiPtr> _emoji[kEmojiSectionCount];
	std::vector<CustomSet> _custom;
	base::flat_map<uint64, std::unique_ptr<CustomInstance>> _instances;

	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;
	int _esize = 0;

	int _selected = -1;
	int _pressedSel = -1;
	int _pickerSel = -1;
	QPoint _lastMousePos;

	object_ptr<EmojiColorPicker> _picker;
	base::Timer _showPickerTimer;

	base::flat_map<crl::time, RepaintSet> _repaints;
	bool _repaintTimerScheduled = false;
	base::Timer _repaintTimer;
	crl::time _repaintNext = 0;

	rpl::event_stream<EmojiPtr> _chosen;
	rpl::event_stream<TabbedSelector::FileChosen> _customChosen;

};

tr::phrase<> EmojiCategoryTitle(int index);

} // namespace ChatHelpers
