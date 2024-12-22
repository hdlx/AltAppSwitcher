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
    int _CellHeight;
    HWND _Parent;
} GUIData;

void CreateText(Cell c, HWND parent, const char* text, GUIData* guiData);
void CreateTooltip(HWND parent, HWND tool, char* string);
void CreateLabel(Cell c, HWND parent, const char* name, const char* tooltip, GUIData* guiData);
void CreateFloatField(Cell c, HWND parent, const char* name, const char* tooltip, float* value, GUIData* appData);
void CreateComboBox(Cell c, HWND parent, const char* name, const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData);
void CreateButton(Cell c, HWND parent, const char* name, HMENU ID, GUIData* guiData);
void CreateBoolControl(Cell c, HWND parent, const char* name, const char* tooltip, bool* value, GUIData* appData);
void InitGUIData(GUIData* guiData, HWND parent);
void DeleteGUIData(GUIData* guiData);