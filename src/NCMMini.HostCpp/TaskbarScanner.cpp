#include "TaskbarScanner.h"
#include "TaskbarDpi.h"

#include <uiautomation.h>
#include <wrl/client.h>

#include <iostream>

namespace ncmmini
{
namespace
{
using Microsoft::WRL::ComPtr;

TaskbarRect Convert(const RECT& rect)
{
    return {rect.left, rect.top, rect.right, rect.bottom};
}

std::wstring ReadString(IUIAutomationElement* element, bool className)
{
    BSTR value = nullptr;
    const HRESULT hr = className ? element->get_CurrentClassName(&value)
                                 : element->get_CurrentAutomationId(&value);
    std::wstring result;
    if (SUCCEEDED(hr) && value) result.assign(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}
}

TaskbarSnapshot ScanTaskbar(bool verbose)
{
    TaskbarSnapshot result;
    result.window = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT bounds{};
    if (!result.window || !GetWindowRect(result.window, &bounds)
        || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        result.error = L"Taskbar is not available";
        return result;
    }
    result.bounds = Convert(bounds);
    GetWindowThreadProcessId(result.window, &result.processId);
    result.dpi = TaskbarDpi::WindowDpi(result.window);
    if (!result.dpi) result.dpi = 96;
    if (bounds.bottom - bounds.top >= bounds.right - bounds.left)
    {
        result.error = L"The probe supports a horizontal primary taskbar only";
        return result;
    }
    if (!FindWindowExW(result.window, nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr))
    {
        result.error = L"Win11 taskbar bridge was not found";
        return result;
    }
    const HWND tray = FindWindowExW(result.window, nullptr, L"TrayNotifyWnd", nullptr);
    RECT trayBounds{};
    if (!tray || !GetWindowRect(tray, &trayBounds) || trayBounds.right <= trayBounds.left)
    {
        result.error = L"Cannot establish the notification-area boundary";
        return result;
    }
    result.occupied.push_back(Convert(trayBounds));

    ComPtr<IUIAutomation2> automation;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(automation.GetAddressOf()));
    if (FAILED(hr))
    {
        result.error = L"UI Automation initialization failed: " + std::to_wstring(hr);
        return result;
    }
    automation->put_ConnectionTimeout(1000);
    automation->put_TransactionTimeout(1000);
    ComPtr<IUIAutomationElement> root;
    ComPtr<IUIAutomationCondition> condition;
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(automation->ElementFromHandle(result.window, root.GetAddressOf()))
        || FAILED(automation->get_ControlViewCondition(condition.GetAddressOf()))
        || FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), elements.GetAddressOf())))
    {
        result.error = L"UI Automation taskbar scan failed";
        return result;
    }
    int count = 0;
    if (FAILED(elements->get_Length(&count)) || count <= 0 || count > 2048)
    {
        result.error = L"UI Automation returned an invalid element count";
        return result;
    }
    bool foundStart = false;
    for (int index = 0; index < count; ++index)
    {
        ComPtr<IUIAutomationElement> element;
        BOOL offscreen = TRUE;
        RECT rect{};
        CONTROLTYPEID type = 0;
        int processId = 0;
        if (FAILED(elements->GetElement(index, element.GetAddressOf()))
            || FAILED(element->get_CurrentProcessId(&processId)))
        {
            result.error = L"An element became unavailable during the scan";
            return result;
        }
        if (static_cast<DWORD>(processId) == GetCurrentProcessId()) continue;
        if (FAILED(element->get_CurrentIsOffscreen(&offscreen))
            || FAILED(element->get_CurrentBoundingRectangle(&rect))
            || FAILED(element->get_CurrentControlType(&type)))
        {
            result.error = L"An element boundary became unavailable";
            return result;
        }
        if (offscreen || rect.right <= rect.left || rect.bottom <= rect.top) continue;
        if (rect.bottom <= bounds.top || rect.top >= bounds.bottom
            || rect.right <= bounds.left || rect.left >= bounds.right) continue;
        const auto id = ReadString(element.Get(), false);
        const auto className = ReadString(element.Get(), true);
        if (verbose)
        {
            std::wcout << L"UIA id=" << id << L" class=" << className << L" type=" << type
                << L" rect=" << rect.left << L"," << rect.top << L"," << rect.right
                << L"," << rect.bottom << L'\n';
        }
        if (id == L"StartButton") foundStart = true;
        // Containers span empty space; reserve controls and content, including unknown buttons.
        const bool container = type == UIA_PaneControlTypeId || type == UIA_GroupControlTypeId
            || type == UIA_ToolBarControlTypeId || type == UIA_WindowControlTypeId;
        if (!container || id == L"WidgetsButton" || id == L"StartButton"
            || className == L"Taskbar.TaskListButtonAutomationPeer")
        {
            result.occupied.push_back(Convert(rect));
        }
    }
    DWORD currentProcessId = 0;
    GetWindowThreadProcessId(result.window, &currentProcessId);
    RECT currentBounds{};
    if (!foundStart || currentProcessId != result.processId || !GetWindowRect(result.window, &currentBounds)
        || !EqualRect(&bounds, &currentBounds))
    {
        result.error = L"Taskbar changed or StartButton could not be identified; retrying";
        return result;
    }
    result.valid = true;
    return result;
}
}
