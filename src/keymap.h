#include <dev/wscons/wsksymdef.h>

#define WSDV_EXIT	KS_Escape
#define WSDV_NEXTIMG	KS_space
#define WSDV_PREVIMG	KS_BackSpace
#define WSDV_SWITCH_SCREEN_2	KS_2

int	wsdv_keycode_translate(int);
bool	wsdv_keymap_load(char *);
