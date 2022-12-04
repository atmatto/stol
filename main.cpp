#define SOKOL_IMPL
#define SOKOL_GLES3

#include <vector>

#include "cpr/cpr.h"
#include "gumbo/gumbo.h"

#include "imgui/imgui.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_imgui.h"

static sg_pass_action pass_action;

// A for loop iterating over the children vector `vec`. Exposes the variable `child` of type **GumboNode.
#define gumboForEachChild(vec) for (auto child = (GumboNode **)vec.data; child < (GumboNode **)vec.data + vec.length; child++)

// Iterates over the children vector `vec` and sets the variable `ret` of type *GumboNode to point to the first child
// for which the boolean expression `check` returns true. If no such child exists, this does nothing. The `check` can
// access a variable named `child`, of type **GumboNode.
#define gumboFindChild(ret, vec, check) gumboForEachChild(vec) if (check) { ret = *child; break; }

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

struct HTML {
    GumboOutput * const output;
    GumboElement *focus;

    HTML(const HTML&) = delete;
    HTML(HTML&&) = delete;
    HTML& operator=(const HTML&) = delete;
    HTML& operator=(HTML&&) = delete;

    explicit HTML(char *buffer) : output(gumbo_parse(buffer)), focus(nullptr) {}

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

    class Query {
    private:
        char query[256] = "";
        bool done = false;
        cpr::AsyncResponse request;
        std::string rawData;
        HTML *data = nullptr;

        void getQueryURL(char *out) const {
            sprintf(out, "https://en.wiktionary.org/wiki/%s", query);
        }

        static GumboElement *findContent(GumboNode *root) {
            // TODO: Robustness
            if (root->type != GUMBO_NODE_ELEMENT) return nullptr;
            GumboNode *node = root;
            gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && (*child)->v.element.tag == GUMBO_TAG_BODY)
            gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementIdEquals(&(*child)->v.element, "content"))
            gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementIdEquals(&(*child)->v.element, "bodyContent"))
            gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementIdEquals(&(*child)->v.element, "mw-content-text"))
            gumboFindChild(node, node->v.element.children, (*child)->type == GUMBO_NODE_ELEMENT && gumboElementClassEquals(&(*child)->v.element, "mw-parser-output"))
            if (node->type != GUMBO_NODE_ELEMENT || !gumboElementClassEquals(&node->v.element, "mw-parser-output")) return nullptr;
            return &node->v.element;
        }

        void processResult() {
            data = new HTML(rawData.data());
            data->focus = findContent(data->output->root);
        }

        static void displayRecursive(GumboNode *node) {
            // TODO: What about GUMBO_NODE_WHITESPACE?
            if (node->type == GUMBO_NODE_TEXT && node->v.text.text != nullptr) {
                ImGui::TextUnformatted(node->v.text.text);
            } else if (node->type == GUMBO_NODE_ELEMENT) {
                auto element = node->v.element;
                switch (element.tag) {
                default:
                    gumboForEachChild(element.children) {
                        displayRecursive(*child);
                    }
                }
            }
        }

    public:
        void DisplayAsTabItem() {
            if (!done) {
                if (request.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    auto r = request.get();
                    rawData = r.text;
                    done = true;
                    processResult();
                } else {
                    return;
                }
            }
            assert(data != nullptr);

            if (ImGui::BeginTabItem(query)) { // TODO: Handle the case when there are multiple equal queries.
                // TODO: alternative search results dropdown
                auto content = data->focus;
                if (content != nullptr) {
//                    for (int i = 0; i < content->children.length; i++) {
//                        displayRecursive((GumboNode *) content->children.data[i]);
//                    }
                    gumboForEachChild(content->children) {
                        displayRecursive(*child);
                    }
                } else {
                    ImGui::TextUnformatted("Could not retrieve content.");
                }
                ImGui::EndTabItem();
            }

        }

        explicit Query(char *text) {
            memcpy(query, text, sizeof(query));
            text[0] = '\0';
            char url[512];
            getQueryURL(url);
            request = cpr::GetAsync(cpr::Url{url});
        }
    };

    char input[256] = "";
    std::vector<Query> queries; // TODO: Maybe use list?

public:
    void Display() {
        ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_Appearing);
        ImGui::Begin("Wiktionary");
        // Search field
        ImGui::InputText("##Word", input, 256); // TODO: Handle enter key.
        ImGui::SameLine();
        if (ImGui::Button("Look up")) {
            queries.emplace_back(input);
        }
        // Results
        ImGui::BeginTabBar("Results");
        for (auto &query : queries) {
            query.DisplayAsTabItem();
        }
        ImGui::EndTabBar();
        ImGui::End();
    }
};

static WiktionaryProvider wiktionary;

void init() {
	sg_desc desc = { };
	desc.context = sapp_sgcontext();
	sg_setup(&desc);

	simgui_desc_t simgui_desc = { };
	simgui_setup(&simgui_desc);

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	pass_action.colors[0].action = SG_ACTION_CLEAR;
	pass_action.colors[0].value = { 0.9f, 0.9f, 0.9f, 1.0f };
}

void frame() {
	simgui_new_frame({
		sapp_width(),
		sapp_height(),
		sapp_frame_duration(),
		sapp_dpi_scale(),
	});

//	ImGui::ShowDemoWindow();

    wiktionary.Display();

	sg_begin_default_pass(pass_action, sapp_width(), sapp_height());
	simgui_render();
	sg_end_pass();
	sg_commit();
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

	sapp_desc desc = { };
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
