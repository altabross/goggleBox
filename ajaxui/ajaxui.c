/*
 *  tvheadend, AJAX / HTML user interface
 *  Copyright (C) 2008 Andreas �man
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "tvhead.h"
#include "http.h"
#include "ajaxui.h"
#include "ajaxui_mailbox.h"
#include "dispatch.h"

#include "obj/ajaxui.cssh"

#include "obj/prototype.jsh"
#include "obj/builder.jsh"
#include "obj/controls.jsh"
#include "obj/dragdrop.jsh"
#include "obj/effects.jsh"
#include "obj/scriptaculous.jsh"
#include "obj/slider.jsh"

#include "obj/tvheadend.jsh"

#include "obj/sbbody_l.gifh"
#include "obj/sbbody_r.gifh"
#include "obj/sbhead_l.gifh"
#include "obj/sbhead_r.gifh"

#include "obj/mapped.pngh"
#include "obj/unmapped.pngh"


extern const char *htsversion;

const char *ajax_tabnames[] = {
  [AJAX_TAB_CHANNELS]      = "Channels",
  [AJAX_TAB_RECORDER]      = "Recorder",
  [AJAX_TAB_CONFIGURATION] = "Configuration",
  [AJAX_TAB_ABOUT]         = "About",
};


const char *
ajaxui_escape_apostrophe(const char *content)
{
  static char buf[2000];
  int i = 0;

  while(i < sizeof(buf) - 2 && *content) {
    if(*content == '\'')
      buf[i++] = '\\';
    buf[i++] = *content++;
  }
  buf[i] = 0;
  return buf;
}


/**
 *
 */
void
ajax_generate_select_functions(htsbuf_queue_t *hq, const char *fprefix,
			       char **selvector)
{
  int n;

  htsbuf_qprintf(hq, "<script type=\"text/javascript\">\r\n"
	      "//<![CDATA[\r\n");
  
  /* Select all */
  htsbuf_qprintf(hq, "%s_sel_all = function() {\r\n", fprefix);
  for(n = 0; selvector[n] != NULL; n++)
    htsbuf_qprintf(hq, "$('sel_%s').checked = true;\r\n", selvector[n]);
  htsbuf_qprintf(hq, "}\r\n");

  /* Select none */
  htsbuf_qprintf(hq, "%s_sel_none = function() {\r\n", fprefix);
  for(n = 0; selvector[n] != NULL; n++)
    htsbuf_qprintf(hq, "$('sel_%s').checked = false;\r\n", selvector[n]);
  htsbuf_qprintf(hq, "}\r\n");

  /* Invert selection */
  htsbuf_qprintf(hq, "%s_sel_invert = function() {\r\n", fprefix);
  for(n = 0; selvector[n] != NULL; n++)
    htsbuf_qprintf(hq, "$('sel_%s').checked = !$('sel_%s').checked;\r\n",
		selvector[n], selvector[n]);
  htsbuf_qprintf(hq, "}\r\n");

  /* Invoke AJAX call containing all selected elements */
  htsbuf_qprintf(hq, 
	      "%s_sel_do = function(op, arg1, arg2, check) {\r\n"
	      "if(check == true && !confirm(\"Are you sure?\")) {return;}\r\n"
	      "var h = new Hash();\r\n"
	      "h.set('arg1', arg1);\r\n"
	      "h.set('arg2', arg2);\r\n", fprefix
	      );
  
  for(n = 0; selvector[n] != NULL; n++)
    htsbuf_qprintf(hq, 
		"if($('sel_%s').checked) {h.set('%s', 'selected') }\r\n",
		selvector[n], selvector[n]);
  htsbuf_qprintf(hq, " new Ajax.Request('/ajax/' + op, "
	      "{parameters: h});\r\n");
  htsbuf_qprintf(hq, "}\r\n");
  htsbuf_qprintf(hq, 
	      "\r\n//]]>\r\n"
	      "</script>\r\n");
}


/**
 * AJAX table
 */
