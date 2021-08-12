/**
 * Copyright (c) 2006-2021 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

// LOVE
#include "common/config.h"
#include "common/math.h"
#include "common/Vector.h"

#include "Graphics.h"
#include "font/Font.h"
#include "StreamBuffer.h"
#include "math/MathModule.h"
#include "window/Window.h"
#include "Buffer.h"
#include "ShaderStage.h"

#include "libraries/xxHash/xxhash.h"

// C++
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>

// C
#include <cmath>
#include <cstdio>

#ifdef LOVE_IOS
#include <SDL_syswm.h>
#endif

namespace love
{
namespace graphics
{
namespace opengl
{

static GLenum getGLBlendOperation(BlendOperation op)
{
	switch (op)
	{
		case BLENDOP_ADD: return GL_FUNC_ADD;
		case BLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
		case BLENDOP_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
		case BLENDOP_MIN: return GL_MIN;
		case BLENDOP_MAX: return GL_MAX;
		case BLENDOP_MAX_ENUM: return 0;
	}
	return 0;
}

static GLenum getGLBlendFactor(BlendFactor factor)
{
	switch (factor)
	{
		case BLENDFACTOR_ZERO: return GL_ZERO;
		case BLENDFACTOR_ONE: return GL_ONE;
		case BLENDFACTOR_SRC_COLOR: return GL_SRC_COLOR;
		case BLENDFACTOR_ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
		case BLENDFACTOR_SRC_ALPHA: return GL_SRC_ALPHA;
		case BLENDFACTOR_ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
		case BLENDFACTOR_DST_COLOR: return GL_DST_COLOR;
		case BLENDFACTOR_ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
		case BLENDFACTOR_DST_ALPHA: return GL_DST_ALPHA;
		case BLENDFACTOR_ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
		case BLENDFACTOR_SRC_ALPHA_SATURATED: return GL_SRC_ALPHA_SATURATE;
		case BLENDFACTOR_MAX_ENUM: return 0;
	}
	return 0;
}

Graphics::Graphics()
	: windowHasStencil(false)
	, mainVAO(0)
	, internalBackbufferFBO(0)
	, requestedBackbufferMSAA(0)
	, bufferMapMemory(nullptr)
	, bufferMapMemorySize(2 * 1024 * 1024)
	, defaultBuffers()
	, supportedFormats()
{
	gl = OpenGL();

	try
	{
		bufferMapMemory = new char[bufferMapMemorySize];
	}
	catch (std::exception &)
	{
		// Handled in getBufferMapMemory.
	}

	auto window = getInstance<love::window::Window>(M_WINDOW);

	if (window != nullptr)
	{
		window->setGraphics(this);

		if (window->isOpen())
		{
			int w, h;
			love::window::WindowSettings s;
			window->getWindow(w, h, s);

			double dpiW = w;
			double dpiH = h;
			window->windowToDPICoords(&dpiW, &dpiH);

			setMode((int) dpiW, (int) dpiH, window->getPixelWidth(), window->getPixelHeight(), s.stencil, s.msaa);
		}
	}
}

Graphics::~Graphics()
{
	delete[] bufferMapMemory;
}

const char *Graphics::getName() const
{
	return "love.graphics.opengl";
}

love::graphics::StreamBuffer *Graphics::newStreamBuffer(BufferUsage type, size_t size)
{
	return CreateStreamBuffer(type, size);
}

love::graphics::Texture *Graphics::newTexture(const Texture::Settings &settings, const Texture::Slices *data)
{
	return new Texture(settings, data);
}

love::graphics::ShaderStage *Graphics::newShaderStageInternal(ShaderStageType stage, const std::string &cachekey, const std::string &source, bool gles)
{
	return new ShaderStage(this, stage, source, gles, cachekey);
}

love::graphics::Shader *Graphics::newShaderInternal(StrongRef<love::graphics::ShaderStage> stages[SHADERSTAGE_MAX_ENUM])
{
	return new Shader(stages);
}

love::graphics::Buffer *Graphics::newBuffer(const Buffer::Settings &settings, const std::vector<Buffer::DataDeclaration> &format, const void *data, size_t size, size_t arraylength)
{
	return new Buffer(this, settings, format, data, size, arraylength);
}

void Graphics::setViewportSize(int width, int height, int pixelwidth, int pixelheight)
{
	this->width = width;
	this->height = height;
	this->pixelWidth = pixelwidth;
	this->pixelHeight = pixelheight;

	if (!isRenderTargetActive())
	{
		// Set the viewport to top-left corner.
		gl.setViewport({0, 0, pixelwidth, pixelheight});

		// Re-apply the scissor if it was active, since the rectangle passed to
		// glScissor is affected by the viewport dimensions.
		if (states.back().scissor)
			setScissor(states.back().scissorRect);

		// Set up the projection matrix
		projectionMatrix = Matrix4::ortho(0.0, (float) width, (float) height, 0.0, -10.0f, 10.0f);
	}

	updateBackbuffer(width, height, pixelwidth, pixelheight, requestedBackbufferMSAA);
}

void Graphics::updateBackbuffer(int width, int height, int /*pixelwidth*/, int pixelheight, int msaa)
{
	bool useinternalbackbuffer = false;
	if (msaa > 1)
		useinternalbackbuffer = true;

	// Our internal backbuffer code needs glBlitFramebuffer.
	if (!(GLAD_VERSION_3_0 || GLAD_ARB_framebuffer_object || GLAD_ES_VERSION_3_0
		  || GLAD_EXT_framebuffer_blit || GLAD_ANGLE_framebuffer_blit || GLAD_NV_framebuffer_blit))
	{
		if (!(msaa > 1 && GLAD_APPLE_framebuffer_multisample))
			useinternalbackbuffer = false;
	}

	GLuint prevFBO = gl.getFramebuffer(OpenGL::FRAMEBUFFER_ALL);
	bool restoreFBO = prevFBO != getInternalBackbufferFBO();

	if (useinternalbackbuffer)
	{
		Texture::Settings settings;
		settings.width = width;
		settings.height = height;
		settings.dpiScale = (float)pixelheight / (float)height;
		settings.msaa = msaa;
		settings.renderTarget = true;
		settings.readable.set(false);

		settings.format = isGammaCorrect() ? PIXELFORMAT_sRGBA8_UNORM : PIXELFORMAT_RGBA8_UNORM;
		internalBackbuffer.set(newTexture(settings), Acquire::NORETAIN);

		settings.format = PIXELFORMAT_DEPTH24_UNORM_STENCIL8;
		internalBackbufferDepthStencil.set(newTexture(settings), Acquire::NORETAIN);

		RenderTargets rts;
		rts.colors.push_back(internalBackbuffer.get());
		rts.depthStencil.texture = internalBackbufferDepthStencil;

		internalBackbufferFBO = bindCachedFBO(rts);
	}
	else
	{
		internalBackbuffer.set(nullptr);
		internalBackbufferDepthStencil.set(nullptr);
		internalBackbufferFBO = 0;
	}

	requestedBackbufferMSAA = msaa;

	if (restoreFBO)
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, prevFBO);
}

GLuint Graphics::getInternalBackbufferFBO() const
{
	if (internalBackbufferFBO != 0)
		return internalBackbufferFBO;
	else
		return getSystemBackbufferFBO();
}

GLuint Graphics::getSystemBackbufferFBO() const
{
#ifdef LOVE_IOS
	// Hack: iOS uses a custom FBO.
	SDL_SysWMinfo info = {};
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(SDL_GL_GetCurrentWindow(), &info);

	if (info.info.uikit.resolveFramebuffer != 0)
		return info.info.uikit.resolveFramebuffer;
	else
		return info.info.uikit.framebuffer;
#else
	return 0;
#endif
}

