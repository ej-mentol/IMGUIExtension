#include <metahook.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <rapidjson/document.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include "plugins.h"
#include "FontAtlas.h"
#include "FontGlyphRanges.h"
#include "InputCapture.h"

static const ImWchar symbols_glyph_ranges[] = {
	0x25A0, 0x25FF, // Geometric Shapes
	0x2600, 0x26FF, // Miscellaneous Symbols
	0x2700, 0x27BF, // Dingbats
	0xF000, 0xFFFF, // Private Use Area (Font Awesome / Material Design Icons)
	0,
};

static const ImWchar latin_extended_glyph_ranges[] = {
	0x0020, 0x00FF, // Basic Latin + Latin Supplement
	0x0400, 0x052F, // Unicode block [0x0400, 0x052F]
	0,
};

static bool IsIconFont(const std::string& name)
{
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	return (lower.find("awesome") != std::string::npos ||
	        lower.find("symbol") != std::string::npos ||
	        lower.find("material") != std::string::npos ||
	        lower.find("seguisym") != std::string::npos ||
	        lower.find("icofont") != std::string::npos ||
	        lower.find("fa-") != std::string::npos);
}

static void GenerateConfigFromFolder(const char* szFontsDir, const char* szJsonPath)
{
	FILESYSTEM_ANY_CREATEDIR("imguiextension", "GAME");
	FILESYSTEM_ANY_CREATEDIR("imguiextension/fonts", "GAME");

	std::ofstream file(szJsonPath);
	if (!file.is_open()) return;

	file << "{\n  \"fonts\": [\n";

	std::string searchPath = std::string(szFontsDir) + "/*.*";
	for (char& c : searchPath)
	{
		if (c == '/') c = '\\';
	}
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

	if (hFind != INVALID_HANDLE_VALUE)
	{
		bool isFormattingFirst = true;
		bool hasBaseFont = false;
		do
		{
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;

			std::string fn = findData.cFileName;

			size_t idx = fn.find_last_of('.');
			if (idx != std::string::npos)
			{
				std::string ext = fn.substr(idx);
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

				if (ext == ".ttf" || ext == ".otf")
				{
					bool isIcon = IsIconFont(fn);
					if (!isFormattingFirst)
						file << ",\n";

					file << "    {\n";
					file << "      \"file\": \"" << fn << "\",\n";
					file << "      \"size\": 18.0,\n";
					if (isIcon)
					{
						file << "      \"merge\": true,\n";
						file << "      \"ranges\": [\"symbols\"]\n";
					}
					else if (!hasBaseFont)
					{
						file << "      \"ranges\": [\"latin\", \"cyrillic\"]\n";
						hasBaseFont = true;
					}
					else
					{
						file << "      \"merge\": true,\n";
						file << "      \"ranges\": [\"latin\", \"cyrillic\"]\n";
					}
					file << "    }";
					isFormattingFirst = false;
				}
			}
		} while (FindNextFileA(hFind, &findData));
		FindClose(hFind);
	}
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] GenerateConfigFromFolder failed to open directory: %s (Error: %lu)\n",
			searchPath.c_str(), GetLastError());
	}

	file << "\n  ]\n}\n";
}

static bool DirectoryContainsFonts(const std::string& dirPath)
{
	std::string searchPath = dirPath + "/*.*";
	for (char& c : searchPath) if (c == '/') c = '\\';

	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;

	bool hasFont = false;
	do
	{
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		std::string fn = findData.cFileName;
		size_t idx = fn.find_last_of('.');
		if (idx != std::string::npos)
		{
			std::string ext = fn.substr(idx);
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			if (ext == ".ttf" || ext == ".otf")
			{
				hasFont = true;
				break;
			}
		}
	} while (FindNextFileA(hFind, &findData));

	FindClose(hFind);
	return hasFont;
}

