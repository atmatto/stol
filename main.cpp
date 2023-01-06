#define SOKOL_IMPL
#define SOKOL_GLES3

#include <list>
#include <initializer_list>
#include <cctype>

#include "cpr/cpr.h"
#include "gumbo/gumbo.h"

// TODO: Update Dear ImGui to v1.89.2 (https://github.com/ocornut/imgui/commit/bd96f6eac4ad544efb265d7e6bcdb30f99a841c4)
#include "imgui/imgui.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_imgui.h"

const char *fallbackText = "(Error getting text)";
#define safeCharPtr(ptr) (((ptr) == nullptr) ? fallbackText : (ptr))

// A for loop iterating over the children vector `vec`. Exposes the variable `child` of type **GumboNode.
#define gumboForEachChild(vec) for (auto child = (GumboNode **)vec.data; child < (GumboNode **)vec.data + vec.length; child++)

// Iterates over the children vector `vec` and sets the variable `ret` of type *GumboNode to point to the first child
// for which the boolean expression `check` returns true. If no such child exists, this does nothing. The `check` can
// access a variable named `child`, of type **GumboNode.
#define gumboFindChild(ret, vec, check) do { gumboForEachChild(vec) if (check) { ret = *child; break; } } while (0)

bool gumboElementIdEquals(GumboElement *e, const char *id) {
    auto attribute = gumbo_get_attribute(&e->attributes, "id");
    if (attribute == nullptr) return false;
    return 0 == strcmp(attribute->value, id);
}

bool gumboElementClassEquals(GumboElement *e, const char *cl) {
    auto attribute = gumbo_get_attribute(&e->attributes, "class");
    if (attribute == nullptr) return false;
    return 0 == strcmp(attribute->value, cl);
}

void displayLoadingIcon() {
    int f = (ImGui::GetFrameCount() / 30) % 6;
    switch (f) {
        case 0:
            ImGui::TextUnformatted("... ");
            break;
        case 1:
        case 5:
            ImGui::TextUnformatted(".. .");
            break;
        case 2:
        case 4:
            ImGui::TextUnformatted(". ..");
            break;
        case 3:
            ImGui::TextUnformatted(" ...");
            break;
    }
}

// Adds underline to previously drawn text. Created by Doug Binks, source:
// https://mastodon.gamedev.place/@dougbinks/99009293355650878
void AddUnderline(ImColor color = ImGui::GetStyleColorVec4(ImGuiCol_Text)) {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    min.y = max.y;
    ImGui::GetWindowDrawList()->AddLine(min, max, color);
}

struct HTML {
    GumboOutput * const output;
    GumboElement *focus;

    HTML(const HTML&) = delete;
    HTML(HTML&&) = delete;
    HTML& operator=(const HTML&) = delete;
    HTML& operator=(HTML&&) = delete;

    explicit HTML(char *buffer) : output(gumbo_parse(buffer)), focus(nullptr) {}

    static bool tagEquals(GumboElement *e, const char *tag) {
        int i = 1;
        for (; i < e->original_tag.length && e->original_tag.data[i] != '>' && e->original_tag.data[i] != ' '; i++) {
            if (std::tolower(tag[i - 1]) != std::tolower(e->original_tag.data[i])) {
                return false;
            }
        }
        return tag[i - 1] == '\0';
    }

    // Returns the first child of `node`, satisfying the query, which is a very simplified CSS selector:
    // `tag`, `.class`, `#id`. Returns `nullptr` if there is no such child.
    static GumboNode *queryNode(GumboNode *node, const char *query) {
        if (node == nullptr || node->type != GUMBO_NODE_ELEMENT) {
            return nullptr;
        }
        GumboNode *prev = node;
        switch (query[0]) {
            case '.':
                gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementClassEquals(&(*child)->v.element, query + 1));
                break;
            case '#':
                gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementIdEquals(&(*child)->v.element, query + 1));
                break;
            default:
                gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && tagEquals(&(*child)->v.element, query));
                break;
        }
        if (node == prev) return nullptr;
        return node;
    }

    // Returns the first node which is a descendant of `node` and satisfies the CSS query:
    // `queries[0] > queries[1] > ... > queries[n]`. Each subquery may be one of these very simplified
    // CSS selectors: `tag`, `.class`, `#id`.
    static GumboNode *queryNode(GumboNode *node, std::initializer_list<const char *> queries) {
        for (const char *query : queries) {
            node = queryNode(node, query);
        }
        return node;
    }

    ~HTML() {
        gumbo_destroy_output(&kGumboDefaultOptions, output);
    }
};

