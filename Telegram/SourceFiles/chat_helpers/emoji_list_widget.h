/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/compose/compose_features.h"
#include "chat_helpers/tabbed_selector.h"
#include "ui/effects/animations.h"
#include "ui/widgets/tooltip.h"
#include "ui/round_rect.h"
#include "base/timer.h"

#include <map>

class StickerPremiumMark;

namespace style {
struct EmojiPan;
} // namespace style

namespace Core {
struct RecentEmojiId;
} // namespace Core

namespace Data {
class StickersSet;
} // namespace Data

namespace PowerSaving {
enum Flag : uint32;
} // namespace PowerSaving

namespace tr {
template <typename ...Tags>
struct phrase;
} // namespace tr

namespace Ui {
class RippleAnimation;
class TabbedSearch;
} // namespace Ui

namespace Ui::Emoji {
enum class Section;
} // namespace Ui::Emoji

namespace Ui::Text {
class CustomEmoji;
struct CustomEmojiPaintContext;
} // namespace Ui::Text

namespace Ui::CustomEmoji {
class Loader;
class Instance;
struct RepaintRequest;
} // namespace Ui::CustomEmoji

namespace Window {
class SessionController;
class MediaPreviewWidget;
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
	TopicIcon,
	EmojiStatus,
	ChannelStatus,
	FullReactions,
	RecentReactions,
	UserpicBuilder,
	BackgroundEmoji,
	PeerTitle,
	MessageEffects,
	CustomOnly,
};

[[nodiscard]] std::vector<EmojiStatusId> DocumentListToRecent(
	const std::vector<DocumentId> &documents);

struct EmojiListDescriptor {
	std::shared_ptr<Show> show;
	EmojiListMode mode = EmojiListMode::Full;
	Fn<QColor()> customTextColor;
	Fn<bool()> paused;
	std::vector<EmojiStatusId> customRecentList;
	Fn<std::unique_ptr<Ui::Text::CustomEmoji>(
		DocumentId,
		Fn<void()>)> customRecentFactory;
	base::flat_set<DocumentId> freeEffects;
	const style::EmojiPan *st = nullptr;
	ComposeFeatures features;
	QWidget *mediaPreviewParent = nullptr;
	QMargins mediaPreviewMargins;
	bool mediaPreviewPanelStyle = true;
};

