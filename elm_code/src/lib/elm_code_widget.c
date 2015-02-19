#ifdef HAVE_CONFIG
# include "config.h"
#endif

#include <Elm_Code.h>
#include "elm_code_private.h"

typedef enum {
   ELM_CODE_WIDGET_COLOR_GUTTER_BG = ELM_CODE_TOKEN_TYPE_COUNT,
   ELM_CODE_WIDGET_COLOR_GUTTER_FG,
   ELM_CODE_WIDGET_COLOR_CURSOR,

   ELM_CODE_WIDGET_COLOR_COUNT
} Elm_Code_Widget_Colors;

Eina_Unicode status_icons[] = {
 ' ',
 '!',

 '+',
 '-',
 ' ',

 0x2713,
 0x2717,

 0
};


EOLIAN static void
_elm_code_widget_eo_base_constructor(Eo *obj, Elm_Code_Widget_Data *pd EINA_UNUSED)
{
   eo_do_super(obj, ELM_CODE_WIDGET_CLASS, eo_constructor());

   pd->cursor_line = 1;
   pd->cursor_col = 1;
}

EOLIAN static void
_elm_code_widget_class_constructor(Eo_Class *klass EINA_UNUSED)
{

}

static void
_elm_code_widget_scroll_by(Elm_Code_Widget *widget, int by_x, int by_y)
{
   Elm_Code_Widget_Data *pd;
   Evas_Coord x, y, w, h;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   elm_scroller_region_get(pd->scroller, &x, &y, &w, &h);
   x += by_x;
   y += by_y;
   elm_scroller_region_show(pd->scroller, x, y, w, h);
}

static Eina_Bool
_elm_code_widget_resize(Elm_Code_Widget *widget)
{
   Elm_Code_Line *line;
   Eina_List *item;
   Evas_Coord ww, wh, old_width, old_height;
   int w, h, cw, ch, gutter;
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   gutter = elm_code_widget_text_left_gutter_width_get(widget);

   if (!pd->code)
     return EINA_FALSE;

   evas_object_geometry_get(widget, NULL, NULL, &ww, &wh);
   evas_object_textgrid_cell_size_get(pd->grid, &cw, &ch);
   old_width = ww;
   old_height = wh;

   w = 0;
   h = elm_code_file_lines_get(pd->code->file);
   EINA_LIST_FOREACH(pd->code->file->lines, item, line)
     if (line->length + gutter + 1 > w)
        w = line->length + gutter + 1;

   if (w*cw > ww)
     ww = w*cw;
   if (h*ch > wh)
     wh = h*ch;

   evas_object_textgrid_size_set(pd->grid, ww/cw+1, wh/ch+1);
   evas_object_size_hint_min_set(pd->grid, w*cw, h*ch);

   if (pd->gravity_x == 1.0 || pd->gravity_y == 1.0)
     _elm_code_widget_scroll_by(widget, 
        (pd->gravity_x == 1.0 && ww > old_width) ? ww - old_width : 0,
        (pd->gravity_y == 1.0 && wh > old_height) ? wh - old_height : 0);

   return h > 0 && w > 0;
}

static void
_elm_code_widget_fill_line_token(Evas_Textgrid_Cell *cells, int count, int start, int end, Elm_Code_Token_Type type)
{
   int x;

   for (x = start; x <= end && x < count; x++)
     {
        cells[x].fg = type;
     }
}

static void
_elm_code_widget_fill_line_tokens(Elm_Code_Widget *widget, Evas_Textgrid_Cell *cells,
                                  unsigned int count, Elm_Code_Line *line)
{
   Eina_List *item;
   Elm_Code_Token *token;
   int start, length, offset;

   offset = elm_code_widget_text_left_gutter_width_get(widget) - 1;
   start = offset + 1;
   length = line->length + offset;

   EINA_LIST_FOREACH(line->tokens, item, token)
     {

        _elm_code_widget_fill_line_token(cells, count, start, token->start + offset, ELM_CODE_TOKEN_TYPE_DEFAULT);

        // TODO handle a token starting before the previous finishes
        _elm_code_widget_fill_line_token(cells, count, token->start + offset, token->end + offset, token->type);

        start = token->end + offset + 1;
     }

   _elm_code_widget_fill_line_token(cells, count, start, length, ELM_CODE_TOKEN_TYPE_DEFAULT);
}

