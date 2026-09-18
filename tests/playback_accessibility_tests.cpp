#include "../src/NCMMini.HostCpp/PlaybackAccessibility.h"
#include <wrl/client.h>
#include <iostream>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace ncmmini;

// A small external MSAA provider fixture. Production discovery and classification run unchanged.
class Node final : public IAccessible
{
public:
    std::wstring name;
    long role = ROLE_SYSTEM_GROUPING;
    long state = 0;
    std::vector<ComPtr<Node>> children;
    ULONG references = 1;
    unsigned reads = 0;
    HRESULT failure = S_OK;
    long left = -32000, top = -32000, width = 2116, height = 1504;
    HRESULT locationResult = S_OK, hitResult = S_OK;
    unsigned hitTests = 0;
    Node* refreshTarget = nullptr;
    std::wstring refreshedName;
    bool deferRefresh = false;
    bool refreshRequested = false;
    bool treeUnavailable = false;
    bool nameRequested = false;
    bool activationCompletes = true;
    unsigned activationRequests = 0;
    Node* Add(const wchar_t* value, long kind = ROLE_SYSTEM_GROUPING) {
        auto* node = new Node; node->name = value; node->role = kind;
        ComPtr<Node> owner; owner.Attach(node); children.push_back(owner); return node;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IDispatch && id != IID_IAccessible) return E_NOINTERFACE;
        *out = static_cast<IAccessible*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --references; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override { ++reads; *count = treeUnavailable ? 0 : static_cast<long>(children.size()); return failure; }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT id, IDispatch** out) override {
        ++reads; *out = nullptr;
        if (FAILED(failure)) return failure;
        if (id.vt != VT_I4 || id.lVal <= 0 || id.lVal > static_cast<long>(children.size())) return E_INVALIDARG;
        *out = children[id.lVal - 1].Get(); (*out)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT, BSTR* value) override { ++reads; nameRequested = true; *value = SysAllocString(name.c_str()); return failure; }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT, VARIANT* value) override { ++reads; value->vt = VT_I4; value->lVal = role; return failure; }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT, VARIANT* value) override { value->vt = VT_I4; value->lVal = state; return failure; }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR*, VARIANT, long*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT, BSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accSelect(long, VARIANT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accLocation(long* x, long* y, long* w, long* h, VARIANT) override {
        *x = left; *y = top; *w = width; *h = height; return locationResult;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long, VARIANT, VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* out) override {
        ++hitTests;
        VariantInit(out);
        if (hitResult != S_OK) return hitResult;
        if (width <= 0 || height <= 0 || x < left || y < top
            || static_cast<long long>(x) >= static_cast<long long>(left) + width
            || static_cast<long long>(y) >= static_cast<long long>(top) + height) return E_INVALIDARG;
        refreshRequested = true;
        if (refreshTarget && !deferRefresh) refreshTarget->name = refreshedName;
        out->vt = VT_DISPATCH; out->pdispVal = this; AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_ACCESSDENIED; }
};

namespace
{
LRESULT CALLBACK RendererProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_GETOBJECT && lparam == 1)
    {
        auto* provider = reinterpret_cast<Node*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (provider)
        {
            ++provider->activationRequests;
            if (provider->nameRequested && provider->activationCompletes)
                PostMessageW(window, WM_APP, 0, 0);
        }
        return 0;
    }
    if (message == WM_APP)
    {
        auto* provider = reinterpret_cast<Node*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        provider->treeUnavailable = false;
        return 0;
    }
    if (message == WM_GETOBJECT && static_cast<DWORD>(lparam) == static_cast<DWORD>(OBJID_CLIENT))
    {
        auto* provider = reinterpret_cast<IAccessible*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        return provider ? LresultFromObject(IID_IAccessible, wparam, provider) : 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterTestWindow(const wchar_t* name, WNDPROC procedure = DefWindowProcW)
{
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = name;
    cls.lpfnWndProc = procedure;
    return RegisterClassW(&cls) != 0;
}

HWND TestWindow(const wchar_t* name, HWND parent = nullptr, IAccessible* provider = nullptr)
{
    // No WS_VISIBLE: these fixtures never appear on the desktop or change focus.
    return CreateWindowW(name, L"", parent ? WS_CHILD : WS_OVERLAPPED, 0, 0, 1, 1,
        parent, nullptr, GetModuleHandleW(nullptr), provider);
}

void WaitWithMessages(DWORD milliseconds)
{
    // The fixture owns real hidden windows; keep them responsive during timed tests.
    const auto deadline = GetTickCount64() + milliseconds;
    do
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount64() >= deadline) break;
        Sleep(5);
    } while (true);
}
}

