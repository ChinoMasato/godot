/**************************************************************************/
/*  uzu_palette_dither.cpp                                                */
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

#include "uzu_palette_dither.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#ifdef RD_ENABLED
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/storage/render_data.h"

// Each pixel on its own, so the color buffer is read and written in place.
static const char *UZU_PALETTE_DITHER_SHADER = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float levels;
	float dither_strength;
	float dither_scale;
	float pad0;
	float pad1;
	float pad2;
} params;

const float BAYER[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (p.x >= int(params.size.x) || p.y >= int(params.size.y)) {
		return;
	}
	vec4 c = imageLoad(color, p);
	ivec2 cell = (p / int(params.dither_scale)) % 4;
	float threshold = (BAYER[cell.y * 4 + cell.x] + 0.5) / 16.0 - 0.5;
	float steps = params.levels - 1.0;
	// Quantize in a display-like gamma space (this runs before tonemapping).
	vec3 g = pow(max(c.rgb, vec3(0.0)), vec3(1.0 / 2.2));
	vec3 q = floor(g * steps + 0.5 + threshold * params.dither_strength) / steps;
	q = max(q, vec3(0.0));
	imageStore(color, p, vec4(pow(q, vec3(2.2)), c.a));
}
)";
#endif

static const int BAYER_4X4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

void UZUPaletteDither::set_levels(int p_levels) {
	levels = CLAMP(p_levels, 2, 64);
}

int UZUPaletteDither::get_levels() const {
	return levels;
}

void UZUPaletteDither::set_dither_strength(float p_strength) {
	dither_strength = CLAMP(p_strength, 0.0f, 1.0f);
}

float UZUPaletteDither::get_dither_strength() const {
	return dither_strength;
}

void UZUPaletteDither::set_dither_scale(int p_scale) {
	dither_scale = CLAMP(p_scale, 1, 8);
}

int UZUPaletteDither::get_dither_scale() const {
	return dither_scale;
}

float UZUPaletteDither::get_dither_threshold(const Vector2i &p_pixel) const {
	const int x = (MAX(p_pixel.x, 0) / dither_scale) % 4;
	const int y = (MAX(p_pixel.y, 0) / dither_scale) % 4;
	return (BAYER_4X4[y * 4 + x] + 0.5f) / 16.0f - 0.5f;
}

Color UZUPaletteDither::quantize(const Color &p_color, const Vector2i &p_pixel) const {
	const float threshold = get_dither_threshold(p_pixel);
	const float steps = levels - 1.0f;
	Color out = p_color;
	for (int i = 0; i < 3; i++) {
		const float g = Math::pow(MAX(p_color[i], 0.0f), 1.0f / 2.2f);
		const float q = MAX(Math::floor(g * steps + 0.5f + threshold * dither_strength) / steps, 0.0f);
		out[i] = Math::pow(q, 2.2f);
	}
	return out;
}

void UZUPaletteDither::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "effect_callback_type" || p_property.name.begins_with("access_resolved") || p_property.name.begins_with("needs_")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

bool UZUPaletteDither::_ensure_pipeline() {
#ifdef RD_ENABLED
	if (pipeline.is_valid()) {
		return true;
	}
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (shader_failed || rd == nullptr) {
		return false;
	}
	String error;
	const Vector<uint8_t> spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, UZU_PALETTE_DITHER_SHADER, RD::SHADER_LANGUAGE_GLSL, &error);
	if (spirv.is_empty()) {
		shader_failed = true;
		ERR_FAIL_V_MSG(false, "UZUPaletteDither: could not compile the shader: " + error);
	}
	Vector<RD::ShaderStageSPIRVData> stages;
	RD::ShaderStageSPIRVData stage;
	stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv;
	stages.push_back(stage);
	shader = rd->shader_create_from_spirv(stages, "UZUPaletteDither");
	if (shader.is_valid()) {
		pipeline = rd->compute_pipeline_create(shader);
	}
	shader_failed = !pipeline.is_valid();
	return pipeline.is_valid();
#else
	return false;
#endif
}

void UZUPaletteDither::_render(int p_effect_callback_type, const RenderData *p_render_data) {
#ifdef RD_ENABLED
	if (p_render_data == nullptr) {
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
	struct PushConstant {
		float size[2];
		float levels;
		float dither_strength;
		float dither_scale;
		float pad[3];
	};
	const PushConstant push = { { float(size.x), float(size.y) }, float(levels), dither_strength, float(dither_scale), { 0.0f, 0.0f, 0.0f } };
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		Vector<RD::Uniform> uniforms;
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view)));
		const RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
		const RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, pipeline);
		rd->compute_list_bind_uniform_set(list, uniform_set, 0);
		rd->compute_list_set_push_constant(list, &push, sizeof(PushConstant));
		rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
		rd->compute_list_end();
		rd->free_rid(uniform_set);
	}
#endif
}

void UZUPaletteDither::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_levels", "levels"), &UZUPaletteDither::set_levels);
	ClassDB::bind_method(D_METHOD("get_levels"), &UZUPaletteDither::get_levels);
	ClassDB::bind_method(D_METHOD("set_dither_strength", "strength"), &UZUPaletteDither::set_dither_strength);
	ClassDB::bind_method(D_METHOD("get_dither_strength"), &UZUPaletteDither::get_dither_strength);
	ClassDB::bind_method(D_METHOD("set_dither_scale", "scale"), &UZUPaletteDither::set_dither_scale);
	ClassDB::bind_method(D_METHOD("get_dither_scale"), &UZUPaletteDither::get_dither_scale);
	ClassDB::bind_method(D_METHOD("get_dither_threshold", "pixel"), &UZUPaletteDither::get_dither_threshold);
	ClassDB::bind_method(D_METHOD("quantize", "color", "pixel"), &UZUPaletteDither::quantize);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "levels", PROPERTY_HINT_RANGE, "2,64,1"), "set_levels", "get_levels");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dither_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dither_strength", "get_dither_strength");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dither_scale", PROPERTY_HINT_RANGE, "1,8,1,suffix:px"), "set_dither_scale", "get_dither_scale");
}

UZUPaletteDither::UZUPaletteDither() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && get_rid().is_valid()) {
		rs->compositor_effect_set_callback(get_rid(), RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, callable_mp(this, &UZUPaletteDither::_render));
	}
}

UZUPaletteDither::~UZUPaletteDither() {
#ifdef RD_ENABLED
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd != nullptr && shader.is_valid()) {
		rd->free_rid(shader); // Also frees the pipeline.
	}
#endif
}
