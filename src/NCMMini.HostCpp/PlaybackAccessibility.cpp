#include "PlaybackAccessibility.h"
#include <wrl/client.h>
#include <deque>
#include <algorithm>
#include <limits>
#include <utility>

namespace ncmmini
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr long MaximumChildren = 64;
constexpr unsigned MaximumNodes = 256;
constexpr unsigned MaximumDepth = 16;
constexpr unsigned MaximumRenderers = 4;
constexpr unsigned MaximumDetachedHosts = 32;
constexpr ULONGLONG RefreshIntervalMs = 500;
constexpr ULONGLONG RecoveryIntervalMs = 2000;

void RequestPageTree(HWND window, IAccessible* root)
{
    // Chromium's WindowsAccessibilityEnabler requires both accName and its
    // WM_GETOBJECT honey-pot (object id 1) before enabling basic web contents.
    // OBJID_CLIENT alone can expose an empty document indefinitely.
    VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
    BSTR name = nullptr;
    root->get_accName(self, &name);
    SysFreeString(name);
    DWORD_PTR reply = 0;
    SendMessageTimeoutW(window, WM_GETOBJECT, 0, 1,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &reply);
    // A zero reply is normal; only a later, complete button group proves recovery.
}

bool RequestPageUpdate(IAccessible* root)
{
    VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
    long left = 0, top = 0, width = 0, height = 0;
    if (root->accLocation(&left, &top, &width, &height, self) != S_OK || width <= 0 || height <= 0)
        return false;
    const auto x = static_cast<std::int64_t>(left) + width / 2;
    const auto y = static_cast<std::int64_t>(top) + height / 2;
    if (x > std::numeric_limits<long>::max() || y > std::numeric_limits<long>::max()) return false;
    // Chromium sends this read-only hit test to the renderer, refreshing its cached
    // accessibility tree even while minimized. The response may arrive on a later poll.
    VARIANT hit{};
    const auto hr = root->accHitTest(static_cast<long>(x), static_cast<long>(y), &hit);
    const bool requested = hr == S_OK && (hit.vt == VT_I4 || (hit.vt == VT_DISPATCH && hit.pdispVal));
    VariantClear(&hit);
    return requested;
}

struct RendererSearch
{
    DWORD pid;
    std::vector<HWND> windows;
    unsigned detachedHosts = 0;
    bool overflow = false;
};

BOOL CALLBACK CollectRenderer(HWND window, LPARAM data)
{
    auto& search = *reinterpret_cast<RendererSearch*>(data);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search.pid) return TRUE;
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (std::wstring(name) != L"Chrome_RenderWidgetHostHWND") return TRUE;
    if (std::find(search.windows.begin(), search.windows.end(), window) != search.windows.end()) return TRUE;
    if (search.windows.size() >= MaximumRenderers) { search.overflow = true; return FALSE; }
    search.windows.push_back(window);
    return TRUE;
}

BOOL CALLBACK CollectDetachedRenderer(HWND window, LPARAM data)
{
    auto& search = *reinterpret_cast<RendererSearch*>(data);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search.pid) return TRUE;
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (std::wstring(name) != L"Chrome_WidgetWin_0") return TRUE;
    if (++search.detachedHosts > MaximumDetachedHosts) { search.overflow = true; return FALSE; }
    EnumChildWindows(window, CollectRenderer, data);
    return !search.overflow;
}

struct Element
{
    ComPtr<IAccessible> object;
    long child = CHILDID_SELF;
};

Element ChildElement(IAccessible* parent, const VARIANT& value)
{
    Element result;
    if (value.vt == VT_DISPATCH && value.pdispVal)
        value.pdispVal->QueryInterface(IID_PPV_ARGS(result.object.GetAddressOf()));
    else if (value.vt == VT_I4 && value.lVal != CHILDID_SELF)
    {
        ComPtr<IDispatch> dispatch;
        if (SUCCEEDED(parent->get_accChild(value, dispatch.GetAddressOf())) && dispatch)
            dispatch.As(&result.object);
        if (!result.object) { result.object = parent; result.child = value.lVal; }
    }
    return result;
}

