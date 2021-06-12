#include "build_info.h"
#include "exception.h"
#include "utils.h"
#include <fstream>
#include <filesystem>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include <assert.h>



const char *CBuildInfo::FindDateString(uint8_t *startAddr, int maxLen) const
{
    const int dateStringLen = 11;
    maxLen -= dateStringLen;

    for (int i = 0; i < maxLen; ++i)
    { 
        int index = 0;
        const char *text = reinterpret_cast<const char*>(startAddr + i);
        if (Utils::IsSymbolAlpha(text[index++]) &&
            Utils::IsSymbolAlpha(text[index++]) &&
            Utils::IsSymbolAlpha(text[index++]) &&
            Utils::IsSymbolSpace(text[index++]))
        {
            if (Utils::IsSymbolDigit(text[index]) || Utils::IsSymbolSpace(text[index]))
            {
                ++index;
                if (Utils::IsSymbolDigit(text[index++]) &&
                    Utils::IsSymbolSpace(text[index++]) &&
                    Utils::IsSymbolDigit(text[index++]) &&
                    Utils::IsSymbolDigit(text[index++]) &&
                    Utils::IsSymbolDigit(text[index++]))
                        return text;
            }
        }
    }
    return nullptr;
}

bool CBuildInfo::LoadBuildInfoFile(std::vector<uint8_t> &fileContents)
{
    std::wstring libraryDirPath;
    Utils::GetLibraryDirectory(libraryDirPath);
    std::ifstream file(std::filesystem::path(libraryDirPath + L"\\build_info.json"), std::ios::in | std::ios::binary);
    fileContents.clear();
    if (file.is_open())
    {
#ifdef max
#undef max
        file.ignore(std::numeric_limits<std::streamsize>::max());
#endif
        size_t length = file.gcount();
        file.clear();
        file.seekg(0, std::ios_base::beg);
        fileContents.reserve(length);
        fileContents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        fileContents.push_back('\0');
        file.close();
        return true;
    }
    else {
        return false;
    }
}

