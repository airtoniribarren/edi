#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <libgen.h>
#include <regex.h>

#include <Eina.h>
#include <Elementary.h>
#include <Eio.h>
#include <elm_widget_fileselector.h>

#include "Edi.h"

#include "edi_filepanel.h"
#include "edi_content_provider.h"

#include "edi_private.h"

typedef struct _Edi_Dir_Data
{
   const char *path;
   Eio_Monitor *monitor;
   Eina_Bool isdir;
} Edi_Dir_Data;

static Elm_Genlist_Item_Class itc, itc2;
static Evas_Object *list;
static Eina_Hash *_list_items;
static edi_filepanel_item_clicked_cb _open_cb;

static Evas_Object *menu, *_main_win, *_filepanel_box, *_filter_box, *_filter;
static const char *_menu_cb_path, *_root_path;
static regex_t _filter_regex;
static Eina_Bool _filter_set = EINA_FALSE;
static Edi_Dir_Data *_root_dir;

static void
_file_listing_fill(Edi_Dir_Data *dir, Elm_Object_Item *parent_it);
static void
_file_listing_empty(Edi_Dir_Data *dir, Elm_Object_Item *parent_it);

static Eina_Bool
_file_path_hidden(const char *path, Eina_Bool filter)
{
   Edi_Build_Provider *provider;
   const char *relative;

   provider = edi_build_provider_for_project_get();
   if (provider && provider->file_hidden_is(path))
     return EINA_TRUE;

   if (ecore_file_file_get(path)[0] == '.')
     return EINA_TRUE;

   if (!filter || !_filter_set)
     return EINA_FALSE;

   relative = path + strlen(_root_path);
   return regexec(&_filter_regex, relative, 0, NULL, 0);
}

static void
_item_menu_open_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                   void *event_info EINA_UNUSED)
{
   if (ecore_file_is_dir(_menu_cb_path))
     return;

   _open_cb(_menu_cb_path, NULL, EINA_FALSE);
}

static void
_item_menu_open_window_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                   void *event_info EINA_UNUSED)
{
   if (ecore_file_is_dir(_menu_cb_path))
     return;

   _open_cb(_menu_cb_path, NULL, EINA_TRUE);
}

static void
_item_menu_xdgopen_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                      void *event_info EINA_UNUSED)
{
   char *cmd;
   int cmdlen;
   const char *format = "xdg-open \"%s\"";

   cmdlen = strlen(format) + strlen(_menu_cb_path) - 1;
   cmd = malloc(sizeof(char) * cmdlen);
   snprintf(cmd, cmdlen, format, _menu_cb_path);

   ecore_exe_run(cmd, NULL);
   free(cmd);
}

static void
_item_menu_open_as_text_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                           void *event_info EINA_UNUSED)
{
   _open_cb(_menu_cb_path, "text", EINA_FALSE);
}

static void
_item_menu_open_as_code_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                           void *event_info EINA_UNUSED)
{
   _open_cb(_menu_cb_path, "code", EINA_FALSE);
}

static void
_item_menu_open_as_image_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                            void *event_info EINA_UNUSED)
{
   _open_cb(_menu_cb_path, "image", EINA_FALSE);
}

static void
_item_menu_dismissed_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                        void *ev EINA_UNUSED)
{
   eina_stringshare_del(_menu_cb_path);
   _menu_cb_path = NULL;
}

static void
_item_menu_filetype_create(Evas_Object *menu, Elm_Object_Item *parent, const char *type,
                           Evas_Smart_Cb func)
{
   Edi_Content_Provider *provider;

   provider = edi_content_provider_for_id_get(type);
   if (!provider)
     return;

   elm_menu_item_add(menu, parent, provider->icon, provider->id, func, NULL);
}

