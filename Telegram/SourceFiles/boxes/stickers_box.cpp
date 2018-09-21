/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stickers_box.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "chat_helpers/stickers.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "apiwrap.h"
#include "storage/localstorage.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/slide_animation.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/input_fields.h"
#include "auth_session.h"
#include "messenger.h"

namespace {

constexpr auto kArchivedLimitFirstRequest = 10;
constexpr auto kArchivedLimitPerPage = 30;
constexpr auto kHandleMegagroupSetAddressChangeTimeout = TimeMs(1000);

} // namespace

int stickerPacksCount(bool includeArchivedOfficial) {
	auto result = 0;
	auto &order = Auth().data().stickerSetsOrder();
	auto &sets = Auth().data().stickerSets();
	for (auto i = 0, l = order.size(); i < l; ++i) {
		auto it = sets.constFind(order.at(i));
		if (it != sets.cend()) {
			if (!(it->flags & MTPDstickerSet::Flag::f_archived) || ((it->flags & MTPDstickerSet::Flag::f_official) && includeArchivedOfficial)) {
				++result;
			}
		}
	}
	return result;
}

class StickersBox::CounterWidget : public Ui::RpWidget {
public:
	CounterWidget(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setCounter(int counter);

	QString _text;
	Dialogs::Layout::UnreadBadgeStyle _st;

};

StickersBox::CounterWidget::CounterWidget(QWidget *parent)
: RpWidget(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	_st.sizeId = Dialogs::Layout::UnreadBadgeInStickersBox;
	_st.textTop = st::stickersFeaturedBadgeTextTop;
	_st.size = st::stickersFeaturedBadgeSize;
	_st.padding = st::stickersFeaturedBadgePadding;
	_st.font = st::stickersFeaturedBadgeFont;

	Auth().data().featuredStickerSetsUnreadCountValue(
	) | rpl::start_with_next([this](int count) {
		setCounter(count);
		update();
	}, lifetime());
}

void StickersBox::CounterWidget::setCounter(int counter) {
	_text = (counter > 0) ? QString::number(counter) : QString();
	auto dummy = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	Painter p(&dummy);

	auto newWidth = 0;
	Dialogs::Layout::paintUnreadCount(p, _text, 0, 0, _st, &newWidth);

	resize(newWidth, st::stickersFeaturedBadgeSize);
}

void StickersBox::CounterWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_text.isEmpty()) {
		auto unreadRight = rtl() ? 0 : width();
		auto unreadTop = 0;
		Dialogs::Layout::paintUnreadCount(p, _text, unreadRight, unreadTop, _st);
	}
}

template <typename ...Args>
StickersBox::Tab::Tab(int index, Args&&... args)
: _index(index)
, _widget(std::forward<Args>(args)...)
, _weak(_widget) {
}

object_ptr<StickersBox::Inner> StickersBox::Tab::takeWidget() {
	return std::move(_widget);
}

void StickersBox::Tab::returnWidget(object_ptr<Inner> widget) {
	_widget = std::move(widget);
	Assert(_widget == _weak);
}

void StickersBox::Tab::saveScrollTop() {
	_scrollTop = widget()->getVisibleTop();
}

StickersBox::StickersBox(QWidget*, Section section)
: _tabs(this, st::stickersTabs)
, _unreadBadge(this)
, _section(section)
, _installed(0, this, Section::Installed)
, _featured(1, this, Section::Featured)
, _archived(2, this, Section::Archived) {
	_tabs->setRippleTopRoundRadius(st::boxRadius);
}

StickersBox::StickersBox(QWidget*, not_null<ChannelData*> megagroup)
: _section(Section::Installed)
, _installed(0, this, megagroup)
, _megagroupSet(megagroup) {
	subscribe(_installed.widget()->scrollToY, [this](int y) { onScrollToY(y); });
}

void StickersBox::getArchivedDone(uint64 offsetId, const MTPmessages_ArchivedStickers &result) {
	_archivedRequestId = 0;
	_archivedLoaded = true;
	if (result.type() != mtpc_messages_archivedStickers) {
		return;
	}

	auto &stickers = result.c_messages_archivedStickers();
	auto &archived = Auth().data().archivedStickerSetsOrderRef();
	if (offsetId) {
		auto index = archived.indexOf(offsetId);
		if (index >= 0) {
			archived = archived.mid(0, index + 1);
		}
	} else {
		archived.clear();
	}

	auto addedSet = false;
	auto changedSets = false;
	for_const (const auto &stickerSet, stickers.vsets.v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		}
		if (!setData) continue;

		if (auto set = Stickers::FeedSet(*setData)) {
			auto index = archived.indexOf(set->id);
			if (archived.isEmpty() || index != archived.size() - 1) {
				changedSets = true;
				if (index < archived.size() - 1) {
					archived.removeAt(index);
				}
				archived.push_back(set->id);
			}
			if (_archived.widget()->appendSet(*set)) {
				addedSet = true;
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					Auth().api().scheduleStickerSetRequest(set->id, set->access);
				}
			}
		}
	}
	if (addedSet) {
		_archived.widget()->updateSize();
	} else {
		_allArchivedLoaded = stickers.vsets.v.isEmpty()
			|| (!changedSets && offsetId != 0);
		if (changedSets) {
			loadMoreArchived();
		}
	}

	refreshTabs();
	_someArchivedLoaded = true;
	if (_section == Section::Archived && addedSet) {
		Auth().api().requestStickerSets();
	}
}

void StickersBox::prepare() {
	if (_section == Section::Installed) {
		if (_tabs) {
			Local::readArchivedStickers();
		} else {
			setTitle(langFactory(lng_stickers_group_set));
		}
	} else if (_section == Section::Archived) {
		requestArchivedSets();
	}
	if (_tabs) {
		if (Auth().data().archivedStickerSetsOrder().isEmpty()) {
			preloadArchivedSets();
		}
		setNoContentMargin(true);
		_tabs->sectionActivated(
		) | rpl::start_with_next(
			[this] { switchTab(); },
			lifetime());
		refreshTabs();
	}
	if (_installed.widget() && _section != Section::Installed) _installed.widget()->hide();
	if (_featured.widget() && _section != Section::Featured) _featured.widget()->hide();
	if (_archived.widget() && _section != Section::Archived) _archived.widget()->hide();

	if (_featured.widget()) {
		_featured.widget()->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
	}
	if (_archived.widget()) {
		_archived.widget()->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
		_archived.widget()->setLoadMoreCallback([this] { loadMoreArchived(); });
	}

	if (_megagroupSet) {
		addButton(langFactory(lng_settings_save), [this] { _installed.widget()->saveGroupSet(); closeBox(); });
		addButton(langFactory(lng_cancel), [this] { closeBox(); });
	} else {
		addButton(langFactory(lng_about_done), [this] { closeBox(); });
	}

	if (_section == Section::Installed) {
		_tab = &_installed;
	} else if (_section == Section::Archived) {
		_tab = &_archived;
	} else { // _section == Section::Featured
		_tab = &_featured;
	}
	setInnerWidget(_tab->takeWidget(), getTopSkip());
	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	Auth().data().stickersUpdated(
	) | rpl::start_with_next(
		[this] { handleStickersUpdated(); },
		lifetime());
	Auth().api().updateStickers();

	if (_installed.widget()) {
		connect(_installed.widget(), SIGNAL(draggingScrollDelta(int)), this, SLOT(onDraggingScrollDelta(int)));
		if (!_megagroupSet) {
			boxClosing() | rpl::start_with_next([=] {
				saveChanges();
			}, lifetime());
		}
	}

	if (_tabs) {
		_tabs->raise();
		_unreadBadge->raise();
	}
	rebuildList();
}