bool Graphics::setMode(int width, int height, int pixelwidth, int pixelheight, bool windowhasstencil, int msaa)
{
	this->width = width;
	this->height = height;

	this->windowHasStencil = windowhasstencil;
	this->requestedBackbufferMSAA = msaa;

	// Okay, setup OpenGL.
	gl.initContext();

	if (gl.isCoreProfile())
	{
		glGenVertexArrays(1, &mainVAO);
		glBindVertexArray(mainVAO);
	}

	gl.setupContext();

	created = true;
	initCapabilities();

	// Enable blending
	gl.setEnableState(OpenGL::ENABLE_BLEND, true);

	// Auto-generated mipmaps should be the best quality possible
	if (!gl.isCoreProfile())
		glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);

	if (!GLAD_ES_VERSION_2_0 && !gl.isCoreProfile())
	{
		// Make sure antialiasing works when set elsewhere
		glEnable(GL_MULTISAMPLE);

		// Enable texturing
		glEnable(GL_TEXTURE_2D);
	}

	if (!GLAD_ES_VERSION_2_0)
		glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

	gl.setTextureUnit(0);

	// Set pixel row alignment - code that calls glTexSubImage and glReadPixels
	// assumes there's no row alignment, but OpenGL defaults to 4 bytes.
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	// Always enable seamless cubemap filtering when possible.
	if (GLAD_VERSION_3_2 || GLAD_ARB_seamless_cube_map)
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	// Set whether drawing converts input from linear -> sRGB colorspace.
	if (!gl.bugs.brokenSRGB && (GLAD_VERSION_3_0 || GLAD_ARB_framebuffer_sRGB
		|| GLAD_EXT_framebuffer_sRGB || GLAD_ES_VERSION_3_0))
	{
		if (GLAD_VERSION_1_0 || GLAD_EXT_sRGB_write_control)
			gl.setEnableState(OpenGL::ENABLE_FRAMEBUFFER_SRGB, isGammaCorrect());
	}
	else
		setGammaCorrect(false);

	setDebug(isDebugEnabled());

	setViewportSize(width, height, pixelwidth, pixelheight);

	if (batchedDrawState.vb[0] == nullptr)
	{
		// Initial sizes that should be good enough for most cases. It will
		// resize to fit if needed, later.
		batchedDrawState.vb[0] = CreateStreamBuffer(BUFFERUSAGE_VERTEX, 1024 * 1024 * 1);
		batchedDrawState.vb[1] = CreateStreamBuffer(BUFFERUSAGE_VERTEX, 256  * 1024 * 1);
		batchedDrawState.indexBuffer = CreateStreamBuffer(BUFFERUSAGE_INDEX, sizeof(uint16) * LOVE_UINT16_MAX);
	}

	// TODO: one buffer each for float, int, uint
	if (capabilities.features[FEATURE_TEXEL_BUFFER] && defaultBuffers[BUFFERUSAGE_TEXEL].get() == nullptr)
	{
		Buffer::Settings settings(BUFFERUSAGEFLAG_TEXEL, BUFFERDATAUSAGE_STATIC);
		std::vector<Buffer::DataDeclaration> format = {{"", DATAFORMAT_FLOAT_VEC4, 0}};

		const float texel[] = {0.0f, 0.0f, 0.0f, 1.0f};

		auto buffer = newBuffer(settings, format, texel, sizeof(texel), 1);
		defaultBuffers[BUFFERUSAGE_TEXEL].set(buffer, Acquire::NORETAIN);
	}

	if (capabilities.features[FEATURE_GLSL4] && defaultBuffers[BUFFERUSAGE_SHADER_STORAGE].get() == nullptr)
	{
		Buffer::Settings settings(BUFFERUSAGEFLAG_SHADER_STORAGE, BUFFERDATAUSAGE_STATIC);
		std::vector<Buffer::DataDeclaration> format = {{"", DATAFORMAT_FLOAT, 0}};

		std::vector<float> data;
		data.resize(Buffer::SHADER_STORAGE_BUFFER_MAX_STRIDE / 4);

		auto buffer = newBuffer(settings, format, data.data(), data.size() * sizeof(float), data.size());
		defaultBuffers[BUFFERUSAGE_SHADER_STORAGE].set(buffer, Acquire::NORETAIN);
	}

	// Load default resources before other Volatile.
	for (int i = 0; i < BUFFERUSAGE_MAX_ENUM; i++)
	{
		if (defaultBuffers[i].get())
			((Buffer *) defaultBuffers[i].get())->loadVolatile();
	}

	if (defaultBuffers[BUFFERUSAGE_TEXEL].get())
		gl.setDefaultTexelBuffer((GLuint) defaultBuffers[BUFFERUSAGE_TEXEL]->getTexelBufferHandle());

	if (defaultBuffers[BUFFERUSAGE_SHADER_STORAGE].get())
		gl.setDefaultStorageBuffer((GLuint) defaultBuffers[BUFFERUSAGE_SHADER_STORAGE]->getHandle());

	// Reload all volatile objects.
	if (!Volatile::loadAll())
		::printf("Could not reload all volatile objects.\n");

	createQuadIndexBuffer();

	// Restore the graphics state.
	restoreState(states.back());

	// We always need a default shader.
	for (int i = 0; i < Shader::STANDARD_MAX_ENUM; i++)
	{
		auto stype = (Shader::StandardShader) i;

		if (i == Shader::STANDARD_ARRAY && !capabilities.textureTypes[TEXTURE_2D_ARRAY])
			continue;

		// Apparently some intel GMA drivers on windows fail to compile shaders
		// which use array textures despite claiming support for the extension.
		try
		{
			if (!Shader::standardShaders[i])
			{
				std::vector<std::string> stages;
				stages.push_back(Shader::getDefaultCode(stype, SHADERSTAGE_VERTEX));
				stages.push_back(Shader::getDefaultCode(stype, SHADERSTAGE_PIXEL));
				Shader::standardShaders[i] = newShader(stages);
			}
		}
		catch (love::Exception &)
		{
			if (i == Shader::STANDARD_ARRAY)
				capabilities.textureTypes[TEXTURE_2D_ARRAY] = false;
			else
				throw;
		}
	}

	// A shader should always be active, but the default shader shouldn't be
	// returned by getShader(), so we don't do setShader(defaultShader).
	if (!Shader::current)
		Shader::standardShaders[Shader::STANDARD_DEFAULT]->attach();

	return true;
}

void Graphics::unSetMode()
{
	if (!isCreated())
		return;

	flushBatchedDraws();

	internalBackbuffer.set(nullptr);
	internalBackbufferDepthStencil.set(nullptr);

	// Unload all volatile objects. These must be reloaded after the display
	// mode change.
	Volatile::unloadAll();

	for (const auto &pair : framebufferObjects)
		gl.deleteFramebuffer(pair.second);

	for (auto temp : temporaryTextures)
		temp.texture->release();

	framebufferObjects.clear();
	temporaryTextures.clear();

	if (mainVAO != 0)
	{
		glDeleteVertexArrays(1, &mainVAO);
		mainVAO = 0;
	}

	gl.deInitContext();

	created = false;
}

void Graphics::setActive(bool enable)
{
	flushBatchedDraws();

	// Make sure all pending OpenGL commands have fully executed before
	// returning, when going from active to inactive. This is required on iOS.
	if (isCreated() && this->active && !enable)
		glFinish();

	active = enable;
}

