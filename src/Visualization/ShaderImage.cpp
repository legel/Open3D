// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2015 Qianyi Zhou <Qianyi.Zhou@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "ShaderImage.h"

#include "Shader.h"
#include "ColorMap.h"

namespace three{

namespace glsl {

bool ShaderImageDefault::Compile()
{
	if (CompileShaders(
			ImageVertexShader,
			NULL,
			ImageFragmentShader) == false)
	{
		PrintWarning("[ShaderImageDefault] Compiling shaders failed.\n");
		return false;
	}
	
	vertex_position_ = glGetAttribLocation(program_, "vertex_position");
	vertex_UV_ = glGetAttribLocation(program_, "vertex_UV");
	image_texture_ = glGetUniformLocation(program_, "image_texture");

	return true;
}

void ShaderImageDefault::Release()
{
	UnbindGeometry();
	ReleaseProgram();
}

bool ShaderImageDefault::BindGeometry(
		const Geometry &geometry, 
		const RenderMode &mode,
		const ViewControl &view)
{
	// Sanity check to see if this geometry is worth binding.
	if (geometry.GetGeometryType() != Geometry::GEOMETRY_IMAGE ||
			mode.GetRenderModeType() != RenderMode::RENDERMODE_IMAGE) {
		PrintWarning("[ShaderImageDefault] Binding failed with wrong binding type.\n");
		return false;
	}
	const Image &image = (const Image &)geometry;
	if (image.HasData() == false) {
		PrintWarning("[ShaderImageDefault] Binding failed with empty image.\n");
		return false;
	}
	const auto &image_render_mode = (const ImageRenderMode &)mode;

	// If there is already geometry, we first unbind it.
	// In the default PointCloud render mode, we use GL_STATIC_DRAW. When
	// geometry changes, we clear buffers and rebind the geometry. Note that
	// this approach is slow. If the geomtry is changing per frame, consider
	// implementing a new ShaderWrapper using GL_STREAM_DRAW, and replace
	// UnbindGeometry() with Buffer Object Streaming mechanisms.
	UnbindGeometry();

	// Copy data to renderer's own container. A double-to-float cast is
	// performed for performance reason.
	const void *data_ptr;
	Image render_image;
	const ColorMap &global_color_map = *GetGlobalColorMap();
	if (image.num_of_channels_ == 3 && image.bytes_per_channel_ == 1) {
		data_ptr = image.data_.data();
	} else {
		render_image.PrepareImage(image.width_, image.height_, 3, 1);
		if (image.num_of_channels_ == 1 && 
				image.bytes_per_channel_ == 1) {
			for (int i = 0; i < image.height_ * image.width_; i++) {
				render_image.data_[i * 3] = image.data_[i];
				render_image.data_[i * 3 + 1] = image.data_[i];
				render_image.data_[i * 3 + 2] = image.data_[i];
			}
		} else if (image.num_of_channels_ == 3 && 
				image.bytes_per_channel_ == 2) {
			for (int i = 0; i < image.height_ * image.width_ * 3; i++) {
				uint16_t *p = (uint16_t*)(image.data_.data() + i * 2);
				render_image.data_[i] = (unsigned char)((*p) & 0xff);
			}
		} else if (image.num_of_channels_ == 1 && 
				image.bytes_per_channel_ == 2) {
			const int max_depth = image_render_mode.GetMaxDepth();
			for (int i = 0; i < image.height_ * image.width_; i++) {
				uint16_t *p = (uint16_t*)(image.data_.data() + i * 2);
				double depth = std::min(double(*p) / double(max_depth), 1.0);
				Eigen::Vector3d color = global_color_map.GetColor(depth);
				render_image.data_[i * 3] = (unsigned char)(color(0) * 255);
				render_image.data_[i * 3 + 1] = (unsigned char)(color(1) * 255);
				render_image.data_[i * 3 + 2] = (unsigned char)(color(2) * 255);
			}
		}
		data_ptr = render_image.data_.data();
	}
	
	// Create buffers and bind the geometry
	GLfloat ratio_x, ratio_y;
	switch (image_render_mode.GetImageStretchOption()) {
	case ImageRenderMode::IMAGE_STRETCH_KEEP_RATIO:
		ratio_x = GLfloat(image.width_) / GLfloat(view.GetWindowWidth());
		ratio_y = GLfloat(image.height_) / GLfloat(view.GetWindowHeight());
		if (ratio_x < ratio_y) {
			ratio_x /= ratio_y;
			ratio_y = 1.0f;
		} else {
			ratio_y /= ratio_x;
			ratio_x = 1.0f;
		}
		break;
	case ImageRenderMode::IMAGE_STRETCH_WITH_WINDOW:
		ratio_x = 1.0f;
		ratio_y = 1.0f;
		break;
	case ImageRenderMode::IMAGE_ORIGINAL_SIZE:
	default:
		ratio_x = GLfloat(image.width_) / GLfloat(view.GetWindowWidth());
		ratio_y = GLfloat(image.height_) / GLfloat(view.GetWindowHeight());
		break;
	}
	const GLfloat vertex_position_buffer_data[18] = {
		-ratio_x, -ratio_y, 0.0f,
		ratio_x, -ratio_y, 0.0f,
		ratio_x, ratio_y, 0.0f,
		-ratio_x, -ratio_y, 0.0f,
		ratio_x, ratio_y, 0.0f,
		-ratio_x, ratio_y, 0.0f,
	};
	const GLfloat vertex_UV_buffer_data[12] = {
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 0.0f,
		0.0f, 0.0f,
	};
	glGenBuffers(1, &vertex_position_buffer_);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_position_buffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_position_buffer_data), 
			vertex_position_buffer_data, GL_STATIC_DRAW);
	glGenBuffers(1, &vertex_UV_buffer_);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_UV_buffer_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_UV_buffer_data),
			vertex_UV_buffer_data, GL_STATIC_DRAW);

	glGenTextures(1, &image_texture_buffer_);
	glBindTexture(GL_TEXTURE_2D, image_texture_buffer_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width_, image.height_, 0, 
			GL_RGB, GL_UNSIGNED_BYTE, data_ptr);

	if (image_render_mode.GetInterpolationOption() == 
			ImageRenderMode::IMAGE_INTERPOLATION_NEAREST) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); 
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	bound_ = true;	
	return true;
}

void ShaderImageDefault::UnbindGeometry()
{
	if (bound_) {
		glDeleteBuffers(1, &vertex_position_buffer_);
		glDeleteBuffers(1, &vertex_UV_buffer_);
		glDeleteTextures(1, &image_texture_buffer_);
		bound_ = false;
	}
}

bool ShaderImageDefault::Render(
		const RenderMode &mode, 
		const ViewControl &view)
{
	if (compiled_ == false || bound_ == false ||
			mode.GetRenderModeType() != RenderMode::RENDERMODE_IMAGE) {
		return false;
	}
	
	const auto &rendermode = (const ImageRenderMode &)mode;
	glPointSize(10.0f);

	glUseProgram(program_);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, image_texture_buffer_);
	glUniform1i(image_texture_, 0);
	glEnableVertexAttribArray(vertex_position_);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_position_buffer_);
	glVertexAttribPointer(vertex_position_, 3, GL_FLOAT, GL_FALSE, 0, NULL);
	glEnableVertexAttribArray(vertex_UV_);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_UV_buffer_);
	glVertexAttribPointer(vertex_UV_, 2, GL_FLOAT, GL_FALSE, 0, NULL);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(vertex_position_);
	glDisableVertexAttribArray(vertex_UV_);
	
	return true;
}

}

}	// namespace three
