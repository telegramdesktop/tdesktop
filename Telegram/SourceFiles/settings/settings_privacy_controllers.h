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
#include "api/api_blocked_peers.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class ChatStyle;
namespace Toast {
class Instance;
} // namespace Toast
} // namespace Ui

namespace Settings {

class BlockedBoxController final : public PeerListController {
public:
	explicit BlockedBoxController(
		not_null<Window::SessionController*> window);

	::Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] rpl::producer<int> rowsCountChanges() const;

	static void BlockNewPeer(not_null<Window::SessionController*> window);

private:
	void applySlice(const Api::BlockedPeers::Slice &slice);
	void handleBlockedEvent(not_null<PeerData*> peer);

	bool appendRow(not_null<PeerData*> peer);
	bool prependRow(not_null<PeerData*> peer);
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer) const;

	const not_null<Window::SessionController*> _window;

	int _offset = 0;
	bool _allLoaded = false;

	base::has_weak_ptr _guard;

	rpl::event_stream<int> _rowsCountChanges;

};

class PhoneNumberPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	explicit PhoneNumberPrivacyController(
		not_null<Window::SessionController*> controller);

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<TextWithEntities> warning() const override;
	void prepareWarningLabel(not_null<Ui::FlatLabel*> warning) const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	object_ptr<Ui::RpWidget> setupMiddleWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) override;

	void saveAdditional() override;

private:
	const not_null<Window::SessionController*> _controller;
	rpl::variable<Option> _phoneNumberOption = { Option::Contacts };
	rpl::variable<Option> _addedByPhone = { Option::Everyone };
	Fn<void()> _saveAdditional;

};

class LastSeenPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	explicit LastSeenPrivacyController(not_null<::Main::Session*> session);

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<TextWithEntities> warning() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	void handleExceptionsChange(
		Exception exception,
		rpl::producer<int> value) override;

	object_ptr<Ui::RpWidget> setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> option) override;

	void confirmSave(
		bool someAreDisallowed,
		Fn<void()> saveCallback) override;

	void saveAdditional() override;

private:
	const not_null<::Main::Session*> _session;
	rpl::variable<Option> _option;
	rpl::variable<int> _exceptionsNever;
	bool _hideReadTime = false;

};

class GroupsInvitePrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;
	bool allowPremiumsToggle(Exception exception) const override;

};

class CallsPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	object_ptr<Ui::RpWidget> setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> option) override;

};

class CallsPeer2PeerPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	QString optionLabel(EditPrivacyBox::Option option) const override;
	rpl::producer<TextWithEntities> warning() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

};

class ForwardsPrivacyController
	: public EditPrivacyController
	, private HistoryView::SimpleElementDelegate {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	explicit ForwardsPrivacyController(
		not_null<Window::SessionController*> controller);

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<TextWithEntities> warning() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	object_ptr<Ui::RpWidget> setupAboveWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) override;

private:
	using Element = HistoryView::Element;
	not_null<HistoryView::ElementDelegate*> delegate();
	HistoryView::Context elementContext() override;

	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;

};

class ProfilePhotoPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	void handleExceptionsChange(
		Exception exception,
		rpl::producer<int> value) override;

	object_ptr<Ui::RpWidget> setupAboveWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) override;

	object_ptr<Ui::RpWidget> setupMiddleWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue) override;

	void saveAdditional() override;

private:
	Fn<void()> _saveAdditional;
	rpl::variable<Option> _option;
	rpl::variable<int> _exceptionsNever;

};

class VoicesPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	explicit VoicesPrivacyController(not_null<::Main::Session*> session);

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;
	object_ptr<Ui::RpWidget> setupBelowWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> option) override;
	Fn<void()> premiumClickedCallback(
		Option option,
		not_null<Window::SessionController*> controller) override;

private:
	base::weak_ptr<Ui::Toast::Instance> _toastInstance;
	rpl::lifetime _lifetime;

};

class AboutPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

};

class BirthdayPrivacyController final : public EditPrivacyController {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	Key key() const override;

	rpl::producer<QString> title() const override;
	rpl::producer<QString> optionsTitleKey() const override;
	rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const override;
	rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const override;
	rpl::producer<QString> exceptionsDescription() const override;

	object_ptr<Ui::RpWidget> setupAboveWidget(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> parent,
		rpl::producer<Option> optionValue,
		not_null<QWidget*> outerContainer) override;

};

} // namespace Settings
