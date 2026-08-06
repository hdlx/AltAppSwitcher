#include <windef.h>
#include <stdbool.h>
typedef struct EnumString EnumString;
typedef struct gui_window_data gui_window_data;
HWND CreateText(const char* text, const char* tooltip, gui_window_data* guiData);
void CreatePercentField(const char* tooltip, float* value, gui_window_data* guiData);
void CreateIntField(const char* tooltip, int* value, gui_window_data* guiData);
void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, gui_window_data* guiData);
HWND CreateKeyInputField(gui_window_data* guiData, unsigned int* target);
HWND CreateButton(const char* text, gui_window_data* guiData, void (*fn)(void*), void* data);
void CreateBoolControl(const char* tooltip, bool* value, gui_window_data* guiData);
void GridLayout(int columns, gui_window_data* guiData);
void ApplyBindings(const gui_window_data* guiData);
void GUIWindow(void (*setupGUI)(gui_window_data*, void*),
    void* userAppData,
    HANDLE instance, const char* className);
void SetBoldFont(gui_window_data* gui);
void SetNormalFont(gui_window_data* gui);
void AlignLeft(gui_window_data* gui);
void AlignCenter(gui_window_data* gui);
void WhiteSpace(gui_window_data* gui);
void CloseGUI(gui_window_data* gui);

void set_win_data(HWND win, void* d);
void* get_win_data(HWND win);
HINSTANCE get_instance(HWND win);