/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/compose/compose_features.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/stickers/data_stickers.h"
#include "ui/round_rect.h"
#include "base/variant.h"
#include "base/timer.h"

class StickerPremiumMark;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class LinkButton;
class PopupMenu;
class RippleAnimation;
class BoxContent;
class PathShiftGradient;
class TabbedSearch;
} // namespace Ui

namespace Lottie {
class Animation;
class MultiPlayer;
class FrameRenderer;
} // namespace Lottie

namespace Data {
class DocumentMedia;
class StickersSet;
} // namespace Data

namespace Media::Clip {
class ReaderPointer;
enum class Notification;
} // namespace Media::Clip

namespace style {
struct EmojiPan;
struct FlatLabel;
} // namespace style

namespace ChatHelpers {

struct StickerIcon;
enum class ValidateIconAnimations;
class StickersListFooter;
class LocalStickersManager;

enum class StickersListMode {
	Full,
	Masks,
	UserpicBuilder,
	ChatIntro,
	MessageEffects,
};

struct StickerCustomRecentDescriptor {
	not_null<DocumentData*> document;
	QString cornerEmoji;
};

struct StickersListDescriptor {
	std::shared_ptr<Show> show;
	StickersListMode mode = StickersListMode::Full;
	Fn<bool()> paused;
	std::vector<StickerCustomRecentDescriptor> customRecentList;
	const style::EmojiPan *st = nullptr;
	ComposeFeatures features;
};

class StickersListWidget final : public TabbedSelector::Inner {
public:
	using Mode = StickersListMode;

	StickersListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		PauseReason level,
		Mode mode = Mode::Full);
	StickersListWidget(
		QWidget *parent,
		StickersListDescriptor &&descriptor);

	rpl::producer<FileChosen> chosen() const;
	rpl::producer<> scrollUpdated() const;
	rpl::producer<TabbedSelector::Action> choosingUpdated() const;

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showStickerSet(uint64 setId);
	void showMegagroupSet(ChannelData *megagroup);

	void afterShown() override;
	void beforeHiding() override;

	void refreshStickers();

	std::vector<StickerIcon> fillIcons();

	uint64 currentSet(int yOffset) const;

	void sendSearchRequest();
	void searchForSets(const QString &query, std::vector<EmojiPtr> emoji);

	std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();

	base::unique_qptr<Ui::PopupMenu> fillContextMenu(
		const SendMenu::Details &details) override;

	bool mySetsEmpty() const;

	void applySearchQuery(std::vector<QString> &&query);
	[[nodiscard]] rpl::producer<int> recentShownCount() const;

	~StickersListWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	void processPanelHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private:
	struct Sticker;
	struct Set;

	enum class Section {
		Featured,
		Stickers,
		Search,
	};

	struct OverSticker {
		int section = 0;
		int index = 0;
		bool overDelete = false;

		inline bool operator==(OverSticker other) const {
			return (section == other.section)
				&& (index == other.index)
				&& (overDelete == other.overDelete);
		}
		inline bool operator!=(OverSticker other) const {
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
	struct OverGroupAdd {
		inline bool operator==(OverGroupAdd other) const {
			return true;
		}
		inline bool operator!=(OverGroupAdd other) const {
			return !(*this == other);
		}
	};
	using OverState = std::variant<
		v::null_t,
		OverSticker,
		OverSet,
		OverButton,
		OverGroupAdd>;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};

	struct FeaturedSet {
		uint64 id = 0;
		Data::StickersSetFlags flags;
		std::vector<Sticker> stickers;
	};

	static std::vector<Sticker> PrepareStickers(
		const QVector<DocumentData*> &pack,
		bool skipPremium);

	void setupSearch();
	void preloadMoreOfficial();
	QSize boundingBoxSize() const;

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void setSection(Section section);
	void displaySet(uint64 setId);
	void removeMegagroupSet(bool locally);
	void removeSet(uint64 setId);
	void refreshMySets();
	void refreshFeaturedSets();
	void refreshSearchSets();
	void refreshSearchIndex();

