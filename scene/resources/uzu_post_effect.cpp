/**************************************************************************/
/*  uzu_post_effect.cpp                                                   */
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

#include "uzu_post_effect.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#ifdef RD_ENABLED
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/storage/render_data.h"

static const char *UZU_POST_EFFECT_SHADER = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D color_out;
layout(rgba16f, set = 0, binding = 1) uniform restrict readonly image2D color_in;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float edge_strength;
	float vignette_strength;
} params;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size);
	if (p.x >= size.x || p.y >= size.y) {
		return;
	}
	vec4 c = imageLoad(color_in, p);
	// Unsharp mask: push each pixel away from the average of its neighbors, so
	// the light side of an edge gets lighter and the dark side darker.
	vec3 around = (imageLoad(color_in, clamp(p + ivec2(1, 0), ivec2(0), size - 1)).rgb +
			imageLoad(color_in, clamp(p - ivec2(1, 0), ivec2(0), size - 1)).rgb +
			imageLoad(color_in, clamp(p + ivec2(0, 1), ivec2(0), size - 1)).rgb +
			imageLoad(color_in, clamp(p - ivec2(0, 1), ivec2(0), size - 1)).rgb) * 0.25;
	vec3 color = max(c.rgb + (c.rgb - around) * params.edge_strength, vec3(0.0));
	// Vignette: darken toward the corners, keeping the center untouched.
	vec2 uv = (vec2(p) + 0.5) / params.size - 0.5;
	uv.x *= params.size.x / params.size.y;
	color *= 1.0 - params.vignette_strength * smoothstep(0.4, 0.95, length(uv));
	imageStore(color_out, p, vec4(color, c.a));
}
)";
#endif

void UZUPostEffect::set_edge_strength(float p_strength) {
	edge_strength = MAX(0.0f, p_strength);
}

float UZUPostEffect::get_edge_strength() const {
	return edge_strength;
}

void UZUPostEffect::set_vignette_strength(float p_strength) {
	vignette_strength = CLAMP(p_strength, 0.0f, 1.0f);
}

float UZUPostEffect::get_vignette_strength() const {
	return vignette_strength;
}

void UZUPostEffect::_validate_property(PropertyInfo &p_property) const {
	// The effect always runs after transparent geometry; its own callback is
	// registered in the constructor, so the base class setting does not apply.
	if (p_property.name == "effect_callback_type" || p_property.name.begins_with("access_resolved") || p_property.name.begins_with("needs_")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

bool UZUPostEffect::_ensure_pipeline() {
#ifdef RD_ENABLED
	if (pipeline.is_valid()) {
		return true;
	}
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (shader_failed || rd == nullptr) {
		return false;
	}
	String error;
	const Vector<uint8_t> spirv = rd->shader_compile_spirv_from_source(RD::SHADER_STAGE_COMPUTE, UZU_POST_EFFECT_SHADER, RD::SHADER_LANGUAGE_GLSL, &error);
	if (spirv.is_empty()) {
		shader_failed = true;
		ERR_FAIL_V_MSG(false, "UZUPostEffect: could not compile the shader: " + error);
	}
	Vector<RD::ShaderStageSPIRVData> stages;
	RD::ShaderStageSPIRVData stage;
	stage.shader_stage = RD::SHADER_STAGE_COMPUTE;
	stage.spirv = spirv;
	stages.push_back(stage);
	shader = rd->shader_create_from_spirv(stages, "UZUPostEffect");
	if (shader.is_valid()) {
		pipeline = rd->compute_pipeline_create(shader);
	}
	shader_failed = !pipeline.is_valid();
	return pipeline.is_valid();
#else
	return false;
#endif
}

void UZUPostEffect::_render(int p_effect_callback_type, const RenderData *p_render_data) {
#ifdef RD_ENABLED
	if (p_render_data == nullptr || (Math::is_zero_approx(edge_strength) && Math::is_zero_approx(vignette_strength))) {
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
	if (scratch.is_null() || scratch_size != size) {
		if (scratch.is_valid()) {
			rd->free_rid(scratch);
		}
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		format.width = size.x;
		format.height = size.y;
		format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT;
		scratch = rd->texture_create(format, RD::TextureView());
		scratch_size = size;
	}
	struct PushConstant {
		float size[2];
		float edge_strength;
		float vignette_strength;
	};
	// The color buffer cannot be a copy source, so the same shader first copies
	// it to the scratch texture (strengths 0), then writes the effect back.
	const PushConstant copy_pass = { { float(size.x), float(size.y) }, 0.0f, 0.0f };
	const PushConstant effect_pass = { { float(size.x), float(size.y) }, edge_strength, vignette_strength };

	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		const RID color = buffers->get_internal_texture(view);
		const RID targets[2] = { scratch, color };
		const RID sources[2] = { color, scratch };
		const PushConstant *passes[2] = { &copy_pass, &effect_pass };
		for (int pass = 0; pass < 2; pass++) {
			Vector<RD::Uniform> uniforms;
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, targets[pass]));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, sources[pass]));
			const RID uniform_set = rd->uniform_set_create(uniforms, shader, 0);
			const RD::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, pipeline);
			rd->compute_list_bind_uniform_set(list, uniform_set, 0);
			rd->compute_list_set_push_constant(list, passes[pass], sizeof(PushConstant));
			rd->compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1);
			rd->compute_list_end();
			rd->free_rid(uniform_set);
		}
	}
#endif
}

void UZUPostEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_edge_strength", "strength"), &UZUPostEffect::set_edge_strength);
	ClassDB::bind_method(D_METHOD("get_edge_strength"), &UZUPostEffect::get_edge_strength);
	ClassDB::bind_method(D_METHOD("set_vignette_strength", "strength"), &UZUPostEffect::set_vignette_strength);
	ClassDB::bind_method(D_METHOD("get_vignette_strength"), &UZUPostEffect::get_vignette_strength);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_strength", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_edge_strength", "get_edge_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vignette_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_vignette_strength", "get_vignette_strength");
}

UZUPostEffect::UZUPostEffect() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && get_rid().is_valid()) {
		rs->compositor_effect_set_callback(get_rid(), RSE::COMPOSITOR_EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, callable_mp(this, &UZUPostEffect::_render));
	}
}

UZUPostEffect::~UZUPostEffect() {
#ifdef RD_ENABLED
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd != nullptr) {
		if (scratch.is_valid()) {
			rd->free_rid(scratch);
		}
		if (shader.is_valid()) {
			rd->free_rid(shader); // Also frees the pipeline.
		}
	}
#endif
}