static bool computeDispatchBarriers(Shader *shader, GLbitfield &preDispatchBarriers, GLbitfield &postDispatchBarriers)
{
	// TODO: handle indirect argument buffer types, when those are added.
	for (auto buffer : shader->getActiveWritableStorageBuffers())
	{
		if (buffer == nullptr)
			return false;

		auto usage = buffer->getUsageFlags();

		postDispatchBarriers |= GL_BUFFER_UPDATE_BARRIER_BIT;

		if (usage & BUFFERUSAGEFLAG_SHADER_STORAGE)
		{
			preDispatchBarriers |= GL_SHADER_STORAGE_BARRIER_BIT;
			postDispatchBarriers |= GL_SHADER_STORAGE_BARRIER_BIT;
		}

		if (usage & BUFFERUSAGEFLAG_TEXEL)
			postDispatchBarriers |= GL_TEXTURE_FETCH_BARRIER_BIT;

		if (usage & BUFFERUSAGEFLAG_INDEX)
			postDispatchBarriers |= GL_ELEMENT_ARRAY_BARRIER_BIT;

		if (usage & BUFFERUSAGEFLAG_VERTEX)
			postDispatchBarriers |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;

		if (usage & (BUFFERUSAGEFLAG_COPY_SOURCE | BUFFERUSAGEFLAG_COPY_DEST))
			postDispatchBarriers |= GL_PIXEL_BUFFER_BARRIER_BIT;
	}

	for (auto texture : shader->getActiveWritableTextures())
	{
		if (texture == nullptr)
			return false;

		preDispatchBarriers |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

		postDispatchBarriers |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
			| GL_TEXTURE_UPDATE_BARRIER_BIT
			| GL_TEXTURE_FETCH_BARRIER_BIT;

		if (texture->isRenderTarget())
			postDispatchBarriers |= GL_FRAMEBUFFER_BARRIER_BIT;
	}

	return true;
}

bool Graphics::dispatch(int x, int y, int z)
{
	// Set by higher level code before calling dispatch(x, y, z).
	auto shader = (Shader *) Shader::current;

	GLbitfield preDispatchBarriers = 0;
	GLbitfield postDispatchBarriers = 0;

	if (!computeDispatchBarriers(shader, preDispatchBarriers, postDispatchBarriers))
		return false;

	// glMemoryBarrier before dispatch to make sure non-compute-read ->
	// compute-write is synced.
	// TODO: is this needed? spec language around GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
	// makes me think so.
	// This is overly conservative (dispatch -> dispatch will have redundant
	// barriers).
	if (preDispatchBarriers != 0)
		glMemoryBarrier(preDispatchBarriers);

	glDispatchCompute(x, y, z);

	// Not as (theoretically) efficient as issuing the barrier right before
	// they're used later, but much less complicated.
	if (postDispatchBarriers != 0)
		glMemoryBarrier(postDispatchBarriers);

	return true;
}

void Graphics::draw(const DrawCommand &cmd)
{
	gl.prepareDraw(this);
	gl.setVertexAttributes(*cmd.attributes, *cmd.buffers);
	gl.bindTextureToUnit(cmd.texture, 0, false);
	gl.setCullMode(cmd.cullMode);

	GLenum glprimitivetype = OpenGL::getGLPrimitiveType(cmd.primitiveType);

	if (cmd.instanceCount > 1)
		glDrawArraysInstanced(glprimitivetype, cmd.vertexStart, cmd.vertexCount, cmd.instanceCount);
	else
		glDrawArrays(glprimitivetype, cmd.vertexStart, cmd.vertexCount);

	++drawCalls;
}

void Graphics::draw(const DrawIndexedCommand &cmd)
{
	gl.prepareDraw(this);
	gl.setVertexAttributes(*cmd.attributes, *cmd.buffers);
	gl.bindTextureToUnit(cmd.texture, 0, false);
	gl.setCullMode(cmd.cullMode);

	const void *gloffset = BUFFER_OFFSET(cmd.indexBufferOffset);
	GLenum glprimitivetype = OpenGL::getGLPrimitiveType(cmd.primitiveType);
	GLenum gldatatype = OpenGL::getGLIndexDataType(cmd.indexType);

	gl.bindBuffer(BUFFERUSAGE_INDEX, cmd.indexBuffer->getHandle());

	if (cmd.instanceCount > 1)
		glDrawElementsInstanced(glprimitivetype, cmd.indexCount, gldatatype, gloffset, cmd.instanceCount);
	else
		glDrawElements(glprimitivetype, cmd.indexCount, gldatatype, gloffset);

	++drawCalls;
}

static inline void advanceVertexOffsets(const VertexAttributes &attributes, BufferBindings &buffers, int vertexcount)
{
	// TODO: Figure out a better way to avoid touching the same buffer multiple
	// times, if multiple attributes share the buffer.
	uint32 touchedbuffers = 0;

	for (unsigned int i = 0; i < VertexAttributes::MAX; i++)
	{
		if (!attributes.isEnabled(i))
			continue;

		auto &attrib = attributes.attribs[i];

		uint32 bufferbit = 1u << attrib.bufferIndex;
		if ((touchedbuffers & bufferbit) == 0)
		{
			touchedbuffers |= bufferbit;
			const auto &layout = attributes.bufferLayouts[attrib.bufferIndex];
			buffers.info[attrib.bufferIndex].offset += layout.stride * vertexcount;
		}
	}
}

void Graphics::drawQuads(int start, int count, const VertexAttributes &attributes, const BufferBindings &buffers, love::graphics::Texture *texture)
{
	const int MAX_VERTICES_PER_DRAW = LOVE_UINT16_MAX;
	const int MAX_QUADS_PER_DRAW    = MAX_VERTICES_PER_DRAW / 4;

	gl.prepareDraw(this);
	gl.bindTextureToUnit(texture, 0, false);
	gl.setCullMode(CULL_NONE);

	gl.bindBuffer(BUFFERUSAGE_INDEX, quadIndexBuffer->getHandle());

	if (gl.isBaseVertexSupported())
	{
		gl.setVertexAttributes(attributes, buffers);

		int basevertex = start * 4;

		for (int quadindex = 0; quadindex < count; quadindex += MAX_QUADS_PER_DRAW)
		{
			int quadcount = std::min(MAX_QUADS_PER_DRAW, count - quadindex);

			glDrawElementsBaseVertex(GL_TRIANGLES, quadcount * 6, GL_UNSIGNED_SHORT, BUFFER_OFFSET(0), basevertex);
			++drawCalls;

			basevertex += quadcount * 4;
		}
	}
	else
	{
		BufferBindings bufferscopy = buffers;
		if (start > 0)
			advanceVertexOffsets(attributes, bufferscopy, start * 4);

		for (int quadindex = 0; quadindex < count; quadindex += MAX_QUADS_PER_DRAW)
		{
			gl.setVertexAttributes(attributes, bufferscopy);

			int quadcount = std::min(MAX_QUADS_PER_DRAW, count - quadindex);

			glDrawElements(GL_TRIANGLES, quadcount * 6, GL_UNSIGNED_SHORT, BUFFER_OFFSET(0));
			++drawCalls;

			if (count > MAX_QUADS_PER_DRAW)
				advanceVertexOffsets(attributes, bufferscopy, quadcount * 4);
		}
	}
}

static void APIENTRY debugCB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei /*len*/, const GLchar *msg, const GLvoid* /*usr*/)
{
	// Human-readable strings for the debug info.
	const char *sourceStr = OpenGL::debugSourceString(source);
	const char *typeStr = OpenGL::debugTypeString(type);
	const char *severityStr = OpenGL::debugSeverityString(severity);

	const char *fmt = "OpenGL: %s [source=%s, type=%s, severity=%s, id=%d]\n";
	printf(fmt, msg, sourceStr, typeStr, severityStr, id);
}

