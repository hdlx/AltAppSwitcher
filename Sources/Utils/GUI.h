#include <windef.h>
#include <stdbool.h>
typedef struct EnumString EnumString;
typedef struct GUIData GUIData;
HWND CreateText(const char* text, const char* tooltip, GUIData* guiData);
void CreatePercentField(const char* tooltip, float* value, GUIData* guiData);
void CreateIntField(const char* tooltip, int* value, GUIData* guiData);
void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData);
HWND CreateKeyInputField(GUIData* guiData, unsigned int* target);
HWND CreateButton(const char* text, GUIData* guiData, void (*fn)(void*), void* data);
void CreateBoolControl(const char* tooltip, bool* value, GUIData* guiData);
void GridLayout(int columns, GUIData* guiData);
void ApplyBindings(const GUIData* guiData);
void GUIWindow(void (*setupGUI)(GUIData*, void*),
    void* userAppData,
    HANDLE instance, const char* className);
void SetBoldFont(GUIData* gui);
void SetNormalFont(GUIData* gui);
void AlignLeft(GUIData* gui);
void AlignCenter(GUIData* gui);
void WhiteSpace(GUIData* gui);
void CloseGUI(GUIData* gui);