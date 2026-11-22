#include "ModMenu.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "2s2h/BenGui/BenMenu.h"
#include "2s2h/BenGui/BenGui.hpp"

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
    bool pageWidget = false; // Fleet Combo: a gathered NEI page
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

void DrawGatheredPage(WidgetInfo& info);

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
    if (registered.pageWidget) {
        widget.CustomFunction(DrawGatheredPage);
    } else if (definition.onChange != nullptr) {
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


// Fleet Combo: mods each add their own menu pages. Gather them into one "NEI" section, one page per category. Each
// page is drawn by a single custom widget: every mod gets a collapsible header holding its settings, and mod test
// tools go to Dev Tools. Drawing the gathered settings here does not depend on the menu's per-sidebar column rules.
struct MenuHome {
    std::string section;
    std::string sidebar;
    std::string header;
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
        return { "NEI", category->second, sidebar };
    }
    return { "NEI", "Other", sidebar };
}

struct GatheredGroup {
    std::string name;
    std::vector<RegisteredWidget*> widgets; // deque elements never move
};

struct GatheredPage {
    std::string section;
    std::string sidebar;
    std::string widgetName;
    std::vector<GatheredGroup> groups;
};

std::deque<GatheredPage> sGatheredPages;
std::deque<RegisteredWidget> sGatheredWidgets;
const char* const kFleetComboPage = "##gathered.NEI.Fleet Combo";

void DrawGatheredWidget(RegisteredWidget& registered) {
    const auto& definition = registered.definition;
    const char* label = registered.label.c_str();
    const char* cvar = registered.cvar.c_str();
    const char* tooltip = registered.tooltip.c_str();
    bool changed = false;
    ImGui::PushID(&registered);
    switch (definition.type) {
        case S2H_MOD_MENU_CHECKBOX:
            changed = UIWidgets::CVarCheckbox(label, cvar,
                                              UIWidgets::CheckboxOptions()
                                                  .DefaultValue(definition.defaultInt != 0)
                                                  .Tooltip(tooltip)
                                                  .Color(THEME_COLOR));
            break;
        case S2H_MOD_MENU_INT_SLIDER:
            changed = UIWidgets::CVarSliderInt(label, cvar,
                                               UIWidgets::IntSliderOptions()
                                                   .DefaultValue(definition.defaultInt)
                                                   .Min(definition.minInt)
                                                   .Max(definition.maxInt)
                                                   .Tooltip(tooltip)
                                                   .Color(THEME_COLOR));
            break;
        case S2H_MOD_MENU_FLOAT_SLIDER:
            changed = UIWidgets::CVarSliderFloat(label, cvar,
                                                 UIWidgets::FloatSliderOptions()
                                                     .DefaultValue(definition.defaultFloat)
                                                     .Min(definition.minFloat)
                                                     .Max(definition.maxFloat)
                                                     .Tooltip(tooltip)
                                                     .Color(THEME_COLOR));
            break;
        case S2H_MOD_MENU_COMBOBOX:
            changed = UIWidgets::CVarCombobox(label, cvar, registered.valuePointers,
                                              UIWidgets::ComboboxOptions()
                                                  .DefaultIndex(static_cast<uint32_t>(definition.defaultInt))
                                                  .Tooltip(tooltip)
                                                  .Color(THEME_COLOR));
            break;
        case S2H_MOD_MENU_SEPARATOR:
            ImGui::Separator();
            break;
        case S2H_MOD_MENU_TEXT:
            ImGui::TextWrapped("%s", label);
            break;
        case S2H_MOD_MENU_CUSTOM:
            if (definition.onChange != nullptr) {
                definition.onChange(); // a custom widget's callback is its draw function
            }
            break;
        default:
            break;
    }
    if (changed && definition.type != S2H_MOD_MENU_CUSTOM && definition.onChange != nullptr) {
        definition.onChange();
    }
    ImGui::PopID();
}

void DrawFleetComboPage() {
    ImGui::TextWrapped("Settings for the combined OoT + MM game. World Era is in the Ocarina of Time menu.");
    ImGui::Spacing();
    UIWidgets::CVarCheckbox("Door portals between worlds", "gFleetCombo.DoorPortals",
                            UIWidgets::CheckboxOptions()
                                .DefaultValue(false)
                                .Tooltip("Also travel through the Clock Tower / Happy Mask Shop doors. The Song of "
                                         "Soaring always works.")
                                .Color(THEME_COLOR));
}

