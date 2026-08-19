/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_peer_colors.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "window/themes/window_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/color_int_conversion.h"

namespace Api {
namespace {

constexpr auto kRequestEach = 3600 * crl::time(1000);

} // namespace

PeerColors::PeerColors(not_null<ApiWrap*> api)
: _api(&api->instance())
, _timer([=] { request(); requestProfile(); }) {
	request();
	requestProfile();
	_timer.callEach(kRequestEach);
}

PeerColors::~PeerColors() = default;

void PeerColors::request() {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPhelp_GetPeerColors(
		MTP_int(_hash)
	)).done([=](const MTPhelp_PeerColors &result) {
		_requestId = 0;
		result.match([&](const MTPDhelp_peerColors &data) {
			_hash = data.vhash().v;
			apply(data);
		}, [](const MTPDhelp_peerColorsNotModified &) {
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void PeerColors::requestProfile() {
	if (_profileRequestId) {
		return;
	}
	_profileRequestId = _api.request(MTPhelp_GetPeerProfileColors(
		MTP_int(_profileHash)
	)).done([=](const MTPhelp_PeerColors &result) {
		_profileRequestId = 0;
		result.match([&](const MTPDhelp_peerColors &data) {
			_profileHash = data.vhash().v;
			applyProfile(data);
		}, [](const MTPDhelp_peerColorsNotModified &) {
		});
	}).fail([=] {
		_profileRequestId = 0;
	}).send();
}

std::vector<uint8> PeerColors::suggested() const {
	return _suggested.current();
}

rpl::producer<std::vector<uint8>> PeerColors::suggestedValue() const {
	return _suggested.value();
}

auto PeerColors::indicesValue() const
-> rpl::producer<Ui::ColorIndicesCompressed> {
	return rpl::single(
		indicesCurrent()
	) | rpl::then(_colorIndicesChanged.events() | rpl::map([=] {
		return indicesCurrent();
	}));
}

Ui::ColorIndicesCompressed PeerColors::indicesCurrent() const {
	return _colorIndicesCurrent
		? *_colorIndicesCurrent
		: Ui::ColorIndicesCompressed();
}

const base::flat_map<uint8, int> &PeerColors::requiredLevelsGroup() const {
	return _requiredLevelsGroup;
}

const base::flat_map<uint8, int> &PeerColors::requiredLevelsChannel() const {
	return _requiredLevelsChannel;
}

int PeerColors::requiredLevelFor(
		PeerId channel,
		uint8 index,
		bool isMegagroup,
		bool profile) const {
	if (Data::DecideColorIndex(channel) == index) {
		return 0;
	}
	if (profile) {
		const auto it = _profileColors.find(index);
		if (it != end(_profileColors)) {
			return isMegagroup
				? it->second.requiredLevelsGroup
				: it->second.requiredLevelsChannel;
		}
		return 1;
	}
	const auto &levels = isMegagroup
		? _requiredLevelsGroup
		: _requiredLevelsChannel;
	if (const auto i = levels.find(index); i != end(levels)) {
		return i->second;
	}
	return 1;
}

void PeerColors::apply(const MTPDhelp_peerColors &data) {
	auto suggested = std::vector<uint8>();
	auto colors = std::make_shared<
		std::array<Ui::ColorIndexData, Ui::kColorIndexCount>>();

	using ParsedColor = std::array<uint32, Ui::kColorPatternsCount>;
	const auto parseColors = [](const MTPhelp_PeerColorSet &set) {
		return set.match([&](const MTPDhelp_peerColorSet &data) {
			auto result = ParsedColor();
			const auto &list = data.vcolors().v;
			if (list.empty() || list.size() > Ui::kColorPatternsCount) {
				LOG(("API Error: Bad count for PeerColorSet.colors: %1"
					).arg(list.size()));
				return ParsedColor();
			}
			auto fill = result.data();
			for (const auto &color : list) {
				*fill++ = (uint32(1) << 24) | uint32(color.v);
			}
			return result;
		}, [](const MTPDhelp_peerColorProfileSet &) {
			LOG(("API Error: peerColorProfileSet in colors result!"));
			return ParsedColor();
		});
	};

	const auto &list = data.vcolors().v;
	_requiredLevelsGroup.clear();
	_requiredLevelsChannel.clear();
	suggested.reserve(list.size());
	for (const auto &color : list) {
		const auto &data = color.data();
		const auto colorIndexBare = data.vcolor_id().v;
		if (colorIndexBare < 0 || colorIndexBare >= Ui::kColorIndexCount) {
			LOG(("API Error: Bad color index: %1").arg(colorIndexBare));
			continue;
		}
		const auto colorIndex = uint8(colorIndexBare);
		if (const auto min = data.vgroup_min_level()) {
			_requiredLevelsGroup[colorIndex] = min->v;
		}
		if (const auto min = data.vchannel_min_level()) {
			_requiredLevelsChannel[colorIndex] = min->v;
		}
		if (!data.is_hidden()) {
			suggested.push_back(colorIndex);
		}
		if (const auto light = data.vcolors()) {
			auto &fields = (*colors)[colorIndex];
			fields.light = parseColors(*light);
			if (const auto dark = data.vdark_colors()) {
				fields.dark = parseColors(*dark);
			} else {
				fields.dark = fields.light;
			}
		}
	}

	if (!_colorIndicesCurrent) {
		_colorIndicesCurrent = std::make_unique<Ui::ColorIndicesCompressed>(
			Ui::ColorIndicesCompressed{ std::move(colors) });
		_colorIndicesChanged.fire({});
	} else if (*_colorIndicesCurrent->colors != *colors) {
		_colorIndicesCurrent->colors = std::move(colors);
		_colorIndicesChanged.fire({});
	}
	_suggested = std::move(suggested);
}

void PeerColors::applyProfile(const MTPDhelp_peerColors &data) {
	const auto parseColors = [](const MTPhelp_PeerColorSet &set) {
		const auto toUint = [](const MTPint &c) {
			return (uint32(1) << 24) | uint32(c.v);
		};
		return set.match([&](const MTPDhelp_peerColorSet &) {
			LOG(("API Error: peerColorSet in profile colors result!"));
			return Data::ColorProfileSet();
		}, [&](const MTPDhelp_peerColorProfileSet &data) {
			auto set = Data::ColorProfileSet();
			set.palette.reserve(data.vpalette_colors().v.size());
			set.bg.reserve(data.vbg_colors().v.size());
			set.story.reserve(data.vstory_colors().v.size());
			for (const auto &c : data.vpalette_colors().v) {
				set.palette.push_back(Ui::ColorFromSerialized(toUint(c)));
			}
			for (const auto &c : data.vbg_colors().v) {
				set.bg.push_back(Ui::ColorFromSerialized(toUint(c)));
			}
			for (const auto &c : data.vstory_colors().v) {
				set.story.push_back(Ui::ColorFromSerialized(toUint(c)));
			}
			return set;
		});
	};

	auto suggested = std::vector<Data::ColorProfileData>();
	const auto &list = data.vcolors().v;
	suggested.reserve(list.size());
	for (const auto &color : list) {
		const auto &data = color.data();
		const auto colorIndexBare = data.vcolor_id().v;
		if (colorIndexBare < 0 || colorIndexBare >= Ui::kColorIndexCount) {
			LOG(("API Error: Bad color index: %1").arg(colorIndexBare));
			continue;
		}
		const auto colorIndex = uint8(colorIndexBare);
		auto result = ProfileColorOption();
		result.isHidden = data.is_hidden();
		if (const auto min = data.vgroup_min_level()) {
			result.requiredLevelsGroup = min->v;
		}
		if (const auto min = data.vchannel_min_level()) {
			result.requiredLevelsChannel = min->v;
		}
		if (const auto light = data.vcolors()) {
			result.data.light = parseColors(*light);
		}
		if (const auto dark = data.vdark_colors()) {
			result.data.dark = parseColors(*dark);
		}
		_profileColors[colorIndex] = std::move(result);
	}
}

std::optional<Data::ColorProfileSet> PeerColors::colorProfileFor(
		not_null<PeerData*> peer) const {
	if (const auto colorProfileIndex = peer->colorProfileIndex()) {
		return colorProfileFor(*colorProfileIndex);
	}
	return std::nullopt;
}

std::optional<Data::ColorProfileSet> PeerColors::colorProfileFor(
		uint8 index) const {
	const auto i = _profileColors.find(index);
	if (i != end(_profileColors)) {
		return Window::Theme::IsNightMode()
			? i->second.data.dark
			: i->second.data.light;
	}
	return std::nullopt;
}

std::vector<uint8> PeerColors::profileColorIndices() const {
	auto result = std::vector<uint8>();
	result.reserve(_profileColors.size());
	for (const auto &[index, option] : _profileColors) {
		result.push_back(index);
	}
	return result;
}

} // namespace Api