static void
_item_menu_create(Evas_Object *win)
{
   Elm_Object_Item *menu_it;

   menu = elm_menu_add(win);
   evas_object_smart_callback_add(menu, "dismissed", _item_menu_dismissed_cb, NULL);

   elm_menu_item_add(menu, NULL, "fileopen", "open", _item_menu_open_cb, NULL);
   elm_menu_item_add(menu, NULL, "window-new", "open new window", _item_menu_open_window_cb, NULL);

   menu_it = elm_menu_item_add(menu, NULL, "gtk-execute", "open external",
                               _item_menu_xdgopen_cb, NULL);
   menu_it = elm_menu_item_add(menu, NULL, NULL, "open as", NULL, NULL);
   _item_menu_filetype_create(menu, menu_it, "text", _item_menu_open_as_text_cb);
   _item_menu_filetype_create(menu, menu_it, "code", _item_menu_open_as_code_cb);
   _item_menu_filetype_create(menu, menu_it, "image", _item_menu_open_as_image_cb);
}

static void
_item_clicked_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj,
                 void *event_info)
{
   Evas_Event_Mouse_Down *ev;
   Elm_Object_Item *it;
   Edi_Dir_Data *sd;

   ev = event_info;
   if (ev->button != 3) return;

   it = elm_genlist_at_xy_item_get(obj, ev->output.x, ev->output.y, NULL);
   sd = elm_object_item_data_get(it);

   if (sd->isdir)
     return;

   if (!menu)
     _item_menu_create(_main_win);

   _menu_cb_path = eina_stringshare_add(sd->path);
   elm_menu_move(menu, ev->canvas.x, ev->canvas.y);
   evas_object_show(menu);
}

static char *
_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *source EINA_UNUSED)
{
   Edi_Dir_Data *sd = data;

   return strdup(basename((char *)sd->path));
}

static Eina_Hash *mime_entries = NULL;

static Edi_Content_Provider*
_get_provider_from_hashset(const char *filename)
{
   if ( mime_entries == NULL ) mime_entries = eina_hash_string_superfast_new(NULL);
   const char *mime = eina_hash_find(mime_entries, filename);
   if ( !mime )
     {
       mime = efreet_mime_type_get(filename);

       if (mime)
         eina_hash_add(mime_entries, filename, strdup(mime));
     }

   return edi_content_provider_for_mime_get(mime);
}

