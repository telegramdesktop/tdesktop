/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "ui/widgets/tooltip.h"
#include "ui/round_rect.h"
#include "base/timer.h"

namespace style {
struct EmojiPan;
} // namespace style

namespace Core {
struct RecentEmojiId;
} // namespace Core

namespace Data {
class StickersSet;
} // namespace Data

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Ui::Emoji {
enum class Section;
} // namespace Ui::Emoji

namespace Ui::Text {
struct CustomEmojiColored;
} // namespace Ui::Text

namespace Ui::CustomEmoji {
class Loader;
class Instance;
struct RepaintRequest;
} // namespace Ui::CustomEmoji

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {

inline constexpr auto kEmojiSectionCount = 8;

struct StickerIcon;
class EmojiColorPicker;
class StickersListFooter;
class GradientPremiumStar;
class LocalStickersManager;

enum class EmojiListMode {
	Full,
	EmojiStatus,
	FullReactions,
	RecentReactions,
};

struct EmojiListDescriptor {
	not_null<Main::Session*> session;
	EmojiListMode mode = EmojiListMode::Full;
	Window::SessionController *controller = nullptr;
	Fn<bool()> paused;
	std::vector<DocumentId> customRecentList;
	Fn<std::unique_ptr<Ui::Text::CustomEmoji>(
		DocumentId,
		Fn<void()>)> customRecentFactory;
	const style::EmojiPan *st = nullptr;
};

class EmojiListWidget
	: public TabbedSelector::Inner
	, public Ui::AbstractTooltipShower {
public:
	using Mode = EmojiListMode;
	using EmojiChosen = TabbedSelector::EmojiChosen;
	using FileChosen = TabbedSelector::FileChosen;

	EmojiListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::GifPauseReason level,
		Mode mode);
	EmojiListWidget(QWidget *parent, EmojiListDescriptor &&descriptor);
	~EmojiListWidget();

	using Section = Ui::Emoji::Section;

	void refreshRecent() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showSet(uint64 setId);
	[[nodiscard]] uint64 currentSet(int yOffset) const;
	void setAllowWithoutPremium(bool allow);

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void refreshEmoji();

	[[nodiscard]] rpl::producer<EmojiChosen> chosen() const;
	[[nodiscard]] rpl::producer<FileChosen> customChosen() const;
	[[nodiscard]] rpl::producer<FileChosen> premiumChosen() const;
	[[nodiscard]] rpl::producer<> jumpedToPremium() const;

	void provideRecent(const std::vector<DocumentId> &customRecentList);

	void paintExpanding(
		QPainter &p,
		QRect clip,
		int finalBottom,
		float64 progress,
		RectPart origin);

	base::unique_qptr<Ui::PopupMenu> fillContextMenu(
		SendMenu::Type type) override;

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
	void processPanelHideFinished() override;
	int countDesiredHeight(int newWidth) override;
	int defaultMinimalHeight() const override;

private:
	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
		bool premiumRequired = false;
		bool collapsed = false;
	};
	struct CustomOne {
		not_null<Ui::Text::CustomEmoji*> custom;
		not_null<DocumentData*> document;
	};
	struct CustomSet {
		uint64 id = 0;
		not_null<Data::StickersSet*> set;
		DocumentData *thumbnailDocument = nullptr;
		QString title;
		std::vector<CustomOne> list;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
		bool painted = false;
		bool expanded = false;
		bool canRemove = false;
		bool premiumRequired = false;
	};
	struct CustomEmojiInstance;
	struct RightButton {
		QImage back;
		QImage backOver;
		QImage rippleMask;
		QString text;
		int textWidth = 0;
	};
	struct RecentOne;
	struct OverEmoji {
		int section = 0;
		int index = 0;

		inline bool operator==(OverEmoji other) const {
			return (section == other.section)
				&& (index == other.index);
		}
		inline bool operator!=(OverEmoji other) const {
			return !(*this == other);
		}
	};
	struct OverSet {
		int section = 0;

		inline bool operator==(OverSet other) const {
			return (section == other.section);
		}
		inline bool operator!=(OverSet other) const {
			return !(*this == other);
		}
	};
	struct OverButton {
		int section = 0;

		inline bool operator==(OverButton other) const {
			return (section == other.section);
		}
		inline bool operator!=(OverButton other) const {
			return !(*this == other);
		}
	};
	using OverState = std::variant<
		v::null_t,
		OverEmoji,
		OverSet,
		OverButton>;
	struct ExpandingContext {
		float64 progress = 0.;
		int finalHeight = 0;
		bool expanding = false;
	};

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	[[nodiscard]] SectionInfo sectionInfo(int section) const;
	[[nodiscard]] SectionInfo sectionInfoByOffset(int yOffset) const;
	[[nodiscard]] int sectionsCount() const;
	void setSingleSize(QSize size);

	void showPicker();
	void pickerHidden();
	void colorChosen(EmojiChosen data);
	bool checkPickerHide();
	void refreshCustom();
	void unloadNotSeenCustom(int visibleTop, int visibleBottom);
	void unloadAllCustom();
	void unloadCustomIn(const SectionInfo &info);

	void ensureLoaded(int section);
	void updateSelected();
	void setSelected(OverState newSelected);
	void setPressed(OverState newPressed);

	[[nodiscard]] EmojiPtr lookupOverEmoji(const OverEmoji *over) const;
	[[nodiscard]] DocumentData *lookupCustomEmoji(
		int index,
		int section) const;
	[[nodiscard]] EmojiChosen lookupChosen(
		EmojiPtr emoji,
		not_null<const OverEmoji*> over);
	[[nodiscard]] FileChosen lookupChosen(
		not_null<DocumentData*> custom,
		const OverEmoji *over,
		Api::SendOptions options = Api::SendOptions());
	void selectEmoji(EmojiChosen data);
	void selectCustom(FileChosen data);
	void paint(QPainter &p, ExpandingContext context, QRect clip);
	void drawCollapsedBadge(QPainter &p, QPoint position, int count);
	void drawRecent(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		crl::time now,
		bool paused,
		int index);
	void drawEmoji(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		EmojiPtr emoji);
	void drawCustom(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		crl::time now,
		bool paused,
		int set,
		int index);
	[[nodiscard]] bool hasRemoveButton(int index) const;
	[[nodiscard]] QRect removeButtonRect(int index) const;
	[[nodiscard]] QRect removeButtonRect(const SectionInfo &info) const;
	[[nodiscard]] bool hasAddButton(int index) const;
	[[nodiscard]] QRect addButtonRect(int index) const;
	[[nodiscard]] bool hasUnlockButton(int index) const;
	[[nodiscard]] QRect unlockButtonRect(int index) const;
	[[nodiscard]] bool hasButton(int index) const;
	[[nodiscard]] QRect buttonRect(int index) const;
	[[nodiscard]] QRect buttonRect(
		const SectionInfo &info,
		const RightButton &button) const;
	[[nodiscard]] const RightButton &rightButton(int index) const;
	[[nodiscard]] QRect emojiRect(int section, int index) const;
	[[nodiscard]] int emojiRight() const;
	[[nodiscard]] int emojiLeft() const;
	[[nodiscard]] uint64 sectionSetId(int section) const;
	[[nodiscard]] std::vector<StickerIcon> fillIcons();
	int paintButtonGetWidth(
		QPainter &p,
		const SectionInfo &info,
		bool selected,
		QRect clip) const;

	void displaySet(uint64 setId);
	void removeSet(uint64 setId);

	void refreshColoredStatuses();
	void initButton(RightButton &button, const QString &text, bool gradient);
	[[nodiscard]] std::unique_ptr<Ui::RippleAnimation> createButtonRipple(
		int section);
	[[nodiscard]] QPoint buttonRippleTopLeft(int section) const;

	void repaintCustom(uint64 setId);

	void fillRecent();
	void fillRecentFrom(const std::vector<DocumentId> &list);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomEmoji(
		not_null<DocumentData*> document,
		uint64 setId);
	[[nodiscard]] Ui::Text::CustomEmoji *resolveCustomRecent(
		Core::RecentEmojiId customId);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomRecent(
		DocumentId documentId);
	[[nodiscard]] Fn<void()> repaintCallback(
		DocumentId documentId,
		uint64 setId);

	Window::SessionController *_controller = nullptr;
	Mode _mode = Mode::Full;
	const int _staticCount = 0;
	StickersListFooter *_footer = nullptr;
	std::unique_ptr<GradientPremiumStar> _premiumIcon;
	std::unique_ptr<LocalStickersManager> _localSetsManager;
	Fn<std::unique_ptr<Ui::Text::CustomEmoji>(
		DocumentId,
		Fn<void()>)> _customRecentFactory;

	int _counts[kEmojiSectionCount];
	std::vector<RecentOne> _recent;
	base::flat_set<DocumentId> _recentCustomIds;
	base::flat_set<uint64> _repaintsScheduled;
	std::unique_ptr<Ui::Text::CustomEmojiColored> _emojiStatusColor;
	bool _recentPainted = false;
	bool _grabbingChosen = false;
	QVector<EmojiPtr> _emoji[kEmojiSectionCount];
	std::vector<CustomSet> _custom;
	base::flat_map<DocumentId, CustomEmojiInstance> _customEmoji;
	base::flat_map<
		DocumentId,
		std::unique_ptr<Ui::Text::CustomEmoji>> _customRecent;
	int _customSingleSize = 0;
	bool _allowWithoutPremium = false;
	Ui::RoundRect _overBg;

	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;
	QPoint _areaPosition;
	QPoint _innerPosition;
	QPoint _customPosition;

	RightButton _add;
	RightButton _unlock;
	RightButton _restore;
	Ui::RoundRect _collapsedBg;

	OverState _selected;
	OverState _pressed;
	OverState _pickerSelected;
	QPoint _lastMousePos;

	object_ptr<EmojiColorPicker> _picker;
	base::Timer _showPickerTimer;

	rpl::event_stream<EmojiChosen> _chosen;
	rpl::event_stream<FileChosen> _customChosen;
	rpl::event_stream<FileChosen> _premiumChosen;
	rpl::event_stream<> _jumpedToPremium;

};

tr::phrase<> EmojiCategoryTitle(int index);

} // namespace ChatHelpers
