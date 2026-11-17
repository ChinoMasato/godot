/**************************************************************************/
/*  test_error_log_json.cpp                                               */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_error_log_json)

#include "core/error/error_log_json.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

namespace TestErrorLogJSON {

TEST_CASE("[ErrorLogJSON] Lines are JSON with stable codes") {
	const String line = ErrorLogJSON::format_line("load_scene", "scene/main.cpp", 42, "Condition \"p_path.is_empty()\" is true.", "Missing path \"a\b\"", ERR_HANDLER_ERROR);
	const Variant parsed = JSON::parse_string(line);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary entry = parsed;
	CHECK(String(entry["level"]) == "error");
	CHECK(String(entry["file"]) == "scene/main.cpp");
	CHECK(int(entry["line"]) == 42);
	CHECK(String(entry["function"]) == "load_scene");
	CHECK(String(entry["message"]) == "Missing path \"a\b\"");
	CHECK(String(entry["condition"]) == "Condition \"p_path.is_empty()\" is true.");
	CHECK(String(entry["code"]).begins_with("E"));
	CHECK(String(entry["code"]).length() == 9);
	CHECK_FALSE(line.contains("\n"));

	// The code depends on the site, not on the message or the line number.
	CHECK(ErrorLogJSON::code_for("scene/main.cpp", "load_scene", "cond") == ErrorLogJSON::code_for("scene\\main.cpp", "load_scene", "cond"));
	CHECK(ErrorLogJSON::code_for("scene/main.cpp", "load_scene", "cond") != ErrorLogJSON::code_for("scene/main.cpp", "save_scene", "cond"));
	const String other = ErrorLogJSON::format_line("load_scene", "scene/main.cpp", 99, "Condition \"p_path.is_empty()\" is true.", "Another message", ERR_HANDLER_ERROR);
	CHECK(Dictionary(JSON::parse_string(other))["code"] == entry["code"]);

	// An empty message falls back to the condition.
	const Dictionary bare = JSON::parse_string(ErrorLogJSON::format_line("f", "a.cpp", 1, "Something failed.", "", ERR_HANDLER_WARNING));
	CHECK(String(bare["level"]) == "warning");
	CHECK(String(bare["message"]) == "Something failed.");
}

TEST_CASE("[ErrorLogJSON] Errors are written to the file while active") {
	const String path = OS::get_singleton()->get_cache_path().path_join("uzu_error_log_test.jsonl");
	REQUIRE(ErrorLogJSON::start(path) == OK);
	CHECK(ErrorLogJSON::is_active());
	CHECK(ErrorLogJSON::start(path) == ERR_ALREADY_IN_USE);
	ERR_PRINT("uzu error log test message");
	WARN_PRINT("uzu error log test warning");
	ErrorLogJSON::stop();
	CHECK_FALSE(ErrorLogJSON::is_active());
	ERR_PRINT("not logged after stop");

	const String text = FileAccess::get_file_as_string(path);
	const PackedStringArray lines = text.strip_edges().split("\n");
	REQUIRE(lines.size() == 2);
	const Dictionary first = JSON::parse_string(lines[0]);
	const Dictionary second = JSON::parse_string(lines[1]);
	CHECK(String(first["level"]) == "error");
	CHECK(String(first["message"]) == "uzu error log test message");
	CHECK(String(first["file"]).ends_with("test_error_log_json.cpp"));
	CHECK(String(second["level"]) == "warning");
	DirAccess::remove_absolute(path);
}

} // namespace TestErrorLogJSON
