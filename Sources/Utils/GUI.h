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

typedef struct GUIData
{
    EnumBinding _EBindings[64];
    unsigned int _EBindingCount;
    FloatBinding _FBindings[64];
    unsigned int _FBindingCount;
    BoolBinding _BBindings[64];
    unsigned int _BBindingCount;
    HFONT _Font;
    HFONT _FontTitle;
    HBRUSH _Background;
    HWND _Parent;
    Cell _Cell;
    int _Columns;
    int _Column;
} GUIData;

void CreateText(const char* text, const char* tooltip, GUIData* guiData);
void CreateFloatField(const char* tooltip, float* value, GUIData* appData);
void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData);
void CreateButton(const char* text, HMENU ID, GUIData* guiData);
void CreateBoolControl(const char* tooltip, bool* value, GUIData* appData);
void InitGUIData(GUIData* guiData, HWND parent);
void DeleteGUIData(GUIData* guiData);
void GridLayout(int columns, GUIData* guiData);
void ApplyBindings(const GUIData* guiData);
void RegisterGUIClass(LRESULT (*windowProc)(HWND, UINT, WPARAM, LPARAM), HANDLE instance, const char* className);
void UnregisterGUIClass(HANDLE instance, const char* className);
void FitParentWindow(const GUIData* gui);