void Graphics::setDebug(bool enable)
{
	// Make sure debug output is supported. The AMD ext. is a bit different
	// so we don't make use of it, since AMD drivers now support KHR_debug.
	if (!(GLAD_VERSION_4_3 || GLAD_KHR_debug || GLAD_ARB_debug_output))
		return;

	// TODO: We don't support GL_KHR_debug in GLES yet.
	if (GLAD_ES_VERSION_2_0)
		return;

	// Ugly hack to reduce code duplication.
	if (GLAD_ARB_debug_output && !(GLAD_VERSION_4_3 || GLAD_KHR_debug))
	{
		fp_glDebugMessageCallback = (pfn_glDebugMessageCallback) fp_glDebugMessageCallbackARB;
		fp_glDebugMessageControl = (pfn_glDebugMessageControl) fp_glDebugMessageControlARB;
	}

	if (!enable)
	{
		// Disable the debug callback function.
		glDebugMessageCallback(nullptr, nullptr);

		// We can disable debug output entirely with KHR_debug.
		if (GLAD_VERSION_4_3 || GLAD_KHR_debug)
			glDisable(GL_DEBUG_OUTPUT);

		return;
	}

	// We don't want asynchronous debug output.
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	glDebugMessageCallback(debugCB, nullptr);

	// Initially, enable everything.
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);

	// Disable messages about deprecated OpenGL functionality.
	glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_FALSE);
	glDebugMessageControl(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR, GL_DONT_CARE, 0, 0, GL_FALSE);

	if (GLAD_VERSION_4_3 || GLAD_KHR_debug)
		glEnable(GL_DEBUG_OUTPUT);

	::printf("OpenGL debug output enabled (LOVE_GRAPHICS_DEBUG=1)\n");
}

void Graphics::setRenderTargetsInternal(const RenderTargets &rts, int w, int h, int pixelw, int pixelh, bool hasSRGBtexture)
{
	const DisplayState &state = states.back();

	OpenGL::TempDebugGroup debuggroup("setRenderTargets");

	flushBatchedDraws();
	endPass();

	bool iswindow = rts.getFirstTarget().texture == nullptr;
	Winding vertexwinding = state.winding;

	if (iswindow)
	{
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, getInternalBackbufferFBO());

		// The projection matrix is flipped compared to rendering to a texture,
		// due to OpenGL considering (0,0) bottom-left instead of top-left.
		projectionMatrix = Matrix4::ortho(0.0, (float) w, (float) h, 0.0, -10.0f, 10.0f);
	}
	else
	{
		bindCachedFBO(rts);

		projectionMatrix = Matrix4::ortho(0.0, (float) w, 0.0, (float) h, -10.0f, 10.0f);

		// Flip front face winding when rendering to a texture, since our
		// projection matrix is flipped.
		vertexwinding = vertexwinding == WINDING_CW ? WINDING_CCW : WINDING_CW;
	}

	glFrontFace(vertexwinding == WINDING_CW ? GL_CW : GL_CCW);

	gl.setViewport({0, 0, pixelw, pixelh});

	// Re-apply the scissor if it was active, since the rectangle passed to
	// glScissor is affected by the viewport dimensions.
	if (state.scissor)
		setScissor(state.scissorRect);

	// Make sure the correct sRGB setting is used when drawing to the textures.
	if (GLAD_VERSION_1_0 || GLAD_EXT_sRGB_write_control)
	{
		if (hasSRGBtexture != gl.isStateEnabled(OpenGL::ENABLE_FRAMEBUFFER_SRGB))
			gl.setEnableState(OpenGL::ENABLE_FRAMEBUFFER_SRGB, hasSRGBtexture);
	}
}

void Graphics::endPass()
{
	auto &rts = states.back().renderTargets;
	love::graphics::Texture *depthstencil = rts.depthStencil.texture.get();

	// Discard the depth/stencil buffer if we're using an internal cached one.
	if (depthstencil == nullptr && (rts.temporaryRTFlags & (TEMPORARY_RT_DEPTH | TEMPORARY_RT_STENCIL)) != 0)
		discard({}, true);
	else if (!rts.getFirstTarget().texture.get())
		discard({}, true); // Backbuffer

	// Resolve MSAA buffers. MSAA is only supported for 2D render targets so we
	// don't have to worry about resolving to slices.
	if (rts.colors.size() > 0 && rts.colors[0].texture->getMSAA() > 1)
	{
		int mip = rts.colors[0].mipmap;
		int w = rts.colors[0].texture->getPixelWidth(mip);
		int h = rts.colors[0].texture->getPixelHeight(mip);

		for (int i = 0; i < (int) rts.colors.size(); i++)
		{
			Texture *c = (Texture *) rts.colors[i].texture.get();

			if (!c->isReadable())
				continue;

			glReadBuffer(GL_COLOR_ATTACHMENT0 + i);

			gl.bindFramebuffer(OpenGL::FRAMEBUFFER_DRAW, c->getFBO());

			if (GLAD_APPLE_framebuffer_multisample)
				glResolveMultisampleFramebufferAPPLE();
			else
				glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
	}

	if (depthstencil != nullptr && depthstencil->getMSAA() > 1 && depthstencil->isReadable())
	{
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_DRAW, ((Texture *) depthstencil)->getFBO());

		if (GLAD_APPLE_framebuffer_multisample)
			glResolveMultisampleFramebufferAPPLE();
		else
		{
			int mip = rts.depthStencil.mipmap;
			int w = depthstencil->getPixelWidth(mip);
			int h = depthstencil->getPixelHeight(mip);
			PixelFormat format = depthstencil->getPixelFormat();

			GLbitfield mask = 0;

			if (isPixelFormatDepth(format))
				mask |= GL_DEPTH_BUFFER_BIT;
			if (isPixelFormatStencil(format))
				mask |= GL_STENCIL_BUFFER_BIT;

			if (mask != 0)
				glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, mask, GL_NEAREST);
		}
	}

	// generateMipmaps can't be used for depth/stencil textures.
	for (const auto &rt : rts.colors)
	{
		if (rt.texture->getMipmapsMode() == Texture::MIPMAPS_AUTO && rt.mipmap == 0)
			rt.texture->generateMipmaps();
	}
}

void Graphics::clear(OptionalColorf c, OptionalInt stencil, OptionalDouble depth)
{
	if (c.hasValue || stencil.hasValue || depth.hasValue)
		flushBatchedDraws();

	GLbitfield flags = 0;

	if (c.hasValue)
	{
		gammaCorrectColor(c.value);
		glClearColor(c.value.r, c.value.g, c.value.b, c.value.a);
		flags |= GL_COLOR_BUFFER_BIT;
	}

	if (stencil.hasValue)
	{
		glClearStencil(stencil.value);
		flags |= GL_STENCIL_BUFFER_BIT;
	}

	bool hadDepthWrites = gl.hasDepthWrites();

	if (depth.hasValue)
	{
		if (!hadDepthWrites) // glDepthMask also affects glClear.
			gl.setDepthWrites(true);

		gl.clearDepth(depth.value);
		flags |= GL_DEPTH_BUFFER_BIT;
	}

	if (flags != 0)
		glClear(flags);

	if (depth.hasValue && !hadDepthWrites)
		gl.setDepthWrites(hadDepthWrites);

	if (c.hasValue && gl.bugs.clearRequiresDriverTextureStateUpdate && Shader::current)
	{
		// This seems to be enough to fix the bug for me. Other methods I've
		// tried (e.g. dummy draws) don't work in all cases.
		gl.useProgram(0);
		gl.useProgram((GLuint) Shader::current->getHandle());
	}
}

