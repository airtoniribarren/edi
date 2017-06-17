#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/wait.h>

#include <Ecore.h>

#include "Edi.h"
#include "edi_private.h"

EAPI int
edi_exe_wait(const char *command)
{
   pid_t pid;
   Ecore_Exe *exe;
   int exit;

   ecore_thread_main_loop_begin();
   exe = ecore_exe_pipe_run(command,
                            ECORE_EXE_PIPE_READ_LINE_BUFFERED | ECORE_EXE_PIPE_READ |
                            ECORE_EXE_PIPE_ERROR_LINE_BUFFERED | ECORE_EXE_PIPE_ERROR |
                            ECORE_EXE_PIPE_WRITE | ECORE_EXE_USE_SH, NULL);
   pid = ecore_exe_pid_get(exe);
   ecore_thread_main_loop_end();

   waitpid(pid, &exit, 0);
   return exit;
}

EAPI char *
edi_exe_response(const char *command)
{
   FILE *p;
   char buf[8192];
   Eina_Strbuf *lines;
   char *out;
   ssize_t len;

   p = popen(command, "r");
   if (!p)
     return NULL;

   lines = eina_strbuf_new();

   while ((fgets(buf, sizeof(buf), p)) != NULL)
     {
        eina_strbuf_append_printf(lines, "%s", buf);
     }

   pclose(p);

   len = eina_strbuf_length_get(lines);
   eina_strbuf_remove(lines, len - 1, len);

   out = strdup(eina_strbuf_string_get(lines));

   eina_strbuf_free(lines);

   return out;
}
