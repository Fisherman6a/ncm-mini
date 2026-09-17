#include "PlaybackAccessibility.h"
#include <wrl/client.h>
#include <deque>
#include <utility>

namespace ncmmini
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr long MaximumChildren = 64;
constexpr unsigned MaximumNodes = 256;
constexpr unsigned MaximumDepth = 16;

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
    struct Search { DWORD pid; std::vector<HWND> windows; } search{processId, {}};
    EnumChildWindows(playerWindow, [](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t name[128]{}; DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        GetClassNameW(window, name, 128);
        if (owner == search.pid && std::wstring(name) == L"Chrome_RenderWidgetHostHWND") search.windows.push_back(window);
        return search.windows.size() < 4;
    }, reinterpret_cast<LPARAM>(&search));
    // Store only indices, never COM interfaces that could outlive the caller's apartment.
    struct Hint { HWND window = nullptr; DWORD pid = 0; std::vector<long> path; };
    static thread_local Hint hint;
    for (const auto window : search.windows)
    {
        if (hint.window != window || hint.pid != processId) hint = {window, processId, {}};
        ComPtr<IAccessible> root;
        if (FAILED(AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
            reinterpret_cast<void**>(root.GetAddressOf())))) { hint.path.clear(); continue; }
        const auto state = ReadPlaybackControls(root.Get(), hint.path);
        if (state != PlaybackState::Unknown) return state;
    }
    return PlaybackState::Unknown;
}
}