bool Children(IAccessible* parent, std::vector<Element>& result)
{
    long count = 0;
    if (!parent || FAILED(parent->get_accChildCount(&count)) || count < 0 || count > MaximumChildren) return false;
    if (!count) return true;
    VARIANT values[MaximumChildren]{};
    long fetched = 0;
    const auto hr = AccessibleChildren(parent, 0, count, values, &fetched);
    if (SUCCEEDED(hr) && fetched == count)
        for (long i = 0; i < fetched; ++i) result.push_back(ChildElement(parent, values[i]));
    for (auto& value : values) VariantClear(&value);
    return SUCCEEDED(hr) && fetched == count;
}

std::wstring ButtonName(const Element& element, bool allowDisabled = false)
{
    if (!element.object) return {};
    VARIANT child{}; child.vt = VT_I4; child.lVal = element.child;
    VARIANT role{}, state{};
    const bool button = SUCCEEDED(element.object->get_accRole(child, &role))
        && role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PUSHBUTTON;
    const bool available = SUCCEEDED(element.object->get_accState(child, &state))
        && state.vt == VT_I4 && !(state.lVal & (STATE_SYSTEM_INVISIBLE | (allowDisabled ? 0 : STATE_SYSTEM_UNAVAILABLE)));
    VariantClear(&role); VariantClear(&state);
    if (!button || !available) return {};
    BSTR name = nullptr;
    const auto hr = element.object->get_accName(child, &name);
    const std::wstring result = SUCCEEDED(hr) && name ? std::wstring(name, SysStringLen(name)) : std::wstring();
    SysFreeString(name);
    return result;
}

PlaybackState GroupState(const std::vector<Element>& children)
{
    std::vector<std::wstring> names;
    for (const auto& child : children) names.push_back(ButtonName(child, true));
    PlaybackState result = PlaybackState::Unknown;
    unsigned matches = 0;
    for (std::size_t i = 1; i + 1 < names.size(); ++i)
    {
        if (names[i - 1] != L"pre" || names[i + 1] != L"next") continue;
        if (names[i] != L"play" && names[i] != L"pause") continue;
        if (ButtonName(children[i]) != names[i]) continue;
        result = names[i] == L"pause" ? PlaybackState::Playing : PlaybackState::Paused;
        ++matches;
    }
    return matches == 1 ? result : PlaybackState::Unknown;
}

ComPtr<IAccessible> Follow(IAccessible* root, const std::vector<long>& path)
{
    ComPtr<IAccessible> current = root;
    if (path.size() > MaximumDepth) return {};
    for (const auto offset : path)
    {
        if (!current || offset < 0 || offset >= MaximumChildren) return {};
        VARIANT value{};
        long fetched = 0;
        const auto hr = AccessibleChildren(current.Get(), offset, 1, &value, &fetched);
        const auto next = SUCCEEDED(hr) && fetched == 1 ? ChildElement(current.Get(), value) : Element{};
        VariantClear(&value);
        if (next.child != CHILDID_SELF) return {};
        current = next.object;
    }
    return current;
}
}

PlaybackState ReadPlaybackControls(IAccessible* root, std::vector<long>& path)
{
    if (!root) { path.clear(); return PlaybackState::Unknown; }
    const auto cached = Follow(root, path);
    std::vector<Element> children;
    if (cached && Children(cached.Get(), children))
    {
        const auto state = GroupState(children);
        if (state != PlaybackState::Unknown) return state;
    }
    path.clear();
    struct Pending { ComPtr<IAccessible> object; std::vector<long> path; };
    std::deque<Pending> pending;
    pending.push_back({root, {}});
    const auto deadline = GetTickCount64() + 400;
    unsigned visited = 0;
    // Breadth-first search reaches the player bar without traversing every playlist row.
    while (!pending.empty() && visited++ < MaximumNodes && GetTickCount64() < deadline)
    {
        auto node = std::move(pending.front()); pending.pop_front();
        children.clear();
        if (!Children(node.object.Get(), children)) continue;
        const auto state = GroupState(children);
        if (state != PlaybackState::Unknown) { path = std::move(node.path); return state; }
        if (node.path.size() >= MaximumDepth) continue;
        for (std::size_t i = 0; i < children.size() && visited + pending.size() < MaximumNodes; ++i)
        {
            const auto& child = children[i];
            if (!child.object || child.child != CHILDID_SELF) continue;
            auto childPath = node.path; childPath.push_back(static_cast<long>(i));
            pending.push_back({child.object, std::move(childPath)});
        }
    }
    return PlaybackState::Unknown;
}

