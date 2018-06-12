/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/stickers.h"
#include "base/variant.h"
#include "base/timer.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class LinkButton;
class RippleAnimation;
} // namespace Ui

namespace ChatHelpers {

struct StickerIcon;

class StickersListWidget
	: public TabbedSelector::Inner
	, private base::Subscriber
	, private MTP::Sender {
	Q_OBJECT

public:
	StickersListWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller);

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showStickerSet(uint64 setId);
	void showMegagroupSet(ChannelData *megagroup);

	void afterShown() override;
	void beforeHiding() override;

	void refreshStickers();

	void fillIcons(QList<StickerIcon> &icons);
	bool preventAutoHide();

	uint64 currentSet(int yOffset) const;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	void sendSearchRequest();
	void searchForSets(const QString &query);

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

private slots:
	void onSettings();
	void onPreview();

signals:
	void selected(DocumentData *sticker);
	void scrollUpdated();
	void checkForHide();

private:
	class Footer;

	enum class Section {
		Featured,
		Stickers,
		Search,
	};

	struct OverSticker {
		int section;
		int index;
		bool overDelete;
	};
	struct OverSet {
		int section;
	};
	struct OverButton {
		int section;
	};
	struct OverGroupAdd {
	};
	friend inline bool operator==(OverSticker a, OverSticker b) {
		return (a.section == b.section) && (a.index == b.index) && (a.overDelete == b.overDelete);
	}
	friend inline bool operator==(OverSet a, OverSet b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverButton a, OverButton b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverGroupAdd a, OverGroupAdd b) {
		return true;
	}
	using OverState = base::optional_variant<OverSticker, OverSet, OverButton, OverGroupAdd>;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};

	struct Set {
		Set(
			uint64 id,
			MTPDstickerSet::Flags flags,
			const QString &title,
			const QString &shortName,
			bool externalLayout,
			int count,
			const Stickers::Pack &pack = Stickers::Pack());
		Set(Set &&other);
		Set &operator=(Set &&other);
		~Set();

		uint64 id = 0;
		MTPDstickerSet::Flags flags = MTPDstickerSet::Flags();
		QString title;
		QString shortName;
		Stickers::Pack pack;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		bool externalLayout = false;
		int count = 0;
	};

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void displaySet(uint64 setId);
	void installSet(uint64 setId);
	void removeMegagroupSet(bool locally);
	void removeSet(uint64 setId);
	void sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input);
	void refreshSearchSets();
	void refreshSearchIndex();

	bool setHasTitle(const Set &set) const;
	bool stickerHasDeleteButton(const Set &set, int index) const;
	Stickers::Pack collectRecentStickers();
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
	void readVisibleSets();

	void paintFeaturedStickers(Painter &p, QRect clip);
	void paintStickers(Painter &p, QRect clip);
	void paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected, TimeMs ms);
	void paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected);
	void paintEmptySearchResults(Painter &p);

	int stickersRight() const;
	bool featuredHasAddButton(int index) const;
	QRect featuredAddRect(int index) const;
	bool hasRemoveButton(int index) const;
	QRect removeButtonRect(int index) const;
	int megagroupSetInfoLeft() const;
	void refreshMegagroupSetGeometry();
	QRect megagroupSetButtonRectFinal() const;

	enum class AppendSkip {
		None,
		Archived,
		Installed,
	};
	void appendSet(
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

	void cancelSetsSearch();
	void showSearchResults();
	void searchResultsDone(const MTPmessages_FoundStickerSets &result);
	void refreshSearchRows();
	void refreshSearchRows(const std::vector<uint64> *cloudSets);
	void fillLocalSearchRows(const QString &query);
	void fillCloudSearchRows(const std::vector<uint64> &cloudSets);
	void addSearchRow(not_null<const Stickers::Set*> set);

	ChannelData *_megagroupSet = nullptr;
	std::vector<Set> _mySets;
	std::vector<Set> _featuredSets;
	std::vector<Set> _searchSets;
	base::flat_set<uint64> _installedLocallySets;
	std::vector<bool> _custom;
	base::flat_set<not_null<DocumentData*>> _favedStickersMap;

	Section _section = Section::Stickers;

	uint64 _displayingSetId = 0;
	uint64 _removingSetId = 0;

	Footer *_footer = nullptr;
	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;

	OverState _selected;
	OverState _pressed;
	QPoint _lastMousePosition;

	Text _megagroupSetAbout;
	QString _megagroupSetButtonText;
	int _megagroupSetButtonTextWidth = 0;
	QRect _megagroupSetButtonRect;
	std::unique_ptr<Ui::RippleAnimation> _megagroupSetButtonRipple;

	QString _addText;
	int _addWidth;

	object_ptr<Ui::LinkButton> _settings;

	QTimer _previewTimer;
	bool _previewShown = false;

	std::map<QString, std::vector<uint64>> _searchCache;
	std::vector<std::pair<uint64, QStringList>> _searchIndex;
	base::Timer _searchRequestTimer;
	QString _searchQuery, _searchNextQuery;
	mtpRequestId _searchRequestId = 0;

};

} // namespace ChatHelpers
