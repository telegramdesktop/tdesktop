/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "mtproto/connection_abstract.h"

namespace Ui {
class InputField;
class PortInput;
class PasswordInput;
class Checkbox;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
} // namespace Ui

class AutoDownloadBox : public BoxContent {
public:
	AutoDownloadBox(QWidget *parent);

protected:
	void prepare() override;

private:
	void setupContent();

};

class ProxiesBoxController : public base::Subscriber {
public:
	using Type = ProxyData::Type;

	ProxiesBoxController();

	static void ShowApplyConfirmation(
		Type type,
		const QMap<QString, QString> &fields);

	static object_ptr<BoxContent> CreateOwningBox();
	object_ptr<BoxContent> create();

	enum class ItemState {
		Connecting,
		Online,
		Checking,
		Available,
		Unavailable
	};
	struct ItemView {
		int id = 0;
		QString type;
		QString host;
		uint32 port = 0;
		int ping = 0;
		bool selected = false;
		bool deleted = false;
		bool supportsShare = false;
		bool supportsCalls = false;
		ItemState state = ItemState::Checking;

	};

	void deleteItem(int id);
	void restoreItem(int id);
	void shareItem(int id);
	void applyItem(int id);
	object_ptr<BoxContent> editItemBox(int id);
	object_ptr<BoxContent> addNewItemBox();
	bool setProxyEnabled(bool enabled);
	void setProxyForCalls(bool enabled);
	void setTryIPv6(bool enabled);
	rpl::producer<bool> proxyEnabledValue() const;

	rpl::producer<ItemView> views() const;

	~ProxiesBoxController();

private:
	using Checker = MTP::internal::ConnectionPointer;
	struct Item {
		int id = 0;
		ProxyData data;
		bool deleted = false;
		Checker checker;
		Checker checkerv6;
		ItemState state = ItemState::Checking;
		int ping = 0;

	};

	std::vector<Item>::iterator findById(int id);
	std::vector<Item>::iterator findByProxy(const ProxyData &proxy);
	void setDeleted(int id, bool deleted);
	void updateView(const Item &item);
	void share(const ProxyData &proxy);
	void saveDelayed();
	void refreshChecker(Item &item);
	void setupChecker(int id, const Checker &checker);

	void replaceItemWith(
		std::vector<Item>::iterator which,
		std::vector<Item>::iterator with);
	void replaceItemValue(
		std::vector<Item>::iterator which,
		const ProxyData &proxy);
	void addNewItem(const ProxyData &proxy);

	int _idCounter = 0;
	std::vector<Item> _list;
	rpl::event_stream<ItemView> _views;
	base::Timer _saveTimer;
	rpl::event_stream<bool> _proxyEnabledChanges;

	ProxyData _lastSelectedProxy;
	bool _lastSelectedProxyUsed = false;

};