void StickersBox::refreshTabs() {
	if (!_tabs) return;

	_tabIndices.clear();
	auto sections = QStringList();
	sections.push_back(lang(lng_stickers_installed_tab).toUpper());
	_tabIndices.push_back(Section::Installed);
	if (!Auth().data().featuredStickerSetsOrder().isEmpty()) {
		sections.push_back(lang(lng_stickers_featured_tab).toUpper());
		_tabIndices.push_back(Section::Featured);
	}
	if (!Auth().data().archivedStickerSetsOrder().isEmpty()) {
		sections.push_back(lang(lng_stickers_archived_tab).toUpper());
		_tabIndices.push_back(Section::Archived);
	}
	_tabs->setSections(sections);
	if ((_tab == &_archived && !_tabIndices.contains(Section::Archived))
		|| (_tab == &_featured && !_tabIndices.contains(Section::Featured))) {
		switchTab();
	} else if (_tab == &_archived) {
		_tabs->setActiveSectionFast(_tabIndices.indexOf(Section::Archived));
	} else if (_tab == &_featured) {
		_tabs->setActiveSectionFast(_tabIndices.indexOf(Section::Featured));
	}
	updateTabsGeometry();
}

void StickersBox::loadMoreArchived() {
	if (_section != Section::Archived || _allArchivedLoaded || _archivedRequestId) {
		return;
	}

	uint64 lastId = 0;
	for (auto setIt = Auth().data().archivedStickerSetsOrder().cend(), e = Auth().data().archivedStickerSetsOrder().cbegin(); setIt != e;) {
		--setIt;
		auto it = Auth().data().stickerSets().constFind(*setIt);
		if (it != Auth().data().stickerSets().cend()) {
			if (it->flags & MTPDstickerSet::Flag::f_archived) {
				lastId = it->id;
				break;
			}
		}
	}
	_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(0), MTP_long(lastId), MTP_int(kArchivedLimitPerPage)), rpcDone(&StickersBox::getArchivedDone, lastId));
}

void StickersBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, 0, getTopSkip(), width(), getms());
		if (!_slideAnimation->animating()) {
			_slideAnimation.reset();
			setInnerVisible(true);
			update();
		}
	}
}

void StickersBox::updateTabsGeometry() {
	if (!_tabs) return;

	_tabs->resizeToWidth(_tabIndices.size() * width() / 3);
	_unreadBadge->setVisible(_tabIndices.contains(Section::Featured));

	setInnerTopSkip(getTopSkip());

	auto featuredLeft = width() / 3;
	auto featuredRight = 2 * width() / 3;
	auto featuredTextWidth = st::stickersTabs.labelFont->width(lang(lng_stickers_featured_tab).toUpper());
	auto featuredTextRight = featuredLeft + (featuredRight - featuredLeft - featuredTextWidth) / 2 + featuredTextWidth;
	auto unreadBadgeLeft = featuredTextRight - st::stickersFeaturedBadgeSkip;
	auto unreadBadgeTop = st::stickersFeaturedBadgeTop;
	if (unreadBadgeLeft + _unreadBadge->width() > featuredRight) {
		unreadBadgeLeft = featuredRight - _unreadBadge->width();
	}
	_unreadBadge->moveToLeft(unreadBadgeLeft, unreadBadgeTop);

	_tabs->moveToLeft(0, 0);
}

int StickersBox::getTopSkip() const {
	return _tabs ? (_tabs->height() - st::lineWidth) : 0;
}

void StickersBox::switchTab() {
	if (!_tabs) return;

	auto tab = _tabs->activeSection();
	Assert(tab >= 0 && tab < _tabIndices.size());
	auto newSection = _tabIndices[tab];

	auto newTab = _tab;
	if (newSection == Section::Installed) {
		newTab = &_installed;
	} else if (newSection == Section::Featured) {
		newTab = &_featured;
	} else if (newSection == Section::Archived) {
		newTab = &_archived;
		requestArchivedSets();
	}
	if (_tab == newTab) {
		return;
	}

	if (_tab == &_installed) {
		_localOrder = _tab->widget()->getFullOrder();
		_localRemoved = _tab->widget()->getRemovedSets();
	}
	auto wasCache = grabContentCache();
	auto wasIndex = _tab->index();
	_tab->saveScrollTop();
	auto widget = takeInnerWidget<Inner>();
	widget->setParent(this);
	widget->hide();
	_tab->returnWidget(std::move(widget));
	_tab = newTab;
	_section = newSection;
	setInnerWidget(_tab->takeWidget(), getTopSkip());
	_tabs->raise();
	_unreadBadge->raise();
	_tab->widget()->show();
	rebuildList();
	onScrollToY(_tab->getScrollTop());
	setInnerVisible(true);
	auto nowCache = grabContentCache();
	auto nowIndex = _tab->index();

	_slideAnimation = std::make_unique<Ui::SlideAnimation>();
	_slideAnimation->setSnapshots(std::move(wasCache), std::move(nowCache));
	auto slideLeft = wasIndex > nowIndex;
	_slideAnimation->start(slideLeft, [this] { update(); }, st::slideDuration);
	setInnerVisible(false);

	setFocus();
	update();
}

QPixmap StickersBox::grabContentCache() {
	_tabs->hide();
	auto result = grabInnerCache();
	_tabs->show();
	return result;
}

void StickersBox::installSet(uint64 setId) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
		return;
	}

	if (_localRemoved.contains(setId)) {
		_localRemoved.removeOne(setId);
		if (_installed.widget()) _installed.widget()->setRemovedSets(_localRemoved);
		if (_featured.widget()) _featured.widget()->setRemovedSets(_localRemoved);
		_archived.widget()->setRemovedSets(_localRemoved);
	}
	if (!(it->flags & MTPDstickerSet::Flag::f_installed_date)
		|| (it->flags & MTPDstickerSet::Flag::f_archived)) {
		MTP::send(
			MTPmessages_InstallStickerSet(
				Stickers::inputSetId(*it),
				MTP_boolFalse()),
			rpcDone(&StickersBox::installDone),
			rpcFail(&StickersBox::installFail, setId));

		Stickers::InstallLocally(setId);
	}
}

void StickersBox::installDone(const MTPmessages_StickerSetInstallResult &result) {
	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		Stickers::ApplyArchivedResult(
			result.c_messages_stickerSetInstallResultArchive());
	}
}

bool StickersBox::installFail(uint64 setId, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
		return true;
	}

	Stickers::UndoInstallLocally(setId);
	return true;
}

void StickersBox::preloadArchivedSets() {
	if (!_tabs) return;
	if (!_archivedRequestId) {
		_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(0), MTP_long(0), MTP_int(kArchivedLimitFirstRequest)), rpcDone(&StickersBox::getArchivedDone, 0ULL));
	}
}

