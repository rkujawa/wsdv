#include <stdio.h>

#include <prop/proplib.h>

#define MAX_KEY_SIZE 16 
#define MAX_VAL_SIZE 16 

static prop_dictionary_t pd;

int
wsdv_keycode_translate(int eventval) 
{
	int tkeycode;
	bool r;
	char okeycode[MAX_KEY_SIZE];

	if (pd == NULL)
		return eventval;

	snprintf(okeycode, MAX_KEY_SIZE-1, "%d", eventval);
	r = prop_dictionary_get_int32(pd, okeycode, &tkeycode);
#if 0
	if (!r) {
		snprintf(okeycode, MAX_KEY_SIZE-1, "%x", eventval);
		r = prop_dictionary_get_int32(pd, okeycode, &tkeycode);
	}
#endif 

	if (!r) {
		fprintf(stderr, "unable to map event value %s\n", okeycode);
		return eventval;
	}

#if WSDV_DEBUG
	fprintf(stderr, "translated event value %x into %x\n", eventval,
	    tkeycode);
#endif

	return tkeycode;
}

bool
wsdv_keymap_load(char *file)
{
	pd = prop_dictionary_internalize_from_file(file);
	return pd;
}

#if 0 
void
wsdv_keymap_save_default(void)
{
	int32_t v;
	char key[MAX_KEY_SIZE];
	char val[MAX_VAL_SIZE];

	pd = prop_dictionary_create();

	snprintf(key, MAX_KEY_SIZE-1, "%d", 0x29);
	//snprintf(val, MAX_VAL_SIZE-1, "%d", 0x29);
	prop_dictionary_set_int32(pd, key, 0x29);

	prop_dictionary_externalize_to_file(pd, "map1.plist");
		
}

int
main(int argc, void *argv[]) {
	wsdv_keymap_load("map1.plist");
	wsdv_keycode_translate(0x2C);
	wsdv_keycode_translate(0x2B);
}
#endif 