void Graphics::clear(const std::vector<OptionalColorf> &colors, OptionalInt stencil, OptionalDouble depth)
{
	if (colors.size() == 0 && !stencil.hasValue && !depth.hasValue)
		return;

	int ncolorRTs = (int) states.back().renderTargets.colors.size();
	int ncolors = (int) colors.size();

	if (ncolors <= 1 && ncolorRTs <= 1)
	{
		clear(ncolors > 0 ? colors[0] : OptionalColorf(), stencil, depth);
		return;
	}

	flushBatchedDraws();

	bool drawbuffersmodified = false;
	ncolors = std::min(ncolors, ncolorRTs);

	for (int i = 0; i < ncolors; i++)
	{
		if (!colors[i].hasValue)
			continue;

		Colorf c = colors[i].value;
		gammaCorrectColor(c);

		if (GLAD_ES_VERSION_3_0 || GLAD_VERSION_3_0)
		{
			const GLfloat carray[] = {c.r, c.g, c.b, c.a};
			glClearBufferfv(GL_COLOR, i, carray);
		}
		else
		{
			glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
			glClearColor(c.r, c.g, c.b, c.a);
			glClear(GL_COLOR_BUFFER_BIT);

			drawbuffersmodified = true;
		}
	}

	// Revert to the expected draw buffers once we're done, if glClearBuffer
	// wasn't supported.
	if (drawbuffersmodified)
	{
		GLenum bufs[MAX_COLOR_RENDER_TARGETS];

		for (int i = 0; i < ncolorRTs; i++)
			bufs[i] = GL_COLOR_ATTACHMENT0 + i;

		glDrawBuffers(ncolorRTs, bufs);
	}

	GLbitfield flags = 0;

	if (stencil.hasValue)
	{
		glClearStencil(stencil.value);
		flags |= GL_STENCIL_BUFFER_BIT;
	}

	bool hadDepthWrites = gl.hasDepthWrites();

	if (depth.hasValue)
	{
		if (!hadDepthWrites) // glDepthMask also affects glClear.
			gl.setDepthWrites(true);

		gl.clearDepth(depth.value);
		flags |= GL_DEPTH_BUFFER_BIT;
	}

	if (flags != 0)
		glClear(flags);

	if (depth.hasValue && !hadDepthWrites)
		gl.setDepthWrites(hadDepthWrites);

	if (gl.bugs.clearRequiresDriverTextureStateUpdate && Shader::current)
	{
		// This seems to be enough to fix the bug for me. Other methods I've
		// tried (e.g. dummy draws) don't work in all cases.
		gl.useProgram(0);
		gl.useProgram((GLuint) Shader::current->getHandle());
	}
}

void Graphics::discard(const std::vector<bool> &colorbuffers, bool depthstencil)
{
	flushBatchedDraws();
	discard(OpenGL::FRAMEBUFFER_ALL, colorbuffers, depthstencil);
}

void Graphics::discard(OpenGL::FramebufferTarget target, const std::vector<bool> &colorbuffers, bool depthstencil)
{
	if (!(GLAD_VERSION_4_3 || GLAD_ARB_invalidate_subdata || GLAD_ES_VERSION_3_0 || GLAD_EXT_discard_framebuffer))
		return;

	GLenum gltarget = GL_FRAMEBUFFER;
	if (target == OpenGL::FRAMEBUFFER_READ)
		gltarget = GL_READ_FRAMEBUFFER;
	else if (target == OpenGL::FRAMEBUFFER_DRAW)
		gltarget = GL_DRAW_FRAMEBUFFER;

	std::vector<GLenum> attachments;
	attachments.reserve(colorbuffers.size());

	// glDiscardFramebuffer uses different attachment enums for the default FBO.
	if (!isRenderTargetActive() && getInternalBackbufferFBO() == 0)
	{
		if (colorbuffers.size() > 0 && colorbuffers[0])
			attachments.push_back(GL_COLOR);

		if (depthstencil)
		{
			attachments.push_back(GL_STENCIL);
			attachments.push_back(GL_DEPTH);
		}
	}
	else
	{
		int rendertargetcount = std::max((int) states.back().renderTargets.colors.size(), 1);

		for (int i = 0; i < (int) colorbuffers.size(); i++)
		{
			if (colorbuffers[i] && i < rendertargetcount)
				attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
		}

		if (depthstencil)
		{
			attachments.push_back(GL_STENCIL_ATTACHMENT);
			attachments.push_back(GL_DEPTH_ATTACHMENT);
		}
	}

	// Hint for the driver that it doesn't need to save these buffers.
	if (GLAD_VERSION_4_3 || GLAD_ARB_invalidate_subdata || GLAD_ES_VERSION_3_0)
		glInvalidateFramebuffer(gltarget, (GLint) attachments.size(), &attachments[0]);
	else if (GLAD_EXT_discard_framebuffer)
		glDiscardFramebufferEXT(gltarget, (GLint) attachments.size(), &attachments[0]);
}

void Graphics::cleanupRenderTexture(love::graphics::Texture *texture)
{
	if (!texture->isRenderTarget())
		return;

	for (auto it = framebufferObjects.begin(); it != framebufferObjects.end(); /**/)
	{
		bool hastexture = false;
		const auto &rts = it->first;

		for (const RenderTarget &rt : rts.colors)
		{
			if (rt.texture == texture)
			{
				hastexture = true;
				break;
			}
		}

		hastexture = hastexture || rts.depthStencil.texture == texture;

		if (hastexture)
		{
			if (isCreated())
				gl.deleteFramebuffer(it->second);
			it = framebufferObjects.erase(it);
		}
		else
			++it;
	}
}

GLuint Graphics::bindCachedFBO(const RenderTargets &targets)
{
	GLuint fbo = framebufferObjects[targets];

	if (fbo != 0)
	{
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, fbo);
	}
	else
	{
		int msaa = targets.getFirstTarget().texture->getMSAA();
		bool hasDS = targets.depthStencil.texture != nullptr;

		glGenFramebuffers(1, &fbo);
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, fbo);

		int ncolortargets = 0;
		GLenum drawbuffers[MAX_COLOR_RENDER_TARGETS];

		auto attachRT = [&](const RenderTarget &rt)
		{
			bool renderbuffer = msaa > 1 || !rt.texture->isReadable();
			bool srgb = false;
			OpenGL::TextureFormat fmt = OpenGL::convertPixelFormat(rt.texture->getPixelFormat(), renderbuffer, srgb);

			if (fmt.framebufferAttachments[0] == GL_COLOR_ATTACHMENT0)
			{
				fmt.framebufferAttachments[0] = GL_COLOR_ATTACHMENT0 + ncolortargets;
				drawbuffers[ncolortargets] = fmt.framebufferAttachments[0];
				ncolortargets++;
			}

			GLuint handle = (GLuint) rt.texture->getRenderTargetHandle();

			for (GLenum attachment : fmt.framebufferAttachments)
			{
				if (attachment == GL_NONE)
					continue;
				else if (renderbuffer)
					glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, handle);
				else
				{
					TextureType textype = rt.texture->getTextureType();

					int layer = textype == TEXTURE_CUBE ? 0 : rt.slice;
					int face = textype == TEXTURE_CUBE ? rt.slice : 0;
					int level = rt.mipmap;

					gl.framebufferTexture(attachment, textype, handle, level, layer, face);
				}
			}
		};

		for (const auto &rt : targets.colors)
			attachRT(rt);

		if (hasDS)
			attachRT(targets.depthStencil);

		if (ncolortargets > 1)
			glDrawBuffers(ncolortargets, drawbuffers);
		else if (ncolortargets == 0 && hasDS && (GLAD_ES_VERSION_3_0 || !GLAD_ES_VERSION_2_0))
		{
			// glDrawBuffers is an ext in GL2. glDrawBuffer doesn't exist in ES3.
			GLenum none = GL_NONE;
			if (GLAD_ES_VERSION_3_0)
				glDrawBuffers(1, &none);
			else
				glDrawBuffer(GL_NONE);
			glReadBuffer(GL_NONE);
		}

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			gl.deleteFramebuffer(fbo);
			const char *sstr = OpenGL::framebufferStatusString(status);
			throw love::Exception("Could not create Framebuffer Object! %s", sstr);
		}

		framebufferObjects[targets] = fbo;
	}

	return fbo;
}

