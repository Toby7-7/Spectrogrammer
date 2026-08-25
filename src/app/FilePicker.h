
struct linked_list
{
    char *pStr;
    linked_list *pNext;
};

// Lists regular names in pDirectory (no extension filter).
linked_list *GetFilesInFolder(const char *pDirectory);
// Lists names in pDirectory ending with any of `suffixes` (case-insensitive),
// e.g. {".wav"} for the file-analysis picker.
linked_list *GetFilesInFolderEx(const char *pDirectory, const char *const *suffixes, int suffix_count);
linked_list *SortLinkedList(linked_list *node);
void FreeLinkedList(linked_list *node);