#include "FilePicker.h"
#include <dirent.h>
#include <stdlib.h> 
#include <string.h>
#include <strings.h>

static int has_suffix_ci(const char *name, const char *suffix)
{
    const size_t nlen = strlen(name);
    const size_t slen = strlen(suffix);
    if (nlen < slen)
        return 0;
    return strcasecmp(name + (nlen - slen), suffix) == 0 ? 1 : 0;
}

linked_list *GetFilesInFolder(const char *pDirectory)
{
    linked_list *head = NULL;

    DIR *dir = opendir(pDirectory);
    if (dir != NULL) 
    {
        for(;;) 
        {
            struct dirent *ent = readdir(dir);
            if (ent==NULL)
                break;
            const char *pS = ent->d_name;

            if (strcmp(pS, ".")==0 || strcmp(pS, "..")==0)
                continue;

            linked_list *node = (linked_list *)malloc(sizeof(linked_list));
            node->pStr = (char*)malloc(strlen(pS)+1);
            strcpy(node->pStr, pS);            

            node->pNext = head;
            head = node;
        }
        closedir (dir);    
    }
    return head;
}

linked_list *GetFilesInFolderEx(const char *pDirectory, const char *const *suffixes, int suffix_count)
{
    linked_list *head = NULL;

    DIR *dir = opendir(pDirectory);
    if (dir != NULL)
    {
        for(;;)
        {
            struct dirent *ent = readdir(dir);
            if (ent==NULL)
                break;
            const char *pS = ent->d_name;

            if (strcmp(pS, ".")==0 || strcmp(pS, "..")==0)
                continue;

            int match = 0;
            for (int i = 0; i < suffix_count; i++)
            {
                if (suffixes[i] != NULL && has_suffix_ci(pS, suffixes[i]))
                {
                    match = 1;
                    break;
                }
            }
            if (!match)
                continue;

            linked_list *node = (linked_list *)malloc(sizeof(linked_list));
            node->pStr = (char*)malloc(strlen(pS)+1);
            strcpy(node->pStr, pS);

            node->pNext = head;
            head = node;
        }
        closedir (dir);
    }
    return head;
}

int LinkedListSize(const linked_list *node)
{
    int i=0;
    while(node!=NULL)
    {
        i++;
        node = node->pNext;
    }
    return i;
}

int sortstring( const void *str1, const void *str2 )
{
    const char **pp1 = (const char **)str1;
    const char **pp2 = (const char **)str2;
    return strcmp(*pp1, *pp2);
}

linked_list *SortLinkedList(linked_list *node)
{
    int count = LinkedListSize(node);
    if (count<=1)
        return node;

    char **array = (char **)malloc(sizeof(char **) * count);

    linked_list *head = node;

    for(int i=0; i<count; i++)
    {
        array[i] = node->pStr;
        node = node->pNext;
    }

    qsort(array, count, sizeof(char *), sortstring);

    node = head;
    for(int i=0; i<count; i++)
    {
        node->pStr = array[i];
        node = node->pNext;
    }
    free(array);

    return head;
}


void FreeLinkedList(linked_list *node)
{    
    while(node!=NULL)
    {
        free(node->pStr);
        linked_list *next = node->pNext;
        free(node);
        node = next;
    }
}

#ifdef TEST
#include <cstdio>

void Print(linked_list *node)
{
    while(node!=NULL)
    {
        printf("%s\n", node->pStr);
        node = node->pNext;
    }
}

int main()
{
    linked_list *files = GetFilesInFolder(".");
    files = SortLinkedList(files);   
    Print(files);
    FreeLinkedList(files);
    return 0;
}
#endif