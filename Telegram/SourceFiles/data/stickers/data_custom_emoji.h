/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/stickers/data_stickers_set.h"
#include "ui/text/custom_emoji_instance.h"
#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class CustomEmojiLoader;

struct CustomEmojiId {
	DocumentId id = 0;
};

class CustomEmojiManager final : public base::has_weak_ptr {
public:
	enum class SizeTag : uchar {
		Normal,
		Large,
		Isolated,

		kCount,
	};

	CustomEmojiManager(not_null<Session*> owner);
	~CustomEmojiManager();

	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		QStringView data,
		Fn<void()> update,
		SizeTag tag = SizeTag::Normal,
		int sizeOverride = 0);
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		DocumentId documentId,
		Fn<void()> update,
		SizeTag tag = SizeTag::Normal,
		int sizeOverride = 0);
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		not_null<DocumentData*> document,
		Fn<void()> update,
		SizeTag tag = SizeTag::Normal,
		int sizeOverride = 0);

	class Listener {
	public:
		virtual void customEmojiResolveDone(
			not_null<DocumentData*> document) = 0;
	};
	void resolve(QStringView data, not_null<Listener*> listener);
	void resolve(DocumentId documentId, not_null<Listener*> listener);
	void unregisterListener(not_null<Listener*> listener);

	[[nodiscard]] std::unique_ptr<Ui::CustomEmoji::Loader> createLoader(
		not_null<DocumentData*> document,
		SizeTag tag,
		int sizeOverride = 0);
	[[nodiscard]] std::unique_ptr<Ui::CustomEmoji::Loader> createLoader(
		DocumentId documentId,
		SizeTag tag,
		int sizeOverride = 0);

	[[nodiscard]] QString lookupSetName(uint64 setId);

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Session &owner() const;

	[[nodiscard]] uint64 coloredSetId() const;

private:
	static constexpr auto kSizeCount = int(SizeTag::kCount);

	struct RepaintBunch {
		crl::time when = 0;
		std::vector<base::weak_ptr<Ui::CustomEmoji::Instance>> instances;
	};
	struct LoaderWithSetId {
		std::unique_ptr<Ui::CustomEmoji::Loader> loader;
		uint64 setId = 0;
	};

	[[nodiscard]] LoaderWithSetId createLoaderWithSetId(
		not_null<DocumentData*> document,
		SizeTag tag,
		int sizeOverride = 0);
	[[nodiscard]] LoaderWithSetId createLoaderWithSetId(
		DocumentId documentId,
		SizeTag tag,
		int sizeOverride = 0);

	void request();
	void requestFinished();
	void repaintLater(
		not_null<Ui::CustomEmoji::Instance*> instance,
		Ui::CustomEmoji::RepaintRequest request);
	void scheduleRepaintTimer();
	void invokeRepaints();
	void fillColoredFlags(not_null<DocumentData*> document);
	void processLoaders(not_null<DocumentData*> document);
	void processListeners(not_null<DocumentData*> document);
	void requestSetFor(not_null<DocumentData*> document);

	[[nodiscard]] Ui::CustomEmoji::Preview prepareNonExactPreview(
		DocumentId documentId,
		SizeTag tag,
		int sizeOverride) const;
	template <typename LoaderFactory>
	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		DocumentId documentId,
		Fn<void()> update,
		SizeTag tag,
		int sizeOverride,
		LoaderFactory factory);
	[[nodiscard]] static int SizeIndex(SizeTag tag);

	const not_null<Session*> _owner;

	std::array<
		base::flat_map<
			DocumentId,
			std::unique_ptr<Ui::CustomEmoji::Instance>>,
		kSizeCount> _instances;
	std::array<
		base::flat_map<
			DocumentId,
			std::vector<base::weak_ptr<CustomEmojiLoader>>>,
		kSizeCount> _loaders;
	base::flat_map<
		DocumentId,
		base::flat_set<not_null<Listener*>>> _resolvers;
	base::flat_map<
		not_null<Listener*>,
		base::flat_set<DocumentId>> _listeners;
	base::flat_set<DocumentId> _pendingForRequest;
	base::flat_map<
		uint64,
		base::flat_set<
			not_null<Ui::CustomEmoji::Instance*>>> _coloredSetPending;

	mtpRequestId _requestId = 0;

	uint64 _coloredSetId = 0;

	base::flat_map<crl::time, RepaintBunch> _repaints;
	crl::time _repaintNext = 0;
	base::Timer _repaintTimer;
	bool _repaintTimerScheduled = false;
	bool _requestSetsScheduled = false;

	rpl::lifetime _lifetime;

};

[[nodiscard]] int FrameSizeFromTag(CustomEmojiManager::SizeTag tag);

[[nodiscard]] QString SerializeCustomEmojiId(const CustomEmojiId &id);
[[nodiscard]] QString SerializeCustomEmojiId(
	not_null<DocumentData*> document);
[[nodiscard]] CustomEmojiId ParseCustomEmojiData(QStringView data);

[[nodiscard]] bool AllowEmojiWithoutPremium(not_null<PeerData*> peer);

void InsertCustomEmoji(
	not_null<Ui::InputField*> field,
	not_null<DocumentData*> document);

[[nodiscard]] Ui::Text::CustomEmojiFactory ReactedMenuFactory(
	not_null<Main::Session*> session);

} // namespace Data
