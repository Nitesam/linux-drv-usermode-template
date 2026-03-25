#ifndef DBD_MODULE_H
#define DBD_MODULE_H

#include "../../game_interface.h"
#include "dbd_esp.h"
#include "dbd_reader.h"

#include "imgui.h"

class DbdModule : public GameModule {
public:
    DbdModule() = default;

    const char* game_name()     override { return "Dead by Daylight"; }
    const char* process_name()  override { return "DeadByDaylight-Win64-Shipping.exe"; }
    const char* module_filter() override { return "DeadByDaylight"; }

    void update(MemClient& client, int pid, uint64_t base) override {
        if (!reader_)
            reader_ = std::make_unique<DbdReader>(client);
        state_ = reader_->update(pid, base);
    }

    bool is_valid()     override { return state_.valid; }
    int  player_count() override { return state_.player_count; }

    std::string status_text() override {
        std::string s;
        if (state_.has_camera) {
            char buf[128];
            snprintf(buf, sizeof(buf), "FOV: %.0f  Pos: (%.0f, %.0f, %.0f)",
                state_.camera.FOV,
                state_.camera.Location.X,
                state_.camera.Location.Y,
                state_.camera.Location.Z);
            s = buf;
        }
        int surv = 0, kill = 0;
        for (auto& p : state_.players) {
            if (p.type == EDbdActorType::Survivor) surv++;
            else kill++;
        }
        if (surv > 0 || kill > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  Surv: %d  Kill: %d", surv, kill);
            s += buf;
        }
        return s;
    }

    void render_controls() override {
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Max Dist", &esp_.settings.max_distance, 50.0f, 500.0f, "%.0f");
    }

    void render_table() override {
        if (!state_.valid && !state_.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Error: %s", state_.error.c_str());
        }

        if (!ImGui::BeginTable("DbdPlayerTable", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp))
            return;

        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Role",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Dist (m)", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int idx = 0;
        for (const auto& p : state_.players) {
            if (!p.valid) continue;

            ImGui::TableNextRow();

            ImVec4 role_col = (p.type == EDbdActorType::Survivor)
                ? ImVec4(0.0f, 0.86f, 0.2f, 1.0f)
                : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

            ImGui::TableNextColumn(); ImGui::Text("%d", ++idx);
            ImGui::TableNextColumn();
            ImGui::TextColored(role_col, "%s",
                p.type == EDbdActorType::Survivor ? "Survivor" : "Killer");
            ImGui::TableNextColumn(); ImGui::Text("%s", p.name.c_str());
            ImGui::TableNextColumn();
            if (p.distance > 0)
                ImGui::Text("%.0f", p.distance);
            else
                ImGui::Text("-");

            ImGui::TableNextColumn();
            if (DbdIsFiniteVec(p.position))
                ImGui::Text("%.0f,%.0f,%.0f", p.position.X, p.position.Y, p.position.Z);
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "n/a");
        }

        ImGui::EndTable();
    }

    void render_esp(ImDrawList* draw_list, int screen_w, int screen_h) override {
        esp_.render(draw_list, state_, screen_w, screen_h);
    }

    void render_esp_controls() override {
        esp_.render_controls();
    }

private:
    std::unique_ptr<DbdReader> reader_;
    DbdWorldState state_{};
    static inline DbdEspRenderer esp_{};
};

#endif
