/*
 * Copyright (C) 2008-2014 David Robillard <d@drobilla.net>
 * Copyright (C) 2009-2011 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2009-2013 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2014-2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <iostream>
#include "evoral/midi_events.h"
#include "ardour/midi_track.h"

#include "gtkmm2ext/keyboard.h"

#include "editing.h"
#include "piano_roll_header.h"
#include "midi_time_axis.h"
#include "midi_streamview.h"
#include "public_editor.h"
#include "ui_config.h"

using namespace std;
using namespace Gtkmm2ext;

PianoRollHeader::Color PianoRollHeader::white = PianoRollHeader::Color(0.77f, 0.78f, 0.76f);
PianoRollHeader::Color PianoRollHeader::white_highlight = PianoRollHeader::Color(1.00f, 0.40f, 0.40f);

PianoRollHeader::Color PianoRollHeader::black = PianoRollHeader::Color(0.14f, 0.14f, 0.14f);
PianoRollHeader::Color PianoRollHeader::black_highlight = PianoRollHeader::Color(0.60f, 0.10f, 0.10f);

PianoRollHeader::Color::Color()
	: r(1.0f)
	, g(1.0f)
	, b(1.0f)
{
}

PianoRollHeader::Color::Color(double _r, double _g, double _b)
	: r(_r)
	, g(_g)
	, b(_b)
{
}

inline void
PianoRollHeader::Color::set(const PianoRollHeader::Color& c)
{
	r = c.r;
	g = c.g;
	b = c.b;
}

PianoRollHeader::PianoRollHeader(MidiStreamView& v)
	: _view(v)
	, _highlighted_note(NO_MIDI_NOTE)
	, _clicked_note(NO_MIDI_NOTE)
	, _dragging(false)
	, _adj(v.note_range_adjustment)
{
	_adj.set_lower(0);
	_adj.set_upper(127);

	/* set minimum view range to one octave */
	//set_min_page_size(12);

	//_adj = v->note_range_adjustment;
	add_events (Gdk::BUTTON_PRESS_MASK |
		    Gdk::BUTTON_RELEASE_MASK |
		    Gdk::POINTER_MOTION_MASK |
		    Gdk::ENTER_NOTIFY_MASK |
		    Gdk::LEAVE_NOTIFY_MASK |
		    Gdk::SCROLL_MASK);

	for (int i = 0; i < 128; ++i) {
		_active_notes[i] = false;
	}

	_view.NoteRangeChanged.connect (sigc::mem_fun (*this, &PianoRollHeader::note_range_changed));
}

inline void
create_path(Cairo::RefPtr<Cairo::Context> cr, double x[], double y[], int start, int stop)
{
	cr->move_to(x[start], y[start]);

	for (int i = start+1; i <= stop; ++i) {
		cr->line_to(x[i], y[i]);
	}
}

inline void
render_rect(Cairo::RefPtr<Cairo::Context> cr, int /*note*/, double x[], double y[],
	     PianoRollHeader::Color& bg)
{
	cr->set_source_rgb(bg.r, bg.g, bg.b);
	create_path(cr, x, y, 0, 4);
	cr->fill();
}

bool
PianoRollHeader::on_scroll_event (GdkEventScroll* ev)
{
	int hovered_note = _view.y_to_note(ev->y);
	switch (ev->direction) {
	case GDK_SCROLL_UP:
		_adj.set_value (min (_adj.get_value() + 1.0, _adj.get_upper() - _adj.get_page_size()));
		break;
	case GDK_SCROLL_DOWN:
		_adj.set_value (_adj.get_value() - 1.0);
		break;
	case GDK_SCROLL_LEFT: //ZOOM OUT
		_adj.set_page_size (max(_adj.get_page_size () -1,0.0));
		break;
	case GDK_SCROLL_RIGHT: //ZOOM IN
		if (_adj.get_value() + _adj.get_page_size() < 127.0){
		//	_adj.set_page_size (min(_adj.get_page_size () +1 ,  127.0 ));
		//	_adj.set_value (_adj.get_value() - (_adj.get_page_size() / 10.0));
		}
		break;
	default:
		return false;
	}
	std::cout << "hilight_note: " << std::to_string(_highlighted_note) << " Hov_Note: " << std::to_string(_view.y_to_note(ev->y)) << " Val: " << _adj.get_value() << " upper: " << _adj.get_upper() << " lower: " << _adj.get_lower() << " page_size: "<< _adj.get_page_size () << std::endl;
	_adj.value_changed ();
	queue_draw ();
	return true;
}