class EmojiListWidget final
	: public TabbedSelector::Inner
	, public Ui::AbstractTooltipShower {
public:
	using Mode = EmojiListMode;

	EmojiListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		PauseReason level,
		Mode mode);
	EmojiListWidget(QWidget *parent, EmojiListDescriptor &&descriptor);
	~EmojiListWidget();

	using Section = Ui::Emoji::Section;

	void refreshRecent() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void afterShown() override;
	void beforeHiding() override;

	void showSet(uint64 setId);
	[[nodiscard]] uint64 currentSet(int yOffset) const;
	void setAllowWithoutPremium(bool allow);
	void showMegagroupSet(ChannelData *megagroup);

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void refreshEmoji();

	[[nodiscard]] rpl::producer<EmojiChosen> chosen() const;
	[[nodiscard]] rpl::producer<FileChosen> customChosen() const;
	[[nodiscard]] rpl::producer<> jumpedToPremium() const;
	[[nodiscard]] rpl::producer<> escapes() const;

	void provideRecent(const std::vector<EmojiStatusId> &customRecentList);

	void setSearchRightReserved(int value);

	void prepareExpanding();
	void paintExpanding(
		Painter &p,
		QRect clip,
		int finalBottom,
		float64 geometryProgress,
		float64 fullProgress,
		RectPart origin);

	base::unique_qptr<Ui::PopupMenu> fillContextMenu(
		const SendMenu::Details &details) override;

	[[nodiscard]] rpl::producer<std::vector<QString>> searchQueries() const;
	[[nodiscard]] rpl::producer<int> recentShownCount() const;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
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
		std::shared_ptr<Data::EmojiStatusCollectible> collectible;
		not_null<Ui::Text::CustomEmoji*> custom;
		not_null<DocumentData*> document;
		EmojiPtr emoji = nullptr;
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
	struct OverSearchShortcut {
		int index = 0;

		inline bool operator==(OverSearchShortcut other) const {
			return (index == other.index);
		}
		inline bool operator!=(OverSearchShortcut other) const {
			return !(*this == other);
		}
	};
	struct OverSearchBack {
		inline bool operator==(OverSearchBack other) const {
			return true;
		}
		inline bool operator!=(OverSearchBack other) const {
			return !(*this == other);
		}
	};
	using OverState = std::variant<
		v::null_t,
		OverEmoji,
		OverSet,
		OverButton,
		OverSearchShortcut,
		OverSearchBack>;
	struct ExpandingContext {
		float64 progress = 0.;
		int finalHeight = 0;
		bool expanding = false;
	};
	struct ResolvedCustom {
		DocumentData *document = nullptr;
		std::shared_ptr<Data::EmojiStatusCollectible> collectible;

		explicit operator bool() const {
			return document != nullptr;
		}
	};

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	[[nodiscard]] SectionInfo sectionInfo(int section) const;
	[[nodiscard]] SectionInfo sectionInfoByOffset(int yOffset) const;
	[[nodiscard]] int sectionsCount() const;
	void setSingleSize(QSize size);
	void setColorAllForceRippled(bool force);

	void showPicker();
	void pickerHidden();
	void colorChosen(EmojiChosen data);
	bool checkPickerHide();
	void refreshCustom();
	enum class GroupStickersPlace {
		Visible,
		Hidden,
	};
	void refreshEmojiStatusCollectibles();
	void refreshMegagroupStickers(
		Fn<void(uint64 setId, bool installed)> push,
		GroupStickersPlace place);
	void unloadNotSeenCustom(int visibleTop, int visibleBottom);
	void unloadAllCustom();
	void unloadCustomIn(const SectionInfo &info);

	void setupSearch();
	[[nodiscard]] std::vector<EmojiPtr> collectPlainSearchResults();
	void appendPremiumSearchResults();
	void sendSearchRequest();
	void sendSearchSetsRequest(const QString &query);
	void requestSearchCloud(
		const QString &query,
		int offset,
		bool fallbackToEmpty);
	void cancelSearchRequest();
	void toggleSearchLoading(bool loading);
	void searchCloudResultsDone(
		const QString &query,
		int requestedOffset,
		const MTPmessages_FoundStickers &result);
	void loadMoreSearchCloud();
	void checkPaginateSearchCloud(int visibleTop, int visibleBottom);
	void searchSetsResultsDone(
		const QString &query,
		const MTPmessages_FoundStickerSets &result);
	void showSearchResults();
	void fillCloudSearchResults();
	void refreshSearchShortcuts();
	void fillLocalSearchShortcuts(const QString &query);
	bool addSearchShortcut(not_null<Data::StickersSet*> set);
	[[nodiscard]] std::vector<CustomOne> collectSearchSet(
		not_null<Data::StickersSet*> set);
	void fillSelectedSearchShortcut();
	[[nodiscard]] bool searchShortcutsShown() const;
	[[nodiscard]] bool searchShortcutSelected() const;
	void startSearchSwapAnimation(Fn<void()> change, bool packToPack = false);
	[[nodiscard]] int searchShortcutsHeight() const;
	[[nodiscard]] int searchShortcutsTop() const;
	[[nodiscard]] QRect searchBackRect() const;
	[[nodiscard]] QRect searchShortcutRect(int index) const;
	void refreshSearchShortcutsScroll(int newWidth);
	void scrollSearchShortcutsTo(int value);
	void paintSearchShortcuts(Painter &p, QRect clip);
	void paintSearchShortcutIcon(Painter &p, const CustomSet &set, QRect rect);
	void toggleSearchShortcut(int index);
	void backToSearchResults();
	[[nodiscard]] CustomSet &searchSetBySection(int section);
	[[nodiscard]] const CustomSet &searchSetBySection(int section) const;
	void ensureLoaded(int section);
	void updateSelected();
	void setSelected(OverState newSelected);
	void setPressed(OverState newPressed);

	void fillRecentMenu(
		not_null<Ui::PopupMenu*> menu,
		int section,
		int index);
	void fillEmojiStatusMenu(
		not_null<Ui::PopupMenu*> menu,
		int section,
		int index);
	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> fillSetContextMenu(
		const CustomSet &set);

	[[nodiscard]] EmojiPtr lookupOverEmoji(const OverEmoji *over) const;
	[[nodiscard]] ResolvedCustom lookupCustomEmoji(
		const OverEmoji *over) const;
	[[nodiscard]] ResolvedCustom lookupCustomEmoji(
		int index,
		int section) const;
	[[nodiscard]] EmojiChosen lookupChosen(
		EmojiPtr emoji,
		not_null<const OverEmoji*> over);
	[[nodiscard]] FileChosen lookupChosen(
		ResolvedCustom custom,
		const OverEmoji *over,
		Api::SendOptions options = Api::SendOptions());
	void selectEmoji(EmojiChosen data);
	void selectCustom(FileChosen data);
	void paint(Painter &p, ExpandingContext context, QRect clip);
	void drawCollapsedBadge(QPainter &p, QPoint position, int count);
	void drawRecent(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		const RecentOne &recent);
	void drawEmoji(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		EmojiPtr emoji);
	void drawCustom(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		int set,
		int index);
	void drawSearchSetCustom(
		QPainter &p,
		const ExpandingContext &context,
		QPoint position,
		int section,
		int index);
	void validateEmojiPaintContext(const ExpandingContext &context);
	[[nodiscard]] bool hasColorButton(int index) const;
	[[nodiscard]] QRect colorButtonRect(int index) const;
	[[nodiscard]] QRect colorButtonRect(const SectionInfo &info) const;
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
	void paintEmptySearchResults(Painter &p);

	void displaySet(not_null<DocumentData*> document);
	void displaySet(uint64 setId);
	void removeSet(uint64 setId);
	void removeMegagroupSet(bool locally);

	void initButton(RightButton &button, const QString &text, bool gradient);
	[[nodiscard]] std::unique_ptr<Ui::RippleAnimation> createButtonRipple(
		int section);
	[[nodiscard]] QPoint buttonRippleTopLeft(int section) const;
	[[nodiscard]] std::unique_ptr<Ui::RippleAnimation>
	createSearchShortcutRipple(int index);
	[[nodiscard]] PowerSaving::Flag powerSavingFlag() const;

	void repaintCustom(uint64 setId);

	void fillRecent();
	void fillRecentFrom(const std::vector<EmojiStatusId> &list);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomEmoji(
		EmojiStatusId id,
		not_null<DocumentData*> document,
		uint64 setId);
	[[nodiscard]] Ui::Text::CustomEmoji *resolveCustomRecent(
		Core::RecentEmojiId customId);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomRecent(
		DocumentId documentId);
	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomRecent(
		EmojiStatusId id);
	[[nodiscard]] Fn<void()> repaintCallback(
		DocumentId documentId,
		uint64 setId);

	void showPreview();
	bool showPreviewFor(not_null<DocumentData*> document);
	void ensureMediaPreview();

	void applyNextSearchQuery();

	const std::shared_ptr<Show> _show;
	const ComposeFeatures _features;
	const bool _onlyUnicodeEmoji;
	Mode _mode = Mode::Full;
	QWidget *_mediaPreviewParent = nullptr;
	QMargins _mediaPreviewMargins;
	bool _mediaPreviewPanelStyle = true;
	std::unique_ptr<Ui::TabbedSearch> _search;
	MTP::Sender _api;
	const int _staticCount = 0;
	StickersListFooter *_footer = nullptr;
	std::unique_ptr<GradientPremiumStar> _premiumIcon;
	std::unique_ptr<LocalStickersManager> _localSetsManager;
	ChannelData *_megagroupSet = nullptr;
	uint64 _megagroupSetIdRequested = 0;
	Fn<std::unique_ptr<Ui::Text::CustomEmoji>(
		DocumentId,
		Fn<void()>)> _customRecentFactory;

	int _counts[kEmojiSectionCount];
	std::vector<RecentOne> _recent;
	base::flat_set<DocumentId> _recentCustomIds;
	base::flat_set<DocumentId> _freeEffects;
	base::flat_set<uint64> _repaintsScheduled;
	rpl::variable<int> _recentShownCount;
	std::unique_ptr<Ui::Text::CustomEmojiPaintContext> _emojiPaintContext;
	bool _recentPainted = false;
	bool _grabbingChosen = false;
	bool _paintAsPremium = false;
	QVector<EmojiPtr> _emoji[kEmojiSectionCount];
	std::vector<CustomSet> _custom;
	base::flat_set<DocumentId> _restrictedCustomList;
	std::map<EmojiStatusId, CustomEmojiInstance> _customEmoji;
	base::flat_map<
		DocumentId,
		std::unique_ptr<Ui::Text::CustomEmoji>> _customRecent;
	Fn<QColor()> _customTextColor;
	int _customSingleSize = 0;
	bool _allowWithoutPremium = false;
	Ui::RoundRect _overBg;
	QImage _searchExpandCache;

	std::unique_ptr<StickerPremiumMark> _premiumMark;
	QImage _premiumMarkFrameCache;
	mutable std::unique_ptr<Ui::RippleAnimation> _colorAllRipple;
	bool _colorAllRippleForced = false;
	rpl::lifetime _colorAllRippleForcedLifetime;

	rpl::event_stream<std::vector<QString>> _searchQueries;
	std::vector<QString> _nextSearchQuery;
	std::vector<QString> _searchQuery;
	QString _searchQueryText;
	base::flat_set<EmojiPtr> _searchEmoji;
	base::flat_set<EmojiPtr> _searchEmojiPrevious;
	base::flat_set<DocumentId> _searchCustomIds;
	std::vector<RecentOne> _searchResults;
	bool _searchMode = false;
	std::map<QString, std::vector<DocumentId>> _searchCloudCache;
	std::map<QString, int> _searchCloudNextOffset;
	std::map<QString, std::vector<uint64>> _searchSetsCache;
	std::vector<CustomSet> _searchSets;
	std::vector<CustomSet> _searchShortcutSets;
	QString _searchRequestQuery;
	QString _searchNextRequestQuery;
	QString _searchEmoticon;
	uint64 _searchSelectedSetId = 0;
	int _searchShortcutsScroll = 0;
	int _searchShortcutsScrollMax = 0;
	int _searchShortcutsDragStart = 0;
	QPoint _searchShortcutsMouseDown;
	bool _searchShortcutsDragging = false;
	Ui::Animations::Simple _searchSwapAnimation;
	QPixmap _searchSwapBefore;
	QPixmap _searchSwapAfter;
	int _searchSwapTop = 0;
	bool _searchSwapReverse = false;
	bool _searchSwapPartial = false;
	mtpRequestId _searchCloudRequestId = 0;
	mtpRequestId _searchSetsRequestId = 0;
	bool _searchLoading = false;

	int _rowsTop = 0;
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

	base::Timer _searchRequestTimer;
	object_ptr<EmojiColorPicker> _picker;
	base::Timer _showPickerTimer;
	base::Timer _previewTimer;
	bool _previewShown = false;


	base::unique_qptr<Window::MediaPreviewWidget> _mediaPreview;

	rpl::event_stream<EmojiChosen> _chosen;
	rpl::event_stream<FileChosen> _customChosen;
	rpl::event_stream<> _jumpedToPremium;

};

tr::phrase<> EmojiCategoryTitle(int index);

} // namespace ChatHelpers