void
ajax_table_top(ajax_table_t *t, http_connection_t *hc, htsbuf_queue_t *hq,
	       const char *names[], int weights[])
{
  int n = 0, i, tw = 0;
  while(names[n]) {
    tw += weights[n];
    n++;
  }
  assert(n <= 20);

  t->columns = n;

  memset(t, 0, sizeof(ajax_table_t));

  t->hq = hq;

  for(i = 0; i < n; i++)
    t->csize[i] = 100 * weights[i] / tw;

  htsbuf_qprintf(hq, "<div style=\"padding-right: 20px\">");

  htsbuf_qprintf(hq, "<div style=\"overflow: auto; width: 100%%\">");
  
  for(i = 0; i < n; i++)
    htsbuf_qprintf(hq, "<div style=\"float: left; width: %d%%\">%s</div>",
		t->csize[i], *names[i] ? names[i]: "&nbsp;");
  htsbuf_qprintf(hq, "</div></div><hr><div class=\"normaltable\">\r\n");
}

/**
 * AJAX table new row
 */
void
ajax_table_row_start(ajax_table_t *t, const char *id)
{
  t->rowid = id;
  t->rowcol = !t->rowcol;
  htsbuf_qprintf(t->hq, "%s<div style=\"%soverflow: auto; width: 100%\">",
	      t->inrow ? "</div>\r\n" : "",
	      t->rowcol ? "background: #fff; " : "");
  t->inrow = 1;
  t->curcol = 0;
}

/**
 * AJAX table new row
 */
void
ajax_table_subrow_start(ajax_table_t *t)
{
  htsbuf_qprintf(t->hq, "<div style=\"overflow: auto; width: 100%\">");
  t->curcol = 0;
}


/**
 * AJAX table new row
 */
void
ajax_table_subrow_end(ajax_table_t *t)
{
  htsbuf_qprintf(t->hq, "</div>");
  t->curcol = 0;
}


/**
 * AJAX table new row
 */
void
ajax_table_details_start(ajax_table_t *t)
{
  assert(t->inrow == 1);
  t->inrow = 0;
  /* Extra info */
  htsbuf_qprintf(t->hq, "</div><div id=\"details_%s\" style=\"%sdisplay: none\">",
	      t->rowid, t->rowcol ? "background: #fff; " : "");
}

/**
 * AJAX table new row
 */
void
ajax_table_details_end(ajax_table_t *t)
{
  /* Extra info */
  htsbuf_qprintf(t->hq, "</div>");
}


/**
 * AJAX table cell
 */
void
ajax_table_cell(ajax_table_t *t, const char *id, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  if(t->rowid && id) {
    htsbuf_qprintf(t->hq, "<div id=\"%s_%s\"", id, t->rowid);
  } else {
    htsbuf_qprintf(t->hq, "<div");
  }
  htsbuf_qprintf(t->hq,
	      " style=\"float: left; width: %d%%\">", t->csize[t->curcol]);
  t->curcol++;
  if(t->curcol == 20)
    abort();

  if(fmt == NULL)
    htsbuf_qprintf(t->hq, "&nbsp;");
  else
    htsbuf_vqprintf(t->hq, fmt, ap);

  va_end(ap);
  htsbuf_qprintf(t->hq, "</div>");
}

/**
 * AJAX table cell for toggling display of more details
 */
void
ajax_table_cell_detail_toggler(ajax_table_t *t)
{
  ajax_table_cell(t, NULL,
		  "<a id=\"toggle_details_%s\" href=\"javascript:void(0)\" "
		  "onClick=\"showhide('details_%s')\" >"
		  "More</a>",
		  t->rowid, t->rowid);
}

/**
 * AJAX table cell for selecting row
 */
void
ajax_table_cell_checkbox(ajax_table_t *t)
{
  ajax_table_cell(t, NULL,
		  "<input id=\"sel_%s\" type=\"checkbox\" class=\"nicebox\">",
		  t->rowid);
}

/**
 * AJAX table footer
 */
void
ajax_table_bottom(ajax_table_t *t)
{
  htsbuf_qprintf(t->hq, "%s</div>", t->inrow ? "</div>" : "");
}

/**
 * AJAX box start
 */