void LoadFontsFromConfig()
{
	ImGuiIO& io = ImGui::GetIO();

	// Font discovery needs the engine filesystem (and a game directory); fall back
	// to the built-in font instead of dereferencing unset host pointers.
	const char* pszGameDir = g_pMetaHookAPI ? g_pMetaHookAPI->GetGameDirectory() : nullptr;
	if ((!g_pFileSystem && !g_pFileSystem_HL25) || !pszGameDir)
	{
		io.Fonts->AddFontDefault();
		return;
	}

	char szJsonPath[MAX_PATH] = { 0 };
	std::string gameDir = pszGameDir;
	if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/imguiextension.json", szJsonPath, sizeof(szJsonPath)))
	{
		snprintf(szJsonPath, sizeof(szJsonPath), "%s/imguiextension/imguiextension.json", gameDir.c_str());
	}

	for (char* p = szJsonPath; *p; ++p)
		if (*p == '\\') *p = '/';

	char szFontsDir[MAX_PATH] = { 0 };
	std::string addonFontsDir = gameDir + "_addon/imguiextension/fonts";
	std::string baseFontsDir = gameDir + "/imguiextension/fonts";

	if (DirectoryContainsFonts(addonFontsDir))
	{
		snprintf(szFontsDir, sizeof(szFontsDir), "%s", addonFontsDir.c_str());
	}
	else if (DirectoryContainsFonts(baseFontsDir))
	{
		snprintf(szFontsDir, sizeof(szFontsDir), "%s", baseFontsDir.c_str());
	}
	else
	{
		if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/fonts", szFontsDir, sizeof(szFontsDir)))
		{
			snprintf(szFontsDir, sizeof(szFontsDir), "%s/imguiextension/fonts", gameDir.c_str());
		}
	}

	for (char* p = szFontsDir; *p; ++p)
		if (*p == '\\') *p = '/';

	std::ifstream testFile(szJsonPath);
	if (!testFile.good())
	{
		testFile.close();
		GenerateConfigFromFolder(szFontsDir, szJsonPath);
	}
	else
	{
		testFile.close();
	}

	std::ifstream file(szJsonPath);
	if (!file.is_open())
	{
		ImGui::GetIO().Fonts->AddFontDefault();
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();
	file.close();

	rapidjson::Document doc;
	if (doc.Parse(content.c_str()).HasParseError() || !doc.IsObject() || !doc.HasMember("fonts") || !doc["fonts"].IsArray())
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Error parsing imguiextension.json. Falling back to default font.\n");
		ImGui::GetIO().Fonts->AddFontDefault();
		return;
	}

	auto* fontsArrPtr = &doc["fonts"];

	// If the config is empty (size 0), auto-regenerate it from the fonts folder.
	// This helps on the very first run if the user copied fonts AFTER launching the plugin once.
	if (fontsArrPtr->Size() == 0)
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Config 'imguiextension.json' has no fonts listed. Scanning folder and regenerating...\n");
		GenerateConfigFromFolder(szFontsDir, szJsonPath);

		bool bReloaded = false;
		std::ifstream file2(szJsonPath);
		if (file2.is_open())
		{
			std::stringstream buffer2;
			buffer2 << file2.rdbuf();
			content = buffer2.str();
			file2.close();

			// The document that fontsArrPtr points into is overwritten by this call:
			// the old pointer must not be used when the new content does not parse.
			doc.Parse(content.c_str());
			if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("fonts") && doc["fonts"].IsArray())
			{
				fontsArrPtr = &doc["fonts"];
				bReloaded = true;
			}
		}

		if (!bReloaded)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Regenerated imguiextension.json could not be parsed. Falling back to default font.\n");
			io.Fonts->AddFontDefault();
			return;
		}
	}

	const auto& fontsArr = *fontsArrPtr;
	int loadedFonts = 0;
	int loadedMerges = 0;
	bool hasBaseFont = false;

	struct FontEntry
	{
		std::string filename;
		std::string resolvedPath;
		float size = 18.0f;
		bool merge = false;
		bool usable = false;
		std::vector<const ImWchar*> ranges;
	};

	std::vector<FontEntry> entries(fontsArr.Size());

	// Resolve and validate every entry first. This keeps the load phase free of
	// "is this font usable?" questions and lets a usable secondary base take over
	// when the declared primary font is missing or broken.
	auto validateEntry = [&](FontEntry& entry, const rapidjson::Value& fObj) {
		entry.merge = fObj.HasMember("merge") && fObj["merge"].IsBool() && fObj["merge"].GetBool();

		if (!fObj.HasMember("file") || !fObj["file"].IsString())
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Font entry without a \"file\" name ignored\n");
			return;
		}
		entry.filename = fObj["file"].GetString();
		if (entry.filename.empty())
			return;

		if (fObj.HasMember("size") && fObj["size"].IsNumber())
			entry.size = fObj["size"].GetFloat();
		// Reject 0/negative/NaN sizes: ImGui would bake a zero-size font.
		if (!(entry.size >= 6.0f && entry.size <= 200.0f))
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Invalid size %.1f for font '%s'; using 18.0\n", entry.size, entry.filename.c_str());
			entry.size = 18.0f;
		}

		char szResolvedPath[MAX_PATH] = { 0 };
		std::string fsPath = "imguiextension/fonts/" + entry.filename;
		if (!FILESYSTEM_ANY_GETLOCALPATH(fsPath.c_str(), szResolvedPath, sizeof(szResolvedPath)))
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Could not resolve font file '%s' (check imguiextension/imguiextension.json)\n", entry.filename.c_str());
			return;
		}

		for (char* p = szResolvedPath; *p; ++p)
			if (*p == '\\') *p = '/';
		entry.resolvedPath = szResolvedPath;

		// Dear ImGui asserts on unreadable or implausibly small font data, so the
		// file is validated here: a stale config entry must not turn into a Debug
		// build assert (or a crash inside the loader) at startup.
		WIN32_FILE_ATTRIBUTE_DATA fileInfo = {};
		if (!GetFileAttributesExA(szResolvedPath, GetFileExInfoStandard, &fileInfo) ||
			(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Font file not found: %s\n", szResolvedPath);
			return;
		}

		const unsigned long long fileSize =
			((unsigned long long)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
		if (fileSize <= 100)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Skipping '%s': %llu bytes is not a usable font file\n",
				szResolvedPath, fileSize);
			return;
		}

		if (fObj.HasMember("ranges") && fObj["ranges"].IsArray())
		{
			const auto& rArr = fObj["ranges"];
			for (rapidjson::SizeType r = 0; r < rArr.Size(); ++r)
			{
				if (!rArr[r].IsString()) continue;
				std::string rName = rArr[r].GetString();
				if (rName == "latin" || rName == "cyrillic")
					entry.ranges.push_back(latin_extended_glyph_ranges);
				else if (rName == "symbols")
					entry.ranges.push_back(symbols_glyph_ranges);
				else if (rName == "default")
					entry.ranges.push_back(io.Fonts->GetGlyphRangesDefault());
				else if (rName == "korean")
					entry.ranges.push_back(io.Fonts->GetGlyphRangesKorean());
				else if (rName == "japanese")
					entry.ranges.push_back(io.Fonts->GetGlyphRangesJapanese());
				else if (rName == "chinese")
					entry.ranges.push_back(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
				else
					gEngfuncs.Con_Printf("[IMGUIExtension] Unknown glyph range '%s' for '%s' (ignored)\n", rName.c_str(), entry.filename.c_str());
			}
		}

		entry.usable = true;
	};

	int primaryIndex = -1;
	for (rapidjson::SizeType i = 0; i < fontsArr.Size(); ++i)
	{
		if (!fontsArr[i].IsObject())
			continue;

		validateEntry(entries[i], fontsArr[i]);
		if (entries[i].usable && !entries[i].merge && primaryIndex < 0)
			primaryIndex = (int)i;
	}

	// Order: the primary base font (Fonts[0] and the destination for merges),
	// then every merge source, then any additional base fonts. Loading all bases
	// first would attach icon fonts to the *last* base instead, which is not what
	// a config listing "primary, icons, secondary" means. If the declared primary
	// is unusable, the first usable base takes its place.
	std::vector<int> loadOrder;
	if (primaryIndex >= 0)
		loadOrder.push_back(primaryIndex);
	for (rapidjson::SizeType i = 0; i < fontsArr.Size(); ++i)
		if (entries[i].usable && entries[i].merge)
			loadOrder.push_back((int)i);
	for (rapidjson::SizeType i = 0; i < fontsArr.Size(); ++i)
		if (entries[i].usable && !entries[i].merge && (int)i != primaryIndex)
			loadOrder.push_back((int)i);

	auto loadEntry = [&](const FontEntry& entry) {
		const bool merge = entry.merge;

		if (merge && !hasBaseFont)
		{
			// AddFontDefault() with no ImFontConfig uses an *implicit* reference size.
			// Merging a font that has an *explicit* size (e.g. "size": 18.0 from JSON)
			// onto an implicit-size base trips ImGui's assert in imgui_draw.cpp:
			// "Cannot use MergeMode with an explicit reference size when the
			//  destination font used an implicit reference size!"
			// Giving the fallback base font an explicit SizePixels avoids the mismatch.
			ImFontConfig defaultFontConfig;
			defaultFontConfig.SizePixels = entry.size;
			io.Fonts->AddFontDefault(&defaultFontConfig);
			hasBaseFont = true;
			gEngfuncs.Con_Printf("[IMGUIExtension] No base font before merge '%s'; using the built-in default as base (add a non-merge font to imguiextension.json).\n", entry.filename.c_str());
		}

		// Multiple ranges are combined; a single one is used as-is.
		const ImWchar* glyph_ranges_ptr = imguiext::CombineGlyphRanges(entry.ranges);

		ImFontConfig config;
		config.MergeMode = merge;
		// The return value is checked below; never let ImGui assert on a bad file.
		config.Flags |= ImFontFlags_NoLoadError;

		if (io.Fonts->AddFontFromFileTTF(entry.resolvedPath.c_str(), entry.size, &config, glyph_ranges_ptr) != nullptr)
		{
			if (GetDeveloperLevel() > 1)
			{
				gEngfuncs.Con_DPrintf("[IMGUIExtension] loaded font: %s (size %.1f, merge %d)\n",
					entry.filename.c_str(), entry.size, merge ? 1 : 0);
			}
			if (merge)
				++loadedMerges;
			else
				++loadedFonts;
			hasBaseFont = true;
		}
		else
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Failed to load font file: %s\n", entry.resolvedPath.c_str());
		}
	};

	for (const int index : loadOrder)
	{
		loadEntry(entries[static_cast<size_t>(index)]);
	}

	if (loadedFonts + loadedMerges > 0)
	{
		// The atlas is built lazily by ImGui and the backend owns the texture: do not
		// call Build() or the legacy GetTexDataAsAlpha8() here.
		if (GetDeveloperLevel() > 1)
		{
			gEngfuncs.Con_DPrintf("[IMGUIExtension] font atlas: %d font(s), %d merge source(s)\n",
				loadedFonts, loadedMerges);
		}
	}
	else if (io.Fonts->Fonts.Size == 0)
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] No fonts loaded. Falling back to default font.\n");
		io.Fonts->AddFontDefault();
	}
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] No configured font loaded; keeping the built-in fallback base font.\n");
	}
}