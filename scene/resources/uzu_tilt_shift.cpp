/**************************************************************************/
/*  uzu_tilt_shift.cpp                                                    */
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

#include "uzu_tilt_shift.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#ifdef RD_ENABLED
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/storage/render_data.h"

// Three passes through one shader: 0 copies the color to scratch A, 1 blurs A
// horizontally into B, 2 blurs B vertically into the color. The blur radius of a
// pixel depends on its row only, so the separable blur stays a band blur.
static const char *UZU_TILT_SHIFT_SHADER = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D color_out;
layout(rgba16f, set = 0, binding = 1) uniform restrict readonly image2D color_in;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float focus_center;
	float focus_width;
	float transition;
	float max_blur;
	float pass;
	float pad;
} params;

const int MAX_RADIUS = 24;

float blur_radius(float y) {
	float d = abs(y - params.focus_center) - params.focus_width;
	float t = clamp(d / max(params.transition, 0.0001), 0.0, 1.0);
	return params.max_blur * (params.size.y / 1080.0) * t * t * (3.0 - 2.0 * t);
}

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size);
	if (p.x >= size.x || p.y >= size.y) {
		return;
	}
	vec4 c = imageLoad(color_in, p);
	float radius = min(blur_radius((float(p.y) + 0.5) / params.size.y), float(MAX_RADIUS));
	if (params.pass < 0.5 || radius < 0.5) {
		imageStore(color_out, p, c);
		return;
	}
	ivec2 dir = params.pass < 1.5 ? ivec2(1, 0) : ivec2(0, 1);
	float sigma = radius * 0.5; // No floor: the blur starts from nothing at the band edge.
	vec3 sum = vec3(0.0);
	float weights = 0.0;
	int r = int(ceil(radius));
	for (int i = -MAX_RADIUS; i <= MAX_RADIUS; i++) {
		if (i < -r || i > r) {
			continue;
		}
		float w = exp(-float(i * i) / (2.0 * sigma * sigma));
		sum += imageLoad(color_in, clamp(p + dir * i, ivec2(0), size - 1)).rgb * w;
		weights += w;
	}
	imageStore(color_out, p, vec4(sum / weights, c.a));
}
)";
#endif

void UZUTiltShift::set_focus_center(float p_center) {
	focus_center = CLAMP(p_center, 0.0f, 1.0f);
}

float UZUTiltShift::get_focus_center() const {
	return focus_center;
}

void UZUTiltShift::set_focus_width(float p_width) {
	focus_width = CLAMP(p_width, 0.0f, 1.0f);
}

float UZUTiltShift::get_focus_width() const {
	return focus_width;
}

void UZUTiltShift::set_transition(float p_transition) {
	transition = CLAMP(p_transition, 0.001f, 1.0f);
}

float UZUTiltShift::get_transition() const {
	return transition;
}

void UZUTiltShift::set_max_blur(float p_pixels) {
	max_blur = CLAMP(p_pixels, 0.0f, 24.0f);
}

float UZUTiltShift::get_max_blur() const {
	return max_blur;
}

float UZUTiltShift::get_blur_radius_at(float p_screen_y, float p_screen_height) const {
	const float d = Math::abs(p_screen_y - focus_center) - focus_width;
	const float t = CLAMP(d / MAX(transition, 0.0001f), 0.0f, 1.0f);
	return MIN(max_blur * (p_screen_height / 1080.0f) * t * t * (3.0f - 2.0f * t), 24.0f);
}