void Graphics::present(void *screenshotCallbackData)
{
	if (!isActive())
		return;

	if (isRenderTargetActive())
		throw love::Exception("present cannot be called while a render target is active.");

	deprecations.draw(this);

	flushBatchedDraws();
	endPass();

	int w = getPixelWidth();
	int h = getPixelHeight();

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, getInternalBackbufferFBO());

	// Copy internal backbuffer to system backbuffer. When MSAA is used this
	// is a direct MSAA resolve.
	if (internalBackbuffer.get())
	{
		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_DRAW, getSystemBackbufferFBO());

		// Discard system backbuffer to prevent it from copying its contents
		// from VRAM to chip memory.
		discard(OpenGL::FRAMEBUFFER_DRAW, {true}, true);

		// updateBackbuffer checks for glBlitFramebuffer support.
		if (GLAD_APPLE_framebuffer_multisample && internalBackbuffer->getMSAA() > 1)
			glResolveMultisampleFramebufferAPPLE();
		else
			glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		// Discarding the internal backbuffer directly after resolving it should
		// eliminate any copy back to vram it might need to do.
		discard(OpenGL::FRAMEBUFFER_READ, {true}, false);
	}

	if (!pendingScreenshotCallbacks.empty())
	{
		size_t row = 4 * w;
		size_t size = row * h;

		GLubyte *pixels = nullptr;
		GLubyte *screenshot = nullptr;

		try
		{
			pixels = new GLubyte[size];
			screenshot = new GLubyte[size];
		}
		catch (std::exception &)
		{
			delete[] pixels;
			delete[] screenshot;
			throw love::Exception("Out of memory.");
		}

		gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, getSystemBackbufferFBO());
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		// Replace alpha values with full opacity.
		for (size_t i = 3; i < size; i += 4)
			pixels[i] = 255;

		// OpenGL sucks and reads pixels from the lower-left. Let's fix that.
		GLubyte *src = pixels - row;
		GLubyte *dst = screenshot + size;

		for (int i = 0; i < h; ++i)
			memcpy(dst-=row, src+=row, row);

		delete[] pixels;

		auto imagemodule = Module::getInstance<love::image::Image>(M_IMAGE);

		for (int i = 0; i < (int) pendingScreenshotCallbacks.size(); i++)
		{
			const auto &info = pendingScreenshotCallbacks[i];
			image::ImageData *img = nullptr;

			try
			{
				img = imagemodule->newImageData(w, h, PIXELFORMAT_RGBA8_UNORM, screenshot);
			}
			catch (love::Exception &)
			{
				delete[] screenshot;
				info.callback(&info, nullptr, nullptr);
				for (int j = i + 1; j < (int) pendingScreenshotCallbacks.size(); j++)
				{
					const auto &ninfo = pendingScreenshotCallbacks[j];
					ninfo.callback(&ninfo, nullptr, nullptr);
				}
				pendingScreenshotCallbacks.clear();
				throw;
			}

			info.callback(&info, img, screenshotCallbackData);
			img->release();
		}

		delete[] screenshot;
		pendingScreenshotCallbacks.clear();
	}

#ifdef LOVE_IOS
	// Hack: SDL's color renderbuffer must be bound when swapBuffers is called.
	SDL_SysWMinfo info = {};
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(SDL_GL_GetCurrentWindow(), &info);
	glBindRenderbuffer(GL_RENDERBUFFER, info.info.uikit.colorbuffer);
#endif

	for (StreamBuffer *buffer : batchedDrawState.vb)
		buffer->nextFrame();
	batchedDrawState.indexBuffer->nextFrame();

	auto window = getInstance<love::window::Window>(M_WINDOW);
	if (window != nullptr)
		window->swapBuffers();

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, getInternalBackbufferFBO());

	// Reset the per-frame stat counts.
	drawCalls = 0;
	gl.stats.shaderSwitches = 0;
	renderTargetSwitchCount = 0;
	drawCallsBatched = 0;

	// This assumes temporary textures will only be used within a render pass.
	for (int i = (int) temporaryTextures.size() - 1; i >= 0; i--)
	{
		if (temporaryTextures[i].framesSinceUse >= MAX_TEMPORARY_TEXTURE_UNUSED_FRAMES)
		{
			temporaryTextures[i].texture->release();
			temporaryTextures[i] = temporaryTextures.back();
			temporaryTextures.pop_back();
		}
		else
			temporaryTextures[i].framesSinceUse++;
	}
}

int Graphics::getRequestedBackbufferMSAA() const
{
	return requestedBackbufferMSAA;
}

int Graphics::getBackbufferMSAA() const
{
	return internalBackbuffer.get() ? internalBackbuffer->getMSAA() : 0;
}

void Graphics::setScissor(const Rect &rect)
{
	flushBatchedDraws();

	DisplayState &state = states.back();

	if (!gl.isStateEnabled(OpenGL::ENABLE_SCISSOR_TEST))
		gl.setEnableState(OpenGL::ENABLE_SCISSOR_TEST, true);

	double dpiscale = getCurrentDPIScale();

	Rect glrect;
	glrect.x = (int) (rect.x * dpiscale);
	glrect.y = (int) (rect.y * dpiscale);
	glrect.w = (int) (rect.w * dpiscale);
	glrect.h = (int) (rect.h * dpiscale);

	// OpenGL's reversed y-coordinate is compensated for in OpenGL::setScissor.
	gl.setScissor(glrect, isRenderTargetActive());

	state.scissor = true;
	state.scissorRect = rect;
}

void Graphics::setScissor()
{
	if (states.back().scissor)
		flushBatchedDraws();

	states.back().scissor = false;

	if (gl.isStateEnabled(OpenGL::ENABLE_SCISSOR_TEST))
		gl.setEnableState(OpenGL::ENABLE_SCISSOR_TEST, false);
}

void Graphics::drawToStencilBuffer(StencilAction action, int value)
{
	const auto &rts = states.back().renderTargets;
	love::graphics::Texture *dstexture = rts.depthStencil.texture.get();

	if (!isRenderTargetActive() && !windowHasStencil)
		throw love::Exception("The window must have stenciling enabled to draw to the main screen's stencil buffer.");
	else if (isRenderTargetActive() && (rts.temporaryRTFlags & TEMPORARY_RT_STENCIL) == 0 && (dstexture == nullptr || !isPixelFormatStencil(dstexture->getPixelFormat())))
		throw love::Exception("Drawing to the stencil buffer with a render target active requires either stencil=true or a custom stencil-type texture to be used, in setRenderTarget.");

	flushBatchedDraws();

	writingToStencil = true;

	// Disable color writes but don't save the state for it.
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	GLenum glaction = GL_REPLACE;

	switch (action)
	{
	case STENCIL_REPLACE:
	default:
		glaction = GL_REPLACE;
		break;
	case STENCIL_INCREMENT:
		glaction = GL_INCR;
		break;
	case STENCIL_DECREMENT:
		glaction = GL_DECR;
		break;
	case STENCIL_INCREMENT_WRAP:
		glaction = GL_INCR_WRAP;
		break;
	case STENCIL_DECREMENT_WRAP:
		glaction = GL_DECR_WRAP;
		break;
	case STENCIL_INVERT:
		glaction = GL_INVERT;
		break;
	}

	// The stencil test must be enabled in order to write to the stencil buffer.
	if (!gl.isStateEnabled(OpenGL::ENABLE_STENCIL_TEST))
		gl.setEnableState(OpenGL::ENABLE_STENCIL_TEST, true);

	glStencilFunc(GL_ALWAYS, value, 0xFFFFFFFF);
	glStencilOp(GL_KEEP, GL_KEEP, glaction);
}