void StickersBox::requestArchivedSets() {
	// Reload the archived list.
	if (!_archivedLoaded) {
		preloadArchivedSets();
	}

	auto &sets = Auth().data().stickerSets();
	for_const (auto setId, Auth().data().archivedStickerSetsOrder()) {
		auto it = sets.constFind(setId);
		if (it != sets.cend()) {
			if (it->stickers.isEmpty() && (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				Auth().api().scheduleStickerSetRequest(setId, it->access);
			}
		}
	}
	Auth().api().requestStickerSets();
}

void StickersBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_tabs) {
		updateTabsGeometry();
	}
	if (_titleShadow) {
		_titleShadow->setGeometry(0, 0, width(), st::lineWidth);
	}
	if (_installed.widget()) _installed.widget()->resize(width(), _installed.widget()->height());
	if (_featured.widget()) _featured.widget()->resize(width(), _featured.widget()->height());
	if (_archived.widget()) _archived.widget()->resize(width(), _archived.widget()->height());
}

void StickersBox::handleStickersUpdated() {
	if (_section == Section::Installed || _section == Section::Featured) {
		rebuildList();
	} else {
		_tab->widget()->updateRows();
	}
	if (Auth().data().archivedStickerSetsOrder().isEmpty()) {
		preloadArchivedSets();
	} else {
		refreshTabs();
	}
}

void StickersBox::rebuildList(Tab *tab) {
	if (!tab) tab = _tab;

	if (tab == &_installed) {
		_localOrder = tab->widget()->getFullOrder();
		_localRemoved = tab->widget()->getRemovedSets();
	}
	tab->widget()->rebuild();
	if (tab == &_installed) {
		tab->widget()->setFullOrder(_localOrder);
	}
	tab->widget()->setRemovedSets(_localRemoved);
}

void StickersBox::saveChanges() {
	// Make sure that our changes in other tabs are applied in the Installed tab.
	rebuildList(&_installed);

	if (_someArchivedLoaded) {
		Local::writeArchivedStickers();
	}
	if (AuthSession::Exists()) {
		Auth().api().saveStickerSets(_installed.widget()->getOrder(), _installed.widget()->getRemovedSets());
	}
}

void StickersBox::setInnerFocus() {
	if (_megagroupSet) {
		_installed.widget()->setInnerFocus();
	}
}

StickersBox::~StickersBox() = default;

StickersBox::Inner::Row::Row(
	uint64 id,
	uint64 accessHash,
	DocumentData *sticker,
	int32 count,
	const QString &title,
	int titleWidth,
	bool installed,
	bool official,
	bool unread,
	bool archived,
	bool removed,
	int32 pixw,
	int32 pixh)
: id(id)
, accessHash(accessHash)
, sticker(sticker)
, count(count)
, title(title)
, titleWidth(titleWidth)
, installed(installed)
, official(official)
, unread(unread)
, archived(archived)
, removed(removed)
, pixw(pixw)
, pixh(pixh) {
}

StickersBox::Inner::Row::~Row() = default;

StickersBox::Inner::Inner(QWidget *parent, StickersBox::Section section) : TWidget(parent)
, _section(section)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersMarginTop)
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _undoText(lang(lng_stickers_return).toUpper())
, _undoWidth(st::stickersUndoRemove.font->width(_undoText)) {
	setup();
}

StickersBox::Inner::Inner(QWidget *parent, not_null<ChannelData*> megagroup) : TWidget(parent)
, _section(StickersBox::Section::Installed)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersMarginTop)
, _megagroupSet(megagroup)
, _megagroupSetInput(_megagroupSet->mgInfo->stickerSet)
, _megagroupSetField(this, st::groupStickersField, [] { return qsl("stickerset"); }, QString(), true)
, _megagroupDivider(this)
, _megagroupSubTitle(this, lang(lng_stickers_group_from_your), Ui::FlatLabel::InitType::Simple, st::boxTitle) {
	_megagroupSetField->setLinkPlaceholder(Messenger::Instance().createInternalLink(qsl("addstickers/")));
	_megagroupSetField->setPlaceholderHidden(false);
	_megagroupSetAddressChangedTimer.setCallback([this] { handleMegagroupSetAddressChange(); });
	connect(
		_megagroupSetField,
		&Ui::MaskedInputField::changed,
		[=] {
			_megagroupSetAddressChangedTimer.callOnce(
				kHandleMegagroupSetAddressChangeTimeout);
		});
	connect(
		_megagroupSetField,
		&Ui::MaskedInputField::submitted,
		[=] {
			_megagroupSetAddressChangedTimer.cancel();
			handleMegagroupSetAddressChange();
		});

	setup();
}

void StickersBox::Inner::setup() {
	subscribe(Auth().downloaderTaskFinished(), [this] {
		update();
		readVisibleSets();
	});
	setMouseTracking(true);
}

void StickersBox::Inner::setInnerFocus() {
	if (_megagroupSetField) {
		_megagroupSetField->setFocusFast();
	}
}

void StickersBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_a_shifting.animating()) {
		_a_shifting.step();
	}

	auto clip = e->rect();
	auto ms = getms();
	p.fillRect(clip, st::boxBg);
	p.setClipRect(clip);

	if (_megagroupSelectedSet) {
		auto setTop = _megagroupDivider->y() - _rowHeight;
		p.translate(0, setTop);
		paintRow(p, _megagroupSelectedSet.get(), -1, ms);
		p.translate(0, -setTop);
	}

	auto y = _itemsTop;
	if (_rows.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, y, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		p.translate(0, _itemsTop);

		int32 yFrom = clip.y() - _itemsTop, yTo = clip.y() + clip.height() - _itemsTop;
		int32 from = floorclamp(yFrom - _rowHeight, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo + _rowHeight, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (int32 i = from; i < to; ++i) {
			if (i != _above) {
				paintRow(p, _rows[i].get(), i, ms);
			}
			p.translate(0, _rowHeight);
		}
		if (from <= _above && _above < to) {
			p.translate(0, (_above - to) * _rowHeight);
			paintRow(p, _rows[_above].get(), _above, ms);
		}
	}
}

void StickersBox::Inner::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void StickersBox::Inner::updateControlsGeometry() {
	if (_megagroupSet) {
		auto top = st::groupStickersFieldPadding.top();
		auto fieldLeft = st::boxLayerTitlePosition.x();
		_megagroupSetField->setGeometryToLeft(fieldLeft, top, width() - fieldLeft - st::groupStickersFieldPadding.right(), _megagroupSetField->height());
		top += _megagroupSetField->height() + st::groupStickersFieldPadding.bottom();
		if (_megagroupSelectedRemove) {
			_megagroupSelectedShadow->setGeometryToLeft(0, top, width(), st::lineWidth);
			top += st::lineWidth;
			_megagroupSelectedRemove->moveToRight(st::groupStickersRemovePosition.x(), top + st::groupStickersRemovePosition.y());
			top += _rowHeight;
		}
		_megagroupDivider->setGeometryToLeft(0, top, width(), _megagroupDivider->height());
		top += _megagroupDivider->height();
		_megagroupSubTitle->resizeToNaturalWidth(width() - 2 * st::boxLayerTitlePosition.x());
		_megagroupSubTitle->moveToLeft(st::boxLayerTitlePosition.x(), top + st::boxLayerTitlePosition.y());
	}
}

