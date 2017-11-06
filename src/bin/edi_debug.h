#ifndef EDI_DEBUG_H_
# define EDI_DEBUG_H_

#include <Elementary.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for managing debugging features.
 */

#define DEBUG_PROCESS_SLEEPING 0
#define DEBUG_PROCESS_ACTIVE 1

typedef struct _Edi_Debug_Tool {
   const char *name;
   const char *exec;
   const char *arguments;
   const char *command_start;
   const char *command_continue;
   const char *command_arguments;
   Eina_Bool  external;
} Edi_Debug_Tool;

/**
 * @brief Debug management functions.
 * @defgroup Debug
 *
 * @{
 *
 * Initialisation and management of debugging features.
 *
 */

/**
 * Obtain process information of debugged process.
 *
 * @param state Value to obtain process state.
 *
 * @return process id of debugged process.
 *
 * @ingroup Debug
 */
int edi_debug_process_id(int *state);

/**
 * Obtain debugging info for given program name.
 *
 * @param name The name of the tool used to obtain helper data for given program.
 *
 * @return Pointer to debugging information instance associated with its name.
 *
 * @ingroup Debug
 */
Edi_Debug_Tool *edi_debug_tool_get(const char *name);

/**
 * Return a pointer to the list of available debugging tools.
 *
 * @return Pointer to debugging information for all available tools.
 *
 * @ingroup Debug
 */
Edi_Debug_Tool *edi_debug_available_tools_get(void);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif

#endif /* EDI_DEBUG_H_ */
