#include <windef.h>
#include <stdbool.h>

#define WIN_PAD 10
#define LINE_PAD 4
#define DARK_COLOR 0x002C2C2C;
#define LIGHT_COLOR 0x00FFFFFF;

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
} GUIData;

void CreateText(int x, int y, int width, int height, HWND parent, const char* text, GUIData* guiData);
void CreateTooltip(HWND parent, HWND tool, char* string);
void CreateLabel(int x, int y, int width, int height, HWND parent, const char* name, const char* tooltip, GUIData* guiData);
void CreateFloatField(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, float* value, GUIData* appData);
void CreateComboBox(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData);
void CreateButton(int x, int y, int w, int h, HWND parent, const char* name, HMENU ID, GUIData* guiData);
void CreateBoolControl(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, bool* value, GUIData* appData);