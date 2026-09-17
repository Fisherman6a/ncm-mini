#include "../src/NCMMini.HostCpp/PlaybackAccessibility.h"
#include <wrl/client.h>
#include <iostream>

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
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override { ++reads; *count = static_cast<long>(children.size()); return failure; }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT id, IDispatch** out) override {
        ++reads; *out = nullptr;
        if (FAILED(failure)) return failure;
        if (id.vt != VT_I4 || id.lVal <= 0 || id.lVal > static_cast<long>(children.size())) return E_INVALIDARG;
        *out = children[id.lVal - 1].Get(); (*out)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT, BSTR* value) override { ++reads; *value = SysAllocString(name.c_str()); return failure; }
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
    HRESULT STDMETHODCALLTYPE accLocation(long*, long*, long*, long*, VARIANT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accNavigate(long, VARIANT, VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accHitTest(long, long, VARIANT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_ACCESSDENIED; }
};

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
    CoUninitialize();
    return failures ? 1 : 0;
}
