/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "gram/api/gram_api_nft.h"

#include <memory>

class FileLoader;
class Painter;

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

[[nodiscard]] bool UniqueGiftMatchesAddress(
	const std::shared_ptr<Data::UniqueGift> &unique,
	const Gram::Address &address);

struct CollectibleView {
	QString name;
	QString number;
	QString collectionName;
	Gram::NftKind kind = Gram::NftKind::Generic;
	QString key;
};

[[nodiscard]] TextWithEntities CollectibleTitleText(
	const CollectibleView &view);

class CollectibleMedia final : public base::has_weak_ptr {
public:
	explicit CollectibleMedia(not_null<Main::Session*> session);
	~CollectibleMedia();

	void resolve(const QString &item);

	[[nodiscard]] CollectibleView view(const QString &item) const;
	[[nodiscard]] QString collection(const QString &item) const;
	void paint(
		Painter &p,
		const QString &item,
		QRect rect,
		int outerWidth,
		int radius);

	[[nodiscard]] rpl::producer<QString> changed() const;
	[[nodiscard]] rpl::producer<QString> repaint() const;

private:
	struct Entry;

	void resolveFromRecord(
		not_null<Entry*> entry,
		const Gram::NftItem &record);
	void requestGift(not_null<Entry*> entry, const QString &slug);
	void startLoad(
		std::unique_ptr<FileLoader> &slot,
		const QString &url,
		Fn<void(QByteArray)> done);
	void startImageLoad(not_null<Entry*> entry, const QString &url);
	void startDescriptorLoad(not_null<Entry*> entry, const QString &url);
	[[nodiscard]] Entry *find(const QString &item) const;
	[[nodiscard]] const QImage &preparedFor(
		not_null<Entry*> entry,
		int side,
		int radius);

	const not_null<Main::Session*> _session;
	rpl::event_stream<QString> _changed;
	rpl::event_stream<QString> _repaint;
	base::flat_map<QString, std::unique_ptr<Entry>> _map;

};

} // namespace Wallet
