#include "AccessibilityLayer.h"

#include <OleAuto.h>
#include <comdef.h>

#include <sstream>
#include <stdexcept>

#include "Logger.h"

namespace iee {
namespace {

std::wstring SafeGetBstrProperty(HRESULT hr, const _bstr_t& value) {
    if (FAILED(hr) || value.length() == 0) {
        return L"";
    }

    return static_cast<const wchar_t*>(value);
}

}  // namespace

AccessibilityLayer::AccessibilityLayer() {
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initResult)) {
        ownsComApartment_ = true;
    } else if (initResult != RPC_E_CHANGED_MODE) {
        Logger::Error("AccessibilityLayer", "CoInitializeEx failed");
        throw std::runtime_error("Failed to initialize COM for accessibility layer");
    }

    if (!EnsureAutomation()) {
        Logger::Error("AccessibilityLayer", "Failed to create UI Automation instance");
        throw std::runtime_error("Failed to initialize UI Automation");
    }
}

AccessibilityLayer::~AccessibilityLayer() {
    automation_.Reset();
    if (ownsComApartment_) {
        CoUninitialize();
    }
}

bool AccessibilityLayer::EnsureAutomation() {
    if (automation_) {
        return true;
    }

    const HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation_));
    if (FAILED(hr) || !automation_) {
        return false;
    }

    return true;
}

std::vector<UiElement> AccessibilityLayer::CaptureTree(HWND windowHandle) {
    std::vector<UiElement> elements;
    if (!windowHandle || !EnsureAutomation()) {
        return elements;
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
    const HRESULT hr = automation_->ElementFromHandle(windowHandle, &root);
    if (FAILED(hr) || !root) {
        return elements;
    }

    return WalkTree(root.Get(), 0, 8, "");
}

std::optional<UiElement> AccessibilityLayer::FindElementByLabel(HWND windowHandle, const std::wstring& label) {
    auto element = FindAutomationElementByLabel(windowHandle, label);
    if (!element) {
        return std::nullopt;
    }

    return BuildUiElement(element.Get(), 0, "");
}

bool AccessibilityLayer::Activate(HWND windowHandle, const std::wstring& label) {
    auto element = FindAutomationElementByLabel(windowHandle, label);
    if (!element) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invokePattern;
    HRESULT hr = element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePattern));
    if (SUCCEEDED(hr) && invokePattern) {
        hr = invokePattern->Invoke();
        return SUCCEEDED(hr);
    }

    Microsoft::WRL::ComPtr<IUIAutomationSelectionItemPattern> selectionPattern;
    hr = element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionPattern));
    if (SUCCEEDED(hr) && selectionPattern) {
        hr = selectionPattern->Select();
        return SUCCEEDED(hr);
    }

    return false;
}

bool AccessibilityLayer::SetValue(HWND windowHandle, const std::wstring& label, const std::wstring& value) {
    auto element = FindAutomationElementByLabel(windowHandle, label);
    if (!element) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationValuePattern> valuePattern;
    HRESULT hr = element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern));
    if (FAILED(hr) || !valuePattern) {
        return false;
    }

    BSTR writeValue = SysAllocString(value.c_str());
    if (!writeValue) {
        return false;
    }

    hr = valuePattern->SetValue(writeValue);
    SysFreeString(writeValue);
    return SUCCEEDED(hr);
}

bool AccessibilityLayer::Select(HWND windowHandle, const std::wstring& label) {
    auto element = FindAutomationElementByLabel(windowHandle, label);
    if (!element) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationSelectionItemPattern> selectionPattern;
    HRESULT hr = element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionPattern));
    if (SUCCEEDED(hr) && selectionPattern) {
        hr = selectionPattern->Select();
        return SUCCEEDED(hr);
    }

    Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invokePattern;
    hr = element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePattern));
    if (SUCCEEDED(hr) && invokePattern) {
        hr = invokePattern->Invoke();
        return SUCCEEDED(hr);
    }

    return false;
}