PlaybackState ReadAccessiblePlayback(HWND playerWindow, DWORD processId)
{
    DWORD owner = 0;
    if (!playerWindow || !processId || !IsWindow(playerWindow)
        || !GetWindowThreadProcessId(playerWindow, &owner) || owner != processId || IsHungAppWindow(playerWindow))
        return PlaybackState::Unknown;
    RendererSearch search{processId, {}};
    EnumChildWindows(playerWindow, CollectRenderer, reinterpret_cast<LPARAM>(&search));
    const bool minimized = IsIconic(playerWindow) != FALSE;
    if (minimized)
    {
        // NetEase reparents its renderer to a separate CEF host while minimized.
        // Keep attached renderers too: a minimized page can temporarily have both
        // parent arrangements during a renderer transition.
        EnumWindows(CollectDetachedRenderer, reinterpret_cast<LPARAM>(&search));
    }
    if (search.overflow) return PlaybackState::Unknown;
    // Store only indices, never COM interfaces that could outlive the caller's apartment.
    struct Hint
    {
        HWND window;
        std::vector<long> path;
        ULONGLONG nextRefresh = 0;
        bool refreshRequested = false;
        ULONGLONG nextRecovery = 0;
    };
    struct Cache { HWND player = nullptr; DWORD pid = 0; bool minimized = false; std::vector<Hint> hints; };
    static thread_local Cache cache;
    if (cache.player != playerWindow || cache.pid != processId || cache.minimized != minimized)
        cache = {playerWindow, processId, minimized, {}};
    cache.hints.erase(std::remove_if(cache.hints.begin(), cache.hints.end(), [&](const Hint& hint) {
        return std::find(search.windows.begin(), search.windows.end(), hint.window) == search.windows.end();
    }), cache.hints.end());
    PlaybackState result = PlaybackState::Unknown;
    unsigned matches = 0;
    bool unresolved = false;
    for (const auto window : search.windows)
    {
        auto found = std::find_if(cache.hints.begin(), cache.hints.end(),
            [window](const Hint& hint) { return hint.window == window; });
        if (found == cache.hints.end())
        {
            cache.hints.push_back({window, {}});
            found = cache.hints.end() - 1;
        }
        auto& hint = *found;
        ComPtr<IAccessible> root;
        const auto accessible = AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
            reinterpret_cast<void**>(root.GetAddressOf()));
        if (FAILED(accessible) || !root)
        {
            hint.path.clear(); hint.nextRefresh = 0; hint.refreshRequested = false;
            unresolved = minimized;
            continue;
        }
        long childCount = 0;
        if (FAILED(root->get_accChildCount(&childCount)) || childCount <= 0)
        {
            hint.path.clear(); hint.nextRefresh = 0; hint.refreshRequested = false;
            if (GetTickCount64() >= hint.nextRecovery)
            {
                RequestPageTree(window, root.Get());
                hint.nextRecovery = GetTickCount64() + RecoveryIntervalMs;
            }
            unresolved = true;
            continue;
        }
        if (minimized)
        {
            if (GetTickCount64() >= hint.nextRefresh)
            {
                hint.refreshRequested = RequestPageUpdate(root.Get());
                hint.nextRefresh = GetTickCount64() + RefreshIntervalMs;
            }
            if (!hint.refreshRequested) { hint.path.clear(); unresolved = true; continue; }
        }
        const auto state = ReadPlaybackControls(root.Get(), hint.path);
        if (state != PlaybackState::Unknown)
        {
            if (!minimized) return state;
            result = state;
            ++matches;
        }
        else if (minimized)
        {
            unresolved = true;
        }
    }
    return !unresolved && matches == 1 ? result : PlaybackState::Unknown;
}
}