QRect StickersBox::Inner::relativeButtonRect(bool removeButton) const {
	auto buttonw = st::stickersRemove.width;
	auto buttonh = st::stickersRemove.height;
	auto buttonshift = st::stickersRemoveSkip;
	if (!removeButton) {
		auto &st = (_section == Section::Installed) ? st::stickersUndoRemove : st::stickersTrendingAdd;
		auto textWidth = (_section == Section::Installed) ? _undoWidth : _addWidth;
		buttonw = textWidth - st.width;
		buttonh = st.height;
		buttonshift = 0;
	}
	auto buttonx = width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - buttonw + buttonshift;
	auto buttony = st::contactsPadding.top() + (st::contactsPhotoSize - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

void StickersBox::Inner::paintRow(Painter &p, Row *set, int index, TimeMs ms) {
	auto xadd = 0, yadd = qRound(set->yadd.current());
	if (xadd || yadd) p.translate(xadd, yadd);

	if (_megagroupSet) {
		auto selectedIndex = [&] {
			if (auto index = base::get_if<int>(&_selected)) {
				return *index;
			}
			return -1;
		}();
		if (index >= 0 && index == selectedIndex) {
			p.fillRect(0, 0, width(), _rowHeight, st::contactsBgOver);
			if (set->ripple) {
				set->ripple->paint(p, 0, 0, width(), ms);
			}
		}
	}

	if (_section == Section::Installed) {
		if (index >= 0 && index == _above) {
			auto current = _aboveShadowFadeOpacity.current();
			if (_started >= 0) {
				auto reachedOpacity = aboveShadowOpacity();
				if (reachedOpacity > current) {
					_aboveShadowFadeOpacity = anim::value(reachedOpacity, reachedOpacity);
					current = reachedOpacity;
				}
			}
			auto row = myrtlrect(st::contactsPadding.left() / 2, st::contactsPadding.top() / 2, width() - (st::contactsPadding.left() / 2) - _scrollbar - st::contactsPadding.left() / 2, _rowHeight - ((st::contactsPadding.top() + st::contactsPadding.bottom()) / 2));
			p.setOpacity(current);
			Ui::Shadow::paint(p, row, width(), st::boxRoundShadow);
			p.setOpacity(1);

			App::roundRect(p, row, st::boxBg, BoxCorners);

			p.setOpacity(1. - current);
			paintFakeButton(p, set, index, ms);
			p.setOpacity(1.);
		} else if (!_megagroupSet) {
			paintFakeButton(p, set, index, ms);
		}
	} else if (!_megagroupSet) {
		paintFakeButton(p, set, index, ms);
	}

	if (set->removed && _section == Section::Installed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	auto stickerx = st::contactsPadding.left();

	if (!_megagroupSet && _section == Section::Installed) {
		stickerx += st::stickersReorderIcon.width() + st::stickersReorderSkip;
		if (!set->isRecentSet()) {
			st::stickersReorderIcon.paint(p, st::contactsPadding.left(), (_rowHeight - st::stickersReorderIcon.height()) / 2, width());
		}
	}

	if (set->sticker) {
		const auto origin = Data::FileOriginStickerSet(
			set->id,
			set->accessHash);
		set->sticker->thumb->load(origin);
		auto pix = set->sticker->thumb->pix(origin, set->pixw, set->pixh);
		p.drawPixmapLeft(stickerx + (st::contactsPhotoSize - set->pixw) / 2, st::contactsPadding.top() + (st::contactsPhotoSize - set->pixh) / 2, width(), pix);
	}

	int namex = stickerx + st::contactsPhotoSize + st::contactsPadding.left();
	int namey = st::contactsPadding.top() + st::contactsNameTop;

	int statusx = namex;
	int statusy = st::contactsPadding.top() + st::contactsStatusTop;

	p.setFont(st::contactsNameStyle.font);
	p.setPen(st::contactsNameFg);
	p.drawTextLeft(namex, namey, width(), set->title, set->titleWidth);

	if (set->unread) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::stickersFeaturedUnreadBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(rtlrect(namex + set->titleWidth + st::stickersFeaturedUnreadSkip, namey + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
		}
	}

	auto statusText = (set->count > 0) ? lng_stickers_count(lt_count, set->count) : lang(lng_contacts_loading);

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(statusx, statusy, width(), statusText);

	p.setOpacity(1);
	if (xadd || yadd) p.translate(-xadd, -yadd);
}

void StickersBox::Inner::paintFakeButton(Painter &p, Row *set, int index, TimeMs ms) {
	auto removeButton = (_section == Section::Installed && !set->removed);
	auto rect = relativeButtonRect(removeButton);
	if (_section != Section::Installed && set->installed && !set->archived && !set->removed) {
		// Checkbox after installed from Trending or Archived.
		int checkx = width() - (st::contactsPadding.right() + st::contactsCheckPosition.x() + (rect.width() + st::stickersFeaturedInstalled.width()) / 2);
		int checky = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersFeaturedInstalled.height()) / 2;
		st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
	} else {
		auto selected = (index == _actionSel && _actionDown < 0) || (index == _actionDown);
		if (removeButton) {
			// Trash icon button when not disabled in Installed.
			if (set->ripple) {
				set->ripple->paint(p, rect.x(), rect.y(), width(), ms);
				if (set->ripple->empty()) {
					set->ripple.reset();
				}
			}
			auto &icon = selected ? st::stickersRemove.iconOver : st::stickersRemove.icon;
			auto position = st::stickersRemove.iconPosition;
			if (position.x() < 0) position.setX((rect.width() - icon.width()) / 2);
			if (position.y() < 0) position.setY((rect.height() - icon.height()) / 2);
			icon.paint(p, rect.topLeft() + position, ms);
		} else {
			// Round button ADD when not installed from Trending or Archived.
			// Or round button UNDO after disabled from Installed.
			auto &st = (_section == Section::Installed) ? st::stickersUndoRemove : st::stickersTrendingAdd;
			auto textWidth = (_section == Section::Installed) ? _undoWidth : _addWidth;
			auto &text = (_section == Section::Installed) ? _undoText : _addText;
			auto &textBg = selected ? st.textBgOver : st.textBg;
			App::roundRect(p, myrtlrect(rect), textBg, ImageRoundRadius::Small);
			if (set->ripple) {
				set->ripple->paint(p, rect.x(), rect.y(), width(), ms);
				if (set->ripple->empty()) {
					set->ripple.reset();
				}
			}
			p.setFont(st.font);
			p.setPen(selected ? st.textFgOver : st.textFg);
			p.drawTextLeft(rect.x() - (st.width / 2), rect.y() + st.textTop, width(), text, textWidth);
		}
	}
}

void StickersBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_dragging >= 0) mouseReleaseEvent(e);
	_mouse = e->globalPos();
	onUpdateSelected();

	setPressed(_selected);
	if (_actionSel >= 0) {
		setActionDown(_actionSel);
		update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
	} else if (auto selectedIndex = base::get_if<int>(&_selected)) {
		if (_section == Section::Installed && !_rows[*selectedIndex]->isRecentSet() && _inDragArea) {
			_above = _dragging = _started = *selectedIndex;
			_dragStart = mapFromGlobal(_mouse);
		}
	}
}

