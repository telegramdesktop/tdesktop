/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "data/stickers/data_stickers.h"
#include "base/variant.h"
#include "base/timer.h"

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

namespace ChatHelpers {

struct StickerIcon;

class StickersListWidget final : public TabbedSelector::Inner {
public:
	StickersListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		bool masks = false);

	Main::Session &session() const;

	rpl::producer<TabbedSelector::FileChosen> chosen() const;
	rpl::producer<> scrollUpdated() const;
	rpl::producer<> checkForHide() const;

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
	bool preventAutoHide();

	uint64 currentSet(int yOffset) const;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	void sendSearchRequest();
	void searchForSets(const QString &query);

	std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();

	void fillContextMenu(
		not_null<Ui::PopupMenu*> menu,
		SendMenu::Type type) override;

	bool mySetsEmpty() const;

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
	class Footer;

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

	struct Sticker {
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> documentMedia;
		Lottie::Animation *animated = nullptr;
		QPixmap savedFrame;

		void ensureMediaCreated();
	};

	struct Set {
		Set(
			uint64 id,
			Data::StickersSet *set,
			Data::StickersSetFlags flags,
			const QString &title,
			const QString &shortName,
			int count,
			bool externalLayout,
			std::vector<Sticker> &&stickers = {});
		Set(Set &&other);
		Set &operator=(Set &&other);
		~Set();

		uint64 id = 0;
		Data::StickersSet *set = nullptr;
		Data::StickersSetFlags flags;
		QString title;
		QString shortName;
		std::vector<Sticker> stickers;
		std::unique_ptr<Ui::RippleAnimation> ripple;

		std::unique_ptr<Lottie::MultiPlayer> lottiePlayer;
		rpl::lifetime lottieLifetime;

		int count = 0;
		bool externalLayout = false;
	};
	struct FeaturedSet {
		uint64 id = 0;
		Data::StickersSetFlags flags;
		std::vector<Sticker> stickers;
	};

	static std::vector<Sticker> PrepareStickers(
		const QVector<DocumentData*> &pack);

	void preloadMoreOfficial();
	QSize boundingBoxSize() const;

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void setSection(Section section);
	void displaySet(uint64 setId);
	void checkHideWithBox(QPointer<Ui::BoxContent> box);
	void installSet(uint64 setId);
	void removeMegagroupSet(bool locally);
	void removeSet(uint64 setId);
	void sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input);
	void refreshMySets();
	void refreshFeaturedSets();
	void refreshSearchSets();
	void refreshSearchIndex();

	bool setHasTitle(const Set &set) const;
	bool stickerHasDeleteButton(const Set &set, int index) const;
	std::vector<Sticker> collectRecentStickers();
	void refreshRecentStickers(bool resize = true);
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
	std::unique_ptr<Ui::RippleAnimation> createButtonRipple(int section);
	QPoint buttonRippleTopLeft(int section) const;

	enum class ValidateIconAnimations {
		Full,
		Scroll,
		None,
	};
	void validateSelectedIcon(ValidateIconAnimations animations);

	std::vector<Set> &shownSets();
	const std::vector<Set> &shownSets() const;
	int featuredRowHeight() const;
	void checkVisibleFeatured(int visibleTop, int visibleBottom);
	void readVisibleFeatured(int visibleTop, int visibleBottom);

	void paintStickers(Painter &p, QRect clip);
	void paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected);
	void paintSticker(Painter &p, Set &set, int y, int section, int index, bool selected, bool deleteSelected);
	void paintEmptySearchResults(Painter &p);

	void ensureLottiePlayer(Set &set);
	void setupLottie(Set &set, int section, int index);
	void markLottieFrameShown(Set &set);
	void checkVisibleLottie();
	void pauseInvisibleLottieIn(const SectionInfo &info);
	void takeHeavyData(std::vector<Set> &to, std::vector<Set> &from);
	void takeHeavyData(Set &to, Set &from);
	void takeHeavyData(Sticker &to, Sticker &from);
	void clearHeavyIn(Set &set, bool clearSavedFrames = true);
	void clearHeavyData();

	int stickersRight() const;
	bool featuredHasAddButton(int index) const;
	QRect featuredAddRect(int index) const;
	bool hasRemoveButton(int index) const;
	QRect removeButtonRect(int index) const;
	int megagroupSetInfoLeft() const;
	void refreshMegagroupSetGeometry();
	QRect megagroupSetButtonRectFinal() const;

	const Data::StickersSetsOrder &defaultSetsOrder() const;
	Data::StickersSetsOrder &defaultSetsOrderRef();

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

	void selectEmoji(EmojiPtr emoji);
	int stickersLeft() const;
	QRect stickerRect(int section, int sel);

	void removeRecentSticker(int section, int index);
	void removeFavedSticker(int section, int index);
	void setColumnCount(int count);
	void refreshFooterIcons();

	void showStickerSetBox(not_null<DocumentData*> document);

	void cancelSetsSearch();
	void showSearchResults();
	void searchResultsDone(const MTPmessages_FoundStickerSets &result);
	void refreshSearchRows();
	void refreshSearchRows(const std::vector<uint64> *cloudSets);
	void fillLocalSearchRows(const QString &query);
	void fillCloudSearchRows(const std::vector<uint64> &cloudSets);
	void addSearchRow(not_null<Data::StickersSet*> set);

	void showPreview();

	MTP::Sender _api;
	ChannelData *_megagroupSet = nullptr;
	uint64 _megagroupSetIdRequested = 0;
	std::vector<Set> _mySets;
	std::vector<Set> _officialSets;
	std::vector<Set> _searchSets;
	int _featuredSetsCount = 0;
	base::flat_set<uint64> _installedLocallySets;
	std::vector<bool> _custom;
	base::flat_set<not_null<DocumentData*>> _favedStickersMap;
	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;

	mtpRequestId _officialRequestId = 0;
	int _officialOffset = 0;

	Section _section = Section::Stickers;
	const bool _isMasks;

	bool _displayingSet = false;
	uint64 _removingSetId = 0;

	Footer *_footer = nullptr;
	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;

	OverState _selected;
	OverState _pressed;
	QPoint _lastMousePosition;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

	Ui::Text::String _megagroupSetAbout;
	QString _megagroupSetButtonText;
	int _megagroupSetButtonTextWidth = 0;
	QRect _megagroupSetButtonRect;
	std::unique_ptr<Ui::RippleAnimation> _megagroupSetButtonRipple;

	QString _addText;
	int _addWidth;

	object_ptr<Ui::LinkButton> _settings;

	base::Timer _previewTimer;
	bool _previewShown = false;

	std::map<QString, std::vector<uint64>> _searchCache;
	std::vector<std::pair<uint64, QStringList>> _searchIndex;
	base::Timer _searchRequestTimer;
	QString _searchQuery, _searchNextQuery;
	mtpRequestId _searchRequestId = 0;

	rpl::event_stream<TabbedSelector::FileChosen> _chosen;
	rpl::event_stream<> _scrollUpdated;
	rpl::event_stream<> _checkForHide;

};

} // namespace ChatHelpers