void DrawGatheredPage(WidgetInfo& info) {
    if (info.name == kFleetComboPage) {
        DrawFleetComboPage();
        return;
    }
    for (const auto& page : sGatheredPages) {
        if (page.widgetName != info.name) {
            continue;
        }
        if (page.groups.empty()) {
            ImGui::TextDisabled("No mod settings here.");
        }
        for (const auto& group : page.groups) {
            ImGui::PushID(group.name.c_str());
            if (ImGui::CollapsingHeader(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();
                for (auto* widget : group.widgets) {
                    DrawGatheredWidget(*widget);
                }
                ImGui::Unindent();
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
        return;
    }
}

void AddPageWidget(const std::string& section, const std::string& sidebar, const std::string& name) {
    RegisteredWidget page = {};
    page.definition.structSize = sizeof(page.definition);
    page.definition.type = S2H_MOD_MENU_CUSTOM;
    page.section = section;
    page.sidebar = sidebar;
    page.label = name;
    page.pageWidget = true;
    sWidgets.push_back(std::move(page));
    if (IsMenuBuilt()) {
        AddWidget(sWidgets.back());
    }
}

void EnsureSidebar(const std::string& section, const std::string& sidebar, const std::string& cvar) {
    const bool known = std::any_of(sSidebars.begin(), sSidebars.end(), [&](const auto& registered) {
        return registered.section == section && registered.sidebar == sidebar;
    });
    if (known) {
        return;
    }
    sSidebars.push_back({ section, sidebar, cvar, 1 });
    if (IsMenuBuilt()) {
        AddSidebar(sSidebars.back());
    }
}

GatheredPage& GetGatheredPage(const std::string& section, const std::string& sidebar) {
    for (auto& page : sGatheredPages) {
        if (page.section == section && page.sidebar == sidebar) {
            return page;
        }
    }
    const std::string cvar = "gFleetCombo.Menu." + section + "Page";
    if (section == "NEI" && sGatheredPages.empty()) {
        EnsureSidebar("NEI", "Fleet Combo", cvar);
        AddPageWidget("NEI", "Fleet Combo", kFleetComboPage);
    }
    const std::string name = "##gathered." + section + "." + sidebar;
    sGatheredPages.push_back({ section, sidebar, name, {} });
    EnsureSidebar(section, sidebar, cvar);
    AddPageWidget(section, sidebar, name);
    return sGatheredPages.back();
}

void GatherWidget(const MenuHome& home, RegisteredWidget registered) {
    GatheredPage& page = GetGatheredPage(home.section, home.sidebar);
    auto group = std::find_if(page.groups.begin(), page.groups.end(),
                              [&](const auto& existing) { return existing.name == home.header; });
    if (group == page.groups.end()) {
        page.groups.push_back({ home.header, {} });
        group = std::prev(page.groups.end());
        SPDLOG_WARN("[ModMenu] {} > {} > {}", home.section, home.sidebar, home.header);
    }
    sGatheredWidgets.push_back(std::move(registered));
    RegisteredWidget* stored = &sGatheredWidgets.back();
    PointAtComboValues(*stored);
    auto position = group->widgets.end();
    if (!stored->anchor.empty()) {
        auto anchor = std::find_if(group->widgets.begin(), group->widgets.end(),
                                   [&](const auto* widget) { return widget->label == stored->anchor; });
        if (anchor != group->widgets.end()) {
            position = stored->after ? std::next(anchor) : anchor;
        }
    }
    group->widgets.insert(position, stored);
}

bool StoreWidget(const S2HModMenuWidget* widget, const char* anchor, bool after) {
    if (!IsValidWidget(widget)) {
        SPDLOG_WARN("[ModMenu] Rejected widget '{}' ({}/{})",
                    widget != nullptr && widget->label != nullptr ? widget->label : "?",
                    widget != nullptr && widget->section != nullptr ? widget->section : "?",
                    widget != nullptr && widget->sidebar != nullptr ? widget->sidebar : "?");
        return false;
    }
    RegisteredWidget registered = {};
    // A mod built against an older header passes a shorter struct; the missing tail stays zero.
    std::memcpy(&registered.definition, widget, std::min<size_t>(widget->structSize, sizeof(S2HModMenuWidget)));
    CopyComboValues(widget, registered);
    registered.label = widget->label;
    registered.cvar = widget->cvar == nullptr ? "" : widget->cvar;
    registered.tooltip = widget->tooltip == nullptr ? "" : widget->tooltip;
    registered.anchor = anchor == nullptr ? "" : anchor;
    registered.after = after;
    const MenuHome home = RemapModMenu(widget->section, widget->sidebar);
    if (home.section != widget->section || home.sidebar != widget->sidebar) {
        GatherWidget(home, std::move(registered));
        return true;
    }
    registered.section = home.section;
    registered.sidebar = home.sidebar;
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
        GetGatheredPage(home.section, home.sidebar); // gathered pages are created on demand
        return true;
    }
    sSidebars.push_back({ section, sidebar, selectionCvar, columns });
    if (IsMenuBuilt()) {
        AddSidebar(sSidebars.back());
    }
    return true;
}

void ModMenu_Describe(std::vector<std::string>& lines) {
    for (const auto& page : sGatheredPages) {
        lines.push_back("gathered " + page.section + " > " + page.sidebar);
        for (const auto& group : page.groups) {
            lines.push_back("  section " + group.name + " (" + std::to_string(group.widgets.size()) + ")");
            for (const auto* widget : group.widgets) {
                lines.push_back("    " + std::to_string(static_cast<int>(widget->definition.type)) + " " + widget->label +
                                (widget->cvar.empty() ? "" : " {" + widget->cvar + "}"));
            }
        }
    }
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