std::vector<UiElement> AccessibilityLayer::WalkTree(IUIAutomationElement* root, int depth, int maxDepth, const std::string& parentId) {
    std::vector<UiElement> elements;
    if (!root || depth > maxDepth) {
        return elements;
    }

    UiElement current = BuildUiElement(root, depth, parentId);
    elements.push_back(current);

    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
    HRESULT hr = automation_->CreateTrueCondition(&condition);
    if (FAILED(hr) || !condition) {
        return elements;
    }

    Microsoft::WRL::ComPtr<IUIAutomationElementArray> children;
    hr = root->FindAll(TreeScope_Children, condition.Get(), &children);
    if (FAILED(hr) || !children) {
        return elements;
    }

    int length = 0;
    children->get_Length(&length);
    for (int index = 0; index < length; ++index) {
        Microsoft::WRL::ComPtr<IUIAutomationElement> child;
        if (SUCCEEDED(children->GetElement(index, &child)) && child) {
            auto childElements = WalkTree(child.Get(), depth + 1, maxDepth, current.id);
            elements.insert(elements.end(), childElements.begin(), childElements.end());
        }
    }

    return elements;
}

UiElement AccessibilityLayer::BuildUiElement(IUIAutomationElement* element, int depth, const std::string& parentId) {
    UiElement uiElement;
    if (!element) {
        return uiElement;
    }

    _bstr_t name;
    _bstr_t automationId;
    _bstr_t className;

    const HRESULT nameHr = element->get_CurrentName(name.GetAddress());
    const HRESULT automationIdHr = element->get_CurrentAutomationId(automationId.GetAddress());
    const HRESULT classNameHr = element->get_CurrentClassName(className.GetAddress());

    CONTROLTYPEID controlTypeId = UIA_CustomControlTypeId;
    element->get_CurrentControlType(&controlTypeId);

    BOOL isEnabled = FALSE;
    BOOL isOffscreen = FALSE;

    element->get_CurrentIsEnabled(&isEnabled);
    element->get_CurrentIsOffscreen(&isOffscreen);
    element->get_CurrentBoundingRectangle(&uiElement.bounds);

    uiElement.isEnabled = isEnabled != FALSE;
    uiElement.isOffscreen = isOffscreen != FALSE;
    uiElement.name = SafeGetBstrProperty(nameHr, name);
    uiElement.automationId = SafeGetBstrProperty(automationIdHr, automationId);
    uiElement.className = SafeGetBstrProperty(classNameHr, className);
    uiElement.controlType = ControlTypeFromUiaId(controlTypeId);
    uiElement.parentId = parentId;
    uiElement.depth = depth;
    uiElement.id = BuildElementId(element);
    uiElement.lastInteractionTicks = static_cast<std::uint64_t>(GetTickCount64());

    BOOL hasKeyboardFocus = FALSE;
    if (SUCCEEDED(element->get_CurrentHasKeyboardFocus(&hasKeyboardFocus))) {
        uiElement.isFocused = hasKeyboardFocus != FALSE;
    }

    Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invokePattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invokePattern))) && invokePattern) {
        uiElement.supportsInvoke = true;
    }

    Microsoft::WRL::ComPtr<IUIAutomationValuePattern> valuePattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&valuePattern))) && valuePattern) {
        uiElement.supportsValue = true;
        _bstr_t currentValue;
        if (SUCCEEDED(valuePattern->get_CurrentValue(currentValue.GetAddress()))) {
            uiElement.value = SafeGetBstrProperty(S_OK, currentValue);
        }
    }

    Microsoft::WRL::ComPtr<IUIAutomationSelectionItemPattern> selectionPattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selectionPattern))) && selectionPattern) {
        uiElement.supportsSelection = true;
    }

    return uiElement;
}

