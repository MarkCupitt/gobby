/* gobby - A GTKmm driven libobby client
 * Copyright (C) 2005 0x539 dev group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <obby/client_document.hpp>
#include <obby/host_document.hpp>
#include <obby/client_buffer.hpp>
#include <obby/host_buffer.hpp>
#include "document.hpp"
#include "folder.hpp"

#include <cassert>

Gobby::Document::Document(obby::document& doc, const Folder& folder)
#ifdef WITH_GTKSOURCEVIEW
 : Gtk::SourceView(),
#else
 : Gtk::TextView(),
#endif
   m_doc(doc), m_folder(folder), m_editing(true)
{
#ifdef WITH_GTKSOURCEVIEW
	set_show_line_numbers(true);
	Glib::RefPtr<Gtk::SourceBuffer> buf = get_buffer();
#else
	Glib::RefPtr<Gtk::TextBuffer> buf = get_buffer();
#endif

	// Catch key press events
	add_events(Gdk::KEY_PRESS_MASK);

	// Set monospaced font
	Pango::FontDescription desc;
	desc.set_family("monospace");
	modify_font(desc);


#ifdef WITH_GTKSOURCEVIEW
	// Set SourceLanguage by file extension
	Glib::ustring mime_type =
		folder.get_mime_map().get_mime_type_by_file(doc.get_title() );
	if(!mime_type.empty() )
	{
		Glib::RefPtr<Gtk::SourceLanguagesManager> manager =
			folder.get_lang_manager();
		Glib::RefPtr<Gtk::SourceLanguage> language = 
			manager->get_language_from_mime_type(mime_type);

		if(language)
			buf->set_language(language);
	}

	buf->set_highlight(true);
#endif

	// Insert users from user table to insert users that have already
	// left the obby session.
	const obby::buffer& obbybuf = doc.get_buffer();
	const obby::user_table& user_table = doc.get_buffer().get_user_table();
	for(obby::user_table::user_iterator iter = user_table.user_begin();
	    iter != user_table.user_end();
	    ++ iter)
	{
		// Create new tag
		Glib::RefPtr<Gtk::TextBuffer::Tag> tag =
			buf->create_tag("gobby_user_" + iter->get_name() );

		// Build user color
		Gdk::Color color;
		color.set_red(iter->get_red() * 65535 / 255);
		color.set_green(iter->get_green() * 65535 / 255);
		color.set_blue(iter->get_blue() * 65535 / 255);

		// Assign color to tag
		tag->property_background_gdk() = color;
	}

	// Textbuffer signal handlers
	buf->signal_insert().connect(
		sigc::mem_fun(*this, &Document::on_insert_before), false);
	buf->signal_erase().connect(
		sigc::mem_fun(*this, &Document::on_erase_before), false);
	buf->signal_insert().connect(
		sigc::mem_fun(*this, &Document::on_insert_after), true);
	buf->signal_erase().connect(
		sigc::mem_fun(*this, &Document::on_erase_after), true);
	buf->signal_mark_set().connect(
		sigc::mem_fun(*this, &Document::on_mark_set), false);
	buf->signal_apply_tag().connect(
		sigc::mem_fun(*this, &Document::on_apply_tag_after), true);

	// Obby signal handlers
	doc.insert_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_insert) );
	doc.delete_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_delete) );
	doc.change_event().before().connect(
		sigc::mem_fun(*this, &Document::on_obby_change_before) );
	doc.change_event().after().connect(
		sigc::mem_fun(*this, &Document::on_obby_change_after) );

	// Set initial text
	buf->set_text(doc.get_whole_buffer() );

	// Set initial authors
	for(unsigned int i = 0; i < doc.get_line_count(); ++ i)
	{
		// Get current line
		const obby::line& line = doc.get_line(i);
		obby::line::author_iterator prev = line.author_begin();
		obby::line::author_iterator cur = prev;

		// Iterate through it's authors list
		for(++ cur; prev != line.author_end(); ++ cur)
		{
			// Get current user
			const obby::user_table::user* user = prev->author;
			// user should never be NULL...
			if(user == NULL) continue;

			// Get the range to highlight
			obby::line::size_type endpos;
			if(cur != line.author_end() )
				endpos = cur->position;
			else
				endpos = line.length();

			Gtk::TextBuffer::iterator begin =
				buf->get_iter_at_line_index(i, prev->position);
			Gtk::TextBuffer::iterator end =
				buf->get_iter_at_line_index(i, endpos);

			// Apply corresponding tag
			buf->apply_tag_by_name(
				"gobby_user_" + user->get_name(),
				begin,
				end
			);

			prev = cur;
		}
	}

	m_editing = false;
}

Gobby::Document::~Document()
{
}

const obby::document& Gobby::Document::get_document() const
{
	return m_doc;
}

obby::document& Gobby::Document::get_document()
{
	return m_doc;
}

Gobby::Document::signal_cursor_moved_type
Gobby::Document::cursor_moved_event() const
{
	return m_signal_cursor_moved;
}

Gobby::Document::signal_content_changed_type
Gobby::Document::content_changed_event() const
{
	return m_signal_content_changed;
}

#ifdef WITH_GTKSOURCEVIEW
Gobby::Document::signal_language_changed_type
Gobby::Document::language_changed_event() const
{
	return m_signal_language_changed;
}
#endif

void Gobby::Document::get_cursor_position(unsigned int& row,
                                          unsigned int& col)
{
	// Get insert mark
	Glib::RefPtr<Gtk::TextBuffer::Mark> mark =
		get_buffer()->get_insert();

	// Get corresponding iterator
	// Gtk::TextBuffer::Mark::get_iter is not const. Why not? It prevents
	// this function from being const.
	const Gtk::TextBuffer::iterator iter = mark->get_iter();

	// Read line and column from iterator
	row = iter.get_line();
	col = iter.get_line_offset();
}

unsigned int Gobby::Document::get_unsynced_changes_count() const
{
	obby::client_document* doc = 
		dynamic_cast<obby::client_document*>(&m_doc);

	// Changes in Server/Host documents are always synced
	if(doc == NULL)
		return 0;

	// Return amount reported by document otherwise
	return doc->unsynced_count();
}

unsigned int Gobby::Document::get_revision() const
{
	// Get revision from obby document
	return m_doc.get_revision();
}

#ifdef WITH_GTKSOURCEVIEW
Glib::RefPtr<Gtk::SourceLanguage> Gobby::Document::get_language() const
{
	return get_buffer()->get_language();
}

void Gobby::Document::set_language(
	const Glib::RefPtr<Gtk::SourceLanguage>& language
)
{
	get_buffer()->set_language(language);
	m_signal_language_changed.emit();
}
#endif

Glib::ustring Gobby::Document::get_content()
{
	return get_buffer()->get_text();
}

#ifdef WITH_GTKSOURCEVIEW
bool Gobby::Document::get_show_line_numbers() const
{
	return Gtk::SourceView::get_show_line_numbers();
}

void Gobby::Document::set_show_line_numbers(bool show)
{
	Gtk::SourceView::set_show_line_numbers(show);
}
#endif

void Gobby::Document::obby_user_join(obby::user& user)
{
	// Build tag name for this user
	Glib::ustring tag_name = "gobby_user_" + user.get_name();

	// Find already existing tag
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::TagTable> tag_table =
		buffer->get_tag_table();
	Glib::RefPtr<Gtk::TextBuffer::Tag> tag = tag_table->lookup(tag_name);

	// Create new tag, if it doesn't exist
	if(!tag)
		tag = buffer->create_tag(tag_name);

	// Build color
	Gdk::Color color;
	color.set_red(user.get_red() * 65535 / 255);
	color.set_green(user.get_green() * 65535 / 255);
	color.set_blue(user.get_blue() * 65535 / 255);

	// Set/Update color
	tag->property_background_gdk() = color;
}

void Gobby::Document::obby_user_part(obby::user& user)
{
}

void Gobby::Document::on_obby_insert(const obby::insert_record& record)
{
	if(m_editing) return;
	m_editing = true;

	// Get textbuffer
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();

	// Translate position to row/column
	unsigned int row, col;
	m_doc.position_to_coord(record.get_position(), row, col);

	// Find obby::user that inserted the text
	obby::user* user = m_doc.get_buffer().find_user(record.get_from() );
	assert(user != NULL);

	// Insert text
	Gtk::TextBuffer::iterator end = buffer->insert(
		buffer->get_iter_at_line_index(row, col),
		record.get_text()
	);

	// Colourize new text
	Gtk::TextBuffer::iterator begin = end;
	begin.backward_chars(record.get_text().length() );
	update_user_colour(begin, end, *user);

	m_editing = false;
}

void Gobby::Document::on_obby_delete(const obby::delete_record& record)
{
	if(m_editing) return;
	m_editing = true;

	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();

	unsigned int brow, bcol, erow, ecol;
	m_doc.position_to_coord(record.get_begin(), brow, bcol);
	m_doc.position_to_coord(record.get_end(), erow, ecol);

	buffer->erase(
		buffer->get_iter_at_line_index(brow, bcol),
		buffer->get_iter_at_line_index(erow, ecol)
	);
	m_editing = false;
}

void Gobby::Document::on_obby_change_before()
{
}

void Gobby::Document::on_obby_change_after()
{
	// Document changed
	m_signal_content_changed.emit();
}

void Gobby::Document::on_insert_before(const Gtk::TextBuffer::iterator& begin,
                                       const Glib::ustring& text,
                                       int bytes)
{
	if(m_editing) return;
	m_editing = true;

	m_doc.insert(
		m_doc.coord_to_position(
			begin.get_line(),
			begin.get_line_index()
		),
		text
	);

	m_editing = false;
}

void Gobby::Document::on_erase_before(const Gtk::TextBuffer::iterator& begin,
                                      const Gtk::TextBuffer::iterator& end)
{
	if(m_editing) return;
	m_editing = true;

	m_doc.erase(
		m_doc.coord_to_position(
			begin.get_line(),
			begin.get_line_index()
		),
		m_doc.coord_to_position(
			end.get_line(),
			end.get_line_index()
		)
	);

	m_editing = false;
}

void Gobby::Document::on_insert_after(const Gtk::TextBuffer::iterator& end,
                                      const Glib::ustring& text,
                                      int bytes)
{
	// Other editing function is at work.
	if(!m_editing)
	{
		// Find the user that has written this text
		const obby::user& user =
			dynamic_cast<const obby::local_buffer&>(
				m_doc.get_buffer()
			).get_self();

		// Find start position of new text
		Gtk::TextBuffer::iterator pos = end;
		pos.backward_chars(text.length() );

		// Update user colour. Set m_editing to true because this
		// colour update came from an editing operation. See
		// on_tag_apply below for more information on why this is
		// necessary.
		m_editing = true;
		update_user_colour(pos, end, user);
		m_editing = false;

		// Content has changed
		m_signal_content_changed.emit();

		// Cursor position has changed
		m_signal_cursor_moved.emit();
	}
}

void Gobby::Document::on_erase_after(const Gtk::TextBuffer::iterator& begin,
                                     const Gtk::TextBuffer::iterator& end)
{
	if(!m_editing)
	{
		// Cursor position may have changed
		m_signal_cursor_moved.emit();

		// Content has changed
		m_signal_content_changed.emit();
	}
}

void Gobby::Document::on_apply_tag_after(const Glib::RefPtr<Gtk::TextTag>& tag,
                                         const Gtk::TextBuffer::iterator& begin,
                                         const Gtk::TextBuffer::iterator& end)
{
	Glib::ustring tag_name = tag->property_name();
	if(!m_editing && tag_name.compare(0, 10, "gobby_user") == 0)
	{
		// Not editing, but user tag is inserted. Not good. May result
		// from a copy+paste operation where tags where copied. Refresh
		// given range.
		unsigned int num_line = begin.get_line();
		unsigned int num_col = begin.get_line_index();

		// Find author of the text
		const obby::line& line = m_doc.get_line(num_line);
		obby::line::author_iterator iter = line.author_begin();
		for(iter; iter != line.author_end(); ++ iter)
			if(iter->position > num_col)
				break;
		--iter;

		// Refresh. iter->author->get_user() may result in a NULL
		// pointer if the user who wrote the original text has left
		// the obby session. This will be automatically fixed as soon
		// as #8 is fixed.
		if(iter->author->get_user() != NULL)
		{
			m_editing = true;
			update_user_colour(
				begin, end, *iter->author->get_user()
			);
			m_editing = false;
		}
	}
}

void Gobby::Document::on_mark_set(
	const Gtk::TextBuffer::iterator& location,
	const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark
)
{
	// Insert mark changed position: Cursor position change
	if(mark == get_buffer()->get_insert() )//->get_name() == "insert")
		m_signal_cursor_moved.emit();
}

void Gobby::Document::update_user_colour(const Gtk::TextBuffer::iterator& begin,
                                         const Gtk::TextBuffer::iterator& end,
                                         const obby::user& user)
{
	// Remove other user tags in that range
	Glib::RefPtr<Gtk::TextBuffer> buffer = get_buffer();
	Glib::RefPtr<Gtk::TextBuffer::TagTable> tag_table =
		buffer->get_tag_table();

	tag_table->foreach(
		sigc::bind(
			sigc::mem_fun(
				*this,
				&Document::on_remove_user_colour
			),
			sigc::ref(begin),
			sigc::ref(end)
		)
	);

	// Insert new user tag to the given range
	Glib::RefPtr<Gtk::TextTag> tag =
		tag_table->lookup("gobby_user_" + user.get_name() );
	buffer->apply_tag(tag, begin, end);
}

void
Gobby::Document::on_remove_user_colour(Glib::RefPtr<Gtk::TextBuffer::Tag> tag,
                                       const Gtk::TextBuffer::iterator& begin,
				       const Gtk::TextBuffer::iterator& end)
{
	// Remove tag if it is a user color tag.
	Glib::ustring tag_name = tag->property_name();
	if(tag_name.compare(0, 10, "gobby_user") == 0)
		get_buffer()->remove_tag(tag, begin, end);
}