void StickersBox::Inner::setActionDown(int newActionDown) {
	if (_actionDown == newActionDown) {
		return;
	}
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		auto &set = _rows[_actionDown];
		if (set->ripple) {
			set->ripple->lastStop();
		}
	}
	_actionDown = newActionDown;
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		auto &set = _rows[_actionDown];
		auto removeButton = (_section == Section::Installed && !set->removed);
		if (!set->ripple) {
			if (_section == Section::Installed) {
				if (set->removed) {
					auto rippleSize = QSize(_undoWidth - st::stickersUndoRemove.width, st::stickersUndoRemove.height);
					auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::buttonRadius);
					ensureRipple(st::stickersUndoRemove.ripple, std::move(rippleMask), removeButton);
				} else {
					auto rippleSize = st::stickersRemove.rippleAreaSize;
					auto rippleMask = Ui::RippleAnimation::ellipseMask(QSize(rippleSize, rippleSize));
					ensureRipple(st::stickersRemove.ripple, std::move(rippleMask), removeButton);
				}
			} else if (!set->installed || set->archived || set->removed) {
				auto rippleSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
				auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::buttonRadius);
				ensureRipple(st::stickersTrendingAdd.ripple, std::move(rippleMask), removeButton);
			}
		}
		if (set->ripple) {
			auto rect = relativeButtonRect(removeButton);
			set->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(myrtlrect(rect).x(), _itemsTop + _actionDown * _rowHeight + rect.y()));
		}
	}
}

void StickersBox::Inner::setSelected(SelectedRow selected) {
	if (_selected == selected) {
		return;
	}
	if ((_megagroupSet || _section != Section::Installed)
		&& ((_selected.has_value() || _pressed.has_value()) != (selected.has_value() || _pressed.has_value()))) {
		if (!_inDragArea) {
			setCursor((selected.has_value() || _pressed.has_value())
				? style::cur_pointer
				: style::cur_default);
		}
	}
	auto countSelectedIndex = [&] {
		if (auto index = base::get_if<int>(&_selected)) {
			return *index;
		}
		return -1;
	};
	auto selectedIndex = countSelectedIndex();
	if (_megagroupSet && selectedIndex >= 0 && selectedIndex < _rows.size()) {
		update(0, _itemsTop + selectedIndex * _rowHeight, width(), _rowHeight);
	}
	_selected = selected;
	selectedIndex = countSelectedIndex();
	if (_megagroupSet && selectedIndex >= 0 && selectedIndex < _rows.size()) {
		update(0, _itemsTop + selectedIndex * _rowHeight, width(), _rowHeight);
	}
}

void StickersBox::Inner::setPressed(SelectedRow pressed) {
	if (_pressed == pressed) {
		return;
	}
	auto countPressedIndex = [&] {
		if (auto index = base::get_if<int>(&_pressed)) {
			return *index;
		}
		return -1;
	};
	auto pressedIndex = countPressedIndex();
	if (_megagroupSet && pressedIndex >= 0 && pressedIndex < _rows.size()) {
		update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
		auto &set = _rows[pressedIndex];
		if (set->ripple) {
			set->ripple->lastStop();
		}
	}
	_pressed = pressed;
	pressedIndex = countPressedIndex();
	if (_megagroupSet && pressedIndex >= 0 && pressedIndex < _rows.size()) {
		update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
		auto &set = _rows[pressedIndex];
		auto rippleMask = Ui::RippleAnimation::rectMask(QSize(width(), _rowHeight));
		if (!_rows[pressedIndex]->ripple) {
			_rows[pressedIndex]->ripple = std::make_unique<Ui::RippleAnimation>(st::contactsRipple, std::move(rippleMask), [this, pressedIndex] {
				update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
			});
		}
		_rows[pressedIndex]->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, _itemsTop + pressedIndex * _rowHeight));
	}
}

void StickersBox::Inner::ensureRipple(const style::RippleAnimation &st, QImage mask, bool removeButton) {
	_rows[_actionDown]->ripple = std::make_unique<Ui::RippleAnimation>(st, std::move(mask), [this, index = _actionDown, removeButton] {
		update(myrtlrect(relativeButtonRect(removeButton).translated(0, _itemsTop + index * _rowHeight)));
	});
}

void StickersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouse = e->globalPos();
	onUpdateSelected();
}

