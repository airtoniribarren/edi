#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libgen.h>

#include <Eina.h>
#include <Elementary.h>

#include "edi_editor.h"

#include "mainview/edi_mainview.h"
#include "edi_config.h"

#include "edi_private.h"

typedef struct
{
   unsigned int line;
   unsigned int col;
} Edi_Location;

typedef struct
{
   Edi_Location start;
   Edi_Location end;
} Edi_Range;

static Evas_Object *_clang_autocomplete_popup_bg;

void
edi_editor_save(Edi_Editor *editor)
{
   if (!editor->modified)
     return;

   editor->save_time = time(NULL);
   edi_mainview_save();

   editor->modified = EINA_FALSE;
   ecore_timer_del(editor->save_timer);
   editor->save_timer = NULL;
}

static Eina_Bool
_edi_editor_autosave_cb(void *data)
{
   Edi_Editor *editor;

   editor = (Edi_Editor *)data;

   edi_editor_save(editor);
   return ECORE_CALLBACK_CANCEL;
}

static void
_changed_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Edi_Editor *editor = data;

   editor->modified = EINA_TRUE;

   if (editor->save_timer)
     ecore_timer_reset(editor->save_timer);
   else if (_edi_config->autosave)
     editor->save_timer = ecore_timer_add(EDI_CONTENT_SAVE_TIMEOUT, _edi_editor_autosave_cb, editor);
}

#if HAVE_LIBCLANG
static char*
_edi_editor_currnet_word_get(Edi_Editor *editor)
{
   Elm_Code *code;
   Elm_Code_Line *line;
   char *ptr, *curword, *curtext;
   unsigned int curlen, col, row, wordlen;

   elm_obj_code_widget_cursor_position_get(editor->entry, &col, &row);

   code = elm_code_widget_code_get(editor->entry);
   line = elm_code_file_line_get(code->file, row);

   curtext = (char *)elm_code_line_text_get(line, &curlen);
   ptr = curtext + col - 1;

   while (ptr != curtext &&
         ((*(ptr - 1) >= 'a' && *(ptr - 1) <= 'z') ||
          (*(ptr - 1) >= 'A' && *(ptr - 1) <= 'Z') ||
          (*(ptr - 1) >= '0' && *(ptr - 1) <= '9') ||
          *(ptr - 1) == '_'))
     ptr--;

   wordlen = col - (ptr - curtext) - 1;
   curword = malloc(sizeof(char) * (wordlen + 1));
   strncpy(curword, ptr, wordlen);
   curword[wordlen] = '\0';

   return curword;
}

static void
_autocomplete_list_cb_key_down(void *data EINA_UNUSED, Evas *e EINA_UNUSED,
                               Evas_Object *obj, void *event_info)
{
   Edi_Mainview_Item *item;
   Edi_Editor *editor;
   Elm_Code *code;
   Elm_Code_Line *line;
   Evas_Event_Key_Down *ev = event_info;
   char *word;
   const char *list_word;
   unsigned int wordlen, col, row;

   item = edi_mainview_item_current_get();

   if (!item)
     return;

   editor = (Edi_Editor *)evas_object_data_get(item->view, "editor");

   elm_code_widget_cursor_position_get(editor->entry, &col, &row);

   code = elm_code_widget_code_get(editor->entry);
   line = elm_code_file_line_get(code->file, row);

   if (!strcmp(ev->key, "Down") || !strcmp(ev->key, "Up"))
     return;
   else if (!strcmp(ev->key, "Return"))
     {
        Elm_Object_Item *it;

        word = _edi_editor_currnet_word_get(editor);
        wordlen = strlen(word);
        free(word);

        elm_code_line_text_remove(line, col - wordlen - 1, wordlen);
        it = elm_genlist_selected_item_get(obj);
        list_word = elm_object_item_data_get(it);

        elm_code_line_text_insert(line, col - wordlen - 1,
                                  list_word, strlen(list_word));
        elm_code_widget_cursor_position_set(editor->entry,
                                  col - wordlen + strlen(list_word), row);
     }

   evas_object_del(_clang_autocomplete_popup_bg);
}