void
ajax_box_begin(htsbuf_queue_t *hq, ajax_box_t type,
	       const char *id, const char *style, const char *title)
{
  char id0[100], style0[100];
  
  if(id != NULL)
    snprintf(id0, sizeof(id0), "id=\"%s\" ", id);
  else
    id0[0] = 0;

  if(style != NULL)
    snprintf(style0, sizeof(style0), "style=\"%s\" ", style);
  else
    style0[0] = 0;


  switch(type) {
  case AJAX_BOX_SIDEBOX:
    htsbuf_qprintf(hq,
		"<div class=\"sidebox\">"
		"<div class=\"boxhead\"><h2>%s</h2></div>\r\n"
		"  <div class=\"boxbody\" %s%s>",
		title, id0, style0);
    break;

  case AJAX_BOX_FILLED:
    htsbuf_qprintf(hq, 
		"<div style=\"margin: 3px\">"
		"<b class=\"filledbox\">"
		"<b class=\"filledbox1\"><b></b></b>"
		"<b class=\"filledbox2\"><b></b></b>"
		"<b class=\"filledbox3\"></b>"
		"<b class=\"filledbox4\"></b>"
		"<b class=\"filledbox5\"></b></b>"
		"<div class=\"filledboxfg\" %s%s>\r\n",
		id0, style0);
    break;

  case AJAX_BOX_BORDER:
   htsbuf_qprintf(hq, 
		"<div style=\"margin: 3px\">"
		"<b class=\"borderbox\">"
		"<b class=\"borderbox1\"><b></b></b>"
		"<b class=\"borderbox2\"><b></b></b>"
		"<b class=\"borderbox3\"></b></b>"
		"<div class=\"borderboxfg\" %s%s>\r\n",
		id0, style0);

    break;
  }
}

/**
 * AJAX box end
 */
void
ajax_box_end(htsbuf_queue_t *hq, ajax_box_t type)
{
  switch(type) {
  case AJAX_BOX_SIDEBOX:
    htsbuf_qprintf(hq,"</div></div>");
    break;
    
  case AJAX_BOX_FILLED:
    htsbuf_qprintf(hq,
		"</div>"
		"<b class=\"filledbox\">"
		"<b class=\"filledbox5\"></b>"
		"<b class=\"filledbox4\"></b>"
		"<b class=\"filledbox3\"></b>"
		"<b class=\"filledbox2\"><b></b></b>"
		"<b class=\"filledbox1\"><b></b></b></b>"
		"</div>\r\n");
    break;

 case AJAX_BOX_BORDER:
    htsbuf_qprintf(hq,
		"</div>"
		"<b class=\"borderbox\">"
		"<b class=\"borderbox3\"></b>"
		"<b class=\"borderbox2\"><b></b></b>"
		"<b class=\"borderbox1\"><b></b></b></b>"
		"</div>\r\n");
    break;
 
  }
}

/**
 *
 */
void
ajax_js(htsbuf_queue_t *hq, const char *fmt, ...)
{
  va_list ap;

  htsbuf_qprintf(hq, 
	      "<script type=\"text/javascript\">\r\n"
	      "//<![CDATA[\r\n");

  va_start(ap, fmt);
  htsbuf_vqprintf(hq, fmt, ap);
  va_end(ap);

  htsbuf_qprintf(hq, 
	      "\r\n//]]>\r\n"
	      "</script>\r\n");
}



/**
 * Based on the given char[] array, generate a menu bar
 */
void
ajax_menu_bar_from_array(htsbuf_queue_t *hq, const char *name, 
			 const char **vec, int num, int cur)
{
  int i;
 
  htsbuf_qprintf(hq, "<ul class=\"menubar\">");

  for(i = 0; i < num; i++) {
    htsbuf_qprintf(hq,
		"<li%s>"
		"<a href=\"javascript:switchtab('%s', '%d')\">%s</a>"
		"</li>",
		cur == i ? " style=\"font-weight:bold;\"" : "", name,
		i, vec[i]);
  }
  htsbuf_qprintf(hq, "</ul>");
}


/**
 *
 */
void
ajax_a_jsfuncf(htsbuf_queue_t *hq, const char *innerhtml, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  htsbuf_qprintf(hq, "<a href=\"javascript:void(0)\" onClick=\"javascript:");
  htsbuf_vqprintf(hq, fmt, ap);
  htsbuf_qprintf(hq, "\">%s</a>", innerhtml);
}

/**
 *
 */
void
ajax_button(htsbuf_queue_t *hq, const char *caption, const char *code, ...)
{
  va_list ap;
  va_start(ap, code);

  htsbuf_qprintf(hq, "<input type=\"button\" value=\"%s\" onClick=\"",
	      caption);
  htsbuf_vqprintf(hq, code, ap);
  htsbuf_qprintf(hq, "\">");
}



