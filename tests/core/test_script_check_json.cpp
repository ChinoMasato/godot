/**************************************************************************/
/*  test_script_check_json.cpp                                            */
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

TEST_FORCE_LINK(test_script_check_json)

#ifdef TOOLS_ENABLED

#include "core/error/script_check_json.h"
#include "core/io/json.h"

namespace TestScriptCheckJSON {

TEST_CASE("[ScriptCheckJSON] Error codes ignore names and numbers") {
	const String a = ScriptCheckJSON::error_code("Identifier \"foo\" not declared in the current scope.");
	const String b = ScriptCheckJSON::error_code("Identifier \"bar_2\" not declared in the current scope.");
	CHECK(a == b);
	CHECK(a.begins_with("GD"));
	CHECK(a.length() == 10);
	CHECK(ScriptCheckJSON::error_code("Expected 2 arguments but got 3.") == ScriptCheckJSON::error_code("Expected 10 arguments but got 1."));
	CHECK(ScriptCheckJSON::error_code("Cannot assign 'x' to a constant.") == ScriptCheckJSON::error_code("Cannot assign 'y' to a constant."));
	CHECK(a != ScriptCheckJSON::error_code("Function \"foo\" not found in base self."));
}

TEST_CASE("[ScriptCheckJSON] Result has the expected fields") {
	List<EditorLanguage::ScriptError> errors;
	EditorLanguage::ScriptError e;
	e.start_line = 3;
	e.start_column = 5;
	e.end_line = 3;
	e.end_column = 8;
	e.message = "Identifier \"foo\" not declared in the current scope.";
	errors.push_back(e);
	EditorLanguage::ScriptError other = e;
	other.path = "res://other.gd";
	errors.push_back(other);
	List<EditorLanguage::Warning> warnings;
	EditorLanguage::Warning w;
	w.start_line = 2;
	w.start_column = 6;
	w.string_code = "UNUSED_VARIABLE";
	w.message = "The local variable \"x\" is declared but never used in the block.";
	warnings.push_back(w);

	const String text = ScriptCheckJSON::format("res://main.gd", errors, warnings);
	CHECK_FALSE(text.contains("\n"));
	const Variant parsed = JSON::parse_string(text);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary result = parsed;
	CHECK_FALSE(bool(result["ok"]));
	CHECK(String(result["file"]) == "res://main.gd");
	const Array errs = result["errors"];
	REQUIRE(errs.size() == 2);
	const Dictionary first = errs[0];
	CHECK(String(first["file"]) == "res://main.gd");
	CHECK(int(first["line"]) == 3);
	CHECK(int(first["column"]) == 5);
	CHECK(int(first["end_column"]) == 8);
	CHECK(String(first["code"]) == ScriptCheckJSON::error_code(e.message));
	CHECK(String(first["message"]) == String(e.message));
	CHECK(String(Dictionary(errs[1])["file"]) == "res://other.gd");
	const Array warns = result["warnings"];
	REQUIRE(warns.size() == 1);
	CHECK(String(Dictionary(warns[0])["code"]) == "UNUSED_VARIABLE");
	CHECK(int(Dictionary(warns[0])["line"]) == 2);

	const Dictionary clean = JSON::parse_string(ScriptCheckJSON::format("res://ok.gd", List<EditorLanguage::ScriptError>(), List<EditorLanguage::Warning>()));
	CHECK(bool(clean["ok"]));
	CHECK(Array(clean["errors"]).is_empty());
}

} // namespace TestScriptCheckJSON

#endif // TOOLS_ENABLED