static void
_elm_code_widget_fill_line(Elm_Code_Widget *widget, Evas_Textgrid_Cell *cells, Elm_Code_Line *line)
{
   char *chr, *number;
   unsigned int length, x;
   int w, gutter, g;
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   gutter = elm_code_widget_text_left_gutter_width_get(widget);

   if (!_elm_code_widget_resize(widget))
     return;

   length = line->length;
   evas_object_textgrid_size_get(pd->grid, &w, NULL);

   cells[gutter-1].codepoint = status_icons[line->status];
   cells[gutter-1].bold = 1;
   cells[gutter-1].fg = ELM_CODE_WIDGET_COLOR_GUTTER_FG;
   cells[gutter-1].bg = (line->status == ELM_CODE_STATUS_TYPE_DEFAULT) ? ELM_CODE_WIDGET_COLOR_GUTTER_BG : line->status;

   if (pd->show_line_numbers)
     {
        number = malloc(sizeof(char) * gutter);
        snprintf(number, gutter, "%*d", gutter - 1, line->number);

        for (g = 0; g < gutter - 1; g++)
          {
             cells[g].codepoint = *(number + g);
             cells[g].fg = ELM_CODE_WIDGET_COLOR_GUTTER_FG;
             cells[g].bg = ELM_CODE_WIDGET_COLOR_GUTTER_BG;
          }
        free(number);
     }

   if (line->modified)
      chr = line->modified;
   else
      chr = (char *)line->content;
   for (x = gutter; x < (unsigned int) w && x < length + gutter; x++)
     {
        cells[x].codepoint = *chr;
        cells[x].bg = line->status;

        chr++;
     }
   for (; x < (unsigned int) w; x++)
     {
        cells[x].codepoint = 0;
        cells[x].bg = line->status;
     }

   _elm_code_widget_fill_line_tokens(widget, cells, w, line);
   if (pd->editable && pd->focussed && pd->cursor_line == line->number)
     {
        if (pd->cursor_col + gutter - 1 < (unsigned int) w)
          cells[pd->cursor_col + gutter - 1].bg = ELM_CODE_WIDGET_COLOR_CURSOR;
     }

   evas_object_textgrid_update_add(pd->grid, 0, line->number - 1, w, 1);
}

static void
_elm_code_widget_fill(Elm_Code_Widget *widget)
{
   Elm_Code_Line *line;
   Evas_Textgrid_Cell *cells;
   int w, h;
   unsigned int y;
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (!_elm_code_widget_resize(widget))
     return;
   evas_object_textgrid_size_get(pd->grid, &w, &h);

   for (y = 1; y <= (unsigned int) h && y <= elm_code_file_lines_get(pd->code->file); y++)
     {
        line = elm_code_file_line_get(pd->code->file, y);

        cells = evas_object_textgrid_cellrow_get(pd->grid, y - 1);
        _elm_code_widget_fill_line(widget, cells, line);
     }
}

static Eina_Bool
_elm_code_widget_line_cb(void *data, Eo *obj EINA_UNUSED,
                         const Eo_Event_Description *desc EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;

   widget = (Elm_Code_Widget *)data;

   if (!_elm_code_widget_resize(widget))
     return EINA_TRUE;

   // FIXME refresh just the row unless we have resized (by being the result of a row append)
   _elm_code_widget_fill(widget);

   return EO_CALLBACK_CONTINUE;
}


static Eina_Bool
_elm_code_widget_file_cb(void *data, Eo *obj EINA_UNUSED, const Eo_Event_Description *desc EINA_UNUSED,
                         void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;

   widget = (Elm_Code_Widget *)data;

   _elm_code_widget_fill(widget);
   return EO_CALLBACK_CONTINUE;
}

static void
_elm_code_widget_resize_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                           void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;

   widget = (Elm_Code_Widget *)data;

   _elm_code_widget_fill(widget);
}

