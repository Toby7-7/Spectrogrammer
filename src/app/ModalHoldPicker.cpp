#include <string.h>
#include <time.h>
#include <stdio.h>
#include <string>
#include "imgui.h"
#include "ModalHoldPicker.h"
#include "FilePicker.h"
#include "SpectrumFile.h"
#include "Spectrogrammer.h"

static bool BuildPath(char *out, size_t out_size, const char *dir, const char *name)
{
    if (out == nullptr || dir == nullptr || name == nullptr || out_size == 0)
        return false;
    const int n = snprintf(out, out_size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < out_size;
}

static linked_list *pList = NULL;
static std::string sSelectedFilename;

void FreeHoldPickerFiles()
{
    sSelectedFilename.clear();
    if (pList != NULL)
    {
        FreeLinkedList(pList);
        pList = NULL;
    }
}

void RefreshFiles(const char *pWorkingDirectory)
{
    sSelectedFilename.clear();
    if (pList != NULL)
    {
        FreeLinkedList(pList);
        pList = NULL;
    }
    const char *suffixes[] = {".spec"};
    pList = GetFilesInFolderEx(pWorkingDirectory, suffixes, 1);
    if (pList != NULL)
        pList = SortLinkedList(pList);
}

HoldPickerResult HoldPicker(const char *pWorkingDirectory,
                            bool bCanSave,
                            const BufferIODouble *in_snapshot,
                            BufferIODouble *out_loaded,
                            float *sample_rate,
                            uint32_t *fft_size,
                            std::string *status_msg)
{
    HoldPickerResult result = HoldPickerResult::None;

    if (pList == NULL && pWorkingDirectory != nullptr)
        RefreshFiles(pWorkingDirectory);

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float spacing_x = ImGui::GetStyle().ItemSpacing.x;
    const float btn_w = (avail_w - spacing_x) * 0.5f;
    const float btn_h = std::max(64.0f, ImGui::GetFrameHeight() * 1.45f);

    const float list_height = std::max(180.0f, 6.0f * ImGui::GetTextLineHeightWithSpacing());
    if (ImGui::BeginListBox("##spec_files", ImVec2(-FLT_MIN, list_height)))
    {
        if (pList == NULL)
        {
            ImGui::TextDisabled("%s", ui_text("No .spec files found in app folder.", "应用目录中未找到 .spec 快照文件。"));
        }
        else
        {
            linked_list *pTmp = pList;
            while (pTmp != NULL)
            {
                const bool is_selected = (sSelectedFilename == pTmp->pStr);
                if (ImGui::Selectable(pTmp->pStr, is_selected, 0, ImVec2(0.0f, ImGui::GetFrameHeight() * 1.15f)))
                {
                    if (is_selected)
                        sSelectedFilename.clear();
                    else
                        sSelectedFilename = pTmp->pStr;
                }
                pTmp = pTmp->pNext;
            }
        }
        ImGui::EndListBox();
    }

    const bool has_selection = !sSelectedFilename.empty();
    if (has_selection)
    {
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s: %s",
                           ui_text("Selected file", "已选择快照"), sSelectedFilename.c_str());
    }
    else
    {
        ImGui::TextDisabled("%s", ui_text("Tap a snapshot to select. Tapping again deselects.", "点击列表项可选中或取消选中。"));
    }

    ImGui::Spacing();

    // Row 1: Load and Save
    if (!has_selection)
        ImGui::BeginDisabled();
    if (ImGui::Button(ui_text("Load Snapshot", "加载选中快照"), ImVec2(btn_w, btn_h)))
    {
        char filename[1024];
        if (BuildPath(filename, sizeof(filename), pWorkingDirectory, sSelectedFilename.c_str()))
        {
            BufferIODouble temp;
            float loaded_rate = 0.0f;
            uint32_t loaded_fft = 0;
            if (LoadSpectrum(filename, &temp, &loaded_rate, &loaded_fft))
            {
                if (out_loaded != nullptr)
                    out_loaded->copy(&temp);
                if (sample_rate != nullptr)
                    *sample_rate = loaded_rate;
                if (fft_size != nullptr)
                    *fft_size = loaded_fft;
                if (status_msg != nullptr)
                    *status_msg = std::string(ui_text("Loaded snapshot: ", "已加载快照：")) + sSelectedFilename;
                result = HoldPickerResult::Loaded;
            }
            else
            {
                if (status_msg != nullptr)
                    *status_msg = ui_text("Failed to load file.", "加载文件失败（格式不正确）。");
            }
        }
    }
    if (!has_selection)
        ImGui::EndDisabled();

    ImGui::SameLine();

    const bool can_save_snapshot = bCanSave && in_snapshot != nullptr && in_snapshot->GetSize() > 0;
    if (!can_save_snapshot)
        ImGui::BeginDisabled();
    static char saveFilename[1024];
    if (ImGui::Button(ui_text("Save Snapshot...", "保存当前快照..."), ImVec2(btn_w, btn_h)))
    {
        time_t timer = time(NULL);
        struct tm *tm_info = localtime(&timer);
        strftime(saveFilename, sizeof(saveFilename), "%Y%m%d-%H%M%S_data.spec", tm_info);
        ImGui::OpenPopup(ui_text("Save Snapshot###save_snap_popup", "保存快照###save_snap_popup"));
    }
    if (!can_save_snapshot)
        ImGui::EndDisabled();

    const float modal_w = std::min(ImGui::GetIO().DisplaySize.x * 0.90f, 540.0f);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowSize(ImVec2(modal_w, -1.0f));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(ui_text("Save Snapshot###save_snap_popup", "保存快照###save_snap_popup"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", ui_text("Enter snapshot file name:", "输入快照文件名："));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("###SaveName", saveFilename, sizeof(saveFilename), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();
        const float m_avail_w = ImGui::GetContentRegionAvail().x;
        const float m_btn_w = (m_avail_w - spacing_x) * 0.5f;
        const float m_btn_h = std::max(56.0f, ImGui::GetFrameHeight() * 1.3f);
        if (ImGui::Button(ui_text("Save", "保存"), ImVec2(m_btn_w, m_btn_h)))
        {
            std::string final_name = saveFilename;
            if (final_name.find('.') == std::string::npos)
                final_name += ".spec";

            char filename[1024];
            if (BuildPath(filename, sizeof(filename), pWorkingDirectory, final_name.c_str()))
            {
                const float rate = (sample_rate != nullptr) ? *sample_rate : 48000.0f;
                const uint32_t fft = (fft_size != nullptr) ? *fft_size : (static_cast<uint32_t>(in_snapshot->GetSize() - 1) * 2);
                if (SaveSpectrum(filename, in_snapshot, rate, fft))
                {
                    RefreshFiles(pWorkingDirectory);
                    sSelectedFilename = final_name;
                    if (status_msg != nullptr)
                        *status_msg = std::string(ui_text("Saved snapshot: ", "已保存快照：")) + final_name;
                    result = HoldPickerResult::Saved;
                }
                else
                {
                    if (status_msg != nullptr)
                        *status_msg = ui_text("Failed to save snapshot.", "保存快照失败。");
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(ui_text("Cancel", "取消"), ImVec2(m_btn_w, m_btn_h)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Row 2: Rename and Delete
    if (!has_selection)
        ImGui::BeginDisabled();
    static char newFilename[1024];
    if (ImGui::Button(ui_text("Rename...", "重命名快照..."), ImVec2(btn_w, btn_h)))
    {
        ImGui::OpenPopup(ui_text("Rename###rename_popup", "重命名###rename_popup"));
        snprintf(newFilename, sizeof(newFilename), "%s", sSelectedFilename.c_str());
    }

    center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowSize(ImVec2(modal_w, -1.0f));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(ui_text("Rename###rename_popup", "重命名###rename_popup"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s", ui_text("New file name:", "新文件名："));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("###Name", newFilename, sizeof(newFilename), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();
        const float m_avail_w = ImGui::GetContentRegionAvail().x;
        const float m_btn_w = (m_avail_w - spacing_x) * 0.5f;
        const float m_btn_h = std::max(56.0f, ImGui::GetFrameHeight() * 1.3f);
        if (ImGui::Button(ui_text("OK", "确定"), ImVec2(m_btn_w, m_btn_h)))
        {
            char oldFilename[1024];
            char renamedFilename[1024];
            if (BuildPath(oldFilename, sizeof(oldFilename), pWorkingDirectory, sSelectedFilename.c_str()) &&
                BuildPath(renamedFilename, sizeof(renamedFilename), pWorkingDirectory, newFilename))
            {
                if (rename(oldFilename, renamedFilename) == 0)
                {
                    RefreshFiles(pWorkingDirectory);
                    sSelectedFilename = newFilename;
                    if (status_msg != nullptr)
                        *status_msg = ui_text("File renamed successfully.", "文件重命名成功。");
                    result = HoldPickerResult::Renamed;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(ui_text("Cancel", "取消"), ImVec2(m_btn_w, m_btn_h)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button(ui_text("Delete...", "删除快照..."), ImVec2(btn_w, btn_h)))
    {
        ImGui::OpenPopup(ui_text("Delete?###delete_popup", "确认删除？###delete_popup"));
    }

    center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowSize(ImVec2(modal_w, -1.0f));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(ui_text("Delete?###delete_popup", "确认删除？###delete_popup"), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s\n%s", ui_text("Delete selected spectrum file?", "确定要删除该频谱文件吗？"), sSelectedFilename.c_str());
        ImGui::Separator();
        const float m_avail_w = ImGui::GetContentRegionAvail().x;
        const float m_btn_w = (m_avail_w - spacing_x) * 0.5f;
        const float m_btn_h = std::max(56.0f, ImGui::GetFrameHeight() * 1.3f);
        if (ImGui::Button(ui_text("Delete", "删除"), ImVec2(m_btn_w, m_btn_h)))
        {
            char filename[1024];
            if (BuildPath(filename, sizeof(filename), pWorkingDirectory, sSelectedFilename.c_str()))
            {
                if (remove(filename) == 0)
                {
                    RefreshFiles(pWorkingDirectory);
                    sSelectedFilename.clear();
                    if (status_msg != nullptr)
                        *status_msg = ui_text("File deleted.", "文件已删除。");
                    result = HoldPickerResult::Deleted;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(ui_text("Cancel", "取消"), ImVec2(m_btn_w, m_btn_h)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!has_selection)
        ImGui::EndDisabled();

    return result;
}
