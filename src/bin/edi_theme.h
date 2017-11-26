#ifndef __EDI_THEME_H__
#define __EDI_THEME_H__

#include <Elementary.h>

typedef struct _Edi_Theme {
        char *name;
        char *path;
} Edi_Theme;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines used for managing Edi theme actions.
 */

/**
 * @brief Theme management functions.
 * @defgroup Theme
 *
 * @{
 *
 * Management of theming actions. 
 *
 */

/**
 * Set the Edi theme by name.
 * 
 * @param obj The object to apply the theme to.
 * @param name The name of the theme to apply.
 *
 * @ingroup Theme
 */
void edi_theme_theme_set(Evas_Object *obj, const char *name);

/**
 * Get a list of all themes available.
 * 
 * @return a list of all available themes as Edi_Theme instances.
 *
 * @ingroup Theme
 */
Eina_List *edi_theme_themes_get(void);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif



#endif