static Eina_Bool
_elm_code_widget_cursor_key_will_move(Elm_Code_Widget *widget, const char *key)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   line = elm_code_file_line_get(pd->code->file, pd->cursor_line);

   if (!line)
     return EINA_FALSE;

   if (!strcmp(key, "Up"))
     return pd->cursor_line > 1;
   else if (!strcmp(key, "Down"))
     return pd->cursor_line < elm_code_file_lines_get(pd->code->file);
   else if (!strcmp(key, "Left"))
     return pd->cursor_col > 1;
   else if (!strcmp(key, "Right"))
     return pd->cursor_col < (unsigned int) line->length + 1;
   else
     return EINA_FALSE;
}

static void
_elm_code_widget_update_focus_directions(Elm_Code_Widget *obj)
{
   if (_elm_code_widget_cursor_key_will_move(obj, "Up"))
     elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_UP);
   else
     elm_widget_focus_next_object_set(obj, NULL, ELM_FOCUS_UP);

   if (_elm_code_widget_cursor_key_will_move(obj, "Down"))
     elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_DOWN);
   else
     elm_widget_focus_next_object_set(obj, NULL, ELM_FOCUS_DOWN);

   if (_elm_code_widget_cursor_key_will_move(obj, "Left"))
     elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_LEFT);
   else
     elm_widget_focus_next_object_set(obj, NULL, ELM_FOCUS_LEFT);

   if (_elm_code_widget_cursor_key_will_move(obj, "Right"))
     elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_RIGHT);
   else
     elm_widget_focus_next_object_set(obj, NULL, ELM_FOCUS_RIGHT);

   elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_PREVIOUS);
   elm_widget_focus_next_object_set(obj, obj, ELM_FOCUS_NEXT);
}

static void
_elm_code_widget_clicked_editable_cb(Elm_Code_Widget *widget, Evas_Coord x, Evas_Coord y)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;
   int cw, ch;
   unsigned int row, col;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   evas_object_textgrid_cell_size_get(pd->grid, &cw, &ch);
   col = ((double) x / cw) - elm_code_widget_text_left_gutter_width_get(widget) + 1;
   row = ((double) y / ch) + 1;

   line = elm_code_file_line_get(pd->code->file, row);
   if (line)
     {
        pd->cursor_line = row;

        if (col <= (unsigned int) line->length + 1)
          pd->cursor_col = col;
        else
          pd->cursor_col = line->length + 1;
     }
   if (pd->cursor_col == 0)
     pd->cursor_col = 1;

   _elm_code_widget_update_focus_directions(widget);
   _elm_code_widget_fill(widget);
}

static void
_elm_code_widget_clicked_readonly_cb(Elm_Code_Widget *widget, Evas_Coord y)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;
   int ch;
   unsigned int row;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   evas_object_textgrid_cell_size_get(pd->grid, NULL, &ch);
   row = ((double) y / ch) + 1;

   line = elm_code_file_line_get(pd->code->file, row);
   if (line)
     eo_do(widget, eo_event_callback_call(ELM_CODE_WIDGET_EVENT_LINE_CLICKED, line));
}

static void
_elm_code_widget_clicked_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED,
                            void *event_info)
{
   Elm_Code_Widget *widget;
   Elm_Code_Widget_Data *pd;
   Evas_Event_Mouse_Up *event;
   Evas_Coord x, y, ox, oy, sx, sy;

   widget = (Elm_Code_Widget *)data;
   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   event = (Evas_Event_Mouse_Up *)event_info;

   evas_object_geometry_get(widget, &ox, &oy, NULL, NULL);
   elm_scroller_region_get(pd->scroller, &sx, &sy, NULL, NULL);
   x = event->canvas.x + sx - ox;
   y = event->canvas.y + sy - oy;

   if (pd->editable)
     _elm_code_widget_clicked_editable_cb(widget, x, y);
   else
     _elm_code_widget_clicked_readonly_cb(widget, y);
}

static void
_elm_code_widget_cursor_move_up(Elm_Code_Widget *widget)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (pd->cursor_line > 1)
     pd->cursor_line--;

   line = elm_code_file_line_get(pd->code->file, pd->cursor_line);
   if (pd->cursor_col > (unsigned int) line->length + 1)
     pd->cursor_col = line->length + 1;

   _elm_code_widget_fill(widget);
}

