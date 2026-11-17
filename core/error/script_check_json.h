/**************************************************************************/
/*  script_check_json.h                                                   */
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

#ifdef TOOLS_ENABLED

#include "core/object/editor_language.h"
#include "core/templates/list.h"

// JSON output for --check-only (BACKLOG B-202). With --check-only --json, the
// script is validated by its language and the result is printed as one line:
// {"ok", "file", "errors": [...], "warnings": [...]}, each item with
// {"file", "line", "column", "end_line", "end_column", "code", "message"}.
// Error "code" is "GD" + 8 hex digits computed from the message with quoted
// names and numbers removed, so one kind of error keeps one code whatever the
// identifiers are. Warning "code" is the warning's own name (UNUSED_VARIABLE...).
class ScriptCheckJSON {
public:
	static String error_code(const String &p_message);
	static String format(const String &p_path, const List<EditorLanguage::ScriptError> &p_errors, const List<EditorLanguage::Warning> &p_warnings);
};

#endif // TOOLS_ENABLED
