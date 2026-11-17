#include "ModMenu.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "2s2h/BenGui/BenMenu.h"

namespace BenGui {
extern std::shared_ptr<BenMenu> mBenMenu;
}

namespace {

struct RegisteredWidget {
    S2HModMenuWidget definition;
    std::string section;
    std::string sidebar;
    std::string label;
    std::string cvar;
    std::string tooltip;
    std::string anchor;
    bool after = false;
    std::vector<std::string> values;
    std::vector<const char*> valuePointers;
};

struct RegisteredSidebar {
    std::string section;
    std::string sidebar;
    std::string cvar;
    uint8_t columns;
};

// Deques: BenMenu keeps the cvar and tooltip pointers of every widget it was handed.
std::deque<RegisteredWidget> sWidgets;
std::deque<RegisteredSidebar> sSidebars;

bool ToWidgetType(S2HModMenuWidgetType type, WidgetType& widgetType) {
    switch (type) {
        case S2H_MOD_MENU_CHECKBOX:
            widgetType = WIDGET_CVAR_CHECKBOX;
            return true;
        case S2H_MOD_MENU_INT_SLIDER:
            widgetType = WIDGET_CVAR_SLIDER_INT;
            return true;
        case S2H_MOD_MENU_FLOAT_SLIDER:
            widgetType = WIDGET_CVAR_SLIDER_FLOAT;
            return true;
        case S2H_MOD_MENU_SEPARATOR:
            widgetType = WIDGET_SEPARATOR;
            return true;
        case S2H_MOD_MENU_TEXT:
            widgetType = WIDGET_TEXT;
            return true;
        case S2H_MOD_MENU_CUSTOM:
            widgetType = WIDGET_CUSTOM;
            return true;
        case S2H_MOD_MENU_COMBOBOX:
            widgetType = WIDGET_CVAR_COMBOBOX;
            return true;
        default:
            return false;
    }
}

void ApplyOptions(WidgetInfo& widget, const RegisteredWidget& registered) {
    const S2HModMenuWidget& definition = registered.definition;
    const char* tooltip = registered.tooltip.c_str();
    switch (definition.type) {
        case S2H_MOD_MENU_CHECKBOX:
            widget.Options(UIWidgets::CheckboxOptions().DefaultValue(definition.defaultInt != 0).Tooltip(tooltip));
            break;
        case S2H_MOD_MENU_INT_SLIDER:
            widget.Options(UIWidgets::IntSliderOptions()
                               .DefaultValue(definition.defaultInt)
                               .Min(definition.minInt)
                               .Max(definition.maxInt)
                               .Tooltip(tooltip));
            break;
        case S2H_MOD_MENU_FLOAT_SLIDER:
            widget.Options(UIWidgets::FloatSliderOptions()
                               .DefaultValue(definition.defaultFloat)
                               .Min(definition.minFloat)
                               .Max(definition.maxFloat)
                               .Tooltip(tooltip));
            break;
        case S2H_MOD_MENU_TEXT:
            widget.Options(UIWidgets::WidgetOptions().Tooltip(tooltip));
            break;
        case S2H_MOD_MENU_COMBOBOX:
            widget.Options(UIWidgets::ComboboxOptions()
                               .ComboVec(&registered.valuePointers)
                               .DefaultIndex(static_cast<uint32_t>(definition.defaultInt))
                               .Tooltip(tooltip));
            break;
        default:
            break;
    }
}

// BenMenu callbacks are plain function pointers, so the widget's name finds the mod callback it belongs to.
void RunModCallback(WidgetInfo& info) {
    for (const RegisteredWidget& registered : sWidgets) {
        if (registered.label == info.name && registered.definition.onChange != nullptr) {
            registered.definition.onChange();
            return;
        }
    }
}

void AddWidget(const RegisteredWidget& registered) {
    const S2HModMenuWidget& definition = registered.definition;
    WidgetPath path = { registered.section, registered.sidebar, (SectionColumns)definition.column };
    WidgetType type;
    if (!BenGui::mBenMenu->HasModWidgetPath(path) || !ToWidgetType(definition.type, type)) {
        SPDLOG_ERROR("[ModMenu] Invalid widget '{}' at {}/{} column {}", registered.label, registered.section,
                     registered.sidebar, definition.column);
        return;
    }

    WidgetInfo& widget = BenGui::mBenMenu->AddWidget(path, registered.label, type);
    if (!registered.cvar.empty()) {
        widget.CVar(registered.cvar.c_str());
    }
    ApplyOptions(widget, registered);
    if (definition.onChange != nullptr) {
        if (definition.type == S2H_MOD_MENU_CUSTOM) {
            widget.CustomFunction(RunModCallback);
        } else {
            widget.Callback(RunModCallback);
        }
    }
    if (!registered.anchor.empty() &&
        !BenGui::mBenMenu->MoveModWidget(path, registered.label, registered.anchor, registered.after)) {
        SPDLOG_WARN("[ModMenu] Anchor '{}' not found for widget '{}'", registered.anchor, registered.label);
    }
}

void AddSidebar(const RegisteredSidebar& sidebar) {
    BenGui::mBenMenu->EnsureModSidebar(sidebar.section, sidebar.sidebar, sidebar.cvar.c_str(), sidebar.columns);
}

bool IsMenuBuilt() {
    return BenGui::mBenMenu != nullptr && BenGui::mBenMenu->AreMenuElementsInitialized();
}

void RegisterWidgets() {
    for (const auto& sidebar : sSidebars) {
        AddSidebar(sidebar);
    }
    for (const auto& widget : sWidgets) {
        AddWidget(widget);
    }
}

RegisterMenuInitFunc sMenuInit(RegisterWidgets);

bool IsFilled(const char* text) {
    return text != nullptr && text[0] != '\0';
}

bool HasComboValues(const S2HModMenuWidget* widget) {
    return widget->structSize >= S2H_MOD_MENU_WIDGET_COMBOBOX_SIZE && widget->values != nullptr &&
           widget->valueCount > 0;
}

bool IsValidWidget(const S2HModMenuWidget* widget) {
    if (widget == nullptr || widget->structSize < S2H_MOD_MENU_WIDGET_MIN_SIZE) {
        return false;
    }
    if (!IsFilled(widget->section) || !IsFilled(widget->sidebar) || !IsFilled(widget->label) || widget->column > 2) {
        return false;
    }
    switch (widget->type) {
        case S2H_MOD_MENU_CHECKBOX:
        case S2H_MOD_MENU_INT_SLIDER:
        case S2H_MOD_MENU_FLOAT_SLIDER:
            return IsFilled(widget->cvar);
        case S2H_MOD_MENU_COMBOBOX:
            return IsFilled(widget->cvar) && HasComboValues(widget);
        case S2H_MOD_MENU_SEPARATOR:
        case S2H_MOD_MENU_TEXT:
        case S2H_MOD_MENU_CUSTOM:
            return true;
        default:
            return false;
    }
}

void CopyComboValues(const S2HModMenuWidget* widget, RegisteredWidget& registered) {
    if (widget->type != S2H_MOD_MENU_COMBOBOX) {
        return;
    }
    for (uint32_t index = 0; index < widget->valueCount; index++) {
        registered.values.emplace_back(widget->values[index] == nullptr ? "" : widget->values[index]);
    }
}

// The pointers are taken once the widget sits in the deque, where its strings no longer move.
void PointAtComboValues(RegisteredWidget& registered) {
    for (const std::string& value : registered.values) {
        registered.valuePointers.push_back(value.c_str());
    }
}


// Fleet Combo: mods each add their own menu pages. Gather them into one "NEI" section, one page per category, each
// mod's settings under a header with its own page name; mod test tools go to Dev Tools.
struct MenuHome {
    std::string section;
    std::string sidebar;
    std::string header; // empty: no header
};

MenuHome RemapModMenu(const std::string& section, const std::string& sidebar) {
    if (section != "Mods") {
        return { section, sidebar, "" };
    }
    static const std::map<std::string, std::string> kCategory = {
        { "Majora's Mask", "Items & Masks" },  { "Ocarina of Time", "Items & Masks" }, { "Items", "Items & Masks" },
        { "Equipment", "Equipment" },          { "Ikana Shield", "Equipment" },        { "Partners", "Companions" },
        { "Companions", "Companions" },        { "Gameplay", "Gameplay" },
    };
    if (sidebar == "Item Drop Tester" || sidebar == "Animation Viewer" || sidebar == "Debug") {
        return { "Dev Tools", "Mod Tools", sidebar };
    }
    auto category = kCategory.find(sidebar);
    if (category != kCategory.end()) {
        return { "NEI", category->second, sidebar == category->second ? std::string{} : sidebar };
    }
    return { "NEI", "Other", sidebar };
}

std::set<std::string> sHeadersAdded; // "sidebar|header"

bool StoreWidget(const S2HModMenuWidget* widget, const char* anchor, bool after) {
    if (!IsValidWidget(widget)) {
        return false;
    }
    RegisteredWidget registered = {};
    // A mod built against an older header passes a shorter struct; the missing tail stays zero.
    std::memcpy(&registered.definition, widget, std::min<size_t>(widget->structSize, sizeof(S2HModMenuWidget)));
    CopyComboValues(widget, registered);
    const MenuHome home = RemapModMenu(widget->section, widget->sidebar);
    const bool remapped = home.section != widget->section || home.sidebar != widget->sidebar;
    registered.section = home.section;
    registered.sidebar = home.sidebar;
    if (remapped) {
        registered.definition.column = 0; // gathered pages are one tidy column
        if (!home.header.empty() && sHeadersAdded.insert(home.sidebar + "|" + home.header).second) {
            for (const auto type : { S2H_MOD_MENU_SEPARATOR, S2H_MOD_MENU_TEXT }) {
                RegisteredWidget header = {};
                header.definition.structSize = sizeof(header.definition);
                header.definition.type = type;
                header.definition.column = 0;
                header.section = home.section;
                header.sidebar = home.sidebar;
                header.label = type == S2H_MOD_MENU_TEXT ? home.header : home.header + "##separator";
                sWidgets.push_back(std::move(header));
                if (IsMenuBuilt()) {
                    AddWidget(sWidgets.back());
                }
            }
        }
    }
    registered.label = widget->label;
    registered.cvar = widget->cvar == nullptr ? "" : widget->cvar;
    registered.tooltip = widget->tooltip == nullptr ? "" : widget->tooltip;
    registered.anchor = anchor == nullptr ? "" : anchor;
    registered.after = after;
    sWidgets.push_back(std::move(registered));
    PointAtComboValues(sWidgets.back());
    if (IsMenuBuilt()) {
        AddWidget(sWidgets.back());
    }
    return true;
}

} // namespace