static void
_elm_code_widget_cursor_move_down(Elm_Code_Widget *widget)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (pd->cursor_line < elm_code_file_lines_get(pd->code->file))
     pd->cursor_line++;

   line = elm_code_file_line_get(pd->code->file, pd->cursor_line);
   if (pd->cursor_col > (unsigned int) line->length + 1)
     pd->cursor_col = line->length + 1;

   _elm_code_widget_fill(widget);
}

static void
_elm_code_widget_cursor_move_left(Elm_Code_Widget *widget)
{
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (pd->cursor_col > 1)
     pd->cursor_col--;

   _elm_code_widget_fill(widget);
}

static void
_elm_code_widget_cursor_move_right(Elm_Code_Widget *widget)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *line;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   line = elm_code_file_line_get(pd->code->file, pd->cursor_line);
   if (pd->cursor_col <= (unsigned int) line->length)
     pd->cursor_col++;

   _elm_code_widget_fill(widget);
}

static void
_elm_code_widget_key_down_cb(void *data, Evas *evas EINA_UNUSED,
                              Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Code_Widget *widget;
   Elm_Code_Widget_Data *pd;

   widget = (Elm_Code_Widget *)data;
   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   Evas_Event_Key_Down *ev = event_info;

   if (!pd->editable)
     return;

   _elm_code_widget_update_focus_directions(widget);

   if (!strcmp(ev->key, "Up"))
     _elm_code_widget_cursor_move_up(widget);
   else if (!strcmp(ev->key, "Down"))
     _elm_code_widget_cursor_move_down(widget);
   else if (!strcmp(ev->key, "Left"))
     _elm_code_widget_cursor_move_left(widget);
   else if (!strcmp(ev->key, "Right"))
     _elm_code_widget_cursor_move_right(widget);
   else
     INF("Unhandled key %s", ev->key);
}

static void
_elm_code_widget_focused_event_cb(void *data, Evas_Object *obj,
                                  void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;
   Elm_Code_Widget_Data *pd;

   widget = (Elm_Code_Widget *)data;
   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   pd->focussed = EINA_TRUE;

   _elm_code_widget_update_focus_directions(widget);
   _elm_code_widget_fill(obj);
}

static void
_elm_code_widget_unfocused_event_cb(void *data, Evas_Object *obj,
                                    void *event_info EINA_UNUSED)
{
   Elm_Code_Widget *widget;
   Elm_Code_Widget_Data *pd;

   widget = (Elm_Code_Widget *)data;
   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   pd->focussed = EINA_FALSE;
   _elm_code_widget_fill(obj);
}

EOLIAN static Eina_Bool
_elm_code_widget_elm_widget_focus_next_manager_is(Eo *obj EINA_UNUSED,
                                                  Elm_Code_Widget_Data *pd EINA_UNUSED)
{
   return EINA_FALSE;
}

EOLIAN static Eina_Bool
_elm_code_widget_elm_widget_focus_direction_manager_is(Eo *obj EINA_UNUSED,
                                                       Elm_Code_Widget_Data *pd EINA_UNUSED)
{
   return EINA_TRUE;
}

EOLIAN static void
_elm_code_widget_font_size_set(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd, Evas_Font_Size font_size)
{
   evas_object_textgrid_font_set(pd->grid, "Mono", font_size * elm_config_scale_get());
   pd->font_size = font_size;
}

EOLIAN static Evas_Font_Size
_elm_code_widget_font_size_get(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd)
{
   return pd->font_size;
}

EOLIAN static void
_elm_code_widget_code_set(Eo *obj, Elm_Code_Widget_Data *pd EINA_UNUSED, Elm_Code *code)
{
   pd->code = code;

   code->widgets = eina_list_append(code->widgets, obj);
}

EOLIAN static Elm_Code *
_elm_code_widget_code_get(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd)
{
   return pd->code;
}

EOLIAN static void
_elm_code_widget_gravity_set(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd, double x, double y)
{
   pd->gravity_x = x;
   pd->gravity_y = y;
}

