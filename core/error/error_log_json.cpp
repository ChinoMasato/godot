/**************************************************************************/
/*  error_log_json.cpp                                                    */
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

#include "error_log_json.h"

#include "core/config/engine.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/script_backtrace.h"
#include "core/object/script_language.h"
#include "core/os/mutex.h"

static Mutex error_log_mutex;
static Ref<FileAccess> error_log_file;
static ErrorHandlerList error_log_handler;
static thread_local bool error_log_writing = false;

String ErrorLogJSON::level_name(ErrorHandlerType p_type) {
	switch (p_type) {
		case ERR_HANDLER_WARNING:
			return "warning";
		case ERR_HANDLER_SCRIPT:
			return "script_error";
		case ERR_HANDLER_SHADER:
			return "shader_error";
		case ERR_HANDLER_ERROR:
		default:
			return "error";
	}
}

String ErrorLogJSON::code_for(const String &p_file, const String &p_function, const String &p_condition) {
	// The site, not the message: messages often contain values that change.
	const String key = p_file.replace_char('\\', '/') + ":" + p_function + ":" + p_condition;
	return "E" + String::num_uint64(key.hash(), 16).lpad(8, "0").to_upper();
}

String ErrorLogJSON::format_line(const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, ErrorHandlerType p_type, const String &p_script_file, int p_script_line, const String &p_script_function) {
	const String file = String::utf8(p_file);
	const String function = String::utf8(p_function);
	const String condition = String::utf8(p_error);
	String message = String::utf8(p_message);
	if (message.is_empty()) {
		message = condition;
	}
	Dictionary entry;
	entry["level"] = level_name(p_type);
	entry["code"] = code_for(file, function, condition);
	entry["file"] = file;
	entry["line"] = p_line;
	entry["function"] = function;
	entry["message"] = message;
	entry["condition"] = condition;
	// The frame the error happened in (B-308), so a failure can be replayed up to it.
	if (Engine::get_singleton()) {
		entry["frame"] = Engine::get_singleton()->get_process_frames();
	}
	if (!p_script_file.is_empty()) {
		entry["script_file"] = p_script_file;
		entry["script_line"] = p_script_line;
		entry["script_function"] = p_script_function;
	}
	return JSON::stringify(entry, "", false);
}

static void _error_log_json_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	if (error_log_writing) {
		return; // An error while writing the log must not recurse.
	}
	error_log_writing = true;
	// An engine error raised from a script (push_error, get_node...) points to C++;
	// the innermost script frame says which script line caused it.
	String script_file;
	String script_function;
	int script_line = -1;
	if (p_type != ERR_HANDLER_SCRIPT && p_type != ERR_HANDLER_SHADER) {
		for (const Ref<ScriptBacktrace> &backtrace : ScriptServer::capture_script_backtraces(false)) {
			if (backtrace.is_valid() && !backtrace->is_empty()) {
				script_file = backtrace->get_frame_file(0);
				script_line = backtrace->get_frame_line(0);
				script_function = backtrace->get_frame_function(0);
				break;
			}
		}
	}
	{
		MutexLock lock(error_log_mutex);
		if (error_log_file.is_valid()) {
			error_log_file->store_line(ErrorLogJSON::format_line(p_function, p_file, p_line, p_error, p_message, p_type, script_file, script_line, script_function));
			error_log_file->flush();
		}
	}
	error_log_writing = false;
}

Error ErrorLogJSON::start(const String &p_path) {
	MutexLock lock(error_log_mutex);
	if (error_log_file.is_valid()) {
		return ERR_ALREADY_IN_USE;
	}
	Error err;
	error_log_file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (error_log_file.is_null()) {
		return err != OK ? err : ERR_CANT_OPEN;
	}
	error_log_handler.errfunc = _error_log_json_handler;
	add_error_handler(&error_log_handler);
	return OK;
}

void ErrorLogJSON::stop() {
	MutexLock lock(error_log_mutex);
	if (error_log_file.is_null()) {
		return;
	}
	remove_error_handler(&error_log_handler);
	error_log_file->flush();
	error_log_file.unref();
}

bool ErrorLogJSON::is_active() {
	MutexLock lock(error_log_mutex);
	return error_log_file.is_valid();
}