bool ModMenu_RegisterWidget(const S2HModMenuWidget* widget) {
    return StoreWidget(widget, nullptr, false);
}

bool ModMenu_RegisterSidebar(const char* section, const char* sidebar, const char* selectionCvar, uint8_t columns) {
    if (section == nullptr || section[0] == '\0' || sidebar == nullptr || sidebar[0] == '\0' ||
        selectionCvar == nullptr || selectionCvar[0] == '\0' || columns < 1 || columns > 3) {
        return false;
    }
    const MenuHome home = RemapModMenu(section, sidebar);
    if (home.section != section || home.sidebar != sidebar) {
        // Gathered pages: one column, and the NEI section remembers its own selected page.
        const bool known = std::any_of(sSidebars.begin(), sSidebars.end(), [&](const auto& registered) {
            return registered.section == home.section && registered.sidebar == home.sidebar;
        });
        if (!known) {
            sSidebars.push_back({ home.section, home.sidebar,
                                  home.section == "NEI" ? "gFleetCombo.Menu.NEIPage" : selectionCvar, 1 });
            if (IsMenuBuilt()) {
                AddSidebar(sSidebars.back());
            }
        }
        return true;
    }
    sSidebars.push_back({ section, sidebar, selectionCvar, columns });
    if (IsMenuBuilt()) {
        AddSidebar(sSidebars.back());
    }
    return true;
}

bool ModMenu_RegisterWidgetAt(const S2HModMenuWidget* widget, const char* anchor, bool after) {
    if (anchor == nullptr || anchor[0] == '\0') {
        return false;
    }
    return StoreWidget(widget, anchor, after);
}

bool ModUI_BeginTab(const char* label) {
    return label != nullptr && ImGui::BeginTabItem(label);
}

void ModUI_EndTab(void) {
    ImGui::EndTabItem();
}

bool ModUI_BeginSection(const char* label) {
    return label != nullptr && ImGui::TreeNode(label);
}

void ModUI_EndSection(void) {
    ImGui::TreePop();
}

void ModUI_Text(const char* text) {
    if (text != nullptr) {
        ImGui::TextUnformatted(text);
    }
}

bool ModUI_Checkbox(const char* label, bool* value) {
    return label != nullptr && value != nullptr && ImGui::Checkbox(label, value);
}

bool ModUI_InputInt(const char* label, int32_t* value) {
    return label != nullptr && value != nullptr && ImGui::InputInt(label, value);
}