void
PianoRollHeader::get_path(int note, double x[], double y[])
{
	double y_pos = floor(_view.note_to_y(note));
	double note_height;
	_raw_note_height = floor(_view.note_to_y(note - 1)) - y_pos;
	double width = get_width();

	if (note == 0) {
		note_height = floor(_view.contents_height()) - y_pos;
	} else {
		note_height = _raw_note_height <= 3? _raw_note_height : _raw_note_height - 1.f;
	}
	x[0] = 1.f;
	y[0] = y_pos + note_height;
	x[1] = 1.f;
	y[1] = y_pos;
	x[2] = width;
	y[2] = y_pos;
	x[3] = width;
	y[3] = y_pos + note_height;
	x[4] = 1.f;
	y[4] = y_pos + note_height;
	return;
}

bool
PianoRollHeader::on_expose_event (GdkEventExpose* ev)
{
	GdkRectangle& rect = ev->area;
	double font_size;
	int lowest, highest;
	PianoRollHeader::Color bg;
	Cairo::RefPtr<Cairo::Context> cr = get_window()->create_cairo_context();
	double x[9];
	double y[9];
	int oct_rel;
	int y1 = max(rect.y, 0);
	int y2 = min(rect.y + rect.height, (int) floor(_view.contents_height()));

	//Cairo::TextExtents te;
	lowest = max(_view.lowest_note(), _view.y_to_note(y2));
	highest = min(_view.highest_note(), _view.y_to_note(y1));

	if (lowest > 127) {
		lowest = 0;
	}

	cr->select_font_face ("Roboto", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
	font_size = min((double) 10.0f, _note_height - 4.0f);
	cr->set_font_size(font_size);

	/* fill the entire rect with the color for non-highlighted white notes.
	 * then we won't have to draw the background for those notes,
	 * and would only have to draw the background for the one highlighted white note*/
	//cr->rectangle(rect.x, rect.y, rect.width, rect.height);
	//cr->set_source_rgb(white.r, white.g, white.b);
	//cr->fill();

	cr->set_line_width(1.0f);

	/* draw vertical lines with shade at both ends of the widget */
	cr->set_source_rgb(0.0f, 0.0f, 0.0f);
	cr->move_to(0.f, rect.y);
	cr->line_to(0.f, rect.y + rect.height);
	cr->stroke();
	cr->move_to(get_width(),rect.y);
	cr->line_to(get_width(), rect.y + rect.height);
	cr->stroke();

	for (int i = lowest; i <= highest; ++i) {
		oct_rel = i % 12;

		switch (oct_rel) {
		case 1:
		case 3:
		case 6:
		case 8:
		case 10:
			/* black note */
			if (i == _highlighted_note) {
				bg.set(black_highlight);
			} else {
				bg.set(black);
			}

			/* draw black separators */
			cr->set_source_rgb(0.0f, 0.0f, 0.0f);
			get_path(i, x, y);
			create_path(cr, x, y, 0, 1);
			cr->stroke();

			get_path(i, x, y);
			create_path(cr, x, y, 0, 1);
			cr->stroke();

			get_path(i, x, y);
			render_rect(cr, i, x, y, bg);
			break;

		default:
			/* white note */
			if (i == _highlighted_note) {
				bg.set(white_highlight);
			} else {
				bg.set(white);
			}

		switch(oct_rel) {
		case 0:
		case 5:
		case 2:
		case 7:
		case 9:
		case 4:
		case 11:
			cr->set_source_rgb(0.0f, 0.0f, 0.0f);
			get_path(i, x, y);
			render_rect(cr, i, x, y, bg);
			break;

		default:
			break;

		}
		break;

		}

		/* render the name of which C this is */
		if (oct_rel == 0) {
			std::stringstream s;
			double y = floor(_view.note_to_y(i)) - 0.5f;
			double note_height = floor(_view.note_to_y(i - 1)) - y;

			int cn = i / 12 - 1;
			s << "C" << cn;

			//cr->get_text_extents(s.str(), te);
			cr->set_source_rgb(0.30f, 0.30f, 0.30f);
			cr->move_to(2.0f, y + note_height - 1.0f - (note_height - font_size) / 2.0f);
			cr->show_text(s.str());
		}
	}
	on_size_request(_r);

	return true;
}

bool
PianoRollHeader::on_motion_notify_event (GdkEventMotion* ev)
{
	int note = _view.y_to_note(ev->y);
	set_note_highlight (note);

	if (_dragging) {

		if ( false /*editor().current_mouse_mode() == Editing::MouseRange*/ ) {   //ToDo:  fix this.  this mode is buggy, and of questionable utility anyway

			/* select note range */

			if (Keyboard::no_modifiers_active (ev->state)) {
				AddNoteSelection (note); // EMIT SIGNAL
			}

		} else {
			/* play notes */
			/* redraw already taken care of above in set_note_highlight */
			if (_clicked_note != NO_MIDI_NOTE && _clicked_note != note) {
				_active_notes[_clicked_note] = false;
				send_note_off(_clicked_note);

				_clicked_note = note;

				if (!_active_notes[note]) {
					_active_notes[note] = true;
					send_note_on(note);
				}
			}
		}
	}

	_adj.value_changed();
	//win->process_updates(false);

	return true;
}

bool
PianoRollHeader::on_button_press_event (GdkEventButton* ev)
{
	int note = _view.y_to_note(ev->y);
	bool tertiary = Keyboard::modifier_state_contains (ev->state, Keyboard::TertiaryModifier);
	int modifiers;

	modifiers = gtk_accelerator_get_default_mod_mask ();

	if (ev->state == GDK_CONTROL_MASK){
		if (ev->type == GDK_2BUTTON_PRESS) {
			_adj.set_value (0.0);
			_adj.set_page_size (127.0);
			_adj.value_changed ();
			queue_draw ();
			return false;
		}
		return false;
	} else if (ev->button == 2 && Keyboard::no_modifiers_active (ev->state)) {
		SetNoteSelection (note); // EMIT SIGNAL
		return true;
	} else if (tertiary && (ev->button == 1 || ev->button == 2)) {
		ExtendNoteSelection (note); // EMIT SIGNAL
		return true;
	} else if (ev->button == 1 && note >= 0 && note < 128) {
		add_modal_grab();
		_dragging = true;

		if (!_active_notes[note]) {
			_active_notes[note] = true;
			_clicked_note = note;
			send_note_on(note);

			invalidate_note_range(note, note);
		} else {
			reset_clicked_note(note);
		}
	}
	return true;
}

bool
PianoRollHeader::on_button_release_event (GdkEventButton* ev)
{
	int note = _view.y_to_note(ev->y);

	if (false /*editor().current_mouse_mode() == Editing::MouseRange*/ ) {  //Todo:  this mode is buggy, and of questionable utility anyway

		if (Keyboard::no_modifiers_active (ev->state)) {
			AddNoteSelection (note); // EMIT SIGNAL
		} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::PrimaryModifier)) {
			ToggleNoteSelection (note); // EMIT SIGNAL
		} else if (Keyboard::modifier_state_equals (ev->state, Keyboard::RangeSelectModifier)) {
			ExtendNoteSelection (note); // EMIT SIGNAL
		}

	} else {

		if (_dragging) {
			remove_modal_grab();

			if (note == _clicked_note) {
				reset_clicked_note(note);
			}
		}
	}

	_dragging = false;
	return true;
}