	bool setHasTitle(const Set &set) const;
	bool stickerHasDeleteButton(const Set &set, int index) const;
	[[nodiscard]] std::vector<Sticker> collectRecentStickers();
	[[nodiscard]] std::vector<Sticker> collectCustomRecents();
	void refreshRecentStickers(bool resize = true);
	void refreshEffects();
	void refreshFavedStickers();
	enum class GroupStickersPlace {
		Visible,
		Hidden,
	};
	void refreshMegagroupStickers(GroupStickersPlace place);
	void refreshSettingsVisibility();

	void updateSelected();
	void setSelected(OverState newSelected);
	void setPressed(OverState newPressed);
	[[nodiscard]] std::unique_ptr<Ui::RippleAnimation> createButtonRipple(
		int section);
	[[nodiscard]] QPoint buttonRippleTopLeft(int section) const;

	[[nodiscard]] std::vector<Set> &shownSets();
	[[nodiscard]] const std::vector<Set> &shownSets() const;
	[[nodiscard]] int featuredRowHeight() const;
	void checkVisibleFeatured(int visibleTop, int visibleBottom);
	void readVisibleFeatured(int visibleTop, int visibleBottom);

	void paintStickers(Painter &p, QRect clip);
	void paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected);
	void paintSticker(
		Painter &p,
		Set &set,
		int y,
		int section,
		int index,
		crl::time now,
		bool paused,
		bool selected,
		bool deleteSelected);
	void paintEmptySearchResults(Painter &p);

	void ensureLottiePlayer(Set &set);
	void setupLottie(Set &set, int section, int index);
	void setupWebm(Set &set, int section, int index);
	void clipCallback(
		Media::Clip::Notification notification,
		uint64 setId,
		not_null<DocumentData*> document,
		int indexHint);
	[[nodiscard]] bool itemVisible(const SectionInfo &info, int index) const;
	void markLottieFrameShown(Set &set);
	void checkVisibleLottie();
	void pauseInvisibleLottieIn(const SectionInfo &info);
	void takeHeavyData(std::vector<Set> &to, std::vector<Set> &from);
	void takeHeavyData(Set &to, Set &from);
	void takeHeavyData(Sticker &to, Sticker &from);
	void clearHeavyIn(Set &set, bool clearSavedFrames = true);
	void clearHeavyData();
	void updateItems();
	void updateSets();
	void repaintItems(crl::time now = 0);
	void updateSet(const SectionInfo &info);
	void repaintItems(
		const SectionInfo &info,
		crl::time now);

	[[nodiscard]] int stickersRight() const;
	[[nodiscard]] bool featuredHasAddButton(int index) const;
	[[nodiscard]] QRect featuredAddRect(int index) const;
	[[nodiscard]] QRect featuredAddRect(
		const SectionInfo &info,
		bool installedSet) const;
	[[nodiscard]] bool hasRemoveButton(int index) const;
	[[nodiscard]] QRect removeButtonRect(int index) const;
	[[nodiscard]] QRect removeButtonRect(const SectionInfo &info) const;
	[[nodiscard]] int megagroupSetInfoLeft() const;
	void refreshMegagroupSetGeometry();
	[[nodiscard]] QRect megagroupSetButtonRectFinal() const;

	[[nodiscard]] const Data::StickersSetsOrder &defaultSetsOrder() const;
	[[nodiscard]] Data::StickersSetsOrder &defaultSetsOrderRef();
	void filterEffectsByEmoji(const std::vector<EmojiPtr> &emoji);

	enum class AppendSkip {
		None,
		Archived,
		Installed,
	};
	bool appendSet(
		std::vector<Set> &to,
		uint64 setId,
		bool externalLayout,
		AppendSkip skip = AppendSkip::None);

	int stickersLeft() const;
	QRect stickerRect(int section, int sel);

	void removeRecentSticker(int section, int index);
	void removeFavedSticker(int section, int index);
	void setColumnCount(int count);
	void refreshFooterIcons();
	void refreshIcons(ValidateIconAnimations animations);

