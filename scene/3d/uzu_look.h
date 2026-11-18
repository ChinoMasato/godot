/**************************************************************************/
/*  uzu_look.h                                                            */
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

#include "core/object/object.h"
#include "core/variant/binder_common.h"
#include "scene/3d/look_preset_3d.h"

class Camera3D;
class Decal;
class GPUParticles3D;
class MeshInstance3D;
class Node3D;
class ShaderMaterial;
class Sprite3D;
class Texture2D;
class CameraAttributesPractical;
class DirectionalLight3D;
class Environment;
class Node;
class OmniLight3D;
class StandardMaterial3D;

// Script access to the U.ZU look presets (scene/3d/look_preset_3d.h), so that
// agents and tool scripts can build and apply them without the editor UI.
// All methods are static: UZULook.create_environment(...), etc.
class UZULook : public Object {
	GDCLASS(UZULook, Object);

protected:
	static void _bind_methods();

public:
	enum Quality {
		QUALITY_HIGH = LookPreset3D::QUALITY_HIGH,
		QUALITY_STANDARD = LookPreset3D::QUALITY_STANDARD,
		QUALITY_LOW = LookPreset3D::QUALITY_LOW,
	};

	enum Atmosphere {
		ATMOSPHERE_CLEAR = LookPreset3D::ATMOSPHERE_CLEAR,
		ATMOSPHERE_HUMID = LookPreset3D::ATMOSPHERE_HUMID,
		ATMOSPHERE_GOLDEN_HOUR = LookPreset3D::ATMOSPHERE_GOLDEN_HOUR,
		ATMOSPHERE_NIGHT = LookPreset3D::ATMOSPHERE_NIGHT,
	};

	enum MaterialKind {
		MATERIAL_SKIN = LookPreset3D::MATERIAL_SKIN,
		MATERIAL_FOLIAGE = LookPreset3D::MATERIAL_FOLIAGE,
		MATERIAL_GLASS = LookPreset3D::MATERIAL_GLASS,
		MATERIAL_WATER = LookPreset3D::MATERIAL_WATER,
		MATERIAL_LAMP = LookPreset3D::MATERIAL_LAMP,
	};

	enum Lens {
		LENS_WIDE = LookPreset3D::LENS_WIDE,
		LENS_STANDARD = LookPreset3D::LENS_STANDARD,
		LENS_TELEPHOTO = LookPreset3D::LENS_TELEPHOTO,
	};

	enum EyeLevel {
		EYE_LEVEL_STANDING = LookPreset3D::EYE_LEVEL_STANDING,
		EYE_LEVEL_LOW = LookPreset3D::EYE_LEVEL_LOW,
	};

	enum ParticleKind {
		PARTICLES_DUST = LookPreset3D::PARTICLES_DUST,
		PARTICLES_FIREFLIES = LookPreset3D::PARTICLES_FIREFLIES,
		PARTICLES_SNOW = LookPreset3D::PARTICLES_SNOW,
		PARTICLES_EMBERS = LookPreset3D::PARTICLES_EMBERS,
	};

	static Quality get_default_quality(const String &p_rendering_method);

	static Ref<Environment> create_environment(Quality p_quality = QUALITY_HIGH, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR, bool p_clouds = false);
	static Ref<CameraAttributesPractical> create_camera_attributes(Quality p_quality = QUALITY_HIGH);
	static void configure_sun(DirectionalLight3D *p_sun, Quality p_quality = QUALITY_HIGH, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
	static void configure_fill_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
	static void configure_rim_light(DirectionalLight3D *p_light, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
	static void configure_lamp(OmniLight3D *p_lamp, Quality p_quality = QUALITY_HIGH);
	static Ref<StandardMaterial3D> create_material(MaterialKind p_kind);
	static void configure_camera(Camera3D *p_camera, Lens p_lens = LENS_STANDARD, float p_shift = 0.0);
	static float get_lens_fov(Lens p_lens);
	static float get_eye_height(EyeLevel p_eye_level);
	static int get_shadow_filter_quality(Quality p_quality);
	static Ref<ShaderMaterial> create_lit_sprite_material(const Ref<Texture2D> &p_texture, const Ref<Texture2D> &p_normal_map, float p_outline_width, const Color &p_outline_color);
	static void configure_lit_sprite(Sprite3D *p_sprite, const Ref<Texture2D> &p_normal_map, float p_outline_width);
	static Decal *add_blob_shadow(Node3D *p_node, float p_radius);
	static Ref<ShaderMaterial> create_wind_foliage_material(float p_strength, float p_speed);
	static GPUParticles3D *create_particles(ParticleKind p_kind, const Vector3 &p_extents);
	static MeshInstance3D *add_god_rays(Node *p_root, const Vector3 &p_position, float p_width, float p_length);
	static Ref<ShaderMaterial> create_water_material();
	// Adds a water surface (B-727) of p_size (x, z) at p_position. p_options sets the
	// material: shallow_color, deep_color, clarity, refraction, wave_strength,
	// wave_scale, wave_speed, wave_height, foam_width, foam_color, reflection,
	// surface_roughness. An unknown option is an error and adds nothing.
	static MeshInstance3D *add_water(Node *p_root, const Vector3 &p_position, const Vector2 &p_size = Vector2(10, 10), const Dictionary &p_options = Dictionary());
	static Dictionary apply_hd2d(Node *p_root, Atmosphere p_atmosphere = ATMOSPHERE_GOLDEN_HOUR, bool p_camera = true);

	// Scene helpers. New nodes are owned by p_root, so they are saved with it.
	// Adds (or updates) a WorldEnvironment, and configures the sun (the first
	// DirectionalLight3D that drives the sky) for the atmosphere, adding one if
	// there is none. Returns what was done, e.g. {"world_environment": "added", "sun": "configured"}.
	static Dictionary apply_to_scene(Node *p_root, Quality p_quality = QUALITY_HIGH, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR, bool p_clouds = false);
	// Adds fill and rim lights (three-point lighting with the sun as the key light).
	static Dictionary add_lighting_rig(Node *p_root, Atmosphere p_atmosphere = ATMOSPHERE_CLEAR);
	// Adds a warm lamp (an OmniLight3D with a small glowing bulb) at p_position.
	static Dictionary add_lamp(Node *p_root, const Vector3 &p_position, Quality p_quality = QUALITY_HIGH);
};

VARIANT_ENUM_CAST(UZULook::Quality);
VARIANT_ENUM_CAST(UZULook::Atmosphere);
VARIANT_ENUM_CAST(UZULook::MaterialKind);
VARIANT_ENUM_CAST(UZULook::Lens);
VARIANT_ENUM_CAST(UZULook::EyeLevel);
VARIANT_ENUM_CAST(UZULook::ParticleKind);
