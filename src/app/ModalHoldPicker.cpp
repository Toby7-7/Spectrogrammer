#include <string.h>
#include <time.h>
#include <stdio.h>
#include "imgui.h"
#include "ModalHoldPicker.h"
#include "FilePicker.h"
#include "SpectrumFile.h"

static bool BuildPath(char *out, size_t out_size, const char *dir, const char *name)
{
    if (out == nullptr || dir == nullptr || name == nullptr || out_size == 0)
        return false;
    const int n = snprintf(out, out_size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < out_size;
}

static linked_list *pList = NULL;
static const char *pSelectedFilename = NULL;

void RefreshFiles(const char *pWorkingDirectory)
{
    pSelectedFilename = NULL;
    if (pList!=NULL)
    {
        FreeLinkedList(pList);
    }
    pList = GetFilesInFolder(pWorkingDirectory);
    pList = SortLinkedList(pList);
}

bool HoldPicker(const char *pWorkingDirectory, bool bCanSave, BufferIODouble *pBuffer, float *sample_rate, uint32_t *fft_size)
{
    if (bCanSave==false)
        pSelectedFilename = NULL;

    bool res = false;
    if (ImGui::BeginListBox("##files", ImVec2(-1, 5 * ImGui::GetTextLineHeightWithSpacing())))
    {
        linked_list *pTmp = pList;
        while(pTmp != NULL)
        {
            const bool is_selected = (pSelectedFilename == pTmp->pStr);
            if (ImGui::Selectable(pTmp->pStr, is_selected))
            {
                pSelectedFilename = pTmp->pStr;

                char filename[1024];
                if (BuildPath(filename, sizeof(filename), pWorkingDirectory, pTmp->pStr))
                    res = LoadSpectrum(filename, pBuffer, sample_rate, fft_size);
            }

            pTmp = pTmp->pNext;
        }
        ImGui::EndListBox();
    }

    // Save file

    bool bDisableSave = false;

    if ((pSelectedFilename != NULL) || (bCanSave==false))
        bDisableSave = true;

    if (bDisableSave) 
        ImGui::BeginDisabled();

    if ( ImGui::Button("Save"))
    {
        time_t timer;
        struct tm* tm_info;
        timer = time(NULL);
        tm_info = localtime(&timer);

        char base_name[64];
        strftime(base_name, sizeof(base_name), "%Y%m%d-%H%M%S_data.spec", tm_info);
        char filename[1024];
        if (BuildPath(filename, sizeof(filename), pWorkingDirectory, base_name))
            res = SaveSpectrum(filename, pBuffer, *sample_rate, *fft_size);

        RefreshFiles(pWorkingDirectory);
    }

    if (bDisableSave) 
        ImGui::EndDisabled();

    // Delete selectedfile

    bool bDisableFileOperations = (pSelectedFilename == NULL);

    if (bDisableFileOperations) 
        ImGui::BeginDisabled();

    ImGui::SameLine();
    static char newFilename[1024];
    if (ImGui::Button("Rename"))
    {
        ImGui::OpenPopup("Rename");
        snprintf(newFilename, sizeof(newFilename), "%s", pSelectedFilename);
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rename", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("NewName");
        ImGui::InputText("###Name", newFilename, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) 
        { 
            char oldFilename[1024];
            char renamedFilename[1024];
            if (BuildPath(oldFilename, sizeof(oldFilename), pWorkingDirectory, pSelectedFilename) &&
                BuildPath(renamedFilename, sizeof(renamedFilename), pWorkingDirectory, newFilename))
            {
                if (rename(oldFilename, renamedFilename) == 0)
                    RefreshFiles(pWorkingDirectory);
            }
            ImGui::CloseCurrentPopup(); 
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        ImGui::OpenPopup("Delete?");
    }

    center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Are you sure?");
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) 
        { 
            char filename[1024];
            if (BuildPath(filename, sizeof(filename), pWorkingDirectory, pSelectedFilename))
            {
                if (remove(filename) == 0)
                    RefreshFiles(pWorkingDirectory);
            }

            ImGui::CloseCurrentPopup(); 
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (bDisableFileOperations) 
        ImGui::EndDisabled();

    return res;
}
