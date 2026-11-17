/**************************************************************************/
/*  shader_check_json.cpp                                                 */
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

#include "shader_check_json.h"

#ifdef TOOLS_ENABLED

#include "core/error/script_check_json.h"
#include "core/io/json.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/shader_language.h"
#include "servers/rendering/shader_preprocessor.h"
#include "servers/rendering/shader_types.h"

static ShaderLanguage::DataType _global_uniform_type(const StringName &p_name) {
	if (!RenderingServer::get_singleton()) {
		return ShaderLanguage::TYPE_VOID;
	}
	return (ShaderLanguage::DataType)RenderingServer::global_shader_uniform_type_get_shader_datatype(RenderingServer::get_singleton()->global_shader_parameter_get_type(p_name));
}

static Dictionary _item(const String &p_file, int p_line, const String &p_code, const String &p_message) {
	Dictionary d;
	d["file"] = p_file;
	d["line"] = p_line;
	d["column"] = -1; // The shader compiler reports lines only.
	d["code"] = p_code;
	d["message"] = p_message;
	return d;
}

static String _error_code(const String &p_message) {
	return "SH" + ScriptCheckJSON::error_code(p_message).substr(2);
}

String ShaderCheckJSON::check(const String &p_code, const String &p_path) {
	Array errors;
	Array warnings;
	String shader_type;

	ShaderPreprocessor preprocessor;
	String code;
	String pp_error;
	List<ShaderPreprocessor::FilePosition> pp_positions;
	const Error pp_result = preprocessor.preprocess_for_editor(p_code, p_path, code, &pp_error, &pp_positions);
	if (pp_result != OK) {
		const ShaderPreprocessor::FilePosition at = pp_positions.is_empty() ? ShaderPreprocessor::FilePosition() : pp_positions.back()->get();
		errors.push_back(_item(at.file.is_empty() ? p_path : at.file, pp_positions.is_empty() ? -1 : at.line, _error_code(pp_error), pp_error));
	} else {
		const bool is_include = p_path.get_extension() == "gdshaderinc";
		ShaderLanguage::ShaderCompileInfo info;
		info.global_shader_uniform_type_func = _global_uniform_type;
		if (is_include) {
			info.is_include = true;
		} else {
			shader_type = ShaderLanguage::get_shader_type(code);
			RSE::ShaderMode mode = RSE::SHADER_SPATIAL;
			if (shader_type == "canvas_item") {
				mode = RSE::SHADER_CANVAS_ITEM;
			} else if (shader_type == "particles") {
				mode = RSE::SHADER_PARTICLES;
			} else if (shader_type == "sky") {
				mode = RSE::SHADER_SKY;
			} else if (shader_type == "fog") {
				mode = RSE::SHADER_FOG;
			} else if (shader_type == "texture_blit") {
				mode = RSE::SHADER_TEXTURE_BLIT;
			}
			info.functions = ShaderTypes::get_singleton()->get_functions(mode);
			info.render_modes = ShaderTypes::get_singleton()->get_modes(mode);
			info.stencil_modes = ShaderTypes::get_singleton()->get_stencil_modes(mode);
			info.shader_types = ShaderTypes::get_singleton()->get_types();
		}

		ShaderLanguage language;
		language.enable_warning_checking(true);
		uint32_t flags = 0xFFFFFFFF;
		if (is_include) {
			// An include declares things for the files that include it.
			flags &= ~(ShaderWarning::UNUSED_CONSTANT_FLAG | ShaderWarning::UNUSED_FUNCTION_FLAG | ShaderWarning::UNUSED_STRUCT_FLAG | ShaderWarning::UNUSED_UNIFORM_FLAG | ShaderWarning::UNUSED_VARYING_FLAG);
		}
		language.set_warning_flags(flags);
		if (language.compile(code, info) != OK) {
			const Vector<ShaderLanguage::FilePosition> positions = language.get_include_positions();
			const String message = language.get_error_text();
			if (positions.size() > 1) {
				errors.push_back(_item(positions[positions.size() - 1].file, positions[positions.size() - 1].line, _error_code(message), message));
			} else {
				errors.push_back(_item(p_path, language.get_error_line(), _error_code(message), message));
			}
		} else {
			for (List<ShaderWarning>::Element *E = language.get_warnings_ptr(); E; E = E->next()) {
				warnings.push_back(_item(p_path, E->get().get_line(), E->get().get_name(), E->get().get_message()));
			}
		}
	}

	Dictionary result;
	result["ok"] = errors.is_empty();
	result["file"] = p_path;
	result["shader_type"] = shader_type;
	result["errors"] = errors;
	result["warnings"] = warnings;
	return JSON::stringify(result, "", false);
}

#endif // TOOLS_ENABLED