void StickersBox::Inner::onUpdateSelected() {
	auto local = mapFromGlobal(_mouse);
	if (_dragging >= 0) {
		auto shift = 0;
		auto ms = getms();
		int firstSetIndex = 0;
		if (_rows.at(firstSetIndex)->isRecentSet()) {
			++firstSetIndex;
		}
		if (_dragStart.y() > local.y() && _dragging > 0) {
			shift = -floorclamp(_dragStart.y() - local.y() + (_rowHeight / 2), _rowHeight, 0, _dragging - firstSetIndex);
			for (int32 from = _dragging, to = _dragging + shift; from > to; --from) {
				qSwap(_rows[from], _rows[from - 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() - _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		} else if (_dragStart.y() < local.y() && _dragging + 1 < _rows.size()) {
			shift = floorclamp(local.y() - _dragStart.y() + (_rowHeight / 2), _rowHeight, 0, _rows.size() - _dragging - 1);
			for (int32 from = _dragging, to = _dragging + shift; from < to; ++from) {
				qSwap(_rows[from], _rows[from + 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() + _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		}
		if (shift) {
			_dragging += shift;
			_above = _dragging;
			_dragStart.setY(_dragStart.y() + shift * _rowHeight);
			if (!_a_shifting.animating()) {
				_a_shifting.start();
			}
		}
		_rows[_dragging]->yadd = anim::value(local.y() - _dragStart.y(), local.y() - _dragStart.y());
		_animStartTimes[_dragging] = 0;
		_a_shifting.step(ms, true);

		auto countDraggingScrollDelta = [this, local] {
			if (local.y() < _visibleTop) {
				return local.y() - _visibleTop;
			} else if (local.y() >= _visibleBottom) {
				return local.y() + 1 - _visibleBottom;
			}
			return 0;
		};

		emit draggingScrollDelta(countDraggingScrollDelta());
	} else {
		bool in = rect().marginsRemoved(QMargins(0, _itemsTop, 0, st::membersMarginBottom)).contains(local);
		auto selected = SelectedRow();
		auto actionSel = -1;
		auto inDragArea = false;
		if (in && !_rows.empty()) {
			auto selectedIndex = floorclamp(local.y() - _itemsTop, _rowHeight, 0, _rows.size() - 1);
			selected = selectedIndex;
			local.setY(local.y() - _itemsTop - selectedIndex * _rowHeight);
			auto &set = _rows[selectedIndex];
			if (!_megagroupSet && (_section == Section::Installed || !set->installed || set->archived || set->removed)) {
				auto removeButton = (_section == Section::Installed && !set->removed);
				auto rect = myrtlrect(relativeButtonRect(removeButton));
				actionSel = rect.contains(local) ? selectedIndex : -1;
			} else {
				actionSel = -1;
			}
			if (!_megagroupSet && _section == Section::Installed && !set->isRecentSet()) {
				auto dragAreaWidth = st::contactsPadding.left() + st::stickersReorderIcon.width() + st::stickersReorderSkip;
				auto dragArea = myrtlrect(0, 0, dragAreaWidth, _rowHeight);
				inDragArea = dragArea.contains(local);
			}
		} else if (_megagroupSelectedSet) {
			auto setTop = _megagroupDivider->y() - _rowHeight;
			if (QRect(0, setTop, width(), _rowHeight).contains(local)) {
				selected = MegagroupSet();
			}
		}
		setSelected(selected);
		if (_inDragArea != inDragArea) {
			_inDragArea = inDragArea;
			setCursor(_inDragArea
				? style::cur_sizeall
				: ((_selected.has_value() || _pressed.has_value())
					? style::cur_pointer
					: style::cur_default));
		}
		setActionSel(actionSel);
		emit draggingScrollDelta(0);
	}
}

float64 StickersBox::Inner::aboveShadowOpacity() const {
	if (_above < 0) return 0;

	auto dx = 0;
	auto dy = qAbs(_above * _rowHeight + qRound(_rows[_above]->yadd.current()) - _started * _rowHeight);
	return qMin((dx + dy)  * 2. / _rowHeight, 1.);
}

void StickersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = std::exchange(_pressed, SelectedRow());

	if (_section != Section::Installed && !_selected.has_value() && pressed.has_value()) {
		setCursor(style::cur_default);
	}

	_mouse = e->globalPos();
	onUpdateSelected();
	if (_actionDown == _actionSel && _actionSel >= 0) {
		if (_section == Section::Installed) {
			setRowRemoved(_actionDown, !_rows[_actionDown]->removed);
		} else if (_installSetCallback) {
			_installSetCallback(_rows[_actionDown]->id);
		}
	} else if (_dragging >= 0) {
		QPoint local(mapFromGlobal(_mouse));
		_rows[_dragging]->yadd.start(0.);
		_aboveShadowFadeStart = _animStartTimes[_dragging] = getms();
		_aboveShadowFadeOpacity = anim::value(aboveShadowOpacity(), 0);
		if (!_a_shifting.animating()) {
			_a_shifting.start();
		}

		_dragging = _started = -1;
	} else if (pressed == _selected && _actionSel < 0 && _actionDown < 0) {
		auto selectedIndex = [&] {
			if (auto index = base::get_if<int>(&_selected)) {
				return *index;
			}
			return -1;
		}();
		auto getSetByRow = [&](const Row &row) -> const Stickers::Set* {
			auto &sets = Auth().data().stickerSetsRef();
			if (!row.isRecentSet()) {
				auto it = sets.find(row.id);
				if (it != sets.cend()) {
					return &*it;
				}
			}
			return nullptr;
		};
		auto showSetByRow = [&](const Row &row) {
			if (auto set = getSetByRow(row)) {
				setSelected(SelectedRow());
				Ui::show(
					Box<StickerSetBox>(Stickers::inputSetId(*set)),
					LayerOption::KeepOther);
			}
		};
		if (selectedIndex >= 0 && !_inDragArea) {
			auto &row = *_rows[selectedIndex];
			if (_megagroupSet) {
				if (auto set = getSetByRow(row)) {
					setMegagroupSelectedSet(MTP_inputStickerSetID(
						MTP_long(set->id),
						MTP_long(set->access)));
				}
			} else {
				showSetByRow(row);
			}
		} else if (_megagroupSelectedSet && _selected.is<MegagroupSet>()) {
			showSetByRow(*_megagroupSelectedSet);
		}
	}
	setActionDown(-1);
}

void StickersBox::Inner::saveGroupSet() {
	Expects(_megagroupSet != nullptr);
	auto oldId = (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID) ? _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID().vid.v : 0;
	auto newId = (_megagroupSetInput.type() == mtpc_inputStickerSetID) ? _megagroupSetInput.c_inputStickerSetID().vid.v : 0;
	if (newId != oldId) {
		Auth().api().setGroupStickerSet(_megagroupSet, _megagroupSetInput);
		Auth().api().stickerSetInstalled(Stickers::MegagroupSetId);
	}
}

void StickersBox::Inner::setRowRemoved(int index, bool removed) {
	auto &row = _rows[index];
	if (row->removed != removed) {
		row->removed = removed;
		row->ripple.reset();
		update(0, _itemsTop + index * _rowHeight, width(), _rowHeight);
		onUpdateSelected();
	}
}

void StickersBox::Inner::leaveEventHook(QEvent *e) {
	_mouse = QPoint(-1, -1);
	onUpdateSelected();
}

void StickersBox::Inner::leaveToChildEvent(QEvent *e, QWidget *child) {
	_mouse = QPoint(-1, -1);
	onUpdateSelected();
}

void StickersBox::Inner::step_shifting(TimeMs ms, bool timer) {
	if (anim::Disabled()) {
		ms += st::stickersRowDuration;
	}
	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, l = _animStartTimes.size(); i < l; ++i) {
		auto start = _animStartTimes.at(i);
		if (start) {
			if (updateMin < 0) updateMin = i;
			updateMax = i;
			if (start + st::stickersRowDuration > ms && ms >= start) {
				_rows[i]->yadd.update(float64(ms - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_rows[i]->yadd.finish();
				_animStartTimes[i] = 0;
			}
		}
	}
	if (_aboveShadowFadeStart) {
		if (updateMin < 0 || updateMin > _above) updateMin = _above;
		if (updateMax < _above) updateMin = _above;
		if (_aboveShadowFadeStart + st::stickersRowDuration > ms && ms > _aboveShadowFadeStart) {
			_aboveShadowFadeOpacity.update(float64(ms - _aboveShadowFadeStart) / st::stickersRowDuration, anim::sineInOut);
			animating = true;
		} else {
			_aboveShadowFadeOpacity.finish();
			_aboveShadowFadeStart = 0;
		}
	}
	if (timer) {
		if (_dragging >= 0) {
			if (updateMin < 0 || updateMin > _dragging) {
				updateMin = _dragging;
			}
			if (updateMax < _dragging) updateMax = _dragging;
		}
		if (updateMin == 1 && _rows[0]->isRecentSet()) {
			updateMin = 0; // Repaint from the very top of the content.
		}
		if (updateMin >= 0) {
			update(0, _itemsTop + _rowHeight * (updateMin - 1), width(), _rowHeight * (updateMax - updateMin + 3));
		}
	}
	if (!animating) {
		_above = _dragging;
		_a_shifting.stop();
	}
}

void StickersBox::Inner::clear() {
	_rows.clear();
	_animStartTimes.clear();
	_aboveShadowFadeStart = 0;
	_aboveShadowFadeOpacity = anim::value();
	_a_shifting.stop();
	_above = _dragging = _started = -1;
	setSelected(SelectedRow());
	setPressed(SelectedRow());
	setActionSel(-1);
	setActionDown(-1);
	update();
}

void StickersBox::Inner::setActionSel(int32 actionSel) {
	if (actionSel != _actionSel) {
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		_actionSel = actionSel;
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		if (_section == Section::Installed) {
			setCursor((_actionSel >= 0 && (_actionDown < 0 || _actionDown == _actionSel)) ? style::cur_pointer : style::cur_default);
		}
	}
}

void StickersBox::Inner::AddressField::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newText = now;
	auto newCursor = nowCursor;
	auto removeFromBeginning = {
		qstr("http://"),
		qstr("https://"),
		qstr("www.t.me/"),
		qstr("www.telegram.me/"),
		qstr("www.telegram.dog/"),
		qstr("t.me/"),
		qstr("telegram.me/"),
		qstr("telegram.dog/"),
		qstr("addstickers/"),
	};
	for (auto &removePhrase : removeFromBeginning) {
		if (newText.startsWith(removePhrase)) {
			newText = newText.mid(removePhrase.size());
			newCursor = newText.size();
		}
	}
	setCorrectedText(now, nowCursor, newText, newCursor);
}

void StickersBox::Inner::handleMegagroupSetAddressChange() {
	auto text = _megagroupSetField->getLastText().trimmed();
	if (text.isEmpty()) {
		if (_megagroupSelectedSet) {
			auto it = Auth().data().stickerSets().constFind(_megagroupSelectedSet->id);
			if (it != Auth().data().stickerSets().cend() && !it->shortName.isEmpty()) {
				setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
			}
		}
	} else if (!_megagroupSetRequestId) {
		_megagroupSetRequestId = request(MTPmessages_GetStickerSet(MTP_inputStickerSetShortName(MTP_string(text)))).done([this](const MTPmessages_StickerSet &result) {
			_megagroupSetRequestId = 0;
			auto set = Stickers::FeedSetFull(result);
			setMegagroupSelectedSet(MTP_inputStickerSetID(MTP_long(set->id), MTP_long(set->access)));
		}).fail([this](const RPCError &error) {
			_megagroupSetRequestId = 0;
			setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
		}).send();
	} else {
		_megagroupSetAddressChangedTimer.callOnce(kHandleMegagroupSetAddressChangeTimeout);
	}
}

void StickersBox::Inner::rebuildMegagroupSet() {
	Expects(_megagroupSet != nullptr);
	if (_megagroupSetInput.type() != mtpc_inputStickerSetID) {
		if (_megagroupSelectedSet) {
			_megagroupSetField->setText(QString());
			_megagroupSetField->finishAnimating();
		}
		_megagroupSelectedSet.reset();
		_megagroupSelectedRemove.destroy();
		_megagroupSelectedShadow.destroy();
		return;
	}
	auto &set = _megagroupSetInput.c_inputStickerSetID();
	auto setId = set.vid.v;
	auto &sets = Auth().data().stickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
		Auth().api().scheduleStickerSetRequest(set.vid.v, set.vaccess_hash.v);
		return;
	}

	auto maxNameWidth = countMaxNameWidth();
	auto titleWidth = 0;
	auto title = fillSetTitle(*it, maxNameWidth, &titleWidth);
	auto count = fillSetCount(*it);
	auto sticker = (DocumentData*)nullptr;
	auto pixw = 0, pixh = 0;
	fillSetCover(*it, &sticker, &pixw, &pixh);
	auto installed = true, official = false, unread = false, archived = false, removed = false;
	if (!_megagroupSelectedSet || _megagroupSelectedSet->id != it->id) {
		_megagroupSetField->setText(it->shortName);
		_megagroupSetField->finishAnimating();
	}
	_megagroupSelectedSet = std::make_unique<Row>(
		it->id,
		it->access,
		sticker,
		count,
		title,
		titleWidth,
		installed,
		official,
		unread,
		archived,
		removed,
		pixw,
		pixh);
	_itemsTop += st::lineWidth + _rowHeight;

	if (!_megagroupSelectedRemove) {
		_megagroupSelectedRemove.create(this, st::groupStickersRemove);
		_megagroupSelectedRemove->show(anim::type::instant);
		_megagroupSelectedRemove->setClickedCallback([this] {
			setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
		});
		_megagroupSelectedShadow.create(this);
		updateControlsGeometry();
	}
}

void StickersBox::Inner::rebuild() {
	_itemsTop = st::membersMarginTop;

	if (_megagroupSet) {
		_itemsTop += st::groupStickersFieldPadding.top() + _megagroupSetField->height() + st::groupStickersFieldPadding.bottom();
		_itemsTop += _megagroupDivider->height() + st::groupStickersSubTitleHeight;
		rebuildMegagroupSet();
	}

	auto maxNameWidth = countMaxNameWidth();

	clear();
	auto &order = ([&]() -> const Stickers::Order & {
		if (_section == Section::Installed) {
			auto &result = Auth().data().stickerSetsOrder();
			if (_megagroupSet && result.empty()) {
				return Auth().data().featuredStickerSetsOrder();
			}
			return result;
		} else if (_section == Section::Featured) {
			return Auth().data().featuredStickerSetsOrder();
		}
		return Auth().data().archivedStickerSetsOrder();
	})();
	_rows.reserve(order.size() + 1);
	_animStartTimes.reserve(order.size() + 1);

	auto &sets = Auth().data().stickerSets();
	if (_megagroupSet) {
		auto usingFeatured = Auth().data().stickerSetsOrder().empty();
		_megagroupSubTitle->setText(lang(usingFeatured
			? lng_stickers_group_from_featured
			: lng_stickers_group_from_your));
		updateControlsGeometry();
	} else if (_section == Section::Installed) {
		auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);
		if (cloudIt != sets.cend() && !cloudIt->stickers.isEmpty()) {
			rebuildAppendSet(cloudIt.value(), maxNameWidth);
		}
	}
	for_const (auto setId, order) {
		auto it = sets.constFind(setId);
		if (it == sets.cend()) {
			continue;
		}

		rebuildAppendSet(it.value(), maxNameWidth);

		if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
			Auth().api().scheduleStickerSetRequest(it->id, it->access);
		}
	}
	Auth().api().requestStickerSets();
	updateSize();
}

void StickersBox::Inner::setMegagroupSelectedSet(const MTPInputStickerSet &set) {
	_megagroupSetInput = set;
	rebuild();
	scrollToY.notify(0, true);
	onUpdateSelected();
}

void StickersBox::Inner::setMinHeight(int newWidth, int minHeight) {
	_minHeight = minHeight;
	updateSize(newWidth);
}

void StickersBox::Inner::updateSize(int newWidth) {
	auto naturalHeight = _itemsTop + int(_rows.size()) * _rowHeight + st::membersMarginBottom;
	resize(newWidth ? newWidth : width(), qMax(_minHeight, naturalHeight));
	updateControlsGeometry();
	checkLoadMore();
}

void StickersBox::Inner::updateRows() {
	int maxNameWidth = countMaxNameWidth();
	auto &sets = Auth().data().stickerSets();
	for_const (auto &row, _rows) {
		auto it = sets.constFind(row->id);
		if (it != sets.cend()) {
			auto &set = it.value();
			if (!row->sticker) {
				auto sticker = (DocumentData*)nullptr;
				auto pixw = 0, pixh = 0;
				fillSetCover(set, &sticker, &pixw, &pixh);
				if (sticker) {
					row->sticker = sticker;
					row->pixw = pixw;
					row->pixh = pixh;
				}
			}
			if (!row->isRecentSet()) {
				auto wasInstalled = row->installed;
				auto wasArchived = row->archived;
				fillSetFlags(set, &row->installed, &row->official, &row->unread, &row->archived);
				if (_section == Section::Installed) {
					row->archived = false;
				}
				if (row->installed != wasInstalled || row->archived != wasArchived) {
					row->ripple.reset();
				}
			}
			row->title = fillSetTitle(set, maxNameWidth, &row->titleWidth);
			row->count = fillSetCount(set);
		}
	}
	update();
}

bool StickersBox::Inner::appendSet(const Stickers::Set &set) {
	for_const (auto &row, _rows) {
		if (row->id == set.id) {
			return false;
		}
	}
	rebuildAppendSet(set, countMaxNameWidth());
	return true;
}

int StickersBox::Inner::countMaxNameWidth() const {
	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	if (!_megagroupSet && _section == Section::Installed) {
		namex += st::stickersReorderIcon.width() + st::stickersReorderSkip;
	}
	int namew = st::boxWideWidth - namex - st::contactsPadding.right() - st::contactsCheckPosition.x();
	if (_section == Section::Installed) {
		if (!_megagroupSet) {
			namew -= _undoWidth - st::stickersUndoRemove.width;
		}
	} else {
		namew -= _addWidth - st::stickersTrendingAdd.width;
		if (_section == Section::Featured) {
			namew -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
		}
	}
	return namew;
}

void StickersBox::Inner::rebuildAppendSet(const Stickers::Set &set, int maxNameWidth) {
	bool installed = true, official = true, unread = false, archived = false, removed = false;
	if (set.id != Stickers::CloudRecentSetId) {
		fillSetFlags(set, &installed, &official, &unread, &archived);
	}
	if (_section == Section::Installed && archived) {
		return;
	}

	DocumentData *sticker = nullptr;
	int pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);

	int titleWidth = 0;
	QString title = fillSetTitle(set, maxNameWidth, &titleWidth);
	int count = fillSetCount(set);

	_rows.push_back(std::make_unique<Row>(
		set.id,
		set.access,
		sticker,
		count,
		title,
		titleWidth,
		installed,
		official,
		unread,
		archived,
		removed,
		pixw,
		pixh));
	_animStartTimes.push_back(0);
}

void StickersBox::Inner::fillSetCover(const Stickers::Set &set, DocumentData **outSticker, int *outWidth, int *outHeight) const {
	if (set.stickers.isEmpty()) {
		*outSticker = nullptr;
		*outWidth = *outHeight = 0;
		return;
	}
	auto sticker = *outSticker = set.stickers.front();

	auto pixw = sticker->thumb->width();
	auto pixh = sticker->thumb->height();
	if (pixw > st::contactsPhotoSize) {
		if (pixw > pixh) {
			pixh = (pixh * st::contactsPhotoSize) / pixw;
			pixw = st::contactsPhotoSize;
		} else {
			pixw = (pixw * st::contactsPhotoSize) / pixh;
			pixh = st::contactsPhotoSize;
		}
	} else if (pixh > st::contactsPhotoSize) {
		pixw = (pixw * st::contactsPhotoSize) / pixh;
		pixh = st::contactsPhotoSize;
	}
	*outWidth = pixw;
	*outHeight = pixh;
}

int StickersBox::Inner::fillSetCount(const Stickers::Set &set) const {
	int result = set.stickers.isEmpty() ? set.count : set.stickers.size(), added = 0;
	if (set.id == Stickers::CloudRecentSetId) {
		auto customIt = Auth().data().stickerSets().constFind(Stickers::CustomSetId);
		if (customIt != Auth().data().stickerSets().cend()) {
			added = customIt->stickers.size();
			for_const (auto &sticker, Stickers::GetRecentPack()) {
				if (customIt->stickers.indexOf(sticker.first) < 0) {
					++added;
				}
			}
		} else {
			added = Stickers::GetRecentPack().size();
		}
	}
	return result + added;
}

QString StickersBox::Inner::fillSetTitle(const Stickers::Set &set, int maxNameWidth, int *outTitleWidth) const {
	auto result = set.title;
	int titleWidth = st::contactsNameStyle.font->width(result);
	if (titleWidth > maxNameWidth) {
		result = st::contactsNameStyle.font->elided(result, maxNameWidth);
		titleWidth = st::contactsNameStyle.font->width(result);
	}
	if (outTitleWidth) {
		*outTitleWidth = titleWidth;
	}
	return result;
}

void StickersBox::Inner::fillSetFlags(
		const Stickers::Set &set,
		bool *outInstalled,
		bool *outOfficial,
		bool *outUnread,
		bool *outArchived) {
	*outInstalled = (set.flags & MTPDstickerSet::Flag::f_installed_date);
	*outOfficial = (set.flags & MTPDstickerSet::Flag::f_official);
	*outArchived = (set.flags & MTPDstickerSet::Flag::f_archived);
	if (_section == Section::Featured) {
		*outUnread = (set.flags & MTPDstickerSet_ClientFlag::f_unread);
	} else {
		*outUnread = false;
	}
}

template <typename Check>
Stickers::Order StickersBox::Inner::collectSets(Check check) const {
	Stickers::Order result;
	result.reserve(_rows.size());
	for_const (auto &row, _rows) {
		if (check(row.get())) {
			result.push_back(row->id);
		}
	}
	return result;
}

Stickers::Order StickersBox::Inner::getOrder() const {
	return collectSets([](Row *row) {
		return !row->archived && !row->removed && !row->isRecentSet();
	});
}

Stickers::Order StickersBox::Inner::getFullOrder() const {
	return collectSets([](Row *row) {
		return !row->isRecentSet();
	});
}

Stickers::Order StickersBox::Inner::getRemovedSets() const {
	return collectSets([](Row *row) {
		return row->removed;
	});
}

int StickersBox::Inner::getRowIndex(uint64 setId) const {
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		auto &row = _rows[i];
		if (row->id == setId) {
			return i;
		}
	}
	return -1;
}

