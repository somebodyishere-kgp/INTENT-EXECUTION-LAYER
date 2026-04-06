#pragma once

#include <UIAutomation.h>
#include <Windows.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <vector>

#include "UiElement.h"

namespace iee {

class IAccessibilityLayer {
public:
    virtual ~IAccessibilityLayer() = default;

    virtual std::vector<UiElement> CaptureTree(HWND windowHandle) = 0;
    virtual std::optional<UiElement> FindElementByLabel(HWND windowHandle, const std::wstring& label) = 0;
    virtual bool Activate(HWND windowHandle, const std::wstring& label) = 0;
    virtual bool SetValue(HWND windowHandle, const std::wstring& label, const std::wstring& value) = 0;
    virtual bool Select(HWND windowHandle, const std::wstring& label) = 0;
};

class AccessibilityLayer : public IAccessibilityLayer {
public:
    AccessibilityLayer();
    ~AccessibilityLayer() override;

    std::vector<UiElement> CaptureTree(HWND windowHandle) override;
    std::optional<UiElement> FindElementByLabel(HWND windowHandle, const std::wstring& label) override;
    bool Activate(HWND windowHandle, const std::wstring& label) override;
    bool SetValue(HWND windowHandle, const std::wstring& label, const std::wstring& value) override;
    bool Select(HWND windowHandle, const std::wstring& label) override;

private:
    std::vector<UiElement> WalkTree(IUIAutomationElement* root, int depth, int maxDepth, const std::string& parentId);
    UiElement BuildUiElement(IUIAutomationElement* element, int depth, const std::string& parentId);
    UiControlType ControlTypeFromUiaId(CONTROLTYPEID controlTypeId) const;
    std::string BuildElementId(IUIAutomationElement* element) const;

    Microsoft::WRL::ComPtr<IUIAutomationElement> FindAutomationElementByLabel(
        HWND windowHandle,
        const std::wstring& label);

    bool EnsureAutomation();

    Microsoft::WRL::ComPtr<IUIAutomation> automation_;
    bool ownsComApartment_{false};
};

}  // namespace iee