/*
 * Titlebar AJAX page
 */
static int
ajax_page_titlebar(http_connection_t *hc, http_reply_t *hr, 
		   const char *remain, void *opaque)
{
  if(remain == NULL)
    return HTTP_STATUS_NOT_FOUND;

  ajax_menu_bar_from_array(&hr->hr_q, "top", 
			   ajax_tabnames, AJAX_TABS, atoi(remain));
  http_output_html(hc, hr);
  return 0;
}



/**
 * About
 */
static int
ajax_about_tab(http_connection_t *hc, http_reply_t *hr)
{
  htsbuf_queue_t *hq = &hr->hr_q;
  
  htsbuf_qprintf(hq, "<center>");
  htsbuf_qprintf(hq, "<div style=\"padding: auto; width: 400px\">");

  ajax_box_begin(hq, AJAX_BOX_SIDEBOX, NULL, NULL, "About");

  htsbuf_qprintf(hq, "<div style=\"text-align: center\">");

  htsbuf_qprintf(hq, 
	      "<p>HTS / Tvheadend</p>"
	      "<p>(c) 2006-2008 Andreas \303\226man</p>"
	      "<p>Latest release and information is available at:</p>"
	      "<p><a href=\"http://www.lonelycoder.com/hts/\">"
	      "http://www.lonelycoder.com/hts/</a></p>"
	      "<p>&nbsp;</p>"
	      "<p>This webinterface is powered by</p>"
	      "<p><a href=\"http://www.prototypejs.org/\">Prototype</a>"
	      " and "
	      "<a href=\"http://script.aculo.us/\">script.aculo.us</a>"
	      "</p>"
	      "<p>&nbsp;</p>"
	      "<p>Media formats and codecs by</p>"
	      "<p><a href=\"http://www.ffmpeg.org/\">FFmpeg</a></p>"
	      );

  htsbuf_qprintf(hq, "</div>");
  ajax_box_end(hq, AJAX_BOX_SIDEBOX);
  htsbuf_qprintf(hq, "</div>");
  htsbuf_qprintf(hq, "</center>");

  http_output_html(hc, hr);
  return 0;
}



/*
 * Tab AJAX page
 *
 * Find the 'tab' id and continue with tab specific code
 */
static int
ajax_page_tab(http_connection_t *hc, http_reply_t *hr, 
	      const char *remain, void *opaque)
{
  int tab;

  if(remain == NULL)
    return HTTP_STATUS_NOT_FOUND;

  tab = atoi(remain);

  switch(tab) {
  case AJAX_TAB_CHANNELS:
    return ajax_channelgroup_tab(hc, hr);

  case AJAX_TAB_CONFIGURATION:
    return ajax_config_tab(hc, hr);

  case AJAX_TAB_ABOUT:
    return ajax_about_tab(hc, hr);

  default:
    return HTTP_STATUS_NOT_FOUND;
  }
  return 0;
}



/*
 * Root page
 */
