#pragma once

#include <algorithm>
#include <list>
#include <vector>

#include <imgui.h>

namespace imguiext
{

// Combines several NUL-terminated ImWchar range lists (start/end pairs, as
// returned by ImFontAtlas::GetGlyphRanges* and by the tables in FontAtlas.cpp)
// into one list and returns a pointer that stays valid for the rest of the
// process.
//
// ImFontConfig::GlyphRanges is only a pointer that Dear ImGui dereferences for as
// long as the font is alive, so the combined array must not live on the stack.
// Every result is owned by a function-local std::list, whose elements keep their
// address when more results are appended later.
//
// Returns nullptr when nothing was requested, which lets ImGui use its default
// glyph ranges.
inline const ImWchar* CombineGlyphRanges(const std::vector<const ImWchar*>& range_lists)
{
	if (range_lists.empty())
		return nullptr;
	if (range_lists.size() == 1)
		return range_lists[0];

	struct Range
	{
		ImWchar first;
		ImWchar last;
	};

	std::vector<Range> sorted;
	for (const ImWchar* ranges : range_lists)
	{
		if (!ranges)
			continue;

		for (const ImWchar* p = ranges; p[0] != 0; p += 2)
		{
			ImWchar first = p[0];
			ImWchar last = p[1] != 0 ? p[1] : p[0];
			if (last < first)
				std::swap(first, last);
			sorted.push_back({ first, last });
		}
	}

	if (sorted.empty())
		return nullptr;

	std::sort(sorted.begin(), sorted.end(), [](const Range& a, const Range& b) {
		return a.first < b.first;
	});

	std::vector<ImWchar> combined;
	for (const Range& range : sorted)
	{
		const unsigned int first = static_cast<unsigned int>(range.first);
		if (!combined.empty() && first <= static_cast<unsigned int>(combined.back()) + 1u)
		{
			if (range.last > combined.back())
				combined.back() = range.last;
		}
		else
		{
			combined.push_back(range.first);
			combined.push_back(range.last);
		}
	}
	combined.push_back(0);

	static std::list<std::vector<ImWchar>> storage;
	storage.push_back(std::move(combined));
	return storage.back().data();
}

} // namespace imguiext