	void showStickerSetBox(
		not_null<DocumentData*> document,
		uint64 setId);

	void cancelSetsSearch();
	void showSearchResults();
	void searchResultsDone(const MTPmessages_FoundStickerSets &result);
	void refreshSearchRows();
	void refreshSearchRows(const std::vector<uint64> *cloudSets);
	void fillFilteredStickersRow();
	void fillLocalSearchRows(const QString &query);
	void fillCloudSearchRows(const std::vector<uint64> &cloudSets);
	void addSearchRow(not_null<Data::StickersSet*> set);
	void toggleSearchLoading(bool loading);

	void showPreview();

	Ui::MessageSendingAnimationFrom messageSentAnimationInfo(
		int section,
		int index,
		not_null<DocumentData*> document);

	const Mode _mode;
	const std::shared_ptr<Show> _show;
	const ComposeFeatures _features;
	Ui::RoundRect _overBg;
	std::unique_ptr<Ui::TabbedSearch> _search;
	MTP::Sender _api;
	std::unique_ptr<LocalStickersManager> _localSetsManager;
	ChannelData *_megagroupSet = nullptr;
	uint64 _megagroupSetIdRequested = 0;
	std::vector<StickerCustomRecentDescriptor> _customRecentIds;
	std::vector<Set> _mySets;
	std::vector<Set> _officialSets;
	std::vector<Set> _searchSets;
	int _featuredSetsCount = 0;
	std::vector<bool> _custom;
	std::vector<EmojiPtr> _cornerEmoji;
	base::flat_set<not_null<DocumentData*>> _favedStickersMap;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;

	bool _paintAsPremium = false;
	bool _showingSetById = false;
	crl::time _lastScrolledAt = 0;
	crl::time _lastFullUpdatedAt = 0;

	mtpRequestId _officialRequestId = 0;
	int _officialOffset = 0;

	Section _section = Section::Stickers;
	const bool _isMasks;
	const bool _isEffects;

	base::Timer _updateItemsTimer;
	base::Timer _updateSetsTimer;
	base::flat_set<uint64> _repaintSetsIds;

	StickersListFooter *_footer = nullptr;
	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;

	OverState _selected;
	OverState _pressed;
	QPoint _lastMousePosition;

	Ui::RoundRect _trendingAddBgOver, _trendingAddBg, _inactiveButtonBg;
	Ui::RoundRect _groupCategoryAddBgOver, _groupCategoryAddBg;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

	Ui::Text::String _megagroupSetAbout;
	QString _megagroupSetButtonText;
	int _megagroupSetButtonTextWidth = 0;
	QRect _megagroupSetButtonRect;
	std::unique_ptr<Ui::RippleAnimation> _megagroupSetButtonRipple;

	QString _addText;
	int _addWidth;
	QString _installedText;
	int _installedWidth;

	object_ptr<Ui::LinkButton> _settings;

	base::Timer _previewTimer;
	bool _previewShown = false;

	std::unique_ptr<StickerPremiumMark> _premiumMark;

	std::vector<not_null<DocumentData*>> _filteredStickers;
	std::vector<EmojiPtr> _filterStickersCornerEmoji;
	rpl::variable<int> _recentShownCount;
	std::map<QString, std::vector<uint64>> _searchCache;
	std::vector<std::pair<uint64, QStringList>> _searchIndex;
	base::Timer _searchRequestTimer;
	QString _searchQuery, _searchNextQuery;
	mtpRequestId _searchRequestId = 0;

	rpl::event_stream<FileChosen> _chosen;
	rpl::event_stream<> _scrollUpdated;
	rpl::event_stream<TabbedSelector::Action> _choosingUpdated;

};

[[nodiscard]] object_ptr<Ui::BoxContent> MakeConfirmRemoveSetBox(
	not_null<Main::Session*> session,
	const style::FlatLabel &st,
	uint64 setId);

} // namespace ChatHelpers