UiControlType AccessibilityLayer::ControlTypeFromUiaId(CONTROLTYPEID controlTypeId) const {
    switch (controlTypeId) {
    case UIA_WindowControlTypeId:
        return UiControlType::Window;
    case UIA_ButtonControlTypeId:
        return UiControlType::Button;
    case UIA_EditControlTypeId:
        return UiControlType::TextBox;
    case UIA_MenuControlTypeId:
        return UiControlType::Menu;
    case UIA_MenuItemControlTypeId:
        return UiControlType::MenuItem;
    case UIA_ComboBoxControlTypeId:
        return UiControlType::ComboBox;
    case UIA_ListItemControlTypeId:
        return UiControlType::ListItem;
    case UIA_DocumentControlTypeId:
        return UiControlType::Document;
    default:
        return UiControlType::Unknown;
    }
}

std::string AccessibilityLayer::BuildElementId(IUIAutomationElement* element) const {
    if (!element) {
        return "";
    }

    SAFEARRAY* runtimeIdArray = nullptr;
    if (SUCCEEDED(element->GetRuntimeId(&runtimeIdArray)) && runtimeIdArray != nullptr) {
        LONG lowerBound = 0;
        LONG upperBound = -1;
        std::ostringstream stream;
        stream << "rid";

        const bool boundsOk =
            SUCCEEDED(SafeArrayGetLBound(runtimeIdArray, 1, &lowerBound)) &&
            SUCCEEDED(SafeArrayGetUBound(runtimeIdArray, 1, &upperBound));

        if (boundsOk) {
            for (LONG index = lowerBound; index <= upperBound; ++index) {
                int part = 0;
                if (SUCCEEDED(SafeArrayGetElement(runtimeIdArray, &index, &part))) {
                    stream << '-' << part;
                }
            }
            SafeArrayDestroy(runtimeIdArray);
            return stream.str();
        }

        SafeArrayDestroy(runtimeIdArray);
    }

    RECT bounds{};
    element->get_CurrentBoundingRectangle(&bounds);

    std::ostringstream stream;
    stream << bounds.left << ':' << bounds.top << ':' << bounds.right << ':' << bounds.bottom;
    return stream.str();
}

Microsoft::WRL::ComPtr<IUIAutomationElement> AccessibilityLayer::FindAutomationElementByLabel(
    HWND windowHandle,
    const std::wstring& label) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
    if (!windowHandle || !EnsureAutomation() || FAILED(automation_->ElementFromHandle(windowHandle, &root)) || !root) {
        return nullptr;
    }

    VARIANT nameVariant;
    VariantInit(&nameVariant);
    nameVariant.vt = VT_BSTR;
    nameVariant.bstrVal = SysAllocString(label.c_str());

    Microsoft::WRL::ComPtr<IUIAutomationCondition> nameCondition;
    automation_->CreatePropertyCondition(UIA_NamePropertyId, nameVariant, &nameCondition);
    VariantClear(&nameVariant);

    if (nameCondition) {
        Microsoft::WRL::ComPtr<IUIAutomationElement> matched;
        if (SUCCEEDED(root->FindFirst(TreeScope_Subtree, nameCondition.Get(), &matched)) && matched) {
            return matched;
        }
    }

    VARIANT automationIdVariant;
    VariantInit(&automationIdVariant);
    automationIdVariant.vt = VT_BSTR;
    automationIdVariant.bstrVal = SysAllocString(label.c_str());

    Microsoft::WRL::ComPtr<IUIAutomationCondition> automationIdCondition;
    automation_->CreatePropertyCondition(UIA_AutomationIdPropertyId, automationIdVariant, &automationIdCondition);
    VariantClear(&automationIdVariant);

    if (automationIdCondition) {
        Microsoft::WRL::ComPtr<IUIAutomationElement> matched;
        if (SUCCEEDED(root->FindFirst(TreeScope_Subtree, automationIdCondition.Get(), &matched)) && matched) {
            return matched;
        }
    }

    return nullptr;
}

}  // namespace iee