int CBuildInfo::DateToBuildNumber(const char *date) const
{
    int m = 0, d = 0, y = 0;
    static int b = 0;
    static int monthDaysCount[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    static const char *monthNames[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    if (b != 0) 
        return b;

    for (m = 0; m < 11; m++)
    {
        if (!strnicmp(&date[0], monthNames[m], 3))
            break;
        d += monthDaysCount[m];
    }

    d += atoi(&date[4]) - 1;
    y = atoi(&date[7]) - 1900;
    b = d + (int)((y - 1) * 365.25f);

    if((y % 4 == 0) && m > 1)
    {
        b += 1;
    }
    b -= 34995;

    return b;
}

void CBuildInfo::ParseBuildInfo(std::vector<uint8_t> &fileContents)
{
    rapidjson::Document doc;
    doc.Parse(reinterpret_cast<const char *>(fileContents.data()));

    if (!doc.IsObject()) {
        EXCEPT("JSON: build info document root is not object");
    }
    if (!doc.HasMember("build_number_signatures")) {
        EXCEPT("JSON: build info document hasn't member build_number_signatures");
    }
    if (!doc.HasMember("engine_builds_info")) {
        EXCEPT("JSON: build info document hasn't member engine_builds_info");
    }

    const rapidjson::Value &buildSignatures = doc["build_number_signatures"];
    for (size_t i = 0; i < buildSignatures.Size(); ++i) {
        m_BuildNumberSignatures.emplace_back(buildSignatures[i].GetString());
    }

    const rapidjson::Value &engineBuilds = doc["engine_builds_info"];
    for (size_t i = 0; i < engineBuilds.Size(); ++i)
    {
        CBuildInfoEntry infoEntry;
        const rapidjson::Value &entryObject = engineBuilds[i];
        const rapidjson::Value &signatures = entryObject["signatures"];
        infoEntry.SetBuildNumber(entryObject["number"].GetInt());
        infoEntry.SetFunctionPattern(FUNCTYPE_SPR_LOAD, CMemoryPattern(signatures["SPR_Load"].GetString()));
        infoEntry.SetFunctionPattern(FUNCTYPE_SPR_FRAMES, CMemoryPattern(signatures["SPR_Frames"].GetString()));
        infoEntry.SetFunctionPattern(FUNCTYPE_PRECACHE_MODEL, CMemoryPattern(signatures["PrecacheModel"].GetString()));
        infoEntry.SetFunctionPattern(FUNCTYPE_PRECACHE_SOUND, CMemoryPattern(signatures["PrecacheSound"].GetString()));
        m_InfoEntries.push_back(infoEntry);
    }
}

void *CBuildInfo::FindFunctionAddress(FunctionType funcType, void *startAddr, void *endAddr) const
{
    int currBuildNumber = GetBuildNumber();
    const int buildInfoLen = m_InfoEntries.size();
    const int lastEntryIndex = buildInfoLen - 1;
    CMemoryPattern funcPattern = m_InfoEntries[lastEntryIndex].GetFunctionPattern(funcType);

    for (int i = 0; i < lastEntryIndex; ++i)
    {
        const CBuildInfoEntry &buildInfo = m_InfoEntries[i];
        const CBuildInfoEntry &nextBuildInfo = m_InfoEntries[i + 1];

        // valid only if build info entries sorted ascending
        if (nextBuildInfo.GetBuildNumber() > currBuildNumber)
        {
            funcPattern = buildInfo.GetFunctionPattern(funcType);
            break;
        }
    }

    if (!endAddr)
        endAddr = (uint8_t*)startAddr + funcPattern.GetLength();

    return Utils::FindPatternAddress(
        startAddr,
        endAddr,
        funcPattern
    );
}

bool CBuildInfo::FindBuildNumberFunc(const moduleinfo_t &engineModule)
{
    uint8_t *moduleStartAddr = engineModule.baseAddr;
    uint8_t *moduleEndAddr = moduleStartAddr + engineModule.imageSize;
    for (size_t i = 0; i < m_BuildNumberSignatures.size(); ++i)
    {
        m_pfnGetBuildNumber = (int(*)())Utils::FindPatternAddress(
            moduleStartAddr,
            moduleEndAddr,
            m_BuildNumberSignatures[i]
        );

        if (m_pfnGetBuildNumber && m_pfnGetBuildNumber() > 0)
            return true;
    }
    return false;
}

bool CBuildInfo::ApproxBuildNumber(const moduleinfo_t &engineModule)
{
    CMemoryPattern datePattern("Jan", 4);
    uint8_t *moduleStartAddr = engineModule.baseAddr;
    uint8_t *moduleEndAddr = moduleStartAddr + engineModule.imageSize;
    uint8_t *scanStartAddr = moduleStartAddr;
    while (true)
    {
        uint8_t *stringAddr = (uint8_t*)Utils::FindPatternAddress(
            scanStartAddr, moduleEndAddr, datePattern
        );

        if (stringAddr)
        {
            const int scanOffset = 64; // offset for finding date string
            uint8_t *probeAddr = stringAddr - scanOffset;
            const char *dateString = FindDateString(probeAddr, scanOffset);
            if (dateString)
            {
                m_iBuildNumber = DateToBuildNumber(dateString);
                return true;
            }
            else
            {
                scanStartAddr = stringAddr + 1;
                continue;
            }
        }
        else
            return false;
    }
}

void CBuildInfo::Initialize(const moduleinfo_t &engineModule)
{
    std::vector<uint8_t> fileContents;
    if (LoadBuildInfoFile(fileContents)) {
        ParseBuildInfo(fileContents);
    }
    else {
        EXCEPT("failed to load build info file");
    }

    if (!FindBuildNumberFunc(engineModule))
    {
        if (!ApproxBuildNumber(engineModule)) {
            EXCEPT("failed to approximate engine build number");
        }
    }
}

int CBuildInfo::GetBuildNumber() const
{
    if (m_pfnGetBuildNumber)
        return m_pfnGetBuildNumber();
    else if (m_iBuildNumber)
        return m_iBuildNumber;
    else
        return 0;
}