static Evas_Object *
_content_get(void *data, Evas_Object *obj, const char *source)
{
   Edi_Content_Provider *provider;
   Edi_Dir_Data *sd = data;
   Evas_Object *ic;

   if (strcmp(source, "elm.swallow.icon"))
     return NULL;

   provider = _get_provider_from_hashset(sd->path);

   ic = elm_icon_add(obj);
   if (provider)
     elm_icon_standard_set(ic, provider->icon);
   else
     elm_icon_standard_set(ic, "empty");

   evas_object_size_hint_aspect_set(ic, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
   evas_object_show(ic);

   return ic;
}

static void
_item_del(void *data, Evas_Object *obj EINA_UNUSED)
{
   Edi_Dir_Data *sd = data;

   eina_stringshare_del(sd->path);
}

static void
_item_sel(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Edi_Dir_Data *sd = data;

   if (!ecore_file_is_dir(sd->path))
     _open_cb(sd->path, NULL, EINA_FALSE);
}

static Evas_Object *
_content_dir_get(void *data EINA_UNUSED, Evas_Object *obj, const char *source)
{
   Evas_Object *ic;

   if (strcmp(source, "elm.swallow.icon"))
     return NULL;

   ic = elm_icon_add(obj);
   elm_icon_standard_set(ic, "folder");
   evas_object_size_hint_aspect_set(ic, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
   evas_object_show(ic);
   return ic;
}

static Eina_Bool
_ls_filter_cb(void *data EINA_UNUSED, Eio_File *handler EINA_UNUSED,
              const Eina_File_Direct_Info *info)
{
   return info->path[info->name_start] != '.';
}

static int
_file_list_cmp(const void *data1, const void *data2)
{
   Edi_Dir_Data *sd1, *sd2;

   const Elm_Object_Item *item1 = data1;
   const Elm_Object_Item *item2 = data2;
   const Elm_Genlist_Item_Class *ca = elm_genlist_item_item_class_get(item1);
   const Elm_Genlist_Item_Class *cb = elm_genlist_item_item_class_get(item2);

   // move dirs to the top
   if (ca == &itc2)
     {
        if (cb != &itc2)
          return -1;
     }
   else if (cb == &itc2)
     {
        return 1;
     }

   sd1 = elm_object_item_data_get(item1);
   sd2 = elm_object_item_data_get(item2);

   return strcasecmp(sd1->path, sd2->path);
}

static void
_listing_request_cleanup(Listing_Request *lreq)
{
   eina_stringshare_del(lreq->path);
   free(lreq);
}

static void
_on_list_expand_req(void *data       EINA_UNUSED,
                    Evas_Object *obj EINA_UNUSED,
                    void *event_info)
{
   Elm_Object_Item *it = event_info;

   elm_genlist_item_expanded_set(it, EINA_TRUE);
}

static void
_on_list_contract_req(void *data       EINA_UNUSED,
                      Evas_Object *obj EINA_UNUSED,
                      void *event_info)
{
   Elm_Object_Item *it = event_info;

   elm_genlist_item_expanded_set(it, EINA_FALSE);
}

static void
_on_list_expanded(void *data EINA_UNUSED,
                  Evas_Object *obj EINA_UNUSED,
                  void *event_info)
{
   Elm_Object_Item *it = event_info;
   Edi_Dir_Data *sd = elm_object_item_data_get(it);

   _file_listing_fill(sd, it);
}

static void
_on_list_contracted(void *data EINA_UNUSED,
                    Evas_Object *obj EINA_UNUSED,
                    void *event_info)
{
   Elm_Object_Item *it = event_info;
   Edi_Dir_Data *sd = elm_object_item_data_get(it);

   _file_listing_empty(sd, it);
}

static Elm_Object_Item *_file_listing_item_find(const char *path, Elm_Object_Item *root)
{
   Elm_Object_Item *item;
   Edi_Dir_Data *sd;
   const Eina_List *itemlist, *l;

   return  eina_hash_find(_list_items, path);
}

static void
_file_listing_item_insert(const char *path, Eina_Bool isdir, Elm_Object_Item *parent_it)
{
   Elm_Genlist_Item_Class *clas = &itc;
   Edi_Dir_Data *sd;
   Elm_Object_Item *item;

   item = _file_listing_item_find(path, parent_it);
   if (item)
     return;

   sd = calloc(1, sizeof(Edi_Dir_Data));
   if (isdir)
     {
        clas = &itc2;
        sd->isdir = EINA_TRUE;
     }

   if (_file_path_hidden(path, !isdir))
     return;
   sd->path = eina_stringshare_add(path);

   item = elm_genlist_item_sorted_insert(list, clas, sd, parent_it,
                                         isdir ? ELM_GENLIST_ITEM_TREE : ELM_GENLIST_ITEM_NONE,
                                         _file_list_cmp, _item_sel, sd);
   eina_hash_add(_list_items, sd->path, item);
}

static void
_file_listing_item_delete(const char *path, Elm_Object_Item *parent_it)
{
   Elm_Object_Item *item;

   item = _file_listing_item_find(path, parent_it);
   if (!item)
     return;

   eina_hash_del(_list_items, path, NULL);
   elm_object_item_del(item);
}

static void
_ls_main_cb(void *data, Eio_File *handler, const Eina_File_Direct_Info *info)
{
   Listing_Request *lreq = data;

   if (eio_file_check(handler)) return;

   //if (!lreq->sd->files_view || lreq->sd->current != handler)
     //{
        //eio_file_cancel(handler);
        //return;
     //}

//   _signal_first(lreq);

   _file_listing_item_insert(info->path, info->type == EINA_FILE_DIR, lreq->parent_it);
}

static void
_ls_done_cb(void *data, Eio_File *handler EINA_UNUSED)
{
   Listing_Request *lreq = data;

   _listing_request_cleanup(lreq);
}

static void
_ls_error_cb(void *data, Eio_File *handler EINA_UNUSED, int error EINA_UNUSED)
{
   Listing_Request *lreq = data;

   _listing_request_cleanup(lreq);
}

static void
_file_listing_empty(Edi_Dir_Data *dir, Elm_Object_Item *parent_it)
{
   const Eina_List *list, *l;
   Elm_Object_Item *subit;
   Edi_Dir_Data *subdir;

   if (dir->monitor) eio_monitor_del(dir->monitor);

   list = elm_genlist_item_subitems_get(parent_it);
   EINA_LIST_FOREACH(list, l, subit)
     {
        subdir = elm_object_item_data_get(subit);
        eina_hash_del(_list_items, subdir->path, NULL);
     }
   elm_genlist_item_subitems_clear(parent_it);
}

static void
_file_listing_fill(Edi_Dir_Data *dir, Elm_Object_Item *parent_it)
{
   Listing_Request *lreq;

   if (!dir) return;

   lreq = malloc(sizeof (Listing_Request));
   if (!lreq) return;

   lreq->parent_it = parent_it;
   lreq->path = eina_stringshare_add(dir->path);
   lreq->first = EINA_TRUE;

   dir->monitor = eio_monitor_add(dir->path);
   eio_file_stat_ls(dir->path, _ls_filter_cb, _ls_main_cb,
                               _ls_done_cb, _ls_error_cb, lreq);
}

static void
_file_listing_updated(void *data EINA_UNUSED, int type EINA_UNUSED,
                      void *event EINA_UNUSED)
{
   const char *dir;
   Eio_Monitor_Event *ev = event;
   Elm_Object_Item *parent_it;

   dir = ecore_file_dir_get(ev->filename);
   if (strncmp(edi_project_get(), dir, strlen(edi_project_get())))
     return;

   parent_it = _file_listing_item_find(dir, NULL);

   if (type == EIO_MONITOR_FILE_CREATED)
     _file_listing_item_insert(ev->filename, EINA_FALSE, parent_it);
   else if (type == EIO_MONITOR_FILE_DELETED)
     _file_listing_item_delete(ev->filename, parent_it);
   if (type == EIO_MONITOR_DIRECTORY_CREATED)
     _file_listing_item_insert(ev->filename, EINA_TRUE, parent_it);
   else if (type == EIO_MONITOR_DIRECTORY_DELETED)
     _file_listing_item_delete(ev->filename, parent_it);
   else
     DBG("Ignoring file update event for %s", ev->filename);
}

/* Panel filtering */

static Eina_Bool
_filter_get(void *data, Evas_Object *obj EINA_UNUSED, void *key EINA_UNUSED)
{
   Edi_Dir_Data *sd = data;

   return !_file_path_hidden(sd->path, EINA_TRUE);
}

static void
_filter_clear(Evas_Object *filter)
{
   elm_object_text_set(filter, NULL);
   _filter_set = EINA_FALSE;
   regfree(&_filter_regex);
}

static void
_filter_clear_bt_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _filter_clear((Evas_Object *)data);
}

static void
_filter_cancel_bt_cb(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _filter_clear((Evas_Object *)data);

   evas_object_hide(_filter_box);
   elm_box_unpack(_filepanel_box, _filter_box);
}

static void
_filter_key_down_cb(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Evas_Object *tree;
   const char *match;

   tree = (Evas_Object *)data;
   match = elm_object_text_get(obj);

   regfree(&_filter_regex);
   _filter_set = !regcomp(&_filter_regex, match, REG_NOSUB | REG_ICASE);

   if (!match || strlen(match) == 0 || !_filter_set)
     elm_genlist_filter_set(tree, "");
   else
     elm_genlist_filter_set(tree, (void *)strdup(match));
}

void
edi_filepanel_search()
{
   elm_box_pack_start(_filepanel_box, _filter_box);
   evas_object_show(_filter_box);
   elm_object_focus_set(_filter, EINA_TRUE);
}

/* Panel setup */

void
edi_filepanel_add(Evas_Object *parent, Evas_Object *win,
                  const char *path, edi_filepanel_item_clicked_cb cb)
{
   Evas_Object *box, *hbox, *filter, *clear, *cancel, *icon;

   box = elm_box_add(parent);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_homogeneous_set(box, EINA_FALSE);
   evas_object_show(box);
   elm_box_pack_end(parent, box);
   _filepanel_box = box;

   hbox = elm_box_add(box);
   evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, 0.0);
   evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(hbox, EINA_TRUE);
   elm_box_homogeneous_set(hbox, EINA_FALSE);
   _filter_box = hbox;

   filter = elm_entry_add(hbox);
   elm_entry_scrollable_set(filter, EINA_TRUE);
   elm_entry_single_line_set(filter, EINA_TRUE);
   elm_object_part_text_set(filter, "guide", "Find file");
   evas_object_size_hint_weight_set(filter, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(filter, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_entry_editable_set(filter, EINA_TRUE);
   evas_object_show(filter);
   elm_box_pack_end(hbox, filter);
   _filter = filter;

   clear = elm_button_add(hbox);
   evas_object_smart_callback_add(clear, "clicked", _filter_clear_bt_cb, filter);
   evas_object_show(clear);
   elm_box_pack_end(hbox, clear);

   icon = elm_icon_add(clear);
   evas_object_size_hint_min_set(icon, 14 * elm_config_scale_get(), 14 * elm_config_scale_get());
   elm_icon_standard_set(icon, "edit-clear");
   elm_object_part_content_set(clear, "icon", icon);

   cancel = elm_button_add(hbox);
   evas_object_smart_callback_add(cancel, "clicked", _filter_cancel_bt_cb, filter);
   evas_object_show(cancel);
   elm_box_pack_end(hbox, cancel);

   icon = elm_icon_add(cancel);
   evas_object_size_hint_min_set(icon, 14 * elm_config_scale_get(), 14 * elm_config_scale_get());
   elm_icon_standard_set(icon, "window-close");
   elm_object_part_content_set(cancel, "icon", icon);

   list = elm_genlist_add(parent);
   elm_genlist_homogeneous_set(list, EINA_TRUE);
   elm_genlist_select_mode_set(list, ELM_OBJECT_SELECT_MODE_ALWAYS);
   elm_genlist_filter_set(list, "");
   evas_object_size_hint_min_set(list, 100, -1);
   evas_object_size_hint_weight_set(list, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(list, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(list);
   elm_box_pack_end(box, list);

   _root_path = eina_stringshare_add(path);
   evas_object_event_callback_add(list, EVAS_CALLBACK_MOUSE_DOWN,
                                  _item_clicked_cb, NULL);
   ecore_event_handler_add(EIO_MONITOR_FILE_CREATED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);
   ecore_event_handler_add(EIO_MONITOR_FILE_MODIFIED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);
   ecore_event_handler_add(EIO_MONITOR_FILE_DELETED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);
   ecore_event_handler_add(EIO_MONITOR_DIRECTORY_CREATED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);
   ecore_event_handler_add(EIO_MONITOR_DIRECTORY_MODIFIED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);
   ecore_event_handler_add(EIO_MONITOR_DIRECTORY_DELETED, (Ecore_Event_Handler_Cb)_file_listing_updated, _root_path);

   evas_object_smart_callback_add(list, "expand,request", _on_list_expand_req, parent);
   evas_object_smart_callback_add(list, "contract,request", _on_list_contract_req, parent);
   evas_object_smart_callback_add(list, "expanded", _on_list_expanded, parent);
   evas_object_smart_callback_add(list, "contracted", _on_list_contracted, parent);

   itc.item_style = "default";
   itc.func.text_get = _text_get;
   itc.func.content_get = _content_get;
   itc.func.filter_get = _filter_get;
   itc.func.del = _item_del;

   itc2.item_style = "default";
   itc2.func.text_get = _text_get;
   itc2.func.content_get = _content_dir_get;
//   itc2.func.state_get = _state_get;
   itc2.func.del = _item_del;

   _open_cb = cb;
   _main_win = win;

   _list_items = eina_hash_string_superfast_new(NULL);
   _root_dir = calloc(1, sizeof(Edi_Dir_Data));
   _root_dir->path = path;
   _file_listing_fill(_root_dir, NULL);
   evas_object_smart_callback_add(filter, "changed", _filter_key_down_cb, list);
}

const char *
edi_filepanel_selected_path_get(Evas_Object *obj EINA_UNUSED)
{
   Elm_Object_Item *it;

   it = elm_genlist_selected_item_get(list);
   return elm_object_item_data_get(it);
}