static void
_autocomplete_list_cb_focus(void *data EINA_UNUSED,
                            Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *it = event_info;
   elm_genlist_item_selected_set(it, EINA_TRUE);
}

static Evas_Object *
_autocomplete_list_content_get(void *data, Evas_Object *obj, const char *part)
{
   Edi_Editor *editor;
   Edi_Mainview_Item *item;
   Evas_Object *label;
   char *format, *display, *auto_str = data;
   const char *font;
   int font_size, displen;

   if (strcmp(part, "elm.swallow.content"))
     return NULL;

   item = edi_mainview_item_current_get();

   if (!item)
     return NULL;

   editor = (Edi_Editor *)evas_object_data_get(item->view, "editor");
   elm_code_widget_font_get(editor->entry, &font, &font_size);

   format = "<align=left><font=%s><font_size=%d> %s</font_size></font></align>";
   displen = strlen(auto_str) + strlen(format) + strlen(font);
   display = malloc(sizeof(char) * displen);
   snprintf(display, displen, format, font, font_size, auto_str);

   label = elm_label_add(obj);
   elm_object_text_set(label, display);
   evas_object_color_set(label, 255, 255, 255, 255);
   evas_object_size_hint_weight_set(label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(label, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(label);
   free(display);

   return label;
}

static void
_autocomplete_list_del(void *data, Evas_Object *obj EINA_UNUSED)
{
   char *auto_str = data;
   free(auto_str);
}

static void
_autocomplete_list_update(Evas_Object *genlist, Edi_Editor *editor)
{
   Elm_Code *code;
   CXIndex idx;
   CXTranslationUnit tx_unit;
   CXCodeCompleteResults *res;
   char *curword, **clang_argv;
   const char *path, *args;
   unsigned int clang_argc, row, col;

   elm_obj_code_widget_cursor_position_get(editor->entry, &col, &row);

   code = elm_code_widget_code_get(editor->entry);
   path = elm_code_file_path_get(code->file);

   curword = _edi_editor_currnet_word_get(editor);

   //Genlist Item Class
   Elm_Genlist_Item_Class *ic = elm_genlist_item_class_new();
   ic->item_style = "full";
   ic->func.content_get = _autocomplete_list_content_get;
   ic->func.del = _autocomplete_list_del;

   //Initialize Clang
   args = "-I/usr/inclue/ " EFL_CFLAGS " " CLANG_INCLUDES " -Wall -Wextra";
   clang_argv = eina_str_split_full(args, " ", 0, &clang_argc);

   idx = clang_createIndex(0, 0);
   /* FIXME: Possibly activate more options? */
   tx_unit = clang_parseTranslationUnit(idx, path,
                                        (const char *const *)clang_argv,
                                        (int)clang_argc, NULL, 0,
                                        CXTranslationUnit_PrecompiledPreamble);
   clang_reparseTranslationUnit(tx_unit, 0, 0, 0);
   res = clang_codeCompleteAt(tx_unit, path, row, col - strlen(curword), NULL, 0,
                              CXCodeComplete_IncludeMacros |
                              CXCodeComplete_IncludeCodePatterns);

   clang_sortCodeCompletionResults(res->Results, res->NumResults);

   for (unsigned int i = 0; i < res->NumResults; i++)
     {
        const CXCompletionString str = res->Results[i].CompletionString;
        for (unsigned int j = 0; j < clang_getNumCompletionChunks(str); j++)
          {
             if (clang_getCompletionChunkKind(str, j) !=
                 CXCompletionChunk_TypedText)
               continue;

             const CXString str_out = clang_getCompletionChunkText(str, j);
             char *auto_str = strdup(clang_getCString(str_out));

             if (eina_str_has_prefix(auto_str, curword))
               {
                  elm_genlist_item_append(genlist,
                                          ic,
                                          auto_str,
                                          NULL,
                                          ELM_GENLIST_ITEM_NONE,
                                          NULL,
                                          NULL);
               }
          }
     }
   elm_genlist_item_class_free(ic);

   clang_disposeCodeCompleteResults(res);
   clang_disposeTranslationUnit(tx_unit);
   clang_disposeIndex(idx);
   free(curword);
}

static void
_clang_autocomplete_popup(Edi_Editor *editor)
{
   unsigned int col, row;
   Evas_Coord cx, cy, cw, ch;

   elm_obj_code_widget_cursor_position_get(editor->entry, &col, &row);
   elm_code_widget_geometry_for_position_get(editor->entry, row, col,
                                             &cx, &cy, &cw, &ch);
   edi_editor_save(editor);

   //Popup bg
   Evas_Object *bg = elm_bubble_add(editor->entry);
   _clang_autocomplete_popup_bg = bg;
   evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(bg, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_resize(bg, 350, 200);
   evas_object_move(bg, cx, cy);
   evas_object_show(bg);

   //Genlist
   Evas_Object *genlist = elm_genlist_add(editor->entry);
   evas_object_event_callback_add(genlist, EVAS_CALLBACK_KEY_DOWN,
                                  _autocomplete_list_cb_key_down, NULL);
   evas_object_smart_callback_add(genlist, "item,focused",
                                  _autocomplete_list_cb_focus, NULL);
   elm_object_content_set(bg, genlist);
   evas_object_show(genlist);

   _autocomplete_list_update(genlist, editor);

   //Focus first item
   Elm_Object_Item *item = elm_genlist_first_item_get(genlist);
   if (item)
     elm_object_item_focus_set(item, EINA_TRUE);
   else
     evas_object_del(bg);

}
#endif

static void
_smart_cb_key_down(void *data EINA_UNUSED, Evas *e EINA_UNUSED,
                   Evas_Object *obj EINA_UNUSED, void *event)
{
   Edi_Mainview_Item *item;
   Edi_Editor *editor;
   Eina_Bool ctrl, alt, shift;
   Evas_Event_Key_Down *ev = event;

   ctrl = evas_key_modifier_is_set(ev->modifiers, "Control");
   alt = evas_key_modifier_is_set(ev->modifiers, "Alt");
   shift = evas_key_modifier_is_set(ev->modifiers, "Shift");

   item = edi_mainview_item_current_get();

   if (!item)
     return;

   editor = (Edi_Editor *)evas_object_data_get(item->view, "editor");

   if ((!alt) && (ctrl) && (!shift))
     {
        if (!strcmp(ev->key, "Prior"))
          {
             edi_mainview_item_prev();
          }
        else if (!strcmp(ev->key, "Next"))
          {
             edi_mainview_item_next();
          }
        else if (!strcmp(ev->key, "s"))
          {
             edi_editor_save(editor);
          }
        else if (!strcmp(ev->key, "f"))
          {
             edi_mainview_search();
          }
        else if (!strcmp(ev->key, "g"))
          {
             edi_mainview_goto_popup_show();
          }
#if HAVE_LIBCLANG
        else if (!strcmp(ev->key, "space"))
          {
             _clang_autocomplete_popup(editor);
          }
#endif
     }
}

static void
_edit_cursor_moved(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;
   char buf[30];
   unsigned int line;
   unsigned int col;

   widget = (Elm_Code_Widget *)obj;
   elm_code_widget_cursor_position_get(widget, &col, &line);

   snprintf(buf, sizeof(buf), "Line:%d, Column:%d", line, col);
   elm_object_text_set((Evas_Object *)data, buf);
}

static void
_edi_editor_statusbar_add(Evas_Object *panel, Edi_Editor *editor, Edi_Mainview_Item *item)
{
   Evas_Object *position, *mime, *lines;
   Elm_Code *code;

   elm_box_horizontal_set(panel, EINA_TRUE);

   mime = elm_label_add(panel);
   if (item->mimetype)
     elm_object_text_set(mime, item->mimetype);
   else
     elm_object_text_set(mime, item->editortype);
   evas_object_size_hint_align_set(mime, 0.0, 0.5);
   evas_object_size_hint_weight_set(mime, 0.1, 0.0);
   elm_box_pack_end(panel, mime);
   evas_object_show(mime);
   elm_object_disabled_set(mime, EINA_TRUE);

   lines = elm_label_add(panel);
   code = elm_code_widget_code_get(editor->entry);
   if (elm_code_file_line_ending_get(code->file) == ELM_CODE_FILE_LINE_ENDING_WINDOWS)
     elm_object_text_set(lines, "WIN");
   else
     elm_object_text_set(lines, "UNIX");
   evas_object_size_hint_align_set(lines, 0.0, 0.5);
   evas_object_size_hint_weight_set(lines, EVAS_HINT_EXPAND, 0.0);
   elm_box_pack_end(panel, lines);
   evas_object_show(lines);
   elm_object_disabled_set(lines, EINA_TRUE);

   position = elm_label_add(panel);
   evas_object_size_hint_align_set(position, 1.0, 0.5);
   evas_object_size_hint_weight_set(position, EVAS_HINT_EXPAND, 0.0);
   elm_box_pack_end(panel, position);
   evas_object_show(position);
   elm_object_disabled_set(position, EINA_TRUE);

   _edit_cursor_moved(position, editor->entry, NULL);
   evas_object_smart_callback_add(editor->entry, "cursor,changed", _edit_cursor_moved, position);
}

#if HAVE_LIBCLANG
static void
_edi_range_color_set(Edi_Editor *editor, Edi_Range range, Elm_Code_Token_Type type)
{
   Elm_Code *code;
   Elm_Code_Line *line, *extra_line;
   unsigned int number;

   ecore_thread_main_loop_begin();

   code = elm_code_widget_code_get(editor->entry);
   line = elm_code_file_line_get(code->file, range.start.line);

   elm_code_line_token_add(line, range.start.col - 1, range.end.col - 2,
                           range.end.line - range.start.line + 1, type);

   elm_code_widget_line_refresh(editor->entry, line);
   for (number = line->number + 1; number <= range.end.line; number++)
     {
        extra_line = elm_code_file_line_get(code->file, number);
        elm_code_widget_line_refresh(editor->entry, extra_line);
     }

   ecore_thread_main_loop_end();
}

static void
_edi_line_status_set(Edi_Editor *editor, unsigned int number, Elm_Code_Status_Type status,
                     const char *text)
{
   Elm_Code *code;
   Elm_Code_Line *line;

   ecore_thread_main_loop_begin();

   code = elm_code_widget_code_get(editor->entry);
   line = elm_code_file_line_get(code->file, number);
   if (!line)
     {
        if (text)
          ERR("Status on invalid line %d (\"%s\")", number, text);

        ecore_thread_main_loop_end();
        return;
     }

   elm_code_line_status_set(line, status);
   if (text)
     elm_code_line_status_text_set(line, text);

   elm_code_widget_line_refresh(editor->entry, line);

   ecore_thread_main_loop_end();
}

static void
_clang_load_highlighting(const char *path, Edi_Editor *editor)
{
        CXFile cfile = clang_getFile(editor->tx_unit, path);

        CXSourceRange range = clang_getRange(
              clang_getLocationForOffset(editor->tx_unit, cfile, 0),
              clang_getLocationForOffset(editor->tx_unit, cfile, eina_file_size_get(eina_file_open(path, EINA_FALSE))));

        clang_tokenize(editor->tx_unit, range, &editor->tokens, &editor->token_count);
        editor->cursors = (CXCursor *) malloc(editor->token_count * sizeof(CXCursor));
        clang_annotateTokens(editor->tx_unit, editor->tokens, editor->token_count, editor->cursors);
}

static void
_clang_show_highlighting(Edi_Editor *editor)
{
   unsigned int i = 0;

   for (i = 0 ; i < editor->token_count ; i++)
     {
        Edi_Range range;
        Elm_Code_Token_Type type = ELM_CODE_TOKEN_TYPE_DEFAULT;

        CXSourceRange tkrange = clang_getTokenExtent(editor->tx_unit, editor->tokens[i]);
        clang_getSpellingLocation(clang_getRangeStart(tkrange), NULL,
              &range.start.line, &range.start.col, NULL);
        clang_getSpellingLocation(clang_getRangeEnd(tkrange), NULL,
              &range.end.line, &range.end.col, NULL);
        /* FIXME: Should probably do something fancier, this is only a limited
         * number of types. */
        switch (clang_getTokenKind(editor->tokens[i]))
          {
             case CXToken_Punctuation:
                if (i < editor->token_count - 1 && range.start.col == 1 &&
                    (clang_getTokenKind(editor->tokens[i + 1]) == CXToken_Identifier && (editor->cursors[i + 1].kind == CXCursor_MacroDefinition ||
                    editor->cursors[i + 1].kind == CXCursor_InclusionDirective || editor->cursors[i + 1].kind == CXCursor_PreprocessingDirective)))
                  type = ELM_CODE_TOKEN_TYPE_PREPROCESSOR;
                else
                  type = ELM_CODE_TOKEN_TYPE_BRACE;
                break;
             case CXToken_Identifier:
                if (editor->cursors[i].kind < CXCursor_FirstRef)
                  {
                      type = ELM_CODE_TOKEN_TYPE_CLASS;
                      break;
                  }
                switch (editor->cursors[i].kind)
                  {
                   case CXCursor_DeclRefExpr:
                      /* Handle different ref kinds */
                      type = ELM_CODE_TOKEN_TYPE_FUNCTION;
                      break;
                   case CXCursor_MacroDefinition:
                   case CXCursor_InclusionDirective:
                   case CXCursor_PreprocessingDirective:
                      type = ELM_CODE_TOKEN_TYPE_PREPROCESSOR;
                      break;
                   case CXCursor_TypeRef:
                      type = ELM_CODE_TOKEN_TYPE_TYPE;
                      break;
                   case CXCursor_MacroExpansion:
                      type = ELM_CODE_TOKEN_TYPE_PREPROCESSOR;//_MACRO_EXPANSION;
                      break;
                   default:
                      break;
                  }
                break;
             case CXToken_Keyword:
                switch (editor->cursors[i].kind)
                  {
                   case CXCursor_PreprocessingDirective:
                      type = ELM_CODE_TOKEN_TYPE_PREPROCESSOR;
                      break;
                   case CXCursor_CaseStmt:
                   case CXCursor_DefaultStmt:
                   case CXCursor_IfStmt:
                   case CXCursor_SwitchStmt:
                   case CXCursor_WhileStmt:
                   case CXCursor_DoStmt:
                   case CXCursor_ForStmt:
                   case CXCursor_GotoStmt:
                   case CXCursor_IndirectGotoStmt:
                   case CXCursor_ContinueStmt:
                   case CXCursor_BreakStmt:
                   case CXCursor_ReturnStmt:
                   case CXCursor_AsmStmt:
                   case CXCursor_ObjCAtTryStmt:
                   case CXCursor_ObjCAtCatchStmt:
                   case CXCursor_ObjCAtFinallyStmt:
                   case CXCursor_ObjCAtThrowStmt:
                   case CXCursor_ObjCAtSynchronizedStmt:
                   case CXCursor_ObjCAutoreleasePoolStmt:
                   case CXCursor_ObjCForCollectionStmt:
                   case CXCursor_CXXCatchStmt:
                   case CXCursor_CXXTryStmt:
                   case CXCursor_CXXForRangeStmt:
                   case CXCursor_SEHTryStmt:
                   case CXCursor_SEHExceptStmt:
                   case CXCursor_SEHFinallyStmt:
//                      type = ELM_CODE_TOKEN_TYPE_KEYWORD_STMT;
                      break;
                   default:
                      type = ELM_CODE_TOKEN_TYPE_KEYWORD;
                      break;
                  }
                break;
             case CXToken_Literal:
                if (editor->cursors[i].kind == CXCursor_StringLiteral || editor->cursors[i].kind == CXCursor_CharacterLiteral)
                  type = ELM_CODE_TOKEN_TYPE_STRING;
                else
                  type = ELM_CODE_TOKEN_TYPE_NUMBER;
                break;
             case CXToken_Comment:
                type = ELM_CODE_TOKEN_TYPE_COMMENT;
                break;
          }

        if (editor->highlight_cancel)
          break;
        _edi_range_color_set(editor, range, type);
     }
}

static void
_clang_free_highlighting(Edi_Editor *editor)
{
   free(editor->cursors);
   clang_disposeTokens(editor->tx_unit, editor->tokens, editor->token_count);
}

static void
_clang_load_errors(const char *path EINA_UNUSED, Edi_Editor *editor)
{
   Elm_Code *code;
   const char *filename;
   unsigned n = clang_getNumDiagnostics(editor->tx_unit);
   unsigned i = 0;

   ecore_thread_main_loop_begin();
   code = elm_code_widget_code_get(editor->entry);
   filename = elm_code_file_path_get(code->file);
   ecore_thread_main_loop_end();

   for(i = 0, n = clang_getNumDiagnostics(editor->tx_unit); i != n; ++i)
     {
        CXDiagnostic diag = clang_getDiagnostic(editor->tx_unit, i);
        CXFile file;
        unsigned int line;
        CXString path;

        // the parameter after line would be a caret position but we're just highlighting for now
        clang_getSpellingLocation(clang_getDiagnosticLocation(diag), &file, &line, NULL, NULL);

        path = clang_getFileName(file);
        if (!clang_getCString(path) || strcmp(filename, clang_getCString(path)))
          continue;

        /* FIXME: Also handle ranges and fix suggestions. */
        Elm_Code_Status_Type status = ELM_CODE_STATUS_TYPE_DEFAULT;

        switch (clang_getDiagnosticSeverity(diag))
          {
           case CXDiagnostic_Ignored:
              status = ELM_CODE_STATUS_TYPE_IGNORED;
              break;
           case CXDiagnostic_Note:
              status = ELM_CODE_STATUS_TYPE_NOTE;
              break;
           case CXDiagnostic_Warning:
              status = ELM_CODE_STATUS_TYPE_WARNING;
              break;
           case CXDiagnostic_Error:
              status = ELM_CODE_STATUS_TYPE_ERROR;
              break;
           case CXDiagnostic_Fatal:
              status = ELM_CODE_STATUS_TYPE_FATAL;
              break;
          }
        CXString str = clang_getDiagnosticSpelling(diag);
        if (status != ELM_CODE_STATUS_TYPE_DEFAULT)
          _edi_line_status_set(editor, line, status, clang_getCString(str));
        clang_disposeString(str);

        clang_disposeDiagnostic(diag);
        if (editor->highlight_cancel)
          break;
     }
}

static void
_edi_clang_setup(void *data, Ecore_Thread *thread EINA_UNUSED)
{
   Edi_Editor *editor;
   Elm_Code *code;
   const char *path, *args;
   char **clang_argv;
   unsigned int clang_argc;

   ecore_thread_main_loop_begin();

   editor = (Edi_Editor *)data;
   code = elm_code_widget_code_get(editor->entry);
   path = elm_code_file_path_get(code->file);

   ecore_thread_main_loop_end();

   /* Clang */
   /* FIXME: index should probably be global. */
   args = "-I/usr/inclue/ " EFL_CFLAGS " " CLANG_INCLUDES " -Wall -Wextra";
   clang_argv = eina_str_split_full(args, " ", 0, &clang_argc);

   editor->idx = clang_createIndex(0, 0);

   /* FIXME: Possibly activate more options? */
   editor->tx_unit = clang_parseTranslationUnit(editor->idx, path, (const char *const *)clang_argv, (int)clang_argc, NULL, 0,
     clang_defaultEditingTranslationUnitOptions() | CXTranslationUnit_DetailedPreprocessingRecord);

   _clang_load_errors(path, editor);
   _clang_load_highlighting(path, editor);
   _clang_show_highlighting(editor);
}

static void
_edi_clang_dispose(void *data, Ecore_Thread *thread EINA_UNUSED)
{
   Edi_Editor *editor = (Edi_Editor *)data;

   _clang_free_highlighting(editor);
   clang_disposeTranslationUnit(editor->tx_unit);
   clang_disposeIndex(editor->idx);

   editor->highlight_thread = NULL;
   editor->highlight_cancel = EINA_FALSE;
}
#endif

static void
_unfocused_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Edi_Editor *editor;

   editor = (Edi_Editor *)data;

   if (_edi_config->autosave)
     edi_editor_save(editor);
}

static void
_mouse_up_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED,
             Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Code_Widget *widget;
   Evas_Object *popup;
   Evas_Event_Mouse_Up *event;
   Eina_Bool ctrl;
   unsigned int row;
   int col;
   const char *word;

   widget = (Elm_Code_Widget *)data;
   event = (Evas_Event_Mouse_Up *)event_info;

   if (_clang_autocomplete_popup_bg)
     evas_object_del(_clang_autocomplete_popup_bg);

   ctrl = evas_key_modifier_is_set(event->modifiers, "Control");
   if (event->button != 3 || !ctrl)
     return;

   elm_code_widget_position_at_coordinates_get(widget, event->canvas.x, event->canvas.y, &row, &col);
   elm_code_widget_selection_select_word(widget, row, col);
   word = elm_code_widget_selection_text_get(widget);
   if (!word || !strlen(word))
     return;

   popup = elm_popup_add(widget);
   elm_popup_timeout_set(popup,1.5);

   elm_object_style_set(popup, "transparent");
   elm_object_part_text_set(popup, "title,text", word);
   elm_object_text_set(popup, "No help available for this term");

   evas_object_show(popup);
}

static void
_edi_editor_parse_line_cb(Elm_Code_Line *line EINA_UNUSED, void *data)
{
   Edi_Editor *editor = (Edi_Editor *)data;

   // We have caused a reset in the file parser, if it is active
   if (!editor->highlight_thread)
     return;

   editor->highlight_cancel = EINA_TRUE;
}

static void
_edi_editor_parse_file_cb(Elm_Code_File *file EINA_UNUSED, void *data)
{
   Edi_Editor *editor;

   editor = (Edi_Editor *)data;
   if (editor->highlight_thread)
     return;

#if HAVE_LIBCLANG
   editor->highlight_cancel = EINA_FALSE;
   editor->highlight_thread = ecore_thread_run(_edi_clang_setup, _edi_clang_dispose, NULL, editor);
#endif
}

static Eina_Bool
_edi_editor_config_changed(void *data, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   Elm_Code_Widget *widget;
   Elm_Code *code;

   widget = (Elm_Code_Widget *) data;
   code = elm_code_widget_code_get(widget);

   code->config.trim_whitespace = _edi_config->trim_whitespace;

   elm_obj_code_widget_font_set(widget, _edi_project_config->font.name, _edi_project_config->font.size);
   elm_obj_code_widget_show_whitespace_set(widget, _edi_project_config->gui.show_whitespace);
   elm_obj_code_widget_tab_inserts_spaces_set(widget, _edi_project_config->gui.tab_inserts_spaces);
   elm_obj_code_widget_line_width_marker_set(widget, _edi_project_config->gui.width_marker);
   elm_obj_code_widget_tabstop_set(widget, _edi_project_config->gui.tabstop);

   return ECORE_CALLBACK_RENEW;
}

static void
_editor_del_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *o EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Ecore_Event_Handler *ev_handler = data;

   ecore_event_handler_del(ev_handler);
}

