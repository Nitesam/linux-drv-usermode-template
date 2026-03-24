#ifndef HLL_MODULE_H
#define HLL_MODULE_H

#include "../../game_interface.h"
#include "hll_reader.h"

#include "imgui.h"

class HllModule : public GameModule {
public:
    HllModule() = default;

    const char* game_name()     override { return "Hell Let Loose"; }
    const char* process_name()  override { return "HLLEpicGamesStore-Win64-Shipping.exe"; }
    const char* module_filter() override { return "HLLEpicGamesStore"; }

    void update(MemClient& client, int pid, uint64_t base) override {
        if (!reader_)
            reader_ = std::make_unique<HllReader>(client);
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
            if (state_.has_local_team) {
                s += "  Team: " + std::to_string(state_.local_team);
            }
            if (state_.has_local_ammo) {
                s += "  Ammo: " + std::to_string(state_.local_ammo.CurrentAmmo);
            }
        }
        return s;
    }

    void render_controls() override {
        ImGui::Checkbox("Team Filter", &team_filter_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Max Dist", &max_distance_, 50.0f, 1000.0f, "%.0f");
    }

    void render_table() override {
        if (!state_.valid && !state_.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Error: %s", state_.error.c_str());
        }

        if (!ImGui::BeginTable("PlayerTable", 8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp))
            return;

        ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Team",     ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Role",     ImGuiTableColumnFlags_WidthFixed, 35);
        ImGui::TableSetupColumn("HP",       ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Weapon",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Dist (m)", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bones",    ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const auto& p : state_.players) {
            if (!p.valid) continue;
            if (team_filter_ && state_.has_local_team &&
                p.team == state_.local_team && p.team != 0)
                continue;
            if (p.distance > max_distance_ && p.distance > 0)
                continue;

            ImGui::TableNextRow();
            bool is_dead = p.health <= 0.0f;
            if (is_dead)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.7f));

            ImGui::TableNextColumn(); ImGui::Text("%d", p.index);
            ImGui::TableNextColumn(); ImGui::Text("%d", p.team);
            ImGui::TableNextColumn(); ImGui::Text("%d", p.role);

            ImGui::TableNextColumn();
            if (p.health > 50.0f)
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%.0f", p.health);
            else if (p.health > 0)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%.0f", p.health);
            else
                ImGui::TextColored(ImVec4(0.6f, 0.2f, 0.2f, 1.0f), "DEAD");

            ImGui::TableNextColumn(); ImGui::Text("%s", p.weapon_name.c_str());

            ImGui::TableNextColumn();
            if (p.distance > 0)
                ImGui::Text("%.0f", p.distance);
            else
                ImGui::Text("-");

            ImGui::TableNextColumn();
            if (p.has_location && IsFiniteVector(p.location))
                ImGui::Text("%.0f,%.0f,%.0f", p.location.X, p.location.Y, p.location.Z);
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "n/a");

            ImGui::TableNextColumn(); ImGui::Text("%d", (int)p.bone_transforms.size());

            if (is_dead)
                ImGui::PopStyleColor();
        }

        ImGui::EndTable();
    }

private:
    std::unique_ptr<HllReader> reader_;
    HllWorldState state_{};
    bool  team_filter_   = true;
    float max_distance_  = 300.0f;
};

#endif
