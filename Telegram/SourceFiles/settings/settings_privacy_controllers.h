/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "boxes/edit_privacy_box.h"
#include "history/view/history_view_element.h"
#include "mtproto/sender.h"

namespace Settings {

class BlockedBoxController : public PeerListController, private base::Subscriber, private MTP::Sender {
public:
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	static void BlockNewUser();

private:
	void receivedUsers(const QVector<MTPContactBlocked> &result);
	void handleBlockedEvent(not_null<UserData*> user);

	bool appendRow(not_null<UserData*> user);
	bool prependRow(not_null<UserData*> user);
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

class LastSeenPrivacyController : public EditPrivacyBox::Controller {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	LangKey optionsTitleKey() override;
	rpl::producer<QString> warning() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

	void confirmSave(bool someAreDisallowed, FnMut<void()> saveCallback) override;

};

class GroupsInvitePrivacyController : public EditPrivacyBox::Controller {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	bool hasOption(Option option) override;
	LangKey optionsTitleKey() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

};

class CallsPrivacyController : public EditPrivacyBox::Controller {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	LangKey optionsTitleKey() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

};

class CallsPeer2PeerPrivacyController : public EditPrivacyBox::Controller {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	LangKey optionsTitleKey() override;
	LangKey optionLabelKey(EditPrivacyBox::Option option) override;
	rpl::producer<QString> warning() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

};

class ForwardsPrivacyController
	: public EditPrivacyBox::Controller
	, private HistoryView::ElementDelegate {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	LangKey optionsTitleKey() override;
	rpl::producer<QString> warning() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

	object_ptr<Ui::RpWidget> setupAboveWidget(
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) override;

private:
	using Element = HistoryView::Element;
	not_null<HistoryView::ElementDelegate*> delegate();
	HistoryView::Context elementContext() override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message) override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message) override;
	bool elementUnderCursor(not_null<const Element*> view) override;
	void elementAnimationAutoplayAsync(
		not_null<const Element*> element) override;
	crl::time elementHighlightTime(
		not_null<const Element*> element) override;
	bool elementInSelectionMode() override;

	static void PaintForwardedTooltip(
		Painter &p,
		not_null<HistoryView::Element*> view,
		Option value);

};

class ProfilePhotoPrivacyController : public EditPrivacyBox::Controller {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() override;
	MTPInputPrivacyKey apiKey() override;

	QString title() override;
	LangKey optionsTitleKey() override;
	LangKey exceptionButtonTextKey(Exception exception) override;
	QString exceptionBoxTitle(Exception exception) override;
	rpl::producer<QString> exceptionsDescription() override;

};

} // namespace Settings
