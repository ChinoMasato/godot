/**************************************************************************/
/*  uzu_picture.h                                                         */
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

#include "core/io/image.h"
#include "scene/3d/node_3d.h"

class Camera3D;
class DirectionalLight3D;
class OmniLight3D;
class ShaderMaterial;
class ImageTexture;
class ArrayMesh;

// U.ZU Picture (BACKLOG B-901, P-1): turns one picture into a living 3D scene from a
// spec (a Dictionary, or a JSON file). The picture becomes a relief mesh from its depth
// map, torn where the depth jumps, with a backdrop behind it whose near things are
// painted out (pull-push fill), so near and far slide apart as layers. It is lit as a
// PBR surface by real lights placed at picture pixels, and rain, embers, sheets of fog,
// clouds and lightning live in the same space, while a slow camera drift shows the
// parallax. Every position in the spec is a picture pixel (x right, y down), so an
// agent can look at the picture, name points and regions, render and adjust.
// The spec is the one of the prototype (ai_loop/prototypes/picture/README.md).
class UZUPicture : public Node3D {
	GDCLASS(UZUPicture, Node3D);

	Dictionary spec;
	String spec_path;
	Dictionary stats;
	bool built = false;

	Vector2i image_size;
	float tan_half = 0.0;
	float aspect = 1.0;
	Ref<Image> depth;
	float depth_a = 0.0;
	float depth_b = 0.0;
	bool disparity = true;
	float near_z = 4.5;
	float far_z = 160.0;
	float time = 0.0;
	Vector<Ref<ShaderMaterial>> relief_materials;
	Vector<Ref<ShaderMaterial>> fog_materials;
	struct LightState {
		OmniLight3D *node = nullptr;
		float energy = 1.0;
		float flicker = 0.0;
		float seed = 0.0;
	};
	Vector<LightState> lights;
	Camera3D *camera = nullptr;
	DirectionalLight3D *bolt = nullptr;
	float flash_at = -1.0;
	uint64_t flash_seed = 1;
	Ref<ImageTexture> noise;

	void _clear();
	void _build_relief(const Ref<Image> &p_art, const String &p_quality);
	Ref<ArrayMesh> _grid_mesh(const Vector<float> &p_d, int p_sx, int p_sy, float p_tear, float p_push);
	void _set_regions(const Ref<ShaderMaterial> &p_material);
	void _set_clouds(const Ref<ShaderMaterial> &p_material);
	void _build_lights();
	void _build_effects(const String &p_quality);
	void _build_camera(float p_fov);
	void _build_look();
	void _update_lightning();
	void _add_built(Node *p_node);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_spec(const Dictionary &p_spec);
	Dictionary get_spec() const;
	void set_spec_path(const String &p_path);
	String get_spec_path() const;

	// Builds (or rebuilds) the scene from the spec; returns the stats (also get_stats()),
	// with "ok" false and "error" when the spec cannot be used.
	Dictionary build();
	Dictionary get_stats() const;
	bool is_built() const;

	// A picture pixel and a depth value 0..1 -> the point in front of the camera
	// (the camera at the origin, looking down -Z).
	Vector3 place(const Vector2 &p_pixel, float p_depth) const;
	// The depth value (0..1, bilinear) at a picture pixel, and the point there.
	float depth_at(const Vector2 &p_pixel) const;
	Vector3 point_at(const Vector2 &p_pixel) const;

	// The building blocks, public for tests and tools: the smallest value in a square of
	// radius p_r around each cell (the farthest depth nearby), and the picture with the
	// cells where p_d is clearly nearer than p_back_d painted out and filled from around
	// (pull-push), at half size.
	static Vector<float> min_filter(const Vector<float> &p_src, int p_w, int p_h, int p_r);
	static Ref<Image> fill_behind(const Ref<Image> &p_art, const Vector<float> &p_d, const Vector<float> &p_back_d, int p_sx, int p_sy, float *r_filled_share = nullptr);
	static Ref<Image> load_image(const String &p_path);

	UZUPicture();
};
