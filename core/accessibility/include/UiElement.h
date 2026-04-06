#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

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
    bool isFocused{false};
    bool supportsInvoke{false};
    bool supportsValue{false};
    bool supportsSelection{false};
    std::wstring value;
    std::uint64_t lastInteractionTicks{0};
};

}  // namespace iee