void UZUTiltShift::_validate_property(PropertyInfo &p_property) const {
	// The effect always runs after transparent geometry (registered in the constructor).
	if (p_property.name == "effect_callback_type" || p_property.name.begins_with("access_resolved") || p_property.name.begins_with("needs_")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

bool UZUTiltShift::_ensure_pipeline() {
#ifdef RD_ENABLED
	if (pipeline.is_valid()) {
		return true;
	}
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (shader_failed || rd == nullptr) {
		return false;
	}
	String error;
	const Vector<uint8_t> spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, UZU_TILT_SHIFT_SHADER, RD::SHADER_LANGUAGE_GLSL, &error);
	if (spirv.is_empty()) {
		shader_failed = true;
		ERR_FAIL_V_MSG(false, "UZUTiltShift: could not compile the shader: " + error);
	}
	Vector<RD::ShaderStageSPIRVData> stages;
	RD::ShaderStageSPIRVData stage;
	stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv;
	stages.push_back(stage);
	shader = rd->shader_create_from_spirv(stages, "UZUTiltShift");
	if (shader.is_valid()) {
		pipeline = rd->compute_pipeline_create(shader);
	}
	shader_failed = !pipeline.is_valid();
	return pipeline.is_valid();
#else
	return false;
#endif
}

void UZUTiltShift::_free_scratch() {
#ifdef RD_ENABLED
	RenderingDevice *rd = RenderingDevice::get_singleton();
	for (RID &texture : scratch) {
		if (rd != nullptr && texture.is_valid()) {
			rd->free_rid(texture);
		}
		texture = RID();
	}
#endif
}

void UZUTiltShift::_render(int p_effect_callback_type, const RenderData *p_render_data) {
#ifdef RD_ENABLED
	if (p_render_data == nullptr || Math::is_zero_approx(max_blur)) {
		return;
	}
	Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (buffers.is_null() || rd == nullptr || !buffers->has_internal_texture() || buffers->get_base_data_format() != RD::DATA_FORMAT_R16G16B16A16_SFLOAT || !_ensure_pipeline()) {
		return;
	}
	const Size2i size = buffers->get_internal_size();
	if (size.x <= 0 || size.y <= 0) {
		return;
	}
	if (scratch[0].is_null() || scratch_size != size) {
		_free_scratch();
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		format.width = size.x;
		format.height = size.y;
		format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT;
		scratch[0] = rd->texture_create(format, RD::TextureView());
		scratch[1] = rd->texture_create(format, RD::TextureView());
		scratch_size = size;
	}
	struct PushConstant {
		float size[2];
		float focus_center;
		float focus_width;
		float transition;
		float max_blur;
		float pass;
		float pad;
	};
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		const RID color = buffers->get_internal_texture(view);
		const RID targets[3] = { scratch[0], scratch[1], color };
		const RID sources[3] = { color, scratch[0], scratch[1] };
		for (int pass = 0; pass < 3; pass++) {
			const PushConstant push = { { float(size.x), float(size.y) }, focus_center, focus_width, transition, max_blur, float(pass), 0.0f };
			Vector<RD::Uniform> uniforms;
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, targets[pass]));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, sources[pass]));
			const RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
			const RD::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, pipeline);
			rd->compute_list_bind_uniform_set(list, uniform_set, 0);
			rd->compute_list_set_push_constant(list, &push, sizeof(PushConstant));
			rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
			rd->compute_list_end();
			rd->free_rid(uniform_set);
		}
	}
#endif
}

void UZUTiltShift::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_focus_center", "center"), &UZUTiltShift::set_focus_center);
	ClassDB::bind_method(D_METHOD("get_focus_center"), &UZUTiltShift::get_focus_center);
	ClassDB::bind_method(D_METHOD("set_focus_width", "width"), &UZUTiltShift::set_focus_width);
	ClassDB::bind_method(D_METHOD("get_focus_width"), &UZUTiltShift::get_focus_width);
	ClassDB::bind_method(D_METHOD("set_transition", "transition"), &UZUTiltShift::set_transition);
	ClassDB::bind_method(D_METHOD("get_transition"), &UZUTiltShift::get_transition);
	ClassDB::bind_method(D_METHOD("set_max_blur", "pixels"), &UZUTiltShift::set_max_blur);
	ClassDB::bind_method(D_METHOD("get_max_blur"), &UZUTiltShift::get_max_blur);
	ClassDB::bind_method(D_METHOD("get_blur_radius_at", "screen_y", "screen_height"), &UZUTiltShift::get_blur_radius_at, DEFVAL(1080.0));

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "focus_center", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_focus_center", "get_focus_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "focus_width", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_focus_width", "get_focus_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transition", PROPERTY_HINT_RANGE, "0.001,1,0.01"), "set_transition", "get_transition");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_blur", PROPERTY_HINT_RANGE, "0,24,0.1,suffix:px"), "set_max_blur", "get_max_blur");
}

UZUTiltShift::UZUTiltShift() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && get_rid().is_valid()) {
		rs->compositor_effect_set_callback(get_rid(), RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, callable_mp(this, &UZUTiltShift::_render));
	}
}

UZUTiltShift::~UZUTiltShift() {
#ifdef RD_ENABLED
	_free_scratch();
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd != nullptr && shader.is_valid()) {
		rd->free_rid(shader); // Also frees the pipeline.
	}
#endif
}
