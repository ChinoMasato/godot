/**************************************************************************/
/*  script_check_json.cpp                                                 */
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

#include "script_check_json.h"

#ifdef TOOLS_ENABLED

#include "core/io/json.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

String ScriptCheckJSON::error_code(const String &p_message) {
	// Keep the message's wording, drop what varies: quoted names and numbers.
	String shape;
	char32_t quote = 0;
	bool in_number = false;
	for (int i = 0; i < p_message.length(); i++) {
		const char32_t c = p_message[i];
		if (quote != 0) {
			if (c == quote) {
				shape += String::chr(c);
				quote = 0;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			shape += String::chr(c);
			quote = c;
			continue;
		}
		if (is_digit(c)) {
			if (!in_number) {
				shape += "#";
			}
			in_number = true;
			continue;
		}
		in_number = false;
		shape += String::chr(c);
	}
	return "GD" + String::num_uint64(shape.strip_edges().hash(), 16).lpad(8, "0").to_upper();
}

String ScriptCheckJSON::format(const String &p_path, const List<EditorLanguage::ScriptError> &p_errors, const List<EditorLanguage::Warning> &p_warnings) {
	Array errors;
	for (const EditorLanguage::ScriptError &e : p_errors) {
		Dictionary d;
		d["file"] = e.path.is_empty() ? p_path : e.path;
		d["line"] = e.start_line;
		d["column"] = e.start_column;
		d["end_line"] = e.end_line;
		d["end_column"] = e.end_column;
		d["code"] = error_code(e.message);
		d["message"] = e.message;
		errors.push_back(d);
	}
	Array warnings;
	for (const EditorLanguage::Warning &w : p_warnings) {
		Dictionary d;
		d["file"] = p_path;
		d["line"] = w.start_line;
		d["column"] = w.start_column;
		d["end_line"] = w.end_line;
		d["end_column"] = w.end_column;
		d["code"] = w.string_code;
		d["message"] = w.message;
		warnings.push_back(d);
	}
	Dictionary result;
	result["ok"] = errors.is_empty();
	result["file"] = p_path;
	result["errors"] = errors;
	result["warnings"] = warnings;
	return JSON::stringify(result, "", false);
}

#endif // TOOLS_ENABLED