class WiktionaryProvider {
    // HTML structure:
    // (#mw-content-text > .mw-parser-output)
    //   See also: [.disambig-see-also-2 a]
    //   Language heading: (h2 > .mw-headline)
    //   Content

    static GumboElement *findContent(GumboNode *root) {
        if (root->type != GUMBO_NODE_ELEMENT) return nullptr;
        GumboNode *node = root;
        node = HTML::queryNode(node, {"body", "#content", "#bodyContent", "#mw-content-text", ".mw-parser-output"});
        if (node->type != GUMBO_NODE_ELEMENT || !gumboElementClassEquals(&node->v.element, "mw-parser-output")) return nullptr;
        return &node->v.element;
    }

    static const char *getHeaderText(GumboElement *element) {
        GumboNode *node;
        gumboFindChild(node, element->children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementClassEquals(&(*child)->v.element, "mw-headline"));
        if (node->type == GUMBO_NODE_ELEMENT && node->v.element.children.length >= 1) {
            node = (GumboNode *)node->v.element.children.data[0];
            if (node->type == GUMBO_NODE_TEXT) {
                return node->v.text.text;
            }
        }
        // Couldn't find the text.
        return nullptr;
    }

    // If true, then the header is open.
    bool displayLanguageHeader(GumboNode *node) {
        assert(node->type == GUMBO_NODE_ELEMENT);
        const char *text = getHeaderText(&node->v.element);
        if (text == nullptr) {
            ImGui::Separator();
            return true;
        } else {
//            return ImGui::CollapsingHeader(text);
            return ImGui::TreeNodeEx(text, ImGuiTreeNodeFlags_CollapsingHeader | (strncmp(text, defaultLanguage, 256) == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0));
        }
    }

    // Display the node and its children as plain text.
    static void displayText(GumboNode *node, std::string &text) {
        if (node->type == GUMBO_NODE_TEXT && node->v.text.text != nullptr) {
            text += node->v.text.text;
        } else if (node->type == GUMBO_NODE_WHITESPACE) {
            text += " ";
        } else if (node->type == GUMBO_NODE_ELEMENT) {
            auto &element = node->v.element;
            gumboForEachChild(element.children) {
                if ((*child)->type != GUMBO_NODE_ELEMENT) {
                    displayText(*child, text);
                    continue;
                }
                auto childElement = (*child)->v.element;
                if (gumboElementClassEquals(&childElement, "audiotable")) continue;
                switch (childElement.tag) {
                    // ignore
                    case GUMBO_TAG_STYLE:
                        break;
                    // inline
                    case GUMBO_TAG_SPAN:
                    case GUMBO_TAG_I:
                    case GUMBO_TAG_A:
                    case GUMBO_TAG_B:
                    case GUMBO_TAG_EM:
                    case GUMBO_TAG_U:
                    case GUMBO_TAG_STRONG:
                    case GUMBO_TAG_SUB:
                    case GUMBO_TAG_SUP:
                    case GUMBO_TAG_ABBR:
                        displayText(*child, text);
                        break;
                    // block
                    default:
                        if (!text.empty() && text[text.length() - 1] != '\n') {
                            text += '\n';
                        }
                        displayText(*child, text);
                        break;
                }
            }
        }
    }

    static void displayList(GumboElement *element, bool ordered = false) {
        int counter = 0;
        gumboForEachChild(element->children) {
            std::string text;
            if ((*child)->type == GUMBO_NODE_ELEMENT) {
                switch ((*child)->v.element.tag) {
                    case GUMBO_TAG_LI:
                    case GUMBO_TAG_DD:
                    case GUMBO_TAG_DT:
                        if (gumboElementClassEquals(&(*child)->v.element, "mw-empty-elt")) continue;
                        displayText((*child), text);
                        if (text.empty()) continue;
                        counter++;
                        if (ordered) {
                            ImGui::Text("%d.", counter);
                            ImGui::SameLine(0, 0);
                            ImGui::TextWrapped("%s", text.data());
                        } else {
                            ImGui::Bullet();
                            ImGui::TextWrapped("%s", text.data());
                        }
                        break;
                    default:
                        displayText((*child), text);
                        ImGui::TextWrapped("%s", text.data());
                }
            }
        }
    }

