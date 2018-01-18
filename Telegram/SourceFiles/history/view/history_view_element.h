/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "base/runtime_composer.h"
#include "base/flags.h"

class HistoryBlock;
class HistoryItem;
class HistoryMessage;
class HistoryService;
class HistoryMedia;
class HistoryWebPage;
struct HistoryTextState;
struct HistoryStateRequest;
enum InfoDisplayType : char;

namespace HistoryView {

class Element;
class ElementDelegate {
public:
	virtual std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message) = 0;
	virtual std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message) = 0;

};

TextSelection UnshiftItemSelection(
	TextSelection selection,
	uint16 byLength);
TextSelection ShiftItemSelection(
	TextSelection selection,
	uint16 byLength);
TextSelection UnshiftItemSelection(
	TextSelection selection,
	const Text &byText);
TextSelection ShiftItemSelection(
	TextSelection selection,
	const Text &byText);

class Element;
//struct Group : public RuntimeComponent<Group, Element> {
//	MessageGroupId groupId = MessageGroupId::None;
//	Element *leader = nullptr;
//	std::vector<not_null<HistoryItem*>> others;
//};

enum class Context : char {
	History,
	Feed,
	AdminLog
};

class Element
	: public Object
	, public RuntimeComposer<Element>
	, public ClickHandlerHost {
public:
	Element(not_null<HistoryItem*> data, Context context);

	enum class Flag : uchar {
		NeedsResize        = 0x01,
		AttachedToPrevious = 0x02,
		AttachedToNext     = 0x04,
		HiddenByGroup      = 0x08,
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<HistoryItem*> data() const;
	HistoryMedia *media() const;
	Context context() const;
	void refreshDataId();

	int y() const;
	void setY(int y);

	int marginTop() const;
	int marginBottom() const;
	void setPendingResize();
	bool pendingResize() const;
	bool isUnderCursor() const;

	bool isAttachedToPrevious() const;
	bool isAttachedToNext() const;

	int skipBlockWidth() const;
	int skipBlockHeight() const;
	QString skipBlock() const;
	virtual int infoWidth() const;

	bool isHiddenByGroup() const;
	//void makeGroupMember(not_null<Element*> leader);
	//void makeGroupLeader(std::vector<not_null<HistoryItem*>> &&others);
	//bool groupIdValidityChanged();
	//void validateGroupId();
	//Group *getFullGroup();

	// For blocks context this should be called only from recountAttachToPreviousInBlocks().
	void setAttachToPrevious(bool attachToNext);

	// For blocks context this should be called only from recountAttachToPreviousInBlocks()
	// of the next item or when the next item is removed through nextInBlocksChanged() call.
	void setAttachToNext(bool attachToNext);

	// For blocks context this should be called only from recountDisplayDate().
	void setDisplayDate(bool displayDate);

	bool computeIsAttachToPrevious(not_null<Element*> previous);

	virtual void draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		TimeMs ms) const = 0;
	[[nodiscard]] virtual bool hasPoint(QPoint point) const = 0;
	[[nodiscard]] virtual HistoryTextState getState(
		QPoint point,
		HistoryStateRequest request) const = 0;
	virtual void updatePressed(QPoint point) = 0;
	virtual void drawInfo(
		Painter &p,
		int right,
		int bottom,
		int width,
		bool selected,
		InfoDisplayType type) const;
	virtual bool pointInTime(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const;
	virtual TextWithEntities selectedText(
		TextSelection selection) const = 0;
	[[nodiscard]] virtual TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const;

	// ClickHandlerHost interface.
	void clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	virtual bool hasFromPhoto() const;
	virtual bool displayFromPhoto() const;
	virtual bool hasFromName() const;
	virtual bool displayFromName() const;
	virtual bool displayForwardedFrom() const;
	virtual bool hasOutLayout() const;
	virtual bool drawBubble() const;
	virtual bool hasBubble() const;
	virtual bool hasFastReply() const;
	virtual bool displayFastReply() const;
	virtual bool displayRightAction() const;
	virtual void drawRightAction(
		Painter &p,
		int left,
		int top,
		int outerWidth) const;
	virtual ClickHandlerPtr rightActionLink() const;
	virtual bool displayEditedBadge() const;
	virtual QDateTime displayedEditDate() const;
	virtual bool hasVisibleText() const;

	// Legacy blocks structure.
	HistoryBlock *block();
	const HistoryBlock *block() const;
	void attachToBlock(not_null<HistoryBlock*> block, int index);
	void removeFromBlock();
	void refreshInBlock();
	void setIndexInBlock(int index);
	int indexInBlock() const;
	Element *previousInBlocks() const;
	Element *nextInBlocks() const;
	void previousInBlocksChanged();
	void nextInBlocksChanged();

	void clipCallback(Media::Clip::Notification notification);

	virtual ~Element();

protected:
	void setInitialSize(int maxWidth, int minHeight);
	void setCurrentSize(int width, int height);

private:
	// This should be called only from previousInBlocksChanged()
	// to add required bits to the Composer mask
	// after that always use Has<HistoryMessageDate>().
	void recountDisplayDateInBlocks();

	// This should be called only from previousInBlocksChanged() or when
	// HistoryMessageDate or HistoryMessageUnreadBar bit is changed in the Composer mask
	// then the result should be cached in a client side flag MTPDmessage_ClientFlag::f_attach_to_previous.
	void recountAttachToPreviousInBlocks();

	QSize countOptimalSize() final override;
	QSize countCurrentSize(int newWidth) final override;

	virtual QSize performCountOptimalSize() = 0;
	virtual QSize performCountCurrentSize(int newWidth) = 0;

	void refreshMedia();
	//void resetGroupMedia(const std::vector<not_null<HistoryItem*>> &others);

	const not_null<HistoryItem*> _data;
	std::unique_ptr<HistoryMedia> _media;

	int _y = 0;
	Context _context;

	Flags _flags = Flag::NeedsResize;

	HistoryBlock *_block = nullptr;
	int _indexInBlock = -1;

};

} // namespace HistoryView