void StickersBox::Inner::setFullOrder(const Stickers::Order &order) {
	for_const (auto setId, order) {
		auto index = getRowIndex(setId);
		if (index >= 0) {
			auto row = std::move(_rows[index]);
			auto count = _rows.size();
			for (auto i = index + 1; i != count; ++i) {
				_rows[i - 1] = std::move(_rows[i]);
			}
			_rows[count - 1] = std::move(row);
		}
	}
}

void StickersBox::Inner::setRemovedSets(const Stickers::Order &removed) {
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		setRowRemoved(i, removed.contains(_rows[i]->id));
	}
}

void StickersBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	updateScrollbarWidth();
	if (_section == Section::Featured) {
		readVisibleSets();
	}
	checkLoadMore();
}

void StickersBox::Inner::checkLoadMore() {
	if (_loadMoreCallback) {
		auto scrollHeight = (_visibleBottom - _visibleTop);
		int scrollTop = _visibleTop, scrollTopMax = height() - scrollHeight;
		if (scrollTop + PreloadHeightsCount * scrollHeight >= scrollTopMax) {
			_loadMoreCallback();
		}
	}
}

void StickersBox::Inner::readVisibleSets() {
	auto itemsVisibleTop = _visibleTop - _itemsTop;
	auto itemsVisibleBottom = _visibleBottom - _itemsTop;
	int rowFrom = floorclamp(itemsVisibleTop, _rowHeight, 0, _rows.size());
	int rowTo = ceilclamp(itemsVisibleBottom, _rowHeight, 0, _rows.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		if (!_rows[i]->unread) {
			continue;
		}
		if (i * _rowHeight < itemsVisibleTop || (i + 1) * _rowHeight > itemsVisibleBottom) {
			continue;
		}
		if (!_rows[i]->sticker || _rows[i]->sticker->thumb->loaded() || _rows[i]->sticker->loaded()) {
			Auth().api().readFeaturedSetDelayed(_rows[i]->id);
		}
	}
}

void StickersBox::Inner::updateScrollbarWidth() {
	auto width = (_visibleBottom - _visibleTop < height()) ? (st::boxLayerScroll.width - st::boxLayerScroll.deltax) : 0;
	if (_scrollbar != width) {
		_scrollbar = width;
		update();
	}
}

StickersBox::Inner::~Inner() {
	clear();
}