void
PianoRollHeader::set_note_highlight (uint8_t note) {
	if (_highlighted_note == note) {
		return;
	}

	if (_highlighted_note != NO_MIDI_NOTE) {
		if (note > _highlighted_note) {
			invalidate_note_range (_highlighted_note, note);
		} else {
			invalidate_note_range (note, _highlighted_note);
		}
	}

	_highlighted_note = note;

	if (_highlighted_note != NO_MIDI_NOTE) {
		invalidate_note_range (_highlighted_note, _highlighted_note);
	}
}

bool
PianoRollHeader::on_enter_notify_event (GdkEventCrossing* ev)
{
	set_note_highlight (_view.y_to_note(ev->y));
	return true;
}

bool
PianoRollHeader::on_leave_notify_event (GdkEventCrossing*)
{
	invalidate_note_range(_highlighted_note, _highlighted_note);

	if (_clicked_note != NO_MIDI_NOTE) {
		reset_clicked_note(_clicked_note, _clicked_note != _highlighted_note);
	}

	_highlighted_note = NO_MIDI_NOTE;
	return true;
}

void
PianoRollHeader::note_range_changed()
{
	_note_height = floor(_view.note_height()) + 0.5f;
	queue_draw();
}

void
PianoRollHeader::invalidate_note_range(int lowest, int highest)
{
	Glib::RefPtr<Gdk::Window> win = get_window();
	Gdk::Rectangle rect;

	lowest = max((int) _view.lowest_note(), lowest - 1);
	highest = min((int) _view.highest_note(), highest + 2);

	double y = _view.note_to_y(highest);
	double height = _view.note_to_y(lowest - 1) - y;

	rect.set_x(0);
	rect.set_width(get_width());
	rect.set_y((int) floor(y));
	rect.set_height((int) floor(height));

	if (win) {
		win->invalidate_rect(rect, false);
	}
}