EOLIAN static void
_elm_code_widget_gravity_get(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd, double *x, double *y)
{
   *x = pd->gravity_x;
   *y = pd->gravity_y;
}

EOLIAN static void
_elm_code_widget_editable_set(Eo *obj, Elm_Code_Widget_Data *pd, Eina_Bool editable)
{
   pd->editable = editable;
   elm_object_focus_allow_set(obj, editable);
}

EOLIAN static Eina_Bool
_elm_code_widget_editable_get(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd)
{
   return pd->editable;
}

EOLIAN static void
_elm_code_widget_line_numbers_set(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd, Eina_Bool line_numbers)
{
   pd->show_line_numbers = line_numbers;
}

EOLIAN static Eina_Bool
_elm_code_widget_line_numbers_get(Eo *obj EINA_UNUSED, Elm_Code_Widget_Data *pd)
{
   return pd->show_line_numbers;
}

static void
_elm_code_widget_setup_palette(Evas_Object *o)
{
   // setup status colors
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_DEFAULT,
                                    36, 36, 36, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_ERROR,
                                    205, 54, 54, 255);

   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_ADDED,
                                    36, 96, 36, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_REMOVED,
                                    96, 36, 36, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_CHANGED,
                                    36, 36, 96, 255);

   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_PASSED,
                                    54, 96, 54, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_STATUS_TYPE_FAILED,
                                    96, 54, 54, 255);

   // setup token colors
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_TOKEN_TYPE_DEFAULT,
                                    205, 205, 205, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_TOKEN_TYPE_COMMENT,
                                    54, 205, 255, 255);

   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_TOKEN_TYPE_ADDED,
                                    54, 255, 54, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_TOKEN_TYPE_REMOVED,
                                    255, 54, 54, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_TOKEN_TYPE_CHANGED,
                                    54, 54, 255, 255);

   // other styles that the widget uses
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_WIDGET_COLOR_CURSOR,
                                    205, 205, 54, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_WIDGET_COLOR_GUTTER_BG,
                                    75, 75, 75, 255);
   evas_object_textgrid_palette_set(o, EVAS_TEXTGRID_PALETTE_STANDARD, ELM_CODE_WIDGET_COLOR_GUTTER_FG,
                                    139, 139, 139, 255);
}

EOLIAN static void
_elm_code_widget_evas_object_smart_add(Eo *obj, Elm_Code_Widget_Data *pd)
{
   Evas_Object *grid, *scroller;

   eo_do_super(obj, ELM_CODE_WIDGET_CLASS, evas_obj_smart_add());
   elm_object_focus_allow_set(obj, EINA_TRUE);

   elm_layout_file_set(obj, PACKAGE_DATA_DIR "/themes/elm_code.edj", "elm_code/layout/default");

   scroller = elm_scroller_add(obj);
   evas_object_size_hint_weight_set(scroller, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(scroller, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(scroller);
   elm_layout_content_set(obj, "elm.swallow.content", scroller);
   elm_object_focus_allow_set(scroller, EINA_FALSE);
   pd->scroller = scroller;

   grid = evas_object_textgrid_add(obj); 
   evas_object_size_hint_weight_set(grid, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(grid, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(grid);
   elm_object_content_set(scroller, grid);
   pd->grid = grid;
   _elm_code_widget_setup_palette(grid);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_RESIZE, _elm_code_widget_resize_cb, obj);
   evas_object_event_callback_add(grid, EVAS_CALLBACK_MOUSE_UP, _elm_code_widget_clicked_cb, obj);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_KEY_DOWN, _elm_code_widget_key_down_cb, obj);

   evas_object_smart_callback_add(obj, "focused", _elm_code_widget_focused_event_cb, obj);
   evas_object_smart_callback_add(obj, "unfocused", _elm_code_widget_unfocused_event_cb, obj);

   eo_do(obj,
         eo_event_callback_add(&ELM_CODE_EVENT_LINE_STYLE_SET, _elm_code_widget_line_cb, obj);
         eo_event_callback_add(&ELM_CODE_EVENT_FILE_LOAD_DONE, _elm_code_widget_file_cb, obj));

   _elm_code_widget_font_size_set(obj, pd, 10);
}

#include "elm_code_widget.eo.c"
