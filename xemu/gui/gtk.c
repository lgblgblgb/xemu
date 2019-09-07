/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016,2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/* ---------------------------------------- LINUX/UNIX STUFFS based on GTK ---------------------------------------- */


int xemunativegui_init ( void )
{
	DEBUGPRINT("NATIVEGUI: GTK3 GUI is being used." NL);
	if (!gtk_init_check(NULL, NULL)) {
		ERROR_WINDOW("Cannot initialize GTK");
		return 1;
	}
	int n = xemunativegui_iteration();
	DEBUGPRINT("NATIVEGUI: GTK3 initialized, %d iterations." NL, n);	// consume possible peding (if any?) GTK stuffs after initialization - maybe not needed at all?
	is_xemunativegui_ok = 1;
	return 0;
}


int xemunativegui_iteration ( void )
{
	if (!is_xemunativegui_ok)
		return 0;
	int n = 0;
	while (gtk_events_pending()) {
		gtk_main_iteration();
		n++;
	}
	return n;
}


int xemunativegui_file_selector ( int dialog_mode, const char *dialog_title, char *default_dir, char *selected, int path_max_size )
{
	if (!is_xemunativegui_ok)
		return  1;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	GtkWidget *dialog = gtk_file_chooser_dialog_new(dialog_title,
		NULL, // parent window!
		action,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		"_Open",
		GTK_RESPONSE_ACCEPT,
		NULL
	);
	if (default_dir)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), default_dir);
	*selected = '\0';
	gint res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		if (strlen(filename) < path_max_size) {
			strcpy(selected, filename);
			store_dir_from_file_selection(default_dir, filename, dialog_mode);
		} else
			res = GTK_RESPONSE_CANCEL;
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
	xemunativegui_iteration();
	xemu_drop_events();
	return res != GTK_RESPONSE_ACCEPT;
}
