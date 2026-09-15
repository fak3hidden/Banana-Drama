#include "update_check.h"

#include "../build_info.h"
#include "json.h"
#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace bd::update_check {
namespace {

constexpr wchar_t kHost[] = L"api.github.com";
constexpr wchar_t kPath[] =
    L"/repos/fak3hidden/Banana-Drama/commits?sha=refs/heads/arena/01a09c74-banana-drama&per_page=1";

State g_state = State::Idle;
Info g_info;
std::string g_error;

void Fail(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    g_error = buffer;
    g_state = State::Failed;
    log::Error("update: %s", buffer);
}

} // namespace

void Check()
{
    g_state = State::Idle;
    g_info = Info{};
    g_error.clear();

    const HINTERNET session =
        WinHttpOpen(L"BananaDrama/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        Fail("WinHttpOpen failed (%lu)", GetLastError());
        return;
    }

    // Keep it short: this runs on the game's own thread and stalls the frame.
    WinHttpSetTimeouts(session, 2000, 2000, 2000, 3000);

    const HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        Fail("WinHttpConnect failed (%lu)", GetLastError());
        WinHttpCloseHandle(session);
        return;
    }

    const HINTERNET request =
        WinHttpOpenRequest(connection, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        Fail("WinHttpOpenRequest failed (%lu)", GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return;
    }

    const std::wstring headers = L"Accept: application/vnd.github+json\r\n"
                                 L"User-Agent: BananaDrama\r\n";
    const BOOL sent = WinHttpSendRequest(request, headers.c_str(),
                                         static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA,
                                         0, 0, 0);
    const BOOL received = sent && WinHttpReceiveResponse(request, nullptr);

    std::string body;
    if (received) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                break;
            std::vector<char> chunk(static_cast<std::size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
                break;
            body.append(chunk.data(), static_cast<std::size_t>(read));
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!sent || !received) {
        Fail("the request to GitHub failed (%lu)", GetLastError());
        return;
    }
    if (body.empty()) {
        Fail("GitHub sent an empty reply");
        return;
    }

    std::string error;
    const json::Value root = json::parse(body, &error);
    if (!root.isArray() || root.size() == 0) {
        Fail("could not read GitHub's reply: %s", error.c_str());
        return;
    }

    const json::Value& latest = root.at(0);
    if (const json::Value* sha = latest.find("sha"))
        g_info.sha = sha->asString();

    if (const json::Value* commit = latest.find("commit")) {
        if (const json::Value* message = commit->find("message")) {
            g_info.message = message->asString();
            const std::size_t newline = g_info.message.find('\n');
            if (newline != std::string::npos)
                g_info.message.resize(newline);
        }
        if (const json::Value* author = commit->find("author")) {
            if (const json::Value* date = author->find("date"))
                g_info.date = date->asString();
        }
    }

    if (g_info.sha.empty()) {
        Fail("GitHub's reply had no commit hash");
        return;
    }

    const std::int64_t committed = build_info::EpochFromIso(g_info.date);
    const std::int64_t compiled = build_info::UtcTime();
    const bool outdated = committed > compiled + 60;

    g_state = outdated ? State::Outdated : State::UpToDate;
    log::Info("update: newest commit %s from %s, this dll was built %s",
              g_info.sha.substr(0, 7).c_str(), g_info.date.c_str(), build_info::Stamp());
}

State GetState()
{
    return g_state;
}

const Info& GetInfo()
{
    return g_info;
}

const std::string& GetError()
{
    return g_error;
}

} // namespace bd::update_check
