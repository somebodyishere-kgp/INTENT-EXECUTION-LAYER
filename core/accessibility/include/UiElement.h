#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace iee {

enum class UiControlType {
    Unknown,
    Window,
    Button,
    TextBox,
    Menu,
    MenuItem,
    ComboBox,
    ListItem,
    Document
};

struct UiElement {
    std::string id;
    std::string parentId;
    std::wstring name;
    std::wstring automationId;
    std::wstring className;
    UiControlType controlType{UiControlType::Unknown};
    RECT bounds{};
    int depth{0};
    bool isEnabled{false};
    bool isOffscreen{false};
    bool isVisible{true};
    bool isCollapsed{false};
    bool isHidden{false};
    bool isFocused{false};
    bool supportsInvoke{false};
    bool supportsValue{false};
    bool supportsSelection{false};
    std::wstring accessKey;
    std::wstring acceleratorKey;
    std::wstring value;
    std::vector<std::string> children;
    std::uint64_t lastInteractionTicks{0};
};

}  // namespace iee
