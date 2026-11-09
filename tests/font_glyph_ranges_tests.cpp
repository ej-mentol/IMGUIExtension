#include <cassert>
#include <iostream>
#include <vector>

#include "../src/FontGlyphRanges.h"

// Plain range tables in the same format ImGui uses: start/end pairs, NUL terminated.
static const ImWchar kAsciiPrintable[] = { 0x0020, 0x007E, 0 };
static const ImWchar kCapitalLetters[] = { 0x0041, 0x005A, 0 };
static const ImWchar kLatinSupplement[] = { 0x007F, 0x009F, 0 };
static const ImWchar kCyrillic[] = { 0x0400, 0x04FF, 0 };
static const ImWchar kIcons[] = { 0xF000, 0xF0FF, 0x25A0, 0x25FF, 0 };

static std::vector<ImWchar> Flatten(const ImWchar* ranges)
{
	std::vector<ImWchar> result;
	for (const ImWchar* p = ranges; p && p[0] != 0; p += 2)
	{
		result.push_back(p[0]);
		result.push_back(p[1]);
	}
	return result;
}

static void TestNothingRequested()
{
	std::cout << "[Test] Running TestNothingRequested...\n";
	assert(imguiext::CombineGlyphRanges({}) == nullptr);

	std::vector<const ImWchar*> only_null{ nullptr };
	assert(imguiext::CombineGlyphRanges(only_null) == nullptr);

	std::cout << "[Test] TestNothingRequested passed.\n";
}

static void TestSingleRangeIsPassedThrough()
{
	std::cout << "[Test] Running TestSingleRangeIsPassedThrough...\n";
	std::vector<const ImWchar*> ranges{ kCapitalLetters };
	assert(imguiext::CombineGlyphRanges(ranges) == kCapitalLetters);
	std::cout << "[Test] TestSingleRangeIsPassedThrough passed.\n";
}

static void TestOverlappingRangesCoalesce()
{
	std::cout << "[Test] Running TestOverlappingRangesCoalesce...\n";
	const ImWchar* combined = imguiext::CombineGlyphRanges({ kCapitalLetters, kAsciiPrintable });
	assert(combined != nullptr);

	const std::vector<ImWchar> expected{ 0x0020, 0x007E };
	assert(Flatten(combined) == expected);

	std::cout << "[Test] TestOverlappingRangesCoalesce passed.\n";
}

static void TestAdjacentRangesCoalesce()
{
	std::cout << "[Test] Running TestAdjacentRangesCoalesce...\n";
	const ImWchar* combined = imguiext::CombineGlyphRanges({ kLatinSupplement, kAsciiPrintable });
	assert(combined != nullptr);

	const std::vector<ImWchar> expected{ 0x0020, 0x009F };
	assert(Flatten(combined) == expected);

	std::cout << "[Test] TestAdjacentRangesCoalesce passed.\n";
}

static void TestDisjointRangesStaySorted()
{
	std::cout << "[Test] Running TestDisjointRangesStaySorted...\n";
	// Icons are declared before Cyrillic on purpose: the result is sorted.
	const ImWchar* combined = imguiext::CombineGlyphRanges({ kIcons, kCyrillic });
	assert(combined != nullptr);

	const std::vector<ImWchar> expected{
		0x0400, 0x04FF,
		0x25A0, 0x25FF,
		0xF000, 0xF0FF };
	assert(Flatten(combined) == expected);

	std::cout << "[Test] TestDisjointRangesStaySorted passed.\n";
}

static void TestResultStorageSurvivesLaterCalls()
{
	std::cout << "[Test] Running TestResultStorageSurvivesLaterCalls...\n";
	const ImWchar* first = imguiext::CombineGlyphRanges({ kCyrillic, kIcons });
	assert(first != nullptr);
	const std::vector<ImWchar> first_before = Flatten(first);

	const ImWchar* second = imguiext::CombineGlyphRanges({ kAsciiPrintable, kLatinSupplement });
	assert(second != nullptr);
	assert(second != first);

	// ImGui keeps the pointer for the font lifetime: appending another result must
	// not move or change the previous one.
	assert(Flatten(first) == first_before);
	const std::vector<ImWchar> expected_second{ 0x0020, 0x009F };
	assert(Flatten(second) == expected_second);

	std::cout << "[Test] TestResultStorageSurvivesLaterCalls passed.\n";
}

static void TestDuplicateRangesCollapse()
{
	std::cout << "[Test] Running TestDuplicateRangesCollapse...\n";
	const ImWchar* combined = imguiext::CombineGlyphRanges({ kCyrillic, kCyrillic, kCyrillic });
	assert(combined != nullptr);

	const std::vector<ImWchar> expected{ 0x0400, 0x04FF };
	assert(Flatten(combined) == expected);

	std::cout << "[Test] TestDuplicateRangesCollapse passed.\n";
}

static void TestMissingFontDoesNotAbort()
{
	std::cout << "[Test] Running TestMissingFontDoesNotAbort...\n";

	// The plugin pre-validates font files, but a file can still disappear between
	// the check and the load. ImGui asserts through IM_ASSERT_USER_ERROR in that
	// case unless ImFontFlags_NoLoadError is set, so verify the flag actually
	// suppresses the assert (this test binary keeps asserts enabled via /UNDEBUG).
	ImGui::CreateContext();

	ImFontConfig config;
	config.Flags |= ImFontFlags_NoLoadError;
	ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF("imguiextension-missing-font.ttf", 18.0f, &config, nullptr);
	assert(font == nullptr);

	ImGui::DestroyContext();

	std::cout << "[Test] TestMissingFontDoesNotAbort passed.\n";
}

int main()
{
	std::cout << "=== Running Font Glyph Range Tests ===\n";
	TestNothingRequested();
	TestSingleRangeIsPassedThrough();
	TestOverlappingRangesCoalesce();
	TestAdjacentRangesCoalesce();
	TestDisjointRangesStaySorted();
	TestResultStorageSurvivesLaterCalls();
	TestDuplicateRangesCollapse();
	TestMissingFontDoesNotAbort();
	std::cout << "=== All Tests Passed Successfully ===\n";
	return 0;
}