Evas_Object *
edi_editor_add(Evas_Object *parent, Edi_Mainview_Item *item)
{
   Evas_Object *vbox, *box, *searchbar, *statusbar;
   Evas_Modifier_Mask ctrl, shift, alt;
   Ecore_Event_Handler *ev_handler;
   Evas *e;

   Elm_Code *code;
   Elm_Code_Widget *widget;
   Edi_Editor *editor;

   vbox = elm_box_add(parent);
   evas_object_size_hint_weight_set(vbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(vbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(vbox);

   searchbar = elm_box_add(vbox);
   evas_object_size_hint_weight_set(searchbar, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(searchbar, EVAS_HINT_FILL, 0.0);
   elm_box_pack_end(vbox, searchbar);

   box = elm_box_add(vbox);
   elm_box_horizontal_set(box, EINA_TRUE);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(vbox, box);
   evas_object_show(box);

   statusbar = elm_box_add(vbox);
   evas_object_size_hint_weight_set(statusbar, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(statusbar, EVAS_HINT_FILL, 0.0);
   elm_box_pack_end(vbox, statusbar);
   evas_object_show(statusbar);

   code = elm_code_create();
   widget = elm_code_widget_add(vbox, code);
   elm_code_widget_editable_set(widget, EINA_TRUE);
   elm_code_widget_line_numbers_set(widget, EINA_TRUE);
   _edi_editor_config_changed(widget, 0, NULL);

   editor = calloc(1, sizeof(*editor));
   editor->entry = widget;
   editor->show_highlight = !strcmp(item->editortype, "code");
   evas_object_event_callback_add(widget, EVAS_CALLBACK_KEY_DOWN,
                                  _smart_cb_key_down, editor);
   evas_object_smart_callback_add(widget, "changed,user", _changed_cb, editor);
   evas_object_event_callback_add(widget, EVAS_CALLBACK_MOUSE_UP, _mouse_up_cb, widget);
   evas_object_smart_callback_add(widget, "unfocused", _unfocused_cb, editor);

   elm_code_parser_standard_add(code, ELM_CODE_PARSER_STANDARD_TODO);
   if (editor->show_highlight)
     elm_code_parser_add(code, _edi_editor_parse_line_cb,
		               _edi_editor_parse_file_cb, editor);
   elm_code_file_open(code, item->path);

   evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(widget);
   elm_box_pack_end(box, widget);

   edi_editor_search_add(searchbar, editor);
   _edi_editor_statusbar_add(statusbar, editor, item);

   e = evas_object_evas_get(widget);
   ctrl = evas_key_modifier_mask_get(e, "Control");
   alt = evas_key_modifier_mask_get(e, "Alt");
   shift = evas_key_modifier_mask_get(e, "Shift");

   (void)!evas_object_key_grab(widget, "Prior", ctrl, shift | alt, 1);
   (void)!evas_object_key_grab(widget, "Next", ctrl, shift | alt, 1);
   (void)!evas_object_key_grab(widget, "s", ctrl, shift | alt, 1);
   (void)!evas_object_key_grab(widget, "f", ctrl, shift | alt, 1);

   evas_object_data_set(vbox, "editor", editor);
   ev_handler = ecore_event_handler_add(EDI_EVENT_CONFIG_CHANGED, _edi_editor_config_changed, widget);
   evas_object_event_callback_add(vbox, EVAS_CALLBACK_DEL, _editor_del_cb, ev_handler);

   return vbox;
}