int main()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    int failures = 0;
    const auto check = [&](bool value, const char* text) {
        std::cout << (value ? "PASS " : "FAIL ") << text << '\n'; if (!value) ++failures;
    };
    {
        ComPtr<Node> root; root.Attach(new Node);
        auto* list = root->Add(L"Playlist");
        list->Add(L"play", ROLE_SYSTEM_PUSHBUTTON);
        list->Add(L"play all", ROLE_SYSTEM_PUSHBUTTON);
        auto* bar = root->Add(L"")->Add(L"");
        bar->Add(L"loop", ROLE_SYSTEM_PUSHBUTTON);
        bar->Add(L"pre", ROLE_SYSTEM_PUSHBUTTON);
        auto* play = bar->Add(L"play", ROLE_SYSTEM_PUSHBUTTON);
        bar->Add(L"next", ROLE_SYSTEM_PUSHBUTTON);
        std::vector<long> hint;
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Paused, "transport play button means paused; list buttons are ignored");
        play->name = L"pause";
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Playing, "external state change selects pause without a local click");
        play->name = L"play";
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Paused, "pause is visible on the next read, without waiting for audio inactivity");
        std::swap(root->children[0], root->children[1]);
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Paused, "changed tree path is rediscovered and validated");
        play->name = L"playlist";
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Unknown, "unknown control names never fabricate playback");
        play->name = L"play"; play->state = STATE_SYSTEM_UNAVAILABLE;
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Unknown, "unavailable transport controls are unknown");
        play->state = 0; play->failure = E_FAIL;
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Unknown, "provider failure discards previously known state");
        play->failure = S_OK; bar->children[1]->state = STATE_SYSTEM_UNAVAILABLE;
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Paused, "disabled previous button does not hide the current playback state");
        bar->children[1]->state = 0; play->role = ROLE_SYSTEM_TEXT;
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Unknown, "text named play is not a playback button");
        play->role = ROLE_SYSTEM_PUSHBUTTON;
        bar->Add(L"pre", ROLE_SYSTEM_PUSHBUTTON);
        bar->Add(L"pause", ROLE_SYSTEM_PUSHBUTTON);
        bar->Add(L"next", ROLE_SYSTEM_PUSHBUTTON);
        check(ReadPlaybackControls(root.Get(), hint) == PlaybackState::Unknown, "ambiguous transport groups do not guess a state");
        ComPtr<Node> deep; deep.Attach(new Node);
        auto* leaf = deep.Get();
        for (int i = 0; i < 50; ++i) leaf = leaf->Add(L"");
        leaf->Add(L"pre", ROLE_SYSTEM_PUSHBUTTON); leaf->Add(L"play", ROLE_SYSTEM_PUSHBUTTON); leaf->Add(L"next", ROLE_SYSTEM_PUSHBUTTON);
        check(ReadPlaybackControls(deep.Get(), hint) == PlaybackState::Unknown, "discovery has a finite depth budget");
        check(ReadPlaybackControls(nullptr, hint) == PlaybackState::Unknown, "absent accessibility tree is unknown");
        check(ReadAccessiblePlayback(nullptr, 0) == PlaybackState::Unknown, "absent player window does not inspect another process");
    }
    {
        const bool registered = RegisterTestWindow(L"NcmPlaybackTestHost")
            && RegisterTestWindow(L"Chrome_WidgetWin_0")
            && RegisterTestWindow(L"Chrome_RenderWidgetHostHWND", RendererProcedure);
        check(registered, "hidden renderer fixture classes register");
        ComPtr<Node> root; root.Attach(new Node);
        auto* bar = root->Add(L"Transport");
        bar->Add(L"pre", ROLE_SYSTEM_PUSHBUTTON);
        auto* play = bar->Add(L"pause", ROLE_SYSTEM_PUSHBUTTON);
        bar->Add(L"next", ROLE_SYSTEM_PUSHBUTTON);
        const auto host = TestWindow(L"NcmPlaybackTestHost");
        const auto detachedHost = TestWindow(L"Chrome_WidgetWin_0");
        const auto renderer = TestWindow(L"Chrome_RenderWidgetHostHWND", host, root.Get());
        check(host && detachedHost && renderer, "hidden renderer fixtures are created");
        if (host && detachedHost && renderer)
        {
            const auto pid = GetCurrentProcessId();
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing, "attached renderer reports playing");
            SetParent(renderer, detachedHost);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "normal window does not adopt an unrelated detached renderer");
            SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) | WS_MINIMIZE);
            check(IsIconic(host) && !IsWindowVisible(host) && !IsWindowVisible(detachedHost),
                "minimized fixture stays hidden without desktop input");
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "minimized player finds its renderer under the detached CEF host");
            play->name = L"play";
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Paused,
                "detached minimized renderer reads an external pause on the next query");
            play->name = L"pause";
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "detached minimized renderer reads an external resume on the next query");
            play->state = STATE_SYSTEM_UNAVAILABLE;
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "detached disabled playback control does not invent a state");
            play->state = 0;
            check(ReadAccessiblePlayback(host, pid + 1) == PlaybackState::Unknown,
                "a mismatched player PID cannot use the detached renderer");
            const auto attachedRenderer = TestWindow(L"Chrome_RenderWidgetHostHWND", host, root.Get());
            check(attachedRenderer, "mixed attached and detached renderer fixture is created");
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "minimized mixed renderer parents remain ambiguous");
            if (attachedRenderer) DestroyWindow(attachedRenderer);

            const auto secondHost = TestWindow(L"Chrome_WidgetWin_0");
            ComPtr<Node> secondRoot; secondRoot.Attach(new Node);
            auto* secondBar = secondRoot->Add(L"Transport");
            secondBar->Add(L"pre", ROLE_SYSTEM_PUSHBUTTON);
            secondBar->Add(L"pause", ROLE_SYSTEM_PUSHBUTTON);
            secondBar->Add(L"next", ROLE_SYSTEM_PUSHBUTTON);
            const auto secondRenderer = TestWindow(L"Chrome_RenderWidgetHostHWND", secondHost, secondRoot.Get());
            check(secondHost && secondRenderer, "second detached renderer fixture is created");
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "multiple detached transport renderers are ambiguous even when states agree");
            const auto multipleHits = root->hitTests;
            const auto secondMultipleHits = secondRoot->hitTests;
            for (int i = 0; i < 3; ++i) ReadAccessiblePlayback(host, pid);
            check(root->hitTests == multipleHits && secondRoot->hitTests == secondMultipleHits,
                "multiple renderers retain independent refresh limits");
            WaitWithMessages(550);
            ReadAccessiblePlayback(host, pid);
            check(root->hitTests == multipleHits + 1 && secondRoot->hitTests == secondMultipleHits + 1,
                "each renderer refreshes after its own interval");
            secondRoot->failure = E_FAIL;
            WaitWithMessages(550);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "a failed renderer makes a mixed minimized scan unknown");
            secondRoot->failure = S_OK;
            if (secondHost) DestroyWindow(secondHost);
            SetParent(renderer, host);
            SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) & ~WS_MINIMIZE);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "restored player rediscovers the reattached renderer");

            const auto waitFor = [&](PlaybackState expected) {
                const auto deadline = GetTickCount64() + 1600;
                do {
                    if (ReadAccessiblePlayback(host, pid) == expected) return true;
                    WaitWithMessages(25);
                } while (GetTickCount64() < deadline);
                return false;
            };
            const auto hitsBefore = root->hitTests;
            root->refreshTarget = play;
            root->refreshedName = L"play";
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing && root->hitTests == hitsBefore,
                "non-minimized hidden window does not request extra page queries");
            SetParent(renderer, detachedHost);
            SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) | WS_MINIMIZE);
            check(waitFor(PlaybackState::Paused),
                "minimized frozen page refreshes an external pause using MSAA without restoring");
            check(IsIconic(host) && !IsWindowVisible(host) && GetParent(renderer) == detachedHost,
                "refresh leaves window state and renderer parent unchanged");
            const auto referencesBefore = root->references;
            root->refreshedName = L"pause";
            check(waitFor(PlaybackState::Playing),
                "periodic refresh observes resume while the window stays minimized");
            check(root->references == referencesBefore, "hit-test result releases its returned COM reference");
            const auto hitsAfter = root->hitTests;
            for (int i = 0; i < 3; ++i) ReadAccessiblePlayback(host, pid);
            check(root->hitTests == hitsAfter, "frequent polls do not flood the renderer with hit tests");
            root->refreshedName = L"play";
            root->hitResult = E_FAIL;
            check(waitFor(PlaybackState::Unknown), "failed refresh cannot validate a stale minimized state");
            root->hitResult = S_OK;
            check(waitFor(PlaybackState::Paused), "refresh retries recover from a transient provider failure");
            root->hitResult = S_FALSE;
            check(waitFor(PlaybackState::Unknown), "an empty hit-test response does not validate cached playback");
            root->hitResult = S_OK;
            check(waitFor(PlaybackState::Paused), "valid hit testing recovers after an empty response");

            root->deferRefresh = true;
            root->refreshRequested = false;
            root->refreshedName = L"pause";
            WaitWithMessages(550);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Paused && root->refreshRequested,
                "an asynchronous request does not fabricate an immediate playback change");
            play->name = L"pause";
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "the next poll observes a provider update delivered after the request");
            root->deferRefresh = false;

            root->width = 0;
            const auto invalidHits = root->hitTests;
            check(waitFor(PlaybackState::Unknown) && root->hitTests == invalidHits,
                "empty accessible bounds never produce a hit-test point");
            root->width = 2116;
            root->left = std::numeric_limits<long>::max();
            WaitWithMessages(550);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown && root->hitTests == invalidHits,
                "overflowing accessible coordinates are rejected");
            root->left = -32000;
            check(waitFor(PlaybackState::Playing), "negative minimized coordinates remain valid");

            SetParent(renderer, host);
            SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) & ~WS_MINIMIZE);
            const auto restoredHits = root->hitTests;
            play->name = L"play";
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Paused && root->hitTests == restoredHits,
                "restoring stops forced queries and keeps normal playback reads");
            root->refreshedName = L"pause";
            SetWindowLongPtrW(host, GWL_STYLE, GetWindowLongPtrW(host, GWL_STYLE) | WS_MINIMIZE);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "re-entering minimized mode queries immediately even with an attached renderer");

            // Model Chromium's empty document after a provider reset, including
            // recovery delivered on a later message rather than during the query.
            root->refreshTarget = nullptr;
            for (bool minimized : {false, true})
            {
                SetParent(renderer, minimized ? detachedHost : host);
                auto style = GetWindowLongPtrW(host, GWL_STYLE);
                SetWindowLongPtrW(host, GWL_STYLE, minimized ? style | WS_MINIMIZE : style & ~WS_MINIMIZE);
                root->treeUnavailable = true;
                root->nameRequested = false;
                const auto requests = root->activationRequests;
                check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                    "empty page never reuses the old playback value");
                check(root->activationRequests == requests + 1 && root->nameRequested,
                    "empty page requests Chromium accessibility and reads root name");
                play->name = L"play";
                WaitWithMessages(25);
                check(ReadAccessiblePlayback(host, pid) == PlaybackState::Paused,
                    "a later poll recovers paused state from the rebuilt page");
                play->name = L"pause";
                check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                    "rebuilt page continues tracking external resume");
            }
            root->treeUnavailable = true;
            root->activationCompletes = false;
            WaitWithMessages(2100);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Unknown,
                "unrecoverable page stays unknown");
            const auto recoveryRequests = root->activationRequests;
            for (int i = 0; i < 5; ++i) ReadAccessiblePlayback(host, pid);
            check(root->activationRequests == recoveryRequests,
                "repeated empty reads do not flood accessibility requests");
            root->activationCompletes = true;
            WaitWithMessages(2100);
            ReadAccessiblePlayback(host, pid);
            check(root->activationRequests == recoveryRequests + 1,
                "empty page recovery retries after the backoff");
            WaitWithMessages(25);
            check(ReadAccessiblePlayback(host, pid) == PlaybackState::Playing,
                "same renderer can recover again after a second tree reset");
        }
        if (host) DestroyWindow(host);
        if (detachedHost) DestroyWindow(detachedHost);
    }
    CoUninitialize();
    return failures ? 1 : 0;
}