void
PianoRollHeader::on_size_request(Gtk::Requisition* r)
{
	_r = r;
	float rtn;
	if (_raw_note_height >= 5.0){
	rtn = std::max (100.f, rintf (100.f * UIConfiguration::instance().get_ui_scale()));
	} else {
	rtn = std::max (25.f, rintf (25.f * UIConfiguration::instance().get_ui_scale()));
	}
	r->width = rtn;

}

void
PianoRollHeader::send_note_on(uint8_t note)
{
	boost::shared_ptr<ARDOUR::MidiTrack> track = _view.trackview().midi_track();
	MidiTimeAxisView* mtv = dynamic_cast<MidiTimeAxisView*> (&_view.trackview ());

	//cerr << "note on: " << (int) note << endl;

	if (track) {
		_event[0] = (MIDI_CMD_NOTE_ON | mtv->get_channel_for_add ());
		_event[1] = note;
		_event[2] = 100;

		track->write_immediate_event(3, _event);
	}
}

void
PianoRollHeader::send_note_off(uint8_t note)
{
	boost::shared_ptr<ARDOUR::MidiTrack> track = _view.trackview().midi_track();
	MidiTimeAxisView* mtv = dynamic_cast<MidiTimeAxisView*> (&_view.trackview ());

	if (track) {
		_event[0] = (MIDI_CMD_NOTE_OFF | mtv->get_channel_for_add ());
		_event[1] = note;
		_event[2] = 100;

		track->write_immediate_event(3, _event);
	}
}

void
PianoRollHeader::reset_clicked_note (uint8_t note, bool invalidate)
{
	_active_notes[note] = false;
	_clicked_note = NO_MIDI_NOTE;
	send_note_off (note);
	if (invalidate) {
		invalidate_note_range (note, note);
	}
}

PublicEditor&
PianoRollHeader::editor() const
{
	return _view.trackview().editor();
}

void
PianoRollHeader::set_min_page_size(double page_size)
{
};
