#include <windef.h>
#include <stdbool.h>

typedef struct EnumString EnumString;

typedef struct EnumBinding
{
    unsigned int* _TargetValue;
    HWND _ComboBox;
    const EnumString* _EnumStrings;
} EnumBinding;

typedef struct FloatBinding
{
    float* _TargetValue;
    HWND _Field;
} FloatBinding;

typedef struct BoolBinding
{
    bool* _TargetValue;
    HWND _CheckBox;
} BoolBinding;

typedef struct Cell
{
    int _X, _Y, _W, _H;
} Cell;

typedef struct GUIData GUIData;

HWND CreateText(const char* text, const char* tooltip, GUIData* guiData);
void CreatePercentField(const char* tooltip, float* value, GUIData* appData);
void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData);
HWND CreateButton(const char* text, HMENU ID, GUIData* guiData);
void CreateBoolControl(const char* tooltip, bool* value, GUIData* appData);
void GridLayout(int columns, GUIData* guiData);
void ApplyBindings(const GUIData* guiData);
void GUIWindow(void (*setupGUI)(GUIData*, void*),
    void (*buttonMessage)(UINT, GUIData*, void*),
    void* userAppData,
    HANDLE instance, const char* className);
void SetBoldFont(GUIData* gui);
void SetNormalFont(GUIData* gui);
void AlignLeft(GUIData* gui);
void AlignCenter(GUIData* gui);
void WhiteSpace(GUIData* gui);
void CloseGUI(GUIData* gui);