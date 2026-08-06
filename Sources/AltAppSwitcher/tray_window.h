#include <windef.h>
struct aas_tray;
struct aas_tray* tray_init(HINSTANCE instance);
void tray_deinit(struct aas_tray* tray);