    static void displayDefinition(GumboElement *item) {
        std::string text;
        gumboForEachChild(item->children) {
            if ((*child)->type == GUMBO_NODE_TEXT) {
                text += (*child)->v.text.text;
            } else if ((*child)->type == GUMBO_NODE_WHITESPACE) {
                text += ' ';
            } else if ((*child)->type == GUMBO_NODE_ELEMENT) {
                auto &element = (*child)->v.element;
                switch (element.tag) {
                    // ignore
                    case GUMBO_TAG_STYLE:
                        break;
                    // special
                    case GUMBO_TAG_DL:
                    case GUMBO_TAG_UL: // TODO: If only quotations are in unordered lists, then make them collapsed by default.
                        for (auto &c : text) {
                            if (c != ' ' && c != '\n') {
                                ImGui::TextWrapped("%s", text.data());
                                text.clear();
                                break;
                            }
                        }
                        ImGui::Indent(10);
                        displayList(&element);
                        ImGui::Unindent(10);
                        break;
                    // inline
                    case GUMBO_TAG_SPAN:
                    case GUMBO_TAG_I:
                    case GUMBO_TAG_A:
                    case GUMBO_TAG_B:
                    case GUMBO_TAG_EM:
                    case GUMBO_TAG_U:
                    case GUMBO_TAG_STRONG:
                    case GUMBO_TAG_SUB:
                    case GUMBO_TAG_SUP:
                    case GUMBO_TAG_ABBR:
                        displayText(*child, text);
                        break;
                    // block
                    default:
                        if (!text.empty() && text[text.length() - 1] != '\n') text += '\n';
                        displayText(*child, text);
                        break;
                }
            }
        }
        if (!text.empty()) {
            ImGui::TextWrapped("%s", text.data());
        }
    }

    static void displayDefinitions(GumboElement *list) {
        int counter = 0;
        gumboForEachChild(list->children) {
            std::string text;
            if ((*child)->type == GUMBO_NODE_ELEMENT) {
                if ((*child)->v.element.tag == GUMBO_TAG_LI) {
                    if (gumboElementClassEquals(&(*child)->v.element, "mw-empty-elt")) continue;
                    displayText((*child), text);
                    if (text.empty()) continue;
                    counter++;
                    ImGui::Text("%d.", counter);
                    ImGui::SameLine(0, 0);
                    displayDefinition(&(*child)->v.element);
                } else {
                    displayText((*child), text);
                    ImGui::TextWrapped(".%s", text.data());
                }
            }
        }
    }

    // Returns the amount of columns occupied by the table cell.
    static int getTableCellWidth(GumboElement *cell) {
        auto colspan = gumbo_get_attribute(&cell->attributes, "colspan");
        if (colspan == nullptr) {
            return 1;
        } else {
            int columns = 1;
            sscanf(colspan->value, "%d", &columns);
            return columns;
        }
    }

    // Returns the amount of rows occupied by the table cell.
    static int getTableCellHeight(GumboElement *cell) {
        auto rowspan = gumbo_get_attribute(&cell->attributes, "rowspan");
        if (rowspan == nullptr) {
            return 1;
        } else {
            int columns = 1;
            sscanf(rowspan->value, "%d", &columns);
            return columns;
        }
    }

