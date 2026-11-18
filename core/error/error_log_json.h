/**************************************************************************/
/*  error_log_json.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/error/error_macros.h"
#include "core/string/ustring.h"

// Structured error log (BACKLOG B-201): with --log-json <file>, every engine
// error and warning is also appended to <file> as one line of JSON (JSON Lines),
// so agents can read errors without parsing console text. The normal output is
// unchanged. Each line: {"level", "code", "file", "line", "function", "message",
// "condition", "frame" (the process frame, B-308)}, plus "script_file", "script_line" and "script_function" when an
// engine error (push_error, get_node...) was raised from a script. "code" is stable for a given error site (file, function and the
// checked condition), so the same problem keeps the same code between runs.
class ErrorLogJSON {
public:
	static String level_name(ErrorHandlerType p_type);
	static String code_for(const String &p_file, const String &p_function, const String &p_condition);
	// p_script_* is the script position that caused an engine error (from the
	// script backtrace, B-103); empty when no script was running.
	static String format_line(const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, ErrorHandlerType p_type, const String &p_script_file = String(), int p_script_line = -1, const String &p_script_function = String());

	static Error start(const String &p_path);
	static void stop();
	static bool is_active();
};