static int
ajax_page_root(http_connection_t *hc, http_reply_t *hr, 
	       const char *remain, void *opaque)
{
  htsbuf_queue_t *hq = &hr->hr_q;

  htsbuf_qprintf(hq, 
	      "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\"\r\n"
	      "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">"
	      /*
	      "<!DOCTYPE html "
	      "PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\r\n"
	      "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
	      */
	      "\r\n"
	      "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
	      "xml:lang=\"en\" lang=\"en\">"
	      "<head>"
	      "<title>HTS/Tvheadend</title>"
	      "<meta http-equiv=\"Content-Type\" "
	      "content=\"text/html; charset=utf-8\">\r\n"
	      "<link href=\"/ajax/ajaxui.css\" rel=\"stylesheet\" "
	      "type=\"text/css\">\r\n"
	      "<script src=\"/ajax/prototype.js\" type=\"text/javascript\">"
	      "</script>\r\n"
	      "<script src=\"/ajax/effects.js\" type=\"text/javascript\">"
	      "</script>\r\n"
	      "<script src=\"/ajax/dragdrop.js\" type=\"text/javascript\">"
	      "</script>\r\n"
	      "<script src=\"/ajax/controls.js\" type=\"text/javascript\">"
	      "</script>\r\n"
	      "<script src=\"/ajax/tvheadend.js\" type=\"text/javascript\">"
	      "</script>\r\n"
	      "</head>"
	      "<body>");


  htsbuf_qprintf(hq, "<div style=\"overflow: auto; width: 100%\">");

  htsbuf_qprintf(hq, "<div style=\"float: left; width: 100%\">");

  ajax_box_begin(hq, AJAX_BOX_FILLED, NULL, NULL, NULL);

  htsbuf_qprintf(hq,
	      "<div style=\"width: 100%%; overflow: hidden\">"
	      "<div style=\"float: left; width: 30%%\">"
	      "Tvheadend (%s)"
	      "</div>"
	      "<div style=\"float: left; width: 40%%\" id=\"topmenu\"></div>"
	      "<div style=\"float: left; width: 30%%; text-align: right\">"
	      "&nbsp;"
	      "</div>"
	      "</div>",
	      htsversion);

  ajax_mailbox_start(hq);

  ajax_box_end(hq, AJAX_BOX_FILLED);

  htsbuf_qprintf(hq, "<div id=\"topdeck\"></div>");
  
  ajax_js(hq, "switchtab('top', '0')");
#if 0
  htsbuf_qprintf(hq, "</div><div style=\"float: left; width: 20%\">");

  ajax_box_begin(hq, AJAX_BOX_SIDEBOX, "statusbox", NULL, "System status");
  ajax_box_end(hq, AJAX_BOX_SIDEBOX);
#endif
  htsbuf_qprintf(hq, "</div></div></body></html>");

  http_output_html(hc, hr);
  return 0;
}


/**
 * AJAX user interface
 */
void
ajaxui_start(void)
{
  http_path_add("/ajax/index.html",           NULL, ajax_page_root,
		ACCESS_WEB_INTERFACE);

  http_path_add("/ajax/topmenu",              NULL, ajax_page_titlebar,
		ACCESS_WEB_INTERFACE);

  http_path_add("/ajax/toptab",               NULL, ajax_page_tab,
		ACCESS_WEB_INTERFACE);

  /* Stylesheet */
  http_resource_add("/ajax/ajaxui.css", embedded_ajaxui,
		    sizeof(embedded_ajaxui), "text/css", "gzip");

#define ADD_JS_RESOURCE(path, name) \
  http_resource_add(path, name, sizeof(name), "text/javascript", "gzip")

  /* Prototype */
  ADD_JS_RESOURCE("/ajax/prototype.js",          embedded_prototype);

  /* Scriptaculous */
  ADD_JS_RESOURCE("/ajax/builder.js",            embedded_builder);
  ADD_JS_RESOURCE("/ajax/controls.js",           embedded_controls);
  ADD_JS_RESOURCE("/ajax/dragdrop.js",           embedded_dragdrop);
  ADD_JS_RESOURCE("/ajax/effects.js",            embedded_effects);
  ADD_JS_RESOURCE("/ajax/scriptaculous.js",      embedded_scriptaculous);
  ADD_JS_RESOURCE("/ajax/slider.js",             embedded_slider);

  /* Tvheadend */
  ADD_JS_RESOURCE("/ajax/tvheadend.js",          embedded_tvheadend);

  /* Embedded images */
  http_resource_add("/sidebox/sbbody-l.gif", embedded_sbbody_l,
		    sizeof(embedded_sbbody_l), "image/gif", NULL);
  http_resource_add("/sidebox/sbbody-r.gif", embedded_sbbody_r,
		    sizeof(embedded_sbbody_r), "image/gif", NULL);
  http_resource_add("/sidebox/sbhead-l.gif", embedded_sbhead_l,
		    sizeof(embedded_sbhead_l), "image/gif", NULL);
  http_resource_add("/sidebox/sbhead-r.gif", embedded_sbhead_r,
		    sizeof(embedded_sbhead_r), "image/gif", NULL);

  http_resource_add("/gfx/unmapped.png", embedded_unmapped,
		    sizeof(embedded_unmapped), "image/png", NULL);

  http_resource_add("/gfx/mapped.png", embedded_mapped,
		    sizeof(embedded_mapped), "image/png", NULL);

  ajax_mailbox_init();
  ajax_channels_init();
  ajax_config_init();
  ajax_config_transport_init();
}