    static void displayTableRow(GumboElement *tr, int columns, std::vector<int> &rowspans) {
        if (gumboElementClassEquals(tr, "vsShow")) {
            // These rows are displayed when the table is collapsed.
            return;
        }
        int column = 0, width;
        ImGui::TableNextRow();
        gumboForEachChild(tr->children) {
            if ((*child)->type == GUMBO_NODE_ELEMENT && ((*child)->v.element.tag == GUMBO_TAG_TH || (*child)->v.element.tag == GUMBO_TAG_TD)) {
                if (column >= columns) goto endRow;
                while (rowspans[column] > 0) {
                    rowspans[column]--;
                    column++;
                    if (column >= columns) goto endRow;
                }
                GumboElement *cell = &(*child)->v.element;
                std::string text;
                displayText((*child), text);
                width = getTableCellWidth(cell);
                ImGui::TableSetColumnIndex(column);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (float)width * ImGui::GetColumnWidth());
                ImGui::TextWrapped("%s", text.data());
                ImGui::PopTextWrapPos();
                for (int i = 0; i < width; i++) {
                    rowspans[column + i] = getTableCellHeight(cell) - 1;
                }
                column += width;
            }
        }
    endRow:
        for (int i = column; i < columns; i++) {
            rowspans[i]--;
        }
    }

    // Returns the number of columns. If it can't be retrieved, the function returns a negative value.
    // It may also return 0.
    static int getTableWidth(GumboElement *tbody) {
        GumboElement *firstRow;
        gumboForEachChild(tbody->children) {
            if ((*child)->type == GUMBO_NODE_ELEMENT && (*child)->v.element.tag == GUMBO_TAG_TR) {
                firstRow = &(*child)->v.element;
                break;
            }
        }
        if (firstRow == nullptr) return -1;
        int columns = 0;
        gumboForEachChild(firstRow->children) {
            if ((*child)->type == GUMBO_NODE_ELEMENT && ((*child)->v.element.tag == GUMBO_TAG_TH || (*child)->v.element.tag == GUMBO_TAG_TD)) {
                columns += getTableCellWidth(&(*child)->v.element);
            }
        }
        return columns;
    }

    static void displayTable(GumboElement *tbody) {
        ImGui::PushID(tbody);
        int columns = getTableWidth(tbody);
        if (columns <= 0) {
            ImGui::TextUnformatted("(Invalid table)");
        } else {
            if (ImGui::BeginTable("Table", columns, ImGuiTableFlags_NoClip|ImGuiTableFlags_BordersOuter|ImGuiTableFlags_RowBg)) {
                std::vector<int> rowspans(columns, 0); // rowspans[i] = x means to skip the ith column in next x rows.
                gumboForEachChild(tbody->children) {
                    if ((*child)->type == GUMBO_NODE_ELEMENT && (*child)->v.element.tag == GUMBO_TAG_TR) {
                        displayTableRow(&(*child)->v.element, columns, rowspans);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }

    static void displayRecursive(GumboNode *node) {
        if (node->type == GUMBO_NODE_TEXT && node->v.text.text != nullptr) {
            ImGui::TextUnformatted(node->v.text.text);
        } else if (node->type == GUMBO_NODE_ELEMENT) {
            auto &element = node->v.element;
            std::string text;
            // TODO: div.list-switcher (multi-column ul)
            switch (element.tag) {
                case GUMBO_TAG_STYLE:
                case GUMBO_TAG_DIV: // TODO: Should there be any exceptions to this?
                    break;
                case GUMBO_TAG_H3:
                    ImGui::Dummy(ImVec2(0.0f, 0.5f * ImGui::GetTextLineHeightWithSpacing()));
                    ImGui::Separator();
                    ImGui::TextUnformatted(safeCharPtr(getHeaderText(&element)));
                    AddUnderline();
                    break;
                case GUMBO_TAG_H4:
                case GUMBO_TAG_H5:
                case GUMBO_TAG_H6:
                    ImGui::Dummy(ImVec2(0.0f, 0.5f * ImGui::GetTextLineHeightWithSpacing()));
                    ImGui::TextUnformatted(safeCharPtr(getHeaderText(&element)));
                    AddUnderline();
                    break;
                case GUMBO_TAG_P:
                    displayText(node, text);
                    ImGui::TextWrapped("%s", text.data());
                    break;
                case GUMBO_TAG_UL:
                    displayList(&element);
                    break;
                case GUMBO_TAG_OL: // I hope that ordered lists always contain definitions...
                    displayDefinitions(&element);
                    break;
                case GUMBO_TAG_HR:
                    break;
                case GUMBO_TAG_TBODY:
                    displayTable(&element);
                    break;
                default:
                    gumboForEachChild(element.children) {
                        displayRecursive(*child);
                    }
                    break;
            }
        }
    }

    class Query {
    private:
        char query[256] = "";
        bool done = false;
        cpr::AsyncResponse request;
        std::string rawData;
        HTML *data = nullptr;

        void processResult() {
            data = new HTML(rawData.data());
            data->focus = findContent(data->output->root);
        }

        void getQueryURL(char *out) const {
            sprintf(out, "https://en.wiktionary.org/wiki/%s", query);
        }

    public:
        // Returns false when the tab gets closed.
        bool DisplayAsTabItem(WiktionaryProvider &provider) {
            bool open = true;
            if (ImGui::BeginTabItem(query, &open)) {
                // TODO: Handle the case when there are multiple equal queries.
                // TODO: Alternative search results dropdown
                if (!done) {
                    if (request.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                        auto r = request.get();
                        rawData = r.text;
                        done = true;
                        processResult();
                    } else {
                        displayLoadingIcon();
                    }
                } else {
                    assert(data != nullptr);
                    auto content = data->focus;
                    if (content != nullptr) {
                        bool show = true;
                        bool firstHeading = true;
                        gumboForEachChild(content->children) {
                            if ((*child)->type == GUMBO_NODE_ELEMENT && (*child)->v.element.tag == GUMBO_TAG_H2) {
                                show = provider.displayLanguageHeader(*child);
                                firstHeading = true;
                            } else if (show) {
                                if (firstHeading && (*child)->type == GUMBO_NODE_ELEMENT && (*child)->v.element.tag == GUMBO_TAG_H3) {
                                    ImGui::TextUnformatted(safeCharPtr(getHeaderText(&(*child)->v.element)));
                                    AddUnderline();
                                    firstHeading = false;
                                } else {
                                    displayRecursive(*child);
                                }
                            }
                        }
                    } else {
                        ImGui::TextUnformatted("Could not retrieve content.");
                    }
                }
                ImGui::EndTabItem();
            }
            return open;
        }

        explicit Query(char *text) {
            static_assert(sizeof(input) == sizeof(Query::query));
            memcpy(query, text, sizeof(query));
            text[0] = '\0';
            char url[512];
            getQueryURL(url);
            request = cpr::GetAsync(cpr::Url{url});
        }
    };

    char defaultLanguage[256] = "";

    void displaySettings() {
        ImGui::InputTextWithHint("Default language", "English", defaultLanguage, 256, ImGuiInputTextFlags_AutoSelectAll);
    }

    char input[256] = "";
    std::list<Query> queries;

public:
    void Display() {
        ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_Appearing);
        if (ImGui::Begin("Wiktionary", nullptr, ImGuiWindowFlags_MenuBar)) {
            // Search field
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
            bool search = ImGui::InputTextWithHint("##Word", "Search Wiktionary", input, 256,
                                ImGuiInputTextFlags_AutoSelectAll|ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            search |= ImGui::Button("Look up");
            if (search && input[0] != '\0') {
                queries.emplace_back(input);
            }
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Settings")) {
                    displaySettings();
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            // Results
            if (!queries.empty()) {
                if (ImGui::BeginTabBar("Results", ImGuiTabBarFlags_AutoSelectNewTabs)) {
                    for (auto query = queries.begin(); query != queries.end();) {
                        if (!query->DisplayAsTabItem(*this)) {
                            query = queries.erase(query);
                        } else {
                            query++;
                        }
                    }
                    ImGui::EndTabBar();
                }
            }
        }
        ImGui::End();
    }
};

static sg_pass_action pass_action;

static WiktionaryProvider wiktionary;

void frame() {
    simgui_new_frame({
        sapp_width(),
        sapp_height(),
        sapp_frame_duration(),
        sapp_dpi_scale(),
    });

	ImGui::ShowDemoWindow();

    wiktionary.Display();

    sg_begin_default_pass(pass_action, sapp_width(), sapp_height());
    simgui_render();
    sg_end_pass();
    sg_commit();
}

void init() {
	sg_desc desc = {};
	desc.context = sapp_sgcontext();
	sg_setup(&desc);

	simgui_desc_t simgui_desc = {};
    simgui_desc.no_default_font = true;
    simgui_setup(&simgui_desc);

	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImFontConfig fontCfg;
    fontCfg.FontDataOwnedByAtlas = false;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    fontCfg.RasterizerMultiply = 1.5f;

    static const ImWchar ranges[] = { // TODO: Maybe detect dynamically from text.
        0x0020, 0xFFFF,
        0
    };
    // TODO: Load from memory
    io.Fonts->AddFontFromFileTTF("fonts/NotoSans/NotoSansMono-Regular.ttf", 16.0f, &fontCfg, &ranges[0]);
    fontCfg.MergeMode = true;
    io.Fonts->AddFontFromFileTTF("fonts/DejaVuSans/DejaVuSans.ttf", 16.0f, &fontCfg, &ranges[0]); // fallback

    unsigned char* font_pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    sg_image_desc img_desc = { };
    img_desc.width = font_width;
    img_desc.height = font_height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.min_filter = SG_FILTER_LINEAR;
    img_desc.mag_filter = SG_FILTER_LINEAR;
    img_desc.data.subimage[0][0].ptr = font_pixels;
    img_desc.data.subimage[0][0].size = font_width * font_height * 4;
    io.Fonts->TexID = (ImTextureID)(uintptr_t) sg_make_image(&img_desc).id;

    pass_action.colors[0].action = SG_ACTION_CLEAR;
	pass_action.colors[0].value = { 0.9f, 0.9f, 0.9f, 1.0f };
}

void cleanup() {
	simgui_shutdown();
	sg_shutdown();
}

void event(const sapp_event* ev) {
	simgui_handle_event(ev);
}

sapp_desc sokol_main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	sapp_desc desc = {};
	desc.init_cb = init;
	desc.frame_cb = frame;
	desc.cleanup_cb = cleanup;
	desc.event_cb = event;
	desc.window_title = "Stol";
	desc.enable_clipboard = true;
	desc.width = 800;
	desc.height = 600;
	desc.icon.sokol_default = true;
	return desc;
}
