#include <metahook.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <rapidjson/document.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"

#include "plugins.h"
#include "FontAtlas.h"
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
			gEngfuncs.Con_DPrintf("[IMGUIExtension] Found file during scan: %s\n", fn.c_str());

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
	char szJsonPath[MAX_PATH] = { 0 };
	if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/imguiextension.json", szJsonPath, sizeof(szJsonPath)))
	{
		std::string gameDir = g_pMetaHookAPI->GetGameDirectory();
		sprintf(szJsonPath, "%s/imguiextension/imguiextension.json", gameDir.c_str());
	}

	for (char* p = szJsonPath; *p; ++p)
		if (*p == '\\') *p = '/';

	char szFontsDir[MAX_PATH] = { 0 };
	std::string gameDir = g_pMetaHookAPI->GetGameDirectory();
	std::string addonFontsDir = gameDir + "_addon/imguiextension/fonts";
	std::string baseFontsDir = gameDir + "/imguiextension/fonts";

	if (DirectoryContainsFonts(addonFontsDir))
	{
		strcpy(szFontsDir, addonFontsDir.c_str());
	}
	else if (DirectoryContainsFonts(baseFontsDir))
	{
		strcpy(szFontsDir, baseFontsDir.c_str());
	}
	else
	{
		if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/fonts", szFontsDir, sizeof(szFontsDir)))
		{
			sprintf(szFontsDir, "%s/imguiextension/fonts", gameDir.c_str());
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

	ImGuiIO& io = ImGui::GetIO();
	auto* fontsArrPtr = &doc["fonts"];

	// If the config is empty (size 0), auto-regenerate it from the fonts folder.
	// This helps on the very first run if the user copied fonts AFTER launching the plugin once.
	if (fontsArrPtr->Size() == 0)
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Config 'imguiextension.json' has no fonts listed. Scanning folder and regenerating...\n");
		GenerateConfigFromFolder(szFontsDir, szJsonPath);

		std::ifstream file2(szJsonPath);
		if (file2.is_open())
		{
			std::stringstream buffer2;
			buffer2 << file2.rdbuf();
			content = buffer2.str();
			file2.close();

			doc.Parse(content.c_str());
			if (doc.IsObject() && doc.HasMember("fonts") && doc["fonts"].IsArray())
			{
				fontsArrPtr = &doc["fonts"];
			}
		}
	}

	const auto& fontsArr = *fontsArrPtr;
	bool loadedAny = false;
	bool hasBaseFont = false;

	gEngfuncs.Con_DPrintf("[IMGUIExtension] Loading font atlas...\n");

	std::vector<const rapidjson::Value*> baseFonts;
	std::vector<const rapidjson::Value*> mergedFonts;

	for (rapidjson::SizeType i = 0; i < fontsArr.Size(); ++i)
	{
		if (!fontsArr[i].IsObject()) continue;
		const auto& fObj = fontsArr[i];
		bool merge = false;
		if (fObj.HasMember("merge") && fObj["merge"].IsBool())
			merge = fObj["merge"].GetBool();

		if (merge)
			mergedFonts.push_back(&fObj);
		else
			baseFonts.push_back(&fObj);
	}

	auto processFontObj = [&](const rapidjson::Value& fObj) {
		if (!fObj.HasMember("file") || !fObj["file"].IsString()) return;
		std::string filename = fObj["file"].GetString();

		float size = 18.0f;
		if (fObj.HasMember("size") && fObj["size"].IsNumber())
			size = fObj["size"].GetFloat();

		bool merge = false;
		if (fObj.HasMember("merge") && fObj["merge"].IsBool())
			merge = fObj["merge"].GetBool();

		if (merge && !hasBaseFont)
		{
			// AddFontDefault() with no ImFontConfig uses an *implicit* reference size.
			// Merging a font that has an *explicit* size (e.g. "size": 18.0 from JSON)
			// onto an implicit-size base trips ImGui's assert in imgui_draw.cpp:
			// "Cannot use MergeMode with an explicit reference size when the
			//  destination font used an implicit reference size!"
			// Giving the fallback base font an explicit SizePixels avoids the mismatch.
			ImFontConfig defaultFontConfig;
			defaultFontConfig.SizePixels = size > 0.0f ? size : 13.0f;
			io.Fonts->AddFontDefault(&defaultFontConfig);
			hasBaseFont = true;
			gEngfuncs.Con_Printf("[IMGUIExtension] No base font found before merge target '%s' - loaded built-in default font as base (this usually means every font in imguiextension.json is marked \"merge\": true; add a non-merge base font).\n", filename.c_str());
		}

		char szResolvedPath[MAX_PATH] = { 0 };
		std::string fsPath = "imguiextension/fonts/" + filename;
		if (!FILESYSTEM_ANY_GETLOCALPATH(fsPath.c_str(), szResolvedPath, sizeof(szResolvedPath)))
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Failed to resolve local path for font file '%s'\n", filename.c_str());
			return;
		}

		for (char* p = szResolvedPath; *p; ++p)
			if (*p == '\\') *p = '/';

		const ImWchar* glyph_ranges_ptr = nullptr;
		if (fObj.HasMember("ranges") && fObj["ranges"].IsArray())
		{
			const auto& rArr = fObj["ranges"];
			for (rapidjson::SizeType r = 0; r < rArr.Size(); ++r)
			{
				if (!rArr[r].IsString()) continue;
				std::string rName = rArr[r].GetString();
				if (rName == "latin" || rName == "cyrillic")
				{
					glyph_ranges_ptr = latin_extended_glyph_ranges;
				}
				else if (rName == "symbols")
				{
					glyph_ranges_ptr = symbols_glyph_ranges;
				}
				else if (rName == "default")
				{
					glyph_ranges_ptr = io.Fonts->GetGlyphRangesDefault();
				}
				else if (rName == "korean")
				{
					glyph_ranges_ptr = io.Fonts->GetGlyphRangesKorean();
				}
				else if (rName == "japanese")
				{
					glyph_ranges_ptr = io.Fonts->GetGlyphRangesJapanese();
				}
				else if (rName == "chinese")
				{
					glyph_ranges_ptr = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
				}
			}
		}

		ImFontConfig config;
		config.MergeMode = merge;

		if (io.Fonts->AddFontFromFileTTF(szResolvedPath, size, merge ? &config : nullptr, glyph_ranges_ptr) != nullptr)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Loaded font: %s (Size: %.1f, Merge: %d)\n", filename.c_str(), size, merge);
			loadedAny = true;
			if (!merge)
			{
				hasBaseFont = true;
			}
		}
		else
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Failed to load font file: %s\n", szResolvedPath);
		}
	};

	for (const auto* pObj : baseFonts)
	{
		processFontObj(*pObj);
	}
	for (const auto* pObj : mergedFonts)
	{
		processFontObj(*pObj);
	}

	if (loadedAny)
	{
		if (GetDeveloperLevel() > 1)
		{
			unsigned char* tex_pixels;
			int tex_w, tex_h;
			io.Fonts->GetTexDataAsAlpha8(&tex_pixels, &tex_w, &tex_h);
			int bytes = tex_w * tex_h * 1;
			float mb = (float)bytes / (1024.0f * 1024.0f);
			gEngfuncs.Con_DPrintf("[IMGUIExtension] Font atlas compiled successfully: %dx%d (Alpha8), VRAM: %.2f MB\n", tex_w, tex_h, mb);
		}
		else
		{
			gEngfuncs.Con_DPrintf("[IMGUIExtension] Font atlas compiled successfully.\n");
		}
	}
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] No fonts loaded. Falling back to default font.\n");
		io.Fonts->AddFontDefault();
	}
}