void Graphics::stopDrawToStencilBuffer()
{
	if (!writingToStencil)
		return;

	flushBatchedDraws();

	writingToStencil = false;

	const DisplayState &state = states.back();

	// Revert the color write mask.
	setColorMask(state.colorMask);

	// Use the user-set stencil test state when writes are disabled.
	setStencilTest(state.stencilCompare, state.stencilTestValue);
}

void Graphics::setStencilTest(CompareMode compare, int value)
{
	DisplayState &state = states.back();

	if (state.stencilCompare != compare || state.stencilTestValue != value)
		flushBatchedDraws();

	state.stencilCompare = compare;
	state.stencilTestValue = value;

	if (writingToStencil)
		return;

	if (compare == COMPARE_ALWAYS)
	{
		if (gl.isStateEnabled(OpenGL::ENABLE_STENCIL_TEST))
			gl.setEnableState(OpenGL::ENABLE_STENCIL_TEST, false);
		return;
	}

	/**
	 * OpenGL / GPUs do the comparison in the opposite way that makes sense
	 * for this API. For example, if the compare function is GL_GREATER then the
	 * stencil test will pass if the reference value is greater than the value
	 * in the stencil buffer. With our API it's more intuitive to assume that
	 * setStencilTest(COMPARE_GREATER, 4) will make it pass if the stencil
	 * buffer has a value greater than 4.
	 **/
	GLenum glcompare = OpenGL::getGLCompareMode(getReversedCompareMode(compare));

	if (!gl.isStateEnabled(OpenGL::ENABLE_STENCIL_TEST))
		gl.setEnableState(OpenGL::ENABLE_STENCIL_TEST, true);

	glStencilFunc(glcompare, value, 0xFFFFFFFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void Graphics::setDepthMode(CompareMode compare, bool write)
{
	DisplayState &state = states.back();

	if (state.depthTest != compare || state.depthWrite != write)
		flushBatchedDraws();

	state.depthTest = compare;
	state.depthWrite = write;

	bool depthenable = compare != COMPARE_ALWAYS || write;

	if (depthenable != gl.isStateEnabled(OpenGL::ENABLE_DEPTH_TEST))
		gl.setEnableState(OpenGL::ENABLE_DEPTH_TEST, depthenable);

	if (depthenable)
	{
		glDepthFunc(OpenGL::getGLCompareMode(compare));
		gl.setDepthWrites(write);
	}
}

void Graphics::setFrontFaceWinding(Winding winding)
{
	DisplayState &state = states.back();

	if (state.winding != winding)
		flushBatchedDraws();

	state.winding = winding;

	if (isRenderTargetActive())
		winding = winding == WINDING_CW ? WINDING_CCW : WINDING_CW;

	glFrontFace(winding == WINDING_CW ? GL_CW : GL_CCW);
}

void Graphics::setColor(Colorf c)
{
	c.r = std::min(std::max(c.r, 0.0f), 1.0f);
	c.g = std::min(std::max(c.g, 0.0f), 1.0f);
	c.b = std::min(std::max(c.b, 0.0f), 1.0f);
	c.a = std::min(std::max(c.a, 0.0f), 1.0f);

	states.back().color = c;
}

void Graphics::setColorMask(ColorChannelMask mask)
{
	flushBatchedDraws();

	glColorMask(mask.r, mask.g, mask.b, mask.a);
	states.back().colorMask = mask;
}

void Graphics::setBlendState(const BlendState &blend)
{
	if (!(blend == states.back().blend))
		flushBatchedDraws();

	if (blend.operationRGB == BLENDOP_MAX || blend.operationA == BLENDOP_MAX
		|| blend.operationRGB == BLENDOP_MIN || blend.operationA == BLENDOP_MIN)
	{
		if (!capabilities.features[FEATURE_BLEND_MINMAX])
			throw love::Exception("The 'min' and 'max' blend operations are not supported on this system.");
	}

	if (blend.enable != gl.isStateEnabled(OpenGL::ENABLE_BLEND))
		gl.setEnableState(OpenGL::ENABLE_BLEND, blend.enable);

	if (blend.enable)
	{
		GLenum opRGB  = getGLBlendOperation(blend.operationRGB);
		GLenum opA    = getGLBlendOperation(blend.operationA);
		GLenum srcRGB = getGLBlendFactor(blend.srcFactorRGB);
		GLenum srcA   = getGLBlendFactor(blend.srcFactorA);
		GLenum dstRGB = getGLBlendFactor(blend.dstFactorRGB);
		GLenum dstA   = getGLBlendFactor(blend.dstFactorA);

		glBlendEquationSeparate(opRGB, opA);
		glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
	}

	states.back().blend = blend;
}

void Graphics::setPointSize(float size)
{
	if (size != states.back().pointSize)
		flushBatchedDraws();

	states.back().pointSize = size;
}

void Graphics::setWireframe(bool enable)
{
	// Not supported in OpenGL ES.
	if (GLAD_ES_VERSION_2_0)
		return;

	flushBatchedDraws();

	glPolygonMode(GL_FRONT_AND_BACK, enable ? GL_LINE : GL_FILL);
	states.back().wireframe = enable;
}

void *Graphics::getBufferMapMemory(size_t size)
{
	// We don't need anything more complicated because get/release calls are
	// never interleaved (as of when this comment was written.)
	if (bufferMapMemory == nullptr || size > bufferMapMemorySize)
		return malloc(size);
	return bufferMapMemory;
}

void Graphics::releaseBufferMapMemory(void *mem)
{
	if (mem != bufferMapMemory)
		free(mem);
}

Graphics::Renderer Graphics::getRenderer() const
{
	return GLAD_ES_VERSION_2_0 ? RENDERER_OPENGLES : RENDERER_OPENGL;
}

Graphics::RendererInfo Graphics::getRendererInfo() const
{
	RendererInfo info;

	if (GLAD_ES_VERSION_2_0)
		info.name = "OpenGL ES";
	else
		info.name = "OpenGL";

	const char *str = (const char *) glGetString(GL_VERSION);
	if (str)
		info.version = str;
	else
		throw love::Exception("Cannot retrieve renderer version information.");

	str = (const char *) glGetString(GL_VENDOR);
	if (str)
		info.vendor = str;
	else
		throw love::Exception("Cannot retrieve renderer vendor information.");

	str = (const char *) glGetString(GL_RENDERER);
	if (str)
		info.device = str;
	else
		throw love::Exception("Cannot retrieve renderer device information.");

	return info;
}

void Graphics::getAPIStats(int &shaderswitches) const
{
	shaderswitches = gl.stats.shaderSwitches;
}

void Graphics::initCapabilities()
{
	capabilities.features[FEATURE_MULTI_RENDER_TARGET_FORMATS] = gl.isMultiFormatMRTSupported();
	capabilities.features[FEATURE_CLAMP_ZERO] = gl.isClampZeroOneTextureWrapSupported();
	capabilities.features[FEATURE_BLEND_MINMAX] = GLAD_VERSION_1_4 || GLAD_ES_VERSION_3_0 || GLAD_EXT_blend_minmax;
	capabilities.features[FEATURE_LIGHTEN] = capabilities.features[FEATURE_BLEND_MINMAX];
	capabilities.features[FEATURE_FULL_NPOT] = GLAD_VERSION_2_0 || GLAD_ES_VERSION_3_0 || GLAD_OES_texture_npot;
	capabilities.features[FEATURE_PIXEL_SHADER_HIGHP] = gl.isPixelShaderHighpSupported();
	capabilities.features[FEATURE_SHADER_DERIVATIVES] = GLAD_VERSION_2_0 || GLAD_ES_VERSION_3_0 || GLAD_OES_standard_derivatives;
	capabilities.features[FEATURE_GLSL3] = GLAD_ES_VERSION_3_0 || gl.isCoreProfile();
	capabilities.features[FEATURE_GLSL4] = GLAD_ES_VERSION_3_1 || (gl.isCoreProfile() && GLAD_VERSION_4_3);
	capabilities.features[FEATURE_INSTANCING] = gl.isInstancingSupported();
	capabilities.features[FEATURE_TEXEL_BUFFER] = gl.isBufferUsageSupported(BUFFERUSAGE_TEXEL);
	capabilities.features[FEATURE_COPY_BUFFER] = gl.isBufferUsageSupported(BUFFERUSAGE_COPY_SOURCE);
	static_assert(FEATURE_MAX_ENUM == 12, "Graphics::initCapabilities must be updated when adding a new graphics feature!");

	capabilities.limits[LIMIT_POINT_SIZE] = gl.getMaxPointSize();
	capabilities.limits[LIMIT_TEXTURE_SIZE] = gl.getMax2DTextureSize();
	capabilities.limits[LIMIT_TEXTURE_LAYERS] = gl.getMaxTextureLayers();
	capabilities.limits[LIMIT_VOLUME_TEXTURE_SIZE] = gl.getMax3DTextureSize();
	capabilities.limits[LIMIT_CUBE_TEXTURE_SIZE] = gl.getMaxCubeTextureSize();
	capabilities.limits[LIMIT_TEXEL_BUFFER_SIZE] = gl.getMaxTexelBufferSize();
	capabilities.limits[LIMIT_SHADER_STORAGE_BUFFER_SIZE] = gl.getMaxShaderStorageBufferSize();
	capabilities.limits[LIMIT_THREADGROUPS_X] = gl.getMaxComputeWorkGroupsX();
	capabilities.limits[LIMIT_THREADGROUPS_Y] = gl.getMaxComputeWorkGroupsY();
	capabilities.limits[LIMIT_THREADGROUPS_Z] = gl.getMaxComputeWorkGroupsZ();
	capabilities.limits[LIMIT_RENDER_TARGETS] = gl.getMaxRenderTargets();
	capabilities.limits[LIMIT_TEXTURE_MSAA] = gl.getMaxSamples();
	capabilities.limits[LIMIT_ANISOTROPY] = gl.getMaxAnisotropy();
	static_assert(LIMIT_MAX_ENUM == 13, "Graphics::initCapabilities must be updated when adding a new system limit!");

	for (int i = 0; i < TEXTURE_MAX_ENUM; i++)
		capabilities.textureTypes[i] = gl.isTextureTypeSupported((TextureType) i);
}

PixelFormat Graphics::getSizedFormat(PixelFormat format, bool rendertarget, bool readable) const
{
	uint32 requiredflags = 0;
	if (rendertarget)
		requiredflags |= PIXELFORMATUSAGEFLAGS_RENDERTARGET;
	if (readable)
		requiredflags |= PIXELFORMATUSAGEFLAGS_SAMPLE;

	switch (format)
	{
	case PIXELFORMAT_NORMAL:
		if (isGammaCorrect())
			return PIXELFORMAT_sRGBA8_UNORM;
		else if ((OpenGL::getPixelFormatUsageFlags(PIXELFORMAT_RGBA8_UNORM) & requiredflags) != requiredflags)
			// 32-bit render targets don't have guaranteed support on GLES2.
			return PIXELFORMAT_RGBA4_UNORM;
		else
			return PIXELFORMAT_RGBA8_UNORM;
	case PIXELFORMAT_HDR:
		return PIXELFORMAT_RGBA16_FLOAT;
	default:
		return format;
	}
}

bool Graphics::isPixelFormatSupported(PixelFormat format, bool rendertarget, bool readable, bool sRGB)
{
	if (sRGB && format == PIXELFORMAT_RGBA8_UNORM)
	{
		format = getSRGBPixelFormat(format);
		sRGB = false;
	}

	uint32 requiredflags = 0;
	if (rendertarget)
		requiredflags |= PIXELFORMATUSAGEFLAGS_RENDERTARGET;
	if (readable)
		requiredflags |= PIXELFORMATUSAGEFLAGS_SAMPLE;

	format = getSizedFormat(format, rendertarget, readable);

	OptionalBool &supported = supportedFormats[format][rendertarget ? 1 : 0][readable ? 1 : 0][sRGB ? 1 : 0];

	if (supported.hasValue)
		return supported.value;

	auto supportedflags = OpenGL::getPixelFormatUsageFlags(format);

	if ((requiredflags & supportedflags) != requiredflags)
	{
		supported.set(false);
		return supported.value;
	}

	if (!rendertarget)
	{
		supported.set(true);
		return supported.value;
	}

	// Even though we might have the necessary OpenGL version or extension,
	// drivers are still allowed to throw FRAMEBUFFER_UNSUPPORTED when attaching
	// a texture to a FBO whose format the driver doesn't like. So we should
	// test with an actual FBO.
	GLuint texture = 0;
	GLuint renderbuffer = 0;

	// Avoid the test for depth/stencil formats - not every GL version
	// guarantees support for depth/stencil-only render targets (which we would
	// need for the test below to work), and we already do some finagling in
	// convertPixelFormat to try to use the best-supported internal
	// depth/stencil format for a particular driver.
	if (isPixelFormatDepthStencil(format))
	{
		supported.set(true);
		return true;
	}

	OpenGL::TextureFormat fmt = OpenGL::convertPixelFormat(format, !readable, sRGB);

	GLuint current_fbo = gl.getFramebuffer(OpenGL::FRAMEBUFFER_ALL);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, fbo);

	// Make sure at least something is bound to a color attachment. I believe
	// this is required on ES2 but I'm not positive.
	if (isPixelFormatDepthStencil(format))
		gl.framebufferTexture(GL_COLOR_ATTACHMENT0, TEXTURE_2D, gl.getDefaultTexture(TEXTURE_2D, DATA_BASETYPE_FLOAT), 0, 0, 0);

	if (readable)
	{
		glGenTextures(1, &texture);
		gl.bindTextureToUnit(TEXTURE_2D, texture, 0, false);

		SamplerState s;
		s.minFilter = s.magFilter = SamplerState::FILTER_NEAREST;
		gl.setSamplerState(TEXTURE_2D, s);

		gl.rawTexStorage(TEXTURE_2D, 1, format, sRGB, 1, 1);
	}
	else
	{
		glGenRenderbuffers(1, &renderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, fmt.internalformat, 1, 1);
	}

	for (GLenum attachment : fmt.framebufferAttachments)
	{
		if (attachment == GL_NONE)
			continue;

		if (readable)
			gl.framebufferTexture(attachment, TEXTURE_2D, texture, 0, 0, 0);
		else
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, renderbuffer);
	}

	supported.set(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

	gl.bindFramebuffer(OpenGL::FRAMEBUFFER_ALL, current_fbo);
	gl.deleteFramebuffer(fbo);

	if (texture != 0)
		gl.deleteTexture(texture);

	if (renderbuffer != 0)
		glDeleteRenderbuffers(1, &renderbuffer);

	return supported.value;
}

} // opengl
} // graphics
} // love
