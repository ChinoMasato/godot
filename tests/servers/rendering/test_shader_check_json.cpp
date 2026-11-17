/**************************************************************************/
/*  test_shader_check_json.cpp                                            */
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

TEST_FORCE_LINK(test_shader_check_json)

#ifdef TOOLS_ENABLED

#include "core/io/json.h"
#include "servers/rendering/shader_check_json.h"

namespace TestShaderCheckJSON {

static Dictionary check(const String &p_code, const String &p_path = "res://test.gdshader") {
	const Variant parsed = JSON::parse_string(ShaderCheckJSON::check(p_code, p_path));
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	return parsed;
}

TEST_CASE("[ShaderCheckJSON] A valid shader passes, with its type and warnings") {
	const Dictionary r = check("shader_type spatial;\nuniform float unused_amount;\nvoid fragment() {\n\tALBEDO = vec3(1.0, 0.0, 0.0);\n}\n");
	CHECK(bool(r["ok"]));
	CHECK(String(r["shader_type"]) == "spatial");
	CHECK(Array(r["errors"]).is_empty());
	const Array warnings = r["warnings"];
	REQUIRE(warnings.size() == 1);
	CHECK(String(Dictionary(warnings[0])["code"]) == "UNUSED_UNIFORM");
	CHECK(int(Dictionary(warnings[0])["line"]) == 2);
}

TEST_CASE("[ShaderCheckJSON] Errors have the line, a stable code and the message") {
	const Dictionary r = check("shader_type canvas_item;\n\nvoid fragment() {\n\tCOLOR = vec4(nope, 0.0, 0.0, 1.0);\n}\n");
	CHECK_FALSE(bool(r["ok"]));
	CHECK(String(r["shader_type"]) == "canvas_item");
	const Array errors = r["errors"];
	REQUIRE(errors.size() == 1);
	const Dictionary e = errors[0];
	CHECK(int(e["line"]) == 4);
	CHECK(String(e["file"]) == "res://test.gdshader");
	CHECK(String(e["code"]).begins_with("SH"));
	CHECK(String(e["message"]).contains("nope"));
	// The same kind of error with another name keeps the code.
	const Dictionary other = Array(check("shader_type canvas_item;\nvoid fragment() {\n\tCOLOR = vec4(other_name);\n}\n")["errors"])[0];
	CHECK(String(other["code"]) == String(e["code"]));
	// A built-in of another shader type is an error (the type's functions are used).
	CHECK_FALSE(bool(check("shader_type canvas_item;\nvoid fragment() {\n\tALBEDO = vec3(1.0);\n}\n")["ok"]));
}

TEST_CASE("[ShaderCheckJSON] Preprocessor errors and includes") {
	const Dictionary r = check("shader_type spatial;\n#if\nvoid fragment() {}\n");
	CHECK_FALSE(bool(r["ok"]));
	CHECK(int(Dictionary(Array(r["errors"])[0])["line"]) == 2);
	// An include alone has no shader_type and no unused warnings.
	const Dictionary inc = check("float helper(float x) {\n\treturn x * 2.0;\n}\n", "res://lib.gdshaderinc");
	CHECK(bool(inc["ok"]));
	CHECK(Array(inc["warnings"]).is_empty());
}

} // namespace TestShaderCheckJSON

#endif // TOOLS_ENABLED
